#include "SettingsScreen.h"
#include "../../config.h"
#include "../../core/GPSManager.h"
#include "../../core/IMUManager.h"
#include "../../core/SessionManager.h"
#include "../../core/SyncManager.h"
#include "../../core/WiFiManager.h"
#include "../fonts/Org_01.h"
#include "../fonts/Picopixel.h"
#include "SplashScreenAssets.h"
#include "TimeSettingScreen.h"

extern SessionManager sessionManager;
extern WiFiManager wifiManager;
extern SyncManager syncManager;
extern IMUManager imuManager;

// REMOVED static_tft and sdProgressCallback as they moved to SdTestScreen

SettingsScreen::ScreenMode SettingsScreen::startMode =
    SettingsScreen::MODE_NONE; // Default to Resume

void SettingsScreen::onShow() {
  _isUppercase = true;

  if (startMode != MODE_NONE) {
    _currentMode = startMode; // Use specified mode
    startMode = MODE_NONE;    // Reset for next time
  }
  // Else: Keep previous _currentMode (Resume)

  loadSettings(); // Reload to ensure sync

  TFT_eSPI *tft = _ui->getTft();
  // Clear content area only (preserve Status Bar)
  tft->fillRect(0, 25, SCREEN_WIDTH, SCREEN_HEIGHT - 25,
                _ui->getBackgroundColor());
  drawList(0, true);
}

void SettingsScreen::onHide() { imuManager.requestActivity(false); }

void SettingsScreen::loadSettings() {
  // Ensure any pending changes from the PREVIOUS menu are saved before clearing
  if (_needsSave) {
    commitSettings();
  }

  _settings.clear();

  if (_currentMode == MODE_MAIN) {
    // _settings.push_back({"CLOCK SETTING", TYPE_ACTION}); // Removed per
    // request

    _prefs.begin("laptimer", false);

    // Power Save (Auto Off)
    SettingItem powerSave = {"POWER SAVE", TYPE_VALUE, "power_save"};
    powerSave.options = {"1 min", "5 min", "10 min", "30 min", "Never"};
    powerSave.currentOptionIdx =
        _prefs.getInt("power_save", 1); // Default 5 min
    _settings.push_back(powerSave);

    // Theme (Dark/Light) - REMOVED per user request
    // SettingItem theme = {"THEME", TYPE_TOGGLE, "theme"};
    // theme.checkState = _ui->isDarkMode();
    // _settings.push_back(theme);

    // Brightness
    SettingItem brightness = {"BRIGHTNESS", TYPE_VALUE, "brightness"};
    brightness.options = {"10%", "20%", "30%", "40%", "50%",
                          "60%", "70%", "80%", "90%", "100%"};
    brightness.currentOptionIdx =
        _prefs.getInt("brightness", 9); // Default 100%
    _settings.push_back(brightness);

    _prefs.end();

    // GPS Status Sub-menu REMOVED
    // _settings.push_back({"GPS STATUS", TYPE_ACTION});
    // _settings.push_back({"GNSS LOG", TYPE_ACTION}); // Moved to GPS Status

    // Double Tap
    _settings.push_back({"GNSS FINE TUNING", TYPE_ACTION});

    // Utility Sub-menu (New)
    _settings.push_back({"UTILITY", TYPE_ACTION});

    // Vehicle & Sensor Settings Sub-menu (Consolidated)
    _settings.push_back({"VEHICLE & SENSORS", TYPE_ACTION});

    // Drag Settings (New per user request)
    _settings.push_back({"DRAG SETTINGS", TYPE_ACTION});

    // Lap Timer Settings (New)
    _settings.push_back({"LAP TIMER SETTINGS", TYPE_ACTION});

    // Connection Setup Sub-menu
    _settings.push_back({"CONNECTION SETUP", TYPE_ACTION});

    // About Device
    _settings.push_back({"ABOUT DEVICE", TYPE_ACTION});
  } else if (_currentMode == MODE_WIFI_MENU) {
    _prefs.begin("laptimer", false);

    // Offline Server (Replaces Toggle)
    _settings.push_back({"OFFLINE SERVER", TYPE_ACTION});

    // Connection Setup (Client Mode)
    _settings.push_back({"WIFI CONFIG", TYPE_ACTION});

    // Hotspot Toggle
    extern WiFiManager wifiManager;
    SettingItem hotspot = {"WIFI HOTSPOT", TYPE_TOGGLE, "wifi_ap_enabled"};
    hotspot.checkState = wifiManager.isApEnabled();
    _settings.push_back(hotspot);

    // Live Telemetry Toggle
    SettingItem liveTel = {"LIVE TELEMETRY", TYPE_TOGGLE, "live_tel_en"};
    liveTel.checkState = wifiManager.isLiveTelemetryEnabled();
    _settings.push_back(liveTel);

    _prefs.end();
  } else if (_currentMode == MODE_ENGINE) {
    _prefs.begin("laptimer", false);

    // 1. Pulses Per Revolution (PPR)
    SettingItem ppr = {"PULSE PER REV", TYPE_VALUE, "rpm_ppr"};
    ppr.options = {"1.0 (2T/4T Wasted)", "0.5 (1p/2r)", "2.0 (2T Twin)",
                   "3.0 (3p/1r)", "4.0 (4p/1r)"};
    ppr.currentOptionIdx = _prefs.getInt("rpm_ppr", 0);
    if (ppr.currentOptionIdx < 0 || ppr.currentOptionIdx >= ppr.options.size())
      ppr.currentOptionIdx = 0;
    _settings.push_back(ppr);

    // 2. RPM Sensor Toggle
    extern GPSManager gpsManager;
    SettingItem rpmOnOff = {"RPM SENSOR", TYPE_TOGGLE, "rpm_enabled"};
    rpmOnOff.checkState = gpsManager.isRpmEnabled();
    _settings.push_back(rpmOnOff);

    // 4. Units
    SettingItem units = {"UNITS", TYPE_VALUE, "units"};
    units.options = {"Metric (km/h)", "Imperial (mph)"};
    units.currentOptionIdx = _prefs.getInt("units", 0);
    _settings.push_back(units);
    // 5. G-Force Calibration (Moved from MODE_RPM)
    _settings.push_back({"G-FORCE CALIBRATION", TYPE_ACTION});

    _prefs.end();
  } else if (_currentMode == MODE_RPM) {
    // Redundant MODE_RPM removed - consolidated into MODE_ENGINE
    _currentMode = MODE_ENGINE;
    loadSettings();
    return;
  } else if (_currentMode == MODE_GNSS_CONFIG) {
    _prefs.begin("laptimer", false);

    // 1. GNSS Mode
    SettingItem mode = {"GNSS MODE", TYPE_VALUE, "gnss_mode"};
    mode.options = {
        "All (10Hz)",         // 0
        "GPS+GLO+SBAS(16Hz)", // 1
        "GPS+GAL+GLO(10Hz)",  // 2
        "GPS+GAL+SBAS(20Hz)", // 3
        "GPS+SBAS (25Hz)",    // 4
        "GPS Only (25Hz)",    // 5
        "GPS+BEI+SBAS(12Hz)", // 6
        "GPS+GLO (16Hz)"      // 7
    };
    mode.currentOptionIdx = _prefs.getInt("gnss_mode", 1);
    _settings.push_back(mode);

    // 2. GNSS Coordinate Projection
    SettingItem proj = {"COORD PROJECTION", TYPE_VALUE, "gnss_proj"};
    proj.options = {"No Projection", "Projection (Def)"};
    // Map bool to index: true -> 1, false -> 0
    // But we need to check how we stored it? We stored as bool.
    // Let's rely on internal state or load fresh.
    bool projState = _prefs.getBool("gnss_proj", true);
    proj.currentOptionIdx = projState ? 1 : 0;
    _settings.push_back(proj);

    // 3. Frequency Limit
    SettingItem freq = {"FREQUENCY LIMIT", TYPE_VALUE, "gnss_freq_limit"};
    freq.options = {"10 Hz (Min)", "18 Hz (Max)", "25 Hz (Ult)"}; // Added 25Hz
    freq.currentOptionIdx =
        _prefs.getInt("gnss_freq_limit", 0); // Default 10Hz (Index 0)
    _settings.push_back(freq);

    // 4. Dynamic Model
    SettingItem dyn = {"DYNAMIC MODEL", TYPE_VALUE, "gnss_model"};
    dyn.options = {
        "Portable",         // 0
        "Stationary",       // 1
        "Pedestrian",       // 2
        "Automotive (Def)", // 3
        "At Sea",           // 4
        "Airborne <1g",     // 5
        "Airborne <2g",     // 6
        "Airborne <4g"      // 7
    };
    dyn.currentOptionIdx = _prefs.getInt("gnss_model", 3);
    _settings.push_back(dyn);

    // 5. SBAS Config
    SettingItem sbas = {"SBAS SYSTEM", TYPE_VALUE, "gnss_sbas"};
    sbas.options = {
        "JAPAN (MSAS)",   // 0
        "INDIA (GAGAN)",  // 1
        "AUS (SouthPAN)", // 2
        "CHINA (BDSBAS)", // 3
        "KOREA (KASS)",   // 4
        "DISABLE"         // 5
    };
    sbas.currentOptionIdx = _prefs.getInt("gnss_sbas", 0); // Default 0 (MSAS)
    _settings.push_back(sbas);

    // 6. GNSS RX PIN
    SettingItem rxPin = {"GNSS RX PIN", TYPE_VALUE, "gps_rx_pin"};
    // Options: 22(Def), 21
    rxPin.options = {"22 (Def)", "21"};

    extern GPSManager gpsManager;
    int curRx = gpsManager.getRxPin();
    rxPin.currentOptionIdx = 0; // Default 22
    int validRxCheck[] = {22, 21};
    for (int i = 0; i < 2; i++) {
      if (validRxCheck[i] == curRx) {
        rxPin.currentOptionIdx = i;
        break;
      }
    }
    _settings.push_back(rxPin);

    // 7. GNSS TX PIN
    SettingItem txPin = {"GNSS TX PIN", TYPE_VALUE, "gps_tx_pin"};
    // Options: 21(Def), 22
    txPin.options = {"21 (Def)", "22"};
    int curTx = gpsManager.getTxPin();
    txPin.currentOptionIdx = 0; // Default 21
    int validTxCheck[] = {21, 22};
    for (int i = 0; i < 2; i++) {
      if (validTxCheck[i] == curTx) {
        txPin.currentOptionIdx = i;
        break;
      }
    }
    _settings.push_back(txPin);

    // 8. GNSS BAUD RATE
    SettingItem baud = {"GNSS BAUD RATE", TYPE_VALUE, "gps_baud"};
    baud.options = {"9600 bps", "19200 bps", "38400 bps", "57600 bps",
                    "115200 bps"};
    int curBaud = gpsManager.getBaud();
    baud.currentOptionIdx = 0; // Default 9600
    int validBauds[] = {9600, 19200, 38400, 57600, 115200};
    for (int i = 0; i < 5; i++) {
      if (validBauds[i] == curBaud) {
        baud.currentOptionIdx = i;
        break;
      }
    }
    _settings.push_back(baud);

    _settings.push_back({"RESET GPS", TYPE_ACTION});

    _prefs.end();
  } else if (_currentMode == MODE_UTILITY) {
    _prefs.begin("laptimer", false);

    // SD Card Test (Moved here)
    _settings.push_back({"SD CARD TEST", TYPE_ACTION});

    // TFT Benchmark (Standard)
    _settings.push_back({"TFT BENCHMARK", TYPE_ACTION});

    // Debug Touch Toggle (Moved from Main)
    SettingItem debugTouch = {"DEBUG TOUCH", TYPE_TOGGLE, "debug_touch"};
    debugTouch.checkState = _prefs.getBool("debug_touch", false);
    _settings.push_back(debugTouch);

    _prefs.end();
  } else if (_currentMode == MODE_IMU) {
    // 0. IMU Sensor Toggle
    SettingItem imuOnOff = {"IMU SENSOR", TYPE_TOGGLE, "imu_enabled"};
    imuOnOff.checkState = _prefs.getBool("imu_enabled", true);
    _settings.push_back(imuOnOff);

    // 1. Calibrate Level (Action)
    _settings.push_back({"CALIBRATE LEVEL", TYPE_ACTION});

    // 2. Manual Roll Offset
    SettingItem rollOff = {"ROLL OFFSET", TYPE_VALUE, "imu_roll_off"};
    // Options: -10 to +10 in 0.5 steps? Or simpler -5 to +5
    for (float f = -5.0; f <= 5.1; f += 0.5) {
      rollOff.options.push_back(String(f, 1) + "*");
    }
    float curRollOff = _prefs.getFloat("imu_roll_off", 0.0);
    // Find closest index
    rollOff.currentOptionIdx =
        10; // Default 0.0 is at index 10 ( -5 + 0.5*10 = 0)
    for (int i = 0; i < rollOff.options.size(); i++) {
      if (abs(rollOff.options[i].toFloat() - curRollOff) < 0.1) {
        rollOff.currentOptionIdx = i;
        break;
      }
    }
    _settings.push_back(rollOff);

    // 3. Manual Pitch Offset
    SettingItem pitchOff = {"PITCH OFFSET", TYPE_VALUE, "imu_pitch_off"};
    for (float f = -5.0; f <= 5.1; f += 0.5) {
      pitchOff.options.push_back(String(f, 1) + "*");
    }
    float curPitchOff = _prefs.getFloat("imu_pitch_off", 0.0);
    pitchOff.currentOptionIdx = 10;
    for (int i = 0; i < pitchOff.options.size(); i++) {
      if (abs(pitchOff.options[i].toFloat() - curPitchOff) < 0.1) {
        pitchOff.currentOptionIdx = i;
        break;
      }
    }
    _settings.push_back(pitchOff);

    _prefs.end();
  } else if (_currentMode == MODE_DRAG) {
    _prefs.begin("laptimer", false);

    // Christmas Tree Duration
    SettingItem tree = {"CHRISTMAS TREE", TYPE_VALUE, "drag_tree_sec"};
    tree.options = {"3 seconds", "5 seconds", "7 seconds"};
    tree.currentOptionIdx = _prefs.getInt("drag_tree_sec", 1); // Default 5s
    _settings.push_back(tree);

    _prefs.end();
  } else if (_currentMode == MODE_LAP_CONFIG) {
    _prefs.begin("laptimer", false);

    // Beacon Width
    SettingItem beacon = {"BEACON WIDTH", TYPE_VALUE, "beacon_width"};
    // Options: 20m to 100m in 10m steps. Default 50m.
    // Index 0=20m, 1=30m, 2=40m, 3=50m, ...
    for (int w = 20; w <= 100; w += 10) {
      beacon.options.push_back(String(w) + " m");
    }
    int curWidth = _prefs.getInt("beacon_width", DEFAULT_BEACON_WIDTH);
    // Calc index: (val - 20) / 10
    int idx = (curWidth - 20) / 10;
    if (idx < 0)
      idx = 3; // Default 50m (index 3)
    if (idx >= beacon.options.size())
      idx = 3;
    beacon.currentOptionIdx = idx;
    _settings.push_back(beacon);

    _prefs.end();
  }
}

