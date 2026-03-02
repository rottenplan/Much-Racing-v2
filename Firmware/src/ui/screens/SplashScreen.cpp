#include "SplashScreen.h"
#include "../fonts/Org_01.h"
#include "SplashScreenAssets.h"
#include <Preferences.h>

void SplashScreen::onShow() {
  TFT_eSPI *tft = _ui->getTft();

  // Center bitmap (320x240) on 480x320 screen
  int dx = (SCREEN_WIDTH - 320) / 2;  // 80px offset
  int dy = (SCREEN_HEIGHT - 240) / 2; // 40px offset
  tft->drawBitmap(dx, dy, image_BOLONG_bits, 320, 240, 0xFFFF);

  // Initialize progress bar (thinner: 8px height, longer: 280px width,
  // centered) Position Bar ABOVE Text
  _progress = 0;
  int barX = (SCREEN_WIDTH - 280) / 2;
  int barY = 198 + dy;
  tft->fillRect(barX, barY, _progress, 8, 0xFFFF);

  // Draw "ENGINE STARTING" text centered (smaller font) below bar
  // REMOVED at user request for simultaneous appearance with status
  /*
  tft->setTextColor(0xFFFF);
  tft->setTextSize(FONT_SIZE_SPLASH_TEXT);
  tft->setFreeFont(&Org_01);
  tft->setTextDatum(MC_DATUM);
  int textY = 214 + dy;
  tft->drawString("ENGINE STARTING", SCREEN_WIDTH / 2, textY);
  */

  _lastUpdate = millis();
}

void SplashScreen::update() {
  TFT_eSPI *tft = _ui->getTft();
  int dx = (SCREEN_WIDTH - 320) / 2;
  int dy = (SCREEN_HEIGHT - 240) / 2;
  int barY = 198 + dy;

  // Progress bar animation (thinner: 8px height, longer: 280px width, centered)
  if (_progress < 280) {
    if (millis() - _lastUpdate > 10) {
      _progress += 2;

      int barX = (SCREEN_WIDTH - 280) / 2; // Center the bar
      tft->fillRect(barX, barY, _progress, 8, 0xFFFF);

      _lastUpdate = millis();
    }
  } else {
    // Animasi selesai, tunggu sebentar lalu beralih
    if (millis() - _lastUpdate > 1000) {
      // Check if this is first launch
      Preferences prefs;
      prefs.begin("muchrace", true); // Read-only
      bool setupDone = prefs.getBool("setup_done", false);
      prefs.end();

      if (!setupDone) {
        // First launch - go to setup
        Serial.println("First launch detected, showing setup screen");

        // --- FONT SAFETY ---
        tft->setTextSize(1);
        tft->setFreeFont(NULL);
        tft->setTextFont(1);
        tft->setTextPadding(0);

        _ui->switchScreen(SCREEN_SETUP);
      } else {
        // Normal launch - go to menu

        // --- FONT SAFETY ---
        tft->setTextSize(1);
        tft->setFreeFont(NULL);
        tft->setTextFont(1);
        tft->setTextPadding(0);

        _ui->switchScreen(SCREEN_MENU);
      }
    }
  }
}

void SplashScreen::setLoadingStatus(const char *status) {
  if (!_ui)
    return;
  TFT_eSPI *tft = _ui->getTft();
  int dx = (SCREEN_WIDTH - 320) / 2;
  int dy = (SCREEN_HEIGHT - 240) / 2;

  // Text Y matches onShow position (Below Bar)
  int textY = 214 + dy;
  int textH = 20;

  // Clear previous text area
  // Clear full width to be safe
  tft->fillRect(0, textY - 10, SCREEN_WIDTH, textH, 0x0000);

  // Draw new text
  tft->setTextColor(0xFFFF);
  tft->setTextSize(1); // Keep it small/crisp
  tft->setFreeFont(&Org_01);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(status, SCREEN_WIDTH / 2, textY);
}