#include "GpsStatusScreen.h"
#include "../../core/GPSManager.h"
#include "../fonts/Org_01.h"

extern GPSManager gpsManager;

// Safe Area Offset (Status Bar Height)
// #define TOP_OFFSET 25
#define TOP_OFFSET STATUS_BAR_HEIGHT

void GpsStatusScreen::onShow() {
  TFT_eSPI *tft = _ui->getTft();
  // No manual fills needed here, UIManager clears the background
  _ui->drawStatusBar(true);

  // Draw Top-Left Date/Time Box Frame (Optional, or just text)
  // Let's keep it clean black background.

  // Initial values reset
  _lastSats = -1;
  _lastHz = -1;
  _lastLat = -999;
  _lastLng = -999;
  _lastHdop = -1.0;   // Reset HDOP tracker
  _lastFixed = false; // Force re-draw of radar rings

  // Clear any potential leftover artifacts manually if needed,
  // but fillScreen should handle it. The 'white lines' might be status bar
  // related, but let's ensure clean state here.

  // Back Button (Blue Triangle) - Bottom Left
  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, TFT_BLUE);

  // Log Button (Orange Label) - Bottom Right
  tft->fillRoundRect(SCREEN_WIDTH - 60, SCREEN_HEIGHT - 35, 50, 25, 4,
                     TFT_ORANGE);
  tft->setTextColor(TFT_BLACK, TFT_ORANGE);
  tft->setTextDatum(MC_DATUM);
  tft->setTextFont(1);
  tft->drawString("LOG", SCREEN_WIDTH - 35, SCREEN_HEIGHT - 23);

  // Cold Start Button (Blue Label) - Bottom Middle-ish
  tft->fillRoundRect(SCREEN_WIDTH - 120, SCREEN_HEIGHT - 35, 50, 25, 4,
                     TFT_BLUE);
  tft->setTextColor(TFT_WHITE, TFT_BLUE);
  tft->setTextDatum(MC_DATUM);
  tft->setTextFont(1);
  tft->drawString("COLD", SCREEN_WIDTH - 95, SCREEN_HEIGHT - 23);

  drawStatus();
}

void GpsStatusScreen::update() {
  // Touch Handling
  UIManager::TouchPoint p = _ui->getTouchPoint();
  if (p.x != -1) {
    // Back Button Area
    // Back Button Area (Standardized 100x80)
    if (p.x < 80 && p.y > 240) {
      _ui->switchScreen(SCREEN_MENU);
      return;
    }

    // Check for "LOG" Button Area (Bottom Right)
    if (p.x > SCREEN_WIDTH - 80 && p.y > SCREEN_HEIGHT - 60) {
      if (p.x > SCREEN_WIDTH - 40) { // Log
        _ui->switchScreen(SCREEN_GNSS_LOG);
      } else { // Cold
        gpsManager.resetModule(true);
      }
      return;
    }

    // Check for Double Tap on body (Keep as backup)
    unsigned long now = millis();
    if (now - _lastTapTime < 500) {
      // Double Tap!
      _ui->switchScreen(SCREEN_GNSS_LOG);
      _lastTapTime = 0; // Reset
      return;
    }
    _lastTapTime = now;
  }

  // Throttle drawing to 5Hz (200ms) for performance
  unsigned long now = millis();
  if (now - _lastDrawTime >= 200) {
    _lastDrawTime = now;
    drawStatus();
  }
}

// Safe Area Offset (Status Bar Height)

