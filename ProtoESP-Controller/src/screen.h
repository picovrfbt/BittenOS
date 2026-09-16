//Status screen (SSD1306) drawn from /screen.json, the layout the on-board screen designer saves.
//The font and icons come from screen_font.h, generated from the designer's own tables, so the
//mask draws the same pixels the designer shows.
#pragma once
#include <Arduino.h>

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
};

bool screenLoadLayout();          //reads /screen.json; false if there isn't a usable one
bool screenHasLayout();
int screenElementCount();
//Draws into a 1024-byte buffer in u8g2's layout (page * 128 + x, bit = y & 7)
void screenRender(uint8_t *pageBuffer, const ScreenData &d);
