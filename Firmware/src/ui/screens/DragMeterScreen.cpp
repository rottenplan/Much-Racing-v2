#include "DragMeterScreen.h"
#include "../../core/FeedbackManager.h"
#include "../../core/GPSManager.h"
#include "../../core/IMUManager.h"
#include "../../core/SessionManager.h"
#include "../fonts/Org_01.h"
#include "HistoryScreen.h"
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>

extern GPSManager gpsManager;
extern SessionManager sessionManager;
extern IMUManager imuManager;

// Tentukan subset disiplin untuk dilacak
// 0-60 km/h
// 0-100 km/h
// 100-200 km/h
// 402m (1/4 Mile)

// 0-100 km/h
// 100-200 km/h
// 402m (1/4 Mile)

void DragMeterScreen::onShow() {
  _state = STATE_MENU;
  _displayMode = DISPLAY_NORMAL;
  _selectedMenuIdx = -1;
  _selectedSettingIdx = -1;
  _lastTapIdx = -1;
  _lastTapTime = 0;
  _menuItems.clear();
  _menuItems.push_back("DRAG SCREEN");
  _menuItems.push_back("HISTORY");
  _menuItems.push_back("SETTING");
  refreshSettingLabels();

  // Load disciplines based on custom settings
  loadCustomDisciplines();

  _currentSpeed = 0.0;
  _slope = 0.0;
  _highlightTitle = "400 m";
  _highlightValue = "0.0s";

  TFT_eSPI *tft = _ui->getTft();
  // Clear only content area - Redundant, UIManager already clears
  // tft->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
  //               SCREEN_HEIGHT - STATUS_BAR_HEIGHT, COLOR_BG);
  _ui->setTitle("DRAG METER");
  drawDashboardStatic(true);

  // Reset Run State
  _runState = RUN_WAITING;
  _oneFootReached = false;
  _startPosition = 0;

  // Load Settings
  Preferences p;
  p.begin("laptimer", true);
  _rolloutEnabled = p.getBool("rollout", false); // Default false
  // _treeInterval is set via Settings Screen (Saved as Index: 0=3s, 1=5s, 2=7s)
  int treeIdx = p.getInt("drag_tree_sec", 1); // Default Index 1 (5s)
  if (treeIdx == 0)
    _treeInterval = 3000;
  else if (treeIdx == 2)
    _treeInterval = 7000;
  else
    _treeInterval = 5000; // Default or Index 1

  int targetIdx = p.getInt("drag_target", 2); // Default index 2 (10.0s)
  _customStartKph = p.getInt("dr_start_kph", 0);
  _customEndKph = p.getInt("dr_end_kph", 100);
  _customDist20m = p.getInt("dr_dist_20", 20);
  _customDist30m = p.getInt("dr_dist_30", 30);
  _customDist35m = p.getInt("dr_dist_35", 35);
  p.end();

  refreshSettingLabels();  // Update menu items with loaded values
  loadCustomDisciplines(); // Reset disciplines and current results

  _lastTreeCount = -1;
  _lastTreeIsGo = false;
  _wasOverlayActive = false;
  _lastHighlightBgColor = 0xFFFF; // Force first draw

  // Reset coordinates to force a fresh lock on next motion
  _startLat = 0;
  _startLon = 0;
  _startAlt = 0;
  imuManager.calibrateLevel();
  imuManager.requestActivity(true);
}

void DragMeterScreen::update() {
  static unsigned long lastDragTouch = 0;
  UIManager::TouchPoint p = _ui->getTouchPoint();

  // 1. Touch Handling (debounced)
  if (p.x != -1 && (millis() - lastDragTouch > TOUCH_DEBOUNCE_MS)) {
    lastDragTouch = millis();

    // 1.1 Global Back Button Check (Hierarchical Navigation)
    if (p.x < 80 && p.y > 275) {
      if (_state == STATE_VALUE_EDITOR) {
        // From value editor → back to settings menu
        _state = STATE_SETTING_MENU;
        _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT, COLOR_BG);
        drawDashboardStatic(true);
      } else if (_state == STATE_SETTING_MENU) {
        // From settings menu → back to drag meter main menu
        _state = STATE_MENU;
        _ui->setTitle("DRAG METER");
        _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT, COLOR_BG);
        drawDashboardStatic(true);
      } else if (_state == STATE_MENU) {
        // From drag meter main menu → back to main menu
        _ui->switchScreen(SCREEN_MENU);
      } else {
        // From any other state → back to drag meter main menu
        _state = STATE_MENU;
        _ui->setTitle("DRAG METER");
        _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT, COLOR_BG);
        drawDashboardStatic(true);
      }
      return;
    }

    // 1.2 State-specific Touch Logic
    int touchedIdx = -1;
    if (_state == STATE_MENU) {
      // 3 items: Height 50, Gap 12 (Matched to drawGenericMenu)
      touchedIdx = getTouchedIndex(60, 50, 12, 360, p);
      if (touchedIdx != -1) {
        if (_selectedMenuIdx != touchedIdx) {
          _selectedMenuIdx = touchedIdx;
          drawDashboardStatic(false);
        } else {
          // Double tap (tap on already selected item) -> Execute
          handleMenuTouch(touchedIdx);
        }
      }
    } else if (_state == STATE_SETTING_MENU) {
      // 4 items: Height 45, Gap 8 (Matched to drawGenericMenu)
      touchedIdx = getTouchedIndex(60, 45, 8, 360, p);
      if (touchedIdx != -1) {
        if (_selectedSettingIdx != touchedIdx) {
          _selectedSettingIdx = touchedIdx;
          drawDashboardStatic(false);
        }
        handleSettingTouch(touchedIdx);
      }
    } else if (_state == STATE_VALUE_EDITOR) {
      handleValueTouch(p);
    } else if (_state == STATE_RUNNING) {
      // 1.3 Bottom Right Toggle for Christmas Tree Duration (Blue Arrow Area)
      // Area: X > SCREEN_WIDTH - 60, Y > SCREEN_HEIGHT - 60
      if (p.x > SCREEN_WIDTH - 60 && p.y > SCREEN_HEIGHT - 60) {
        startChristmasTree(); // Start the sequence (enters READY state)
      }

      // 1.4 Tree Ready State Touch (Start Countdown)
      if (_runState == RUN_TREE_READY) {
        // Simple bounds check based on display mode approximation
        bool hit = false;
        if (_displayMode == DISPLAY_PREDICTIVE) {
          if (p.y > 125 && p.y < SCREEN_HEIGHT - 45 && p.x > 10 &&
              p.x < SCREEN_WIDTH - 10)
            hit = true;
        } else {
          if (p.y > 130 && p.y < SCREEN_HEIGHT - 50 && p.x > SCREEN_WIDTH / 2)
            hit = true;
        }

        if (hit) {
          _runState = RUN_COUNTDOWN;
          _startTime = millis();
          // Force redraw to clear READY
          _lastTreeCount = -1;
          _lastTreeIsGo = false;
          drawDashboardStatic(false);
        }
      }
    }
  }

  // 2. Continuous Logic (Independent of Touch)
  if (_state == STATE_RUNNING) {
    _currentSpeed = gpsManager.getSpeedKmph(); // Live speed update for display

    // Run Logic State Machine
    if (_runState == RUN_WAITING) {
      checkStartCondition(); // Auto-motion detection
    } else if (_runState == RUN_COUNTDOWN) {
      unsigned long elapsed = millis() - _startTime;
      if (elapsed >= _treeInterval) {
        _runState = RUN_RUNNING;
        _runStartTime = millis();
        sessionManager.startSession();
        drawDashboardStatic(false); // Update title to GO!
      }
    } else if (_runState == RUN_RUNNING) {
      checkStopCondition();
      updateDisciplines();
    }

    // Throttle drawing to ~30 FPS (33ms)
    unsigned long now = millis();
    if (now - _lastDrawTime >= 33) {
      _lastDrawTime = now;

      // Overlay rendering logic
      bool inGoPhase =
          (_runState == RUN_RUNNING && (millis() - _runStartTime < 1000));
      bool showOverlay = (_runState == RUN_COUNTDOWN || inGoPhase ||
                          _runState == RUN_TREE_READY);

      drawDashboardDynamic();

      if (showOverlay) {
        drawChristmasTreeOverlay();
      } else if (_wasOverlayActive) {
        _lastHighlightBgColor = 0xFFFF; // Force area refresh
        drawDashboardDynamic();
        FeedbackManager::getInstance().setLed(0, 0, 0); // Led Off
      }
      _wasOverlayActive = showOverlay;
    }
  }
}

