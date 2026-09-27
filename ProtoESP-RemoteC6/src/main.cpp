//Button remote for the protogen mask. A Seeed XIAO ESP32C6 with a few momentary buttons, sitting
//in deep sleep until one is pressed. On a press it wakes, sends a single ESP-NOW packet to the
//mask and goes straight back to sleep. There is no power switch: deep sleep costs about 15uA, so
//a small cell lasts months and the remote is always armed.
//
//Why ESP-NOW rather than BLE: no pairing, no connection to maintain, and no need for the mask to
//act as a BLE host. The cost is that both ends must sit on the same WiFi channel, which is the
//fiddly part - see findMaskChannel() below.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "esp_sleep.h"
#include "driver/gpio.h"

//--------------------------------//Wiring
//ONLY GPIO 0-7 can wake an ESP32-C6 from deep sleep, and of those the XIAO brings out just D0, D1
//and D2 on its header (D3 upward are GPIO 16-23, which cannot wake it). So three buttons is the
//limit without soldering to the JTAG pads on the underside.
//
//Each button is a plain two-pin momentary: one leg to the pin, the other to GND. The pin is held
//high by a pull-up and a press pulls it low.
#define BUTTON_COUNT 3
static const gpio_num_t BUTTON_PIN[BUTTON_COUNT] = {
  GPIO_NUM_0,   //XIAO D0
  GPIO_NUM_1,   //XIAO D1
  GPIO_NUM_2,   //XIAO D2
};

#define LED_PIN 15            //the XIAO's own LED, active low
#define LED_ACTIVE LOW

//--------------------------------//The mask
//Its access point, used only to find which channel it is on. Change if the mask's WiFi is renamed.
#define MASK_SSID "ProtoWiFi"

//Sent broadcast rather than to the mask's MAC, so swapping the mask onto a different board does
//not mean reflashing the remote. The mask ignores anything without this magic in it.
#define REMOTE_MAGIC 0x50524F54UL     //'PROT'
#define REMOTE_PROTO 1

//Events a button can produce. The mask's own button already knows short from hold, so the remote
//speaks the same vocabulary and the two can share one action table.
enum : uint8_t { EV_PRESS = 0, EV_HOLD = 1 };

struct __attribute__((packed)) RemotePacket {
  uint32_t magic;
  uint8_t  proto;
  uint8_t  button;      //index into BUTTON_PIN
  uint8_t  event;       //EV_PRESS or EV_HOLD
  uint8_t  battPct;     //0-100, or 255 when unknown
  uint32_t seq;         //counts presses for the life of the battery, so repeats can be spotted
};

//--------------------------------//Timings
#define DEBOUNCE_MS     15    //a press has to still be down this long to count
#define CATCH_MS        40    //how long after waking to keep looking for the pin that did it
#define HOLD_MS       1200    //still down this long and a second packet goes out as a hold
#define SEND_WAIT_MS   120    //how long to wait for the radio to report the packet sent
#define RELEASE_MAX_MS 30000  //give up waiting for a release after this and treat it as stuck
#define STUCK_RECHECK_S    2  //while a button is stuck, wake on a timer to see if it has let go

//--------------------------------//State that has to survive deep sleep
//RTC memory keeps its contents through deep sleep, which is what makes the channel cache worth
//having: finding the mask costs a WiFi scan of a second or more, and doing that on every press
//would be both slow and the largest single draw on the battery.
RTC_DATA_ATTR uint8_t rtcChannel = 0;     //0 = not known yet
RTC_DATA_ATTR uint32_t rtcSeq = 0;
RTC_DATA_ATTR bool rtcScannedThisBattery = false;

static volatile bool sendDone = false, sendOk = false;

static void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  sendOk = (status == ESP_NOW_SEND_SUCCESS);
  sendDone = true;
}

//--------------------------------//Buttons
static inline bool pressed(int i) { return digitalRead(BUTTON_PIN[i]) == LOW; }

static uint64_t buttonMask() {
  uint64_t m = 0;
  for(int i = 0; i < BUTTON_COUNT; i++) m |= 1ULL << (int)BUTTON_PIN[i];
  return m;
}

static void armButtons() {
  for(int i = 0; i < BUTTON_COUNT; i++) pinMode(BUTTON_PIN[i], INPUT_PULLUP);
}

//Latching the pad configuration so the pull-ups cannot drift while the chip is asleep. Done only
//on the way into sleep, never earlier, because a held pad is a poor thing to try to read. The C6
//has no gpio_deep_sleep_hold_en(): that belongs to the parts with a separate RTC GPIO domain, and
//on this chip the wake-capable pins stay in a powered domain anyway.
static void holdButtons(uint64_t mask) {
  for(int i = 0; i < BUTTON_COUNT; i++)
    if(mask & (1ULL << (int)BUTTON_PIN[i])) gpio_hold_en(BUTTON_PIN[i]);
}

//Which button is down. Returns -1 if none, after looking for a short while: a quick tap can be
//over before the chip has finished waking, and the press should not be lost because of that.
static int whichButton() {
  unsigned long until = millis() + CATCH_MS;
  do {
    for(int i = 0; i < BUTTON_COUNT; i++) if(pressed(i)) return i;
  } while(millis() < until);
  return -1;
}

