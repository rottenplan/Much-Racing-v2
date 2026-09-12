#ifndef RACE_SCREEN_H
#define RACE_SCREEN_H

#include "../UIManager.h"
#include "LapTimerScreen.h" // For Track struct access
#include <Arduino.h>

class RaceScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void onHide() override;
  void update() override;

  void setTrack(const Track &track) { _currentTrack = track; }

private:
  UIManager *_ui;
  Track _currentTrack;
  int _beaconWidth = 50; // Default 50m

  // Racing State
  bool _isRecording;
  unsigned long _currentLapStart;
  long _lastLapTime;
  long _bestLapTime;
  int _lapCount;
  std::vector<unsigned long> _lapTimes;
  unsigned long _lastUpdate;
  unsigned long _lastTouchTime;

  // Predictive / Delta State
  float _currentDelta = 0.0;
  double _currentLapDist = 0.0;
  int _lastSector = 0;
  unsigned long _sectorStartTime = 0;
  float _sectorDistThresholds[2];
  long _sessionSectorBest[3] = {0, 0, 0};

  // UI Notifications
  unsigned long _notifStartTime = 0;
  String _notifText = "";
  String _notifValue = "";          // Second line/detail
  uint16_t _notifColor = TFT_BLACK; // Second line/detail
  bool _notifRendered = false;      // Flicker fix

  // Performance Trackers (Flicker Reduction)
  float _lastSpeed = -999.0;
  int _lastSats = -1;
  int _lastRpmRender = -1;
  unsigned long _lastMaxRpmRender = 0;
  int _lastLapCountRender = -1;
  long _lastLastLapTimeRender = -1;
  long _lastBestLapTimeRender = -1;
  float _maxSpeedSession = 0.0;
  float _maxSpeedSessionRender = -1.0;
  unsigned long _maxRpmSession = 0;
  unsigned long _maxRpmSessionRender = 0;
  float _lastAccYRender = -999.0;
  float _lastRollRender = -999.0;
  float _lastDeltaRender = -999.0;

  void drawRacingStatic();
  void drawRacing();
  void drawRPMBar(int rpm, int maxRpm);
  void drawTrackMap(int x, int y, int w, int h);
  void checkFinishLine();
  void finalizeRaceSession();

  // Finish Line
  bool _finishLineInside;
  unsigned long _lastFinishCross;
  unsigned long _lastInteractionTime;

  // Interp state
  double _lastLat = 0, _lastLon = 0;
  double _lastDistToFinish = 0;
  unsigned long _lastPointTime = 0;

  // G-Force double tap
  unsigned long _lastGForceTapTime = 0;
  int _gforceTapCount = 0;
};

#endif
