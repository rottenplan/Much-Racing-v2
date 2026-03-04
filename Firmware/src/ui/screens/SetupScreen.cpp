#include "SetupScreen.h"
#include "../../config.h"
#include "../../core/SyncManager.h"
#include "../../core/WiFiManager.h"
#include "../fonts/Org_01.h"

extern WiFiManager wifiManager;

// Update onShow to reset new variables
void SetupScreen::onShow() {
  _currentStep = STEP_WELCOME;
  _username = "";
  _wifiSSID = "";
  _wifiPassword = "";
  _isEditingUsername = false;
  _isEditingSSID = false;
  _isEditingPassword = false;
  _cursorVisible = true;
  _lastTouchTime = 0;
  _lastTapY = -1;
  _isUppercase = true;
  _showPassword = false;
  _scanCount = 0;    // Reset
  _scrollOffset = 0; // Reset
  _hasScanned = false;
  _lastWiFiTapIndex = -1;
  _lastWiFiTapTime = 0;
  _lastHighlightedChar = 0;

  drawWelcome();
}

// Update update() to handle new step with Compact Coordinates
void SetupScreen::update() {
  TFT_eSPI *tft = _ui->getTft();
  UIManager::TouchPoint tp = _ui->getTouchPoint();

  // Handle cursor blink
  if (millis() - _lastBlinkTime > 500) {
    _cursorVisible = !_cursorVisible;
    _lastBlinkTime = millis();

    switch (_currentStep) {
    case STEP_ACCOUNT:
      drawTextField("USERNAME", _username, Layout::FIELD1_Y, _isEditingUsername,
                    false);
      drawTextField("PASSWORD", _password, Layout::FIELD2_Y,
                    _isEditingAccountPassword, true);
      break;
    case STEP_WIFI:
      if (_wifiSSID.length() == 0) {
        drawTextField("SSID", _wifiSSID, Layout::FIELD1_Y, _isEditingSSID,
                      false);
        drawTextField("PASSWORD", _wifiPassword, Layout::FIELD2_Y,
                      _isEditingPassword, true);
      } else {
        drawTextField("PASSWORD", _wifiPassword, Layout::FIELD1_Y, true, true);
      }
      break;
    default:
      break;
    }
  }

  // Handle touch
  if (tp.x >= 0 && tp.y >= 0) {
    if (millis() - _lastTouchTime < TOUCH_KEYBOARD_MS)
      return;
    _lastTouchTime = millis();

    switch (_currentStep) {
    case STEP_WELCOME:
      handleWelcomeTouch(tp.x, tp.y);
      break;
    case STEP_ACCOUNT:
      handleAccountTouch(tp.x, tp.y);
      break;
    case STEP_WIFI_SCAN:
      handleWiFiScanTouch(tp.x, tp.y);
      break;
    case STEP_WIFI:
      handleWiFiTouch(tp.x, tp.y);
      break;
    case STEP_COMPLETE:
      handleCompleteTouch(tp.x, tp.y);
      break;
    }
  }
}

// ===== DRAWING METHODS =====

void SetupScreen::drawWelcome() {
  TFT_eSPI *tft = _ui->getTft();
  // Clear entire screen
  tft->fillScreen(_ui->getBackgroundColor());

  tft->setFreeFont(&Org_01);
  tft->setTextSize(2);
  tft->setTextColor(COLOR_PRIMARY, _ui->getBackgroundColor());
  tft->setTextDatum(MC_DATUM);

  // Title
  tft->setTextSize(2);
  tft->setTextColor(_ui->getTextColor(), _ui->getBackgroundColor());
  tft->drawString("WELCOME TO", SCREEN_WIDTH / 2, 75);

  tft->setTextSize(4);
  tft->setTextColor(COLOR_PRIMARY, _ui->getBackgroundColor());
  tft->drawString("MUCH RACING", SCREEN_WIDTH / 2, 105);

  tft->setTextSize(2);
  tft->setTextColor(_ui->getTextColor(), _ui->getBackgroundColor());
  tft->drawString("LET'S GET STARTED", SCREEN_WIDTH / 2, 145);

  // Continue button (larger)
  drawButton("TAP TO BEGIN", SCREEN_WIDTH / 2 - 120, 200, 240, 60, false);

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}

