#include "SyncManager.h"
#include "../../config.h"
#include "MQTTManager.h"
#include "SessionManager.h"
#include <WiFiClientSecure.h>
#include <base64.h>
#include <time.h>

extern SessionManager sessionManager;

SyncManager::SyncManager() : _isBusy(false) {
  // Create queue for background telemetry
  _telemetryQueue = xQueueCreate(10, sizeof(TelemetryData *));

  // Start background task on Core 0
  xTaskCreatePinnedToCore(telemetryTask, "TelemetryTask", 8192, this, 1,
                          &_telemetryTaskHandle, 0);
}

bool SyncManager::isFirstSyncDone() {
  _prefs.begin("sync", false);
  bool done = _prefs.getBool("first_sync_done", false);
  _prefs.end();
  return done;
}

String SyncManager::getLastSyncTime() {
  _prefs.begin("sync", false);
  String lastSync = _prefs.getString("last_sync", "Never");
  _prefs.end();
  return lastSync;
}

String SyncManager::makeBasicAuthHeader(const char *username,
                                        const char *password) {
  String credentials = String(username) + ":" + String(password);
  String encoded = base64::encode(credentials);
  return "Basic " + encoded;
}

bool SyncManager::performFirstSync(const char *apiUrl, const char *username,
                                   const char *password) {
  if (isFirstSyncDone()) {
    Serial.println("Sync: First sync already completed.");
    return true;
  }

  Serial.println("Sync: Performing first sync...");
  bool success = syncSettings(apiUrl, username, password);

  if (success) {
    // Save credentials to muchrace namespace before marking sync complete
    Preferences prefs;
    prefs.begin("muchrace", false);
    prefs.putString("username", String(username));
    prefs.putString("password", String(password));
    prefs.end();

    markSyncComplete();
    Serial.println("Sync: First sync completed successfully!");
  } else {
    Serial.println("Sync: First sync failed.");
  }

  return success;
}

bool SyncManager::syncSettings(const char *apiUrl, const char *username,
                               const char *password) {
  _isBusy = true;
  _lastError = "";
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sync: WiFi not connected.");
    _isBusy = false;
    return false;
  }
  Serial.print("Sync: WiFi Connected. Device IP: ");
  Serial.println(WiFi.localIP());

  String authHeader = makeBasicAuthHeader(username, password);
  bool success = downloadAndApplySettings(apiUrl, authHeader.c_str());

  if (success) {
    downloadTracks(apiUrl, authHeader.c_str());
    saveLastSyncTime();
  }

  _isBusy = false;
  return success;
}

bool SyncManager::downloadAndApplySettings(const char *apiUrl,
                                           const char *authHeader) {
  bool isHttps = String(apiUrl).startsWith("https://");
  WiFiClient *client;
  WiFiClientSecure secureClient;
  WiFiClient plainClient;

  if (isHttps) {
    secureClient.setInsecure();
    secureClient.setHandshakeTimeout(30000);
    client = &secureClient;
  } else {
    client = &plainClient;
  }

  HTTPClient http;
  http.setTimeout(15000);

  uint64_t totalBytes = SD.totalBytes();
  uint64_t usedBytes = SD.usedBytes();
  int totalMB = (int)(totalBytes / (1024 * 1024));
  int usedMB = (int)(usedBytes / (1024 * 1024));

  uint64_t chipId = ESP.getEfuseMac();
  char chipIdStr[13];
  snprintf(chipIdStr, sizeof(chipIdStr), "%04X%08X", (uint16_t)(chipId >> 32),
           (uint32_t)chipId);
  String url = String(apiUrl) + "?storage_used=" + String(usedMB) +
               "&storage_total=" + String(totalMB) +
               "&deviceId=" + String(chipIdStr);

  Serial.print("Sync: Connecting to ");
  Serial.println(url);

  if (http.begin(*client, url)) {
    http.addHeader("Authorization", authHeader);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("Sync: Response received");

      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        _lastError = "JSON Error";
        Serial.print("Sync: JSON parse error: ");
        Serial.println(error.c_str());
        http.end();
        return false;
      }

      if (!doc["success"].as<bool>()) {
        _lastError = doc["message"] | "API Error";
        Serial.println("Sync: API returned success=false");
        http.end();
        return false;
      }

      applySettings(doc);
      applyTrackSelection(doc);
      applyEngineSettings(doc);
      applyProfileData(doc); // Cache profile data like driverNumber

      http.end();
      return true;

    } else if (httpCode == HTTP_CODE_UNAUTHORIZED) {
      _lastError = "Unauthorized (401)";
      Serial.println("Sync: Authentication failed (401)");
    } else {
      _lastError = "HTTP Error: " + String(httpCode);
      Serial.print("Sync: HTTP error: ");
      Serial.println(httpCode);
    }
    http.end();
  } else {
    _lastError = "Host Unreachable";
    Serial.println("Sync: Connection failed");
  }

  return false;
}

