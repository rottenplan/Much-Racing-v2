#include "IMUManager.h"
#include "../config.h"
#include <Preferences.h>

IMUManager::IMUManager()
    : _mpu(nullptr), _isEnabled(true), _isConnected(false), _dataReady(false),
      _angleX(0), _angleY(0), _angleZ(0), _accX(0), _accY(0), _accZ(0),
      _rollOffset(0), _pitchOffset(0), _accXOffset(0), _accYOffset(0),
      _accZOffset(0), _consecutiveErrors(0), _lastUpdate(0) {}

void IMUManager::begin() {
  _isConnected = false;
  _dataReady = false;

  if (i2cMutex == nullptr) {
    DEBUG_PRINTLN(F("IMU: Mutex not ready, skipping"));
    return;
  }
  if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
    DEBUG_PRINTLN(F("IMU: Could not acquire I2C mutex"));
    return;
  }

  DEBUG_PRINTLN(F("IMU: Starting on shared bus (SDA=33, SCL=32) at 100kHz..."));

  // Ensure bus speed is safe for touch and IMU
  Wire.setClock(100000);
  delay(50);

  // Probe 0x68 (AD0=LOW, default)
  uint8_t foundAddr = 0;
  Wire.beginTransmission(0x68);
  byte err68 = Wire.endTransmission();
  DEBUG_PRINTF("IMU: Probe 0x68 result: %d\n", err68);
  if (err68 == 0)
    foundAddr = 0x68;

  // Probe 0x69 (AD0=HIGH)
  if (!foundAddr) {
    Wire.beginTransmission(0x69);
    byte err69 = Wire.endTransmission();
    DEBUG_PRINTF("IMU: Probe 0x69 result: %d\n", err69);
    if (err69 == 0)
      foundAddr = 0x69;
  }

  if (foundAddr) {
    _isConnected = true;
    DEBUG_PRINTF("IMU: Found at 0x%02X! Initializing MPU6050_light...\n",
                 foundAddr);

    if (_mpu != nullptr) {
      delete _mpu;
    }
    _mpu = new MPU6050(Wire);

    // MPU6050_light begin(gyro_config_num, acc_config_num)
    // 1 = 500 deg/s, 0 = 2g
    byte status = _mpu->begin(1, 0);

    DEBUG_PRINTF("IMU: Init status = %d\n", status);

    if (status == 0) {
      _dataReady = true;
      DEBUG_PRINTLN(F("IMU: MPU6050_light Ready!"));

      // Calc Offsets dynamically (internal gyro calibration holding still
      // briefly) _mpu->calcOffsets(true, true); // Optionally bypass this to
      // start faster and rely on our save

      // Load offsets
      Preferences prefs;
      prefs.begin("laptimer", true);
      _rollOffset = prefs.getFloat("imu_roll_off", 0.0);
      _pitchOffset = prefs.getFloat("imu_pitch_off", 0.0);
      _accXOffset = prefs.getFloat("imu_ax_off", 0.0);
      _accYOffset = prefs.getFloat("imu_ay_off", 0.0);
      _accZOffset = prefs.getFloat("imu_az_off", 0.0);
      _isEnabled = prefs.getBool("imu_enabled", true);
      prefs.end();

      DEBUG_PRINTLN(F("IMU: Software leveling offsets loaded. Hardware "
                      "offsets natively bypassed in light lib."));
    } else {
      DEBUG_PRINTF("IMU: Init FAILED (status=%d)\n", status);
      _isConnected = false;
    }
  } else {
    DEBUG_PRINTLN(F("IMU: Not found at 0x68 or 0x69 on SDA=33/SCL=32"));
  }

  xSemaphoreGive(i2cMutex);
}

