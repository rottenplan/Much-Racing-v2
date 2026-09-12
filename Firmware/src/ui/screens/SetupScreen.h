#ifndef SETUP_SCREEN_H
#define SETUP_SCREEN_H

#include "../UIManager.h"
#include "../components/KeyboardComponent.h"
#include <Arduino.h>
#include <Preferences.h>

class SetupScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void update() override;

private:
  UIManager *_ui;
  KeyboardComponent _keyboard;
  int _cursorPos;
  unsigned long _lastBlinkTime;
  bool _cursorVisible;

  // Setup flow state
  enum SetupStep {
    STEP_WELCOME,
    STEP_ACCOUNT,
    STEP_WIFI_SCAN,
    STEP_WIFI,
    STEP_COMPLETE
  };

  SetupStep _currentStep;

  // Account data
  String _username;
  String _password; // Account Password

  // WiFi data
  String _wifiSSID;
  String _wifiPassword;
  int _scanCount = 0;
  int _scrollOffset = 0; // For scrolling scan results
  bool _hasScanned;

  // Input handling
  bool _isEditingUsername;
  bool _isEditingSSID;
  bool _isEditingPassword;        // For WiFi
  bool _isEditingAccountPassword; // For Account
  bool _isUppercase;
  bool _showPassword;

  // Layout constants
  struct Layout {
    static constexpr int FIELD1_Y = 30;    // label@30, box@43-71
    static constexpr int FIELD2_Y = 74;    // label@74, box@87-115
    static constexpr int KEYBOARD_Y = 118; // starts 3px below box2 bottom
    static constexpr int HEADER_Y = 8;
    static constexpr int BUTTON_Y = 5;
  };

  // Touch state
  // Touch state
  char _lastHighlightedChar = 0;
  unsigned long _lastTouchTime;
  int _lastTapY;
  int _lastWiFiTapIndex = -1;
  unsigned long _lastWiFiTapTime = 0;
  unsigned long _lastBackTapTime = 0;

  // Drawing methods
  void drawWelcome();
  void drawAccountSetup(bool fullRedraw = true, char highlightChar = '\0',
                        char partialRedrawKey = '\0');
  void drawWiFiScan();
  void drawWiFiSetup(bool fullRedraw = true, char highlightChar = '\0',
                     char partialRedrawKey = '\0');
  void drawComplete();

  void drawSetupHeader(const char *title, const char *leftBtn = nullptr,
                       const char *rightBtn = nullptr,
                       bool rightEnabled = true);
  void drawKeyboard(int y, bool isPassword = false, char highlightChar = '\0',
                    char partialRedrawKey = '\0');
  void drawTextField(const char *label, String value, int y, bool isActive,
                     bool isPassword = false);
  void drawButton(const char *label, int x, int y, int w, int h,
                  bool isHighlighted = false, int fontSize = 2);

  // Input methods
  void handleWelcomeTouch(int x, int y);
  void handleAccountTouch(int x, int y);
  void handleWiFiScanTouch(int x, int y);
  void handleWiFiTouch(int x, int y);
  void handleCompleteTouch(int x, int y);

  // Helper methods for reducing redundancy
  void handleCommonKeyboard(KeyboardComponent::KeyResult res, String &target,
                            bool isAccount);
  bool handleFieldSelection(int y, int field1Start, int field1End,
                            bool &field1Active, bool &field2Active);
  bool handleSkipButton(int x, int y);

  void handleKeyboardInput(String &target, char key);

  // Navigation
  void nextStep();
  void saveSetupComplete();
};

#endif
