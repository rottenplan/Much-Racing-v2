#ifndef GNSS_LOG_SCREEN_H
#define GNSS_LOG_SCREEN_H

#include "../UIManager.h"
#include <vector>

class GnssLogScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void onHide() override;
  void update() override;

private:
  UIManager *_ui;
  std::vector<String> _lines;
  bool _paused = false;
  unsigned long _lastDataTime = 0;
  unsigned long _lastByteTime = 0;
  bool _noDataShown = false;
  bool _lastStatusConnected = false;
  bool _needsRedraw = true;
  String _buffer = ""; // For assembling lines

  void drawLines();
  void drawCheckboxes();
};

#endif
