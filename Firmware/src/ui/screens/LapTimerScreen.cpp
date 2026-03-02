#include "LapTimerScreen.h"
#include "../../core/FeedbackManager.h"
#include "../../core/GPSManager.h"
#include "../../core/IMUManager.h"
#include "../../core/SessionManager.h"
#include "../fonts/Org_01.h"
#include "HistoryScreen.h"
#include "RaceScreen.h"
#include <Preferences.h>
#include <algorithm> // Untuk min_element

extern GPSManager gpsManager;
extern SessionManager sessionManager;
extern IMUManager imuManager;

// Konstanta untuk Tata Letak UI
// Konstanta untuk Tata Letak UI
// #define STATUS_BAR_HEIGHT 20 // Using global constant from config.h
#define LIST_ITEM_HEIGHT 30
#define LIST_ITEM_HEIGHT 30

// Tentukan Area Tombol
#define STOP_BTN_Y 255 // Dipindahkan KE BAWAH untuk jarak yang lebih baik
#define STOP_BTN_H 55

void LapTimerScreen::onShow() {
  _lastUpdate = 0;
  _lastTouchTime = millis(); // Prevent ghost touch on entry
  _lastBackTapTime = 0;
  _finishSet = false;
  _listScroll = 0;
  _menuSelectionIdx = -1;

  // Reset Flicker Tracking (Relevant for Track Selection UI)
  _lastSpeed = -999.0;
  _lastSats = -1;
  _maxSpeedSession = 0.0;
  _maxSpeedSessionRender = -1.0;
  _lastAccYRender = -999.0;

  // Initialize GPS recording state
  _recordingState = RECORD_IDLE;
  _recordedPoints.clear();
  _recordStartLat = 0;
  _recordStartLon = 0;
  _recordingStartTime = 0;
  _lastPointTime = 0;
  _totalDistance = 0;

  // Start in Sub-Menu (Draw immediately before loading to avoid blank screen)
  _state = STATE_MENU;
  TFT_eSPI *tft = _ui->getTft();
  gpsManager.setRawDataCallback(nullptr);
  _needsStaticRedraw = true;
  drawMenu();

  loadTracks();
  imuManager.calibrateLevel();
}

void LapTimerScreen::onHide() {
  if (_recordingState == RECORD_ACTIVE) {
    _recordingState = RECORD_COMPLETE;
    String fn = "Auto_" + String(millis());
    saveTrackToGPX("/tracks/" + fn + ".gpx");
  }
}

#include <ArduinoJson.h>

void LapTimerScreen::loadTracks() {
  _tracks.clear();

  double curLat = gpsManager.getLatitude();
  double curLon = gpsManager.getLongitude();

  // Load from SD Card if available
  if (SD.exists("/tracks.json")) {
    File file = SD.open("/tracks.json", FILE_READ);
    if (file) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, file);
      file.close();

      if (!error && doc["tracks"].is<JsonArray>()) {
        JsonArray trackArray = doc["tracks"];
        for (JsonVariant t : trackArray) {
          double tLat = t["lat"].as<double>();
          double tLon = t["lon"].as<double>();

          // Filter 50km Radius
          double dist = gpsManager.distanceBetween(curLat, curLon, tLat, tLon);
          if (dist > 50000)
            continue;

          Track newTrack;
          newTrack.name = t["name"].as<String>();
          newTrack.lat = tLat;
          newTrack.lon = tLon;
          newTrack.isCustom = true; // Loaded from SD

          if (t.containsKey("date")) {
            newTrack.createdDate = t["date"].as<String>();
          }
          if (t.containsKey("time")) {
            newTrack.createdTime = t["time"].as<String>();
          }

          // Configs
          JsonArray configs = t["configs"];
          if (configs.size() > 0) {
            for (JsonVariant c : configs) {
              newTrack.configs.push_back({c.as<String>()});
            }
          } else {
            newTrack.configs.push_back({"Default"});
          }

          if (t.containsKey("path")) {
            newTrack.pathFile = t["path"].as<String>();
          }
          if (t.containsKey("best_lap")) {
            newTrack.bestLap = t["best_lap"].as<unsigned long>();
          }

          _tracks.push_back(newTrack);
        }
        Serial.println("Tracks loaded from SD");
      }
    }
  }

  // Factory Tracks (Hardcoded)
  // Check dist for them too
  Track sonoma;
  sonoma.name = "Test Track (Bordeaux)"; // Renamed to match img
  sonoma.lat = 44.8378;                  // Bordeaux approx
  sonoma.lon = -0.5792;
  sonoma.isCustom = false; // Factory
  sonoma.configs.push_back({"Default"});
  sonoma.createdDate = "Factory";
  sonoma.pathFile = ""; // No file for factory (or hardcode points later)

  // Always add test track for DEBUG/UI TESTING
  _tracks.push_back(sonoma);

  // --- AUTO-TRACK DETECTION ---
  if (!_tracks.empty() && gpsManager.isFixed()) {
    double minTrackDist = 999999.0;
    int closestIdx = -1;

    for (int i = 0; i < (int)_tracks.size(); i++) {
      double d = gpsManager.distanceBetween(curLat, curLon, _tracks[i].lat,
                                            _tracks[i].lon);
      if (d < minTrackDist) {
        minTrackDist = d;
        closestIdx = i;
      }
    }

    // If a track is found within 5km, auto-select it
    if (closestIdx != -1 && minTrackDist < 5000.0) {
      _selectedTrackIdx = closestIdx;
      _currentTrackName = _tracks[closestIdx].name;
      Serial.println("GPS: Auto-selected track: " + _currentTrackName + " (" +
                     String((int)minTrackDist) + "m away)");

      // Load reference lap for the auto-selected track if it has a best lap
      if (_tracks[closestIdx].bestLap > 0 &&
          _tracks[closestIdx].pathFile.length() > 0) {
        sessionManager.loadBestLapAsReference(_tracks[closestIdx].pathFile);
      }
    }
  }
}

void LapTimerScreen::loadTrackPath(String filename) {
  _recordedPoints.clear();

  if (!SD.exists(filename)) {
    Serial.println("Track path file not found: " + filename);
    return;
  }

  File file = SD.open(filename, FILE_READ);
  if (!file)
    return;

  // Read CSV: lat,lon (one per line)
  // Limit points to save RAM (e.g. max 1000)
  int count = 0;
  while (file.available() && count < 1000) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      int commaIndex = line.indexOf(',');
      if (commaIndex > 0) {
        String latStr = line.substring(0, commaIndex);
        String lonStr = line.substring(commaIndex + 1);

        GPSPoint p;
        p.lat = latStr.toDouble();
        p.lon = lonStr.toDouble();
        p.timestamp = 0; // Static path
        _recordedPoints.push_back(p);
        count++;
      }
    }
  }
  file.close();
  Serial.println("Loaded " + String(count) + " points from " + filename);
}

void LapTimerScreen::saveTrackToGPX(String filename) {
  if (_recordedPoints.empty()) {
    Serial.println("No points to save!");
    return;
  }

  // Ensure directory exists
  if (!SD.exists("/tracks")) {
    SD.mkdir("/tracks");
  }

  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing: " + filename);
    return;
  }

  Serial.println("Saving GPX to: " + filename);

  // 1. Header
  file.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
  file.println("<gpx version=\"1.1\" creator=\"MuchRacing\" "
               "xmlns=\"http://www.topografix.com/GPX/1/1\">");

  // 2. Metadata / Track Info
  file.println("  <trk>");
  // Use current track name or generic
  String trackName =
      (_currentTrackName.length() > 0) ? _currentTrackName : " Recorded Track";
  file.println("    <name>" + trackName + "</name>");
  file.println("    <trkseg>");

  // 3. Points
  for (const auto &p : _recordedPoints) {
    file.printf("      <trkpt lat=\"%.7f\" lon=\"%.7f\">\n", p.lat, p.lon);

    // Optional: Add Elevation or Time if available in GPSPoint struct
    // Standard timestamp format: 2023-10-25T14:30:00Z
    // Currently we store raw millis() or similar in timestamp, need real time?
    // If GPSManager has real UTC time, ideally we'd use that.
    // For now, no time tag to avoid confusing parsers with bad data.

    file.println("      </trkpt>");
  }

  // 4. Footer
  file.println("    </trkseg>");
  file.println("  </trk>");
  file.println("</gpx>");

  file.close();
  Serial.println("GPX Saved Successfully.");
}

void LapTimerScreen::saveTrackToCSV(String filename) {
  if (_recordedPoints.empty()) {
    Serial.println("CSV: No points to save!");
    return;
  }

  // Ensure directory exists
  if (!SD.exists("/tracks")) {
    SD.mkdir("/tracks");
  }

  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open CSV for writing: " + filename);
    return;
  }

  Serial.println("Saving CSV to: " + filename);

  for (const auto &p : _recordedPoints) {
    file.printf("%.7f,%.7f\n", p.lat, p.lon);
  }

  file.close();
  Serial.println("CSV Saved Successfully.");
}

