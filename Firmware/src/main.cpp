#include "config.h"
#include "core/GPSManager.h"
#include <TAMC_GT911.h>

#include "core/BatteryManager.h"
#include "core/FeedbackManager.h"
#include "core/IMUManager.h"
#include "core/MQTTManager.h"
#include "core/NavigationManager.h"
#include "core/SessionManager.h"
#include "core/SyncManager.h"
#include "core/WiFiManager.h"
#include "ui/UIManager.h"
#include "ui/screens/SplashScreen.h" // Added include
#include <Arduino.h>
#include <nvs_flash.h>
#include <Preferences.h>

// Objek Perangkat Keras
TFT_eSPI tft = TFT_eSPI();
TAMC_GT911 touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, TOUCH_WIDTH,
                 TOUCH_HEIGHT);
// TouchLib touch(
//     Wire,
//     0x00); // Placeholder, menunggu hasil list_dir untuk tahu konstruktor
UIManager uiManager(&tft);
GPSManager gpsManager;
SessionManager sessionManager;
WiFiManager wifiManager;
SyncManager syncManager;

// SPIClass touchSpi = SPIClass(HSPI); // Tidak diperlukan untuk I2C

// Global I2C Mutex Definition
SemaphoreHandle_t i2cMutex = NULL;

// Forward declaration for FreeRTOS IMU task
void imuTask(void *parameter);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n\n=== BOOTING MUCH RACING PRO ==="));
  Serial.flush();

  // Explicitly init NVS to fix "nvs_open failed" in global constructors
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  Serial.println(F("SYSTEM: NVS Initialized"));

  {
    Preferences prefs;
    prefs.begin("laptimer", false);
    prefs.end();
    prefs.begin("muchrace", false);
    prefs.end();
    prefs.begin("sync", false);
    prefs.end();
  }

  // Now NVS is ready, allow SyncManager to load its credential cache
  syncManager.loadCredentialCache();

  i2cMutex = xSemaphoreCreateMutex();

  // Force Backlight OFF immediately to prevent startup glitches
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, LOW);

  // GT911 Touch Reset (using RST pin only - NOT pin 21 which is GPS TX AND IMU
  // SDA)
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(50);

  // Inisialisasi UI (TFT)
  tft.init();
  tft.setRotation(1); // 0=Potret, 1=Lanskap. Periksa pemasangan Anda!

  // Anti-Glitch: Ensure Backlight is OFF and Screen is Black
  digitalWrite(PIN_TFT_BL, LOW);
  tft.fillScreen(TFT_BLACK);

  // Inisialisasi PWM Lampu Latar (Start 0% Duty)
  ledcSetup(0, 5000, 8); // Saluran 0, 5kHz, 8-bit
  ledcWrite(0, 0);       // Pastikan duty cycle nol SEBELUM pasang pin
  ledcAttachPin(PIN_TFT_BL, 0);

  // Re-clear screen just in case init/rotation caused garbage
  tft.fillScreen(TFT_BLACK);

  // Inisialisasi I2C Shared Bus (Touch & IMU)
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(100000);
  Wire.setTimeOut(50); // Keep persistent timeout

  // Initialize UI Manager Early
  uiManager.begin();
  SplashScreen *splash = uiManager.getSplashScreen();

  // Inisialisasi Sentuh (GT911 uses pin 21 internally - must happen BEFORE IMU)
  if (splash)
    splash->setLoadingStatus("Touch Init...");
  touch.begin();
  uiManager.setTouch(&touch);

  Wire.setClock(100000); // Re-assert in case touch.begin() reset it
  Wire.setTimeOut(
      20); // 20ms timeout is required to read 42 bytes of IMU FIFO at 100kHz

  // Initialize IMU AFTER touch.begin() - Wire1 (SDA=21) will overwrite touch
  // lib's claim on pin 21
  if (splash)
    splash->setLoadingStatus("IMU Starting...");
  imuManager.begin();

  // Inisialisasi Inti (Heavy Lifting happens while Splash is shown)
  if (splash)
    splash->setLoadingStatus("GNSS Starting...");
  gpsManager.begin();

  if (splash)
    splash->setLoadingStatus("Session Manager...");
  sessionManager.begin();

  if (splash)
    splash->setLoadingStatus("Power System...");
  BatteryManager::getInstance().begin();

  // Link GPS to WiFi for Web API
  wifiManager.setGPS(&gpsManager);

  if (splash)
    splash->setLoadingStatus("WiFi Starting...");
  wifiManager.begin(); // Setup AP and Server

  // Initialize Feedback (LEDs/Buzzer)
  if (splash)
    splash->setLoadingStatus("System Ready!");
  FeedbackManager::getInstance().begin();
  FeedbackManager::getInstance().testSequence(); // Visual/Audio confirm

  if (splash)
    splash->setLoadingStatus("MQTT Init...");
  mqttManager.begin();

  // Bluetooth SPP for turn-by-turn navigation ("MuchRacing-Nav")
  navigationManager.begin();

  // --- LOW BATTERY SAFETY CHECK (20%) ---
  // Wait a moment for voltage to stabilize after power-on
  for (int i = 0; i < 5; i++) {
    BatteryManager::getInstance().update();
    delay(100);
  }

  if (BatteryManager::getInstance().getPercentage() < 20 &&
      !BatteryManager::getInstance().isCharging() &&
      !BatteryManager::getInstance().isUsbConnected()) {

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.drawString("LOW BATTERY!", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 20);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(2);
    tft.drawString("Please charge device to 20%+", SCREEN_WIDTH / 2,
                   SCREEN_HEIGHT / 2 + 20);

    FeedbackManager::getInstance().beep(500);
    delay(3000);

    // Shut down (Deep Sleep)
    ledcWrite(0, 0); // Backlight Off
    esp_deep_sleep_start();
  }

  // Start IMU update on Core 0 (isolate I2C blocking from main loop on Core 1)
  xTaskCreatePinnedToCore(imuTask, "IMU_Task", 4096, NULL, 1, NULL, 0);

  // Serial.println("System Ready"); // DISABLED: Serial used for GPS
}

