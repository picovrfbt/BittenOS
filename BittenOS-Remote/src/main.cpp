//Button remote for the protogen mask. A Seeed XIAO ESP32C6 with a few momentary buttons, sitting
//in deep sleep until one is pressed. On a press it wakes, sends one ESP-NOW packet to the mask and
//goes straight back to sleep. There is no power switch: deep sleep costs about 15uA, so a small
//cell lasts months and the remote is always armed.
//
//Why ESP-NOW rather than BLE: no pairing dance with the phone, no connection to maintain, and no
//need for the mask to act as a BLE host.
//
//The remote is paired to one mask and talks only to it, addressed by MAC. That is worth the
//pairing step for one reason above all: a unicast ESP-NOW packet is acknowledged and a broadcast
//is not. With an acknowledgement the remote can tell a delivered press from a lost one, which is
//what lets it notice the mask has moved channel and go and find it again. Broadcasting, the send
//callback reports success whether or not anything heard it, so there is nothing to react to.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
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

//Pairing is all three buttons held together. It used to be one button held through a power-up,
//which assumed the battery could be reached - untrue the moment the case is closed, and there
//is no way to power-cycle a sealed remote. Three at once is a thing no ordinary use produces,
//and it works whenever the remote is in hand.
#define PAIR_HOLD_MS 3000     //all three held this long starts pairing

#define LED_PIN 15            //the XIAO's own LED, active low
#define LED_ACTIVE LOW

//--------------------------------//The protocol
//Shared with the mask. Anything without the magic is not ours and is dropped on sight.
#define REMOTE_MAGIC 0x50524F54UL     //'PROT'
#define REMOTE_PROTO 3                //3 added the gestures byte. 2 was press and hold only.

enum : uint8_t { MSG_PRESS = 0, MSG_PAIR_REQ = 1, MSG_PAIR_ACK = 2, MSG_INFO = 3 };
enum : uint8_t { EV_PRESS = 0, EV_HOLD = 1, EV_DOUBLE = 2 };

struct __attribute__((packed)) RemotePacket {
  uint32_t magic;
  uint8_t  proto;
  uint8_t  type;        //MSG_*
  uint8_t  button;      //index into BUTTON_PIN
  uint8_t  event;       //EV_*
  uint8_t  battPct;     //0-100, or 255 when unknown
  uint8_t  channel;     //only meaningful in MSG_PAIR_ACK: the channel the mask is sitting on
  //Sent by the mask, in MSG_PAIR_ACK and MSG_INFO: bits 0-2 say button 0-2 has a double tap
  //action set, bits 3-5 say it has a hold action. See handlePress() for why the remote needs to
  //be told rather than simply waiting.
  uint8_t  gestures;
  uint32_t seq;         //counts presses, so a repeat can be spotted
};

//--------------------------------//Timings
#define DEBOUNCE_MS       15  //a press has to still be down this long to count
#define CATCH_MS          40  //how long after waking to keep looking for the pin that did it
#define HOLD_MS         1200  //still down this long and a second packet goes out as a hold
#define DOUBLE_MS        260  //a second tap inside this is a double tap, not two presses
#define ACK_WAIT_MS       60  //how long to wait for the mask to acknowledge a packet
#define LINGER_MS       4000  //stay awake this long after a press, in case another follows
#define CHANNEL_MAX       13  //channels to search; ones the region disallows are skipped
#define PAIR_DWELL_MS    140  //how long to listen on each channel while pairing
#define PAIR_ROUNDS        3  //sweeps of every channel before giving up on pairing
#define PAIR_POLL_MS      20  //how often the all-three check looks at the buttons
#define RELEASE_MAX_MS 30000  //give up waiting for a release after this and treat it as stuck
#define STUCK_RECHECK_S    2  //while a button is stuck, wake on a timer to see if it has let go

