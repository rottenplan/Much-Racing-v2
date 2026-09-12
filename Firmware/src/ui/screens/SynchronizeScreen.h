#ifndef SYNCHRONIZE_SCREEN_H
#define SYNCHRONIZE_SCREEN_H

#include "../UIManager.h"

class SynchronizeScreen : public UserScreen {
public:
  void begin(UIManager *ui) override;
  void onShow() override;
  void update() override;

private:
  UIManager *_ui;
  void drawScreen(bool fullRedraw = true);
  void handleTouch(int x, int y);
  void performSync();

  String _statusMessage;
  String _detailMessage;
  bool _isSyncing;
  bool _lastSyncSuccess;
  unsigned long _lastTouchTime;
};

#endif