void DragMeterScreen::checkStartCondition() {
  float speed = gpsManager.getSpeedKmph();
  if (speed > 2.5) { // Moving threshold (Increased to filter GPS jitter)
    unsigned long now = millis();

    if (_runState == RUN_WAITING) {
      // First motion detection
      _startLat = gpsManager.getLatitude();
      _startLon = gpsManager.getLongitude();
      _startAlt = gpsManager.getAltitude();
      _startPosition = 0;
      _totalRunDistance = 0;
    }

    if (_rolloutEnabled) {
      // For rollout, we track distance from initial movement
      double dist = gpsManager.distanceBetween(_startLat, _startLon,
                                               gpsManager.getLatitude(),
                                               gpsManager.getLongitude());
      _startPosition = dist; // approximate rollout distance
      _lastUpdate = now;

      if (_startPosition >= 0.3048) { // 1 ft
        _runState = RUN_RUNNING;
        _runStartTime = now;
        _oneFootReached = true;
        drawDashboardStatic(false);
        _previousSpeed = speed;
        _previousRunTime = 0;
        _previousDistance = 0;

        // Mark distance start for disciplines
        _startLat = gpsManager.getLatitude();
        _startLon = gpsManager.getLongitude();
        _startAlt = gpsManager.getAltitude();

        // Reset disciplines for fresh start from 1ft
        for (auto &d : _disciplines) {
          d.completed = false;
          d.resultTime = 0;
          d.brakingDistance = -1; // Reset start speed marker
        }
      }

    } else {
      // Immediate Start
      _runState = RUN_RUNNING;
      _runStartTime = millis();
      drawDashboardStatic(false); // Update title to GO!
      _previousSpeed = speed;
      _previousRunTime = 0;
      _previousDistance = 0;
      // Reset disciplines
      for (auto &d : _disciplines) {
        d.completed = false;
        d.resultTime = 0;
      }
    }

    if (_runState == RUN_RUNNING) {
      sessionManager.startSession();
    }
  } else {
    _lastUpdate = millis(); // Keep updating time while stationary
    _startPosition = 0;     // Reset rollout if stopped
  }
}

// ... functions ...

void DragMeterScreen::drawSpeedArea(bool dynamicOnly) {
  TFT_eSPI *tft = _ui->getTft();
  int speedCardY = 30;
  int speedCardH = 90;

  if (!dynamicOnly) {
    tft->fillRoundRect(10, speedCardY, SCREEN_WIDTH - 20, speedCardH, 8,
                       0x18E3);
    tft->drawRoundRect(10, speedCardY, SCREEN_WIDTH - 20, speedCardH, 8,
                       0x7BEF);

    // "KPH" Label
    tft->setTextColor(0xC618, 0x18E3);
    tft->setTextDatum(2); // 2
    tft->setFreeFont(&Org_01);
    tft->drawString("KPH", SCREEN_WIDTH - 25, speedCardY + 10);
  }

  // Dynamic Labels
  tft->setTextColor(0xFFFF, 0x18E3); // 0xFFFF
  tft->setTextDatum(1);              // TC_DATUM
  tft->setTextFont(7);
  tft->setTextSize(1);
  tft->setTextPadding(240);
  tft->drawString(String(_currentSpeed, 1), SCREEN_WIDTH / 2, speedCardY + 15);
  tft->setTextPadding(0);
}

void DragMeterScreen::drawPredictiveMode() {
  TFT_eSPI *tft = _ui->getTft();

  // 1. Update Speed (Top Area)
  drawSpeedArea(true);

  // 2. Find Longest Distance Discipline for Label
  String discName = "--- m";
  if (!_disciplines.empty()) {
    // Usually the last one is the longest distance
    discName = _disciplines.back().name;
  }

  // 3. Central Predictive Box & Footer (SKIP IF COUNTDOWN OR GO!)
  bool inGoPhase =
      (_runState == RUN_RUNNING && (millis() - _runStartTime < 1000));
  if (_runState == RUN_COUNTDOWN || inGoPhase) {
    // Don't draw predictive elements during countdown/GO to avoid overlap
    _lastHighlightBgColor = 0xFFFF; // Reset to force redraw when countdown ends
  } else {
    int boxY = 125;
    int boxW = SCREEN_WIDTH - 20;
    int boxH = SCREEN_HEIGHT - boxY - 45;
    int boxX = 10;

    calculatePrediction();

    // Determine color based on target
    uint16_t boxColor = 0x07E0;
    if (_targetTime > 0 && _predictedFinalTime > 0) {
      float delta = _predictedFinalTime - _targetTime;
      if (abs(delta) > 0.1) {
        boxColor = (delta > 0.1) ? 0xF800 : 0x07E0;
      }
    }

    // Draw Box ONLY if color changed OR forced
    if (boxColor != _lastHighlightBgColor) {
      _lastHighlightBgColor = boxColor;
      tft->fillRoundRect(boxX, boxY, boxW, boxH, 8, boxColor);
      tft->drawRoundRect(boxX, boxY, boxW, boxH, 8, 0xFFFF);

      // Label inside box (static part)
      tft->setTextColor(0x0000, boxColor);
      tft->setTextDatum(0);
      tft->setFreeFont(NULL);
      tft->setTextSize(1);
      tft->drawString(discName + " predictive", boxX + 10, boxY + 10);
    }

    // Big Predicted Time (Always Update with Padding)
    tft->setTextColor(0x0000, _lastHighlightBgColor);
    tft->setTextDatum(4); // 4
    tft->setTextFont(7);
    tft->setTextSize(2);
    String timeStr =
        (_predictedFinalTime > 0) ? String(_predictedFinalTime, 2) : "--.--";
    tft->setTextPadding(boxW - 20);
    tft->drawString(timeStr, SCREEN_WIDTH / 2, boxY + boxH / 2 + 10);
    tft->setTextPadding(0);

    // 4. Footer Values
    tft->setTextColor(0xFFFF, _ui->getBackgroundColor());
    tft->setTextDatum(6);
    tft->setFreeFont(&Org_01);
    tft->setTextSize(1);
    String targetStr =
        "Target: " + (_targetTime > 0 ? String(_targetTime, 2) : "--.--");
    tft->setTextPadding(150);
    tft->drawString(targetStr, 10, SCREEN_HEIGHT - 5);

    // Secondary Info (e.g. 200m time)
    tft->setTextDatum(8);
    String secondaryStr = "-- m: --.--";
    if (_disciplines.size() >= 2) {
      for (const auto &d : _disciplines) {
        if (d.name.indexOf("200") != -1 && d.completed) {
          secondaryStr = d.name + ": " + String(d.resultTime / 1000.0, 2);
          break;
        }
      }
    }
    tft->setTextPadding(150);
    tft->drawString(secondaryStr, SCREEN_WIDTH - 10, SCREEN_HEIGHT - 5);
    tft->setTextPadding(0);
  }

  // Font Safety
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
}