void SettingsScreen::saveSetting(int idx) {
  if (idx < 0 || idx >= (int)_settings.size())
    return;

  SettingItem &item = _settings[idx];

  // --- Apply Immediate Hardware Effects ---
  if (item.type == TYPE_VALUE) {
    if (item.key == "brightness") {
      int duty = map(item.currentOptionIdx, 0, 9, 26, 255);
      _ui->setBrightness(duty);
    }

    if (item.key == "power_save") {
      unsigned long ms = 0;
      switch (item.currentOptionIdx) {
      case 0:
        ms = 60000;
        break;
      case 1:
        ms = 300000;
        break;
      case 2:
        ms = 600000;
        break;
      case 3:
        ms = 1800000;
        break;
      case 4:
        ms = 0;
        break;
      }
      _ui->setAutoOff(ms);
    }

    extern GPSManager gpsManager;
    if (item.key == "rpm_ppr") {
      gpsManager.setPPRIndex(item.currentOptionIdx);
    }
    if (item.key == "utc_offset_idx") {
      gpsManager.setUtcOffset(item.currentOptionIdx - 12);
      _ui->drawStatusBar(true);
    }
    if (item.key == "gnss_mode")
      gpsManager.setGnssMode(item.currentOptionIdx);
    if (item.key == "gnss_proj")
      gpsManager.setProjection(item.currentOptionIdx == 1);
    if (item.key == "gnss_model")
      gpsManager.setDynamicModel(item.currentOptionIdx);
    if (item.key == "gnss_sbas")
      gpsManager.setSBASConfig(item.currentOptionIdx);
    if (item.key == "gnss_freq_limit") {
      int freqs[] = {10, 18, 25}; // 10Hz, 18Hz, 25Hz
      if (item.currentOptionIdx >= 0 && item.currentOptionIdx < 3)
        gpsManager.setFrequencyLimit(freqs[item.currentOptionIdx]);
    }
    if (item.key == "gps_rx_pin" || item.key == "gps_tx_pin") {
      int newRx = gpsManager.getRxPin();
      int newTx = gpsManager.getTxPin();
      if (item.key == "gps_rx_pin") {
        int pins[] = {22, 21};
        if (item.currentOptionIdx < 2)
          newRx = pins[item.currentOptionIdx];
      }
      if (item.key == "gps_tx_pin") {
        int pins[] = {21, 22};
        if (item.currentOptionIdx < 2)
          newTx = pins[item.currentOptionIdx];
      }
      gpsManager.setPins(newRx, newTx);
    }
    if (item.key == "gps_baud") {
      int bauds[] = {9600, 19200, 38400, 57600, 115200};
      if (item.currentOptionIdx < 5)
        gpsManager.setBaud(bauds[item.currentOptionIdx]);
    }
    if (item.key == "imu_roll_off")
      imuManager.setRollOffset(-5.0f + (item.currentOptionIdx * 0.5f));
    if (item.key == "imu_pitch_off")
      imuManager.setPitchOffset(-5.0f + (item.currentOptionIdx * 0.5f));

  } else if (item.type == TYPE_TOGGLE) {
    if (item.key == "rpm_enabled") {
      extern GPSManager gpsManager;
      gpsManager.setRpmEnabled(item.checkState);
    }
    if (item.key == "wifi_ap_enabled")
      wifiManager.setApEnabled(item.checkState);
    if (item.key == "live_tel_en")
      wifiManager.setLiveTelemetryEnabled(item.checkState);
    if (item.key == "debug_touch")
      _ui->setDebugTouch(item.checkState);
    if (item.key == "imu_enabled")
      imuManager.setEnabled(item.checkState);
  }

  // --- Mark for Batch Saving ---
  _needsSave = true;
  _lastChangeTime = millis();
}

void SettingsScreen::commitSettings() {
  if (!_needsSave)
    return;

  Serial.println("Settings: Committing changes to NVS...");
  _prefs.begin("laptimer", false);

  for (auto &item : _settings) {
    if (item.type == TYPE_VALUE) {
      if (item.key != "beacon_width") {
        _prefs.putInt(item.key.c_str(), item.currentOptionIdx);
      } else {
        int val = 20 + (item.currentOptionIdx * 10);
        _prefs.putInt("beacon_width", val);
      }
    } else if (item.type == TYPE_TOGGLE) {
      _prefs.putBool(item.key.c_str(), item.checkState);
    }
  }

  _prefs.end();
  _needsSave = false;
  Serial.println("Settings: Save complete.");
}

void SettingsScreen::update() {
  if (_currentMode == MODE_IMU_CALIBRATE) {
    drawIMUCalibration(false);
    handleTouchPoint();
    return;
  }

  static unsigned long lastTouch = 0;

  // WiFi Scanning Animation (runs without touch)
  if (_isScanning) {
    if (millis() - _lastScanAnim > 100) {
      _lastScanAnim = millis();
      _scanAnimStep = (_scanAnimStep + 1) % 4;

      TFT_eSPI *tft = _ui->getTft();
      String dots = "";
      for (int i = 0; i < _scanAnimStep; i++)
        dots += ".";

      // Update within Modal Box only
      int modalW = 200;
      int modalH = 80;
      int modalX = (SCREEN_WIDTH - modalW) / 2;
      int modalY = (SCREEN_HEIGHT - modalH) / 2;
      tft->fillRect(modalX + 5, modalY + 35, modalW - 10, 25,
                    _ui->getBackgroundColor());
      tft->setTextDatum(MC_DATUM);
      tft->setTextColor(TFT_WHITE, _ui->getBackgroundColor());
      tft->setTextFont(2);
      tft->drawString("SCANNING" + dots, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    }

    int n = WiFi.scanComplete();
    if (n >= 0) {
      _isScanning = false;
      _scanCount = n;
      // Clear content area
      _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
      _ui->drawStatusBar(true);
      drawWiFiList();
      _lastWiFiTouch = millis();
    }
    return; // Block other input while scanning
  }

  // Defferred Save Check
  if (_needsSave && millis() - _lastChangeTime > 2000) {
    commitSettings();
  }

  // Handle touches via handleTouchPoint instead of raw in update
  handleTouchPoint();

  if (_currentMode == MODE_ACCOUNT) {
    // Only redraw fields for cursor blinking to save SPI bandwidth
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 500) {
      lastBlink = millis();
      drawAccountLogin(false); // Draw fields only (highlightChar=0 skips kbd
                               // redraw if optimized)
    }
  }
}