void LapTimerScreen::update() {
  UIManager::TouchPoint p = _ui->getTouchPoint();
  bool touched = (p.x != -1);

  if (_state == STATE_MENU) {
    if (touched) {
      // Global Debounce for Menu
      if (millis() - _lastTouchTime < TOUCH_DEBOUNCE_MS)
        return;
      _lastTouchTime = millis();

      // 1. Back/Home (Bottom Left) - Expanded hit area (100x80)
      // 1. Back/Home (Bottom Left) - Reduced hit area (100x45)
      if (p.x < 80 && p.y > 275) {
        _ui->switchScreen(SCREEN_MENU);
        return;
      }

      // 2. Button Logic
      // startY = 50 (Moved up), btnHeight = 50, gap = 8
      int startY = 50;
      int btnHeight = 50;
      int gap = 8;
      int x = (SCREEN_WIDTH - 360) / 2;
      int btnWidth = 360;

      // Check if X is within button width (centered)
      if (p.x > x && p.x < x + btnWidth) {
        int touchedIdx = -1;

        // Check Y coordinates for each button
        // Check Y coordinates for each button
        for (int i = 0; i < 4; i++) {
          int btnY = startY + (i * (btnHeight + gap));
          if (p.y > btnY && p.y < btnY + btnHeight) {
            touchedIdx = i;
            break;
          }
        }

        if (touchedIdx != -1) {
          // Debounce handled above

          // Double Tap Logic
          if (_menuSelectionIdx == touchedIdx) {
            // Second tap on SAME button -> Execute Action

            // Execute Action
            if (touchedIdx == 0) {         // Select Track
              if (!gpsManager.isFixed()) { // GPS CHECK ENABLED
                _state = STATE_NO_GPS;
                _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                        SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                        _ui->getBackgroundColor());
                drawNoGPS();
              } else {
                // Go to Searching Screen first
                _state = STATE_SEARCHING;
                _searchStartTime = millis();
                _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                        SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                        _ui->getBackgroundColor());
                drawSearching();
              }
            } else if (touchedIdx == 1) { // Race Screen
              if (_selectedTrackIdx >= 0 &&
                  _selectedTrackIdx < (int)_tracks.size()) {
                _ui->getRaceScreen()->setTrack(_tracks[_selectedTrackIdx]);
              }
              _ui->switchScreen(SCREEN_RACE);
            } else if (touchedIdx == 2) { // Record Track
              _recordingState = RECORD_IDLE;
              _recordedPoints.clear();
              _state = STATE_RECORD_TRACK;
              _needsStaticRedraw = true; // Atomically handle redraw in update()
            } else if (touchedIdx == 3) { // History
              // Filter to show only TRACK sessions
              _ui->getHistoryScreen()->setFilterType("TRACK", SCREEN_LAP_TIMER);
              _ui->switchScreen(SCREEN_HISTORY);
            }
          } else {
            // First tap (or different button) -> Highlight Only
            _menuSelectionIdx = touchedIdx;
            drawMenu();
          }

          return;
        }
      }
    }
  } else if (_state == STATE_CREATE_TRACK) {
    extern GPSManager gpsManager;
    if (touched) {
      if (millis() - _lastTouchTime < TOUCH_DEBOUNCE_MS)
        return;
      _lastTouchTime = millis();

      // Back Button (Bottom Left)
      // Back Button (Bottom Left) - Expanded hit area (100x80)
      if (p.x < 80 && p.y > 240) {
        _state = STATE_TRACK_LIST;
        _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                _ui->getBackgroundColor());
        drawTrackList();
        return;
      }

      if (_createStep == 0) {
        // SET START Button (Centered, y=235, w=220, h=50)
        int btnW = 220;
        int btnH = 50;
        int btnX = (SCREEN_WIDTH - btnW) / 2;
        int btnY = SCREEN_HEIGHT - 85; // 235

        if (p.x > btnX && p.x < btnX + btnW && p.y > btnY &&
            p.y < btnY + btnH) {
          // Capture GPS
          if (gpsManager.isFixed()) { // GPS CHECK RESTORED
            _createStartLat = gpsManager.getLatitude();
            _createStartLon = gpsManager.getLongitude();
            _createStep = 1;
            // Clear entire screen except status bar for redraw
            _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                      SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
            drawCreateTrack();
          } else {
            _ui->showToast("No GPS Fix!", 2000);
          }
        }
      } else if (_createStep == 1) {
        int btnH = 50;
        int btnY = SCREEN_HEIGHT - 85; // 235

        // Button 1: SAME AS START (Left: 70, W: 180)
        int btn1X = 70;
        int btn1W = 180;

        if (p.x > btn1X && p.x < btn1X + btn1W && p.y > btnY &&
            p.y < btnY + btnH) {
          _createFinishLat = _createStartLat;
          _createFinishLon = _createStartLon;
          // SAVE
          _createStep = 2; // Show "Saving"
          drawCreateTrack();

          // Generate Name (e.g. "Track [Time]")
          String name = "Track " + String(millis() / 1000);
          saveNewTrack(name, _createStartLat, _createStartLon, _createFinishLat,
                       _createFinishLon);

          _ui->showToast("Track Saved!", 2000);
          loadTracks(); // Reload to see it

          // Exit
          _state = STATE_TRACK_LIST;
          _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                    SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
          drawTrackList();
          return;
        }

        // Button 2: SET FINISH (Right: 270, W: 180)
        int btn2X = 270;
        int btn2W = 180;

        if (p.x > btn2X && p.x < btn2X + btn2W && p.y > btnY &&
            p.y < btnY + btnH) {
          if (gpsManager.isFixed()) {
            _createFinishLat = gpsManager.getLatitude();
            _createFinishLon = gpsManager.getLongitude();
            // SAVE
            _createStep = 2;
            drawCreateTrack();

            String name = "Track " + String(millis() / 1000);
            saveNewTrack(name, _createStartLat, _createStartLon,
                         _createFinishLat, _createFinishLon);

            _ui->showToast("Track Saved!", 2000);
            loadTracks();

            _state = STATE_TRACK_LIST;
            _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                      SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
            drawTrackList();
            return;
          } else {
            _ui->showToast("No GPS Fix!", 2000);
          }
        }
      }
    }
  } else if (_state == STATE_NO_GPS) {
    if (touched) {
      if (millis() - _lastTouchTime < TOUCH_DEBOUNCE_MS)
        return;
      _lastTouchTime = millis();

      int btnY = SCREEN_HEIGHT - 60;
      // Retry (Left) x=20, w=130
      if (p.y > btnY && p.y < btnY + 40) {
        if (p.x > 20 && p.x < 150) {
          // Retry Logic DISABLED
          /*
          if (gpsManager.isFixed()) {
            _state = STATE_SEARCHING;
            _searchStartTime = millis();
            _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                    SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                    COLOR_BG);
            drawSearching();
          } else {
            // Feedback (Redraw to blink)
            drawNoGPS();
          }
          */
        }
        // Continue (Right) x=170, w=130 -> Back to Menu
        else if (p.x > 170 && p.x < 300) {
          _state = STATE_MENU;
          _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                  SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                  _ui->getBackgroundColor());
          drawMenu();
          _ui->drawStatusBar();
        }
      }
    }
  } else if (_state == STATE_SEARCHING) {
    // Auto-transition after delay
    if (millis() - _searchStartTime > 2000) {
      loadTracks();
      // If no tracks loaded, maybe stay in searching or show "No Tracks"?
      // For now, go to List, list handles empty state.
      _selectedTrackIdx = -1;
      _state = STATE_TRACK_LIST;
      _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                              SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                              _ui->getBackgroundColor());
      drawTrackList();
    }
  } else if (_state == STATE_TRACK_LIST) {
    if (touched) {
      if (millis() - _lastTouchTime < TOUCH_DEBOUNCE_MS)
        return;
      _lastTouchTime = millis();

      // 1. Back Arrow (if clicked top left still works, though text changed)
      // Title is "Nearby Tracks" at x=10. Back behavior?
      // User didn't specify Back on List, but implied menu access.
      // Let's keep Back check on left just in case < 60x60
      // 1. Back Arrow (Bottom Left) - Expanded hit area (100x80)
      if (p.x < 80 && p.y > 240) {
        _state = STATE_MENU;
        _listScroll = 0; // Reset scroll
        _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT, COLOR_BG);
        drawMenu();
        _ui->drawStatusBar();
        return;
      }

      // 2. Scroll Buttons (Right Side)
      // Up area: x > SCREEN_WIDTH - 60, y < 150
      // Down area: x > SCREEN_WIDTH - 60, y > 150
      if (p.x > SCREEN_WIDTH - 60) {
        if (p.y > 60 && p.y < 150) { // Scroll Up
          if (_listScroll > 0) {
            _listScroll--;
            drawTrackList();
          }
          return;
        } else if (p.y > 150 && p.y < 300) { // Scroll Down
          if (_listScroll + 3 < _tracks.size()) {
            _listScroll++;
            drawTrackList();
          }
          return;
        }
      }

      // 2. New Track Button (Top Right)
      // btnX = SCREEN_WIDTH - 110, Y=22, W=100, H=20
      if (p.x > SCREEN_WIDTH - 110 && p.y < 50) {
        // Go to Create Track Wizard
        _state = STATE_CREATE_TRACK;
        _createStep = 0;
        _createStartLat = 0;
        _createStartLon = 0;
        _createFinishLat = 0;
        _createFinishLon = 0;

        _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                _ui->getBackgroundColor());
        drawCreateTrack();
        return;
      }

      // 3. Track Selection (Open Popup)
      int startY = 60;
      int itemH = 55;
      int gap = 8;
      if (p.y > startY && p.x < SCREEN_WIDTH - 60) {
        int relativeIdx = (p.y - startY) / (itemH + gap);
        int actualIdx = relativeIdx + _listScroll;
        // Limit to 3 items to avoid back button area
        if (actualIdx >= 0 && actualIdx < _tracks.size() && relativeIdx < 3) {
          _selectedTrackIdx = actualIdx;
          _state = STATE_TRACK_MENU;
          // Clear background slightly to dim or just draw popup over?
          // For now just draw popup
          drawTrackOptionsPopup();
        }
      }
    }
  } else if (_state == STATE_TRACK_MENU) {
    if (touched) {
      if (millis() - _lastTouchTime < TOUCH_DEBOUNCE_MS)
        return;
      _lastTouchTime = millis();

      // Popup Coords calculation again
      int w = 220;
      int h = 150;
      int x = (SCREEN_WIDTH - w) / 2;
      int y = (SCREEN_HEIGHT - h) / 2 + 10;

      // Check if touch inside popup
      if (p.x > x && p.x < x + w && p.y > y && p.y < y + h) {
        // Row Check
        int itemH = 25;
        int relY = p.y - (y + 10);
        int idx = relY / itemH;

        if (idx == 0) { // Select
          Track &t = _tracks[_selectedTrackIdx];
          _currentTrackName = t.name;
          _selectedConfigIdx = 0;

          // Load Track Path if available
          if (t.pathFile.length() > 0) {
            loadTrackPath(t.pathFile);
          } else {
            _recordedPoints.clear(); // Clear any previous points
          }

          // LOAD REFERENCE LAP FOR PREDICTIVE TIMING
          if (t.bestLap > 0) {
            sessionManager.loadBestLapAsReference(t.pathFile);
          }

          _ui->getRaceScreen()->setTrack(t);
          _ui->switchScreen(SCREEN_RACE);
        } else if (idx == 1) { // Select & Edit
          // Load Track Path for Preview
          Track &t = _tracks[_selectedTrackIdx];
          if (t.pathFile.length() > 0) {
            loadTrackPath(t.pathFile);
          } else {
            _recordedPoints.clear();
          }
          // Go to Details Screen
          _state = STATE_TRACK_DETAILS;
          _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                    SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
          drawTrackDetails();
        } else if (idx == 2) { // Invert
                               // TODO: Logic
        } else if (idx == 3) { // Reinit Best Lap
          if (_selectedTrackIdx >= 0 && _selectedTrackIdx < _tracks.size()) {
            _tracks[_selectedTrackIdx].bestLap = 0;
          }
          // Close Popup
          _state = STATE_TRACK_LIST;
          _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                    SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
          drawTrackList();
        } else if (idx == 4) { // Remove
          if (_selectedTrackIdx >= 0 && _selectedTrackIdx < _tracks.size()) {
            if (_tracks[_selectedTrackIdx].isCustom) {
              String nameToRemove = _tracks[_selectedTrackIdx].name;
              _tracks.erase(_tracks.begin() + _selectedTrackIdx);

              // Update JSON file persistently
              File file = SD.open("/tracks.json", FILE_READ);
              if (file) {
                JsonDocument doc;
                deserializeJson(doc, file);
                file.close();

                JsonArray tracks = doc["tracks"];
                for (size_t i = 0; i < tracks.size(); i++) {
                  if (tracks[i]["name"] == nameToRemove) {
                    tracks.remove(i);
                    break;
                  }
                }

                File wFile = SD.open("/tracks.json", FILE_WRITE);
                if (wFile) {
                  serializeJson(doc, wFile);
                  wFile.close();
                }
              }
            }
          }
          _state = STATE_TRACK_LIST;
          _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                    SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
          drawTrackList();
        }
      } else {
        // Outside Click -> Close Popup
        _state = STATE_TRACK_LIST;
        // Ideally redraw just the list area or remove popup rect?
        // Simplest is full redraw
        _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                  SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
        drawTrackList();
      }
    }
  } else if (_state == STATE_TRACK_DETAILS) {
    if (touched) {
      if (millis() - _lastTouchTime < TOUCH_DEBOUNCE_MS)
        return;
      _lastTouchTime = millis();

      // 1. Back Button (Bottom-Left)
      // 1. Back Button (Bottom-Left) - Expanded hit area (100x80)
      if (p.x < 80 && p.y > 240) {
        _state = STATE_TRACK_LIST;
        _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                _ui->getBackgroundColor());
        drawTrackList();
        return;
      }

      // 2. Select Button
      int btnW = 200;
      int btnX = (SCREEN_WIDTH - btnW) / 2;
      int btnY = 249; // Match drawTrackDetails cardY + cardH + 15
      int btnH = 46;

      if (p.x > btnX && p.x < btnX + btnW && p.y > btnY && p.y < btnY + btnH) {
        Track &t = _tracks[_selectedTrackIdx];
        _currentTrackName = t.name;
        if (t.pathFile.length() > 0)
          loadTrackPath(t.pathFile);
        if (t.bestLap > 0)
          sessionManager.loadBestLapAsReference(t.pathFile);
        _ui->getRaceScreen()->setTrack(t);
        _ui->switchScreen(SCREEN_RACE);
      }

      // 3. Edit Name Button
      int editX = 412; // infoX + infoW - editW - 8
      int editY = 97;  // cardY + 8
      if (p.x > editX - 5 && p.x < editX + 45 + 5 && p.y > editY - 10 &&
          p.y < editY + 18 + 10) {
        _state = STATE_RENAME_TRACK;
        _renamingName = _tracks[_selectedTrackIdx].name;
        drawRenameTrack(true);
      }

      // 4. Map Box (Tap to Map if empty)
      int margin = 15;
      int headH = 34;
      int headY = STATUS_BAR_HEIGHT + 15;
      int cardY = headY + headH + 20;
      int cardH = 145;
      int mapW = 210;
      int mapX = margin;
      if (_recordedPoints.empty() && p.x > mapX && p.x < mapX + mapW &&
          p.y > cardY && p.y < cardY + cardH) {
        // Go to Record Mode
        _recordingState = RECORD_IDLE;
        _recordedPoints.clear();
        _state = STATE_RECORD_TRACK;
        _needsStaticRedraw = true;
      }
    }
  } else if (_state == STATE_SAVE_TRACK) {
    if (touched) {
      if (millis() - _lastTouchTime < TOUCH_DEBOUNCE_MS)
        return;
      _lastTouchTime = millis();
      KeyboardComponent::KeyResult key =
          _keyboard.handleTouch(p.x, p.y, 100, _keyboardShift);
      if (key.type != KeyboardComponent::KEY_NONE) {
        if (key.type == KeyboardComponent::KEY_CHAR) {
          if (_renamingName.length() < 15) {
            _renamingName += key.value;
            drawSaveTrackName(false);
          }
        } else if (key.type == KeyboardComponent::KEY_DEL) {
          if (_renamingName.length() > 0) {
            _renamingName.remove(_renamingName.length() - 1);
            drawSaveTrackName(false);
          }
        } else if (key.type == KeyboardComponent::KEY_OK) {
          String fn = _renamingName;
          fn.trim();
          if (fn.length() == 0)
            fn = "Track_" + String(millis());

          String csvPath = "/tracks/" + fn + ".csv";
          String gpxPath = "/tracks/" + fn + ".gpx";

          saveTrackToCSV(csvPath);
          saveTrackToGPX(gpxPath);

          if (!_recordedPoints.empty()) {
            // Use CSV path for tracks.json so it can be loaded by Map Preview
            saveNewTrack(fn, _recordedPoints.front().lat,
                         _recordedPoints.front().lon,
                         _recordedPoints.back().lat, _recordedPoints.back().lon,
                         csvPath);
          }

          _ui->showToast("Saved!", 2000);
          _state = STATE_TRACK_LIST;
          loadTracks();
          drawTrackList();
        } else if (key.type == KeyboardComponent::KEY_SHIFT) {
          _keyboardShift = !_keyboardShift;
          // Redraw keyboard to show case change
          _keyboard.draw(_ui->getTft(), 100, _keyboardShift, 0, 0);
        }
      }
      // Cancel Button (Bottom Left)
      // Cancel Button (Bottom Left) - Expanded hit area (100x80)
      if (p.x < 80 && p.y > 240) {
        _state = STATE_RECORD_TRACK;
        _recordingState = RECORD_COMPLETE;
        _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                _ui->getBackgroundColor());
        drawRecordTrack();
      }
    }
  } else if (_state == STATE_RENAME_TRACK) {
    if (touched) {
      if (millis() - _lastTouchTime < TOUCH_DEBOUNCE_MS)
        return;
      _lastTouchTime = millis();
      KeyboardComponent::KeyResult res =
          _keyboard.handleTouch(p.x, p.y, 100, _keyboardShift);

      if (res.type == KeyboardComponent::KEY_CHAR) {
        if (_renamingName.length() < 15) {
          _renamingName += res.value;
          drawRenameTrack(false);
        }
      } else if (res.type == KeyboardComponent::KEY_DEL) {
        if (_renamingName.length() > 0) {
          _renamingName.remove(_renamingName.length() - 1);
          drawRenameTrack(false);
        }
      } else if (res.type == KeyboardComponent::KEY_SPACE) {
        if (_renamingName.length() < 15) {
          _renamingName += " ";
          drawRenameTrack(false);
        }
      } else if (res.type == KeyboardComponent::KEY_SHIFT) {
        _keyboardShift = !_keyboardShift;
        // Redraw keyboard to show case change
        _keyboard.draw(_ui->getTft(), 100, _keyboardShift, 0, 0);
      } else if (res.type == KeyboardComponent::KEY_OK) {
        renameTrack(_selectedTrackIdx, _renamingName);
        _state = STATE_TRACK_DETAILS;
        // Restore background
        _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                  SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
        drawTrackDetails();
      } else if (res.type != KeyboardComponent::KEY_NONE) {
        // Handle other keys...
        if (res.type == KeyboardComponent::KEY_CHAR) {
          _renamingName += res.value;
          drawRenameTrack();
        } else if (res.type == KeyboardComponent::KEY_DEL) {
          if (_renamingName.length() > 0)
            _renamingName.remove(_renamingName.length() - 1);
          drawRenameTrack();
        }
      }
    }
  } else if (_state == STATE_SUMMARY) {

    // --- LOGIKA STATUS RINGKASAN ---
    if (touched) {
      if (millis() - _lastTouchTime < TOUCH_DEBOUNCE_MS)
        return;
      _lastTouchTime = millis();

      // 1. Tombol Kembali (Bottom Left area)
      if (p.x < 100 && p.y > 240) {
        _state = STATE_MENU;
        _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT, COLOR_BG);
        drawMenu();
        _ui->drawStatusBar();
        return;
      }

      // 2. Restart / New Session (Tap anywhere else?)
      // For now, let's keep it simple: Tap bottom right to go to Track Select?
      if (p.x > 200 && p.y > 200) {
        _state = STATE_TRACK_LIST;
        _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                _ui->getBackgroundColor());
        drawTrackList();
        _ui->drawStatusBar();
        return;
      }
    }
  } else if (_state == STATE_RECORD_TRACK) {
    extern GPSManager gpsManager;

    if (touched) {
      // Back button (Standard Bottom Left area)
      if (p.x < 80 && p.y > SCREEN_HEIGHT - 60) {
        if (millis() - _lastBackTapTime < 500) {
          _state = STATE_MENU;
          _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                  SCREEN_HEIGHT - STATUS_BAR_HEIGHT, COLOR_BG);
          drawMenu();
          _ui->drawStatusBar();
          _lastBackTapTime = 0;
        } else {
          _lastBackTapTime = millis();
        }
        return;
      }

      // Constants for Touch Logic (Must match drawRecordTrack)
      int cardY = 55;
      int cardH = 40;
      int clearY = cardY + cardH + 5;
      int gridY = clearY + 10;
      int boxH = 70;
      int btnY = 240;
      int btnH = 55;

      if (_recordingState == RECORD_IDLE) {
        // START button (Centered)
        // Hit Area: X 150-330
        if (p.x > 150 && p.x < 330 && p.y > btnY - 10 &&
            p.y < btnY + btnH + 10) {

          if (true) {
            // BYPASS GPS CHECK for Recording
            // Logic... same as before
            if (_menuSelectionIdx == 10) {
              _menuSelectionIdx = -1;
              _recordingState = RECORD_ACTIVE;
              _recordStartLat = gpsManager.getLatitude();
              _recordStartLon = gpsManager.getLongitude();
              _recordingStartTime = millis();
              _lastPointTime = millis();
              _recordedPoints.clear();

              GPSPoint firstPoint;
              firstPoint.lat = _recordStartLat;
              firstPoint.lon = _recordStartLon;
              firstPoint.timestamp = millis();
              _recordedPoints.push_back(firstPoint);

              drawRecordTrack();
            } else {
              _menuSelectionIdx = 10;
              drawRecordTrack();
            }
          }
        }
      } else if (_recordingState == RECORD_ACTIVE) {
        // STOP button (Centered)
        if (p.x > 150 && p.x < 330 && p.y > btnY - 10 &&
            p.y < btnY + btnH + 10) {

          if (_menuSelectionIdx == 11) {
            _menuSelectionIdx = -1;
            _recordingState = RECORD_COMPLETE;
            drawRecordTrack();
          } else {
            _menuSelectionIdx = 11;
            drawRecordTrack();
          }
        }
      } else if (_recordingState == RECORD_COMPLETE) {
        // SAVE | DISCARD Buttons
        // btnW=100, gap=20.
        // StartX = (480 - 220)/2 = 130.
        // SaveX: 130 to 230
        // DelX: 250 to 350
        // Y position = gridY + boxH + 20 = 110 + 70 + 20 = 200.

        int saveY = 200;

        // SAVE (Left)
        // SAVE (Left)
        if (p.x > 130 && p.x < 230 && p.y > saveY && p.y < saveY + btnH) {
          if (_menuSelectionIdx == 12) {
            // ACTION: SAVE -> GO TO NAMING
            _state = STATE_SAVE_TRACK;
            _renamingName = "";
            _keyboardShift = true;
            drawSaveTrackName(true);
            _menuSelectionIdx = -1;
          } else {
            _menuSelectionIdx = 12;
            drawRecordTrack();
          }
        }
        // DISCARD (Right)
        else if (p.x > 250 && p.x < 350 && p.y > saveY && p.y < saveY + btnH) {
          if (_menuSelectionIdx == 13) {
            // ACTION: DISCARD
            _recordingState = RECORD_IDLE;
            _recordedPoints.clear();
            drawRecordTrack();
            _menuSelectionIdx = -1;
          } else {
            _menuSelectionIdx = 13;
            drawRecordTrack();
          }
        }
      }
    }

    // GPS Recording Loop (when ACTIVE)
    if (_recordingState == RECORD_ACTIVE) {
      unsigned long now = millis();
      if (now - _lastPointTime > 2000) {
        if (gpsManager.isFixed()) {
          double currentLat = gpsManager.getLatitude();
          double currentLon = gpsManager.getLongitude();
          if (_recordedPoints.size() > 0) {
            GPSPoint &lastPoint = _recordedPoints.back();
            double dist = gpsManager.distanceBetween(
                lastPoint.lat, lastPoint.lon, currentLat, currentLon);
            if (dist > 5) {
              GPSPoint newPoint;
              newPoint.lat = currentLat;
              newPoint.lon = currentLon;
              newPoint.timestamp = now;
              _recordedPoints.push_back(newPoint);
              double distToStart = gpsManager.distanceBetween(
                  _recordStartLat, _recordStartLon, currentLat, currentLon);
              if (distToStart < 15 && _recordedPoints.size() > 20) {
                _recordingState = RECORD_COMPLETE;
              }
            }
          }
        }
        _lastPointTime = now;
      }
    }

    // UI Refresh
    unsigned long now = millis();
    if (_needsStaticRedraw) {
      drawRecordTrackStatic();
      drawRecordTrack(); // Draw dynamic content immediately after static
      _needsStaticRedraw = false;
      _lastUpdate = now;
    }
    if (now - _lastUpdate > 500) {
      drawRecordTrack();
      _lastUpdate = now;
    }
  } else if (_state == STATE_NO_GPS) {
    if (touched) {
      // BACK Button (Bottom-Left)
      if (p.x < 100 && p.y > 240) {
        _state = STATE_MENU;
        _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                _ui->getBackgroundColor());
        drawMenu();
        _ui->drawStatusBar();
      }
    }
  }
}

