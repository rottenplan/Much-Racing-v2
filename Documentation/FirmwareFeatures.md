# Firmware Feature Tree

## 1. System Core
*   **Hardware Abstraction**
    *   **GPS Manager** (10Hz/18Hz/25Hz Update Rate, UBLOX Protocol)
    *   **IMU Manager** (6-Axis Sensor, Calibration, Roll/Pitch Detection)
    *   **Battery Manager** (Voltage Monitoring, Percentage Calculation)
    *   **Display Driver** (TFT_eSPI, HSPI, 480x320 Resolution)
    *   **Touch Input** (GT911 I2C Capacitive Touch, Debouncing)
*   **Data Management**
    *   **Session Manager** (Logging, History Indexing, File I/O)
    *   **SD Card** (SPI Mode, Hot-Swap Detection, Speed Test)
    *   **Preferences** (NVS Storage for Settings)
*   **Connectivity**
    *   **WiFi Manager** (Station Mode, AP Mode, Scanning)
    *   **Sync Manager** (Cloud Synchronization, Live Telemetry)
    *   **WebServer** (File Download, status monitoring)
*   **Feedback**
    *   **LED Control** (RGB System Status, Shift Lights)
    *   **Buzzer** (Beep Patterns, Alarms)

## 2. User Interface (Screens)
### A. Main Dashboard (Menu)
*   **Lap Timer**
    *   **Track Selection**
        *   Nearby Tracks (Auto-detect < 50km)
        *   Track Options: Select, Edit Name, Re-init Best Lap, Delete
    *   **Race Mode**
        *   Predictive Timing (vs Best Lap)
        *   Live Delta
        *   Sector Analysis
    *   **Track Creator**
        *   Wizard: Set Start Point -> Set Finish Point (Split/Closed) -> Save
    *   **History View** (Filtered for Track Sessions)
*   **Drag Meter**
    *   **Modes**
        *   Normal (Standard Display)
        *   Predictive (Estimated Final Time)
    *   **disciplines**
        *   Standard: 0-60 KPH, 0-100 KPH, 100-200 KPH, 402m (1/4 Mile)
        *   Custom: Configurable Speed & Distance Gates
    *   **Christmas Tree**
        *   Countdown Logic (Pro Tree / sportsman)
        *   False Start Detection
    *   **History View** (Filtered for Drag Runs)
*   **RPM Sensor**
    *   *Consolidated into Vehicle Settings* (G-Force, Engine Hours)
*   **Speedometer**
    *   Simple Digital Gauge
*   **GPS Status**
    *   Satellite Graph / Signal Strength
    *   Coordinates (Lat/Lon/Alt)
    *   Accuracy Metrics (PDOP/HDOP)
*   **Synchronize**
    *   One-touch Cloud Sync
*   **Settings**

### B. Settings Menu
*   **Power & Display**
    *   Brightness (10-100%)
    *   Power Save (Auto-off Timer)
*   **Vehicle & Sensors**
    *   **Engine**: Pulse Per Rev (PPR), RPM Toggle, Total Engine Hours
    *   **IMU**: Calibration (Level), Roll/Pitch Offsets, G-Force Cal
    *   **Units**: Metric (km/h) / Imperial (mph)
*   **GNSS Fine Tuning**
    *   Constellation Mode (GPS/GLONASS/GALILEO/BEIDOU)
    *   Update Rate Limit (10Hz - 25Hz)
    *   Dynamic Model (Automotive/Racing)
    *   SBAS Setting (WAAS/EGNOS/MSAS/etc.)
    *   Hardware Pins (TX/RX Swap)
    *   Baud Rate
*   **Drag Settings**
    *   Tree Duration (3s, 5s, 7s)
    *   Custom Goals (Start Speed, End Speed, Distances)
*   **Lap Timer Settings**
    *   Beacon Width (20m - 100m)
*   **Connection Setup**
    *   WiFi Network Config (Scan/Manual)
    *   Offline Server Mode
    *   Hotspot Toggle
    *   Live Telemetry Toggle
    *   Account Management (Remove)
*   **Utility**
    *   SD Card Benchmark
    *   TFT Analysis
    *   Touch Debug Mode
*   **About Device**
    *   Firmware Version info

## 3. Data Analysis Features
*   **Session Analysis**
    *   **Track**:
        *   Lap List
        *   Best Lap Identification
        *   Sector Times
        *   Max Speed / Max RPM per lap
    *   **Drag**:
        *   0-60, 0-100, 100-200, 1/4 Mile Times
        *   Slope Calculation (Validity Check)
        *   Peak Speed
*   **History Browser**
    *   Grouped by Month (YYYY-MM)
    *   Icon indicators for Session Type
    *   Delete / Sync individual sessions
