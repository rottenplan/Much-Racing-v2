#ifndef SPLASH_SCREEN_H
#define SPLASH_SCREEN_H

#include "../UIManager.h"

class SplashScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void update() override;

  // New method for status updates
  void setLoadingStatus(const char *status);

private:
  UIManager *_ui;
  unsigned long _startTime;
  int _progress = 0;
  unsigned long _lastUpdate = 0;
  void drawGlitch();
};

#endif