// --- PEMBANTU PENGGAMBARAN ---

void LapTimerScreen::drawMenu() {
  TFT_eSPI *tft = _ui->getTft();

  // Header
  // tft->drawFastHLine(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, COLOR_SECONDARY); //
  // Optional: Status bar has line
  tft->setTextDatum(TC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(2);
  tft->setTextColor(COLOR_TEXT, COLOR_BG);
  tft->drawString("LAP TIMER", SCREEN_WIDTH / 2, STATUS_BAR_HEIGHT + 5);

  // Back Button (Blue Triangle)
  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, TFT_BLUE);

  // Buttons
  // Buttons
  int startY = 50;
  int btnHeight = 50;
  int btnWidth = 360;
  int gap = 8;
  int x = (SCREEN_WIDTH - btnWidth) / 2;

  const char *menuItems[] = {"SELECT TRACK", "RACE SCREEN", "RECORD TRACK",
                             "HISTORY"};

  for (int i = 0; i < 4; i++) {
    int y = startY + (i * (btnHeight + gap));

    // Determine Color based on selection
    uint16_t btnColor = (i == _menuSelectionIdx) ? TFT_RED : TFT_DARKGREY;

    tft->fillRoundRect(x, y, btnWidth, btnHeight, 5, btnColor);
    tft->setTextColor(TFT_WHITE, btnColor);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(menuItems[i], SCREEN_WIDTH / 2, y + btnHeight / 2 + 2);
  }
  _ui->drawStatusBar();
}