// FreeRTOS Task: IMU Update on Core 0 (isolate I2C blocking from main loop)
void imuTask(void *parameter) {
  for (;;) {
    imuManager.update();
    vTaskDelay(pdMS_TO_TICKS(10)); // ~100Hz
  }
}

void loop() {
  // GPS updates as fast as possible for high-frequency tracking
  gpsManager.update();

  // Consume BT navigation data (non-blocking)
  navigationManager.update();

  // Throttle UI and Touch to ~100Hz (10ms) to prevent I2C/SPI contention
  static unsigned long lastUiUpdate = 0;
  if (millis() - lastUiUpdate >= 10) {
    uiManager.update();
    lastUiUpdate = millis();
  }

  // --- PERFORMANCE MONITOR ---
  static unsigned long lastStats = 0;
  static int loopCount = 0;
  static unsigned long maxLoopTime = 0;
  unsigned long loopStart = micros();

  // Perform loop tasks...
  unsigned long now = millis();

  // Less critical background tasks can run at a fixed rate (e.g., 50Hz / 20ms)
  static unsigned long lastBackgroundTask = 0;
  if (now - lastBackgroundTask >= 20) {
    lastBackgroundTask = now;
    // imuManager.update() moved to FreeRTOS task (Core 0) to avoid I2C blocking
    BatteryManager::getInstance().update();
    wifiManager.update();
    mqttManager.update();
    FeedbackManager::getInstance().update();
  }

  // Update Loop Stats
  loopCount++;
  unsigned long loopDuration = micros() - loopStart;
  if (loopDuration > maxLoopTime)
    maxLoopTime = loopDuration;

  // Print Stats every 1 second
  if (now - lastStats >= 1000) {
    DEBUG_PRINTF("STATS: FPS=%d | Heap=%d B | MinHeap=%d B | MaxLoop=%lu us\n",
                 loopCount, esp_get_free_heap_size(),
                 esp_get_minimum_free_heap_size(), maxLoopTime);

    lastStats = now;
    loopCount = 0;
    maxLoopTime = 0;
  }
}
