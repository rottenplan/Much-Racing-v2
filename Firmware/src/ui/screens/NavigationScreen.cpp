#include "NavigationScreen.h"
#include "../../config.h"
#include "../../core/FeedbackManager.h"
#include "../../core/GPSManager.h"
#include "../../core/MQTTManager.h"
#include "../../core/NavigationManager.h"
#include "../fonts/Org_01.h"
#include <math.h>

extern GPSManager gpsManager;

// ---------------------------------------------------------------------------
// Layout constants (content area starts below the 25px status bar)
// ---------------------------------------------------------------------------
static const int CONTENT_Y = STATUS_BAR_HEIGHT;
static const int CONTENT_H = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;

static const char *maneuverLabel(int m) {
  switch (m) {
  case NavigationManager::MANEUVER_ARRIVE:
    return "ARRIVED";
  case NavigationManager::MANEUVER_STRAIGHT:
    return "GO STRAIGHT";
  case NavigationManager::MANEUVER_SLIGHT_LEFT:
    return "SLIGHT LEFT";
  case NavigationManager::MANEUVER_LEFT:
    return "TURN LEFT";
  case NavigationManager::MANEUVER_SHARP_LEFT:
    return "SHARP LEFT";
  case NavigationManager::MANEUVER_SLIGHT_RIGHT:
    return "SLIGHT RIGHT";
  case NavigationManager::MANEUVER_RIGHT:
    return "TURN RIGHT";
  case NavigationManager::MANEUVER_SHARP_RIGHT:
    return "SHARP RIGHT";
  case NavigationManager::MANEUVER_UTURN:
    return "U-TURN";
  case NavigationManager::MANEUVER_ROUNDABOUT:
    return "ROUNDABOUT";
  default:
    return "";
  }
}

// Wrap + draw text centered, word aware, up to maxLines lines.
static void drawWrappedText(TFT_eSPI *tft, const String &text, int xCenter,
                            int yCenter, int maxW, int size, int lineH,
                            uint16_t color, uint16_t bg, int maxLines) {
  tft->setFreeFont(&Org_01);
  tft->setTextSize(size);
  tft->setTextColor(color, bg);
  tft->setTextDatum(TL_DATUM);

  String words = text;
  String lines[4];
  int lineCount = 0;

  // Build lines by words
  int startIdx = 0;
  while (startIdx < (int)words.length() && lineCount < maxLines) {
    // Find end of current line
    int endIdx = words.length();
    for (int i = startIdx; i < (int)words.length(); i++) {
      String candidate = words.substring(startIdx, i + 1);
      if (tft->textWidth(candidate) > maxW) {
        endIdx = i;
        break;
      }
    }
    if (endIdx <= startIdx) {
      // Single word too wide: hard-truncate
      endIdx = words.length();
      String candidate = words.substring(startIdx, endIdx);
      while (candidate.length() > 1 &&
             tft->textWidth(candidate) > maxW) {
        candidate.remove(candidate.length() - 1);
      }
      lines[lineCount++] = candidate;
      startIdx += candidate.length();
    } else {
      lines[lineCount] = words.substring(startIdx, endIdx);
      // Trim trailing space
      while (lines[lineCount].endsWith(" ") && lines[lineCount].length() > 0)
        lines[lineCount].remove(lines[lineCount].length() - 1);
      startIdx = endIdx;
      lineCount++;
    }
    // Skip spaces at line start
    while (startIdx < (int)words.length() && words[startIdx] == ' ')
      startIdx++;
  }
  // Overflow text after maxLines -> ellipsis on last line
  if (startIdx < (int)words.length() && lineCount > 0)
    lines[lineCount - 1] += "...";

  // Center block vertically
  int blockH = (lineCount > 0) ? lineCount * lineH : lineH;
  int y = yCenter - blockH / 2;
  for (int i = 0; i < lineCount; i++) {
    int lineW = tft->textWidth(lines[i]);
    tft->drawString(lines[i], xCenter - lineW / 2, y);
    y += lineH;
  }
}

// ---------------------------------------------------------------------------
// Google Maps-style maneuver icons (thick curved arrows)
// ---------------------------------------------------------------------------
struct NavPt {
  float x, y;
};

static NavPt navBezierQuad(NavPt p0, NavPt p1, NavPt p2, float t) {
  float u = 1.0f - t;
  return {u * u * p0.x + 2.0f * u * t * p1.x + t * t * p2.x,
          u * u * p0.y + 2.0f * u * t * p1.y + t * t * p2.y};
}