void SettingsScreen::handleTouchPoint() {
  UIManager::TouchPoint p = _ui->getTouchPoint();
  if (p.x == -1)
    return;

  static unsigned long lastInteraction = 0;
  if (millis() - lastInteraction < TOUCH_DEBOUNCE_MS)
    return;
  lastInteraction = millis();

  static unsigned long lastSettingTouch = 0; // Restore missing declaration

  // --- SPECIAL MODE HANDLING ---
  if (_currentMode == MODE_ABOUT) {
    // Back Button (Bottom-Left)
    if (p.x < 100 && p.y > 240) { // Large touch area
      _currentMode = MODE_MAIN;
      _ui->setTitle("SETTINGS");
      loadSettings();
      _scrollOffset = 0;
      _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
      _ui->drawStatusBar(true);
      drawList(0, true);
      return;
    }

    // Integrated Remove Account Button (Under Logo) - Center relative to logo
    // area
    int cardX = 15;
    int cardY = 50;
    int cardW = SCREEN_WIDTH - 30;
    int cardH = 220;
    int btnW = 140;
    int btnH = 30;

    // Centered relative to logo visual space (Refined x=360)
    int btnX = 360 - (btnW / 2);
    int btnY = cardY + cardH - btnH - 10;

    if (p.x > btnX && p.x < btnX + btnW && p.y > btnY && p.y < btnY + btnH) {
      if (millis() - _lastKeyboardTouch > TOUCH_DEBOUNCE_MS) {
        _lastKeyboardTouch = millis();
        handleRemoveAccountFlow();
      }
    }
    return; // Ignore other touches in About
  }

  if (_currentMode == MODE_IMU_CALIBRATE) {
    // 1. Back Button (Top-Left) - expanded touch area
    if (p.x < 80 && p.y < 80) {
      if (millis() - lastSettingTouch < TOUCH_DEBOUNCE_MS)
        return;
      lastSettingTouch = millis();

      _currentMode = MODE_ENGINE;
      _ui->setTitle("VEHICLE & SENSORS");
      loadSettings();
      _scrollOffset = 0;
      _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
      _ui->drawStatusBar(true);
      drawList(0, true);
      return;
    }

    // 2. Calibrate Button (Bottom Center)
    // Matches drawIMUCalibration: btnW=300, btnH=45, Y=SCREEN_HEIGHT-60
    // Visual: 260 to 305 (approx)
    // Touch Area: Expanded generously to catch misses
    int btnW = 300;
    int btnH = 45;
    int btnX = (SCREEN_WIDTH - btnW) / 2;
    int btnY = SCREEN_HEIGHT - btnH - 15;

    // Expanded touch box:
    // X: +/- 20px tolerance
    // Y: From top of button (minus 10px) to BOTTOM of screen (catch low
    // touches)
    if (p.x > (btnX - 20) && p.x < (btnX + btnW + 20) && p.y > (btnY - 15)) {
      if (millis() - lastSettingTouch < TOUCH_LONG_PRESS_MS)
        return; // Debounce
      lastSettingTouch = millis();

      // Visual Feedback: Show "CALIBRATING..." in Orange
      _ui->getTft()->fillRoundRect(btnX, btnY, btnW, btnH, 6, TFT_ORANGE);
      _ui->getTft()->setTextColor(TFT_BLACK, TFT_ORANGE);
      _ui->getTft()->setTextDatum(MC_DATUM);
      _ui->getTft()->setTextFont(2);
      _ui->getTft()->setTextSize(1);
      _ui->getTft()->drawString("CALIBRATING...", SCREEN_WIDTH / 2,
                                btnY + btnH / 2);

      // Delay to let user see the feedback
      delay(200);

      // Trigger Hardware Calibration (Blocking)
      imuManager.calibrate();

      // Show "DONE!" in Green
      _ui->getTft()->fillRoundRect(btnX, btnY, btnW, btnH, 6, TFT_GREEN);
      _ui->getTft()->setTextColor(TFT_BLACK, TFT_GREEN);
      _ui->getTft()->drawString("DONE!", SCREEN_WIDTH / 2, btnY + btnH / 2);
      delay(500); // Hold success message

      // Restore Button locally to avoid full screen flicker
      _ui->getTft()->fillRoundRect(btnX, btnY, btnW, btnH, 6, TFT_WHITE);
      _ui->getTft()->setTextColor(TFT_BLACK, TFT_WHITE);
      _ui->getTft()->setTextFont(2);
      _ui->getTft()->setTextSize(1);
      _ui->getTft()->setTextDatum(MC_DATUM);
      _ui->getTft()->drawString("Calibrate", SCREEN_WIDTH / 2, btnY + btnH / 2);
      _ui->getTft()->setTextDatum(TC_DATUM); // Reset for other drawings
      return;
    }

    return; // Ignore other touches in this mode
  }

  // 1. Footer Buttons Zone (Standardized Bottom-Bar y >= 280)
  if (p.y >= 280) {
    if (millis() - lastSettingTouch < TOUCH_DEBOUNCE_MS)
      return;
    lastSettingTouch = millis();

    // A. Tombol Kembali (Bottom-Left) - Standardized Touch Box
    // FIX: Restrict to y > 280 in WiFi Pass mode to avoid DEL key conflict
    // (y=240-275)
    if (p.x < 80 && (p.y > 280 || _currentMode != MODE_WIFI_PASS)) {
      // Visual Feedback (Selection)
      if (_selectedIdx != -2) {
        _selectedIdx = -2;
        drawList(_scrollOffset, false);
      }

      // Logic Back (Single Tap)
      if (_currentMode == MODE_MAIN) {
        _ui->switchScreen(SCREEN_MENU);
        return;
      } else if (_currentMode == MODE_WIFI_PASS) {
        _currentMode = MODE_WIFI;
        _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                  SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
        _ui->drawStatusBar(true);
        drawWiFiList(true);
        return;
      } else if (_currentMode == MODE_WIFI_MENU) {
        _currentMode = MODE_MAIN;
        _ui->setTitle("SETTINGS");
        loadSettings();
      } else if (_currentMode == MODE_DRAG) {
        _currentMode = MODE_MAIN;
        _ui->setTitle("SETTINGS");
        loadSettings();
      } else if (_currentMode == MODE_ENGINE) {
        _currentMode = MODE_MAIN;
        _ui->setTitle("SETTINGS");
        loadSettings();
      } else if (_currentMode == MODE_UTILITY) {
        _currentMode = MODE_MAIN;
        _ui->setTitle("SETTINGS");
        loadSettings();
      } else if (_currentMode == MODE_GRAPHIC_TEST) {
        endGraphicTest();
        _currentMode = MODE_UTILITY;
        _ui->setTitle("UTILITY");
        loadSettings();
      } else if (_currentMode == MODE_IMU_CALIBRATE) {
        _currentMode = MODE_ENGINE;
        _ui->setTitle("VEHICLE & SENSORS"); // Standardized title
        loadSettings();
      } else if (_currentMode == MODE_LAP_CONFIG) {
        _currentMode = MODE_MAIN;
        _ui->setTitle("SETTINGS");
        loadSettings();
      } else if (_currentMode == MODE_ACCOUNT) {
        _currentMode = MODE_MAIN;
        _ui->setTitle("SETTINGS");
        loadSettings();
      } else {
        _currentMode = MODE_MAIN;
        _ui->setTitle("SETTINGS");
        loadSettings();
      }

      _scrollOffset = 0;
      _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
      _ui->drawStatusBar(true);
      drawList(0, true);
      return;
    }

    // B. Scroll Buttons (Bottom-Right)
    if (p.y > 280 && p.x > SCREEN_WIDTH - 120) {
      int listY = 40;
      int itemH = 28;
      int maxY = 280;
      int visibleItems = (maxY - listY) / itemH;

      if (p.x < SCREEN_WIDTH - 60) { // Up
        if (_scrollOffset > 0) {
          _scrollOffset--;
          drawList(_scrollOffset, true);
        }
      } else { // Down
        if (_scrollOffset + visibleItems < _settings.size()) {
          _scrollOffset++;
          drawList(_scrollOffset, true);
        }
      }
      return;
    }
  }

  // 2. List Zone (40 to 280)
  int listY = 40;
  int itemH = 28;
  int maxY = 280;

  if (_currentMode == MODE_MAIN || _currentMode == MODE_RPM ||
      _currentMode == MODE_CLOCK || _currentMode == MODE_ENGINE ||
      _currentMode == MODE_GNSS_CONFIG || _currentMode == MODE_WIFI_MENU ||
      _currentMode == MODE_UTILITY || _currentMode == MODE_IMU ||
      _currentMode == MODE_DRAG || _currentMode == MODE_LAP_CONFIG) {
    if (p.y >= listY && p.y < maxY) {
      if (millis() - lastSettingTouch < TOUCH_DEBOUNCE_MS)
        return;
      lastSettingTouch = millis();

      int idx = _scrollOffset + ((p.y - listY) / itemH);
      if (_currentMode == MODE_LAP_CONFIG && _settings.size() == 1) {
        idx = 0; // Force first item for any touch in list area
      }

      if (idx >= 0 && idx < _settings.size()) {
        SettingItem &item = _settings[idx];

        if (item.type == TYPE_VALUE || item.type == TYPE_TOGGLE) {
          // --- SINGLE TAP for Toggles/Values ---
          _selectedIdx = idx;
          handleTouch(idx);
          _lastTapIdx = -1; // Reset tap logic
        } else if (item.type == TYPE_ACTION) {
          // --- DOUBLE TAP for Actions (Navigation) ---
          if (_selectedIdx == idx) {
            handleTouch(idx); // Already selected -> Execute
          } else {
            _selectedIdx = idx; // Select first
            drawList(_scrollOffset, false);
          }
        }
      }
    }
  }

  // WiFi List Mode
  if (_currentMode == MODE_WIFI && p.y > 60 && p.y < 270) {
    if (millis() - _lastWiFiTouch > TOUCH_DEBOUNCE_MS) {
      _lastWiFiTouch = millis();
      int idx = (p.y - 60) / 30; // Updated item height (matched 30px)
      if (idx >= 0 && idx < _scanCount && idx < 6) { // Limit to 6 items
        _selectedWiFiIdx = idx;
        _targetSSID = WiFi.SSID(idx);
        _enteredPass = "";
        _currentMode = MODE_WIFI_PASS;
        // Clear only content area
        _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                  SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
        _ui->drawStatusBar(true);
        drawKeyboard();
        _lastKeyboardTouch = millis(); // Prevent immediate key press
        return;
      }
    }
  }

  // Account Login Keyboard Mode
  if (_currentMode == MODE_ACCOUNT) {
    String storedUser = syncManager.getStoredUsername();

    if (storedUser.length() > 0) {
      // --- LOGGED IN STATE (Updated to match card UI) ---
      int cardX = 15;
      int cardY = 50;
      int cardH = 220;
      int textX = cardX + 15;

      int btnW = 160;
      int btnH = 40;
      int btnX = textX;
      int btnY = cardY + cardH - 55;

      if (p.x > btnX && p.x < btnX + btnW && p.y > btnY && p.y < btnY + btnH) {
        if (millis() - _lastKeyboardTouch > TOUCH_DEBOUNCE_MS) {
          _lastKeyboardTouch = millis();
          handleRemoveAccountFlow();
        }
      }

    } else {
      // --- LOGGED OUT STATE (LOGIN FORM) ---
      if (millis() - _lastKeyboardTouch > TOUCH_KEYBOARD_MS) {
        _lastKeyboardTouch = millis();

        int keyboardY = 100;
        KeyboardComponent::KeyResult res =
            _keyboard.handleTouch(p.x, p.y, keyboardY, _isUppercase);

        String &target = _isEditingAccUser ? _accountUser : _accountPass;

        if (res.type == KeyboardComponent::KEY_CHAR) {
          // KeyboardComponent already sends correct case, no conversion needed
          char c = res.value;
          target += c;
          drawAccountLogin(false, c);
        } else if (res.type == KeyboardComponent::KEY_SHIFT) {
          _isUppercase = !_isUppercase;
          // Redraw keyboard to show case change
          _keyboard.draw(_ui->getTft(), keyboardY, _isUppercase, 0, 0);
        } else if (res.type == KeyboardComponent::KEY_DEL) {
          if (target.length() > 0)
            target.remove(target.length() - 1);
          drawAccountLogin(false, 2);
        } else if (res.type == KeyboardComponent::KEY_SPACE) {
          target += " ";
          drawAccountLogin(false, ' ');
        } else if (res.type == KeyboardComponent::KEY_OK) {
          if (_isEditingAccUser) {
            _isEditingAccUser = false;
            _isEditingAccPass = true;
            drawAccountLogin(true);
          } else if (_accountUser.length() > 0 && _accountPass.length() > 0) {
            // Perform Login
            TFT_eSPI *tft = _ui->getTft();
            int modalW = 240;
            int modalH = 80;
            int modalX = (SCREEN_WIDTH - modalW) / 2;
            int modalY = (SCREEN_HEIGHT - modalH) / 2;

            tft->fillRoundRect(modalX, modalY, modalW, modalH, 8,
                               _ui->getBackgroundColor());
            tft->drawRoundRect(modalX, modalY, modalW, modalH, 8,
                               COLOR_PRIMARY);
            tft->setTextDatum(MC_DATUM);
            tft->setTextColor(TFT_WHITE, _ui->getBackgroundColor());
            tft->setTextFont(2);
            tft->drawString("LOGIN...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);

            bool success = syncManager.performFirstSync(
                API_URL, _accountUser.c_str(), _accountPass.c_str());

            if (success) {
              tft->fillRect(modalX + 5, modalY + 5, modalW - 10, modalH - 10,
                            _ui->getBackgroundColor());
              tft->setTextColor(TFT_GREEN, _ui->getBackgroundColor());
              tft->drawString("SUCCESS!", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);

              _prefs.begin("muchrace", false);
              _prefs.putString("username", _accountUser);
              _prefs.putString("password", _accountPass);
              _prefs.putBool("setup_done", true);
              _prefs.end();

              delay(1500);
              ESP.restart();
            } else {
              tft->fillRect(modalX + 5, modalY + 5, modalW - 10, modalH - 10,
                            _ui->getBackgroundColor());
              tft->setTextColor(TFT_RED, _ui->getBackgroundColor());
              tft->setTextFont(2);
              tft->drawString("FAILED!", SCREEN_WIDTH / 2,
                              SCREEN_HEIGHT / 2 - 10);
              tft->setTextFont(1);
              tft->setTextColor(TFT_WHITE, _ui->getBackgroundColor());
              tft->drawString(syncManager.getLastError(), SCREEN_WIDTH / 2,
                              SCREEN_HEIGHT / 2 + 15);
              delay(3000);
              drawAccountLogin(true);
            }
          }
        }
      }

      // Field switch touch
      if (p.x > 30 && p.x < 290) {
        if (p.y > 32 && p.y < 62) { // FIELD1_Y
          _isEditingAccUser = true;
          _isEditingAccPass = false;
          drawAccountLogin(true);
        } else if (p.y > 65 && p.y < 95) { // FIELD2_Y
          _isEditingAccUser = false;
          _isEditingAccPass = true;
          drawAccountLogin(true);
        }
      }
    }
  }
  // WiFi Keyboard Mode - handle key presses
  if (_currentMode == MODE_WIFI_PASS) {
    if (millis() - _lastKeyboardTouch > TOUCH_KEYBOARD_MS) {
      _lastKeyboardTouch = millis();

      // Keyboard input (Standardized to 100)
      int keyboardY = 100;
      KeyboardComponent::KeyResult res =
          _keyboard.handleTouch(p.x, p.y, keyboardY, _isUppercase);
      switch (res.type) {
      case KeyboardComponent::KEY_CHAR: {
        // KeyboardComponent already sends correct case, no conversion needed
        char c = res.value;
        _enteredPass += c;
        drawKeyboard(false, c); // Highlight the character
        break;
      }
      case KeyboardComponent::KEY_SHIFT:
        _isUppercase = !_isUppercase;
        // Redraw keyboard to show case change
        _keyboard.draw(_ui->getTft(), keyboardY, _isUppercase, 0, 0);
        break;
      case KeyboardComponent::KEY_DEL:
        if (_enteredPass.length() > 0) {
          _enteredPass.remove(_enteredPass.length() - 1);
          drawKeyboard(false, 2); // Highlight DEL
        }
        break;
      case KeyboardComponent::KEY_SPACE:
        _enteredPass += " ";
        drawKeyboard(false, ' '); // Highlight SPACE
        break;
      case KeyboardComponent::KEY_OK:
        drawKeyboard(false, 3); // Highlight OK (briefly before switching)
        delay(100);             // Brief visual feedback
        connectWiFi();
        break;
      default:
        // No default action (Password Toggle removed)
        break;
      }
    }
  }

  // Continuous Update for GPS Mode
  if (_currentMode == MODE_GPS) {
    drawGPSStatus();
  }

  if (_currentMode == MODE_GRAPHIC_TEST) {
    updateGraphicTest();
  }

  if (_currentMode == MODE_ABOUT) {
    UIManager::TouchPoint p = _ui->getTouchPoint();
    if (p.x != -1) {
      if (millis() - lastSettingTouch > TOUCH_DEBOUNCE_MS) {
        lastSettingTouch = millis();
        // Exit on any touch or specific back button
        _currentMode = MODE_MAIN;
        loadSettings();
        _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                  SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
        _ui->drawStatusBar(true);
        drawList(0, true);
      }
    }
  }
}

void SettingsScreen::handleTouch(int idx) {
  TFT_eSPI *tft = _ui->getTft();
  if (idx < 0 || idx >= _settings.size())
    return;

  SettingItem &item = _settings[idx];

  if (item.type == TYPE_VALUE) {
    item.currentOptionIdx++;
    if (item.currentOptionIdx >= item.options.size()) {
      item.currentOptionIdx = 0;
    }
    drawList(_scrollOffset, false); // Surgical redraw: only update the item
    saveSetting(idx);               // Then save to preferences
  } else if (item.type == TYPE_TOGGLE) {
    item.checkState = !item.checkState;
    drawList(_scrollOffset, false); // Surgical redraw: only update the item
    saveSetting(idx);               // Then save to preferences
  } else if (item.type == TYPE_ACTION) {
    if (item.name == "OFFLINE SERVER") {
      _ui->switchScreen(SCREEN_WEB_SERVER);
      return;
    } else if (item.name == "GPS DEBUG") {
      _ui->switchScreen(SCREEN_GPS_DEBUG);
      return;
    } else if (item.name == "RPM SENSOR") {
      _currentMode = MODE_ENGINE;
      _ui->setTitle("RPM SENSOR");
      loadSettings();
      _scrollOffset = 0; // Fix: Reset scroll
      _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
      _ui->drawStatusBar(true);
      drawList(0, true);
      return;
    } else if (item.name == "G-FORCE CALIBRATION") {
      imuManager.requestActivity(true);
      _currentMode = MODE_IMU_CALIBRATE;
      _ui->getTft()->fillScreen(_ui->getBackgroundColor());
      _ui->drawStatusBar(true);
      drawIMUCalibration(true);
      return;
    } else if (item.name == "CALIBRATE LEVEL") {
      imuManager.requestActivity(true);
      // Visual feedback
      TFT_eSPI *tft = _ui->getTft();
      tft->fillRect(0, 100, SCREEN_WIDTH, 40, COLOR_BG);
      tft->setTextColor(TFT_YELLOW, COLOR_BG);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("Leveling... Keep Still", SCREEN_WIDTH / 2, 120);

      delay(500);
      imuManager.calibrateLevel();

      tft->fillRect(0, 100, SCREEN_WIDTH, 40, COLOR_BG);
      tft->setTextColor(TFT_GREEN, COLOR_BG);
      tft->drawString("DONE!", SCREEN_WIDTH / 2, 120);
      delay(500);

      loadSettings(); // Refresh offsets in list
      drawList(_scrollOffset, true);
      imuManager.requestActivity(false); // Done with leveling if in list
      return;
    } else if (item.name == "GNSS FINE TUNING") {
      _currentMode = MODE_GNSS_CONFIG;
      _selectedIdx = 0;
      _lastSelectedIdx = -1;
      _scrollOffset = 0; // Fix: Reset scroll
      _ui->setTitle("GNSS CONFIG");
      loadSettings();
      _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
      _ui->drawStatusBar(true);
      drawList(0, true);
      return;
    } else if (item.name == "UTILITY") {
      _currentMode = MODE_UTILITY;
      _selectedIdx = 0;
      _lastSelectedIdx = -1;
      _scrollOffset = 0; // Fix: Reset scroll
      _ui->setTitle("UTILITY");
      loadSettings();
      _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
      _ui->drawStatusBar(true);
      drawList(0, true);
      return;
    } else if (item.name == "VEHICLE & SENSORS") {
      _currentMode = MODE_ENGINE;
      _selectedIdx = 0;
      _lastSelectedIdx = -1;
      _scrollOffset = 0; // Fix: Reset scroll
      _ui->setTitle("VEHICLE & SENSORS");
      loadSettings();
      _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
      _ui->drawStatusBar(true);
      drawList(0, true);
      return;
    } else if (item.name == "DRAG SETTINGS") {
      _currentMode = MODE_DRAG;
      _selectedIdx = 0;
      _lastSelectedIdx = -1;
      _scrollOffset = 0; // Fix: Reset scroll
      _ui->setTitle("DRAG SETTINGS");
      loadSettings();
      _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
      _ui->drawStatusBar(true);
      drawList(0, true);
      return;
    } else if (item.name == "CONNECTION SETUP") {
      _currentMode = MODE_WIFI_MENU;
      _selectedIdx = 0;
      _lastSelectedIdx = -1;
      _scrollOffset = 0; // Fix: Reset scroll
      _ui->setTitle("CONNECTION SETUP");
      loadSettings();
      _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
      _ui->drawStatusBar(true);
      drawList(0, true);
      return;
    } else if (item.name == "LAP TIMER SETTINGS") {
      _currentMode = MODE_LAP_CONFIG;
      _selectedIdx = 0;
      _lastSelectedIdx = -1;
      _scrollOffset = 0; // Fix: Reset scroll
      _ui->setTitle("LAP TIMER CONFIG");
      loadSettings();
      _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
      _ui->drawStatusBar(true);
      drawList(0, true);
      return;
    } else if (item.name == "ABOUT DEVICE") {
      _currentMode = MODE_ABOUT;
      _ui->setTitle("ABOUT DEVICE");
      drawAbout();
      return;
    } else if (item.name == "RESET GPS") {
      // Modern Modal Visual feedback
      TFT_eSPI *tft = _ui->getTft();
      int modalW = 200;
      int modalH = 80;
      int modalX = (SCREEN_WIDTH - modalW) / 2;
      int modalY = (SCREEN_HEIGHT - modalH) / 2;

      // Draw Modal Box
      tft->fillRoundRect(modalX, modalY, modalW, modalH, 8,
                         _ui->getBackgroundColor());
      tft->drawRoundRect(modalX, modalY, modalW, modalH, 8, TFT_ORANGE);

      tft->setTextColor(TFT_ORANGE, _ui->getBackgroundColor());
      tft->setTextDatum(MC_DATUM);
      tft->setFreeFont(&Org_01);
      tft->setTextSize(1);

      // Center the two text lines perfectly within the 80px high box
      tft->drawString("RESETTING GPS...", SCREEN_WIDTH / 2, modalY + 30);
      tft->setTextColor(TFT_LIGHTGREY, _ui->getBackgroundColor());
      tft->drawString("Please wait", SCREEN_WIDTH / 2, modalY + 50);

      extern GPSManager gpsManager;
      gpsManager.resetModule();

      delay(1000); // Give the module time to process the reset command

      // Update Modal to Success State
      tft->fillRoundRect(modalX, modalY, modalW, modalH, 8,
                         _ui->getBackgroundColor());
      tft->drawRoundRect(modalX, modalY, modalW, modalH, 8, TFT_GREEN);
      tft->setTextColor(TFT_GREEN, _ui->getBackgroundColor());
      tft->drawString("GPS RESET DONE!", SCREEN_WIDTH / 2,
                      modalY + (modalH / 2));

      delay(800);

      // Redraw screen
      _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
      _ui->drawStatusBar(true);
      loadSettings();
      drawList(_scrollOffset, true);
      return;
    } else if (item.name == "WIFI CONFIG") {
      // Start WiFi scan (Show Modal Box as requested)
      _currentMode = MODE_WIFI;
      TFT_eSPI *tft = _ui->getTft();

      int modalW = 200;
      int modalH = 80;
      int modalX = (SCREEN_WIDTH - modalW) / 2;
      int modalY = (SCREEN_HEIGHT - modalH) / 2;

      tft->fillRoundRect(modalX, modalY, modalW, modalH, 8,
                         _ui->getBackgroundColor());
      tft->drawRoundRect(modalX, modalY, modalW, modalH, 8, COLOR_PRIMARY);
      tft->setTextColor(TFT_WHITE, _ui->getBackgroundColor());
      tft->setTextDatum(MC_DATUM);
      tft->setTextFont(2);
      tft->drawString("SCANNING...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);

      WiFi.mode(WIFI_STA);
      WiFi.disconnect();
      delay(100);
      WiFi.scanNetworks(true); // Async scan
      _isScanning = true;
      _lastScanAnim = millis();
      _scanAnimStep = 0;
      return;
    } else if (item.name == "SYNC WITH CLOUD") {
      // Redirect to standardized Synchronize Screen
      _ui->switchScreen(SCREEN_SYNCHRONIZE);
      return;
      // item.name check updated below

      // Logic moved to logout button handler in MODE_ACCOUNT
    } else if (item.name == "TFT BENCHMARK") {
      _currentMode = MODE_GRAPHIC_TEST;
      startGraphicTest();
      return;
    } else if (item.name == "TOUCH DEBUG") {
      _ui->switchScreen(SCREEN_TOUCH_DEBUG);
      return;
    } else if (item.name == "REMOVE ACCOUNT") {
      _currentMode = MODE_ACCOUNT;
      _ui->setTitle("REMOVE ACCOUNT");
      // Don't reset fields here, rely on drawAccountLogin to show info
      _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
      _ui->drawStatusBar(true);

      _lastKeyboardTouch = millis();
      drawAccountLogin(true);
      loadSettings();
      return;
    } else if (item.name == "SD CARD TEST") {
      _ui->switchScreen(SCREEN_SD_TEST); // Use dedicated screen
      return;
    }
  } else if (item.key == "reset_ref") {
    _prefs.begin("laptimer", false);
    _prefs.remove("drag_ref"); // Key for reference run time
    _prefs.end();
  }
}

void SettingsScreen::drawAbout() {
  TFT_eSPI *tft = _ui->getTft();
  tft->fillScreen(_ui->getBackgroundColor());
  _ui->drawStatusBar(true); // Redraw to show "ABOUT DEVICE" title

  // --- ABOUT VIEW (SD Test Style) ---
  int cardX = 15;
  int cardY = 50;
  int cardW = SCREEN_WIDTH - 30;
  int cardH = 220;

  // Navy Background with Grey Border
  tft->fillRoundRect(cardX, cardY, cardW, cardH, 10, 0x10A2);
  tft->drawRoundRect(cardX, cardY, cardW, cardH, 10, TFT_DARKGREY);

  // Header
  tft->setTextColor(TFT_SKYBLUE, 0x10A2);
  tft->setTextDatum(TL_DATUM);
  tft->setTextFont(2);
  tft->setFreeFont(NULL);
  tft->drawString("ABOUT DEVICE", cardX + 10, cardY + 8);
  tft->drawFastHLine(cardX + 10, cardY + 28, 140,
                     0x3186); // Even shorter to avoid logo

  // Logo (Right Side - Shifted further right/up to avoid overlap)
  // The logo is 320x240.
  tft->drawBitmap(200, 45, image_BOLONG_bits, 320, 240, TFT_WHITE);

  // Text Info (Left Side - Tightened Spacing)
  int textX = cardX + 15;
  int textY = cardY + 45;

  tft->setTextColor(TFT_SILVER, 0x10A2);
  tft->setFreeFont(&Org_01);
  tft->drawString("FIRMWARE", textX, textY);

  tft->setTextColor(TFT_WHITE, 0x10A2);
  tft->setTextFont(2);
  tft->setFreeFont(NULL);
  textY += 12;
  tft->drawString("Much Racing " FIRMWARE_VERSION, textX, textY);

  textY += 28;
  tft->setTextColor(TFT_SILVER, 0x10A2);
  tft->setFreeFont(&Org_01);
  tft->drawString("DEVICE ID", textX, textY);

  tft->setTextColor(TFT_WHITE, 0x10A2);
  tft->setTextFont(2);
  tft->setFreeFont(NULL);
  textY += 12;
  String mac = WiFi.macAddress();
  tft->drawString(mac, textX, textY);

  textY += 28;
  tft->setTextColor(TFT_SILVER, 0x10A2);
  tft->setFreeFont(&Org_01);
  tft->drawString("CREDITS", textX, textY);

  tft->setTextColor(TFT_ORANGE, 0x10A2);
  tft->setTextFont(2);
  tft->setFreeFont(NULL);
  textY += 12;
  tft->drawString("Made by Faisal Muchdas", textX, textY);

  textY += 28;
  tft->setTextColor(TFT_SILVER, 0x10A2);
  tft->setFreeFont(&Org_01);
  tft->drawString("ACCOUNT", textX, textY);

  textY += 12;
  tft->setTextColor(TFT_WHITE, 0x10A2);
  tft->setTextFont(2);
  tft->setFreeFont(NULL);

  // Always show username, or '-' if not set
  String user = syncManager.getStoredUsername();
  if (user.length() == 0) {
    user = "-"; // Simple placeholder instead of "NOT SYNCED"
  }

  // Display username
  tft->drawString(user, textX, textY);

  // Display driver number if available (on next line)
  Preferences prefs;
  prefs.begin("muchrace", true);
  int driverNumber = prefs.getInt("driver_number", 0);
  prefs.end();

  if (driverNumber > 0) {
    textY += 14;
    tft->setTextColor(TFT_SKYBLUE, 0x10A2);
    tft->setTextFont(1);
    String driverText = "Driver #" + String(driverNumber);
    tft->drawString(driverText, textX, textY);
  }

  // Back Button (Blue Triangle)
  tft->fillTriangle(15, SCREEN_HEIGHT - 25, 30, SCREEN_HEIGHT - 35, 30,
                    SCREEN_HEIGHT - 15, TFT_BLUE);

  // --- INTEGRATED REMOVE ACCOUNT BUTTON ---
  int btnW = 140;
  int btnH = 30;
  int btnX = 360 - (btnW / 2); // Perfectly centered with logo (Refined x=360)
  int btnY = cardY + cardH - btnH - 10;

  tft->fillRoundRect(btnX, btnY, btnW, btnH, 6, TFT_RED);
  tft->setTextColor(TFT_WHITE, TFT_RED);
  tft->setTextDatum(MC_DATUM);
  tft->setTextFont(2);
  tft->drawString("REMOVE ACCOUNT", btnX + btnW / 2, btnY + btnH / 2);
}

void SettingsScreen::drawHeader(String title, uint16_t backColor) {
  TFT_eSPI *tft = _ui->getTft();

  // Back Button (Blue Triangle)
  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, TFT_BLUE);
}

void SettingsScreen::drawGPSStatus(bool force) {
  TFT_eSPI *tft = _ui->getTft();

  if (force) {
    tft->fillScreen(_ui->getBackgroundColor());
    _ui->drawStatusBar(true);

    // Static Header
    // Back Button (Blue Triangle)
    tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                      SCREEN_HEIGHT - 20, TFT_BLUE);

    // Static layout elements
    int yStats = 45; // Shifted up from 62 to prevent overlap
    int hStatsHeader = 18;
    tft->fillRect(10, yStats, 160, hStatsHeader, _ui->getTextColor());
    tft->setTextColor(_ui->getBackgroundColor(), _ui->getTextColor());
    tft->setTextSize(1);
    tft->drawString("GPS STATUS", 15, yStats + 9);

    // List Rect
    int listH = 6 * 15 + 8; // 6 items * 15px + pad
    tft->drawRect(10, yStats + hStatsHeader, 160, listH, _ui->getTextColor());

    // Radar
    int cX = 245, cY = 120, r = 55;

    // Radar
    tft->drawCircle(cX, cY, r * 0.66, COLOR_SECONDARY);
    tft->drawCircle(cX, cY, r * 0.33, COLOR_SECONDARY);
    tft->drawFastHLine(cX - r, cY, 2 * r, COLOR_SECONDARY);
    tft->drawFastVLine(cX, cY - r, 2 * r, COLOR_SECONDARY);

    auto drawCard = [&](String l, int x, int y) {
      tft->fillCircle(x, y, 9, _ui->getTextColor());
      tft->setTextColor(_ui->getBackgroundColor(), _ui->getTextColor());
      tft->setTextDatum(MC_DATUM);
      tft->drawString(l, x, y + 1);
    };
    drawCard("N", cX, cY - r);
    drawCard("S", cX, cY + r);
    drawCard("E", cX + r, cY);
    drawCard("W", cX - r, cY);

    // Reset trackers
    _lastSats = -1;
    _lastHdopValue = -1.0;
    _lastLat = 0;
    _lastLon = 0;
    bool _isUppercase = true;
  }

  // Rate Limiting
  static unsigned long lastGPSDraw = 0;
  if (!force && millis() - lastGPSDraw < 1000)
    return;
  lastGPSDraw = millis();

  extern GPSManager gpsManager;
  int sats = gpsManager.getSatellites();
  double hdop = gpsManager.getHDOP();
  double lat = gpsManager.getLatitude();
  double lon = gpsManager.getLongitude();
  bool fixed = gpsManager.isFixed();

  int yStats = 45; // Match static layout
  int hStatsHeader = 18;
  int hItem = 15;
  int curY = yStats + hStatsHeader + 4;

  auto drawRowValue = [&](String label, String val, String lastVal, int y) {
    if (force || val != lastVal) {
      tft->setTextDatum(ML_DATUM);
      tft->setTextColor(_ui->getTextColor(), _ui->getBackgroundColor());
      tft->setFreeFont(&Org_01);
      tft->setTextSize(1);

      // Draw label and separator only on force
      if (force) {
        tft->drawString(label, 15, y + (hItem / 2));
        tft->drawString(":", 60, y + (hItem / 2));
        tft->drawFastHLine(10, y + hItem, 160, COLOR_SECONDARY);
      }

      // Clear and draw value
      tft->fillRect(75, y, 90, hItem - 1, _ui->getBackgroundColor());
      tft->drawString(val, 75, y + (hItem / 2));
    }
  };

  drawRowValue("GPS", String(sats) + " Sat",
               force ? "" : String(_lastSats) + " Sat", curY);
  curY += hItem;
  drawRowValue("GLO", "- Sat", force ? "" : "- Sat", curY);
  curY += hItem;
  drawRowValue("GAL", "- Sat", force ? "" : "- Sat", curY);
  curY += hItem;
  drawRowValue("BEI", "- Sat", force ? "" : "- Sat", curY);
  curY += hItem;
  drawRowValue("MBN", "A : -", force ? "" : "A : -", curY);
  curY += hItem;
  drawRowValue("HDOP", String(hdop, 2), force ? "" : String(_lastHdopValue, 2),
               curY);

  if (force || lat != _lastLat || lon != _lastLon) {
    int tableBottom = yStats + hStatsHeader + (6 * hItem + 8);
    int yLat = tableBottom + 10;
    tft->fillRect(10, yLat, 200, 40, _ui->getBackgroundColor());
    tft->setTextColor(_ui->getTextColor(), _ui->getBackgroundColor());
    tft->setTextDatum(TL_DATUM);
    tft->drawString("LAT : " + String(lat, 6), 15, yLat);
    tft->drawString("LNG : " + String(lon, 6), 15, yLat + 18);
  }

  // Polar Plot Satellites (Simulated for feedback in code)
  if (fixed != _lastFixed || fixed) {
    int cX = 245, cY = 120, r = 55;
    // Only clear plot area if state changed or we need to redraw blinkers
    if (fixed) {
      // Clear old dots (simplest is clear small r+5 area around dots, but
      // radar is fast) Just redraw radar lines to "clean" old dots
      tft->drawCircle(cX, cY, r * 0.66, COLOR_SECONDARY);
      tft->drawCircle(cX, cY, r * 0.33, COLOR_SECONDARY);
      tft->drawFastHLine(cX - r, cY, 2 * r, COLOR_SECONDARY);
      tft->drawFastVLine(cX, cY - r, 2 * r, COLOR_SECONDARY);

      unsigned long t = millis() / 1000;
      float rad = ((t * 5) % 360) * DEG_TO_RAD;
      tft->fillCircle(cX + cos(rad) * (r * 0.5), cY + sin(rad) * (r * 0.5), 3,
                      TFT_GREEN);
      rad = ((t * 2 + 120) % 360) * DEG_TO_RAD;
      tft->fillCircle(cX + cos(rad) * (r * 0.8), cY + sin(rad) * (r * 0.8), 3,
                      TFT_GREEN);
    }
  }

  _lastSats = sats;
  _lastHdopValue = hdop;
  _lastLat = lat;
  _lastLon = lon;
  _lastFixed = fixed;

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}

