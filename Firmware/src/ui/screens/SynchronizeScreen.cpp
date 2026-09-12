#include "SynchronizeScreen.h"
#include "../../config.h"
#include "../../core/SyncManager.h"
#include "../../core/WiFiManager.h"
#include "../fonts/Org_01.h"
#include "SettingsScreen.h"

extern SyncManager syncManager;
extern WiFiManager wifiManager;

void SynchronizeScreen::begin(UIManager *ui) { _ui = ui; }

void SynchronizeScreen::onShow() {
  _statusMessage = "READY TO SYNC";
  _detailMessage = "Tap below to update";
  _isSyncing = false;
  _lastSyncSuccess = false;
  _lastTouchTime = 0;

  // Static Draw (Background & Header)
  TFT_eSPI *tft = _ui->getTft();
  _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                            SCREEN_HEIGHT - STATUS_BAR_HEIGHT);

  // Header (Premium Style)
  int headY = STATUS_BAR_HEIGHT;
  // tft->drawFastHLine(0, headY, SCREEN_WIDTH, COLOR_SECONDARY); // Redundant

  tft->setFreeFont(&Org_01);
  tft->setTextSize(2);
  tft->setTextColor(TFT_WHITE, COLOR_BG);
  tft->setTextDatum(TC_DATUM);
  tft->drawString("SYNCHRONIZE", SCREEN_WIDTH / 2, headY + 8);

  drawScreen(true);

  // Back Button (Standardized Blue Triangle)
  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, TFT_BLUE);
}

void SynchronizeScreen::update() {
  UIManager::TouchPoint p = _ui->getTouchPoint();
  if (p.x != -1 && p.y != -1) {
    if (millis() - _lastTouchTime >
        TOUCH_DEBOUNCE_MS) { // Standardized debounce
      _lastTouchTime = millis();
      handleTouch(p.x, p.y);
    }
  }
}

void SynchronizeScreen::drawScreen(bool fullRedraw) {
  TFT_eSPI *tft = _ui->getTft();

  // Colors
  uint16_t L_COLOR_BG = TFT_BLACK;
  uint16_t L_COLOR_CARD = 0x18E3; // Charcoal
  uint16_t L_COLOR_BTN = 0x10A2;  // Slate
  uint16_t L_COLOR_TEXT = TFT_WHITE;
  uint16_t L_COLOR_LABEL = TFT_SILVER;

  // Clear dynamic area (below header)
  // Header ends approx y=40-45. Start clearing at 50.
  if (fullRedraw) {
    tft->fillRect(0, 50, SCREEN_WIDTH, SCREEN_HEIGHT - 50, L_COLOR_BG);
    // Draw Back Button again on full redraw
    tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                      SCREEN_HEIGHT - 20, TFT_BLUE);
  }

  // --- STATUS CARD ---
  int cardW = 400; // Widened for 480 display
  int cardH = 110; // Slightly taller
  int cardX = (SCREEN_WIDTH - cardW) / 2;
  int cardY = 70; // Moved down slightly

  tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, L_COLOR_CARD);
  tft->drawRoundRect(cardX, cardY, cardW, cardH, 8, TFT_DARKGREY);

  // Status Text
  tft->setFreeFont(&Org_01);
  tft->setTextSize(2);
  tft->setTextDatum(MC_DATUM);

  uint16_t statusColor = L_COLOR_TEXT;
  if (_isSyncing) {
    statusColor = TFT_ORANGE;
  } else if (_lastSyncSuccess) {
    statusColor =
        (_statusMessage.indexOf("PARTIAL") != -1) ? TFT_YELLOW : TFT_GREEN;
  } else if (_statusMessage.indexOf("FAILED") != -1 ||
             _statusMessage.indexOf("ERROR") != -1) {
    statusColor = TFT_RED;
  }

  tft->setTextColor(statusColor, L_COLOR_CARD);
  tft->drawString(_statusMessage, SCREEN_WIDTH / 2, cardY + 30);

  // Detail Text
  tft->setTextSize(1);
  tft->setTextColor(L_COLOR_LABEL, L_COLOR_CARD);
  tft->drawString(_detailMessage, SCREEN_WIDTH / 2, cardY + 60);

  // Last Sync Info (Inside Card)
  tft->setTextColor(TFT_DARKGREY, L_COLOR_CARD);
  tft->setTextSize(1);
  String lastSync = "Last Sync: " + syncManager.getLastSyncTime();
  tft->drawString(lastSync, SCREEN_WIDTH / 2, cardY + 85);

  // --- SYNC BUTTON ---
  int btnW = 240; // Widened
  int btnH = 50;  // Slightly taller
  int btnX = (SCREEN_WIDTH - btnW) / 2;
  int btnY = 200; // Moved down to balance

  if (!_isSyncing) {
    tft->fillRoundRect(btnX, btnY, btnW, btnH, 8, L_COLOR_BTN);
    tft->drawRoundRect(btnX, btnY, btnW, btnH, 8, TFT_WHITE);

    tft->setTextColor(TFT_WHITE, L_COLOR_BTN);
    tft->setTextSize(1); // Standard size for button
    tft->setTextDatum(MC_DATUM);
    tft->drawString("START SYNC", SCREEN_WIDTH / 2, btnY + btnH / 2 - 2);
  } else {
    // Syncing Indication (Disabled Look)
    tft->fillRoundRect(btnX, btnY, btnW, btnH, 8,
                       L_COLOR_BG); // Clear background first
    tft->drawRoundRect(btnX, btnY, btnW, btnH, 8, TFT_DARKGREY);
    tft->setTextColor(TFT_DARKGREY, L_COLOR_BG);
    tft->setTextSize(1);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("SYNCING...", SCREEN_WIDTH / 2, btnY + btnH / 2 - 2);
  }

  // Footer info removed (Moved to Status Card)
}

