#ifndef TOUCH_DEBUG_SCREEN_H
#define TOUCH_DEBUG_SCREEN_H

#include "../UIManager.h"

class TouchDebugScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void update() override;

private:
  UIManager *_ui;
  unsigned long _lastUpdate;
  int _pointCount;
};

#endif
