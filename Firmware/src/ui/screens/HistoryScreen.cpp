#include "HistoryScreen.h"
#include "../../config.h"
#include "../../core/SessionManager.h"
#include "../../core/SyncManager.h"
#include "../../core/WiFiManager.h"
#include "../fonts/Org_01.h"

extern SessionManager sessionManager;
extern SyncManager syncManager;
extern WiFiManager wifiManager;

void HistoryScreen::onShow() {
  _scrollOffset = 0;
  _selectedIdx = -1;

  // Only reset to menu if no filter was preset
  if (!_filterPreset) {
    _currentMode = MODE_MENU;
    scanHistory();
  }

  // Reset Variables
  _wasTouching = false;
  _touchStartX = -1;
  _touchStartY = -1;
  _touchStartTime = 0;
  _lastBackTapTime = 0;
  _isDragging = false;
  _ignoreInitialTouch = true;

  // Reset Replay State to ensure fresh load
  _selectedLapForReplay = -1;
  _replayPoints.clear();
  _replayIdx = 0;
  _lastReplayIdx = -1;
  _lastDotX = -1;
  _lastDotY = -1;
  _currentAnalysis.filename = ""; // Force re-analysis if needed

  TFT_eSPI *tft = _ui->getTft();
  // Safe Clear (Keep Status Bar) - Redundant with UIManager
  // tft->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
  //               SCREEN_HEIGHT - STATUS_BAR_HEIGHT, TFT_BLACK);
  _ui->drawStatusBar(true);

  // --- STATIC HEADER ---
  // tft->drawFastHLine(0, 20, SCREEN_WIDTH, COLOR_SECONDARY); // Redundant

  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextDatum(TC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(FONT_SIZE_MENU_TITLE);
  tft->drawString("HISTORY", SCREEN_WIDTH / 2, STATUS_BAR_HEIGHT + 10);

  // Back Button (Blue Triangle) - Bottom Left
  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, TFT_BLUE);

  // Draw appropriate view based on current mode
  if (_filterPreset && _currentMode == MODE_GROUPS) {
    drawGroups(0);
    _filterPreset = false; // Reset after first show
  } else {
    drawMenu();
  }
}

void HistoryScreen::setFilterType(const char *type, ScreenType returnScreen) {
  _selectedType = type;
  _currentMode = MODE_GROUPS;
  _filterPreset = true;         // Mark that filter is preset
  _returnScreen = returnScreen; // Store return screen
  _returnScreen = returnScreen; // Store return screen
  scanHistory();                // Load data (including dummy)
  scanGroups();
  _scrollOffset = 0;
  _selectedIdx = -1;
  // Note: Drawing will happen in onShow() after switchScreen is called
}

void HistoryScreen::onHide() {
  _historyList.clear();
  _historyList.shrink_to_fit();
  _groups.clear();
  _groups.shrink_to_fit();
}

