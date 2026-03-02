#include "SessionManager.h"
#include <SPI.h>

// Queue item structure (implicitly just char* for now)

void SessionManager::begin() {
  _logging = false;

  Serial.printf("SD Init: SCK=%d, MISO=%d, MOSI=%d, CS=%d\n", PIN_SD_SCLK,
                PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

  // Gunakan instans SPI khusus untuk Kartu SD (VSPI)
  // Ini menghindari konflik dengan Tampilan/Sentuh (biasanya pada HSPI)
  SPIClass *sdSpi = new SPIClass(VSPI);
  sdSpi->begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

  delay(10); // Tunggu SPI stabil

  // Teruskan SPI khusus ke SD.begin
  if (!SD.begin(PIN_SD_CS, *sdSpi, 4000000)) { // Kecepatan aman 4MHz
    Serial.println("SD Card Init Failed!");
  } else {
    Serial.println("SD Card Ready");
    if (!SD.exists("/sessions")) {
      SD.mkdir("/sessions");
    }
  }

  // Initialize Logging Queue (Holds 100 entries)
  _logQueue = xQueueCreate(100, sizeof(LogEntry));

  // Start Logging Task (Pinned to Core 0 to leave Core 1 for UI/Arduino)
  xTaskCreatePinnedToCore(loggingTask, "LoggingTask", 4096, this, 1,
                          &_loggingTaskHandle, 0);
}

bool SessionManager::startSession() {
  if (_logging)
    return true;

  String filename = createFilename();
  _logFile = SD.open(filename, FILE_WRITE);

  if (_logFile) {
    _logging = true;
    _currentFilename = filename;

    // Write Binary Header to identify format
    uint32_t magic = 0x52434D42; // "B M C R" (Binary Much Racing)
    _logFile.write((uint8_t *)&magic, 4);

    logData("Time,Lat,Lon,Speed,Sats,Alt,Heading");

    Serial.println("Started logging to: " + filename);
    return true;
  }
  return false;
}

void SessionManager::stopSession() {
  if (_logging) {
    // Wait a bit for queue to flush?
    // We can't strictly wait essentially, but let's give it a moment
    unsigned long startWait = millis();
    while (uxQueueMessagesWaiting(_logQueue) > 0 &&
           millis() - startWait < 500) {
      delay(10);
    }

    _logging = false;

    // Slight delay to ensure task sees _logging=false or finishes last write
    delay(50);

    if (_logFile) {
      _logFile.close();
      Serial.println("Session Stopped");
    }
  }
}

void SessionManager::logData(String dataLine) {
  if (_logging) {
    LogEntry entry;
    entry.type = 1; // String/Metadata
    entry.metadata = strdup(dataLine.c_str());
    if (entry.metadata) {
      if (xQueueSend(_logQueue, &entry, 0) != pdTRUE) {
        free(entry.metadata);
        Serial.println("Log Queue Full (Str)!");
      }
    }
  }
}

void SessionManager::logData(LogPacket &packet) {
  if (_logging) {
    LogEntry entry;
    entry.type = 0; // Binary Packet
    entry.packet = packet;
    if (xQueueSend(_logQueue, &entry, 0) != pdTRUE) {
      // Discard packet silently if full
    }
  }
}

void SessionManager::loggingTask(void *parameter) {
  SessionManager *self = (SessionManager *)parameter;
  LogEntry entry;

  while (true) {
    if (xQueueReceive(self->_logQueue, &entry, portMAX_DELAY) == pdTRUE) {
      if (self->_logging && self->_logFile) {
        if (entry.type == 0) {
          // Binary Packet
          self->_logFile.write((uint8_t *)&entry.packet, sizeof(LogPacket));
        } else {
          // String Metadata
          self->_logFile.println(entry.metadata);
          free(entry.metadata);
        }
        // Optional: Flush every N lines or rely on close()
      } else if (entry.type == 1) {
        // Still Need to free memory if dropped!
        free(entry.metadata);
      }
    }
  }
}

String SessionManager::createFilename() {
  int i = 0;
  String fn;
  do {
    fn = "/sessions/run_" + String(i) + ".csv";
    i++;
  } while (SD.exists(fn));
  return fn;
}

void SessionManager::appendToHistoryIndex(String filename, String date,
                                          int laps, unsigned long bestLap,
                                          String type) {
  File indexFile = SD.open("/history.csv", FILE_APPEND);
  if (!indexFile) {
    indexFile = SD.open("/history.csv", FILE_WRITE);
  }

  if (indexFile) {
    // Format: NamaFile,Tanggal,Lap,LapTerbaik,Tipe
    String line = filename + "," + date + "," + String(laps) + "," +
                  String(bestLap) + "," + type;
    indexFile.println(line);
    indexFile.close();
    Serial.println("Added to history index: " + line);
  } else {
    Serial.println("Failed to open history index");
  }
}

String SessionManager::loadHistoryIndex() {
  if (!SD.exists("/history.csv"))
    return "";

  File indexFile = SD.open("/history.csv", FILE_READ);
  if (!indexFile)
    return "";

  String content = "";
  content.reserve(indexFile.size());
  while (indexFile.available()) {
    content += (char)indexFile.read();
  }
  indexFile.close();
  return content;
}

std::vector<HistoryItem> SessionManager::loadHistoryItems() {
  std::vector<HistoryItem> list;
  if (!SD.exists("/history.csv"))
    return list;

  File indexFile = SD.open("/history.csv", FILE_READ);
  if (!indexFile)
    return list;

  // Read line by line to avoid heap fragmentation
  while (indexFile.available()) {
    String line = indexFile.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
      continue;

    // Parse CSV: NamaFile,Tanggal,LapCount,BestLap,Type
    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    int c3 = line.indexOf(',', c2 + 1);
    int c4 = line.indexOf(',', c3 + 1);

    if (c1 > 0 && c2 > 0 && c3 > 0) {
      HistoryItem item;
      item.filename = line.substring(0, c1);
      item.date = line.substring(c1 + 1, c2);
      item.laps = line.substring(c2 + 1, c3).toInt();
      item.bestLap =
          line.substring(c3 + 1, (c4 > 0) ? c4 : line.length()).toInt();

      if (c4 > 0) {
        item.type = line.substring(c4 + 1);
        item.type.trim();
      } else {
        item.type = "TRACK";
      }
      list.insert(list.begin(), item); // Newest first
    }
  }

  indexFile.close();
  return list;
}

bool SessionManager::deleteSession(String filename) {
  // 1. Remove the actual log file
  if (SD.exists(filename)) {
    SD.remove(filename);
    Serial.println("Deleted log file: " + filename);
  } else {
    Serial.println("Log file not found: " + filename);
    // Proceed to clean index anyway
  }

  // 2. Rewrite History Index
  if (!SD.exists("/history.csv"))
    return false;

  File inFile = SD.open("/history.csv", FILE_READ);
  if (!inFile)
    return false;

  String tempPath = "/history.tmp";
  File outFile = SD.open(tempPath, FILE_WRITE);
  if (!outFile) {
    inFile.close();
    return false;
  }

  bool found = false;
  while (inFile.available()) {
    String line = inFile.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
      continue;

    // Check if this line contains the filename
    // Format: /sessions/run_X.csv,Date,...
    int c1 = line.indexOf(',');
    if (c1 > 0) {
      String fName = line.substring(0, c1);
      if (fName == filename) {
        found = true;
        Serial.println("Skipping deleted entry in index: " + fName);
        continue; // Skip this line
      }
    }
    outFile.println(line);
  }

  inFile.close();
  outFile.close();

  SD.remove("/history.csv");
  SD.rename(tempPath, "/history.csv");

  return true;
}

bool SessionManager::getSDStatus(uint64_t &total, uint64_t &used) {
  if (!SD.totalBytes())
    return false; // Periksa apakah terpasang/valid
  total = SD.totalBytes();
  used = SD.usedBytes();
  return true;
}

SessionManager::SDTestResult
SessionManager::runFullTest(void (*progressCallback)(int, String)) {
  SDTestResult res;
  res.success = false;
  res.readSpeedKBps = 0;
  res.writeSpeedKBps = 0;

  if (!SD.totalBytes()) {
    res.cardType = "NO CARD";
    return res;
  }

  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();

  // Format Size Label
  float totalGB = total / (1024.0 * 1024.0 * 1024.0);
  res.sizeLabel = String(totalGB, 1) + " GB";

  float usedMB = used / (1024.0 * 1024.0);
  res.usedLabel = String(usedMB, 0) + " MB";

  sdcard_type_t t = SD.cardType();
  if (t == CARD_MMC)
    res.cardType = "MMC";
  else if (t == CARD_SD)
    res.cardType = "SDSC";
  else if (t == CARD_SDHC)
    res.cardType = "SDHC";
  else
    res.cardType = "UNKNOWN";

  // Benchmark Write
  if (progressCallback)
    progressCallback(0, "Writing...");

  uint8_t *buf = (uint8_t *)malloc(1024); // Reduced to 1KB for power stability
  if (!buf) {
    Serial.println("SD Test: Malloc Failed");
    return res;
  }
  memset(buf, 0xAA, 1024);
  String testFile = "/test_bench.bin";

  unsigned long start = millis();
  File f = SD.open(testFile, FILE_WRITE);
  if (f) {
    // Write 1MB (1024 * 1024)
    int chunks = 1024;
    for (int i = 0; i < chunks; i++) {
      vTaskDelay(
          pdMS_TO_TICKS(1)); // Better than yield() for WDT and scheduling
      f.write(buf, 1024);
      if (i % 20 == 0 && progressCallback) { // Update frequency (more frequent)
        int p = (i * 50) / chunks;           // 0-50%
        progressCallback(p, "Writing...");
      }
    }
    f.close();
    unsigned long duration = millis() - start;
    if (duration > 0) {
      res.writeSpeedKBps = 1024.0 / (duration / 1000.0);
    }
  } else {
    free(buf);
    return res; // Write failed
  }

  // Benchmark Read
  if (progressCallback)
    progressCallback(50, "Reading...");

  start = millis();
  f = SD.open(testFile, FILE_READ);
  if (f) {
    long len = f.size();
    long pos = 0;
    int chunks = 0;

    while (f.available()) {
      vTaskDelay(
          pdMS_TO_TICKS(1)); // Better than yield() for WDT and scheduling
      int bytesRead = f.read(buf, 1024);
      if (bytesRead <= 0)
        break; // Safety break - prevent hang at end of file
      pos += bytesRead;
      chunks++;

      if (chunks % 20 == 0 &&
          progressCallback) {          // Update frequency (more frequent)
        int p = 50 + (pos * 50) / len; // 50-100%
        progressCallback(p, "Reading...");
      }
    }
    f.close();
    unsigned long duration = millis() - start;
    if (duration > 0) {
      res.readSpeedKBps = 1024.0 / (duration / 1000.0);
    }
  } else {
    free(buf);
    return res; // Read failed
  }

  // Cleanup
  if (progressCallback)
    progressCallback(100, "Done!");
  SD.remove(testFile);
  free(buf);
  res.success = true;
  return res;
}

SessionManager::SessionAnalysis
SessionManager::analyzeSession(String filename) {
  SessionAnalysis result;
  result.totalTime = 0;
  result.totalDistance = 0;
  result.maxSpeed = 0;
  result.avgSpeed = 0;
  result.maxRPM = 0;
  result.validLaps = 0;
  result.bestLap = 0;
  result.time0to60 = 0;
  result.time0to100 = 0;
  result.time100to200 = 0;
  result.time400m = 0;
  result.filename = filename;

  Serial.printf("Analyze: Opening %s...\n", filename.c_str());
  File f = SD.open(filename, FILE_READ);
  if (!f) {
    Serial.println("Analyze: Error - Could not open file!");
    return result;
  }

  uint32_t magic = 0;
  if (f.available() >= 4) {
    f.read((uint8_t *)&magic, 4);
  }

  bool isBinary = (magic == 0x52434D42);
  if (!isBinary) {
    f.seek(0); // Reset for CSV
  }

  unsigned long long firstTime = 0;
  unsigned long long lastTime = 0;
  double prevLat = 0;
  double prevLon = 0;
  bool firstPoint = true;

  // Drag Specifics
  bool dragStarted = false;
  unsigned long long dragStartTime = 0;
  double dragStartLat = 0, dragStartLon = 0;
  unsigned long long time100 = 0;

  int packetsFound = 0;
  int pointsFound = 0;

  while (f.available()) {
    unsigned long long t = 0;
    double lat = 0, lon = 0;
    float speed = 0;
    int rpm = 0;
    bool validPoint = false;

    if (isBinary) {
      uint8_t b = f.peek();
      if (b == 0x55) { // header byte
        if (f.available() >= sizeof(LogPacket)) {
          LogPacket p;
          f.read((uint8_t *)&p, sizeof(LogPacket));
          if (p.header == 0xAA55) {
            t = p.timestamp;
            lat = p.lat / 1e7;
            lon = p.lon / 1e7;
            speed = p.speed / 10.0f;
            rpm = p.rpm;
            validPoint = true;
            packetsFound++;
          } else {
            f.seek(f.position() - (sizeof(LogPacket) - 1));
          }
        } else {
          f.read();
        }
      } else if (b >= 32 && b <= 126) { // ASCII
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("LAP,")) {
          int lastComma = line.lastIndexOf(',');
          if (lastComma > 0) {
            unsigned long lapTime =
                strtoul(line.substring(lastComma + 1).c_str(), NULL, 10);
            if (lapTime > 0) {
              result.lapTimes.push_back(lapTime);
              result.validLaps++;
              if (result.bestLap == 0 || lapTime < result.bestLap)
                result.bestLap = lapTime;
            }
          }
        } else if (line.startsWith("SECTOR,")) {
          int c1 = line.indexOf(',');
          int c2 = line.indexOf(',', c1 + 1);
          int c3 = line.indexOf(',', c2 + 1);
          if (c3 > 0) {
            int num = line.substring(c2 + 1, c3).toInt();
            unsigned long st =
                strtoul(line.substring(c3 + 1).c_str(), NULL, 10);
            if (num == 1)
              result.sector1.push_back(st);
            else if (num == 2)
              result.sector2.push_back(st);
            else if (num == 3)
              result.sector3.push_back(st);
          }
        }
      } else {
        f.read();
      }
    } else {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() > 0 &&
          (isdigit(line.charAt(0)) || line.charAt(0) == '-')) {
        int p[5];
        p[0] = line.indexOf(',');
        for (int i = 1; i < 4; i++)
          p[i] = (p[i - 1] != -1) ? line.indexOf(',', p[i - 1] + 1) : -1;
        if (p[3] > 0) {
          t = strtoull(line.substring(0, p[0]).c_str(), NULL, 10);
          lat = line.substring(p[0] + 1, p[1]).toDouble();
          lon = line.substring(p[1] + 1, p[2]).toDouble();
          speed = line.substring(p[2] + 1, p[3]).toFloat();
          validPoint = true;
        }
      }
    }

    if (validPoint) {
      pointsFound++;
      if (firstPoint) {
        firstTime = t;
        prevLat = lat;
        prevLon = lon;
        firstPoint = false;
        Serial.printf("Analyze: First point t=%llu, spd=%.1f\n", t, speed);
      } else {
        float dLat = (lat - prevLat) * DEG_TO_RAD;
        float dLon = (lon - prevLon) * DEG_TO_RAD;
        float a = sin(dLat / 2.) * sin(dLat / 2.) +
                  cos(prevLat * DEG_TO_RAD) * cos(lat * DEG_TO_RAD) *
                      sin(dLon / 2.) * sin(dLon / 2.);
        result.totalDistance += 6371 * (2 * atan2(sqrt(a), sqrt(1 - a)));
        prevLat = lat;
        prevLon = lon;
      }
      lastTime = t;
      if (speed > result.maxSpeed)
        result.maxSpeed = speed;
      if (rpm > result.maxRPM)
        result.maxRPM = rpm;

      if (!dragStarted && speed > 2.0f) {
        dragStarted = true;
        dragStartTime = t;
        dragStartLat = lat;
        dragStartLon = lon;
        Serial.printf("Analyze: Drag START at t=%llu\n", t);
      }

      if (dragStarted) {
        unsigned long runTime = (unsigned long)(t - dragStartTime);
        if (result.time0to60 == 0 && speed >= 60.0f) {
          result.time0to60 = runTime;
          Serial.printf("Analyze: 0-60 in %.2fs\n", runTime / 1000.0);
        }
        if (result.time0to100 == 0 && speed >= 100.0f) {
          result.time0to100 = runTime;
          time100 = t;
          Serial.printf("Analyze: 0-100 in %.2fs\n", runTime / 1000.0);
        }
        if (time100 > 0 && result.time100to200 == 0 && speed >= 200.0f) {
          result.time100to200 = (unsigned long)(t - time100);
          Serial.printf("Analyze: 100-200 in %.2fs\n",
                        result.time100to200 / 1000.0);
        }

        float dLat = (lat - dragStartLat) * DEG_TO_RAD;
        float dLon = (lon - dragStartLon) * DEG_TO_RAD;
        float a = sin(dLat / 2) * sin(dLat / 2) +
                  cos(dragStartLat * DEG_TO_RAD) * cos(lat * DEG_TO_RAD) *
                      sin(dLon / 2) * sin(dLon / 2);
        float distM = 6371000.0f * (2 * atan2(sqrt(a), sqrt(1 - a)));
        if (result.time400m == 0 && distM >= 402.336f) {
          result.time400m = runTime;
          Serial.printf("Analyze: 402m in %.2fs\n", runTime / 1000.0);
        }
      }
    }
  }
  f.close();
  Serial.printf(
      "Analyze: Done. Found %d points from %d packets. Max Speed: %.1f\n",
      pointsFound, packetsFound, result.maxSpeed);

  if (lastTime > firstTime) {
    result.totalTime = (unsigned long)(lastTime - firstTime);
    float hours = result.totalTime / 3600000.0f;
    if (hours > 0)
      result.avgSpeed = result.totalDistance / hours;
  }

  return result;
}

