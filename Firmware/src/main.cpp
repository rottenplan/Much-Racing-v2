#include "config.h"
#include "core/GPSManager.h"
#include <TAMC_GT911.h>

#include "core/BatteryManager.h"
#include "core/FeedbackManager.h"
#include "core/IMUManager.h"
#include "core/MQTTManager.h"
#include "core/RouteNavigator.h"
#include "core/SessionManager.h"
#include "core/SyncManager.h"
#include "core/WiFiManager.h"
#include "ui/UIManager.h"
#include "ui/screens/SplashScreen.h"
#include <Arduino.h>
#include <BluetoothSerial.h>
#include <Preferences.h>
#include <nvs_flash.h>

// Objek Perangkat Keras
TFT_eSPI tft = TFT_eSPI();
TAMC_GT911 touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, TOUCH_WIDTH,
                 TOUCH_HEIGHT);
UIManager uiManager(&tft);
GPSManager gpsManager;
SessionManager sessionManager;
WiFiManager wifiManager;
SyncManager syncManager;
BluetoothSerial btSerial;

// Global I2C Mutex Definition
SemaphoreHandle_t i2cMutex = NULL;

// Forward declaration for FreeRTOS IMU task
void imuTask(void *parameter);

// Buffer untuk data Bluetooth dari HP
String btBuffer = "";

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

  // GT911 Touch Reset
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(50);

  // Inisialisasi UI (TFT)
  tft.init();
  tft.setRotation(1);

  // Anti-Glitch: Ensure Backlight is OFF and Screen is Black
  digitalWrite(PIN_TFT_BL, LOW);
  tft.fillScreen(TFT_BLACK);

  // Inisialisasi PWM Lampu Latar (Start 0% Duty)
  ledcSetup(0, 5000, 8);
  ledcWrite(0, 0);
  ledcAttachPin(PIN_TFT_BL, 0);

  // Re-clear screen just in case init/rotation caused garbage
  tft.fillScreen(TFT_BLACK);

  // Inisialisasi I2C Shared Bus (Touch & IMU)
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(100000);
  Wire.setTimeOut(50);

  // Initialize UI Manager Early
  uiManager.begin();
  SplashScreen *splash = uiManager.getSplashScreen();

  // Inisialisasi Sentuh (GT911 uses pin 21 internally - must happen BEFORE IMU)
  if (splash)
    splash->setLoadingStatus("Touch Init...");
  touch.begin();
  uiManager.setTouch(&touch);

  Wire.setClock(100000);
  Wire.setTimeOut(20);

  // Initialize IMU AFTER touch.begin()
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

  // Muat rute navigasi offline dari SD card (jika ada /route.gpx)
  routeNavigator.loadRoute("/route.gpx");

  if (splash)
    splash->setLoadingStatus("Power System...");
  BatteryManager::getInstance().begin();

  // Link GPS to WiFi for Web API
  wifiManager.setGPS(&gpsManager);

  if (splash)
    splash->setLoadingStatus("WiFi Starting...");
  wifiManager.begin();

  // Initialize Bluetooth for HP navigation input
  if (splash)
    splash->setLoadingStatus("Bluetooth...");
  btSerial.begin("MuchRacing-Nav");
  Serial.println(
      "Bluetooth: MuchRacing-Nav siap (pair & kirim POLY:<polyline>)");

  // Initialize Feedback (LEDs/Buzzer)
  if (splash)
    splash->setLoadingStatus("System Ready!");
  FeedbackManager::getInstance().begin();
  FeedbackManager::getInstance().testSequence();

  if (splash)
    splash->setLoadingStatus("MQTT Init...");
  mqttManager.begin();

  // --- LOW BATTERY SAFETY CHECK (20%) ---
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

    ledcWrite(0, 0);
    esp_deep_sleep_start();
  }

  // Start IMU update on Core 0
  xTaskCreatePinnedToCore(imuTask, "IMU_Task", 4096, NULL, 1, NULL, 0);
}

// FreeRTOS Task: IMU Update on Core 0
void imuTask(void *parameter) {
  for (;;) {
    imuManager.update();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void loop() {
  // GPS updates as fast as possible
  gpsManager.update();

  // Throttle UI and Touch to ~100Hz
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

  unsigned long now = millis();

  // Less critical background tasks at 50Hz
  static unsigned long lastBackgroundTask = 0;
  if (now - lastBackgroundTask >= 20) {
    lastBackgroundTask = now;
    BatteryManager::getInstance().update();
    wifiManager.update();
    mqttManager.update();
    FeedbackManager::getInstance().update();
  }

  // --- BLUETOOTH NAVIGATION HANDLER ---
  // Terima polyline dari HP via Bluetooth
  while (btSerial.available()) {
    char c = btSerial.read();
    if (c == '\n' || c == '\r') {
      if (btBuffer.length() > 0) {
        if (btBuffer.startsWith("POLY:")) {
          String polyline = btBuffer.substring(5);
          routeNavigator.decodePolyline(polyline);
          btSerial.println("OK: " + String(routeNavigator.getPointCount()) +
                           " titik");
        } else if (btBuffer.startsWith("STATUS")) {
          btSerial.println(
              "ACTIVE:" + String(routeNavigator.isActive() ? 1 : 0) +
              " POINTS:" + String(routeNavigator.getPointCount()) +
              " KM:" + String(routeNavigator.getRemainingKm(), 1));
        } else {
          btSerial.println("ERR: format salah. Gunakan POLY:<polyline>");
        }
        btBuffer = "";
      }
    } else {
      btBuffer += c;
      if (btBuffer.length() > 2000)
        btBuffer = "";
    }
  }

  // Update Loop Stats
  loopCount++;
  unsigned long loopDuration = micros() - loopStart;
  if (loopDuration > maxLoopTime)
    maxLoopTime = loopDuration;

  if (now - lastStats >= 1000) {
    DEBUG_PRINTF("STATS: FPS=%d | Heap=%d B | MinHeap=%d B | MaxLoop=%lu us\n",
                 loopCount, esp_get_free_heap_size(),
                 esp_get_minimum_free_heap_size(), maxLoopTime);

    lastStats = now;
    loopCount = 0;
    maxLoopTime = 0;
  }
}
