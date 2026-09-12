#include "TimeSettingScreen.h"
#include "../../config.h"
#include "../../core/GPSManager.h"
#include "../fonts/Org_01.h"
#include <Preferences.h>

// Define specific global variables for Time
// Define specific global variables for Time
// g_utcOffset and others are now managed by GPSManager
int g_manualHour = 12;
int g_manualMinute = 0;
// Note: These locals are still used for UI state in TimeSettingScreen,
// but source of truth is GPSManager.
unsigned long g_lastTimeUpdate = 0;

void TimeSettingScreen::onShow() {
  _selectedIdx = -1;
  TFT_eSPI *tft = _ui->getTft();

  // Sync local display vars from GPS Manager source of truth
  extern GPSManager gpsManager;
  int s, d, mo, y;
  gpsManager.getLocalTime(g_manualHour, g_manualMinute, s, d, mo, y);

  drawScreen();
}

void TimeSettingScreen::update() {
  static unsigned long lastTouch = 0;
  UIManager::TouchPoint p = _ui->getTouchPoint();

  // Time Increment Logic (Keep running here too to show live updates)
  // Time Increment Logic (Keep running here too to show live updates)
  if (g_lastTimeUpdate == 0)
    g_lastTimeUpdate = millis();

  if (millis() - g_lastTimeUpdate >=
      1000) { // Check every second for smoother updates
    extern GPSManager gpsManager;
    int s, d, mo, y;
    int oldMin = g_manualMinute;
    gpsManager.getLocalTime(g_manualHour, g_manualMinute, s, d, mo, y);

    if (g_manualMinute != oldMin) {
      drawScreen(); // Redraw only on minute change
    }
    g_lastTimeUpdate = millis();
  }

  if (p.x != -1) {
    if (millis() - lastTouch > TOUCH_DEBOUNCE_MS) {
      lastTouch = millis();

      // 1. Back Button (Standardized Bottom Left Area - 100x80)
      if (p.x < 80 && p.y > 240) {
        _ui->switchScreen(SCREEN_SETTINGS);
        return;
      }

      // Layout Constants Adjusted to prevent overlap with Header (at y=60)
      int cY = 160;  // Moved down from 140 to 160
      int boxH = 50; // Slightly shorter box (was 60)
      int arrowGap = 8;
      int arrowH = 15;
      int boxW = 60; // Narrower (was 70)
      int gap = 30;

      int hCX = (SCREEN_WIDTH / 2) - (boxW / 2) - (gap / 2);
      int mCX = (SCREEN_WIDTH / 2) + (boxW / 2) + (gap / 2);

      int upYBot = cY - (boxH / 2) - arrowGap;
      int upYTop = upYBot - arrowH;
      int dnYTop = cY + (boxH / 2) + arrowGap;
      int dnYBot = dnYTop + arrowH;

      int hitPadding = 20; // Hit box padding

      bool changed = false;

      // Hour UP
      if (p.x > hCX - 40 && p.x < hCX + 40 && p.y > upYTop - hitPadding &&
          p.y < upYBot + hitPadding) {
        g_manualHour++;
        if (g_manualHour > 23)
          g_manualHour = 0;
        changed = true;
      }

      // Hour DOWN
      if (p.x > hCX - 40 && p.x < hCX + 40 && p.y > dnYTop - hitPadding &&
          p.y < dnYBot + hitPadding) {
        g_manualHour--;
        if (g_manualHour < 0)
          g_manualHour = 23;
        changed = true;
      }

      // Minute UP
      if (p.x > mCX - 40 && p.x < mCX + 40 && p.y > upYTop - hitPadding &&
          p.y < upYBot + hitPadding) {
        g_manualMinute++;
        if (g_manualMinute > 59)
          g_manualMinute = 0;
        changed = true;
      }

      // Minute DOWN
      if (p.x > mCX - 40 && p.x < mCX + 40 && p.y > dnYTop - hitPadding &&
          p.y < dnYBot + hitPadding) {
        g_manualMinute--;
        if (g_manualMinute < 0)
          g_manualMinute = 59;
        changed = true;
      }

      if (changed) {
        g_lastTimeUpdate = millis();
        // Update GPS Manager immediately
        extern GPSManager gpsManager;
        gpsManager.setManualTime(g_manualHour, g_manualMinute, 0);

        // Also save to preferences for persistence (though GPSManager handles
        // runtime)
        Preferences prefs;
        prefs.begin("laptimer", false);
        prefs.putInt("m_hour", g_manualHour);
        prefs.putInt("m_min", g_manualMinute);
        prefs.end();

        drawScreen();
      }
    }
  }
}