void DragMeterScreen::calculatePrediction() {
  // Basic prediction: Use reference time
  _predictedFinalTime = _referenceTime;
}

void DragMeterScreen::saveReferenceRun() {
  if (_disciplines.empty())
    return;

  // Calculate final time of the longest discipline?
  // We iterate to find the longest target discipline.
  // Simplifying: The last one is usually the longest or the "Result".
  Discipline *best = nullptr;
  for (auto &d : _disciplines) {
    if (d.completed)
      best = &d;
  }

  if (best && best->completed) {
    float runTime = best->resultTime / 1000.0;

    // Save if faster (runTime < _referenceTime) or if no reference exists
    // Note: Faster means LOWER time.
    if (_referenceTime <= 0.0 || runTime < _referenceTime) {
      _referenceTime = runTime;

      Preferences p;
      p.begin("laptimer", false);
      p.putFloat("drag_ref", _referenceTime);
      p.end();
    }
  }
}

void DragMeterScreen::checkStopCondition() {
  // Only process if we are currently in a run
  if (_runState != RUN_RUNNING)
    return;

  if (gpsManager.getSpeedKmph() < 0.5) {
    // Only stop if we actually started (which we did if we are here)

    saveReferenceRun(); // Save if good run

    // Save to History
    // Use the first discipline as the primary result
    unsigned long resultTime = 0;
    String runName = "Drag Run";

    // Find the first completed discipline to use as the primary result
    for (const auto &d : _disciplines) {
      if (d.completed) {
        runName = d.name;
        resultTime = d.resultTime;
        break;
      }
    }

    // Fallback: If no discipline is marked 'completed' (e.g. aborted run but
    // some data), try to find one with a valid time > 0
    if (resultTime == 0 && !_disciplines.empty()) {
      for (const auto &d : _disciplines) {
        if (d.resultTime > 0) {
          runName = d.name;
          resultTime = d.resultTime;
          break;
        }
      }
    }

    sessionManager.stopSession();
    // Use the actual filename if we were logging
    String actualFilename = sessionManager.getCurrentFilename();
    if (actualFilename.length() == 0)
      actualFilename = "DragRun"; // Fallback

    String dateStr =
        gpsManager.getDateString() + " " + gpsManager.getTimeString();
    // Use '1' for run count, and resultTime for bestLap (repurposed field)
    sessionManager.appendToHistoryIndex(actualFilename, dateStr, 1, resultTime,
                                        "DRAG");

    _runState = RUN_FINISHED;

    // Go to Summary
    _state = STATE_SUMMARY_VIEW;
    _ui->getTft()->fillScreen(_ui->getBackgroundColor());
    drawSummary();
  }
}

void DragMeterScreen::updateDisciplines() {
  float speed = gpsManager.getSpeedKmph();
  unsigned long now = millis();
  unsigned long runTime = now - _runStartTime;

  // Geometric Distance
  double currentLat = gpsManager.getLatitude();
  double currentLon = gpsManager.getLongitude();
  double currentAlt = gpsManager.getAltitude();

  // Calculate total run distance from start point
  _totalRunDistance =
      gpsManager.distanceBetween(_startLat, _startLon, currentLat, currentLon);

  // Calculate Slope
  if (_totalRunDistance > 50.0) {
    float rise = currentAlt - _startAlt;
    _slope = (rise / _totalRunDistance) * 100.0;
  } else {
    _slope = 0.0;
  }

  // Log Binary Packet
  if (_runState == RUN_RUNNING && sessionManager.isLogging()) {
    LogPacket p;
    p.header = 0xAA55;
    p.timestamp = millis();
    p.lat = (int32_t)(currentLat * 1e7);
    p.lon = (int32_t)(currentLon * 1e7);
    p.speed = (uint16_t)(speed * 10);
    p.rpm = (uint16_t)gpsManager.getRPM();
    p.accX = (int16_t)(imuManager.getAccX() * 100);
    p.accY = (int16_t)(imuManager.getAccY() * 100);
    p.accZ = (int16_t)(imuManager.getAccZ() * 100);
    p.sats = (uint8_t)gpsManager.getSatellites();
    p.fix = (uint8_t)gpsManager.isFixed();
    p.battery = 0;
    p.tilt = (int16_t)(imuManager.getAngleX() * 10);
    p.checksum = 0;
    p.padding = 0;
    sessionManager.logData(p);
  }

  // Check disciplines
  bool allComplete = true;
  for (auto &d : _disciplines) {
    if (!d.completed) {
      allComplete = false;
      d.slope = _slope;
      if (speed > d.peakSpeed)
        d.peakSpeed = speed;

      if (d.isDistance) {
        if (_totalRunDistance >= d.target) {
          d.completed = true;
          // Interpolate
          if (_previousDistance < d.target &&
              _totalRunDistance > _previousDistance) {
            float ratio = (d.target - _previousDistance) /
                          (_totalRunDistance - _previousDistance);
            d.resultTime =
                _previousRunTime +
                (unsigned long)(ratio * (runTime - _previousRunTime));
          } else {
            d.resultTime = runTime;
          }
          d.endSpeed = speed;
          d.valid = (d.slope >= -1.0);
        }
      } else {
        // Speed to Speed Logic
        // 1. Mark Start Time (when hitting d.startSpeed)
        if (d.brakingDistance < 0 && speed >= d.startSpeed) {
          // Interpolate start time if possible
          if (_previousSpeed < d.startSpeed && speed > _previousSpeed) {
            float ratio =
                (d.startSpeed - _previousSpeed) / (speed - _previousSpeed);
            d.brakingDistance = (float)_previousRunTime +
                                (ratio * (float)(runTime - _previousRunTime));
          } else {
            d.brakingDistance = (float)runTime;
          }
        }

        // 2. Check Completion (when hitting d.target)
        if (d.brakingDistance >= 0 && speed >= d.target) {
          d.completed = true;
          unsigned long endTime = runTime;
          // Interpolate end time
          if (_previousSpeed < d.target && speed > _previousSpeed) {
            float ratio =
                (d.target - _previousSpeed) / (speed - _previousSpeed);
            endTime = _previousRunTime +
                      (unsigned long)(ratio * (runTime - _previousRunTime));
          }
          d.resultTime = endTime - (unsigned long)d.brakingDistance;
          d.valid = (d.slope >= -1.0);
        }
      }
    }
  }

  _previousSpeed = speed;
  _previousRunTime = runTime;
  _previousDistance = _totalRunDistance;

  // Abort on False Start
  if (_runState == RUN_COUNTDOWN && speed > 2.0) {
    FeedbackManager::getInstance().setLed(255, 0, 0);
    _runState = RUN_FINISHED;
    _ui->setTitle("FALSE START");
    drawDashboardStatic(false);
  }

  // Update Highlight Display
  if (!_disciplines.empty()) {
    _highlightTitle = _disciplines[0].name;
    if (_disciplines[0].completed) {
      _highlightValue = String(_disciplines[0].resultTime / 1000.0, 2) + "s";
    } else {
      // Show timer for active run
      if (_runState == RUN_RUNNING) {
        // For speed-to-speed runs, show the interval timer if started
        if (!_disciplines[0].isDistance &&
            _disciplines[0].brakingDistance >= 0) {
          float currentInterval =
              (float)runTime - _disciplines[0].brakingDistance;
          _highlightValue = String(currentInterval / 1000.0, 1) + "s";
        } else {
          _highlightValue = String(runTime / 1000.0, 1) + "s";
        }
      } else {
        _highlightValue = "0.0s";
      }
    }
  }
}

