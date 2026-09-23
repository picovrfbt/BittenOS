//Make sure you have everything connected by the schematic in the repository and set these defines correctly!

#define micMode "I2S" //"I2S" for a digital mic (SPH0645, INMP441), "ANALOG" for one on the ADC (MAX4466, MAX9814)
//Digital mic on I2S. An analog mic shares the 3V3 rail with fourteen MAX7219s pulsing current, and
//amplifies whatever noise that puts on it; a digital mic converts sound inside its own package and
//sends numbers, so none of that reaches the signal. Pins are in the order the GY-SPH0645 board
//prints them (BCLK, DOUT, LRCL), so the wires run straight across without crossing.
#define MIC_BCLK 4    //board pad BCLK
#define MIC_DOUT 5    //board pad DOUT, data into the ESP
#define MIC_LRCL 6    //board pad LRCL (word select)
#define MIC_I2S_RATE 16000
//The SPH0645 puts 18 bits of real data at the top of a 32-bit slot. This shift only sets the scale
//the numbers come out at; Noise floor and Range are measured, so they absorb whatever it is. Raise
//it if swings read in the tens of thousands, lower it if they read in single digits.
#define MIC_I2S_SHIFT 20
#define MICpin ADC_CHANNEL_3 //Analog microphone, GPIO 4 (ADC1, the set WiFi doesn't use)
#define T_in 2 //Output from Touch Sensor (for KY-032, Capac)
#define T_en 42 //Enable pin to Touch Sensor (for KY-032)
//Was GPIO 5, which the digital mic uses for DOUT - the two could never be fitted at once. Moved to
//8 (J1 pin 12), which nothing else claims.
#define DATA_PIN_EARS 8  //Ears(Blush) (from outer to inner, POV-right cheek, (if blush: from top to bottom, right cheek nearest to ear first))
#define DATA_PIN_VISOR 7 //Face (right cheek, left segment of eye first)
#define I2C_SDA 9 //SDA for Gyro, OLED, INA219, ToF, J1 pin 15
#define I2C_SCL 10 //SCL for Gyro, OLED, INA219, ToF, J1 pin 16
//Panel pads read CLK, CS, DIN, VCC, GND down the board, and so do J1 pins 18 to 22 (GPIO 12, 13,
//14, then 5V and GND). Wiring them in that order makes the loom one straight five-way ribbon with
//nothing crossing and no gap before power. GPIO 13 came free when the fan went.
#define MAX_CLK 12 //Clock for MAX72xx matrixes, J1 pin 18
#define MAX_CS 13 //ChipSelect for MAX72xx matrixes, J1 pin 19
#define MAX_MOSI 14 //Data for MAX72xx matrixes, J1 pin 20
#define animBtn 21 //Momentary button: short press = next face, hold 1.5s = displays off/on
#define animBtnGnd 47 //Driven LOW as the button's ground, so both button wires sit on neighbouring pins (47 is next to 21)

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

#define boopMode "APDS9930" //"KY-032", "Capac" (boop when HIGH), "APDS9960", "APDS9930", "VL53L1X", or empty for none
//The APDS-9930 and APDS-9960 are different chips that both answer on 0x39. The Adafruit
//APDS9960 library checks the device ID and refuses a 9930, so the 9930 has its own small
//driver below - it only needs proximity. A 9930 has no gesture engine; it does have ambient
//light, so auto-brightness would still be possible on one.

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
#include "screen.h"
#include "screen_font.h"   //the 5x7 font, shared by the status screen and quick phrases
#include "driver/i2s_std.h"
SSDOLED oled;

#include "SparkFun_BMI270_Arduino_Library.h"
#include <Wire.h>
BMI270 imu; //tilt sensor, shares the I2C bus (SDA 8, SCL 9) with the boop sensor
float accelX = 0, accelY = 0, accelZ = 0; //latest accelerometer reading in g, updated by readAccel() in loop()

bool imuBegin() { //the I2C address depends on the board's SDO pin
  if(imu.beginI2C(BMI2_I2C_PRIM_ADDR) == BMI2_OK) return true; //0x68
  return imu.beginI2C(BMI2_I2C_SEC_ADDR) == BMI2_OK;           //0x69
}

bool readAccel() { //false if the sensor didn't answer; loop() counts those and gives up on it
  if(imu.getSensorData() != BMI2_OK) return false;
  accelX = imu.data.accelX;
  accelY = imu.data.accelY;
  accelZ = imu.data.accelZ;
  return true;
}

#include <Adafruit_INA219.h> //edited library in this sketch (replace 0.1R with 0.03R resistor on the board)
Adafruit_INA219 ina219;

#include "Adafruit_APDS9960.h"
Adafruit_APDS9960 apds;

int apdsprox = 0; //latest proximity (0-255, higher = closer), read by loop() every 100 ms
uint8_t imuFails = 0, apdsFails = 0;   //reads in a row that got nothing back
volatile bool boopCalRequest = false;  //set by /boopcal, done on the loop task
int boopRawBuf[5] = {0, 0, 0, 0, 0};   //the last five raw readings, for a median
uint8_t boopRawIdx = 0;
float boopFloor = -1;                  //the tracked resting level; -1 until the first reading
uint8_t boopOver = 0, boopUnder = 0;   //checks in a row above the trigger / below the release
//How often the proximity is read and the boop logic runs. The APDS-9930 converts a proximity
//reading in 2.72 ms (PTIME 0xFF), so the old 100 ms was all firmware: median of five plus three
//confirmations at that rate meant a boop could take most of a second to register. At 30 ms the same
//counts - same rejection of stray readings - come to about a fifth of that.
#define BOOP_POLL_MS 30
//How many readings in a row end a boop. Starting one is cfg.boopConfirm, set on the page, and
//five is the default for a reason: sampling faster means more chances per hour for stray readings
//to line up even though the rejection per sample is unchanged. Simulated over six hours, 30 ms
//with three gave 1.03 false boops an hour against 0.37 at the old 100 ms, while five gave none and
//still registers a tap in about 210 ms rather than 500. Lower it for a quicker boop at the cost of
//the odd false one; raise it if the mask boops itself.
#define BOOP_RELEASE 5
unsigned long lastProxAt = 0, lastFloorAt = 0;

inline bool boopIs(const char *m) { return strcmp(boopMode, m) == 0; }
inline bool boopIsApds() { return boopIs("APDS9960") || boopIs("APDS9930"); }

//defined further down, but the APDS-9930 driver below needs them first
void logPrint(const char *str);
void logPrint(const String &str);
bool apdsStart();
extern bool booping;
extern bool speaking;
extern bool FdisplayVisor;     //set when panels have been written and need flushing
//Repaints the frame that is already showing. The warm-up needs this to paint over its own tearing:
//instantReload would do it, but the renderer reads that as "advance to the next frame", which ate
//the loading bar a frame at a time.
void setAllVisor(struct CRGB *ledArray, long ledColor, int visorFrame);
extern int micVolume;          //0-100, how loud it is against the Speech range
extern float finalMicAvg;      //the raw smoothed mic reading, for the level meter
extern String blinkCachedAnim; //which face the blink rule was last worked out for
extern int btnNum;             //which face the button last stepped to

//--------------------------------//APDS-9930: proximity only, straight at the registers
#define apds9930Addr 0x39
bool apds9930Write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(apds9930Addr);
  Wire.write(0x80 | reg);                       //command byte, repeated-byte protocol
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

int apds9930Read8(uint8_t reg) {
  Wire.beginTransmission(apds9930Addr);
  Wire.write(0x80 | reg);
  if(Wire.endTransmission() != 0) return -1;
  if(Wire.requestFrom(apds9930Addr, 1) != 1) return -1;
  return Wire.read();
}

int apds9930Read16(uint8_t reg) { //-1 if it didn't answer
  Wire.beginTransmission(apds9930Addr);
  Wire.write(0xA0 | reg);                       //auto-increment, low byte then high
  if(Wire.endTransmission() != 0) return -1;
  if(Wire.requestFrom(apds9930Addr, 2) != 2) return -1;
  int lo = Wire.read(), hi = Wire.read();
  return lo | (hi << 8);
}

int apds9930Prox() { //0-255, higher = closer; -1 if it didn't answer
  int v = apds9930Read16(0x18);                 //PDATA, 10-bit 0-1023
  if(v < 0) return -1;
  return v >= 1023 ? 255 : (v >> 2);
}

//Ambient light, both channels: CH0 sees visible and infrared, CH1 mostly infrared. No lux is
//worked out from them, because the usual formula assumes a normal mix of light and a tinted
//visor passes infrared far better than visible, which throws it right off. The two raw numbers
//and the ratio between them are what actually says what the visor is doing.
int alsCh0 = -1, alsCh1 = -1;

//Shadow check for boops. A paw close enough to boop blocks the light reaching the sensor, while
//a stray proximity reading doesn't change the light at all, so when there is light to block a
//boop must also darken it. "Normal" light follows brighter quickly (lights on, paw gone) and
//darker slowly (walking into a dim room), so a paw arriving over half a second barely moves it.
//Too dark to cast a shadow - night, a dark room - and boops go on proximity alone.
#define LIGHT_MIN 20                       //normal CH0 below this: too dark for the shadow check
float lightNormal = -1;                    //-1 until the first reading
int shadeNow = 0;                          //percent darker than normal
uint8_t shadeBuf[10] = {0};                //the last second of shade, as the shadow leads the reading a little
uint8_t shadeIdx = 0;
uint16_t boopsIgnored = 0;                 //proximity said boop but nothing shaded the sensor
bool boopIgnoreLogged = false;

int shadeRecent() {                        //darkest shade in the last second
  int m = 0;
  for(int i = 0; i < 10; i++) if(shadeBuf[i] > m) m = shadeBuf[i];
  return m;
}

bool apds9930Start() {
  Wire.beginTransmission(apds9930Addr);
  if(Wire.endTransmission() != 0) return false; //nothing answering at 0x39 at all
  int id = apds9930Read8(0x12);                 //0x39 on a genuine one. Logged, not enforced:
  logPrint("[I] 0x39 answered, ID 0x" + String(id, HEX));  //a strict ID check is what rejects clones
  apds9930Write(0x00, 0x00);                    //ENABLE: off while it's configured
  apds9930Write(0x01, 0xDB);                    //ATIME: about 100 ms of light gathering
  apds9930Write(0x02, 0xFF);                    //PTIME
  apds9930Write(0x0E, 0x08);                    //PPULSE: 8 pulses
  apds9930Write(0x0F, 0x22);                    //CONTROL: 100mA, CH1 diode, 1x prox, 8x light
  apds9930Write(0x00, 0x07);                    //ENABLE: power on + proximity + ambient light
  delay(12);
  return apds9930Prox() >= 0;
}

bool boopSensorStart() { return boopIs("APDS9930") ? apds9930Start() : apdsStart(); }

//A sensor behind a visor always sees its own IR bouncing off the inside of it, so the resting
//reading is never zero, and it wanders as the mask warms up or the visor shifts. So the resting
//level is tracked (see where the readings come in, in loop()) and a boop is how far above it the
//reading is, rather than the raw number against a fixed threshold.
int boopSignal() {
  if(boopFloor < 0) return 0;
  int v = apdsprox - (int)(boopFloor + 0.5f);
  return v < 0 ? 0 : v;
}

int boopReadProx() { //whichever chip is fitted, as 0-255 with higher meaning closer
  if(boopIs("APDS9930")) return apds9930Prox();   //-1 if it didn't answer, handled by the caller
  return apds.readProximity();
}

String i2cFound = "";   //what answered on the I2C bus, refreshed while the boop sensor is missing

//The bus is swept a few addresses per loop pass rather than all at once - a full sweep in one
//go stalls the animation for tens of milliseconds - and the sweep repeats every 10 s. Anything
//that stops answering is marked missing, which sets the retries below going, so a sensor can be
//knocked loose and plugged back in without restarting the mask.
uint8_t i2cProbe = 1;
bool i2cScanning = true;
String i2cBuild = "";
unsigned long lastI2CStep = 0, lastI2CDone = 0;
bool i2cFresh = false;    //a sweep just finished, so its result hasn't been acted on yet

void i2cScanStep() { //loop task only - the web page reads the cached string, it never touches the bus
  if(!i2cScanning) {
    if(millis() - lastI2CDone >= 10000) { i2cScanning = true; i2cProbe = 1; i2cBuild = ""; }
    return;
  }
  if(millis() - lastI2CStep < 5) return;
  lastI2CStep = millis();
  for(uint8_t n = 0; n < 4 && i2cProbe <= 126; n++, i2cProbe++) {
    Wire.beginTransmission(i2cProbe);
    if(Wire.endTransmission() == 0) {
      if(i2cBuild != "") i2cBuild += " ";
      i2cBuild += "0x" + String(i2cProbe, HEX);
    }
  }
  if(i2cProbe > 126) {
    i2cFound = (i2cBuild == "") ? "none" : i2cBuild;
    i2cScanning = false;
    lastI2CDone = millis();
    i2cFresh = true;              //loop() compares it against what should be there
  }
}

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
#include <ESPmDNS.h>
#include "ESPAsyncWebServer.h"
#include <Update.h>
#include <MD5Builder.h>
#include <Preferences.h>
#include "esp_partition.h"
#include "esp_ota_ops.h"

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
//Boop counter: the running total pops up over the nose after a boop. Panel numbers are this
//mask's layout (data/visType.txt): t7 is the left nose, t6 the right. Kept in the firmware
//rather than the animations so it works over whatever face is showing.
#define noseLeftPanel  7
#define noseRightPanel 6
unsigned long boopShownAt = 0;
int lastBoopPhase = -2;
bool boopCountDirty = false;
bool milestoneBoop = false;     //this boop hit the milestone, so it played the special face
unsigned long boopCountSaved = 0;
volatile int8_t sleepRequest = 0; //from /sleep: 1 = sleep now, -1 = wake, 0 = nothing
volatile int8_t phraseRequest = -1; //from /phrase: the slot to show, -1 = nothing
//Clock. The ESP has no battery-backed clock and no internet, so it cannot know the time by itself:
//whichever phone opens the page tells it, and it counts on from there. That means the time is right
//until the next restart, and unknown again after one until a phone visits.
volatile uint32_t clockSetTo = 0;        //minutes past midnight the phone last reported
volatile unsigned long clockSetAt = 0;   //millis() when it said so
volatile bool clockIs24 = false;         //the phone's own preference, so it matches what you are used to
bool clockKnown() { return clockSetAt != 0; }

//Minutes past midnight now, counting on from whenever the phone last told us
int clockMinutesNow() {
  if(!clockKnown()) return -1;
  unsigned long gone = (millis() - clockSetAt) / 60000UL;
  return (int)((clockSetTo + gone) % 1440);
}
//Sleep easing: dim over the last few seconds before dropping off, come back gently rather than
//snapping to full, and after a long enough sleep shut the panels off entirely if that is switched on.
unsigned long sleepDeepAt = 0;
bool sleepDeepDone = false;
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
//Auto-brightness (loop task only): sets the panel brightness from the APDS-9930's light sensor.
//The panels shine into the sensor, so a reading with them on partly measures themselves. Three ways
//round that, picked in Settings:
//  blank  - every so often, switch the panels off for a moment and read the room without them
//  live   - no blink: a smoothed reading with the panels on, which the panels' own glow skews
//  hybrid - watch the live reading, which is free, and only blank when it says the light changed
#define BLANK_MS 240                    //the light integrates for ~100 ms; two cycles make sure one fits in the dark
int8_t autoLevel = -1;                  //the brightness auto-brightness wants; -1 until it has a reading
int8_t autoShown = -1;                  //what the panels are at, stepping toward autoLevel
int ambient = -1;                       //the reading brightness was last set from
float ambientLive = -1;                 //smoothed reading with the panels on
float liveAtBlank = -1;                 //ambientLive when the last blank reading was taken, for hybrid
bool blanking = false;
volatile bool autoBriMeasureReq = false; //from the web page: look now
unsigned long blankStart = 0, alsSkipUntil = 0, lastMeasureAt = 0, autoLastCheck = 0, autoLastStep = 0;
uint8_t hybridSkips = 0;                //hybrid checks in a row that didn't need a blank
int visorBright() { return (cfg.autoBriEna && autoShown >= 0) ? autoShown : cfg.bVisor; }
void autoBrightnessStep();
//Status screen: drawn from /screen.json, the layout the on-board designer saves. With a layout in
//place it owns the screen; without one, ProtoESP's original face-name screen is used instead.
uint8_t screenBuf[128 * 8], screenPrev[128 * 8];
bool screenPrevValid = false;
unsigned long lastScreenDraw = 0;
//Which page the screen is on. Pages marked "always" are the rotation, stepped by the button or the
//cycle timer; a page with a condition takes over while that condition holds and can linger after.
int screenManualPage = 0;           //where the rotation is, among the always pages
int screenShownPage = -1;           //what is actually on the screen now
unsigned long screenCycleAt = 0;    //when the rotation last stepped
unsigned long screenTakeoverUntil = 0; //a takeover page lingers until here
int screenTakeoverPage = -1;
volatile bool screenReloadReq = false;   //set by /savescreen
volatile bool oledFlipReq = false;       //set when Upside down is changed
volatile bool wakeFlickReq = false;      //from /wakeflick: play the warm-up now
bool bootFlickPending = false;           //switched on: warm up as soon as the first frame is drawn
bool bootBarHandover = false;            //the startup bar is playing; arm the warm-up when it ends
bool bootBarBlanked = false;             //panels held dark until the bar's own first frame is in them
unsigned long bootBlankAt = 0;           //when that blanking started, so it cannot last forever
#define EGG_GAP_MS 450               //longer than this between presses and the burst starts over
//Dino game: the flags live up here because /dino and doButtonAction both sit above the game itself
bool dinoActive = false;
volatile bool dinoStartReq = false, dinoJumpReq = false, dinoStopReq = false;
int dinoScore = 0;

//Slot machine easter egg: same reason, /slots and the button both sit above the machine itself
bool slotActive = false;
volatile bool slotStartReq = false, slotStopReq = false;
int8_t slotOutcome = -1;            //-1 still spinning, 0 no luck, 1 triple sevens
uint8_t slotPos[3] = {0, 0, 0};     //where each reel sits, in pixels down its loop of symbols
//A rapid burst of presses starts an easter egg. The face to come back to is remembered from the
//first press of the burst, not the last, so the faces the burst flicked through are undone too.
uint8_t eggPressCount = 0;
unsigned long eggLastPress = 0;
String eggBackTo = "";
volatile bool oledBrightReq = false;     //set when Screen brightness is changed
void screenTick();
inline bool oledLegacy(); //the old screen writes, only when no layout is loaded
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
//How loud it is, measured peak to peak.
//The original code took 32 ADC samples as fast as it could - about a millisecond - kept the single
//highest, and subtracted a stored resting level. A millisecond is a fraction of one cycle of a human
//voice, so speech was caught at a random point in its waveform and mostly under-reported, while a
//snap or a knock landed hard whatever the timing. Worse, the stored resting level could drift out of
//step and there was no way back.
//Instead: watch the highest and lowest reading over a 25 ms window and take the difference. That
//spans several cycles of any voice, and the resting level cancels out of a difference entirely, so
//there is nothing left to drift. Loudness is that swing above the room's own noise floor.
#define MIC_WINDOW_MS 25
#define MIC_WIN_LOW  (1 << 30)           //sentinels: I2S samples are signed either side of zero
#define MIC_WIN_HIGH (-(1 << 30))
int micWinMin = MIC_WIN_LOW, micWinMax = MIC_WIN_HIGH; //extremes within the window being collected
unsigned long micWinStart = 0;
float micSwing = 0;                      //latest peak-to-peak, lightly smoothed
float micNoise = 40;                     //the swing of a quiet room, from Measure silence
int micRawMid = 0;                       //middle of the swing: should sit near half the supply if wired right
bool prevSpeechEna = false;

