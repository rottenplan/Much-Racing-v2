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

  // Layout constants (must match drawDashboard)
  const int GRID_TOP = STATUS_BAR_HEIGHT + 4;
  const int GRID_H = SCREEN_HEIGHT - GRID_TOP - 4;
  const int GRID_W = 260;
  const int GAP = 4;
  const int CELL_W = (GRID_W - GAP) / 2;     // ~128
  const int CELL_H = (GRID_H - GAP * 2) / 3; // ~96

  // Back button: bottom-left corner, below RPM bar (Y=265+12=277)
  if (p.x != -1 && p.x < 40 && p.y > 277) {
    _ui->switchScreen(SCREEN_MENU);
    return;
  }

  // 2. G-Force Calibration (Double Tap on LEAN card - row 2, col 0)
  int leanCardX = 5;
  int leanCardY = GRID_TOP + 2 * (CELL_H + GAP);
  if (p.x >= leanCardX && p.x <= leanCardX + CELL_W && p.y >= leanCardY &&
      p.y <= leanCardY + CELL_H) {
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
        // Custom toast ABOVE speed number (right panel area)
        {
          TFT_eSPI *tft = _ui->getTft();
          int toastW = 170;
          int toastH = 36;
          int toastX = 298 + (177 - toastW) / 2; // center in right panel
          int toastY = GRID_TOP + 5; // just below status bar, above speed
          uint16_t cardColor = 0x18E3;
          tft->fillRoundRect(toastX, toastY, toastW, toastH, 8, cardColor);
          tft->drawRoundRect(toastX, toastY, toastW, toastH, 8, TFT_SILVER);
          tft->setTextColor(TFT_WHITE, cardColor);
          tft->setFreeFont(&Org_01);
          tft->setTextSize(1);
          tft->setTextDatum(MC_DATUM);
          tft->drawString("G-Force Calibrated", toastX + toastW / 2,
                          toastY + toastH / 2 - 2);
          delay(1000);
          tft->fillRect(toastX, toastY, toastW, toastH,
                        _ui->getBackgroundColor());
          tft->setFreeFont(NULL);
          drawDashboard(true); // restore screen
        }
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

// --- LAYOUT: 3x2 Grid (Left) + Big Speed (Right) + RPM Bar (Bottom Original)
// ---
void SpeedometerScreen::drawDashboard(bool force) {
  TFT_eSPI *tft = _ui->getTft();

  uint16_t colPrimary = COLOR_PRIMARY;
  uint16_t colText = _ui->getTextColor();
  uint16_t colBg = _ui->getBackgroundColor();
  uint16_t colBorder = TFT_DARKGREY;

  // === RPM bar: ORIGINAL fixed position ===
  const int RPM_BAR_Y = 265;
  const int RPM_BAR_H = 12;
  const int RPM_BAR_W = 400;
  const int RPM_BAR_X = (SCREEN_WIDTH - RPM_BAR_W) / 2;

  // === Grid area: status bar down to just above RPM bar ===
  const int MARGIN = 5;
  const int GRID_TOP = STATUS_BAR_HEIGHT + 4;
  const int GRID_BOT = RPM_BAR_Y - 5;     // 5px gap above bar
  const int GRID_H = GRID_BOT - GRID_TOP; // ~240px
  const int GAP = 4;
  const int ROW_COUNT = 3;
  const int COL_COUNT = 2;
  const int GRID_W = 290; // wider boxes
  const int CELL_W = (GRID_W - GAP) / COL_COUNT;
  const int CELL_H = (GRID_H - GAP * 2) / ROW_COUNT;

  const int RIGHT_X = GRID_W + GAP * 2;
  const int RIGHT_W = SCREEN_WIDTH - RIGHT_X - MARGIN;

  int colX[2] = {MARGIN, MARGIN + CELL_W + GAP};
  const char *labels[6] = {"MAX RPM", "MAX SPD", "SATS",
                           "DIST",    "LEAN",    "LAT-G"};

  if (force) {
    _ui->drawStatusBar(true);

    // 6 card outlines
    for (int row = 0; row < ROW_COUNT; row++) {
      int cy = GRID_TOP + row * (CELL_H + GAP);
      for (int col = 0; col < COL_COUNT; col++) {
        int cx = colX[col];
        tft->drawRoundRect(cx, cy, CELL_W, CELL_H, 5, colBorder);
        tft->setFreeFont(&Org_01);
        tft->setTextSize(1);
        tft->setTextColor(TFT_SILVER, colBg);
        tft->setTextDatum(TC_DATUM);
        tft->drawString(labels[row * 2 + col], cx + CELL_W / 2, cy + 4);
      }
    }

    // (no divider line between grid and speed panel)

    // km/h unit label
    tft->setFreeFont(NULL);
    tft->setTextFont(4); // bigger km/h label
    tft->setTextSize(1);
    tft->setTextColor(colPrimary, colBg);
    tft->setTextDatum(TC_DATUM);
    tft->drawString(_lastUnits ? "mph" : "km/h", RIGHT_X + RIGHT_W / 2,
                    GRID_TOP + GRID_H / 2 + 58);

    // RPM bar outline (original position, full width)
    tft->setFreeFont(NULL);
    tft->drawRect(RPM_BAR_X - 1, RPM_BAR_Y - 1, RPM_BAR_W + 2, RPM_BAR_H + 2,
                  colBorder);
    tft->setTextFont(1);
    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(TFT_SILVER, colBg);
    tft->drawString("RPM", RPM_BAR_X - 4, RPM_BAR_Y + RPM_BAR_H / 2);

    // Back button — BELOW rpm bar, clear of grid
    tft->fillTriangle(10, RPM_BAR_Y + RPM_BAR_H + 24, 22,
                      RPM_BAR_Y + RPM_BAR_H + 14, 22,
                      RPM_BAR_Y + RPM_BAR_H + 34, TFT_BLUE);

    tft->setFreeFont(NULL);
  }

  // === DYNAMIC: Card values ===
  tft->setFreeFont(NULL);
  tft->setTextFont(4);
  tft->setTextSize(1);         // ~1.5x approx: 26px (max font4 size1)
  tft->setTextDatum(MC_DATUM); // centered vertically in cell
  tft->setTextColor(colText, colBg);

  char buf[24];
  struct {
    int col;
    int row;
    const char *fmt;
    float val;
  } cells[6] = {
      {0, 0, "%d", (float)_maxRPM},   {1, 0, "%.0f", _maxSpeed},
      {0, 1, "%d", (float)_lastSats}, {1, 1, "%.1f", _lastTrip},
      {0, 2, "%.0f", abs(_lastRoll)}, {1, 2, "%.2fG", _lastAccY},
  };

  for (int i = 0; i < 6; i++) {
    int cx = colX[cells[i].col];
    int cy = GRID_TOP + cells[i].row * (CELL_H + GAP);
    int valX = cx + CELL_W / 2;
    int valY = cy + CELL_H / 2 + 8; // center, shifted down to clear top label
    tft->setTextPadding(CELL_W - 8);
    if (i == 0 || i == 2)
      sprintf(buf, cells[i].fmt, (int)cells[i].val);
    else
      sprintf(buf, cells[i].fmt, cells[i].val);
    tft->drawString(buf, valX, valY);
  }
  tft->setTextPadding(0);

  // === DYNAMIC: Big Speed ===
  int speedCenterX = RIGHT_X + RIGHT_W / 2;
  int speedCenterY = GRID_TOP + GRID_H / 2 - 10;

  tft->setTextFont(7); // 7-segment style font (original)
  // Auto-size: 3+ digits -> size 1 to prevent overlap with grid
  if (_lastSpeed >= 100)
    tft->setTextSize(1);
  else
    tft->setTextSize(2);
  tft->setTextColor(colPrimary, colBg);
  tft->setTextDatum(MC_DATUM);
  tft->setTextPadding(RIGHT_W);
  sprintf(buf, "%.0f", _lastSpeed);
  tft->drawString(buf, speedCenterX, speedCenterY);
  tft->setTextPadding(0);

  // === DYNAMIC: RPM Bar (original full-width) ===
  int fillW = map(constrain(_lastRPM, 0, 12000), 0, 12000, 0, RPM_BAR_W);
  if (fillW > 0)
    tft->fillRect(RPM_BAR_X, RPM_BAR_Y, fillW, RPM_BAR_H, colPrimary);
  if (fillW < RPM_BAR_W)
    tft->fillRect(RPM_BAR_X + fillW, RPM_BAR_Y, RPM_BAR_W - fillW, RPM_BAR_H,
                  colBg);

  // RPM number to the right
  tft->setTextFont(2);
  tft->setTextSize(1);
  tft->setTextColor(colText, colBg);
  tft->setTextDatum(ML_DATUM);
  tft->setTextPadding(60);
  sprintf(buf, "%d", _lastRPM);
  tft->drawString(buf, RPM_BAR_X + RPM_BAR_W + 6, RPM_BAR_Y + RPM_BAR_H / 2);
  tft->setTextPadding(0);

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}
