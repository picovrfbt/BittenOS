#include "screen.h"
#include "screen_font.h"
#define ARDUINOJSON_USE_DOUBLE 0
#include <ArduinoJson.h>
#define CONFIG_LITTLEFS_SPIFFS_COMPAT 1
#include <LittleFS.h>

//Kinds of element, and the lists the layout file names things by. The order matches
//data/screen_render.js, so a layout means the same thing in the designer and here.
enum : uint8_t { EL_TEXT, EL_VALUE, EL_BAR, EL_ICON, EL_RECT };

static const char *const SOURCES[] = {"face", "boops", "wifiClients", "wifiName", "brightness", "uptime",
                                      "temp", "closeness", "shade", "tilt", "boopSensor", "displays",
                                      "clock", "clockAmPm"};
#define SOURCE_COUNT (sizeof(SOURCES) / sizeof(SOURCES[0]))
static const char *const BARS[] = {"closeness", "shade", "brightness"};
#define BAR_COUNT (sizeof(BARS) / sizeof(BARS[0]))
static const char *const SHOWS[] = {"always", "booped", "notBooped", "sleeping", "awake", "displaysOff"};
#define SHOW_COUNT (sizeof(SHOWS) / sizeof(SHOWS[0]))

#define MAX_ELEMENTS 64            //across every page; a 128x64 screen cannot usefully hold more

struct Element {
  uint8_t page = 0;
  uint8_t type = EL_TEXT;
  uint8_t source = 0;
  uint8_t show = 0;
  uint8_t size = 1;
  uint8_t align = 0;               //0 left, 1 centre, 2 right
  int8_t icon = -1;
  int16_t x = 0, y = 0, w = 1, h = 1;
  bool fill = false, marker = false;
  String text, prefix, suffix;
};

static Element elements[MAX_ELEMENTS];
static int elementCount = 0;
static int pageCount = 0;
static uint8_t pageShow[SCREEN_MAX_PAGES];
static int pageHold[SCREEN_MAX_PAGES];
static int cycleSecs = 0;

static int lookup(const char *const *names, size_t count, const char *want, int fallback) {
  if(!want) return fallback;
  for(size_t i = 0; i < count; i++) if(strcmp(names[i], want) == 0) return (int)i;
  return fallback;
}

bool screenHasLayout() { return elementCount > 0 && pageCount > 0; }
int screenPageCount() { return pageCount; }
int screenElementCount() { return elementCount; }
uint8_t screenPageShow(int page) { return (page >= 0 && page < pageCount) ? pageShow[page] : PAGE_ALWAYS; }
int screenPageHold(int page) { return (page >= 0 && page < pageCount) ? pageHold[page] : 0; }
int screenCycleSecs() { return cycleSecs; }

//Reads one element out of the layout file into the flat list, tagged with the page it belongs to
static void loadElement(JsonObject o, uint8_t page) {
  if(elementCount >= MAX_ELEMENTS) return;
  Element &e = elements[elementCount];
  e = Element();
  e.page = page;
  const char *type = o["type"] | "text";
  if(strcmp(type, "value") == 0) e.type = EL_VALUE;
  else if(strcmp(type, "bar") == 0) e.type = EL_BAR;
  else if(strcmp(type, "icon") == 0) e.type = EL_ICON;
  else if(strcmp(type, "rect") == 0) e.type = EL_RECT;
  else e.type = EL_TEXT;
  e.x = o["x"] | 0;
  e.y = o["y"] | 0;
  e.w = o["w"] | 1;
  e.h = o["h"] | 1;
  e.size = constrain((int)(o["size"] | 1), 1, 3);
  e.fill = o["fill"] | false;
  e.marker = o["marker"] | false;
  e.text = (const char *)(o["text"] | "");
  e.prefix = (const char *)(o["prefix"] | "");
  e.suffix = (const char *)(o["suffix"] | "");
  e.show = (uint8_t)lookup(SHOWS, SHOW_COUNT, o["show"] | "always", 0);
  const char *align = o["align"] | "left";
  e.align = strcmp(align, "center") == 0 ? 1 : strcmp(align, "right") == 0 ? 2 : 0;
  if(e.type == EL_VALUE) e.source = (uint8_t)lookup(SOURCES, SOURCE_COUNT, o["source"] | "face", 0);
  if(e.type == EL_BAR) e.source = (uint8_t)lookup(BARS, BAR_COUNT, o["source"] | "closeness", 0);
  if(e.type == EL_ICON) e.icon = (int8_t)lookup(screenIconNames, SCREEN_ICON_COUNT, o["icon"] | "", 0);
  elementCount++;
}

