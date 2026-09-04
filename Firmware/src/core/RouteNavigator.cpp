#include "RouteNavigator.h"
#include <SD.h>
#include <WiFi.h>
#include <math.h>

RouteNavigator routeNavigator;

static const float TURN_ANGLE = 45.0f;
static const float UTURN_ANGLE = 150.0f;
static const int ALERT_100M = 100;
static const int ALERT_50M = 50;
static const int ALERT_20M = 20;
static const int ALERT_NOW = 8;
static const float OFFROUTE_LIMIT = 45.0f;
static const int OFFROUTE_CONFIRM = 5;
static const int LOOKAHEAD_WIN = 40;
static const float SNAP_DIST = 60.0f;

RouteNavigator::RouteNavigator() {
  _active = false;
  _progIdx = 0;
  _offRoute = false;
  _offRouteDist = 0;
  _offRouteCount = 0;
  _alert.active = false;
  _alert.distM = 0;
  _alert.dir = TURN_NONE;
  _alert.urgency = 0;
  _lastUrgency = 0;
  _newUrgency = false;
  _remainingKm = 0;
  _lastRemainCalc = 0;
}

float RouteNavigator::_distM(float lat1, float lng1, float lat2,
                             float lng2) const {
  const float R = 6371000.0f;
  float dLat = (lat2 - lat1) * PI / 180.0f;
  float dLng = (lng2 - lng1) * PI / 180.0f;
  float a = sinf(dLat / 2) * sinf(dLat / 2) +
            cosf(lat1 * PI / 180.0f) * cosf(lat2 * PI / 180.0f) *
                sinf(dLng / 2) * sinf(dLng / 2);
  return R * 2 * atan2f(sqrtf(a), sqrtf(1.0f - a));
}

int RouteNavigator::_findNearest(float lat, float lng, int startIdx,
                                 int window) {
  int n = (int)_points.size();
  float bestD = 1e12f;
  int bestI = startIdx;
  int end = startIdx + window;
  if (end > n)
    end = n;
  for (int i = startIdx; i < end; i++) {
    float d = _distM(lat, lng, _points[i].lat, _points[i].lng);
    if (d < bestD) {
      bestD = d;
      bestI = i;
    }
  }
  return bestI;
}

static bool extractAttr(const char *buf, const char *attr, float &out) {
  const char *p = strstr(buf, attr);
  if (!p)
    return false;
  p += strlen(attr);
  char tmp[16];
  int i = 0;
  while (*p && *p != '"' && i < 15)
    tmp[i++] = *p++;
  tmp[i] = 0;
  out = strtof(tmp, nullptr);
  return true;
}

bool RouteNavigator::loadRoute(const char *path) {
  unload();
  File file = SD.open(path, FILE_READ);
  if (!file) {
    Serial.printf("Nav: %s tidak ditemukan\n", path);
    return false;
  }
  const int BUF = 256;
  char buf[BUF];
  int len = 0;
  while (file.available()) {
    char c = (char)file.read();
    if (c == '<') {
      buf[len] = 0;
      if (strstr(buf, "trkpt") || strstr(buf, "rtept")) {
        float la, lo;
        if (extractAttr(buf, "lat=\"", la) && extractAttr(buf, "lon=\"", lo)) {
          RoutePoint pt = {(float)la, (float)lo};
          _points.push_back(pt);
        }
      }
      len = 0;
      buf[len] = '<';
      len = 1;
    } else {
      if (len < BUF - 1)
        buf[len++] = c;
    }
  }
  file.close();
  if (_points.size() < 3) {
    _points.clear();
    return false;
  }
  if (_points.size() > 600) {
    std::vector<RoutePoint> ds;
    for (size_t i = 0; i < _points.size(); i += 2)
      ds.push_back(_points[i]);
    ds.push_back(_points.back());
    _points.swap(ds);
  }
  _progIdx = 0;
  _offRoute = false;
  _offRouteCount = 0;
  _remainingKm = 0;
  _active = true;
  Serial.printf("Nav: rute dimuat %d titik dari %s\n", (int)_points.size(),
                path);
  return true;
}

static void plDecodeChunk(const char *s, int &i, int len, float &out) {
  long result = 0;
  int shift = 0;
  char b;
  do {
    if (i >= len)
      break;
    b = s[i++] - 63;
    result |= (b & 0x1f) << shift;
    shift += 5;
  } while (b >= 0x20);
  if (result & 1)
    result = ~(result >> 1);
  else
    result = result >> 1;
  out = (float)result;
}

void RouteNavigator::_decodePolyline(const String &encoded) {
  const char *s = encoded.c_str();
  int len = encoded.length();
  int i = 0;
  float lat = 0, lng = 0;
  while (i < len) {
    float dLat, dLng;
    plDecodeChunk(s, i, len, dLat);
    plDecodeChunk(s, i, len, dLng);
    lat += dLat / 1e5f;
    lng += dLng / 1e5f;
    RoutePoint pt = {lat, lng};
    _points.push_back(pt);
  }
}

