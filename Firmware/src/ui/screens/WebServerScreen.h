#ifndef WEB_SERVER_SCREEN_H
#define WEB_SERVER_SCREEN_H

#include "../../core/WiFiManager.h"
#include "../UIManager.h"
#include <Arduino.h>

class WebServerScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void update() override;

private:
  UIManager *_ui;
  unsigned long _lastUpdate;
  unsigned long _lastTouchTime;

  // Render Helpers
  void drawStatic();
  void drawStatus();
  void drawClients();
};

#endif