void SetupScreen::drawComplete() {
  TFT_eSPI *tft = _ui->getTft();
  tft->fillScreen(_ui->getBackgroundColor());

  tft->setFreeFont(&Org_01);
  tft->setTextDatum(MC_DATUM);

  // Line 1: "SETUP" → size 2, text color (like "WELCOME TO")
  tft->setTextSize(2);
  tft->setTextColor(_ui->getTextColor(), _ui->getBackgroundColor());
  tft->drawString("SETUP", SCREEN_WIDTH / 2, 75);

  // Line 2: "COMPLETE!" → size 4, PRIMARY color (like "MUCH RACING")
  tft->setTextSize(4);
  tft->setTextColor(COLOR_PRIMARY, _ui->getBackgroundColor());
  tft->drawString("COMPLETE!", SCREEN_WIDTH / 2, 108);

  // Line 3: "WELCOME [user]!" → size 2, text color (like "LET'S GET STARTED")
  tft->setTextSize(2);
  tft->setTextColor(_ui->getTextColor(), _ui->getBackgroundColor());
  if (_username.length() > 0) {
    String msg = "WELCOME " + _username + "!";
    tft->drawString(msg, SCREEN_WIDTH / 2, 150);
  } else {
    tft->drawString("LET'S RACE!", SCREEN_WIDTH / 2, 150);
  }

  // Button
  drawButton("START RACING", SCREEN_WIDTH / 2 - 100, 205, 200, 50, false);

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}

void SetupScreen::drawSetupHeader(const char *title, const char *leftBtn,
                                  const char *rightBtn, bool rightEnabled) {
  TFT_eSPI *tft = _ui->getTft();
  tft->fillScreen(_ui->getBackgroundColor());

  // Header Style
  tft->setTextFont(1);
  tft->setTextSize(1);
  tft->setTextColor(COLOR_PRIMARY, _ui->getBackgroundColor());
  tft->setTextDatum(TC_DATUM);
  tft->drawString(title, SCREEN_WIDTH / 2, Layout::HEADER_Y);

  // Nav Buttons
  if (leftBtn) {
    drawButton(leftBtn, 5, Layout::BUTTON_Y, 60, 25, false, 1);
  }
  if (rightBtn) {
    drawButton(rightBtn, SCREEN_WIDTH - 65, Layout::BUTTON_Y, 60, 25,
               rightEnabled, 1);
  }
}

// Updated Account Setup (Compact Layout)
void SetupScreen::drawAccountSetup(bool fullRedraw, char highlightChar,
                                   char partialRedrawKey) {
  TFT_eSPI *tft = _ui->getTft();
  if (fullRedraw) {
    drawSetupHeader("ACCOUNT SETUP", NULL, "NEXT",
                    (_username.length() > 0 && _password.length() > 0));
  } else {
    // Partial redraw - update Next button status in header
    tft->setTextFont(1);
    tft->setTextSize(1);
    drawButton("NEXT", SCREEN_WIDTH - 65, Layout::BUTTON_Y, 60, 25,
               (_username.length() > 0 && _password.length() > 0), 1);
  }

  // Ensure one field is active by default
  if (!_isEditingUsername && !_isEditingAccountPassword) {
    _isEditingUsername = true;
  }

  // Dual Field Layout
  drawTextField("USERNAME", _username, Layout::FIELD1_Y, _isEditingUsername,
                false);
  drawTextField("PASSWORD", _password, Layout::FIELD2_Y,
                _isEditingAccountPassword, true);

  // Keyboard (Y=125)
  if (_isEditingUsername || _isEditingAccountPassword) {
    if (fullRedraw || partialRedrawKey != '\0') {
      drawKeyboard(Layout::KEYBOARD_Y, _isEditingAccountPassword, highlightChar,
                   partialRedrawKey);
    }
  }
}