void SyncManager::applySettings(JsonDocument &doc) {
  if (!doc["data"]["settings"].isNull()) {
    JsonObject settings = doc["data"]["settings"];

    _prefs.begin("laptimer", false);

    String units = settings["units"].as<String>();
    _prefs.putInt("units", units == "kmh" ? 0 : 1);

    int brightness = settings["brightness"].as<int>();
    int brightnessIdx = map(brightness, 10, 100, 0, 9);
    _prefs.putInt("brightness", brightnessIdx);

    int powerSave = settings["powerSave"].as<int>();
    int powerSaveIdx = 1;
    switch (powerSave) {
    case 1:
      powerSaveIdx = 0;
      break;
    case 5:
      powerSaveIdx = 1;
      break;
    case 10:
      powerSaveIdx = 2;
      break;
    case 30:
      powerSaveIdx = 3;
      break;
    case 0:
      powerSaveIdx = 4;
      break;
    }
    _prefs.putInt("power_save", powerSaveIdx);

    int contrast = settings["contrast"].as<int>();
    _prefs.putInt("contrast", contrast);

    _prefs.end();
    Serial.println("Sync: Device settings applied");
  }
}

void SyncManager::applyTrackSelection(JsonDocument &doc) {
  if (!doc["data"]["tracks"].isNull()) {
    JsonObject tracks = doc["data"]["tracks"];
    _prefs.begin("tracks", false);

    JsonArray countries = tracks["countries"];
    String countryList = "";
    for (JsonVariant country : countries) {
      if (countryList.length() > 0)
        countryList += ",";
      countryList += country.as<String>();
    }

    _prefs.putString("countries", countryList);
    _prefs.putInt("track_count", tracks["trackCount"].as<int>());
    _prefs.end();

    Serial.print("Sync: Track selection applied - ");
    Serial.print(tracks["trackCount"].as<int>());
    Serial.println(" tracks");
  }
}

void SyncManager::applyEngineSettings(JsonDocument &doc) {
  if (!doc["data"]["engines"].isNull()) {
    JsonArray engines = doc["data"]["engines"];
    _prefs.begin("laptimer", false);

    int activeEngine = doc["data"]["activeEngine"].as<int>();
    _prefs.putInt("active_engine", activeEngine);

    for (JsonVariant engine : engines) {
      if (engine["id"].as<int>() == activeEngine) {
        float hours = engine["hours"].as<float>();
        _prefs.putFloat("engine_hours", hours);
        Serial.print("Sync: Engine hours set to ");
        Serial.println(hours);
        break;
      }
    }
    _prefs.end();
    Serial.println("Sync: Engine settings applied");
  }
}

void SyncManager::applyProfileData(JsonDocument &doc) {
  if (!doc["data"]["profile"].isNull()) {
    JsonObject profile = doc["data"]["profile"];

    _prefs.begin("muchrace", false);

    // Cache driver number for display on About Device screen
    int driverNumber = profile["driverNumber"].as<int>();
    _prefs.putInt("driver_number", driverNumber);

    Serial.print("Sync: Driver number cached: #");
    Serial.println(driverNumber);

    _prefs.end();
  }
}

void SyncManager::markSyncComplete() {
  _prefs.begin("sync", false);
  _prefs.putBool("first_sync_done", true);
  _prefs.end();
  loadCredentialCache(); // Refresh cache with newly saved credentials
}

