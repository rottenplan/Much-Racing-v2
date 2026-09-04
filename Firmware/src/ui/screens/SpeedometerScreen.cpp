#include "SpeedometerScreen.h"
#include "../../config.h"
#include "../../core/FeedbackManager.h"
#include "../../core/GPSManager.h"
#include "../../core/IMUManager.h"
#include "../../core/RouteNavigator.h"
#include "../fonts/Org_01.h"
#include <Preferences.h>

extern GPSManager gpsManager;
extern IMUManager imuManager;

// --- Voltmeter Motor (12V): baca ADC dengan rata-rata ---
float readMotorVoltage() {
  static unsigned long lastRead = 0;
  static float filtered = -1; // -1 = belum pernah baca
  if (millis() - lastRead < 50)
    return filtered; // max 20x/detik

  lastRead = millis();
  long sum = 0;
  for (int i = 0; i < VOLTMETER_SAMPLES; i++) {
    sum += analogRead(PIN_VOLTMETER);
  }
  float avg = sum / (float)VOLTMETER_SAMPLES;
  // ESP32 ADC 12-bit: 0-4095 = 0-3.3V
  float volt = (avg / 4095.0f) * 3.3f * VOLTMETER_RATIO;

  // Smoothing: 70% lama + 30% baru (anti-jitter)
  if (sum < 0) { // never true, placeholder
  }
  if (filtered < 0)
    filtered = volt;
  else
    filtered = filtered * 0.7f + volt * 0.3f;
  return filtered;
}