void SetupScreen::drawTextField(const char *label, String value, int y,
                                bool isActive, bool isPassword) {
  TFT_eSPI *tft = _ui->getTft();

  int boxW = 260;
  if (boxW > SCREEN_WIDTH - 20)
    boxW = SCREEN_WIDTH - 20;
  int boxX = (SCREEN_WIDTH - boxW) / 2;
  int boxH = 28;
  int boxY = y + 13;

  // --- LABEL ---
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextDatum(TL_DATUM);
  tft->setTextColor(_ui->getTextColor(), _ui->getBackgroundColor());
  tft->setTextPadding(boxW);
  tft->drawString(label, boxX, y + 1);
  tft->setTextPadding(0);

  // --- BOX: border + full interior fill (DARKGREY matches text bg) ---
  uint16_t borderColor = isActive ? COLOR_PRIMARY : COLOR_SECONDARY;
  tft->drawRect(boxX, boxY, boxW, boxH, borderColor);
  // Fill interior exactly inside border (1px inset): matches border precisely
  tft->fillRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, TFT_DARKGREY);

  // --- VALUE TEXT ---
  String displayValue = value;

  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextSize(1);
  tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft->setTextDatum(ML_DATUM);
  // Padding covers exactly the interior width minus left margin (5px)
  tft->setTextPadding(boxW - 2 - 5);
  tft->drawString(displayValue, boxX + 5, boxY + boxH / 2);
  tft->setTextPadding(0);

  // --- CURSOR ---
  int cursorX = boxX + 5 + tft->textWidth(displayValue);
  if (isActive && _cursorVisible) {
    tft->drawFastVLine(cursorX, boxY + 4, boxH - 8, COLOR_PRIMARY);
  } else {
    tft->drawFastVLine(cursorX, boxY + 4, boxH - 8, TFT_DARKGREY);
  }
}

void SetupScreen::drawButton(const char *label, int x, int y, int w, int h,
                             bool isHighlighted, int fontSize) {
  TFT_eSPI *tft = _ui->getTft();

  uint16_t bgColor = isHighlighted ? COLOR_PRIMARY : _ui->getBackgroundColor();
  uint16_t borderColor = isHighlighted ? COLOR_PRIMARY : COLOR_SECONDARY;
  uint16_t textColor =
      isHighlighted ? _ui->getBackgroundColor() : _ui->getTextColor();

  tft->fillRect(x, y, w, h, bgColor);
  tft->drawRect(x, y, w, h, borderColor);

  tft->setTextSize(fontSize);
  tft->setTextDatum(MC_DATUM);
  tft->setTextColor(textColor, bgColor);
  tft->drawString(label, x + w / 2, y + h / 2);
}

void SetupScreen::drawKeyboard(int y, bool isPassword, char highlightChar,
                               char partialRedrawKey) {
  _keyboard.draw(_ui->getTft(), y, _isUppercase, highlightChar,
                 partialRedrawKey);
}