void SettingsScreen::drawList(int scrollOffset, bool force) {
  TFT_eSPI *tft = _ui->getTft();

  // Highlight Back Arrow if selected
  uint16_t backColor =
      (_selectedIdx == -2) ? COLOR_HIGHLIGHT : _ui->getTextColor();

  if (force) {
    _ui->drawStatusBar(true);
    // Draw horizontal line after status bar
    // tft->drawFastHLine(0, 20, SCREEN_WIDTH, COLOR_SECONDARY); // Redundant
  }

  // List (starts at y=40, after header)
  // List (starts at y=40, after header)
  int listY = 40;
  int itemH = 28;
  int maxY = 280; // Extended from 264 to align with new touch logic

  // Clear list area to prevent ghosts when scrolling
  if (force) {
    tft->fillRect(0, listY, SCREEN_WIDTH, maxY - listY,
                  _ui->getBackgroundColor());
  }

  int visibleItems = (maxY - listY) / itemH;

  for (int i = 0; i < visibleItems; i++) {
    int idx = scrollOffset + i;
    if (idx >= _settings.size())
      break;

    SettingItem &item = _settings[idx];
    int y = listY + (i * itemH);

    int sIdx = idx;
    bool stateChanged = (sIdx == _selectedIdx || sIdx == _lastSelectedIdx);

    if (force || stateChanged) {
      // Background
      uint16_t bgColor = (sIdx == _selectedIdx) ? _ui->getTextColor()
                                                : _ui->getBackgroundColor();
      uint16_t txtColor = (sIdx == _selectedIdx) ? _ui->getBackgroundColor()
                                                 : _ui->getTextColor();

      // Explicitly clear background - Surgical update if only value changed
      if (!force && _selectedIdx == _lastSelectedIdx && sIdx == _selectedIdx) {
        // Redraw only the right side (value area) to prevent flickering name
        tft->fillRect(SCREEN_WIDTH / 2, y, SCREEN_WIDTH / 2, itemH, bgColor);
      } else {
        tft->fillRect(0, y, SCREEN_WIDTH, itemH, bgColor);
      }
      if (sIdx != _selectedIdx) {
        tft->drawFastHLine(0, y + itemH - 1, SCREEN_WIDTH, COLOR_SECONDARY);
      }

      // Name
      tft->setTextDatum(ML_DATUM); // Middle Left for centering
      tft->setTextFont(1);         // Compact Font
      tft->setTextSize(1);
      tft->setTextColor(txtColor, bgColor);

      String displayName = item.name;
      if (_currentMode == MODE_GNSS_CONFIG || _currentMode == MODE_LAP_CONFIG) {
        displayName = String(idx + 1) + ". " + item.name;
      }
      tft->drawString(displayName, 15, y + itemH / 2);

      // Value / Toggle / Action
      tft->setTextDatum(MR_DATUM); // Middle Right
      String valText = "";
      if (item.type == TYPE_VALUE) {
        if (item.currentOptionIdx >= 0 &&
            item.currentOptionIdx < item.options.size()) {
          valText = item.options[item.currentOptionIdx] + " >";
        }
      } else if (item.type == TYPE_TOGGLE) {
        valText = "";
      } else if (item.type == TYPE_ACTION) {
        valText = ">";
      }
      tft->drawString(valText, SCREEN_WIDTH - 15, y + itemH / 2);

      // Custom Render for Toggle
      if (item.type == TYPE_TOGGLE) {
        int swW = 24;
        int swH = 12;
        int swX = SCREEN_WIDTH - swW - 10;
        int swY = y + (itemH - swH) / 2;
        int r = swH / 2;

        if (item.checkState) {
          tft->fillRoundRect(swX, swY, swW, swH, r, TFT_GREEN);
          tft->fillCircle(swX + swW - r, swY + r, r - 2, TFT_WHITE);
        } else {
          tft->fillRoundRect(swX, swY, swW, swH, r, TFT_RED);
          tft->fillCircle(swX + r, swY + r, r - 2, TFT_WHITE);
        }
      }
    }
  }
  _lastSelectedIdx = _selectedIdx;

  // Draw bottom elements AFTER list
  if (force) {
    // Clear area between list and bottom (260 to 320)
    tft->fillRect(0, maxY, SCREEN_WIDTH, SCREEN_HEIGHT - maxY,
                  _ui->getBackgroundColor());

    // Back Button (Blue Triangle)
    tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                      SCREEN_HEIGHT - 20, TFT_BLUE);

    // Scroll Buttons (Right Edge) - 16x10 standard
    int scrollX = SCREEN_WIDTH - 120; // Shifted left more
    // Up Arrow
    if (scrollOffset > 0 && _currentMode != MODE_LAP_CONFIG) {
      tft->fillTriangle(scrollX, 300, scrollX + 24, 300, scrollX + 12, 285,
                        COLOR_ACCENT);
    }

    // Down Arrow
    if (scrollOffset + visibleItems < _settings.size() &&
        _currentMode != MODE_LAP_CONFIG) {
      tft->fillTriangle(scrollX + 60, 285, scrollX + 84, 285, scrollX + 72, 300,
                        COLOR_ACCENT);
    }
  }

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}

