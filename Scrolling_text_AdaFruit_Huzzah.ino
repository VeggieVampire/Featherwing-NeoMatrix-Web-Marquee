/*
  HUZZAH Feather ESP8266 + Adafruit NeoPixel FeatherWing 4x8

  Final working mapping:
  Corner: TOP + RIGHT
  Layout: COLUMNS
  Pattern: PROGRESSIVE
  Rotation: 2

  Features:
  - Scroll text
  - Set message from Serial (115200) or Web
  - Each letter different color (palette editable from Web)
  - Rename device (hostname) from Web, saved in EEPROM
  - Palette saved in EEPROM
  - mDNS: http://devicename.local/
  - WiFi creds pulled from secrets.h (Option A)
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>

#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

#include "secrets.h"   // <-- Option A: WIFI_SSID / WIFI_PASS live here

// ------------------ Hardware ------------------
#define PIN 15

#define MW 4
#define MH 8

#define MATRIX_FLAGS (NEO_MATRIX_TOP + NEO_MATRIX_RIGHT + NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE)
#define MATRIX_ROTATION 2

Adafruit_NeoMatrix matrix(MW, MH, PIN, MATRIX_FLAGS, NEO_GRB + NEO_KHZ800);
ESP8266WebServer server(80);

// ------------------ Display settings ------------------
uint8_t brightness = 20;           // 0-255
uint16_t scrollIntervalMs = 120;   // bigger = slower
uint8_t extraSpacing = 0;          // wider letters if >0
bool bold = false;                 // true = thicker letters
bool hardClear = false;            // try true if you see trails
int8_t textYOffset = 0;            // 0 is best on 8px tall
uint8_t textSize = 1;

String msg = "HELLO ";
int16_t xPos = 0;
int16_t msgPixelWidth = 0;

String lineBuf;
unsigned long lastStepMs = 0;

// ------------------ Letter colors palette ------------------
static const uint8_t MAX_COLORS = 16;
uint16_t letterColors[MAX_COLORS];
uint8_t letterColorCount = 0;
String paletteText = "FF0000,00FF00,0000FF,FFFF00";

// ------------------ EEPROM layout ------------------
static const int EEPROM_SIZE = 256;

// device name stored 0..31
static const int NAME_ADDR = 0;
static const int NAME_MAX = 32;

// palette text stored 40..159
static const int PAL_ADDR = 40;
static const int PAL_MAX = 120;

String deviceName = "feather-sign";

// ------------------ Helpers ------------------
String sanitizeHostname(String s) {
  s.trim();
  s.toLowerCase();

  String out;
  out.reserve(NAME_MAX);

  for (uint16_t i = 0; i < s.length() && out.length() < NAME_MAX; i++) {
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-');
    if (ok) out += c;
    else if (c == ' ' || c == '_') out += '-';
  }

  while (out.startsWith("-")) out.remove(0, 1);
  while (out.endsWith("-")) out.remove(out.length() - 1);

  if (out.length() == 0) out = "feather-sign";
  return out;
}

String htmlEscape(const String& s) {
  String o;
  o.reserve(s.length());
  for (uint16_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else o += c;
  }
  return o;
}

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return matrix.Color(r, g, b);
}

bool parseHexRGBTo565(String t, uint16_t &out) {
  t.trim();
  if (t.startsWith("#")) t.remove(0, 1);
  if (t.length() != 6) return false;

  for (int i = 0; i < 6; i++) {
    char c = t[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    if (!ok) return false;
  }

  long v = strtol(t.c_str(), nullptr, 16);
  uint8_t r = (v >> 16) & 0xFF;
  uint8_t g = (v >> 8) & 0xFF;
  uint8_t b = v & 0xFF;
  out = rgb565(r, g, b);
  return true;
}

void setDefaultPalette() {
  letterColorCount = 4;
  letterColors[0] = rgb565(255, 0, 0);
  letterColors[1] = rgb565(0, 255, 0);
  letterColors[2] = rgb565(0, 0, 255);
  letterColors[3] = rgb565(255, 255, 0);
  paletteText = "FF0000,00FF00,0000FF,FFFF00";
}

int16_t calcMsgWidthPixels(const String& s) {
  int16_t perChar = 6 * textSize;
  int16_t w = (int16_t)s.length() * (perChar + extraSpacing);
  if (s.length() > 0) w -= extraSpacing;
  if (bold) w += 1;
  return w;
}

void resetScroll() {
  msgPixelWidth = calcMsgWidthPixels(msg);
  xPos = matrix.width();
}

void setMessage(const String& raw) {
  String s = raw;
  s.trim();
  if (s.length() == 0) return;
  if (s.length() > 120) s = s.substring(0, 120);

  msg = s + " ";
  Serial.print("New msg: ");
  Serial.println(msg);

  resetScroll();
}

void drawMessage(int16_t x, int16_t y) {
  int16_t cx = x;
  uint16_t colorIndex = 0;

  for (uint16_t i = 0; i < msg.length(); i++) {
    char c = msg[i];

    if (c != ' ' && letterColorCount > 0) {
      uint16_t col = letterColors[colorIndex % letterColorCount];
      matrix.drawChar(cx, y, c, col, 0, textSize);
      if (bold) matrix.drawChar(cx + 1, y, c, col, 0, textSize);
      colorIndex++;
    }

    cx += (6 * textSize) + extraSpacing;
  }
}

// ------------------ EEPROM save/load ------------------
String loadFixedString(int addr, int maxLen) {
  char buf[256];
  if (maxLen > 255) maxLen = 255;
  for (int i = 0; i < maxLen; i++) {
    byte b = EEPROM.read(addr + i);
    if (b == 0xFF) b = 0;
    buf[i] = (char)b;
  }
  buf[maxLen] = 0;
  String s = String(buf);
  s.trim();
  return s;
}

void saveFixedString(int addr, int maxLen, const String& s) {
  for (int i = 0; i < maxLen; i++) {
    byte v = 0;
    if (i < (int)s.length()) v = (byte)s[i];
    EEPROM.write(addr + i, v);
  }
  EEPROM.commit();
}

void loadDeviceNameAndPalette() {
  EEPROM.begin(EEPROM_SIZE);

  String n = loadFixedString(NAME_ADDR, NAME_MAX);
  if (n.length() > 0) deviceName = sanitizeHostname(n);
  else deviceName = "feather-sign";

  String pal = loadFixedString(PAL_ADDR, PAL_MAX);
  if (pal.length() > 0) paletteText = pal;
}

void saveDeviceName(const String& n) {
  deviceName = sanitizeHostname(n);
  saveFixedString(NAME_ADDR, NAME_MAX, deviceName);
}

void savePaletteText(const String& p) {
  paletteText = p;
  if (paletteText.length() > PAL_MAX) paletteText = paletteText.substring(0, PAL_MAX);
  saveFixedString(PAL_ADDR, PAL_MAX, paletteText);
}

void applyPaletteFromText(const String& raw) {
  String s = raw;
  s.trim();
  if (s.length() == 0) {
    setDefaultPalette();
    return;
  }

  uint16_t newColors[MAX_COLORS];
  uint8_t newCount = 0;

  int start = 0;
  while (start < (int)s.length() && newCount < MAX_COLORS) {
    int comma = s.indexOf(',', start);
    String tok = (comma == -1) ? s.substring(start) : s.substring(start, comma);
    tok.trim();

    uint16_t c565;
    if (parseHexRGBTo565(tok, c565)) {
      newColors[newCount++] = c565;
    }

    if (comma == -1) break;
    start = comma + 1;
  }

  if (newCount == 0) {
    setDefaultPalette();
    return;
  }

  for (uint8_t i = 0; i < newCount; i++) letterColors[i] = newColors[i];
  letterColorCount = newCount;
}

// ------------------ Web UI ------------------
String pageHtml() {
  String html;
  html += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Feather LED Text</title></head><body>";

  html += "<h2>LED Message</h2>";
  html += "<p><b>Device name:</b> " + htmlEscape(deviceName) + " (try http://" + htmlEscape(deviceName) + ".local/ )</p>";
  html += "<p><b>IP:</b> " + WiFi.localIP().toString() + "</p>";

  html += "<form action='/set' method='post'>";
  html += "Message:<br>";
  html += "<textarea name='m' rows='2' style='width:95%'>";
  html += htmlEscape(msg);
  html += "</textarea><br><br>";
  html += "<button type='submit'>Send</button>";
  html += "</form>";

  html += "<hr><h3>Settings</h3>";
  html += "<form action='/cfg' method='get'>";
  html += "Speed ms (bigger = slower):<br><input name='speed' value='" + String(scrollIntervalMs) + "'><br><br>";
  html += "Brightness 0-255:<br><input name='bright' value='" + String(brightness) + "'><br><br>";
  html += "Extra spacing:<br><input name='space' value='" + String(extraSpacing) + "'><br><br>";
  html += "Bold (0 or 1):<br><input name='bold' value='" + String(bold ? 1 : 0) + "'><br><br>";
  html += "Y offset (-8..8):<br><input name='y' value='" + String(textYOffset) + "'><br><br>";
  html += "Hard clear (0 or 1):<br><input name='hc' value='" + String(hardClear ? 1 : 0) + "'><br><br>";
  html += "<button type='submit'>Apply</button>";
  html += "</form>";

  html += "<hr><h3>Letter colors</h3>";
  html += "<form action='/colors' method='post'>";
  html += "Comma-separated hex RGB (RRGGBB). Example: FF0000,00FF00,0000FF<br>";
  html += "<input name='c' style='width:95%' value='" + htmlEscape(paletteText) + "'><br><br>";
  html += "<button type='submit'>Set colors</button>";
  html += "</form>";

  html += "<hr><h3>Rename device</h3>";
  html += "<form action='/name' method='post'>";
  html += "New name (letters, numbers, hyphen):<br>";
  html += "<input name='n' style='width:95%' value='" + htmlEscape(deviceName) + "'><br><br>";
  html += "<button type='submit'>Save and reboot</button>";
  html += "</form>";

  html += "<p>Serial also works at 115200. Send a line and it becomes the message.</p>";
  html += "</body></html>";
  return html;
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", pageHtml());
}

void handleSet() {
  if (!server.hasArg("m")) { server.send(400, "text/plain", "Missing m"); return; }
  setMessage(server.arg("m"));
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "OK");
}

void handleCfg() {
  if (server.hasArg("speed")) {
    int v = server.arg("speed").toInt();
    if (v < 10) v = 10;
    if (v > 2000) v = 2000;
    scrollIntervalMs = (uint16_t)v;
  }
  if (server.hasArg("bright")) {
    int v = server.arg("bright").toInt();
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    brightness = (uint8_t)v;
    matrix.setBrightness(brightness);
  }
  if (server.hasArg("space")) {
    int v = server.arg("space").toInt();
    if (v < 0) v = 0;
    if (v > 10) v = 10;
    extraSpacing = (uint8_t)v;
  }
  if (server.hasArg("bold")) {
    bold = (server.arg("bold").toInt() != 0);
  }
  if (server.hasArg("y")) {
    int v = server.arg("y").toInt();
    if (v < -8) v = -8;
    if (v > 8) v = 8;
    textYOffset = (int8_t)v;
  }
  if (server.hasArg("hc")) {
    hardClear = (server.arg("hc").toInt() != 0);
  }

  resetScroll();
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "OK");
}

void handleName() {
  if (!server.hasArg("n")) { server.send(400, "text/plain", "Missing n"); return; }
  saveDeviceName(server.arg("n"));
  server.send(200, "text/plain", "Saved. Rebooting...");
  delay(250);
  ESP.restart();
}

void handleColors() {
  if (!server.hasArg("c")) { server.send(400, "text/plain", "Missing c"); return; }
  String raw = server.arg("c");
  raw.trim();
  if (raw.length() == 0) { server.send(400, "text/plain", "Empty palette"); return; }
  applyPaletteFromText(raw);
  savePaletteText(raw);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "OK");
}

// ------------------ Setup / Loop ------------------
void setup() {
  Serial.begin(115200);
  delay(50);

  loadDeviceNameAndPalette();

  matrix.begin();
  matrix.setRotation(MATRIX_ROTATION);
  matrix.setBrightness(brightness);
  matrix.setTextWrap(false);
  matrix.setTextSize(textSize);

  setDefaultPalette();
  applyPaletteFromText(paletteText);

  resetScroll();

  WiFi.mode(WIFI_STA);
  WiFi.hostname(deviceName);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  // Credentials from secrets.h
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Device name: ");
  Serial.println(deviceName);

  if (MDNS.begin(deviceName.c_str())) {
    Serial.println("mDNS started");
  } else {
    Serial.println("mDNS failed");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/set", HTTP_POST, handleSet);
  server.on("/cfg", HTTP_GET, handleCfg);
  server.on("/name", HTTP_POST, handleName);
  server.on("/colors", HTTP_POST, handleColors);
  server.begin();

  Serial.println("Web server started.");
}

void loop() {
  server.handleClient();
  MDNS.update();

  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      setMessage(lineBuf);
      lineBuf = "";
    } else {
      lineBuf += ch;
      if (lineBuf.length() > 200) lineBuf.remove(0, 80);
    }
  }

  unsigned long now = millis();
  if (now - lastStepMs >= scrollIntervalMs) {
    lastStepMs = now;

    if (hardClear) {
      matrix.fillScreen(0);
      matrix.show();
      yield();
    } else {
      matrix.fillScreen(0);
    }

    drawMessage(xPos, textYOffset);
    matrix.show();
    yield();

    xPos--;
    if (xPos < -msgPixelWidth) xPos = matrix.width();
  }
}
