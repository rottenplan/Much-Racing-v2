#include "WiFiManager.h"
#include "BatteryManager.h"
#include "RouteNavigator.h"
#include "SyncManager.h"
#include "web_static.h"
#include <ArduinoJson.h>
#include <Update.h>

extern SyncManager syncManager;
extern RouteNavigator routeNavigator;
extern GPSManager gpsManager;

WiFiManager::WiFiManager() : _server(80) {
  _ssid = "";
  _pass = "";
  _lastAttemptTime = 0;
  _isConnecting = false;
}

void WiFiManager::begin() {
  loadCredentials();
  if (!_enabled) {
    DEBUG_PRINTLN("WiFi: Disabled");
    return;
  }
  if (_apEnabled) {
    WiFi.mode(WIFI_AP_STA);
    startAP();
  } else {
    WiFi.mode(WIFI_STA);
  }

  _server.on("/", HTTP_GET, std::bind(&WiFiManager::handleRoot, this));
  _server.on("/api/live", HTTP_GET,
             std::bind(&WiFiManager::handleApiLive, this));
  _server.on("/rpm", HTTP_GET, std::bind(&WiFiManager::handleRpmPage, this));
  _server.on("/update", HTTP_GET,
             std::bind(&WiFiManager::handleUpdateGet, this));
  _server.on("/sessions", HTTP_GET,
             std::bind(&WiFiManager::handleSessionsPage, this));
  _server.on("/api/sessions", HTTP_GET,
             std::bind(&WiFiManager::handleApiSessions, this));
  _server.on("/download", HTTP_GET,
             std::bind(&WiFiManager::handleDownload, this));

  _server.on(
      "/update", HTTP_POST,
      [this]() { _server.sendHeader("Connection", "close"); },
      [this]() { handleUpdateUpload(); });

  _server.begin();
  DEBUG_PRINTLN("Web Server Started on Port 80");

  if (_ssid.length() > 0) {
    DEBUG_PRINTLN("WiFi: Auto-connecting to " + _ssid);
    WiFi.begin(_ssid.c_str(), _pass.c_str());
    _isConnecting = true;
    _lastAttemptTime = millis();
  }
}

void WiFiManager::startAP() {
  WiFi.softAP("MuchRacing-GPS", "12345678");
  DEBUG_PRINTLN("AP Started — IP: " + WiFi.softAPIP().toString());
}

void WiFiManager::update() {
  if (!_enabled)
    return;
  _server.handleClient();
  if (_isConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      _isConnecting = false;
    } else if (millis() - _lastAttemptTime > 15000) {
      _isConnecting = false;
    }
  }
}

void WiFiManager::handleRoot() {
  _server.sendHeader("Access-Control-Allow-Origin", "*");
  _server.send(200, "text/html", INDEX_HTML);
}

#include "BatteryManager.h"

void WiFiManager::handleApiLive() {
  if (!_gps) {
    _server.send(500, "application/json", "{\"error\":\"No GPS\"}");
    return;
  }
  JsonDocument doc;
  doc["lat"] = _gps->getLatitude();
  doc["lng"] = _gps->getLongitude();
  doc["sats"] = _gps->getSatellites();
  doc["fixed"] = _gps->isFixed();
  String json;
  serializeJson(doc, json);
  _server.sendHeader("Access-Control-Allow-Origin", "*");
  _server.send(200, "application/json", json);
}

void WiFiManager::handleUpdateGet() {
  _server.send(200, "text/html", UPDATE_HTML);
}

void WiFiManager::handleUpdateUpload() {
  HTTPUpload &upload = _server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
      Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
      Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      _server.send(200, "text/plain", "OK Rebooting");
      delay(1000);
      ESP.restart();
    }
  }
}

void WiFiManager::handleRpmPage() { _server.send(200, "text/html", RPM_HTML); }
void WiFiManager::handleSessionsPage() {
  _server.send(200, "text/html", SESSIONS_HTML);
}

void WiFiManager::handleApiSessions() {
  _server.sendHeader("Access-Control-Allow-Origin", "*");
  _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  _server.send(200, "application/json", "[");
  File root = SD.open("/sessions");
  if (!root || !root.isDirectory())
    root = SD.open("/");
  bool first = true;
  if (root) {
    File file = root.openNextFile();
    while (file) {
      String fn = String(file.name());
      if (!file.isDirectory() && (fn.endsWith(".csv") || fn.endsWith(".gpx"))) {
        if (!first)
          _server.sendContent(",");
        first = false;
        JsonDocument d;
        d["name"] = fn;
        d["size"] = String(file.size() / 1024.0, 1) + " KB";
        String out;
        serializeJson(d, out);
        _server.sendContent(out);
      }
      file = root.openNextFile();
    }
    root.close();
  }
  _server.sendContent("]");
  _server.sendContent("");
}

