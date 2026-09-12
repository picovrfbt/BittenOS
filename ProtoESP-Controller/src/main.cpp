//Make sure you have everything connected by the schematic in the repository and set these defines correctly!

#define MICpin ADC_CHANNEL_0 //Microphone, pin 1
#define T_in 2 //Output from Touch Sensor (for KY-032, Capac)
#define T_en 42 //Enable pin to Touch Sensor (for KY-032)
#define DATA_PIN_EARS 5  //Ears(Blush) (from outer to inner, POV-right cheek, (if blush: from top to bottom, right cheek nearest to ear first))
#define DATA_PIN_VISOR 7 //Face (right cheek, left segment of eye first)
#define I2C_SDA 8 //SDA for Gyro, OLED, INA219, ToF
#define I2C_SCL 9 //SCL for Gyro, OLED, INA219, ToF
#define MAX_CLK 12 //Clock for MAX72xx matrixes if used
#define MAX_MOSI 11 //Data for MAX72xx matrixes if used
#define MAX_CS 10 //ChipSelect for MAX72xx matrixes if used
#define animBtn 21 //Momentary button: short press = next face, hold 1.5s = displays off/on
#define animBtnGnd 47 //Driven LOW as the button's ground, so both button wires sit on neighbouring pins (47 is next to 21)
#define fanPWM 13 //PWM pin to control 4pin fan

#define visorType "MAX72XX" // What displays are you using? (WS2812 or MAX72XX so far)
#define HARDWARE_TYPE MD_MAX72XX::PAROLA_HW //flip up-down: ::DR1CR0RR1_HW , flip left-right: ::PAROLA_HW , flip both: ::ICSTATION_HW
#define MATRIXESNUM 14 // How many matrices for visor? 14 (FurBitten 14-string: 2x eye, 4x maw, 1x nose per side)
#define FADESTEPS 4 //how many steps when fading between frames? (0=disabled; only for WS2812 displays)

bool earPresent = false; // Are you using ear leds?
#define earLedsNum 74 // How many? (74 or 32 rn)

bool blushPresent = false; // Are you using blush leds?
#define blushLedsNum 8 // How many? (might crash under 8)
bool useRGBblush = true; //Swaps red-green for RGB strip

bool INApresent = false; //Are you using INA219?

#define boopMode "APDS9960" //"KY-032" for KY-032, "Capac" for capacitive sensor/boop when HIGH, "APDS9960" for ADPS9960, "VL53L1X" for VL53L1X, leave empty for none

#define revertTilt 8000 //The maximum time that animation caused by tilt gets shown (used as if tilt bugs out etc)

#define oldMatrixFix false //fix for Legacy WS2812B-2020 matrix

#define oledAddr 60 //define oled on address 0x3c

//--------------------------------//No touching after this!

#include <Arduino.h>

#include "esp_adc/adc_oneshot.h"
adc_oneshot_unit_handle_t adc_handle;

#define earTypeSize 6
#define visTypeSize 2
String earTypes[earTypeSize] = {"custom","rainbow","white_noise","corner_sabers","custom_glow","none"}; //available ear type animations
String visorTypes[visTypeSize] = {"custom","all_rainbow"}; //available visor type animations
String vTAcro[visTypeSize] = {"cust","rnbw"}; //OLED acronyms for visor type animations

#include <ezButton.h>
ezButton hwBtn(animBtn);

#include <StreamUtils.h>
#include <sstream>
#define FASTLED_ESP8266_RAW_PIN_ORDER
//#define FASTLED_RMT5 = 0 //doesnt compile
#define FASTLED_ESP32_FLASH_LOCK 1
#include <FastLED.h>
#define ARDUINOJSON_USE_DOUBLE 0
#include <ArduinoJson.h>

#define CONFIG_LITTLEFS_SPIFFS_COMPAT 1
#include <LittleFS.h>

#include "fileOp.h" //CRC + Config variables store/save/load/default
Config cfg;

#include "Misc.h" //Misc/helping functions

#include "oled.h"
SSDOLED oled;

#include "SparkFun_BMI270_Arduino_Library.h"
#include <Wire.h>
BMI270 imu; //tilt sensor, shares the I2C bus (SDA 8, SCL 9) with the boop sensor
float accelX = 0, accelY = 0, accelZ = 0; //latest accelerometer reading in g, updated by readAccel() in loop()

bool imuBegin() { //the I2C address depends on the board's SDO pin
  if(imu.beginI2C(BMI2_I2C_PRIM_ADDR) == BMI2_OK) return true; //0x68
  return imu.beginI2C(BMI2_I2C_SEC_ADDR) == BMI2_OK;           //0x69
}

void readAccel() {
  if(imu.getSensorData() == BMI2_OK) {
    accelX = imu.data.accelX;
    accelY = imu.data.accelY;
    accelZ = imu.data.accelZ;
  }
}

#include <Adafruit_INA219.h> //edited library in this sketch (replace 0.1R with 0.03R resistor on the board)
Adafruit_INA219 ina219;

#include "Adafruit_APDS9960.h"
Adafruit_APDS9960 apds;

int apdsprox = 255; //latest APDS9960 proximity (0-255, higher = closer), read by loop() every 100 ms

bool apdsStart() { //proximity only, with the pulse setting the boop threshold is tuned for
  if(!apds.begin()) return false;
  apds.enableProximity(true);
  apds.setProxPulse(APDS9960_PPULSELEN_8US, 8);
  return true;
}

#include "Adafruit_VL53L1X.h"
Adafruit_VL53L1X vl53;

//--------------------------------//realtime logger
#define LOG_BUFFER_SIZE (50 * 1024)  // 50 KB

char *logBuffer = nullptr;
size_t logIndex = 0;

void logPrint(const char *str) {
  Serial.println(str);
  if (!logBuffer) return;
  size_t len = strlen(str);
  size_t needed = len + 1; //newline
  if (logIndex + needed >= LOG_BUFFER_SIZE) {
    logIndex = 0;
    logBuffer[0] = '\0';
  }
  memcpy(logBuffer + logIndex, str, len);
  logIndex += len;
  logBuffer[logIndex++] = '\n';
  logBuffer[logIndex] = '\0';
}

inline void logPrint(const __FlashStringHelper *str) {
  logPrint((const char*)str);
}

void logPrint(const String &str) {
    logPrint(str.c_str());
}

//--------------------------------//web / wifi
#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include <ElegantOTA.h>

AsyncWebServer server(80);

//--------------------------------//Config vars
bool instantReload = false, oledInitDone = false, tiltInitDone = false, getfilesProper = true, ToFInitDone = false;
bool brightnessDirty = false; //set by /brightness, applied to the panels in loop()
bool displayOn = true, displayDirty = false; //set by /display; panels stay powered down until turned back on (not saved)
bool sleeping = false; //the sleeping face is showing
//Mouth bob: lift the mouth panels one row every so often, so a still face has a bit of life.
//Done here rather than in the animations so it can be switched on and off while running, and
//so it works on any face - the mouth panels are the ones flagged isMouth, as speech uses.
bool bobUp = false;
#define bobPeriod 1100   //ms between lifts
#define bobLift    220   //ms the mouth stays lifted
volatile int8_t sleepRequest = 0; //from /sleep: 1 = sleep now, -1 = wake, 0 = nothing
unsigned long stillSince = 0; //when the mask last moved
float stillX = 0, stillY = 0, stillZ = 0; //what "not moving" currently looks like
uint8_t moveSamples = 0; //readings in a row over the threshold, so a single noisy one can't wake it
String wakeAnim = ""; //the face to go back to when it wakes
//Face transitions (loop task only): dip the panels dark, swap the face where the cut can't be seen, bring them back.
//The MAX7219 chain runs in wiring order, not the order the panels sit on the face, so a wipe would sweep raggedly;
//dimming is global, so it looks the same whatever the layout.
uint8_t transPhase = 0;                 //0 = idle, 1 = going dark, 2 = coming back
int8_t transLevel = 0;                  //panel intensity while the dip runs
unsigned long transLastStep = 0;
uint16_t transStepMs = 16;              //ms per intensity step, worked out from cfg.transMs when a dip starts
String transAnim = "", transPost = "";  //the face waiting for the bottom of the dip
uint32_t loopCount = 0, loopsPerSec = 0; unsigned long loopRateStart = 0; //loop() runs per second, for the system monitor
uint16_t currentEarsFrame = 0, currentVisorFrame = 0; //frame indexes: scrolling text can have hundreds of frames
uint8_t numOfSegm, numAnimBlush, totalAnims;
uint16_t visorLedsNum = MATRIXESNUM*64;
String currentAnim = "", animToLoad = "", availAnims[50], getfilesCache;
// Web and BLE requests run on other tasks while loop() draws from visorNow->frames.
// They only queue a load here; loop() does the actual loadAnim() between frames.
String postAnimToLoad = "";
SemaphoreHandle_t animQueueMutex = NULL;
String visorThen = ""; //optional "then" of the current animation: the face to load after one pass (loop task only)
float micDC = 800;

