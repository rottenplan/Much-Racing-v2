#ifndef MENU_SCREEN_H
#define MENU_SCREEN_H

#include "../UIManager.h"

class MenuScreen : public UserScreen {
public:
  void begin(UIManager *ui) override { _ui = ui; }
  void onShow() override;
  void update() override;

private:
  UIManager *_ui;
  int _selectedIndex = 0;
  int _currentPage = 0;
  int _touchStartY;

  static const int ITEMS_PER_PAGE = 4;
  unsigned long _lastTouchTime = 0;

  int _lastSelectedIndex;
  int _lastTapIdx;
  unsigned long _lastTapTime;

  void drawMenu(bool force = false);
};

#endif
