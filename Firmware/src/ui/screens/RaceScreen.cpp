#include "RaceScreen.h"
#include "../../core/BatteryManager.h"
#include "../../core/FeedbackManager.h"
#include "../../core/GPSManager.h"
#include "../../core/IMUManager.h"
#include "../../core/SessionManager.h"
#include "../fonts/Org_01.h"
#include "LapTimerScreen.h"
#include "config.h"
#include <Preferences.h>

extern GPSManager gpsManager;
extern IMUManager imuManager;
extern SessionManager sessionManager;

void RaceScreen::onShow() {
  _lastUpdate = 0;
  _lastTouchTime = millis();
  _lapCount = 0;
  _bestLapTime = 0;
  _lapTimes.clear();
  _isRecording = false;
  _finishLineInside = false;
  _lastFinishCross = 0;
  _maxRpmSession = 0;
  _maxSpeedSession = 0.0;
  _currentLapDist = 0;
  _lastSector = 0;
  _currentDelta = 0;

  // Reset Flicker Reduction
  _lastSpeed = -999.0;
  _lastSats = -1;
  _lastRpmRender = -1;
  _lastMaxRpmRender = 0;
  _lastLapCountRender = -1;
  _lastLastLapTimeRender = -1;
  _lastBestLapTimeRender = -1;
  _maxSpeedSessionRender = -1.0;
  _maxRpmSessionRender = 0;
  _lastAccYRender = -999.0;
  _lastRollRender = -999.0;
  _lastDeltaRender = -999.0;
  _lastSector = 0;
  _notifStartTime = 0;
  _notifText = "";
  _notifRendered = false;

  for (int i = 0; i < 3; i++)
    _sessionSectorBest[i] = 0;

  // Load Beacon Width from Settings
  Preferences prefs;
  prefs.begin("laptimer", true);
  _beaconWidth = prefs.getInt("beacon_width", 50); // Default 50m
  prefs.end();

  _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                            SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
  drawRacingStatic();

  imuManager.requestActivity(true);
}

void RaceScreen::onHide() {
  imuManager.requestActivity(false);
  if (_isRecording) {
    finalizeRaceSession();
  }
}

