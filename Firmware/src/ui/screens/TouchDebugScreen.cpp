#include "TouchDebugScreen.h"
// #include "../fonts/Org_01.h" // Comment out to rule out font issues

void TouchDebugScreen::onShow() {
  Serial.println("DEBUG: TouchDebugScreen::onShow START");

  if (!_ui) {
    Serial.println("ERROR: _ui is NULL!");
    return;
  }

  TFT_eSPI *tft = _ui->getTft();
  if (!tft) {
    Serial.println("ERROR: tft is NULL!");
    return;
  }

  // Clear screen to black
  tft->fillScreen(TFT_BLACK);
  Serial.println("DEBUG: Screen Cleared");

  // Draw Grid for reference
  tft->setTextColor(TFT_DARKGREY);
  for (int x = 0; x < SCREEN_WIDTH; x += 40)
    tft->drawFastVLine(x, 0, SCREEN_HEIGHT, 0x3186); // Dark Grey
  for (int y = 0; y < SCREEN_HEIGHT; y += 40)
    tft->drawFastHLine(0, y, SCREEN_WIDTH, 0x3186);
  Serial.println("DEBUG: Grid Drawn");

  // Instructions
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextDatum(TC_DATUM);
  // tft->setFreeFont(&Org_01); // Disable custom font
  tft->setTextFont(1); // Use default font
  tft->setTextSize(2);
  tft->drawString("TOUCH DEBUG MODE", SCREEN_WIDTH / 2, STATUS_BAR_HEIGHT + 8);
  tft->setTextSize(1);
  tft->drawString("Touch anywhere to draw. Tap EXIT to return.",
                  SCREEN_WIDTH / 2, 35);

  // Exit Button Area (Top Right)
  tft->fillRoundRect(SCREEN_WIDTH - 60, STATUS_BAR_HEIGHT, 55, 30, 4, TFT_RED);
  tft->setTextColor(TFT_WHITE, TFT_RED);
  tft->setTextDatum(MC_DATUM);
  tft->drawString("EXIT", SCREEN_WIDTH - 32, STATUS_BAR_HEIGHT + 5);

  _pointCount = 0;
  Serial.println("DEBUG: TouchDebugScreen::onShow END");
}

void TouchDebugScreen::update() {
  // Direct touch access to visualize RAW data if possible, but UIManager
  // filters it. We will use getTouchPoint() which now has debounce. Ideally for
  // drawing we want faster response, but let's see.

  UIManager::TouchPoint p = _ui->getTouchPoint();

  if (p.x != -1) {
    TFT_eSPI *tft = _ui->getTft();

    // Check Exit
    if (p.x > SCREEN_WIDTH - 70 && p.y < 50) {
      _ui->switchScreen(SCREEN_SETTINGS);
      return;
    }

    // Draw Point
    // CRASH TEST: Disable drawing to see if it saves the crash
    // tft->fillCircle(p.x, p.y, 2, TFT_GREEN);

    // Log invalid/valid
    Serial.printf("TOUCH ACTION: X=%d Y=%d\n", p.x, p.y);

    // Show Coordinates
    // Clear previous coord area (Bottom Left)
    // tft->fillRect(0, SCREEN_HEIGHT - 30, 200, 30, TFT_BLACK);

    tft->setTextColor(TFT_CYAN, TFT_BLACK);
    tft->setTextDatum(BL_DATUM);
    tft->setTextSize(2);
    char buf[32];
    sprintf(buf, "X:%d Y:%d", p.x, p.y);
    tft->drawString(buf, 10, SCREEN_HEIGHT - 5);

    _pointCount++;
  }
}
