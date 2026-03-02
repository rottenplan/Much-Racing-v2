#include "SpeedometerScreen.h"
#include "../../config.h"
#include "../../core/BatteryManager.h"
#include "../../core/GPSManager.h"
#include "../../core/IMUManager.h"
#include "../fonts/Org_01.h"
#include <Preferences.h>

extern GPSManager gpsManager;
extern IMUManager imuManager;

void SpeedometerScreen::onShow() {
  TFT_eSPI *tft = _ui->getTft();
  // Clear content area only - Redundant
  // tft->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
  //               SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
  //               _ui->getBackgroundColor());

  _lastSpeed = -1;
  _lastRPM = -1;
  _lastTrip = -1;
  _lastTime = "";
  _lastGear = -1;
  _lastBat = -1;
  _lastRoll = 0;
  _lastAccY = 0;
  _maxSpeed = 0;
  _maxRPM = 0;
  _lastSats = -1;

  // Cache Settings
  Preferences prefs;
  prefs.begin("laptimer", true);
  _lastUnits = prefs.getInt("units", 0) == 1; // 0=km/h, 1=mph
  prefs.end();

  drawDashboard(true);
  imuManager.calibrateLevel();
  imuManager.requestActivity(true);
}

void SpeedometerScreen::onHide() { imuManager.requestActivity(false); }

void SpeedometerScreen::update() {
  // 1. Tombol Kembali
  UIManager::TouchPoint p = _ui->getTouchPoint();
  // 1. Tombol Kembali (Standardized hit area 100x80)
  if (p.x != -1 && p.x < 100 && p.y > 240) {
    _ui->switchScreen(SCREEN_MENU);
    return;
  }

  // 2. Roll Angle Calibration (Double Tap on Card)
  // Roll card area: startX + cardW + gap, bottomCardY
  // Approx bounds: [170, 300] x [195, 245]
  if (p.x >= 170 && p.x <= 300 && p.y >= 195 && p.y <= 245) {
    unsigned long now = millis();
    if (now - _lastTapTime < 300) { // Double tap within 300ms
      _tapCount++;
      if (_tapCount >= 2) {
        imuManager.calibrateLevel();
        imuManager.resetMaxLean();
        _lastRoll = -999; // Force redraw
        _maxSpeed = 0;    // Reset peak stats
        _maxRPM = 0;
        _tapCount = 0;
        _ui->showToast("G-Force Calibrated", 1000);
        Serial.println("Roll Calibration Triggered!");
      }
    } else {
      _tapCount = 1;
    }
    _lastTapTime = now;
  }

  // 2. Pembaruan Data GPS
  float speed = gpsManager.getSpeedKmph();
  float trip = gpsManager.getTotalTrip();

  // Waktu Nyata dari GPS
  int h, m, s, d, mo, y;
  gpsManager.getLocalTime(h, m, s, d, mo, y);
  char timeBuf[6];
  sprintf(timeBuf, "%02d:%02d", h, m);
  String timeStr = String(timeBuf);

  // 3. HITUNG RPM (Real Time)
  int rpm = gpsManager.getRPM();

  // 4. GEAR CALCULATION (Ratio-based)
  int gear = 0;
  if (speed > 5.0f && rpm > 1500) {
    float ratio = (float)rpm / speed;
    // Basic mapping for typical 6-speed motorbike (Adjustable)
    if (ratio > 110.0f)
      gear = 1;
    else if (ratio > 75.0f)
      gear = 2;
    else if (ratio > 58.0f)
      gear = 3;
    else if (ratio > 48.0f)
      gear = 4;
    else if (ratio > 42.0f)
      gear = 5;
    else
      gear = 6;
  }

  // 5. BATTERY PERCENTAGE
  int bat = BatteryManager::getInstance().getPercentage();

  // Cek satuan (km/h atau mph) dari cache
  bool useMph = _lastUnits;

  if (useMph) {
    speed *= 0.621371;
    trip *= 0.621371;
  }

  // Cek apakah ada perubahan data untuk digambar ulang
  int sats = gpsManager.getSatellites();
  if (rpm > _maxRPM)
    _maxRPM = rpm;
  if (speed > _maxSpeed)
    _maxSpeed = speed;

  // IMU Data
  float roll = imuManager.getLeanAngle();
  float accY =
      imuManager.getAccX(); // Lateral G (X is Roll/Lateral in standard mapping)

  if (speed != _lastSpeed || rpm != _lastRPM || useMph != _lastUnits ||
      timeStr != _lastTime || trip != _lastTrip || sats != _lastSats ||
      abs(roll - _lastRoll) > 0.1f || abs(accY - _lastAccY) > 0.01f ||
      gear != _lastGear || bat != _lastBat) {
    _lastSpeed = speed;
    _lastRPM = rpm;
    _lastUnits = useMph;
    _lastTime = timeStr;
    _lastTrip = trip;
    _lastSats = sats;
    _lastRoll = roll;
    _lastAccY = accY;
    _lastGear = gear;
    _lastBat = bat;
    drawDashboard(false);
  }
}