// WiFi Functions

void SettingsScreen::drawWiFiList(bool force) {
  TFT_eSPI *tft = _ui->getTft();

  // FIX: Clear screen to prevent ghosting (Double Screen issue)
  tft->fillScreen(_ui->getBackgroundColor());
  _ui->drawStatusBar(true);

  drawHeader("CONNECTION SETUP");

  // List networks
  int listY = 60;
  int itemH = 30; // Reduced height for more refinement

  for (int i = 0; i < _scanCount && i < 7;
       i++) { // fit more items with smaller height
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    bool isSecure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);

    int y = listY + (i * itemH);

    // Background with spacing
    uint16_t bgColor = (i == _selectedWiFiIdx) ? TFT_BLUE : COLOR_BG;
    uint16_t txtColor = (i == _selectedWiFiIdx) ? TFT_WHITE : COLOR_TEXT;

    tft->fillRoundRect(10, y, SCREEN_WIDTH - 20, itemH - 4, 6, bgColor);

    // Draw Border for unselected items
    if (i != _selectedWiFiIdx) {
      tft->drawRoundRect(10, y, SCREEN_WIDTH - 20, itemH - 4, 6,
                         COLOR_SECONDARY);
    }

    // SSID
    tft->setTextColor(txtColor, bgColor);
    tft->setTextFont(1);
    tft->setTextDatum(ML_DATUM);
    String displayName = ssid;
    if (displayName.length() > 18)
      displayName = displayName.substring(0, 15) + "...";
    tft->drawString(displayName, 20, y + (itemH - 4) / 2);

    // Signal strength
    String signal = String(rssi);
    tft->setTextDatum(MR_DATUM);
    tft->drawString(signal, SCREEN_WIDTH - 45, y + (itemH - 4) / 2);

    // Lock icon if secured
    if (isSecure) {
      tft->drawString("*", SCREEN_WIDTH - 25, y + (itemH - 4) / 2);
    }
  }

  if (_scanCount == 0) {
    tft->setTextColor(COLOR_TEXT, COLOR_BG);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("No networks found", SCREEN_WIDTH / 2, 120);
    tft->fillCircle(SCREEN_WIDTH - 20, 290, 5, TFT_RED);
    tft->drawString("Scan to Refresh", SCREEN_WIDTH / 2, 290);
  }

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}

