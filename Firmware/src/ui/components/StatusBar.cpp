#include "StatusBar.h"
#include "../../config.h"
#include "../../core/BatteryManager.h"
#include "../../core/GPSManager.h"
#include "../../core/SessionManager.h"
#include "../../core/WiFiManager.h"
#include "../UIManager.h" // Full definition for getters
#include "../fonts/Org_01.h"

extern GPSManager gpsManager;
extern WiFiManager wifiManager;
extern SessionManager sessionManager;

StatusBar::StatusBar(UIManager *ui) : _ui(ui) { _tft = _ui->getTft(); }

void StatusBar::begin() {
  // Initial state reset
  _lastWifiStatus = -1;
  _lastFix = false;
  _lastHdop = -1.0;
  _lastSignalStrength = -1;
  _lastSats = -1;
  _lastBat = -1;
  _wasCharging = false;
  _lastLogging = false;
  _lastRpmConn = false;
  _lastTimeStr = "";
}

void StatusBar::update() {
  // Periodically update (e.g. 1Hz) or let logic inside draw() handle diffs
  // Current logic in UIManager checks every 1000ms.
  // We can just call draw(false) frequently, and draw(false) will check diffs.

  // BUT we don't want to burn cycles.
  // Let's check every 500ms?
  if (millis() - _lastUpdateCheck >= 500) {
    draw(false);
    _lastUpdateCheck = millis();
  }
}

