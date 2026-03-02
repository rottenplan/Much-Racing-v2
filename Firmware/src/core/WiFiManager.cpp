#include "WiFiManager.h"
#include "BatteryManager.h"
#include "SyncManager.h"
#include "web_static.h"
#include <ArduinoJson.h>
#include <Update.h>

extern SyncManager syncManager;

WiFiManager::WiFiManager() : _server(80) {
  _ssid = "";
  _pass = "";
  _lastAttemptTime = 0;
  _isConnecting = false;
}

void WiFiManager::begin() {
  loadCredentials();

  if (!_enabled) {
    DEBUG_PRINTLN("WiFi: Disabled by user settings.");
    return;
  }

  // Configure Mode
  if (_enabled) {
    if (_apEnabled) {
      WiFi.mode(WIFI_AP_STA);
      startAP();
    } else {
      WiFi.mode(WIFI_STA);
    }
  }

  // Start Web Server
  _server.on("/", HTTP_GET, std::bind(&WiFiManager::handleRoot, this));
  _server.on("/api/live", HTTP_GET,
             std::bind(&WiFiManager::handleApiLive, this));

  // OTA Update Routes
  _server.on("/update", HTTP_GET,
             std::bind(&WiFiManager::handleUpdateGet, this));

  // Session Manager Routes
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

  // Try Auto Connect if creds exist
  if (_ssid.length() > 0) {
    DEBUG_PRINTLN("WiFi: Auto-connecting to " + _ssid);
    if (_apEnabled)
      WiFi.mode(WIFI_AP_STA);
    else
      WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid.c_str(), _pass.c_str());
    _isConnecting = true;
    _lastAttemptTime = millis();
  } else {
    DEBUG_PRINTLN("WiFi: No saved credentials. Running AP Only.");
  }
}

void WiFiManager::startAP() {
  const char *apSSID = "MuchRacing-GPS";
  const char *apPass = "12345678";

  WiFi.softAP(apSSID, apPass);

  DEBUG_PRINTLN("AP Started: " + String(apSSID));
  DEBUG_PRINTLN("IP Address: " + WiFi.softAPIP().toString());
}

void WiFiManager::update() {
  if (!_enabled)
    return;

  _server.handleClient(); // Handle Web Requests

  if (_isConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINTLN("WiFi: Connected successfully!");
      _isConnecting = false;
    } else if (millis() - _lastAttemptTime > 15000) {
      DEBUG_PRINTLN("WiFi: Auto-connect timeout.");
      _isConnecting = false;
    }
  }

  // Live Telemetry Push
  static unsigned long lastLivePush = 0;
  unsigned long now = millis();
  if (_enabled && isConnected() && _gps && _gps->isFixed() &&
      _liveTelemetryEnabled) {
    if (now - lastLivePush >= 1000) { // Push every 1 second
      lastLivePush = now;
      BatteryManager &bat = BatteryManager::getInstance();
      syncManager.queueTelemetry(_gps->getLatitude(), _gps->getLongitude(),
                                 _gps->getSpeedKmph(), _gps->getRPM(),
                                 _gps->getSatellites(), bat.getVoltage(),
                                 bat.getPercentage());
    }
  } else if (_liveTelemetryEnabled && now - lastLivePush >= 5000) {
    // Diagnostic log every 5 seconds if enabled but conditions not met
    lastLivePush = now;
    if (!_enabled)
      DEBUG_PRINTLN("Telemetry: Skipped - WiFi Disabled.");
    else if (!isConnected())
      DEBUG_PRINTLN("Telemetry: Skipped - WiFi Not Connected.");
    else if (!_gps)
      DEBUG_PRINTLN("Telemetry: Skipped - No GPS Manager.");
    else if (!_gps->isFixed())
      DEBUG_PRINTLN("Telemetry: Skipped - No GPS Fix.");
  }
}

void WiFiManager::handleRoot() {
  _server.sendHeader("Access-Control-Allow-Origin", "*");
  _server.send(200, "text/html", INDEX_HTML);
}

#include "BatteryManager.h"

// ... inside handleApiLive ...

void WiFiManager::handleApiLive() {
  // Serial.println("API Live: Request Received"); // Debug spam
  if (!_gps) {
    DEBUG_PRINTLN("API Live: Error - No GPS Linked!");
    _server.send(500, "application/json",
                 "{\"error\":\"No GPS Manager Linked\"}");
    return;
  }

  JsonDocument doc;
  doc["speed"] = _gps->getSpeedKmph();
  doc["rpm"] = _gps->getRPM();
  doc["trip"] = _gps->getTotalTrip();
  doc["sats"] = _gps->getSatellites();
  doc["lat"] = _gps->getLatitude();
  doc["lng"] = _gps->getLongitude();

  // Add Battery Data
  BatteryManager &bat = BatteryManager::getInstance();
  doc["bat_voltage"] = bat.getVoltage();
  doc["bat_percent"] = bat.getPercentage();
  doc["is_charging"] = bat.isCharging();

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
    DEBUG_PRINTF("Update: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { // start with max available size
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    /* flashing firmware to ESP*/
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) { // true to set the size to the current progress
      DEBUG_PRINTF("Update Success: %u\nRebooting...\n", upload.totalSize);
      _server.send(200, "text/plain", "Update Success! Rebooting...");
      delay(1000);
      ESP.restart();
    }
  }
}