void WiFiManager::handleDownload() {
  if (!_server.hasArg("file")) {
    _server.send(400, "text/plain", "Bad Request");
    return;
  }
  String path = _server.arg("file");
  if (path.indexOf("..") != -1) {
    _server.send(403, "text/plain", "Forbidden");
    return;
  }
  if (SD.exists(path)) {
    File f = SD.open(path, FILE_READ);
    _server.streamFile(f, "application/octet-stream");
    f.close();
  } else {
    _server.send(404, "text/plain", "Not Found");
  }
}

bool WiFiManager::connect(const char *ssid, const char *pass) {
  _ssid = ssid;
  _pass = pass;
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(_ssid.c_str(), _pass.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
    DEBUG_PRINT(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINTLN("\nWiFi: Connected!");
    saveCredentials(_ssid, _pass);
    return true;
  }
  DEBUG_PRINTLN("\nWiFi: Failed");
  return false;
}

int WiFiManager::scanNetworks() {
  WiFi.disconnect();
  delay(100);
  WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks();
  if (n < 0)
    n = 0;
  DEBUG_PRINTF("WiFi: Found %d networks\n", n);
  return n;
}

String WiFiManager::getSSID(int index) { return WiFi.SSID(index); }
int WiFiManager::getRSSI(int index) { return WiFi.RSSI(index); }

void WiFiManager::disconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
}

void WiFiManager::saveCredentials(String ssid, String pass) {
  _ssid = ssid;
  _pass = pass;
  _prefs.begin("laptimer", false);
  _prefs.putString("wifi_ssid", _ssid);
  _prefs.putString("wifi_pass", _pass);
  _prefs.end();
}

void WiFiManager::loadCredentials() {
  _prefs.begin("laptimer", true);
  _enabled = _prefs.getBool("wifi_enabled", true);
  _apEnabled = _prefs.getBool("wifi_ap_enabled", true);
  _liveTelemetryEnabled = _prefs.getBool("live_tel_en", true);
  _ssid = _prefs.getString("wifi_ssid", "");
  _pass = _prefs.getString("wifi_pass", "");
  _prefs.end();
  if (loadFromSD())
    return;
}

bool WiFiManager::loadFromSD() {
  if (SD.cardSize() == 0)
    return false;
  if (SD.exists("/wifi.txt")) {
    File f = SD.open("/wifi.txt", FILE_READ);
    if (f) {
      _ssid = f.readStringUntil('\n');
      _pass = f.readStringUntil('\n');
      _ssid.trim();
      _pass.trim();
      f.close();
      return (_ssid.length() > 0);
    }
  }
  return false;
}

bool WiFiManager::tryAutoConnect() {
  if (_ssid.length() == 0)
    loadCredentials();
  return (_ssid.length() > 0) ? connect(_ssid.c_str(), _pass.c_str()) : false;
}

bool WiFiManager::isConnected() { return WiFi.status() == WL_CONNECTED; }

void WiFiManager::setEnabled(bool enabled) {
  if (_enabled == enabled)
    return;
  _enabled = enabled;
  _prefs.begin("laptimer", false);
  _prefs.putBool("wifi_enabled", _enabled);
  _prefs.end();
  if (enabled)
    begin();
  else {
    _server.stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}

void WiFiManager::setApEnabled(bool enabled) {
  if (_apEnabled == enabled)
    return;
  _apEnabled = enabled;
  _prefs.begin("laptimer", false);
  _prefs.putBool("wifi_ap_enabled", _apEnabled);
  _prefs.end();
  if (_enabled) {
    if (_apEnabled) {
      WiFi.mode(WIFI_AP_STA);
      startAP();
    } else {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
    }
  }
}

void WiFiManager::setLiveTelemetryEnabled(bool enabled) {
  if (_liveTelemetryEnabled == enabled)
    return;
  _liveTelemetryEnabled = enabled;
  _prefs.begin("laptimer", false);
  _prefs.putBool("live_tel_en", _liveTelemetryEnabled);
  _prefs.end();
}
