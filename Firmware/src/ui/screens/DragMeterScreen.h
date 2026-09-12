#ifndef DRAG_METER_SCREEN_H
#define DRAG_METER_SCREEN_H

#include "../UIManager.h"
#include <Arduino.h>
#include <vector>

class DragMeterScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void onHide() override; // Add onHide
  void update() override;

private:
  UIManager *_ui;
  enum State {
    STATE_MENU,
    STATE_SETTING_MENU,
    STATE_VALUE_EDITOR,
    STATE_RUNNING,
    STATE_SUMMARY_VIEW
  };
  State _state;

  struct Discipline {
    String name;
    bool isDistance;          // true=distance (m), false=speed (km/h)
    float startSpeed;         // Start speed
    float target;             // meters or km/h (End target)
    unsigned long resultTime; // ms
    bool completed;
    float endSpeed;        // speed at completion
    float slope;           // %
    float peakSpeed;       // kph
    float brakingDistance; // meters
    bool valid;            // slope check
  };

  std::vector<Discipline> _disciplines;
  std::vector<Discipline> _sessionBest;
  bool _summaryShowBest;

  float _brakingStartDistance;
  bool _brakingMeasurable;
  float _totalRunDistance;

  unsigned long _startTime;
  unsigned long _lastUpdate;
  int _selectedBtn; // -1:None, 0:Back, 1:Reset

  // Interpolation State
  float _previousSpeed;
  unsigned long _previousRunTime;
  float _previousDistance;

  // Display Data
  float _currentSpeed;
  float _slope;
  String _highlightTitle;
  String _highlightValue;

  std::vector<String> _menuItems;
  std::vector<String> _settingItems;
  int _selectedMenuIdx;
  int _selectedSettingIdx;
  int _lastTapIdx;
  unsigned long _lastTapTime;

  // Custom Settings Values
  int _customStartKph;
  int _customEndKph;
  int _customDist20m;
  int _customDist30m;
  int _customDist35m;
  String _editingTarget; // "START_KPH", "END_KPH", "KPH_SETTING", etc.
  int _editingFocus;     // 0 for Start, 1 for End

  // Generic UI Helpers
  void drawGenericMenu(const std::vector<String> &items, int selectedIdx);
  int getTouchedIndex(int startY, int btnH, int gap, int btnW,
                      UIManager::TouchPoint p);

  void drawMenu();
  void drawSettingMenu();
  void drawSummary();
  void drawValueEditor();
  void handleMenuTouch(int idx);
  void handleSettingTouch(int idx);
  void handleValueTouch(UIManager::TouchPoint p);

  // Logic
  // Logic
  void checkStartCondition();
  void checkStopCondition();
  void updateDisciplines();
  void loadCustomDisciplines();
  void refreshSettingLabels();

  // Advanced Run Logic
  enum RunState {
    RUN_WAITING,
    RUN_TREE_READY,
    RUN_COUNTDOWN,
    RUN_RUNNING,
    RUN_FINISHED
  };
  RunState _runState;

  bool _rolloutEnabled;
  bool _oneFootReached;
  unsigned long _treeInterval;
  unsigned long _reactionTime;
  unsigned long _runStartTime;
  float _startPosition; // To track distance for rollout
  int _lastTreeCount;
  bool _lastTreeIsGo;
  bool _wasOverlayActive;
  uint16_t _lastHighlightBgColor;
  unsigned long _lastDrawTime = 0; // Frame throttling timestamp

  // Geometric Tracking
  double _startLat = 0.0;
  double _startLon = 0.0;
  double _startAlt = 0.0;

  void startChristmasTree();
  void drawChristmasTreeOverlay();

  // Predictive Mode
  enum DisplayMode { DISPLAY_NORMAL, DISPLAY_PREDICTIVE };
  DisplayMode _displayMode;

  float _targetTime;
  float _referenceTime; // Best run time
  float _predictedFinalTime;

  void calculatePrediction();
  void saveReferenceRun();
  void loadReferenceRun();

  // Drawing
  void drawPredictiveMode();
  void drawSpeedArea(bool dynamicOnly);
  void drawDashboardStatic(bool forceStatusBar = true);
  void drawDashboardDynamic();
  void drawResults(); // Summary
};

#endif
