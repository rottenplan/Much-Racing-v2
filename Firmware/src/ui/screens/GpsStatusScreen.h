#ifndef GPS_STATUS_SCREEN_H
#define GPS_STATUS_SCREEN_H

#include "../UIManager.h"

class GpsStatusScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void update() override;

private:
  UIManager *_ui;
  void drawStatus();

  // Trackers
  int _lastSats = -1;
  int _lastHz = -1;
  double _lastHdop = -1.0;
  double _lastLat = 0;
  double _lastLng = 0;
  double _lastHdopValue = -1.0;
  bool _lastFixed = false;

  // Double Tap Logic
  unsigned long _lastTapTime = 0;
  bool _waitingForDoubleTap = false;

  // Frame throttling
  unsigned long _lastDrawTime = 0;
};

#endif