void DragMeterScreen::startChristmasTree() {
  _runState = RUN_TREE_READY;
  _lastTreeCount = -1;
  _lastTreeIsGo = false;

  // LED: READY (Red/Amber) - Using RED as requested
  FeedbackManager::getInstance().setLed(255, 0, 0);
  // _startTime will be set later
}

void DragMeterScreen::drawChristmasTreeOverlay() {
  TFT_eSPI *tft = _ui->getTft();

  // Box Coordinates (Recalculated or reused)
  int boxX, boxY, boxW, boxH;
  if (_displayMode == DISPLAY_PREDICTIVE) {
    boxY = 125;
    boxW = SCREEN_WIDTH - 20;
    boxH = SCREEN_HEIGHT - boxY - 45;
    boxX = 10;
  } else {
    int splitX = SCREEN_WIDTH / 2;
    int bottomY = 130;
    int bottomH = SCREEN_HEIGHT - bottomY - 50;
    boxX = splitX + 5;
    boxY = bottomY;
    boxW = splitX - 15;
    boxH = bottomH;
  }

  // --- READY STATE ---
  if (_runState == RUN_TREE_READY) {
    // Only redraw if needed (simple check for now, can optimize)
    if (_lastTreeCount != -2) { // Use -2 as magical 'Ready' state marker
      _lastTreeCount = -2;
      tft->fillRoundRect(boxX, boxY, boxW, boxH, 8, 0x18E3); // Blue
      tft->drawRoundRect(boxX, boxY, boxW, boxH, 8, 0xFFFF);

      tft->setTextColor(0xFFFF, 0x18E3);
      tft->setTextDatum(MC_DATUM);
      tft->setTextFont(4); // Large Font
      tft->setTextSize(1);
      tft->drawString("READY", boxX + boxW / 2, boxY + boxH / 2);

      tft->setTextFont(2);
      tft->drawString("PRESS TO START", boxX + boxW / 2, boxY + boxH / 2 + 30);
    }
    return;
  }

  bool isGo = false;
  int currentCount = 0;

  if (_runState == RUN_RUNNING) {
    isGo = true; // Overlay only active during first 1000ms of RUNNING
  } else if (_runState == RUN_COUNTDOWN) {
    unsigned long elapsed = millis() - _startTime;
    long remaining = (long)_treeInterval - (long)elapsed;
    if (remaining < 0)
      remaining = 0;

    currentCount = (remaining + 999) / 1000;
    if (currentCount <= 0 && remaining > 0)
      currentCount = 1;
  }

  // Background color: Green for GO, Blue for countdown
  uint16_t bgColor = isGo ? 0x07E0 : 0x18E3; // Blue

  if (_lastTreeIsGo != isGo || _lastTreeCount == -1 ||
      currentCount != _lastTreeCount) {
    _lastTreeIsGo = isGo;
    // Force full redraw of box background
    tft->fillRoundRect(boxX, boxY, boxW, boxH, 8, bgColor);
    tft->drawRoundRect(boxX, boxY, boxW, boxH, 8, 0xFFFF);

    // LED & Audio FEEDBACK: One-shot Go (Green)
    if (isGo) {
      FeedbackManager::getInstance().setLed(0, 255, 0); // GREEN
      FeedbackManager::getInstance().beep(300);         // Long Beep for GO
    } else if (!isGo && _runState == RUN_COUNTDOWN &&
               _lastTreeCount != currentCount) {
      // Beep on each countdown second
      FeedbackManager::getInstance().beep(100);
    }
  }

  // CONTINUOUS MONITORING FOR BLINK (Outside change detection)
  if (!isGo && _runState == RUN_COUNTDOWN) {
    // Synchronized Blink Amber with the Countdown (ON for first 500ms of each
    // second)
    unsigned long elapsed = millis() - _startTime;
    long remaining = (long)_treeInterval - (long)elapsed;
    if (remaining > 0 && (remaining % 1000) > 500) {
      FeedbackManager::getInstance().setLed(255, 165, 0); // Amber ON
    } else {
      FeedbackManager::getInstance().setLed(0, 0, 0); // OFF
    }
  } else if (isGo) {
    // Ensure Green is set (redundant but safe)
    FeedbackManager::getInstance().setLed(0, 255, 0);
  }

  if (isGo) {
    tft->setTextDatum(4); // 4
    tft->setTextColor(0x0000, bgColor);
    tft->setTextFont(4);
    tft->setTextSize(3); // Large GO
    tft->setTextPadding(0);

    // If we have a result for the main discipline (index 0), show it!
    if (!_disciplines.empty() && _disciplines[0].completed) {
      String ftStr = String(_disciplines[0].resultTime / 1000.0, 2) + "s";
      tft->drawString(ftStr, boxX + boxW / 2, boxY + boxH / 2);
    } else {
      tft->drawString("GO!", boxX + boxW / 2, boxY + boxH / 2);
    }
  }

  if (_lastTreeCount != currentCount || _lastTreeIsGo != isGo) {
    _lastTreeCount = currentCount;

    if (!isGo) {
      tft->setTextDatum(4); // 4
      tft->setTextColor(0xFFFF, bgColor);
      tft->setTextFont(7);
      tft->setTextSize(1);
      tft->setTextPadding(boxW - 20); // Use padding to clear previous digit
      tft->drawString(String(currentCount), boxX + boxW / 2, boxY + boxH / 2);

      tft->setTextPadding(0);
    }
  }

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}