//--------------------------------//State that has to survive deep sleep
//RTC memory keeps its contents through deep sleep but not through a flat battery, so it holds only
//what is cheap to lose. The pairing itself lives in NVS, which survives everything short of an
//erase, because losing it would mean opening the case to pair again.
RTC_DATA_ATTR uint32_t rtcSeq = 0;
RTC_DATA_ATTR uint8_t rtcChannel = 0;     //last channel the mask answered on; 0 = not known
//Which gestures the mask has configured, as the mask last told us. In RTC memory because it is
//needed on the very first press after waking, and mirrored into NVS because losing it would mean
//a button behaving differently until the next press taught it again.
RTC_DATA_ATTR uint8_t rtcGestures = 0;
static volatile bool gestDirty = false;   //a fresh gestures byte arrived: consider saving it

static unsigned long lastActivity = 0;    //when the last press was dealt with
//Which buttons were already down last time round. handlePress() can return with the button
//still held - that is what sending a hold means - so without this the same finger would be
//read as a fresh press over and over for as long as it stayed there.
static bool wasDown[BUTTON_COUNT] = {false};

static Preferences prefs;
static uint8_t maskMac[6];
static bool havePair = false;

static volatile bool sendDone = false, sendOk = false;
static volatile bool gotPairAck = false;
static uint8_t pairMac[6];
static uint8_t pairChannel = 0;

static void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  sendOk = (status == ESP_NOW_SEND_SUCCESS);
  sendDone = true;
}

static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if(len != (int)sizeof(RemotePacket)) return;
  RemotePacket p;
  memcpy(&p, data, sizeof(p));
  if(p.magic != REMOTE_MAGIC || p.proto != REMOTE_PROTO) return;
  //The mask answers every press with this, so a button whose double tap was just set in the web
  //page behaves as a plain press for one more press and is right from then on.
  if(p.type == MSG_INFO) {
    if(p.gestures != rtcGestures) { rtcGestures = p.gestures; gestDirty = true; }
    return;
  }
  if(p.type != MSG_PAIR_ACK) return;
  memcpy(pairMac, info->src_addr, 6);
  pairChannel = p.channel;
  if(p.gestures != rtcGestures) { rtcGestures = p.gestures; gestDirty = true; }
  gotPairAck = true;
}

//--------------------------------//Buttons
static inline bool pressed(int i) { return digitalRead(BUTTON_PIN[i]) == LOW; }

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

static bool allDown() {
  for(int i = 0; i < BUTTON_COUNT; i++) if(!pressed(i)) return false;
  return true;
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

//--------------------------------//Pairing, stored in NVS
static bool loadPair() {
  prefs.begin("remote", true);
  size_t n = prefs.getBytes("mac", maskMac, 6);
  if(rtcChannel == 0) rtcChannel = prefs.getUChar("ch", 0);
  //0x3F = assume every button has both a hold and a double tap. Only until the mask says
  //otherwise, which it does on the first press: being 260 ms slow once is a far smaller fault
  //than sending a hold as a plain press because nothing had said a hold existed yet.
  if(rtcGestures == 0) rtcGestures = prefs.getUChar("gest", 0x3F);
  prefs.end();
  havePair = (n == 6);
  return havePair;
}

static void savePair(const uint8_t *mac, uint8_t channel) {
  prefs.begin("remote", false);
  prefs.putBytes("mac", mac, 6);
  prefs.putUChar("ch", channel);
  prefs.end();
  memcpy(maskMac, mac, 6);
  rtcChannel = channel;
  havePair = true;
}

//Written back only when the byte actually changed, and never from the radio callback, for the same
//wear reason as the channel: a press happens far more often than a setting does.
static void saveGestures() {
  if(!gestDirty) return;
  gestDirty = false;
  prefs.begin("remote", false);
  if(prefs.getUChar("gest", 255) != rtcGestures) prefs.putUChar("gest", rtcGestures);
  prefs.end();
}

//The channel is written back whenever it changes, not only at pairing, so a mask that has moved
//stays found after the remote's battery is pulled. Only on a change: NVS writes are wear-limited
//and a press happens far more often than a channel does.
static void rememberChannel(uint8_t channel) {
  if(channel == rtcChannel) return;
  rtcChannel = channel;
  prefs.begin("remote", false);
  prefs.putUChar("ch", channel);
  prefs.end();
}

//--------------------------------//Radio
static bool radioUp() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if(esp_now_init() != ESP_OK) return false;
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);
  return true;
}