void SyncManager::saveLastSyncTime() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  char timeStr[25];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S", &timeinfo);

  _prefs.begin("sync", false);
  _prefs.putString("last_sync", String(timeStr));
  _prefs.end();

  Serial.print("Sync: Last sync time saved: ");
  Serial.println(timeStr);
}

void SyncManager::triggerManualSync() {
  // Manual trigger - clear cache to force reload if credentials changed
  loadCredentialCache();
  Serial.println("Sync: Manual sync triggered");
}

void SyncManager::loadCredentialCache() {
  _prefs.begin("muchrace", true);
  _cachedUser = _prefs.getString("username", "");
  _cachedPass = _prefs.getString("password", "");
  _prefs.end();

  // Also cache API URL from config if not custom?
  // Current implementation uses API_URL from config.h in WiFiManager.cpp
  _cachedApiUrl = API_URL;
}

void SyncManager::queueTelemetry(float lat, float lon, float speed, float rpm,
                                 int sats, float bat_v, int bat_p) {
  if (_cachedUser.length() == 0 || _cachedPass.length() == 0)
    return;

  TelemetryData *data = new TelemetryData();
  data->apiUrl = _cachedApiUrl;
  data->username = _cachedUser;
  data->password = _cachedPass;
  data->lat = lat;
  data->lon = lon;
  data->speed = speed;
  data->rpm = rpm;
  data->sats = sats;
  data->bat_v = bat_v;
  data->bat_p = bat_p;

  if (xQueueSend(_telemetryQueue, &data, 0) != pdTRUE) {
    delete data; // Queue full, discard
  }
}

void SyncManager::telemetryTask(void *pvParameters) {
  SyncManager *self = (SyncManager *)pvParameters;
  TelemetryData *data;

  while (true) {
    if (xQueueReceive(self->_telemetryQueue, &data, portMAX_DELAY) == pdTRUE) {
      if (WiFi.status() == WL_CONNECTED) {
        // Try MQTT first for low latency
        bool mqttOk = mqttManager.publishTelemetry(
            data->lat, data->lon, data->speed, data->rpm, data->sats,
            data->bat_v, data->bat_p);

        if (!mqttOk) {
          // Fallback to HTTP Polling if MQTT is not ready/connected
          self->pushLiveTelemetry(data->apiUrl.c_str(), data->username.c_str(),
                                  data->password.c_str(), data->lat, data->lon,
                                  data->speed, data->rpm, data->sats,
                                  data->bat_v, data->bat_p);
        }
      }
      delete data;
    }
  }
}

bool SyncManager::uploadSessions(const char *apiUrl, const char *username,
                                 const char *password) {
  _isBusy = true;
  if (WiFi.status() != WL_CONNECTED) {
    _isBusy = false;
    return false;
  }

  if (!SD.exists("/history.csv")) {
    _isBusy = false;
    return true; // No history to upload
  }

  File historyFile = SD.open("/history.csv", FILE_READ);
  if (!historyFile) {
    _isBusy = false;
    return false;
  }

  bool allSuccess = true;
  while (historyFile.available()) {
    yield();
    String line = historyFile.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
      continue;

    // Format: /sessions/run_X.csv,Date,LapCount,BestLap,Type
    int c1 = line.indexOf(',');
    if (c1 == -1)
      continue;

    String filename = line.substring(0, c1);

    // Parse session type from column 5 (after 4 commas)
    String sessionType = "TRACK"; // default
    int c2 = line.indexOf(',', c1 + 1);
    int c3 = (c2 != -1) ? line.indexOf(',', c2 + 1) : -1;
    int c4 = (c3 != -1) ? line.indexOf(',', c3 + 1) : -1;
    if (c4 != -1) {
      sessionType = line.substring(c4 + 1);
      sessionType.trim();
    }

    // Check if checks/uploads should happen
    if (isSessionSynced(filename)) {
      Serial.print("Sync: Skipping already uploaded session: ");
      Serial.println(filename);
      continue;
    }

    if (SD.exists(filename)) {
      if (uploadSingleSession(apiUrl, username, password, filename,
                              sessionType)) {
        markSessionSynced(filename);
      } else {
        allSuccess = false;
      }
    }
  }
  historyFile.close();
  _isBusy = false;
  return allSuccess;
}

