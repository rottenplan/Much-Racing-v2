#ifndef ROUTE_NAVIGATOR_H
#define ROUTE_NAVIGATOR_H

#include "../config.h"
#include <Arduino.h>
#include <vector>

struct RoutePoint {
  float lat;
  float lng;
};

enum NavTurnDir : int8_t {
  TURN_NONE = 0,
  TURN_LEFT = -1,
  TURN_RIGHT = 1,
  TURN_UTURN = 2
};

struct NavAlert {
  bool active;
  int distM;
  NavTurnDir dir;
  uint8_t urgency;
};

class RouteNavigator {
public:
  RouteNavigator();

  bool loadRoute(const char *path);

  // Decode polyline dari HP (navigasi online via Bluetooth)
  void decodePolyline(const String &encoded);

  void unload();
  bool isActive() const { return _active; }
  int getPointCount() const { return (int)_points.size(); }

  void update(double lat, double lng);

  bool isOffRoute() const { return _offRoute; }

  bool hasTurnAlert() const { return _alert.active; }
  int getTurnDistance() const { return _alert.distM; }
  NavTurnDir getTurnDirection() const { return _alert.dir; }
  uint8_t getUrgency() const { return _alert.urgency; }
  const char *getTurnText() const;

  float getRemainingKm() const { return _remainingKm; }

  bool isNewUrgency() const { return _newUrgency; }
  void clearNewUrgency() { _newUrgency = false; }

private:
  std::vector<RoutePoint> _points;
  bool _active;
  int _progIdx;

  void _decodePolyline(const String &encoded);

  bool _offRoute;
  float _offRouteDist;
  int _offRouteCount;

  NavAlert _alert;
  uint8_t _lastUrgency;
  bool _newUrgency;
  float _remainingKm;
  unsigned long _lastRemainCalc;

  int _findNearest(float lat, float lng, int startIdx, int window);
  float _distM(float lat1, float lng1, float lat2, float lng2) const;
};

extern RouteNavigator routeNavigator;

#endif // ROUTE_NAVIGATOR_H