static bool addPeer(const uint8_t *mac, uint8_t channel) {
  esp_now_del_peer(mac);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = channel;     //0 means "whatever channel the radio is on"
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

static bool sendTo(const uint8_t *mac, RemotePacket &p) {
  sendDone = false; sendOk = false;
  if(esp_now_send(mac, (uint8_t *)&p, sizeof(p)) != ESP_OK) return false;
  unsigned long until = millis() + ACK_WAIT_MS;
  while(!sendDone && millis() < until) delay(1);
  return sendOk;
}

static RemotePacket makePacket(uint8_t type, uint8_t button, uint8_t event, bool bumpSeq) {
  RemotePacket p;
  p.magic = REMOTE_MAGIC;
  p.proto = REMOTE_PROTO;
  p.type = type;
  p.button = button;
  p.event = event;
  p.battPct = 255;            //no battery divider wired on this board yet
  p.channel = 0;
  p.gestures = 0;             //the mask sends this one, not us
  p.seq = bumpSeq ? ++rtcSeq : rtcSeq;
  return p;
}

//Sends one press to the paired mask. Tries the channel it was last heard on, and only if that goes
//unacknowledged does it search - stopping the moment a channel answers, and writing that one down.
//The search is the whole point of pairing: it works only because unicast is acknowledged, where a
//broadcast never is.
static bool sendPress(uint8_t button, uint8_t event) {
  RemotePacket p = makePacket(MSG_PRESS, button, event, true);

  if(rtcChannel) {
    esp_wifi_set_channel(rtcChannel, WIFI_SECOND_CHAN_NONE);
    if(addPeer(maskMac, rtcChannel) && sendTo(maskMac, p)) return true;
  }

  for(uint8_t ch = 1; ch <= CHANNEL_MAX; ch++) {
    if(ch == rtcChannel) continue;                                  //already tried
    if(esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) continue;
    if(!addPeer(maskMac, ch)) continue;
    if(sendTo(maskMac, p)) { rememberChannel(ch); return true; }
  }
  return false;
}

//Pairing: the one time a broadcast is the right tool, because the remote does not yet know who it
//is looking for. It calls on every channel and waits a moment on each for a mask that has been put
//into pairing mode to answer. The reply tells it the mask's MAC and the channel it is sitting on.
static bool runPairing() {
  uint8_t bcast[6];
  memset(bcast, 0xFF, 6);
  RemotePacket p = makePacket(MSG_PAIR_REQ, 0, 0, false);

  for(int round = 0; round < PAIR_ROUNDS; round++) {
    for(uint8_t ch = 1; ch <= CHANNEL_MAX; ch++) {
      if(esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) continue;
      if(!addPeer(bcast, 0)) continue;
      esp_now_send(bcast, (uint8_t *)&p, sizeof(p));
      unsigned long until = millis() + PAIR_DWELL_MS;
      while(millis() < until) {
        if(gotPairAck) {
          savePair(pairMac, pairChannel ? pairChannel : ch);
          return true;
        }
        delay(2);
      }
    }
  }
  return false;
}

//One press, from send to release. Called for the press that woke the chip and for any that follow
//while it is still awake.
//
//Nothing goes out on the way down when there is more than one gesture in play. Press, hold and
//double tap are three different actions on the mask, so sending the press first and correcting
//afterwards would run two of them - holding the bottom button used to start the slot machine and
//then the dino game on top of it.
//
//Waiting costs latency, so it is only paid where it buys something. The mask sends over which
//gestures it actually has configured, and a button with nothing but a plain press on it fires the
//instant it goes down. That is the same bargain the mask's own button strikes with cfg.btnDouble:
//off means a single press acts the moment you let go.
static void sendEvent(int b, uint8_t event) {
  blink(sendPress((uint8_t)b, event) ? 1 : 3);
  lastActivity = millis();
}

static void handlePress(int b) {
  bool hasHold   = (rtcGestures >> (b + 3)) & 1;
  bool hasDouble = (rtcGestures >> b) & 1;

  if(!hasHold && !hasDouble) { sendEvent(b, EV_PRESS); return; }   //nothing to tell it apart from

  //Wait for the release, but no longer than a hold takes to declare itself.
  unsigned long start = millis();
  while(pressed(b) && millis() - start < HOLD_MS) delay(10);

  if(pressed(b)) {
    //Still down. A hold goes out now rather than on release, so it lands while the finger is
    //still there; with no hold configured a long press is simply a press, and no second tap can
    //be coming while the button is held.
    sendEvent(b, hasHold ? EV_HOLD : EV_PRESS);
    return;
  }

  if(hasDouble) {
    unsigned long until = millis() + DOUBLE_MS;
    while(millis() < until) {
      if(pressed(b)) {
        delay(DEBOUNCE_MS);
        if(!pressed(b)) continue;                 //bounce on the way up, not a second tap
        sendEvent(b, EV_DOUBLE);
        return;
      }
      delay(5);
    }
  }
  sendEvent(b, EV_PRESS);
}

//All three held for three seconds. Returns true if pairing was attempted, so the caller knows to
//stop rather than treat the buttons as an ordinary press.
static bool maybePair() {
  if(!allDown()) return false;
  unsigned long until = millis() + PAIR_HOLD_MS;
  while(millis() < until) {
    if(!allDown()) return false;              //let go early: not a pairing request after all
    delay(PAIR_POLL_MS);
  }
  blink(2);                                   //two: listening for a mask
  if(radioUp() && runPairing()) blink(5);     //five: paired
  else blink(3);                              //three: nothing answered
  lastActivity = millis();
  return true;
}

//--------------------------------//Sleep
//Arming a wake on a pin that is still held down wakes the chip again the instant it sleeps, which
//would spin until the battery died. So a held button is waited out first, and if it never lets go
//it is left out of the wake mask and a short timer wake is added instead, so the remote comes back
//to itself once the button finally clears rather than staying dead.
static void sleepNow() {
  saveGestures();                       //RTC memory survives sleep, a flat battery does not
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
  loadPair();

  //Powered up rather than woken: a battery change or a reflash. Nothing to send, so say whether
  //there is a mask to send it to and go back to sleep.
  if(cause != ESP_SLEEP_WAKEUP_GPIO && cause != ESP_SLEEP_WAKEUP_TIMER) {
    rtcSeq = 0;
    blink(havePair ? 1 : 3);                    //one: armed. three: not paired to anything yet
    sleepNow();
  }

  //Woken on the stuck-button timer: nothing to report, just try to re-arm.
  if(cause == ESP_SLEEP_WAKEUP_TIMER) sleepNow();

  int b = whichButton();
  if(b < 0) sleepNow();                     //woken by a glitch, or the tap ended before we looked

  delay(DEBOUNCE_MS);
  if(!pressed(b)) sleepNow();               //bounce rather than a press

  //Three at once is a pairing request, not a press. Checked before havePair, since pairing is the
  //one thing that has to work when nothing is paired yet.
  if(maybePair()) { for(int i = 0; i < BUTTON_COUNT; i++) wasDown[i] = true; return; }

  if(!havePair) { blink(3); sleepNow(); }   //nothing to talk to until it has been paired
  if(!radioUp()) { blink(3); sleepNow(); }

  wasDown[b] = true;                      //already dealt with, so loop() must not read it again
  handlePress(b);
  //Falls through to loop(), which keeps the radio up a few seconds in case another press follows.
}

//Awake for a few seconds after a press rather than sleeping straight away. Bringing the radio up
//is most of the ~170ms a press costs from cold, so a second press inside the window lands in about
//ten - the difference between flicking through faces and waiting for each one.
//
//The cost is the radio idling at a few tens of milliamps for those seconds against 15uA asleep,
//and it is only paid while the remote is actually in someone's hand.
void loop() {
  if(maybePair()) {                         //all three grabbed while it was still awake
    for(int i = 0; i < BUTTON_COUNT; i++) wasDown[i] = true;
    return;
  }
  for(int i = 0; i < BUTTON_COUNT; i++) {
    bool down = pressed(i);
    if(down && !wasDown[i]) {
      delay(DEBOUNCE_MS);
      if(!pressed(i)) continue;           //bounce, not a press
      wasDown[i] = true;
      handlePress(i);
    } else if(!down) {
      wasDown[i] = false;
    }
  }
  saveGestures();
  if(millis() - lastActivity > LINGER_MS) sleepNow();
  delay(5);
}