//--------------------------------//getting stored anims names and count
void getFilesFunc() {
  getfilesCache = "";
  totalAnims = 0;
  File root = LittleFS.open("/anims");
  File file = root.openNextFile();
  while(file){
    availAnims[totalAnims] = String(file.name());
    getfilesCache += availAnims[totalAnims] + ";";
    totalAnims++;
    file = root.openNextFile();
  }
  getfilesProper = false;
}

//--------------------------------//Structs for anims in psram
struct FramesEars {
  int timespan;
  long ledColor[earLedsNum];
};

struct AnimNowEars {
  int type;
  int numOfFrames;
  FramesEars* frames = nullptr;
};

struct FramesVisor {
  int timespan;
  uint64_t leds[MATRIXESNUM];
  long ledsBlush[blushLedsNum];
  long fColor[MATRIXESNUM];
  long ppColor[MATRIXESNUM][64];
};

struct AnimNowVisor {
  int type;
  int numOfFrames;
  FramesVisor* frames = nullptr;
  bool isMouth[MATRIXESNUM];
};

AnimNowEars* earsNow;
AnimNowVisor* visorNow;

//--------------------------------//MAX LEDs
#include <MD_MAX72xx.h>
#include <SPI.h>

MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, MAX_MOSI, MAX_CLK, MAX_CS, MATRIXESNUM);

//--------------------------------//WS2812 LEDs
CRGB earLeds[earLedsNum];
CRGB blushLeds[blushLedsNum];
CRGB visorLeds[MATRIXESNUM*64];
CRGB visorLedsNEW[MATRIXESNUM*64];
CRGB c2Leds[earLedsNum+blushLedsNum];

CLEDController *ledController[2];

CRGB pixelBuffer[18];
CRGB visorPixelBuffer[10];
uint8_t noiseData[earLedsNum];

DEFINE_GRADIENT_PALETTE( blackWhite_gp ) {
  0,   100,  0, 0,
  120,   0,  0, 0,
  255, 255,  255, 255
};
CRGBPalette16 blackWhite = blackWhite_gp;

const std::vector<std::vector<int>> lookupDiag1 = 
{{14,15,16},
 {13,27,28,1},
 {12,26,36,17,2},
 {25,35,29,18},
 {11,34,37,30,3},
 {24,33,31,19},
 {10,23,32,20,4},
 {9,22,21,5},
 {8,7,6}};

const std::vector<std::vector<int>> lookupDiag2 = 
{{2,3,4},
 {1,18,19,5},
 {16,17,30,20,6},
 {28,29,31,21},
 {15,36,37,32,7},
 {27,35,33,22},
 {14,26,34,23,8},
 {13,25,24,9},
 {12,11,10}};

//--------------------------------//Load functions
bool loadAnim(String anim, String temp) {
  JsonDocument doc;
  DeserializationError error;

  if (currentAnim != anim || anim == "POSTAnimLoad") {
    if(anim == "POSTAnimLoad") {
      logPrint(F("[I] POST load"));
      error = deserializeJson(doc, temp);
    } else {
      delay(25);
      File file = LittleFS.open("/anims/"+anim, "r");
      if (!file) {
        logPrint(F("[E] There was an error opening the animation file!"));
        file.close();
        return false;
      }
      logPrint(F("[I] Animation file opened!"));
      ReadBufferingStream bufferedFile{file, 64};
      error = deserializeJson(doc, bufferedFile);
      file.close();
    }
    
    if(error){
      logPrint("[E] Failed to deserialize animation file! : " + String(error.c_str()));
      return false;
    }

    // Free previously allocated frames
    if (earsNow->frames) { free(earsNow->frames); earsNow->frames = nullptr; }
    if (visorNow->frames) { free(visorNow->frames); visorNow->frames = nullptr; }

    xSemaphoreTake(animQueueMutex, portMAX_DELAY);
    currentAnim = anim;
    xSemaphoreGive(animQueueMutex);

    //Ears anim type
    for(int o=0;o<earTypeSize;o++) {
      if(doc["ears"]["type"].as<String>() == earTypes[o]) {
        earsNow->type = o;
        break;
      }
    }
    //Ears anim load
    earsNow->numOfFrames = doc["ears"]["frames"].size();
    earsNow->frames = (FramesEars*) ps_malloc(sizeof(FramesEars) * earsNow->numOfFrames);
    for(int x = 0; x < earsNow->numOfFrames; x++) {
      earsNow->frames[x].timespan = doc["ears"]["frames"][x]["timespan"].as<int>();
      for(int y = 0; y < doc["ears"]["frames"][x]["leds"].size(); y++) {
        earsNow->frames[x].ledColor[y] = strtol(doc["ears"]["frames"][x]["leds"][y].as<String>().c_str(), NULL, 16);
      }
    }
    //Visor anim type
    for(int o=0;o<visTypeSize;o++) {
      if(doc["visor"]["type"].as<String>() == visorTypes[o]) {
        visorNow->type = o;
        break;
      }
    }
    //One-shot animations (e.g. scrolling text set to play once) name the face to go back to
    visorThen = doc["visor"]["then"] | "";
    //isMouth
    for(int y = 0; y < doc["visor"]["isMouth"].size(); y++) {
      visorNow->isMouth[y] = doc["visor"]["isMouth"][y].as<bool>();
    }
    //Visor anim load
    visorNow->numOfFrames = doc["visor"]["frames"].size();
    visorNow->frames = (FramesVisor*) ps_malloc(sizeof(FramesVisor) * visorNow->numOfFrames);
    for(int x = 0; x < visorNow->numOfFrames; x++) {
      visorNow->frames[x].timespan = doc["visor"]["frames"][x]["timespan"].as<int>();
      numAnimBlush = (uint8_t)doc["visor"]["frames"][x]["ledsBlush"].size();
      for(int y = 0; y < numAnimBlush; y++) {
        if(y<blushLedsNum) {
          visorNow->frames[x].ledsBlush[y] = strtol(doc["visor"]["frames"][x]["ledsBlush"][y].as<String>().c_str(), NULL, 16);
        }
      }
      numOfSegm = doc["visor"]["frames"][x]["leds"].size();
      for(int y = 0; y < numOfSegm; y++) {
        visorNow->frames[x].fColor[y] = strtol(doc["visor"]["frames"][x]["fColor"][y].as<String>().c_str(), NULL, 16); //should return 0 if not present
        visorNow->frames[x].leds[y] = strtoull(doc["visor"]["frames"][x]["leds"][y].as<String>().c_str(), NULL, 16); //string to uint64
      }
      for(int i = 0; i < MATRIXESNUM; i++) { //wipe ppColor data
        for(int o = 0; o < 64; o++) {
          visorNow->frames[x].ppColor[i][o] = 0;
        }
      }
      int numOfpp = doc["visor"]["frames"][x]["ppColor"].size();
      for(int y = 0; y < numOfpp; y++) {
        int numOfppData = doc["visor"]["frames"][x]["ppColor"][y]["data"].size();
        for(int z = 0; z < numOfppData; z++) { //ppColor[matrix][pixel] = color
          visorNow->frames[x].ppColor[doc["visor"]["frames"][x]["ppColor"][y]["mIndex"].as<int>()][doc["visor"]["frames"][x]["ppColor"][y]["data"][z][0].as<int>()] = strtol(doc["visor"]["frames"][x]["ppColor"][y]["data"][z][1].as<String>().c_str(), NULL, 16);
        }
      }
    }

    instantReload = true;
    currentVisorFrame = 0;
    currentEarsFrame = 0;

    if(cfg.oledEna && oledInitDone) {
      oled.writeAnim(anim.substring(0,anim.length()-5));
      oled.writeRGB(vTAcro[visorNow->type]);
    }
    return true;
  }
  return false;
}

