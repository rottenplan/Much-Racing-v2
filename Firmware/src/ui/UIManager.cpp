#include "UIManager.h"
#include "../../config.h"
#include "../core/BatteryManager.h"
#include "../core/GPSManager.h"
#include "../core/WiFiManager.h"
#include "fonts/Org_01.h"
#include <Preferences.h>
#include <WiFi.h>

extern WiFiManager wifiManager;

#include "components/StatusBar.h"

// Sertakan layar (dibuat di langkah berikutnya)
#include "screens/DragMeterScreen.h"
#include "screens/GpsStatusScreen.h"
#include "screens/HistoryScreen.h"
#include "screens/LapTimerScreen.h"
#include "screens/MenuScreen.h"
#include "screens/RaceScreen.h"
#include "screens/RpmSensorScreen.h"
#include "screens/SdTestScreen.h"
#include "screens/SettingsScreen.h"
#include "screens/SetupScreen.h"
#include "screens/SpeedometerScreen.h"
#include "screens/SplashScreen.h"
#include "screens/SynchronizeScreen.h"
#include "screens/TimeSettingScreen.h"
#include "screens/TimeSettingScreen.h" // Removed duplicate as per
// instruction #include "screens/AutoOffScreen.h"
#include "screens/GnssLogScreen.h"
#include "screens/GpsDebugScreen.h"
#include "screens/RpmSensorScreen.h"
#include "screens/TouchDebugScreen.h"
#include "screens/WebServerScreen.h"

UIManager::UIManager(TFT_eSPI *tft) : _tft(tft), _touch(nullptr) {
  _currentScreen = nullptr;
  _isDarkMode = true;         // Default Dark
  _debugTouchEnabled = false; // DISABLED
  _lastTouchProcessedTime = 0;

  _statusBar = new StatusBar(this);
}

void UIManager::begin() {
  // Instansiasi Layar
  _splashScreen = new SplashScreen();
  _setupScreen = new SetupScreen();
  _menuScreen = new MenuScreen();
  _lapTimerScreen = new LapTimerScreen();
  _dragMeterScreen = new DragMeterScreen();
  _historyScreen = new HistoryScreen();
  _settingsScreen = new SettingsScreen();
  _timeSettingScreen = new TimeSettingScreen();
  // _autoOffScreen = new AutoOffScreen();
  _rpmSensorScreen = new RpmSensorScreen();
  _speedometerScreen = new SpeedometerScreen();
  _gpsStatusScreen = new GpsStatusScreen();
  _synchronizeScreen = new SynchronizeScreen();
  _gnssLogScreen = new GnssLogScreen();
  _webServerScreen = new WebServerScreen();
  _gpsDebugScreen = new GpsDebugScreen();
  _touchDebugScreen = new TouchDebugScreen();
  _sdTestScreen = new SdTestScreen();
  _raceScreen = new RaceScreen();

  // Mulai Layar
  _splashScreen->begin(this);
  _setupScreen->begin(this);
  _menuScreen->begin(this);
  _lapTimerScreen->begin(this);
  _dragMeterScreen->begin(this);
  _historyScreen->begin(this);
  _settingsScreen->begin(this);
  _timeSettingScreen->begin(this);
  // _autoOffScreen->begin(this);
  _rpmSensorScreen->begin(this);
  _speedometerScreen->begin(this);
  _gpsStatusScreen->begin(this);
  _synchronizeScreen->begin(this);
  _gnssLogScreen->begin(this);
  _webServerScreen->begin(this);
  _webServerScreen->begin(this);
  _gpsDebugScreen->begin(this);
  _touchDebugScreen->begin(this);
  _raceScreen->begin(this);
  _sdTestScreen->begin(this);

  _statusBar->begin();

  // Initialize Sleep Logic (Standardized with power_save)
  Preferences prefs;
  prefs.begin("laptimer", true);
  int psIdx = prefs.getInt("power_save", 1); // Default 5 min
  prefs.end();

  unsigned long ms = 0;
  switch (psIdx) {
  case 0:
    ms = 60000;
    break; // 1 min
  case 1:
    ms = 300000;
    break; // 5 min
  case 2:
    ms = 600000;
    break; // 10 min
  case 3:
    ms = 1800000;
    break; // 30 min
  case 4:
    ms = 0;
    break; // Never
  }
  setAutoOff(ms);

  // Load brightness setting from prefs
  prefs.begin("laptimer", true);
  int bIdx = prefs.getInt("brightness", 9); // Default 100% (index 9)
  prefs.end();

  // Map 0-9 (10%-100%) to PWM duty cycle 26-255
  int duty = map(bIdx, 0, 9, 26, 255);
  // BACKLIGHT ON DELAYED: Moved to end of begin() after first screen is ready

  _lastInteractionTime = millis();
  _isScreenOff = false;

  _lastTapTime = 0;
  _wasTouched = false;

  // Theme load - Forced Dark Mode per user request
  prefs.begin("laptimer", true);
  _isDarkMode = true; // Always Dark
  prefs.end();

  // Mulai dengan Splash
  switchScreen(SCREEN_SPLASH);

  // NOW TURN ON BACKLIGHT: Screen has been cleared and splash bitmap is ready
  // in controller
  setBrightness(duty);
  // prefs.end();
  // g_lastTimeUpdate = millis();
}

