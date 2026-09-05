#ifndef NAVIGATION_MANAGER_H
#define NAVIGATION_MANAGER_H

#include <Arduino.h>

// ============================================================================
// NavigationManager - Turn-by-turn (TBT) navigation receiver over Bluetooth
// Low Energy (BLE) using the NimBLE stack (kept small enough to fit the 2MB
// app partition; the full Bluetooth-Classic stack is too large).
//
// The device advertises as "MuchRacing-Nav" with the standard Nordic UART
// Service (NUS), so any BLE serial app ("Serial Bluetooth Terminal", nRF
// Connect, ...) or a navigation relay app can connect and send newline-
// terminated JSON lines describing the next maneuver:
//
//   {"icon":3,"dist":235,"text":"Turn left onto Jl. Ahmad Yani"}
//
// Fields:
//   icon  (int, optional) maneuver code, see Maneuver enum below.
//   dist  (int, optional) remaining distance in METERS (-1/omitted = unknown)
//   text  (string) human readable instruction / street name
//
// Special events (instead of icon):
//   {"event":"arrive"}   -> arrival at destination (shows ARRIVE state)
//   {"event":"clear"}    -> navigation ended, go back to idle
//
// Maneuver icon codes (Maneuver enum):
//   0 = ARRIVE / finish         5 = SLIGHT_RIGHT
//   1 = STRAIGHT                6 = RIGHT
//   2 = SLIGHT_LEFT             7 = SHARP_RIGHT
//   3 = LEFT                    8 = UTURN
//   4 = SHARP_LEFT              9 = ROUNDABOUT
// ============================================================================

class NavWriteCallbacks;
class NavServerCallbacks;

class NavigationManager {
public:
  enum Maneuver {
    MANEUVER_ARRIVE = 0,
    MANEUVER_STRAIGHT = 1,
    MANEUVER_SLIGHT_LEFT = 2,
    MANEUVER_LEFT = 3,
    MANEUVER_SHARP_LEFT = 4,
    MANEUVER_SLIGHT_RIGHT = 5,
    MANEUVER_RIGHT = 6,
    MANEUVER_SHARP_RIGHT = 7,
    MANEUVER_UTURN = 8,
    MANEUVER_ROUNDABOUT = 9
  };

  enum NavSource {
    NAV_SOURCE_NONE = 0,
    NAV_SOURCE_BLE = 1,
    NAV_SOURCE_MQTT = 2
  };

  void begin();
  void update();

  bool isBleReady() { return _btOn; }          // BLE stack initialized
  bool isConnected();                          // a phone is connected (BLE)
  bool hasActiveRoute();                       // valid guidance data present
  int getManeuver();
  long getDistanceM();
  String getInstruction();
  unsigned long getLastUpdateMs();
  int getSource();                             // where the data came from

  // Public entry point for non-BLE sources (MQTT, etc.)
  void ingestLine(const String &line, NavSource source);
  void clearRoute();

private:
  friend class NavWriteCallbacks;
  friend class NavServerCallbacks;

  void _onIncoming(const char *data, size_t len); // BLE RX callback entry
  void handleLine(String line, NavSource source); // requires lock held
  void parseJson(const String &line, NavSource source);

  bool _btOn = false;
  bool _active = false;
  NavSource _source = NAV_SOURCE_NONE;
  int _maneuver = MANEUVER_STRAIGHT;
  long _distanceM = -1;
  String _instruction = "";
  unsigned long _lastUpdateMs = 0;
  String _rxBuffer; // Partial incoming line (guarded by _mutex)
};

extern NavigationManager navigationManager;

#endif