//--------------------------------//BLE
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 2
#define CONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED
#define CONFIG_BT_NIMBLE_ROLE_OBSERVER_DISABLED
#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1
#include "NimBLEDevice.h"

BLEServer *pServer = NULL;
BLECharacteristic * pCharacteristic;
BLEAdvertising* pAdvertising;

class MyCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
      String temp = String(pCharacteristic->getValue().c_str());
      if(temp.charAt(0) == 'g') { //legacy remote reasons
        pCharacteristic->setValue("i"+String(totalAnims));
        pCharacteristic->notify();
      } else if (temp.charAt(0) == '?') {
        String animtemp;
        for(int i = 0; i < totalAnims; i++) {
          animtemp += availAnims[i].substring(0, availAnims[i].length() - 5);
          animtemp += ";";
        }
        pCharacteristic->setValue(animtemp);
        pCharacteristic->notify(true);
      } else if (temp.charAt(0) == ';') { //command
        if (temp.indexOf("rgb") > 0 && visorType == "WS2812") {
          visorNow->type++;
          if(visorNow->type == visTypeSize)
            visorNow->type = 0;
          if(cfg.oledEna && oledInitDone)
            oled.writeRGB(vTAcro[visorNow->type]);
        } else if (temp.indexOf("set") > 0) {
          temp.remove(0,4);
          if(cfg.oledEna && oledInitDone)
            oled.writeSet(temp.toInt()+1);
        }
      } else if (temp.toInt() > 0 && temp.toInt() <= totalAnims){ //legacy remote reasons
        xSemaphoreTake(animQueueMutex, portMAX_DELAY);
        animToLoad = availAnims[temp.toInt()-1];
        xSemaphoreGive(animQueueMutex);
      } else {
        for(int i = 0; i < totalAnims; i++) {
          if(temp == availAnims[i].substring(0, availAnims[i].length() - 5)) {
            xSemaphoreTake(animQueueMutex, portMAX_DELAY);
            animToLoad = availAnims[i];
            xSemaphoreGive(animQueueMutex);
          }
        }
      }
      logPrint("[I] BT Recv.: "+temp);
    };
} chrCallbacks;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
      NimBLEDevice::startAdvertising();
  }
} serverCallbacks;

bool startBLE() {
  std::string stdStr(cfg.wifiName.c_str(), cfg.wifiName.length());
  BLEDevice::init(stdStr);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(&serverCallbacks);

  BLEService *pService = pServer->createService("ffe0");

  pCharacteristic = pService->createCharacteristic("ffe1",
      NIMBLE_PROPERTY::BROADCAST | NIMBLE_PROPERTY::READ  |
      NIMBLE_PROPERTY::NOTIFY    | NIMBLE_PROPERTY::WRITE |
      NIMBLE_PROPERTY::INDICATE
  );
  pCharacteristic->setValue(totalAnims);
  pCharacteristic->setCallbacks(&chrCallbacks);

  pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(stdStr);
  pAdvertising->addServiceUUID(BLEUUID(pService->getUUID()));
  pAdvertising->enableScanResponse(true);
  if(!pAdvertising->start(0)) {
    return false;
  }
  return true;
}

