#include "GPSManager.h"
#include "../ui/screens/TimeSettingScreen.h"
#include "SessionManager.h"
#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>

// Global pointer for ESP-NOW callback to access GPSManager instance
GPSManager *gpsInstance = nullptr;

// ESP-NOW Data Structure (must match sender)
typedef struct {
  uint16_t rpm;
} WirelessRpmPacket;

// Callback when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (gpsInstance != nullptr && len == sizeof(WirelessRpmPacket)) {
    WirelessRpmPacket *packet = (WirelessRpmPacket *)incomingData;
    gpsInstance->handleWirelessRPM(packet->rpm);
  }
}

void GPSManager::begin() {
  // Note: Serial is now used for GPS (UART0), not debug output!
  // Debug output disabled to avoid conflict with GPS on GPIO 1/3

  _rxPin = PIN_GPS_RX;
  _txPin = PIN_GPS_TX;

  // --- OPEN UART1 FOR GPS ---
  // Load saved baud rate from preferences, with validation
  {
    Preferences tmpPrefs;
    tmpPrefs.begin("laptimer", true);
    int savedBaud = tmpPrefs.getInt("gps_baud", GPS_BAUD);
    tmpPrefs.end();

    // Validate: only accept known valid baud rates
    const int validBauds[] = {9600, 19200, 38400, 57600, 115200};
    bool isValid = false;
    for (int b : validBauds) {
      if (savedBaud == b) {
        isValid = true;
        break;
      }
    }
    _baudRate = isValid ? savedBaud : GPS_BAUD;

    if (!isValid) {
      DEBUG_PRINTF("GPS: Invalid saved baud %d, using default %d\n", savedBaud,
                   GPS_BAUD);
      // Clear the corrupt value
      Preferences fixPrefs;
      fixPrefs.begin("laptimer", false);
      fixPrefs.putInt("gps_baud", GPS_BAUD);
      fixPrefs.end();
    }
  }

  _gpsSerial = &Serial2;

  DEBUG_PRINTF("GPS Manager Begin: RX=%d, TX=%d @ SAVED BAUD=%d\n", _rxPin,
               _txPin, _baudRate);

  // Try to find the current GPS baud rate (might be factory 9600)
  if (!detectBaudRate()) {
    _baudRate = 115200; // Assume 115200 as default target
    _gpsSerial->begin(115200, SERIAL_8N1, _rxPin, _txPin);
  }

  // If we found it but it's not 115200, try to switch it
  if (_baudRate != 115200) {
    DEBUG_PRINTLN("GPS: Switching module to 115200 baud...");
    setBaud(115200);
    delay(200);
    _gpsSerial->end();
    _gpsSerial->begin(115200, SERIAL_8N1, _rxPin, _txPin);
    _baudRate = 115200;
  }

  delay(500); // Wait for GPS module to stabilize

  // Configure NEO-M8N to enable UBX-NAV-PVT message (92 bytes, contains all nav
  // data) UBX-CFG-MSG: Enable NAV-PVT message
  uint8_t enableNavPvt[] = {
      0xB5, 0x62, // Header
      0x06, 0x01, // CFG-MSG
      0x03, 0x00, // Length = 3 bytes
      0x01,       // Message Class: NAV (0x01)
      0x07,       // Message ID: PVT (0x07)
      0x01,       // Rate: send every solution
      0x00, 0x00  // Checksum placeholder
  };

  // Calculate UBX checksum
  uint8_t ck_a = 0, ck_b = 0;
  for (int i = 2; i < 9; i++) {
    ck_a += enableNavPvt[i];
    ck_b += ck_a;
  }
  enableNavPvt[9] = ck_a;
  enableNavPvt[10] = ck_b;

  _gpsSerial->write(enableNavPvt, 11);
  // Enable UBX-NAV-DOP (0x01 0x04)
  uint8_t enableNavDop[] = {0xB5, 0x62, 0x06, 0x01, 0x03, 0x00,
                            0x01, 0x04, 0x01, 0x00, 0x00};

  ck_a = 0;
  ck_b = 0;
  for (int i = 2; i < 9; i++) {
    ck_a += enableNavDop[i];
    ck_b += ck_a;
  }
  enableNavDop[9] = ck_a;
  enableNavDop[10] = ck_b;
  _gpsSerial->write(enableNavDop, 11);
  delay(100);

  // Set to 10Hz for high performance (Accuracy enhancement)
  setFrequencyLimit(10);
  delay(100);

  // Disable high-bandwidth NMEA sentences (GSV, GSA, GLL) - Binary is better
  // disableUnnecessarySentences(); // Temporarily disabled for debugging GNSS
  // Log Screen
  delay(100);

  // NOTE: Dynamic model set from saved preferences below (line ~268).
  // Do NOT hardcode here — preferences override is desired.
  delay(100);

  // --- ESP-NOW WIRELESS RPM SETUP ---
  gpsInstance = this;
  WiFi.mode(WIFI_STA);
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    DEBUG_PRINTLN("GPS: Wireless RPM Interface Initialized");
  } else {
    DEBUG_PRINTLN("GPS: ESP-NOW Init Failed!");
  }

  // --- BEST GNSS CONFIG: GPS + GLONASS + GALILEO + SBAS ---
  // UBX-CFG-GNSS (0x06 0x3E)
  uint8_t setBestGnss[70] = {
      0xB5, 0x62, // Header
      0x06, 0x3E, // CFG-GNSS
      0x3C, 0x00, // Length: 60 bytes (for M8N)
      0x00,       // msgVer
      0x00,       // numTrkChHw
      0x20,       // numTrkChUse (32 channels)
      0x06,       // numConfigBlocks (6 blocks)

      // 1. GPS (Enable)
      0x00,                   // gnssId (0 = GPS)
      0x08,                   // resTrkCh
      0x10,                   // maxTrkCh (16)
      0x00,                   // reserved1
      0x01, 0x00, 0x01, 0x01, // flags (Enable, SigConfig)

      // 2. SBAS (Enable)
      0x01,                   // gnssId (1 = SBAS)
      0x01,                   // resTrkCh
      0x03,                   // maxTrkCh
      0x00,                   // reserved1
      0x01, 0x00, 0x01, 0x01, // flags (Enable)

      // 3. Galileo (Enable)
      0x02,                   // gnssId (2 = Galileo)
      0x04,                   // resTrkCh
      0x08,                   // maxTrkCh
      0x00,                   // reserved1
      0x01, 0x00, 0x01, 0x01, // flags (Enable)

      // 4. BeiDou (Disable - usually exclusive with Glonass on M8N)
      0x03,                   // gnssId (3 = BeiDou)
      0x00,                   // resTrkCh
      0x00,                   // maxTrkCh
      0x00,                   // reserved1
      0x00, 0x00, 0x00, 0x00, // flags (Disable)

      // 5. IMES (Disable)
      0x04,                   // gnssId (4 = IMES)
      0x00,                   // resTrkCh
      0x00,                   // maxTrkCh
      0x00,                   // reserved1
      0x00, 0x00, 0x00, 0x00, // flags (Disable)

      // 6. QZSS (Enable - Helps in Asia/Indo)
      0x05,                   // gnssId (5 = QZSS)
      0x00,                   // resTrkCh
      0x03,                   // maxTrkCh
      0x00,                   // reserved1
      0x01, 0x00, 0x01, 0x01, // flags (Enable)

      // 7. GLONASS (Enable)
      0x06, // gnssId (6 = GLONASS)
      0x08, // resTrkCh
      0x14, // maxTrkCh (20)
      0x00, // reserved1
      0x01, 0x00, 0x01,
      0x01 // flags (Enable)

      // Checksum (Calculated below)
      ,
      0x00, 0x00};

  // Calc Checksum (Length is 4 + 60 = 64 bytes total struct, minus Header 2
  // bytes) Payload starts at index [4] to [63]
  ck_a = 0;
  ck_b = 0;
  for (int i = 2; i < 68; i++) {
    ck_a += setBestGnss[i];
    ck_b += ck_a;
  }
  setBestGnss[68] = ck_a;
  setBestGnss[69] = ck_b;

  // Send Config
  _gpsSerial->write(setBestGnss, 70);
  delay(200);

  // Enable UBX-NAV-SAT (0x01 0x35)
  uint8_t enableNavSat[] = {0xB5, 0x62, 0x06, 0x01, 0x03, 0x00,
                            0x01, 0x35, 0x01, 0x00, 0x00};
  // Calc Checksum
  ck_a = 0;
  ck_b = 0;
  for (int i = 2; i < 9; i++) {
    ck_a += enableNavSat[i];
    ck_b += ck_a;
  }
  enableNavSat[9] = ck_a;
  enableNavSat[10] = ck_b;
  _gpsSerial->write(enableNavSat, 11);
  delay(100);

  // Serial.println NOT available - Serial used for GPS data!

  // Load Preferences
  Preferences prefs;
  prefs.begin("laptimer", true); // Read-only

  // 1. UTC Offset
  int storedOffset = prefs.getInt("utc_offset", -100);
  if (storedOffset == -100) {
    _utcOffset = 7; // Default to WIB (Indonesia)
  } else {
    _utcOffset = storedOffset;
  }

  // 2. GNSS Mode
  // Default to 1 (GPS+GLO+SBAS) if not set
  uint8_t storedMode = prefs.getInt("gnss_mode", 7);
  setGnssMode(storedMode);

  // 3. SBAS Config
  // Default to 0 (MSAS) - Use 0 because we mapped MSAS to 0 in UI
  uint8_t storedSBAS = prefs.getInt("gnss_sbas", 5);
  setSBASConfig(storedSBAS);

  // 4. Dynamic Model
  // Default to 3 (Automotive)
  uint8_t storedModel = prefs.getInt("gnss_model", 3);
  setDynamicModel(storedModel);

  // 5. Frequency Limit
  // Default to 2 (5Hz for safety)
  // But wait, setGnssMode sets frequency limit too? Yes.
  // We should apply explicit frequency limit AFTER mode if needed,
  // or let mode dictate it.
  // "gnss_freq_limit" logic in SettingsScreen overrides it?
  // Let's load it.
  // 5. Frequency Limit
  // Default to 0 (10Hz)
  // "gnss_freq_limit" logic in SettingsScreen overrides it?
  // Let's load it.
  int storedFreqIdx = prefs.getInt("gnss_freq_limit", 0);
  int freqs[] = {10, 18, 25}; // Added 25Hz
  if (storedFreqIdx >= 0 && storedFreqIdx < 3) {
    setFrequencyLimit(freqs[storedFreqIdx]);
  }

  // 6. RPM Enabled
  _rpmEnabled = prefs.getBool("rpm_enabled", true);

  // 7. RPM PPR (Pulses Per Revolution)
  // Default to 1 (1 PPR) -> Index 0?
  // Let's check SetPPRIndex logic or default.
  // Assuming default is 1 PPR (Index 0).
  int storedPPR = prefs.getInt("rpm_ppr", 0);
  setPPRIndex(storedPPR);

  prefs.end();
}