void StatusBar::draw(bool force) {
  TFT_eSPI *tft = _tft;

  uint16_t bgColor = _ui->getBackgroundColor();
  uint16_t textColor = _ui->getTextColor();
  uint16_t secondaryColor = _ui->getSecondaryColor();

  // 1. Time Update Logic
  int h, m, s, d, mo, y;
  gpsManager.getLocalTime(h, m, s, d, mo, y);

  // Check minute change to force status bar refresh
  static int lastMin = -1;
  if (m != lastMin) {
    force = true;
    lastMin = m;
  }

  // Static Line
  if (force) {
    // Draw the line separator at the very bottom of the status bar area
    // STATUS_BAR_HEIGHT is 25. Line at 24.
    tft->drawFastHLine(0, STATUS_BAR_HEIGHT - 1, SCREEN_WIDTH, secondaryColor);
  }

  tft->setFreeFont(&Org_01);
  tft->setTextSize(1); // Standard size for Org_01
  tft->setTextColor(textColor, bgColor);

  // --- WiFi Section (Left) ---
  int wifiStatus = wifiManager.isConnected() ? 1 : 0;
  if (force || wifiStatus != _lastWifiStatus) {
    // Initialize Sprite if not exists
    if (!_sprite) {
      _sprite = new TFT_eSprite(_tft);
      _sprite->setColorDepth(16);
      _sprite->createSprite(30, STATUS_BAR_HEIGHT);
    }

    // Draw to Sprite
    _sprite->fillSprite(bgColor); // Clear

    uint16_t color = (wifiStatus == 1) ? TFT_GREEN : TFT_RED;

    // Draw WiFi Icon - Bold 3-segment redesign
    int dotX = 15;
    int dotY = 14;

    // Top Bar (3px thick, max radius 10)
    _sprite->fillCircle(dotX, dotY, 10, color);
    _sprite->fillCircle(dotX, dotY, 7, bgColor);
    // Middle Bar (3px thick)
    _sprite->fillCircle(dotX, dotY, 5, color);
    _sprite->fillCircle(dotX, dotY, 2, bgColor);
    // Bottom Dot (1px radius center)
    _sprite->fillCircle(dotX, dotY, 1, color);

    // Masking to create wedge (90 degrees)
    // Mask bottom portion - Strictly clamped to stay WITHIN status bar
    _sprite->fillRect(0, dotY + 1, 30, (STATUS_BAR_HEIGHT) - (dotY + 1),
                      bgColor);
    // Side mask triangles for a sharp 90-degree wedge
    _sprite->fillTriangle(dotX, dotY, 0, dotY, 0, 0, bgColor);
    _sprite->fillTriangle(dotX, dotY, 30, dotY, 30, 0, bgColor);

    // Redraw line segment
    _sprite->drawFastHLine(0, STATUS_BAR_HEIGHT - 1, 30, secondaryColor);

    // Push Sprite to Screen at 0,0
    _sprite->pushSprite(0, 0);

    // Note: We don't delete sprite to reuse memory, or we could delete if RAM
    // constrained. Keeping it is faster.

    _lastWifiStatus = wifiStatus;
  }

  // --- GPS Section (Right of WiFi) ---
  double hdop = gpsManager.getHDOP();
  bool fix = gpsManager.isFixed();
  int signalStrength = 0;
  if (fix) {
    if (hdop <= 0.8)
      signalStrength = 4;
    else if (hdop <= 1.0)
      signalStrength = 3;
    else if (hdop <= 1.5)
      signalStrength = 2;
    else
      signalStrength = 1;
  }

  int sats = gpsManager.getSatellites();

  // Draw Signal Bars only if fix or strength changed
  if (force || fix != _lastFix || signalStrength != _lastSignalStrength) {
    // Clear only Signal Bars Area (30 to 50)
    tft->fillRect(30, 0, 20, STATUS_BAR_HEIGHT, bgColor);
    tft->drawFastHLine(30, STATUS_BAR_HEIGHT - 1, 20, secondaryColor);

    // Draw Signal Bars
    int barX = 32;
    int barY = 16;
    int barW = 3;
    int barGap = 2;

    for (int i = 0; i < 4; i++) {
      int h = (i + 1) * 3;
      int x = barX + (i * (barW + barGap));
      int y = barY - h;

      if (i < signalStrength) {
        uint16_t color = fix ? TFT_GREEN : TFT_RED;
        tft->fillRect(x, y, barW, h, color);
      } else {
        tft->drawRect(x, y, barW, h, textColor);
      }
    }

    _lastFix = fix;
    _lastSignalStrength = signalStrength;
  }

  // Draw SAT text only if count changed
  if (force || sats != _lastSats) {
    tft->setFreeFont(NULL);
    tft->setTextFont(1);
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(textColor, bgColor);
    tft->setTextSize(1);

    // Use padding to silently overwrite previous text without a flickery
    // fillRect
    tft->setTextPadding(tft->textWidth("SAT: 88"));

    String satStr = "SAT:" + String(sats);
    tft->drawString(satStr, 58, 10);

    // Reset padding so it doesn't affect other texts
    tft->setTextPadding(0);

    // Redraw Line Segment under text
    tft->drawFastHLine(50, STATUS_BAR_HEIGHT - 1, 40, secondaryColor);

    _lastSats = sats;
  }

  // --- Wireless RPM Indicator (Right of GPS) ---
  bool rpmConn = gpsManager.isWirelessConnected();
  // bool rpmConn = (millis() / 2000) % 2 == 0; // DUMMY: Toggle every 2s
  if (force || rpmConn != _lastRpmConn) {
    // Move to left of Battery
    // Battery area starts around SCREEN_WIDTH - 65
    // Let's place the dot at SCREEN_WIDTH - 75
    int rpmX = SCREEN_WIDTH - 75;
    int rpmY = 10;
    int r = 3;

    if (rpmConn) {
      tft->fillCircle(rpmX, rpmY, r, TFT_CYAN);
    } else {
      // Clear - Don't wipe the bottom line
      tft->fillRect(rpmX - r - 1, 0, (r * 2) + 2, STATUS_BAR_HEIGHT - 1,
                    bgColor);
    }

    _lastRpmConn = rpmConn;
  }

  // --- Center Text (Time / Title) ---
  String centerText = _ui->getScreenTitle(); // Need getter in UI
  if (centerText.length() == 0) {
    char buf[16];
    sprintf(buf, "%02d:%02d", h, m);
    centerText = String(buf);
  }

  if (force || centerText != _lastTimeStr) {
    int areaW = 120;
    // We can't easily clear bg with padding without wiping the line if line is
    // at bottom. Best to fillRect the area first.
    tft->fillRect((SCREEN_WIDTH - areaW) / 2, 0, areaW, STATUS_BAR_HEIGHT,
                  bgColor);

    tft->setFreeFont(NULL);
    tft->setTextFont(1);
    // tft->setTextPadding(areaW); // Avoid padding clear if we manually cleared
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(textColor, bgColor);
    tft->drawString(centerText, SCREEN_WIDTH / 2, 10);
    // tft->setTextPadding(0);

    // Redraw Line Segment
    tft->drawFastHLine((SCREEN_WIDTH - areaW) / 2, STATUS_BAR_HEIGHT - 1, areaW,
                       secondaryColor);

    _lastTimeStr = centerText;
  }

  // --- Battery Section ---
  BatteryManager &batMgr = BatteryManager::getInstance();
  int pct = batMgr.getPercentage();
  bool isCharging = batMgr.isCharging();

  if (force || pct != _lastBat || isCharging != _wasCharging) {
    // Clear Battery Area
    tft->fillRect(SCREEN_WIDTH - 65, 0, 65, STATUS_BAR_HEIGHT, bgColor);

    int batX = SCREEN_WIDTH - 28;
    int batY = 5;
    int batW = 20;
    int batH = 10;

    tft->drawRect(batX, batY, batW, batH, textColor);
    tft->fillRect(batX + batW, batY + 2, 2, 6, textColor);

    int innerW = batW - 4;
    int fillW = (innerW * (pct > 100 ? 100 : pct)) / 100;

    if (pct > 20)
      tft->fillRect(batX + 2, batY + 2, fillW, batH - 4, TFT_GREEN);
    else
      tft->fillRect(batX + 2, batY + 2, fillW, batH - 4, TFT_RED);

    if (isCharging) {
      uint16_t boltColor = TFT_WHITE;
      int cx = batX + (batW / 2);
      int cy = batY + (batH / 2);
      tft->fillTriangle(cx + 1, cy - 4, cx - 2, cy + 1, cx + 2, cy, boltColor);
      tft->fillTriangle(cx - 1, cy + 4, cx + 2, cy - 1, cx - 2, cy, boltColor);
    }

    tft->setFreeFont(NULL);
    tft->setTextFont(1);
    tft->setTextDatum(MR_DATUM);
    tft->setTextSize(1);
    tft->setTextColor(textColor, bgColor);
    tft->drawString(String(pct) + "%", batX - 4, 10);

    // Redraw Line Segment
    tft->drawFastHLine(SCREEN_WIDTH - 65, STATUS_BAR_HEIGHT - 1, 65,
                       secondaryColor);

    _lastBat = pct;
    _wasCharging = isCharging;
  }

  // --- Recording Indicator ---
  bool isLogging = sessionManager.isLogging();
  if (force || isLogging != _lastLogging) {
    int dotX = (SCREEN_WIDTH / 2) + 40;
    tft->fillRect(dotX - 5, 0, 10, STATUS_BAR_HEIGHT, bgColor);
    if (isLogging) {
      tft->fillCircle(dotX, 10, 3, TFT_RED);
    }
    // Redraw Line Segment
    tft->drawFastHLine(dotX - 5, STATUS_BAR_HEIGHT - 1, 10, secondaryColor);

    _lastLogging = isLogging;
  }

  // Reset Fonts
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}