void SynchronizeScreen::handleTouch(int x, int y) {
  // Back Button (Standardized Touch Box x < 80)
  if (x < 80 && y > 240) {
    _ui->switchScreen(SCREEN_MENU);
    return;
  }

  // Sync Button Area (Bottom Position)
  int btnW = 240;
  int btnX = (SCREEN_WIDTH - btnW) / 2;

  if (!_isSyncing && y >= 190 && y <= 260 && x >= btnX && x <= (btnX + btnW)) {
    _isSyncing = true;
    _statusMessage = "CONNECTING...";
    _detailMessage = "Checking WiFi...";
    drawScreen(false); // Partial redraw

    // Use a small delay/yield to let UI update before blocking work
    delay(100);
    performSync();
  }
}

void SynchronizeScreen::performSync() {
  // 1. Check/Connect WiFi
  if (WiFi.status() != WL_CONNECTED) {
    // Try to auto-connect with saved credentials
    if (!wifiManager.tryAutoConnect()) {
      _statusMessage = "WIFI DISCONNECTED";
      _detailMessage = "Setup WiFi in Settings";
      _isSyncing = false;
      drawScreen(false);
      return;
    }
  }

  // 2. Get Credentials
  Preferences prefs;
  prefs.begin("muchrace", true);
  String username = prefs.getString("username", "");
  String password = prefs.getString("password", ""); // Account password
  prefs.end();

  if (username.length() == 0 || password.length() == 0) {
    _statusMessage = "AUTH ERROR";
    _detailMessage = "Update Setup > Account";
    _isSyncing = false;
    drawScreen(false);
    return;
  }

  // 3. Perform Settings Sync
  Serial.println("=== SYNC STAGE 1: Settings ===");
  _statusMessage = "SYNCING SETTINGS...";
  drawScreen(false);
  bool settingsSuccess =
      syncManager.syncSettings(API_URL, username.c_str(), password.c_str());
  Serial.print("Settings Sync Result: ");
  Serial.println(settingsSuccess ? "SUCCESS" : "FAILED");

  // 4. Perform Session Upload
  Serial.println("=== SYNC STAGE 2: Sessions ===");
  _statusMessage = "UPLOADING SESSIONS...";
  drawScreen(false);
  bool uploadSuccess =
      syncManager.uploadSessions(API_URL, username.c_str(), password.c_str());
  Serial.print("Session Upload Result: ");
  Serial.println(uploadSuccess ? "SUCCESS" : "FAILED");

  // 5. Perform GPX Track Upload
  Serial.println("=== SYNC STAGE 3: GPX Tracks ===");
  _statusMessage = "UPLOADING TRACKS...";
  drawScreen(false);
  bool gpxSuccess =
      syncManager.uploadGPXTracks(API_URL, username.c_str(), password.c_str());
  Serial.print("GPX Upload Result: ");
  Serial.println(gpxSuccess ? "SUCCESS" : "FAILED");

  // Final Summary
  Serial.println("=== SYNC SUMMARY ===");
  Serial.print("Settings: ");
  Serial.println(settingsSuccess ? "✓" : "✗");
  Serial.print("Sessions: ");
  Serial.println(uploadSuccess ? "✓" : "✗");
  Serial.print("GPX Tracks: ");
  Serial.println(gpxSuccess ? "✓" : "✗");

  if (settingsSuccess && uploadSuccess && gpxSuccess) {
    _statusMessage = "SYNC COMPLETE";
    _detailMessage = "All Data Updated";
    _lastSyncSuccess = true;
    Serial.println("Final Status: SYNC COMPLETE ✓");
  } else if (settingsSuccess) {
    _statusMessage = "PARTIAL SYNC";
    _lastSyncSuccess = true;
    if (!uploadSuccess && !gpxSuccess) {
      _detailMessage = "Uploads Failed";
    } else if (!uploadSuccess) {
      _detailMessage = "Sessions Incomplete";
    } else {
      _detailMessage = "Tracks Incomplete";
    }
    Serial.println("Final Status: PARTIAL SYNC ⚠");
  } else {
    _statusMessage = "SYNC FAILED";
    _detailMessage = "Check Connection/Auth";
    _lastSyncSuccess = false;
    Serial.println("Final Status: SYNC FAILED ✗");
  }
  Serial.println("====================");

  _isSyncing = false;
  drawScreen(false);
}