// Static Member Initialization
volatile unsigned long GPSManager::_rpmPulses = 0;
volatile unsigned long GPSManager::_lastPulseMicros = 0;
volatile unsigned long GPSManager::_pulseInterval = 0;

void IRAM_ATTR GPSManager::onPulse() {
  unsigned long now = micros();
  unsigned long diff = now - _lastPulseMicros;
  if (diff > 1000) { // 1ms lockout
    _pulseInterval = diff;
    _lastPulseMicros = now;
    _rpmPulses++;
  }
}

void GPSManager::handleWirelessRPM(int rpm) {
  // Smoothing (EMA) for wireless data: 70% old, 30% new
  _currentRPM = (_currentRPM * 7 + rpm * 3) / 10;
  _lastWirelessRpmTime = millis();
}

bool GPSManager::isWirelessConnected() {
  if (millis() - _lastWirelessRpmTime < 1000) {
    return true;
  }
  return false;
}

void GPSManager::update() {
  if (!_gpsSerial)
    return;

  // Track bytes received for debug
  static unsigned long lastDebugTime = 0;
  static int bytesReceived = 0;

  while (_gpsSerial->available() > 0) {
    uint8_t c = _gpsSerial->read();
    _totalBytesReceived++; // Track total bytes for diagnostic

    // --- DEBUG NMEA buffer (fixed char array, no String alloc for racing perf)
    // ---
    static char nmeaBuf[88];
    static uint8_t nmeaIdx = 0;
    if (c == '$') {
      nmeaBuf[0] = '$';
      nmeaIdx = 1;
    } else if (c == '\n' && nmeaIdx > 0 && nmeaBuf[0] == '$') {
      nmeaIdx = 0; // Reset (debug print disabled)
    } else if (nmeaIdx > 0 && nmeaBuf[0] == '$') {
      if (c >= 32 && c <= 126 && nmeaIdx < sizeof(nmeaBuf) - 1) {
        nmeaBuf[nmeaIdx++] = (char)c;
      }
      if (nmeaIdx >= sizeof(nmeaBuf) - 1) {
        nmeaIdx = 0; // Safety clear
      }
    }

    // Invoke debug callback if set (for UI Screen)
    if (_dataCallback) {
      _dataCallback(c);
    }

    // Process UBX binary protocol (Primary Data Source)
    processUBXByte(c);
  }
  // Debug output removed for performance (Serial0 is UART0/Debug)
  // Use diagnostics screen to view bytes received if needed.

  // --- SYSTEM TIME REDUNDANCY ---
  // 1. Tick System Time
  if (_lastTick == 0)
    _lastTick = millis();
  unsigned long now = millis();
  if (now - _lastTick >= 1000) {
    _sysSec++;
    if (_sysSec >= 60) {
      _sysSec = 0;
      _sysMin++;
      if (_sysMin >= 60) {
        _sysMin = 0;
        _sysHour++;
        if (_sysHour >= 24) {
          _sysHour = 0;
          // Day increment logic omitted for simplicity or could be added
        }
      }
    }
    _lastTick = now;
  }

  // 2. Auto-Sync with GPS (if valid)
  // UBX Parser already updates system time directly.
  // No need for secondary sync logic here.

  // Update Trip Meter
  // Position updates are handled in UBX NAV-PVT parser.
  // Trip meter calculation logic should reside there or use
  // _latitude/_longitude.

  // Actually, NAV-PVT already updates these members.
  // Trip accumulation usually happens when a new valid position arrives.
  // Let's keep it here but use our members instead of _gps instance.

  // Actually, NAV-PVT already updates these members.
  // Trip accumulation usually happens when a new valid position arrives.
  // Let's keep it here but use our members instead of _gps instance.

  static double lastLat = 0, lastLon = 0;
  if (_hasValidFix && (_latitude != lastLat || _longitude != lastLon)) {
    double lat = _latitude;
    double lng = _longitude;
    if (_hasLastPos) {
      double dist = distanceBetween(_lastLat, _lastLng, lat, lng);
      // Filter out jitter (e.g. static movements < 2m)
      // ALSO: Only increment distance if we have a high quality fix and are
      // moving above deadzone speed
      bool highQualityFix = (_satelliteCount >= GPS_MIN_SATS);
      if (dist > 2.0 && dist < 1000.0 && _currentSpeed >= GPS_SPEED_DEADZONE &&
          highQualityFix) {
        _totalDistance += dist;
      }
    }

    _lastLat = lat;
    _lastLng = lng;
    lastLat = lat;
    lastLon = lng;
    _hasLastPos = true;
  }

  // Calculate Hz every 1 second
  if (millis() - _lastRateCheck >= 1000) {
    _currentHz = _updatesCount;
    // EMA Smoothing for Update Rate (Filter out timing jitter)
    if (_smoothedHz < 0.1f)
      _smoothedHz = (float)_currentHz;
    else
      _smoothedHz = (_smoothedHz * 0.7f) + ((float)_currentHz * 0.3f);

    _updatesCount = 0;
    _lastRateCheck = millis();
  }

  // --- RPM CALCULATION (PERIOD METHOD) ---
  if (_rpmEnabled) {
    // Check if we have FRESH wireless data (last 1 second)
    bool hasWireless = (millis() - _lastWirelessRpmTime < 1000);

    if (hasWireless) {
      // Wireless data is already set via handleWirelessRPM callback
      // We don't need to do anything here except maybe smoothing if needed
      // (Sender already smooths, but we can do a final pass)
    } else {
      // Fallback to Physical Wired Pulses
      if (millis() - _lastRpmCalcTime > 20) { // 50Hz Update for smoothness
        _lastRpmCalcTime = millis();

        unsigned long lastP = _lastPulseMicros;
        unsigned long interval = _pulseInterval;
        unsigned long nowMicros = micros();

        // Timeout: 0.5s without pulse -> Engine Off/Stall (<120 RPM 4T)
        if (nowMicros - lastP > 500000) {
          _currentRPM = 0;
        } else if (interval > 0) {
          // Calculate RPM: (60 sec * 1000 ms * 1000 us) / (interval * PPR)
          float ppr = (_currentPPR > 0.1) ? _currentPPR : 1.0;
          float instRPM = 60000000.0 / (float)(interval * ppr);

          if (instRPM > 20000)
            instRPM = 0; // Sanity check

          // Smoothing (EMA)
          _currentRPM = (_currentRPM * 7 + (int)instRPM * 3) / 10;
        }
      }
    }
  } else {
    _currentRPM = 0;
  }

  // Periodic Save (Optimization: Only save if changed significantly)
  static unsigned long lastSdSave = 0;
  static double lastSavedDistance = 0;

  if (millis() - _lastSaveTime > 60000) {
    if (abs(_totalDistance - lastSavedDistance) > 0.01) { // 10 meters change
      Preferences prefs;
      prefs.begin("laptimer", false);
      prefs.putDouble("total_trip", _totalDistance);
      prefs.end();
      lastSavedDistance = _totalDistance;
      _lastSaveTime = millis();

      // BACKUP TO SD CARD (Redundancy): Every 5 minutes
      // Only if not currently logging a fast session (to avoid stutters)
      extern SessionManager sessionManager;
      if (!sessionManager.isLogging() &&
          (millis() - lastSdSave > 300000 || lastSdSave == 0)) {
        if (SD.cardSize() > 0) {
          File file = SD.open("/trip.txt", FILE_WRITE);
          if (file) {
            file.println(_totalDistance);
            file.close();
            lastSdSave = millis();
          }
        }
      }
    } else {
      _lastSaveTime = millis(); // Still reset timer to avoid check every loop
    }
  }
}

