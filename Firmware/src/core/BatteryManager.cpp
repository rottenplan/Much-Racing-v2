#include "BatteryManager.h"
#include "../config.h"

BatteryManager &BatteryManager::getInstance() {
  static BatteryManager instance;
  return instance;
}

BatteryManager::BatteryManager()
    : _voltage(0.0f), _percentage(0), _isCharging(false), _sampleIndex(0),
      _lastUpdate(0), _firstRead(true), _historyIndex(0), _lastTrendCheck(0) {
  for (int i = 0; i < SAMPLE_SIZE; i++) {
    _samples[i] = 0.0f;
  }
  for (int i = 0; i < HISTORY_SIZE; i++) {
    _voltageHistory[i] = 0.0f;
  }
}

void BatteryManager::begin() {
#ifdef PIN_BATTERY
  pinMode(PIN_BATTERY, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // Force multiple updates to fill buffer immediately
  _firstRead = true;
  for (int i = 0; i < SAMPLE_SIZE; i++) {
    update();
    delay(5);
  }
#endif
}

void BatteryManager::update() {
  // Debug to verify execution
  // Serial.println("BAT MGR UPDATE");

#ifdef PIN_BATTERY
  // Update every 500ms to avoid noise but keep it responsive
  if (!_firstRead && millis() - _lastUpdate < 500) {
    return;
  }
  _lastUpdate = millis();

  // Read ADC
  // ESP32 ADC is 12-bit (0-4095)
  // Default attenuation typically covers up to ~3.3V, but we check calibration
  // if needed. Assuming standard calibration for now.
  // Read ADC with I2C Mutex to prevent crosstalk/noise on ADC1
  uint32_t rawSum = 0;
  int validSamples = 0;
  if (i2cMutex != NULL &&
      xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    for (int i = 0; i < 8; i++) {
      rawSum += analogRead(PIN_BATTERY);
      delayMicroseconds(20);
    }
    xSemaphoreGive(i2cMutex);
    validSamples = 8;
  } else {
    // If mutex fails, do a single quick read anyway to prevent 0v readings
    rawSum = analogRead(PIN_BATTERY);
    validSamples = 1;
  }

  uint32_t raw = (validSamples > 0) ? (rawSum / validSamples) : 0;

  // Convert to Voltage
  float divider = 2.0f;
#ifdef BATTERY_DIVIDER_RATIO
  divider = BATTERY_DIVIDER_RATIO;
#endif

  float currentVoltage = (raw / 4095.0f) * 3.3f * divider;

  // Moving Average Smoothing
  _samples[_sampleIndex] = currentVoltage;
  _sampleIndex = (_sampleIndex + 1) % SAMPLE_SIZE;

  float sum = 0;
  for (int i = 0; i < SAMPLE_SIZE; i++) {
    sum += _samples[i];
  }

  // If first read, fill all with first value to avoid ramp up
  if (_firstRead) {
    for (int i = 0; i < SAMPLE_SIZE; i++)
      _samples[i] = currentVoltage;
    _voltage = currentVoltage;
    _firstRead = false;
  } else {
    _voltage = sum / SAMPLE_SIZE;
  }

  // 1. Instant Threshold (Full/Almost Full)
  // If voltage is high enough, we are likely plugged in.
  // Raised to 4.30V to avoid false positives on full LiPo
  bool chargingByThreshold = (_voltage > 4.30f);

  // 2. Trend Detection (Rising Voltage)
  // Check every 10 seconds
  if (millis() - _lastTrendCheck > 10000) {
    _lastTrendCheck = millis();

    // Store current average as a history point
    _voltageHistory[_historyIndex] = _voltage;
    _historyIndex = (_historyIndex + 1) % HISTORY_SIZE;

    bool validHistory = true;
    for (int i = 0; i < HISTORY_SIZE; i++) {
      if (_voltageHistory[i] < 2.0f)
        validHistory = false; // Ignore if uninitialized
    }

    if (validHistory) {
      // Find the "Oldest" snapshot in our circular buffer
      int oldestIndex = _historyIndex;
      float oldestVoltage = _voltageHistory[oldestIndex];

      // If we are rising: Current > Oldest + Delta
      // Delta: 0.02V over 60 seconds is a reasonable rise for charging
      if (_voltage > oldestVoltage + 0.02f) {
        _isCharging = true;
      }
      // If we are falling: Current < Oldest - Delta (Definitely Discharging)
      else if (_voltage < oldestVoltage - 0.01f) {
        _isCharging = false;
      } else {
        // Stable...
        if (!chargingByThreshold) {
          // If stable and not high voltage, assume discharging or disconnected
          _isCharging = false;
        } else {
          // If stable and high voltage, we are likely fully charged and still
          // plugged in
          _isCharging = true;
        }
      }
    }
  }

  // Combined logic: No longer using unconditional override at the end.
  // The trend/stability logic above handles it more accurately.

  // Percentage Calculation (Linear 3.0V - 4.2V)
  float minV = BATTERY_VOLTAGE_MIN; // 3.0
  float maxV = BATTERY_VOLTAGE_MAX; // 4.2

  if (_voltage >= maxV)
    _percentage = 100;
  else if (_voltage <= minV)
    _percentage = 0;
  else {
    _percentage = (int)((_voltage - minV) / (maxV - minV) * 100.0f);
  }

  // Debug output
  /*
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 2000) {
    lastPrint = millis();
    Serial.printf("[BAT] ADC:%d V:%.2f PCT:%d%% CHG:%d PIN:%d DIV:%.2f\n", raw,
                  _voltage, _percentage, _isCharging, PIN_BATTERY, divider);
  }
  */
#else
  _voltage = 0;
  _percentage = 0;
  _isCharging = false;
#endif
}

float BatteryManager::getVoltage() const { return _voltage; }

int BatteryManager::getPercentage() const { return _percentage; }

bool BatteryManager::isCharging() const { return _isCharging; }

bool BatteryManager::isUsbConnected() {
  // 1. If we are clearly charging (high voltage or rising trend)
  if (_isCharging)
    return true;

  // 2. If voltage is already near max (even if not rising), it's likely plugged
  // in
  if (_voltage > 4.25f)
    return true;

  return false;
}

void BatteryManager::checkDuringBoot() {
#ifdef PIN_BATTERY
  // Simple check for startup
  update();

  // If battery < 20% and NOT charging, we should not boot.
  // 20% linear is ~3.24V (assuming 3.0V-4.2V range)
  if (_percentage < 20 && !_isCharging) {
    Serial.println(F("CRITICAL: Battery low (<20%). Powering down."));

    // We need to show this on the screen before sleeping
    // Setup minimal UI or just use Serial if display not ready
    // But since we call this in main.cpp, we can let main display a warning
  }
#endif
}