void UIManager::update() {
  // Perbarui logika layar saat ini
  if (_currentScreen) {
    if (!_isScreenOff) {
      _currentScreen->update();
    } else {
      // Handle Wakeup from Off State (Double Tap)
      if (_touch) {
        if (i2cMutex != NULL &&
            xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
          _touch->read();
          xSemaphoreGive(i2cMutex);
        }
        bool touched = _touch->isTouched;

        // Detect Rising Edge (Touch Start)
        if (touched && !_wasTouched) {
          unsigned long now = millis();
          if (now - _lastTapTime < 500) {
            // Double Tap Detected!
            wakeUp();
            _lastTapTime = 0; // Reset
          } else {
            _lastTapTime = now;
          }
        }
        _wasTouched = touched;
      }
    }
  }

  checkSleep();

  // --- PENANGANAN SENTUH ---
  // (Opsional) Kita dapat menangani sentuhan global tertentu di sini, tetapi
  // untuk saat ini membiarkan layar menanganinya memberikan kontrol konteks
  // yang lebih banyak.

  // Periodically update status bar (at least once per second)
  if (_currentType != SCREEN_SPLASH && _currentType != SCREEN_SETUP) {
    if (_statusBar)
      _statusBar->update();
  }
}

UIManager::TouchPoint UIManager::getTouchPoint() {
  TouchPoint p = {-1, -1};
  if (!_touch)
    return p;

  // 1. Initial Read
  if (i2cMutex != NULL &&
      xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    _touch->read();
    xSemaphoreGive(i2cMutex);
  }

  if (_touch->isTouched) {
    int x1 = _touch->points[0].x;
    int y1 = _touch->points[0].y;

    // Reject immediate invalid zeros
    if (x1 == 0 && y1 == 0)
      return p;

    // 2. Stability Delay - Kept as requested by user
    delay(2); // Increased slightly for GT911 stability during bus noise

    // 3. Verification Read
    if (i2cMutex != NULL &&
        xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      _touch->read();
      xSemaphoreGive(i2cMutex);
    }

    if (!_touch->isTouched) {
      // It was a ghost spike!
      return p;
    }

    int x2 = _touch->points[0].x;
    int y2 = _touch->points[0].y;

    // 4. Coordinate Consistency Check
    int dx = x1 - x2;
    int dy = y1 - y2;
    int distSq = dx * dx + dy * dy;

    // Threshold: 30px variance (distSq 900)
    if (distSq > 900) {
      return p; // Inconsistent coordinates -> Noise
    }

    // --- PROCESSING VALID TOUCH ---
    updateInteraction();

    // Calibration Logic (Use the stable second read X2, Y2)
    int pX = x2;
    int pY = y2;

    if (TOUCH_SWAP_XY) {
      int temp = pX;
      pX = pY;
      pY = temp;
    }
    if (TOUCH_INVERT_X) {
      pX = SCREEN_WIDTH - 1 - pX;
    }
    if (TOUCH_INVERT_Y) {
      pY = SCREEN_HEIGHT - 1 - pY;
    }

    p.x = pX;
    p.y = pY;

    if (_debugTouchEnabled) {
      // Serial.printf("TOUCH: Raw[%d,%d] Proc[%d,%d]\n", x2, y2, p.x, p.y);
    }

    // Safety Clamp
    if (p.x < 0)
      p.x = 0;
    if (p.x > SCREEN_WIDTH)
      p.x = SCREEN_WIDTH;
    if (p.y < 0)
      p.y = 0;
    if (p.y > SCREEN_HEIGHT)
      p.y = SCREEN_HEIGHT;

    _wasTouched = true;
    _lastTapTime = millis();
    updateInteraction();

    // Global Debounce (Time between valid clicks)
    unsigned long now = millis();
    if (now - _lastTouchProcessedTime <
        150) { // Increased to 150ms for safer UI
      p.x = -1;
      p.y = -1;
      return p;
    }
    _lastTouchProcessedTime = now;

    if (_debugTouchEnabled) {
      _tft->fillCircle(p.x, p.y, 3, TFT_RED);
    }
  }
  return p;
}

