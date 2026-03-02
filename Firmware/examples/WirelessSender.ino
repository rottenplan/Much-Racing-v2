/*
  Antigravity Wireless RPM Sender
  For: ESP32-C3
  Protocol: ESP-NOW (Low Latency)
*/

#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// --- DYNAMIC PIN CONFIGURATION ---
#ifdef ARDUINO_ESP32C3_DEV
// ESP32-C3 Super Mini / DevKit
#define PIN_SIGNAL 2
#define PIN_LED 8 // Onboard NeoPixel
#else
// Standard ESP32 (DevKit v1, etc)
// Note: Pin 8 is FLASH-RESERVED on standard ESP32. Using it crashes the unit.
#define PIN_SIGNAL 34 // Safe Input-only pin
#define PIN_LED 2     // Usually Onboard LED (Not always NeoPixel)
#endif

#define PPR 1.0 // Pulse Per Revolution (1.0 for most 4T, 0.5 for some, etc)
#define SEND_INTERVAL 30 // Send data every 30ms

// LED Setup
Adafruit_NeoPixel pixels(1, PIN_LED, NEO_GRB + NEO_KHZ800);

// Data structure to send
typedef struct struct_message {
  uint16_t rpm;
} struct_message;

struct_message myData;
volatile unsigned long pulseCount = 0;
volatile unsigned long lastPulseMicros = 0;
volatile unsigned long pulseInterval = 0;

// Interrupt Service Routine (ISR)
void IRAM_ATTR onPulse() {
  unsigned long now = micros();
  unsigned long diff = now - lastPulseMicros;

  // Debounce: 2ms (Max 30,000 RPM) to filter noise/ringing
  if (diff > 2000) {
    pulseInterval = diff;
    lastPulseMicros = now;
    pulseCount++;
  }
}

void setup() {
  Serial.begin(115200);

  // 0. Setup LED
  pixels.begin();
  pixels.setBrightness(20);
  pixels.setPixelColor(0, pixels.Color(50, 0, 0)); // Red (Startup)
  pixels.show();

  // 1. Setup Input Pin
  pinMode(PIN_SIGNAL, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_SIGNAL), onPulse, FALLING);

  // 2. WiFi Scan for Receiver Channel
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("Scanning for Receiver...");

  // Blue Pulse during scan
  pixels.setPixelColor(0, pixels.Color(0, 0, 50));
  pixels.show();

  int n = WiFi.scanNetworks();
  int channel = 1; // Default
  bool found = false;

  Serial.println("Scan done");
  if (n == 0) {
    Serial.println("No networks found");
  } else {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(ssid);
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*");

      if (ssid == "MuchRacing-GPS") {
        channel = WiFi.channel(i);
        found = true;
        Serial.printf("Target Found! Setting Channel: %d\n", channel);

        // Green Blink for Success
        pixels.setPixelColor(0, pixels.Color(0, 50, 0));
        pixels.show();
        delay(500);
        break;
      }
    }
  }

  // Clean up scan memory if needed (ESP32 usually handles this)
  // WiFi.scanDelete();

  // 3. Init ESP-NOW
  // Important: Set channel BEFORE Init? No, set valid channel in Peer Info.
  // Actually, for ESP-NOW to work on a specific channel, the STA interface must
  // be ON that channel. esp_now_init() doesn't change channel. We need to set
  // it via WiFi.printDiag(Serial);

  // Force WiFi to the discovered channel
  // Note: esp_wifi_set_channel is low level, but we can just use:
  // WiFi.printDiag(Serial);
  // There isn't a direct high-level API to "set channel" in STA mode without
  // connecting, except maybe promiscuous mode or similar hacks. BUT:
  // esp_now_add_peer takes a channel argument. CRITICALLY: Sender and Receiver
  // MUST be on the same physical channel. If Receiver is 6, Sender MUST be 6.
  // The most reliable way is `esp_wifi_set_promiscuous(true)` then
  // `esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE)`, or use
  // `esp_wifi_set_channel` directly after initializing WiFi.

  // Let's rely on esp_wifi calls.
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  Serial.printf("WiFi Channel Set to: %d\n", channel);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    pixels.setPixelColor(0, pixels.Color(255, 0, 0)); // Bright Red Error
    pixels.show();
    return;
  }

  // Register Broadcast Peer
  uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = channel; // Use found channel
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  Serial.println("Wireless RPM Sender Ready!");
}

unsigned long lastSend = 0;
int currentRPM = 0;
unsigned long lastLedUpdate = 0;
bool ledState = false;

void loop() {
  unsigned long now = millis();

  if (now - lastSend >= SEND_INTERVAL) {
    lastSend = now;

    unsigned long lastP = lastPulseMicros;
    unsigned long interval = pulseInterval;
    unsigned long nowMicros = micros();

    // 1. Calculate RPM from Period
    // Timeout: 0.5s without pulse -> 0 RPM
    if (nowMicros - lastP > 500000) {
      currentRPM = 0;
    } else if (interval > 0) {
      // RPM = 60,000,000 / (interval * PPR)
      float instRPM = 60000000.0 / (float)(interval * PPR);
      if (instRPM > 20000)
        instRPM = 0; // Filter insane values

      // Smoothing
      currentRPM = (currentRPM * 6 + (int)instRPM * 4) / 10;
    }

    // 2. Broadcast via ESP-NOW
    myData.rpm = (uint16_t)currentRPM;
    esp_now_send(NULL, (uint8_t *)&myData, sizeof(myData));

    // Debug
    Serial.printf("RPM: %d (Ch: %d)\n", currentRPM, WiFi.channel());
  }

  // 3. LED Indicator Logic (Non-blocking)
  // State: RPM = 0 -> Red Breathing/Blink (1Hz)
  // State: RPM > 0 -> Blue Transmission Blink (To show activity)

  if (currentRPM > 0) {
    // Active Transmission: Rapid Blue Blink (e.g. every 100ms)
    if (now - lastLedUpdate > 100) {
      lastLedUpdate = now;
      ledState = !ledState;
      if (ledState) {
        pixels.setPixelColor(0, pixels.Color(0, 0, 100)); // Blue
      } else {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0)); // Off
      }
      pixels.show();
    }
  } else {
    // Idle: Slow Red Pulse/Blink (1Hz)
    if (now - lastLedUpdate > 500) { // 500ms ON / 500ms OFF
      lastLedUpdate = now;
      ledState = !ledState;
      if (ledState) {
        pixels.setPixelColor(0, pixels.Color(20, 0, 0)); // Dim Red
      } else {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0)); // Off
      }
      pixels.show();
    }
  }
}