void LapTimerScreen::drawSearching() {
  TFT_eSPI *tft = _ui->getTft();
  int cx = SCREEN_WIDTH / 2;
  int cy = SCREEN_HEIGHT / 2 - 20;

  // --- Draw Icon ---
  tft->setTextColor(TFT_WHITE, COLOR_BG);

  // 1. Map (Trapezoid-like)
  int mapY = cy + 10;
  tft->drawLine(cx - 20, mapY, cx + 20, mapY, TFT_WHITE); // Top
  tft->drawLine(cx + 20, mapY, cx + 30, mapY + 25,
                TFT_WHITE); // Right Slope
  tft->drawLine(cx + 30, mapY + 25, cx - 30, mapY + 25,
                TFT_WHITE);                                    // Bottom
  tft->drawLine(cx - 30, mapY + 25, cx - 20, mapY, TFT_WHITE); // Left Slope

  // Dotted Path inside (Mock)
  for (int i = 0; i < 3; i++) {
    tft->fillCircle(cx - 15 + (i * 15), mapY + 12, 2, TFT_WHITE);
  }

  // 2. Pin (Above Map)
  int pinY = cy - 10;
  tft->fillCircle(cx, pinY, 8, TFT_WHITE); // Head
  tft->fillTriangle(cx - 8, pinY, cx + 8, pinY, cx, pinY + 15,
                    TFT_WHITE);            // Point
  tft->fillCircle(cx, pinY, 3, TFT_BLACK); // Hole

  // --- Draw Text ---
  tft->setTextDatum(TC_DATUM);
  tft->setFreeFont(&Org_01); // Standard font
  tft->setTextSize(1);
  tft->drawString("Searching nearby Tracks", cx, cy + 50);

  _ui->drawStatusBar();
}

void LapTimerScreen::drawTrackList() {
  TFT_eSPI *tft = _ui->getTft();

  // Clear Screen (Below StatusBar)
  tft->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                SCREEN_HEIGHT - STATUS_BAR_HEIGHT, TFT_BLACK);

  // --- 1. HEADER ---
  int headY = STATUS_BAR_HEIGHT; // Y-coord for header text
  // tft->drawFastHLine(0, headY, SCREEN_WIDTH, COLOR_SECONDARY); // Redundant

  // Title "Nearby Tracks" (Moved to Center for consistency)
  tft->setTextDatum(TC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(2);
  tft->setTextColor(TFT_WHITE, TFT_BLACK); // Global White Text
  tft->drawString("SELECT TRACK", SCREEN_WIDTH / 2, headY + 8);

  // Back Button (Blue Triangle)
  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, TFT_BLUE);

  // "New Track" Button (Top Right) -> "+" Icon style
  int btnX = SCREEN_WIDTH - 40;
  int btnY = 25;
  // Draw Circle Button
  // tft->fillCircle(btnX + 10, btnY + 10, 15, 0x10A2); // Slate Circle
  // tft->drawCircle(btnX + 10, btnY + 10, 15, TFT_WHITE);
  // tft->drawString("+", btnX + 5, btnY + 2);

  // Or "NEW" Text Button
  int newW = 50;
  int newH = 20;
  int newX = SCREEN_WIDTH - newW - 10;
  int newY = 30; // Moved down from 25 to avoid status bar line
  tft->fillRoundRect(newX, newY, newW, newH, 4, 0x10A2);
  tft->drawRoundRect(newX, newY, newW, newH, 4, TFT_WHITE);
  tft->setTextDatum(MC_DATUM);
  tft->setTextSize(1);
  tft->drawString("NEW", newX + newW / 2, newY + newH / 2 + 1);

  // --- 2. LIST ---
  int startY = 60;
  int itemH = 55; // Taller for card style
  int itemW = SCREEN_WIDTH - 20;
  int itemX = 10;
  int gap = 8;

  tft->setTextDatum(TL_DATUM);

  if (_tracks.empty()) {
    tft->setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("No tracks found.", SCREEN_WIDTH / 2, 100);
    tft->drawString("Enable GPS or Create New.", SCREEN_WIDTH / 2, 125);
    _ui->drawStatusBar();
    return;
  }

  // Draw Items (Limited to 3 per page to avoid overlapping back button)
  int maxVisible = 3;
  for (size_t i = 0; i < maxVisible; i++) {
    size_t trackIdx = i + _listScroll;
    if (trackIdx >= _tracks.size())
      break;

    int y = startY + (i * (itemH + gap));

    // Card BG
    tft->fillRoundRect(itemX, y, itemW - 40, itemH, 6, 0x18E3); // Charcoal
    tft->drawRoundRect(itemX, y, itemW - 40, itemH, 6, TFT_DARKGREY);

    // Track Icon (Left)
    tft->fillCircle(itemX + 20, y + itemH / 2, 4,
                    _tracks[trackIdx].isCustom ? TFT_CYAN : TFT_GOLD);

    // Name
    tft->setTextColor(TFT_WHITE, 0x18E3);
    tft->setTextFont(2); // Mid size
    tft->setTextDatum(ML_DATUM);
    tft->drawString(_tracks[trackIdx].name, itemX + 40, y + itemH / 2 - 5);

    // Detail (Configs + Date/Time)
    tft->setTextColor(TFT_SILVER, 0x18E3);
    tft->setTextFont(1);
    char buf[64];
    if (_tracks[trackIdx].createdDate != "") {
      sprintf(buf, "%d Configs | %s %s", (int)_tracks[trackIdx].configs.size(),
              _tracks[trackIdx].createdDate.c_str(),
              _tracks[trackIdx].createdTime.c_str());
    } else {
      sprintf(buf, "%d Configs", (int)_tracks[trackIdx].configs.size());
    }
    tft->drawString(buf, itemX + 40, y + itemH / 2 + 10);

    // Arrow Right
    tft->setTextColor(TFT_DARKGREY, 0x18E3);
    tft->drawString(">", itemX + itemW - 55, y + itemH / 2);
  }

  // --- 3. SCROLL INDICATORS ---
  if (_tracks.size() > maxVisible) {
    int scrollX = SCREEN_WIDTH - 25;
    // Up Arrow
    if (_listScroll > 0) {
      tft->fillTriangle(scrollX - 8, 75, scrollX + 8, 75, scrollX, 65,
                        TFT_BLUE);
    }
    // Down Arrow
    if (_listScroll + maxVisible < _tracks.size()) {
      tft->fillTriangle(scrollX - 8, 275, scrollX + 8, 275, scrollX, 285,
                        TFT_BLUE);
    }
    // Simple bar
    tft->drawFastVLine(scrollX, 90, 170, TFT_DARKGREY);
  }

  _ui->drawStatusBar();
}

