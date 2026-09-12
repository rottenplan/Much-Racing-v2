#ifndef SETTINGS_SCREEN_H
#define SETTINGS_SCREEN_H

#include "../../core/SessionManager.h" // Added for SDTestResult
#include "../UIManager.h"
#include "../components/KeyboardComponent.h"
#include <Arduino.h>
#include <Preferences.h>

#include <WiFi.h>
#include <vector>

class SettingsScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void update() override;
  void onHide() override;

  enum ScreenMode {
    MODE_NONE,
    MODE_MAIN,
    MODE_GPS,
    MODE_GNSS_CONFIG,
    MODE_DIRTY_SD, // Placeholder or actual
    MODE_DRAG,
    MODE_RPM,
    MODE_ENGINE,
    MODE_CLOCK,
    MODE_WIFI_MENU,
    MODE_WIFI,
    MODE_WIFI_PASS,
    MODE_UTILITY,
    MODE_GRAPHIC_TEST,
    MODE_ABOUT,
    MODE_IMU,
    MODE_IMU_CALIBRATE,
    MODE_LAP_CONFIG, // Beacon Width, etc.
    MODE_ACCOUNT     // Login Screen
  };
  static ScreenMode startMode;

private:
  UIManager *_ui;
  Preferences _prefs;
  KeyboardComponent _keyboard;

  enum SettingType { TYPE_TOGGLE, TYPE_VALUE, TYPE_ACTION };

  struct SettingItem {
    String name;
    SettingType type;
    String key; // Preference key
    std::vector<String> options;
    int currentOptionIdx;
    bool checkState;
  };

  std::vector<SettingItem> _settings;
  int _scrollOffset;
  int _selectedIdx;

  ScreenMode _currentMode;

  // Account Members
  String _accountUser = "";
  String _accountPass = "";
  bool _isEditingAccUser = false;
  bool _isEditingAccPass = false;

  // WiFi Members
  int _scanCount = 0;
  int _selectedWiFiIdx = -1;
  String _targetSSID = "";
  String _enteredPass = "";
  bool _isScanning = false;
  bool _isUppercase = true;
  bool _showPassword = false;
  unsigned long _lastScanAnim = 0;
  int _scanAnimStep = 0;

  int _lastSelectedIdx;
  int _lastWiFiSelectedIdx;

  int _lastTapIdx = -1;
  unsigned long _lastTapTime;
  int _touchStartY = -1;
  unsigned long _lastWiFiTouch = 0;
  unsigned long _lastKeyboardTouch = 0;
  unsigned long _lastGPSUpdate = 0;

  int _lastSats = -1;
  double _lastHdopValue = -1.0;
  double _lastLat = 0;
  double _lastLon = 0;
  bool _lastFixed = false;
  bool _needsSave = false;
  unsigned long _lastChangeTime = 0;

  void loadSettings();
  void saveSetting(int idx);
  void commitSettings();
  void drawList(int scrollOffset, bool force = false);
  void drawHeader(String title, uint16_t backColor = COLOR_HIGHLIGHT);
  void drawGPSStatus(bool force = false);
  void drawIMUCalibration(bool force = false);

  void drawAbout();
  void drawAccountLogin(bool fullRedraw = true, char highlightChar = '\0');
  void drawTextField(const char *label, String value, int y, bool isActive,
                     bool isPassword = false);

  void startGraphicTest();
  void updateGraphicTest();
  void endGraphicTest();

  // Benchmark Helpers
  void runBenchmark();
  unsigned long testFillScreen();
  unsigned long testText();
  unsigned long testLines(uint16_t color);
  unsigned long testFastLines(uint16_t color1, uint16_t color2);
  unsigned long testRects(uint16_t color);
  unsigned long testFilledRects(uint16_t color1, uint16_t color2);
  unsigned long testFilledCircles(uint8_t radius, uint16_t color);
  unsigned long testCircles(uint8_t radius, uint16_t color);
  unsigned long testTriangles();
  unsigned long testFilledTriangles();
  unsigned long testRoundRects();
  unsigned long testFilledRoundRects();

  // WiFi Draw Functions
  void drawWiFiList(bool force = false);
  void drawKeyboard(bool fullRedraw = true, char highlightChar = '\0');
  void connectWiFi();

  void handleTouch(int idx);
  void handleTouchPoint();
  void handleRemoveAccountFlow();
};

#endif
