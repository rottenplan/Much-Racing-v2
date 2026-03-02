#include "MQTTManager.h"

MQTTManager::MQTTManager() : _client(_wifiClient), _lastReconnectAttempt(0) {
  uint64_t chipId = ESP.getEfuseMac();
  char chipIdStr[13];
  snprintf(chipIdStr, sizeof(chipIdStr), "%04X%08X", (uint16_t)(chipId >> 32),
           (uint32_t)chipId);

  _clientId = "muchrace-" + String(chipIdStr);
  _baseTopic = String(MQTT_TOPIC_PREFIX) + String(chipIdStr);
}

void MQTTManager::begin() {
  _client.setServer(MQTT_BROKER, MQTT_PORT);
  // No callback needed for now as we only publish
}

void MQTTManager::update() {
  if (!_client.connected()) {
    unsigned long now = millis();
    if (now - _lastReconnectAttempt > 5000) {
      _lastReconnectAttempt = now;
      if (reconnect()) {
        _lastReconnectAttempt = 0;
      }
    }
  } else {
    _client.loop();
  }
}

bool MQTTManager::isConnected() { return _client.connected(); }

bool MQTTManager::reconnect() {
  if (WiFi.status() != WL_CONNECTED)
    return false;

  Serial.print("MQTT: Connecting as ");
  Serial.println(_clientId);

  if (_client.connect(_clientId.c_str())) {
    Serial.println("MQTT: Connected!");
    return true;
  } else {
    Serial.print("MQTT: Failed, rc=");
    Serial.println(_client.state());
    return false;
  }
}

bool MQTTManager::publishTelemetry(float lat, float lon, float speed, float rpm,
                                   int sats, float bat_v, int bat_p) {
  if (!_client.connected())
    return false;

  // Use StaticJsonDocument to avoid heap allocation
  StaticJsonDocument<256> doc;
  doc["lat"] = serialized(String(lat, 6));
  doc["lng"] = serialized(String(lon, 6));
  doc["speed"] = serialized(String(speed, 1));
  doc["rpm"] = (int)rpm;
  doc["sats"] = sats;
  doc["vbat"] = serialized(String(bat_v, 2));
  doc["pbat"] = bat_p;
  doc["ts"] = millis();

  char buffer[256];
  size_t n = serializeJson(doc, buffer);

  return _client.publish(_baseTopic.c_str(), (uint8_t *)buffer, n, false);
}

MQTTManager mqttManager;