void DragMeterScreen::handleMenuTouch(int idx) {
  if (idx < 0 || idx >= _menuItems.size())
    return;

  String &item = _menuItems[idx];
  if (item == "DRAG SCREEN") {
    _state = STATE_RUNNING;
    _ui->setTitle(_displayMode == DISPLAY_PREDICTIVE ? "PREDICTIVE"
                                                     : "DRAG METER");
    _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                            SCREEN_HEIGHT - STATUS_BAR_HEIGHT, COLOR_BG);
    drawDashboardStatic(true);
  } else if (item == "HISTORY") {
    // Filter to show only DRAG sessions
    _ui->getHistoryScreen()->setFilterType("DRAG", SCREEN_DRAG_METER);
    _ui->switchScreen(SCREEN_HISTORY);
  } else if (item == "SETTING") {
    _state = STATE_SETTING_MENU;
    _ui->setTitle("SETTINGS");
    _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                            SCREEN_HEIGHT - STATUS_BAR_HEIGHT, COLOR_BG);
    drawDashboardStatic(true);
  }
}

void DragMeterScreen::loadCustomDisciplines() {
  _disciplines.clear();

  // 1. Custom Speed Run
  Discipline speedRun;
  speedRun.name = "SPEED";
  speedRun.isDistance = false;
  speedRun.startSpeed = (float)_customStartKph;
  speedRun.target = (float)_customEndKph;
  speedRun.resultTime = 0;
  speedRun.completed = false;
  speedRun.endSpeed = 0;
  speedRun.slope = 0;
  speedRun.peakSpeed = 0;
  speedRun.brakingDistance = -1.0f; // Use as start time marker
  speedRun.valid = true;
  _disciplines.push_back(speedRun);

  // 2. Custom Distance Runs
  auto addDist = [&](String name, float target) {
    Discipline d;
    d.name = name;
    d.isDistance = true;
    d.startSpeed = 0;
    d.target = target;
    d.resultTime = 0;
    d.completed = false;
    d.endSpeed = 0;
    d.slope = 0;
    d.peakSpeed = 0;
    d.brakingDistance = 0;
    d.valid = true;
    _disciplines.push_back(d);
  };

  addDist(String(_customDist20m) + "m", (float)_customDist20m);
  addDist(String(_customDist30m) + "m", (float)_customDist30m);
  addDist(String(_customDist35m) + "m", (float)_customDist35m);

  // Set the primary highlight to the largest distance
  _highlightTitle = String(_customDist35m) + "m";
  _highlightValue = "0.0s";

  _sessionBest.clear();
}

void DragMeterScreen::drawDashboardStatic(bool forceStatusBar) {
  TFT_eSPI *tft = _ui->getTft();

  // Header
  tft->setTextDatum(TC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(2);
  tft->setTextColor(COLOR_TEXT, COLOR_BG);
  tft->drawString("DRAG METER", SCREEN_WIDTH / 2, STATUS_BAR_HEIGHT + 5);

  // 1. Solid Background (Removed fillScreen to match LapTimer - handled by
  // switch)

  if (_state == STATE_MENU) {
    _ui->setTitle("DRAG METER");
    drawMenu();
  } else if (_state == STATE_SETTING_MENU) {
    _ui->setTitle("SETTINGS");
    drawSettingMenu();
  } else if (_state == STATE_VALUE_EDITOR) {
    _ui->setTitle("ADJUST VALUE");
    drawValueEditor();
  } else if (_state == STATE_SUMMARY_VIEW) {
  } else {
    bool inGoPhase =
        (_runState == RUN_RUNNING && (millis() - _runStartTime < 1000));
    _ui->setTitle(inGoPhase
                      ? "GO!"
                      : (_displayMode == DISPLAY_PREDICTIVE ? "PREDICTIVE"
                                                            : "DRAG METER"));

    if (_displayMode == DISPLAY_PREDICTIVE) {
      // --- PREDICTIVE LAYOUT ---
      drawSpeedArea(false);

      // 2. Large Central Box Area (Structure only)
      int boxY = 125;
      int boxH = SCREEN_HEIGHT - boxY - 45;
      // Background will be handled by dynamic draw (Green/Red)

      // 3. Footer Area
      tft->setFreeFont(&Org_01);
      tft->setTextSize(1);
      tft->setTextColor(0xFFFF, _ui->getBackgroundColor());
      tft->setTextDatum(6);
      tft->drawString("Target: --.--", 10, SCREEN_HEIGHT - 5);
      tft->setTextDatum(8);
      tft->drawString("-- m: --.--", SCREEN_WIDTH - 10, SCREEN_HEIGHT - 5);

      _lastHighlightBgColor = 0xFFFF; // Force predictive box redraw
    } else {
      // --- NORMAL LAYOUT ---
      drawSpeedArea(false);

      // --- BOTTOM AREA (Two Cards) ---
      int bottomY = 130;
      int bottomH =
          SCREEN_HEIGHT - bottomY - 50; // Increased margin to 50 (Ends at 270)
      int splitX = SCREEN_WIDTH / 2;

      _lastHighlightBgColor = (_runState == RUN_RUNNING) ? 0x07E0 : 0x18E3;

      // List Card (Left) - Always Blue
      tft->fillRoundRect(10, bottomY, splitX - 15, bottomH, 8, 0x18E3);
      tft->drawRoundRect(10, bottomY, splitX - 15, bottomH, 8, 0xFFFF);

      // Highlight/Predictive Card (Right) - Dynamic
      tft->fillRoundRect(splitX + 5, bottomY, splitX - 15, bottomH, 8,
                         _lastHighlightBgColor);
      tft->drawRoundRect(splitX + 5, bottomY, splitX - 15, bottomH, 8, 0xFFFF);

      // List Headers (Left Side) - Always Silver/White
      tft->setTextDatum(0);
      tft->setTextSize(1);
      tft->setTextColor(0xC618, 0x18E3);
      tft->setFreeFont(&Org_01);
      int listStartY = bottomY + 10;
      int gap = 30;
      for (int i = 0; i < _disciplines.size(); i++) {
        tft->drawString(_disciplines[i].name, 25, listStartY + (i * gap));
      }

      // Footer Area (Slope)
      tft->setTextColor(0xFFFF, _ui->getBackgroundColor());
      tft->setTextDatum(0);
      tft->setFreeFont(&Org_01);
      tft->setTextSize(1);
      tft->drawString("SL:", 40,
                      SCREEN_HEIGHT - 25); // Shifted for back button clearance

      // HDOP Label moved to dynamic update or single static label?
      // Actually we update the whole string "HDOP: X.X" dynamically.
      // So no static label needed here, or maybe just placeholder.
    }
    // Force Christmas Tree overlay to redraw because background was
    // overwritten
    _lastTreeCount = -1;
    _lastTreeIsGo = false;

    // --- CHRISTMAS TREE TOGGLE (Bottom Right) ---
    // Only in Waiting or Running states (but mostly waiting)
  }

  // Back Button (Blue Triangle) - Draw LAST
  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, 0x001F);

  // Christmas Tree Toggle (Blue Triangle Right) - ONLY ON MAIN RUNNING SCREEN
  if (_state == STATE_RUNNING) {
    int arrowR_X = SCREEN_WIDTH - 30;   // Base X
    int arrowR_Tip = SCREEN_WIDTH - 15; // Tip X
    tft->fillTriangle(arrowR_Tip, SCREEN_HEIGHT - 30, arrowR_X,
                      SCREEN_HEIGHT - 40, arrowR_X, SCREEN_HEIGHT - 20, 0x001F);
  }

  _ui->drawStatusBar(forceStatusBar);
}

