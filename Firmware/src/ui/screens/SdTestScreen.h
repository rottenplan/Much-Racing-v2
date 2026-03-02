#ifndef SD_TEST_SCREEN_H
#define SD_TEST_SCREEN_H

#include "../../core/SessionManager.h"
#include "../UIManager.h"
#include <Arduino.h>

class SdTestScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void update() override;
  void onHide() override;

private:
  UIManager *_ui;
  SessionManager::SDTestResult _result;
  bool _isTesting = false;
  int _progress = 0;
  String _statusText = "Initializing...";

  void startTest();
  void drawScreen(bool full = true);
  void drawProgress();
  // Static callback for SessionManager
  static void progressCallback(int percent, String status);
  static SdTestScreen *_instance;
};

#endif
