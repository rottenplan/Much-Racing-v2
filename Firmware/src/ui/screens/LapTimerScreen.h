#ifndef LAP_TIMER_SCREEN_H
#define LAP_TIMER_SCREEN_H

#include "../UIManager.h"
#include "../components/KeyboardComponent.h"

#include <Arduino.h>
#include <vector>

// GPS Point for track recording
struct GPSPoint {
  double lat;
  double lon;
  unsigned long timestamp;
};

struct TrackConfig {
  String name;
  // TODO: Add sectors/finish line coordinates here
};

struct Track {
  String name;
  std::vector<TrackConfig> configs;
  double lat;
  double lon;
  bool isCustom = true;
  String pathFile; // Path to CSV file containing track points
  unsigned long bestLap = 0;
  String createdDate;
  String createdTime;
};

enum LapTimerState {
  STATE_MENU,
  STATE_TRACK_LIST,
  STATE_TRACK_MENU, // Context Popup
  STATE_TRACK_DETAILS,
  STATE_SEARCHING,
  STATE_SUMMARY,
  STATE_RECORD_TRACK,
  STATE_CREATE_TRACK,
  STATE_RENAME_TRACK,
  STATE_SAVE_TRACK,
  STATE_NO_GPS
};
enum RaceMode { MODE_BEST, MODE_LAST, MODE_PREDICTIVE };
enum RecordingState { RECORD_IDLE, RECORD_ACTIVE, RECORD_COMPLETE };

class LapTimerScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void onHide() override;
  void update() override;

private:
  UIManager *_ui;
  LapTimerState _state;
  RaceMode _raceMode;
  int _menuSelectionIdx = -1;
  unsigned long _lastUpdate;
  unsigned long _lastTouchTime = 0;
  unsigned long _lastModeTapTime = 0;
  unsigned long _lastBackTapTime = 0;
  unsigned long _searchStartTime = 0; // For Searching delay
  bool _isRecording;
  bool _needsStaticRedraw;

  // GPS Track Recording
  RecordingState _recordingState;
  std::vector<GPSPoint> _recordedPoints;
  double _recordStartLat, _recordStartLon;
  unsigned long _recordingStartTime;
  unsigned long _lastPointTime;
  double _totalDistance;

  // Legacy (can be removed later)
  double _tempStartLat, _tempStartLon;
  int _tempSplitCount;

  // UI Methods
  void drawSummary();
  void drawLapList(int scrollOffset);
  void drawMenu(); // Sub-menu
  void drawTrackList();
  void drawTrackOptionsPopup();
  void drawTrackDetails();
  void drawSearching();                       // New Searching Screen
  void drawRecordTrackStatic();               // Static part of Record UI
  void drawRecordTrack();                     // Dynamic part of Record UI
  void drawCreateTrack();                     // Track Creator Wizard
  void drawRenameTrack(bool force = false);   // Renaming UI
  void drawSaveTrackName(bool force = false); // Save Naming UI
  void drawNoGPS();

  // Lap Data
  double _finishLat, _finishLon;
  bool _finishSet;
  unsigned long _currentLapStart;
  long _lastLapTime;
  long _bestLapTime;
  int _lapCount;
  std::vector<unsigned long> _lapTimes; // Store lap times in ms
  int _listScroll;                      // Scroll offset for list

  // Finish Line Detection State
  bool _finishLineInside;
  unsigned long _lastFinishCross;

  // Track Data
  std::vector<Track> _tracks;
  int _selectedTrackIdx;
  int _selectedConfigIdx;
  String _currentTrackName;             // Store selected track name
  void loadTracks();                    // Load dummy tracks
  void loadTrackPath(String filename);  // Load track path from CSV
  void saveTrackToGPX(String filename); // New helper
  void saveTrackToCSV(String filename); // For UI Preview compatibility

  void saveNewTrack(String name, double sLat, double sLon, double fLat,
                    double fLon, String path = "");
  void renameTrack(int index, String newName);

  // Flicker Reduction Tracking
  float _lastSpeed = -1.0;
  int _lastSats = -1;
  bool _lastRecordGpsFixed = false;
  int _lastRecordSats = -1;
  RecordingState _lastRecordedStateRender = (RecordingState)-1;
  float _maxSpeedSession = 0.0;
  float _maxSpeedSessionRender = -1.0;
  unsigned long _maxRpmSession = 0;
  float _lastAccYRender = -999.0;

  // Track Creator State
  int _createStep; // 0=Start, 1=Finish, 2=Save
  double _createStartLat, _createStartLon;
  double _createFinishLat, _createFinishLon;

  // Renaming State
  KeyboardComponent _keyboard;
  String _renamingName;
  bool _keyboardShift = true;
};

#endif
