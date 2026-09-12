#include "FeedbackManager.h"

FeedbackManager &FeedbackManager::getInstance() {
  static FeedbackManager instance;
  return instance;
}

void FeedbackManager::begin() {
  // Setup RGB LED Pins
  // Setup RGB LED Pins (20kHz to fix audible noise)
#ifdef PIN_RGB_RED
  ledcSetup(CH_RED, 20000, 8);
  ledcAttachPin(PIN_RGB_RED, CH_RED);
#endif

#ifdef PIN_RGB_GREEN
  ledcSetup(CH_GREEN, 20000, 8);
  ledcAttachPin(PIN_RGB_GREEN, CH_GREEN);
#endif

#ifdef PIN_RGB_BLUE
  ledcSetup(CH_BLUE, 20000, 8);
  ledcAttachPin(PIN_RGB_BLUE, CH_BLUE);
#endif

  setLed(0, 0, 0);

  // Setup Buzzer
#ifdef PIN_SPEAKER
  ledcSetup(CH_BUZZER, 2000, 8);
  ledcAttachPin(PIN_SPEAKER, CH_BUZZER);
  ledcWrite(CH_BUZZER, 0); // Off
#endif
}

void FeedbackManager::update() {
  unsigned long now = millis();

  // Handle Beep Timer
  if (_isBeeping && now >= _beepEndTime) {
#ifdef PIN_SPEAKER
    ledcWrite(CH_BUZZER, 0);
    ledcWriteTone(CH_BUZZER, 0);
#endif
    _isBeeping = false;
  }

  // Handle LED Blink Timer
  if (_isBlinking && now >= _ledBlinkEndTime) {
    _isBlinking = false; // Reset flag FIRST so _writeLed isn't blocked
    _writeLed(_prevR, _prevG, _prevB);
  }
}

void FeedbackManager::setLed(uint8_t r, uint8_t g, uint8_t b) {
  if (_isBlinking)
    return; // Don't override blink

  _writeLed(r, g, b);

  _prevR = r;
  _prevG = g;
  _prevB = b;
}

void FeedbackManager::_writeLed(uint8_t r, uint8_t g, uint8_t b) {
#ifdef PIN_RGB_RED
#ifdef LED_COMMON_ANODE
  ledcWrite(CH_RED, 255 - r);
#else
  ledcWrite(CH_RED, r);
#endif
#endif
#ifdef PIN_RGB_GREEN
#ifdef LED_COMMON_ANODE
  ledcWrite(CH_GREEN, 255 - g);
#else
  ledcWrite(CH_GREEN, g);
#endif
#endif
#ifdef PIN_RGB_BLUE
#ifdef LED_COMMON_ANODE
  ledcWrite(CH_BLUE, 255 - b);
#else
  ledcWrite(CH_BLUE, b);
#endif
#endif
}

void FeedbackManager::setLedColor(uint32_t color) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  setLed(r, g, b);
}

void FeedbackManager::blinkLed(uint8_t r, uint8_t g, uint8_t b,
                               int durationMs) {
  _isBlinking = true;
  _ledBlinkEndTime = millis() + durationMs;
  _writeLed(r, g, b);
}

void FeedbackManager::playTone(int frequency, int durationMs) {
#ifdef PIN_SPEAKER
  ledcWriteTone(CH_BUZZER, frequency);
  _beepEndTime = millis() + durationMs;
  _isBeeping = true;
#endif
}

void FeedbackManager::beep(int durationMs) {
  playTone(2000, durationMs); // 2kHz beep
}

void FeedbackManager::testSequence() {
  // Red
  setLed(255, 0, 0);
  beep(100);
  delay(200);

  // Green
  setLed(0, 255, 0);
  beep(100);
  delay(200);

  // Blue
  setLed(0, 0, 255);
  beep(100);
  delay(200);

  // Off
  setLed(0, 0, 0);
}
