# Button remote (XIAO ESP32C6, ESP-NOW)

A few momentary buttons on a Seeed XIAO ESP32C6 that sit in deep sleep until pressed, send one
ESP-NOW packet to the mask, and go back to sleep. **No power switch** — deep sleep is about 15 µA,
so it can be left armed indefinitely and a small LiPo lasts months.

## Wiring

Each button is a plain two-pin momentary: **one leg to the pin, the other to GND**. No resistors
needed to start with — the internal pull-ups are enabled and latched before sleep.

| Button | XIAO pin | GPIO |
|---|---|---|
| 0 | D0 | 0 |
| 1 | D1 | 1 |
| 2 | D2 | 2 |

**Three buttons is the limit on the header.** Only GPIO 0–7 can wake an ESP32-C6 from deep sleep,
and of those the XIAO brings out just D0, D1 and D2 — D3 upward are GPIO 16–23 and cannot wake it.
More would mean soldering to the JTAG pads underneath (GPIO 4, 5, 6, 7).

Change `BUTTON_PIN[]` and `BUTTON_COUNT` at the top of `src/main.cpp` if you wire them differently.
Any pin you use has to be in the 0–7 range or the remote will simply never wake.

**If you ever see phantom wake-ups**, add a 10 kΩ resistor from each button pin to 3V3. The code
holds the pads before sleeping so the internal pull-ups should not drift, but an external pull-up
removes the question entirely.

## What it does

| | |
|---|---|
| Press, from sleep | wakes and sends, **one blink** — about 170 ms |
| Press, within 4 s of the last | sends straight away — about 10 ms |
| Still held after 1.2 s | sends `EV_HOLD` **instead of** a press |
| Two taps inside 260 ms | sends `EV_DOUBLE`, and no press |
| All three held for 3 s | pairing, not a press |
| Not paired, or couldn't reach the mask | **three blinks** |
| Powered up, paired and armed | **one blink** |
| Pairing: listening / paired | **two blinks** / **five blinks** |
| Power-on or reflash | **two blinks**, then arms and sleeps without sending |

Each press sends exactly one packet. Hold and double tap replace the press rather than arriving on
top of it — see below for why that matters.

One blink now genuinely means **the mask acknowledged it**, not merely that the packet was
transmitted. That's the practical gain from pairing: with a broadcast there was no way to tell.

**It stays awake for four seconds after a press.** Bringing the radio up is most of the ~170 ms a
press costs from cold, so a second press inside that window lands in about **ten** — the
difference between flicking through faces and waiting for each one. The window restarts on every
press, so a run of them stays quick throughout.

The cost is the radio idling at a few tens of milliamps for those seconds, against 15 uA asleep,
and it is only paid while the remote is actually in someone's hand. `LINGER_MS` trades
responsiveness against standby.

### Press, hold and double tap

The remote has to decide which of the three a press was **before** it transmits, because each one
is a different action on the mask — sending a press first and correcting afterwards would run two
of them. Holding the bottom button used to start the slot machine and then drop the dino game on
top of it.

Deciding means waiting, and waiting costs latency, so it is only paid where it buys something. The
mask sends back which gestures are actually configured, in the `gestures` byte, and the remote
caches it in RTC memory and NVS:

| Button has | A single tap is sent |
|---|---|
| nothing but a plain press | **on the way down**, about 0 ms |
| a hold | on release |
| a double tap | on release, plus the 260 ms `DOUBLE_MS` window |

That is the same bargain the mask's own button strikes with `cfg.btnDouble`: off means a single
press acts the moment you let go. A hold still goes out at `HOLD_MS` while the finger is still
there, so it lands immediately rather than on release.

The mask answers **every** press with the gestures byte, so a double tap set in the web page is
learned on the next press — the first press after the change still behaves the old way. A remote
with nothing stored yet assumes every gesture exists, since being 260 ms slow once is a smaller
fault than sending a hold as a plain press.

Presses are **edge triggered**: `handlePress()` returns with the button still down once it has sent
a hold, so polling for a low pin would read the same finger as a fresh press over and over.

A held button is waited out before sleeping — arming a wake on a pin that is already low would
wake the chip the instant it slept and spin until the battery died. If a button never releases it
is left out of the wake mask and a 2 s timer wake is armed instead, so the remote recovers by
itself once the button clears rather than staying dead.

## Pairing

