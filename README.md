# BittenOS

Firmware for my protogen mask: a FurBitten Studios premade face (14 × MAX7219 8×8 LED panels) driven by an ESP32-S3.

The face originally came with an Arduino Nano running FurBitten's closed-source firmware. This repo replaces that controller with open firmware I can change. It contains no FurBitten code, and the Nano is kept untouched as a backup.

## What it does

**Faces**

- Drives all 14 panels as one face: 2 panels per eye, 4 per mouth side, 1 per nose side
- 14 faces: default, happy, sad, angry, upset, confused, boop, boop2, heart, surprise, sleep, warning, bsod and paneltest. Most keep the eyes and nose the mask came with, and animate, blinking or twitching every few seconds. `warning` scrolls WARNING across both maws under a donated angry face; `bsod` lights every panel and punches ERROR and a cross through the eyes out of it, so the message is the unlit part; `paneltest` shows each panel its own number, for working out a layout
- A short dip to black when the face changes, adjustable from 20 to 400 ms
- Mouth bob: a switch that lifts the mouth a pixel every second or so, over any face
- A sleeping face (closed eyes, breathing mouth) on demand, or by itself once the mask has been still for a while, waking when it moves
- Scrolling text across the mouth panels, on repeat or once, at four speeds

**Web page** — its own WiFi network, `ProtoWiFi`, and a phone-friendly page at `192.168.4.1`

- Face tiles with previews, showing half of each face at double size since nearly all of them are mirror images
- Face manager: upload, download, rename and delete faces without reflashing
- Three tabs along the bottom — Mask, Settings, System — sized for a phone held in one hand
- Settings grouped by feature that save the moment they're changed, with parts that aren't fitted folded out of the way
- A live boop meter showing closeness and shadow against their trigger lines, for setting boops up
- Brightness and transition sliders, and a power button that turns the displays off while the mask keeps running
- A System tab: uptime, temperature, memory, storage, what's on the I2C bus, and raw sensor readings
- The web Animator, with touch drawing
- Installs on a phone's home screen with its own icon

**Sensors**

- Head tilt from a BMI270
- Boops from an APDS-9930, using both its proximity sensor and its light sensor: a boop has to come close *and* cast a shadow, so stray readings don't count
- Auto-brightness from the same light sensor, with three ways of keeping the panels' own glow out of the reading (blank, hybrid, live), a check interval, and low/high clamps
- A boop counter that pops up on the nose panels once the paw moves away
- Easter eggs on the maw: a slot machine and a dino game, from the page or a quick burst of button presses
- The I2C bus is checked every 10 seconds, and a sensor that drops out reconnects by itself without restarting the mask

**Updates**

- Over-the-air updates from the web page. A firmware update that doesn't run properly rolls itself back; a filesystem update keeps your settings

**Fitted and working:** an SPH0645 digital microphone on I2S, an SSD1306 status screen inside the
mask with its own layout designer, and a three-button ESP-NOW remote.

**Button remote** — a XIAO ESP32C6 in deep sleep until pressed, paired to this mask alone

- Press, hold and double tap on each of three buttons, nine actions, each picked in the web page
- Paired by MAC, so it talks only to this mask and only this mask hears it. Pairing is all three
  buttons held for three seconds, which needs no power cycle — the case is sealed
- Finds the mask again by itself if its WiFi channel moves, because a unicast packet is
  acknowledged where a broadcast is not
- The web page shows how strongly it is being heard, what the last press was, and how many presses
  went missing, with a small indicator that lights as each one lands
- About 15 uA asleep, no power switch, and it stays awake four seconds after a press so a run of
  them stays quick

**For the fun of it**

- A slot machine across the maw with real spinning reels, one spin in three landing triple sevens,
  shown on the inside screen too so the wearer knows what the crowd just saw
- A Chrome-style dino game on both maws, with the button or the remote to jump
- Both reachable from the page, or from a burst of quick presses on the mask's own button
- Boop sass: boop it too fast and it pulls a face at you
- A fake boot sequence: a loading bar filling outward from the nose along both maws
- A wake-up flicker, panels stuttering back on one at a time like an old CRT

## Hardware

