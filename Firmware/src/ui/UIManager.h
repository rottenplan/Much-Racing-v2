#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "../config.h"
#include <TFT_eSPI.h>

// Forward declaration
class UIManager;
class UIManager;
class StatusBar;
class SplashScreen;
class RaceScreen;

enum ScreenType {
  SCREEN_SPLASH,
  SCREEN_SETUP,
  SCREEN_MENU,
  SCREEN_LAP_TIMER,
  SCREEN_DRAG_METER,
  SCREEN_HISTORY,
  SCREEN_SETTINGS,
  SCREEN_TIME_SETTINGS,
  SCREEN_AUTO_OFF,
  SCREEN_RPM_SENSOR,
  SCREEN_SPEEDOMETER,
  SCREEN_GPS_STATUS,
  SCREEN_SYNCHRONIZE,
  SCREEN_GNSS_LOG,
  SCREEN_WEB_SERVER,
  SCREEN_GPS_DEBUG,   // GPS Debug screen
  SCREEN_TOUCH_DEBUG, // Touch Debug screen
  SCREEN_SD_TEST,     // Dedicated SD Test diagnostics
  SCREEN_RACE,        // New isolated Race Dashboard
  SCREEN_NAVIGATION   // BT turn-by-turn navigation
};

// ...

class UserScreen {
public:
  virtual void begin(UIManager *ui) = 0;
  virtual void onShow() = 0;
  virtual void onHide() {}
  virtual void update() = 0;
};

// Include the Touch class declaration (using forward decl if possible to avoid
// circular dep)
#include "TAMC_GT911.h"

// ... existing code ...

class UIManager {
public:
  UIManager(TFT_eSPI *tft);
  void begin();
  void update();
  void switchScreen(ScreenType type);
  void drawStatusBar(bool force = false); // Added force parameter
  void setTitle(String title) { _screenTitle = title; }
  void setAutoOff(unsigned long ms);
  void setBrightness(int level);
  void wakeUp();
  void checkSleep();
  void updateInteraction();

  // UI Helper
  void showToast(String message, int duration = 2000);
  void drawCarbonBackground(int x, int y, int w, int h);

  // Touch Handling
  void setTouch(TAMC_GT911 *touch) { _touch = touch; }
  TAMC_GT911 *getTouch() { return _touch; }

  struct TouchPoint {
    int x;
    int y;
  };
  TouchPoint getTouchPoint();

  TFT_eSPI *getTft() { return _tft; }
  class SplashScreen *getSplashScreen() {
    return (SplashScreen *)_splashScreen;
  }

  // Theme Support
  void setDarkMode(bool enable);
  bool isDarkMode() { return _isDarkMode; }
  uint16_t getBackgroundColor();
  uint16_t getTextColor();
  uint16_t getSecondaryColor();

  // Debug Touch
  void setDebugTouch(bool enable) { _debugTouchEnabled = enable; }
  bool isDebugTouchEnabled() { return _debugTouchEnabled; }

  String getScreenTitle() { return _screenTitle; }
  class HistoryScreen *getHistoryScreen() {
    return (HistoryScreen *)_historyScreen;
  }
  class RaceScreen *getRaceScreen() { return (RaceScreen *)_raceScreen; }
  class LapTimerScreen *getLapTimerScreen() {
    return (LapTimerScreen *)_lapTimerScreen;
  }

private:
  TFT_eSPI *_tft;
  TAMC_GT911 *_touch;    // Added touch pointer
  StatusBar *_statusBar; // <--- Module
  UserScreen *_currentScreen;
  ScreenType _currentType;
  bool _isDarkMode;
  // ... existing code ...

  // Screens
  UserScreen *_splashScreen;
  UserScreen *_setupScreen;
  UserScreen *_menuScreen;
  UserScreen *_lapTimerScreen;
  UserScreen *_dragMeterScreen;
  UserScreen *_historyScreen;
  UserScreen *_settingsScreen;
  UserScreen *_timeSettingScreen;
  // UserScreen *_autoOffScreen;
  UserScreen *_rpmSensorScreen;
  UserScreen *_speedometerScreen;
  UserScreen *_gpsStatusScreen;
  UserScreen *_synchronizeScreen;
  UserScreen *_gnssLogScreen;
  UserScreen *_webServerScreen;
  UserScreen *_gpsDebugScreen;   // GPS Debug screen
  UserScreen *_touchDebugScreen; // Touch Debug screen
  UserScreen *_sdTestScreen;     // SD Card Test screen
  UserScreen *_raceScreen;       // Race Dashboard
  UserScreen *_navigationScreen; // BT Navigation

  String _screenTitle; // Added title

  // Speed / Auto-Off
  unsigned long _lastInteractionTime;
  unsigned long _autoOffMs;
  bool _isScreenOff;
  int _currentBrightness; // To restore after sleep

  // Wakeup
  unsigned long _lastTapTime;
  bool _wasTouched;
  unsigned long _lastTouchProcessedTime; // Added for debounce

  // Debug
  bool _debugTouchEnabled;
};

#endif
