#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include "../config.h"
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <TinyGPS++.h> // Still needed for distanceBetween static helper
#include <functional>
#include <vector>

struct SatelliteInfo {
  uint8_t id;
  int16_t elevation; // 0-90
  int16_t azimuth;   // 0-360
  uint8_t snr;       // Signal Strength
};

class GPSManager {
public:
  enum GnssMode {
    MODE_ALL_10HZ = 0,
    MODE_GPS_GL_SBAS_16HZ = 1, // Default
    MODE_GPS_GAL_GL_SBAS_10HZ = 2,
    MODE_GPS_GAL_SBAS_20HZ = 3,
    MODE_GPS_SBAS_25HZ = 4
  };

  void begin();
  void update();

  // Getters
  bool isFixed();
  double getLatitude();
  double getLongitude();
  float getSpeedKmph();
  float getTotalTrip();
  void resetTrip();
  int getSatellites();
  void setRPM(int rpm) { _currentRPM = rpm; }
  int getRPM() { return _currentRPM; }
  bool isWirelessConnected();
  String getTimeString();
  String getDateString();
  int getRawHour();
  int getRawMinute();
  double getHDOP();
  double getAltitude();
  double getHeading();
  int getUpdateRate();
  unsigned long getUnixTimestamp();
  std::vector<SatelliteInfo> getSatellitesData() { return _satellites; }

  // DIAGNOSTIC: Get total bytes received from GPS
  unsigned long getBytesReceived();

  // Configuration
  void setGnssMode(uint8_t mode);
  uint8_t getGnssMode();
  void setDynamicModel(uint8_t model); // 0=Portable, 1=Station, 2=Ped...
  void setSBASConfig(uint8_t region);  // 0=EGNOS, 1=WAAS...
  void setProjection(bool enabled);    // Coordinate Projection
  bool isProjectionEnabled() { return _projectionEnabled; }
  void setFrequencyLimit(int freq); // 10 or 25 (max)

  // RPM Configuration
  void setRpmEnabled(bool enabled);
  bool isRpmEnabled() { return _rpmEnabled; }
  void setPPRIndex(int idx); // 0=1.0, 1=0.5, 2=2.0, 3=3.0, 4=4.0
  float getPPR() { return _currentPPR; }

  // Manual Time & Redundancy
  void setManualTime(int h, int m, int s = 0);
  void setUtcOffset(int offset); // Hours
  int getUtcOffset() { return _utcOffset; }

  // Pin Configuration
  void setPins(int rx, int tx);
  bool detectBaudRate();
  int getRxPin() { return _rxPin; }
  int getTxPin() { return _txPin; }

  // Baud Rate Config
  void setBaud(int baud);
  int getBaud() { return _baudRate; }

  // Debugging / Logging
  // Callback signature: void(uint8_t c)
  typedef std::function<void(uint8_t)> RawDataCallback;
  void setRawDataCallback(RawDataCallback cb) { _dataCallback = cb; }

  // Utilities
  // Utilities
  void resetModule(bool cold = false); // Reset GPS hardware/software

  double distanceBetween(double lat1, double long1, double lat2, double long2);
  void getLocalTime(int &h, int &m, int &s, int &d, int &mo, int &y);

private:
  HardwareSerial *_gpsSerial;
  void sendUBX(const uint8_t *cmd, int len);
  void configureGpsBaud(int targetBaud);
  void disableUnnecessarySentences(); // Optimize GPS bandwidth

  // Manual Data Callback
  RawDataCallback _dataCallback = nullptr;

  // Pin Config
  int _rxPin = PIN_GPS_RX; // Default from config.h
  int _txPin = PIN_GPS_TX;
  int _baudRate = GPS_BAUD; // Default 9600

  // GPS Data (from UBX or NMEA)
  bool _hasValidFix = false;
  uint8_t _fixType =
      0; // 0=No fix, 1=Dead reckoning, 2=2D, 3=3D, 4=GNSS+DR, 5=Time only
  int _satelliteCount = 0;
  double _latitude = 0.0;
  double _longitude = 0.0;
  double _altitude = 0.0;
  double _currentSpeed = 0.0;
  double _heading = 0.0;
  double _pdop = 99.9; // Position DOP from UBX-NAV-PVT
  unsigned long _lastUpdateTime = 0;
  unsigned long _totalBytesReceived =
      0; // DIAGNOSTIC: Track total bytes from GPS

  // Trip Meter
  double _totalDistance = 0.0;
  double _lastLat = 0.0;
  double _lastLng = 0.0;
  bool _hasLastPos = false;
  unsigned long _lastSaveTime = 0;
  int _currentRPM = 0;

  // Hz Calculation
  int _updatesCount = 0;
  unsigned long _lastRateCheck = 0;
  int _currentHz = 0;
  float _smoothedHz = 0.0f;
  float _smoothedSats = 0.0f;
  float _smoothedPdop = 99.9f;
  double _hdop = 99.9;
  float _smoothedHdop = 99.9f;

  // Settings Cache
  uint8_t _currentGnssMode = 0;
  uint8_t _currentDynModel = 3;   // Default Automotive (User Index 3 -> UBX 4)
  uint8_t _currentSBAS = 0;       // Default MSAS (Japan/Indo)
  bool _projectionEnabled = true; // Default Enabled
  int _targetFreq = 10;

  bool _rpmEnabled = true; // Default Enabled
  float _currentPPR = 1.0; // Default 1.0 (1 pulse per rev)

  // Manual Time System
  int _sysHour = 0;
  int _sysMin = 0;
  int _sysSec = 0;
  int _sysDay = 1;
  int _sysMonth = 1;
  int _sysYear = 2024;
  int _utcOffset = 0;
  unsigned long _lastTick = 0;

  // Wireless RPM Support
  unsigned long _lastWirelessRpmTime = 0;
  typedef struct {
    uint16_t rpm;
  } WirelessRpmPacket;

  // UBX Parser State Machine
  enum UBXState {
    UBX_SYNC1,
    UBX_SYNC2,
    UBX_CLASS,
    UBX_ID,
    UBX_LEN1,
    UBX_LEN2,
    UBX_PAYLOAD,
    UBX_CK_A,
    UBX_CK_B
  };

  UBXState _ubxState = UBX_SYNC1;
  uint8_t _ubxClass = 0;
  uint8_t _ubxId = 0;
  uint16_t _ubxLength = 0;
  uint16_t _ubxPayloadIndex = 0;
  uint8_t _ubxPayload[500]; // Buffer for UBX-NAV-PVT (92) & UBX-NAV-SAT (~300+)
  uint8_t _ubxCkA = 0;
  uint8_t _ubxCkB = 0;

  // UBX Parser Methods
  void processUBXByte(uint8_t b);
  void parseUBXNavPvt();
  void parseUBXNavSat();
  void parseUBXNavDop();
  std::vector<SatelliteInfo> _satellites;

  // RPM Logic
  static volatile unsigned long _rpmPulses;
  static volatile unsigned long _lastPulseMicros;
  static volatile unsigned long _pulseInterval;
  unsigned long _lastRpmCalcTime = 0;

public:
  static void IRAM_ATTR onPulse();
  void handleWirelessRPM(int rpm);
};

#endif
