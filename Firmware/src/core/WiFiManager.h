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
  void setGPS(GPSManager *gps) { _gps = gps; } // Inject GPS

  // Connection Methods
  bool connect(const char *ssid, const char *pass);
  int scanNetworks(); // Returns count of found networks
  String getSSID(int index);
  int getRSSI(int index);
  void connectTo(int index, const char *pass); // Connect by index
  bool tryAutoConnect();                       // Try with saved credentials
  void disconnect();

  // Persistence
  void saveCredentials(String ssid, String pass);
  void loadCredentials();

  // Status
  bool isConnected();
  String getSSID() { return _ssid; }

  // Toggle
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

  WebServer _server; // Web Server Object
  GPSManager *_gps = nullptr;

  bool _enabled = true;              // Default ON
  bool _apEnabled = true;            // Default ON Hotspot
  bool _liveTelemetryEnabled = true; // Default ON Live Telemetry

  bool loadFromSD();
  void startAP(); // Start Hotspot
  void handleRoot();
  void handleApiLive();
  void handleUpdateGet();
  void handleUpdatePost();
  void handleUpdateUpload();

  void handleRpmPage();
  void handleSessionsPage();
  void handleApiSessions();
  void handleDownload();
};

#endif
