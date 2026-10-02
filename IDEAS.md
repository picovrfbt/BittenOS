# Ideas for later

Things worth adding to the mask, parked here so they don't get lost. Roughly ordered by
value-for-effort within each group.

## The big one — masks other than this one

**Want:** BittenOS should drive any protogen face, not only the FurBitten 14-panel.

The layout already has a description — `data/visType.txt` — and both the web page and the virtual
tester parse it. **The firmware does not.** That is the crux. Everything on the mask that needs to
know where a panel sits has a number typed into `src/main.cpp` instead.

What is nailed down today:

| Where | What it assumes |
|---|---|
| `#define MATRIXESNUM 14` | fourteen panels, with `visorLedsNum` derived from it |
| `mawStrips[2]` | the maw is panels 8,9,10,11 and 5,4,3,2, the left one mirrored |
| `noseLeftPanel 7`, `noseRightPanel 6` | where the boop counter pops up |
| blink eligibility | "eye panels are the ones that are neither mouth nor nose" |
| every effect drawn across the maw | 32 columns of maw, 8 of nose |

Face files already carry `isMouth[]` per panel, so mouth-or-not is solved. What is missing is
everything else about a panel: which side it is on, whether it is eye, nose or maw, and which way
round it reads.

Roughly in order:

1. **Let the firmware read `visType.txt`.** It is already the single source of truth for where
   panels sit, already on the mask, already parsed by two other pieces of code. Panel count stops
   being a `#define`.
2. **Say what each panel *is*, not just where it sits.** visType gives geometry; nothing gives
   roles. Either extend it or add a small companion file — eye / nose / maw, left / right,
   mirrored or not. That one file would replace `mawStrips`, both nose defines and the blink rule.
3. **Make the effects measure the maw instead of assuming it.** The slot reels, the dino ground,
   the loading bar, the boop counter and the scrolling text are all sized to 32 columns today.
   They need to ask how wide the maw actually is. Faces drawn to this mask's dimensions —
   `warning`, `bsod` — would need generating per layout rather than shipping fixed.
4. **Face sets per layout.** A face is a list of panels, so it belongs to a layout.
   `tools/convert_mk2_to_14.py` already converts the stock 11-panel set to this one; that wants
   generalising to any-to-any, driven by the role file rather than by a hand-written table. The
   sets in `anims_original_11panel/` and `anims_14panel_v1/` are the test material for it.
5. **The face maker asks rather than assumes.** Done for the editor itself (2026-09-30): the
   Animator reads the mask's own `visType.txt`, and the standalone face maker has a Panel layout
   editor. Mirror drawing works out which panel faces which from the layout.

**Before starting:** the hard part is not the panel count, it is that "maw", "nose" and "eye" are
currently opinions held in C rather than data — step 2 decides how hard the rest is. There is
already a pattern to follow for layouts the firmware does not understand: several effects check
`panel[k] >= numOfSegm` and bow out rather than draw somewhere that does not exist, so an unknown
layout should lose its effects, not break.

## Rave mode - built (2026-10-02)

The top bar's equaliser button opens a small panel: pick **VU bar** or **Spectrum**, then **Start**.
While it runs the same button reads **Stop**, the icon stays lit, and switching looks applies at
once. The trace was simulated and dropped in favour of these two.

Why it is built the way it is, because none of it is obvious from the code alone:

- **It only takes the maw.** It is drawn in `setAllVisor()` beside speech and mouth bob, so eyes,
  nose, blinks, the boop counter and transitions all carry on underneath. The dino and the slots
  take the whole face over; this deliberately does not.
- **Auto-gain is the feature, not a refinement.** Music measured peak to peak sits near the bottom
  of its range and spikes once a beat - over a test clip, median 12 and peak 95 - so a bar scaled
  straight off it lies dead and twitches. Everything goes through a floor that follows the room, a
  peak that jumps up and sinks back slowly, and a curve shaped like hearing.
- **A quiet room has to stay dark,** and this one measures a swing of 0. At raw counts a gain keen
  enough to wake a quiet bar also wakes silence; `RAVE_GATE` and the two minimum peaks are what stop
  that. The firmware's maths was ported line for line into `sim_rave.py` and checked on raw counts
  before it was written: silence dark in both modes, quiet talking showing, music filling the maw,
  bass at the nose, and the hand-written FFT within 1e-13 of numpy.
- **Every sample is kept now.** `micCollect()` used to take 64 samples and leave the rest, and
  threw away a short read entirely. It drains the DMA each call, which an FFT needs - a block with a
  hole in it is a spectrum of the hole - and which also makes Speech see every sample.
- **The screen says RAVE MODE once and then stops.** Your idea, and it is a real saving: a picture
  that never changes means `screenTick` pushes nothing at all, and the OLED blit is the one thing
  already proven to stutter the face.
- **Speech stays separate.** Rave listens whether or not Speech is on, and with only rave on the
  mouth never decides it is talking. It ignores the Speech calibration entirely - quiet and loud
  gates tuned for a voice inside the mask are wrong for music across a room.
- **Use mic off reads nothing.** A made-up groove drives the maw instead - a wave rolling out
  from the nose, the bass thumping at 120 bpm - writing the same levels the mic path writes, so
  the drawing cannot tell. While rave runs, who reads the mic is rave's call alone, because Speech
  cannot show anything on a maw rave owns. The only exception is a calibration asked for by hand.
- **The eyes are the face's.** Any face can be the eyes of a rave: change faces mid-rave and only
  the eyes change. Boop it and the eyes go boop and the counter pops on the nose.

Still to try on the real mask, since the virtual tester has no ears: whether `RAVE_VU_MIN_PEAK`
and `RAVE_SPEC_MIN_PEAK` suit real music at a real distance, and how much current a busy spectrum
draws at high brightness - feed the panels from the supply rather than the board pin for that.

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

- [x] **Boop sass** (2026-09-25) — boop it too fast and it pulls a face at you. The count, the
      window and the face are all settings under Boop counter, and 0 turns it off. A long flurry
      sasses every fifth boop rather than sticking, so the ordinary boop face comes back in
      between and it reads as a reaction. A milestone outranks it.
- Boop rank announcements: **dropped** (2026-09-25), not wanted.
- [x] **Fake boot sequence** (2026-09-18) — a framed bar along both maws, filling outward from the
      nose, stuttering rather than creeping. Runs alongside the wake-up flicker rather than after it.
- [x] **Chrome dino game** (2026-09-18) — on both maws, mirrored. Start from the System tab, the
      mask's own button jumps while it runs. Score and best kept.

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
rather than planned. Kept only so the reasoning is not lost if that ever changes.

The fan is gone for good (decided 2026-09-22): it runs from its own supply, the PWM wire is not
going back on, and the fan curve idea went with it. GPIO 13 is free.

- [ ] **Remote** — decided against the stock BLE remote project for now. Two routes: a bought
      BLE camera-shutter clicker (cheap, two buttons, must be Bluetooth 4.0/BLE since the S3 has no
      classic Bluetooth), or an ESP32-C3 SuperMini sending ESP-NOW (a few dollars, any number of
      buttons, instant, no pairing). ESP-NOW is the better mask remote.
- [ ] **INA219 power monitor** — real watts on the System tab, and a sane ceiling for
      auto-brightness's Highest setting. The OLED code already has a slot for it.
- [ ] **Cheek LEDs (WS2812)** — `blushPresent` and the `blush0`–`blush3` cells exist in the
      upstream code and `data/visType.txt`, unused here.
