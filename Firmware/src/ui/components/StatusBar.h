#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <TFT_eSPI.h>

class UIManager; // Forward declaration

class StatusBar {
public:
  StatusBar(UIManager *ui);
  void begin();

  // Update logic: checks if redraw is needed
  void update();

  // Force redraw (e.g. on screen switch)
  void draw(bool force = false);

private:
  UIManager *_ui;
  TFT_eSPI *_tft;
  TFT_eSprite *_sprite = nullptr;

  // State tracking to minimize flicker
  int _lastWifiStatus = -1;
  bool _lastFix = false;
  double _lastHdop = -1.0;
  int _lastSignalStrength = -1;
  int _lastSats = -1;
  int _lastBat = -1;
  bool _wasCharging = false;
  bool _lastLogging = false;
  String _lastTimeStr = "";
  bool _lastRpmConn = false;

  unsigned long _lastUpdateCheck = 0;
};

#endif