void DragMeterScreen::drawDashboardDynamic() {
  if (_state != STATE_RUNNING)
    return;

  TFT_eSPI *tft = _ui->getTft();

  if (_displayMode == DISPLAY_PREDICTIVE) {
    drawPredictiveMode();
    return;
  }

  // NORMAL MODE DRAWING
  // 1. Update Speed (Top Card)
  drawSpeedArea(true);

  // 2. Update List Values (Bottom Left Card) - Always White on Blue
  tft->setTextColor(0xFFFF, 0x18E3);
  tft->setTextDatum(2);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  int bottomY = 130;
  int listStartY = bottomY + 10;
  int gap = 30;
  int splitX = SCREEN_WIDTH / 2;
  for (int i = 0; i < _disciplines.size(); i++) {
    String valText = "--.--";
    if (_disciplines[i].completed) {
      valText = String(_disciplines[i].resultTime / 1000.0, 2);
    }
    // Right-align values at the split line
    tft->setTextPadding(80);
    tft->drawString(valText, splitX - 20, listStartY + (i * gap));
    tft->setTextPadding(0);
  }

  // 3. Highlight Card (Bottom Right Card)
  uint16_t bgColor = (_runState == RUN_RUNNING) ? 0x07E0 : 0x18E3;
  if (bgColor != _lastHighlightBgColor) {
    drawDashboardStatic(false);
  }

  bool inGoPhase =
      (_runState == RUN_RUNNING && (millis() - _runStartTime < 1000));
  if (_runState == RUN_COUNTDOWN || inGoPhase || _runState == RUN_TREE_READY) {
    // Skip details to keep Christmas Tree / GO! visible
  } else {
    uint16_t txtColor = (_runState == RUN_RUNNING) ? 0x0000 : 0x07FF;
    tft->setTextColor(txtColor, bgColor);

    // Title: Show live distance when running, otherwise show target
    String displayTitle = _highlightTitle;
    if (_runState == RUN_RUNNING) {
      displayTitle = String((int)_totalRunDistance) + "m";
    }
    tft->setFreeFont(NULL);
    tft->setTextSize(2);
    tft->setTextPadding(tft->textWidth("9999m"));
    tft->setTextDatum(1); // TC_DATUM
    tft->drawString(displayTitle, (SCREEN_WIDTH * 3) / 4 - 5, bottomY + 15);
    tft->setTextPadding(0);

    // Value (e.g., "11.37")
    tft->setTextFont(6);
    tft->setTextSize(1);
    tft->setTextPadding(140);
    tft->drawString(_highlightValue, (SCREEN_WIDTH * 3) / 4 - 5, bottomY + 50);
    tft->setTextPadding(0);
  }

  // 4. Slope (Footer)
  tft->setTextColor(0xFFFF, _ui->getBackgroundColor());
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextDatum(0);
  tft->setTextPadding(80);
  tft->drawString(String(_slope, 1) + "%", 70, SCREEN_HEIGHT - 25);
  tft->setTextPadding(0);

  // 5. HDOP (Bottom Center) - Unified Draw
  tft->setTextColor(0xFFFF, _ui->getBackgroundColor());
  tft->setTextDatum(TC_DATUM);
  tft->setTextPadding(100);
  tft->drawString("HDOP: " + String(gpsManager.getHDOP(), 1), SCREEN_WIDTH / 2,
                  SCREEN_HEIGHT - 25);
  tft->setTextPadding(0);

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}

int DragMeterScreen::getTouchedIndex(int startY, int btnH, int gap, int btnW,
                                     UIManager::TouchPoint p) {
  int x = (SCREEN_WIDTH - btnW) / 2;
  if (p.x < x || p.x > x + btnW)
    return -1;

  for (int i = 0; i < 5; i++) { // Support up to 5 buttons
    int btnY = startY + (i * (btnH + gap));
    if (p.y > btnY && p.y < btnY + btnH) {
      return i;
    }
  }
  return -1;
}

void DragMeterScreen::drawGenericMenu(const std::vector<String> &items,
                                      int selectedIdx) {
  TFT_eSPI *tft = _ui->getTft();

  int startY = 60;
  int btnHeight = (items.size() > 3) ? 45 : 50;
  int btnWidth = 360;
  int gap = (items.size() > 3) ? 8 : 12;
  int x = (SCREEN_WIDTH - btnWidth) / 2;

  for (int i = 0; i < (int)items.size(); i++) {
    int y = startY + (i * (btnHeight + gap));

    uint16_t btnColor = (i == selectedIdx) ? TFT_RED : TFT_DARKGREY;

    tft->fillRoundRect(x, y, btnWidth, btnHeight, 6, btnColor);
    // Remove border to match Lap Timer style
    // tft->drawRoundRect(x, y, btnWidth, btnHeight, 6, borderColor);

    tft->setTextColor(0xFFFF, btnColor);
    tft->setTextDatum(4); // 4
    tft->setFreeFont(&Org_01);
    tft->setTextSize(2);
    tft->drawString(items[i], SCREEN_WIDTH / 2, y + btnHeight / 2 + 2);
  }
  // _ui->drawStatusBar(true); // REDUNDANT: drawDashboardStatic calls this at
  // the end

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}

void DragMeterScreen::drawMenu() {
  drawGenericMenu(_menuItems, _selectedMenuIdx);
}

void DragMeterScreen::refreshSettingLabels() {
  _settingItems.clear();
  _settingItems.push_back("CUSTOM KPH");
  _settingItems.push_back("DIST: " + String(_customDist20m) + "m");
  _settingItems.push_back("DIST: " + String(_customDist30m) + "m");
  _settingItems.push_back("DIST: " + String(_customDist35m) + "m");
  _settingItems.push_back(_rolloutEnabled ? "ROLLOUT: ON" : "ROLLOUT: OFF");
}

void DragMeterScreen::drawSettingMenu() {
  drawGenericMenu(_settingItems, _selectedSettingIdx);
}