void SettingsScreen::drawKeyboard(bool fullRedraw, char highlightChar) {
  if (_isScanning)
    return; // Prevent keyboard during scan
  if (fullRedraw) {
    TFT_eSPI *tft = _ui->getTft();

    // FIX: Clear screen to prevent ghosting (Double Screen issue)
    tft->fillScreen(_ui->getBackgroundColor());
    _ui->drawStatusBar(true);

    drawHeader("WIFI PASSWORD");
    // Show SSID (Compact)
    tft->setTextFont(1);
    tft->setTextColor(COLOR_ACCENT, COLOR_BG);
    tft->setTextDatum(TC_DATUM);
    tft->drawString("SSID: " + _targetSSID, SCREEN_WIDTH / 2, 40);
  }

  TFT_eSPI *tft = _ui->getTft();

  // -- Standardized Input Field (Matches SetupScreen) --
  // Box: Centered, Width 260, Height 30, Y=65 (Shifted up for keyboard at
  // 100)
  int boxW = 260;
  int boxH = 30;
  int boxX = (SCREEN_WIDTH - boxW) / 2;
  int boxY = 65;

  // Label "PASSWORD"
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextDatum(BL_DATUM);
  tft->setTextColor(_ui->getTextColor(), _ui->getBackgroundColor());
  tft->drawString("PASSWORD", boxX, boxY - 2);

  // Box Background
  tft->drawRect(boxX, boxY, boxW, boxH, COLOR_PRIMARY);
  tft->fillRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, TFT_DARKGREY);

  // Value (Always Visible)
  tft->setTextFont(1);
  tft->setTextSize(1);
  tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft->setTextDatum(ML_DATUM);

  String displayPass = _enteredPass;
  tft->drawString(displayPass, boxX + 5, boxY + boxH / 2 + 2);

  // Cursor
  int cursorX = boxX + 5 + tft->textWidth(displayPass);
  if ((millis() / 500) % 2 == 0) { // Blink cursor
    tft->drawFastVLine(cursorX, boxY + 6, 18, COLOR_PRIMARY);
  }

  // Keyboard at Y=100 (Standardized)
  int keyboardY = 100;
  _keyboard.draw(tft, keyboardY, _isUppercase, highlightChar);

  // Unified Blue Triangle Back Button
  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, TFT_BLUE);
}

