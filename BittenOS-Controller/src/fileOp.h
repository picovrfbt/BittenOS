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
  int bEar,bVisor,bOled,rbSpeed,rbWidth,spMin,spMax,spTrig,boopThresh,sleepMins,transMs,boopCount,boopBase,boopShade;
  int autoBriSecs,autoBriMin,autoBriMax,autoBriDark,autoBriBright,boopMilestone;
  //Boop sass: this many boops inside sassSecs and it pulls a face at you. 0 turns it off.
  int sassBoops,sassSecs;
  String aSass;
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
  //Easter eggs: pressing the button this many times in quick succession starts one of them.
  //eggAction is slots, dino or none.
  int eggPresses;
  String eggAction;
  int slotWins, slotSpins;            //how the slot machine has treated you so far
  //The ESP-NOW button remote. Three buttons, each with a press and a hold action, set the same
  //way the mask's own button is. An empty MAC means nothing is paired yet.
  String rmtMac;
  String rmtAct[3],rmtFace[3];
  int rmtPhrase[3];
  String rmtHAct[3],rmtHFace[3];
  int rmtHPhrase[3];
  String rmtDAct[3],rmtDFace[3];
  int rmtDPhrase[3];
  //The cheek strip: one WS2812B chain down both sides, data on GPIO 18. The chain runs from one
  //side to the other, so the second side's LEDs count the opposite way round the face; rather than
  //guess which, each side gets a reverse switch and the Ends test shows which way it came out.
  //Brightness is yours to set outright - there is no cap, it just starts at about 30%.
  bool stripEna,stripRevA,stripRevB,stripBlushEna,stripIdleEna,stripRaveEna;
  int stripRaveGain;                  //percent: how hard the cheek meter is driven in rave mode
  int stripProxMin;                   //percent: how dim the glow starts when a paw first registers
  int stripProxStart;                 //percent of the boop distance at which it begins reacting
  bool stripAutoEna;                  //strip brightness follows the room, off the same light reading
  bool stripProxEna;                  //the cheeks warm as a paw comes in, before the boop lands
  int stripAutoMin;                   //percent the room-following is allowed to pull it down to
  bool stripFlareEna;                 //the pale sweep out from the snout before the blush settles
  bool stripBlushHold;                //blush for as long as the paw is there, not a fixed time
  int bStrip,stripNum,stripSplit,stripBlushNum,stripBlushMs;
  String stripBlushStr;
  //Which LEDs are the blush, one character per LED along the chain, '1' for in. Empty means fall
  //back to the outer stripBlushNum of each side, which is only a guess at how the chain was laid.
  String stripBlushMap;
  unsigned long stripBlushCol;
  String wakeFlickStyle;              //crt (slow, heavy stutter) or tube (fast and twitchy)
  int wakeFlickMs,wakeFlickGlitch;    //how long the warm-up runs, and how much the picture tears (0-2)
  int boopConfirm;                    //readings in a row over the line before it counts as a boop
  float neutralX,neutralY,neutralZ,tiltX,tiltY,tiltZ,sleepTol;
  float tiltTrig;                     //how far to lean before a tilt counts, 1 = as far as taught
  //Tilting the other way is a different reading, not the mirror of the first - one axis changes
  //sign - so it gets its own taught position rather than being worked out from the other side.
  float tilt2X,tilt2Y,tilt2Z;
  String aTilt,aTilt2,aBoop,aSleep,visColorStr,wifiName = "ProtoWiFi",wifiPass = "Proto1234";
  unsigned long visColor;
  void setDefault();
  bool save();
  bool load();
  bool getBool(AsyncWebServerRequest *req, const char *name, bool &out);
  bool getInt(AsyncWebServerRequest *req, const char *name, int &out);
  bool getFloat(AsyncWebServerRequest *req, const char *name, float &out);
  bool getString(AsyncWebServerRequest *req, const char *name, String &out);
};