void RaceScreen::update() {
  UIManager::TouchPoint p = _ui->getTouchPoint();
  bool touched = (p.x != -1);

  if (touched) {
    if (millis() - _lastTouchTime < TOUCH_DEBOUNCE_MS)
      return;
    _lastTouchTime = millis();

    // STOP Button (Bottom Left)
    if (p.x < 100 && p.y > 250) {
      finalizeRaceSession();
      return;
    }

    // G-Force Calibration (Double Tap)
    if (p.x >= 5 && p.x <= 135 && p.y >= 110 && p.y <= 160) {
      unsigned long now = millis();
      if (now - _lastGForceTapTime < 300) {
        _gforceTapCount++;
        if (_gforceTapCount >= 2) {
          imuManager.calibrateLevel();
          imuManager.resetMaxLean();
          _lastAccYRender = -999.0;
          _maxSpeedSession = 0; // Reset peak stats
          _maxRpmSession = 0;
          _gforceTapCount = 0;
          _ui->showToast("G-Force Calibrated", 1000);
        }
      } else {
        _gforceTapCount = 1;
      }
      _lastGForceTapTime = now;
    }
  }
  checkFinishLine();

  // Update Max Stats
  float curSpeed = gpsManager.getSpeedKmph();
  if (curSpeed > _maxSpeedSession)
    _maxSpeedSession = curSpeed;
  int curRpm = gpsManager.getRPM();
  if (curRpm > _maxRpmSession)
    _maxRpmSession = (unsigned long)curRpm;

  // Session Logging
  if (sessionManager.isLogging() && (millis() - _lastUpdate > 100)) {
    LogPacket p;
    p.header = 0xAA55;
    p.timestamp = millis();
    p.lat = (int32_t)(gpsManager.getLatitude() * 1e7);
    p.lon = (int32_t)(gpsManager.getLongitude() * 1e7);
    p.speed = (uint16_t)(gpsManager.getSpeedKmph() * 10);
    p.rpm = (uint16_t)gpsManager.getRPM();
    p.accX = (int16_t)(imuManager.getAccX() * 100);
    p.accY = (int16_t)(imuManager.getAccY() * 100);
    p.accZ = (int16_t)(imuManager.getAccZ() * 100);
    p.sats = (uint8_t)gpsManager.getSatellites();
    p.fix = (uint8_t)gpsManager.isFixed();
    p.battery = (uint8_t)BatteryManager::getInstance().getPercentage();
    p.tilt = (int16_t)(imuManager.getAngleX() * 10);
    p.checksum = 0;
    p.padding = 0;
    sessionManager.logData(p);
  }

  // Predictive Logic
  static unsigned long lastPredictiveUpdate = 0;
  static double lastLatRef = 0, lastLonRef = 0;
  if (_isRecording && millis() - lastPredictiveUpdate >= 100) {
    lastPredictiveUpdate = millis();
    double curLat = gpsManager.getLatitude();
    double curLon = gpsManager.getLongitude();

    if (lastLatRef != 0) {
      double d =
          gpsManager.distanceBetween(lastLatRef, lastLonRef, curLat, curLon);

      // EMA Smoothing for Distance (Alpha = 0.8)
      // Reduces flicker caused by GPS micro-jitter
      static double filteredD = 0;
      if (d > 0.1) { // Threshold to ignore stationary noise
        filteredD = 0.8 * d + 0.2 * filteredD;
        _currentLapDist += (float)filteredD;
      }

      if (!sessionManager.referenceLap.empty()) {
        float refTime = sessionManager.getReferenceTime(_currentLapDist);
        if (refTime > 0) {
          unsigned long lapTime = millis() - _currentLapStart;
          _currentDelta = (float)(lapTime - refTime) / 1000.0f;

          // SECTOR SPLIT LOGIC (3 Sectors)
          float totalDist = sessionManager.referenceLap.back().distance;
          bool isPurple = false;

          if (_lastSector == 0 && _currentLapDist > totalDist / 3.0f) {
            _lastSector = 1;
            float sectorTime = (float)lapTime / 1000.0f;
            float refSectorTime = refTime / 1000.0f;
            float sectorDelta = sectorTime - refSectorTime;

            // PURPLE: All-time best or session best if no all-time
            if (_sessionSectorBest[0] == 0 ||
                (unsigned long)(sectorTime * 1000) < _sessionSectorBest[0]) {
              _sessionSectorBest[0] = (unsigned long)(sectorTime * 1000);
              _notifColor = 0x801F; // Purple
              isPurple = true;
            } else {
              _notifColor = (sectorDelta <= 0) ? TFT_GREEN : TFT_RED;
            }

            _notifText = "S1";
            _notifValue =
                String(sectorDelta >= 0 ? "+" : "") + String(sectorDelta, 2);
            _notifStartTime = millis();
            _notifRendered = false;

            sessionManager.logData("SECTOR," + String(_lapCount) + ",1," +
                                   String((unsigned long)(sectorTime * 1000)));
          } else if (_lastSector == 1 &&
                     _currentLapDist > (2.0f * totalDist) / 3.0f) {
            _lastSector = 2;
            float sectorTime = (float)lapTime / 1000.0f;
            float refSectorTime = refTime / 1000.0f;
            float sectorDelta = sectorTime - refSectorTime;

            if (_sessionSectorBest[1] == 0 ||
                (unsigned long)(sectorTime * 1000) < _sessionSectorBest[1]) {
              _sessionSectorBest[1] = (unsigned long)(sectorTime * 1000);
              _notifColor = 0x801F; // Purple
              isPurple = true;
            } else {
              _notifColor = (sectorDelta <= 0) ? TFT_GREEN : TFT_RED;
            }

            _notifText = "S2";
            _notifValue =
                String(sectorDelta >= 0 ? "+" : "") + String(sectorDelta, 2);
            _notifStartTime = millis();
            _notifRendered = false;

            sessionManager.logData("SECTOR," + String(_lapCount) + ",2," +
                                   String((unsigned long)(sectorTime * 1000)));
          }
        }
      } else {
        _currentDelta = 0.0f;
      }
    }
    lastLatRef = curLat;
    lastLonRef = curLon;
  } else if (!_isRecording) {
    _currentLapDist = 0;
    lastLatRef = 0;
  }

  // Detect Notification Expiry to trigger clear
  if (_notifStartTime > 0 && (millis() - _notifStartTime >= 3000)) {
    _notifStartTime = 0;
    _notifText = "";
    _notifRendered = false;
    // Full clear and static redraw to remove the popup box
    _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                              SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
    drawRacingStatic();
    // Force redraw of all dynamic values on next drawRacing()
    _lastSpeed = -999.0;
    _lastSats = -1;
    _lastRpmRender = -1;
    _lastMaxRpmRender = 0;
    _lastLapCountRender = -1;
    _lastLastLapTimeRender = -1;
    _lastBestLapTimeRender = -1;
    _maxSpeedSessionRender = -1.0;
    _maxRpmSessionRender = 0;
    _lastAccYRender = -999.0;
    _lastRollRender = -999.0;
    _lastDeltaRender = -999.0;
  }

  if (millis() - _lastUpdate > 200) {
    drawRacing();
    _lastUpdate = millis();
  }
}