static NavPt navBezierCubic(NavPt p0, NavPt p1, NavPt p2, NavPt p3, float t) {
  float u = 1.0f - t;
  float a = u * u * u;
  float b = 3.0f * u * u * t;
  float c = 3.0f * u * t * t;
  float d = t * t * t;
  return {a * p0.x + b * p1.x + c * p2.x + d * p3.x,
          a * p0.y + b * p1.y + c * p2.y + d * p3.y};
}

// Thick rounded stroke (smooth joints/caps) through sampled path points.
static void strokeNavPath(TFT_eSPI *tft, const NavPt *pts, int n, float w,
                          uint16_t color) {
  int wd = (int)(w + 0.5f);
  int rr = wd / 2;
  for (int i = 0; i < n - 1; i++) {
    tft->drawWideLine(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, w,
                      color, COLOR_BG);
  }
  for (int i = 0; i < n; i++) {
    tft->fillCircle((int)pts[i].x, (int)pts[i].y, rr, color);
  }
}

// Flared arrowhead at `end`, oriented along `tangent`.
static void navArrowHead(TFT_eSPI *tft, NavPt end, NavPt tangent, float s,
                         uint16_t color) {
  float len = sqrtf(tangent.x * tangent.x + tangent.y * tangent.y);
  if (len < 0.001f)
    return;
  float dx = tangent.x / len;
  float dy = tangent.y / len;
  float px = -dy;
  float py = dx;

  float hLen = s * 0.17f;  // head length
  float hWid = s * 0.15f;  // head half width
  float hBack = s * 0.05f; // base inset

  NavPt tip = {end.x + dx * hLen, end.y + dy * hLen};
  NavPt base = {end.x - dx * hBack, end.y - dy * hBack};
  NavPt a = {base.x + px * hWid, base.y + py * hWid};
  NavPt b = {base.x - px * hWid, base.y - py * hWid};

  tft->fillTriangle((int)a.x, (int)a.y, (int)b.x, (int)b.y, (int)tip.x,
                    (int)tip.y, color);
}

// Draws a Google-style curved arrow.
// mode: 0=straight, 1=slight-left, 2=left, 3=sharp-left, 4=slight-right,
//       5=right, 6=sharp-right, 7=u-turn
static void drawCurvedNavArrow(TFT_eSPI *tft, int mode, int cx, int cy,
                               float s, uint16_t color) {
  const int SEG = 18;
  NavPt path[SEG];
  NavPt p0 = {0.0f, 0.45f};
  NavPt p1, p2, p3;
  NavPt tangent = {0.0f, -1.0f};
  bool cubic = false;
  bool straight = false;

  switch (mode) {
  case 0: // straight
    p1 = {0.0f, -0.08f};
    straight = true;
    break;
  case 1: // slight left
    p1 = {0.0f, 0.22f};
    p2 = {-0.17f, -0.34f};
    break;
  case 2: // left
    p1 = {0.0f, -0.02f};
    p2 = {-0.45f, -0.05f};
    break;
  case 3: // sharp left
    p1 = {-0.32f, -0.06f};
    p2 = {-0.44f, -0.32f};
    break;
  case 4: // slight right
    p1 = {0.0f, 0.22f};
    p2 = {0.17f, -0.34f};
    break;
  case 5: // right
    p1 = {0.0f, -0.02f};
    p2 = {0.45f, -0.05f};
    break;
  case 6: // sharp right
    p1 = {0.32f, -0.06f};
    p2 = {0.44f, -0.32f};
    break;
  case 7: { // u-turn
    p1 = {0.0f, 0.12f};
    p2 = {0.34f, -0.26f};
    p3 = {0.22f, 0.36f};
    cubic = true;
    break;
  }
  default:
    p1 = {0.0f, 0.22f};
    p2 = {-0.17f, -0.34f};
    break;
  }

  int n;
  if (straight) {
    path[0] = p0;
    path[1] = p1;
    n = 2;
  } else {
    n = SEG;
    for (int i = 0; i < SEG; i++) {
      float t = (float)i / (float)(SEG - 1);
      if (cubic)
        path[i] = navBezierCubic(p0, p1, p2, p3, t);
      else
        path[i] = navBezierQuad(p0, p1, p2, t);
    }
    if (cubic)
      tangent = {p3.x - p2.x, p3.y - p2.y};
    else
      tangent = {p2.x - p1.x, p2.y - p1.y};
  }

  float scale = s;
  for (int i = 0; i < n; i++) {
    path[i].x = cx + path[i].x * scale;
    path[i].y = cy + path[i].y * scale;
  }

  float w = s * 0.085f;
  strokeNavPath(tft, path, n, w, color);

  NavPt headPos = path[n - 1];
  navArrowHead(tft, headPos, tangent, s, color);
}