// Check if a session has been marked as uploaded in /synced.list
bool SyncManager::isSessionSynced(String filename) {
  if (!SD.exists("/synced.list")) {
    return false;
  }

  File file = SD.open("/synced.list", FILE_READ);
  if (!file)
    return false;

  bool found = false;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line == filename) {
      found = true;
      break;
    }
  }
  file.close();
  return found;
}

// Mark session as synced by appending to /synced.list
void SyncManager::markSessionSynced(String filename) {
  File file = SD.open("/synced.list", FILE_APPEND);
  if (file) {
    file.println(filename);
    file.close();
    Serial.print("Sync: Marked as synced: ");
    Serial.println(filename);
  } else {
    Serial.println("Sync: Failed to update synced.list");
  }
}

bool SyncManager::downloadTracks(const char *apiUrl, const char *authHeader) {
  String url = String(apiUrl);
  int idx = url.indexOf("/api/device/sync");
  if (idx > 0) {
    url = url.substring(0, idx) + "/api/tracks/list";
  } else {
    Serial.println("Sync: Invalid API URL format for tracks");
    return false;
  }

  WiFiClient *client;
  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  bool isHttps = url.startsWith("https://");

  if (isHttps) {
    secureClient.setInsecure();
    secureClient.setHandshakeTimeout(30000);
    client = &secureClient;
  } else {
    client = &plainClient;
  }

  HTTPClient http;
  http.setTimeout(15000);

  Serial.print("Sync: Downloading tracks from ");
  Serial.println(url);

  if (http.begin(*client, url)) {
    http.addHeader("Authorization", authHeader);

    int httpCode = http.GET();
    bool success = false;

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString(); // Read all into memory to parse

      // Save JSON first
      File file = SD.open("/tracks.json", FILE_WRITE);
      if (file) {
        file.print(payload);
        file.close();
        Serial.println("Sync: Tracks saved to /tracks.json");
        success = true;
      } else {
        Serial.println("Sync: Failed to open /tracks.json for writing");
      }

      // Parse to find CSVs to download
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      if (!error && doc["tracks"].is<JsonArray>()) {
        JsonArray tracks = doc["tracks"];
        for (JsonVariant t : tracks) {
          String id = t["id"].as<String>();
          String pathFile = t["pathFile"].as<String>();

          if (id.length() > 0 && pathFile.length() > 0) {
            if (!SD.exists(pathFile)) {
              Serial.print("Sync: Downloading CSV for track " + id + "...");

              // Construct Download URL: /api/tracks/[id]/download
              // Base API URL is like .../api/device/sync
              // We need .../api/tracks/[id]/download
              String downloadUrl = String(apiUrl);
              int idx = downloadUrl.indexOf("/api/device/sync");
              if (idx > 0) {
                downloadUrl = downloadUrl.substring(0, idx) + "/api/tracks/" +
                              id + "/download";

                HTTPClient csvHttp;
                csvHttp.setTimeout(10000);
                if (csvHttp.begin(*client, downloadUrl)) {
                  csvHttp.addHeader("Authorization", authHeader);
                  int csvCode = csvHttp.GET();
                  if (csvCode == HTTP_CODE_OK) {
                    // Ensure directory exists
                    if (!SD.exists("/tracks"))
                      SD.mkdir("/tracks");

                    File csvFile = SD.open(pathFile, FILE_WRITE);
                    if (csvFile) {
                      csvHttp.writeToStream(&csvFile);
                      csvFile.close();
                      Serial.println("OK");
                    } else {
                      Serial.println("Failed to write file");
                    }
                  } else {
                    Serial.print("Failed (HTTP ");
                    Serial.print(csvCode);
                    Serial.println(")");
                  }
                  csvHttp.end();
                }
              }
            }
          }
        }
      }

    } else {
      Serial.print("Sync: HTTP error downloading tracks: ");
      Serial.println(httpCode);
    }
    http.end();
    return success;
  }
  return false;
}

