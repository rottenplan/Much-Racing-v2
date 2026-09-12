#ifndef KEYBOARD_COMPONENT_H
#define KEYBOARD_COMPONENT_H

#include "../../config.h"
#include <Arduino.h>
#include <TFT_eSPI.h>

class KeyboardComponent {
public:
  enum KeyType { KEY_CHAR, KEY_SHIFT, KEY_DEL, KEY_SPACE, KEY_OK, KEY_NONE };

  struct KeyResult {
    KeyType type;
    char value;
  };

  void draw(TFT_eSPI *tft, int startY, bool isUppercase,
            char highlightChar = '\0', char partialRedrawKey = '\0');
  KeyResult handleTouch(int x, int y, int startY, bool isUppercase);

private:
  static const int KEY_W = 28;
  static const int KEY_H = 25;
  static const int GAP = 5;
};

#endif
