#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include "../config.h"
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <vector>

struct GeoPoint {
  double lat;
  double lon;
  unsigned long long t;
  int rpm;
  float gX;
  float gY;
  float lean;
  float heading;
  float speed;
};

struct __attribute__((packed)) LogPacket {
  uint16_t header;    // 0xAA55
  uint32_t timestamp; // ms
  int32_t lat;        // * 1e7
  int32_t lon;        // * 1e7
  uint16_t speed;     // km/h * 10
  uint16_t rpm;
  int16_t accX; // G * 100
  int16_t accY; // G * 100
  int16_t accZ; // G * 100
  uint8_t sats;
  uint8_t fix;
  uint8_t battery; // %
  int16_t tilt;    // degrees * 10
  uint8_t checksum;
  uint8_t padding; // Align to 32 bytes
};

struct LogEntry {
  uint8_t type; // 0=Packet, 1=String Metadata
  union {
    LogPacket packet;
    char *metadata;
  };
};

struct HistoryItem {
  String filename;
  String date;
  int laps;              // or runs for drag
  unsigned long bestLap; // ms
  String type;           // "TRACK" or "DRAG"
};

class SessionManager {
public:
  void begin();

  bool startSession();
  void stopSession();
  void logData(String dataLine);   // For metadata (LAP, etc.)
  void logData(LogPacket &packet); // For high-frequency telemetry

  bool isLogging() { return _logging; }

  void appendToHistoryIndex(String filename, String date, int laps,
                            unsigned long bestLap, String type = "TRACK");
  String loadHistoryIndex(); // Returns full content (Deprecating soon)
  std::vector<HistoryItem> loadHistoryItems(); // Better version

  bool deleteSession(String filename); // Delete file and update index
  void wipeSDData();                   // Wipe sessions, tracks and history

  bool getSDStatus(uint64_t &total, uint64_t &used);

  struct SDTestResult {
    bool success;
    String cardType;
    String sizeLabel; // e.g. "16 GB"
    String usedLabel; // e.g. "200 MB"
    float readSpeedKBps;
    float writeSpeedKBps;
  };

  SDTestResult runFullTest(void (*progressCallback)(int, String) = NULL);

  struct SessionAnalysis {
    unsigned long totalTime;
    float totalDistance; // km
    float maxSpeed;      // km/h
    float avgSpeed;      // km/h
    int maxRPM;          // Peak RPM reached
    int validLaps;
    std::vector<unsigned long> lapTimes;
    unsigned long bestLap;
    // Drag Metrics
    unsigned long time0to60;
    unsigned long time0to100;
    unsigned long time100to200;
    unsigned long time400m;
    std::vector<unsigned long> sector1;
    std::vector<unsigned long> sector2;
    std::vector<unsigned long> sector3;
    String filename; // Added for comparison in UI
  };

  SessionAnalysis analyzeSession(String filename);

  struct ReferencePoint {
    float distance; // Meters from start of LAP
    uint32_t time;  // ms from start of LAP
  };
  std::vector<ReferencePoint> referenceLap;
  bool loadBestLapAsReference(String filename); // Loads best lap from session
  void promoteLastLapToReference(); // Promotes last lap of CURRENT session to
                                    // reference
  float getReferenceTime(float distance); // Interp logic
  std::vector<GeoPoint> getLapPoints(String filename, int lapIdx);
  bool exportSessionToGPX(String filename); // Parses CSV and writes GPX

private:
  bool _logging;
  File _logFile;
  String createFilename();
  String _currentFilename;

public:
  String getCurrentFilename() { return _currentFilename; }

private:
  QueueHandle_t _logQueue;
  TaskHandle_t _loggingTaskHandle;
  static void loggingTask(void *parameter);
};

#endif
