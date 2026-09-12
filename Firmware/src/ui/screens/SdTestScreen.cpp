#include "SdTestScreen.h"
#include "../fonts/Org_01.h"

extern SessionManager sessionManager;

SdTestScreen *SdTestScreen::_instance = nullptr;

void SdTestScreen::onShow() {
  _instance = this;
  _isTesting = false;
  _progress = 0;
  _statusText = "Ready to test";
  _result.success = false;
  _result.cardType = "";

  // Clear initial state
  drawScreen();

  // Auto-start test after a brief delay
  // (Or we could wait for a button press, but auto-start is better for UX)
  _isTesting = true;
  startTest();
}

void SdTestScreen::onHide() { _instance = nullptr; }

void SdTestScreen::progressCallback(int percent, String status) {
  if (_instance) {
    _instance->_progress = percent;
    _instance->_statusText = status;
    _instance->drawProgress(); // Optimized partial redraw
    yield();                   // CRITICAL: Prevent WDT reset
  }
}

void SdTestScreen::startTest() {
  _statusText = "Testing...";
  drawScreen();

  // Run the actual test
  _result = sessionManager.runFullTest(progressCallback);

  _isTesting = false;
  drawScreen();
}

void SdTestScreen::update() {
  UIManager::TouchPoint p = _ui->getTouchPoint();
  if (p.x != -1) {
    // Back Button Area (Bottom Left)
    if (p.x < 80 && p.y > 240) {
      _ui->switchScreen(SCREEN_SETTINGS);
      return;
    }

    // Re-test if tapped on card area and not testing
    if (!_isTesting && p.y > 50 && p.y < 270) {
      _isTesting = true;
      startTest();
    }
  }
}