void LapTimerScreen::drawTrackOptionsPopup() {
  TFT_eSPI *tft = _ui->getTft();

  // Popup Dimensions
  int w = 220;
  int h = 150;
  int x = (SCREEN_WIDTH - w) / 2;
  int y = (SCREEN_HEIGHT - h) / 2 + 10;

  // Draw Box (Black with White Border)
  tft->fillRoundRect(x, y, w, h, 5, TFT_BLACK);
  tft->drawRoundRect(x, y, w, h, 5, TFT_WHITE);

  // Options
  const char *options[] = {"Select", "Select & Edit", "Invert",
                           "Reinit best Lap", "Remove"};
  int startY = y + 10;
  int itemH = 25;

  tft->setTextDatum(TL_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);

  for (int i = 0; i < 5; i++) {
    tft->drawString(options[i], x + 15, startY + (i * itemH));
  }
}

void LapTimerScreen::drawTrackDetails() {
  TFT_eSPI *tft = _ui->getTft();
  if (_selectedTrackIdx < 0 || _selectedTrackIdx >= _tracks.size())
    return;
  Track &t = _tracks[_selectedTrackIdx];

  // 1. Background & Header
  tft->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                SCREEN_HEIGHT - STATUS_BAR_HEIGHT, TFT_BLACK);

  // Status Bar Separator Line
  // tft->drawFastHLine(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, COLOR_SECONDARY); //
  // Redundant

  // Title Box (Premium Style)
  int headW = 220;
  int headH = 34;
  int headX = (SCREEN_WIDTH - headW) / 2;
  int headY = STATUS_BAR_HEIGHT + 15;
  tft->fillRoundRect(headX, headY, headW, headH, 6, 0x10A2); // Slate
  tft->drawRoundRect(headX, headY, headW, headH, 6, TFT_SILVER);

  tft->setTextDatum(MC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(2);
  tft->setTextColor(TFT_WHITE, 0x10A2);
  tft->drawString("TRACK DETAILS", SCREEN_WIDTH / 2, headY + headH / 2 + 1);

  // Back Button (Blue Triangle)
  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, TFT_BLUE);

  // --- LAYOUT ---
  int margin = 15;
  int cardY = headY + headH + 20;
  int cardH = 145;
  int mapW = 210;
  int infoW = SCREEN_WIDTH - mapW - (margin * 3);

  // 1. MAP PREVIEW (Functional Trace)
  int mapX = margin;
  tft->fillRoundRect(mapX, cardY, mapW, cardH, 8, 0x18E3); // Charcoal
  tft->drawRoundRect(mapX, cardY, mapW, cardH, 8, TFT_DARKGREY);

  if (!_recordedPoints.empty()) {
    double minLat = 999, maxLat = -999, minLon = 999, maxLon = -999;
    for (const auto &p : _recordedPoints) {
      if (p.lat < minLat)
        minLat = p.lat;
      if (p.lat > maxLat)
        maxLat = p.lat;
      if (p.lon < minLon)
        minLon = p.lon;
      if (p.lon > maxLon)
        maxLon = p.lon;
    }

    int p = 15; // padding
    int dW = mapW - (p * 2);
    int dH = cardH - (p * 2);
    double latR = maxLat - minLat;
    double lonR = maxLon - minLon;

    if (latR < 0.000001)
      latR = 0.000001;
    if (lonR < 0.000001)
      lonR = 0.000001;

    for (size_t i = 1; i < _recordedPoints.size(); i++) {
      int x1 =
          mapX + p + (int)((_recordedPoints[i - 1].lon - minLon) / lonR * dW);
      int y1 = cardY + cardH - p -
               (int)((_recordedPoints[i - 1].lat - minLat) / latR * dH);
      int x2 = mapX + p + (int)((_recordedPoints[i].lon - minLon) / lonR * dW);
      int y2 = cardY + cardH - p -
               (int)((_recordedPoints[i].lat - minLat) / latR * dH);
      tft->drawLine(x1, y1, x2, y2, COLOR_ACCENT);
      tft->drawLine(x1 + 1, y1, x2 + 1, y2, COLOR_ACCENT);
    }

    tft->fillCircle(mapX + p +
                        (int)((_recordedPoints[0].lon - minLon) / lonR * dW),
                    cardY + cardH - p -
                        (int)((_recordedPoints[0].lat - minLat) / latR * dH),
                    3, TFT_YELLOW);
  } else {
    // Premium "No Map Data" Visual
    tft->setTextColor(TFT_DARKGREY, 0x18E3);
    tft->setFreeFont(&Org_01);
    tft->setTextSize(1);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("NO PATH DATA", mapX + mapW / 2, cardY + cardH / 2 - 10);

    // Tap to Map Action Hint
    tft->setTextColor(TFT_SILVER, 0x18E3);
    tft->setTextFont(1);
    tft->drawString("TAP HERE TO RECORD", mapX + mapW / 2,
                    cardY + cardH / 2 + 15);

    // Small Recording Icon Circle
    tft->fillCircle(mapX + mapW / 2, cardY + cardH / 2 + 35, 12, 0x10A2);
    tft->drawCircle(mapX + mapW / 2, cardY + cardH / 2 + 35, 12, TFT_DARKGREY);
    tft->fillCircle(mapX + mapW / 2, cardY + cardH / 2 + 35, 4, TFT_RED);
  }

  // 2. TRACK INFO CARD
  int infoX = mapX + mapW + margin;
  tft->fillRoundRect(infoX, cardY, infoW, cardH, 8, 0x18E3);
  tft->drawRoundRect(infoX, cardY, infoW, cardH, 8, TFT_DARKGREY);

  // Header for Info Card
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextDatum(TL_DATUM);
  tft->setTextColor(TFT_SILVER, 0x18E3);
  tft->drawString("TRACK INFO", infoX + 12, cardY + 12);

  // Rename Button (Premium Style)
  int editW = 45;
  int editX = infoX + infoW - editW - 8;
  tft->fillRoundRect(editX, cardY + 8, editW, 18, 4, 0x10A2); // Slate
  tft->drawRoundRect(editX, cardY + 8, editW, 18, 4, TFT_SILVER);
  tft->setTextColor(TFT_WHITE, 0x10A2);
  tft->setTextDatum(MC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->drawString("EDIT", editX + editW / 2, cardY + 17);

  // Track Name
  tft->setTextColor(TFT_WHITE, 0x18E3);
  tft->setFreeFont(nullptr); // Back to standard for value
  tft->setTextFont(2);
  tft->setTextDatum(TL_DATUM);
  String dispName = t.name;
  int maxNameW = infoW - 24;
  if (tft->textWidth(dispName) > maxNameW) {
    while (tft->textWidth(dispName + "...") > maxNameW &&
           dispName.length() > 0) {
      dispName = dispName.substring(0, dispName.length() - 1);
    }
    dispName += "...";
  }
  tft->drawString(dispName, infoX + 12, cardY + 34);

  // Stats Details
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextColor(TFT_SILVER, 0x18E3);
  tft->drawString("BEST LAP", infoX + 12, cardY + 62);

  tft->setFreeFont(nullptr);
  tft->setTextColor(TFT_GOLD, 0x18E3);
  tft->setTextFont(2);
  if (t.bestLap > 0) {
    char lapBuf[16];
    sprintf(lapBuf, "%02lu:%02lu.%1lu", (t.bestLap / 60000),
            (t.bestLap / 1000) % 60, (t.bestLap % 1000) / 100);
    tft->drawString(lapBuf, infoX + 12, cardY + 74);
  } else {
    tft->drawString("--:--.-", infoX + 12, cardY + 74);
  }

  tft->setFreeFont(&Org_01);
  tft->setTextColor(TFT_SILVER, 0x18E3);
  tft->drawString("CONFIGS", infoX + 12, cardY + 102);

  tft->setFreeFont(nullptr);
  tft->setTextColor(TFT_SKYBLUE, 0x18E3);
  tft->setTextFont(1);
  tft->drawString(String(t.configs.size()), infoX + 70, cardY + 102);

  // Date/Time
  if (t.createdDate != "") {
    tft->setTextFont(1);
    tft->setTextColor(TFT_DARKGREY, 0x18E3);
    tft->drawString(t.createdDate + " " + t.createdTime, infoX + 12,
                    cardY + 125);
  }

  // 3. ACTION SELECT BUTTON
  int btnW = 200;
  int btnH = 46;
  int btnX = (SCREEN_WIDTH - btnW) / 2;
  int btnY = cardY + cardH + 15;

  tft->fillRoundRect(btnX, btnY, btnW, btnH, 8, 0x05E0); // Green
  tft->setTextColor(TFT_BLACK, 0x05E0);
  tft->setTextDatum(MC_DATUM);
  tft->setTextFont(2);
  tft->drawString("SELECT TRACK", SCREEN_WIDTH / 2, btnY + btnH / 2 + 1);

  _ui->drawStatusBar();
}