// Manual Setters
void GPSManager::setManualTime(int h, int m, int s) {
  // Input is LOCAL time. Convert to UTC for System Time.
  // UTC = Local - Offset
  int utcH = h - _utcOffset;

  // Handle wrap around
  if (utcH < 0)
    utcH += 24;
  if (utcH >= 24)
    utcH -= 24;

  _sysHour = utcH;
  _sysMin = m;
  _sysSec = s;

  // Save preference for "Manual Sync" if desired?
  // Current requirement is just set it.
  // Maybe valid GPS will overwrite this immediately?
  // YES. This is desired. Manual is fallback.
}

void GPSManager::setUtcOffset(int offset) {
  _utcOffset = offset;
  Preferences prefs;
  prefs.begin("laptimer", false);
  prefs.putInt("utc_offset", offset);
  prefs.end();
}

bool GPSManager::isFixed() { return _hasValidFix; }

double GPSManager::getLatitude() {
  // Use UBX parsed data (NEO-M8N sends UBX, not NMEA)
  return _latitude;
}

double GPSManager::getLongitude() {
  // Use UBX parsed data (NEO-M8N sends UBX, not NMEA)
  return _longitude;
}

float GPSManager::getSpeedKmph() {
  // Racing Quality Gate: require 3D fix, minimum sats, and acceptable PDOP
  if (_fixType < 3) {
    return 0.0f; // Reject non-3D fix (no altitude = inaccurate speed)
  }

  if (_satelliteCount < GPS_MIN_SATS) {
    return 0.0f; // Not enough satellites
  }

  if (_pdop > GPS_MAX_PDOP) {
    return 0.0f; // Position dilution too high → unreliable
  }

  // Use UBX parsed speed (already in km/h)
  float speed = _currentSpeed;

  // Apply deadzone threshold to filter out noise when stationary
  if (speed < GPS_SPEED_DEADZONE) {
    return 0.0f;
  }

  return speed;
}

