#ifndef RPM_SENSOR_SCREEN_H
#define RPM_SENSOR_SCREEN_H

#include "../UIManager.h"

class RpmSensorScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void onHide() override;
  void update() override;

private:
  UIManager *_ui;
  void drawScreen();

  // UI Helpers
  void drawGraphGrid();
  void drawGraphLine();
  void updateValues();

  // Constants
  static const int GRAPH_WIDTH = 450;
  static const int GRAPH_HEIGHT = 154;
  const int MAX_SPEED_SCALE = 150; // Max speed for graph scaling (km/h)

  TFT_eSprite *_graphSprite = nullptr;
  bool _spriteAllocated = false;

  // State
  int _currentRpm;
  int _maxRpm;
  int _currentLvl;

  // Speed Tracking
  int _currentSpeed;
  int _speedHistory[GRAPH_WIDTH];

  // Graph Data
  int _graphHistory[GRAPH_WIDTH];
  int _graphIndex;

  unsigned long _lastUpdate;

  // Interrupt Handling
  static volatile unsigned long _rpmPulses;
  static volatile unsigned long _lastPulseMicros;
  static void IRAM_ATTR onPulse();
  unsigned long _lastRpmCalcTime;
};

#endif