void LapTimerScreen::drawRecordTrackStatic() {
  TFT_eSPI *tft = _ui->getTft();

  // Background
  tft->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                SCREEN_HEIGHT - STATUS_BAR_HEIGHT, TFT_BLACK);

  // --- 1. HEADER ---
  int headY = STATUS_BAR_HEIGHT;
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextDatum(TC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(2);
  tft->drawString("TRACK RECORDER", SCREEN_WIDTH / 2, headY + 8);

  // Back Button (Blue Triangle)
  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, TFT_BLUE);

  _ui->drawStatusBar();

  // Reset trackers to force dynamic redraw
  _lastRecordGpsFixed = false;
  _lastRecordSats = -1;
  _lastRecordedStateRender =
      (RecordingState)-1; // CRITICAL: Reset state tracker
}

void LapTimerScreen::drawRecordTrack() {
  TFT_eSPI *tft = _ui->getTft();
  extern GPSManager gpsManager;

  // --- LAYOUT CONSTANTS ---
  // --- LAYOUT CONSTANTS ---
  const int H_BTN = 42;
  const int Y_BTN = STATUS_BAR_HEIGHT + 5; // Top, under Status Bar (25)

  const int Y_HEADER = Y_BTN + H_BTN + 10; // ~77
  const int H_HEADER = 30;

  const int Y_METRICS = Y_HEADER + H_HEADER + 10; // ~117
  const int Y_METRICS_VAL = Y_METRICS + 22;

  const int Y_FEEDBACK = Y_METRICS_VAL + 50; // ~190

  // --- 1. STATE CHANGE DETECTION ---
  bool stateChanged = (_recordingState != _lastRecordedStateRender);

  if (stateChanged) {
    // Clear Button Area (to prevent overlap when switching states)
    tft->fillRect(0, Y_BTN, SCREEN_WIDTH, H_BTN + 5, TFT_BLACK);

    // --- 1. HEADER (Premium Style) ---
    int headY = STATUS_BAR_HEIGHT;
    // tft->drawFastHLine(0, headY, SCREEN_WIDTH, COLOR_SECONDARY); // Redundant

    // Title
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setTextDatum(TC_DATUM);
    tft->setFreeFont(&Org_01);
    tft->setTextSize(2);
    tft->drawString("TRACK RECORDER", SCREEN_WIDTH / 2, headY + 8);
  }

  // --- 2. GPS STATUS (Premium Card Style) ---
  int cardX = 10;
  int cardY = 55;
  int cardW = SCREEN_WIDTH - 20;
  int cardH = 40; // Compact height

  bool gpsFixed = gpsManager.isFixed();
  int sats = gpsManager.getSatellites();

  if (gpsFixed != _lastRecordGpsFixed || sats != _lastRecordSats ||
      stateChanged) {
    if (stateChanged) {
      // Card Background
      tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, 0x18E3); // Charcoal
      tft->drawRoundRect(cardX, cardY, cardW, cardH, 8, TFT_DARKGREY);

      // Label
      tft->setTextColor(TFT_SILVER, 0x18E3);
      tft->setTextDatum(ML_DATUM);
      tft->setTextFont(2);
      tft->drawString("GPS STATUS", cardX + 10, cardY + cardH / 2);
    }

    // Status Text
    uint16_t statusColor = TFT_RED;
    String statusText = "NO FIX";
    if (gpsFixed) {
      if (sats >= 6) {
        statusColor = TFT_GREEN;
        statusText = "READY";
      } else {
        statusColor = TFT_YELLOW;
        statusText = "WEAK";
      }
    }

    tft->setTextColor(statusColor, 0x18E3);
    tft->setTextDatum(MR_DATUM);
    tft->setTextPadding(120);
    tft->setTextFont(2);
    tft->setTextSize(1); // Ensure size is 1 to prevent glitch
    tft->drawString(statusText + " (" + String(sats) + ")", cardX + cardW - 10,
                    cardY + cardH / 2);
    tft->setTextPadding(0);

    _lastRecordGpsFixed = gpsFixed;
    _lastRecordSats = sats;
  }

  // --- 3. MAIN CONTENT DRAWING ---
  if (stateChanged) {
    // Clear Content Area (Below GPS Card)
    int clearY = cardY + cardH + 5;
    tft->fillRect(0, clearY, SCREEN_WIDTH, SCREEN_HEIGHT - clearY, TFT_BLACK);

    // Grid Layout for Metrics (Slate Boxes)
    int gridY = clearY + 10;
    int boxW = (SCREEN_WIDTH - 25) / 2;
    int boxH = 70;
    int box1X = 10;
    int box2X = 15 + boxW;

    // --- STATIC ELEMENTS PER STATE ---
    if (_recordingState == RECORD_IDLE) {
      // Instructions
      tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft->setTextFont(2);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("Go to Start Line", SCREEN_WIDTH / 2, gridY + 15);
      tft->drawString("& Tap Start", SCREEN_WIDTH / 2, gridY + 35);

    } else if (_recordingState == RECORD_ACTIVE) {
      // DRAW GRID BOXES
      // Box 1: Points
      tft->fillRoundRect(box1X, gridY, boxW, boxH, 6, 0x10A2); // Slate
      tft->setTextColor(TFT_SILVER, 0x10A2);
      tft->setTextFont(1);
      tft->setTextDatum(TL_DATUM);
      tft->drawString("POINTS", box1X + 8, gridY + 5);

      // Box 2: Time
      tft->fillRoundRect(box2X, gridY, boxW, boxH, 6, 0x10A2); // Slate
      tft->setTextColor(TFT_SILVER, 0x10A2);
      tft->setTextFont(1);
      tft->setTextDatum(TL_DATUM);
      tft->drawString("TIME", box2X + 8, gridY + 5);

      // STOP Button below grid
      int btnY = 240;

      // STOP Button
      int btnW = 180;
      int btnX = (SCREEN_WIDTH - btnW) / 2;
      uint16_t btnColor = (_menuSelectionIdx == 11) ? 0x8000 : TFT_RED;
      uint16_t txtColor = TFT_WHITE;

      tft->fillRoundRect(btnX, btnY, btnW, H_BTN, 8, btnColor);
      if (_menuSelectionIdx == 11)
        tft->drawRoundRect(btnX, btnY, btnW, H_BTN, 8, TFT_WHITE);

      tft->setTextColor(txtColor, btnColor);
      tft->setTextFont(4);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("STOP", SCREEN_WIDTH / 2, btnY + H_BTN / 2 + 2);

    } else if (_recordingState == RECORD_COMPLETE) {

      // Success Message in Grid area
      tft->setTextColor(TFT_GREEN, TFT_BLACK);
      tft->setTextFont(4);
      tft->setTextDatum(MC_DATUM);
      // FIX: Move UP (was gridY + 25)
      tft->drawString("DONE!", SCREEN_WIDTH / 2, gridY + 10);

      int btnY = gridY + boxH + 20;

      // Buttons: SAVE | DISCARD
      int btnW = 100;
      int gap = 20;
      int startX = (SCREEN_WIDTH - (btnW * 2 + gap)) / 2;

      uint16_t saveColor = (_menuSelectionIdx == 12) ? 0x03E0 : TFT_GREEN;
      uint16_t saveTxt = (_menuSelectionIdx == 12) ? TFT_WHITE : TFT_BLACK;

      tft->fillRoundRect(startX, btnY, btnW, H_BTN, 6, saveColor);
      if (_menuSelectionIdx == 12)
        tft->drawRoundRect(startX, btnY, btnW, H_BTN, 6, TFT_WHITE);

      tft->setTextColor(saveTxt, saveColor);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("SAVE", startX + btnW / 2, btnY + H_BTN / 2);

      uint16_t delColor = (_menuSelectionIdx == 13) ? 0x8000 : TFT_RED;

      tft->fillRoundRect(startX + btnW + gap, btnY, btnW, H_BTN, 6, delColor);
      if (_menuSelectionIdx == 13)
        tft->drawRoundRect(startX + btnW + gap, btnY, btnW, H_BTN, 6,
                           TFT_WHITE);

      tft->setTextColor(TFT_WHITE, delColor);
      tft->drawString("DEL", startX + btnW + gap + btnW / 2, btnY + H_BTN / 2);
    }
    _lastRecordedStateRender = _recordingState;
  }

  // --- 4. DYNAMIC UPDATES ---
  int clearY = cardY + cardH + 5;
  int gridY = clearY + 10;
  int boxW = (SCREEN_WIDTH - 25) / 2;
  int box1X = 10;
  int box2X = 15 + boxW;

  if (_recordingState == RECORD_IDLE) {
    static bool lastReady = false;
    bool ready = true; // BYPASS

    if (ready != lastReady || stateChanged) {
      int btnW = 180;
      int btnX = (SCREEN_WIDTH - btnW) / 2;
      int btnY = 240;

      if (ready) {
        uint16_t btnColor = (_menuSelectionIdx == 10) ? 0x05E0 : TFT_GREEN;
        uint16_t txtColor = (_menuSelectionIdx == 10) ? TFT_WHITE : TFT_BLACK;

        tft->fillRoundRect(btnX, btnY, btnW, H_BTN, 8, btnColor);
        if (_menuSelectionIdx == 10)
          tft->drawRoundRect(btnX, btnY, btnW, H_BTN, 8, TFT_WHITE);

        tft->setTextColor(txtColor, btnColor);
        tft->setTextFont(4);
        tft->setTextDatum(MC_DATUM);
        tft->drawString("START", SCREEN_WIDTH / 2, btnY + H_BTN / 2 + 2);

        // Clear Msg
        tft->fillRect(0, btnY - 30, SCREEN_WIDTH, 25, TFT_BLACK);

      } else {
        tft->fillRect(btnX, btnY, btnW, H_BTN, TFT_BLACK);
        tft->setTextColor(TFT_ORANGE, TFT_BLACK);
        tft->setTextFont(2);
        tft->setTextDatum(MC_DATUM);
        tft->drawString("WAITING FOR GPS...", SCREEN_WIDTH / 2, btnY + 20);
      }
      lastReady = ready;
    }
  } else if (_recordingState == RECORD_ACTIVE) {
    // Dynamic Values inside Boxes

    // Points Value (Left Box)
    tft->setTextColor(TFT_SKYBLUE, 0x10A2);
    tft->setTextFont(4);
    tft->setTextSize(1); // Ensure size 1
    tft->setTextDatum(MC_DATUM);
    tft->setTextPadding(boxW - 10);
    tft->drawNumber(_recordedPoints.size(), box1X + boxW / 2, gridY + 28);

    // Time Value (Right Box)
    tft->setTextColor(TFT_WHITE, 0x10A2);
    unsigned long elapsed = (millis() - _recordingStartTime) / 1000;
    tft->setTextPadding(boxW - 10);
    tft->drawString(String(elapsed) + "s", box2X + boxW / 2, gridY + 28);
    tft->setTextPadding(0);

    // Feedback
    int feedY = 230;
    double currentLat = gpsManager.getLatitude();
    double currentLon = gpsManager.getLongitude();
    double distToStart = gpsManager.distanceBetween(
        _recordStartLat, _recordStartLon, currentLat, currentLon);

    tft->setTextFont(2);
    tft->setTextDatum(MC_DATUM);
    tft->setTextPadding(200);

    if (distToStart < 20 && _recordedPoints.size() > 10) {
      tft->setTextColor(TFT_GREEN, TFT_BLACK);
      tft->drawString("FINISH DETECTED!", SCREEN_WIDTH / 2, feedY);
    } else {
      tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft->drawString("Dist: " + String(distToStart, 0) + "m", SCREEN_WIDTH / 2,
                      feedY);
    }
    tft->setTextPadding(0);

  } else if (_recordingState == RECORD_COMPLETE) {
    if (stateChanged) {
      tft->setTextColor(TFT_WHITE, TFT_BLACK);
      tft->setTextFont(2);
      tft->setTextDatum(MC_DATUM);

      unsigned long elapsed =
          (_recordedPoints.back().timestamp - _recordingStartTime) / 1000;
      String stats = String(_recordedPoints.size()) + " Pts  |  " +
                     String(elapsed) + " Sec";

      // FIX: Move text UP to avoid overlap with buttons at gridY + 70
      // Previous: gridY + 70 (Overlapped)
      // New: Stats at gridY + 35
      tft->drawString(stats, SCREEN_WIDTH / 2, gridY + 35);
    }
  }

  // Back Button (Blue Triangle) - Always draw over updates
  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, TFT_BLUE);
}