float GPSManager::getTotalTrip() {
  return (float)(_totalDistance / 1000.0); // Convert to km
}

void GPSManager::resetTrip() {
  _totalDistance = 0.0;
  Preferences prefs;
  prefs.begin("laptimer", false);
  prefs.putDouble("total_trip", 0.0);
  prefs.end();
}

int GPSManager::getSatellites() {
  // Use the smoothed satellite count to prevent jumping UI values
  if (_smoothedSats < 0.1f)
    return _satelliteCount;
  return (int)(_smoothedSats + 0.5f);
}

String GPSManager::getTimeString() {
  int h, m, s, d, mo, y;
  getLocalTime(h, m, s, d, mo, y);
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
  return String(buf);
}

String GPSManager::getDateString() {
  int h, m, s, d, mo, y;
  getLocalTime(h, m, s, d, mo, y);
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d", d, mo, y);
  return String(buf);
}

void GPSManager::getLocalTime(int &h, int &m, int &s, int &d, int &mo, int &y) {
  // Use Internal System Time (Redundant Source)
  h = _sysHour;
  m = _sysMin;
  s = _sysSec;
  d = _sysDay;
  mo = _sysMonth;
  y = _sysYear;

  h += _utcOffset;

  if (h < 0) {
    h += 24;
    d--;
    if (d < 1) {
      mo--;
      if (mo < 1) {
        mo = 12;
        y--;
      }
      static const int daysInMonth[] = {0,  31, 28, 31, 30, 31, 30,
                                        31, 31, 30, 31, 30, 31};
      int days = daysInMonth[mo];
      if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)))
        days = 29;
      d = days;
    }
  } else if (h >= 24) {
    h -= 24;
    d++;
    static const int daysInMonth[] = {0,  31, 28, 31, 30, 31, 30,
                                      31, 31, 30, 31, 30, 31};
    int days = daysInMonth[mo];
    if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)))
      days = 29;
    if (d > days) {
      d = 1;
      mo++;
      if (mo > 12) {
        mo = 1;
        y++;
      }
    }
  }
}

int GPSManager::getRawHour() { return _sysHour; }

int GPSManager::getRawMinute() { return _sysMin; }

double GPSManager::getHDOP() {
  // Return smoothed HDOP value for stable UI
  if (_smoothedHdop < 99.0) {
    return (double)_smoothedHdop;
  }
  return 99.9; // Default for no fix
}

double GPSManager::getAltitude() {
  // Use UBX parsed altitude
  return _altitude;
}

double GPSManager::getHeading() { return _heading; }

int GPSManager::getUpdateRate() {
  if (_smoothedHz < 0.1f)
    return _currentHz;
  return (int)(_smoothedHz + 0.5f);
}

unsigned long GPSManager::getUnixTimestamp() {
  struct tm t;
  t.tm_year = _sysYear - 1900;
  t.tm_mon = _sysMonth - 1;
  t.tm_mday = _sysDay;
  t.tm_hour = _sysHour;
  t.tm_min = _sysMin;
  t.tm_sec = _sysSec;
  t.tm_isdst = 0; // UTC
  return (unsigned long)mktime(&t);
}

unsigned long GPSManager::getBytesReceived() { return _totalBytesReceived; }

double GPSManager::distanceBetween(double lat1, double long1, double lat2,
                                   double long2) {
  return TinyGPSPlus::distanceBetween(lat1, long1, lat2, long2);
}

// --- CONFIGURATION IMPL ---

void GPSManager::sendUBX(const uint8_t *cmd, int len) {
  if (_gpsSerial) {
    _gpsSerial->write(cmd, len);
  }
}