//--------------------------------//WiFi server setup
void startWiFiWeb() {
  WiFi.softAP(cfg.wifiName, cfg.wifiPass);

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.on("/getfiles", HTTP_GET, [](AsyncWebServerRequest *request){ //returns current anim + all avaible anims
    if(getfilesProper) {
      getFilesFunc();
    }
    xSemaphoreTake(animQueueMutex, portMAX_DELAY);
    String cur = currentAnim;
    xSemaphoreGive(animQueueMutex);
    request->send(200, "text/plain", cur+";"+getfilesCache);
  });

  server.on("/saveconfig", HTTP_GET, [](AsyncWebServerRequest *request){ //saves config
    //features enable
    cfg.getBool(request, "boopEna", cfg.boopEna);
    cfg.getBool(request, "speechEna", cfg.speechEna);
    cfg.getBool(request, "tiltEna", cfg.tiltEna);
    cfg.getBool(request, "bleEna", cfg.bleEna);
    cfg.getBool(request, "oledEna", cfg.oledEna);
    cfg.getBool(request, "sleepEna", cfg.sleepEna);
    cfg.getBool(request, "transEna", cfg.transEna);
    cfg.getBool(request, "bobEna", cfg.bobEna);
    cfg.getInt(request, "transMs", cfg.transMs);
    cfg.getFloat(request, "sleepTol", cfg.sleepTol);
    //brightness
    cfg.getInt(request, "bEar", cfg.bEar);
    cfg.getInt(request, "bVisor", cfg.bVisor);
    if(cfg.getInt(request, "bOled", cfg.bOled)) {
        if(cfg.oledEna && oledInitDone)
            oled.oledBright(cfg.bOled);
    }
    //animation settings
    cfg.getInt(request, "rbSpeed", cfg.rbSpeed);
    cfg.getInt(request, "rbWidth", cfg.rbWidth);
    cfg.getInt(request, "spMin", cfg.spMin);
    cfg.getInt(request, "spMax", cfg.spMax);
    cfg.getInt(request, "spTrig", cfg.spTrig);
    //tilt animations
    cfg.getString(request, "aTilt", cfg.aTilt);
    cfg.getString(request, "aUp", cfg.aUp);
    cfg.getString(request, "aBoop", cfg.aBoop);
    cfg.getString(request, "aSleep", cfg.aSleep);
    //tilt neutral
    cfg.getFloat(request, "neutralX", cfg.neutralX);
    cfg.getFloat(request, "neutralY", cfg.neutralY);
    cfg.getFloat(request, "neutralZ", cfg.neutralZ);
    //tilt triggers
    cfg.getFloat(request, "tiltX", cfg.tiltX);
    cfg.getFloat(request, "tiltY", cfg.tiltY);
    cfg.getFloat(request, "tiltZ", cfg.tiltZ);
    cfg.getFloat(request, "upX", cfg.upX);
    cfg.getFloat(request, "upY", cfg.upY);
    cfg.getFloat(request, "upZ", cfg.upZ);
    cfg.getFloat(request, "tiltTol", cfg.tiltTol);
    //RGB visor color
    if(cfg.getString(request, "visColor", cfg.visColorStr))
        cfg.visColor = strtol(cfg.visColorStr.c_str() + 1, NULL, 16);
    //wifi
    cfg.getString(request, "wifiName", cfg.wifiName);
    cfg.getString(request, "wifiPass", cfg.wifiPass);
    //boop threshold
    cfg.getInt(request, "boopThresh", cfg.boopThresh);
    cfg.getInt(request, "sleepMins", cfg.sleepMins);
    delay(25);
    if(cfg.save()) {
      instantReload = true;
      request->redirect("/saved.html?main");
    } else {
      request->send(200, "text/plain", F("Saving config failed!"));
    }
  });

  server.on("/savefile", HTTP_POST, [](AsyncWebServerRequest *request){ //saves data from POST to file
    if(request->hasParam("file", true) && request->hasParam("content", true)) {
      File file = LittleFS.open("/anims/"+request->getParam("file", true)->value()+".json", "w");
      if (!file) {
        logPrint(F("[E] There was an error opening the file for saving an animation!"));
        file.close();
        request->send(200, "text/plain", F("Error opening file for writing!"));
      } else {
        logPrint(F("[I] File saved!"));
        file.print(request->getParam("content", true)->value());
        file.close();
        getfilesProper = true;
        request->redirect("/saved.html?anim");
      }
    } else {
      request->send(200, "text/plain", F("No valid parameters detected!"));
    }
  });

  server.on("/deletefile", HTTP_GET, [](AsyncWebServerRequest *request){ //deletes asked file
    if(request->hasParam("file")) {
      LittleFS.remove("/anims/"+request->getParam("file")->value());
      getfilesProper = true;
      request->redirect("/saved.html?main");
    } else {
      request->send(200, "text/plain", F("Parameter 'file' not present!"));
    }
  });

  server.on("/change", HTTP_GET, [](AsyncWebServerRequest *request){ //queues anim from selected avaible anims, loop() loads it
    if(request->hasParam("anim")) {
      String anim = request->getParam("anim")->value();
      if(LittleFS.exists("/anims/"+anim)) {
        xSemaphoreTake(animQueueMutex, portMAX_DELAY);
        animToLoad = anim;
        xSemaphoreGive(animQueueMutex);
        request->redirect("/saved.html?main");
      } else {
        request->send(200, "text/plain", F("Loading animation has failed!"));
      }
    } else {
      request->send(200, "text/plain", F("No valid parameters detected!"));
    }
  });

  server.on("/change", HTTP_POST, [](AsyncWebServerRequest *request){ //queues anim from POST request, loop() loads it
    if(request->hasParam("anim", true)) {
      xSemaphoreTake(animQueueMutex, portMAX_DELAY);
      postAnimToLoad = request->getParam("anim", true)->value();
      xSemaphoreGive(animQueueMutex);
      request->redirect("/saved.html?main");
    } else {
      request->send(200, "text/plain", F("No valid parameters detected!"));
    }
  });

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.on("/fanpwm", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("duty")) {
      int duty = request->getParam("duty")->value().toInt();
      if(duty < 256 && duty >= 0) {
        //ledcWrite(0, duty);
        ledcWrite(fanPWM, duty); //Arduino 3.x core
        cfg.fanDuty = duty;
        cfg.save();
        request->send(200, "text/plain", "Set PWM to: " + String(duty));
      } else {
        request->send(200, "text/plain", F("Invalid duty cycle"));
      }
    } else {
      request->send(200, "text/plain", F("No valid parameters detected!"));
    }
  });
  
  server.on("/stats", HTTP_GET, [](AsyncWebServerRequest *request){ //system monitor for the homepage; no I2C here, sensor state is what loop() found
    xSemaphoreTake(animQueueMutex, portMAX_DELAY);
    String anim = currentAnim;
    xSemaphoreGive(animQueueMutex);
    JsonDocument st;
    st["uptime"] = millis() / 1000;
    st["heapFree"] = ESP.getFreeHeap();
    st["heapSize"] = ESP.getHeapSize();
    st["psramFree"] = ESP.getFreePsram();
    st["psramSize"] = ESP.getPsramSize();
    st["fsUsed"] = LittleFS.usedBytes();
    st["fsTotal"] = LittleFS.totalBytes();
    st["tempC"] = temperatureRead();
    st["loopHz"] = loopsPerSec;
    st["wifiClients"] = WiFi.softAPgetStationNum();
    st["tilt"] = tiltInitDone;
    st["boop"] = ToFInitDone;
    st["display"] = displayOn;
    st["sleeping"] = sleeping;
    st["anim"] = anim;
    st["frame"] = currentVisorFrame;
    st["frames"] = visorNow ? visorNow->numOfFrames : 0;
    String out;
    serializeJson(st, out);
    request->send(200, "application/json", out);
  });

  server.on("/sleep", HTTP_GET, [](AsyncWebServerRequest *request){ //sleeping face: state=on|off|toggle, no param = report
    if(request->hasParam("state")) {
      String state = request->getParam("state")->value();
      if(state != "on" && state != "off" && state != "toggle") {
        request->send(200, "text/plain", F("Invalid state"));
        return;
      }
      bool want = (state == "toggle") ? !sleeping : (state == "on");
      sleepRequest = want ? 1 : -1; //loop() does the face change
      request->send(200, "text/plain", want ? "on" : "off");
      return;
    }
    request->send(200, "text/plain", sleeping ? "on" : "off");
  });

  server.on("/display", HTTP_GET, [](AsyncWebServerRequest *request){ //visor panels on/off while the ESP keeps running: state=on|off|toggle, no param = report
    if(request->hasParam("state")) {
      String state = request->getParam("state")->value();
      if(state != "on" && state != "off" && state != "toggle") {
        request->send(200, "text/plain", F("Invalid state"));
        return;
      }
      displayOn = (state == "toggle") ? !displayOn : (state == "on");
      displayDirty = true;
    }
    request->send(200, "text/plain", displayOn ? "on" : "off");
  });

  server.on("/brightness", HTTP_GET, [](AsyncWebServerRequest *request){ //MAX7219 brightness 0-15, save=1 also stores it
    if(request->hasParam("value")) {
      int value = request->getParam("value")->value().toInt();
      if(value >= 0 && value <= 15) {
        cfg.bVisor = value;
        brightnessDirty = true;
        if(request->hasParam("save")) {
          cfg.save();
        }
        request->send(200, "text/plain", "Set brightness to: " + String(value));
      } else {
        request->send(200, "text/plain", F("Invalid brightness"));
      }
    } else {
      request->send(200, "text/plain", F("No valid parameters detected!"));
    }
  });

  server.on("/gyro", HTTP_GET, [](AsyncWebServerRequest *request){ //latest accelerometer reading (x;y;z in g), read by loop()
    if(!tiltInitDone) {
      request->send(200, "text/plain", F("Tilt sensor (BMI270) not found"));
      return;
    }
    request->send(200, "text/plain", String(floor(accelX*100)/100)+";"+String(floor(accelY*100)/100)+";"+String(floor(accelZ*100)/100));
  });

  server.on("/tof", HTTP_GET, [](AsyncWebServerRequest *request){ //current boop sensor reading, for setting the Boop distance
    if(!ToFInitDone) {
      request->send(200, "text/plain", F("Boop sensor not found"));
    } else if (boopMode == "APDS9960") {
      request->send(200, "text/plain", String(255 - apdsprox)); //read by loop(), not from the web task
    } else if (boopMode == "VL53L1X") {
      if (vl53.dataReady()) {
        request->send(200, "text/plain", String(vl53.distance()));
      } else {
        request->send(200, "text/plain", F("Data not ready!"));
      }
    } else {
      request->send(200, "text/plain", F("No distance sensor configured"));
    }
  });
  
  server.on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", logBuffer);
  });

  server.onNotFound([](AsyncWebServerRequest *request){request->send(404, "text/plain", "Not found");});
  ElegantOTA.begin(&server);
  server.begin();

  ElegantOTA.setAutoReboot(true);
}

//--------------------------------//Sleep mode
void setSleeping(bool on) { //loop task only: it loads animations
  if(on == sleeping) return;
  sleeping = on;
  if(on) {
    wakeAnim = currentAnim;
    logPrint("[I] Sleeping: " + cfg.aSleep);
    loadAnim(cfg.aSleep,"");
  } else {
    logPrint(F("[I] Woke up"));
    if(wakeAnim != "" && wakeAnim != cfg.aSleep) loadAnim(wakeAnim,"");
    wakeAnim = "";
  }
  stillSince = millis();
}