//--------------------------------//Radio
//The mask's access point is the only way to know what channel to talk on, and it moves: with
//"Join a network" switched on, the mask's AP follows the channel of whatever hotspot it joined.
//So the channel is found by scanning, then cached in RTC memory and only looked for again when a
//send fails - which is exactly what a channel change looks like from here.
static uint8_t findMaskChannel() {
  int n = WiFi.scanNetworks(false, true, false, 120);
  uint8_t found = 0;
  for(int i = 0; i < n; i++) {
    if(WiFi.SSID(i) == MASK_SSID) { found = WiFi.channel(i); break; }
  }
  WiFi.scanDelete();
  return found;
}

static bool radioUp(uint8_t channel) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if(channel > 0) esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  if(esp_now_init() != ESP_OK) return false;
  esp_now_register_send_cb(onSent);
  esp_now_peer_info_t peer = {};
  memset(peer.peer_addr, 0xFF, 6);        //broadcast: no MAC to keep in step with the mask
  peer.channel = channel;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
  return true;
}

static bool sendPacket(uint8_t button, uint8_t event) {
  RemotePacket p;
  p.magic = REMOTE_MAGIC;
  p.proto = REMOTE_PROTO;
  p.button = button;
  p.event = event;
  p.battPct = 255;                        //no battery divider wired on this board yet
  p.seq = ++rtcSeq;
  uint8_t bcast[6]; memset(bcast, 0xFF, 6);
  sendDone = false; sendOk = false;
  if(esp_now_send(bcast, (uint8_t *)&p, sizeof(p)) != ESP_OK) return false;
  unsigned long until = millis() + SEND_WAIT_MS;
  while(!sendDone && millis() < until) delay(1);
  return sendOk;
}

//Sends, and if that fails re-finds the channel once and tries again. A failure here almost always
//means the mask moved channel rather than that it is switched off, and one rescan is far cheaper
//than leaving the remote dead until it is reflashed.
static bool sendWithRetry(uint8_t button, uint8_t event) {
  if(rtcChannel == 0) {
    WiFi.mode(WIFI_STA);
    rtcChannel = findMaskChannel();
    rtcScannedThisBattery = true;
  }
  if(!radioUp(rtcChannel)) return false;
  if(sendPacket(button, event)) return true;

  uint8_t again = findMaskChannel();
  if(again == 0 || again == rtcChannel) return false;   //not a channel problem: give up quietly
  rtcChannel = again;
  esp_now_deinit();
  if(!radioUp(rtcChannel)) return false;
  return sendPacket(button, event);
}

static void blink(int times) {
  pinMode(LED_PIN, OUTPUT);
  for(int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LED_ACTIVE);
    delay(18);
    digitalWrite(LED_PIN, !LED_ACTIVE);
    if(i + 1 < times) delay(80);
  }
  pinMode(LED_PIN, INPUT);                //back to high impedance so it costs nothing asleep
}

//--------------------------------//Sleep
//Arming a wake on a pin that is still held down wakes the chip again the instant it sleeps, which
//would spin until the battery died. So a held button is waited out first, and if it never lets go
//it is left out of the wake mask and a short timer wake is added instead, so the remote comes back
//to itself once the button finally clears rather than staying dead.
static void sleepNow() {
  unsigned long until = millis() + RELEASE_MAX_MS;
  bool anyDown = true;
  while(anyDown && millis() < until) {
    anyDown = false;
    for(int i = 0; i < BUTTON_COUNT; i++) if(pressed(i)) anyDown = true;
    if(anyDown) delay(20);
  }

  uint64_t mask = 0;
  bool stuck = false;
  for(int i = 0; i < BUTTON_COUNT; i++) {
    if(pressed(i)) { stuck = true; continue; }           //still down: do not arm this one
    mask |= 1ULL << (int)BUTTON_PIN[i];
  }
  if(mask) {
    esp_deep_sleep_enable_gpio_wakeup(mask, ESP_GPIO_WAKEUP_GPIO_LOW);
    holdButtons(mask);
  }
  if(stuck) esp_sleep_enable_timer_wakeup((uint64_t)STUCK_RECHECK_S * 1000000ULL);

  esp_deep_sleep_start();
}

//--------------------------------//
void setup() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  //Holds from the last sleep have to be released before the pins can be read again.
  for(int i = 0; i < BUTTON_COUNT; i++) gpio_hold_dis(BUTTON_PIN[i]);
  armButtons();

  //A fresh battery or a reflash: nothing to send, just arm and sleep. Two blinks say it is alive,
  //which is the only feedback this thing has.
  if(cause != ESP_SLEEP_WAKEUP_GPIO && cause != ESP_SLEEP_WAKEUP_TIMER) {
    rtcChannel = 0;
    rtcSeq = 0;
    rtcScannedThisBattery = false;
    blink(2);
    sleepNow();
  }

  //Woken on the stuck-button timer: nothing to report, just try to re-arm.
  if(cause == ESP_SLEEP_WAKEUP_TIMER) sleepNow();

  int b = whichButton();
  if(b < 0) sleepNow();                   //woken by a glitch, or the tap was over before we looked

  delay(DEBOUNCE_MS);
  if(!pressed(b)) sleepNow();             //bounce rather than a press

  bool ok = sendWithRetry((uint8_t)b, EV_PRESS);
  blink(ok ? 1 : 3);                      //one blink sent, three could not reach the mask

  //Still down after a moment: send the hold as well, so the mask can treat it like its own
  //button's hold action rather than the remote having to know what a hold means.
  unsigned long holdAt = millis() + HOLD_MS;
  while(millis() < holdAt) {
    if(!pressed(b)) break;
    delay(10);
  }
  if(pressed(b)) {
    sendWithRetry((uint8_t)b, EV_HOLD);
    blink(1);
  }

  sleepNow();
}

void loop() { }   //never runs: setup() always ends in deep sleep