void HistoryScreen::update() {
  // --- REPLAY ANIMATION TICK ---
  if (_currentMode == MODE_VIEW_DATA) {
    bool isDrag = (_historyList[_lastTapIdx].type == "DRAG");
    if (_viewPage == 3 && !isDrag) {
      if (millis() - _lastReplayTick > 30) { // ~33 FPS
        _lastReplayTick = millis();
        drawLapReplay(_currentAnalysis);
      }
    }
  }

  UIManager::TouchPoint p = _ui->getTouchPoint();
  bool isTouching = (p.x != -1);

  // Anti-Ghosting / Debounce Logic
  if (_ignoreInitialTouch) {
    if (!isTouching) {
      _ignoreInitialTouch = false; // Finger released, ready for input
    } else {
      return; // Ignore lingering touch
    }
  }

  // --- STATE MACHINE: START ---
  if (isTouching && !_wasTouching) {
    _touchStartX = p.x;
    _touchStartY = p.y;
    _touchStartTime = millis();
    _isDragging = false;
    _lastTouchY = p.y;
    _wasTouching = true;
  }

  // --- STATE MACHINE: DRAGGING ---
  if (isTouching && _wasTouching) {
    int dy = p.y - _lastTouchY;
    if (abs(p.y - _touchStartY) > _dragThreshold) {
      _isDragging = true;
    }

    if (_isDragging) {
      if (_currentMode == MODE_GROUPS) {
        if (abs(dy) > 5) {
          if (dy > 0 && _scrollOffset > 0) {
            _scrollOffset--;
            drawGroups(_scrollOffset);
            _lastTouchY = p.y;
          } else if (dy < 0 && _scrollOffset < (int)_groups.size() - 7) {
            _scrollOffset++;
            drawGroups(_scrollOffset);
            _lastTouchY = p.y;
          }
        }
      } else if (_currentMode == MODE_LIST) {
        // Calculate filter count for bounds
        int filteredCount = 0;
        for (const auto &item : _historyList) {
          if (item.type == _selectedType && item.date.length() >= 10) {
            String g =
                item.date.substring(6, 10) + "-" + item.date.substring(3, 5);
            if (g == _selectedGroup)
              filteredCount++;
          }
        }
        int maxScroll = filteredCount - 7;

        if (abs(dy) > 5) {
          if (dy > 0 && _scrollOffset > 0) {
            _scrollOffset--;
            drawList(_scrollOffset);
            _lastTouchY = p.y;
          } else if (dy < 0 && _scrollOffset < maxScroll) {
            _scrollOffset++;
            drawList(_scrollOffset);
            _lastTouchY = p.y;
          }
        }
      }
    }
  }

  // --- STATE MACHINE: RELEASE (TAP) ---
  if (!isTouching && _wasTouching) {
    _wasTouching = false;
    _lastTouchY = -1;

    // Only register tap if short duration and not dragged far
    if (!_isDragging && (millis() - _touchStartTime < 500)) {
      int tx = _touchStartX;
      int ty = _touchStartY;

      // 1. Back button (Standardized Bottom Left Area)
      // Adjusted to > 275 to avoid overlap with bottom list items
      if (tx < 80 && ty > 275) {
        if (_currentMode == MODE_GROUPS) {
          // Return to calling screen (Lap Timer or Drag Meter)
          _ui->switchScreen(_returnScreen);
          return;
        } else if (_currentMode == MODE_LIST) {
          _currentMode = MODE_GROUPS;
          _scrollOffset = 0;
          _selectedIdx = -1;
          // Clear only content area
          _ui->getTft()->fillRect(0, 60, SCREEN_WIDTH, SCREEN_HEIGHT - 60,
                                  TFT_BLACK);
          drawGroups(0);
          return;
        } else if (_currentMode == MODE_OPTIONS) {
          _currentMode = MODE_LIST;
          // Clear only content area
          // Clear from STATUS_BAR_HEIGHT to remove title
          _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                  SCREEN_HEIGHT - STATUS_BAR_HEIGHT, TFT_BLACK);
          drawList(_scrollOffset);
          return;
        } else if (_currentMode == MODE_VIEW_DATA) {
          _currentMode = MODE_OPTIONS;
          // Clear only content area
          // Clear from STATUS_BAR_HEIGHT to remove title
          _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                  SCREEN_HEIGHT - STATUS_BAR_HEIGHT, TFT_BLACK);
          _selectedIdx = 0;
          drawOptions();
          return;
        }
      }

      // 2. Scroll Buttons (Bottom Right Footer Zone)
      // Matches SettingsScreen: y > 280, x > SCREEN_WIDTH - 120
      if (ty > 280 && tx > SCREEN_WIDTH - 120) {
        if (_currentMode == MODE_GROUPS) {
          int maxScroll = (_groups.size() > 5) ? _groups.size() - 5 : 0;
          if (tx < SCREEN_WIDTH - 60) {
            // Up
            if (_scrollOffset > 0) {
              _scrollOffset--;
              drawGroups(_scrollOffset);
            }
          } else {
            // Down
            if (_scrollOffset < maxScroll) {
              _scrollOffset++;
              drawGroups(_scrollOffset);
            }
          }
          return; // Handled
        } else if (_currentMode == MODE_LIST) {
          // Recalculate maxScroll for List
          int filteredCount = 0;
          for (const auto &item : _historyList) {
            if (item.type == _selectedType && item.date.length() >= 10) {
              String g =
                  item.date.substring(6, 10) + "-" + item.date.substring(3, 5);
              if (g == _selectedGroup)
                filteredCount++;
            }
          }
          int maxScroll = (filteredCount > 7) ? filteredCount - 7 : 0;

          if (tx < SCREEN_WIDTH - 60) {
            // Up
            if (_scrollOffset > 0) {
              _scrollOffset--;
              drawList(_scrollOffset);
            }
          } else {
            // Down
            if (_scrollOffset < maxScroll) {
              _scrollOffset++;
              drawList(_scrollOffset);
            }
          }
          return; // Handled
        }
      }

      // Mode Specific Tap Logic
      if (_currentMode == MODE_GROUPS) {
        int listY = 80; // Matched to drawGroups
        int itemH = 26; // Standardized item height
        if (ty > listY) {
          int visIdx = (ty - listY) / itemH;
          int actualIdx = visIdx + _scrollOffset;
          if (actualIdx >= 0 && actualIdx < _groups.size()) {
            _selectedGroup = _groups[actualIdx];
            _currentMode = MODE_LIST;
            _scrollOffset = 0;
            _selectedIdx = -1;
            // Clear only content area (below title)
            _ui->getTft()->fillRect(0, 60, SCREEN_WIDTH, SCREEN_HEIGHT - 60,
                                    TFT_BLACK);
            drawList(0);
          }
        }

      } else if (_currentMode == MODE_LIST) {
        int listY = 80; // Matched to drawList
        int itemH = 26; // Standardized item height
        if (ty > listY) {
          int visIdx = (ty - listY) / itemH;
          int count = 0;
          int skip = 0;
          int targetIdx = -1;
          for (int i = 0; i < (int)_historyList.size(); i++) {
            if (_historyList[i].type == _selectedType) {
              if (_historyList[i].date.length() >= 10) {
                String g = _historyList[i].date.substring(6, 10) + "-" +
                           _historyList[i].date.substring(3, 5);
                if (g == _selectedGroup) {
                  if (skip < _scrollOffset) {
                    skip++;
                    continue;
                  }
                  if (count == visIdx) {
                    targetIdx = i;
                    break;
                  }
                  count++;
                }
              }
            }
          }
          if (targetIdx != -1) {
            _lastTapIdx = targetIdx;
            _currentMode = MODE_OPTIONS;
            _selectedIdx = 0;
            // Clear only content area
            _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                    SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                    COLOR_BG);
            drawOptions();
          }
        }

      } else if (_currentMode == MODE_OPTIONS) {
        int startY = 60;
        int h = 50;
        int idx = (ty - startY) / h;
        if (idx >= 0 && idx < 3) {
          _selectedIdx = idx;
          drawOptions();
          if (idx == 0) { // View Data
            _currentMode = MODE_VIEW_DATA;
            _viewPage = 0;
            _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                    SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                    TFT_BLACK);
            _selectedLapForReplay = -1; // Reset Replay for this session
            _replayPoints.clear();
            drawViewData();
          } else if (idx == 1) { // Synchronize
            syncSession();
          } else if (idx == 2) { // Delete
            _currentMode = MODE_CONFIRM_DELETE;
            _selectedIdx = 1;
            _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                    SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                    TFT_BLACK);
            drawConfirmDelete();
          }
        }

      } else if (_currentMode == MODE_VIEW_DATA) {
        // Tap anywhere (except back which is handled)
        // Check Type - Drag sessions only have 1 page (Summary)
        bool isDrag = (_historyList[_lastTapIdx].type == "DRAG");
        if (isDrag) {
          return; // Ignore general taps for Drag Summary to prevent flicker
        } else {
          _viewPage++;
          if (_viewPage > 4) // Cycle through 5 pages (0-4)
            _viewPage = 0;
        }

        // Reset replay state if entering map page to force full UI redraw
        if (_viewPage == 3) {
          _selectedLapForReplay = -1;
          _lastDotX = -1;
          _lastDotY = -1;
        }
        drawViewData();

      } else if (_currentMode == MODE_CONFIRM_DELETE) {
        int y = 160;
        int btnH = 40;
        if (ty > y && ty < y + btnH) {
          int btnW = 100;
          int gap = 20;
          int startX = (SCREEN_WIDTH - (btnW * 2 + gap)) / 2;
          int idx = -1;
          if (tx > startX && tx < startX + btnW)
            idx = 0; // Yes
          else if (tx > startX + btnW + gap && tx < startX + btnW + gap + btnW)
            idx = 1; // No

          if (idx != -1) {
            _selectedIdx = idx;
            drawConfirmDelete();
            if (idx == 0) { // YES
              if (_lastTapIdx >= 0 && _lastTapIdx < (int)_historyList.size()) {
                sessionManager.deleteSession(
                    _historyList[_lastTapIdx].filename);
                scanHistory();
                scanGroups();
                _currentMode = MODE_LIST;
                _selectedIdx = -1;
                // Clear only content area
                _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                        SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                        COLOR_BG);
                drawList(0);
              }
            } else { // NO
              _currentMode = MODE_OPTIONS;
              _selectedIdx = 2;
              // Clear only content area
              _ui->getTft()->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                                      SCREEN_HEIGHT - STATUS_BAR_HEIGHT,
                                      COLOR_BG);
              drawOptions();
            }
          }
        }
      }
    }
  }
}

void HistoryScreen::scanHistory() {
  _historyList = sessionManager.loadHistoryItems();

  // --- DUMMY DATA FOR TESTING (Requested by User) ---
  // Added back for testing if needed, though loadHistoryItems is preferred
  if (_historyList.empty()) {
    HistoryItem dummyTrack;
    dummyTrack.filename = "dummy_track";
    dummyTrack.date = "01/01/2026 10:00";
    dummyTrack.laps = 8;
    dummyTrack.bestLap = 12500; // 12.5s
    dummyTrack.type = "TRACK";
    _historyList.push_back(dummyTrack);

    HistoryItem dummyDrag;
    dummyDrag.filename = "dummy_drag";
    dummyDrag.date = "02/01/2026 14:00";
    dummyDrag.laps = 1;
    dummyDrag.bestLap = 4200; // 4.2s
    dummyDrag.type = "DRAG";
    _historyList.push_back(dummyDrag);
  }
}