//--------------------------------//Setup
void setup() {
  animQueueMutex = xSemaphoreCreateMutex();
  Serial.begin(115200);

  pinMode(animBtn, INPUT_PULLUP);
  pinMode(animBtnGnd, OUTPUT); //the button's other leg
  digitalWrite(animBtnGnd, LOW);
  pinMode(0, INPUT_PULLUP);
  pinMode(fanPWM, OUTPUT);

  if(boopMode == "KY-032") {
    pinMode(T_in, INPUT_PULLUP);
    pinMode(T_en, OUTPUT);
  } else if ((boopMode == "Capac")) {
    pinMode(T_in, INPUT_PULLDOWN);
  }

  hwBtn.setDebounceTime(50);

  if(!LittleFS.begin(true)) {
    logPrint(F("[E] An Error has occurred while mounting LittleFS! Halting"));
    while(1){};
  }

  if(psramInit() && ESP.getFreePsram() != 0) {
    earsNow  = (AnimNowEars *)  ps_calloc(1, sizeof(AnimNowEars));
    visorNow = (AnimNowVisor *) ps_calloc(1, sizeof(AnimNowVisor));
    logBuffer = (char*) ps_malloc(LOG_BUFFER_SIZE);
    logIndex = 0;
    logBuffer[0] = '\0';
  } else {
    logPrint(F("[E] Could not init PSRAM, either this ESP doesn't have one or is malfunctioning, halting..."));
    while(1){};
  }

  if(!cfg.load()) {
    logPrint(F("[E] An Error has occurred while loading config file! Loading defaults"));
    cfg.setDefault();
  }

  micDC = (float)cfg.spMin;

  adc_oneshot_unit_init_cfg_t init_config = {
    .unit_id = ADC_UNIT_1,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));
  adc_oneshot_chan_cfg_t channel_config = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_12,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, MICpin, &channel_config));

  ledcAttach(fanPWM, 25000, 8); //suport Arduino 3.x
  ledcWrite(fanPWM, cfg.fanDuty); //Arduino 3.x core
  //ledcSetup(0, 25000, 8); //For Arduino 2.x
  //ledcAttachPin(fanPWM, 0); //For Arduino 2.x
  //ledcWrite(0, cfg.fanDuty); //for Arduino 2.x

  if(visorType == "WS2812") {
    ledController[0] = &FastLED.addLeds<WS2812B, DATA_PIN_VISOR, GRB>(visorLeds, visorLedsNum);
  } else if (visorType == "MAX72XX") {
    mx.begin();
  }
  if(earPresent || blushPresent) { //RMT4 2 controller fix
    ledController[1] = &FastLED.addLeds<WS2812B, DATA_PIN_EARS, GRB>(c2Leds, earLedsNum+blushLedsNum);
  }
  /*if(earPresent) {
    ledController[1] = &FastLED.addLeds<WS2812B, DATA_PIN_EARS, GRB>(earLeds, earLedsNum);
  }
  if(blushPresent) {
    ledController[2] = &FastLED.addLeds<WS2812B, DATA_PIN_BLUSH, RGB>(blushLeds, blushLedsNum);
  }*/
  FastLED.setCorrection(TypicalPixelString);
  FastLED.setDither(0);

  startWiFiWeb();

  getFilesFunc();
  if(cfg.bleEna) { //you can disable BLE in config
    if(!startBLE()) {
      logPrint(F("[E] An Error has occurred while starting BLE!"));
    }
  }

  //I2C things
  Wire.setPins(I2C_SDA, I2C_SCL);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000); //100k = 113ms; 400k = 33ms; (800k = 20ms; 1mhz / 2mhz = 17ms = breaks apds)

  tiltInitDone = imuBegin(); //set up even with tilt off, so the calibration Read buttons work
  logPrint(tiltInitDone ? F("[I] BMI270 ready") : F("[E] BMI270 not found on I2C (0x68/0x69)"));

  if(cfg.oledEna) {
    if(!oled.init(oledAddr,cfg.bOled,INApresent)) {
      logPrint(F("[E] An Error has occurred while initializing SSD1306!"));
      cfg.oledEna = false;
    } else {
      oledInitDone = true;
      oled.speak(false);
      oled.writeSet(1);
    }
  }

  if(INApresent && oledInitDone) {
    if(!ina219.begin()) {
      logPrint(F("[E] An Error has occurred while initializing INA219 chip!"));
      INApresent = false;
    } else {
      ina219.setCalibration_16V_8A();
    }
  }

  if(boopMode == "APDS9960") { //set up even with boop off, so the boop reading can be checked while calibrating
    ToFInitDone = apdsStart();
    logPrint(ToFInitDone ? F("[I] APDS9960 ready") : F("[E] APDS9960 not found on I2C (0x39)"));
  } else if (boopMode == "VL53L1X" && cfg.boopEna) {
    Wire.beginTransmission(0x29);
    if(Wire.endTransmission() != 0) {
      cfg.boopEna = false;
      logPrint(F("[E] An Error has occurred while finding VL53L1X chip!"));
    } else {
      if(!vl53.begin()) {
        cfg.boopEna = false;
        logPrint(F("[E] An Error has occurred while initializing VL53L1X chip!"));
      } else {
        if (!vl53.startRanging()) {
          cfg.boopEna = false;
          logPrint(F("[E] An Error has occurred while starting ranging with VL53L1X chip!"));
        } else {
          ToFInitDone = true;
          vl53.setTimingBudget(50);
        }
      }
    }
  }
  
  while(millis()<2000) {yield();} //2s delay for the anim to load properly (idk why but it doesnt without this or with 1s)
  loadAnim("default.json","");

  logPrint("[I] Free heap: "+String(ESP.getFreeHeap()));
  logPrint("[I] Free PSRAM: "+String(ESP.getFreePsram()));
}

//--------------------------------//Loop vars
String oldanim, boopoldanim;
bool FdisplayVisor = false, FdisplayBlush = false, FdisplayEar = false, booping = false, wasTilt = false, boopRea = false, remoteSign = false, speaking = true;
float zAx,yAx,finalMicAvg,avgMicArr[10], micAttack = 0.35f, micRelease = 0.2f, env = 0.0f;
int boopRead, startIndex = 1, micVolume, currentMicAvg = 0, btnNum = 0, currFade = 1;
unsigned long lastMillsEars = 0, lastMillsVisor = 0, lastMillsTilt = 0, laskSpeakCheck = 0, lastMillsBoop = 0, lastFLED = 0, vaStatLast = 0, btnPressTime = 0, tiltChange = 0, check0button = 0, looptime = 0, fadeTime = 0, laskSpeakAnim = 0, lastBoopCheck = 0;
bool btnHeld = false, btnLongDone = false; //button: held down now / hold action already fired
unsigned long lastImuRetry = 0, lastApdsRetry = 0; //sensor not found yet: when it was last looked for

void dynamicSpeak(uint64_t *leds, bool isMouth[MATRIXESNUM], int volume) {
  int mouthIndexes[MATRIXESNUM];
  int mouthCount = 0;
  for (int i = 0; i < MATRIXESNUM; i++) { // collect true (mouth) indexes
    if (isMouth[i]) {
      mouthIndexes[mouthCount++] = i;
    }
  }
  int half = mouthCount / 2;
  for (int i = 0; i < half; i++) {
    int x = map(volume, 0, 100, 0, half * 8) - (i * 8);
    int leftIndex  = mouthIndexes[half - 1 - i];
    int rightIndex = mouthIndexes[half + i];
    if (x > 0) {
      x = constrain(x, 0, 8);
      leds[leftIndex]  = speakMatrix(leds[leftIndex],  x, true);
      leds[rightIndex] = speakMatrix(leds[rightIndex], x, false);
    }
  }
}

void setAllVisor(struct CRGB *ledArray, long ledColor, int visorFrame) {
  uint64_t tempLeds[MATRIXESNUM];
  memcpy(tempLeds, visorNow->frames[visorFrame].leds, sizeof(tempLeds));
  if(speaking) {
    dynamicSpeak(tempLeds, visorNow->isMouth, micVolume);
  }
  if(bobUp) { //mouth bob: shift the mouth panels up a row. Row i sits in byte i, so >>8 is up.
    for(int y = 0; y < numOfSegm; y++) {
      if(visorNow->isMouth[y]) tempLeds[y] >>= 8;
    }
  }
  for(int y = 0; y < numOfSegm; y++) {
    for (int i = 0; i < 8; i++) {
      byte row = (tempLeds[y] >> i * 8) & 0xFF; //---------remove byte from upper global
      for (int j = 0; j < 8; j++) {
        if(visorType == "WS2812") {
          long tempColor = ledColor; //use given color
          if(ledColor == 0) { //if not given a color
            if(visorNow->frames[visorFrame].ppColor[y][(i*8)+j] != 0) { //use ppColor if available
              tempColor = visorNow->frames[visorFrame].ppColor[y][(i*8)+j];
            } else if(visorNow->frames[visorFrame].fColor[y] != 0) { //if not, use fColor if available
              tempColor = visorNow->frames[visorFrame].fColor[y];
            } else { // else config color
              tempColor = cfg.visColor;
            }
          }
          if(oldMatrixFix) {
            ledArray[(y*64)+(i*8)+((i%2!=0)?j:7-j)] = (bitRead(row,j))?tempColor:CRGB::Black; //includes fix for bad rgbmatrix
          } else {
            ledArray[(y*64)+(i*8)+j] = (bitRead(row,j))?tempColor:CRGB::Black;
          }
        } else if (visorType == "MAX72XX") {
          mx.setPoint(i, j+(y*8), bitRead(row, j)); //MAXstuff
        }
      }
    }
  }
  FdisplayVisor = true;
}

