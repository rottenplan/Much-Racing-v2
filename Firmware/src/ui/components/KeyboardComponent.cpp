#include "KeyboardComponent.h"

// Helper to calculate key width dynamically
int getDynamicKeyWidth() { return (SCREEN_WIDTH - 20) / 10; }

void KeyboardComponent::draw(TFT_eSPI *tft, int startY, bool isUppercase,
                             char highlightChar, char partialRedrawKey) {
  tft->setTextFont(1);
  tft->setTextSize(1);

  int keyW = getDynamicKeyWidth();
  int keyH = 35;

  // QWERTY keyboard rows
  // Row 3: SHFT (col 0)
  String rows[] = {
      "1234567890", "QWERTYUIOP", "ASDFGHJKL",
      " ZXCVBNM"}; // Standardized to uppercase for case switching logic

  for (int row = 0; row < 4; row++) {
    String keys = rows[row];
    int numKeys = keys.length();
    int totalW = numKeys * keyW;
    int startX = (SCREEN_WIDTH - totalW) / 2;

    for (int col = 0; col < numKeys; col++) {
      int x = startX + (col * keyW);
      int ky = startY + (row * keyH);

      char c = keys[col];
      bool isHighlighted = (c == highlightChar);

      // Special highlighting for SHFT in Row 3
      if (row == 3) {
        if (col == 0)
          isHighlighted = (highlightChar == 1);
      }

      if (!isUppercase && c >= 'A' && c <= 'Z') {
        c = c + ('a' - 'A');
        if (highlightChar >= 'a' && highlightChar <= 'z') {
          isHighlighted = (c == highlightChar);
        }
      }

      // Optimization: Skip if we only want to redraw a specific key
      if (partialRedrawKey != 0) {
        // Check if this is the key we want to redraw
        bool match = (c == partialRedrawKey);
        // Handle SHFT special case (value 1)
        if (row == 3 && col == 0 && partialRedrawKey == 1)
          match = true;

        if (!match)
          continue;
      }

      uint16_t boxColor = isHighlighted ? COLOR_HIGHLIGHT : COLOR_BG;
      uint16_t txtColor = isHighlighted ? TFT_BLACK : TFT_WHITE;

      if (isHighlighted) {
        tft->fillRect(x, ky, keyW, keyH, boxColor);
      } else {
        tft->drawRect(x, ky, keyW, keyH, TFT_WHITE);
        tft->fillRect(x + 1, ky + 1, keyW - 2, keyH - 2, COLOR_BG);
      }

      tft->setTextColor(txtColor, boxColor);
      tft->setTextDatum(MC_DATUM);

      if (row == 3 && col == 0) {
        tft->drawString("SHFT", x + keyW / 2, ky + keyH / 2);
      } else {
        tft->drawString(String(c), x + keyW / 2, ky + keyH / 2);
      }
    }
  }

  // Row 4 (Special): DEL, SPACE, ENT
  int specialY = startY + (4 * keyH);
  int availW = SCREEN_WIDTH - 20;

  // Layout: [DEL 20%] [SPACE 55%] [ENT 25%] - Gaps handled
  int gap = 4;
  int delW = (availW - 2 * gap) * 0.20;
  int entW = (availW - 2 * gap) * 0.25;
  int spaceW = availW - delW - entW - 2 * gap;

  int startX_row4 = 10;
  int delX = startX_row4;
  int spaceX = delX + delW + gap;
  int entX = spaceX + spaceW + gap;

  // DEL (Value 2)
  if (partialRedrawKey == 0 || partialRedrawKey == 2) {
    bool delHigh = (highlightChar == 2);
    uint16_t delCol = delHigh ? COLOR_HIGHLIGHT : COLOR_BG;
    uint16_t delTxt = delHigh ? TFT_BLACK : TFT_WHITE;
    tft->fillRect(delX, specialY, delW, keyH, delCol);
    tft->drawRect(delX, specialY, delW, keyH, TFT_WHITE);
    tft->setTextColor(delTxt, delCol);
    tft->drawString("DEL", delX + delW / 2, specialY + keyH / 2);
  }

  // SPACE (Value ' ')
  if (partialRedrawKey == 0 || partialRedrawKey == ' ') {
    bool spaceHigh = (highlightChar == ' ');
    uint16_t spaceCol = spaceHigh ? COLOR_HIGHLIGHT : COLOR_BG;
    uint16_t spaceTxt = spaceHigh ? TFT_BLACK : TFT_WHITE;
    tft->fillRect(spaceX, specialY, spaceW, keyH, spaceCol);
    tft->drawRect(spaceX, specialY, spaceW, keyH, TFT_WHITE);
    tft->setTextColor(spaceTxt, spaceCol);
    tft->drawString("SPACE", spaceX + spaceW / 2, specialY + keyH / 2);
  }

  // ENTER (Value 3)
  if (partialRedrawKey == 0 || partialRedrawKey == 3) {
    bool entHigh = (highlightChar == 3);
    uint16_t entCol = entHigh ? COLOR_HIGHLIGHT : COLOR_BG;
    uint16_t entTxt = entHigh ? TFT_BLACK : TFT_WHITE;
    tft->fillRect(entX, specialY, entW, keyH, entCol);
    tft->drawRect(entX, specialY, entW, keyH, TFT_WHITE);
    tft->setTextColor(entTxt, entCol);
    tft->drawString("ENT", entX + entW / 2, specialY + keyH / 2);
  }
}

KeyboardComponent::KeyResult
KeyboardComponent::handleTouch(int x, int y, int startY, bool isUppercase) {
  KeyResult result = {KEY_NONE, 0};
  int keyW = getDynamicKeyWidth();
  int keyH = 35;
  int keyY = y - startY;
  if (keyY < 0)
    return result;

  int row = keyY / keyH;
  String rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", " ZXCVBNM"};

  if (row >= 0 && row < 4) {
    String keys = rows[row];
    int numKeys = keys.length();
    int totalW = numKeys * keyW;
    int startX = (SCREEN_WIDTH - totalW) / 2;

    if (x < startX || x > startX + totalW)
      return result;
    int col = (x - startX) / keyW;

    if (col >= 0 && col < numKeys) {
      if (row == 3 && col == 0)
        result.type = KEY_SHIFT;
      else {
        result.type = KEY_CHAR;
        char c = keys[col];
        if (!isUppercase && c >= 'A' && c <= 'Z') {
          c = c + ('a' - 'A');
        }
        result.value = c;
      }
    }
  } else if (row == 4) {
    int availW = SCREEN_WIDTH - 20;
    int gap = 4;
    int delW = (availW - 2 * gap) * 0.20;
    int entW = (availW - 2 * gap) * 0.25;
    int spaceW = availW - delW - entW - 2 * gap;

    int startX_row4 = 10;
    int delX = startX_row4;
    int spaceX = delX + delW + gap;
    int entX = spaceX + spaceW + gap;

    if (x >= delX && x < delX + delW) {
      result.type = KEY_DEL;
    } else if (x >= spaceX && x < spaceX + spaceW) {
      result.type = KEY_SPACE;
    } else if (x >= entX && x < entX + entW) {
      result.type = KEY_OK;
    }
  }
  return result;
}
