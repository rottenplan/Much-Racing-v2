#ifndef NAVIGATION_SCREEN_H
#define NAVIGATION_SCREEN_H

#include "../UIManager.h"
#include <Arduino.h>

class NavigationScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void onHide() override {}
  void update() override;

private:
  UIManager *_ui;

  void drawAll(bool force = true);
  void drawIdle();
  void drawRoute();
  void drawArrowIcon(int maneuver, int cx, int cy, int size, uint16_t color);
  void drawArriveIcon(int cx, int cy, int size);
  void drawRoundaboutIcon(int cx, int cy, int size, uint16_t color);
  void drawStatusChip(bool btConnected);

  // Change detection caches
  int _lastManeuver = -2;
  long _lastDistance = -2;
  String _lastInstruction;
  bool _lastConnected = false;
  bool _lastActive = false;
  bool _lastHasBt = false;
  bool _lastMqtt = false;
  unsigned long _lastBeepMs = 0;
};

#endif
