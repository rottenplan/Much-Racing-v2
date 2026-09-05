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

// Draw a solid arrow in the given screen direction.
static void drawDirectionalArrow(TFT_eSPI *tft, float dx, float dy, int cx,
                                 int cy, float s, uint16_t color) {
  // Normalize direction
  float len = sqrtf(dx * dx + dy * dy);
  dx /= len;
  dy /= len;
  float px = -dy, py = dx; // perpendicular

  float tail = s * 0.38f;
  float neck = s * 0.18f;
  float headLen = s * 0.34f;
  float shaftHalf = s * 0.085f;
  float headHalf = s * 0.30f;

  // Shaft quad corners (center based)
  float ax = cx + (-dx * tail) + (-px * shaftHalf);
  float ay = cy + (-dy * tail) + (-py * shaftHalf);
  float bx = cx + (-dx * tail) + (px * shaftHalf);
  float by = cy + (-dy * tail) + (py * shaftHalf);
  float ex = cx + (dx * neck) + (px * shaftHalf);
  float ey = cy + (dy * neck) + (py * shaftHalf);
  float fx = cx + (dx * neck) + (-px * shaftHalf);
  float fy = cy + (dy * neck) + (-py * shaftHalf);

  // Head corners
  float hx = cx + (dx * (neck + headLen));
  float hy = cy + (dy * (neck + headLen));
  float gx = cx + (dx * neck) + (-px * headHalf);
  float gy = cy + (dy * neck) + (-py * headHalf);
  float ix = cx + (dx * neck) + (px * headHalf);
  float iy = cy + (dy * neck) + (py * headHalf);

  tft->fillTriangle((int)ax, (int)ay, (int)bx, (int)by, (int)ex, (int)ey,
                    color);
  tft->fillTriangle((int)ax, (int)ay, (int)ex, (int)ey, (int)fx, (int)fy,
                    color);
  tft->fillTriangle((int)gx, (int)gy, (int)ix, (int)iy, (int)hx, (int)hy,
                    color);
}

void NavigationScreen::drawArrowIcon(int maneuver, int cx, int cy, int size,
                                     uint16_t color) {
  TFT_eSPI *tft = _ui->getTft();
  float s = (float)size;
  switch (maneuver) {
  case NavigationManager::MANEUVER_STRAIGHT:
    drawDirectionalArrow(tft, 0.0f, -1.0f, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_SLIGHT_LEFT:
    drawDirectionalArrow(tft, -0.40f, -0.92f, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_LEFT:
    drawDirectionalArrow(tft, -1.0f, 0.0f, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_SHARP_LEFT:
    drawDirectionalArrow(tft, -0.86f, 0.51f, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_SLIGHT_RIGHT:
    drawDirectionalArrow(tft, 0.40f, -0.92f, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_RIGHT:
    drawDirectionalArrow(tft, 1.0f, 0.0f, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_SHARP_RIGHT:
    drawDirectionalArrow(tft, 0.86f, 0.51f, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_UTURN:
    drawDirectionalArrow(tft, 0.0f, 1.0f, cx, cy, s, color);
    break;
  case NavigationManager::MANEUVER_ROUNDABOUT:
    drawRoundaboutIcon(cx, cy, size, color);
    break;
  case NavigationManager::MANEUVER_ARRIVE:
    drawArriveIcon(cx, cy, size);
    break;
  default:
    drawDirectionalArrow(tft, 0.0f, -1.0f, cx, cy, s, color);
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
  int r = size / 2;
  tft->drawCircle(cx, cy, r, color);
  // Inner arrow suggestion
  drawDirectionalArrow(tft, 0.0f, -1.0f, cx, cy, (float)r * 0.8f, color);
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