#include "../core/GPSManager.h"
#include "../core/SessionManager.h"

extern GPSManager gpsManager;
extern SessionManager sessionManager;

void UIManager::switchScreen(ScreenType type) {
  // Global Cleanup: Ensure no GPS log callbacks persist across screens
  gpsManager.setRawDataCallback(nullptr);

  if (_currentScreen) {
    _currentScreen->onHide();
  }
  // Tambahkan penundaan 1 detik untuk transisi yang mulus (kecuali dari Splash)
  // Tidak ada penundaan untuk peralihan instan

  _currentType = type;

  // OPTIMIZATION: Only clear the CONTENT area (below Status Bar)
  // This prevents the Status Bar from flickering/disappearing during transition
  // Use a slightly more descriptive color name or method if needed
  _tft->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                 SCREEN_HEIGHT - STATUS_BAR_HEIGHT, getBackgroundColor());

  switch (type) {
  case SCREEN_SPLASH:
    _currentScreen = _splashScreen;
    _screenTitle = "";
    // Splash needs full clear usually? Or just let it handle itself.
    if (type == SCREEN_SPLASH)
      _tft->fillScreen(getBackgroundColor());
    break;
  case SCREEN_SETUP:
    _currentScreen = _setupScreen;
    _screenTitle = "";
    break;
  case SCREEN_MENU:
    _currentScreen = _menuScreen;
    _screenTitle = ""; // Kosong = Tampilkan Waktu
    break;
  case SCREEN_LAP_TIMER:
    _currentScreen = _lapTimerScreen;
    _screenTitle = "LAP TIMER";
    break;
  case SCREEN_RACE:
    _currentScreen = _raceScreen;
    _screenTitle = "RACING";
    break;
  case SCREEN_DRAG_METER:
    _currentScreen = _dragMeterScreen;
    _screenTitle = "DRAG METER";
    break;
  case SCREEN_HISTORY:
    _currentScreen = _historyScreen;
    _screenTitle = "HISTORY";
    break;
  case SCREEN_SETTINGS:
    _currentScreen = _settingsScreen;
    _screenTitle = "SETTINGS";
    break;
  case SCREEN_TIME_SETTINGS:
    _currentScreen = _timeSettingScreen;
    _screenTitle = "CLOCK";
    break;
  // case SCREEN_AUTO_OFF:
  //   _currentScreen = _autoOffScreen;
  //   _screenTitle = "AUTO OFF";
  //   break;
  case SCREEN_SD_TEST:
    _currentScreen = _sdTestScreen;
    _screenTitle = "SD TEST";
    break;
  case SCREEN_RPM_SENSOR:
    _currentScreen = _rpmSensorScreen;
    _screenTitle = "RPM SENSOR";
    break;
  case SCREEN_SPEEDOMETER:
    _currentScreen = _speedometerScreen;
    // setTitle("Speedometer"); // Custom Title handled in screen
    break;
  case SCREEN_GPS_STATUS:
    _currentScreen = _gpsStatusScreen;
    break;
  case SCREEN_SYNCHRONIZE:
    _currentScreen = _synchronizeScreen;
    _screenTitle = "SYNCHRONIZE";
    break;
  case SCREEN_GNSS_LOG:
    _currentScreen = _gnssLogScreen;
    _screenTitle =
        ""; // Empty to show Time, avoids overlap with "GPS LOG" header
    break;
  case SCREEN_WEB_SERVER:
    _currentScreen = _webServerScreen;
    _screenTitle = "OFFLINE SERVER";
    break;
  case SCREEN_GPS_DEBUG:
    _currentScreen = _gpsDebugScreen;
    _screenTitle = "GPS DEBUG";
    break;
  case SCREEN_TOUCH_DEBUG:
    _currentScreen = _touchDebugScreen;
    _screenTitle = "TOUCH DEBUG"; // Assuming a title for consistency
    break;
  }

  if (_currentScreen) {
    _currentScreen->onShow();
  }

  // Gambar Bilah Status setelah onShow agar tidak tertimpa oleh fillScreen di
  // layar
  if (_currentType != SCREEN_SPLASH && _currentType != SCREEN_SETUP) {
    drawStatusBar(true); // Force redraw on switch
  }
}