void DragMeterScreen::drawValueEditor() {
  TFT_eSPI *tft = _ui->getTft();
  int midX = SCREEN_WIDTH / 2;
  int midY = SCREEN_HEIGHT / 2;

  tft->setTextColor(0xFFFF, COLOR_BG);
  tft->setTextDatum(4);

  // --- UNIFIED PRO UI: 3-Row Layout ---
  tft->setFreeFont(NULL);
  tft->setTextFont(2);
  tft->setTextSize(1);
  tft->setTextColor(0xFFFF, COLOR_BG);
  tft->setTextPadding(150);
  tft->drawString(_rolloutEnabled ? "ROLLOUT: ON" : "ROLLOUT: OFF", 10,
                  SCREEN_HEIGHT - 20, 2);

  // GPS Accuracy Info (RaceBox style)
  char satBuf[16];
  sprintf(satBuf, "GPS: %d SAT", gpsManager.getSatellites());
  tft->setTextDatum(TR_DATUM);
  tft->drawString(satBuf, SCREEN_WIDTH - 10, SCREEN_HEIGHT - 20, 2);
  tft->setTextPadding(0);

  // Layout constants
  int startY = STATUS_BAR_HEIGHT + 40;
  if (_editingTarget != "KPH_SETTING") {
    startY = STATUS_BAR_HEIGHT + 90; // Better centering for 2 rows
  }
  int gapY = 70;
  int labelX = 20;
  int boxX = SCREEN_WIDTH - 140;
  int boxW = 120;
  int boxH = 50;
  int arrowX = boxX - 30;

  auto drawRow = [&](int idx, const char *label, String value, bool editable) {
    int rowY = startY + (idx * gapY);
    bool isFocused = (_editingFocus == idx);

    // Label
    tft->setTextDatum(3);
    tft->setTextFont(4);
    tft->setTextSize(1);
    tft->setTextColor(0xFFFF, COLOR_BG);
    tft->drawString(label, labelX, rowY);

    // Value Box
    if (isFocused) {
      tft->fillRoundRect(boxX, rowY - boxH / 2, boxW, boxH, 4, 0xFFFF);
      tft->setTextColor(0x0000, 0xFFFF);
    } else {
      tft->fillRoundRect(boxX, rowY - boxH / 2, boxW, boxH, 4, COLOR_BG);
      if (editable) {
        tft->drawRoundRect(boxX, rowY - boxH / 2, boxW, boxH, 4,
                           0xFFFF); // Add visual outline only if editable
      }
      tft->setTextColor(0xFFFF, COLOR_BG);
    }

    tft->setTextDatum(4); // 4
    tft->setTextFont(4);
    tft->drawString(value, boxX + boxW / 2, rowY);

    // Arrows (only for focused + editable)
    if (isFocused && editable) {
      int triW = 16;
      tft->fillTriangle(arrowX, rowY - 15, arrowX - triW / 2, rowY - 2,
                        arrowX + triW / 2, rowY - 2, 0xFFFF);
      tft->fillTriangle(arrowX, rowY + 15, arrowX - triW / 2, rowY + 2,
                        arrowX + triW / 2, rowY + 2, 0xFFFF);
    } else if (!isFocused) {
      tft->fillRect(arrowX - 10, rowY - 20, 20, 40, COLOR_BG);
    }
  };

  if (_editingTarget == "KPH_SETTING") {
    drawRow(0, "Units", "Kph", false);
    drawRow(1, "Start", String(_customStartKph), true);
    drawRow(2, "End", String(_customEndKph), true);
  } else {
    String valText = "";
    if (_editingTarget == "DIST_20")
      valText = String(_customDist20m) + "m";
    else if (_editingTarget == "DIST_30")
      valText = String(_customDist30m) + "m";
    else if (_editingTarget == "DIST_35")
      valText = String(_customDist35m) + "m";

    drawRow(0, "Type", "Distance", false);
    drawRow(1, "Value", valText, true); // Row 1 is editable for distance
  }

  // OK Button (Bottom Right)
  int okW = 130;
  int okH = 40;
  int okX = SCREEN_WIDTH - okW - 10;
  int okY = SCREEN_HEIGHT - okH - 10;
  tft->fillRoundRect(okX, okY, okW, okH, 6, 0x07E0);
  tft->setTextColor(0x0000);
  tft->setTextDatum(4);
  tft->setFreeFont(NULL);
  tft->setTextFont(2);
  tft->setTextSize(1);
  tft->drawString("OK / SAVE", okX + okW / 2, okY + okH / 2);
}

void DragMeterScreen::drawSummary() {
  TFT_eSPI *tft = _ui->getTft();

  // Removed fillScreen - handled by transition

  // Header Title
  tft->setTextColor(0xFFFF, _ui->getBackgroundColor());
  tft->setTextDatum(TC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(2);
  String header = _summaryShowBest ? "SESSION BEST" : "LAST RUN";
  tft->drawString(header, SCREEN_WIDTH / 2, 28);

  // Toggle Arrows (Subtle)
  tft->setTextSize(1);
  tft->setTextColor(0xC618, _ui->getBackgroundColor());
  if (_summaryShowBest) {
    tft->drawString("v", SCREEN_WIDTH / 2 + 85, 28);
  } else {
    tft->drawString("^", SCREEN_WIDTH / 2 + 85, 28);
  }

  // --- TOP STAT CARD ---
  int topStatY = 55;
  int topStatH = 40;
  tft->fillRoundRect(10, topStatY, SCREEN_WIDTH - 20, topStatH, 6, 0x18E3);
  tft->drawRoundRect(10, topStatY, SCREEN_WIDTH - 20, topStatH, 6, 0x7BEF);

  float peak = 0;
  const std::vector<Discipline> *data =
      _summaryShowBest ? &_sessionBest : &_disciplines;
  if (!data->empty()) {
    peak = data->back().peakSpeed;
  }

  tft->setTextColor(0x07FF, 0x18E3);
  tft->setTextDatum(3);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->drawString("PEAK SPEED:", 20, topStatY + topStatH / 2);

  tft->setTextDatum(5);
  tft->setTextFont(4);
  tft->drawString(String(peak, 1) + " KPH", SCREEN_WIDTH - 20,
                  topStatY + topStatH / 2);

  // --- TABLE AREA ---
  int tableY = 105;
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextColor(0xC618, _ui->getBackgroundColor());
  tft->setTextDatum(0);
  tft->drawString("DISC.", 20, tableY);
  tft->setTextDatum(TC_DATUM);
  tft->drawString("TIME", 210, tableY); // Adjusted column
  tft->setTextDatum(2);
  tft->drawString("@KPH", 350, tableY); // Adjusted column
  tft->drawString("SL%", 460, tableY);  // Adjusted column

  tft->drawFastHLine(0, tableY + 15, SCREEN_WIDTH, 0x18E3);

  int startY = tableY + 22;
  int gap = 30;

  if (data->empty()) {
    tft->setTextColor(0x7BEF, _ui->getBackgroundColor());
    tft->setTextDatum(1); // TC_DATUM
    tft->drawString("NO DATA AVAILABLE", SCREEN_WIDTH / 2, 180);
  } else {
    for (int i = 0; i < data->size(); i++) {
      const Discipline &d = (*data)[i];
      int rowY = startY + (i * gap);
      uint16_t color = d.completed ? (d.valid ? 0x07E0 : 0xF800) : 0x7BEF;

      tft->setTextColor(color, _ui->getBackgroundColor());
      tft->setTextSize(1);
      tft->setTextFont(2);

      tft->setTextDatum(0);
      tft->drawString(d.name, 20, rowY);

      tft->setTextDatum(1); // TC_DATUM
      tft->drawString(d.completed ? String(d.resultTime / 1000.0, 2) + "s"
                                  : "-",
                      210, rowY);

      tft->setTextDatum(2);
      tft->drawString(d.completed ? String(d.endSpeed, 1) : "-", 350, rowY);

      // Slope with Valid/Invalid Badge
      String slopeStr = String(d.slope, 1) + "%";
      if (d.completed) {
        slopeStr += d.valid ? " (V)" : " (*)";
      }
      tft->drawString(slopeStr, 460, rowY);
    }
  }

  // Footer: Braking
  float brakeDist = data->empty() ? 0 : data->back().brakingDistance;
  tft->setTextColor(0xC618, _ui->getBackgroundColor());
  tft->setTextSize(1);
  tft->setTextDatum(0);
  tft->drawString("BRAKING DISTANCE:", 40, SCREEN_HEIGHT - 25);
  tft->setTextDatum(2);
  tft->setTextColor(0xFFFF, _ui->getBackgroundColor());
  tft->drawString(String(brakeDist, 1) + " m", SCREEN_WIDTH - 10,
                  SCREEN_HEIGHT - 25);

  _ui->drawStatusBar(true);

  tft->setTextPadding(0);
}

void DragMeterScreen::handleSettingTouch(int idx) {
  if (idx < 0 || idx >= (int)_settingItems.size())
    return;

  _state = STATE_VALUE_EDITOR;

  // Index-based detection (more reliable than string matching)
  if (idx == 0) {
    // First item: CUSTOM KPH
    _editingTarget = "KPH_SETTING";
    _editingFocus = 1; // Focus on "Start" (Row 1) initially
  } else if (idx == 1) {
    // Second item: 20m (or custom value)
    _editingTarget = "DIST_20";
    _editingFocus = 1; // Focus on "Value" (Row 1) for 2-row layout
  } else if (idx == 2) {
    // Third item: 30m (or custom value)
    _editingTarget = "DIST_30";
    _editingFocus = 1;
  } else if (idx == 4) {
    // 1-Foot Rollout Toggle
    _rolloutEnabled = !_rolloutEnabled;
    Preferences p;
    p.begin("laptimer", false);
    p.putBool("rollout", _rolloutEnabled);
    p.end();
    refreshSettingLabels();    // Update the label
    drawDashboardStatic(true); // Redraw menu
    FeedbackManager::getInstance().beep(50);
  }

  _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                          SCREEN_HEIGHT - STATUS_BAR_HEIGHT, COLOR_BG);
  drawDashboardStatic(true);
}

