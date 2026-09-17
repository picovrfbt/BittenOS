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

inline bool boopIs(const char *m) { return strcmp(boopMode, m) == 0; }
inline bool boopIsApds() { return boopIs("APDS9960") || boopIs("APDS9930"); }

//defined further down, but the APDS-9930 driver below needs them first
void logPrint(const char *str);
void logPrint(const String &str);
bool apdsStart();
extern bool booping;
extern bool speaking;
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
//Sleep easing: dim over the last few seconds before dropping off, come back gently rather than
//snapping to full, and after a long enough sleep shut the panels off entirely if that is switched on.
int8_t sleepDimLevel = -1;        //brightness the fade is holding, -1 when the fade is not running
unsigned long sleepFadeStep = 0, sleepDeepAt = 0;
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
    cfg.getString(request, "aUp", cfg.aUp);
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
    cfg.getBool(request, "sleepFadeEna", cfg.sleepFadeEna);
    cfg.getBool(request, "sleepDeepEna", cfg.sleepDeepEna);
    if(cfg.getInt(request, "sleepFadeSecs", cfg.sleepFadeSecs)) cfg.sleepFadeSecs = constrain(cfg.sleepFadeSecs, 0, 30);
    if(cfg.getInt(request, "sleepDeepMins", cfg.sleepDeepMins)) cfg.sleepDeepMins = constrain(cfg.sleepDeepMins, 0, 240);
    if(cfg.getString(request, "blinkMode", cfg.blinkMode)) {
      if(cfg.blinkMode != "face" && cfg.blinkMode != "blink" && cfg.blinkMode != "still") cfg.blinkMode = "face";
      blinkCachedAnim = "";                //look at the face again under the new setting
      instantReload = true;
    }
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
    }
    //Come back gently rather than snapping to full brightness
    if(cfg.sleepFadeEna) sleepDimLevel = 0;
    else sleepDimLevel = -1;
    sleepFadeStep = millis();
    brightnessDirty = true;
  }
  stillSince = millis();
}

