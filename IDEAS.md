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
- [x] **Microphone** (2026-09-16) — MAX4466 on GPIO 4, with the quiet level measured rather than
      guessed, and a level meter in Settings. Still to be proven working on the mask.

## Software only — nothing to buy

- [ ] **Idle blinks** — the current face blinks every so often. Generated the same way as the
      sleeping face, so `tools/make_sleep_face.py` is the starting point. Cheapest way to make the
      mask look alive rather than static.
- [ ] **More from the screen** — several pages that cycle or swap on a boop, a tiny live preview of
      the face itself, or a boop alert page. The renderer already supports show-conditions, so
      pages are mostly designer and firmware plumbing.
- [ ] **Quick phrases** — saved scrolling-text messages as one-tap buttons. The text engine
      already exists; this just stores favourites.
- [ ] **Sleep refinements** — dim gradually before dropping off, wake more gently, deeper sleep
      after longer.
- [ ] **Per-face settings** — mouth bob, transition speed or brightness remembered per face,
      rather than one setting for all of them.
- [ ] **Boop milestones** — every hundredth boop plays a special face or screen.

## Uses the mic, now fitted

- [ ] **Mouth shapes from volume** — instead of the mouth opening by one amount, pick between a
      few mouth drawings by how loud you are. Much more convincing than the current flap.
- [ ] **Shout trigger** — a sudden loud noise switches to a surprised face for a moment.

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
