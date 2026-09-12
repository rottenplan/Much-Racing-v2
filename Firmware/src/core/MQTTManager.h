#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include "../config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

class MQTTManager {
public:
  MQTTManager();
  void begin();
  void update();
  bool isConnected();

  // Publish telemetry
  bool publishTelemetry(float lat, float lon, float speed, float rpm, int sats,
                        float bat_v, int bat_p);

  // Navigation subscription (JSON TBT messages)
  String getNavTopic() { return _navTopic; }

private:
  WiFiClient _wifiClient;
  PubSubClient _client;
  String _clientId;
  String _baseTopic;
  String _navTopic;
  unsigned long _lastReconnectAttempt;

  bool reconnect();
};

extern MQTTManager mqttManager;

#endif