//Eases the panels down as sleep approaches and back up on waking, and handles the deeper sleep that
//turns them off. Brightness is only borrowed while this runs: visorBright() is left alone, and the
//normal brightness path takes over again the moment the fade finishes.
void sleepEaseStep() {
  int target = visorBright();
  if(!cfg.sleepFadeEna && sleepDimLevel >= 0) {   //switched off mid-fade
    sleepDimLevel = -1;
    brightnessDirty = true;
  }
  //Falling asleep: dim through the last seconds of stillness, so it fades rather than cuts
  if(!sleeping && cfg.sleepEna && cfg.sleepFadeEna && cfg.sleepFadeSecs > 0 && tiltInitDone) {
    unsigned long still = millis() - stillSince;
    unsigned long sleepAt = (unsigned long)cfg.sleepMins * 60000UL;
    unsigned long fade = (unsigned long)cfg.sleepFadeSecs * 1000UL;
    if(sleepAt > fade && still > sleepAt - fade && still < sleepAt) {
      float gone = (float)(still - (sleepAt - fade)) / (float)fade;   //0 at the start of the fade, 1 at sleep
      int want = (int)(target - gone * target + 0.5f);
      if(want != sleepDimLevel) {
        sleepDimLevel = want;
        if(transPhase == 0 && visorType == "MAX72XX") mx.control(MD_MAX72XX::INTENSITY, max(0, (int)sleepDimLevel));
      }
      return;
    }
  }
  //Waking: climb back to the normal brightness a step at a time
  if(sleepDimLevel >= 0 && !sleeping) {
    if(millis() - sleepFadeStep >= 60) {
      sleepFadeStep = millis();
      sleepDimLevel++;
      if(sleepDimLevel >= target) {
        sleepDimLevel = -1;                //done: hand the panels back
        brightnessDirty = true;
      } else if(transPhase == 0 && visorType == "MAX72XX") {
        mx.control(MD_MAX72XX::INTENSITY, sleepDimLevel);
      }
    }
    return;
  }
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
  pinMode(fanPWM, OUTPUT);

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
String blinkCachedAnim = "";
#define BLINK_STEP_MS 18            //per step of the close and open

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

uint64_t boopDigitsWord(int value, int lift) { //row r, column c sits at bit r*8+c
  uint64_t w = 0;
  bool twoDigits = value >= 10;
  int tens = (value / 10) % 10, units = value % 10;
  int unitsX = twoDigits ? 4 : 3;               //a single digit sits centred instead
  for(int r = 0; r < 5; r++) {
    int y = 1 + r - lift;
    if(y < 0 || y > 7) continue;
    for(int c = 0; c < 3; c++) {
      if(twoDigits && ((boopDigits[tens][r] >> (2 - c)) & 1)) w |= 1ULL << (y * 8 + c);
      if((boopDigits[units][r] >> (2 - c)) & 1) w |= 1ULL << (y * 8 + unitsX + c);
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
    uint64_t w = (bst.kind == 0) ? boopBarWord(bst.amount)
                                 : boopDigitsWord(cfg.boopCount % 100, bst.amount);
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
     && transPhase == 0 && !booping) {    //never blink over a face change or a boop
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
  if(millis() - lastScreenDraw < 400) return;   //a few times a second is plenty, and it shares I2C with the sensors
  lastScreenDraw = millis();
  ScreenData d;
  d.face = currentAnim.endsWith(".json") ? currentAnim.substring(0, currentAnim.length() - 5) : currentAnim;
  d.wifiName = cfg.wifiName;
  d.boops = cfg.boopCount;
  d.wifiClients = WiFi.softAPgetStationNum();
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
  //Pick the page. A page with a condition wins while that condition holds, and keeps the screen for
  //its linger time afterwards, so a boop page does not vanish the instant the paw lifts.
  int page = -1;
  for(int i = 0; i < screenPageCount(); i++) {
    uint8_t show = screenPageShow(i);
    if(show == PAGE_ALWAYS) continue;
    bool on = (show == PAGE_BOOPED && d.booping) || (show == PAGE_NOTBOOPED && !d.booping) ||
              (show == PAGE_SLEEPING && d.sleeping) || (show == PAGE_AWAKE && !d.sleeping) ||
              (show == PAGE_DISPLAYSOFF && !d.displayOn);
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
  screenTick();
  if(brightnessDirty && visorType == "MAX72XX") {
    brightnessDirty = false;
    if(transPhase == 0) mx.control(MD_MAX72XX::INTENSITY, visorBright()); //a running transition owns the intensity
  }
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
  if(visorNow->type == 0 || (visorNow->type == 1 && visorType == "MAX72XX")) { //custom
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
  if(visorNow && (visorNow->type == 0 || (visorNow->type == 1 && visorType == "MAX72XX"))) {
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
        //1,2,3 shut then 3,2,1 open again, held one step at the bottom
        wantBlink = (blinkStep < 4) ? blinkStep + 1 : blinkStep - 1;
        if(wantBlink > 6) { wantBlink = 0; nextBlinkAt = millis() + random(2500, 7000); }
      }
    } else if(blinkStep != 0) {
      wantBlink = 0;                       //mode changed or the face took over: open the eyes again
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
  //--------------------------------//I2C sensors: read every 100 ms on the loop task; the web pages use these cached values
  if(lastMillsTilt+100<=millis()) {
    lastMillsTilt = millis();
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
        if(!booping) {
          if(apdsprox < boopFloor) boopFloor += (apdsprox - boopFloor) * 0.3f;
          else boopFloor += (apdsprox - boopFloor) * 0.01f;
        }
      }
    }
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
      if(cfg.btnDouble == "none") {
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
  if(lastBoopCheck+100<=millis() && cfg.boopEna) {
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
        //apdsprox is read every 100 ms in the I2C sensor section
        //Three checks in a row (about 300 ms) to start a boop and two to end one, so stray readings do neither.
        //Release is a good way under the trigger, so it can't chatter at the boundary.
        int sig = boopSignal();
        if(booping == false) {
          boopUnder = 0;
          boopOver = (sig > cfg.boopThresh) ? min(boopOver + 1, 3) : 0;
          if(boopOver == 0) boopIgnoreLogged = false;
          //With light to block, it also needs a shadow in the last second. Still over but no shadow
          //stays waiting rather than resetting, so a shadow arriving a moment later still counts.
          bool shadowOk = !cfg.boopLightEna || !boopIs("APDS9930") || lightNormal < LIGHT_MIN || shadeRecent() >= cfg.boopShade;
          if(boopOver >= 3 && !shadowOk && !boopIgnoreLogged) {
            boopIgnoreLogged = true;
            boopsIgnored++;
            logPrint("[I] Boop ignored: signal " + String(sig) + " but only " + String(shadeRecent()) + "% shade");
          }
          if(boopOver >= 3 && shadowOk) {
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
          if(boopUnder >= 2 || stuck) {
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
  if(oledLegacy() && vaStatLast+1000<millis()) {
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
        if(transPhase == 0) mx.control(MD_MAX72XX::INTENSITY, visorBright()); //a running transition owns the intensity
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

  if(boopCountDirty && millis() - boopShownAt > 20000 && millis() - boopCountSaved > 20000) {
    boopCountDirty = false;                 //flash lasts longer if the total is saved once things settle
    boopCountSaved = millis();
    cfg.save();
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