bool SyncManager::uploadGPXTracks(const char *apiUrl, const char *username,
                                  const char *password) {
  _isBusy = true;
  if (WiFi.status() != WL_CONNECTED) {
    _isBusy = false;
    return false;
  }

  String authHeader = makeBasicAuthHeader(username, password);

  String url = String(apiUrl);
  int idx = url.indexOf("/api/device/sync");
  if (idx > 0) {
    url = url.substring(0, idx) + "/api/tracks/upload";
  } else {
    Serial.println("Sync: Invalid API URL format for track upload");
    _isBusy = false;
    return false;
  }

  Serial.println("Sync: Scanning for GPX tracks in /tracks/...");

  if (!SD.exists("/tracks")) {
    Serial.println("Sync: /tracks directory not found.");
    _isBusy = false;
    return true;
  }

  File root = SD.open("/tracks");
  if (!root || !root.isDirectory()) {
    Serial.println("Sync: Failed to open /tracks directory.");
    _isBusy = false;
    return false;
  }

  bool allSuccess = true;
  File file = root.openNextFile();

  while (file) {
    yield();
    String filename = String(file.name());

    if (!file.isDirectory() && filename.endsWith(".gpx")) {
      Serial.print("Sync: Found track: ");
      Serial.println(filename);

      String gpxData = file.readString();

      WiFiClient *client;
      WiFiClientSecure secureClient;
      WiFiClient plainClient;
      bool isHttps = url.startsWith("https://");

      if (isHttps) {
        secureClient.setInsecure();
        secureClient.setHandshakeTimeout(30000);
        client = &secureClient;
      } else {
        client = &plainClient;
      }

      HTTPClient http;
      http.setTimeout(15000);

      if (http.begin(*client, url)) {
        http.addHeader("Authorization", authHeader);
        http.addHeader("Content-Type", "application/json");

        JsonDocument doc;
        doc["filename"] = filename;
        doc["gpx_data"] = gpxData;

        String jsonPayload;
        serializeJson(doc, jsonPayload);

        Serial.println("Sync: Uploading...");
        int httpCode = http.POST(jsonPayload);
        http.end();

        if (httpCode == HTTP_CODE_OK || httpCode == 201) {
          Serial.println("Sync: Upload success!");

          if (!SD.exists("/uploaded_tracks")) {
            SD.mkdir("/uploaded_tracks");
          }

          file.close(); // Close before rename
          String oldPath = "/tracks/" + filename;
          String newPath = "/uploaded_tracks/" + filename;
          if (SD.rename(oldPath, newPath)) {
            Serial.println("Sync: Moved to " + newPath);
          } else {
            Serial.println("Sync: Failed to move file!");
          }
        } else {
          Serial.print("Sync: Upload failed. Code: ");
          Serial.println(httpCode);
          allSuccess = false;
          file.close(); // Close if failed
        }
      } else {
        file.close();
        allSuccess = false;
      }
    } else {
      file.close();
    }
    file = root.openNextFile();
  }

  root.close();
  _isBusy = false;

  // GPX upload is optional - don't fail sync if GPX uploads fail
  // Return true to allow sync to complete successfully
  Serial.println("Sync: GPX upload phase complete (optional)");
  return true; // Always return true - GPX is optional
}

// Removed calculateEscapedLength as it is no longer used for Base64 streaming