void SpeedometerScreen::onShow() {
  TFT_eSPI *tft = _ui->getTft();

  _lastSpeed = -1;
  _lastRPM = -1;
  _lastTrip = -1;
  _lastTime = "";
  _lastAccY = 0;
  _lastHeading = -1;
  _lastLat = 0;
  _lastLng = 0;
  _maxSpeed = 0;
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

  // Layout constants (touch areas)
  const int GRID_TOP = STATUS_BAR_HEIGHT + 4;

  // Back button: bottom-left corner, below RPM bar (Y=265+12=277)
  if (p.x != -1 && p.x < 40 && p.y > 277) {
    _ui->switchScreen(SCREEN_MENU);
    return;
  }

  // 2. G-Force Calibration (Double Tap pada card G-Force — posisi sama
  // dengan toast "G-Force Calibrated")
  int gForceX1 = 298;
  int gForceX2 = 470;
  int gForceY1 = GRID_TOP + 2;
  int gForceY2 = GRID_TOP + 48;
  if (p.x >= gForceX1 && p.x <= gForceX2 && p.y >= gForceY1 &&
      p.y <= gForceY2) {
    unsigned long now = millis();
    if (now - _lastTapTime < 300) { // Double tap within 300ms
      _tapCount++;
      if (_tapCount >= 2) {
        imuManager.calibrateLevel();
        imuManager.resetMaxLean();
        _lastAccY = 0; // Reset G display
        _tapCount = 0;
        // Custom toast atas panel kanan
        {
          TFT_eSPI *tft = _ui->getTft();
          int toastW = 170;
          int toastH = 36;
          int toastX = 298 + (177 - toastW) / 2; // center in right panel
          int toastY = GRID_TOP + 5;
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

  // 3. Pembaruan Data GPS
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

  // Cek satuan (km/h atau mph) dari cache
  bool useMph = _lastUnits;

  if (useMph) {
    speed *= 0.621371;
    trip *= 0.621371;
  }

  // Cek apakah ada perubahan data untuk digambar ulang
  int sats = gpsManager.getSatellites();
  if (speed > _maxSpeed)
    _maxSpeed = speed;

  // IMU G (hanya akselerasi yang ditampilkan; lean tidak dipakai)
  float accY =
      imuManager.getAccX(); // Lateral G (X is Roll/Lateral in standard mapping)

  // Navigation Data (Heading + Position)
  float heading = gpsManager.getHeading();
  double lat = gpsManager.getLatitude();
  double lng = gpsManager.getLongitude();

  // Voltmeter Motor (12V) — baca dengan rata-rata internal
  float volt = readMotorVoltage();

  // Navigasi offline: update posisi ke RouteNavigator + beep saat urgensi naik
  if (routeNavigator.isActive() && gpsManager.isFixed()) {
    routeNavigator.update(gpsManager.getLatitude(), gpsManager.getLongitude());
    if (routeNavigator.isNewUrgency()) {
      routeNavigator.clearNewUrgency();
      uint8_t u = routeNavigator.getUrgency();
      if (u >= 4)
        FeedbackManager::getInstance().beep(300); // belokan segera
      else if (u >= 2)
        FeedbackManager::getInstance().beep(150); // belokan dekat
      else
        FeedbackManager::getInstance().beep(80); // info belokan
    }
  }

  // KEUNCI ANTI-FLICKER: kecepatan dibulatkan untuk perbandingan,
  // jadi redraw hanya saat angka layar benar-benar berubah (bukan tiap loop).
  float speedCmp = roundf(speed);

  // Redraw hanya jika data di layar benar-benar berubah (anti-flicker)
  if (speedCmp != _lastSpeed || rpm != _lastRPM || useMph != _lastUnits ||
      timeStr != _lastTime || trip != _lastTrip || sats != _lastSats ||
      abs(accY - _lastAccY) > 0.02f || abs(heading - _lastHeading) > 1.5f ||
      fabs(lat - _lastLat) > 0.0001 || fabs(lng - _lastLng) > 0.0001 ||
      fabs(volt - _lastVolt) > 0.15f) {
    _lastSpeed = speedCmp;
    _lastRPM = rpm;
    _lastUnits = useMph;
    _lastTime = timeStr;
    _lastTrip = trip;
    _lastSats = sats;
    _lastAccY = accY;
    _lastHeading = heading;
    _lastLat = lat;
    _lastLng = lng;
    _lastVolt = volt;
    drawDashboard(false);
  }
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
  // Baris 3 (LEAN & LAT-G) diganti area navigasi
  const char *labels[4] = {"RPM", "VOLTMETER", "SATS", "DIST"};

  if (force) {
    _ui->drawStatusBar(true);

    // 4 card outlines (baris 1 & 2 saja)
    for (int row = 0; row < 2; row++) {
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

    // Area baris 3: bersihkan + gambar border gabungan untuk navigasi
    int navRowY = GRID_TOP + 2 * (CELL_H + GAP);
    tft->fillRect(MARGIN, navRowY, GRID_W, CELL_H, colBg);
    tft->drawRoundRect(MARGIN, navRowY, GRID_W, CELL_H, 5, colBorder);

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
  } cells[4] = {
      {0, 0, "%d", (float)_lastRPM}, // RPM Live
      {1, 0, "%.1f", _lastVolt},     // Voltase Aki Motor 12V
      {0, 1, "%d", (float)_lastSats},
      {1, 1, "%.1f", _lastTrip},
  };

  for (int i = 0; i < 4; i++) {
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

  // === DYNAMIC: G-Force Bar (card gaya toast, via SPRITE anti-flicker) ===
  int gCardW = 170;                      // sama dgn toast kalibrasi
  int gCardH = 44;                       // sedikit lebih tinggi utk bar+teks
  int gCardX = 298 + (177 - gCardW) / 2; // center di panel kanan (spt toast)
  int gCardY = GRID_TOP + 5;   // posisi pas spt toast "G-Force Calibrated"
  uint16_t cardColor = 0x18E3; // warna card sama dgn toast

  static TFT_eSprite *gSpr = nullptr;
  if (!gSpr) {
    gSpr = new TFT_eSprite(tft);
    gSpr->createSprite(gCardW, gCardH);
    gSpr->setFreeFont(&Org_01); // font harus di-set PADA SPRITE
  }
  gSpr->fillSprite(colBg);

  // Card background + border (sama persis dgn toast kalibrasi)
  gSpr->fillRoundRect(0, 0, gCardW, gCardH, 8, cardColor);
  gSpr->drawRoundRect(0, 0, gCardW, gCardH, 8, TFT_SILVER);

  // Bar G & teks di-center-kan di TENGAH card secara horizontal & vertikal.
  int gBarW = 120;
  int gBarH = 11;
  int gBarX = (gCardW - gBarW) / 2; // center horizontal
  // Grup konten (bar 11 + gap 5 + teks ~9px) = 25 → mulai (44-25)/2 ≈ 9
  int gBarY = 9;
  int gCenterX = gCardW / 2; // titik nol tepat di tengah card

  // Bersihkan area bar lama dgn bg card (transisi mulus di dlm sprite)
  gSpr->fillRect(gBarX, gBarY, gBarW, gBarH, cardColor);
  gSpr->drawLine(gCenterX, gBarY - 2, gCenterX, gBarY + gBarH + 2, TFT_WHITE);

  // Isi bar dari tengah berdasarkan G lateral (accY)
  float gVal = constrain(_lastAccY, -1.5f, 1.5f);
  int halfW = gBarW / 2;
  int fillLen = (int)((fabs(gVal) / 1.5f) * halfW);
  uint16_t gColor = TFT_GREEN;
  if (fabs(gVal) > 0.8f)
    gColor = TFT_YELLOW;
  if (fabs(gVal) > 1.2f)
    gColor = TFT_RED;

  if (gVal >= 0) {
    gSpr->fillRect(gCenterX, gBarY, fillLen, gBarH, gColor);
  } else {
    gSpr->fillRect(gCenterX - fillLen, gBarY, fillLen, gBarH, gColor);
  }

  // Teks DI BAWAH bar — di-center-kan dengan textWidth (tidak memotong
  // garis kotak) & diberi margin bawah cukup agar garis tidak terpotong.
  gSpr->setTextSize(1);
  gSpr->setTextColor(gColor, cardColor);
  gSpr->setTextDatum(TC_DATUM);
  gSpr->setTextPadding(gCardW - 20); // jangan beri padding selebar kotak
  sprintf(buf, "G-FORCE %.2fG", _lastAccY);
  int textW = gSpr->textWidth(buf);
  if (textW < 8)
    textW = gSpr->textWidth("G-FORCE 0.00G"); // fallback utk lebar
  int textX = (gCardW - textW) / 2;           // center horizontal yg akurat
  int textY = gBarY + gBarH + 6; // margin bawah aman dr garis kotak
  gSpr->drawString(buf, textX + textW / 2, textY);
  gSpr->setTextPadding(0);

  // Push SEKALI — anti-flicker
  gSpr->pushSprite(gCardX, gCardY);

  // === DYNAMIC: Navigasi Turn-by-Turn di kotak baris 3 (kiri bawah) ===
  // Kotak (GRID_W x CELL_H) diisi banner belokan / status rute / koordinat.
  // Dirender via SPRITE lalu di-push sekali → anti-flicker.
  int navRowY = GRID_TOP + 2 * (CELL_H + GAP);
  const int navW = GRID_W;
  const int navH = CELL_H;

  static TFT_eSprite *nSpr = nullptr;
  if (!nSpr) {
    nSpr = new TFT_eSprite(tft);
    nSpr->createSprite(navW, navH);
    nSpr->setFreeFont(&Org_01);
  }
  nSpr->fillSprite(colBg);

  // Border kotak tetap digambar di dalam sprite
  nSpr->drawRoundRect(0, 0, navW, navH, 5, colBorder);

  bool routeOn = routeNavigator.isActive();
  bool turn = routeOn && routeNavigator.hasTurnAlert();
  bool off = routeOn && routeNavigator.isOffRoute();

  if (turn) {
    // Card belokan: teal + panah + teks arah + jarak
    nSpr->fillRoundRect(3, 3, navW - 6, navH - 6, 6, 0x04DF);
    int cx = 28, cy = navH / 2;
    NavTurnDir d = routeNavigator.getTurnDirection();
    if (d == TURN_LEFT) {
      nSpr->fillTriangle(cx + 11, cy - 12, cx + 11, cy + 12, cx - 12, cy,
                         TFT_BLACK);
    } else if (d == TURN_RIGHT) {
      nSpr->fillTriangle(cx - 11, cy - 12, cx - 11, cy + 12, cx + 12, cy,
                         TFT_BLACK);
    } else { // UTURN
      nSpr->drawCircle(cx, cy, 10, TFT_BLACK);
      nSpr->fillTriangle(cx - 10, cy - 2, cx - 3, cy - 12, cx - 5, cy + 5,
                         TFT_BLACK);
    }

    char nbuf[24];
    nSpr->setTextDatum(TL_DATUM);
    nSpr->setTextColor(TFT_BLACK, 0x04DF);
    snprintf(nbuf, sizeof(nbuf), "%s", routeNavigator.getTurnText());
    nSpr->drawString(nbuf, 52, navH / 2 - 13);
    nSpr->setTextFont(2);
    snprintf(nbuf, sizeof(nbuf), "%d m", routeNavigator.getTurnDistance());
    nSpr->drawString(nbuf, 52, navH / 2 + 6);
    nSpr->setFreeFont(&Org_01);
  } else if (off) {
    // Keluar dari rute: card merah
    nSpr->fillRoundRect(3, 3, navW - 6, navH - 6, 6, 0xF800);
    nSpr->setTextDatum(MC_DATUM);
    nSpr->setTextColor(TFT_WHITE, 0xF800);
    nSpr->drawString("OFF ROUTE", navW / 2, navH / 2 - 2);
  } else {
    // Status normal: sisa rute (atau tanpa rute) + koordinat
    nSpr->setTextDatum(MC_DATUM);
    nSpr->setTextColor(TFT_GREEN, colBg);
    if (routeOn) {
      char nbuf[24];
      snprintf(nbuf, sizeof(nbuf), "RUTE %.1f km",
               routeNavigator.getRemainingKm());
      nSpr->drawString(nbuf, navW / 2, navH / 2 - 12);
    } else {
      nSpr->drawString("TANPA RUTE", navW / 2, navH / 2 - 12);
    }
    nSpr->setTextDatum(MC_DATUM);
    nSpr->setTextColor(TFT_SKYBLUE, colBg);
    char latBuf[14], lngBuf[14];
    dtostrf(_lastLat, 7, 4, latBuf);
    dtostrf(_lastLng, 8, 4, lngBuf);
    snprintf(buf, sizeof(buf), "%s, %s", latBuf, lngBuf);
    nSpr->setTextFont(1);
    nSpr->drawString(buf, navW / 2, navH / 2 + 12);
    nSpr->setFreeFont(&Org_01);
  }

  nSpr->pushSprite(MARGIN, navRowY);

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