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
      per-face "the mask may blink this face" tick box in the designer. The mask's own blink was
      broken until 2026-09-18: the step counter turned around at 4 instead of counting through, so
      it ticked 3,4,3,4 forever, the lids shut to a line and the redraw check then stopped
      repainting. Also repaced from a 108 ms twitch to 450 ms, matching default.json's own blink.
- [x] **Mic widgets on the screen** (2026-09-18) — a bar that fills with how loud you are and marks
      where the mouth starts moving, a mic icon that fills in while you talk, and a value source for
      the number. Added `speaking` / `notSpeaking` conditions for elements and whole pages.
- [x] **Boop milestone screen page** (2026-09-18) — an `On a boop milestone` condition, bounded by
      `boopPhase()` so it lasts the celebration rather than until the next boop.
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

## Funnies — for the heck of it

**The rule these are judged by:** a joke needs an audience, and the audience only ever sees the
**visor**. The inside screen is the wearer's instrument panel, not a stage — a bouncing DVD logo on
it was suggested and rejected on exactly that ground. Anything meant to be funny goes on the face.

- [ ] **Boop sass** (~25 lines) — five boops inside ten seconds and it pulls `upset.json` at you.
      The face and the boop timestamps both already exist.
- [ ] **Boop rank announcements** (~35 lines) — at a milestone, "BOOP FIEND" scrolls across the maw.
      Reuses `showPhrase()` unchanged: the face's own panels stay as the eyes, text scrolls the
      mouth row. Ranks something like Novice at 10, Fiend at 500.
- [x] **Fake boot sequence** (2026-09-18) — a framed bar along both maws, filling outward from the
      nose, stuttering rather than creeping. Runs alongside the wake-up flicker rather than after it.
- [x] **Chrome dino game** (2026-09-18) — on both maws, mirrored. Start from the System tab, the
      mask's own button jumps while it runs. Score and best kept.
- [ ] **Boop sass** (~25 lines) — still to do; see above.

## Being done by hand, not by me

- **Speech-reactive screen page** — the `speaking` condition is in and verified at page level; the
  page itself is being designed in the screen designer.

## Decided against — do not suggest these again

- **Sleep easing** — dimmed down before sleep and climbed back on waking. Read as the mask dying
  rather than dozing off. Removed 2026-09-17; deep sleep stayed.
- **Per-face settings** — mouth bob, transition speed or brightness per face. Declined 2026-09-17:
  a lot of machinery for the payoff.
- **Live face preview on the screen** — a downscaled copy of the visor on the SSD1306. Declined
  2026-09-18. It would be ~190 lines across seven files and, at 80x24 on a mono screen, would
  almost certainly read as noise. The diagnostic argument for it (spotting a dead panel without a
  mirror) does not apply — that is not a worry here.
- **Shout trigger** — a loud noise switches to a surprised face. Declined twice.
- **Bouncing DVD logo screensaver** — suggested as a joke, rejected because the inside screen has
  an audience of one. See the rule at the top of Funnies.

## Needs parts — closed for now

**No more hardware is going on this mask** (decided 2026-09-17), so everything below is parked
rather than planned. Kept only so the reasoning is not lost if that ever changes. The fan has been
moved to its own supply, which takes the fan curve off the table too.

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