// --- Predictive Timing ---
bool SessionManager::loadBestLapAsReference(String filename) {
  referenceLap.clear();
  File f = SD.open(filename, FILE_READ);
  if (!f)
    return false;

  // Pass 1: Find Best Lap Index
  int bestLapIdx = -1;
  unsigned long bestTime = 0;
  int currentLap = 0; // 0-indexed count

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.startsWith("LAP,")) {
      // LAP,Count,Time
      int lastComma = line.lastIndexOf(',');
      if (lastComma > 0) {
        unsigned long t =
            strtoul(line.substring(lastComma + 1).c_str(), NULL, 10);
        if (bestLapIdx == -1 || t < bestTime) {
          bestTime = t;
          bestLapIdx = currentLap;
        }
      }
      currentLap++;
    }
  }

  if (bestLapIdx == -1) {
    f.close();
    return false; // No laps found
  }

  // Pass 2: Extract Points for Best Lap
  f.seek(0);
  currentLap = 0;
  bool collecting = (currentLap == bestLapIdx);

  double prevLat = 0, prevLon = 0;
  float totalDist = 0;
  unsigned long lapStartTime = 0;
  bool firstPointInLap = true;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
      continue;

    if (line.startsWith("LAP,")) {
      if (currentLap == bestLapIdx) {
        break;
      }
      currentLap++;
      collecting = (currentLap == bestLapIdx);
      if (collecting) {
        totalDist = 0;
        firstPointInLap = true;
        lapStartTime = 0;
      }
      continue;
    }

    if (collecting && isdigit(line.charAt(0))) {
      int p1 = line.indexOf(',');
      int p2 = line.indexOf(',', p1 + 1);
      int p3 = line.indexOf(',', p2 + 1);
      if (p1 > 0 && p2 > 0 && p3 > 0) {
        unsigned long long t =
            strtoull(line.substring(0, p1).c_str(), NULL, 10);
        double lat = line.substring(p1 + 1, p2).toDouble();
        double lon = line.substring(p2 + 1, p3).toDouble();

        if (firstPointInLap) {
          lapStartTime = t;
          prevLat = lat;
          prevLon = lon;
          totalDist = 0;
          firstPointInLap = false;
          referenceLap.push_back({0.0f, 0});
        } else {
          float dLat = (lat - prevLat) * DEG_TO_RAD;
          float dLon = (lon - prevLon) * DEG_TO_RAD;
          float a = sin(dLat / 2) * sin(dLat / 2) +
                    cos(prevLat * DEG_TO_RAD) * cos(lat * DEG_TO_RAD) *
                        sin(dLon / 2) * sin(dLon / 2);
          float c = 2 * atan2(sqrt(a), sqrt(1 - a));
          float dist = 6371000 * c;

          if (dist > 0.1f) {
            totalDist += dist;
            unsigned long relTime = t - lapStartTime;
            referenceLap.push_back({totalDist, (uint32_t)relTime});
            prevLat = lat;
            prevLon = lon;
          }
        }
      }
    }
  }

  f.close();
  return !referenceLap.empty();
}

