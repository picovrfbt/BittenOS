//Status screen (SSD1306) drawn from /screen.json, the layout the on-board screen designer saves.
//The font and icons come from screen_font.h, generated from the designer's own tables, so the
//mask draws the same pixels the designer shows.
//
//A layout holds one or more pages. Pages marked "always" are the normal rotation, stepped by the
//button or a timer; a page with a condition takes the screen over while that condition holds (a
//boop, say) and can linger for a moment afterwards. A layout saved before pages existed is just a
//list of elements, and loads as a single page.
#pragma once
#include <Arduino.h>

#define SCREEN_MAX_PAGES 8

//When a page is shown. Order matches the designer's list.
enum : uint8_t { PAGE_ALWAYS, PAGE_BOOPED, PAGE_NOTBOOPED, PAGE_SLEEPING, PAGE_AWAKE, PAGE_DISPLAYSOFF,
                 PAGE_SPEAKING, PAGE_NOTSPEAKING, PAGE_MILESTONE };

//Everything a layout can show. Filled in by the caller each time the screen is drawn.
struct ScreenData {
  String face;          //the face on the mask now, without .json
  String wifiName;
  int boops = 0;
  int wifiClients = 0;
  int brightness = 0;   //0-15
  unsigned long uptime = 0;  //seconds
  float tempC = 0;
  int closeness = 0;    //boop signal over resting
  int boopThresh = 15;
  int shade = 0;        //percent darker than normal
  int shadeNeed = 30;
  bool booping = false;
  bool sleeping = false;
  bool displayOn = true;
  bool tilt = false;
  bool boopSensor = false;
  int clockMins = -1;   //minutes past midnight, or -1 when no phone has said what time it is
  bool clock24 = false; //24 hour or 12 hour, following the phone that set it
  String ip;            //address on the joined network, empty when it hasn't joined one
  int micLevel = 0;     //0-100, how loud it is against the Speech range
  int micTrig = 10;     //the level the mouth starts moving at, same 0-100 scale
  bool speaking = false; //the mouth is moving right now
  bool milestone = false; //this boop hit the milestone, and the celebration is still running
};

bool screenLoadLayout();          //reads /screen.json; false if there isn't a usable one
bool screenHasLayout();
int screenPageCount();
int screenElementCount();         //across every page, for the startup log
uint8_t screenPageShow(int page); //one of the PAGE_ values
int screenPageHold(int page);     //ms a takeover page lingers after its condition ends
int screenCycleSecs();            //seconds between automatic steps through the always pages; 0 = manual only
//Draws one page into a 1024-byte buffer in u8g2's layout (page * 128 + x, bit = y & 7)
void screenRender(uint8_t *pageBuffer, const ScreenData &d, int page);

//The same font, for anything that draws its own screen rather than a designed layout - the slot
//machine takes the screen over while it spins, and wants captions in the screen's own type.
void screenDrawText(uint8_t *buf, const String &s, int x, int y, uint8_t size);
int screenTextWidth(const String &s, uint8_t size);