unsigned long micCalUntil = 0;           //Measure silence: the quietest swing in this window is the floor
float micCalFloor = 0;
volatile bool micCalReq = false;

unsigned long micLoudUntil = 0;          //Measure loud: the biggest swing while you talk sets Range
float micLoudPeak = 0;
bool micLoudClipped = false;
volatile bool micLoudReq = false;
uint8_t micGainHint = 0;                 //0 not measured, 1 about right, 2 turn the pot up, 3 turn it down

//Mic test: two seconds of quiet, then three of talking, reported as two plain numbers so "is this
//thing working at all" stops being a matter of interpretation.
uint8_t micTestPhase = 0;                //0 idle, 1 keep quiet, 2 talk, 3 done
unsigned long micTestQuietUntil = 0, micTestTalkUntil = 0;
float micTestQuiet = 0, micTestLoud = 0;
volatile bool micTestReq = false;

inline bool micIs(const char *m) { return strcmp(micMode, m) == 0; }

//--------------------------------//Digital mic (I2S)
i2s_chan_handle_t micRxChan = NULL;
bool micI2SReady = false;
bool micLeftChannel = true;              //SEL unconnected on the board selects the left channel

bool micI2SBegin() {
  if(micRxChan) {                        //starting again: let go of the old channel first
    i2s_channel_disable(micRxChan);
    i2s_del_channel(micRxChan);
    micRxChan = NULL;
  }
  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  if(i2s_new_channel(&chanCfg, NULL, &micRxChan) != ESP_OK) return false;
  i2s_std_config_t stdCfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_I2S_RATE),
    //The SPH0645 sends its data left-justified rather than in standard Philips timing, which is why
    //it reads as nonsense with the usual settings. MSB format is that left-justified layout.
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)MIC_BCLK,
      .ws = (gpio_num_t)MIC_LRCL,
      .dout = I2S_GPIO_UNUSED,
      .din = (gpio_num_t)MIC_DOUT,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
      },
    },
  };
  stdCfg.slot_cfg.slot_mask = micLeftChannel ? I2S_STD_SLOT_LEFT : I2S_STD_SLOT_RIGHT;
  if(i2s_channel_init_std_mode(micRxChan, &stdCfg) != ESP_OK) return false;
  if(i2s_channel_enable(micRxChan) != ESP_OK) return false;
  logPrint("[I] Digital mic started on BCLK " + String(MIC_BCLK) + ", DOUT " + String(MIC_DOUT) +
           ", LRCL " + String(MIC_LRCL));
  return true;
}

//Pulls whatever samples are waiting into the window. Never waits: the animation must not stall for
//the mic, so a pass with nothing ready simply adds nothing.
void micCollect() {
  if(micIs("I2S")) {
    if(!micI2SReady) return;
    int32_t buf[64];
    size_t got = 0;
    if(i2s_channel_read(micRxChan, buf, sizeof(buf), &got, 0) != ESP_OK) return;
    int n = got / sizeof(int32_t);
    for(int i = 0; i < n; i++) {
      int v = (int)(buf[i] >> MIC_I2S_SHIFT);
      if(v < micWinMin) micWinMin = v;
      if(v > micWinMax) micWinMax = v;
    }
    return;
  }
  int rawInput = 0;
  for(int i = 0; i < 16; i++) {           //analog: loop() comes back thousands of times a second
    adc_oneshot_read(adc_handle, MICpin, &rawInput);
    if(rawInput < micWinMin) micWinMin = rawInput;
    if(rawInput > micWinMax) micWinMax = rawInput;
  }
}

void startMicCal() {
  micCalUntil = millis() + 1500;
  micCalFloor = 99999;
  logPrint(F("[I] Listening to the room to find the noise floor"));
}

void startMicLoud() {
  micLoudUntil = millis() + 3000;
  micLoudPeak = 0;
  micLoudClipped = false;
  logPrint(F("[I] Listening to a normal speaking voice to set Range"));
}

void startMicTest() {
  micTestPhase = 1;
  micTestQuietUntil = millis() + 2000;
  micTestTalkUntil = millis() + 5000;
  micTestQuiet = 99999;
  micTestLoud = 0;
  logPrint(F("[I] Mic test: keep quiet"));
}

void finishMicCal() {
  micCalUntil = 0;
  if(micCalFloor > 90000) return;
  micNoise = micCalFloor;
  cfg.spMin = (int)(micNoise + 0.5f);
  cfg.save();
  logPrint("[I] Noise floor measured: " + String(cfg.spMin));
  //A quiet room should swing a few tens of counts out of 4095. Hundreds means the mic board is
  //amplifying its own supply noise, and since loudness is measured above this floor, a voice
  //quieter than the noise can never register. Say so rather than quietly accepting it.
  if(cfg.spMin > 800) {
    micGainHint = 3;
    logPrint("[E] Noise floor is huge (" + String(cfg.spMin) + " of 4095): turn the mic's gain right down");
  } else if(micGainHint == 3) {
    micGainHint = 0;                     //a sane floor: drop the old complaint
  }
}

void finishMicLoud() {
  micLoudUntil = 0;
  float above = micLoudPeak - micNoise;
  if(above < 10) {                       //heard next to nothing over the room itself
    micGainHint = 2;
    logPrint("[E] Heard almost nothing (swing " + String((int)micLoudPeak) + " against a floor of " +
             String((int)micNoise) + "): turn the mic's gain up");
    return;
  }
  //Aim for normal speech to land about 80 of 100, leaving room above for a raised voice
  cfg.spMax = constrain((int)(above * 100.0f / 80.0f + 0.5f), 20, 4000);
  micGainHint = micLoudClipped ? 3 : 1;
  cfg.save();
  logPrint("[I] Range set to " + String(cfg.spMax) + " from a swing of " + String((int)micLoudPeak) +
           (micLoudClipped ? " (clipping: turn the gain down)" : ""));
}

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
  bool canBlink = false;      //the face says the mask may blink it (set in the face designer)
  bool saysBlink = false;     //whether the face said anything at all; if not, fall back to the timings
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
    //canBlink: the face designer's "The mask may blink this face". Faces drawn before this existed
    //say nothing, and those fall back to judging by the frame timings.
    visorNow->saysBlink = !doc["visor"]["canBlink"].isNull();
    visorNow->canBlink = doc["visor"]["canBlink"] | false;
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

    if(oledLegacy()) {
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

//Commands a phone can send, beyond the four the physical remote uses. Everything here runs on the
//NimBLE stack task, not the loop task, so nothing touches the panels, the filesystem or the config
//directly: it sets the same request flags the web endpoints set and lets loop() do the work.
//Defined below this point in the file, so they need saying up front.
void showPhrase(int slot);
void doButtonAction(const String &action, const String &face, int phraseSlot);
volatile bool bleSaveReq = false;     //config changed over BLE: save it from the loop task
volatile int8_t bleDisplayReq = 0;    //1 = displays on, -1 = off
volatile int8_t blePhraseReq = -1;    //quick phrase slot to play
volatile int8_t bleScreenReq = 0;     //1 = step the status screen on a page

//What the mask is doing now, as one short line a phone can parse: each field is key=value.
//Deliberately small enough to fit a single BLE notification at the negotiated MTU.
static String bleStatusLine() {
  String s = "s";
  s += " face=" + (currentAnim.endsWith(".json") ? currentAnim.substring(0, currentAnim.length() - 5) : currentAnim);
  s += " bright=" + String(visorBright());
  s += " boops=" + String(cfg.boopCount);
  s += " sleep=" + String(sleeping ? 1 : 0);
  s += " disp=" + String(displayOn ? 1 : 0);
  s += " speech=" + String(cfg.speechEna ? 1 : 0);
  return s;
}

class MyCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
      String temp = String(pCharacteristic->getValue().c_str());
      //--- commands a phone app sends: one letter, a colon, then the value ---
      if(temp.length() > 1 && temp.charAt(1) == ':') {
        String val = temp.substring(2);
        switch(temp.charAt(0)) {
          case 's':                                  //s: ask what the mask is doing
            pCharacteristic->setValue(bleStatusLine().c_str());
            pCharacteristic->notify();
            logPrint(F("[I] BT: status"));
            return;
          case 'b': {                                //b:0-15 panel brightness
            int v = constrain(val.toInt(), 0, 15);
            cfg.bVisor = v;
            brightnessDirty = true;
            bleSaveReq = true;
            logPrint("[I] BT: brightness " + String(v));
            return;
          }
          case 'd':                                  //d:0 / d:1 displays off or on
            bleDisplayReq = val.toInt() ? 1 : -1;
            logPrint("[I] BT: displays " + String(val.toInt() ? "on" : "off"));
            return;
          case 'z':                                  //z:0 / z:1 wake or sleep
            sleepRequest = val.toInt() ? 1 : -1;
            logPrint("[I] BT: " + String(val.toInt() ? "sleep" : "wake"));
            return;
          case 'p':                                  //p:0-5 play a quick phrase
            blePhraseReq = (int8_t)constrain(val.toInt(), 0, 5);
            logPrint("[I] BT: phrase " + String(blePhraseReq));
            return;
          case 'n':                                  //n:1 step the status screen on
            bleScreenReq = 1;
            logPrint(F("[I] BT: next screen page"));
            return;
          case 'w':                                  //w:1 play the wake-up warm-up
            wakeFlickReq = true;
            logPrint(F("[I] BT: warm-up"));
            return;
          case 'm':                                  //m:0 / m:1 speech on or off
            cfg.speechEna = val.toInt() != 0;
            bleSaveReq = true;
            logPrint("[I] BT: speech " + String(cfg.speechEna ? "on" : "off"));
            return;
          case 'f': {                                //f:name pick a face by name
            for(int i = 0; i < totalAnims; i++) {
              if(val == availAnims[i].substring(0, availAnims[i].length() - 5)) {
                xSemaphoreTake(animQueueMutex, portMAX_DELAY);
                animToLoad = availAnims[i];
                xSemaphoreGive(animQueueMutex);
                break;
              }
            }
            logPrint("[I] BT: face " + val);
            return;
          }
        }
      }
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
          if(oledLegacy())
            oled.writeRGB(vTAcro[visorNow->type]);
        } else if (temp.indexOf("set") > 0) {
          temp.remove(0,4);
          if(oledLegacy())
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
  //The default 23-byte MTU leaves 20 usable bytes, which silently truncates the face list and the
  //status line. Ask for the most a phone will give us; a device that refuses just keeps the default.
  NimBLEDevice::setMTU(517);

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

//--------------------------------//Over-the-air updates
//Replaces ElegantOTA. One upload endpoint takes either image and works out which it is from the
//bytes, not the file name: an app image starts with 0xE9, a LittleFS image has "littlefs" at
//offset 8. Anything else is refused before a byte is written.
//
//Firmware goes to whichever app slot isn't running, and only becomes the boot slot once the
//whole image has arrived and checked out. It then boots on probation: if it resets before it has
//run for 30 s, the bootloader puts the previous firmware back on its own.
//
//The filesystem can't work that way - there is only one live copy, and the page doing the
//upload is served from it. So it is written to the fsstage partition, read back and checked,
//and only copied over the live filesystem at the next boot, before anything mounts it. Settings
//live inside that filesystem, so they are lifted out first and put back afterwards. If power
//goes during the copy, the next boot simply starts it again.
//
//Costs nothing while idle: the endpoints only do work during an upload, and loop() just checks
//two numbers.

//The core confirms any new firmware as good during start-up, before setup() runs, which would
//make rollback protect against almost nothing. Deferring it lets loop() confirm it instead,
//once it has actually run.
extern "C" bool verifyRollbackLater() { return true; }

enum OtaKind { OTA_NONE, OTA_APP, OTA_FS };
OtaKind otaKind = OTA_NONE;
String otaError = "";
size_t otaBytes = 0;                       //filesystem bytes committed to the staging area so far
const esp_partition_t *otaStage = nullptr;
MD5Builder otaMd5;
static uint8_t otaBlock[4096];             //staging is written a sector at a time
static uint8_t otaCheckBuf[4096];
size_t otaBlockFill = 0;
volatile unsigned long otaRebootAt = 0;    //set once an upload is accepted; loop() restarts
bool otaConfirmed = false;

bool otaFlushBlock() { //erase one sector, then write it, so the erase is spread over the upload
  if(otaBlockFill == 0) return true;
  if(otaBytes + otaBlockFill > otaStage->size) { otaError = "The filesystem image is bigger than the partition"; return false; }
  if(esp_partition_erase_range(otaStage, otaBytes, sizeof(otaBlock)) != ESP_OK ||
     esp_partition_write(otaStage, otaBytes, otaBlock, otaBlockFill) != ESP_OK) {
    otaError = "Writing the staging area failed";
    return false;
  }
  otaBytes += otaBlockFill;
  otaBlockFill = 0;
  return true;
}

void otaUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if(index == 0) {
    otaKind = OTA_NONE; otaError = ""; otaBytes = 0; otaBlockFill = 0;
    if(len >= 1 && data[0] == 0xE9) otaKind = OTA_APP;
    else if(len >= 16 && memcmp(data + 8, "littlefs", 8) == 0) otaKind = OTA_FS;
    else otaError = "That isn't a firmware or filesystem image";

    if(otaKind == OTA_APP && !Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      otaError = String("Couldn't start the update: ") + Update.errorString();
      otaKind = OTA_NONE;
    } else if(otaKind == OTA_FS) {
      otaStage = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fsstage");
      if(!otaStage) {
        otaError = "No staging area - the new partition table has to be flashed over USB once first";
        otaKind = OTA_NONE;
      } else {
        otaMd5.begin();
      }
    }
    logPrint(otaKind == OTA_APP ? F("[I] Update: receiving firmware")
           : otaKind == OTA_FS  ? F("[I] Update: receiving filesystem") : F("[E] Update: file refused"));
  }

  if(otaError == "") {
    if(otaKind == OTA_APP) {
      if(Update.write(data, len) != len) otaError = String("Writing firmware failed: ") + Update.errorString();
    } else if(otaKind == OTA_FS) {
      otaMd5.add(data, len);
      size_t off = 0;
      while(off < len && otaError == "") {
        size_t room = sizeof(otaBlock) - otaBlockFill;
        size_t take = (len - off < room) ? (len - off) : room;
        memcpy(otaBlock + otaBlockFill, data + off, take);
        otaBlockFill += take;
        off += take;
        if(otaBlockFill == sizeof(otaBlock)) otaFlushBlock();
      }
    }
  }

  if(!final) return;
  if(otaKind == OTA_APP) {
    if(otaError != "") Update.abort();
    else if(!Update.end(true)) otaError = String("The firmware didn't check out: ") + Update.errorString();
  } else if(otaKind == OTA_FS && otaError == "" && otaFlushBlock()) {
    otaMd5.calculate();
    String want = otaMd5.toString();
    const esp_partition_t *live = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if(!live || otaBytes != live->size) {
      otaError = "The filesystem image isn't the size of the filesystem partition";
    } else {
      MD5Builder check;                       //read back what landed rather than trust the write
      check.begin();
      for(size_t pos = 0; pos < otaBytes && otaError == ""; pos += sizeof(otaCheckBuf)) {
        size_t n = (otaBytes - pos < sizeof(otaCheckBuf)) ? (otaBytes - pos) : sizeof(otaCheckBuf);
        if(esp_partition_read(otaStage, pos, otaCheckBuf, n) != ESP_OK) otaError = "Reading the staging area back failed";
        else check.add(otaCheckBuf, n);
      }
      check.calculate();
      if(otaError == "" && check.toString() != want) otaError = "The staged filesystem didn't read back correctly";
    }
    if(otaError == "") {
      Preferences pref;
      pref.begin("ota", false);
      pref.putUInt("fsLen", otaBytes);
      pref.putString("fsMd5", want);
      pref.putUChar("fsTries", 0);
      pref.putBool("fsPending", true);        //set last, so a half-written record is never acted on
      pref.end();
    }
  }
}

void applyStagedFilesystem() { //setup(), before LittleFS mounts: a staged filesystem goes live here
  Preferences pref;
  if(!pref.begin("ota", false)) return;
  if(!pref.getBool("fsPending", false)) { pref.end(); return; }

  const esp_partition_t *stage = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fsstage");
  const esp_partition_t *live  = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  size_t len = pref.getUInt("fsLen", 0);
  String want = pref.getString("fsMd5", "");
  uint8_t tries = pref.getUChar("fsTries", 0);
  if(!stage || !live || len != live->size || want == "" || tries >= 3) {
    Serial.println(F("[E] Staged filesystem abandoned"));
    pref.putBool("fsPending", false);
    pref.end();
    return;
  }
  pref.putUChar("fsTries", tries + 1);

  //Settings are inside the filesystem about to be replaced. Lift them out on the first attempt
  //and hold them in NVS, so an interrupted copy can't lose them.
  if(!pref.getBool("cfgHeld", false)) {
    if(LittleFS.begin(false)) {
      File f = LittleFS.open("/config.json", "r");
      if(f) { pref.putString("cfgJson", f.readString()); f.close(); }
      f = LittleFS.open("/configCRC.txt", "r");
      if(f) { pref.putString("cfgCrc", f.readString()); f.close(); }
      LittleFS.end();
    }
    pref.putBool("cfgHeld", true);
  }

  Serial.println(F("[I] Applying the staged filesystem..."));
  bool ok = esp_partition_erase_range(live, 0, live->size) == ESP_OK;
  for(size_t pos = 0; ok && pos < len; pos += sizeof(otaCheckBuf)) {
    size_t n = (len - pos < sizeof(otaCheckBuf)) ? (len - pos) : sizeof(otaCheckBuf);
    ok = esp_partition_read(stage, pos, otaCheckBuf, n) == ESP_OK &&
         esp_partition_write(live, pos, otaCheckBuf, n) == ESP_OK;
  }
  MD5Builder check;
  check.begin();
  for(size_t pos = 0; ok && pos < len; pos += sizeof(otaCheckBuf)) {
    size_t n = (len - pos < sizeof(otaCheckBuf)) ? (len - pos) : sizeof(otaCheckBuf);
    ok = esp_partition_read(live, pos, otaCheckBuf, n) == ESP_OK;
    if(ok) check.add(otaCheckBuf, n);
  }
  check.calculate();
  if(!ok || check.toString() != want) {
    //Leave it pending: the staging copy is still good, so start over rather than let
    //LittleFS.begin(true) format a half-copied filesystem.
    Serial.println(F("[E] Filesystem copy didn't verify, trying again"));
    pref.end();
    delay(200);
    ESP.restart();
  }

  if(pref.getBool("cfgHeld", false) && LittleFS.begin(false)) {
    if(pref.isKey("cfgJson")) {
      File f = LittleFS.open("/config.json", "w");
      if(f) { f.print(pref.getString("cfgJson", "")); f.close(); }
    }
    if(pref.isKey("cfgCrc")) {
      File f = LittleFS.open("/configCRC.txt", "w");
      if(f) { f.print(pref.getString("cfgCrc", "")); f.close(); }
    }
    LittleFS.end();
  }
  pref.remove("cfgJson");
  pref.remove("cfgCrc");
  pref.putBool("cfgHeld", false);
  pref.putUChar("fsTries", 0);
  pref.putBool("fsPending", false);          //cleared last: settings are back before this is forgotten
  pref.end();
  Serial.println(F("[I] Staged filesystem applied, settings kept"));
}