bool screenLoadLayout() {
  elementCount = 0;
  pageCount = 0;
  cycleSecs = 0;
  File f = LittleFS.open("/screen.json", "r");
  if(!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if(err) return false;

  cycleSecs = constrain((int)(doc["cycle"] | 0), 0, 600);

  JsonArray pages = doc["pages"].as<JsonArray>();
  if(!pages.isNull()) {
    for(JsonObject p : pages) {
      if(pageCount >= SCREEN_MAX_PAGES) break;
      pageShow[pageCount] = (uint8_t)lookup(SHOWS, SHOW_COUNT, p["show"] | "always", PAGE_ALWAYS);
      pageHold[pageCount] = constrain((int)(p["hold"] | 0), 0, 10000);
      JsonArray els = p["elements"].as<JsonArray>();
      if(!els.isNull()) for(JsonObject o : els) loadElement(o, (uint8_t)pageCount);
      pageCount++;
    }
  } else {
    //Saved before pages existed: the whole file is one page, and it keeps working untouched
    JsonArray els = doc["elements"].as<JsonArray>();
    if(els.isNull()) return false;
    pageShow[0] = PAGE_ALWAYS;
    pageHold[0] = 0;
    pageCount = 1;
    for(JsonObject o : els) loadElement(o, 0);
  }
  return elementCount > 0 && pageCount > 0;
}

//--------------------------------//drawing helpers
static inline void setPixel(uint8_t *buf, int x, int y) {
  if(x < 0 || x > 127 || y < 0 || y > 63) return;
  buf[(y >> 3) * 128 + x] |= (uint8_t)(1 << (y & 7));
}

static uint8_t glyphWidth(char c) {
  int idx = (uint8_t)c - SCREEN_FONT_FIRST;
  if(idx < 0 || idx >= SCREEN_FONT_COUNT) idx = '?' - SCREEN_FONT_FIRST;
  return pgm_read_byte(&screenFontWidth[idx]);
}

static int textWidth(const String &s, uint8_t size) {
  if(s.length() == 0) return 0;
  int cols = 0;
  for(size_t i = 0; i < s.length(); i++) cols += glyphWidth(s[i]) + 1;
  return (cols - 1) * size;
}

static void drawText(uint8_t *buf, const String &s, int x, int y, uint8_t size) {
  int cx = x;
  for(size_t i = 0; i < s.length(); i++) {
    int idx = (uint8_t)s[i] - SCREEN_FONT_FIRST;
    if(idx < 0 || idx >= SCREEN_FONT_COUNT) idx = '?' - SCREEN_FONT_FIRST;
    uint8_t w = pgm_read_byte(&screenFontWidth[idx]);
    for(uint8_t c = 0; c < w; c++) {
      uint8_t col = pgm_read_byte(&screenFont[idx][c]);
      for(uint8_t r = 0; r < 8; r++) {
        if(!((col >> r) & 1)) continue;
        for(uint8_t sy = 0; sy < size; sy++)
          for(uint8_t sx = 0; sx < size; sx++) setPixel(buf, cx + c * size + sx, y + r * size + sy);
      }
    }
    cx += (w + 1) * size;
  }
}

static String fmtUptime(unsigned long s) {
  unsigned long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
  if(d) return String(d) + "d" + String(h) + "h";
  if(h) return String(h) + "h" + String(m) + "m";
  return String(m) + "m" + String(s % 60) + "s";
}

static String sourceText(uint8_t source, const ScreenData &d) {
  switch(source) {
    case 0: return d.face;
    case 1: return String(d.boops);
    case 2: return String(d.wifiClients);
    case 3: return d.wifiName;
    case 4: return String(d.brightness);
    case 5: return fmtUptime(d.uptime);
    case 6: return String((int)(d.tempC + 0.5f)) + "C";
    case 7: return String(d.closeness);
    case 8: return String(d.shade) + "%";
    case 9: return d.tilt ? "ok" : "--";
    case 10: return d.boopSensor ? "ok" : "--";
    case 11: return d.displayOn ? "on" : "off";
    case 12: {                            //the time, or a blinking 00:00 until a phone has told the mask
      if(d.clockMins < 0) return ((millis() / 600) % 2) ? "00:00" : "     ";
      int h = d.clockMins / 60, m = d.clockMins % 60;
      if(!d.clock24) {
        h = h % 12;
        if(h == 0) h = 12;
      }
      String out = (d.clock24 && h < 10) ? "0" + String(h) : String(h);
      return out + ":" + (m < 10 ? "0" + String(m) : String(m));
    }
    case 13:                              //am or pm on its own, for putting beside a big clock
      if(d.clockMins < 0) return "";
      return (d.clockMins < 720) ? "am" : "pm";
  }
  return "?";
}

static void barFractions(uint8_t source, const ScreenData &d, float &frac, float &mark, bool &hasMark) {
  hasMark = true;
  switch(source) {
    case 0: {
      float span = max(10, d.boopThresh * 2);
      frac = d.closeness / span;
      mark = d.boopThresh / span;
      break;
    }
    case 1:
      frac = d.shade / 100.0f;
      mark = d.shadeNeed / 100.0f;
      break;
    default:
      frac = d.brightness / 15.0f;
      hasMark = false;
      break;
  }
}

static bool visible(uint8_t show, const ScreenData &d) {
  switch(show) {
    case PAGE_BOOPED: return d.booping;
    case PAGE_NOTBOOPED: return !d.booping;
    case PAGE_SLEEPING: return d.sleeping;
    case PAGE_AWAKE: return !d.sleeping;
    case PAGE_DISPLAYSOFF: return !d.displayOn;
  }
  return true;
}

void screenRender(uint8_t *buf, const ScreenData &d, int page) {
  memset(buf, 0, 128 * 8);
  for(int i = 0; i < elementCount; i++) {
    const Element &e = elements[i];
    if(e.page != page) continue;
    if(!visible(e.show, d)) continue;
    if(e.type == EL_TEXT || e.type == EL_VALUE) {
      String s = e.type == EL_TEXT ? e.text : e.prefix + sourceText(e.source, d) + e.suffix;
      int w = textWidth(s, e.size);
      int x = e.align == 1 ? e.x - w / 2 : e.align == 2 ? e.x - w + 1 : e.x;
      drawText(buf, s, x, e.y, e.size);
    } else if(e.type == EL_ICON) {
      int8_t icon = e.icon < 0 ? 0 : e.icon;
      for(uint8_t r = 0; r < 8; r++) {
        uint8_t row = pgm_read_byte(&screenIcons[icon][r]);
        for(uint8_t c = 0; c < 8; c++) {
          if(!((row >> (7 - c)) & 1)) continue;
          for(uint8_t sy = 0; sy < e.size; sy++)
            for(uint8_t sx = 0; sx < e.size; sx++) setPixel(buf, e.x + c * e.size + sx, e.y + r * e.size + sy);
        }
      }
    } else if(e.type == EL_BAR) {
      int w = max((int)e.w, 3), h = max((int)e.h, 3);
      float frac = 0, mark = 0;
      bool hasMark = false;
      barFractions(e.source, d, frac, mark, hasMark);
      for(int x = 0; x < w; x++) { setPixel(buf, e.x + x, e.y); setPixel(buf, e.x + x, e.y + h - 1); }
      for(int y = 0; y < h; y++) { setPixel(buf, e.x, e.y + y); setPixel(buf, e.x + w - 1, e.y + y); }
      int inner = w - 4;
      int fill = (int)lround(constrain(frac, 0.0f, 1.0f) * inner);
      for(int x = 0; x < fill; x++) for(int y = 2; y < h - 2; y++) setPixel(buf, e.x + 2 + x, e.y + y);
      if(e.marker && hasMark) {
        int mx = e.x + 2 + (int)lround(constrain(mark, 0.0f, 1.0f) * (inner - 1));
        for(int y = -1; y <= h; y++) setPixel(buf, mx, e.y + y);
      }
    } else if(e.type == EL_RECT) {
      int w = max((int)e.w, 1), h = max((int)e.h, 1);
      for(int y = 0; y < h; y++) for(int x = 0; x < w; x++)
        if(e.fill || y == 0 || y == h - 1 || x == 0 || x == w - 1) setPixel(buf, e.x + x, e.y + y);
    }
  }
}