void GPSManager::setGnssMode(uint8_t mode) {
  if (!_gpsSerial)
    return;

  // UBX-CFG-GNSS commands for different constellations
  // We construct these based on U-blox M8 protocol
  // Simplify: Trigger Cold Start or just minimal configuration?
  // Real implementation requires constructing complex payload.
  // For this prototype, we will handle RATE mostly as it's the primary
  // "User Visible" change Hz.

  // Mapping Mode to Hz Limit
  int targetRate = 1; // Default safer 1Hz for 9600 baud

  // Only allow higher rates if Baud Rate is sufficient (>38400)
  // 9600 baud can barely handle 10Hz if sentences are short, but with
  // full NMEA it chokes. Safe limit: 1Hz for 9600.
  if (_baudRate > 38400) {
    switch (mode) {
    case 0:
      targetRate = 10;
      break; // All (Standard 10Hz)
    case 1:
      targetRate = 10;
      break; // GPS+GLO+SBAS (Standard)
    case 2:
      targetRate = 10;
      break; // GPS+GAL+GLO+SBAS
    case 3:
      targetRate = 18;
      break; // GPS+GAL (18Hz Performance)
    case 4:
      targetRate = 18;
      break; // GPS Only (18Hz Max)
    case 5:
      targetRate = 1;
      break; // 1Hz Power Save
    default:
      targetRate = 10;
    }
  } else {
    // For 9600 baud, force 1Hz to be safe.
    targetRate = 1;
  }
  setFrequencyLimit(targetRate);
  _currentGnssMode = mode;

  // Manual Constellation Configuration for Performance Mode (Mode 3 & 4)
  if (mode == 3 || mode == 4) {
    // Disable GLONASS (Id 6) to save bandwidth for 18Hz
    // UBX-CFG-GNSS
    uint8_t disableGlo[] = {
        0xB5, 0x62, 0x06, 0x3E, 0x0C, 0x00, 0x00, 0x00,
        0x20, 0x01,                                    // ver, trk, config
        0x06, 0x08, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00 // GNSS 6 (GLO) Disable
    };
    // Checksum calculation and send would go here...
    // For now, relies on setFrequencyLimit doing the heavy lifting.
    // Ideally we send the full GNSS config packet like in begin().
  }
}

uint8_t GPSManager::getGnssMode() { return _currentGnssMode; }

void GPSManager::setDynamicModel(uint8_t modelIdx) {
  if (!_gpsSerial)
    return;

  // User Index to UBX DynModel Mapping
  // 0: Portable    -> 0
  // 1: Stationary  -> 2
  // 2: Pedestrian  -> 3
  // 3: Automotive  -> 4 (Default)
  // 4: At Sea      -> 5
  // 5: Air <1g     -> 6
  // 6: Air <2g     -> 7
  // 7: Air <4g     -> 8

  uint8_t ubxModel = 4; // Default Automotive
  switch (modelIdx) {
  case 0:
    ubxModel = 0;
    break;
  case 1:
    ubxModel = 2;
    break;
  case 2:
    ubxModel = 3;
    break;
  case 3:
    ubxModel = 4;
    break;
  case 4:
    ubxModel = 5;
    break;
  case 5:
    ubxModel = 6;
    break;
  case 6:
    ubxModel = 7;
    break;
  case 7:
    ubxModel = 8;
    break;
  }

  // UBX-CFG-NAV5 (0x06 0x24)
  uint8_t packet[] = {
      0xB5,     0x62, 0x06, 0x24, 0x24, 0x00, 0xFF, 0xFF, // Mask
      ubxModel,                                           // dynModel
      0x03,                                               // fixMode (3=Auto)
      0x00,     0x00, 0x00, 0x00,                         // fixedAlt
      0x10,     0x27, 0x00, 0x00,                         // fixedAltVar
      0x05,                                               // minElev
      0x00,                                               // drLimit
      0xFA,     0x00,                                     // pDop
      0xFA,     0x00,                                     // tDop
      0x64,     0x00,                                     // pAcc
      0x2C,     0x01,                                     // tAcc
      0x00,                                               // staticHoldThresh
      0x3C,                                               // dgpsTimeOut
      0x00,     0x00, 0x00, 0x00,                         // cnoThresh
      0x00,     0x00,                                     // reserved
      0x00,     0x00, 0x00, 0x00,                         // reserved
      0x00,     0x00                                      // Checksum
  };

  // Calc Checksum
  uint8_t ck_a = 0, ck_b = 0;
  for (int i = 2; i < 38; i++) {
    ck_a += packet[i];
    ck_b += ck_a;
  }
  packet[38] = ck_a;
  packet[39] = ck_b;

  sendUBX(packet, sizeof(packet));

  _currentDynModel = modelIdx;
  Preferences prefs;
  prefs.begin("laptimer", false);
  prefs.putInt("gnss_model", modelIdx);
  prefs.end();
}

void GPSManager::setSBASConfig(uint8_t regionIndex) {
  if (!_gpsSerial)
    return;

  // SBAS Configuration (UBX-CFG-SBAS 0x06 0x16)
  // We mainly want to enable/disable or set the PRN mask.
  // For simplicity in this "Blind" implementation, we will validly toggle
  // the system.

  // Region Index (Simplified for Asia/Indo):
  // 0: MSAS (Japan)    -> Enable
  // 1: GAGAN (India)   -> Enable
  // 2: SouthPAN (Aus)  -> Enable
  // 3: BDSBAS (China)  -> Enable
  // 4: KASS (Korea)    -> Enable
  // 5: DISABLE         -> Disable

  bool enable = true;
  uint32_t prnMask = 0; // 0 = Auto/All

  // Disable if index is 5
  if (regionIndex == 5) {
    enable = false; // Disable SBAS
  } else {
    // Enable for others
    prnMask = 0x00000000;
  }

  uint8_t mode = enable ? 0x01 : 0x00;

  uint8_t packet[] = {
      0xB5, 0x62, 0x06, 0x16, 0x08, 0x00,
      mode,                   // mode (Enable/Disable)
      0x03,                   // usage (Range+DiffCorr+Integrity)
      0x03,                   // maxSBAS (3 channels)
      0x00,                   // scanmode2 (PRN Mask Low - 0 for auto)
      0x00, 0x00, 0x00, 0x00, // scanmode1 (PRN Mask High)
      0x00, 0x00              // Checksum
  };

  // If we wanted to be rigorous:
  // WAAS PRNs: 131,133,135,138 -> Map to bits
  // Since we don't have the exact bitmask function handy and don't want
  // to break it, we assume 0 (Auto Scan) is sufficient for "Enable".
  // Disabling (Index >= 8) allows revert to raw GPS.

  uint8_t ck_a = 0, ck_b = 0;
  for (int i = 2; i < 14; i++) {
    ck_a += packet[i];
    ck_b += ck_a;
  }
  packet[14] = ck_a;
  packet[15] = ck_b;

  sendUBX(packet, sizeof(packet));

  _currentSBAS = regionIndex;
  Preferences prefs;
  prefs.begin("laptimer", false);
  prefs.putInt("gnss_sbas", regionIndex);
  prefs.end();
}