void loop() {
  ElegantOTA.loop();
  if(brightnessDirty && visorType == "MAX72XX") {
    brightnessDirty = false;
    mx.control(MD_MAX72XX::INTENSITY, cfg.bVisor);
  }
  if(displayDirty && visorType == "MAX72XX") { //from /display: MAX7219 shutdown turns the LEDs off but keeps the picture
    displayDirty = false;
    mx.control(MD_MAX72XX::SHUTDOWN, displayOn ? MD_MAX72XX::OFF : MD_MAX72XX::ON);
    logPrint(displayOn ? F("[I] Displays on") : F("[I] Displays off"));
  }
  if(transPhase != 0 && visorType == "MAX72XX" && millis()-transLastStep >= transStepMs) { //face transition
    transLastStep = millis();
    if(transPhase == 1) {
      if(transLevel > 0) {
        mx.control(MD_MAX72XX::INTENSITY, --transLevel);
      } else { //dark: swap now. loadAnim() resets the frame index and asks for an instant redraw.
        if(transAnim != "") {
          if(sleeping) { sleeping = false; wakeAnim = ""; stillSince = millis(); } //picking a face wakes it, and stays on that face
          loadAnim(transAnim,"");
          wasTilt = false;
          booping = false;
          transAnim = "";
        }
        if(transPost != "") { loadAnim("POSTAnimLoad",transPost); transPost = ""; }
        transPhase = 2;
      }
    } else {
      if(transLevel < cfg.bVisor) {
        mx.control(MD_MAX72XX::INTENSITY, ++transLevel);
      } else {
        transPhase = 0;
        brightnessDirty = true; //hand the panels back to the normal brightness path
      }
    }
  }
  loopCount++; //loop() runs per second, shown by the homepage system monitor
  if(millis() - loopRateStart >= 1000) {
    loopsPerSec = loopCount;
    loopCount = 0;
    loopRateStart = millis();
  }
  //--------------------------------//EAR Leds render
  if(earPresent) {
    if(earsNow->type == 0) { //custom
      if(lastMillsEars+earsNow->frames[currentEarsFrame].timespan <= millis() || instantReload) {
        currentEarsFrame++;
        lastMillsEars = millis();
        if(currentEarsFrame == earsNow->numOfFrames) { currentEarsFrame = 0; } //loop back to first frame if last frame
        for(int y = 0; y < earLedsNum; y++) { earLeds[y] = earsNow->frames[currentEarsFrame].ledColor[y]; } //set ear leds
        FdisplayEar = true;
      }
    } else if (earsNow->type == 1) { //rainbow
      fill_rainbow(pixelBuffer, 4, millis()/cfg.rbSpeed, 255/cfg.rbWidth);
      if(earLedsNum == 74) {
        for(int x = 0;x<earLedsNum;x++) {
          if(x<16) {
            earLeds[x] = pixelBuffer[0];
            earLeds[x+37] = pixelBuffer[0];
          } else if(x<28) {
            earLeds[x] = pixelBuffer[1];
            earLeds[x+37] = pixelBuffer[1];
          } else if(x<36) {
            earLeds[x] = pixelBuffer[2];
            earLeds[x+37] = pixelBuffer[2];
          } else if(x==36) {
            earLeds[x] = pixelBuffer[3];
            earLeds[x+37] = pixelBuffer[3];
          }
        }
      } else {
        for(int x = 0;x<earLedsNum;x++) {
          earLeds[x] = pixelBuffer[0];
        }
      }
      FdisplayEar = true;
    } else if (earsNow->type == 2) { //white_noise
      memset(noiseData, 0, earLedsNum);
      fill_raw_noise8(noiseData, earLedsNum, 2, 0, 50, millis()/4);
      for(int x = 0;x<earLedsNum;x++) {
        earLeds[x] = ColorFromPalette(blackWhite, noiseData[x]);
      }
      FdisplayEar = true;
    } else if (earsNow->type == 3 && earLedsNum == 74) { //corner_sabers - only 74 led mode
      if(lastFLED+cfg.rbSpeed < millis()) {
        lastFLED = millis();
        startIndex++;
        int tempIndex = startIndex;
        for(int x = 0;x<18;x++) {
          pixelBuffer[x] = ColorFromPalette(RainbowStripeColors_p, tempIndex, 255, NOBLEND);
          tempIndex+=3;
        }
        for(int x = 0;x<9;x++) {
          for(int y = 0;y<lookupDiag1[x].size();y++) {
            earLeds[lookupDiag2[x][y]-1] = pixelBuffer[x];
            earLeds[lookupDiag1[x][y]+36] = pixelBuffer[x];
          }
        }
        FdisplayEar = true;
      }
    } else if (earsNow->type == 4) { //custom_glow
      fill_rainbow(pixelBuffer, 4, millis()/cfg.rbSpeed, 255/cfg.rbWidth);
      for(int y = 0; y < earLedsNum; y++) {
        if(earsNow->frames[0].ledColor[y] == 0) {
          earLeds[y] = 0x000000;
        } else {
          earLeds[y] = pixelBuffer[0];
        }
      }
      FdisplayEar = true;
    } else if (earsNow->type == 5) {} //none
  }

  //--------------------------------//VISOR+BLUSH Leds render
  if(visorNow->type == 0 || (visorNow->type == 1 && visorType == "MAX72XX")) { //custom
    if(lastMillsVisor+visorNow->frames[currentVisorFrame].timespan <= millis() || instantReload) {
      currentVisorFrame++;
      lastMillsVisor = millis();
      if(currentVisorFrame == visorNow->numOfFrames) {
        currentVisorFrame = 0;
        if(visorThen != "") { //one pass done: queue the face this animation hands over to
          xSemaphoreTake(animQueueMutex, portMAX_DELAY);
          animToLoad = visorThen;
          xSemaphoreGive(animQueueMutex);
          visorThen = "";
        }
      }
      setAllVisor(visorLedsNEW,0,currentVisorFrame); //set visor leds
      if(blushPresent) {
        for(int x = 0; x<blushLedsNum; x++) { blushLeds[x] = visorNow->frames[currentVisorFrame].ledsBlush[x]; } //set blush leds
        FdisplayBlush = true;
      }
      instantReload = false;
    }
  } else if (visorNow->type == 1 && visorType == "WS2812") { //all_rainbow
    if(lastMillsVisor+visorNow->frames[currentVisorFrame].timespan <= millis() || instantReload) {
      currentVisorFrame++;
      lastMillsVisor = millis();
      if(currentVisorFrame == visorNow->numOfFrames) { currentVisorFrame = 0; }
      instantReload = false;
    }
    fill_rainbow(visorPixelBuffer, 1, millis()/cfg.rbSpeed, 128/cfg.rbWidth);
    setAllVisor(visorLeds,((long)visorPixelBuffer[0].r << 16) | ((long)visorPixelBuffer[0].g << 8 ) | (long)visorPixelBuffer[0].b,currentVisorFrame);
    if(blushPresent) {
      for(int x = 0; x<blushLedsNum; x++) { blushLeds[x] = visorNow->frames[currentVisorFrame].ledsBlush[x]; } //set blush leds
      FdisplayBlush = true;
    }
  }

  //--------------------------------//MOUTH BOB
  //The frame often doesn't change for seconds at a time, so the redraw has to be asked for
  //here rather than waiting for the next frame.
  if(visorNow && (visorNow->type == 0 || (visorNow->type == 1 && visorType == "MAX72XX"))) {
    bool wantUp = cfg.bobEna && (millis() % bobPeriod) < bobLift;
    if(wantUp != bobUp) {
      bobUp = wantUp;
      setAllVisor(visorLedsNEW,0,currentVisorFrame);
    }
  }

  //--------------------------------//TILT
  //--------------------------------//I2C sensors: read every 100 ms on the loop task; the web pages use these cached values
  if(lastMillsTilt+100<=millis()) {
    lastMillsTilt = millis();
    if(boopMode == "APDS9960" && ToFInitDone) apdsprox = apds.readProximity();
    if(tiltInitDone) {
      readAccel();
      if(cfg.sleepEna && cfg.sleepMins > 0) { //left alone for long enough? sleep. Moved? wake up.
        //One threshold can't do both jobs: staying asleep wants a generous one, waking up wants a
        //touchy one. So while it's asleep, half the setting is enough and 2 readings (200 ms) do it.
        float tol = sleeping ? cfg.sleepTol * 0.5f : cfg.sleepTol;
        uint8_t need = sleeping ? 2 : 3;
        if(fabs(accelX-stillX) > tol || fabs(accelY-stillY) > tol || fabs(accelZ-stillZ) > tol) {
          if(++moveSamples >= need) { //a single noisy reading isn't movement; a few in a row is
            stillX = accelX; stillY = accelY; stillZ = accelZ;
            stillSince = millis();
            moveSamples = 0;
            if(sleeping) setSleeping(false);
          }
        } else {
          moveSamples = 0;
          if(!sleeping && millis()-stillSince > (unsigned long)cfg.sleepMins * 60000UL) {
            setSleeping(true);
          }
        }
      } else {
        stillSince = millis();
      }
      if(cfg.tiltEna) {
        if(isApproxEqual(accelX,accelY,accelZ,cfg.upX,cfg.upY,cfg.upZ,cfg.tiltTol) && !wasTilt) {
          logPrint(F("[I] Tilt: UP!"));
          wasTilt = true;
          oldanim = currentAnim;
          tiltChange = millis();
          loadAnim(cfg.aUp,"");
        } else if (isApproxEqual(accelX,accelY,accelZ,cfg.tiltX,cfg.tiltY,cfg.tiltZ,cfg.tiltTol) && !wasTilt) {
          logPrint(F("[I] Tilt: Side!"));
          wasTilt = true;
          oldanim = currentAnim;
          tiltChange = millis();
          loadAnim(cfg.aTilt,"");
        } else if ((tiltChange+revertTilt<millis() || isApproxEqual(accelX,accelY,accelZ,cfg.neutralX,cfg.neutralY,cfg.neutralZ,cfg.tiltTol)) && wasTilt) {
          logPrint(F("[I] Tilt: Neutral!"));
          wasTilt = false;
          loadAnim(oldanim,"");
        }
      }
    } else if(cfg.tiltEna && lastImuRetry+5000<=millis()) { //tilt on but no BMI270 yet: look again every 5 s
      lastImuRetry = millis();
      tiltInitDone = imuBegin();
      logPrint(tiltInitDone ? F("[I] BMI270 found") : F("[E] BMI270 not found on I2C (0x68/0x69)"));
    }
  }

  //looptime = micros();
  //--------------------------------//SPEECH Detection
  if(cfg.speechEna) { //1.045uS
    int nvol = 0, micline = 0, rawInput = 0;
    for (int i = 0; i<32; i++){
      adc_oneshot_read(adc_handle, MICpin, &rawInput);
      micline = abs(rawInput - 512);
      nvol = max(micline, nvol);
    }
    if(currentMicAvg == 9) {
      currentMicAvg = 0;
    } else {
      avgMicArr[currentMicAvg++] = nvol;
    }

    if(laskSpeakCheck+10<=millis()) {
      finalMicAvg = 0;
      for (int i = 0; i<10; i++){
        finalMicAvg+=avgMicArr[i];
      }
      finalMicAvg = finalMicAvg/10;
      //Serial.print(String(finalMicAvg));
      //Serial.print(",");
      float centered = finalMicAvg - micDC; //DC removal
      float mag = fabsf(centered);
      if (mag > env) { // Envelope follower
        env += (mag - env) * micAttack;
      } else {
        env += (mag - env) * micRelease;
      }
      if (env < 25.0f) { // Noise gate
        env = 0.0f;
      }
      if (env < 25.0f) { // Update DC only when quiet
          micDC = micDC * (1.0f - 0.0005f) + finalMicAvg * 0.0005f;
      }
      micVolume = (int)(env * 100.0f / (float)cfg.spMax + 0.5f); // Normalize 0-100
      micVolume = constrain(micVolume, 0, 100);
      //Serial.println(String(micVolume));

      if(micVolume > cfg.spTrig) {
        if(!speaking) {
          speaking = true;
          if(cfg.oledEna && oledInitDone) {
            oled.speak(true);
          }
          logPrint(F("[I] Speak"));
        }
      } else {
        if(speaking) {
          logPrint(F("[I] unSpeak"));
          speaking = false;
          if(cfg.oledEna && oledInitDone) {
            oled.speak(false);
          }
          if(visorNow->type == 0 || (visorNow->type == 1 && visorType == "MAX72XX")) {
            setAllVisor(visorLedsNEW,0,currentVisorFrame);
          }
        }
      }

      laskSpeakCheck = millis();
    }

    if(laskSpeakAnim+60<=millis() && speaking) {
      if(visorNow->type == 0 || (visorNow->type == 1 && visorType == "MAX72XX")) { //custom
        setAllVisor(visorLedsNEW,0,currentVisorFrame);
        laskSpeakAnim = millis();
      }
    }
  }
  //Serial.println(">SPK1:"+String(micros()-looptime));

  //--------------------------------//Single button: short press = next face, hold 1.5s = displays off/on
  hwBtn.loop();
  if(hwBtn.isPressed()) { //detect press
    btnPressTime = millis();
    btnHeld = true;
    btnLongDone = false;
  }
  if(btnHeld && !btnLongDone && millis()-btnPressTime >= 1500) { //held 1.5s: toggle the displays right away, like the web power button
    btnLongDone = true;
    displayOn = !displayOn;
    displayDirty = true;
  }
  if(hwBtn.isReleased()) {
    btnHeld = false;
    if(!btnLongDone && totalAnims > 0) { //short press: the face after the one showing, wherever it was picked
      int next = btnNum + 1;
      for(int i = 0; i < totalAnims; i++) {
        if(availAnims[i] == currentAnim) { next = i + 1; break; }
      }
      btnNum = next % totalAnims;
      xSemaphoreTake(animQueueMutex, portMAX_DELAY);
      animToLoad = availAnims[btnNum];
      xSemaphoreGive(animQueueMutex);
      logPrint("Changing to "+availAnims[btnNum]+", amount of anims: "+String(totalAnims));
    }
  }

  //looptime = micros();
  //--------------------------------//BOOP Detection; 14-800uS
  if(lastBoopCheck+100<=millis() && cfg.boopEna) {
    if(boopMode == "KY-032") {
      digitalWrite(T_en, HIGH);
      delayMicroseconds(210);
      if(booping == false && !digitalRead(T_in)) {
        delayMicroseconds(395);
        if(!digitalRead(T_in)) {
          logPrint(F("[I] IR BOOP"));
          booping = true;
          boopoldanim = currentAnim;
          loadAnim(cfg.aBoop,"");
          lastMillsBoop = millis();
        }
        digitalWrite(T_en, LOW);
      } else if(booping == true && lastMillsBoop+1000<millis() && digitalRead(T_in)) {
        digitalWrite(T_en, LOW);
        logPrint(F("[I] IR unBOOP"));
        booping = false;
        if(!wasTilt) {
          loadAnim(boopoldanim,"");
        }
      }
    } else if (boopMode == "Capac") {
      if(booping == false && digitalRead(T_in)) {
        delayMicroseconds(395);
        if(digitalRead(T_in)) {
          logPrint(F("[I] Touch BOOP"));
          booping = true;
          boopoldanim = currentAnim;
          loadAnim(cfg.aBoop,"");
          lastMillsBoop = millis();
        }
      } else if(booping == true && lastMillsBoop+1000<millis() && !digitalRead(T_in)) {
        logPrint(F("[I] Touch unBOOP"));
        booping = false;
        if(!wasTilt) {
          loadAnim(boopoldanim,"");
        }
      }
    } else if (boopMode == "APDS9960") {
      if(ToFInitDone) {
        //apdsprox is read every 100 ms in the I2C sensor section
        if(booping == false && (255 - apdsprox) < cfg.boopThresh) {
          logPrint(F("[I] ToF BOOP"));
          booping = true;
          boopoldanim = currentAnim;
          loadAnim(cfg.aBoop,"");
          lastMillsBoop = millis();
        } else if(booping == true && lastMillsBoop+1000<millis() && (255 - apdsprox) > cfg.boopThresh) {
          logPrint(F("[I] ToF unBOOP"));
          booping = false;
          if(!wasTilt) {
            loadAnim(boopoldanim,"");
          }
        }
      } else if(lastApdsRetry+5000<=millis()) { //boop on but no APDS9960 yet: look again every 5 s, e.g. after plugging it in
        lastApdsRetry = millis();
        ToFInitDone = apdsStart();
        logPrint(ToFInitDone ? F("[I] APDS9960 found") : F("[E] APDS9960 not found on I2C (0x39)"));
      }
    } else if (boopMode == "VL53L1X") {
      if(ToFInitDone) {
        int16_t distance = -1;
        if (vl53.dataReady()) {
          distance = vl53.distance();
        }
        if(booping == false && distance < cfg.boopThresh && distance != -1) {
          logPrint(F("[I] ToF BOOP"));
          booping = true;
          boopoldanim = currentAnim;
          loadAnim(cfg.aBoop,"");
          lastMillsBoop = millis();
        } else if(booping == true && lastMillsBoop+1000<millis() && distance > cfg.boopThresh && distance != -1) {
          logPrint(F("[I] ToF unBOOP"));
          booping = false;
          if(!wasTilt) {
            loadAnim(boopoldanim,"");
          }
        }
      } else {
        Wire.beginTransmission(0x29);
        if(Wire.endTransmission() != 0) {
          cfg.boopEna = false;
          logPrint(F("[E] An Error has occurred while finding VL53L1X chip!"));
        } else {
          if(!vl53.begin()){
            cfg.boopEna = false;
            logPrint(F("[E] An Error has occurred while initializing VL53L1X chip!"));
          } else {
            if (!vl53.startRanging()) {
              cfg.boopEna = false;
              logPrint(F("[E] An Error has occurred while starting ranging with VL53L1X chip!"));
            } else {
              ToFInitDone = true;
              vl53.setTimingBudget(50);
            }
          }
        }
      }
    }
    lastBoopCheck=millis();
  }
  //Serial.println(">BP:"+String(micros()-looptime));

  //--------------------------------//OLED routine, ~~10ms qwq~~, 1-5ms.. eh better
  if(cfg.oledEna && oledInitDone && vaStatLast+1000<millis()) {
    //looptime = micros();
    if(INApresent) {
      oled.writeINA(ina219.getBusVoltage_V(),ina219.getCurrent_mA());
    }
    if(cfg.bleEna) {
      if(pServer->getConnectedCount() == 0) {
        remoteSign = !remoteSign;
        oled.remote(remoteSign);
      } else if (pServer->getConnectedCount() > 0 && remoteSign == false) {
        oled.remote(true);
        remoteSign = true;
      }
    } else if (!cfg.bleEna && remoteSign) {
      oled.remote(false);
      remoteSign = false;
    }
    vaStatLast = millis();
    //Serial.println(">OLED:"+String(micros()-looptime));
  }
  if (!oledInitDone && cfg.oledEna) {
    if(!oled.init(oledAddr,cfg.bOled,INApresent)) {
      logPrint(F("[E] An Error has occurred while initializing SSD1306."));
      cfg.oledEna = false;
    } else {
      oledInitDone = true;
    }
  }

  if((FdisplayEar || FdisplayBlush || FdisplayVisor) ) {
    if(visorType == "WS2812") {
      if(FdisplayVisor) {
        if(visorNow->type == 0) {
          if(FADESTEPS == 0) { //just fading
            memcpy(visorLeds, visorLedsNEW, sizeof(CRGB) * visorLedsNum);
            ledController[0]->showLeds(cfg.bVisor);
            FdisplayVisor = false;
          } else if(fadeTime + (visorLedsNum*0.03) < millis()) {
            for (uint16_t i = 0; i < visorLedsNum; i++) {
              visorLeds[i] = blend(visorLeds[i], visorLedsNEW[i], (currFade * 255) / FADESTEPS);
            }
            ledController[0]->showLeds(cfg.bVisor); //visor
            currFade++;
            if(currFade > FADESTEPS) {
              memcpy(visorLeds, visorLedsNEW, sizeof(CRGB) * visorLedsNum);
              FdisplayVisor = false;
              currFade = 1;
            }
          }
          /*for (uint8_t step = 1; step <= FADESTEPS; step++) {
            for (uint16_t i = 0; i < visorLedsNum; i++) {
              visorLeds[i] = blend(visorLeds[i], visorLedsNEW[i], (step * 255) / FADESTEPS);
            }
            ledController[0]->showLeds(cfg.bVisor); //visor
            delay(21); 
          }
          memcpy(visorLeds, visorLedsNEW, sizeof(CRGB) * visorLedsNum);
          FdisplayVisor = false;*/
        } else {
          ledController[0]->showLeds(cfg.bVisor);
          FdisplayVisor = false;
        }
      }
    } else if (visorType == "MAX72XX") {
      if(FdisplayVisor) {
        if(cfg.bVisor > 15) { cfg.bVisor = 15;}
        if(transPhase == 0) mx.control(MD_MAX72XX::INTENSITY, cfg.bVisor); //a running transition owns the intensity
        mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
        mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
        FdisplayVisor = false;
      }
    }
    if(blushPresent && useRGBblush) {
      for(int i = 0; i < blushLedsNum; i++) {
        uint32_t temp = blushLeds[i].r;
        blushLeds[i].r = blushLeds[i].g;
        blushLeds[i].g = temp;
      }
    }
    if(earPresent && !blushPresent && FdisplayEar) { //RMT4 2 controllers fix
      for(int i = 0; i < earLedsNum; i++) {
        c2Leds[i] = earLeds[i];
      }
      ledController[1]->showLeds(cfg.bEar);
      FdisplayEar = false;
    } else if (blushPresent && !earPresent && FdisplayBlush) {
      for(int i = 0; i < blushLedsNum; i++) {
        c2Leds[i] = blushLeds[i];
      }
      ledController[1]->showLeds(cfg.bEar);
      FdisplayBlush = false;
    } else if (blushPresent && earPresent && (FdisplayBlush || FdisplayEar)) { //ear-blush-ear
      for(int i = 0; i < (earLedsNum/2); i++) {
        c2Leds[i] = earLeds[i];
      }
      for(int i = (earLedsNum/2); i < (earLedsNum/2)+blushLedsNum; i++) {
        c2Leds[i] = blushLeds[i-(earLedsNum/2)];
      }
      for(int i = (earLedsNum/2)+blushLedsNum; i < earLedsNum+blushLedsNum; i++) {
        c2Leds[i] = earLeds[i-blushLedsNum];
      }
      ledController[1]->showLeds(cfg.bEar);
      FdisplayEar = false;
      FdisplayBlush = false;
    }
    /*if(FdisplayEar && earPresent) {
      ledController[1]->showLeds(cfg.bEar); //ears
      FdisplayEar = false;
    }
    if(FdisplayBlush && blushPresent) {
      ledController[2]->showLeds(cfg.bBlush); //blush
      FdisplayBlush = false;
    }*/
  }

  //press boot button for 10sec to reset
  if(check0button+10000 < millis() && check0button+10500 > millis() && digitalRead(0) == LOW) {
    logPrint(F("[I] Resetting to defaults"));
    cfg.setDefault();
    delay(20);
    ESP.restart();
  } else if (digitalRead(0) == HIGH && check0button+10000 > millis() && check0button < millis()) {
    check0button = 0;
  } else if (digitalRead(0) == LOW && check0button+10000 < millis() && check0button < millis()) {
    check0button = millis();
  }

  if(sleepRequest != 0) { //from /sleep
    setSleeping(sleepRequest > 0);
    sleepRequest = 0;
  }

  String queuedAnim, queuedPost;
  xSemaphoreTake(animQueueMutex, portMAX_DELAY);
  queuedAnim = animToLoad; animToLoad = "";
  queuedPost = postAnimToLoad; postAnimToLoad = "";
  xSemaphoreGive(animQueueMutex);
  if(cfg.transEna && visorType == "MAX72XX" && transPhase == 0 && (queuedAnim != "" || queuedPost != "")) {
    transAnim = queuedAnim; transPost = queuedPost; //hold the swap until the panels are dark
    uint8_t levels = cfg.bVisor > 0 ? cfg.bVisor : 1; //the dip time is shared out over the brightness steps there are
    transStepMs = cfg.transMs / levels;
    if(transStepMs < 1) transStepMs = 1;
    transPhase = 1; transLevel = cfg.bVisor; transLastStep = millis();
    queuedAnim = ""; queuedPost = "";
  }
  if(queuedAnim != "") {
    if(sleeping) { sleeping = false; wakeAnim = ""; stillSince = millis(); } //picking a face wakes it, and stays on that face
    loadAnim(queuedAnim,"");
    wasTilt = false;
    booping = false;
  }
  if(queuedPost != "") {
    loadAnim("POSTAnimLoad",queuedPost);
  }
}