void TimeSettingScreen::drawScreen() {
  TFT_eSPI *tft = _ui->getTft();

  // Draw background to clear previous state (Excluding Status Bar & Header)
  // Status bar (0-20), Header (21-60). Content starts at 61.
  // Instead of full fillRect, we call targeted updates if needed,
  // but for simplicity we'll keep the fillRect but only if absolutely necessary
  // or use a smaller range.
  tft->fillRect(0, 61, SCREEN_WIDTH, SCREEN_HEIGHT - 61, COLOR_BG);

  // Back Button (Blue Triangle)
  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, TFT_BLUE);

  // Title
  tft->setTextColor(COLOR_TEXT, COLOR_BG);
  tft->setTextDatum(TC_DATUM);
  tft->setTextSize(1);
  tft->drawString("SET CLOCK", SCREEN_WIDTH / 2, 40);

  tft->drawFastHLine(0, 60, SCREEN_WIDTH, COLOR_SECONDARY);

  // Layout Constants (Must match update())
  int cY = 160;
  int boxH = 50;
  int arrowGap = 8;
  int arrowH = 10;
  int boxW = 60;
  int gap = 30;

  int hCX = (SCREEN_WIDTH / 2) - (boxW / 2) - (gap / 2);
  int mCX = (SCREEN_WIDTH / 2) + (boxW / 2) + (gap / 2);

  // Arrow Params
  int arrowW = 8; // Half width (total 16)

  int upYBot = cY - (boxH / 2) - arrowGap;
  int upYTop = upYBot - arrowH;
  int dnYTop = cY + (boxH / 2) + arrowGap;
  int dnYBot = dnYTop + arrowH;

  // Draw Hours Column
  // Label at top
  tft->setTextSize(1);
  tft->setTextColor(COLOR_TEXT, COLOR_BG);
  tft->setTextDatum(BC_DATUM); // Bottom Center
  tft->drawString("HOURS", hCX, upYTop - 5);

  // Up Arrow
  tft->fillTriangle(hCX, upYTop, hCX - arrowW, upYBot, hCX + arrowW, upYBot,
                    TFT_LIGHTGREY);

  // Box
  tft->drawRoundRect(hCX - (boxW / 2), cY - (boxH / 2), boxW, boxH, 5,
                     TFT_LIGHTGREY);

  // Number
  tft->setTextFont(4);
  tft->setTextSize(1);
  tft->setTextColor(COLOR_TEXT, COLOR_BG);
  tft->setTextDatum(MC_DATUM); // Middle Center for perfect alignment
  tft->drawNumber(g_manualHour, hCX, cY);

  // Down Arrow
  tft->fillTriangle(hCX, dnYBot, hCX - arrowW, dnYTop, hCX + arrowW, dnYTop,
                    TFT_LIGHTGREY);

  // Colon
  tft->setTextFont(4);
  tft->setTextColor(COLOR_TEXT, COLOR_BG);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(":", SCREEN_WIDTH / 2, cY);

  // Draw Minutes Column
  // Label
  tft->setTextFont(1); // Reset font
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextDatum(BC_DATUM);
  tft->drawString("MINUTES", mCX, upYTop - 5);

  // Up Arrow
  tft->fillTriangle(mCX, upYTop, mCX - arrowW, upYBot, mCX + arrowW, upYBot,
                    TFT_LIGHTGREY);

  // Box
  tft->drawRoundRect(mCX - (boxW / 2), cY - (boxH / 2), boxW, boxH, 5,
                     TFT_LIGHTGREY);

  // Number
  tft->setTextFont(4);
  tft->setTextDatum(MC_DATUM);

  char buf[4];
  sprintf(buf, "%02d", g_manualMinute);
  tft->drawString(buf, mCX, cY);

  // Down Arrow
  tft->fillTriangle(mCX, dnYBot, mCX - arrowW, dnYTop, mCX + arrowW, dnYTop,
                    TFT_LIGHTGREY);

  _ui->drawStatusBar();

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}