//--------------------------------//Joining an existing network
//As well as making its own access point, the mask can join a network you already have: a phone's
//hotspot, or the house WiFi. That is the whole difference between reaching the mask and having
//internet at the same time, because on the mask's own access point a phone cannot have both.
//The access point keeps running throughout, so nothing set here can lock you out of the mask.
//One radio means one channel: while the mask is on someone else's network its own access point
//moves to that network's channel, and anything already connected to it has to join again. That is
//the hardware being a single radio, not a fault.
bool staJoined = false;             //on the network right now
volatile bool staApplyReq = false;  //the details changed on the web page: start over
unsigned long staTryAt = 0;         //when this attempt started; 0 when not trying
bool mdnsUp = false;

void staBegin() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.staName.c_str(), cfg.staPass.c_str());
  staTryAt = millis();
  logPrint("[I] Joining " + cfg.staName);
}

void staStop() {
  if(mdnsUp) { MDNS.end(); mdnsUp = false; }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);               //the mask's own access point carries on
  staJoined = false;
  staTryAt = 0;
}

//Non-blocking: boot is never held up waiting for a network, and a wrong password just keeps
//retrying slowly in the background instead of stalling the mask.
void staStep() {
  if(staApplyReq) {
    staApplyReq = false;
    staStop();
    if(cfg.staEna && cfg.staName.length()) staBegin();
    else logPrint(F("[I] Not joining a network"));
    return;
  }
  if(!cfg.staEna) return;
  bool up = (WiFi.status() == WL_CONNECTED);
  if(up != staJoined) {
    staJoined = up;
    if(up) {
      staTryAt = 0;
      logPrint("[I] Joined " + cfg.staName + " as " + WiFi.localIP().toString());
      if(!mdnsUp && MDNS.begin(cfg.mdnsName.c_str())) {
        MDNS.addService("http", "tcp", 80);
        mdnsUp = true;
        logPrint("[I] Also at http://" + cfg.mdnsName + ".local");
      }
    } else {
      logPrint(F("[I] Dropped off the network"));
      if(mdnsUp) { MDNS.end(); mdnsUp = false; }
      staTryAt = millis();
    }
  }
  if(!staJoined && cfg.staName.length() && staTryAt != 0 && millis() - staTryAt > 25000) {
    logPrint("[I] Still trying to join " + cfg.staName);
    WiFi.begin(cfg.staName.c_str(), cfg.staPass.c_str());
    staTryAt = millis();
  }
}