void RaceScreen::drawRacingStatic() {
  TFT_eSPI *tft = _ui->getTft();
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextPadding(0);

  int rpmY = STATUS_BAR_HEIGHT + 2;
  int rpmH = 22;
  int midY = rpmY + rpmH + 6;
  int dashH = 180;
  int sideW = 140;
  int centerW = SCREEN_WIDTH - (sideW * 2);

  // 1. RPM BAR
  tft->drawRoundRect(5, rpmY, SCREEN_WIDTH - 10, rpmH, 5, TFT_DARKGREY);
  for (int i = 1; i < 10; i++) {
    int dx = 5 + (i * (SCREEN_WIDTH - 10) / 10);
    tft->drawFastVLine(dx, rpmY + rpmH - 4, 3, TFT_SILVER);
  }

  // 2. LEFT: TRACK INFO
  int leftX = 5;
  int leftW = sideW - 10;
  tft->fillRoundRect(leftX, midY, leftW, dashH, 8, 0x18E3);
  tft->setTextColor(TFT_SILVER, 0x18E3);
  tft->setTextDatum(TC_DATUM);
  tft->drawString("TRACK INFO", leftX + leftW / 2, midY + 5);
  tft->drawFastHLine(leftX + 10, midY + 16, leftW - 20, TFT_DARKGREY);
  tft->setTextDatum(TL_DATUM);
  tft->drawString("MAX SPD", leftX + 10, midY + 25);
  tft->drawString("G-FORCE", leftX + 10, midY + 75);
  tft->drawString("SATS", leftX + 10, midY + 125);

  // 3. CENTER: SPEED
  tft->setTextColor(TFT_SILVER, TFT_BLACK);
  tft->setTextFont(2);
  tft->setTextDatum(BC_DATUM);
  tft->drawString("KM/H", SCREEN_WIDTH / 2, midY + dashH - 10);

  // 4. RIGHT: LAP TIMER
  int rightX = sideW + centerW + 5;
  int rightW = sideW - 10;
  tft->fillRoundRect(rightX, midY, rightW, dashH, 8, 0x18E3);
  tft->setTextColor(TFT_SILVER, 0x18E3);
  tft->setTextFont(1); // Sync with TRACK INFO
  tft->setTextDatum(TC_DATUM);
  tft->drawString("LAP TIMER", rightX + rightW / 2, midY + 5);
  tft->drawFastHLine(rightX + 10, midY + 16, rightW - 20, TFT_DARKGREY);
  tft->setTextDatum(TL_DATUM);
  tft->drawString("LAST", rightX + 10, midY + 25);
  tft->drawString("BEST", rightX + 10, midY + 75);
  tft->drawString("LEAN", rightX + 10, midY + 125);

  // 5. BOTTOM: PREDICTIVE
  int footerY = midY + dashH + 5;
  tft->drawRoundRect(5, footerY, SCREEN_WIDTH - 10, 80, 8, TFT_DARKGREY);

  _ui->drawStatusBar(true);
}