void SessionManager::promoteLastLapToReference() {
  if (_currentFilename.length() == 0)
    return;

  // We need to read the points from the LAST completed lap in the current
  // file. Since we just finished a lap, it's the lap before the current one
  // (if we just incremented lapCount). In RaceScreen.cpp, we call this AFTER
  // incrementing _lapCount. So we want the points for lap (_lapCount - 1).

  // Simple implementation: Load the file and find the last lap points.
  // We can reuse parts of loadBestLapAsReference logic but specifically for
  // the LAST lap.

  File f = SD.open(_currentFilename, FILE_READ);
  if (!f)
    return;

  // Find the last "LAP," marker to identify where the last lap ended
  // And the one before it to find where it started.
  std::vector<ReferencePoint> newRef;

  uint32_t magic = 0;
  if (f.available() >= 4) {
    f.read((uint8_t *)&magic, 4);
  }
  bool isBinary = (magic == 0x52434D42);
  f.seek(isBinary ? 4 : 0);

  // We'll scan for all LAP markers and keep track of positions
  std::vector<unsigned long> lapMarkers;
  while (f.available()) {
    unsigned long pos = f.position();
    String line = f.readStringUntil('\n');
    if (line.startsWith("LAP,")) {
      lapMarkers.push_back(pos);
    }
  }

  if (lapMarkers.empty()) {
    f.close();
    return;
  }

  // The last lap started at the marker before the last one, or at start if
  // only 1 lap exists
  unsigned long startPos = (lapMarkers.size() >= 2)
                               ? lapMarkers[lapMarkers.size() - 2]
                               : (isBinary ? 4 : 0);
  unsigned long endPos = lapMarkers.back();

  f.seek(startPos);
  double prevLat = 0, prevLon = 0;
  float totalDist = 0;
  unsigned long lapStartTime = 0;
  bool firstPoint = true;

  while (f.position() < endPos && f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.startsWith("LAP,"))
      continue;

    if (isdigit(line.charAt(0))) {
      int p1 = line.indexOf(',');
      int p2 = line.indexOf(',', p1 + 1);
      int p3 = line.indexOf(',', p2 + 1);
      if (p1 > 0 && p2 > 0 && p3 > 0) {
        unsigned long long t =
            strtoull(line.substring(0, p1).c_str(), NULL, 10);
        double lat = line.substring(p1 + 1, p2).toDouble();
        double lon = line.substring(p2 + 1, p3).toDouble();

        if (firstPoint) {
          lapStartTime = t;
          prevLat = lat;
          prevLon = lon;
          newRef.push_back({0.0f, 0});
          firstPoint = false;
        } else {
          float dLat = (lat - prevLat) * DEG_TO_RAD;
          float dLon = (lon - prevLon) * DEG_TO_RAD;
          float a = sin(dLat / 2) * sin(dLat / 2) +
                    cos(prevLat * DEG_TO_RAD) * cos(lat * DEG_TO_RAD) *
                        sin(dLon / 2) * sin(dLon / 2);
          float c = 2 * atan2(sqrt(a), sqrt(1 - a));
          float dist = 6371000 * c;

          if (dist > 0.1f) {
            totalDist += dist;
            newRef.push_back({totalDist, (uint32_t)(t - lapStartTime)});
            prevLat = lat;
            prevLon = lon;
          }
        }
      }
    }
  }

  f.close();

  if (!newRef.empty()) {
    referenceLap = newRef;
    Serial.println("PREDICTIVE: Promoted last lap to reference!");
  }
}