bool SyncManager::uploadSingleSession(const char *apiUrl, const char *username,
                                      const char *password, String filename,
                                      String sessionType) {
  _isBusy = true;
  if (WiFi.status() != WL_CONNECTED) {
    _isBusy = false;
    return false;
  }

  if (!SD.exists(filename)) {
    Serial.println("Sync: Session file not found: " + filename);
    _isBusy = false;
    return false;
  }

  File f = SD.open(filename, FILE_READ);
  if (!f) {
    _isBusy = false;
    return false;
  }

  // Calculate JSON payload size for Base64
  // Overhead calculation for:
  // {"type":"upload_session","filename":"...","session_type":"TRACK","is_base64":true,"csv_data":"..."}
  // Overhead: ~100 characters (added ~30 for session_type field)
  size_t fileSize = f.size();
  size_t encodedLen = ((fileSize + 2) / 3) * 4; // Base64 formula
  size_t jsonLen = 100 + filename.length() + sessionType.length() + encodedLen;

  String url = String(apiUrl);
  bool isHttps = url.startsWith("https://");
  String host;
  int port = isHttps ? 443 : 80;
  String path = url;

  // Parse URL
  int protocolEnd = url.indexOf("://");
  if (protocolEnd != -1) {
    path = url.substring(protocolEnd + 3);
  }
  int slashIndex = path.indexOf('/');
  if (slashIndex != -1) {
    host = path.substring(0, slashIndex);
    path = path.substring(slashIndex);
  } else {
    host = path;
    path = "/";
  }

  int colonIndex = host.indexOf(':');
  if (colonIndex != -1) {
    port = host.substring(colonIndex + 1).toInt();
    host = host.substring(0, colonIndex);
  }

  WiFiClient *client;
  WiFiClientSecure secureClient;
  WiFiClient plainClient;

  if (isHttps) {
    secureClient.setInsecure();
    secureClient.setHandshakeTimeout(30000);
    client = &secureClient;
  } else {
    client = &plainClient;
  }

  Serial.println("Sync: Connecting to " + host + ":" + String(port));

  if (client->connect(host.c_str(), port)) {
    Serial.println("Sync: Connected. Uploading " + filename + " (" +
                   String(jsonLen) + " bytes)");

    // Send Headers
    client->println("POST " + path + " HTTP/1.1");
    client->println("Host: " + host);
    client->println("Authorization: " +
                    makeBasicAuthHeader(username, password));
    client->println("Content-Type: application/json");
    client->println("Content-Length: " + String(jsonLen));
    client->println("Connection: close");
    client->println();

    // --- Buffering & Streaming ---
    const size_t READ_BUFFER_SIZE = 512;
    const size_t WRITE_BUFFER_SIZE = 2048;
    uint8_t *readBuff = (uint8_t *)malloc(READ_BUFFER_SIZE);
    char *writeBuff = (char *)malloc(WRITE_BUFFER_SIZE);

    if (!readBuff || !writeBuff) {
      Serial.println("Sync: Malloc Failed!");
      if (readBuff)
        free(readBuff);
      if (writeBuff)
        free(writeBuff);
      client->stop();
      f.close();
      _isBusy = false;
      return false;
    }

    size_t writePos = 0;
    auto flushBuffer = [&]() {
      if (writePos > 0) {
        client->write((const uint8_t *)writeBuff, writePos);
        writePos = 0;
      }
    };

    auto bufferStr = [&](const char *s) {
      size_t slen = strlen(s);
      for (size_t i = 0; i < slen; i++) {
        if (writePos >= WRITE_BUFFER_SIZE)
          flushBuffer();
        writeBuff[writePos++] = s[i];
      }
    };

    // Body Start - include session_type so server can classify DRAG vs TRACK
    bufferStr("{\"type\":\"upload_session\",\"filename\":\"");
    bufferStr(filename.c_str());
    bufferStr("\",\"session_type\":\"");
    bufferStr(sessionType.c_str());
    bufferStr("\",\"is_base64\":true,\"csv_data\":\"");

    f.seek(0);
    size_t totalProcessed = 0;
    size_t totalSize = f.size();
    unsigned long lastLog = millis();

    // Base64 encoding in chunks of 3 bytes to avoid padding issues
    const size_t B64_READ_SIZE = 3 * 64; // Must be multiple of 3
    uint8_t bufferB64[B64_READ_SIZE];

    while (f.available() && client->connected()) {
      int readBytes = f.read(bufferB64, B64_READ_SIZE);
      if (readBytes <= 0)
        break;

      String encoded = base64::encode(bufferB64, readBytes);
      bufferStr(encoded.c_str());

      totalProcessed += readBytes;
      if (millis() - lastLog > 2000) {
        Serial.printf("Sync: %d%% (%d KB)\n",
                      (int)((totalProcessed * 100) / totalSize),
                      (int)(totalProcessed / 1024));
        lastLog = millis();
      }
      yield();
    }

    bufferStr("\"}");
    flushBuffer();

    free(readBuff);
    free(writeBuff);
    f.close();

    bool success = false;
    unsigned long timeout = millis() + 20000;
    Serial.println("Sync: Waiting for response...");

    // Wait for data or disconnect or timeout
    while (!client->available() && client->connected()) {
      if (millis() > timeout)
        break;
      yield();
    }

    if (client->available()) {
      String resp = client->readStringUntil('\n');
      Serial.println("Sync: Response: " + resp);
      if (resp.indexOf("200") != -1 || resp.indexOf("201") != -1)
        success = true;

      // Clean up remaining buffer
      while (client->available()) {
        client->read();
        yield();
      }
    } else {
      Serial.println("Sync: No response from server (timeout or disconnect)");
    }

    client->stop();
    _isBusy = false;
    return success;
  } else {
    Serial.println("Sync: Connect failed");
    f.close();
    _isBusy = false;
    return false;
  }
}