void RaceScreen::drawRacing() {
  TFT_eSPI *tft = _ui->getTft();
  int rpmY = STATUS_BAR_HEIGHT + 2;
  int rpmH = 22;
  int midY = rpmY + rpmH + 6;
  int dashH = 180;
  int sideW = 140;
  int centerW = SCREEN_WIDTH - (sideW * 2);
  int footerY = midY + dashH + 5;
  uint16_t cardBg = 0x18E3;

  // Check if popup is active
  bool isPopupActive =
      (_notifStartTime > 0 && millis() - _notifStartTime < 3000);

  // If Popup is active, DO NOT redraw background numbers to prevent
  // flickering/fighting
  if (!isPopupActive) {
    // RPM
    int currentRpm = gpsManager.getRPM();
    if (abs(currentRpm - _lastRpmRender) > 50) {
      drawRPMBar(currentRpm, 10000);
      _lastRpmRender = currentRpm;
    }

    // Speed
    float speed = gpsManager.getSpeedKmph();
    if (abs(speed - _lastSpeed) > 0.1) {
      tft->setTextFont(7);
      tft->setTextColor(TFT_WHITE, TFT_BLACK);
      tft->setTextDatum(MC_DATUM);
      tft->setTextSize(2);
      tft->setTextPadding(
          tft->textWidth("999")); // Fixed padding for up to 3 digits
      tft->drawString(String((int)speed), SCREEN_WIDTH / 2, midY + 85);
      tft->setTextPadding(0);
      _lastSpeed = speed;
      tft->setTextSize(1);
      tft->setTextFont(1);
    }

    // Left Column
    if (abs(_maxSpeedSessionRender - _maxSpeedSession) > 0.1) {
      tft->setTextColor(TFT_WHITE, cardBg);
      tft->setTextFont(4);
      tft->setTextDatum(TL_DATUM);
      tft->setTextPadding(tft->textWidth("8888")); // Widen to 4 digits of 8s
      tft->drawString(String((int)_maxSpeedSession), 5 + 10, midY + 42);
      tft->setTextPadding(0);
      _maxSpeedSessionRender = _maxSpeedSession;
    }
    int sats = gpsManager.getSatellites();
    if (sats != _lastSats) {
      tft->setTextColor(TFT_WHITE, cardBg);
      tft->setTextFont(4);
      tft->setTextDatum(TL_DATUM);
      tft->setTextPadding(tft->textWidth("888")); // Widen to 3 digits
      tft->drawString(String(sats), 5 + 10, midY + 142);
      tft->setTextPadding(0);
      _lastSats = sats;
    }
    float accY = imuManager.getAccX();
    if (abs(accY - _lastAccYRender) > 0.05) {
      tft->setTextColor(TFT_WHITE, cardBg);
      tft->setTextFont(4);
      tft->setTextPadding(tft->textWidth("-8.88G")); // Use 8s for max width
      tft->setTextDatum(TL_DATUM);
      char gBuf[16];
      sprintf(gBuf, "%.2fG", accY);
      tft->drawString(String(gBuf), 5 + 10, midY + 92);
      tft->setTextPadding(0);
      _lastAccYRender = accY;
    }

    // Right Column (Lap Times)
    auto formatTime = [](long t) {
      if (t <= 0)
        return String("--:--.--");
      int ms = (t % 1000) / 10;
      int s = (t / 1000) % 60;
      int m = (t / 60000);
      char b[16];
      sprintf(b, "%d:%02d.%02d", m, s, ms);
      return String(b);
    };

    // Calculate padding using 8s and extra minute digit for safety
    int timePadding = tft->textWidth("888:88.88", 2);

    if (_lastLapTime != _lastLastLapTimeRender) {
      tft->setTextColor(TFT_WHITE, cardBg);
      tft->setTextFont(2);
      tft->setTextDatum(TL_DATUM);
      tft->setTextPadding(timePadding);
      tft->drawString(formatTime(_lastLapTime), sideW + centerW + 15,
                      midY + 42);
      tft->setTextPadding(0);
      _lastLastLapTimeRender = _lastLapTime;
    }
    if (_bestLapTime != _lastBestLapTimeRender) {
      tft->setTextColor(TFT_GOLD, cardBg);
      tft->setTextFont(2);
      tft->setTextDatum(TL_DATUM);
      tft->setTextPadding(timePadding);
      tft->drawString(formatTime(_bestLapTime), sideW + centerW + 15,
                      midY + 92);
      tft->setTextPadding(0);
      _lastBestLapTimeRender = _bestLapTime;
    }

    // Lean Angle (Right Column)
    float roll = imuManager.getLeanAngle();
    if (abs(roll - _lastRollRender) > 0.5) {
      tft->setTextColor(TFT_WHITE, cardBg);
      tft->setTextFont(4);
      tft->setTextDatum(TL_DATUM);
      tft->setTextPadding(tft->textWidth("-888.8"));

      // Current Lean
      tft->drawString(String(abs(roll), 1), sideW + centerW + 15, midY + 142);

      // Max Lean Left/Right (Small)
      tft->setTextFont(1);
      tft->setTextSize(1);
      tft->setTextColor(TFT_SILVER, cardBg);

      tft->setTextPadding(0);
      _lastRollRender = roll;
    }

    // Footer (Predictive)
    if (_isRecording && !sessionManager.referenceLap.empty()) {
      uint16_t deltaColor = (_currentDelta <= 0) ? TFT_GREEN : TFT_RED;
      tft->fillRoundRect(10, footerY + 5, SCREEN_WIDTH - 20, 70, 6, deltaColor);
      tft->setTextColor(TFT_WHITE, deltaColor);
      tft->setTextFont(6);
      tft->setTextDatum(MC_DATUM);
      char dBuf[16];
      snprintf(dBuf, sizeof(dBuf), "%s%.2f", (_currentDelta >= 0 ? "+" : ""),
               _currentDelta);
      tft->drawString(dBuf, SCREEN_WIDTH / 2, footerY + 32);
    }
  }

  // Sector / Notification Overlay (Refined: Black/White Premium)
  if (_notifStartTime > 0 && millis() - _notifStartTime < 3000) {
    if (!_notifRendered) {
      int popupW = SCREEN_WIDTH - 80;
      int popupH = 140;
      int popupX = (SCREEN_WIDTH - popupW) / 2;
      int popupY = (SCREEN_HEIGHT - popupH) / 2;

      // Background: Black, Border/Stroke: White (Two lines of border for extra
      // weight)
      tft->fillRoundRect(popupX, popupY, popupW, popupH, 12, TFT_BLACK);
      tft->drawRoundRect(popupX, popupY, popupW, popupH, 12, TFT_WHITE);
      tft->drawRoundRect(popupX + 1, popupY + 1, popupW - 2, popupH - 2, 11,
                         TFT_WHITE);

      tft->setTextDatum(MC_DATUM);

      if (_notifValue.length() > 0) {
        // Line 1: Label (BEST TIMING, SECTOR X, etc.)
        tft->setTextColor(TFT_WHITE, TFT_BLACK);
        tft->setTextFont(4);
        tft->drawString(_notifText, SCREEN_WIDTH / 2, popupY + 45);

        // Line 2: Value (1:12.80) - Larger and Vibrant Color
        tft->setTextFont(6);
        tft->setTextColor(TFT_GOLD, TFT_BLACK);
        tft->drawString(_notifValue, SCREEN_WIDTH / 2, popupY + 95);
      } else {
        // Single line centered
        tft->setTextColor(TFT_WHITE, TFT_BLACK);
        tft->setTextFont(4);
        tft->drawString(_notifText, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
      }
      _notifRendered = true;
    }
  }
}

void RaceScreen::drawRPMBar(int rpm, int maxRpm) {
  TFT_eSPI *tft = _ui->getTft();
  int xStart = 7;
  int y = STATUS_BAR_HEIGHT + 7;
  int w = SCREEN_WIDTH - 14;
  int h = 16;
  if (maxRpm < 5000)
    maxRpm = 10000;

  int segments = 20;
  int segW = (w / segments) - 2;
  int activeSegs = map(constrain(rpm, 0, maxRpm), 0, maxRpm, 0, segments);

  for (int i = 0; i < segments; i++) {
    int sx = xStart + (i * (segW + 2));
    uint16_t color =
        (i < activeSegs)
            ? (i < 12 ? TFT_GREEN : (i < 16 ? TFT_ORANGE : TFT_RED))
            : 0x10A2;
    tft->fillRect(sx, y, segW, h, color);
  }

  FeedbackManager &fb = FeedbackManager::getInstance();
  float p = (float)rpm / maxRpm;
  if (p >= 0.98) {
    fb.setLed(0, 0, 255);
  } else if (p >= 0.90)
    fb.setLed(255, 0, 0);
  else if (p >= 0.80)
    fb.setLed(255, 128, 0);
  else if (p >= 0.60)
    fb.setLed(0, 255, 0);
  else
    fb.setLed(0, 0, 0);
}

void RaceScreen::checkFinishLine() {
  double lat = gpsManager.getLatitude();
  double lon = gpsManager.getLongitude();
  double dist = gpsManager.distanceBetween(lat, lon, _currentTrack.lat,
                                           _currentTrack.lon);

  float radius = _beaconWidth / 2.0;

  if (dist < radius) {
    if (!_finishLineInside && (millis() - _lastFinishCross > 10000)) {
      if (!_isRecording) {
        _isRecording = true;
        sessionManager.startSession();
        _lapCount = 1;
        _currentLapStart = millis();
      } else {
        // --- INTERPOLATION FOR HIGH PRECISION ---
        unsigned long now = millis();
        unsigned long lapTimeRaw = now - _currentLapStart;

        // If we have a previous point inside the approach path
        if (_lastDistToFinish > 0 && _lastDistToFinish > dist) {
          // Total distance covered between samples
          double totalStep = _lastDistToFinish + dist;
          // % of step that was BEFORE finish line
          double ratio = _lastDistToFinish / totalStep;
          // Time delta between samples
          unsigned long timeDelta = now - _lastPointTime;

          // Adjusted timestamp for crossing
          unsigned long offset = (unsigned long)(ratio * (double)timeDelta);
          unsigned long interpLapTime =
              (_lastPointTime + offset) - _currentLapStart;

          _lastLapTime = interpLapTime;
          _currentLapStart =
              _lastPointTime +
              offset; // Reset start for next lap to the interp point

          // LOG LAP TO CSV
          sessionManager.logData("LAP," + String(_lapCount) + "," +
                                 String(interpLapTime));

          // Also log the 3rd sector (finish line)
          sessionManager.logData(
              "SECTOR," + String(_lapCount) + ",3," +
              String(interpLapTime - _sessionSectorBest[0] -
                     _sessionSectorBest[1])); // Rough estimate if we don't
                                              // have better sector logic
                                              // for S3

          DEBUG_PRINTF("LAP: Raw=%lu ms, Interp=%lu ms (Diff: %ld ms)\n",
                       lapTimeRaw, interpLapTime,
                       (long)interpLapTime - (long)lapTimeRaw);
        } else {
          _lastLapTime = lapTimeRaw;
          _currentLapStart = now;

          // LOG LAP TO CSV
          sessionManager.logData("LAP," + String(_lapCount) + "," +
                                 String(lapTimeRaw));
        }

        _lapTimes.push_back(_lastLapTime);

        // FINISH LINE NOTIFICATION (Combined Best Lap & Normal Lap)
        // Always show popup for lap time, prioritizing Best Lap status

        // Format Time: M:SS.ms
        int ms = (_lastLapTime % 1000) / 10;
        int s = (_lastLapTime / 1000) % 60;
        int m = (_lastLapTime / 60000);
        char tBuf[16];
        sprintf(tBuf, "%d:%02d.%02d", m, s, ms);
        String timeStr = String(tBuf);

        if (_bestLapTime == 0 || _lastLapTime < _bestLapTime) {
          // NEW SESSION BEST!
          _notifColor = 0x801F; // Purple
          _notifText = timeStr; // Just time, Star handled in draw
          // Update Best Trigger is slightly later, but we capture state
          // here
        } else {
          // NORMAL LAP
          // Check delta against reference if available for color context
          if (!sessionManager.referenceLap.empty()) {
            float refFinalTime =
                (float)sessionManager.referenceLap.back().time / 1000.0f;
            // Green if faster than ref, Red if slower
            // Note: Reference is usually best lap. If slower, Red.
            if ((float)_lastLapTime / 1000.0f > refFinalTime) {
              _notifColor = TFT_RED;
            } else {
              _notifColor = TFT_GREEN;
            }
          } else {
            _notifColor = TFT_GREEN; // Default to Green if no reference
          }
          _notifText = "LAP";
          _notifValue = timeStr;
        }

        _notifStartTime = millis(); // Always trigger popup
        _notifRendered = false;

        if (_bestLapTime == 0 || _lastLapTime < _bestLapTime) {
          _bestLapTime = _lastLapTime;
          _notifText = "BEST TIMING";
          _notifValue = timeStr;
          FeedbackManager::getInstance().blinkLed(
              0, 255, 0, 1000); // Green LED for Best Lap

          // PHASE 3: PROMOTE TO REFERENCE
          // If this is a new session best, promote it to the live reference
          // lap so deltas in the NEXT lap are compared against this new
          // best.
          sessionManager.promoteLastLapToReference();
        }
        _lapCount++;
      }

      _lastFinishCross = millis();
      _finishLineInside = true;
      _currentLapDist = 0;
      _lastSector = 0; // Reset for next lap
    }
  } else if (dist > (radius + 5.0)) {
    _finishLineInside = false;
  }

  // Store for next interpolation
  _lastLat = lat;
  _lastLon = lon;
  _lastDistToFinish = dist;
  _lastPointTime = millis();
}

void RaceScreen::finalizeRaceSession() {
  if (sessionManager.isLogging()) {
    String dateStr =
        gpsManager.getDateString() + " " + gpsManager.getTimeString();
    sessionManager.appendToHistoryIndex("Race Session", dateStr, _lapCount,
                                        _bestLapTime, "TRACK");
  }
  sessionManager.stopSession();
  _isRecording = false;
  _ui->switchScreen(SCREEN_LAP_TIMER);
}

void RaceScreen::drawTrackMap(int x, int y, int w, int h) {
  // Basic map logic would go here if needed
}