float SessionManager::getReferenceTime(float distance) {
  if (referenceLap.empty())
    return -1.0;

  auto it = std::lower_bound(
      referenceLap.begin(), referenceLap.end(), distance,
      [](const ReferencePoint &a, float val) { return a.distance < val; });

  if (it == referenceLap.begin())
    return referenceLap[0].time;
  if (it == referenceLap.end())
    return referenceLap.back().time;

  const ReferencePoint &p2 = *it;
  const ReferencePoint &p1 = *(it - 1);

  float distDelta = p2.distance - p1.distance;
  if (distDelta < 0.1)
    return p1.time;

  float ratio = (distance - p1.distance) / distDelta;
  return p1.time + (p2.time - p1.time) * ratio;
}

bool SessionManager::exportSessionToGPX(String filename) {
  if (!SD.exists(filename))
    return false;

  File csvFile = SD.open(filename, FILE_READ);
  if (!csvFile)
    return false;

  String gpxFilename = filename;
  gpxFilename.replace(".csv", ".gpx");
  File gpxFile = SD.open(gpxFilename, FILE_WRITE);
  if (!gpxFile) {
    csvFile.close();
    return false;
  }

  gpxFile.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
  gpxFile.println("<gpx version=\"1.1\" creator=\"MuchRacing\" "
                  "xmlns=\"http://www.topografix.com/GPX/1/1\">");
  gpxFile.println("  <trk>");
  gpxFile.println("    <name>Exported Session</name>");
  gpxFile.println("    <trkseg>");

  while (csvFile.available()) {
    String line = csvFile.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || !isdigit(line.charAt(0)))
      continue;

    // Time,Lat,Lon,Speed...
    int p1 = line.indexOf(',');
    int p2 = line.indexOf(',', p1 + 1);
    int p3 = line.indexOf(',', p2 + 1);
    int p4 = line.indexOf(',', p3 + 1);

    if (p1 > 0 && p2 > 0 && p3 > 0) {
      String ts = line.substring(0, p1);
      double lat = line.substring(p1 + 1, p2).toDouble();
      double lon = line.substring(p2 + 1, p3).toDouble();

      gpxFile.printf("      <trkpt lat=\"%.7f\" lon=\"%.7f\">\n", lat, lon);

      // Convert Unix timestamp (ms) to ISO8601
      unsigned long long ms = strtoull(ts.c_str(), NULL, 10);
      time_t t = ms / 1000;
      struct tm *tm_info = gmtime(&t);
      char timeBuf[30];
      strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", tm_info);
      gpxFile.printf("        <time>%s</time>\n", timeBuf);

      gpxFile.println("      </trkpt>");
    }
  }

  gpxFile.println("    </trkseg>");
  gpxFile.println("  </trk>");
  gpxFile.println("</gpx>");

  gpxFile.close();
  csvFile.close();
  return true;
}