The remote talks to **one mask**, addressed by its MAC. Nothing else hears it and it hears nothing
else.

**To pair:** open **Settings > Button remote > Pair** on the mask, then hold **all three buttons**
on the remote together for **three seconds**. The remote blinks twice, calls on every channel for a
few seconds, and blinks **five times** when the mask answers. Three blinks means nothing replied.
The mask's window stays open for 30 seconds.

It used to be one button held through a power-up. That assumed the battery could be reached, which
stopped being true the moment the case was closed — there is no way to power cycle a sealed
remote. Three buttons at once is something ordinary use never produces, and it works whenever the
remote is in your hand, awake or asleep.

The mask's MAC and channel are kept in NVS, so they survive a flat battery and a reflash. Pairing
is only needed once, or again after you move the mask to a different board.

## The channel problem

ESP-NOW needs both ends on the same channel, and the mask's moves on its own — with *Join a
network* on, its access point follows whichever hotspot it joined.

Pairing is what makes this solvable. **A unicast ESP-NOW packet is acknowledged; a broadcast is
not.** So the remote can tell a delivered press from a lost one:

1. Send to the mask on the channel it last answered on — one packet, about 10 ms
2. No acknowledgement? Walk the channels, stopping the moment one is acknowledged
3. Write that channel down, in RTC memory and in NVS, so the next press goes straight there

A normal press is a single send. The search only happens after the mask has actually moved, and it
fixes itself without you touching anything.

This is also why the remote no longer needs to know the mask's **WiFi name**. An earlier version
scanned for `ProtoWiFi` to find the channel, which tied the remote to a setting on the mask —
rename the WiFi and the remote went deaf until reflashed. Nothing here names the mask now.

## The packet

```c
struct __attribute__((packed)) RemotePacket {
  uint32_t magic;    // 0x50524F54 ('PROT')
  uint8_t  proto;    // 3
  uint8_t  type;     // 0 press, 1 pair request, 2 pair acknowledgement, 3 gesture info
  uint8_t  button;   // 0..BUTTON_COUNT-1
  uint8_t  event;    // 0 press, 1 hold, 2 double tap
  uint8_t  battPct;  // 0-100, 255 = unknown (no divider wired yet)
  uint8_t  channel;  // pair acknowledgement only: the channel the mask is on
  uint8_t  gestures; // from the mask: bits 0-2 "button has a double tap", bits 3-5 "has a hold"
  uint32_t seq;      // counts presses
};
```

15 bytes, well inside ESP-NOW v1's 250-byte limit, so the v1/v2 version split never comes into it.

Both ends drop a packet whose length isn't `sizeof(RemotePacket)`, so **adding a field is a flag
day**: mask and remote have to be flashed together. Until both are, presses are ignored and the
remote blinks three times. Proto 2 was press and hold with no `gestures` byte.

`seq` lets the mask spot a repeat — ESP-NOW retries at the MAC layer and a press can arrive twice.
It resets to 0 when the battery is changed or the remote is reflashed, so the mask should treat a
`seq` lower than the last one seen as a fresh start rather than a repeat.

## Building and flashing

```bash
python -m platformio run -d BittenOS-Remote -t upload
```

Set `PLATFORMIO_CORE_DIR=C:\pio` first, same as the mask. The C6 is RISC-V, so the first build
downloads its own toolchain — after that it's cached.

The XIAO's only port is native USB, so it flashes straight over USB-C with no BOOT button, and the
serial log comes out the same port (`ARDUINO_USB_CDC_ON_BOOT=1`). Note the log is only readable for
the fraction of a second the chip is awake.

## The mask side

Built. `BittenOS-Controller` has the receiver, a pairing window opened from **Settings > Remote**,
and a press and a hold action for each of the three buttons. What it does:

1. Starts ESP-NOW alongside the access point, in `startWiFiWeb()`
2. Drops any packet without the magic, from a sender that isn't the paired remote, or with a `seq`
   already seen
3. While pairing is open (30 s, from the page), answers `MSG_PAIR_REQ` with the channel it is on
   and stores the remote's MAC in the config
4. Hands `button` + `event` to `doButtonAction()` from the loop task, never from the radio task

Because it goes through `doButtonAction()`, the remote inherits everything the mask's own button
can do — **including that a running dino game swallows the press as a jump**, which needed no code
of its own. The action list gained `slots`, `dino` and `boot` at the same time, so the mask's own
button can start those too.
