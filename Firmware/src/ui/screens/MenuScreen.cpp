#include "MenuScreen.h"
#include "../../core/GPSManager.h"
#include "SettingsScreen.h"

// extern GPSManager gpsManager; // If needed

#include "../fonts/Org_01.h"

#include <stdlib.h>

#define MENU_ITEMS 7
const char *menuLabels[MENU_ITEMS] = {"LAP TIMER",   "DRAG METER", "RPM SENSOR",
                                      "SPEEDOMETER", "GPS STATUS", "SETTINGS",
                                      "SYNCHRONIZE"};

void MenuScreen::onShow() {
  // _selectedIndex is now persistent member
  // _currentPage is now persistent member
  _lastSelectedIndex = -1;
  _lastTouchTime = 0;

  // Auto-calculate page based on currently selected index
  _currentPage = _selectedIndex / ITEMS_PER_PAGE;

  _touchStartY = -1;
  _lastTapIdx = -1;
  _lastTapTime = 0;

  // Draw Static Layout (Background & Title)
  TFT_eSPI *tft = _ui->getTft();
  // Redundant - UIManager already clears the content area
  // tft->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
  //               SCREEN_HEIGHT - STATUS_BAR_HEIGHT, COLOR_BG);
  // tft->drawFastHLine(0, 20, SCREEN_WIDTH, COLOR_SECONDARY); // Redundant with
  // Status Bar

  tft->setTextDatum(TC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(FONT_SIZE_MENU_TITLE);
  tft->setTextColor(COLOR_ACCENT);
  tft->drawString("MAIN MENU", SCREEN_WIDTH / 2, 38);

  drawMenu(true);
}

void MenuScreen::update() {
  bool enter = false;
  UIManager::TouchPoint p = _ui->getTouchPoint();

  // Layout Constants for 480x320
  int startY = 75;     // Start below title
  int gap = 50;        // Gap between items
  int itemHeight = 42; // Button height

  if (p.x != -1) { // Touched

    if (_touchStartY == -1) {
      _touchStartY = p.y;
    } else {
      // Just tracking for swipe on release or continuous?
      // Let's do "Swipe to Switch" - Trigger on Release or Threshold
    }

    // Check for Swipe vs Tap
    // We handle logic slightly differently:
    // If finger moves significantly -> Swipe Page
    // If finger stays -> Tap Item
  } else {
    // Touch Released
    _touchStartY = -1;
  }

  // Re-implementing Swipe/Tap Logic inside the "Touched" block properly
  if (p.x != -1) {
    if (_touchStartY == -1)
      _touchStartY = p.y;

    // Calculate Delta from Start
    int deltaY = _touchStartY - p.y;

    if (abs(deltaY) > 40) { // Significant Swipe
      // Debounce Page Switch
      static unsigned long lastPageSwitch = 0;
      if (millis() - lastPageSwitch > TOUCH_DEBOUNCE_MS) {
        if (deltaY > 0) { // Swipe Up -> Prev Page
          if (_currentPage > 0) {
            _currentPage--;
            drawMenu(true);
            lastPageSwitch = millis();
            _touchStartY = p.y; // Reset anchor
          }
        } else { // Swipe Down -> Next Page
          int maxPage = (MENU_ITEMS - 1) / ITEMS_PER_PAGE;
          if (_currentPage < maxPage) {
            _currentPage++;
            drawMenu(true);
            lastPageSwitch = millis();
            _touchStartY = p.y;
          }
        }
      }
    }
    // Tap Detection
    else {
      static unsigned long lastPageSwitch = 0;

      // 1. Check Navigation Arrow (Bottom Center)
      int maxPage = (MENU_ITEMS - 1) / ITEMS_PER_PAGE;

      // Toggle Button Area (Y > 280, below last menu item area)
      if (p.y > 280) {
        if (millis() - lastPageSwitch > TOUCH_DEBOUNCE_MS) {
          if (_currentPage < maxPage) {
            _currentPage++; // Go Down/Next
          } else if (_currentPage > 0) {
            _currentPage--; // Go Up/Prev
          }
          drawMenu(true);
          lastPageSwitch = millis();
        }
        return; // Skip item tap
      }

      // Removed Top Arrow Logic as we now use a single toggle button at the
      // bottom

      // 2. Item Tap Detection - Expanded hit areas (50px gap)
      int localIndex = -1;
      for (int i = 0; i < ITEMS_PER_PAGE; i++) {
        int yTop = startY + (i * gap);
        int yBot =
            yTop +
            gap; // Changed from itemHeight to gap (50px) to remove dead zones
        if (p.y >= yTop && p.y <= yBot) {
          localIndex = i;
          break;
        }
      }

      if (localIndex != -1) {
        int actualIndex = (_currentPage * ITEMS_PER_PAGE) + localIndex;
        if (actualIndex < MENU_ITEMS) {
          if (millis() - _lastTouchTime > TOUCH_DEBOUNCE_MS) { // Debounce
            // Check Double Tap
            if (_lastTapIdx == actualIndex && (millis() - _lastTapTime < 500)) {
              enter = true;
              _lastTapIdx = -1;
            } else {
              _lastTapIdx = actualIndex;
              _lastTapTime = millis();
              if (_selectedIndex != actualIndex) {
                _selectedIndex = actualIndex;
                drawMenu(false);
              }
            }
            _lastTouchTime = millis();
          }
        }
      }
    }
  } else {
    _touchStartY = -1;
  }

  if (enter) {
    switch (_selectedIndex) {
    case 0:
      _ui->switchScreen(SCREEN_LAP_TIMER);
      break;
    case 1:
      _ui->switchScreen(SCREEN_DRAG_METER);
      break;
    case 2:
      _ui->switchScreen(SCREEN_RPM_SENSOR);
      break;
    case 3:
      _ui->switchScreen(SCREEN_SPEEDOMETER);
      break;
    case 4:
      _ui->switchScreen(SCREEN_GPS_STATUS);
      break;
    case 5:
      SettingsScreen::startMode = SettingsScreen::MODE_MAIN;
      _ui->switchScreen(SCREEN_SETTINGS);
      break;
    case 6:
      _ui->switchScreen(SCREEN_SYNCHRONIZE);
      break;
    }
  }

  // static unsigned long lastStatusUpdate = 0;
  // if (millis() - lastStatusUpdate > 1000) {
  //   _ui->drawStatusBar();
  //   lastStatusUpdate = millis();
  // }
}

void MenuScreen::drawMenu(bool force) {
  TFT_eSPI *tft = _ui->getTft();

  if (force) {
    // Clear only the LIST area (below title) to prevent flicker
    tft->fillRect(0, 65, SCREEN_WIDTH, SCREEN_HEIGHT - 65, COLOR_BG);
  }

  tft->setTextSize(FONT_SIZE_MENU_ITEM);

  int startY = 75;
  int gap = 50;
  int itemHeight = 42;
  int rectWidth = SCREEN_WIDTH - 20;
  int rectX = 10;

  // Draw Items for CURRENT PAGE
  int startIndex = _currentPage * ITEMS_PER_PAGE;
  int endIndex = startIndex + ITEMS_PER_PAGE;
  if (endIndex > MENU_ITEMS)
    endIndex = MENU_ITEMS;

  for (int i = 0; i < (endIndex - startIndex); i++) {
    int actualIndex = startIndex + i;
    int yPos = startY + (i * gap);

    // Only draw if forced OR if this item's selection state changed
    bool stateChanged =
        (actualIndex == _selectedIndex || actualIndex == _lastSelectedIndex);

    if (force || stateChanged) {
      // Clear item area
      tft->fillRect(rectX, yPos, rectWidth, itemHeight, COLOR_BG);

      if (actualIndex == _selectedIndex) {
        tft->setTextColor(COLOR_BG, COLOR_HIGHLIGHT);
        tft->fillRoundRect(rectX, yPos, rectWidth, itemHeight, 5,
                           COLOR_HIGHLIGHT);
      } else {
        tft->setTextColor(COLOR_TEXT, COLOR_BG);
      }

      tft->setTextDatum(MC_DATUM);
      tft->setFreeFont(&Org_01);
      tft->setTextSize(FONT_SIZE_MENU_ITEM);
      tft->drawString(menuLabels[actualIndex], SCREEN_WIDTH / 2,
                      yPos + (itemHeight / 2));
    }
  }

  _lastSelectedIndex = _selectedIndex;

  if (force) {
    // Page Indicators (only on full redraw/page change)
    tft->setTextDatum(BC_DATUM);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_SECONDARY);

    int maxPage = (MENU_ITEMS - 1) / ITEMS_PER_PAGE;

    if (maxPage > 0) {
      if (_currentPage < maxPage) {
        // Down arrow (smaller, 8px width)
        tft->fillTriangle(SCREEN_WIDTH / 2 - 8, 295, SCREEN_WIDTH / 2 + 8, 295,
                          SCREEN_WIDTH / 2, 305, COLOR_ACCENT);
      } else if (_currentPage > 0) {
        // Up arrow (smaller, 8px width)
        tft->fillTriangle(SCREEN_WIDTH / 2 - 8, 305, SCREEN_WIDTH / 2 + 8, 305,
                          SCREEN_WIDTH / 2, 295, COLOR_ACCENT);
      }
    }
  }

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}