void SdTestScreen::drawScreen(bool full) {
  TFT_eSPI *tft = _ui->getTft();

  if (full) {
    // Clean header area handled by switchScreen, but we need to draw our
    // specific parts
    _ui->drawStatusBar(true);

    // Back Button
    tft->fillTriangle(15, SCREEN_HEIGHT - 30, 30, SCREEN_HEIGHT - 40, 30,
                      SCREEN_HEIGHT - 20, TFT_BLUE);
  }

  if (_isTesting) {
    // --- PROGRESS VIEW ---
    int cardX = 40;
    int cardY = 80;
    int cardW = SCREEN_WIDTH - 80;
    int cardH = 120;

    if (full) {
      tft->fillRoundRect(cardX, cardY, cardW, cardH, 12, 0x18E3);
      tft->drawRoundRect(cardX, cardY, cardW, cardH, 12, TFT_DARKGREY);
    }

    drawProgress();

  } else if (!_result.success && _result.cardType == "NO CARD") {
    // --- ERROR VIEW ---
    int cardX = 40;
    int cardY = 80;
    int cardW = SCREEN_WIDTH - 80;
    int cardH = 120;

    tft->fillRoundRect(cardX, cardY, cardW, cardH, 12, 0x2800);
    tft->drawRoundRect(cardX, cardY, cardW, cardH, 12, TFT_RED);

    tft->fillCircle(SCREEN_WIDTH / 2, cardY + 35, 20, TFT_RED);
    tft->setTextColor(TFT_WHITE, TFT_RED);
    tft->setTextDatum(MC_DATUM);
    tft->setTextFont(4);
    tft->drawString("!", SCREEN_WIDTH / 2, cardY + 36);

    tft->setTextColor(TFT_WHITE, 0x2800);
    tft->setTextDatum(TC_DATUM);
    tft->setTextFont(2);
    tft->drawString("MISSING SD CARD", SCREEN_WIDTH / 2, cardY + 65);
    tft->setTextColor(TFT_SILVER, 0x2800);
    tft->drawString("Insert card to start", SCREEN_WIDTH / 2, cardY + 85);

  } else {
    // --- RESULT VIEW (The "Grown Up" Version) ---
    int cardX = 15;
    int cardY = 50;
    int cardW = SCREEN_WIDTH - 30;
    int cardH = 220;

    tft->fillRoundRect(cardX, cardY, cardW, cardH, 10, 0x10A2);
    tft->drawRoundRect(cardX, cardY, cardW, cardH, 10, TFT_DARKGREY);

    tft->setTextColor(TFT_SKYBLUE, 0x10A2);
    tft->setTextDatum(TL_DATUM);
    tft->setFreeFont(&Org_01);
    tft->drawString("SD CARD DIAGNOSIS", cardX + 15, cardY + 10);
    tft->drawFastHLine(cardX + 10, cardY + 25, cardW - 20, 0x3186);

    // Icon
    int icX = cardX + cardW - 60;
    int icY = cardY + 40;
    tft->fillRoundRect(icX, icY, 35, 45, 4, TFT_DARKGREY);
    tft->fillTriangle(icX, icY, icX + 10, icY, icX, icY + 10, 0x10A2);
    tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft->setTextDatum(MC_DATUM);
    tft->setTextFont(1);
    tft->drawString("SD", icX + 17, icY + 15);
    for (int i = 0; i < 5; i++)
      tft->fillRect(icX + 5 + (i * 6), icY + 37, 4, 6, TFT_GOLD);

    // Details
    tft->setTextDatum(TL_DATUM);
    tft->setFreeFont(&Org_01);
    tft->setTextColor(TFT_SILVER, 0x10A2);
    tft->drawString("HARDWARE", cardX + 15, cardY + 45);
    tft->setTextColor(TFT_WHITE, 0x10A2);
    tft->setTextFont(2);
    tft->setFreeFont(NULL);
    tft->drawString(_result.cardType, cardX + 15, cardY + 58);
    tft->drawString(_result.sizeLabel, cardX + 15, cardY + 75);

    tft->setTextColor(TFT_SILVER, 0x10A2);
    tft->setFreeFont(&Org_01);
    tft->drawString("UTILIZATION", cardX + 130, cardY + 45);
    tft->setTextColor(TFT_WHITE, 0x10A2);
    tft->setTextFont(2);
    tft->setFreeFont(NULL);
    tft->drawString(_result.usedLabel + " Used", cardX + 130, cardY + 58);

    // Bars
    tft->drawFastHLine(cardX + 10, cardY + 110, cardW - 20, 0x3186);
    tft->setTextColor(TFT_SKYBLUE, 0x10A2);
    tft->setFreeFont(&Org_01);
    tft->drawString("PERFORMANCE (KB/s)", cardX + 15, cardY + 115);

    int barY = cardY + 135;
    int barW = cardW - 100;

    // Read
    float rVal = _result.readSpeedKBps;
    uint16_t rCol =
        (rVal > 4000)
            ? TFT_CYAN
            : (rVal > 2000 ? TFT_GREEN : (rVal > 800 ? TFT_YELLOW : TFT_RED));
    int rFill = map((int)constrain(rVal, 0, 8000), 0, 8000, 0, barW);
    tft->setTextColor(TFT_WHITE, 0x10A2);
    tft->setFreeFont(NULL);
    tft->drawString("READ", cardX + 15, barY);
    tft->drawRect(cardX + 60, barY, barW, 12, TFT_DARKGREY);
    tft->fillRect(cardX + 61, barY + 1, rFill, 10, rCol);
    tft->setTextColor(rCol, 0x10A2);
    tft->drawRightString(String((int)rVal), cardX + cardW - 10, barY, 1);

    // Write
    barY += 25;
    float wVal = _result.writeSpeedKBps;
    uint16_t wCol =
        (wVal > 3000)
            ? TFT_CYAN
            : (wVal > 1500 ? TFT_GREEN : (wVal > 500 ? TFT_YELLOW : TFT_RED));
    int wFill = map((int)constrain(wVal, 0, 8000), 0, 8000, 0, barW);
    tft->setTextColor(TFT_WHITE, 0x10A2);
    tft->drawString("WRITE", cardX + 15, barY);
    tft->drawRect(cardX + 60, barY, barW, 12, TFT_DARKGREY);
    tft->fillRect(cardX + 61, barY + 1, wFill, 10, wCol);
    tft->setTextColor(wCol, 0x10A2);
    tft->drawRightString(String((int)wVal), cardX + cardW - 10, barY, 1);

    tft->setTextColor(TFT_DARKGREY, 0x10A2);
    tft->setTextDatum(BC_DATUM);
    tft->drawString("Test complete - Tap to re-test", SCREEN_WIDTH / 2,
                    cardY + cardH - 10);
  }

  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
}

void SdTestScreen::drawProgress() {
  TFT_eSPI *tft = _ui->getTft();
  int cardX = 40;
  int cardY = 80;
  int cardW = SCREEN_WIDTH - 80;

  tft->setTextColor(TFT_WHITE, 0x18E3);
  tft->setTextDatum(MC_DATUM);
  tft->setFreeFont(&Org_01);
  // Clear status text area
  tft->fillRect(cardX + 10, cardY + 20, cardW - 20, 20, 0x18E3);
  tft->drawString(_statusText, SCREEN_WIDTH / 2, cardY + 30);

  // Progress Bar
  int barW = cardW - 40;
  int barH = 15;
  int barX = cardX + 20;
  int barY = cardY + 70;

  // Partial fill for bar
  int fillW = (barW * _progress) / 100;
  tft->fillRect(barX + 1, barY + 1, fillW - 2, barH - 2, TFT_GREEN);
  tft->fillRect(barX + fillW, barY + 1, barW - fillW - 1, barH - 2, 0x18E3);

  tft->setFreeFont(NULL);
  tft->setTextFont(2);
  tft->setTextColor(TFT_WHITE, 0x18E3);
  // Clear percent area
  tft->fillRect(SCREEN_WIDTH / 2 - 20, barY + 20, 40, 20, 0x18E3);
  tft->drawString(String(_progress) + "%", SCREEN_WIDTH / 2, barY + 30);
}