void GPSManager::setRpmEnabled(bool enabled) {
  _rpmEnabled = enabled;

  if (PIN_RPM_INPUT >= 0) {
    if (_rpmEnabled) {
      pinMode(PIN_RPM_INPUT, INPUT);
      attachInterrupt(digitalPinToInterrupt(PIN_RPM_INPUT), onPulse, FALLING);
    } else {
      detachInterrupt(digitalPinToInterrupt(PIN_RPM_INPUT));
      _currentRPM = 0;
      _rpmPulses = 0;
    }
  }

  // Save Preference
  Preferences prefs;
  prefs.begin("laptimer", false);
  prefs.putBool("rpm_enabled", enabled);
  prefs.end();
}

void GPSManager::setPPRIndex(int idx) {
  _currentPPR = 1.0;
  switch (idx) {
  case 0:
    _currentPPR = 1.0;
    break;
  case 1:
    _currentPPR = 0.5;
    break;
  case 2:
    _currentPPR = 2.0;
    break;
  case 3:
    _currentPPR = 3.0;
    break;
  case 4:
    _currentPPR = 4.0;
    break;
  }
}

void GPSManager::setFrequencyLimit(int freq) {
  if (!_gpsSerial)
    return;

  // UBX-CFG-RATE
  // rate = 1000 / freq
  uint16_t rateMs = 1000 / freq;

  uint8_t packet[] = {
      0xB5,
      0x62,
      0x06,
      0x08,
      0x06,
      0x00,
      (uint8_t)(rateMs & 0xFF),
      (uint8_t)((rateMs >> 8) & 0xFF), // measRate
      0x01,
      0x00, // navRate (always 1)
      0x01,
      0x00, // timeRef (GPS)
      0x00,
      0x00 // Checksum
  };

  uint8_t ck_a = 0, ck_b = 0;
  for (int i = 2; i < 12; i++) {
    ck_a += packet[i];
    ck_b += ck_a;
  }
  packet[12] = ck_a;
  packet[13] = ck_b;

  sendUBX(packet, sizeof(packet));
  _targetFreq = freq;
}

void GPSManager::setProjection(bool enabled) {
  _projectionEnabled = enabled;
  Preferences prefs;
  prefs.begin("laptimer", false);
  prefs.putBool("gnss_proj", enabled);
  prefs.end();
}

void GPSManager::setPins(int rx, int tx) {
  if (_rxPin == rx && _txPin == tx)
    return;

  _rxPin = rx;
  _txPin = tx;

  // Save to prefs
  Preferences prefs;
  prefs.begin("laptimer", false);
  prefs.putInt("gps_rx_pin", _rxPin);
  prefs.putInt("gps_tx_pin", _txPin);
  prefs.end();

  // Restart Serial
  if (_gpsSerial) {
    _gpsSerial->end();
    delay(100);
    _gpsSerial->begin(_baudRate, SERIAL_8N1, _rxPin, _txPin);

    // Re-apply config as module might have power cycled?
    // Actually ESP32 UART reset doesn't reset the GPS module itself,
    // but just in case we need to re-init communication.
    delay(100);
    setGnssMode(_currentGnssMode);
    setDynamicModel(_currentDynModel);
    setSBASConfig(_currentSBAS);
  }
}

void GPSManager::setBaud(int baud) {
  if (_baudRate == baud)
    return;

  // 1. Command GPS to switch (while still at old baud)
  configureGpsBaud(baud);

  _baudRate = baud;

  Preferences prefs;
  prefs.begin("laptimer", false);
  prefs.putInt("gps_baud", _baudRate);
  prefs.end();

  // 2. Switch ESP32 to new baud
  if (_gpsSerial) {
    delay(200); // Wait for module
    _gpsSerial->updateBaudRate(_baudRate);
    delay(100);
    // Re-apply config
    setGnssMode(_currentGnssMode);
    setDynamicModel(_currentDynModel);
    setSBASConfig(_currentSBAS);
  }
}

void GPSManager::configureGpsBaud(int targetBaud) {
  if (!_gpsSerial)
    return;

  // UBX-CFG-PRT (0x06 0x00)
  uint8_t packet[] = {
      0xB5,
      0x62,
      0x06,
      0x00,
      0x14,
      0x00,
      0x01,
      0x00,
      0x00,
      0x00, // PortID=1 (UART1)
      0xD0,
      0x08,
      0x00,
      0x00,                         // Mode (8N1)
      (uint8_t)(targetBaud & 0xFF), // Baud LSB
      (uint8_t)((targetBaud >> 8) & 0xFF),
      (uint8_t)((targetBaud >> 16) & 0xFF),
      (uint8_t)((targetBaud >> 24) & 0xFF), // Baud MSB
      0x07,
      0x00, // In Proto (UBX+NMEA+RTCM)
      0x03,
      0x00, // Out Proto (UBX+NMEA)
      0x00,
      0x00, // Flags
      0x00,
      0x00, // Reserved
      0x00,
      0x00 // Checksum
  };

  // Calc Checksum
  uint8_t ck_a = 0, ck_b = 0;
  for (int i = 2; i < 26; i++) {
    ck_a += packet[i];
    ck_b += ck_a;
  }
  packet[26] = ck_a;
  packet[27] = ck_b;

  sendUBX(packet, sizeof(packet));
}

