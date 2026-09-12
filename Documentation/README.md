# Much Racing - Race Computer Firmware

Much Racing is a high-performance GPS-based race computer firmware designed for ESP32 devices. It features high-frequency GNSS data acquisition, real-time telemetry, and advanced racing analysis.

## Key Features

- **High-Precision Lap Timing**: Automatic track detection and predictive timing.
- **Drag Meter**: Specialized mode for 0-60, 0-100, and 1/4 mile performance testing.
- **Real-Time Dashboard**: Digital speedometer with 7-segment font and RPM virtualization.
- **Deep GNSS Configuration**: Support for multiple constellations (GPS, GLONASS, Galileo, BeiDou) up to 25Hz.
- **Integrated Storage**: Full session recording to SD Card with offline analysis.
- **Cloud Sync**: Synchronize sessions with the Much Racing dashboard.

## Project Structure

- **/Firmware**: The main ESP32 source code (C++/TFT_eSPI).
- **/Application**: Companion web interface/dashboard.

## Documentation

For a complete breakdown of the device menus and settings, see:
👉 **[Menu Structure & Navigation](MENU_STRUCTURE.md)**

---

Developed by Muchdas.