// PUBLIC: decode polyline dari HP (navigasi online via Bluetooth)
void RouteNavigator::decodePolyline(const String &encoded) {
  _points.clear();
  _decodePolyline(encoded);
  if (_points.size() < 3) {
    _points.clear();
    return;
  }
  if (_points.size() > 800) {
    std::vector<RoutePoint> ds;
    for (size_t i = 0; i < _points.size(); i += 2)
      ds.push_back(_points[i]);
    ds.push_back(_points.back());
    _points.swap(ds);
  }
  _progIdx = 0;
  _offRoute = false;
  _offRouteCount = 0;
  _remainingKm = 0;
  _active = true;
  Serial.printf("Nav: polyline dimuat %d titik\n", (int)_points.size());
}

void RouteNavigator::unload() {
  _points.clear();
  _active = false;
  _progIdx = 0;
  _offRoute = false;
  _offRouteCount = 0;
  _alert.active = false;
  _alert.urgency = 0;
  _lastUrgency = 0;
  _newUrgency = false;
  _remainingKm = 0;
}

const char *RouteNavigator::getTurnText() const {
  switch (_alert.dir) {
  case TURN_LEFT:
    return "BELOK KIRI";
  case TURN_RIGHT:
    return "BELOK KANAN";
  case TURN_UTURN:
    return "PUTAR BALIK";
  default:
    return "";
  }
}

void RouteNavigator::update(double latD, double lngD) {
  if (!_active)
    return;
  float lat = (float)latD;
  float lng = (float)lngD;
  int n = (int)_points.size();
  int nearest = _findNearest(lat, lng, _progIdx, LOOKAHEAD_WIN);
  float distToRoute =
      _distM(lat, lng, _points[nearest].lat, _points[nearest].lng);
  if (nearest == _progIdx && distToRoute > SNAP_DIST && _progIdx > 0) {
    int back = _progIdx - 20;
    if (back < 0)
      back = 0;
    nearest = _findNearest(lat, lng, back, 25);
    distToRoute = _distM(lat, lng, _points[nearest].lat, _points[nearest].lng);
  }
  if (distToRoute > OFFROUTE_LIMIT) {
    if (!_offRoute) {
      _offRouteCount++;
      if (_offRouteCount >= OFFROUTE_CONFIRM)
        _offRoute = true;
    }
  } else {
    _offRouteCount = 0;
    _offRoute = false;
    _progIdx = nearest;
  }

  _alert.active = false;
  _alert.distM = 0;
  _alert.dir = TURN_NONE;
  _alert.urgency = 0;
  float accDist = 0;
  int scanEnd = nearest + 80;
  if (scanEnd > n - 2)
    scanEnd = n - 2;
  if (scanEnd < nearest + 3)
    scanEnd = nearest + 3;
  for (int i = nearest; i < scanEnd; i++) {
    float d1 = _distM(_points[i].lat, _points[i].lng, _points[i + 1].lat,
                      _points[i + 1].lng);
    float d2 = _distM(_points[i + 1].lat, _points[i + 1].lng,
                      _points[i + 2].lat, _points[i + 2].lng);
    if (d1 <= 0.01f || d2 <= 0.01f)
      continue;
    float latA = _points[i].lat * PI / 180.0f,
          lngA = _points[i].lng * PI / 180.0f;
    float latB = _points[i + 1].lat * PI / 180.0f,
          lngB = _points[i + 1].lng * PI / 180.0f;
    float latC = _points[i + 2].lat * PI / 180.0f,
          lngC = _points[i + 2].lng * PI / 180.0f;
    float y1 = sinf(lngB - lngA) * cosf(latB);
    float x1 =
        cosf(latA) * sinf(latB) - sinf(latA) * cosf(latB) * cosf(lngB - lngA);
    float brg1 = atan2f(y1, x1);
    float y2 = sinf(lngC - lngB) * cosf(latC);
    float x2 =
        cosf(latB) * sinf(latC) - sinf(latB) * cosf(latC) * cosf(lngC - lngB);
    float brg2 = atan2f(y2, x2);
    float diff = (brg2 - brg1) * 180.0f / PI;
    while (diff > 180.0f)
      diff -= 360.0f;
    while (diff < -180.0f)
      diff += 360.0f;
    if (fabsf(diff) >= TURN_ANGLE) {
      float dist = _distM(lat, lng, _points[i + 1].lat, _points[i + 1].lng);
      if (dist < 3.0f)
        continue;
      NavTurnDir dir;
      if (fabsf(diff) >= UTURN_ANGLE)
        dir = TURN_UTURN;
      else
        dir = (diff > 0) ? TURN_RIGHT : TURN_LEFT;
      int distM = (int)dist;
      uint8_t urg = 0;
      if (distM <= ALERT_NOW)
        urg = 4;
      else if (distM <= ALERT_20M)
        urg = 3;
      else if (distM <= ALERT_50M)
        urg = 2;
      else if (distM <= ALERT_100M)
        urg = 1;
      if (urg > 0) {
        _alert.active = true;
        _alert.distM = distM;
        _alert.dir = dir;
        _alert.urgency = urg;
      }
      break;
    }
    accDist += d1;
    if (accDist > 250)
      break;
  }
  if (_alert.active && _alert.urgency > _lastUrgency)
    _newUrgency = true;
  _lastUrgency = _alert.active ? _alert.urgency : 0;
  unsigned long now = millis();
  if (now - _lastRemainCalc > 2000) {
    _lastRemainCalc = now;
    float rem = 0;
    for (int i = nearest; i < n - 1; i++)
      rem += _distM(_points[i].lat, _points[i].lng, _points[i + 1].lat,
                    _points[i + 1].lng);
    _remainingKm = rem / 1000.0f;
  }
}