void WiFiManager::handleSessionsPage() {
  _server.send(200, "text/html", SESSIONS_HTML);
}

void WiFiManager::handleApiSessions() {
  _server.sendHeader("Access-Control-Allow-Origin", "*");
  _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  _server.send(200, "application/json", "[");

  File root = SD.open("/sessions");
  if (!root || !root.isDirectory()) {
    root = SD.open("/");
  }

  bool first = true;
  if (root) {
    File file = root.openNextFile();
    while (file) {
      String fileName = String(file.name());
      if (!file.isDirectory() &&
          (fileName.endsWith(".csv") || fileName.endsWith(".gpx"))) {

        if (!first)
          _server.sendContent(",");
        first = false;

        JsonDocument itemDoc;
        int lastSlash = fileName.lastIndexOf('/');
        String cleanName =
            (lastSlash >= 0) ? fileName.substring(lastSlash + 1) : fileName;

        itemDoc["name"] = cleanName;

        float kb = file.size() / 1024.0;
        if (kb > 1024)
          itemDoc["size"] = String(kb / 1024.0, 2) + " MB";
        else
          itemDoc["size"] = String(kb, 1) + " KB";

        String fullPath = String(root.path()) + "/" + cleanName;
        if (String(root.path()) == "/")
          fullPath = "/" + cleanName;
        itemDoc["path"] = fullPath;

        String itemOutput;
        serializeJson(itemDoc, itemOutput);
        _server.sendContent(itemOutput);
      }
      file = root.openNextFile();
    }
    root.close();
  }

  _server.sendContent("]");
  _server.sendContent(""); // Finalize chunked encoding
}

void WiFiManager::handleDownload() {
  if (!_server.hasArg("file")) {
    _server.send(400, "text/plain", "Bad Request");
    return;
  }

  String path = _server.arg("file");
  // Basic security: No parent dir traversing
  if (path.indexOf("..") != -1) {
    _server.send(403, "text/plain", "Forbidden");
    return;
  }

  if (SD.exists(path)) {
    File file = SD.open(path, FILE_READ);
    _server.streamFile(file, "application/octet-stream");
    file.close();
  } else {
    _server.send(404, "text/plain", "File Not Found");
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
    DEBUG_PRINTLN("\nWiFi: Manual connection success!");
    saveCredentials(_ssid, _pass);
    return true;
  } else {
    DEBUG_PRINTLN("\nWiFi: Manual connection failed.");
    return false;
  }
}

int WiFiManager::scanNetworks() {
  DEBUG_PRINTLN("WiFi: Scanning...");
  WiFi.disconnect(); // Stop any pending connection attempts
  delay(100);
  WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks();

  // Handle error codes
  if (n < 0) {
    DEBUG_PRINTF("WiFi: Scan failed with code %d\n", n);
    n = 0;
  }

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

  if (SD.cardSize() > 0) {
    if (SD.exists("/wifi.txt") || SD.open("/wifi.txt", FILE_WRITE)) {
      File f = SD.open("/wifi.txt", FILE_WRITE);
      if (f) {
        f.println(_ssid);
        f.println(_pass);
        f.close();
      }
    }
  }
}

void WiFiManager::loadCredentials() {
  _prefs.begin("laptimer", true);
  _enabled = _prefs.getBool("wifi_enabled", true);             // Default to ON
  _apEnabled = _prefs.getBool("wifi_ap_enabled", true);        // Default to ON
  _liveTelemetryEnabled = _prefs.getBool("live_tel_en", true); // Default to ON
  _ssid = _prefs.getString("wifi_ssid", "");
  _pass = _prefs.getString("wifi_pass", "");
  _prefs.end();

  if (loadFromSD()) {
    // SD Card Overrides stored SSID/Pass, but not enabled state (unless we add
    // it there too, but let's keep it simple)
    return;
  }
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
      if (_ssid.length() > 0)
        return true;
    }
  }
  return false;
}

bool WiFiManager::tryAutoConnect() {
  if (_ssid.length() == 0) {
    loadCredentials();
  }

  if (_ssid.length() > 0) {
    // Use connect logic but standard blocking or async?
    // Re-using connect() which is blocking-ish (loop with delay)
    return connect(_ssid.c_str(), _pass.c_str());
  }
  return false;
}

bool WiFiManager::isConnected() { return WiFi.status() == WL_CONNECTED; }

void WiFiManager::setEnabled(bool enabled) {
  if (_enabled == enabled)
    return;
  _enabled = enabled;

  _prefs.begin("laptimer", false);
  _prefs.putBool("wifi_enabled", _enabled);
  _prefs.end();

  if (_enabled) {
    begin(); // Re-run begin to start everything up
  } else {
    DEBUG_PRINTLN("WiFi: Disabling...");
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
      DEBUG_PRINTLN("WiFi: Enabling Hotspot...");
      WiFi.mode(WIFI_AP_STA);
      startAP();
    } else {
      DEBUG_PRINTLN("WiFi: Disabling Hotspot...");
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
