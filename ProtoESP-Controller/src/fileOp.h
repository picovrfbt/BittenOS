#include <FastCRC.h>
#define ARDUINOJSON_USE_DOUBLE 0
#include <ArduinoJson.h>
#define CONFIG_LITTLEFS_SPIFFS_COMPAT 1
#include <LittleFS.h>
#include <StreamUtils.h>
#include <ESPAsyncWebServer.h>

//--------------------------------//CRC checksum class
class CrcWriter {
public:
  CrcWriter();
  size_t write(uint8_t c);
  size_t write(const uint8_t *buffer, size_t length);
  uint32_t hash() const;
private:
  FastCRC32 _hasher;
  uint32_t _hash;
};

//--------------------------------//Config variables store/save/load/default
class Config {
public:
  bool boopEna, speechEna, tiltEna, bleEna, oledEna, sleepEna, transEna, bobEna, boopCountEna, boopLightEna, autoBriEna, oledFlip;
  int bEar,bVisor,bOled,rbSpeed,rbWidth,spMin,spMax,spTrig,fanDuty,boopThresh,sleepMins,transMs,boopCount,boopBase,boopShade;
  int autoBriSecs,autoBriMin,autoBriMax,autoBriDark,autoBriBright,boopMilestone;
  String autoBriMode,mouthMode,aMilestone,blinkMode;
  int blinkDoublePct;                 //percent of the mask's blinks that come as a quick pair
  //Button: what a short press, a double press and a hold each do, and how long a hold is.
  //Actions are names: none, nextFace, prevFace, displays, sleep, face, phrase.
  String btnShort,btnDouble,btnHold,btnShortFace,btnDoubleFace,btnHoldFace;
  int btnHoldMs,btnShortPhrase,btnDoublePhrase,btnHoldPhrase;
  //Quick phrases: saved scrolling-text messages, one tap each
  String phrase[6];
  int phraseSpeed[6];
  bool phraseOnce[6];
  //Sleep: go deeper after a while, switching the panels off
  bool sleepDeepEna;
  int sleepDeepMins;
  //Joining an existing network, as well as making one. The mask keeps its own access point either
  //way, so a wrong password here can never lock you out of it.
  bool staEna;
  String staName,staPass,mdnsName;
  //Waking from deep sleep: the panels stutter back on one at a time, like an old device warming up
  bool wakeFlickEna,wakeFlickBoot;    //after a deep sleep, and again when the mask is first switched on
  bool bootBarEna;                    //a loading bar fills the maw at startup, before the warm-up
  int bootBarMs;                      //how long that bar takes, tuned separately from the warm-up
  int dinoBest;                       //best distance reached in the dino game on the maw
  String wakeFlickStyle;              //crt (slow, heavy stutter) or tube (fast and twitchy)
  int wakeFlickMs,wakeFlickGlitch;    //how long the warm-up runs, and how much the picture tears (0-2)
  float neutralX,neutralY,neutralZ,tiltX,tiltY,tiltZ,upX,upY,upZ,tiltTol,sleepTol;
  String aTilt,aUp,aBoop,aSleep,visColorStr,wifiName = "ProtoWiFi",wifiPass = "Proto1234";
  unsigned long visColor;
  void setDefault();
  bool save();
  bool load();
  bool getBool(AsyncWebServerRequest *req, const char *name, bool &out);
  bool getInt(AsyncWebServerRequest *req, const char *name, int &out);
  bool getFloat(AsyncWebServerRequest *req, const char *name, float &out);
  bool getString(AsyncWebServerRequest *req, const char *name, String &out);
};