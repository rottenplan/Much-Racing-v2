#ifndef GPS_DEBUG_SCREEN_H
#define GPS_DEBUG_SCREEN_H

#include "../UIManager.h"

class GpsDebugScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void update() override;
  void handleTouch(int x,
                   int y); // Note: handleTouch isn't in UserScreen interface,
                           // usually handled in update() or separately

private:
  UIManager *_ui;
  unsigned long _lastUpdate = 0;
  void drawDebugStatic();
  void drawDebugDynamic();
};

#endif