void SettingsScreen::connectWiFi() {
  TFT_eSPI *tft = _ui->getTft();
  tft->fillScreen(COLOR_BG);
  _ui->drawStatusBar(true);

  tft->setTextColor(TFT_WHITE, COLOR_BG);
  tft->setTextDatum(MC_DATUM);
  tft->drawString("Connecting...", SCREEN_WIDTH / 2, 120);

  // Debug: Log WiFi credentials before attempting connection
  Serial.println("=== SettingsScreen WiFi Connection ===");
  Serial.print("Target SSID: ");
  Serial.println(_targetSSID);
  Serial.print("Entered Password: ");
  Serial.println(_enteredPass);
  Serial.print("Password length: ");
  Serial.println(_enteredPass.length());
  Serial.println("======================================");

  // Use core manager for connection
  bool success = wifiManager.connect(_targetSSID.c_str(), _enteredPass.c_str());

  if (success) {
    tft->fillRect(0, 80, SCREEN_WIDTH, 100,
                  COLOR_BG); // Clear "Connecting..." area
    tft->setTextColor(TFT_GREEN, COLOR_BG);
    tft->drawString("Connected!", SCREEN_WIDTH / 2, 120);
    delay(2000);

    // Return to main settings only on success
    _currentMode = MODE_MAIN;
    loadSettings();
    tft->fillScreen(COLOR_BG);
    _ui->drawStatusBar(true);
    drawList(0, true);
  } else {
    tft->fillRect(0, 80, SCREEN_WIDTH, 100,
                  COLOR_BG); // Clear "Connecting..." area
    tft->setTextColor(TFT_RED, COLOR_BG);
    tft->drawString("Failed!", SCREEN_WIDTH / 2, 120);
    delay(2000);

    // Stay in keyboard mode on failure
    tft->fillScreen(COLOR_BG);
    _ui->drawStatusBar(true);
    drawKeyboard(true); // Full redraw to restore keyboard UI
  }
}

void SettingsScreen::startGraphicTest() {
  TFT_eSPI *tft = _ui->getTft();
  tft->fillScreen(TFT_BLACK);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextDatum(TL_DATUM);
  tft->setTextFont(2);
  tft->drawString("Running Benchmark...", 10, STATUS_BAR_HEIGHT + 10);

  runBenchmark();

  // After benchmark, we just show the results (already printed by
  // runBenchmark)

  tft->setTextColor(TFT_GREEN, TFT_BLACK);
  tft->setTextDatum(BL_DATUM);
  tft->setTextFont(2);
  tft->drawString("< Exit", 10, SCREEN_HEIGHT - 10);
}

void SettingsScreen::updateGraphicTest() {
  // Check for any touch to exit
  UIManager::TouchPoint p = _ui->getTouchPoint();
  if (p.x != -1) {
    // Debounce a bit to avoid catching the touch that started the test
    static unsigned long lastTouch = 0;
    if (millis() - lastTouch > 500) {
      endGraphicTest();
      _currentMode = MODE_UTILITY;
      _ui->setTitle("UTILITY");
      loadSettings();
      _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
      _ui->drawStatusBar(true);
      drawList(0, true);
    }
    lastTouch = millis();
  }
}

void SettingsScreen::endGraphicTest() {
  // Cleanup if needed
}

void SettingsScreen::runBenchmark() {
  TFT_eSPI *tft = _ui->getTft();
  unsigned long total = 0;
  int xPos = 5;
  int yPos = 28;
  int yStep = 16; // Tighter spacing to fit 14 items + header

  tft->fillScreen(TFT_BLACK);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextFont(2);

  // Headers
  tft->setTextDatum(TL_DATUM);
  tft->setTextColor(TFT_GREEN, TFT_BLACK);
  tft->drawString("Benchmark", xPos, STATUS_BAR_HEIGHT + 8);
  tft->setTextDatum(TR_DATUM);
  tft->drawString("microseconds", SCREEN_WIDTH - 5, STATUS_BAR_HEIGHT + 8);
  tft->drawLine(0, 25, SCREEN_WIDTH, 25, TFT_DARKGREY);

  tft->setTextColor(TFT_WHITE, TFT_BLACK);

  // Helper lambda to run test and print result
  auto runAndPrint = [&](const char *name, unsigned long time) {
    tft->setTextDatum(TL_DATUM);
    tft->drawString(name, xPos, yPos);
    tft->setTextDatum(TR_DATUM);
    tft->drawString(String(time), SCREEN_WIDTH - 5, yPos);
    yPos += yStep;
    total += time;
    delay(500); // Slower animation per user request
  };

  runAndPrint("Screen fill", testFillScreen());
  runAndPrint("Text", testText());
  runAndPrint("Lines", testLines(TFT_CYAN));
  runAndPrint("Horiz/Vert Lines", testFastLines(TFT_RED, TFT_BLUE));
  runAndPrint("Rectangles", testRects(TFT_GREEN));
  runAndPrint("Rectangles-filled", testFilledRects(TFT_YELLOW, TFT_MAGENTA));
  runAndPrint("Circles", testCircles(10, TFT_WHITE));
  runAndPrint("Circles-filled", testFilledCircles(10, TFT_MAGENTA));
  runAndPrint("Triangles", testTriangles());
  runAndPrint("Triangles-filled", testFilledTriangles());
  runAndPrint("Rounded rects", testRoundRects());
  runAndPrint("Rounded rects-fill", testFilledRoundRects());

  // Total
  yPos += 5;
  tft->drawLine(0, yPos, SCREEN_WIDTH, yPos, TFT_DARKGREY);
  yPos += 5;
  tft->setTextColor(TFT_YELLOW, TFT_BLACK);
  tft->setTextDatum(TL_DATUM);
  tft->drawString("Total", xPos, yPos);
  tft->setTextDatum(TR_DATUM);
  tft->drawString(String(total), SCREEN_WIDTH - 5, yPos);

  // Memory Debug Info (Moved from About)
  yPos += 20;
  uint32_t freeHeap = ESP.getFreeHeap() / 1024;
  uint32_t minHeap = ESP.getMinFreeHeap() / 1024;

  tft->setTextColor(TFT_CYAN, TFT_BLACK);
  tft->setTextDatum(TL_DATUM);
  tft->drawString("Free Heap:", xPos, yPos);
  tft->setTextDatum(TR_DATUM);
  tft->drawString(String(freeHeap) + " KB", SCREEN_WIDTH - 5, yPos);

  yPos += 16;
  tft->setTextDatum(TL_DATUM);
  tft->drawString("Min Heap:", xPos, yPos);
  tft->setTextDatum(TR_DATUM);
  tft->drawString(String(minHeap) + " KB", SCREEN_WIDTH - 5, yPos);
}

unsigned long SettingsScreen::testFillScreen() {
  TFT_eSPI *tft = _ui->getTft();
  unsigned long start = micros();
  tft->fillScreen(TFT_BLACK);
  tft->fillScreen(TFT_RED);
  tft->fillScreen(TFT_GREEN);
  tft->fillScreen(TFT_BLUE);
  tft->fillScreen(TFT_BLACK);
  return micros() - start;
}

unsigned long SettingsScreen::testText() {
  TFT_eSPI *tft = _ui->getTft();
  tft->fillScreen(TFT_BLACK);
  unsigned long start = micros();
  tft->setTextColor(TFT_WHITE);
  tft->setTextDatum(TL_DATUM);
  tft->setTextFont(2);
  for (int i = 0; i < 500; i++) {
    tft->drawString("Hello World", 0, STATUS_BAR_HEIGHT + 8);
    tft->drawNumber(i, 100, 100);
  }
  return micros() - start;
}

unsigned long SettingsScreen::testLines(uint16_t color) {
  TFT_eSPI *tft = _ui->getTft();
  unsigned long start;
  int x1, y1, x2, y2, w = tft->width(), h = tft->height();
  tft->fillScreen(TFT_BLACK);

  x1 = y1 = 0;
  y2 = h - 1;
  start = micros();
  for (x2 = 0; x2 < w; x2 += 6)
    tft->drawLine(x1, y1, x2, y2, color);
  x2 = w - 1;
  for (y2 = 0; y2 < h; y2 += 6)
    tft->drawLine(x1, y1, x2, y2, color);

  return micros() - start;
}

unsigned long SettingsScreen::testFastLines(uint16_t color1, uint16_t color2) {
  TFT_eSPI *tft = _ui->getTft();
  unsigned long start;
  int w = tft->width(), h = tft->height();
  tft->fillScreen(TFT_BLACK);
  start = micros();
  for (int y = 0; y < h; y += 5)
    tft->drawFastHLine(0, y, w, color1);
  for (int x = 0; x < w; x += 5)
    tft->drawFastVLine(x, 0, h, color2);
  return micros() - start;
}

unsigned long SettingsScreen::testRects(uint16_t color) {
  TFT_eSPI *tft = _ui->getTft();
  unsigned long start;
  int w = tft->width(), h = tft->height();
  tft->fillScreen(TFT_BLACK);
  start = micros();
  for (int x = 2; x < w; x += 6) {
    if (x + 2 > h)
      break;
    tft->drawRect(w / 2 - x / 2, h / 2 - x / 2, x, x, color);
  }
  return micros() - start;
}

unsigned long SettingsScreen::testFilledRects(uint16_t color1,
                                              uint16_t color2) {
  TFT_eSPI *tft = _ui->getTft();
  unsigned long start;
  int w = tft->width(), h = tft->height();
  tft->fillScreen(TFT_BLACK);
  start = micros();
  for (int x = w - 1; x > 6; x -= 6) {
    if (x > h)
      continue;
    tft->fillRect(w / 2 - x / 2, h / 2 - x / 2, x, x, color1);
    tft->drawRect(w / 2 - x / 2, h / 2 - x / 2, x, x, color2);
  }
  return micros() - start;
}

unsigned long SettingsScreen::testFilledCircles(uint8_t radius,
                                                uint16_t color) {
  TFT_eSPI *tft = _ui->getTft();
  unsigned long start;
  int w = tft->width(), h = tft->height();
  tft->fillScreen(TFT_BLACK);
  start = micros();
  for (int x = radius; x < w; x += radius * 2) {
    for (int y = radius; y < h; y += radius * 2) {
      tft->fillCircle(x, y, radius, color);
    }
  }
  return micros() - start;
}

unsigned long SettingsScreen::testCircles(uint8_t radius, uint16_t color) {
  TFT_eSPI *tft = _ui->getTft();
  unsigned long start;
  int w = tft->width(), h = tft->height();
  tft->fillScreen(TFT_BLACK);
  start = micros();
  for (int x = 0; x < w + radius; x += radius * 2) {
    for (int y = 0; y < h + radius; y += radius * 2) {
      tft->drawCircle(x, y, radius, color);
    }
  }
  return micros() - start;
}

unsigned long SettingsScreen::testTriangles() {
  TFT_eSPI *tft = _ui->getTft();
  unsigned long start;
  int w = tft->width(), h = tft->height();
  tft->fillScreen(TFT_BLACK);
  start = micros();
  for (int i = 0; i < w / 2; i += 5) {
    tft->drawTriangle(w / 2, h / 2 - i, w / 2 - i, h / 2 + i, w / 2 + i,
                      h / 2 + i, TFT_CYAN);
  }
  return micros() - start;
}