void SyncManager::pushLiveTelemetry(const char *apiUrl, const char *username,
                                    const char *password, float lat, float lon,
                                    float speed, float rpm, int sats,
                                    float bat_v, int bat_p) {
  if (WiFi.status() != WL_CONNECTED)
    return;

  // Construct API URL: Replace /device/sync with /device/live
  String url = String(apiUrl);
  int idx = url.indexOf("/device/sync");
  if (idx > 0) {
    url = url.substring(0, idx) + "/device/live";
  } else {
    // Fallback or just append if structure is different
    // Assuming API_URL is base/api/device/sync
    // If not found, try to strip last part
    int lastSlash = url.lastIndexOf('/');
    if (lastSlash > 0) {
      url = url.substring(0, lastSlash) + "/live";
    }
  }

  bool isHttps = url.startsWith("https://");
  WiFiClient *client;
  WiFiClientSecure secureClient;
  WiFiClient plainClient;

  if (isHttps) {
    secureClient.setInsecure();
    secureClient.setHandshakeTimeout(2000); // Short timeout for live data
    client = &secureClient;
  } else {
    client = &plainClient;
  }

  HTTPClient http;

  if (http.begin(*client, url)) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", makeBasicAuthHeader(username, password));

    // Create JSON Payload
    String payload = "{";
    payload += "\"lat\":" + String(lat, 6) + ",";
    payload += "\"lng\":" + String(lon, 6) + ",";
    payload += "\"speed\":" + String(speed, 1) + ",";
    payload += "\"rpm\":" + String(rpm, 0) + ",";
    payload += "\"sats\":" + String(sats) + ",";
    payload += "\"bat_v\":" + String(bat_v, 2) + ",";
    payload += "\"bat_p\":" + String(bat_p);
    payload += "}";

    // Send POST (Fire and Forget - very short timeout)
    http.setTimeout(100); // 100ms timeout to avoid hanging the main loop
    http.POST(payload);
    http.end();
  }
}
String SyncManager::getStoredUsername() { return _cachedUser; }

String SyncManager::getStoredPassword() { return _cachedPass; }

void SyncManager::logout() {
  Serial.println("Sync: Logging out...");

  // Clear user credentials from NVS
  _prefs.begin("muchrace", false);
  _prefs.clear(); // Clear all user data (including setup_done)
  _prefs.end();

  // Clear WiFi credentials from laptimer namespace
  _prefs.begin("laptimer", false);
  _prefs.remove("wifi_ssid");
  _prefs.remove("wifi_pass");
  _prefs.end();

  // Clear generic WiFi settings if any
  _prefs.begin("wifi", false);
  _prefs.clear();
  _prefs.end();

  // Clear Sync tokens/status
  _prefs.begin("sync", false);
  _prefs.clear();
  _prefs.end();

  // Clear cached credentials
  _cachedUser = "";
  _cachedPass = "";
  _cachedApiUrl = "";

  Serial.println("Sync: Logout complete.");
}