void GPSManager::disableUnnecessarySentences() {
  if (!_gpsSerial)
    return;

  // Disable GSA (DOP and active satellites) - Not critical for racing
  uint8_t disableGSA[] = {
      0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x02, // NMEA-GxGSA
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // Disable on all ports
      0x01, 0x31                                      // Checksum
  };
  sendUBX(disableGSA, sizeof(disableGSA));
  delay(50);

  // Disable GSV (Satellites in view) - Not critical, uses lots of
  // bandwidth
  uint8_t disableGSV[] = {
      0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x03, // NMEA-GxGSV
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x38  // Checksum
  };
  sendUBX(disableGSV, sizeof(disableGSV));
  delay(50);

  // Disable GLL (Geographic position) - Redundant with RMC
  uint8_t disableGLL[] = {
      0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x01, // NMEA-GxGLL
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2A  // Checksum
  };
  sendUBX(disableGLL, sizeof(disableGLL));
  delay(50);

  // Keep enabled: GGA (Position), RMC (Recommended minimum), VTG
  // (Track/Speed) These are essential for racing/tracking
}
// UBX Binary Protocol Parser Implementation
// This file contains the UBX parser methods for GPSManager
// Append this to the end of GPSManager.cpp

void GPSManager::resetModule(bool cold) {
  if (!_gpsSerial)
    return;

  Serial.println(cold ? "GPS: Performing COLD START..."
                      : "GPS: Performing WARM START...");

  // UBX-CFG-RST (0x06 0x04)
  // Payload:
  // navBbrMask: 0xFFFF (Cold) or 0x01 (Warm)
  // resetMode: 0x01 (Hardware reset)
  uint8_t cfgRst[12] = {
      0xB5, 0x62, // Header
      0x06, 0x04, // Class/ID
      0x04, 0x00, // Length
      0x01, 0x00, // Mask (Warm Default)
      0x01,       // Reset Mode
      0x00,       // Reserved
      0x00, 0x00  // Checksum
  };

  if (cold) {
    cfgRst[6] = 0xFF; // navBbrMask LSB
    cfgRst[7] = 0xFF; // navBbrMask MSB
  }

  // Calculate Checksum
  uint8_t ckA = 0, ckB = 0;
  for (int i = 2; i < 10; i++) {
    ckA += cfgRst[i];
    ckB += ckA;
  }
  cfgRst[10] = ckA;
  cfgRst[11] = ckB;

  sendUBX(cfgRst, 12);

  // Reset internal state
  _hasValidFix = false;
  _satelliteCount = 0;
  _pdop = 99.9;

  // Re-init some configs after a short delay
  delay(500);
  begin();
}

void GPSManager::processUBXByte(uint8_t b) {
  switch (_ubxState) {
  case UBX_SYNC1:
    if (b == 0xB5) {
      _ubxState = UBX_SYNC2;
    }
    break;

  case UBX_SYNC2:
    if (b == 0x62) {
      _ubxState = UBX_CLASS;
      _ubxCkA = 0;
      _ubxCkB = 0;
    } else {
      _ubxState = UBX_SYNC1;
    }
    break;

  case UBX_CLASS:
    _ubxClass = b;
    _ubxCkA += b;
    _ubxCkB += _ubxCkA;
    _ubxState = UBX_ID;
    break;

  case UBX_ID:
    _ubxId = b;
    _ubxCkA += b;
    _ubxCkB += _ubxCkA;
    _ubxState = UBX_LEN1;
    break;

  case UBX_LEN1:
    _ubxLength = b;
    _ubxCkA += b;
    _ubxCkB += _ubxCkA;
    _ubxState = UBX_LEN2;
    break;

  case UBX_LEN2:
    _ubxLength |= (b << 8);
    _ubxCkA += b;
    _ubxCkB += _ubxCkA;
    _ubxPayloadIndex = 0;
    _ubxState = (_ubxLength > 0) ? UBX_PAYLOAD : UBX_CK_A;
    break;

  case UBX_PAYLOAD:
    // Always store byte if space exists
    if (_ubxPayloadIndex < sizeof(_ubxPayload)) {
      _ubxPayload[_ubxPayloadIndex] = b;
    }
    // Always increment index to track progress against _ubxLength
    _ubxPayloadIndex++;

    _ubxCkA += b;
    _ubxCkB += _ubxCkA;

    if (_ubxPayloadIndex >= _ubxLength) {
      // Packet Complete
      _ubxState = UBX_CK_A;
    }
    break;

  case UBX_CK_A:
    // We already calculated _ubxCkA from payload.
    // The byte b IS the received CK_A.
    // Wait, the calculated values include Class, ID, Len, Payload.
    // We compare calculated _ubxCkA with received b.
    if (b == _ubxCkA) {
      _ubxState = UBX_CK_B;
    } else {
      _ubxState = UBX_SYNC1; // Fail
    }
    break;

  case UBX_CK_B:
    if (b == _ubxCkB) {
      // Valid Packet!
      if (_ubxClass == 0x01 && _ubxId == 0x07) {
        parseUBXNavPvt();
      } else if (_ubxClass == 0x01 && _ubxId == 0x35) {
        parseUBXNavSat();
      } else if (_ubxClass == 0x01 && _ubxId == 0x04) {
        parseUBXNavDop();
      }
    }
    _ubxState = UBX_SYNC1;
    break;
  }
}

void GPSManager::parseUBXNavSat() {
  uint8_t numSats = _ubxPayload[5];

  // DEBUG
  // Serial.print("NAV-SAT: Sats=");
  // Serial.println(numSats);

  _satellites.clear();

  // Safety check
  if (numSats > 50)
    return;

  int offset = 8;
  for (int i = 0; i < numSats; i++) {
    if (offset + 12 > _ubxLength)
      break;
    if (offset + 12 > sizeof(_ubxPayload))
      break; // Buffer safety

    SatelliteInfo sat;
    sat.id = _ubxPayload[offset + 1];
    sat.snr = _ubxPayload[offset + 2];
    sat.elevation = (int8_t)_ubxPayload[offset + 3];
    sat.azimuth =
        (int16_t)(_ubxPayload[offset + 4] | (_ubxPayload[offset + 5] << 8));

    _satellites.push_back(sat);

    offset += 12;
  }
}