// Pembantu untuk menggambar segmen jajargenjang
void drawSegment(TFT_eSPI *tft, int x, int y, int w, int h, int angleOffset,
                 uint16_t color) {
  tft->fillTriangle(x, y + h, x + w, y + h, x + angleOffset, y, color);
  tft->fillTriangle(x + w, y + h, x + w + angleOffset, y, x + angleOffset, y,
                    color);
}

// --- LAYOUT CORRECTIONS ---
void SpeedometerScreen::drawDashboard(bool force) {
  TFT_eSPI *tft = _ui->getTft();

  // --- THEME COLORS ---
  uint16_t colPrimary = COLOR_PRIMARY;
  uint16_t colText = _ui->getTextColor();
  uint16_t colBg = _ui->getBackgroundColor();
  uint16_t colCardBorder = TFT_DARKGREY;

  // Layout Constants (Optimized for 480x320)
  int cardY = 30; // Slightly lower for better breathing room
  int cardH = 50;
  int cardW = 130;
  int gap = 15;
  int startX = 25;
  int bottomCardY = 195; // Moved up from 210 to give more space at bottom

  // Y Positions
  int valY = cardY + 30;
  int bottomValY = valY + (bottomCardY - cardY);
  int speedY = 130; // Centered Speed more vertically
  int unitY = speedY + 50;

  if (force) {
    // Clear Content - Redundant
    // _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
    //                           SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
    _ui->drawStatusBar(true);

    // --- TOP DATA CARDS ---
    tft->drawRoundRect(startX, cardY, cardW, cardH, 6, colCardBorder);
    tft->drawRoundRect(startX + cardW + gap, cardY, cardW, cardH, 6,
                       colCardBorder);
    tft->drawRoundRect(startX + (cardW + gap) * 2, cardY, cardW, cardH, 6,
                       colCardBorder);

    // --- BOTTOM DATA CARDS ---
    tft->drawRoundRect(startX, bottomCardY, cardW, cardH, 6, colCardBorder);
    tft->drawRoundRect(startX + cardW + gap, bottomCardY, cardW, cardH, 6,
                       colCardBorder);
    tft->drawRoundRect(startX + (cardW + gap) * 2, bottomCardY, cardW, cardH, 6,
                       colCardBorder);

    // --- LABELS (Inside Cards, Top) ---
    tft->setFreeFont(&Org_01);
    tft->setTextSize(1);
    tft->setTextColor(TFT_SILVER, colBg);
    tft->setTextDatum(TC_DATUM);

    int labelY = cardY + 4;
    int bottomLabelY = bottomCardY + 4;

    // Top Labels
    tft->drawString("MAX RPM", startX + (cardW / 2), labelY);
    tft->drawString("MAX SPEED", startX + cardW + gap + (cardW / 2), labelY);
    tft->drawString("SATELLITES", startX + (cardW + gap) * 2 + (cardW / 2),
                    labelY);

    // Bottom Labels
    tft->drawString("DISTANCE", startX + (cardW / 2), bottomLabelY);
    tft->drawString("LEAN ANGLE", startX + cardW + gap + (cardW / 2),
                    bottomLabelY);
    tft->drawString("LAT G-FORCE", startX + (cardW + gap) * 2 + (cardW / 2),
                    bottomLabelY);

    // --- UNIT ---
    tft->setTextFont(2);
    tft->setTextSize(1);
    tft->setTextColor(colPrimary, colBg);
    tft->drawCentreString("km/h", SCREEN_WIDTH / 2, unitY, 1);

    // --- RPM BAR OUTLINE ---
    int rpmY = 265; // Moved up from 290
    int rpmH = 12;
    int rpmW = 400;
    int rpmX = (SCREEN_WIDTH - rpmW) / 2;

    tft->drawRect(rpmX - 1, rpmY - 1, rpmW + 2, rpmH + 2, TFT_DARKGREY);
    tft->setTextDatum(MR_DATUM);
    tft->drawString("RPM", rpmX - 10, rpmY + 6);

    // --- BACK BUTTON (Standardized Triangle) ---
    tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                      SCREEN_HEIGHT - 20, TFT_BLUE);
    tft->setFreeFont(NULL); // Reset font
  }

  // --- DYNAMIC UPDATES ---
  char buf[32];

  tft->setTextDatum(MC_DATUM);
  tft->setTextColor(colText, colBg);
  tft->setTextFont(4);
  tft->setTextSize(1);
  int padW = cardW - 10;

  // 1. UPDATE TOP CARDS
  tft->setTextPadding(padW);

  // Max RPM
  sprintf(buf, "%d", _maxRPM);
  tft->drawString(buf, startX + (cardW / 2), valY);

  // Max Speed
  sprintf(buf, "%.0f", _maxSpeed);
  tft->drawString(buf, startX + cardW + gap + (cardW / 2), valY);

  // Sats
  sprintf(buf, "%d", _lastSats);
  tft->drawString(buf, startX + (cardW + gap) * 2 + (cardW / 2), valY);

  // 2. UPDATE BOTTOM CARDS
  // Distance
  sprintf(buf, "%.1f", _lastTrip);
  tft->drawString(buf, startX + (cardW / 2), bottomValY);

  // Lean
  sprintf(buf, "%.0f*", abs(_lastRoll));
  tft->drawString(buf, startX + cardW + gap + (cardW / 2), bottomValY);

  // Max Lean Left/Right (Smaller)
  tft->setTextFont(1);
  tft->setTextSize(1);
  tft->setTextColor(TFT_SILVER, colBg);

  tft->setTextFont(4);
  tft->setTextSize(1);
  tft->setTextColor(colText, colBg);

  // Lat G
  sprintf(buf, "%.2fG", _lastAccY);
  tft->drawString(buf, startX + (cardW + gap) * 2 + (cardW / 2), bottomValY);

  tft->setTextPadding(0);

  // 3. MAIN SPEED
  tft->setTextFont(7); // 7-Segment
  tft->setTextSize(2);
  tft->setTextColor(colPrimary, colBg);
  tft->setTextDatum(MC_DATUM);
  tft->setTextPadding(SCREEN_WIDTH);
  sprintf(buf, "%.0f", _lastSpeed);
  tft->drawString(buf, SCREEN_WIDTH / 2, speedY);
  tft->setTextPadding(0);

  // 4. RPM BAR
  int rpmY = 265; // MATCH OUTLINE
  int rpmH = 12;
  int rpmW = 400;
  int rpmX = (SCREEN_WIDTH - rpmW) / 2;

  int fillW = map(constrain(_lastRPM, 0, 12000), 0, 12000, 0, rpmW);
  if (fillW > 0)
    tft->fillRect(rpmX, rpmY, fillW, rpmH, colPrimary);
  if (fillW < rpmW)
    tft->fillRect(rpmX + fillW, rpmY, rpmW - fillW, rpmH, colBg);

  tft->setTextFont(2);
  tft->setTextSize(1);
  tft->setTextColor(colText, colBg);
  tft->setTextDatum(ML_DATUM);
  tft->setTextPadding(60);
  sprintf(buf, "%d", _lastRPM);
  tft->drawString(buf, rpmX + rpmW + 10, rpmY + 6);

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}
