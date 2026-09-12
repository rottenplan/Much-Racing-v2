#ifndef FEEDBACK_MANAGER_H
#define FEEDBACK_MANAGER_H

#include "../config.h"
#include <Arduino.h>

class FeedbackManager {
public:
  static FeedbackManager &getInstance();

  void begin();
  void update();

  // LED Control
  void setLed(uint8_t r, uint8_t g, uint8_t b);
  void setLedColor(uint32_t color); // 0xRRGGBB
  void blinkLed(uint8_t r, uint8_t g, uint8_t b, int durationMs);

  // Audio Control
  void playTone(int frequency, int durationMs);
  void beep(int durationMs = 100);

  // Test
  void testSequence();

private:
  FeedbackManager() {}

  unsigned long _beepEndTime = 0;
  bool _isBeeping = false;

  // LED State
  unsigned long _ledBlinkEndTime = 0;
  bool _isBlinking = false;
  uint8_t _prevR = 0, _prevG = 0, _prevB = 0;

  void _writeLed(uint8_t r, uint8_t g, uint8_t b);

  // PWM Channels (ESP32 specific)
  const int CH_RED = 1;
  const int CH_GREEN = 2;
  const int CH_BLUE = 3;
  const int CH_BUZZER = 4;
};

#endif