void HistoryScreen::drawMenu() {
  // This should never be called since we now filter directly
  // But keep it as a safety fallback
  TFT_eSPI *tft = _ui->getTft();
  tft->fillRect(0, 50, SCREEN_WIDTH, 230, TFT_BLACK);
  tft->setTextDatum(MC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextColor(TFT_RED, TFT_BLACK);
  tft->drawString("ERROR: ACCESS HISTORY", SCREEN_WIDTH / 2, 120);
  tft->drawString("VIA LAP TIMER OR", SCREEN_WIDTH / 2, 145);
  tft->drawString("DRAG METER", SCREEN_WIDTH / 2, 170);

  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}

void HistoryScreen::drawGroups(int scrollOffset) {
  TFT_eSPI *tft = _ui->getTft();

  // Clear Content Area (Below Header)
  // Clear Content Area (Below Header) - Expanded to prevent overlap
  // Clear Content Area (Below Header) - Adjusted to preserve Title
  // Title is at STATUS_BAR_HEIGHT + 3 (~33). Font height ~20-25.
  // Start clear at 60 to be safe.
  tft->fillRect(0, 60, SCREEN_WIDTH, SCREEN_HEIGHT - 60 - 40, TFT_BLACK);

  // Sub-Header "- SESSIONS -"
  tft->setTextColor(TFT_SILVER, TFT_BLACK);
  tft->setTextDatum(TC_DATUM);
  tft->setFreeFont(NULL);
  tft->setTextSize(1);
  tft->drawString("- SELECT MONTH -", SCREEN_WIDTH / 2, 65);

  int startY = 80;
  int itemH = 26;
  int count = 0;
  int skip = 0;

  for (int i = 0; i < _groups.size(); i++) {
    if (count >= 5) // Show 5 groups
      break;
    if (skip < scrollOffset) {
      skip++;
      continue;
    }

    int y = startY + (count * itemH);
    bool selected = (i == _selectedIdx);

    uint16_t bg = selected ? 0x18E3 : TFT_BLACK;
    uint16_t fg = selected ? TFT_GOLD : TFT_WHITE;

    if (selected)
      tft->fillRect(0, y, SCREEN_WIDTH, itemH, bg);

    tft->setTextColor(fg, bg);
    tft->setTextDatum(TL_DATUM);
    tft->setTextFont(2);

    // Convert YYYY-MM to Month Name
    String year = _groups[i].substring(0, 4);
    String month = _groups[i].substring(5, 7);
    const char *months[] = {"January",   "February", "March",    "April",
                            "May",       "June",     "July",     "August",
                            "September", "October",  "November", "December"};
    int mIdx = month.toInt() - 1;
    String disp = (mIdx >= 0 && mIdx < 12) ? String(months[mIdx]) + " " + year
                                           : _groups[i];

    tft->drawString(disp, 20, y + 8);
    // Arrow icon
    tft->drawString(">", SCREEN_WIDTH - 30, y + 8);

    count++;
  }

  if (_groups.empty()) {
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("No Sessions Found", SCREEN_WIDTH / 2, 120);
  }

  // Draw Up/Down Buttons (Bottom-Right, matching SettingsScreen)
  int scrollX = SCREEN_WIDTH - 120;
  int maxScroll = (_groups.size() > 5) ? _groups.size() - 5 : 0;

  if (scrollOffset > 0) {
    // Up Arrow
    tft->fillTriangle(scrollX, 300, scrollX + 24, 300, scrollX + 12, 285,
                      COLOR_ACCENT);
  }

  if (scrollOffset < maxScroll) {
    // Down Arrow
    tft->fillTriangle(scrollX + 60, 285, scrollX + 84, 285, scrollX + 72, 300,
                      COLOR_ACCENT);
  }

  // Back Triangle
  tft->fillTriangle(10, 290, 25, 282, 25, 298, TFT_BLUE);

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}

void HistoryScreen::drawList(int scrollOffset) {
  TFT_eSPI *tft = _ui->getTft();

  // Calculate total in this group
  int totalInGroup = 0;
  for (const auto &h : _historyList) {
    if (h.type == _selectedType && h.date.length() >= 10) {
      String g = h.date.substring(6, 10) + "-" + h.date.substring(3, 5);
      if (g == _selectedGroup)
        totalInGroup++;
    }
  }

  // Clear Content Area
  tft->fillRect(0, 60, SCREEN_WIDTH, SCREEN_HEIGHT - 60 - 40, TFT_BLACK);

  // Column Headers
  tft->setTextColor(TFT_SILVER, TFT_BLACK);
  tft->setTextFont(1);
  tft->setTextDatum(TL_DATUM);
  tft->drawString("ID", 5, 65);
  tft->drawString("DATE/TIME", 45, 65);
  tft->drawString(_selectedType == "DRAG" ? "RUN" : "TIME", 175, 65);

  int startY = 80;
  int itemH = 26;
  int count = 0;
  int skip = 0;
  int currentGroupIdx = 0;

  for (int i = 0; i < _historyList.size(); i++) {
    if (_historyList[i].type != _selectedType)
      continue;

    // Filter by Group
    String g = "";
    if (tft && _historyList[i].date.length() >= 10) {
      g = _historyList[i].date.substring(6, 10) + "-" +
          _historyList[i].date.substring(3, 5);
    }
    if (g != _selectedGroup)
      continue;

    int idVal = totalInGroup - currentGroupIdx;
    currentGroupIdx++;

    if (skip < scrollOffset) {
      skip++;
      continue;
    }
    if (count >= 7)
      break;

    int y = startY + (count * itemH);

    // Row Background (Slight alternate or highlight)
    if (count % 2 != 0)
      tft->fillRect(0, y, SCREEN_WIDTH - 40, itemH,
                    0x0841); // Very dark gray, leave space for buttons

    tft->setTextColor(TFT_WHITE, count % 2 != 0 ? 0x0841 : TFT_BLACK);
    tft->setTextDatum(TL_DATUM);

    // ID
    char bufID[16];
    sprintf(bufID, "%03d", idVal);
    tft->drawString(bufID, 5, y + 5, 2);

    // Parse Date and Time
    String dRaw = _historyList[i].date.substring(0, 5); // DD/MM
    String tRaw = (_historyList[i].date.length() > 11)
                      ? _historyList[i].date.substring(11, 16)
                      : "";
    String dtCombined = dRaw + " " + tRaw;
    tft->drawString(dtCombined, 45, y + 5, 2);

    if (_selectedType == "DRAG") {
      float res = _historyList[i].bestLap / 1000.0;
      String resStr = String(res, 2) + "s";
      tft->setTextColor(TFT_GOLD, count % 2 != 0 ? 0x0841 : TFT_BLACK);
      tft->drawString(resStr, 175, y + 5, 2);
    } else {
      tft->drawString(
          tRaw, 175, y + 5,
          2); // Already in dtCombined, but placeholder for lap time if we split
      // Actually, for Track, the 'TIME' column should probably show the best
      // lap time too
      float lapSeconds = _historyList[i].bestLap / 1000.0;
      tft->setTextColor(TFT_GOLD, count % 2 != 0 ? 0x0841 : TFT_BLACK);
      tft->drawString(String(lapSeconds, 2) + "s", 175, y + 5, 2);

      // Lap Count (Subtle)
      tft->setTextColor(TFT_SILVER, count % 2 != 0 ? 0x0841 : TFT_BLACK);
      tft->drawString("(" + String(_historyList[i].laps) + "L)", 245, y + 5, 2);
    }

    count++;
  }

  // Draw Up/Down Buttons (Bottom-Right, matching SettingsScreen)
  int scrollX = SCREEN_WIDTH - 120;
  int maxScroll = (totalInGroup > 7) ? totalInGroup - 7 : 0;

  if (scrollOffset > 0) {
    // Up Arrow
    tft->fillTriangle(scrollX, 300, scrollX + 24, 300, scrollX + 12, 285,
                      COLOR_ACCENT);
  }

  if (scrollOffset < maxScroll) {
    // Down Arrow
    tft->fillTriangle(scrollX + 60, 285, scrollX + 84, 285, scrollX + 72, 300,
                      COLOR_ACCENT);
  }

  // Back Triangle
  tft->fillTriangle(10, 290, 25, 282, 25, 298, TFT_BLUE);

  // --- FONT SAFETY ---
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}

void HistoryScreen::scanGroups() {
  _groups.clear();
  for (const auto &item : _historyList) {
    // Filter by type first
    if (item.type != _selectedType)
      continue;

    // Date format: "DD/MM/YYYY" -> "YYYY-MM"
    if (item.date.length() >= 10) {
      String yyyy = item.date.substring(6, 10);
      String mm = item.date.substring(3, 5);
      String group = yyyy + "-" + mm;

      bool exists = false;
      for (const auto &g : _groups) {
        if (g == group) {
          exists = true;
          break;
        }
      }
      if (!exists)
        _groups.push_back(group);
    }
  }
}

void HistoryScreen::drawOptions() {
  TFT_eSPI *tft = _ui->getTft();
  // _ui->drawStatusBar();

  // Header
  // tft->drawFastHLine(0, 20, SCREEN_WIDTH, TFT_WHITE); // Redundant
  // Back Button (Blue Triangle)
  tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                    SCREEN_HEIGHT - 20, TFT_BLUE);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextDatum(TC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->drawString("SESSION OPTIONS", SCREEN_WIDTH / 2,
                  STATUS_BAR_HEIGHT + 10); // Spaced from SB
  tft->drawFastHLine(0, STATUS_BAR_HEIGHT + 30, SCREEN_WIDTH,
                     TFT_WHITE); // Separator

  // Options
  const char *options[] = {"1. View Data", "2. Synchronize",
                           "3. Delete Session"};
  int startY = STATUS_BAR_HEIGHT + 40; // Dynamic start
  int h = 40;

  for (int i = 0; i < 3; i++) {
    int y = startY + (i * 50);
    bool sel = (i == _selectedIdx);

    if (sel) {
      tft->fillRoundRect(20, y, SCREEN_WIDTH - 40, h, 5, TFT_WHITE);
      tft->setTextColor(TFT_BLACK, TFT_WHITE);
    } else {
      tft->fillRoundRect(20, y, SCREEN_WIDTH - 40, h, 5, TFT_DARKGREY);
      tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
    }

    tft->setTextDatum(MC_DATUM);
    tft->setTextFont(2);
    tft->drawString(options[i], SCREEN_WIDTH / 2, y + h / 2);
  }

  // --- SYNC STATUS FEEDBACK ---
  if (_isSyncing || _syncStatus.length() > 0) {
    int statusY = startY + 160;
    tft->fillRect(0, statusY - 10, SCREEN_WIDTH, 60, TFT_BLACK);
    tft->setTextColor(
        _isSyncing
            ? TFT_ORANGE
            : (_syncStatus.indexOf("COMPLETE") != -1 ? TFT_GREEN : TFT_RED));
    tft->setTextDatum(TC_DATUM);
    tft->setFreeFont(NULL);
    tft->setTextSize(1);
    tft->drawString(_syncStatus, SCREEN_WIDTH / 2, statusY);
    tft->setTextColor(TFT_SILVER);
    tft->drawString(_syncDetail, SCREEN_WIDTH / 2, statusY + 15);
  }
}

void HistoryScreen::syncSession() {
  if (_isSyncing)
    return;

  _isSyncing = true;
  _syncStatus = "SYNCING SESSION...";
  _syncDetail = "Connecting to WiFi...";
  drawOptions();

  // 1. Check WiFi
  if (WiFi.status() != WL_CONNECTED) {
    if (!wifiManager.tryAutoConnect()) {
      _syncStatus = "SYNC FAILED";
      _syncDetail = "WiFi Disconnected";
      _isSyncing = false;
      drawOptions();
      return;
    }
  }

  // 2. Get Credentials
  Preferences prefs;
  prefs.begin("muchrace", true);
  String username = prefs.getString("username", "");
  String password = prefs.getString("password", "");
  prefs.end();

  if (username.length() == 0 || password.length() == 0) {
    _syncStatus = "SYNC FAILED";
    _syncDetail = "Setup > Account Refquired";
    _isSyncing = false;
    drawOptions();
    return;
  }

  // 3. Perform Sync
  _syncStatus = "UPLOADING DATA...";
  _syncDetail = "Sending to Server...";
  drawOptions();

  bool success = syncManager.uploadSingleSession(
      API_URL, username.c_str(), password.c_str(),
      _historyList[_lastTapIdx].filename);

  if (success) {
    _syncStatus = "SYNC COMPLETE";
    _syncDetail = "Session Uploaded Successfully";
  } else {
    _syncStatus = "SYNC FAILED";
    _syncDetail = "Check Conn / Large File";
  }

  _isSyncing = false;
  drawOptions();
}

void HistoryScreen::drawViewData() {
  TFT_eSPI *tft = _ui->getTft();
  // Clear only content area
  tft->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                SCREEN_HEIGHT - STATUS_BAR_HEIGHT, TFT_BLACK);
  // _ui->drawStatusBar(); // Removed to prevent flicker

  // Page Header
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextDatum(TC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);

  String title = "";
  // Check Type
  bool isDrag = (_historyList[_lastTapIdx].type == "DRAG");

  if (isDrag) {
    title = "DRAG SUMMARY"; // Only 1 page for now?
  } else {
    switch (_viewPage) {
    case 0:
      title = "SESSION SUMMARY";
      break;
    case 1:
      title = "LAP LIST";
      break;
    case 2:
      title = "SECTOR ANALYSIS";
      break;
    case 3:
      title = "LAP REPLAY";
      break;
    case 4:
      title = "RPM & TEMP";
      break;
    }
  }
  tft->drawString(title, SCREEN_WIDTH / 2,
                  40); // Increased spacing from 25 to 40

  String currentFile = _historyList[_lastTapIdx].filename;
  if (currentFile != _currentAnalysis.filename) {
    // Show Loading...
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("Loading...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    _currentAnalysis = sessionManager.analyzeSession(currentFile);
    _currentAnalysis.filename = currentFile; // Store for comparison
    // Clear Loading
    tft->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                  SCREEN_HEIGHT - STATUS_BAR_HEIGHT, TFT_BLACK);
  }

  int startY = 50;

  if (isDrag) {
    // --- REDESIGNED DRAG SUMMARY DASHBOARD ---
    int mainBoxY = startY;
    int mainBoxH = 90;
    int subBoxY = mainBoxY + mainBoxH + 12;
    int subBoxH = 65;
    int padding = 12;

    // 1. PRIMARY RESULT: 402m (1/4 Mile) - "The Hero Metric"
    tft->fillRoundRect(10, mainBoxY, SCREEN_WIDTH - 20, mainBoxH, 8, 0x18E3);
    tft->drawRoundRect(10, mainBoxY, SCREEN_WIDTH - 20, mainBoxH, 8,
                       COLOR_ACCENT);

    tft->setTextColor(TFT_SILVER, 0x18E3);
    tft->setFreeFont(&Org_01);
    tft->setTextDatum(TL_DATUM);
    tft->drawString("402m (1/4 MILE)", 20, mainBoxY + 10);

    tft->setTextColor(TFT_YELLOW, 0x18E3);
    tft->setTextDatum(MC_DATUM);
    tft->setTextFont(6); // Large font for visibility
    tft->drawString(_currentAnalysis.time400m > 0
                        ? String(_currentAnalysis.time400m / 1000.0, 2) + "s"
                        : "--",
                    SCREEN_WIDTH / 2, mainBoxY + mainBoxH / 2 + 10);

    // 2. SPEED SPLITS GRID
    int boxW = (SCREEN_WIDTH - 34) / 3;
    uint16_t boxCol = 0x2104; // Dark grey background for sub-items

    // 0-60 KPH
    tft->fillRoundRect(10, subBoxY, boxW, subBoxH, 6, boxCol);
    tft->setTextColor(TFT_SILVER, boxCol);
    tft->setTextDatum(TC_DATUM);
    tft->setFreeFont(NULL);
    tft->setTextFont(1);
    tft->drawString("0-60 KPH", 10 + boxW / 2, subBoxY + 8);
    tft->setTextColor(TFT_WHITE, boxCol);
    tft->setTextFont(4);
    tft->drawString(_currentAnalysis.time0to60 > 0
                        ? String(_currentAnalysis.time0to60 / 1000.0, 2) + "s"
                        : "--",
                    10 + boxW / 2, subBoxY + 28);

    // 0-100 KPH
    tft->fillRoundRect(10 + (boxW + 7), subBoxY, boxW, subBoxH, 6, boxCol);
    tft->setTextColor(TFT_SILVER, boxCol);
    tft->setTextDatum(TC_DATUM);
    tft->setTextFont(1);
    tft->drawString("0-100 KPH", 10 + (boxW + 7) + boxW / 2, subBoxY + 8);
    tft->setTextColor(TFT_ORANGE, boxCol);
    tft->setTextFont(4);
    tft->drawString(_currentAnalysis.time0to100 > 0
                        ? String(_currentAnalysis.time0to100 / 1000.0, 2) + "s"
                        : "--",
                    10 + (boxW + 7) + boxW / 2, subBoxY + 28);

    // 100-200 KPH
    tft->fillRoundRect(10 + 2 * (boxW + 7), subBoxY, boxW, subBoxH, 6, boxCol);
    tft->setTextColor(TFT_SILVER, boxCol);
    tft->setTextDatum(TC_DATUM);
    tft->setTextFont(1);
    tft->drawString("100-200 KPH", 10 + 2 * (boxW + 7) + boxW / 2, subBoxY + 8);
    tft->setTextColor(TFT_CYAN, boxCol);
    tft->setTextFont(4);
    tft->drawString(_currentAnalysis.time100to200 > 0
                        ? String(_currentAnalysis.time100to200 / 1000.0, 2) +
                              "s"
                        : "--",
                    10 + 2 * (boxW + 7) + boxW / 2, subBoxY + 28);

    // 3. AUXILIARY STATS FOOTER
    int footerY = subBoxY + subBoxH + 15;
    tft->drawFastHLine(10, footerY, SCREEN_WIDTH - 20,
                       0x3186); // Subtle separator

    tft->setFreeFont(&Org_01);
    tft->setTextSize(1);
    tft->setTextColor(TFT_SILVER, TFT_BLACK);
    tft->setTextDatum(TL_DATUM);

    char footBuf[64];
    sprintf(footBuf, "TOP SP: %.1f km/h", _currentAnalysis.maxSpeed);
    tft->drawString(footBuf, 20, footerY + 12);

    sprintf(footBuf, "DIST: %.2f km", _currentAnalysis.totalDistance);
    tft->setTextDatum(TC_DATUM);
    tft->drawString(footBuf, SCREEN_WIDTH / 2, footerY + 12);

    sprintf(footBuf, "MAX RPM: %d", _currentAnalysis.maxRPM);
    tft->setTextDatum(TR_DATUM);
    tft->drawString(footBuf, SCREEN_WIDTH - 20, footerY + 12);

    // Back Button (Blue Triangle)
    tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                      SCREEN_HEIGHT - 20, TFT_BLUE);

    return;
  }

  if (_viewPage == 0) {
    // SUMMARY PAGE
    // 4 Grid Box - Optimized for 480x320
    int boxW = (SCREEN_WIDTH - 30) / 2;
    int boxH = 70;
    int gap = 10;

    // Box 1: Total Time
    tft->fillRoundRect(10, startY, boxW, boxH, 5, 0x18E3);
    tft->setTextColor(TFT_SILVER, 0x18E3);
    tft->setTextDatum(TL_DATUM);
    tft->drawString("TOTAL TIME", 15, startY + 5);
    // Fmt
    unsigned long tt = _currentAnalysis.totalTime;
    int ms = tt % 1000;
    int s = (tt / 1000) % 60;
    int m = (tt / 60000) % 60;
    int h = (tt / 3600000);
    char buf[16];
    sprintf(buf, "%02d:%02d:%02d", h, m, s);
    tft->setTextColor(TFT_WHITE, 0x18E3);
    tft->setTextDatum(MC_DATUM);
    sprintf(buf, "%02d:%02d:%02d", h, m, s);
    tft->setTextColor(TFT_WHITE, 0x18E3);
    tft->setTextDatum(MC_DATUM);
    tft->setTextFont(4);
    tft->drawString(buf, 10 + boxW / 2, startY + 40);

    // Box 2: Valid Laps (Count)
    tft->fillRoundRect(15 + boxW, startY, boxW, boxH, 5, 0x18E3);
    tft->setTextColor(TFT_SILVER, 0x18E3);
    tft->setTextDatum(TL_DATUM);
    tft->setFreeFont(&Org_01); // Reset Font
    tft->setTextSize(1);
    tft->drawString("VALID LAPS", 20 + boxW, startY + 5);
    tft->setTextColor(TFT_SKYBLUE, 0x18E3);
    tft->setTextDatum(MC_DATUM);
    tft->setTextFont(4);
    tft->drawString(String(_currentAnalysis.validLaps), 15 + boxW + boxW / 2,
                    startY + 40);

    // Row 2
    int Y2 = startY + boxH + gap;

    // Box 3: Distance
    tft->fillRoundRect(10, Y2, boxW, boxH, 8, 0x18E3);
    tft->setTextColor(TFT_SILVER, 0x18E3);
    tft->setTextDatum(TL_DATUM);
    tft->setFreeFont(&Org_01); // Reset Font
    tft->setTextSize(1);
    tft->drawString("DISTANCE (km)", 15, Y2 + 5);
    tft->setTextColor(TFT_ORANGE, 0x18E3);
    tft->setTextDatum(MC_DATUM);
    tft->setTextFont(4);
    tft->drawFloat(_currentAnalysis.totalDistance, 2, 10 + boxW / 2, Y2 + 40);

    // Box 4: Max Speed
    tft->fillRoundRect(15 + boxW, Y2, boxW, boxH, 8, 0x18E3);
    tft->setTextColor(TFT_SILVER, 0x18E3);
    tft->setTextDatum(TL_DATUM);
    tft->setFreeFont(&Org_01); // Reset Font
    tft->setTextSize(1);
    tft->drawString("MAX SPEED", 20 + boxW, Y2 + 5);
    tft->setTextColor(TFT_RED, 0x18E3);
    tft->setTextDatum(MC_DATUM);
    tft->setTextFont(4);
    tft->drawFloat(_currentAnalysis.maxSpeed, 1, 15 + boxW + boxW / 2, Y2 + 40);

    // Best Lap Highlight
    int Y3 = Y2 + boxH + 10;
    tft->drawRect(10, Y3, SCREEN_WIDTH - 20, 60, TFT_DARKGREY);
    tft->setTextColor(TFT_GOLD, TFT_BLACK);
    tft->setTextDatum(MC_DATUM);
    if (_currentAnalysis.bestLap > 0) {
      unsigned long b = _currentAnalysis.bestLap;
      int bs = (b / 1000) % 60;
      int bm = (b / 60000);
      int bms = b % 1000;
      sprintf(buf, "BEST: %d:%02d.%02d", bm, bs, bms / 10);
      tft->setTextFont(4);
      tft->drawString(buf, SCREEN_WIDTH / 2, Y3 + 20); // Moved up slightly

      // Add Max RPM below Best Lap
      sprintf(buf, "MAX RPM: %d", _currentAnalysis.maxRPM);
      tft->setTextFont(2);
      tft->setTextColor(TFT_SILVER, TFT_BLACK);
      tft->drawString(buf, SCREEN_WIDTH / 2, Y3 + 45);
    } else {
      tft->drawString("NO LAP DATA", SCREEN_WIDTH / 2, Y3 + 25);
    }

  } else if (_viewPage == 1) {
    // LAP LIST
    tft->setTextDatum(TL_DATUM);
    tft->setTextColor(TFT_SILVER, TFT_BLACK);
    tft->drawString("LAP TIMES:", 20, startY);

    if (_currentAnalysis.lapTimes.empty()) {
      tft->setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft->drawString("No Laps Recorded", 20, startY + 30);
    } else {
      int y = startY + 25;
      int count = 0;
      for (unsigned long t : _currentAnalysis.lapTimes) {
        if (count > 9)
          break; // Show max 10
        int ms = t % 1000;
        int s = (t / 1000) % 60;
        int m = (t / 60000);
        char buf[32];
        sprintf(buf, "%d.  %d:%02d.%02d", count + 1, m, s, ms / 10);

        if (t == _currentAnalysis.bestLap)
          tft->setTextColor(TFT_GREEN, TFT_BLACK);
        else
          tft->setTextColor(TFT_WHITE, TFT_BLACK);

        tft->drawString(buf, 30, y, 2);
        y += 22;
        count++;
      }
    }
  } else if (_viewPage == 2) {
    // SECTOR ANALYSIS
    tft->setTextDatum(TL_DATUM);
    tft->setTextColor(TFT_SILVER, TFT_BLACK);
    tft->drawString("LAP    S1      S2      S3     TOTAL", 20, startY, 2);
    tft->drawFastHLine(10, startY + 18, SCREEN_WIDTH - 20, TFT_DARKGREY);

    unsigned long bestS1 = 0, bestS2 = 0, bestS3 = 0;
    for (unsigned long s : _currentAnalysis.sector1)
      if (bestS1 == 0 || s < bestS1)
        bestS1 = s;
    for (unsigned long s : _currentAnalysis.sector2)
      if (bestS2 == 0 || s < bestS2)
        bestS2 = s;
    for (unsigned long s : _currentAnalysis.sector3)
      if (bestS3 == 0 || s < bestS3)
        bestS3 = s;

    int y = startY + 25;
    for (int i = 0; i < (int)_currentAnalysis.lapTimes.size() && i < 8; i++) {
      char buf[64];
      unsigned long s1 = (i < _currentAnalysis.sector1.size())
                             ? _currentAnalysis.sector1[i]
                             : 0;
      unsigned long s2 = (i < _currentAnalysis.sector2.size())
                             ? _currentAnalysis.sector2[i]
                             : 0;
      unsigned long s3 = (i < _currentAnalysis.sector3.size())
                             ? _currentAnalysis.sector3[i]
                             : 0;
      unsigned long tot = _currentAnalysis.lapTimes[i];

      // Columnar display - Optimized for 480px width
      sprintf(buf, "%d", i + 1);
      tft->drawString(buf, 20, y, 2);

      sprintf(buf, "%.2fs", s1 / 1000.0);
      tft->setTextColor((s1 == bestS1 && s1 > 0) ? TFT_GREEN : TFT_WHITE,
                        TFT_BLACK);
      tft->drawString(buf, 80, y, 2);

      sprintf(buf, "%.2fs", s2 / 1000.0);
      tft->setTextColor((s2 == bestS2 && s2 > 0) ? TFT_GREEN : TFT_WHITE,
                        TFT_BLACK);
      tft->drawString(buf, 170, y, 2);

      sprintf(buf, "%.2fs", s3 / 1000.0);
      tft->setTextColor((s3 == bestS3 && s3 > 0) ? TFT_GREEN : TFT_WHITE,
                        TFT_BLACK);
      tft->drawString(buf, 260, y, 2);

      sprintf(buf, "%d:%02d.%02d", (int)(tot / 60000), (int)((tot / 1000) % 60),
              (int)((tot % 1000) / 10));
      tft->setTextColor(
          (tot == _currentAnalysis.bestLap) ? TFT_GOLD : TFT_WHITE, TFT_BLACK);
      tft->drawString(buf, 350, y, 2);

      y += 24; // Reduce spacing to avoid overlap
    }

    // Theo Best
    if (bestS1 > 0 && bestS2 > 0 && bestS3 > 0) {
      unsigned long theo = bestS1 + bestS2 + bestS3;
      char buf[32];
      sprintf(buf, "THEO BEST: %d:%02d.%d", (int)(theo / 60000),
              (int)((theo / 1000) % 60), (int)((theo % 1000) / 100));
      tft->setTextColor(TFT_GOLD, TFT_BLACK);
      tft->setTextDatum(BC_DATUM);
      tft->drawString(buf, SCREEN_WIDTH / 2, 285, 2);
    }
  } else if (_viewPage == 3) {
    // MAP VIEW
    drawLapReplay(_currentAnalysis);
  } else if (_viewPage == 4) {
    // RPM & LEAN ANALYSIS CHART
    if (_selectedLapForReplay == -1 || _replayPoints.empty()) {
      int bestLapIdx = 0;
      for (size_t i = 0; i < _currentAnalysis.lapTimes.size(); i++) {
        if (_currentAnalysis.lapTimes[i] == _currentAnalysis.bestLap) {
          bestLapIdx = i;
          break;
        }
      }
      _replayPoints = sessionManager.getLapPoints(
          _historyList[_lastTapIdx].filename, bestLapIdx);
      _selectedLapForReplay = bestLapIdx;
    }

    if (!_replayPoints.empty()) {
      int chartX = 20, chartY = 75, chartW = 440, chartH = 180;
      tft->drawRect(chartX - 1, chartY - 1, chartW + 2, chartH + 2,
                    TFT_DARKGREY);

      int maxRpm = 0;
      float maxLean = 0;
      for (const auto &p : _replayPoints) {
        if (p.rpm > maxRpm)
          maxRpm = p.rpm;
        if (abs(p.lean) > maxLean)
          maxLean = abs(p.lean);
      }
      if (maxRpm < 1000)
        maxRpm = 8000;
      if (maxLean < 10)
        maxLean = 45;

      // Draw Grid
      tft->drawFastHLine(chartX, chartY + chartH / 2, chartW, 0x3186); // Center

      // Data Path
      for (size_t i = 1; i < _replayPoints.size(); i++) {
        int x1 = chartX + (i - 1) * chartW / _replayPoints.size();
        int x2 = chartX + i * chartW / _replayPoints.size();

        // RPM (Cyan)
        int yRpm1 = chartY + chartH -
                    (int)((float)_replayPoints[i - 1].rpm / maxRpm * chartH);
        int yRpm2 = chartY + chartH -
                    (int)((float)_replayPoints[i].rpm / maxRpm * chartH);
        tft->drawLine(x1, yRpm1, x2, yRpm2, TFT_CYAN);

        // Lean (Orange)
        int yLean1 = chartY + chartH -
                     (int)(abs(_replayPoints[i - 1].lean) / maxLean * chartH);
        int yLean2 = chartY + chartH -
                     (int)(abs(_replayPoints[i].lean) / maxLean * chartH);
        tft->drawLine(x1, yLean1, x2, yLean2, TFT_ORANGE);
      }

      // Legend & Max Values
      tft->setFreeFont(&Org_01);
      tft->setTextSize(1);
      tft->setTextDatum(TL_DATUM);
      tft->setTextColor(TFT_CYAN, TFT_BLACK);
      tft->drawString("RPM (MAX: " + String(maxRpm) + ")", chartX + 10,
                      chartY + 10);
      tft->setTextColor(TFT_ORANGE, TFT_BLACK);
      tft->drawString("LEAN (MAX: " + String(maxLean, 1) + ")", chartX + 10,
                      chartY + 28);
    } else {
      tft->setTextDatum(MC_DATUM);
      tft->setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft->drawString("No Telemetry Data", SCREEN_WIDTH / 2, 130, 2);
    }
  }

  // Back Triangle (Standardized Bottom-Left)
  tft->fillTriangle(10, 290, 25, 282, 25, 298, COLOR_ACCENT);
}