| Part | Details |
|---|---|
| Controller | ESP32-S3 board with an ESP32-S3-N16R8 module (16MB flash, 8MB PSRAM). PSRAM is required. Built on a Lonely Binary "Gold Edition"; `platformio.ini` sets `flash_mode = dio`, which both it and a HiLetgo DevKitC boot from. |
| Display | FurBitten Studios premade face: 14 × MAX7219 8×8 panels, already chained |
| Tilt sensor | BMI270 on I2C (SDA GPIO 9, SCL GPIO 10), 3V3 |
| Boop sensor | APDS-9930 on the same I2C wires, 3V3 — not 5V tolerant |
| Button | Momentary button across GPIO 21 and 47 (optional) |
| Microphone | SPH0645 digital mic on I2S: BCLK GPIO 4, DOUT GPIO 5, LRCL GPIO 6, 3V3 |
| Status screen | 0.96" SSD1306 OLED on the same I2C wires as the sensors, address 0x3C |
| Remote | Seeed XIAO ESP32C6, three buttons, its own project in `BittenOS-Remote/` |
| Power | 5V. About 3W with the stock faces at brightness 5 |

Wiring, to the first panel in the chain (left eye, back):

| ESP32-S3 | MAX7219 |
|---|---|
| GPIO 12 | CLK |
| GPIO 13 | CS |
| GPIO 14 | DIN |
| GND | GND |
| 5V | VCC |

Running the panels from the board's 5V pin works at normal brightness. For all-pixels-on faces or higher brightness, feed the panels from the 5V supply directly. The guide has the numbers and an optional level shifter.

## Panel layout

Chain order: left eye (back, front) → left mouth (back to front) → left nose → right nose → right mouth (front to back) → right eye (front, back).

Seen from the front, which is how the Animator draws it:

```
t13 t12  .   .   .   .   .   .  t1  t0      eyes
 .   .   .  t7   .   .  t6   .   .   .      noses
t11 t10 t9  t8   .   .  t5  t4  t3  t2      mouth
```

## Changes from upstream

### Hardware and layout

| Change | Where | Why |
|---|---|---|
| 14-panel MAX7219 configuration | `src/main.cpp` (`visorType`, `MATRIXESNUM`), `data/visType.txt` | Upstream defaults to 11 RGB panels |
| `HARDWARE_TYPE` set to `PAROLA_HW` | `src/main.cpp` | Every panel was mirrored left to right with the stock setting |
| 16MB partition table | `16MB_ota.csv`, `platformio.ini` | Upstream maps 8MB of the 16MB flash. This uses the rest: two 3MB firmware slots, the live filesystem, a staging area for safe filesystem updates, and the coredump. The filesystem is at 0x610000 and the coredump at 0xDD0000. |
| Displays-only defaults | `data/config.json`, `src/fileOp.cpp` | Speech, BLE and OLED stay off; brightness defaults to 5 of 15 rather than an RGB value |

### Stability fixes

| Change | Where | Why |
|---|---|---|
| **Crash fix:** face changes are queued for the main loop | `src/main.cpp`: `/change` handlers, BLE handler, `loop()` | The web server freed the current face's frames while the drawing loop was still reading them, which crashed the board |
| Long animations | `src/main.cpp` | The frame counter was 8-bit, so animations over 255 frames restarted early |
| Face previews survive a dropped request | `data/index.html` | One failed fetch used to leave every preview tile black with no error. Fetches now retry, and a real failure is reported. |

### Faces