void GPSManager::parseUBXNavPvt() {
  if (_ubxLength < 92)
    return; // Invalid UBX-NAV-PVT message

  // Extract fix type (offset 20)
  _fixType = _ubxPayload[20];
  _hasValidFix = (_fixType == 0x02 || _fixType == 0x03 ||
                  _fixType == 0x04); // 2D, 3D, or GNSS+DR

  // Extract satellite count (offset 23)
  _satelliteCount = _ubxPayload[23];

  // EMA Smoothing for Satellite Count
  if (_smoothedSats < 0.1f)
    _smoothedSats = (float)_satelliteCount;
  else
    _smoothedSats = (_smoothedSats * 0.8f) + ((float)_satelliteCount * 0.2f);

  // Extract ground speed (offset 60, 4 bytes, mm/s)
  int32_t speedRaw = *((int32_t *)(&_ubxPayload[60]));
  _currentSpeed = (speedRaw / 1000.0) * 3.6; // Convert mm/s to km/h

  // STATIC NAVIGATION FILTER
  // Only update position/heading if speed is above deadzone OR if we moved
  // significantly (e.g. startup jump)

  // Extract proposed coords first
  int32_t lonRaw = *((int32_t *)(&_ubxPayload[24]));
  double newLon = lonRaw / 10000000.0;

  int32_t latRaw = *((int32_t *)(&_ubxPayload[28]));
  double newLat = latRaw / 10000000.0;

  // Calculate jump distance to see if this is initialization or teleport (not
  // just drift) Simple check: if _latitude is 0.0, we ALWAYS update. Or use
  // distance.

  bool significantJump = false;
  if (_latitude == 0.0 && _longitude == 0.0) {
    significantJump = true; // First fix
  } else {
    // We can use _gps.distanceBetween but we need to include TinyGPS++ headers
    // correctly or rely on member Actually we have distanceBetween helper
    // function
    double dist = distanceBetween(_latitude, _longitude, newLat, newLon);
    if (dist > 25.0)
      significantJump = true; // Teleport/Init > 25m
  }

  if (_currentSpeed > GPS_SPEED_DEADZONE || significantJump) {
    _longitude = newLon;
    _latitude = newLat;

    // Extract heading (offset 64, 4 bytes, 1e-5 degrees)
    int32_t headRaw = *((int32_t *)(&_ubxPayload[64]));
    _heading = headRaw / 100000.0;
  }

  // Extract altitude MSL (offset 36, 4 bytes, mm)
  int32_t altRaw = *((int32_t *)(&_ubxPayload[36]));
  _altitude = altRaw / 1000.0; // Convert mm to meters

  // Extract PDOP (offset 76, 2 bytes, 1e-2)
  uint16_t pdopRaw;
  memcpy(&pdopRaw, &_ubxPayload[76], 2);
  _pdop = pdopRaw / 100.0;

  // Extract date/time if valid (offset 11 - valid flags)
  // Bit 0: Valid Date, Bit 1: Valid Time. Both must be 1 (0x03)
  if ((_ubxPayload[11] & 0x03) == 0x03) {
    uint16_t year;
    memcpy(&year, &_ubxPayload[4], 2);
    _sysYear = year;
    _sysMonth = _ubxPayload[6];
    _sysDay = _ubxPayload[7];
    _sysHour = _ubxPayload[8];
    _sysMin = _ubxPayload[9];
    _sysSec = _ubxPayload[10];
  }

  // Update counters
  _updatesCount++;
  _lastUpdateTime = millis();
}

void GPSManager::parseUBXNavDop() {
  if (_ubxLength < 18)
    return;

  uint16_t gdopRaw, pdopRaw, tdopRaw, vdopRaw, hdopRaw;
  memcpy(&gdopRaw, &_ubxPayload[4], 2);
  memcpy(&pdopRaw, &_ubxPayload[6], 2);
  memcpy(&tdopRaw, &_ubxPayload[8], 2);
  memcpy(&vdopRaw, &_ubxPayload[10], 2);
  memcpy(&hdopRaw, &_ubxPayload[12], 2);

  double newHdop = hdopRaw / 100.0;
  _hdop = newHdop;

  // EMA Smoothing for HDOP (Horizontal is what users care about)
  if (_smoothedHdop > 99.0) {
    _smoothedHdop = (float)newHdop;
  } else {
    // Smoother transition: 80% old, 20% new
    _smoothedHdop = (_smoothedHdop * 0.8f) + ((float)newHdop * 0.2f);
  }
}

bool GPSManager::detectBaudRate() {
  // Check 115200 FIRST as it's our target rate
  int rates[] = {115200, 9600, 19200, 38400, 57600};

  Serial.println("GPS: Auto-detecting baud rate...");

  for (int i = 0; i < 5; i++) {
    int rate = rates[i];
    Serial.print("Trying ");
    Serial.print(rate);
    Serial.print("... ");

    _gpsSerial->begin(rate, SERIAL_8N1, _rxPin, _txPin);
    unsigned long start = millis();
    bool validDataFound = false;

    // Listen for 1200ms to catch at least one message even at 1Hz
    uint8_t lastChar = 0;
    while (millis() - start < 1200) {
      if (_gpsSerial->available()) {
        uint8_t c = _gpsSerial->read();

        // Check for UBX Sync (0xB5 0x62)
        if (lastChar == 0xB5 && c == 0x62) {
          validDataFound = true;
          break;
        }
        // Check for NMEA Start ($G)
        if (lastChar == '$' && c == 'G') {
          validDataFound = true;
          break;
        }
        lastChar = c;
      }
    }

    if (validDataFound) {
      Serial.println("FOUND!");
      _baudRate = rate;
      // Keep Serial2 open at this rate
      return true;
    } else {
      Serial.println("No valid data.");
      _gpsSerial->end();
      delay(50); // Small pause
    }
  }
  return false;
}