void LapTimerScreen::drawNoGPS() {
  TFT_eSPI *tft = _ui->getTft();

  // Colors (Renamed to avoid config.h macro conflicts)
  uint16_t L_COLOR_BG = TFT_BLACK;
  uint16_t L_COLOR_CARD = 0x18E3; // Charcoal
  uint16_t L_COLOR_BTN = 0x10A2;  // Slate
  uint16_t L_COLOR_TEXT = TFT_WHITE;
  uint16_t L_COLOR_LABEL = TFT_SILVER;

  tft->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                SCREEN_HEIGHT - STATUS_BAR_HEIGHT, L_COLOR_BG);

  // --- HEADER ---
  int headY = STATUS_BAR_HEIGHT;
  // tft->drawFastHLine(0, headY, SCREEN_WIDTH, COLOR_SECONDARY); // Redundant

  tft->setTextColor(TFT_WHITE, L_COLOR_BG);
  tft->setTextDatum(TC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(2);
  tft->drawString("GPS STATUS", SCREEN_WIDTH / 2, headY + 8);

  // --- MESSAGE CARD ---
  int cardW = 260;
  int cardH = 100;
  int cardX = (SCREEN_WIDTH - cardW) / 2;
  int cardY = (SCREEN_HEIGHT - cardH) / 2;

  tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, L_COLOR_CARD);
  tft->drawRoundRect(cardX, cardY, cardW, cardH, 8, TFT_DARKGREY);

  // Message Text
  tft->setTextColor(TFT_RED, L_COLOR_CARD);
  tft->setTextSize(1);
  tft->setTextDatum(MC_DATUM);
  tft->drawString("NO SATELLITES FIX", SCREEN_WIDTH / 2, cardY + 30);

  tft->setTextColor(L_COLOR_LABEL, L_COLOR_CARD);
  tft->drawString("Cannot record track.", SCREEN_WIDTH / 2, cardY + 55);
  tft->drawString("Please check GPS antenna.", SCREEN_WIDTH / 2, cardY + 75);

  // 3. START BUTTON (Bottom Center)
  int btnW = 180;
  int btnH = 45;
  int btnX = (SCREEN_WIDTH - btnW) / 2;
  int btnY = 255; // Shifted Down (was 240)

  tft->fillRoundRect(btnX, btnY, btnW, btnH, 6, L_COLOR_BTN);
  tft->drawRoundRect(btnX, btnY, btnW, btnH, 6, TFT_WHITE);

  tft->setTextColor(TFT_WHITE, L_COLOR_BTN);
  tft->setTextSize(1);
  tft->drawString("CONTINUE", SCREEN_WIDTH / 2, btnY + (btnH / 2) - 2);

  _ui->drawStatusBar();
}

void LapTimerScreen::drawSummary() {
  TFT_eSPI *tft = _ui->getTft();
  tft->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                SCREEN_HEIGHT - STATUS_BAR_HEIGHT, TFT_BLACK);

  // --- 1. HEADER ---
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextDatum(TC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(2);
  tft->drawString("SESSION SUMMARY", SCREEN_WIDTH / 2, STATUS_BAR_HEIGHT + 3);

  // Back Arrow
  tft->fillTriangle(10, 290, 25, 282, 25, 298, COLOR_ACCENT);

  // --- DATA PROCESSING ---
  int bestIdx = -1;
  unsigned long bestTime = 0;
  unsigned long totalTime = 0;

  if (!_lapTimes.empty()) {
    bestTime = _lapTimes[0];
    bestIdx = 0;
    for (int i = 0; i < (int)_lapTimes.size(); i++) {
      totalTime += _lapTimes[i];
      if (_lapTimes[i] < bestTime) {
        bestTime = _lapTimes[i];
        bestIdx = i;
      }
    }
  }

  // --- 2. BEST LAP CARD ---
  int cardX = 10;
  int cardY = 55;
  int cardW = SCREEN_WIDTH - 20;
  int cardH = 65;

  tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, 0x18E3);
  tft->drawRoundRect(cardX, cardY, cardW, cardH, 8, TFT_DARKGREY);

  tft->setTextSize(1);
  tft->setTextFont(2);
  tft->setTextColor(TFT_SILVER, 0x18E3);
  tft->setTextDatum(TL_DATUM);
  tft->drawString("BEST LAP", cardX + 10, cardY + 8);

  if (bestIdx != -1) {
    String lapTag = "LAP " + String(bestIdx + 1);
    int tagW = tft->textWidth(lapTag);
    tft->fillRoundRect(cardX + cardW - tagW - 15, cardY + 8, tagW + 10, 16, 4,
                       TFT_GOLD);
    tft->setTextColor(TFT_BLACK, TFT_GOLD);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(lapTag, cardX + cardW - 10 - tagW / 2, cardY + 16);

    int ms = bestTime % 1000;
    int s = (bestTime / 1000) % 60;
    int m = (bestTime / 60000);
    char buf[16];
    sprintf(buf, "%d:%02d.%02d", m, s, ms / 10);

    tft->setTextColor(TFT_WHITE, 0x18E3);
    tft->setTextFont(6);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(buf, cardX + cardW / 2, cardY + cardH / 2 + 8);
  } else {
    tft->setTextColor(TFT_DARKGREY, 0x18E3);
    tft->setTextFont(4);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("NO SESSIONS", cardX + cardW / 2, cardY + cardH / 2 + 8);
  }

  // --- 3. STATS GRID ---
  int gridY = cardY + cardH + 10;
  int boxW = (SCREEN_WIDTH - 25) / 2;
  int boxH = 45;

  tft->fillRoundRect(10, gridY, boxW, boxH, 6, 0x10A2);
  tft->setTextColor(TFT_SILVER, 0x10A2);
  tft->setTextFont(1);
  tft->setTextDatum(TL_DATUM);
  tft->drawString("TOTAL LAPS", 18, gridY + 5);
  tft->setTextFont(4);
  tft->setTextColor(TFT_SKYBLUE, 0x10A2);
  tft->setTextDatum(MC_DATUM);
  tft->drawNumber(_lapCount, 10 + boxW / 2, gridY + 25);

  tft->fillRoundRect(15 + boxW, gridY, boxW, boxH, 6, 0x10A2);
  tft->setTextColor(TFT_SILVER, 0x10A2);
  tft->setTextFont(1);
  tft->setTextDatum(TL_DATUM);
  tft->drawString("MAX RPM", 23 + boxW, gridY + 5);
  tft->setTextFont(4);
  tft->setTextColor(TFT_ORANGE, 0x10A2);
  tft->setTextDatum(MC_DATUM);
  tft->drawNumber(_maxRpmSession, 15 + boxW + boxW / 2, gridY + 25);

  // --- 4. DATA LIST ---
  int listY = gridY + boxH + 10;
  tft->drawFastHLine(20, listY, SCREEN_WIDTH - 40, TFT_DARKGREY);
  tft->setTextFont(1);
  tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft->setTextDatum(TL_DATUM);
  tft->drawString("RECENT LAPS", 20, listY + 5);

  int itemsToShow = 3;
  int startIdx = (_lapTimes.size() > (size_t)itemsToShow)
                     ? _lapTimes.size() - itemsToShow
                     : 0;
  int rowY = listY + 20;

  for (int i = (int)_lapTimes.size() - 1; i >= startIdx; i--) {
    if (i < 0)
      break;
    unsigned long t = _lapTimes[i];
    int ms = t % 1000;
    int s = (t / 1000) % 60;
    int m = (t / 60000);
    char buf[32];
    uint16_t color = (i == bestIdx) ? TFT_GREEN : TFT_WHITE;
    sprintf(buf, "%d:%02d.%02d", m, s, ms / 10);
    tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft->drawString(String(i + 1) + ".", 20, rowY);
    tft->setTextColor(color, TFT_BLACK);
    tft->setTextFont(2);
    tft->drawString(buf, 60, rowY);

    if (bestIdx != -1 && i != bestIdx) {
      long delta = (long)t - (long)bestTime;
      int d_ms = abs(delta) % 1000;
      int d_s = abs(delta) / 1000;
      sprintf(buf, "%s%d.%02d", (delta > 0) ? "+" : "-", d_s, d_ms / 10);
      tft->setTextColor((delta > 0) ? TFT_RED : TFT_GREEN, TFT_BLACK);
      tft->setTextDatum(TR_DATUM);
      tft->drawString(buf, SCREEN_WIDTH - 20, rowY);
      tft->setTextDatum(TL_DATUM);
    }
    rowY += 20;
  }
  _ui->drawStatusBar();
}