void NavigationScreen::drawArrowIcon(int maneuver, int cx, int cy, int size,
                                     uint16_t color) {
  TFT_eSPI *tft = _ui->getTft();
  float s = (float)size;
  switch (maneuver) {
  case NavigationManager::MANEUVER_STRAIGHT:
    drawCurvedNavArrow(tft, 0, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_SLIGHT_LEFT:
    drawCurvedNavArrow(tft, 1, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_LEFT:
    drawCurvedNavArrow(tft, 2, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_SHARP_LEFT:
    drawCurvedNavArrow(tft, 3, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_SLIGHT_RIGHT:
    drawCurvedNavArrow(tft, 4, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_RIGHT:
    drawCurvedNavArrow(tft, 5, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_SHARP_RIGHT:
    drawCurvedNavArrow(tft, 6, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_UTURN:
    drawCurvedNavArrow(tft, 7, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_ROUNDABOUT:
    drawRoundaboutIcon(cx, cy, size, color);
    break;
  case NavigationManager::MANEUVER_ARRIVE:
    drawArriveIcon(cx, cy, size);
    break;
  default:
    drawCurvedNavArrow(tft, 0, cx, cy, s, color);
    break;
  }
}

void NavigationScreen::drawArriveIcon(int cx, int cy, int size) {
  TFT_eSPI *tft = _ui->getTft();
  // Checkered flag head
  int cells = 3;
  float cell = size * 0.20f;
  float flagW = cell * cells;
  float flagH = cell * 2;
  int x0 = cx - (int)flagW / 2;
  int y0 = cy - (int)flagH / 2 - (int)(cell * 0.5f);
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < cells; c++) {
      uint16_t col = ((r + c) % 2 == 0) ? TFT_WHITE : COLOR_BG;
      tft->fillRect(x0 + (int)(c * cell), y0 + (int)(r * cell), (int)cell + 1,
                    (int)cell + 1, col);
    }
  }
  // Outline
  tft->drawRect(x0, y0, (int)flagW + 1, (int)flagH + 1, COLOR_ACCENT);
  // Pole
  tft->fillRect(cx - 2, y0 + (int)flagH, 4, (int)(size * 0.30f), TFT_DARKGREY);
}

void NavigationScreen::drawRoundaboutIcon(int cx, int cy, int size,
                                          uint16_t color) {
  TFT_eSPI *tft = _ui->getTft();
  float s = (float)size;

  int R = (int)(s * 0.36f);
  int wd = (int)(s * 0.085f + 0.5f);

  // Stem entering from below
  tft->drawWideLine((float)cx, (float)(cy + (int)(s * 0.45f)), (float)cx,
                    (float)(cy + R), (float)wd, color, COLOR_BG);

  // Ring (radius R, thickness wd)
  tft->drawArc(cx, cy, R, R - wd, 0, 360, color, COLOR_BG);

  // Clockwise travel arrows on the ring
  navArrowHead(tft, {(float)cx, (float)(cy - R)}, {1.0f, 0.0f}, s * 0.5f,
               color); // top -> right
  navArrowHead(tft, {(float)(cx - R), (float)cy}, {0.0f, -1.0f}, s * 0.5f,
               color); // left -> up
}

void NavigationScreen::drawStatusChip(bool btConnected) {
  TFT_eSPI *tft = _ui->getTft();
  int x = SCREEN_WIDTH - 72;
  int y = CONTENT_Y + CONTENT_H - 26;
  int w = 64;
  int h = 20;

  const char *label = "BT WAIT";
  uint16_t bg = 0x39E7;
  uint16_t border = COLOR_SECONDARY;
  uint16_t textCol = TFT_WHITE;

  if (navigationManager.getSource() == NavigationManager::NAV_SOURCE_MQTT) {
    label = "MQTT NAV";
    bg = 0x03EF;
    border = COLOR_ACCENT;
    textCol = COLOR_ACCENT;
  } else if (btConnected) {
    label = "BT LINK";
    bg = 0x03EF;
    border = TFT_GREEN;
    textCol = TFT_GREEN;
  }

  tft->fillRoundRect(x, y, w, h, 5, bg);
  tft->drawRoundRect(x, y, w, h, 5, border);
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextColor(textCol, bg);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(label, x + w / 2, y + h / 2 - 1);
}

void NavigationScreen::onShow() {
  _lastManeuver = -2;
  _lastDistance = -2;
  _lastInstruction = "";
  _lastConnected = false;
  _lastActive = false;
  _lastHasBt = false;
  _lastMqtt = false;
  drawAll(true);
}

void NavigationScreen::update() {
  UIManager::TouchPoint p = _ui->getTouchPoint();

  // Back button (bottom-left corner, same convention as Speedometer)
  if (p.x != -1 && p.x < 45 && p.y > CONTENT_Y + CONTENT_H - 40) {
    _ui->switchScreen(SCREEN_MENU);
    return;
  }

  NavigationManager &nav = navigationManager;
  bool btReady = nav.isBleReady();
  bool connected = btReady && nav.isConnected();
  bool active = nav.hasActiveRoute();
  bool mqttOn = mqttManager.isConnected();

  if (!btReady || !active) {
    // Idle: only redraw when visibility/connection state changes
    if (_lastActive != false || _lastConnected != connected ||
        _lastHasBt != btReady || _lastMqtt != mqttOn) {
      _lastActive = false;
      _lastConnected = connected;
      _lastHasBt = btReady;
      _lastMqtt = mqttOn;
      drawIdle();
    }
    return;
  }

  // Keep the screen awake while actively navigating (no touch while riding)
  _ui->updateInteraction();

  int maneuver = nav.getManeuver();
  long distance = nav.getDistanceM();
  String instruction = nav.getInstruction();

  bool dataChanged = (maneuver != _lastManeuver || distance != _lastDistance ||
                      instruction != _lastInstruction || !_lastActive ||
                      connected != _lastConnected);

  if (dataChanged) {
    if (maneuver != _lastManeuver && _lastManeuver != -2 &&
        millis() - _lastBeepMs > 1500) {
      FeedbackManager::getInstance().beep(120);
      _lastBeepMs = millis();
    }
    _lastManeuver = maneuver;
    _lastDistance = distance;
    _lastInstruction = instruction;
    _lastActive = true;
    _lastConnected = connected;
    drawRoute();
    return;
  }
}

void NavigationScreen::drawAll(bool force) {
  (void)force;
  NavigationManager &nav = navigationManager;
  bool active = nav.hasActiveRoute();
  bool connected = nav.isBleReady() && nav.isConnected();
  _lastActive = active;
  _lastConnected = connected;
  _lastHasBt = nav.isBleReady();
  _lastMqtt = mqttManager.isConnected();
  if (active) {
    _lastManeuver = nav.getManeuver();
    _lastDistance = nav.getDistanceM();
    _lastInstruction = nav.getInstruction();
    drawRoute();
  } else {
    drawIdle();
  }
}

void NavigationScreen::drawIdle() {
  TFT_eSPI *tft = _ui->getTft();
  uint16_t bg = _ui->getBackgroundColor();
  tft->fillRect(0, CONTENT_Y, SCREEN_WIDTH, CONTENT_H, bg);

  NavigationManager &nav = navigationManager;
  bool btReady = nav.isBleReady();
  bool connected = btReady && nav.isConnected();
  bool mqttOn = mqttManager.isConnected();

  // Title
  tft->setFreeFont(&Org_01);
  tft->setTextSize(3);
  tft->setTextColor(COLOR_ACCENT, bg);
  tft->setTextDatum(MC_DATUM);
  tft->drawString("NAVIGATION", SCREEN_WIDTH / 2, 55);
  tft->setTextSize(1);

  // Connection summary line
  tft->setTextFont(2);
  tft->setTextSize(1);
  String st;
  if (btReady) {
    st = connected ? "BLE: OK" : "BLE: --";
  } else {
    st = "BLE: FAIL";
  }
  st += mqttOn ? "   |   MQTT: ON" : "   |   MQTT: OFF";
  tft->setTextColor(COLOR_ACCENT, bg);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(st, SCREEN_WIDTH / 2, 88);

  tft->setTextColor(COLOR_TEXT, bg);
  tft->drawString("Waiting for navigation data...", SCREEN_WIDTH / 2, 118);

  tft->setTextColor(COLOR_SECONDARY, bg);
  tft->drawString("BLE: pair \"MuchRacing-Nav\" & send JSON",
                  SCREEN_WIDTH / 2, 150);
  tft->drawString("MQTT: publish JSON to topic:", SCREEN_WIDTH / 2, 176);

  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextColor(COLOR_ACCENT, bg);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(mqttManager.getNavTopic(), SCREEN_WIDTH / 2, 200);

  tft->setTextFont(2);
  tft->setTextSize(1);
  tft->setTextColor(COLOR_SECONDARY, bg);
  tft->drawString("Start navigation - instructions appear here",
                  SCREEN_WIDTH / 2, 226);

  // Back hint + status chip
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextColor(COLOR_ACCENT, bg);
  tft->setTextDatum(TL_DATUM);
  tft->drawString("< BACK", 8, CONTENT_Y + CONTENT_H - 23);
  drawStatusChip(connected);

  // FONT SAFETY
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}

void NavigationScreen::drawRoute() {
  TFT_eSPI *tft = _ui->getTft();
  uint16_t bg = _ui->getBackgroundColor();
  tft->fillRect(0, CONTENT_Y, SCREEN_WIDTH, CONTENT_H, bg);

  NavigationManager &nav = navigationManager;
  int maneuver = nav.getManeuver();
  long distance = nav.getDistanceM();
  String instruction = nav.getInstruction();
  bool connected = nav.isBleReady() && nav.isConnected();

  // --- Instruction text (top, wrapped, up to 2 lines) ---
  String topText = instruction;
  if (topText.length() == 0)
    topText = String(maneuverLabel(maneuver));
  drawWrappedText(tft, topText, SCREEN_WIDTH / 2, 62, 440, 2, 22, COLOR_TEXT,
                  bg, 2);

  // --- Left: big arrow ---
  int boxX = 12;
  int boxY = 84;
  int boxW = 250;
  int boxH = 200;
  tft->drawRect(boxX, boxY, boxW, boxH, COLOR_SECONDARY);
  int cx = boxX + boxW / 2;
  int cy = boxY + boxH / 2 + 4;
  int arrowColor = (maneuver == NavigationManager::MANEUVER_ARRIVE)
                       ? TFT_GREEN
                       : COLOR_PRIMARY;
  drawArrowIcon(maneuver, cx, cy, 170, arrowColor);

  // Maneuver tag under arrow
  tft->setFreeFont(&Org_01);
  tft->setTextSize(2);
  tft->setTextColor(COLOR_HIGHLIGHT, bg);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(maneuverLabel(maneuver), cx, boxY + boxH - 16);

  // --- Right: distance ---
  int rx = 268;
  int rw = SCREEN_WIDTH - rx - 12;
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextColor(COLOR_ACCENT, bg);
  tft->setTextDatum(TC_DATUM);
  tft->drawString("NEXT TURN", rx + rw / 2, boxY + 4);

  // Distance value (auto-fit size)
  String distStr;
  if (distance < 0) {
    distStr = "--";
  } else if (distance >= 1000) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f km", distance / 1000.0);
    distStr = buf;
  } else {
    distStr = String(distance) + " m";
  }
  int size = 5;
  for (; size > 2; size--) {
    tft->setFreeFont(&Org_01);
    tft->setTextSize(size);
    if (tft->textWidth(distStr) <= rw - 8)
      break;
  }
  tft->setTextDatum(MC_DATUM);
  tft->setTextColor(COLOR_TEXT, bg);
  tft->drawString(distStr, rx + rw / 2, boxY + 62);

  // Arrival / destination box
  if (maneuver == NavigationManager::MANEUVER_ARRIVE) {
    tft->setFreeFont(&Org_01);
    tft->setTextSize(3);
    tft->setTextColor(TFT_GREEN, bg);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("ARRIVED", rx + rw / 2, boxY + 115);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_SECONDARY, bg);
    tft->setTextFont(1);
    tft->drawString("You have reached your destination", rx + rw / 2,
                    boxY + 145);
  } else {
    // GPS live speed
    float speed = gpsManager.getSpeedKmph();
    char buf[24];
    snprintf(buf, sizeof(buf), "SPD %d KM/H", (int)(speed + 0.5f));
    tft->setFreeFont(&Org_01);
    tft->setTextSize(2);
    tft->setTextColor(COLOR_ACCENT, bg);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(buf, rx + rw / 2, boxY + 115);
    tft->setTextSize(1);
    tft->setTextColor(COLOR_SECONDARY, bg);
    tft->drawString("GPS SPEED", rx + rw / 2, boxY + 140);
  }

  // Bottom bar
  tft->setFreeFont(&Org_01);
  tft->setTextSize(1);
  tft->setTextColor(COLOR_ACCENT, bg);
  tft->setTextDatum(TL_DATUM);
  tft->drawString("< BACK", 8, CONTENT_Y + CONTENT_H - 23);
  drawStatusChip(connected);

  // FONT SAFETY
  tft->setTextSize(1);
  tft->setFreeFont(NULL);
  tft->setTextFont(1);
  tft->setTextPadding(0);
}