//--------------------------------//WiFi server setup
void startWiFiWeb() {
  //AP_STA runs both at once: the mask's own access point stays up whatever happens to the network
  //it is asked to join, so a wrong password here can never lock you out of your own mask.
  WiFi.mode(cfg.staEna ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(cfg.wifiName, cfg.wifiPass);
  if(cfg.staEna && cfg.staName.length()) staBegin();

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
    cfg.getBool(request, "boopCountEna", cfg.boopCountEna);
    cfg.getBool(request, "boopLightEna", cfg.boopLightEna);
    if(cfg.getBool(request, "oledFlip", cfg.oledFlip)) oledFlipReq = true; //the loop task talks to the screen
    if(cfg.getInt(request, "boopShade", cfg.boopShade)) cfg.boopShade = constrain(cfg.boopShade, 5, 90);
    //auto-brightness: any change takes a fresh look at the light straight away
    bool autoChanged = false;
    autoChanged |= cfg.getBool(request, "autoBriEna", cfg.autoBriEna);
    if(cfg.getString(request, "autoBriMode", cfg.autoBriMode)) {
      autoChanged = true;
      if(cfg.autoBriMode != "blank" && cfg.autoBriMode != "live" && cfg.autoBriMode != "hybrid") cfg.autoBriMode = "blank";
    }
    if(cfg.getInt(request, "autoBriSecs", cfg.autoBriSecs)) { autoChanged = true; cfg.autoBriSecs = constrain(cfg.autoBriSecs, 5, 3600); }
    if(cfg.getInt(request, "autoBriMin", cfg.autoBriMin)) { autoChanged = true; cfg.autoBriMin = constrain(cfg.autoBriMin, 0, 15); }
    if(cfg.getInt(request, "autoBriMax", cfg.autoBriMax)) { autoChanged = true; cfg.autoBriMax = constrain(cfg.autoBriMax, 0, 15); }
    if(cfg.getInt(request, "autoBriDark", cfg.autoBriDark)) { autoChanged = true; cfg.autoBriDark = constrain(cfg.autoBriDark, 0, 65000); }
    if(cfg.getInt(request, "autoBriBright", cfg.autoBriBright)) { autoChanged = true; cfg.autoBriBright = constrain(cfg.autoBriBright, 1, 65535); }
    if(autoChanged) autoBriMeasureReq = true;
    if(cfg.getInt(request, "boopCount", cfg.boopCount)) boopCountDirty = false; //the Reset button
    cfg.getInt(request, "transMs", cfg.transMs);
    cfg.getFloat(request, "sleepTol", cfg.sleepTol);
    //brightness
    cfg.getInt(request, "bEar", cfg.bEar);
    cfg.getInt(request, "bVisor", cfg.bVisor);
    if(cfg.getInt(request, "bOled", cfg.bOled)) {
        cfg.bOled = constrain(cfg.bOled, 0, 4);
        oledBrightReq = true;              //the loop task talks to the screen, not the web task
    }
    //animation settings
    cfg.getInt(request, "rbSpeed", cfg.rbSpeed);
    cfg.getInt(request, "rbWidth", cfg.rbWidth);
    cfg.getInt(request, "spMin", cfg.spMin);
    cfg.getInt(request, "spMax", cfg.spMax);
    cfg.getInt(request, "spTrig", cfg.spTrig);
    //tilt animations
    cfg.getString(request, "aTilt", cfg.aTilt);
    cfg.getString(request, "aTilt2", cfg.aTilt2);
    cfg.getString(request, "aBoop", cfg.aBoop);
    cfg.getString(request, "aSleep", cfg.aSleep);
    cfg.getString(request, "aMilestone", cfg.aMilestone);
    //button
    cfg.getString(request, "btnShort", cfg.btnShort);
    cfg.getString(request, "btnDouble", cfg.btnDouble);
    cfg.getString(request, "btnHold", cfg.btnHold);
    cfg.getString(request, "btnShortFace", cfg.btnShortFace);
    cfg.getString(request, "btnDoubleFace", cfg.btnDoubleFace);
    cfg.getString(request, "btnHoldFace", cfg.btnHoldFace);
    if(cfg.getInt(request, "btnHoldMs", cfg.btnHoldMs)) cfg.btnHoldMs = constrain(cfg.btnHoldMs, 300, 5000);
    if(cfg.getInt(request, "btnShortPhrase", cfg.btnShortPhrase)) cfg.btnShortPhrase = constrain(cfg.btnShortPhrase, 0, 5);
    if(cfg.getInt(request, "btnDoublePhrase", cfg.btnDoublePhrase)) cfg.btnDoublePhrase = constrain(cfg.btnDoublePhrase, 0, 5);
    if(cfg.getInt(request, "btnHoldPhrase", cfg.btnHoldPhrase)) cfg.btnHoldPhrase = constrain(cfg.btnHoldPhrase, 0, 5);
    //quick phrases, one slot per request: phraseN, phraseSpeedN, phraseOnceN
    for(int i = 0; i < 6; i++) {
      cfg.getString(request, ("phrase" + String(i)).c_str(), cfg.phrase[i]);
      if(cfg.getInt(request, ("phraseSpeed" + String(i)).c_str(), cfg.phraseSpeed[i]))
        cfg.phraseSpeed[i] = constrain(cfg.phraseSpeed[i], 20, 300);
      cfg.getBool(request, ("phraseOnce" + String(i)).c_str(), cfg.phraseOnce[i]);
    }
    //sleep
    cfg.getBool(request, "sleepDeepEna", cfg.sleepDeepEna);
    if(cfg.getInt(request, "sleepDeepMins", cfg.sleepDeepMins)) cfg.sleepDeepMins = constrain(cfg.sleepDeepMins, 0, 240);
    cfg.getBool(request, "wakeFlickEna", cfg.wakeFlickEna);
    cfg.getBool(request, "wakeFlickBoot", cfg.wakeFlickBoot);
    cfg.getBool(request, "bootBarEna", cfg.bootBarEna);
    if(cfg.getInt(request, "bootBarMs", cfg.bootBarMs)) cfg.bootBarMs = constrain(cfg.bootBarMs, 300, 8000);
    if(cfg.getString(request, "wakeFlickStyle", cfg.wakeFlickStyle)) {
      if(cfg.wakeFlickStyle != "crt" && cfg.wakeFlickStyle != "tube") cfg.wakeFlickStyle = "crt";
    }
    if(cfg.getInt(request, "wakeFlickMs", cfg.wakeFlickMs)) cfg.wakeFlickMs = constrain(cfg.wakeFlickMs, 300, 8000);
    if(cfg.getInt(request, "wakeFlickGlitch", cfg.wakeFlickGlitch)) cfg.wakeFlickGlitch = constrain(cfg.wakeFlickGlitch, 0, 2);
    if(cfg.getString(request, "blinkMode", cfg.blinkMode)) {
      if(cfg.blinkMode != "face" && cfg.blinkMode != "blink" && cfg.blinkMode != "still") cfg.blinkMode = "face";
      blinkCachedAnim = "";                //look at the face again under the new setting
      instantReload = true;
    }
    if(cfg.getInt(request, "blinkDoublePct", cfg.blinkDoublePct)) cfg.blinkDoublePct = constrain(cfg.blinkDoublePct, 0, 100);
    if(cfg.getInt(request, "boopMilestone", cfg.boopMilestone)) cfg.boopMilestone = constrain(cfg.boopMilestone, 0, 10000);
    if(cfg.getString(request, "mouthMode", cfg.mouthMode)) {
      if(cfg.mouthMode != "flap" && cfg.mouthMode != "shapes") cfg.mouthMode = "shapes";
    }
    //tilt neutral
    cfg.getFloat(request, "neutralX", cfg.neutralX);
    cfg.getFloat(request, "neutralY", cfg.neutralY);
    cfg.getFloat(request, "neutralZ", cfg.neutralZ);
    //tilt triggers
    cfg.getFloat(request, "tiltX", cfg.tiltX);
    cfg.getFloat(request, "tilt2X", cfg.tilt2X);
    cfg.getFloat(request, "tilt2Y", cfg.tilt2Y);
    cfg.getFloat(request, "tilt2Z", cfg.tilt2Z);
    cfg.getFloat(request, "tiltY", cfg.tiltY);
    cfg.getFloat(request, "tiltZ", cfg.tiltZ);
    if(cfg.getFloat(request, "tiltTrig", cfg.tiltTrig)) cfg.tiltTrig = constrain(cfg.tiltTrig, 0.2f, 1.5f);
    //RGB visor color
    if(cfg.getString(request, "visColor", cfg.visColorStr))
        cfg.visColor = strtol(cfg.visColorStr.c_str() + 1, NULL, 16);
    //wifi: the mask's own access point
    cfg.getString(request, "wifiName", cfg.wifiName);
    cfg.getString(request, "wifiPass", cfg.wifiPass);
    //wifi: a network to join as well. Applied without a restart, so the page can say what happened.
    {
      bool wasEna = cfg.staEna;
      String wasName = cfg.staName, wasPass = cfg.staPass;
      cfg.getBool(request, "staEna", cfg.staEna);
      cfg.getString(request, "staName", cfg.staName);
      cfg.getString(request, "staPass", cfg.staPass);
      if(cfg.getString(request, "mdnsName", cfg.mdnsName)) {
        cfg.mdnsName.toLowerCase();                    //.local names are case-insensitive; keep one spelling
        if(cfg.mdnsName == "") cfg.mdnsName = "protogen";
      }
      if(cfg.staEna != wasEna || cfg.staName != wasName || cfg.staPass != wasPass) staApplyReq = true;
    }
    //boop threshold
    cfg.getInt(request, "boopThresh", cfg.boopThresh);
    if(cfg.getInt(request, "boopConfirm", cfg.boopConfirm)) cfg.boopConfirm = constrain(cfg.boopConfirm, 2, 10);
    if(cfg.getInt(request, "eggPresses", cfg.eggPresses)) cfg.eggPresses = constrain(cfg.eggPresses, 3, 10);
    cfg.getString(request, "eggAction", cfg.eggAction);
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

  server.on("/savescreen", HTTP_POST, [](AsyncWebServerRequest *request){ //the screen designer saving a layout
    if(!request->hasParam("layout", true)) {
      request->send(200, "text/plain", F("No layout sent"));
      return;
    }
    String layout = request->getParam("layout", true)->value();
    //Either shape is valid: pages, or the flat element list saved before pages existed. Checking for
    //one or the other before overwriting the file, so a truncated upload cannot wipe the layout.
    if(layout.indexOf("\"pages\"") < 0 && layout.indexOf("\"elements\"") < 0) {
      request->send(200, "text/plain", F("That doesn't look like a screen layout"));
      return;
    }
    File f = LittleFS.open("/screen.json", "w");
    if(!f) {
      request->send(200, "text/plain", F("Could not open screen.json"));
      return;
    }
    f.print(layout);
    f.close();
    screenReloadReq = true;                       //the loop task re-reads it and redraws
    logPrint(F("[I] Screen layout saved"));
    request->send(200, "text/plain", "ok");
  });

  server.on("/miccal", HTTP_GET, [](AsyncWebServerRequest *request){ //listen to the silence, store it as the quiet level
    if(!cfg.speechEna) {
      request->send(200, "text/plain", F("Turn Speech on first"));
      return;
    }
    micCalReq = true;                      //the loop task reads the mic; the web task never does
    request->send(200, "text/plain", "ok");
  });

  server.on("/micloud", HTTP_GET, [](AsyncWebServerRequest *request){ //talk normally: sets Range from what it hears
    if(!cfg.speechEna) {
      request->send(200, "text/plain", F("Turn Speech on first"));
      return;
    }
    micLoudReq = true;
    request->send(200, "text/plain", "ok");
  });

  server.on("/mictest", HTTP_GET, [](AsyncWebServerRequest *request){ //two seconds quiet, three talking, two numbers back
    if(!cfg.speechEna) {
      request->send(200, "text/plain", F("Turn Speech on first"));
      return;
    }
    micTestReq = true;
    request->send(200, "text/plain", "ok");
  });

  server.on("/autobri", HTTP_GET, [](AsyncWebServerRequest *request){ //measure=1: look at the light now
    if(request->hasParam("measure")) autoBriMeasureReq = true; //the loop task does it; the web task never touches I2C
    request->send(200, "text/plain", "ok");
  });

  server.on("/settime", HTTP_GET, [](AsyncWebServerRequest *request){ //the page tells the mask what time it is
    if(!request->hasParam("mins")) {
      request->send(200, "text/plain", F("No time given"));
      return;
    }
    int mins = request->getParam("mins")->value().toInt();
    if(mins < 0 || mins > 1439) {
      request->send(200, "text/plain", F("Minutes must be 0 to 1439"));
      return;
    }
    clockSetTo = (uint32_t)mins;
    clockSetAt = millis();
    if(request->hasParam("h24")) clockIs24 = request->getParam("h24")->value() == "1";
    request->send(200, "text/plain", "ok");
  });

  server.on("/phrase", HTTP_GET, [](AsyncWebServerRequest *request){ //show a saved phrase: ?slot=0..5
    if(!request->hasParam("slot")) {
      request->send(200, "text/plain", F("No slot given"));
      return;
    }
    int slot = request->getParam("slot")->value().toInt();
    if(slot < 0 || slot > 5) {
      request->send(200, "text/plain", F("Slot must be 0 to 5"));
      return;
    }
    if(cfg.phrase[slot] == "") {
      request->send(200, "text/plain", F("That phrase is empty"));
      return;
    }
    phraseRequest = slot;                  //the loop task builds it; the web task never loads faces
    request->send(200, "text/plain", "ok");
  });

  server.on("/boopcal", HTTP_GET, [](AsyncWebServerRequest *request){ //measure the resting reading
    boopCalRequest = true;                 //the loop task does it; the web task never touches I2C
    request->send(200, "text/plain", "ok");
  });

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
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
    st["staOn"] = cfg.staEna;                                         //joining a network is switched on
    st["staJoined"] = staJoined;                                      //and it actually got on
    st["staIp"] = staJoined ? WiFi.localIP().toString() : String("");
    st["staRssi"] = staJoined ? WiFi.RSSI() : 0;
    st["staName"] = cfg.staName;
    st["mdns"] = cfg.mdnsName;
    st["tilt"] = tiltInitDone;
    st["boop"] = ToFInitDone;
    st["boops"] = cfg.boopCount;
    st["i2c"] = i2cFound;
    st["boopRaw"] = apdsprox;
    st["boopBase"] = boopFloor < 0 ? 0 : (int)(boopFloor + 0.5f);   //the tracked resting level
    st["boopSig"] = boopSignal();
    st["als0"] = alsCh0;
    st["als1"] = alsCh1;
    st["lightNormal"] = lightNormal < 0 ? -1 : (int)(lightNormal + 0.5f);
    st["shade"] = shadeNow;                                           //percent darker than normal, right now
    st["lightOk"] = lightNormal >= LIGHT_MIN;                         //bright enough for the shadow check
    st["boopsIgnored"] = boopsIgnored;
    st["booping"] = booping;
    st["oled"] = oledInitDone;
    st["mic"] = cfg.speechEna ? micVolume : -1;                       //0-100; -1 when Speech is off
    st["micRaw"] = micRawMid;                                         //middle of the swing: near half the supply if wired right
    st["micSwing"] = (int)(micSwing + 0.5f);                          //how loud it is, peak to peak over 25 ms
    st["micQuiet"] = (int)(micNoise + 0.5f);                          //the room's own noise floor
    st["micCal"] = micCalUntil != 0;
    st["micLoud"] = micLoudUntil != 0;                                //measuring a speaking voice
    st["micGain"] = micGainHint;                                      //0 unmeasured, 1 ok, 2 turn up, 3 turn down
    st["micTest"] = micTestPhase;                                     //1 keep quiet, 2 talk, 3 done
    st["micTestQuiet"] = micTestQuiet > 90000 ? -1 : (int)(micTestQuiet + 0.5f);
    st["micTestLoud"] = (int)(micTestLoud + 0.5f);
    st["clockMins"] = clockMinutesNow();                              //-1 until a phone says what time it is
    st["clock24"] = clockIs24;
    st["micType"] = micMode;                                          //I2S or ANALOG: a digital mic has no gain pot
    st["micReady"] = micIs("I2S") ? micI2SReady : true;
    st["speaking"] = speaking;
    st["bright"] = visorBright();                                     //what the panels are actually set to
    st["autoLevel"] = autoLevel;                                      //-1 until auto-brightness has a reading
    st["ambient"] = ambient;                                          //the light reading brightness was last set from
    st["ambientLive"] = ambientLive < 0 ? -1 : (int)(ambientLive + 0.5f);
    st["measuredAgo"] = lastMeasureAt ? (millis() - lastMeasureAt) / 1000 : -1;
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

  server.on("/dino", HTTP_GET, [](AsyncWebServerRequest *request){ //the game on the maw: go, jump, stop
    if(visorType != "MAX72XX") { request->send(200, "text/plain", F("Only MAX7219 panels")); return; }
    if(request->hasParam("go")) dinoStartReq = true;
    else if(request->hasParam("jump")) dinoJumpReq = true;
    else if(request->hasParam("stop")) dinoStopReq = true;
    JsonDocument d;                         //the page shows the score while it plays
    d["on"] = dinoActive;
    d["score"] = dinoScore;
    d["best"] = cfg.dinoBest;
    String out;
    serializeJson(d, out);
    request->send(200, "application/json", out);
  });

  server.on("/slots", HTTP_GET, [](AsyncWebServerRequest *request){ //the slot machine on the maw
    if(visorType != "MAX72XX") { request->send(200, "text/plain", F("Only MAX7219 panels")); return; }
    if(request->hasParam("go")) slotStartReq = true;
    else if(request->hasParam("stop")) slotStopReq = true;
    JsonDocument d;                         //the page follows along while the reels spin
    d["on"] = slotActive;
    d["outcome"] = slotOutcome;             //-1 spinning, 0 no luck, 1 triple sevens
    d["wins"] = cfg.slotWins;
    d["spins"] = cfg.slotSpins;
    String out;
    serializeJson(d, out);
    request->send(200, "application/json", out);
  });

  server.on("/wakeflick", HTTP_GET, [](AsyncWebServerRequest *request){ //play the wake-up warm-up without waiting for a deep sleep
    if(visorType != "MAX72XX") { request->send(200, "text/plain", F("Only MAX7219 panels flicker")); return; }
    if(!cfg.wakeFlickEna) { request->send(200, "text/plain", F("Wake-up flicker is switched off")); return; }
    wakeFlickReq = true;                 //the panels belong to the loop task
    request->send(200, "text/plain", F("ok"));
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
    } else if (boopIsApds()) {
      request->send(200, "text/plain", String(boopSignal())); //read by loop(), not from the web task
    } else if (boopIs("VL53L1X")) {
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

  server.on("/ota", HTTP_POST, [](AsyncWebServerRequest *request){ //runs once the upload has finished
    if(otaKind == OTA_NONE || otaError != "") {
      logPrint("[E] Update failed: " + (otaError == "" ? String("nothing received") : otaError));
      request->send(500, "text/plain", otaError == "" ? "Nothing was received" : otaError);
    } else {
      logPrint(otaKind == OTA_APP ? F("[I] Firmware accepted, restarting") : F("[I] Filesystem staged, restarting to apply it"));
      request->send(200, "text/plain", otaKind == OTA_APP ? "firmware" : "filesystem");
      otaRebootAt = millis() + 1000;        //long enough for the reply to reach the phone
    }
  }, otaUpload);

  server.on("/otainfo", HTTP_GET, [](AsyncWebServerRequest *request){ //for the update page
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    const esp_partition_t *live = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    const esp_partition_t *stage = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fsstage");
    JsonDocument d;
    d["running"] = run ? run->label : "?";
    d["next"] = next ? next->label : "none";
    d["appSize"] = ESP.getSketchSize();
    d["appSlot"] = run ? run->size : 0;
    d["fsSize"] = live ? live->size : 0;
    d["staging"] = stage != nullptr;
    d["confirmed"] = otaConfirmed;
    d["built"] = String(__DATE__) + " " + __TIME__;
    String out;
    serializeJson(d, out);
    request->send(200, "application/json", out);
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){ //where ElegantOTA used to live
    request->redirect("/update.html");
  });

  server.onNotFound([](AsyncWebServerRequest *request){request->send(404, "text/plain", "Not found");});
  server.begin();
}

//--------------------------------//Sleep mode
//--------------------------------//Quick phrases
//The homepage builds its scrolling text in JavaScript and posts it, which is fine from a phone but
//no use to a button press with nobody holding a phone. So the mask builds its own: the current
//face's panels stay as the eyes, and the text scrolls along whichever row has the most panels,
//which is the mouth. Same 5x7 font the status screen uses, so it is already in the firmware.
void showPhrase(int slot) {
  if(slot < 0 || slot > 5) return;
  String text = cfg.phrase[slot];
  text.trim();
  if(text == "") {
    logPrint("[E] Quick phrase " + String(slot + 1) + " is empty");
    return;
  }
  if(!visorNow || numOfSegm == 0) return;

  //The mouth row: the panels flagged isMouth, left to right as they are numbered
  int mouth[MATRIXESNUM], mouthCount = 0;
  for(int i = 0; i < numOfSegm && i < MATRIXESNUM; i++) if(visorNow->isMouth[i]) mouth[mouthCount++] = i;
  if(mouthCount == 0) {
    logPrint(F("[E] This face has no mouth panels to scroll text along"));
    return;
  }

  //Text as columns of pixels, a blank column between letters
  const int maxCols = 64 * 6;
  static uint8_t cols[maxCols];
  int colCount = 0;
  for(unsigned int c = 0; c < text.length() && colCount < maxCols - 6; c++) {
    int idx = (uint8_t)text[c] - SCREEN_FONT_FIRST;
    if(idx < 0 || idx >= SCREEN_FONT_COUNT) idx = '?' - SCREEN_FONT_FIRST;
    uint8_t w = pgm_read_byte(&screenFontWidth[idx]);
    for(uint8_t x = 0; x < w; x++) cols[colCount++] = pgm_read_byte(&screenFont[idx][x]);
    cols[colCount++] = 0;
  }

  int width = mouthCount * 8;
  int steps = colCount + width;
  int speed = constrain(cfg.phraseSpeed[slot], 20, 300);
  //Frames are built as JSON because that is what loadAnim() understands; keeping one builder means
  //the phrase path cannot drift away from how faces are loaded.
  String out = "{\"ears\":{\"type\":\"none\"},\"visor\":{\"type\":\"custom\",\"isMouth\":[";
  for(int i = 0; i < numOfSegm; i++) out += (i ? ",false" : "false");   //no speech wobble on the text
  out += "],\"frames\":[";
  for(int s = 0; s < steps; s++) {
    if(s) out += ",";
    out += "{\"timespan\":" + String(speed) + ",\"leds\":[";
    for(int p = 0; p < numOfSegm; p++) {
      if(p) out += ",";
      int at = -1;
      for(int m = 0; m < mouthCount; m++) if(mouth[m] == p) { at = m; break; }
      uint64_t word = 0;
      if(at < 0) {
        word = visorNow->frames[currentVisorFrame].leds[p];   //not the mouth: leave the face as it is
      } else {
        for(int x = 0; x < 8; x++) {
          int t = at * 8 + x + s - width;
          uint8_t col = (t >= 0 && t < colCount) ? cols[t] : 0;
          for(int r = 0; r < 8; r++) if((col >> r) & 1) word |= 1ULL << (r * 8 + x);
        }
      }
      out += "\"" + String((uint32_t)(word >> 32), HEX) + String((uint32_t)word, HEX) + "\"";
    }
    out += "]}";
  }
  //Play once then go back to the face that was showing, or loop until something else is picked
  if(cfg.phraseOnce[slot] && currentAnim != "" && currentAnim != "POSTAnimLoad")
    out += "],\"then\":\"" + currentAnim + "\"}}";
  else out += "]}}";

  xSemaphoreTake(animQueueMutex, portMAX_DELAY);
  postAnimToLoad = out;
  xSemaphoreGive(animQueueMutex);
  logPrint("[I] Quick phrase " + String(slot + 1) + ": " + text);
}

//--------------------------------//What the button does
//Runs on the loop task, so it is free to load faces and touch the panels.

void doButtonAction(const String &action, const String &face, int phraseSlot) {
  if(dinoActive) { dinoJumpReq = true; return; }   //while a game runs the button jumps, whatever it is set to
  if(slotActive) return;                           //the reels spin on their own: nothing to press
  if(action == "none" || action == "") return;
  if(action == "nextFace" || action == "prevFace") {
    if(totalAnims <= 0) return;
    int at = btnNum;
    for(int i = 0; i < totalAnims; i++) {
      if(availAnims[i] == currentAnim) { at = i; break; }
    }
    btnNum = (action == "nextFace") ? (at + 1) % totalAnims
                                    : (at - 1 + totalAnims) % totalAnims;
    xSemaphoreTake(animQueueMutex, portMAX_DELAY);
    animToLoad = availAnims[btnNum];
    xSemaphoreGive(animQueueMutex);
    logPrint("[I] Button: " + availAnims[btnNum]);
    return;
  }
  if(action == "displays") {
    displayOn = !displayOn;
    displayDirty = true;
    logPrint(displayOn ? F("[I] Button: displays on") : F("[I] Button: displays off"));
    return;
  }
  if(action == "sleep") {
    sleepRequest = sleeping ? -1 : 1;      //loop() does the face change
    logPrint(sleeping ? F("[I] Button: waking") : F("[I] Button: sleeping"));
    return;
  }
  if(action == "face") {
    if(face == "" || !LittleFS.exists("/anims/" + face)) {
      logPrint("[E] Button: no such face, " + face);
      return;
    }
    xSemaphoreTake(animQueueMutex, portMAX_DELAY);
    animToLoad = face;
    xSemaphoreGive(animQueueMutex);
    logPrint("[I] Button: " + face);
    return;
  }
  if(action == "phrase") {
    showPhrase(phraseSlot);
    return;
  }
  if(action == "screen") {                 //step the status screen to its next page
    screenManualPage++;
    screenCycleAt = millis();              //a manual step restarts the cycle timer
    screenPrevValid = false;               //force a redraw even if the pixels happen to match
    logPrint(F("[I] Button: next screen page"));
    return;
  }
  logPrint("[E] Button: unknown action " + action);
}

//--------------------------------//Fake boot sequence
//A loading bar crawls the maw at startup, then the face snaps in. Built as an animation with a
//"then" the same way quick phrases are, so it goes through loadAnim() like any face rather than
//needing its own drawing path, and hands the mask back on its own when it finishes.
//
//It costs nothing to run alongside the wake-up warm-up: that drives the MAX7219 shutdown and
//intensity registers while this is only picture, so the panels stutter to life revealing a bar
//that is already filling. Nothing needed doing to make the two agree.
//
//Words are written as a padded 16-character hex string. loadAnim() reads them with strtoull, so
//padding is safe - and it is safer than how showPhrase() writes them, which glues the two halves
//together unpadded and would lose the low word of something like 0x0000000100000001. That is only
//harmless there because scrolling text makes dense words; a bar makes sparse ones.
//Builds and queues the bar. False means there is no bar to show, and the caller lights the panels
//back up. Split in two because the cleanup cannot be reached by jumping past the locals below.
static bool buildBootBar() {
  if(!cfg.bootBarEna || visorType != "MAX72XX") return false;
  if(!visorNow || numOfSegm == 0) return false;

  //The two maws in the order they sit on the face, not the order they are wired. data/visType.txt
  //reads t11;t10;t9;t8;-;-;t5;t4;t3;t2 along the bottom row, so panels 8 and 5 are the ends nearest
  //the nose and the bar grows outward from there on both sides at once. Hardcoded like the nose
  //panels further up, because the firmware never reads visType.txt - the virtual tester does, and
  //this layout was checked against what it draws rather than guessed at.
  struct Strip { uint8_t panel[4]; bool mirrored; };
  static const Strip strips[2] = {
    {{8, 9, 10, 11}, true},   //left maw, nose end first: its columns count right to left
    {{5, 4, 3,  2},  false},  //right maw, nose end first: its columns count left to right
  };
  const int LEN = 32;                      //four panels of eight columns
  for(int t = 0; t < 2; t++)
    for(int k = 0; k < 4; k++)
      if(strips[t].panel[k] >= numOfSegm) return false;  //not this mask's layout: leave startup alone

  //A bar that jumps and pauses reads as something working; one that creeps smoothly reads as an
  //animation of a bar. The last value repeats so the full bar is held a moment before the face.
  static const uint8_t STUT[] = {0,3,3,7,7,7,12,12,16,16,16,16,21,25,25,25,27,27,30,30,30,30};
  const int steps = sizeof(STUT) / sizeof(STUT[0]);
  //The bar runs on its own clock, before the warm-up rather than underneath it, so the two can be
  //tuned separately. Running them together put the bar's most interesting part behind four seconds
  //of stuttering panels and tearing, which read as no bar at all.
  //
  //A frame shorter than about 45 ms is gone before it registers, so below that the bar drops
  //frames instead of shortening them: a short setting gives a coarser bar rather than a longer
  //one. Clamping the span alone made everything under ~990 ms come out at 990 ms, so the bottom
  //third of the setting did nothing at all.
  const int barMs = constrain(cfg.bootBarMs, 300, 8000);
  int span = barMs / steps;
  int skip = 1;                                                 //show every skip'th step of the curve
  if(span < 45) {
    skip = (45 * steps + barMs - 1) / barMs;                    //enough thinning to keep frames visible
    if(skip < 1) skip = 1;
    span = barMs / ((steps + skip - 1) / skip);
  }
  span = constrain(span, 45, 400);

  String out = "{\"ears\":{\"type\":\"none\"},\"visor\":{\"type\":\"custom\",\"isMouth\":[";
  //every flag false: no speech wobble, and mouth bob leaves the bar alone rather than lifting it
  for(int i = 0; i < numOfSegm; i++) out += (i ? ",false" : "false");
  out += "],\"frames\":[";
  bool firstFrame = true;
  for(int s = 0; s < steps; s += skip) {
    if(s + skip >= steps) s = steps - 1;                        //always finish on the full bar
    uint64_t words[MATRIXESNUM];
    for(int i = 0; i < MATRIXESNUM; i++) words[i] = 0;
    for(int t = 0; t < 2; t++) {
      for(int pos = 0; pos < LEN; pos++) {
        uint8_t panel = strips[t].panel[pos / 8];
        uint8_t col = strips[t].mirrored ? (uint8_t)(7 - (pos % 8)) : (uint8_t)(pos % 8);
        words[panel] |= 1ULL << (1 * 8 + col);                //top rail of the frame
        words[panel] |= 1ULL << (6 * 8 + col);                //bottom rail
        if(pos == 0 || pos == LEN - 1)                        //end caps close the frame off
          for(int r = 1; r <= 6; r++) words[panel] |= 1ULL << (r * 8 + col);
        if(pos >= 2 && pos <= STUT[s])                        //the fill, inset a row from each rail
          for(int r = 3; r <= 4; r++) words[panel] |= 1ULL << (r * 8 + col);
      }
    }
    if(!firstFrame) out += ",";
    firstFrame = false;
    out += "{\"timespan\":" + String(span) + ",\"leds\":[";
    for(int p = 0; p < numOfSegm; p++) {
      if(p) out += ",";
      char hex[17];
      snprintf(hex, sizeof(hex), "%08lX%08lX",
               (unsigned long)(words[p] >> 32), (unsigned long)(words[p] & 0xFFFFFFFFUL));
      out += "\"" + String(hex) + "\"";
    }
    out += "]}";
  }
  out += "],\"then\":\"default.json\"}}";   //the face the mask starts on

  xSemaphoreTake(animQueueMutex, portMAX_DELAY);
  postAnimToLoad = out;
  xSemaphoreGive(animQueueMutex);
  bootBarHandover = (cfg.wakeFlickEna && cfg.wakeFlickBoot);    //warm up once the bar has finished
  logPrint("[I] Booting: loading bar over " + String(constrain(cfg.bootBarMs, 300, 8000)) + " ms");
  return true;
}

void showBootBar() {
  if(buildBootBar()) return;
  //No bar after all. The panels were blanked ready for it, so hand them back rather than leave the
  //mask dark, and let the warm-up do the reveal on its own.
  if(bootBarBlanked) {
    bootBarBlanked = false;
    if(visorType == "MAX72XX")
      for(int p = 0; p < MATRIXESNUM; p++) mx.control((uint8_t)p, MD_MAX72XX::SHUTDOWN, MD_MAX72XX::OFF);
    if(cfg.wakeFlickEna && cfg.wakeFlickBoot) bootFlickPending = true;
  }
}

//--------------------------------//Dino game on the maw
//The visor is a 10x3 grid of panel slots with only 14 filled, so there is no continuous strip to
//run along. Each maw is a clean 32x8 run though, so the game plays out on one and is mirrored onto
//the other, which reads as deliberate rather than lopsided. Playfield position 0 is the end nearest
//the nose and 31 is the end nearest the back of the head, matching the loading bar.
//
//Unlike the loading bar this cannot be a prebuilt animation: it reacts to the button every frame.
//So it takes the panels over directly while it runs, and the normal face render is skipped.
uint8_t dinoArcAt = 0;              //0 = on the ground, otherwise how far through the jump
bool dinoJustJumped = false;        //so the first airborne frame is shown rather than skipped past
int8_t dinoCactus[3];               //playfield positions, -1 for an empty slot
unsigned long dinoTickAt = 0, dinoOverAt = 0;
uint16_t dinoStepMs = 90;
int dinoTicks = 0;
String dinoBackTo = "";

//Lift per frame of a jump. The first arc tried was {1,2,3,3,3,3,2,1}, and simulating the rules
//showed it was unfair: a cactus sits inside the dino's box for five consecutive ticks while only
//six frames cleared it, leaving a one tick margin - one correct trigger position, which is the
//pixel-perfect timing that picking the short dino was meant to avoid. Rising straight to 2 and
//holding longer widens that to a real window.
static const uint8_t DINO_ARC[] = {2, 3, 3, 3, 3, 3, 2, 1};
#define DINO_ARC_LEN (sizeof(DINO_ARC) / sizeof(DINO_ARC[0]))
static const char *const DINO_SPRITE[] = {".###", ".#.#", "####", ".#.#"};   //4 wide, 4 tall
#define DINO_W 4
#define DINO_H 4
#define DINO_X 4                    //where the dino stands, in playfield positions
#define DINO_LEN 32                 //four panels of eight columns
//Column 0 of the sprite is only the tail tip, on one row. Colliding against it made the dino four
//wide and cost a tick of jump margin, so the box is the body: columns 1 to 3.
#define DINO_BOX_LO (DINO_X + 1)
#define DINO_BOX_HI (DINO_X + DINO_W - 1)

//The two maws in the order they sit on the face, exactly as the loading bar uses them.
//Shared by everything that draws across the maw: the dino game and the slot machine.
struct MawStrip { uint8_t panel[4]; bool mirrored; };
static const MawStrip mawStrips[2] = {
  {{8, 9, 10, 11}, true},           //left maw, nose end first
  {{5, 4, 3,  2},  false},          //right maw, nose end first
};

static inline uint8_t dinoLift() { return dinoArcAt == 0 ? 0 : DINO_ARC[dinoArcAt - 1]; }

//Draws straight to the panels, the same way setAllVisor() ends, since there is no face to build
static void mawPush(const uint64_t *words) {
  if(visorType != "MAX72XX") return;
  for(int y = 0; y < numOfSegm; y++) {
    for(int i = 0; i < 8; i++) {
      byte row = (words[y] >> (i * 8)) & 0xFF;
      for(int j = 0; j < 8; j++) mx.setPoint(i, j + (y * 8), bitRead(row, j));
    }
  }
  FdisplayVisor = true;
}

static void dinoRender(bool crashed) {
  uint64_t words[MATRIXESNUM];
  for(int i = 0; i < MATRIXESNUM; i++) words[i] = 0;
  //one plot lands on both maws at once, mirrored
  auto plot = [&](int pos, int r) {
    if(pos < 0 || pos >= DINO_LEN || r < 0 || r > 7) return;
    for(int t = 0; t < 2; t++) {
      uint8_t panel = mawStrips[t].panel[pos / 8];
      uint8_t col = mawStrips[t].mirrored ? (uint8_t)(7 - (pos % 8)) : (uint8_t)(pos % 8);
      words[panel] |= 1ULL << (r * 8 + col);
    }
  };
  for(int pos = 0; pos < DINO_LEN; pos++) plot(pos, 7);          //the ground
  uint8_t lift = dinoLift();
  for(int ri = 0; ri < DINO_H; ri++)
    for(int ci = 0; ci < DINO_W; ci++)
      if(DINO_SPRITE[ri][ci] == '#') plot(DINO_X + ci, (7 - DINO_H) + ri - lift);
  for(int c = 0; c < 3; c++) {
    if(dinoCactus[c] < 0) continue;
    for(int ri = 0; ri < 2; ri++) for(int ci = 0; ci < 2; ci++) plot(dinoCactus[c] + ci, 5 + ri);
  }
  if(crashed && ((millis() / 120) % 2)) {                        //flash the wreck before bowing out
    for(int i = 0; i < MATRIXESNUM; i++) words[i] = 0;
    for(int pos = 0; pos < DINO_LEN; pos++) plot(pos, 7);
  }
  mawPush(words);
}

void dinoStart() {
  if(visorType != "MAX72XX" || !visorNow || numOfSegm == 0) return;
  for(int t = 0; t < 2; t++)
    for(int k = 0; k < 4; k++)
      if(mawStrips[t].panel[k] >= numOfSegm) return;            //not this mask's layout
  dinoBackTo = currentAnim;
  dinoActive = true;
  dinoArcAt = 0;
  dinoJustJumped = false;
  dinoCactus[0] = DINO_LEN - 1; dinoCactus[1] = -1; dinoCactus[2] = -1;
  dinoScore = 0; dinoTicks = 0;
  dinoStepMs = 90;
  dinoOverAt = 0;
  dinoTickAt = millis();
  logPrint(F("[I] Dino: go"));
}

void dinoEnd() {
  if(!dinoActive) return;
  dinoActive = false;
  dinoOverAt = 0;
  if(dinoScore > cfg.dinoBest) { cfg.dinoBest = dinoScore; cfg.save(); }
  logPrint("[I] Dino: " + String(dinoScore) + " (best " + String(cfg.dinoBest) + ")");
  if(dinoBackTo != "" && dinoBackTo != "POSTAnimLoad") loadAnim(dinoBackTo, "");
  dinoBackTo = "";
  instantReload = true;
}

void dinoStep() {
  if(dinoStartReq) { dinoStartReq = false; if(!dinoActive) dinoStart(); }
  if(dinoStopReq)  { dinoStopReq = false; if(dinoActive) dinoEnd(); }
  if(!dinoActive) { dinoJumpReq = false; return; }
  if(dinoJumpReq) {                                             //only from the ground, no mid-air hops
    dinoJumpReq = false;
    if(dinoArcAt == 0) { dinoArcAt = 1; dinoJustJumped = true; }
  }
  if(dinoOverAt) {                                              //crashed: hold the wreck a moment
    dinoRender(true);
    if(millis() - dinoOverAt > 1200) dinoEnd();
    return;
  }
  if(millis() - dinoTickAt < dinoStepMs) return;
  dinoTickAt = millis();

  if(dinoArcAt) {
    if(dinoJustJumped) dinoJustJumped = false;                  //show frame one before moving on
    else { dinoArcAt++; if(dinoArcAt > DINO_ARC_LEN) dinoArcAt = 0; }
  }
  for(int c = 0; c < 3; c++) if(dinoCactus[c] >= 0) dinoCactus[c]--;
  for(int c = 0; c < 3; c++) if(dinoCactus[c] < -2) dinoCactus[c] = -1;
  //a new one once the last is far enough along, with a random gap so it never settles into a rhythm
  int furthest = -1;
  for(int c = 0; c < 3; c++) if(dinoCactus[c] > furthest) furthest = dinoCactus[c];
  if(furthest < DINO_LEN - 12 - (int)random(0, 8))
    for(int c = 0; c < 3; c++) if(dinoCactus[c] < 0) { dinoCactus[c] = DINO_LEN - 1; break; }

  if(++dinoTicks % 4 == 0) dinoScore++;
  if(dinoStepMs > 45 && dinoTicks % 40 == 0) dinoStepMs -= 5;    //creeps faster the longer you last

  uint8_t lift = dinoLift();
  int dTop = (7 - DINO_H) - lift, dBot = 6 - lift;               //the dino's rows right now
  for(int c = 0; c < 3; c++) {
    if(dinoCactus[c] < 0) continue;
    bool acrossX = (dinoCactus[c] + 1 >= DINO_BOX_LO) && (dinoCactus[c] <= DINO_BOX_HI);
    bool acrossY = (dBot >= 5) && (dTop <= 6);
    if(acrossX && acrossY) { dinoOverAt = millis(); dinoRender(true); return; }
  }
  dinoRender(false);
}

//--------------------------------//Slot machine on the maw
//An easter egg: three reels spin across both maws and stop one after another, left to right. One
//spin in three lands on triple sevens. The wearer cannot see their own face, so the same three
//reels are mirrored onto the inside screen - that is the only way to know whether the person in
//front of you just won.
//
//Reels are drawn from a loop of five symbols, each six wide and six tall, sitting in an eight-row
//cell so there is a one-row gap above and below. The maw window is exactly eight rows, so a reel
//stopped on a whole cell frames its symbol perfectly, and anything in between shows the tail of
//one symbol and the head of the next - which is what makes it read as a spin rather than a
//flicker between pictures.
#define SLOT_SYMS 5
#define SLOT_SPAN (SLOT_SYMS * 8)           //the reel loop, in pixels
#define SLOT_SEVEN 0                        //symbol 0 is the jackpot, so a win is three zeroes

//Six rows a symbol, six bits a row, bit 5 leftmost.
static const uint8_t SLOT_GLYPH[SLOT_SYMS][6] = {
  {0b111111, 0b000010, 0b000100, 0b001100, 0b001000, 0b001000},   //seven
  {0b011110, 0b100001, 0b100001, 0b100001, 0b100001, 0b011110},   //ring
  {0b001100, 0b011110, 0b111111, 0b111111, 0b011110, 0b001100},   //diamond
  {0b100001, 0b010010, 0b001100, 0b001100, 0b010010, 0b100001},   //cross
  {0b111111, 0b111111, 0b000000, 0b000000, 0b111111, 0b111111},   //bars
};

//Where things sit across the 32 columns of a maw: a reel window is eight wide with the six-wide
//symbol centred in it, and a solid divider stands between neighbouring reels.
static const uint8_t SLOT_REEL_X[3] = {2, 13, 24};   //leftmost column of each symbol
static const uint8_t SLOT_DIV_X[2] = {10, 21};

static unsigned long slotNextAt[3], slotStopAt[3], slotDoneAt = 0;
//The panels are quick to write but the screen is a 1024-byte push down a shared I2C bus, about
//30 ms of it. Drawing either one every time round the loop would spend the whole spin talking
//to displays, so each gets its own floor: fast enough that nothing looks stepped, no faster.
static unsigned long slotDrewAt = 0, slotScreenAt = 0;
#define SLOT_MAW_MS 12                 //the maw, comfortably ahead of the fastest reel
#define SLOT_SCREEN_MS 80              //the inside screen, two frames to a blink beat
static uint16_t slotPace[3];               //ms a pixel, climbing as a reel winds down
static int16_t slotLeft[3], slotRun[3];    //pixels left in the wind-down, and how long it is
static bool slotWinding[3], slotStopped[3];
static uint8_t slotTarget[3];
static String slotBackTo = "";

//True while a win is in its dark beat, so the maw and the screen blink together
static inline bool slotBlinkOff() {
  return slotOutcome == 1 && slotDoneAt && ((millis() - slotDoneAt) / 160) % 2 == 1;
}

static void slotRender() {
  uint64_t words[MATRIXESNUM];
  for(int i = 0; i < MATRIXESNUM; i++) words[i] = 0;
  //one plot lands on both maws at once, mirrored, exactly as the dino does it
  auto plot = [&](int pos, int r) {
    if(pos < 0 || pos >= 32 || r < 0 || r > 7) return;
    for(int t = 0; t < 2; t++) {
      uint8_t panel = mawStrips[t].panel[pos / 8];
      uint8_t col = mawStrips[t].mirrored ? (uint8_t)(7 - (pos % 8)) : (uint8_t)(pos % 8);
      words[panel] |= 1ULL << (r * 8 + col);
    }
  };
  for(int d = 0; d < 2; d++) for(int r = 0; r < 8; r++) plot(SLOT_DIV_X[d], r);
  if(!slotBlinkOff())
    for(int reel = 0; reel < 3; reel++)
      for(int y = 0; y < 8; y++) {
        int sr = (slotPos[reel] + y) % SLOT_SPAN;    //the row of the reel strip showing here
        int cell = sr % 8;
        if(cell < 1 || cell > 6) continue;           //the gap between one symbol and the next
        uint8_t row = SLOT_GLYPH[sr / 8][cell - 1];
        for(int c = 0; c < 6; c++) if((row >> (5 - c)) & 1) plot(SLOT_REEL_X[reel] + c, y);
      }
  mawPush(words);
}

//The wearer's view. Drawn straight into the screen buffer rather than through a designed layout,
//so the egg works on any mask without anyone having to add elements for it in the designer.
static void slotScreen() {
  if(!cfg.oledEna || !oledInitDone) return;
  memset(screenBuf, 0, 128 * 8);
  auto px = [&](int x, int y) {
    if(x < 0 || x > 127 || y < 0 || y > 63) return;
    screenBuf[(y >> 3) * 128 + x] |= (uint8_t)(1 << (y & 7));
  };
  String cap = (slotOutcome < 0) ? F("SPINNING") : (slotOutcome == 1 ? F("TRIPLE SEVENS!") : F("NO LUCK"));
  screenDrawText(screenBuf, cap, (128 - screenTextWidth(cap, 1)) / 2, 2, 1);
  bool dark = slotBlinkOff();
  for(int reel = 0; reel < 3; reel++) {
    const int bx = 8 + reel * 43, by = 14, bw = 26, bh = 34;   //a framed window a symbol at 4x fits
    for(int x = bx; x < bx + bw; x++) { px(x, by); px(x, by + bh - 1); }
    for(int y = by; y < by + bh; y++) { px(bx, y); px(bx + bw - 1, y); }
    if(dark) continue;
    for(int y = 0; y < 8; y++) {
      int sr = (slotPos[reel] + y) % SLOT_SPAN;
      int cell = sr % 8;
      if(cell < 1 || cell > 6) continue;
      uint8_t row = SLOT_GLYPH[sr / 8][cell - 1];
      for(int c = 0; c < 6; c++) {
        if(!((row >> (5 - c)) & 1)) continue;
        for(int sy = 0; sy < 4; sy++)
          for(int sx = 0; sx < 4; sx++) px(bx + 1 + c * 4 + sx, by + 1 + y * 4 + sy);
      }
    }
  }
  if(slotOutcome >= 0) {
    String tally = "won " + String(cfg.slotWins + (slotOutcome == 1 ? 1 : 0)) +
                   " of " + String(cfg.slotSpins);
    screenDrawText(screenBuf, tally, (128 - screenTextWidth(tally, 1)) / 2, 52, 1);
  }
  oled.blit(screenBuf);
  screenPrevValid = false;                 //the layout has to redraw from scratch afterwards
}

//backTo is the face to return to, which for a button burst is the one from before the burst
void slotStart(const String &backTo) {
  if(visorType != "MAX72XX" || !visorNow || numOfSegm == 0) return;
  for(int t = 0; t < 2; t++)
    for(int k = 0; k < 4; k++)
      if(mawStrips[t].panel[k] >= numOfSegm) return;           //not this mask's layout
  slotBackTo = (backTo != "") ? backTo : currentAnim;
  slotActive = true;
  slotOutcome = -1;
  slotDoneAt = 0;
  //One spin in three is the jackpot. The rest are drawn so they cannot accidentally be three
  //sevens as well, and a third of those are near misses - two sevens and something else - because
  //a machine that only ever loses flatly is a machine nobody watches to the end.
  if(random(0, 3) == 0) {
    slotTarget[0] = slotTarget[1] = slotTarget[2] = SLOT_SEVEN;
  } else if(random(0, 3) == 0) {
    slotTarget[0] = slotTarget[1] = SLOT_SEVEN;
    slotTarget[2] = (uint8_t)random(1, SLOT_SYMS);
  } else {
    do { for(int r = 0; r < 3; r++) slotTarget[r] = (uint8_t)random(0, SLOT_SYMS); }
    while(slotTarget[0] == SLOT_SEVEN && slotTarget[1] == SLOT_SEVEN && slotTarget[2] == SLOT_SEVEN);
  }
  unsigned long now = millis();
  for(int r = 0; r < 3; r++) {
    slotPos[r] = (uint8_t)random(0, SLOT_SPAN);
    slotPace[r] = 16;
    slotNextAt[r] = now;
    slotStopAt[r] = now + 400 + r * 250;   //the earliest each may begin winding down
    slotWinding[r] = false;
    slotStopped[r] = false;
    slotLeft[r] = 0;
    slotRun[r] = 1;
  }
  slotDrewAt = 0;
  slotScreenAt = 0;
  cfg.slotSpins++;
  logPrint(F("[I] Slots: spinning"));
}

void slotEnd() {
  if(!slotActive) return;
  slotActive = false;
  if(slotOutcome == 1) cfg.slotWins++;
  cfg.save();                              //a spin is a deliberate act and rare, unlike a boop
  logPrint("[I] Slots: " + String(slotOutcome == 1 ? "won" : "lost") + ", " +
           String(cfg.slotWins) + " of " + String(cfg.slotSpins));
  if(slotBackTo != "" && slotBackTo != "POSTAnimLoad") loadAnim(slotBackTo, "");
  slotBackTo = "";
  instantReload = true;
  screenPrevValid = false;
}

void slotTick() {
  if(slotStartReq) { slotStartReq = false; if(!slotActive && !dinoActive) slotStart(""); }
  if(slotStopReq)  { slotStopReq = false; if(slotActive) slotEnd(); }
  if(!slotActive) return;
  unsigned long now = millis();
  bool moved = false;
  int resting = 0;
  for(int r = 0; r < 3; r++) {
    if(slotStopped[r]) { resting++; continue; }
    if(now < slotNextAt[r]) continue;
    slotPos[r] = (uint8_t)((slotPos[r] + 1) % SLOT_SPAN);
    moved = true;
    //A reel only begins winding down once the one to its left has come to rest. Timing it by
    //the clock alone does not hold the order: the run-in to a stop is however far the reel
    //happens to be from its symbol, so a short one overtakes a long one and the reels land out
    //of sequence. Waiting on the neighbour makes left-to-right the one thing that cannot drift.
    if(!slotWinding[r] && now >= slotStopAt[r] && (r == 0 || slotStopped[r - 1])) {
      //Spread the slowing down over exactly the pixels left to the target, so the reel eases to
      //a stop on its symbol instead of snapping onto it.
      slotWinding[r] = true;
      int d = ((int)slotTarget[r] * 8 - (int)slotPos[r] + SLOT_SPAN) % SLOT_SPAN;
      if(d < 8) d += SLOT_SPAN;            //too close to be seen slowing: go round once more
      slotRun[r] = d;
      slotLeft[r] = d;
    } else if(slotWinding[r]) {
      slotLeft[r]--;
      if(slotLeft[r] <= 0) {
        slotStopped[r] = true;
        slotPos[r] = slotTarget[r] * 8;
        resting++;
        continue;
      }
      slotPace[r] = (uint16_t)(16 + (28L * (slotRun[r] - slotLeft[r])) / slotRun[r]);
    }
    slotNextAt[r] = now + slotPace[r];
  }
  if(resting == 3 && slotDoneAt == 0) {
    slotDoneAt = now;
    slotOutcome = (slotTarget[0] == SLOT_SEVEN && slotTarget[1] == SLOT_SEVEN &&
                   slotTarget[2] == SLOT_SEVEN) ? 1 : 0;
    logPrint(slotOutcome == 1 ? F("[I] Slots: triple sevens") : F("[I] Slots: no luck"));
    moved = true;
  }
  if(slotDoneAt) {                         //a win blinks, so this has to keep drawing
    if(now - slotDrewAt >= SLOT_MAW_MS) { slotDrewAt = now; slotRender(); }
    if(now - slotScreenAt >= SLOT_SCREEN_MS) { slotScreenAt = now; slotScreen(); }
    if(now - slotDoneAt > (unsigned long)(slotOutcome == 1 ? 2200 : 1300)) slotEnd();
    return;
  }
  if(moved && now - slotDrewAt >= SLOT_MAW_MS) { slotDrewAt = now; slotRender(); }
  if(moved && now - slotScreenAt >= SLOT_SCREEN_MS) { slotScreenAt = now; slotScreen(); }
}

//--------------------------------//Waking up from deep sleep
//Deep sleep leaves all fourteen panels shut down. Rather than handing them back all at once, each
//panel stutters on in its own time, dim and unsteady before it settles, with the picture tearing
//while it does: an old device warming up. Only the MAX7219 shutdown and intensity registers are
//driven, one chip at a time, plus a few throwaway rows the next frame paints over, so the face
//itself is never touched. The chain runs in wiring order rather than the order the panels sit on
//the face, which is exactly why the panels are woken in a random order instead of a sweep.
uint8_t wakeFlickPhase = 0;          //0 = idle, 1 = warming up
unsigned long wakeFlickEnds = 0;     //backstop, so a stuck panel can't hold the mask dark
unsigned long wfNext[MATRIXESNUM];   //when each panel next changes
uint16_t wfStep[MATRIXESNUM];        //each panel's own flicker speed
uint8_t wfLeft[MATRIXESNUM];         //stutters still to come before it settles
bool wfOn[MATRIXESNUM];
unsigned long wfGlitchAt = 0, wfGlitchClear = 0;

void wakeFlickBegin() {
  if(visorType != "MAX72XX" || !cfg.wakeFlickEna) return;
  unsigned long now = millis();
  int win = constrain(cfg.wakeFlickMs, 300, 8000);
  bool tube = (cfg.wakeFlickStyle == "tube");
  uint16_t unit = max(10, win / (tube ? 40 : 18)); //one flicker step: a tube twitches faster and more often
  for(int p = 0; p < MATRIXESNUM; p++) {
    wfOn[p] = false;
    mx.control((uint8_t)p, MD_MAX72XX::SHUTDOWN, MD_MAX72XX::ON);
    wfLeft[p] = tube ? random(5, 13) : random(3, 7);
    wfStep[p] = random(unit, unit * 2);
    wfNext[p] = now + random(0, (long)(win * 0.55f)); //first pop spread over the first half of the window
  }
  mx.control(MD_MAX72XX::INTENSITY, 0);
  displayOn = true;
  displayDirty = false;              //the warm-up owns the panels now
  wakeFlickPhase = 1;
  wakeFlickEnds = now + win + 400;
  wfGlitchAt = now + 60;
  wfGlitchClear = 0;
  logPrint("[I] Waking up: " + cfg.wakeFlickStyle + " warm-up over " + String(win) + " ms");
}

//Everything on and handed back to the normal brightness path. Also used to cut the warm-up short.
void wakeFlickEnd() {
  if(wakeFlickPhase == 0) return;
  for(int p = 0; p < MATRIXESNUM; p++) mx.control((uint8_t)p, MD_MAX72XX::SHUTDOWN, MD_MAX72XX::OFF);
  wakeFlickPhase = 0;
  wfGlitchClear = 0;
  brightnessDirty = true;
  instantReload = true;              //paint the real face over any tearing left behind
}

void wakeFlickStep() {
  if(wakeFlickPhase == 0) return;
  if(visorType != "MAX72XX" || !cfg.wakeFlickEna) { wakeFlickEnd(); return; }
  unsigned long now = millis();
  int target = visorBright();
  bool waiting = false;
  for(int p = 0; p < MATRIXESNUM; p++) {
    if(wfLeft[p] == 0) continue;     //this one has settled
    waiting = true;
    if(now < wfNext[p]) continue;
    wfOn[p] = !wfOn[p];
    if(--wfLeft[p] == 0) wfOn[p] = true; //however the stutter landed, a panel finishes lit
    mx.control((uint8_t)p, MD_MAX72XX::SHUTDOWN, wfOn[p] ? MD_MAX72XX::OFF : MD_MAX72XX::ON);
    if(wfOn[p]) {                    //unsteady while it warms, the real brightness once it settles
      int lvl = (wfLeft[p] == 0) ? target : random(0, target + 1);
      mx.control((uint8_t)p, MD_MAX72XX::INTENSITY, constrain(lvl, 0, 15));
    }
    wfNext[p] = now + random(wfStep[p] / 2, wfStep[p] * 2);
  }
  //Tearing: a few throwaway rows and columns, wiped by the next real frame
  if(cfg.wakeFlickGlitch > 0 && waiting && wfGlitchClear == 0 && now >= wfGlitchAt) {
    int burst = (cfg.wakeFlickGlitch >= 2) ? random(3, 9) : random(1, 4);
    for(int i = 0; i < burst; i++) {
      if(random(0, 2)) {
        mx.setColumn((uint16_t)random(0, MATRIXESNUM * 8), (uint8_t)random(0, 256));
      } else {
        uint8_t d = (uint8_t)random(0, MATRIXESNUM);
        mx.setRow(d, d, (uint8_t)random(0, 8), (uint8_t)random(0, 256));
      }
    }
    mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);  //push the mess out now; the renderer isn't due yet
    mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
    wfGlitchClear = now + random(30, 110);
  }
  if(wfGlitchClear != 0 && now >= wfGlitchClear) {   //put the picture back over the tearing
    wfGlitchClear = 0;
    wfGlitchAt = now + ((cfg.wakeFlickGlitch >= 2) ? random(70, 260) : random(200, 700));
    //Repaint what is showing rather than setting instantReload. The renderer treats that as
    //"advance a frame", so with tearing on it skipped the startup loading bar forward every
    //cleanup - a couple of dozen times across a long warm-up, which used the whole bar up in
    //about a second and left the face showing before the panels had finished stuttering on.
    if(visorNow && currentVisorFrame < visorNow->numOfFrames) setAllVisor(visorLedsNEW, 0, currentVisorFrame);
  }
  if(!waiting || now > wakeFlickEnds) {
    wakeFlickEnd();
    logPrint(F("[I] Warm-up done"));
  }
}

void setSleeping(bool on) { //loop task only: it loads animations
  if(on == sleeping) return;
  sleeping = on;
  if(on) {
    wakeAnim = currentAnim;
    logPrint("[I] Sleeping: " + cfg.aSleep);
    loadAnim(cfg.aSleep,"");
    sleepDeepAt = millis();
    sleepDeepDone = false;
  } else {
    logPrint(F("[I] Woke up"));
    if(wakeAnim != "" && wakeAnim != cfg.aSleep) loadAnim(wakeAnim,"");
    wakeAnim = "";
    if(sleepDeepDone) {                    //deep sleep had switched the panels off: bring them back
      sleepDeepDone = false;
      displayOn = true;
      displayDirty = true;
      wakeFlickBegin();                    //one panel at a time, if the warm-up is switched on
    }
    brightnessDirty = true;
  }
  stillSince = millis();
}

//Sleep goes straight to the sleeping face: no dimming down, no climbing back. This only handles
//the deeper sleep that switches the panels off after a while.
void sleepEaseStep() {
  //Asleep a long time: switch the panels off entirely, if that is switched on
  if(sleeping && cfg.sleepDeepEna && cfg.sleepDeepMins > 0 && !sleepDeepDone && displayOn &&
     millis() - sleepDeepAt >= (unsigned long)cfg.sleepDeepMins * 60000UL) {
    sleepDeepDone = true;
    displayOn = false;
    displayDirty = true;
    logPrint("[I] Deep sleep after " + String(cfg.sleepDeepMins) + " minutes: displays off");
  }
}

//--------------------------------//Setup
void setup() {
  animQueueMutex = xSemaphoreCreateMutex();
  Serial.begin(115200);

  pinMode(animBtn, INPUT_PULLUP);
  pinMode(animBtnGnd, OUTPUT); //the button's other leg
  digitalWrite(animBtnGnd, LOW);
  pinMode(0, INPUT_PULLUP);

  if(boopIs("KY-032")) {
    pinMode(T_in, INPUT_PULLUP);
    pinMode(T_en, OUTPUT);
  } else if ((boopIs("Capac"))) {
    pinMode(T_in, INPUT_PULLDOWN);
  }

  hwBtn.setDebounceTime(50);

  applyStagedFilesystem(); //must come before the mount below: begin(true) would format a half-copied one
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

  micNoise = (float)cfg.spMin;   //the noise floor measured last time

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

  //After the ADC, so the bit clock takes GPIO 4 back from it rather than the other way round
  if(micIs("I2S")) {
    micI2SReady = micI2SBegin();
    if(!micI2SReady) logPrint(F("[E] Digital mic did not start; check BCLK, DOUT and LRCL"));
  }

  if(visorType == "WS2812") {
    ledController[0] = &FastLED.addLeds<WS2812B, DATA_PIN_VISOR, GRB>(visorLeds, visorLedsNum);
  } else if (visorType == "MAX72XX") {
    mx.begin();
    //Startup runs in order: the loading bar fills, then the warm-up stutters the panels back on to
    //reveal the face. Either way the panels start dark here.
    //
    //They have to. showBootBar() needs the face loaded before it can know which panels are the maw,
    //so setup loads the face first and only then queues the bar - and the loop draws and flushes
    //that face once before it picks the bar up off the queue. Leaving the panels lit meant that one
    //frame showed: a split second of face before the bar appeared. Blanking here and lighting them
    //only when the bar's own frame is in the chips removes it.
    if(cfg.wakeFlickEna && cfg.wakeFlickBoot) bootFlickPending = true;
    if(bootFlickPending || cfg.bootBarEna) {
      for(int p = 0; p < MATRIXESNUM; p++) mx.control((uint8_t)p, MD_MAX72XX::SHUTDOWN, MD_MAX72XX::ON);
      bootBarBlanked = cfg.bootBarEna;     //the bar lights them; without one the warm-up does
      bootBlankAt = millis();
    }
    if(cfg.bootBarEna) bootFlickPending = false;   //armed later, when the bar hands over
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

  if(screenLoadLayout()) logPrint("[I] Screen layout loaded, " + String(screenElementCount()) + " elements on " +
                                  String(screenPageCount()) + " page" + (screenPageCount() == 1 ? "" : "s"));
  else logPrint(F("[I] No screen layout: using the original screen"));

  if(cfg.oledEna) {
    if(!oled.init(oledAddr,cfg.bOled,INApresent)) {
      logPrint(F("[E] An Error has occurred while initializing SSD1306!"));
      cfg.oledEna = false;
    } else {
      oledInitDone = true;
      oled.setFlip(cfg.oledFlip);
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

  if(boopIsApds()) { //set up even with boop off, so the boop reading can be checked while calibrating
    ToFInitDone = boopSensorStart();
    logPrint(ToFInitDone ? F("[I] Boop sensor ready") : F("[E] Boop sensor not found on I2C (0x39)"));
  } else if (boopIs("VL53L1X") && cfg.boopEna) {
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
  showBootBar();   //a loading bar first, if that is switched on; it hands back to the face itself

  logPrint("[I] Free heap: "+String(ESP.getFreeHeap()));
  logPrint("[I] Free PSRAM: "+String(ESP.getFreePsram()));
}

//--------------------------------//Loop vars
String oldanim, boopoldanim;
bool FdisplayVisor = false, FdisplayBlush = false, FdisplayEar = false, booping = false, wasTilt = false, boopRea = false, remoteSign = false, speaking = true;
float zAx,yAx,finalMicAvg; //the envelope follower and its averaging array went with peak-to-peak sampling
int boopRead, startIndex = 1, micVolume, btnNum = 0, currFade = 1;
unsigned long lastMillsEars = 0, lastMillsVisor = 0, lastMillsTilt = 0, laskSpeakCheck = 0, lastMillsBoop = 0, lastFLED = 0, vaStatLast = 0, btnPressTime = 0, tiltChange = 0, check0button = 0, looptime = 0, fadeTime = 0, laskSpeakAnim = 0, lastBoopCheck = 0;
bool btnHeld = false, btnLongDone = false; //button: held down now / hold action already fired
//Double press: a single press has to wait this long to see whether a second one is coming. Only
//waited when Double press is actually set to something, so the button stays instant otherwise.
#define BTN_DOUBLE_MS 260
bool btnWaitingSingle = false;
unsigned long btnFirstRelease = 0;
unsigned long lastImuRetry = 0, lastApdsRetry = 0; //sensor not found yet: when it was last looked for

//How the mouth moves with your voice, chosen by Mouth movement in Settings:
//  flap   - upstream's. The mouth opens by one pixel and volume only decides how far along the
//           mouth that opening spreads, so every volume ends up looking much the same.
//  shapes - the mouth opens further the louder you are, in four steps, and each step reaches a
//           different way along the mouth. A mumble barely parts it; a shout opens it wide.
uint8_t mouthBand = 0;              //0 closed, 3 widest
unsigned long mouthBandAt = 0;

//Blinks, chosen by Blinks in Settings:
//  face  - leave the face alone: most of these faces are animations that blink themselves
//  blink - the mask does the blinking, at random intervals, and holds the face on its resting frame
//          so its own blink cannot fire as well and stutter against this one
//  still - hold the resting frame and never blink
//A face can say for itself whether the mask may blink it, with the tick box in the face designer.
//Faces drawn before that existed say nothing, so those are judged by their frame timings instead:
//"a long look plus a short blink" can be held, while a face whose frames all run the same length is
//a real animation (confused, heart, angry) and freezing it would throw the whole thing away.
uint8_t blinkStep = 0;              //0 open, 3 nearly shut
unsigned long blinkStepAt = 0, nextBlinkAt = 0;
int blinkRestFrame = 0;             //the long "eyes open" frame
bool blinkFaceEligible = false;
bool blinkPaired = false;           //this blink is the second of a quick pair, so don't pair again
String blinkCachedAnim = "";
//Per step of the close and open. Six steps, so a blink takes about 450 ms - the same as the blink
//the default face draws for itself (five frames at 90 ms), so the mask's blink and a face's own
//read as the same gesture rather than one of them looking like a twitch.
#define BLINK_STEP_MS 75

//Works out, once per face, whether it is a long look plus a short blink, and which frame the look is
void blinkCheckFace() {
  blinkCachedAnim = currentAnim;
  blinkFaceEligible = false;
  blinkRestFrame = 0;
  if(!visorNow || visorNow->numOfFrames < 1) return;
  int longest = 0, second = 0, longestIdx = 0;
  bool toldByFace = visorNow->saysBlink;
  if(toldByFace && !visorNow->canBlink) return;   //the face says no: leave it entirely alone
  for(int i = 0; i < visorNow->numOfFrames; i++) {
    int t = visorNow->frames[i].timespan;
    if(t > longest) { second = longest; longest = t; longestIdx = i; }
    else if(t > second) second = t;
  }
  blinkRestFrame = longestIdx;
  //The face's own say overrides the guess; otherwise one frame at least three times any other means
  //a still face with a blink tacked on the end.
  blinkFaceEligible = toldByFace ? true
                                 : ((visorNow->numOfFrames == 1) || (second > 0 && longest >= second * 3));
}

bool blinkHoldsFace() {
  if(cfg.blinkMode == "face") return false;
  if(currentAnim != blinkCachedAnim) blinkCheckFace();
  return blinkFaceEligible;
}

void dynamicSpeak(uint64_t *leds, bool isMouth[MATRIXESNUM], int volume) {
  int mouthIndexes[MATRIXESNUM];
  int mouthCount = 0;
  for (int i = 0; i < MATRIXESNUM; i++) { // collect true (mouth) indexes
    if (isMouth[i]) {
      mouthIndexes[mouthCount++] = i;
    }
  }
  int half = mouthCount / 2;
  if (half <= 0) return;

  if (cfg.mouthMode == "shapes") {
    uint8_t want = volume >= 70 ? 3 : volume >= 40 ? 2 : volume >= 15 ? 1 : 0;
    //Open straight away, close after a moment: speech dips between syllables, and snapping shut on
    //every dip is what makes a mouth look like it is chattering rather than talking.
    if (want > mouthBand) { mouthBand = want; mouthBandAt = millis(); }
    else if (want < mouthBand && millis() - mouthBandAt >= 90) { mouthBand = want; mouthBandAt = millis(); }
    if (mouthBand == 0) return;
    static const uint8_t openBy[4] = {0, 1, 2, 3};      //pixels the mouth opens
    static const uint8_t reachPct[4] = {0, 55, 80, 100}; //how far along the mouth it reaches
    int reach = (half * 8 * reachPct[mouthBand]) / 100;
    for (int i = 0; i < half; i++) {
      int x = reach - (i * 8);
      if (x <= 0) continue;
      x = constrain(x, 0, 8);
      int leftIndex  = mouthIndexes[half - 1 - i];
      int rightIndex = mouthIndexes[half + i];
      leds[leftIndex]  = speakMatrixGrow(leds[leftIndex],  x, true,  openBy[mouthBand]);
      leds[rightIndex] = speakMatrixGrow(leds[rightIndex], x, false, openBy[mouthBand]);
    }
    return;
  }

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

//3x5 digits for the boop counter, one bit per pixel, top row first
const uint8_t boopDigits[10][5] = {
  {0b111,0b101,0b101,0b101,0b111}, {0b010,0b110,0b010,0b010,0b111},
  {0b111,0b001,0b111,0b100,0b111}, {0b111,0b001,0b111,0b001,0b111},
  {0b101,0b101,0b111,0b001,0b001}, {0b111,0b100,0b111,0b001,0b111},
  {0b111,0b100,0b111,0b101,0b111}, {0b111,0b001,0b001,0b001,0b001},
  {0b111,0b101,0b111,0b101,0b111}, {0b111,0b101,0b111,0b001,0b111},
};

//The number as a strip of pixel columns, three wide a digit with a blank between, most significant
//first. Bit r of a column is row r. Returns how many columns were written.
#define BOOP_MAXCOLS 24                         //five digits is 19, so this is room to spare
static int boopNumberCols(int value, uint8_t *cols) {
  int digits[5], n = 0;
  if(value <= 0) digits[n++] = 0;
  else { int v = value; while(v > 0 && n < 5) { digits[n++] = v % 10; v /= 10; } }
  int used = 0;
  for(int i = n - 1; i >= 0 && used < BOOP_MAXCOLS; i--) {
    for(int c = 0; c < 3 && used < BOOP_MAXCOLS; c++) {
      uint8_t col = 0;
      for(int r = 0; r < 5; r++) if((boopDigits[digits[i]][r] >> (2 - c)) & 1) col |= 1 << r;
      cols[used++] = col;
    }
    if(i > 0 && used < BOOP_MAXCOLS) cols[used++] = 0;   //a blank column between digits
  }
  return used;
}

//How the number pops. Each stage runs until that many ms after the boop: kind 0 is a bar of
//that width, kind 1 the digits lifted by that many rows. It squeezes open, overshoots high,
//bounces low, settles, then collapses back out again.
struct BoopStage { uint16_t until; uint8_t kind; int8_t amount; };
const BoopStage boopStages[] = {
  {  60, 0, 2}, { 110, 0, 4}, { 160, 0, 6},     //a dot widening into a bar
  { 220, 1, 1}, { 280, 1, -1}, { 340, 1, 0},    //digits: overshoot, bounce, land
  {1200, 1, 0},                                 //sitting there to be read
  {1270, 1, 1}, {1330, 0, 6}, {1390, 0, 2},     //lift, collapse, gone
};
#define boopStageCount (sizeof(boopStages) / sizeof(boopStages[0]))

int boopNumberWidth(int value) {                //how many columns that number needs
  uint8_t cols[BOOP_MAXCOLS];
  return boopNumberCols(value, cols);
}

//How far along the number has scrolled, in columns. A number that fits the panel never moves. One
//that does not sits still a moment, walks left far enough to show its end, and sits again - the way
//an mp3 player shows a title too long for its screen. The window is the stage the number already
//spends being read, from boopStages: index 5 ends at 340 ms and the long hold ends at 1200 ms.
int boopScrollOffset(unsigned long dt, int value) {
  int travel = boopNumberWidth(value) - 8;
  if(travel <= 0) return 0;                     //it fits: nothing to scroll
  const unsigned long from = boopStages[5].until, to = boopStages[6].until;
  if(dt <= from) return 0;
  if(dt >= to) return travel;
  float t = (float)(dt - from) / (float)(to - from);      //0 to 1 across the hold
  const float pause = 0.25f;                    //still at each end, moving through the middle
  if(t <= pause) return 0;
  if(t >= 1.0f - pause) return travel;
  return (int)(((t - pause) / (1.0f - 2 * pause)) * travel + 0.5f);
}

uint64_t boopDigitsWord(int value, int lift, int xoff) { //row r, column c sits at bit r*8+c
  uint8_t cols[BOOP_MAXCOLS];
  int n = boopNumberCols(value, cols);
  int x0 = (n <= 8) ? (8 - n) / 2 : -xoff;      //centred while it fits, scrolled once it does not
  uint64_t w = 0;
  for(int c = 0; c < n; c++) {
    int x = x0 + c;
    if(x < 0 || x > 7) continue;                //outside the panel: clipped by the window
    for(int r = 0; r < 5; r++) {
      if(!((cols[c] >> r) & 1)) continue;
      int y = 1 + r - lift;
      if(y < 0 || y > 7) continue;
      w |= 1ULL << (y * 8 + x);
    }
  }
  return w;
}

uint64_t boopBarWord(int width) { //a centred bar, two rows tall - the number squeezing in and out
  uint64_t w = 0;
  int x0 = (8 - width) / 2;
  for(int r = 3; r <= 4; r++)
    for(int c = x0; c < x0 + width; c++)
      w |= 1ULL << (r * 8 + c);
  return w;
}

int boopPhase() { //which stage of the pop we're in, -1 for nothing showing
  if(!cfg.boopCountEna || boopShownAt == 0) return -1;
  unsigned long dt = millis() - boopShownAt;
  for(uint8_t i = 0; i < boopStageCount; i++) {
    if(dt < boopStages[i].until) return i;
  }
  return -1;
}

void countBoop() { //the boop landed: count it, but don't show anything yet
  cfg.boopCount++;
  boopCountDirty = true;      //saved from loop() a while later, not on every boop
  logPrint("[I] Boop #" + String(cfg.boopCount));
}

//Every so many boops, a special face instead of the usual one. countBoop() has already counted this
//boop by the time this is asked, so the hundredth boop is the one that celebrates.
String boopFaceNow() {
  if(cfg.boopMilestone > 0 && cfg.aMilestone != "" && (cfg.boopCount % cfg.boopMilestone) == 0) {
    logPrint("[I] Boop #" + String(cfg.boopCount) + " is a milestone: " + cfg.aMilestone);
    milestoneBoop = true;
    return cfg.aMilestone;
  }
  milestoneBoop = false;
  return cfg.aBoop;
}

void endBoop() { //the paw has gone: now pop the total up, over the face the boop face left behind
  boopShownAt = millis();
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
  if(blinkStep > 0) {
    //Close the eyes by squeezing their artwork toward the middle: keep the rows in a band that
    //shrinks as the blink deepens. Eye panels are the ones that are neither mouth nor nose, which
    //on this layout is the four at the ends. Row r sits in byte r of the panel's word.
    for(int y = 0; y < numOfSegm; y++) {
      if(visorNow->isMouth[y] || y == noseLeftPanel || y == noseRightPanel) continue;
      for(int r = 0; r < 8; r++) {
        if(r >= blinkStep && r <= 7 - blinkStep) continue;
        tempLeds[y] &= ~(0xFFULL << (r * 8));
      }
    }
  }
  int bphase = boopPhase();
  if(bphase >= 0) { //boop counter: the number takes over both nose panels while it pops
    const BoopStage &bst = boopStages[bphase];
    int shown = cfg.boopCount % 100000;         //five digits is all the strip holds
    uint64_t w = (bst.kind == 0) ? boopBarWord(bst.amount)
                                 : boopDigitsWord(shown, bst.amount,
                                     boopScrollOffset(millis() - boopShownAt, shown));
    if(noseLeftPanel  < numOfSegm) tempLeds[noseLeftPanel]  = w;
    if(noseRightPanel < numOfSegm) tempLeds[noseRightPanel] = w;
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

//--------------------------------//Auto-brightness
//A light reading -> the brightness it should give. Log scale between the dark and bright calibration
//points, because eyes see light that way: a room at 50 and one at 500 feel far more different than
//500 and 950. A change has to be worth over half a step, so it can't hover between two levels.
bool liveRebase = false; //hybrid: re-take the live reference once the panels have settled after a blank

void setAmbient(float v) {
  ambient = (int)(v + 0.5f);
  lastMeasureAt = millis();
  float dark = max(cfg.autoBriDark, 0) + 1.0f;
  float bright = max(cfg.autoBriBright, 0) + 1.0f;
  if(bright < dark * 1.5f) bright = dark * 1.5f;
  float t = (log(v + 1.0f) - log(dark)) / (log(bright) - log(dark));
  t = constrain(t, 0.0f, 1.0f);
  int lo = min(cfg.autoBriMin, cfg.autoBriMax), hi = max(cfg.autoBriMin, cfg.autoBriMax);
  float want = lo + t * (hi - lo);
  int8_t before = autoLevel;
  if(autoLevel < 0 || fabsf(want - autoLevel) >= 0.6f) autoLevel = (int8_t)lround(want);
  autoLevel = constrain(autoLevel, lo, hi); //the clamps may have just changed
  if(autoLevel != before) logPrint("[I] Light " + String(ambient) + ": brightness " + String(autoLevel));
}

void autoBrightnessStep() {
  unsigned long now = millis();
  if(!cfg.autoBriEna || visorType != "MAX72XX" || !boopIs("APDS9930") || !ToFInitDone) {
    if(blanking) { blanking = false; displayDirty = true; } //sensor gone mid-blank: put the panels back
    if(autoShown >= 0) { autoLevel = autoShown = -1; brightnessDirty = true; } //back to the manual brightness
    return;
  }

  if(blanking) { //panels are off: once a whole light reading fits in the dark, take it and switch them back on
    if(now - blankStart >= BLANK_MS) {
      int v = apds9930Read16(0x14);
      blanking = false;
      mx.control(MD_MAX72XX::SHUTDOWN, displayOn ? MD_MAX72XX::OFF : MD_MAX72XX::ON);
      alsSkipUntil = now + 250;           //the next reading straddles the blank
      if(v >= 0) { setAmbient(v); liveRebase = true; }
    }
    return;
  }

  bool asked = autoBriMeasureReq;
  if((asked || autoLevel < 0 || now - autoLastCheck >= (unsigned long)cfg.autoBriSecs * 1000UL)
     && transPhase == 0 && wakeFlickPhase == 0 && !booping) { //never blink over a face change, a warm-up or a boop
    autoBriMeasureReq = false;
    autoLastCheck = now;
    bool blank = cfg.autoBriMode != "live";
    if(cfg.autoBriMode == "hybrid" && !asked && ambient >= 0) {
      if(liveRebase && autoShown == autoLevel) { //settled since the last blank: the live reading with the panels at this level is the new reference
        liveAtBlank = ambientLive;
        liveRebase = false;
        blank = false;
      } else if(!liveRebase) {
        float moved = liveAtBlank > 0 ? fabsf(ambientLive - liveAtBlank) / max(liveAtBlank, 5.0f) : 1.0f;
        blank = moved >= 0.25f || ++hybridSkips >= 10; //and every tenth check regardless, in case the reference drifted
      } else {
        blank = false;                    //still stepping to the last level: look again next time
      }
    }
    if(cfg.autoBriMode == "live") {
      if(ambientLive >= 0) setAmbient(ambientLive);
    } else if(blank) {
      hybridSkips = 0;
      if(!displayOn) {                    //already dark: nothing to blank
        int v = apds9930Read16(0x14);
        if(v >= 0) { setAmbient(v); liveRebase = true; }
      } else {
        blanking = true;
        blankStart = now;
        mx.control(MD_MAX72XX::SHUTDOWN, MD_MAX72XX::ON);
      }
    }
  }

  if(autoLevel >= 0 && autoShown != autoLevel && transPhase == 0 && now - autoLastStep >= 120) { //ease there a step at a time
    autoLastStep = now;
    autoShown = autoShown < 0 ? autoLevel : autoShown + (autoLevel > autoShown ? 1 : -1);
    brightnessDirty = true;
  }
}

//--------------------------------//Status screen from screen.json
inline bool oledLegacy() { return cfg.oledEna && oledInitDone && !screenHasLayout(); }

void screenTick() {
  if(slotActive) return;                   //the slot machine draws the screen itself while it spins
  if(screenReloadReq) {                    //a new layout was just saved from the designer
    screenReloadReq = false;
    screenLoadLayout();
    screenPrevValid = false;
    screenManualPage = 0;
    screenTakeoverPage = -1;
    logPrint("[I] Screen layout reloaded, " + String(screenElementCount()) + " elements on " +
             String(screenPageCount()) + " page" + (screenPageCount() == 1 ? "" : "s"));
  }
  if(oledBrightReq && oledInitDone) {
    oledBrightReq = false;
    oled.oledBright(cfg.bOled);
  }
  if(oledFlipReq && oledInitDone) {        //screen turned the other way up
    oledFlipReq = false;
    oled.setFlip(cfg.oledFlip);
    screenPrevValid = false;
    if(!screenHasLayout() && cfg.oledEna) { //the original screen doesn't redraw itself
      oled.speak(speaking);
      oled.writeSet(1);
      if(currentAnim.endsWith(".json")) oled.writeAnim(currentAnim.substring(0, currentAnim.length() - 5));
    }
  }
  if(!cfg.oledEna || !oledInitDone || !screenHasLayout()) return;
  //A few times a second is plenty, and the screen shares I2C with the sensors. The unset clock
  //blinks on a 600 ms beat, so while it is blinking the draw has to come round faster than that or
  //it would stop on whichever half it last drew.
  unsigned long drawEvery = clockKnown() ? 400 : 200;
  if(millis() - lastScreenDraw < drawEvery) return;
  lastScreenDraw = millis();
  ScreenData d;
  d.face = currentAnim.endsWith(".json") ? currentAnim.substring(0, currentAnim.length() - 5) : currentAnim;
  d.wifiName = cfg.wifiName;
  d.boops = cfg.boopCount;
  d.wifiClients = WiFi.softAPgetStationNum();
  d.ip = staJoined ? WiFi.localIP().toString() : String("");
  d.brightness = visorBright();
  d.uptime = millis() / 1000;
  d.tempC = temperatureRead();
  d.closeness = boopSignal();
  d.boopThresh = cfg.boopThresh;
  d.shade = shadeNow;
  d.shadeNeed = cfg.boopShade;
  d.booping = booping;
  d.sleeping = sleeping;
  d.displayOn = displayOn;
  d.tilt = tiltInitDone;
  d.boopSensor = ToFInitDone;
  d.clockMins = clockMinutesNow();
  d.clock24 = clockIs24;
  d.micLevel = cfg.speechEna ? micVolume : 0;
  d.micTrig = cfg.spTrig;
  d.speaking = cfg.speechEna && speaking;   //speaking is what actually moves the mouth
  //milestoneBoop stays set until the next ordinary boop, so on its own it would pin the page up
  //forever. The counter pop is the celebration: bound it to that, and the page's own linger then
  //carries it a moment further, the same way the boop page does.
  d.milestone = milestoneBoop && boopPhase() >= 0;
  //Pick the page. A page with a condition wins while that condition holds, and keeps the screen for
  //its linger time afterwards, so a boop page does not vanish the instant the paw lifts.
  int page = -1;
  for(int i = 0; i < screenPageCount(); i++) {
    uint8_t show = screenPageShow(i);
    if(show == PAGE_ALWAYS) continue;
    bool on = (show == PAGE_BOOPED && d.booping) || (show == PAGE_NOTBOOPED && !d.booping) ||
              (show == PAGE_SLEEPING && d.sleeping) || (show == PAGE_AWAKE && !d.sleeping) ||
              (show == PAGE_DISPLAYSOFF && !d.displayOn) ||
              (show == PAGE_SPEAKING && d.speaking) || (show == PAGE_NOTSPEAKING && !d.speaking) ||
              (show == PAGE_MILESTONE && d.milestone);
    if(on) {
      page = i;
      screenTakeoverPage = i;
      screenTakeoverUntil = millis() + screenPageHold(i);
      break;
    }
  }
  if(page < 0 && screenTakeoverPage >= 0 && millis() < screenTakeoverUntil) {
    page = screenTakeoverPage;         //condition ended, but it lingers a moment
  } else if(page < 0) {
    screenTakeoverPage = -1;
    //The rotation: step through the always pages on the timer, if one is set
    int alwaysCount = 0;
    for(int i = 0; i < screenPageCount(); i++) if(screenPageShow(i) == PAGE_ALWAYS) alwaysCount++;
    if(alwaysCount > 1 && screenCycleSecs() > 0 &&
       millis() - screenCycleAt >= (unsigned long)screenCycleSecs() * 1000UL) {
      screenCycleAt = millis();
      screenManualPage++;
    }
    if(alwaysCount <= 0) page = 0;     //every page is conditional: fall back to the first
    else {
      screenManualPage %= alwaysCount;
      int seen = 0;
      for(int i = 0; i < screenPageCount(); i++) {
        if(screenPageShow(i) != PAGE_ALWAYS) continue;
        if(seen == screenManualPage) { page = i; break; }
        seen++;
      }
    }
  }
  if(page < 0) page = 0;
  screenShownPage = page;

  screenRender(screenBuf, d, page);
  if(screenPrevValid && memcmp(screenBuf, screenPrev, sizeof(screenBuf)) == 0) return; //nothing changed: don't push pixels
  memcpy(screenPrev, screenBuf, sizeof(screenBuf));
  screenPrevValid = true;
  oled.blit(screenBuf);
}

void loop() {
  if(otaRebootAt && millis() >= otaRebootAt) { //an update was accepted: restart into it
    ESP.restart();
  }
  if(!otaConfirmed && millis() > 30000) { //30 s running without falling over: keep this firmware
    otaConfirmed = true;
    esp_ota_img_states_t otaState;
    if(esp_ota_get_state_partition(esp_ota_get_running_partition(), &otaState) == ESP_OK &&
       otaState == ESP_OTA_IMG_PENDING_VERIFY) {
      esp_ota_mark_app_valid_cancel_rollback();
      logPrint(F("[I] New firmware confirmed"));
    }
  }
  autoBrightnessStep();
  sleepEaseStep();
  wakeFlickStep();
  staStep();
  screenTick();
  if(brightnessDirty && visorType == "MAX72XX") {
    brightnessDirty = false;
    if(transPhase == 0 && wakeFlickPhase == 0) mx.control(MD_MAX72XX::INTENSITY, visorBright()); //a transition or warm-up owns the intensity
  }
  if(displayDirty && wakeFlickPhase != 0) wakeFlickEnd(); //turning the displays on or off cuts the warm-up short
  if(displayDirty && visorType == "MAX72XX" && !blanking) { //from /display: MAX7219 shutdown turns the LEDs off but keeps the picture
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
      if(transLevel < visorBright()) {
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
  dinoStep();                              //while a game is running it owns the panels
  slotTick();                              //as does the slot machine
  if(!dinoActive && !slotActive && (visorNow->type == 0 || (visorNow->type == 1 && visorType == "MAX72XX"))) { //custom
    if(blinkHoldsFace()) {                 //the mask is doing the blinking: sit on the resting frame
      if(currentVisorFrame != blinkRestFrame || instantReload) {
        currentVisorFrame = blinkRestFrame;
        lastMillsVisor = millis();
        setAllVisor(visorLedsNEW,0,currentVisorFrame);
        instantReload = false;
      }
    } else if(lastMillsVisor+visorNow->frames[currentVisorFrame].timespan <= millis() || instantReload) {
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
  if(!dinoActive && visorNow && (visorNow->type == 0 || (visorNow->type == 1 && visorType == "MAX72XX"))) {
    bool wantUp = cfg.bobEna && (millis() % bobPeriod) < bobLift;
    //Blink, when the mask is the one doing it. Random gaps: a blink on a metronome reads as
    //mechanical. Never while asleep (the eyes are already shut) or mid-dip between faces.
    uint8_t wantBlink = blinkStep;
    if(cfg.blinkMode == "blink" && blinkHoldsFace() && !sleeping && transPhase == 0) {
      if(blinkStep == 0) {
        if(nextBlinkAt == 0) nextBlinkAt = millis() + random(2500, 7000);
        else if(millis() >= nextBlinkAt) { wantBlink = 1; blinkStepAt = millis(); }
      } else if(millis() - blinkStepAt >= BLINK_STEP_MS) {
        blinkStepAt = millis();
        //Count 1..6 straight through and let drawStep below fold it: 1,2,3 close the lids, then
        //4,5,6 draw as 3,2,1 to open them, holding a moment at the bottom.
        //This used to turn the counter around at 4 instead of counting on, which left it ticking
        //3,4,3,4 forever - and since both of those draw as 3, the eyes shut to a line, stopped
        //changing, and so stopped being redrawn at all.
        wantBlink = blinkStep + 1;
        if(wantBlink > 6) {
          wantBlink = 0;
          //Now and then, blink twice. A second blink a tenth of a second behind the first is what
          //reads as alive; a single blink on a timer, however random the gap, still reads as a
          //machine ticking. blinkPaired makes sure a pair is a pair and never a stutter of three.
          if(blinkPaired) {
            blinkPaired = false;
            nextBlinkAt = millis() + random(2500, 7000);
          } else if(cfg.blinkDoublePct > 0 && (int)random(0, 100) < cfg.blinkDoublePct) {
            blinkPaired = true;
            nextBlinkAt = millis() + random(90, 190);   //the gap inside a pair, not between blinks
          } else {
            nextBlinkAt = millis() + random(2500, 7000);
          }
        }
      }
    } else if(blinkStep != 0) {
      wantBlink = 0;                       //mode changed or the face took over: open the eyes again
      blinkPaired = false;                 //and don't owe a second blink from before
    }
    uint8_t drawStep = wantBlink <= 3 ? wantBlink : (uint8_t)(7 - wantBlink); //4,5,6 reopen as 3,2,1
    uint8_t lastDraw = blinkStep <= 3 ? blinkStep : (uint8_t)(7 - blinkStep);
    int bphase = boopPhase();
    if(wantUp != bobUp || bphase != lastBoopPhase || drawStep != lastDraw) {
      bobUp = wantUp;
      lastBoopPhase = bphase;
      blinkStep = wantBlink;
      setAllVisor(visorLedsNEW,0,currentVisorFrame);
    } else {
      blinkStep = wantBlink;
    }
  }

  //--------------------------------//TILT
  //--------------------------------//Boop proximity, on its own fast timer
  //Read at BOOP_POLL_MS rather than with the rest, because this is the one that decides how quickly
  //a boop registers. The light channel integrates for about 100 ms (ATIME 0xDB) so reading it
  //faster gains nothing, and the tilt sensor has no need to be quick - both stay where they were.
  if(lastProxAt+BOOP_POLL_MS<=millis()) {
    lastProxAt = millis();
    if(boopIsApds() && ToFInitDone) {
      int prox = boopReadProx();
      if(prox < 0) {
        if(++apdsFails >= 10) {
          apdsFails = 0;
          ToFInitDone = false;
          logPrint(F("[E] Boop sensor stopped responding"));
        }
      } else {
        apdsFails = 0;
        if(boopFloor < 0) {                    //first reading: prime the median and the resting level
          for(int i = 0; i < 5; i++) boopRawBuf[i] = prox;
          boopFloor = prox;
        }
        //Median of the last five: a stray spike or two can't move it. Three was tried first and still
        //let about one false boop through every six minutes in simulation; five let none through at
        //any threshold, and still catches a 0.3 s tap every time.
        boopRawBuf[boopRawIdx] = prox;
        boopRawIdx = (boopRawIdx + 1) % 5;
        int sorted5[5];
        memcpy(sorted5, boopRawBuf, sizeof(sorted5));
        for(int i = 1; i < 5; i++) {           //insertion sort, five numbers
          int v = sorted5[i], j = i - 1;
          while(j >= 0 && sorted5[j] > v) { sorted5[j + 1] = sorted5[j]; j--; }
          sorted5[j + 1] = v;
        }
        apdsprox = sorted5[2];
        //Track the resting level. It follows a weaker reflection down quickly and creeps up slowly
        //with drift, so a boop - a fast rise - is never mistaken for drift. It holds still during a
        //boop, so a paw left on the visor isn't soaked into it.
        //Still tracked on the old 100 ms cadence even though the reading is now faster. These
        //coefficients are per-sample, so running them at 30 ms would make the resting level creep
        //three times quicker in real time and start soaking up the very boop it is meant to ignore.
        if(!booping && millis() - lastFloorAt >= 100) {
          lastFloorAt = millis();
          if(apdsprox < boopFloor) boopFloor += (apdsprox - boopFloor) * 0.3f;
          else boopFloor += (apdsprox - boopFloor) * 0.01f;
        }
      }
    }
  }

  //--------------------------------//I2C sensors: light and tilt, every 100 ms on the loop task
  if(lastMillsTilt+100<=millis()) {
    lastMillsTilt = millis();
    if(boopIs("APDS9930") && ToFInitDone) {    //every 100 ms, to catch a paw's shadow as it arrives
      alsCh0 = apds9930Read16(0x14);
      alsCh1 = apds9930Read16(0x16);
      //While the panels are blanked for a light reading, and just after, the reading is the room
      //without the panels: not a shadow, and not what normal looks like with them on.
      if(alsCh0 >= 0 && !blanking && millis() >= alsSkipUntil) {
        //the live reading for auto-brightness, held still while something is near the visor
        if(ambientLive < 0) ambientLive = alsCh0;
        else if(!booping && boopSignal() <= cfg.boopThresh / 2) ambientLive += (alsCh0 - ambientLive) * 0.05f;
        if(lightNormal < 0) lightNormal = alsCh0;
        shadeNow = lightNormal > 0 ? constrain((int)(100.0f * (1.0f - alsCh0 / lightNormal)), 0, 100) : 0;
        shadeBuf[shadeIdx] = shadeNow;
        shadeIdx = (shadeIdx + 1) % 10;
        //Hold normal still during a boop and while something is near, so a paw isn't soaked into it
        if(!booping && boopSignal() <= cfg.boopThresh / 2) {
          if(alsCh0 > lightNormal) lightNormal += (alsCh0 - lightNormal) * 0.3f;
          else lightNormal += (alsCh0 - lightNormal) * 0.02f;
        }
      }
    }
    if(tiltInitDone) {
      if(readAccel()) imuFails = 0;
      else if(++imuFails >= 10) { //a second of silence: hand it to the retry rather than sit there
        imuFails = 0;
        tiltInitDone = false;
        logPrint(F("[E] Tilt sensor stopped responding"));
      }
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
        //How far the head has leaned along each taught direction, as a fraction of the way to it.
        //This used to match the reading against each taught point inside a tolerance box, which
        //meant leaning FURTHER than taught fell out of the box and cancelled the face. A projection
        //keeps rising as you lean, so leaning harder can only ever hold the tilt.
        float fSide  = tiltFraction(accelX,accelY,accelZ, cfg.tiltX,cfg.tiltY,cfg.tiltZ,   cfg.neutralX,cfg.neutralY,cfg.neutralZ);
        float fSide2 = tiltFraction(accelX,accelY,accelZ, cfg.tilt2X,cfg.tilt2Y,cfg.tilt2Z, cfg.neutralX,cfg.neutralY,cfg.neutralZ);
        //Which way the head is actually leaning decides the face; how far along decides whether it
        //counts. Picking on the fraction alone got this wrong: each fraction is measured against
        //its own taught distance, so a short taught direction scores high from a big lean in a
        //roughly similar one, and leaning to one side fired the up face.
        float gSide  = tiltAlign(accelX,accelY,accelZ, cfg.tiltX,cfg.tiltY,cfg.tiltZ,   cfg.neutralX,cfg.neutralY,cfg.neutralZ);
        float gSide2 = tiltAlign(accelX,accelY,accelZ, cfg.tilt2X,cfg.tilt2Y,cfg.tilt2Z, cfg.neutralX,cfg.neutralY,cfg.neutralZ);
        float align = gSide, best = fSide;
        //aTilt/tilt* is the left lean and aTilt2/tilt2* the right, named that way on the page.
        //Which is which is only ever decided by the position you teach each button, so the
        //stored keys keep their old names rather than churning every config file.
        String face = cfg.aTilt, which = F("left");
        if(gSide2 > align) { align = gSide2; best = fSide2; face = cfg.aTilt2; which = F("right"); }
        //Leaning somewhere nothing was taught should do nothing rather than pick the nearest guess
        if(!wasTilt && align >= 0.5f && best >= cfg.tiltTrig) {
          logPrint("[I] Tilt: " + which + " (" + String((int)(best * 100)) + "% of the way)");
          wasTilt = true;
          oldanim = currentAnim;
          tiltChange = millis();
          loadAnim(face,"");
        //Lets go a good way below the trigger so it cannot chatter on the boundary, and still
        //gives up after revertTilt in case the sensor or the taught positions have drifted.
        } else if (wasTilt && (best < cfg.tiltTrig * 0.7f || tiltChange+revertTilt<millis())) {
          logPrint(F("[I] Tilt: back to neutral"));
          wasTilt = false;
          loadAnim(oldanim,"");
        }
      }
    }
    //the retry that used to live here was gated on Tilt being switched on, so a sensor knocked
    //loose stayed lost until it was. It runs unconditionally now, further down with the boop one.
  }

  //looptime = micros();
  //--------------------------------//SPEECH Detection
  if(cfg.speechEna && !prevSpeechEna) {     //just switched on, or booted with it on
    if(micIs("I2S") && !micI2SReady) {       //wired up since the last try?
      micI2SReady = micI2SBegin();
      if(!micI2SReady) logPrint(F("[E] Digital mic did not start; check BCLK, DOUT and LRCL"));
    }
    startMicCal();
  }
  prevSpeechEna = cfg.speechEna;
  if(micCalReq) {
    micCalReq = false;
    if(cfg.speechEna) startMicCal();
  }
  if(micLoudReq) {
    micLoudReq = false;
    if(cfg.speechEna) startMicLoud();
  }
  if(micTestReq) {
    micTestReq = false;
    if(cfg.speechEna) startMicTest();
  }
  if(phraseRequest >= 0) {                 //from the page or a button bound to a phrase
    int slot = phraseRequest;
    phraseRequest = -1;
    showPhrase(slot);
  }
  if(cfg.speechEna) {
    micCollect();

    if(millis() - micWinStart >= MIC_WINDOW_MS && micWinMax > MIC_WIN_HIGH) {
      micWinStart = millis();
      int swing = micWinMax - micWinMin;   //peak to peak: the resting level cancels out of a difference
      micRawMid = (micWinMax + micWinMin) / 2;
      finalMicAvg = micRawMid;             //shown on the page as raw; near zero on a digital mic
      //Analog clipping is the signal hitting the ends of the ADC's range. A digital mic has no gain
      //to misjudge, so there is nothing to clip and nothing to warn about.
      bool clipping = micIs("ANALOG") && (micWinMax >= 4000 || micWinMin <= 60);
      micWinMin = MIC_WIN_LOW;
      micWinMax = MIC_WIN_HIGH;
      if(swing > micSwing) micSwing += (swing - micSwing) * 0.6f;   //rise with the voice
      else micSwing += (swing - micSwing) * 0.25f;                  //fall back a little slower

      if(micCalUntil) {                    //Measure silence: the quietest window is the room's floor
        if(micSwing < micCalFloor) micCalFloor = micSwing;
        if(millis() >= micCalUntil) finishMicCal();
      }
      if(micLoudUntil) {                   //Measure loud: the biggest window while you talk
        if(micSwing > micLoudPeak) micLoudPeak = micSwing;
        if(clipping) micLoudClipped = true;
        if(millis() >= micLoudUntil) finishMicLoud();
      }
      if(micTestPhase == 1) {
        if(micSwing < micTestQuiet) micTestQuiet = micSwing;
        if(millis() >= micTestQuietUntil) { micTestPhase = 2; logPrint(F("[I] Mic test: now talk")); }
      } else if(micTestPhase == 2) {
        if(micSwing > micTestLoud) micTestLoud = micSwing;
        if(millis() >= micTestTalkUntil) {
          micTestPhase = 3;
          logPrint("[I] Mic test: quiet " + String((int)micTestQuiet) + ", talking " + String((int)micTestLoud));
        }
      }

      float above = micSwing - micNoise;
      if(above < 0) above = 0;
      micVolume = constrain((int)(above * 100.0f / (float)max(20, cfg.spMax) + 0.5f), 0, 100);

      if(micVolume > cfg.spTrig) {
        if(!speaking) {
          speaking = true;
          if(oledLegacy()) {
            oled.speak(true);
          }
          logPrint(F("[I] Speak"));
        }
      } else {
        if(speaking) {
          logPrint(F("[I] unSpeak"));
          speaking = false;
          if(oledLegacy()) {
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

  //--------------------------------//Single button: each of short press, double press and hold does
  //whatever Settings says. Defaults are what the button always did: short = next face, hold = displays.
  hwBtn.loop();
  if(hwBtn.isPressed()) {
    btnPressTime = millis();
    btnHeld = true;
    btnLongDone = false;
  }
  if(btnHeld && !btnLongDone && millis()-btnPressTime >= (unsigned long)max(200, cfg.btnHoldMs)) {
    btnLongDone = true;                    //fires while still held down, so it feels immediate
    btnWaitingSingle = false;              //a hold is not the first half of a double press
    doButtonAction(cfg.btnHold, cfg.btnHoldFace, cfg.btnHoldPhrase);
  }
  if(hwBtn.isReleased()) {
    btnHeld = false;
    if(!btnLongDone) {
      //A burst of quick presses is the easter egg. It is counted here rather than made to wait,
      //so every press still does its normal job the instant you let go - the button stays as
      //sharp as it was. The face the burst flicked through is undone when the egg ends, because
      //the face remembered is the one from before the first press of the burst.
      bool eggFired = false;
      if(!dinoActive && !slotActive) {
        if(millis() - eggLastPress > EGG_GAP_MS) { eggPressCount = 0; eggBackTo = currentAnim; }
        eggLastPress = millis();
        eggPressCount++;
        if(cfg.eggAction != "none" && cfg.eggPresses >= 2 && eggPressCount >= cfg.eggPresses) {
          eggPressCount = 0;
          btnWaitingSingle = false;        //whatever this press was for, the egg takes it
          eggFired = true;
          if(cfg.eggAction == "dino") { dinoStartReq = true; logPrint(F("[I] Easter egg: dino")); }
          else slotStart(eggBackTo);
        }
      }
      if(eggFired) {
      } else if(cfg.btnDouble == "none") {
        doButtonAction(cfg.btnShort, cfg.btnShortFace, cfg.btnShortPhrase); //nothing to wait for
      } else if(btnWaitingSingle && millis() - btnFirstRelease <= BTN_DOUBLE_MS) {
        btnWaitingSingle = false;          //second press inside the window
        doButtonAction(cfg.btnDouble, cfg.btnDoubleFace, cfg.btnDoublePhrase);
      } else {
        btnWaitingSingle = true;           //might be the first of two: hold it briefly
        btnFirstRelease = millis();
      }
    }
  }
  //The single press only fires once the double-press window has passed without a second press
  if(btnWaitingSingle && millis() - btnFirstRelease > BTN_DOUBLE_MS) {
    btnWaitingSingle = false;
    doButtonAction(cfg.btnShort, cfg.btnShortFace, cfg.btnShortPhrase);
  }

  //--------------------------------//Look for the boop sensor, Boop switched on or not.
  //setup() starts it either way so the reading can be checked while calibrating, so the retry
  //has to work either way too - otherwise wiring the sensor in does nothing until Boop is
  //turned on, with no hint as to why. The bus is scanned at the same time: if 0x39 answers but
  //the sensor still won't start, the module is a clone the library's ID check rejects.
  if(boopCalRequest) { //from /boopcal: average a handful of readings with nothing near the visor
    boopCalRequest = false;
    if(boopIsApds() && ToFInitDone) {
      long sum = 0; int taken = 0;
      for(int i = 0; i < 16; i++) {
        int v = boopReadProx();
        if(v >= 0) { sum += v; taken++; }
        delay(6);
      }
      if(taken > 0) {
        cfg.boopBase = sum / taken;
        boopFloor = cfg.boopBase;              //start tracking from here
        cfg.save();
        logPrint("[I] Boop resting level set to " + String(cfg.boopBase) + " of 255");
      }
    }
  }

  i2cScanStep();
  if(i2cFresh) { //a sensor that has stopped answering is marked missing, so the retries restart it
    i2cFresh = false;
    if(ToFInitDone && i2cFound.indexOf("0x39") < 0) {
      ToFInitDone = false;
      logPrint("[E] Boop sensor gone from the bus; I2C has: " + i2cFound);
    }
    if(tiltInitDone && i2cFound.indexOf("0x68") < 0 && i2cFound.indexOf("0x69") < 0) {
      tiltInitDone = false;
      logPrint("[E] Tilt sensor gone from the bus; I2C has: " + i2cFound);
    }
  }

  if(!tiltInitDone && lastImuRetry+5000<=millis()) { //plugged back in? pick it up without a restart
    lastImuRetry = millis();
    tiltInitDone = imuBegin();
    if(tiltInitDone) logPrint(F("[I] Tilt sensor back"));
  }

  if(boopIsApds() && !ToFInitDone && lastApdsRetry+5000<=millis()) {
    lastApdsRetry = millis();
    ToFInitDone = boopSensorStart();
    if(ToFInitDone) logPrint("[I] Boop sensor found; I2C: " + i2cFound);
    else logPrint("[E] Boop sensor not starting; I2C has: " + i2cFound);
  }

  //looptime = micros();
  //--------------------------------//BOOP Detection; 14-800uS
  if(lastBoopCheck+BOOP_POLL_MS<=millis() && cfg.boopEna) {
    if(boopIs("KY-032")) {
      digitalWrite(T_en, HIGH);
      delayMicroseconds(210);
      if(booping == false && !digitalRead(T_in)) {
        delayMicroseconds(395);
        if(!digitalRead(T_in)) {
          logPrint(F("[I] IR BOOP"));
          booping = true;
          countBoop();
          boopoldanim = currentAnim;
          loadAnim(boopFaceNow(),"");
          lastMillsBoop = millis();
        }
        digitalWrite(T_en, LOW);
      } else if(booping == true && lastMillsBoop+1000<millis() && digitalRead(T_in)) {
        digitalWrite(T_en, LOW);
        logPrint(F("[I] IR unBOOP"));
        booping = false;
        endBoop();
        if(!wasTilt) {
          loadAnim(boopoldanim,"");
        }
      }
    } else if (boopIs("Capac")) {
      if(booping == false && digitalRead(T_in)) {
        delayMicroseconds(395);
        if(digitalRead(T_in)) {
          logPrint(F("[I] Touch BOOP"));
          booping = true;
          countBoop();
          boopoldanim = currentAnim;
          loadAnim(boopFaceNow(),"");
          lastMillsBoop = millis();
        }
      } else if(booping == true && lastMillsBoop+1000<millis() && !digitalRead(T_in)) {
        logPrint(F("[I] Touch unBOOP"));
        booping = false;
        endBoop();
        if(!wasTilt) {
          loadAnim(boopoldanim,"");
        }
      }
    } else if (boopIsApds()) {
      if(ToFInitDone) {
        //apdsprox is read every BOOP_POLL_MS in the proximity section above.
        //cfg.boopConfirm checks in a row to start a boop and BOOP_RELEASE to end one, so stray
        //readings do neither. Release is a good way under the trigger, so it can't chatter.
        //Release is a good way under the trigger, so it can't chatter at the boundary.
        int sig = boopSignal();
        if(booping == false) {
          boopUnder = 0;
          boopOver = (sig > cfg.boopThresh) ? min(boopOver + 1, cfg.boopConfirm) : 0;
          if(boopOver == 0) boopIgnoreLogged = false;
          //With light to block, it also needs a shadow in the last second. Still over but no shadow
          //stays waiting rather than resetting, so a shadow arriving a moment later still counts.
          bool shadowOk = !cfg.boopLightEna || !boopIs("APDS9930") || lightNormal < LIGHT_MIN || shadeRecent() >= cfg.boopShade;
          if(boopOver >= cfg.boopConfirm && !shadowOk && !boopIgnoreLogged) {
            boopIgnoreLogged = true;
            boopsIgnored++;
            logPrint("[I] Boop ignored: signal " + String(sig) + " but only " + String(shadeRecent()) + "% shade");
          }
          if(boopOver >= cfg.boopConfirm && shadowOk) {
            boopOver = 0;
            boopIgnoreLogged = false;
            logPrint("[I] BOOP (signal " + String(sig) + " over " + String(cfg.boopThresh) + ", " + String(shadeRecent()) + "% shade)");
            booping = true;
            countBoop();
            boopoldanim = currentAnim;
            loadAnim(boopFaceNow(),"");
            lastMillsBoop = millis();
          }
        } else if(lastMillsBoop+1000<millis()) {
          boopUnder = (sig < (cfg.boopThresh * 3) / 5) ? boopUnder + 1 : 0;
          //Still "booped" after 8 s almost always means the resting level moved - a visor shifted, or
          //it warmed up - rather than a real paw. Let go and take this as the new resting level, or
          //it would stay latched and no real boop could ever register.
          bool stuck = millis() - lastMillsBoop > 8000;
          if(boopUnder >= BOOP_RELEASE || stuck) {
            if(stuck) {
              boopFloor = apdsprox;
              logPrint(F("[I] Boop held 8 s: taking that as the new resting level"));
            } else {
              logPrint(F("[I] ToF unBOOP"));
            }
            boopUnder = 0;
            booping = false;
            endBoop();
            if(!wasTilt) {
              loadAnim(boopoldanim,"");
            }
          }
        }
      }
    } else if (boopIs("VL53L1X")) {
      if(ToFInitDone) {
        int16_t distance = -1;
        if (vl53.dataReady()) {
          distance = vl53.distance();
        }
        if(booping == false && distance < cfg.boopThresh && distance != -1) {
          logPrint(F("[I] ToF BOOP"));
          booping = true;
          countBoop();
          boopoldanim = currentAnim;
          loadAnim(boopFaceNow(),"");
          lastMillsBoop = millis();
        } else if(booping == true && lastMillsBoop+1000<millis() && distance > cfg.boopThresh && distance != -1) {
          logPrint(F("[I] ToF unBOOP"));
          booping = false;
          endBoop();
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
  if(oledLegacy() && !slotActive && vaStatLast+1000<millis()) {
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
      oledInitDone = true;               //switched on from the page: fill the screen straight away
      oled.setFlip(cfg.oledFlip);
      screenPrevValid = false;
      if(!screenHasLayout()) {
        oled.speak(false);
        oled.writeSet(1);
        if(currentAnim.endsWith(".json")) oled.writeAnim(currentAnim.substring(0, currentAnim.length() - 5));
      }
    }
  } else if (oledInitDone && !cfg.oledEna) { //switched off from the page
    oled.off();
    oledInitDone = false;
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
        if(transPhase == 0 && wakeFlickPhase == 0) mx.control(MD_MAX72XX::INTENSITY, visorBright()); //a transition or warm-up owns the intensity
        mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
        mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
        FdisplayVisor = false;
        //The startup bar's own frame is in the chips now, so it is safe to light the panels. Held
        //dark until this point so the face that setup draws first is never seen. The timeout is a
        //safety net: if the bar never starts - no maw panels, say - the mask must not stay dark.
        if(bootBarBlanked && (currentAnim == "POSTAnimLoad" || millis() - bootBlankAt > 4000)) {
          bootBarBlanked = false;
          for(int p = 0; p < MATRIXESNUM; p++) mx.control((uint8_t)p, MD_MAX72XX::SHUTDOWN, MD_MAX72XX::OFF);
        }
        if(bootFlickPending) { //the face is in the panels now: warm them up over the top of it
          bootFlickPending = false;
          wakeFlickBegin();
        }
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

  if(boopCountDirty && millis() - boopShownAt > 20000 && millis() - boopCountSaved > 20000) {
    boopCountDirty = false;                 //flash lasts longer if the total is saved once things settle
    boopCountSaved = millis();
    cfg.save();
  }

  if(sleepRequest != 0) { //from /sleep
    setSleeping(sleepRequest > 0);
    sleepRequest = 0;
  }

  if(wakeFlickReq) { //from /wakeflick: the Play button in Settings
    wakeFlickReq = false;
    wakeFlickBegin();
  }

  //Requests from the BLE task, which must not touch the panels or the filesystem itself
  if(bleDisplayReq != 0) {
    displayOn = (bleDisplayReq > 0);
    bleDisplayReq = 0;
    displayDirty = true;
  }
  if(blePhraseReq >= 0) {
    int slot = blePhraseReq;
    blePhraseReq = -1;
    showPhrase(slot);
  }
  if(bleScreenReq != 0) {
    bleScreenReq = 0;
    screenManualPage++;
    screenCycleAt = millis();
    screenPrevValid = false;
  }
  if(bleSaveReq) {
    bleSaveReq = false;
    cfg.save();
  }

  String queuedAnim, queuedPost;
  xSemaphoreTake(animQueueMutex, portMAX_DELAY);
  queuedAnim = animToLoad; animToLoad = "";
  queuedPost = postAnimToLoad; postAnimToLoad = "";
  xSemaphoreGive(animQueueMutex);
  if((queuedAnim != "" || queuedPost != "") && wakeFlickPhase != 0) wakeFlickEnd(); //a face change cuts the warm-up short
  //The startup bar has reached its end and handed over to the face. Arm the warm-up now, so it
  //fires on the face's first drawn frame. Done here rather than in either branch below, because
  //with transitions on the queued face is swallowed by the dip and never reaches the plain load.
  if(bootBarHandover && queuedAnim != "") {
    bootBarHandover = false;
    bootFlickPending = true;
  }
  if(cfg.transEna && visorType == "MAX72XX" && transPhase == 0 && (queuedAnim != "" || queuedPost != "")) {
    transAnim = queuedAnim; transPost = queuedPost; //hold the swap until the panels are dark
    uint8_t levels = visorBright() > 0 ? visorBright() : 1; //the dip time is shared out over the brightness steps there are
    transStepMs = cfg.transMs / levels;
    if(transStepMs < 1) transStepMs = 1;
    transPhase = 1; transLevel = visorBright(); transLastStep = millis();
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