// Racing logic moved to RaceScreen.cpp

// Racing logic moved to RaceScreen.cpp

// --- TRACK CREATOR WIZARD ---
void LapTimerScreen::drawCreateTrack() {
  TFT_eSPI *tft = _ui->getTft();
  extern GPSManager gpsManager;

  _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                            SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
  _ui->drawStatusBar(true);

  int headY = STATUS_BAR_HEIGHT + 10;
  tft->drawFastHLine(0, headY + 30, SCREEN_WIDTH, COLOR_SECONDARY);

  tft->setTextDatum(TC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(2);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->drawString("NEW TRACK", SCREEN_WIDTH / 2, headY + 5);

  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, TFT_BLUE);

  int contentY = headY + 45;
  tft->setTextDatum(TC_DATUM);
  tft->setTextFont(1);
  tft->setTextSize(1);
  tft->setTextColor(TFT_SILVER);
  String stepStr = "STEP " + String(_createStep + 1) + "/2";
  tft->drawString(stepStr, SCREEN_WIDTH / 2, contentY);

  int cardX = 20, cardY = contentY + 15, cardW = SCREEN_WIDTH - 40, cardH = 60;
  tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, 0x18E3);
  tft->drawRoundRect(cardX, cardY, cardW, cardH, 8, TFT_DARKGREY);

  String instr = (_createStep == 0)
                     ? "Go to START Line"
                     : (_createStep == 1 ? "Go to FINISH Line" : "SAVING...");
  tft->setTextDatum(MC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(2);
  tft->setTextColor(TFT_WHITE, 0x18E3);
  tft->drawString(instr, SCREEN_WIDTH / 2, cardY + cardH / 2 + 3);

  int gpsY = cardY + cardH + 15;
  bool fixed = gpsManager.isFixed();
  tft->setTextFont(2);
  tft->setTextSize(1);
  tft->setTextColor(fixed ? TFT_CYAN : TFT_RED, COLOR_BG);

  char coordBuf[64];
  if (fixed) {
    sprintf(coordBuf, "LAT: %.6f   LON: %.6f", gpsManager.getLatitude(),
            gpsManager.getLongitude());
  } else {
    sprintf(coordBuf, "WAITING FOR GPS FIX...");
  }
  tft->drawString(coordBuf, SCREEN_WIDTH / 2, gpsY + 10);

  int btnW = 220, btnH = 50, btnX = (SCREEN_WIDTH - btnW) / 2,
      btnY = SCREEN_HEIGHT - 85;
  uint16_t btnColor = fixed ? TFT_GREEN : 0x4208;

  if (_createStep < 2) {
    if (_createStep == 1) {
      int smallBtnW = 180;
      tft->fillRoundRect(70, btnY, smallBtnW, btnH, 8, TFT_CYAN);
      tft->setTextColor(TFT_BLACK, TFT_CYAN);
      tft->setFreeFont(&Org_01);
      tft->setTextSize(1);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("SAME AS START", 70 + smallBtnW / 2, btnY + btnH / 2);

      tft->fillRoundRect(270, btnY, smallBtnW, btnH, 8, btnColor);
      tft->setTextColor(TFT_BLACK, btnColor);
      tft->drawString("SET FINISH", 270 + smallBtnW / 2, btnY + btnH / 2);
    } else {
      tft->fillRoundRect(btnX, btnY, btnW, btnH, 8, btnColor);
      tft->drawRoundRect(btnX, btnY, btnW, btnH, 8, TFT_WHITE);
      tft->setTextColor(TFT_BLACK, btnColor);
      tft->setFreeFont(&Org_01);
      tft->setTextSize(1);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("SET START LINE", btnX + btnW / 2, btnY + btnH / 2);
    }
  }
  _ui->drawStatusBar();
}

void LapTimerScreen::saveNewTrack(String name, double sLat, double sLon,
                                  double fLat, double fLon, String path) {
  if (!SD.exists("/tracks.json")) {
    File f = SD.open("/tracks.json", FILE_WRITE);
    if (f) {
      f.print("{\"tracks\":[]}");
      f.close();
    }
  }
  File file = SD.open("/tracks.json", FILE_READ);
  if (!file)
    return;
  JsonDocument doc;
  deserializeJson(doc, file);
  file.close();
  JsonArray tracks = doc["tracks"];
  JsonObject t = tracks.add<JsonObject>();
  t["name"] = name;
  t["lat"] = sLat;
  t["lon"] = sLon;
  if (fLat != 0 || fLon != 0) {
    t["f_lat"] = fLat;
    t["f_lon"] = fLon;
  }
  if (path != "") {
    t["path"] = path;
  }
  t["date"] = gpsManager.getDateString();
  t["time"] = gpsManager.getTimeString();
  JsonArray cfgs = t["configs"].to<JsonArray>();
  cfgs.add("Default");
  File wFile = SD.open("/tracks.json", FILE_WRITE);
  if (wFile) {
    serializeJson(doc, wFile);
    wFile.close();
  }
}

void LapTimerScreen::drawRenameTrack(bool force) {
  TFT_eSPI *tft = _ui->getTft();
  static String lastRenamingName = "";
  static bool lastShift = !_keyboardShift;
  if (force)
    lastRenamingName = "";
  bool fullRedraw = force || (lastRenamingName == "");
  if (fullRedraw) {
    _ui->drawCarbonBackground(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                              SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
    tft->setTextDatum(TC_DATUM);
    tft->setFreeFont(&Org_01);
    tft->setTextSize(2);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->drawString("RENAME TRACK", SCREEN_WIDTH / 2, 28);
  }
  int boxY = 60, boxH = 40;
  if (fullRedraw || lastRenamingName != _renamingName) {
    tft->fillRoundRect(20, boxY, SCREEN_WIDTH - 40, boxH, 8, 0x18E3);
    tft->drawRoundRect(20, boxY, SCREEN_WIDTH - 40, boxH, 8, TFT_GOLD);
    tft->setTextColor(TFT_WHITE, 0x18E3);
    tft->setTextFont(2);
    tft->setTextSize(1);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(_renamingName + "|", SCREEN_WIDTH / 2, boxY + boxH / 2 + 1);
    lastRenamingName = _renamingName;
  }
  if (fullRedraw || lastShift != _keyboardShift) {
    _keyboard.draw(tft, 100, _keyboardShift);
    lastShift = _keyboardShift;
  }
  if (fullRedraw)
    _ui->drawStatusBar();
}

void LapTimerScreen::renameTrack(int index, String newName) {
  if (index < 0 || index >= (int)_tracks.size() || newName.length() == 0)
    return;
  _tracks[index].name = newName;
  if (!SD.exists("/tracks.json"))
    return;
  File file = SD.open("/tracks.json", FILE_READ);
  if (!file)
    return;
  JsonDocument doc;
  deserializeJson(doc, file);
  file.close();
  JsonArray tracks = doc["tracks"];
  if (index < (int)tracks.size()) {
    tracks[index]["name"] = newName;
    File wFile = SD.open("/tracks.json", FILE_WRITE);
    if (wFile) {
      serializeJson(doc, wFile);
      wFile.close();
    }
  }
}

void LapTimerScreen::drawSaveTrackName(bool force) {
  TFT_eSPI *tft = _ui->getTft();
  if (force) {
    tft->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                  SCREEN_HEIGHT - STATUS_BAR_HEIGHT, _ui->getBackgroundColor());
    tft->setTextFont(1);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_PRIMARY, _ui->getBackgroundColor());
    tft->setTextDatum(TC_DATUM);
    tft->drawString("NAME YOUR TRACK", SCREEN_WIDTH / 2, STATUS_BAR_HEIGHT + 8);
    int boxW = 300, boxH = 40, boxX = (SCREEN_WIDTH - boxW) / 2, boxY = 40;
    tft->drawRect(boxX, boxY, boxW, boxH, COLOR_SECONDARY);
    tft->fillRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, TFT_DARKGREY);
    int btnY = 275, btnW = 100, gap = 20,
        startX = (SCREEN_WIDTH - (btnW * 2 + gap)) / 2;
    int cancelX = startX;
    tft->fillRect(cancelX, btnY, btnW, 40, TFT_RED);
    tft->drawRect(cancelX, btnY, btnW, 40, TFT_WHITE);
    tft->setTextColor(TFT_WHITE, TFT_RED);
    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(1);
    tft->drawString("CANCEL", cancelX + btnW / 2, btnY + 20);
    int saveX = startX + btnW + gap;
    tft->fillRect(saveX, btnY, btnW, 40, COLOR_PRIMARY);
    tft->drawRect(saveX, btnY, btnW, 40, TFT_BLACK);
    tft->setTextColor(TFT_BLACK, COLOR_PRIMARY);
    tft->drawString("SAVE", saveX + btnW / 2, btnY + 20);
  }
  int boxW = 300, boxX = (SCREEN_WIDTH - boxW) / 2, boxY = 40;
  tft->setTextFont(1);
  tft->setTextSize(2);
  tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft->setTextDatum(ML_DATUM);
  tft->setTextPadding(boxW - 10);
  tft->drawString(_renamingName, boxX + 10, boxY + 20);
  tft->setTextPadding(0);
  _keyboard.draw(tft, 100, _keyboardShift);
}