| Change | Where | Why |
|---|---|---|
| Original eyes and nose, more animation | `data/anims/`, `tools/make_faces.py` | The eyes and nose are read straight from the faces the mask came with (kept in `anims_14panel_v1/`), including each face's own blink. The originals sat still for up to 10 seconds, so long holds are capped and eyes that never moved get a slow lift. |
| Redrawn mouths | `tools/make_faces.py` | The stock faces were drawn for an 11-panel visor and machine-converted, which left blocky mouths. Most are redrawn two pixels thick; default keeps its original toothed mouth, one row flatter; boop2 has no mouth at all. |
| New faces: heart, surprise, boop2, sleep | `tools/make_faces.py`, `tools/make_sleep_face.py` | Heart and surprise have eyes drawn from scratch, having no original. Sleep is closed eyes and a breathing mouth. |
| Face transitions | `src/main.cpp` | A dip to black between faces. A wipe would sweep raggedly, because the MAX7219 chain runs in wiring order rather than the order the panels sit on the face; dimming looks the same whatever the layout. |
| Clock without a clock chip | `src/main.cpp` (`/settime`, `clockMinutesNow`), `data/index.html` | The ESP32-S3 has no battery-backed clock and the mask has no internet, so it cannot know the time. Whichever phone opens the page posts minutes-past-midnight and its own 12/24 hour preference, and the mask counts on from `millis()`. Sent on open, every five minutes, and whenever the page is returned to, since a locked phone stops the timer. Unset, the clock blinks `00:00` — and the screen's draw interval halves while it does, because the screen only redraws when pixels change and a blink would otherwise freeze half-drawn. |
| Status screen pages | `src/screen.cpp`, `src/main.cpp` (`screenTick`), `data/screen.html` | Up to eight pages, each with its own elements. Pages "in the rotation" are stepped by a button set to Next screen page or by a cycle timer; a page with a condition takes the screen over while that condition holds and lingers for a set time after, so a boop page doesn't vanish as the paw lifts. Layouts saved before pages existed are a flat element list, and both the firmware and the designer fold those into a single page, so nothing needs converting. Both save endpoints accept either shape. |
| Quick phrases built on the mask | `src/main.cpp` (`showPhrase`), `data/index.html` | The homepage builds its scrolling text in JavaScript and posts it, which is no use to a button press with nobody holding a phone. Six phrases are stored in the config and rendered by the firmware itself, using the same 5x7 font as the status screen: the face's own panels stay as the eyes, the text scrolls along the mouth row, and Play once hands back to the face that was showing. |
| Configurable button | `src/main.cpp` (`doButtonAction`), `data/index.html` | Short press, double press and hold are each set separately: nothing, next or previous face, displays on/off, sleep/wake, a chosen face, or a quick phrase. Defaults are what the button always did. Hold length is configurable, and the quarter-second wait needed to tell a single press from a double is only applied when double press is actually in use, so the single press stays instant otherwise. |
| ESP-NOW button remote | `src/main.cpp` (`rmtBegin`, `rmtOnRecv`), `BittenOS-Remote/` | Upstream's remote is a BLE device the mask hosts. This one is a XIAO ESP32C6 asleep at 15 uA until a button is pressed, sending one ESP-NOW packet to this mask alone. Paired by MAC, because a unicast packet is acknowledged and a broadcast is not — which is what lets the remote tell a delivered press from a lost one, and go looking for the mask when its WiFi channel moves. Press, hold and double tap per button, decided on the remote before anything is sent, since each is a different action and correcting afterwards would run two of them. The mask sends back which gestures are configured, so a button with only a plain press on it fires on the way down instead of waiting. The page shows signal strength, the last press and how many went missing. |
| Easter eggs on the maw | `src/main.cpp` (`slotTick`, the dino block), `data/index.html` | A slot machine with reels that spin down one after another, one spin in three landing triple sevens, drawn on the inside screen as well so the wearer knows what the crowd just saw. A Chrome-style dino game on both maws. Both start from the page, from a configurable burst of quick presses on the mask's own button, or from the remote. A running game swallows any button press as a jump, which the remote inherited for free. |
| Boop sass and milestones | `src/main.cpp` (`boopIsSassy`, `boopFaceNow`) | Boop it too many times inside a set window and it pulls a face at you; a long flurry sasses every fifth boop rather than sticking, so the ordinary boop face comes back in between and it reads as a reaction. Every hundredth boop plays a face of your choosing instead. Both configurable, both off at 0. |
| Fake boot sequence | `src/main.cpp` (`bootBar`) | A framed loading bar along both maws, filling outward from the nose and stuttering rather than creeping, played at startup alongside the wake-up flicker or on demand from a button. |
| Deep sleep | `src/main.cpp` (`sleepEaseStep`) | An off-by-default deep sleep switches the panels off entirely after a set time asleep and restores them on waking, where the wake-up flicker takes over. Sleeping and waking themselves are instant: a gradual dim in and out was tried and removed, because watching the panels fade down read as the mask dying rather than dozing off. |
| Wake-up flicker | `src/main.cpp` (`wakeFlickBegin`, `wakeFlickStep`) | Coming back from a deep sleep, the fourteen panels stutter on one at a time instead of together, each with its own timing and an unsteady brightness before it settles, and the picture tears while it warms up. Every MAX7219 takes its shutdown and intensity registers individually, so this is done without touching the face data at all; the tearing is a few throwaway rows and columns the next frame paints over. The wake order is deliberately random: the chain runs in wiring order rather than the order the panels sit on the face, so a sweep would look ragged. Everything that wants the panels back — a face change, the displays button, a transition, auto-brightness — cuts it short, and a backstop timer ends it even if a panel somehow never settles. The same warm-up runs at power-on when **Flicker on startup** is set: the panels are shut down as soon as `mx.begin()` returns and the warm-up is triggered by the first flushed frame rather than from `setup()`, so the chips already hold the face and the stutter reveals it instead of revealing nothing. |
| Blinks, the face's or the mask's | `src/main.cpp` (`blinkCheckFace`, `setAllVisor`) | These faces are animations that blink themselves, so a blink drawn on top would collide with the face's own and stutter. **Blinks** picks: leave the face alone, or let the mask blink — which holds the face on its resting frame so only one blink system runs, and fires at random gaps of 2.5–7 s so it doesn't read as a metronome. A face can declare this itself: the face designer has a **The mask may blink this face** tick box that writes `visor.canBlink` into the face file, so a still face you draw needs no blink frames at all. Faces without the flag are judged by their timings instead — one long look plus a short blink can be held (one frame at least three times any other), while a face whose frames run evenly is a real animation and always plays itself. The blink squeezes the eye panels' artwork toward the middle at draw time, like mouth bob, so it works on hand-drawn faces too. |
| Mouth bob | `src/main.cpp` | Done while drawing rather than baked into files, using the same mouth-panel flags speech uses, so it works on any face and can be switched live |

