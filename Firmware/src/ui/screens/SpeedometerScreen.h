#ifndef SPEEDOMETER_SCREEN_H
#define SPEEDOMETER_SCREEN_H

#include "../UIManager.h"
#include <Arduino.h>

class SpeedometerScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void onHide() override;
  void update() override;

private:
  UIManager *_ui;
  void drawDashboard(bool force = false);

  float _lastSpeed = -1;
  float _maxSpeed = 0;
  int _lastRPM = -1;
  int _lastSats = -1; // Added for GPS signal
  float _lastTrip = -1;
  String _lastTime = "";
  bool _lastUnits = false; // false = km/h, true = mph
  float _lastAccY = 0;
  float _lastVolt = -1; // Voltase motor (12V)

  // Navigation data
  float _lastHeading = -1;
  double _lastLat = 0;
  double _lastLng = 0;

  // Double tap detection
  unsigned long _lastTapTime = 0;
  int _tapCount = 0;
};

#endif
