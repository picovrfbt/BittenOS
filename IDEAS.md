# Ideas for later

Things worth adding to the mask, parked here so they don't get lost. Roughly ordered by
value-for-effort within each group.

## Done

- [x] **Face manager** — Manage button on the Faces card, with Rename and Delete per face.
- [x] **Face transitions** — a dip to black when the face changes, switchable under Features.
- [x] **Add to home screen** — manifest and icons. Fullscreen on iOS; on Android it stays a
      browser shortcut, because a service worker can't register over plain HTTP to an IP.
- [x] **Boop counter** — counts, pops up on the nose once the paw moves away, resettable.
- [x] **Light-confirmed boops** — the light sensor's shadow has to agree with proximity.
- [x] **Auto-brightness** (2026-09-15) — Blank, Hybrid and Live methods, check interval,
      Lowest/Highest clamps, and dark/bright calibration points.
- [x] **Status screen** (2026-09-16) — SSD1306 fitted, with a layout designer on the mask itself
      at `/screen.html` saving to `data/screen.json`, drawn by `src/screen.cpp`.
- [x] **Microphone** (2026-09-17) — SPH0645 digital mic on I2S (BCLK 4, DOUT 5, LRCL 6), after the
      analog MAX4466 was defeated by panel supply noise. Peak-to-peak measurement, noise floor
      measured rather than guessed, level meter and Mic test in Settings.
- [x] **Idle blinks** (2026-09-16) — Blinks setting picks the face's own or the mask's, with a
      per-face "the mask may blink this face" tick box in the designer.
- [x] **Quick phrases** (2026-09-16) — six saved messages rendered by the firmware itself, so a
      button press works with no phone nearby.
- [x] **Screen pages** (2026-09-16) — up to eight, rotation or condition-triggered with linger.
- [x] **Mouth shapes from volume** (2026-09-16) — several mouth drawings picked by loudness.
- [x] **Boop milestones** (2026-09-16) — every Nth boop plays a chosen face.
- [x] **Configurable button** (2026-09-16) — short, double and hold each set separately.
- [x] **Clock** (2026-09-17) — time from whichever phone opens the page; blinks 00:00 until told.
- [x] **Wake-up flicker** (2026-09-17) — panels stutter back on one at a time after a deep sleep
      and at power-on, with picture tearing. CRT or fluorescent-tube styles.
- [x] **Join a network** (2026-09-17) — AP_STA: keeps its own access point and also joins a phone
      hotspot or house WiFi, so the phone keeps its internet. mDNS, IP as a screen source.
- [x] **BLE command set** (2026-09-17) — status, face, brightness, displays, sleep, phrase, screen
      page, warm-up, speech on the existing ffe0/ffe1 profile, plus an MTU fix that stops the face
      list truncating for the physical remote. Off by default. A Web Bluetooth page exists in
      `tools/ble_control.html` but was parked: it needs https hosting to be useful.

**Removed deliberately:** sleep easing (dim down before sleep, climb back on wake). It read as the
mask dying rather than dozing off, so it came out on 2026-09-17. Deep sleep stayed.

## Software only — nothing to buy

- [ ] **Per-face settings** — mouth bob, transition speed or brightness remembered per face,
      rather than one setting for all of them.
- [ ] **Boop milestone screen page** — the milestone plays a face; it could take over the status
      screen too. The page conditions already exist, so this is a new show-condition and little else.
- [ ] **Face preview on the screen** — a tiny live copy of the visor on the SSD1306. The panel data
      is already in a buffer each frame; this is downscaling it into the screen buffer.
- [ ] **Shout trigger** — a sudden loud noise switches to a surprised face for a moment. Declined
      once already ("I dont want mic yelling"), kept here only for completeness.

## Needs parts

- [ ] **Remote** — decided against ProtoESP's own BLE remote project for now. Two routes: a bought
      BLE camera-shutter clicker (cheap, two buttons, must be Bluetooth 4.0/BLE since the S3 has no
      classic Bluetooth), or an ESP32-C3 SuperMini sending ESP-NOW (a few dollars, any number of
      buttons, instant, no pairing). ESP-NOW is the better mask remote.
- [ ] **Fan curve** — the PWM is already set up on GPIO 13 at 25 kHz, it's just fixed-duty. Make it
      follow the chip temperature the System tab already reads. Only needs the fan fitted.
- [ ] **INA219 power monitor** — real watts on the System tab, and a sane ceiling for
      auto-brightness's Highest setting. The OLED code already has a slot for it.
- [ ] **Cheek LEDs (WS2812)** — `blushPresent` and the `blush0`–`blush3` cells exist in the
      upstream code and `data/visType.txt`, unused here.