void IMUManager::update() {
  static unsigned long lastStatusLog = 0;
  if (millis() - lastStatusLog > 10000) {
    if (!_isEnabled)
      DEBUG_PRINTLN(F("IMU: Disabled in settings"));
    else if (!_isConnected)
      DEBUG_PRINTLN(F("IMU: Not connected"));
    else if (!_dataReady)
      DEBUG_PRINTLN(F("IMU: DATA NOT READY"));
    lastStatusLog = millis();
  }

  if (!_isEnabled || !_isConnected || !_dataReady || _mpu == nullptr ||
      !_requestActive) {
    if (_isEnabled && (!_isConnected || !_dataReady)) {
      static unsigned long lastRetry = 0;
      if (millis() - lastRetry > 15000) {
        begin();
        lastRetry = millis();
      }
    }
    return;
  }

  if (millis() - _lastUpdate < 10)
    return;
  _lastUpdate = millis();

  if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(20)) == pdTRUE) {

    _mpu->update(); // Reads 14 bytes (Acc, Temp, Gyro), much safer than DMP 42
                    // bytes

    // Standard complementary filter results from MPU6050_light:
    float rawRoll = _mpu->getAngleX();
    float rawPitch = _mpu->getAngleY();
    float rawYaw = _mpu->getAngleZ();

    // Mapping untuk MPU yang dipasang "Berdiri" (Vertical Mount)
    // Secara default (Rebahan): Z adalah Atas/Bawah, Y adalah Depan/Belakang,
    // X Kanan/Kiri. Jika "Berdiri", maka umumnya sumbu Y dan Z bertukar peran.

    // Note: The user hasn't confirmed the exact symptom directions yet,
    // but typically a vertical mount swaps Pitch and Yaw.
    // For now, we apply the default vertical swap we had before as a baseline.
    // - Roll(X) stays Roll
    // - Pitch is now Yaw's axis
    // - Yaw is now Pitch's axis

    _angleX = rawRoll - _rollOffset; // Roll (Kiri/Kanan)
    _angleY =
        -(rawPitch - _pitchOffset); // Pitch (Depan/Belakang) - Inverted back
    _angleZ = rawYaw;               // Yaw (Putar)

    // Track Max Lean Angle (Point 6)
    // Positive roll = Right lean, Negative roll = Left lean
    if (_angleX > _maxLeanRight)
      _maxLeanRight = _angleX;
    if (_angleX < _maxLeanLeft)
      _maxLeanLeft = _angleX;

    // Mapping acceleration
    float rawAccX = _mpu->getAccX();
    float rawAccY = _mpu->getAccY();
    float rawAccZ = _mpu->getAccZ();

    // Swap Y and Z for acceleration as well to match angle verticality
    // THEN subtract offsets to zero it out (Software Leveling)
    _accX = rawAccX - _accXOffset;
    _accY = (-rawAccZ) - _accYOffset; // Y mengambil nilai sensor Z - Inverted
    _accZ = rawAccY - _accZOffset;    // Z mengambil nilai sensor Y

    // DEBUG:
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 500) {
      DEBUG_PRINTF("IMU Data: Roll=%.2f Pitch=%.2f Yaw=%.2f\n", _angleX,
                   _angleY, _angleZ);
      lastDebug = millis();
    }

    _consecutiveErrors = 0;
    xSemaphoreGive(i2cMutex);
  } else {
    _consecutiveErrors++;
    if (_consecutiveErrors > 50) {
      _dataReady = false;
      _isConnected = false;
      DEBUG_PRINTLN(F("IMU: Persistent I2C mutex starvation, resetting..."));
      _consecutiveErrors = 0;
    }
  }
}

void IMUManager::calibrate() {
  if (!_isConnected || _mpu == nullptr || i2cMutex == nullptr)
    return;

  if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
    DEBUG_PRINTLN(F("IMU: User requested Calibration. Averaging samples..."));

    float sumRoll = 0;
    float sumPitch = 0;
    float sumAccX = 0;
    float sumAccY = 0;
    float sumAccZ = 0;
    int samples = 20;

    // Average samples for stability
    for (int i = 0; i < samples; i++) {
      _mpu->update();
      sumRoll += _mpu->getAngleX();
      sumPitch += _mpu->getAngleY();

      // Capture what the logical axes are reading RIGHT NOW
      sumAccX += _mpu->getAccX();
      sumAccY += -_mpu->getAccZ(); // Matches logical Y mapping
      sumAccZ += _mpu->getAccY();  // Matches logical Z mapping

      delay(10);
    }

    _rollOffset = sumRoll / samples;
    _pitchOffset = sumPitch / samples;
    _accXOffset = sumAccX / samples;
    _accYOffset = sumAccY / samples;
    _accZOffset = sumAccZ / samples;

    Preferences prefs;
    prefs.begin("laptimer", false);
    prefs.putFloat("imu_roll_off", _rollOffset);
    prefs.putFloat("imu_pitch_off", _pitchOffset);
    prefs.putFloat("imu_ax_off", _accXOffset);
    prefs.putFloat("imu_ay_off", _accYOffset);
    prefs.putFloat("imu_az_off", _accZOffset);

    // Clear any leftover PROPRIETARY hardware offsets (MPU internal)
    // These are separate from our software offsets
    prefs.remove("mpu_ax_off");
    prefs.remove("mpu_ay_off");
    prefs.remove("mpu_az_off");
    prefs.remove("mpu_gx_off");
    prefs.remove("mpu_gy_off");
    prefs.remove("mpu_gz_off");

    prefs.end();
    DEBUG_PRINTF("IMU: Calibrated! Offsets -> R:%.2f P:%.2f AX:%.2f AY:%.2f\n",
                 _rollOffset, _pitchOffset, _accXOffset, _accYOffset);
    xSemaphoreGive(i2cMutex);
  }
}

void IMUManager::calibrateLevel() {
  calibrate(); // Redirect to the consolidated function
}

void IMUManager::saveSettings() {
  Preferences prefs;
  prefs.begin("laptimer", false);
  prefs.putBool("imu_enabled", _isEnabled);
  prefs.putFloat("imu_roll_off", _rollOffset);
  prefs.putFloat("imu_pitch_off", _pitchOffset);
  prefs.putFloat("imu_ax_off", _accXOffset);
  prefs.putFloat("imu_ay_off", _accYOffset);
  prefs.putFloat("imu_az_off", _accZOffset);
  prefs.end();
}

IMUManager imuManager;