void HistoryScreen::drawConfirmDelete() {
  TFT_eSPI *tft = _ui->getTft();
  // Clear only content area - MOVED TO CALLER to prevent flicker on update
  // tft->fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH,
  //               SCREEN_HEIGHT - STATUS_BAR_HEIGHT, TFT_BLACK);
  // _ui->drawStatusBar(); // Removed to prevent flicker

  tft->setTextColor(TFT_RED, TFT_BLACK);
  tft->setTextDatum(MC_DATUM);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(2); // Large warning
  tft->drawString("DELETE?", SCREEN_WIDTH / 2, 80);

  tft->setTextSize(1);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->drawString("Confirm Permanent Delete", SCREEN_WIDTH / 2, 120);

  // Yes / No options
  int btnW = 100;
  int btnH = 40;
  int gap = 20;
  int startX = (SCREEN_WIDTH - (btnW * 2 + gap)) / 2;
  int y = 160;

  // YES
  bool selYes = (_selectedIdx == 0);
  tft->fillRoundRect(startX, y, btnW, btnH, 5, selYes ? TFT_RED : TFT_DARKGREY);
  tft->setTextColor(TFT_WHITE, selYes ? TFT_RED : TFT_DARKGREY);
  tft->drawString("YES", startX + btnW / 2, y + btnH / 2);

  // NO
  bool selNo = (_selectedIdx == 1);
  tft->fillRoundRect(startX + btnW + gap, y, btnW, btnH, 5,
                     selNo ? TFT_GREEN : TFT_DARKGREY);
  tft->setTextColor(TFT_BLACK, selNo ? TFT_GREEN : TFT_DARKGREY);
  tft->drawString("NO", startX + btnW + gap + btnW / 2, y + btnH / 2);
}