unsigned long SettingsScreen::testFilledTriangles() {
  TFT_eSPI *tft = _ui->getTft();
  unsigned long start;
  int w = tft->width(), h = tft->height();
  tft->fillScreen(TFT_BLACK);
  start = micros();
  for (int i = w / 2; i > 10; i -= 5) {
    tft->fillTriangle(w / 2, h / 2 - i, w / 2 - i, h / 2 + i, w / 2 + i,
                      h / 2 + i, tft->color565(0, i, i));
  }
  return micros() - start;
}

unsigned long SettingsScreen::testRoundRects() {
  TFT_eSPI *tft = _ui->getTft();
  unsigned long start;
  int w = tft->width(), h = tft->height();
  tft->fillScreen(TFT_BLACK);
  start = micros();
  for (int i = 0; i < w / 2 - 10; i += 6) {
    if (i * 2 + 10 > h)
      break;
    tft->drawRoundRect(i, i, w - 2 * i, h - 2 * i, 10, TFT_RED);
  }
  return micros() - start;
}

unsigned long SettingsScreen::testFilledRoundRects() {
  TFT_eSPI *tft = _ui->getTft();
  unsigned long start;
  int w = tft->width(), h = tft->height();
  tft->fillScreen(TFT_BLACK);
  start = micros();
  for (int i = 0; i < w / 2 - 10; i += 6) {
    if (i * 2 + 10 > h)
      break;
    tft->fillRoundRect(i, i, w - 2 * i, h - 2 * i, 10, tft->color565(i, 0, i));
  }
  return micros() - start;
}

void SettingsScreen::drawIMUCalibration(bool force) {
  TFT_eSPI *tft = _ui->getTft();

  if (force) {
    tft->fillScreen(TFT_BLACK);
    _ui->drawStatusBar(true);

    // Top-Left Back Triangle (Standardized position)
    tft->fillTriangle(15, 40, 30, 30, 30, 50, TFT_BLUE);

    // Title / Instructions (Increased readability)
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setTextDatum(TC_DATUM);
    tft->setTextFont(2); // Changed from Org_01 (Too small)
    tft->setTextSize(1);
    tft->drawString("Keep device stationary and level", SCREEN_WIDTH / 2, 40);
    tft->drawString("for accurate G-force calibration", SCREEN_WIDTH / 2, 60);

    // Crosshair Lines (Smaller)
    int cX = SCREEN_WIDTH / 2;
    int cY = 140; // Shifted up
    int len = 45; // Smaller (was 60)
    tft->drawFastHLine(cX - len, cY, 2 * len, TFT_DARKGREY);
    tft->drawFastVLine(cX, cY - len, 2 * len, TFT_DARKGREY);

    // Calibrate Button (Smaller)
    int btnW = 300; // was 440
    int btnH = 45;  // was 60
    int btnX = (SCREEN_WIDTH - btnW) / 2;
    int btnY = SCREEN_HEIGHT - btnH - 15;
    tft->fillRoundRect(btnX, btnY, btnW, btnH, 6, TFT_WHITE);
    tft->setTextColor(TFT_BLACK, TFT_WHITE);
    tft->setTextFont(2); // Smaller font for smaller button
    tft->setTextSize(1);
    tft->setTextDatum(MC_DATUM); // Ensure both Horizontal and Vertical center
    tft->drawString("Calibrate", SCREEN_WIDTH / 2, btnY + btnH / 2);
    tft->setTextDatum(TC_DATUM); // Reset for other drawings
  }

  // Real-time Data Rates (Limiting to 10Hz approx)
  static unsigned long lastUpdate = 0;
  if (!force && millis() - lastUpdate < 100)
    return;
  lastUpdate = millis();

  float ax = imuManager.getAccX();
  float ay = imuManager.getAccY();
  float az = imuManager.getAccZ();

  // 1. Crosshair Dot (Green)
  int cX = SCREEN_WIDTH / 2;
  int cY = 140;
  static int lastDotX = -1, lastDotY = -1;

  // Clear previous dot position by redrawing axes
  if (lastDotX != -1) {
    tft->fillRect(lastDotX - 4, lastDotY - 4, 8, 8, TFT_BLACK);
    // Redraw intersecting lines
    int len = 45;
    tft->drawFastHLine(cX - len, cY, 2 * len, TFT_DARKGREY);
    tft->drawFastVLine(cX, cY - len, 2 * len, TFT_DARKGREY);
  }

  // Scale: Smaller scale factor (35.0) for smaller UI
  int dotX = cX - (int)(ax * 35.0);
  int dotY = cY + (int)(ay * 35.0); // Inverted sign as requested

  // Constrain to crosshair area
  dotX = constrain(dotX, cX - 40, cX + 40);
  dotY = constrain(dotY, cY - 40, cY + 40);

  tft->fillRect(dotX - 3, dotY - 3, 7, 7, TFT_GREEN); // Smaller dot
  lastDotX = dotX;
  lastDotY = dotY;

  // 2. Data Values (Horizontal layout below crosshair to save space)
  int dataY = 220;
  int spacing = 140;

  tft->setTextFont(2);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextDatum(TC_DATUM);

  auto drawVal = [&](const char *label, float val, int x) {
    tft->drawString(String(label) + ": ", x - 25, dataY);
    tft->fillRect(x + 10, dataY - 5, 60, 20, TFT_BLACK);
    tft->drawString(String(val, 2), x + 40, dataY);
  };

  drawVal("F/B", ay, 100);
  drawVal("L/R", ax, 240);
  drawVal("U/D", az, 380);

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}
void SettingsScreen::drawAccountLogin(bool fullRedraw, char highlightChar) {
  TFT_eSPI *tft = _ui->getTft();

  if (fullRedraw) {
    tft->fillScreen(_ui->getBackgroundColor());
    _ui->drawStatusBar(true);
    tft->drawFastHLine(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, COLOR_SECONDARY);

    // Nav Buttons (Blue Triangles)
    tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                      SCREEN_HEIGHT - 20, TFT_BLUE);
  }

  String storedUser = syncManager.getStoredUsername();
  if (storedUser.length() > 0) {
    // --- LOGGED IN VIEW (About Style) ---
    int cardX = 15;
    int cardY = 50;
    int cardW = SCREEN_WIDTH - 30;
    int cardH = 220;

    // Navy Background with Grey Border
    tft->fillRoundRect(cardX, cardY, cardW, cardH, 10, 0x10A2);
    tft->drawRoundRect(cardX, cardY, cardW, cardH, 10, TFT_DARKGREY);

    // Header
    tft->setTextColor(TFT_SKYBLUE, 0x10A2);
    tft->setTextDatum(TL_DATUM);
    tft->setTextFont(2);
    tft->setFreeFont(NULL);
    tft->drawString("ACCOUNT INFO", cardX + 10, cardY + 8);
    tft->drawFastHLine(cardX + 10, cardY + 28, cardW - 20, 0x3186);

    // Logo (Right Side - Scaled Down)
    tft->drawBitmap(160, 70, image_BOLONG_bits, 320, 240, TFT_WHITE);

    // Text Info (Left Side)
    int textX = cardX + 15;
    int textY = cardY + 50;

    tft->setTextColor(TFT_SILVER, 0x10A2);
    tft->setFreeFont(&Org_01);
    tft->drawString("LOGGED IN AS", textX, textY);

    tft->setTextColor(TFT_WHITE, 0x10A2);
    tft->setTextFont(2);
    tft->setFreeFont(NULL);
    textY += 15;
    tft->drawString(storedUser, textX, textY);

    // Remove Account Button (Inside Card)
    int btnW = 160;
    int btnH = 40;
    int btnX = textX;
    int btnY = cardY + cardH - 55;

    tft->fillRoundRect(btnX, btnY, btnW, btnH, 6, TFT_RED);
    tft->setTextColor(TFT_WHITE, TFT_RED);
    tft->setTextDatum(MC_DATUM);
    tft->setTextFont(2);
    tft->drawString("REMOVE ACCOUNT", btnX + btnW / 2, btnY + btnH / 2);
  } else {
    // --- LOGIN FORM VIEW ---
    // Fields
    drawTextField("USERNAME", _accountUser, 32, _isEditingAccUser, false);
    drawTextField("PASSWORD", _accountPass, 65, _isEditingAccPass, true);

    // Keyboard - Only redraw on fullRedraw or if a key is highlighted
    if (fullRedraw || highlightChar != '\0') {
      _keyboard.draw(tft, 100, _isUppercase, highlightChar);
    }
  }
}

void SettingsScreen::drawTextField(const char *label, String value, int y,
                                   bool isActive, bool isPassword) {
  TFT_eSPI *tft = _ui->getTft();

  // Code mirrored from SetupScreen for consistency
  int boxW = 260;
  int boxX = (SCREEN_WIDTH - boxW) / 2;

  // Use Org_01 (Tiny Font) for the Label
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextDatum(BL_DATUM);
  tft->setTextColor(_ui->getTextColor(), _ui->getBackgroundColor());
  tft->drawString(label, boxX, y);

  // Field background
  uint16_t borderColor = isActive ? COLOR_PRIMARY : COLOR_SECONDARY;
  tft->drawRect(boxX, y + 2, boxW, 30, borderColor);
  tft->fillRect(boxX + 1, y + 3, boxW - 2, 28, TFT_DARKGREY);

  // Switch back to Standard Font 1 for Value
  tft->setTextFont(1);
  tft->setTextSize(1);

  // Field value
  String displayValue = value;

  tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft->setTextDatum(ML_DATUM);
  tft->drawString(displayValue, boxX + 5, y + 17);

  // Cursor
  static unsigned long lastBlink = 0;
  static bool cursorOn = true;
  if (millis() - lastBlink > 500) {
    cursorOn = !cursorOn;
    lastBlink = millis();
  }

  if (isActive && cursorOn) {
    int cursorX = boxX + 5 + tft->textWidth(displayValue);
    tft->drawFastVLine(cursorX, y + 8, 18, COLOR_PRIMARY);
  }
}

void SettingsScreen::handleRemoveAccountFlow() {
  TFT_eSPI *tft = _ui->getTft();
  // --- 1. PROCESSING CARD ---
  int cardW = 280;
  int cardH = 100;
  int cardX = (SCREEN_WIDTH - cardW) / 2;
  int cardY = (SCREEN_HEIGHT - cardH) / 2;

  tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, 0x18E3); // Charcoal
  tft->drawRoundRect(cardX, cardY, cardW, cardH, 8, TFT_DARKGREY);

  tft->setTextColor(TFT_WHITE, 0x18E3);
  tft->setTextDatum(MC_DATUM);
  tft->setTextFont(2);
  tft->setTextSize(1);
  tft->drawString("Removing Account...", SCREEN_WIDTH / 2, cardY + 50);

  syncManager.logout();
  sessionManager.wipeSDData();

  delay(1500);

  // --- 2. SUCCESS CARD ---
  tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, 0x18E3);
  tft->drawRoundRect(cardX, cardY, cardW, cardH, 8, TFT_GREEN);

  tft->setTextColor(TFT_GREEN, 0x18E3);
  tft->setTextFont(4);
  tft->drawString("SUCCESS", SCREEN_WIDTH / 2, cardY + 35);

  tft->setTextColor(TFT_SILVER, 0x18E3);
  tft->setTextFont(2);
  tft->drawString("Restarting Device...", SCREEN_WIDTH / 2, cardY + 70);

  delay(3000);
  ESP.restart();
}
