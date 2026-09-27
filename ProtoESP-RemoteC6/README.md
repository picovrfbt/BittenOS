# Button remote (XIAO ESP32C6, ESP-NOW)

A few momentary buttons on a Seeed XIAO ESP32C6 that sit in deep sleep until pressed, send one
ESP-NOW packet to the mask, and go back to sleep. **No power switch** — deep sleep is about 15 µA,
so it can be left armed indefinitely and a small LiPo lasts months.

> **The mask side does not exist yet.** This firmware transmits; nothing in
> `ProtoESP-Controller` listens for it. The packet format below is what a receiver has to match.

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
| Press | wakes, sends `EV_PRESS`, **one blink** |
| Still held after 1.2 s | sends `EV_HOLD` as well, another blink |
| Radio wouldn't start | **three blinks** |
| Power-on or reflash | **two blinks**, then arms and sleeps without sending |

One blink means the press was transmitted, not that the mask heard it — a broadcast isn't
acknowledged, so the only honest failure the remote can report is the radio refusing to start.

A held button is waited out before sleeping — arming a wake on a pin that is already low would
wake the chip the instant it slept and spin until the battery died. If a button never releases it
is left out of the wake mask and a 2 s timer wake is armed instead, so the remote recovers by
itself once the button clears rather than staying dead.

## The channel problem

ESP-NOW needs both ends on the same WiFi channel, and the mask's channel isn't knowable from here.
It moves on its own: with *Join a network* switched on, the mask's AP follows the channel of
whichever hotspot it joined, and that can change without warning.

**The remote sends the packet on every channel, 1 to 13.** Thirteen sends of twelve bytes costs
about a tenth of a second and is beneath noticing on the battery. Every copy carries the same
`seq`, and the mask keeps the first it sees.

That means the remote depends on **nothing** about the mask — not its WiFi name, not its MAC, not
its channel. Rename `ProtoWiFi`, move the mask to the spare board, join a different hotspot: the
remote keeps working, untouched.

Two neater-looking approaches were tried and thrown out:

- **Scanning for the mask's SSID** ties the remote to a name that is a *setting on the mask*.
  Rename the WiFi and the remote goes deaf until it's reflashed.
- **Caching the channel and rescanning when a send fails** cannot work at all, because
  **ESP-NOW broadcasts are never acknowledged**. The send callback reports success whether or not
  anything heard it, so there's no failure to detect and nothing to correct against.

Channels the region disallows simply fail to be set and are skipped, so a US board sweeping 1–13
quietly does 1–11.

## The packet

Sent to the broadcast address rather than the mask's MAC, so swapping the mask onto a different
board doesn't mean reflashing the remote. The receiver ignores anything without the magic value.

```c
struct __attribute__((packed)) RemotePacket {
  uint32_t magic;    // 0x50524F54 ('PROT')
  uint8_t  proto;    // 1
  uint8_t  button;   // 0..BUTTON_COUNT-1
  uint8_t  event;    // 0 press, 1 hold
  uint8_t  battPct;  // 0-100, 255 = unknown (no divider wired yet)
  uint32_t seq;      // counts presses for the life of the battery
};
```

12 bytes, well inside ESP-NOW v1's 250-byte limit, so the v1/v2 version split never comes into it.

`seq` is what makes the channel sweep work: the receiver keeps the first copy of a `seq` it sees
and drops the other twelve. It also covers ordinary duplicates, since ESP-NOW has no delivery
guarantee. It resets to 0 when the battery is changed or the remote is reflashed, so the mask
should treat a `seq` lower than the last one seen as a fresh start rather than a repeat.

## Building and flashing

```bash
python -m platformio run -d ProtoESP-RemoteC6 -t upload
```

Set `PLATFORMIO_CORE_DIR=C:\pio` first, same as the mask. The C6 is RISC-V, so the first build
downloads its own toolchain — after that it's cached.

The XIAO's only port is native USB, so it flashes straight over USB-C with no BOOT button, and the
serial log comes out the same port (`ARDUINO_USB_CDC_ON_BOOT=1`). Note the log is only readable for
the fraction of a second the chip is awake.

## What the mask still needs

Roughly 60–100 lines in `ProtoESP-Controller`:

1. `esp_now_init()` alongside the existing WiFi, with a receive callback
2. Drop any packet whose `magic` isn't `0x50524F54`, and any `seq` already seen — **this is
   required, not optional**, since every press arrives up to thirteen times, once per channel
3. Map `button` + `event` onto `doButtonAction()`, which already takes an action name and covers
   next face, sleep, a chosen face, a quick phrase and next screen page

Settings for which action each button maps to would follow the same shape as the existing
`btnShort` / `btnDouble` / `btnHold` pickers.