void GpsStatusScreen::drawStatus() {
  TFT_eSPI *tft = _ui->getTft();

  int sats = gpsManager.getSatellites();
  double hdop = gpsManager.getHDOP();
  int hz = gpsManager.getUpdateRate();
  double lat = gpsManager.getLatitude();
  double lng = gpsManager.getLongitude();

  // Colors
  uint16_t COLOR_CARD = 0x18E3;
  uint16_t COLOR_LABEL = TFT_SILVER;
  uint16_t COLOR_VALUE = TFT_WHITE;

  // --- 1. DATE/TIME & LAT/LON CARD (Left Side) ---
  // If first run or interval, redraw card base
  static unsigned long lastUpdate = 0;
  bool forceRedraw = (_lastSats == -1);

  if (forceRedraw || millis() - lastUpdate > 1000) {
    lastUpdate = millis();
    int cardX = 15;
    int cardY = TOP_OFFSET + 10;
    int cardW = 220;
    int cardH = 145;

    if (forceRedraw) {
      tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, 0x10A2); // Navy Base
      tft->drawRoundRect(cardX, cardY, cardW, cardH, 8, TFT_DARKGREY);

      tft->setTextColor(TFT_SKYBLUE, 0x10A2);
      tft->setTextDatum(TL_DATUM);
      tft->setFreeFont(&Org_01);
      tft->drawString("GNSS LOCATION", cardX + 15, cardY + 10);
      tft->drawFastHLine(cardX + 10, cardY + 25, cardW - 20, 0x3186);
    }

    // Dynamic Values - Time
    int h, m, s, d, mo, y;
    gpsManager.getLocalTime(h, m, s, d, mo, y);

    char dateBuf[32];
    sprintf(dateBuf, "%02d/%02d/%04d", d, mo, y);
    char timeBuf[16];
    sprintf(timeBuf, "%02d:%02d:%02d", h, m, s);

    tft->setTextColor(TFT_WHITE, 0x10A2);
    tft->setTextDatum(TL_DATUM);
    tft->setTextFont(4); // Big Time
    tft->drawString(timeBuf, cardX + 15, cardY + 32);

    tft->setTextFont(2);
    tft->setTextColor(TFT_SILVER, 0x10A2);
    tft->drawString(dateBuf, cardX + 15, cardY + 56);

    // Dynamic Values - Lat/Lon
    tft->setFreeFont(&Org_01);
    tft->setTextColor(TFT_SKYBLUE, 0x10A2);
    tft->drawString("LATITUDE", cardX + 15, cardY + 78);
    tft->drawString("LONGITUDE", cardX + 15, cardY + 110);

    tft->setTextColor(TFT_WHITE, 0x10A2);
    tft->setFreeFont(NULL);
    tft->setTextFont(2);
    tft->drawString(String(lat, 6), cardX + 15, cardY + 90);
    tft->drawString(String(lng, 6), cardX + 15, cardY + 122);

    // Alt / Heading tiny indicators
    int alt = (int)gpsManager.getAltitude();
    int head = (int)gpsManager.getHeading();
    tft->setTextColor(TFT_GOLD, 0x10A2);
    tft->drawRightString(String(alt) + "m", cardX + cardW - 15, cardY + 78, 1);
    tft->drawRightString(String(head) + "°", cardX + cardW - 15, cardY + 110,
                         1);
  }

  // --- 2. SIGNAL STRENGTH & STATUS (Bottom Card) ---
  if (forceRedraw || sats != _lastSats || hz != _lastHz) {
    int cardX = 15;
    int cardY = TOP_OFFSET + 165;
    int cardW = SCREEN_WIDTH - 30;
    int cardH = 80;

    if (forceRedraw) {
      tft->fillRoundRect(cardX, cardY, cardW, cardH, 10, 0x18E3); // Modern Teal
      tft->setTextColor(TFT_WHITE, 0x18E3);
      tft->setTextDatum(TL_DATUM);
      tft->setFreeFont(&Org_01);
      tft->drawString("SIGNAL STATUS", cardX + 15, cardY + 10);
      tft->drawFastHLine(cardX + 10, cardY + 25, cardW - 20, 0x4228);

      // Satellite Icon
      int icX = cardX + 15;
      int icY = cardY + 35;
      tft->fillRoundRect(icX, icY, 25, 25, 4, 0x4228);
      tft->fillCircle(icX + 12, icY + 12, 6, TFT_WHITE);
      tft->fillRect(icX + 2, icY + 11, 21, 2, TFT_SILVER);
      tft->fillRect(icX + 11, icY + 2, 2, 21, TFT_SILVER);
    }

    // SATS / RATE / PRECISION Labels (Moved Left to avoid overlap)
    tft->setTextDatum(TC_DATUM);
    tft->setFreeFont(NULL);
    tft->setTextFont(1);
    tft->setTextColor(TFT_SILVER, 0x18E3);
    tft->drawString("SATS", cardX + 60, cardY + 35);
    tft->drawString("RATE", cardX + 120, cardY + 35);
    tft->drawString("PRECISION", cardX + 190, cardY + 35);

    tft->setTextFont(4);
    tft->setTextColor(TFT_GREEN, 0x18E3);
    tft->setTextPadding(40); // Reduced from 80 to avoid masking icon on left
    tft->drawString(String(sats), cardX + 60, cardY + 45);

    tft->setTextPadding(70); // Individual padding for Hz
    tft->setTextColor(TFT_CYAN, 0x18E3);
    tft->drawString(String(hz) + "Hz", cardX + 120, cardY + 45);

    tft->setTextPadding(80); // Padding for HDOP
    tft->setTextColor(TFT_YELLOW, 0x18E3);
    tft->drawString(String(hdop, 1), cardX + 190, cardY + 45);
    tft->setTextPadding(0);

    // Fix Indicator
    String fixStr = gpsManager.isFixed() ? "3D FIX" : "NO FIX";
    uint16_t fixColor = gpsManager.isFixed() ? TFT_GREEN : TFT_RED;
    tft->fillRoundRect(cardX + cardW - 85, cardY + 40, 70, 25, 12, fixColor);
    tft->setTextColor(TFT_BLACK, fixColor);
    tft->setTextFont(2);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(fixStr, cardX + cardW - 50, cardY + 53);

    // --- SIGNAL STRENGTH BARS (Positioned after PRECISION) ---
    int barBaseX = cardX + 240;
    int barBaseY = cardY + 68;

    // Clear bar area first to avoid artifacts
    tft->fillRect(barBaseX - 5, barBaseY - 25, 100, 30, 0x18E3);

    std::vector<SatelliteInfo> satData = gpsManager.getSatellitesData();
    std::sort(satData.begin(), satData.end(),
              [](const SatelliteInfo &a, const SatelliteInfo &b) {
                return a.snr > b.snr;
              });

    for (int i = 0; i < 8 && i < satData.size(); i++) {
      int barH = map(satData[i].snr, 0, 50, 2, 20);
      if (barH > 20)
        barH = 20;
      uint16_t bCol = (satData[i].snr > 30)
                          ? TFT_GREEN
                          : (satData[i].snr > 20 ? TFT_YELLOW : TFT_ORANGE);
      tft->fillRect(barBaseX + (i * 12), barBaseY - barH, 8, barH, bCol);
    }
  }

  _lastSats = sats;
  _lastHz = hz;

  // --- 3. RADAR (Right Side) ---
  int radarX = 250;
  int radarY = TOP_OFFSET + 10;
  int radarW = SCREEN_WIDTH - radarX - 15;
  int radarH = 145;
  int cX = radarX + radarW / 2;
  int cY = radarY + radarH / 2;
  int r = 68; // Increased from 55 to fill space (Max 72)

  if (forceRedraw || sats != _lastSats || hz != _lastHz) {
    // Clear Radar Area (Inside)
    tft->fillCircle(cX, cY, r + 5, 0x0841);

    // Re-draw crosshair & circles
    tft->drawCircle(cX, cY, r, 0x3186);
    tft->drawCircle(cX, cY, r * 0.6, 0x18E3);
    tft->drawCircle(cX, cY, r * 0.2, 0x18E3);
    tft->drawLine(cX - r, cY, cX + r, cY, 0x3186);
    tft->drawLine(cX, cY - r, cX, cY + r, 0x3186);

    std::vector<SatelliteInfo> satData = gpsManager.getSatellitesData();
    for (const auto &sat : satData) {
      if (sat.elevation < 0)
        continue;

      float rad = r * (1.0 - (sat.elevation / 90.0));
      float angle = (sat.azimuth - 90) * DEG_TO_RAD;

      int sx = cX + (rad * cos(angle));
      int sy = cY + (rad * sin(angle));

      uint16_t dotColor =
          (sat.snr > 35)
              ? TFT_GREEN
              : (sat.snr > 25 ? TFT_YELLOW
                              : (sat.snr > 15 ? TFT_ORANGE : TFT_RED));
      tft->fillCircle(sx, sy, 3, dotColor);
      tft->drawCircle(sx, sy, 3, TFT_WHITE);
    }
  }

  if (!_lastFixed) {
    // Logic for satellites if we had them or just static crosshair
    _lastFixed = true;
  }

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}
