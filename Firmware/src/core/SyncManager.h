#ifndef SYNC_MANAGER_H
#define SYNC_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>

class SyncManager {
public:
  SyncManager();

  // Main sync functions
  bool performFirstSync(const char *apiUrl, const char *username,
                        const char *password);
  bool syncSettings(const char *apiUrl, const char *username,
                    const char *password);
  bool uploadSessions(const char *apiUrl, const char *username,
                      const char *password);
  bool uploadSingleSession(const char *apiUrl, const char *username,
                           const char *password, String filename,
                           String sessionType = "TRACK");
  bool uploadGPXTracks(const char *apiUrl, const char *username,
                       const char *password);

  // Live Telemetry
  struct TelemetryData {
    String apiUrl;
    String username;
    String password;
    float lat;
    float lon;
    float speed;
    float rpm;
    int sats;
    float bat_v;
    int bat_p;
  };

  void pushLiveTelemetry(const char *apiUrl, const char *username,
                         const char *password, float lat, float lon,
                         float speed, float rpm, int sats, float bat_v,
                         int bat_p);
  void queueTelemetry(float lat, float lon, float speed, float rpm, int sats,
                      float bat_v, int bat_p);

  // Status checks
  bool isFirstSyncDone();
  bool isBusy() { return _isBusy; }
  String getLastSyncTime();
  String getStoredUsername();
  String getStoredPassword();
  String getLastError() { return _lastError; }

  // Manual sync trigger
  void triggerManualSync();
  void loadCredentialCache();
  void logout();

private:
  Preferences _prefs;
  bool _isBusy = false;
  String _cachedUser;
  String _cachedPass;
  String _cachedApiUrl;
  String _lastError;

  QueueHandle_t _telemetryQueue;
  TaskHandle_t _telemetryTaskHandle;
  static void telemetryTask(void *pvParameters);

  // HTTP helpers
  String makeBasicAuthHeader(const char *username, const char *password);
  bool downloadAndApplySettings(const char *apiUrl, const char *authHeader);
  bool downloadTracks(const char *apiUrl, const char *authHeader);

  // Settings application
  void applySettings(JsonDocument &doc);
  void applyTrackSelection(JsonDocument &doc);
  void applyEngineSettings(JsonDocument &doc);
  void applyProfileData(JsonDocument &doc);

  // Storage
  // Storage
  void markSyncComplete();
  void saveLastSyncTime();

  // Synced Session Tracking
  bool isSessionSynced(String filename);
  void markSessionSynced(String filename);
};

#endif
