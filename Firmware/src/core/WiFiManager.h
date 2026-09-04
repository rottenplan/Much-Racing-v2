#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "GPSManager.h"
#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>

class WiFiManager {
public:
  WiFiManager();
  void begin();
  void update();
  void setGPS(GPSManager *gps) { _gps = gps; }

  bool connect(const char *ssid, const char *pass);
  int scanNetworks();
  String getSSID(int index);
  int getRSSI(int index);
  void connectTo(int index, const char *pass);
  bool tryAutoConnect();
  void disconnect();

  void saveCredentials(String ssid, String pass);
  void loadCredentials();

  bool isConnected();
  String getSSID() { return _ssid; }

  void setEnabled(bool enabled);
  bool isEnabled() { return _enabled; }

  void setApEnabled(bool enabled);
  bool isApEnabled() { return _apEnabled; }

  void setLiveTelemetryEnabled(bool enabled);
  bool isLiveTelemetryEnabled() { return _liveTelemetryEnabled; }

private:
  String _ssid;
  String _pass;
  Preferences _prefs;
  unsigned long _lastAttemptTime;
  bool _isConnecting;

  WebServer _server;
  GPSManager *_gps = nullptr;

  bool _enabled = true;
  bool _apEnabled = true;
  bool _liveTelemetryEnabled = true;

  bool loadFromSD();
  void startAP();
  void handleRoot();
  void handleApiLive();
  void handleUpdateGet();
  void handleUpdatePost();
  void handleUpdateUpload();
  void handleRpmPage();
  void handleSessionsPage();
  void handleApiSessions();
  void handleDownload();
  void handleNavPage();
  void handleNavSet();
  void handleNavStatus();
};

#endif