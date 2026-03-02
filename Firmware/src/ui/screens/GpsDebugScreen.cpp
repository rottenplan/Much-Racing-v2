#include "GpsDebugScreen.h"
#include "../../core/GPSManager.h"
#include "../fonts/Org_01.h"

extern GPSManager gpsManager;

void GpsDebugScreen::onShow() {
  TFT_eSPI *tft = _ui->getTft();

  // Clear screen
  _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                            SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
  _ui->drawStatusBar(true);

  // Header - Match other screen headers
  // Header - Match other screen headers
  int headerY = STATUS_BAR_HEIGHT;
  int lineY = headerY + 25;
  tft->drawFastHLine(0, lineY, SCREEN_WIDTH, COLOR_SECONDARY);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextDatum(MC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(2); // Match Session Summary / History title size
  tft->drawString("GPS DEBUG", SCREEN_WIDTH / 2, headerY + 12);

  // Back Button (Standardized Blue Triangle)
  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, TFT_BLUE);

  _lastUpdate = 0; // Force immediate update
  drawDebugStatic();
}

void GpsDebugScreen::drawDebugStatic() {
  TFT_eSPI *tft = _ui->getTft();

  int cardX = 20;
  int cardW = 440;
  int cardH = 110; // Increased to 110 to fit content
  int startY = 48;

  tft->setTextFont(2);
  tft->setTextSize(1);
  tft->setTextDatum(TL_DATUM);

  // --- CARD 1: HARDWARE & SIGNAL ---
  tft->fillRoundRect(cardX, startY, cardW, cardH, 8, 0x18E3); // Dark Gray-Blue
  tft->drawRoundRect(cardX, startY, cardW, cardH, 8, TFT_DARKGREY);

  tft->setTextColor(TFT_SKYBLUE, 0x18E3);
  tft->drawString("SIGNAL & HARDWARE", cardX + 15, startY + 10);

  int textY = startY + 35;
  tft->setTextColor(TFT_WHITE, 0x18E3);
  tft->drawString("Data RX:", cardX + 15, textY);
  tft->drawString("Satellites:", cardX + 220, textY);

  textY += 22;
  tft->setTextColor(TFT_WHITE, 0x18E3);
  tft->drawString("HDOP:", cardX + 15, textY);
  tft->drawString("Rate:", cardX + 220, textY);

  // --- CARD 2: POSITION & MOTION ---
  startY += cardH + 10;
  tft->fillRoundRect(cardX, startY, cardW, cardH, 8, 0x10A2); // Deeper Blue
  tft->drawRoundRect(cardX, startY, cardW, cardH, 8, TFT_DARKGREY);

  tft->setTextColor(TFT_YELLOW, 0x10A2);
  tft->drawString("POSITION & MOTION", cardX + 15, startY + 10);

  textY = startY + 35;
  tft->setTextColor(TFT_WHITE, 0x10A2);
  tft->drawString("Coordinates:", cardX + 15, textY);

  textY += 22;
  tft->setTextColor(TFT_WHITE, 0x10A2);
  tft->drawString("Fix Status:", cardX + 15, textY);

  textY += 22;
  tft->setTextColor(TFT_WHITE, 0x10A2);
  tft->drawString("Kmh / Alt:", cardX + 15, textY);
}

void GpsDebugScreen::drawDebugDynamic() {
  TFT_eSPI *tft = _ui->getTft();

  // Get all GPS data
  unsigned long gpsbytes = gpsManager.getBytesReceived();
  int sats = gpsManager.getSatellites();
  double lat = gpsManager.getLatitude();
  double lng = gpsManager.getLongitude();
  bool hasFix = gpsManager.isFixed();
  double speed = gpsManager.getSpeedKmph();
  double alt = gpsManager.getAltitude();
  double hdop = gpsManager.getHDOP();
  int hz = gpsManager.getUpdateRate();

  int cardX = 20;
  int cardH = 110;
  int startY = 48;
  char buf[80];

  tft->setTextFont(2);
  tft->setTextSize(1);
  tft->setTextDatum(TL_DATUM);
  tft->setTextPadding(120); // Pad to clear old text

  // --- CARD 1 ---
  int textY = startY + 35;

  sprintf(buf, "%lu B", gpsbytes);
  tft->setTextColor(gpsbytes > 0 ? TFT_GREEN : TFT_RED, 0x18E3);
  tft->drawString(buf, cardX + 100, textY);

  sprintf(buf, "%d", sats);
  tft->setTextColor(sats > 0 ? TFT_GREEN : 0xFDA0, 0x18E3);
  tft->drawString(buf, cardX + 310, textY);

  textY += 22;
  sprintf(buf, "%.1f", hdop);
  tft->setTextColor(TFT_SKYBLUE, 0x18E3);
  tft->drawString(buf, cardX + 100, textY);

  sprintf(buf, "%d Hz", hz);
  tft->setTextColor(TFT_SKYBLUE, 0x18E3);
  tft->drawString(buf, cardX + 310, textY);

  // --- CARD 2 ---
  startY += cardH + 10;
  textY = startY + 35;

  tft->setTextPadding(300); // Wider for coords
  sprintf(buf, "%.6f, %.6f", lat, lng);
  tft->setTextColor(TFT_LIGHTGREY, 0x10A2);
  tft->drawString(buf, cardX + 110, textY);

  textY += 22;
  tft->setTextPadding(150);
  tft->setTextColor(hasFix ? TFT_GREEN : TFT_RED, 0x10A2);
  tft->drawString(hasFix ? "FIXED" : "NO FIX", cardX + 110, textY);

  textY += 22;
  sprintf(buf, "%.1f / %.0f m", speed, alt);
  tft->setTextColor(TFT_SKYBLUE, 0x10A2);
  tft->drawString(buf, cardX + 110, textY);

  tft->setTextPadding(0);
}

void GpsDebugScreen::update() {
  unsigned long now = millis();

  // Handle Touch
  UIManager::TouchPoint p = _ui->getTouchPoint();
  if (p.x != -1) {
    // Back Button (Standardized Touch Box x < 80)
    if (p.x < 80 && p.y > 240) {
      _ui->switchScreen(SCREEN_SETTINGS);
      return;
    }
  }

  // Refresh every 500ms
  if (now - _lastUpdate >= 500) {
    drawDebugDynamic();
    _lastUpdate = now;
  }
}
