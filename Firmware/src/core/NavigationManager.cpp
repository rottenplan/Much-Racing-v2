#include "NavigationManager.h"

#include <ArduinoJson.h>
#include <NimBLEDevice.h>

// Global instance (see main.cpp)
NavigationManager navigationManager;

// Nordic UART Service (NUS) - recognized by most BLE serial terminal apps
#define NAV_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NAV_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // write here
#define NAV_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // notify

static NimBLEServer *s_server = nullptr;
static SemaphoreHandle_t s_lock = nullptr;

// ---------------------------------------------------------------------------
// BLE callbacks
// ---------------------------------------------------------------------------
class NavServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server) override {
    Serial.println("NAV: Phone connected");
  }
  void onDisconnect(NimBLEServer *server) override {
    Serial.println("NAV: Phone disconnected");
    // Resume advertising so a new phone can pair again
    NimBLEDevice::startAdvertising();
  }
};

class NavWriteCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic) override {
    std::string value = characteristic->getValue();
    navigationManager._onIncoming(value.data(), value.size());
  }
};

// ---------------------------------------------------------------------------
// NavigationManager
// ---------------------------------------------------------------------------
void NavigationManager::begin() {
  if (s_lock == nullptr)
    s_lock = xSemaphoreCreateMutex();

  NimBLEDevice::init("MuchRacing-Nav");
  // No security/pairing configured: keep it open so any phone can connect
  // instantly.

  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(new NavServerCallbacks());

  NimBLEService *service = s_server->createService(NAV_SERVICE_UUID);

  NimBLECharacteristic *rx =
      service->createCharacteristic(NAV_RX_UUID,
                                    NIMBLE_PROPERTY::WRITE |
                                        NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(new NavWriteCallbacks());
  rx->setValue("");

  // TX characteristic (notify) reserved for future ACK / keep-alive
  NimBLECharacteristic *tx = service->createCharacteristic(
      NAV_TX_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  tx->setValue("");

  service->start();

  // Build an explicit advertisement so phones actually show the device:
  // advertise the service UUID + complete local name (with scan response).
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setName("MuchRacing-Nav");
  adv->addServiceUUID(NAV_SERVICE_UUID);
  adv->setScanResponse(true);
  bool advStarted = NimBLEDevice::startAdvertising();

  _btOn = true;
  Serial.printf("NAV: BLE ready, advertising as 'MuchRacing-Nav' (NUS) -> %s\n",
                advStarted ? "OK" : "FAILED");
}

void NavigationManager::update() {
  // All incoming data is handled inside the BLE write callback or the MQTT
  // callback; nothing to do here. Kept so the main loop call site stays
  // future-proof.
}

bool NavigationManager::isConnected() {
  return _btOn && s_server != nullptr && s_server->getConnectedCount() > 0;
}

bool NavigationManager::hasActiveRoute() {
  if (!_btOn || !_active)
    return false;
  // Stale guidance (no update for 120 s) is treated as inactive.
  if (millis() - _lastUpdateMs > 120000UL)
    return false;
  return true;
}

void NavigationManager::clearRoute() {
  if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    _active = false;
    _source = NAV_SOURCE_NONE;
    _maneuver = MANEUVER_STRAIGHT;
    _distanceM = -1;
    _instruction = "";
    _rxBuffer = "";
    xSemaphoreGive(s_lock);
  }
  Serial.println("NAV: Route cleared");
}

// Called from the NimBLE task (not an ISR, but still must be fast).
void NavigationManager::_onIncoming(const char *data, size_t len) {
  if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    for (size_t i = 0; i < len; i++) {
      char c = data[i];
      if (c == '\n' || c == '\r') {
        if (_rxBuffer.length() > 0) {
          handleLine(_rxBuffer, NAV_SOURCE_BLE);
          _rxBuffer = "";
        }
      } else {
        if (_rxBuffer.length() < 512)
          _rxBuffer += c;
      }
    }
    xSemaphoreGive(s_lock);
  }
}

// Public entry for MQTT (or any other source) - takes the lock itself.
void NavigationManager::ingestLine(const String &line, NavSource source) {
  if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    handleLine(line, source);
    xSemaphoreGive(s_lock);
  }
  if (source == NAV_SOURCE_MQTT) {
    Serial.printf("NAV(MQTT): %.60s\n", line.c_str());
  }
}

void NavigationManager::handleLine(String line, NavSource source) {
  line.trim();
  if (line.length() == 0)
    return;
  parseJson(line, source);
}

void NavigationManager::parseJson(const String &line, NavSource source) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    // Ignore non-JSON garbage (echoes, chat messages, etc.)
    return;
  }

  // Special events first
  const char *event = doc["event"] | "";
  if (strcmp(event, "clear") == 0) {
    _active = false;
    _maneuver = MANEUVER_STRAIGHT;
    _distanceM = -1;
    _instruction = "";
    _source = source;
    return;
  } else if (strcmp(event, "arrive") == 0) {
    _active = true;
    _maneuver = MANEUVER_ARRIVE;
    _distanceM = 0;
    _instruction = doc["text"] | "ARRIVED";
    _source = source;
    _lastUpdateMs = millis();
    return;
  }

  // Regular maneuver update
  _active = true;
  _maneuver = doc["icon"] | MANEUVER_STRAIGHT;
  if (_maneuver < 0 || _maneuver > 9)
    _maneuver = MANEUVER_STRAIGHT;
  _distanceM = doc["dist"] | -1L;
  const char *text = doc["text"] | "";
  _instruction = String(text);
  _source = source;
  _lastUpdateMs = millis();
}

int NavigationManager::getManeuver() {
  int v = MANEUVER_STRAIGHT;
  if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    v = _maneuver;
    xSemaphoreGive(s_lock);
  }
  return v;
}

long NavigationManager::getDistanceM() {
  long v = -1;
  if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    v = _distanceM;
    xSemaphoreGive(s_lock);
  }
  return v;
}

String NavigationManager::getInstruction() {
  String v;
  if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    v = _instruction;
    xSemaphoreGive(s_lock);
  }
  return v;
}

unsigned long NavigationManager::getLastUpdateMs() {
  unsigned long v = 0;
  if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    v = _lastUpdateMs;
    xSemaphoreGive(s_lock);
  }
  return v;
}

int NavigationManager::getSource() {
  int v = NAV_SOURCE_NONE;
  if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    v = (int)_source;
    xSemaphoreGive(s_lock);
  }
  return v;
}