// New WiFi Scan Screen
void SetupScreen::drawWiFiScan() {
  drawSetupHeader("SELECT WIFI NETWORK", "SKIP", "SCAN");

  TFT_eSPI *tft = _ui->getTft();

  if (!_hasScanned) {
    // Draw Scanning Modal Box as requested
    int modalW = 200;
    int modalH = 80;
    int modalX = (SCREEN_WIDTH - modalW) / 2;
    int modalY = (SCREEN_HEIGHT - modalH) / 2;

    tft->fillRoundRect(modalX, modalY, modalW, modalH, 8,
                       _ui->getBackgroundColor());
    tft->drawRoundRect(modalX, modalY, modalW, modalH, 8, COLOR_PRIMARY);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(TFT_WHITE, _ui->getBackgroundColor());
    tft->setTextFont(2);
    tft->drawString("SCANNING...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);

    // Perform Scan (Blocking)
    _scanCount = wifiManager.scanNetworks();
    _hasScanned = true;

    // Redraw screen content after scan
    tft->fillScreen(_ui->getBackgroundColor());
    drawWiFiScan();
    return;
  }

  // List Networks
  int startY = 45;
  int itemH = 36;
  int limit = 6;
  for (int i = 0; i < _scanCount && i < limit; i++) {
    int y = startY + (i * itemH);
    uint16_t color =
        (i == _lastWiFiTapIndex) ? COLOR_HIGHLIGHT : COLOR_SECONDARY;
    tft->drawRoundRect(10, y, SCREEN_WIDTH - 20, 32, 4, color);

    String ssid = wifiManager.getSSID(i);
    if (ssid.length() > 22)
      ssid = ssid.substring(0, 19) + "...";

    tft->setTextFont(2);
    tft->setTextColor(TFT_WHITE, _ui->getBackgroundColor());
    tft->setTextDatum(ML_DATUM);
    tft->drawString(ssid, 20, y + 16);

    int rssi = wifiManager.getRSSI(i);
    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(COLOR_ACCENT, _ui->getBackgroundColor());
    tft->drawString(String(rssi) + " dB", SCREEN_WIDTH - 20, y + 16);
  }

  // Custom Manual Entry Option
  int visibleCount = (_scanCount > limit) ? limit : _scanCount;
  int my = startY + visibleCount * itemH;
  tft->drawRoundRect(10, my, SCREEN_WIDTH - 20, 32, 4, COLOR_PRIMARY);
  tft->setTextFont(2);
  tft->setTextColor(COLOR_PRIMARY, _ui->getBackgroundColor());
  tft->setTextDatum(MC_DATUM);
  tft->drawString("MANUAL SETUP", SCREEN_WIDTH / 2, my + 16);

  // MANUAL SETUP button is drawn above. Skip button removed per user request.
}

void SetupScreen::drawWiFiSetup(bool fullRedraw, char highlightChar,
                                char partialRedrawKey) {
  TFT_eSPI *tft = _ui->getTft();
  if (fullRedraw) {
    String title = (_wifiSSID.length() > 0) ? ("WIFI: " + _wifiSSID)
                                            : "WIFI CONFIGURATION";
    drawSetupHeader(title.c_str(), "BACK", "DONE", (_wifiSSID.length() > 0));
  } else {
    // Update DONE button state
    tft->setTextFont(1);
    tft->setTextSize(1);
    drawButton("DONE", SCREEN_WIDTH - 65, Layout::BUTTON_Y, 60, 25,
               (_wifiSSID.length() > 0), 1);
  }

  // Ensure one field is active by default (AUTO-FOCUS Fix)
  if (!_isEditingSSID && !_isEditingPassword) {
    if (_wifiSSID.length() == 0)
      _isEditingSSID = true;
    else
      _isEditingPassword = true;
  }

  // Redundant fillRect removed as it clobbers shifted labels

  if (_wifiSSID.length() == 0) {
    // Manual entry mode
    drawTextField("SSID", _wifiSSID, Layout::FIELD1_Y, _isEditingSSID, false);
    drawTextField("PASSWORD", _wifiPassword, Layout::FIELD2_Y,
                  _isEditingPassword, true);
  } else {
    // Single password field mode (SSID already selected)
    drawTextField("PASSWORD", _wifiPassword, Layout::FIELD1_Y, true, true);
  }

  if (fullRedraw || partialRedrawKey != '\0') {
    drawKeyboard(Layout::KEYBOARD_Y, true, highlightChar, partialRedrawKey);
  }

  // Unified Footer Triangle as requested ("keyboard di atas back button")
  // REMOVED per user request
  // tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
  //                   SCREEN_HEIGHT - 20, TFT_BLUE);
}

// ===== TOUCH HANDLERS =====

void SetupScreen::handleWelcomeTouch(int x, int y) {
  // Check if "START RACING" button was pressed
  // Button is 200x50 at position (SCREEN_WIDTH/2 - 100, 210)
  if (y >= 210 && y <= 260 && x >= SCREEN_WIDTH / 2 - 100 &&
      x <= SCREEN_WIDTH / 2 + 100) {
    nextStep();
  }
}

void SetupScreen::handleWiFiScanTouch(int x, int y) {
  // Rescan (Top Right Button: SCAN)
  if (y >= 5 && y <= 80 && x >= SCREEN_WIDTH - 100) {
    _scanCount = 0;
    _hasScanned = false;
    drawWiFiScan();
    return;
  }

  // List Selection
  int startY = 45; // Match drawWiFiScan
  int itemH = 36;  // Match drawWiFiScan
  int limit = 6;   // Match drawWiFiScan
  for (int i = 0; i < _scanCount && i < limit; i++) {
    int itemY = startY + (i * itemH);
    if (y > itemY && y < itemY + 32) { // Item box height is 32
      // Single Tap to select
      _wifiSSID = wifiManager.getSSID(i);
      _wifiPassword = "";
      _currentStep = STEP_WIFI;
      _isEditingPassword = true; // AUTO-FOCUS on Password
      _isEditingSSID = false;
      drawWiFiSetup(true); // Ensure clean transition
      return;
    }
  }

  // Manual Entry Option
  int visibleCount = (_scanCount > limit) ? limit : _scanCount;
  int manY = startY + visibleCount * itemH;
  if (y > manY && y < manY + 32) {
    _wifiSSID = "";
    _wifiPassword = "";
    _currentStep = STEP_WIFI;
    _isEditingSSID = true; // AUTO-FOCUS on SSID
    _isEditingPassword = false;
    drawWiFiSetup();
    return;
  }

  // SKIP Button handling removed per user request.

  // Consolidated Skip Logic
  if (handleSkipButton(x, y))
    return;
}

// Touch Handlers with seasonal Y coordinates
void SetupScreen::handleWiFiTouch(int x, int y) {
  // BACK Button (Top Left)
  if (x < 80 && y < 50) {
    _currentStep = STEP_WIFI_SCAN;
    _hasScanned = false;
    _scanCount = 0;
    drawWiFiScan();
    return;
  }

  // DONE/CONN Button (Top Right)
  if (y >= Layout::BUTTON_Y && y <= Layout::BUTTON_Y + 30 &&
      x >= SCREEN_WIDTH - 70) {
    if (_wifiSSID.length() > 0) {
      // Connecting logic...
      TFT_eSPI *tft = _ui->getTft();
      int modalW = 240;
      int modalH = 80;
      int modalX = (SCREEN_WIDTH - modalW) / 2;
      int modalY = (SCREEN_HEIGHT - modalH) / 2;

      tft->fillRoundRect(modalX, modalY, modalW, modalH, 8,
                         _ui->getBackgroundColor());
      tft->drawRoundRect(modalX, modalY, modalW, modalH, 8, COLOR_PRIMARY);
      tft->setTextDatum(MC_DATUM);
      tft->setTextColor(TFT_WHITE, _ui->getBackgroundColor());
      tft->setTextSize(2);
      tft->drawString("CONNECTING...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);

      // Debug: Log WiFi credentials before attempting connection
      Serial.println("=== WiFi Connection Attempt ===");
      Serial.print("SSID: ");
      Serial.println(_wifiSSID);
      Serial.print("Password: ");
      Serial.println(_wifiPassword);
      Serial.print("Password length: ");
      Serial.println(_wifiPassword.length());
      Serial.println("===============================");

      bool success =
          wifiManager.connect(_wifiSSID.c_str(), _wifiPassword.c_str());

      if (success) {
        tft->fillRect(modalX + 5, modalY + 5, modalW - 10, modalH - 10,
                      _ui->getBackgroundColor());
        tft->setTextColor(TFT_GREEN, _ui->getBackgroundColor());
        tft->drawString("CONNECTED!", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
        delay(1000);
        nextStep();
      } else {
        tft->fillRect(modalX + 5, modalY + 5, modalW - 10, modalH - 10,
                      _ui->getBackgroundColor());
        tft->setTextColor(TFT_RED, _ui->getBackgroundColor());
        tft->drawString("FAILED!", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
        delay(1500);
        drawWiFiSetup(true);
      }
    }
    return;
  }

  // Check field selection
  // Touch area = label(13px) + box(28px) = 41px → use +42 for safety
  if (_wifiSSID.length() == 0) {
    // Manual Entry
    if (handleFieldSelection(y, Layout::FIELD1_Y, Layout::FIELD1_Y + 42,
                             _isEditingSSID, _isEditingPassword) ||
        handleFieldSelection(y, Layout::FIELD2_Y, Layout::FIELD2_Y + 42,
                             _isEditingPassword, _isEditingSSID)) {
      drawWiFiSetup(false);
      return;
    }
  } else {
    // Selection Mode
    if (handleFieldSelection(y, Layout::FIELD1_Y, Layout::FIELD1_Y + 42,
                             _isEditingPassword, _isEditingSSID)) {
      drawWiFiSetup(false);
      return;
    }
  }

  // Keyboard
  if ((_isEditingSSID || _isEditingPassword) && y >= Layout::KEYBOARD_Y) {
    KeyboardComponent::KeyResult res =
        _keyboard.handleTouch(x, y, Layout::KEYBOARD_Y, _isUppercase);
    String &target = _isEditingSSID ? _wifiSSID : _wifiPassword;
    handleCommonKeyboard(res, target, false);
  }
}

void SetupScreen::handleCompleteTouch(int x, int y) {
  // Button "START RACING" is 200x50 at Y=210
  if (y >= 210 && y <= 260 && x >= SCREEN_WIDTH / 2 - 100 &&
      x <= SCREEN_WIDTH / 2 + 100) {
    saveSetupComplete();
    _ui->switchScreen(SCREEN_MENU);
  }
}

void SetupScreen::handleKeyboardInput(String &target, char key) {
  // KeyboardComponent already sends the correct character case based on
  // _isUppercase No conversion needed here
  target += key;

  // Debug: Log what we're adding
  Serial.print("Keyboard Input: '");
  Serial.print(key);
  Serial.print("' (ASCII: ");
  Serial.print((int)key);
  Serial.print(") Current string: ");
  Serial.println(target);
}

// ===== HELPER FUNCTIONS =====

// Unified keyboard handler - eliminates ~120 lines of duplication
void SetupScreen::handleCommonKeyboard(KeyboardComponent::KeyResult res,
                                       String &target, bool isAccount) {
  if (res.type == KeyboardComponent::KEY_CHAR) {
    char c = res.value;
    // handleKeyboardInput already handles case conversion based on _isUppercase
    // No need for duplicate conversion here
    handleKeyboardInput(target, c);

    // Optimized Redraw
    // 1. Unhighlight previous key directly (much faster than full screen/field
    // redraw)
    if (_lastHighlightedChar != 0 && _lastHighlightedChar != c) {
      _keyboard.draw(_ui->getTft(), Layout::KEYBOARD_Y, _isUppercase, 0,
                     _lastHighlightedChar);
    }

    // 2. Update text field and highlight new key in ONE pass
    if (isAccount)
      drawAccountSetup(false, c, c);
    else
      drawWiFiSetup(false, c, c);

    _lastHighlightedChar = c;

  } else if (res.type == KeyboardComponent::KEY_SHIFT) {
    _isUppercase = !_isUppercase;
    // Full keyboard redraw to show uppercase/lowercase change
    _keyboard.draw(_ui->getTft(), Layout::KEYBOARD_Y, _isUppercase, 0, 0);
  } else if (res.type == KeyboardComponent::KEY_DEL) {
    if (target.length() > 0)
      target.remove(target.length() - 1);
    // DEL key logic similar to char
    char c = 2; // DEL value
    if (_lastHighlightedChar != 0 && _lastHighlightedChar != c) {
      _keyboard.draw(_ui->getTft(), Layout::KEYBOARD_Y, _isUppercase, 0,
                     _lastHighlightedChar);
    }
    if (isAccount)
      drawAccountSetup(false, c, c);
    else
      drawWiFiSetup(false, c, c);
    _lastHighlightedChar = c;

  } else if (res.type == KeyboardComponent::KEY_SPACE) {
    target += " ";
    char c = ' ';
    if (_lastHighlightedChar != 0 && _lastHighlightedChar != c) {
      _keyboard.draw(_ui->getTft(), Layout::KEYBOARD_Y, _isUppercase, 0,
                     _lastHighlightedChar);
    }
    if (isAccount)
      drawAccountSetup(false, c, c);
    else
      drawWiFiSetup(false, c, c);
    _lastHighlightedChar = c;

  } else if (res.type == KeyboardComponent::KEY_OK) {
    // Unhighlight last key if any
    if (_lastHighlightedChar != 0) {
      _keyboard.draw(_ui->getTft(), Layout::KEYBOARD_Y, _isUppercase, 0,
                     _lastHighlightedChar);
      _lastHighlightedChar = 0;
    }

    // OK (Enter) - Logic remains same, but might need redraw
    if (isAccount) {
      if (_isEditingUsername) {
        _isEditingUsername = false;
        _isEditingAccountPassword = true;
        drawAccountSetup(false);
      } else if (_username.length() > 0 && _password.length() > 0) {
        nextStep();
      } else {
        // Blink error?
        drawAccountSetup(false, 3); // Highlight ENT
        delay(100);
        drawAccountSetup(false);
      }
    } else {
      // WiFi Step
      if (_wifiSSID.length() == 0 && _isEditingSSID) {
        _isEditingSSID = false;
        _isEditingPassword = true;
        drawWiFiSetup(false);
      } else {
        _isEditingSSID = false;
        _isEditingPassword = false;
        drawWiFiSetup(false, 3);
        delay(100);
        drawWiFiSetup(false);
      }
    }
  }
}

// Unified field selection - eliminates ~40 lines of duplication
bool SetupScreen::handleFieldSelection(int y, int field1Start, int field1End,
                                       bool &field1Active, bool &field2Active) {
  if (y >= field1Start && y <= field1End) {
    field1Active = true;
    field2Active = false;
    return true;
  }
  return false;
}

bool SetupScreen::handleSkipButton(int x, int y) {
  if (x < 100 && y < 80) {
    _currentStep = STEP_COMPLETE;
    drawComplete();
    return true;
  }
  return false;
}

void SetupScreen::handleAccountTouch(int x, int y) {
  // NEXT Button (Top Right)
  if (y >= Layout::BUTTON_Y && y <= Layout::BUTTON_Y + 30 &&
      x >= SCREEN_WIDTH - 65 && x <= SCREEN_WIDTH - 5) {
    if (_username.length() > 0 && _password.length() > 0) {
      nextStep();
    }
    return;
  }

  // SKIP Button removed for mandatory login

  // Check field selection
  // Touch area = label(13px) + box(28px) = 41px → use +42 for safety
  if (handleFieldSelection(y, Layout::FIELD1_Y, Layout::FIELD1_Y + 42,
                           _isEditingUsername, _isEditingAccountPassword) ||
      handleFieldSelection(y, Layout::FIELD2_Y, Layout::FIELD2_Y + 42,
                           _isEditingAccountPassword, _isEditingUsername)) {
    drawAccountSetup(false);
    return;
  }

  // Keyboard
  if ((_isEditingUsername || _isEditingAccountPassword) &&
      y >= Layout::KEYBOARD_Y) {
    KeyboardComponent::KeyResult res =
        _keyboard.handleTouch(x, y, Layout::KEYBOARD_Y, _isUppercase);
    String &target = _isEditingUsername ? _username : _password;
    handleCommonKeyboard(res, target, true);
  }
}

// Updated nextStep routing
void SetupScreen::nextStep() {
  switch (_currentStep) {
  case STEP_WELCOME:
    _currentStep = STEP_WIFI_SCAN;
    drawWiFiScan();
    break;
  case STEP_ACCOUNT:
    // Enhanced Synchronizing UI (Modal Style)
    {
      TFT_eSPI *tft = _ui->getTft();
      int modalW = 280;
      int modalH = 100;
      int modalX = (SCREEN_WIDTH - modalW) / 2;
      int modalY = (SCREEN_HEIGHT - modalH) / 2;

      // Draw Modal Shadow/Dimming (simple fill)
      tft->fillRoundRect(modalX + 4, modalY + 4, modalW, modalH, 8, TFT_BLACK);
      // Main Box
      tft->fillRoundRect(modalX, modalY, modalW, modalH, 8,
                         _ui->getBackgroundColor());
      tft->drawRoundRect(modalX, modalY, modalW, modalH, 8, COLOR_PRIMARY);

      // Header
      tft->setFreeFont(&Org_01);
      tft->setTextSize(1);
      tft->setTextColor(COLOR_ACCENT, _ui->getBackgroundColor());
      tft->setTextDatum(TC_DATUM);
      tft->drawString("CLOUD SYNC", SCREEN_WIDTH / 2, modalY + 10);

      // Message
      tft->setTextFont(1);
      tft->setTextSize(2);
      tft->setTextColor(TFT_WHITE, _ui->getBackgroundColor());
      tft->setTextDatum(MC_DATUM);
      tft->drawString("Synchronizing...", SCREEN_WIDTH / 2, modalY + 45);

      // Stylized Progress Bar (Static but looks active)
      int barW = 200;
      int barH = 8;
      int barX = (SCREEN_WIDTH - barW) / 2;
      int barY = modalY + 75;
      tft->drawRect(barX, barY, barW, barH, COLOR_SECONDARY);
      tft->fillRect(barX + 2, barY + 2, barW / 2, barH - 4,
                    COLOR_ACCENT); // 50% "fake" progress

      // Force a yield and small delay to ensure UI is drawn before blocking
      // sync
      yield();
      delay(100);

      extern SyncManager syncManager;
      // Perform First Sync
      bool syncSuccess = syncManager.performFirstSync(
          API_URL, _username.c_str(), _password.c_str());

      // Update Result in Modal
      tft->fillRect(modalX + 5, modalY + 35, modalW - 10, 30,
                    _ui->getBackgroundColor());
      if (syncSuccess) {
        tft->setTextColor(TFT_GREEN, _ui->getBackgroundColor());
        tft->drawString("SYNC SUCCESS!", SCREEN_WIDTH / 2, modalY + 45);
        tft->fillRect(barX + 2, barY + 2, barW - 4, barH - 4,
                      TFT_GREEN); // Full Green Bar
      } else {
        tft->setTextColor(TFT_RED, _ui->getBackgroundColor());
        tft->drawString("SYNC FAILED", SCREEN_WIDTH / 2, modalY + 45);
      }
      delay(1500);

      if (syncSuccess) {
        // Go to Complete
        _currentStep = STEP_COMPLETE;
        drawComplete();
      } else {
        // Sync Failed - Stay on Account Screen and redraw
        // Modal result remains visible for 1.5s (due to delay(1500) above),
        // then redraw the fields for retry.
        drawAccountSetup(true);
      }
    }
    break;
  case STEP_WIFI_SCAN:
    // Handled in touch, but logic flow: Scan -> Wifi Setup
    break;
  case STEP_WIFI:
    _currentStep = STEP_ACCOUNT;
    drawAccountSetup();
    break;
  default:
    break;
  }
}

void SetupScreen::saveSetupComplete() {
  Preferences prefs;
  prefs.begin("muchrace", false);
  prefs.putBool("setup_done", true);
  prefs.end();

  Serial.println("Setup marked as complete!");
}