### Web page

| Change | Where | Why |
|---|---|---|
| Phone-friendly redesign, MAX7219 only | `data/index.html`, `data/animator.html` | Built for phones; RGB, ear-LED and other-helmet controls removed. The old pages are in git tag `ui-before-redesign`. |
| Tabbed layout, settings that save themselves | `data/index.html` | Mask, Settings and System tabs along the bottom. Settings are grouped rows by feature — switches, steppers and face pickers instead of a long form — and each sends only its own setting the moment it changes, going back if the mask doesn't accept it. Rows under a switch that's off are greyed. A Status screen group switches the SSD1306 OLED and its brightness; the microphone and remote have their own groups, with parts that aren't wired greyed and tagged *Not installed*. Tilt positions and WiFi keep a button, and Remote (BLE) still needs a restart, since BLE only starts at boot. |
| Live sliders | `data/index.html`, `src/main.cpp` (`/brightness`) | Brightness and transition apply as you drag; the main loop drives the panels, since the web server can't safely do it mid-draw |
| Face manager, scrolling text, power button, system card, home screen icon | `data/index.html`, `src/main.cpp` | See [What it does](#what-it-does) |
| Sleep mode and wake sensitivity | `src/main.cpp`, `data/index.html` | Sleeps after a set time without movement. Staying asleep and waking use different thresholds, so it doesn't jolt awake at a knock but still wakes easily when picked up. |

### Sensors

| Change | Where | Why |
|---|---|---|
| BMI270 tilt sensor | `src/main.cpp` | Replaces the LSM6DS3 driver, whose chip check rejects common clones. Tries addresses 0x68 and 0x69. |
| APDS-9930 driver | `src/main.cpp` | The 9930 answers on the same address as the APDS-9960, and the Adafruit 9960 library refuses it by device ID. It has its own small driver here, which logs the ID instead of enforcing it. |
| Boop detection that holds up behind a visor | `src/main.cpp`, `data/index.html` | A sensor behind a visor always sees its own infrared reflecting back, so it never reads zero, and that level wanders. Readings are smoothed (the middle of the last five), the resting level is tracked automatically, and a boop needs three readings in a row to start and two to end. A boop still held after 8 s is released and taken as the new resting level, so it can never stay latched. **Calibrate** resets the resting level. |
| Light-confirmed boops | `src/main.cpp`, `data/index.html` | Proximity alone still gave random boops through a visor. A paw close enough to boop also shades the APDS-9930's light sensor, which a bad proximity reading can't fake, so a boop must also be a set percentage darker than normal within the last second. Normal light is tracked (up fast, down slowly, frozen while something is near). Below a minimum level it's too dark to cast a shadow, and boops fall back to proximity alone. Ignored boops are counted. |
| Mic zero measured, not guessed | `src/main.cpp`, `data/index.html` | Upstream assumed the MAX9814's resting voltage, and the code only re-learns it while it thinks things are quiet — so a board that rests elsewhere (a MAX4466 sits at half its supply) leaves the mouth stuck open. Switching Speech on now averages a second of silence and stores it as Quiet level; **Measure silence** repeats it. The mic input also moved to GPIO 4. |
| Mouth shapes from volume | `src/main.cpp` (`dynamicSpeak`), `src/misc.cpp` (`speakMatrixGrow`) | Upstream's mouth only ever opens by one pixel — volume just decides how far along the mouth that opening spreads, so a mumble and a shout look the same. **Shapes** opens the mouth by one to three pixels in four volume steps, each reaching a different way along the mouth, opening instantly and closing after 90 ms so it doesn't chatter between syllables. **Flap** keeps the original behaviour. |
| Digital microphone on I2S | `src/main.cpp` (`micI2SBegin`, `micCollect`) | An analog mic shares the 3V3 rail with fourteen MAX7219s pulsing current and amplifies whatever noise that puts on it — on this mask a quiet room measured a noise floor of 1170 counts out of 4095, which no voice could beat. A digital mic converts sound inside its own package and sends numbers, so none of that reaches the signal, and there is no gain pot to misjudge. `micMode` picks I2S or ANALOG; the SPH0645's left-justified timing is handled with MSB slot format, and its DC offset is harmless because peak-to-peak cancels it. |
| Loudness measured peak to peak | `src/main.cpp` (speech block) | Upstream took 32 ADC samples as fast as it could — about a millisecond — kept the single highest, and subtracted a stored resting level. A millisecond is a fraction of one cycle of a human voice, so speech was sampled at a random point in its waveform and under-reported, while a snap or a knock landed hard whatever the timing: the mask heard bangs but not talking. Now the highest and lowest readings over a 25 ms window are tracked and the difference taken, which spans several cycles of any voice. The resting level cancels out of a difference, so there is nothing left to drift and no zero to get stuck on — the whole Quiet level deadlock and its auto-recovery are gone. Loudness is that swing above the room's measured noise floor. |
| Mic checks in plain numbers | `src/main.cpp` (`startMicTest`, `finishMicLoud`), `data/index.html` | **Mic test** listens for two seconds of quiet then three of talking and reports both swings, so "is this thing working" is answered by numbers rather than interpretation. **Measure loud** sets Range so normal speech lands about 80 of 100, and flags a gain pot that is too low or clipping. **Measure silence** sets the noise floor. The live swing, floor and mid-point are all on the page. |
| Boop milestones | `src/main.cpp` (`boopFaceNow`) | Every hundredth boop (configurable, 0 to turn off) plays a face of your choosing instead of the usual boop face. |
| Boop counter | `src/main.cpp`, `data/index.html` | Counts every boop and pops the total up on the nose once the paw moves away, so it never covers the boop face. Resettable. Saved a while after the boops stop, not on every one. |
| Sensors that recover themselves | `src/main.cpp` | The bus is swept every 10 seconds, a few addresses at a time so the animation never stalls. A sensor that stops answering is restarted automatically — and the retries no longer depend on the feature being switched on. |
| Auto-brightness | `src/main.cpp` (`autoBrightnessStep`), `data/index.html` | The panels shine into the sensor, so a plain reading partly measures them. **Blank** switches them off for 240 ms (two light-integration cycles) and reads the room; **Hybrid** watches the free live reading and only blanks when it moves 25% or more (and every tenth check); **Live** never blinks and accepts the glow. Readings map to brightness on a log scale between two calibration points, clamped to Lowest/Highest, with half-a-step hysteresis and one step per 120 ms. It never blanks over a face change or a boop, and the boop shadow check ignores readings taken during and just after a blank. |
| Ambient light reading | `src/main.cpp`, `data/index.html` | Read every 100 ms for the shadow check. Shown as raw channels on the System tab, not lux: a tinted visor passes infrared far better than visible light, which throws the standard formula off |

### Updates

| Change | Where | Why |
|---|---|---|
| Over-the-air updates, replacing ElegantOTA | `src/main.cpp`, `data/update.html` | See [Updating over WiFi](#updating-over-wifi) |

## Quick start

1. Install [PlatformIO](https://platformio.org/) (the VS Code extension, or `pip install platformio`).
2. Open the `BittenOS-Controller` folder.
3. Flash the firmware, then the filesystem:

   ```
   pio run -e esp32-s3 -t upload
   pio run -e esp32-s3 -t uploadfs
   ```

4. Join the WiFi network `ProtoWiFi` (password `Proto1234`) and open `http://192.168.4.1`.

On Windows, build from PowerShell or VS Code, not Git Bash, and turn on long path support, or the toolchain install fails.

`uploadfs` replaces everything stored on the board, including settings. On a mask that's already set up, the guide describes reading the settings off first so they survive.

## Updating over WiFi

Once the board has the 16MB partition table, updates don't need a cable. In the web page, go to **Tools > Update firmware** and choose a file from `.pio/build/esp32-s3/`:

- **`firmware.bin`** goes into the firmware slot that isn't running, and only takes over once it has fully arrived and checked out. If the new firmware resets within 30 seconds of starting, the mask switches back to the previous one by itself.
- **`littlefs.bin`** replaces the faces and web pages. It's written to a staging area and checked, then copied in at the next boot. Settings, calibration and the boop count are kept, and if the power goes during the copy, it simply starts again.

The page works out which kind of file it is from its contents and refuses anything else.

**Moving an existing mask to the new partition table needs one flash over USB.** The filesystem moved from 0x410000 to 0x610000, so settings have to be read from the old address first. The guide covers it.

## Tools

All in `BittenOS-Controller/tools/`, run from the `BittenOS-Controller` folder.

| Script | Does |
|---|---|
| `make_faces.py` | Builds the faces: original eyes and nose, the mouths, and the animation timing. `--preview <face>` prints one as text. |
| `render_faces.py` | Draws the faces to a PNG, one dot per LED, to check them without the mask |
| `make_sleep_face.py` | Builds the sleeping face |
| `make_icons.py` | Draws the home screen icons (needs Pillow) |
| `make_screen_font.py` | Builds `src/screen_font.h`, the 5x7 font used by the status screen and the scrolling text |
| `convert_mk2_to_14.py` | Converts faces drawn for the 11-panel layout to this 14-panel one |
| `virtual_tester.py` | Runs the web pages on your PC against a pretend mask (below) |
| `face-maker.html` | The face designer as one standalone file, with this mask's layout baked in and saving as a download. Made to hand to other people, who send a `.json` back — it needs no mask, no server and no internet |
| `tester.html`, `ble_control.html` | The virtual tester's own page, and a Web Bluetooth control page (parked: it needs https hosting) |

Faces found online are usually drawn for the stock 11-panel "M16 mk2" layout. Put them in `data/anims/` and run `python tools/convert_mk2_to_14.py`: eyes are copied, each mouth side gains a fourth panel, and the single centre nose is split across the two nose panels. Faces that are already 14-panel are skipped.

## Testing the web pages without the mask

```
cd BittenOS-Controller
python tools/virtual_tester.py
```

The dashboard at `http://localhost:8080/__tester` shows the page next to a virtual 14-panel face playing the current face at the current brightness, with a log of every request. It simulates the physical button, boops (including holding one), the boop counter and mouth bob, and answers the update page without writing anything.

Pages are served live from `data/`, so edits show up on reload. Faces and settings live on a separate virtual board in `tools/.virtual_board/` (not committed), so testing never changes `data/`; start with `--reset` to copy them from `data/` again. Firmware changes still need a real build and flash.

## Repository layout

| Path | Contents |
|---|---|
| `BittenOS-Controller/` | The firmware (PlatformIO project), with the web pages and faces in `data/` |
| `BittenOS-Controller/tools/` | Face building, rendering and conversion, icons, and the virtual tester |
| `BittenOS-Controller/16MB_ota.csv` | The partition table |
| `BittenOS-Controller/anims_14panel_v1/` | The faces as they came, converted to 14 panels — the source of the original eyes and nose |
| `BittenOS-Controller/anims_original_11panel/` | The stock faces before conversion |
| `MY_PROTOGEN_SETUP.md` | Full setup, flashing and usage guide for this mask |
| `IDEAS.md` | Ideas not built yet, and decisions already made about them |
| `BittenOS-Remote/` | Button remote for the mask: a XIAO ESP32C6 that sleeps until pressed, then sends over ESP-NOW |

## Credits and license

BittenOS is licensed under **GPL-3.0** — see [`LICENSE`](LICENSE).

It began as a fork of **[ProtoESP (ProtogenHelmet-ESP32)](https://github.com/NCPlyn/ProtogenHelmet-ESP32)** by **[NCPlyn](https://github.com/NCPlyn)**. None of this would exist without his work, and the licence comes from there too.

If you want a protogen controller of your own rather than one bent around this particular mask, start with his: it supports WS2812 matrices, ear rings, blush LEDs and a good deal more hardware than this fork kept.

The scrolling-text font is the system font from the [MD_MAX72XX](https://github.com/MajicDesigns/MD_MAX72XX) library by MajicDesigns (LGPL-2.1).

The face hardware is by FurBitten Studios. Their firmware is not included here.

---

*An AI assistant was used to help write this firmware.*