std::vector<GeoPoint> SessionManager::getLapPoints(String filename,
                                                   int lapIdx) {
  std::vector<GeoPoint> points;
  File f = SD.open(filename, FILE_READ);
  if (!f) {
    Serial.println("Replay: Failed to open " + filename);
    return points;
  }

  int pointsFound = 0;

  uint32_t magic = 0;
  if (f.available() >= 4)
    f.read((uint8_t *)&magic, 4);
  bool isBinary = (magic == 0x52434D42);
  if (!isBinary)
    f.seek(0);

  bool collecting =
      (!isBinary && lapIdx == 0); // CSV default: auto-collect lap 0
  if (isBinary)
    collecting =
        true; // Binary logs (Drag) usually have no lap markers, collect all

  Serial.printf("Replay: Extracting points for lap %d from %s (binary: %s)\n",
                lapIdx, filename.c_str(), isBinary ? "YES" : "NO");

  while (f.available()) {
    if (isBinary) {
      uint8_t b = f.peek();
      if (b == 0x55) { // Match header
        if (f.available() >= sizeof(LogPacket)) {
          LogPacket lp;
          f.read((uint8_t *)&lp, sizeof(LogPacket));
          if (lp.header == 0xAA55 && collecting) {
            GeoPoint gp;
            gp.t = lp.timestamp;
            gp.lat = lp.lat / 1e7;
            gp.lon = lp.lon / 1e7;
            gp.speed = lp.speed / 10.0f;
            gp.rpm = lp.rpm;
            gp.gX = lp.accX / 100.0f;
            gp.gY = lp.accY / 100.0f;
            gp.lean = lp.tilt / 10.0f;
            points.push_back(gp);
            pointsFound++;
          }
        } else
          f.read();
      } else {
        // Check for ASCII (e.g. LAP, markers inserted in binary stream)
        if (b >= 32 && b <= 126) {
          String line = f.readStringUntil('\n');
          if (line.startsWith("LAP,")) {
            int c1 = 4, c2 = line.indexOf(',', c1);
            if (c2 > 0) {
              int idx = line.substring(c1, c2).toInt();
              unsigned long lTime = line.substring(c2 + 1).toInt();
              if (idx == lapIdx) {
                if (lTime == 0)
                  collecting = true;
                else {
                  if (collecting)
                    break;
                } // End of lap
              } else {
                if (collecting)
                  break;
              } // Start of next lap
            }
          }
        } else
          f.read();
      }
    } else {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0)
        continue;

      if (line.startsWith("LAP,")) {
        int c1 = 4; // After "LAP,"
        int c2 = line.indexOf(',', c1);
        if (c2 > 0) {
          int idx = line.substring(c1, c2).toInt();
          unsigned long lTime = line.substring(c2 + 1).toInt();

          if (idx == lapIdx) {
            if (lTime == 0) {
              collecting = true; // Explicit start of our lap
              pointsFound = 0;   // Reset if we were collecting by fallback
              points.clear();
            } else {
              if (collecting) {
                // End of our lap
                break;
              }
            }
          } else {
            if (collecting) {
              // We were collecting but now entered a different lap tag
              break;
            }
          }
        }
        continue;
      }

      if (collecting && isdigit(line.charAt(0))) {
        int p[11]; // Positions of commas
        p[0] = line.indexOf(',');
        for (int i = 1; i < 11; i++) {
          if (p[i - 1] != -1)
            p[i] = line.indexOf(',', p[i - 1] + 1);
          else
            p[i] = -1;
        }

        if (p[0] > 0 && p[1] > 0 && p[2] > 0) {
          GeoPoint gp;
          gp.t = strtoull(line.substring(0, p[0]).c_str(), NULL, 10);
          gp.lat = line.substring(p[0] + 1, p[1]).toDouble();
          gp.lon = line.substring(p[1] + 1, p[2]).toDouble();
          if (p[2] > 0 && p[3] > 0)
            gp.speed = line.substring(p[2] + 1, p[3]).toFloat();
          else
            gp.speed = 0;
          if (p[5] > 0 && p[6] > 0)
            gp.heading = line.substring(p[5] + 1, p[6]).toFloat();
          if (p[6] > 0)
            gp.rpm = ((p[7] > 0) ? line.substring(p[6] + 1, p[7])
                                 : line.substring(p[6] + 1))
                         .toInt();
          if (p[7] > 0)
            gp.gX = ((p[8] > 0) ? line.substring(p[7] + 1, p[8])
                                : line.substring(p[7] + 1))
                        .toFloat();
          if (p[8] > 0)
            gp.gY = ((p[9] > 0) ? line.substring(p[8] + 1, p[9])
                                : line.substring(p[8] + 1))
                        .toFloat();
          if (p[9] > 0)
            gp.lean = line.substring(p[9] + 1).toFloat();
          points.push_back(gp);
          pointsFound++;
        }
      }
    }
  }
  f.close();
  Serial.printf("Replay: Extracted %d points for lap %d\n", pointsFound,
                lapIdx);
  return points;
}

void SessionManager::wipeSDData() {
  if (!SD.totalBytes())
    return;

  auto clearFolder = [](String path) {
    File root = SD.open(path);
    if (!root || !root.isDirectory())
      return;

    File file = root.openNextFile();
    while (file) {
      String fileName = file.name();
      // Ensure path is correct for removal
      String fullPath = path;
      if (!fullPath.endsWith("/"))
        fullPath += "/";
      if (fileName.startsWith("/")) {
        SD.remove(fileName);
      } else {
        SD.remove(fullPath + fileName);
      }
      file = root.openNextFile();
    }
  };

  // 1. Clear contents of known data folders
  clearFolder("/sessions");
  clearFolder("/tracks");
  clearFolder("/uploaded_tracks");

  // 2. Remove history index & wifi details
  if (SD.exists("/history.csv")) {
    SD.remove("/history.csv");
  }
  if (SD.exists("/wifi.txt")) {
    SD.remove("/wifi.txt");
  }

  Serial.println("SD Data Wipe Complete");
}