void DragMeterScreen::handleValueTouch(UIManager::TouchPoint p) {
  int midX = SCREEN_WIDTH / 2;
  int midY = SCREEN_HEIGHT / 2;
  int delta = 0;

  if (_editingTarget == "KPH_SETTING" || _editingTarget.startsWith("DIST_")) {
    int startY = STATUS_BAR_HEIGHT + 40;
    int numRows = 3;
    if (_editingTarget != "KPH_SETTING") {
      startY = STATUS_BAR_HEIGHT + 90;
      numRows = 2;
    }
    int gapY = 70;

    // Detect Row Selection (Left side) - skip row 0 for distance and KPH
    // (non-editable)
    if (p.x < 250) {
      int startRow = 1; // Always start from row 1 (Value/Start), skipping Row 0
                        // (Type/Units)
      for (int i = startRow; i < numRows; i++) {
        int rowY = startY + (i * gapY);
        if (p.y > rowY - 30 && p.y < rowY + 30) {
          if (_editingFocus != i) {
            _editingFocus = i;
            drawDashboardStatic(false);
          }
          return;
        }
      }
    }

    // Detect Adjustment (Right side)
    if (p.x >= 250 && p.y < SCREEN_HEIGHT - 65) {
      int rowY = startY + (_editingFocus * gapY);
      int tapBoundaryY = (p.y < rowY) ? -1 : 1;
      delta = (tapBoundaryY == -1) ? 1 : -1;
    }
  } else {
    // Single Value Logic
    int btnW = 100;
    int btnH = 50;
    int btnOffY = 80;

    // Detect UP Button (+)
    if (p.x > midX - 110 && p.x < midX - 10) {
      if (p.y > midY + btnOffY && p.y < midY + btnOffY + btnH)
        delta = 1;
    }

    // Detect DOWN Button (-)
    if (p.x > midX + 10 && p.x < midX + 110) {
      if (p.y > midY + btnOffY && p.y < midY + btnOffY + btnH)
        delta = -1;
    }
  }

  // Detect OK Button (Bottom Right)
  if (p.x > SCREEN_WIDTH - 150 && p.y > SCREEN_HEIGHT - 60) {
    if (p.x < SCREEN_WIDTH - 5) {
      // Save and back
      Preferences prefs;
      prefs.begin("laptimer", false);
      if (_editingTarget == "KPH_SETTING") {
        prefs.putInt("dr_start_kph", _customStartKph);
        prefs.putInt("dr_end_kph", _customEndKph);
      } else if (_editingTarget == "DIST_20")
        prefs.putInt("dr_dist_20", _customDist20m);
      else if (_editingTarget == "DIST_30")
        prefs.putInt("dr_dist_30", _customDist30m);
      else if (_editingTarget == "DIST_35")
        prefs.putInt("dr_dist_35", _customDist35m);
      prefs.end();

      // Refresh disciplines with new values
      loadCustomDisciplines();
      refreshSettingLabels(); // Ensure menu buttons update immediately

      _state = STATE_SETTING_MENU;
      _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                              SCREEN_HEIGHT - STATUS_BAR_HEIGHT, COLOR_BG);
      drawDashboardStatic(true);
      return;
    }
  }

  if (delta != 0) {
    if (_editingTarget == "KPH_SETTING") {
      // Only edit rows 1 (Start) and 2 (End)
      if (_editingFocus == 1)
        _customStartKph += delta;
      else if (_editingFocus == 2)
        _customEndKph += delta;

      if (_customStartKph < 0)
        _customStartKph = 0;
      if (_customEndKph < 0)
        _customEndKph = 0;
    } else if (_editingTarget == "DIST_20")
      _customDist20m += delta;
    else if (_editingTarget == "DIST_30")
      _customDist30m += delta;
    else if (_editingTarget == "DIST_35")
      _customDist35m += delta;

    // Redraw Editor
    drawDashboardStatic(false);
  }
}

void DragMeterScreen::onHide() {
  imuManager.requestActivity(false);
  // If leaving while running, finalize session
  if (_runState == RUN_RUNNING) {
    saveReferenceRun();

    unsigned long resultTime = 0;
    String runName = "Drag Run";

    for (const auto &d : _disciplines) {
      if (d.completed) {
        runName = d.name;
        resultTime = d.resultTime;
        break;
      }
    }

    sessionManager.stopSession();
    String actualFilename = sessionManager.getCurrentFilename();
    if (actualFilename.length() == 0)
      actualFilename = "DragRun";

    String dateStr =
        gpsManager.getDateString() + " " + gpsManager.getTimeString();
    sessionManager.appendToHistoryIndex(actualFilename, dateStr, 1, resultTime,
                                        "DRAG");

    _runState = RUN_FINISHED;
  }

  // Ensure LEDs are off when leaving this screen
  FeedbackManager::getInstance().setLed(0, 0, 0);
}