void UIManager::drawStatusBar(bool force) {
  if (_statusBar) {
    _statusBar->draw(force);
  }
}

// --- Auto Off Logic ---

void UIManager::setAutoOff(unsigned long ms) {
  _autoOffMs = ms;
  updateInteraction();
}

void UIManager::setBrightness(int level) {
  _currentBrightness = level;
  if (!_isScreenOff) {
    ledcWrite(0, _currentBrightness);
  }
}
void UIManager::updateInteraction() {
  _lastInteractionTime = millis();
  // if (_isScreenOff) {
  //     wakeUp();
  // }
}

void UIManager::checkSleep() {
  if (_autoOffMs > 0 && !_isScreenOff) {
    if (millis() - _lastInteractionTime > _autoOffMs) {
      _isScreenOff = true;
      ledcWrite(0, 0);
    }
  }
}

void UIManager::wakeUp() {
  _isScreenOff = false;
  _lastInteractionTime = millis();
  ledcWrite(0, _currentBrightness);
}

void UIManager::showToast(String message, int duration) {
  int w = 180; // Smaller width
  int h = 40;  // Smaller height
  int x = (SCREEN_WIDTH - w) / 2;
  int y =
      45; // Center-ish (Top-Mid) to avoid covering bottom buttons and mid speed

  // Colors
  uint16_t COLOR_CARD = 0x18E3; // Charcoal
  uint16_t COLOR_BORDER = TFT_SILVER;

  // Draw Toast Background (Premium Card Style)
  _tft->fillRoundRect(x, y, w, h, 8, COLOR_CARD);
  _tft->drawRoundRect(x, y, w, h, 8, COLOR_BORDER);

  // Draw Text
  _tft->setTextColor(TFT_WHITE, COLOR_CARD);
  _tft->setFreeFont(&Org_01); // Use styled font
  _tft->setTextSize(1);       // Size 1 is readable for Org_01
  _tft->setTextDatum(MC_DATUM);
  _tft->drawString(message, SCREEN_WIDTH / 2,
                   y + h / 2 - 2); // Slight adjustment for font baseline

  delay(duration);

  // Clear Toast Area
  _tft->fillRect(x, y, w, h, getBackgroundColor());

  // Trigger Static Redraw of current screen to restore any covered elements
  if (_currentScreen) {
    // Some screens might need a full background restore
    // For now, clearing the box is a good start, but a full static redraw is
    // safer
    _currentScreen->onShow();
  }
}

void UIManager::drawCarbonBackground(int x, int y, int w, int h) {
  _tft->fillRect(x, y, w, h, getBackgroundColor());
}

// --- Theme Support ---
void UIManager::setDarkMode(bool enable) {
  if (_isDarkMode == enable)
    return;
  _isDarkMode = enable;

  // Force global redraw if possible, or just let next update handle it?
  // Ideally, we trigger a screen refresh.
  // Simplest: fill screen and re-show current
  if (_currentScreen) {
    _tft->fillScreen(getBackgroundColor());
    drawStatusBar(true);
    _currentScreen->onShow(); // Reload screen to apply new colors
  }
}

uint16_t UIManager::getBackgroundColor() {
  return _isDarkMode ? TFT_BLACK : TFT_WHITE;
}

uint16_t UIManager::getTextColor() {
  return _isDarkMode ? TFT_WHITE : TFT_BLACK;
}

uint16_t UIManager::getSecondaryColor() {
  return _isDarkMode ? TFT_DARKGREY : TFT_LIGHTGREY;
}