void HistoryScreen::drawLapReplay(
    const SessionManager::SessionAnalysis &analysis) {
  TFT_eSPI *tft = _ui->getTft();
  String currentFile = _historyList[_lastTapIdx].filename;

  // --- Pro Replay Design Layout (Shifted for Status Bar) ---
  int headerY = STATUS_BAR_HEIGHT; // Start at 25
  int headerH = 35;
  int dataY = headerY + headerH + 5; // ~65
  int mapY = dataY + 55, mapH = 140; // ~120, H=140
  int footerY = 280;                 // Fixed bottom area

  int mapX = 10, mapW = 460;

  auto toX = [&](double lon) {
    if (_maxLon == _minLon)
      return mapX + mapW / 2;
    double margin = 0.20; // 20% margin for absolute marker safety
    return mapX + (int)((margin / 2 + (1.0 - margin) * (lon - _minLon) /
                                          (_maxLon - _minLon)) *
                        mapW);
  };
  auto toY = [&](double lat) {
    if (_maxLat == _minLat)
      return mapY + mapH / 2;
    double margin = 0.20;
    return mapY + (int)((1.0 - (margin / 2 + (1.0 - margin) * (lat - _minLat) /
                                                 (_maxLat - _minLat))) *
                        mapH);
  };

  // 1. Initial Load of Points & Full Static Redraw
  if (_selectedLapForReplay == -1) {
    int bestLapIdx = 0;
    for (size_t i = 0; i < analysis.lapTimes.size(); i++) {
      if (analysis.lapTimes[i] == analysis.bestLap) {
        bestLapIdx = i;
        break;
      }
    }

    _replayPoints = sessionManager.getLapPoints(currentFile, bestLapIdx);
    _selectedLapForReplay = bestLapIdx;
    _replayIdx = 0;
    _lastReplayIdx = -1;
    _lastDotX = -1;
    _lastDotY = -1;

    if (!_replayPoints.empty()) {
      _minLat = _maxLat = _replayPoints[0].lat;
      _minLon = _maxLon = _replayPoints[0].lon;
      for (const auto &p : _replayPoints) {
        if (p.lat < _minLat)
          _minLat = p.lat;
        if (p.lat > _maxLat)
          _maxLat = p.lat;
        if (p.lon < _minLon)
          _minLon = p.lon;
        if (p.lon > _maxLon)
          _maxLon = p.lon;
      }
    }

    // DRAW STATIC BASE ONCE
    tft->fillRect(0, 70, SCREEN_WIDTH, SCREEN_HEIGHT - 100, TFT_BLACK);

    if (_replayPoints.empty()) {
      tft->setTextDatum(MC_DATUM);
      tft->drawString("No GPS Points for Replay", SCREEN_WIDTH / 2, 160);
      return;
    }

    // Draw Map Outline & Track
    tft->drawRect(mapX - 5, mapY - 5, mapW + 10, mapH + 10, TFT_DARKGREY);
    for (size_t i = 1; i < _replayPoints.size(); i++) {
      int x1 = toX(_replayPoints[i - 1].lon);
      int y1 = toY(_replayPoints[i - 1].lat);
      int x2 = toX(_replayPoints[i].lon);
      int y2 = toY(_replayPoints[i].lat);
      // Robust 5-point bold line (cross offset)
      tft->drawLine(x1, y1, x2, y2, TFT_SILVER);
      tft->drawLine(x1 + 1, y1, x2 + 1, y2, TFT_SILVER);
      tft->drawLine(x1 - 1, y1, x2 - 1, y2, TFT_SILVER);
      tft->drawLine(x1, y1 + 1, x2, y2 + 1, TFT_SILVER);
      tft->drawLine(x1, y1 - 1, x2, y2 - 1, TFT_SILVER);
    }

    // Draw Box 1: Lap Info (Left)
    // No box container for minimalist look

    // 1. TOP HEADER BAR (Shifted)
    tft->fillRect(0, headerY, 480, headerH, 0x2104); // Dark Grey Header
    tft->drawFastHLine(0, (headerY + headerH) - 1, 480, COLOR_ACCENT);

    tft->setTextColor(TFT_WHITE, 0x2104);
    tft->setTextDatum(MC_DATUM); // CENTERED TITLE
    tft->drawString("SESSION REPLAY", 240, headerY + headerH / 2, 2);

    char lapBuf[16];
    sprintf(lapBuf, "LAP %d", _selectedLapForReplay + 1);
    tft->setTextDatum(ML_DATUM);
    tft->drawString(lapBuf, 15, headerY + headerH / 2, 2);

    // 2. DATA STRIP (Time & Speed Area)
    tft->setTextDatum(TL_DATUM);
    tft->setTextColor(TFT_CYAN, TFT_BLACK);
    char timeBuf[16];
    unsigned long lapTime = analysis.lapTimes[_selectedLapForReplay];
    sprintf(timeBuf, "%d:%02d.%02d", (int)(lapTime / 60000),
            (int)((lapTime / 1000) % 60), (int)((lapTime % 1000) / 10));
    tft->drawString(timeBuf, 10, dataY, 4);

    if (analysis.lapTimes[_selectedLapForReplay] == analysis.bestLap) {
      tft->setTextColor(TFT_GOLD, TFT_BLACK);
      tft->drawString("BEST", 10, dataY + 28, 2);
    }

    // Huge Speed Display on Right
    tft->setTextDatum(TR_DATUM);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->drawString("KPH", 470, dataY + 25, 2);
    tft->setTextColor(COLOR_ACCENT, TFT_BLACK);

    // 3. TELEMETRY FOOTER BAR
    tft->drawFastHLine(10, footerY - 5, 460, 0x3186); // Subtle separator
    tft->setTextDatum(TC_DATUM);
    tft->setTextColor(TFT_SILVER, TFT_BLACK);
    tft->drawString("RPM", 80, footerY, 1);
    tft->drawString("G-FORCE", 240, footerY, 1);
    tft->drawString("LEAN", 400, footerY, 1);
  }

  // 1. ADVANCE STATE
  _lastReplayIdx = _replayIdx;
  _replayIdx++;
  if (_replayIdx >= (int)_replayPoints.size()) {
    _replayIdx = 0;
  }

  // 2. PRE-CALCULATE NEW POSITION (to draw immediately after clearing)
  int newX = toX(_replayPoints[_replayIdx].lon);
  int newY = toY(_replayPoints[_replayIdx].lat);
  int s = 12; // Marker Size

  // 3. CLEAR & REPAIR PREVIOUS POSITION
  if (_lastDotX != -1) {
    tft->fillCircle(_lastDotX, _lastDotY, 13, TFT_BLACK);

    // Selective UI Restoration
    bool nearMapEdge = (_lastDotX < mapX + 20 || _lastDotX > mapX + mapW - 20 ||
                        _lastDotY < mapY + 20 || _lastDotY > mapY + mapH - 20);
    bool nearHeader = (_lastDotY < (headerY + headerH) + 15);
    bool nearFooter = (_lastDotY > footerY - 20);

    if (nearMapEdge)
      tft->drawRect(mapX - 5, mapY - 5, mapW + 10, mapH + 10, TFT_DARKGREY);
    if (nearHeader)
      tft->drawFastHLine(0, (headerY + headerH) - 1, 480, COLOR_ACCENT);
    if (nearFooter)
      tft->drawFastHLine(10, footerY - 5, 460, 0x3186);

    // Repair window
    int win = 15;
    int start = (_lastReplayIdx > win) ? _lastReplayIdx - win : 0;
    int end = (_lastReplayIdx + win < (int)_replayPoints.size() - 1)
                  ? _lastReplayIdx + win
                  : (int)_replayPoints.size() - 1;

    for (int i = start; i < end; i++) {
      int rx1 = toX(_replayPoints[i].lon), ry1 = toY(_replayPoints[i].lat);
      int rx2 = toX(_replayPoints[i + 1].lon),
          ry2 = toY(_replayPoints[i + 1].lat);
      tft->drawLine(rx1, ry1, rx2, ry2, TFT_SILVER);
      tft->drawLine(rx1 + 1, ry1, rx2 + 1, ry2, TFT_SILVER);
      tft->drawLine(rx1 - 1, ry1, rx2 - 1, ry2, TFT_SILVER);
      tft->drawLine(rx1, ry1 + 1, rx2, ry2 + 1, TFT_SILVER);
      tft->drawLine(rx1, ry1 - 1, rx2, ry2 - 1, TFT_SILVER);
    }
  }

  // 4. DRAW NEW MARKER (Immediately after repair to prevent flicker)
  float angle;
  int lookAhead = 5;
  int targetIdx = (_replayIdx + lookAhead < (int)_replayPoints.size())
                      ? _replayIdx + lookAhead
                      : (int)_replayPoints.size() - 1;
  if (targetIdx > _replayIdx) {
    angle = atan2(toY(_replayPoints[targetIdx].lat) - newY,
                  toX(_replayPoints[targetIdx].lon) - newX);
  } else {
    int prevIdx = (_replayIdx > 5) ? _replayIdx - 5 : 0;
    angle = atan2(newY - toY(_replayPoints[prevIdx].lat),
                  newX - toX(_replayPoints[prevIdx].lon));
  }

  int mx1 = newX + (int)(s * cos(angle));
  int my1 = newY + (int)(s * sin(angle));
  int mx2 = newX + (int)(s * cos(angle + 2.356));
  int my2 = newY + (int)(s * sin(angle + 2.356));
  int mx3 = newX + (int)(s * cos(angle - 2.356));
  int my3 = newY + (int)(s * sin(angle - 2.356));

  tft->fillTriangle(mx1, my1, mx2, my2, mx3, my3, COLOR_ACCENT);
  tft->drawTriangle(mx1, my1, mx2, my2, mx3, my3, TFT_WHITE);

  // 5. UPDATE LAST POSITION & TELEMETRY
  _lastDotX = newX;
  _lastDotY = newY;

  const auto &p = _replayPoints[_replayIdx];
  char valBuf[32];

  // UPDATE METRICS IN DATA STRIP & FOOTER
  // Large Speed
  sprintf(valBuf, "%.1f", p.speed);
  tft->setTextDatum(TR_DATUM);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextPadding(tft->textWidth("888.8", 6)); // Padding for large Font 6
  tft->drawString(valBuf, 470, dataY, 6);

  // Footer Values setup
  tft->setTextDatum(TC_DATUM);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  int footerValPadding = tft->textWidth("888.8 deg", 2);

  // RPM
  tft->setTextPadding(footerValPadding);
  sprintf(valBuf, "%d", p.rpm);
  tft->drawString(valBuf, 80, footerY + 15, 2);

  // G-Force
  tft->setTextPadding(footerValPadding);
  sprintf(valBuf, "%.2f G", p.gX);
  tft->drawString(valBuf, 240, footerY + 15, 2);

  // Lean Angle
  tft->setTextPadding(footerValPadding);
  sprintf(valBuf, "%.1f deg", p.lean);
  tft->drawString(valBuf, 400, footerY + 15, 2);

  tft->setTextPadding(0);

  _lastDotX = newX;
  _lastDotY = newY;
}
