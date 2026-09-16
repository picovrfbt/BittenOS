# protogen-fw

Firmware for my protogen mask: a FurBitten Studios premade face (14 × MAX7219 8×8 LED panels) driven by an ESP32-S3, running a modified version of [ProtoESP](https://github.com/NCPlyn/ProtogenHelmet-ESP32) by NCPlyn.

The face originally came with an Arduino Nano running FurBitten's closed-source firmware. This repo replaces that controller with open firmware I can change. It contains no FurBitten code, and the Nano is kept untouched as a backup.

## What it does

**Faces**

- Drives all 14 panels as one face: 2 panels per eye, 4 per mouth side, 1 per nose side
- 12 faces: default, happy, sad, angry, upset, confused, boop, boop2, heart, surprise, sleep and bsod. They keep the eyes and nose the mask came with, and every one animates, blinking or twitching every few seconds
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
- The web Animator from ProtoESP, with touch drawing
- Installs on a phone's home screen with its own icon

**Sensors**

- Head tilt from a BMI270
- Boops from an APDS-9930, using both its proximity sensor and its light sensor: a boop has to come close *and* cast a shadow, so stray readings don't count
- Auto-brightness from the same light sensor, with three ways of keeping the panels' own glow out of the reading (blank, hybrid, live), a check interval, and low/high clamps
- A boop counter that pops up on the nose panels once the paw moves away
- The I2C bus is checked every 10 seconds, and a sensor that drops out reconnects by itself without restarting the mask

**Updates**

- Over-the-air updates from the web page. A firmware update that doesn't run properly rolls itself back; a filesystem update keeps your settings

Microphone, OLED and the wireless remote are supported by ProtoESP but not fitted, so they're off here and untested.

## Hardware

| Part | Details |
|---|---|
| Controller | ESP32-S3 board with an ESP32-S3-N16R8 module (16MB flash, 8MB PSRAM). PSRAM is required. Built on a Lonely Binary "Gold Edition" board. |
| Display | FurBitten Studios premade face: 14 × MAX7219 8×8 panels, already chained |
| Tilt sensor | BMI270 on I2C (SDA GPIO 8, SCL GPIO 9), 3V3 |
| Boop sensor | APDS-9930 on the same I2C wires, 3V3 — not 5V tolerant |
| Button | Momentary button across GPIO 21 and 47 (optional) |
| Power | 5V. About 3W with the stock faces at brightness 5 |

Wiring, to the first panel in the chain (left eye, back):

| ESP32-S3 | MAX7219 |
|---|---|
| GPIO 11 | DIN |
| GPIO 10 | CS |
| GPIO 12 | CLK |
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

## Changes from upstream ProtoESP

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
| Mouth bob | `src/main.cpp` | Done while drawing rather than baked into files, using the same mouth-panel flags speech uses, so it works on any face and can be switched live |

### Web page

| Change | Where | Why |
|---|---|---|
| Phone-friendly redesign, MAX7219 only | `data/index.html`, `data/animator.html` | Built for phones; RGB, ear-LED and other-helmet controls removed. The old pages are in `ui_backup/` and git tag `ui-before-redesign`. |
| Tabbed layout, settings that save themselves | `data/index.html` | Mask, Settings and System tabs along the bottom. Settings are grouped rows by feature — switches, steppers and face pickers instead of a long form — and each sends only its own setting the moment it changes, going back if the mask doesn't accept it. Rows under a switch that's off are greyed. A Status screen group switches the SSD1306 OLED and its brightness; the microphone, fan and remote have their own groups, with parts that aren't wired greyed and tagged *Not installed*. Tilt positions and WiFi keep a button, and Remote (BLE) still needs a restart, since BLE only starts at boot. |
| Live sliders | `data/index.html`, `src/main.cpp` (`/brightness`) | Brightness, fan and transition apply as you drag; the main loop drives the panels, since the web server can't safely do it mid-draw |
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
2. Open the `ProtoESP-Controller` folder.
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

All in `ProtoESP-Controller/tools/`, run from the `ProtoESP-Controller` folder.

| Script | Does |
|---|---|
| `make_faces.py` | Builds the faces: original eyes and nose, the mouths, and the animation timing. `--preview <face>` prints one as text. |
| `render_faces.py` | Draws the faces to a PNG, one dot per LED, to check them without the mask |
| `make_sleep_face.py` | Builds the sleeping face |
| `make_icons.py` | Draws the home screen icons (needs Pillow) |
| `convert_mk2_to_14.py` | Converts faces drawn for the 11-panel layout to this 14-panel one |
| `virtual_tester.py` | Runs the web pages on your PC against a pretend mask (below) |

Faces found online are usually drawn for ProtoESP's 11-panel "M16 mk2" layout. Put them in `data/anims/` and run `python tools/convert_mk2_to_14.py`: eyes are copied, each mouth side gains a fourth panel, and the single centre nose is split across the two nose panels. Faces that are already 14-panel are skipped.

## Testing the web pages without the mask

```
cd ProtoESP-Controller
python tools/virtual_tester.py
```

The dashboard at `http://localhost:8080/__tester` shows the page next to a virtual 14-panel face playing the current face at the current brightness, with a log of every request. It simulates the physical button, boops (including holding one), the boop counter and mouth bob, and answers the update page without writing anything.

Pages are served live from `data/`, so edits show up on reload. Faces and settings live on a separate virtual board in `tools/.virtual_board/` (not committed), so testing never changes `data/`; start with `--reset` to copy them from `data/` again. Firmware changes still need a real build and flash.

## Repository layout

| Path | Contents |
|---|---|
| `ProtoESP-Controller/` | The firmware (PlatformIO project), with the web pages and faces in `data/` |
| `ProtoESP-Controller/tools/` | Face building, rendering and conversion, icons, and the virtual tester |
| `ProtoESP-Controller/16MB_ota.csv` | The partition table |
| `ProtoESP-Controller/anims_14panel_v1/` | The faces as they came, converted to 14 panels — the source of the original eyes and nose |
| `ProtoESP-Controller/anims_original_11panel/` | The stock faces before conversion |
| `ProtoESP-Controller/ui_backup/` | The web pages from before the redesign |
| `MY_PROTOGEN_SETUP.md` | Full setup, flashing and usage guide for this mask |
| `IDEAS.md` | Ideas not built yet, and decisions already made about them |
| `README-ProtoESP-upstream.md` | NCPlyn's original README, with links to the upstream docs |
| `ProtoESP-Remote/`, `RGBMatrix/` | Upstream wireless remote and RGB panel projects, unchanged and not used here |

## Credits and license

Based on [ProtoESP (ProtogenHelmet-ESP32)](https://github.com/NCPlyn/ProtogenHelmet-ESP32) by [NCPlyn](https://github.com/NCPlyn), licensed under GPL-3.0. This repo is GPL-3.0 as well (see `LICENSE`). NCPlyn's credit line in `index.html` and `animator.html` is kept, as the original project asks. If ProtoESP is useful to you, consider supporting NCPlyn; the links are in the upstream README.

The scrolling-text font is the system font from the [MD_MAX72XX](https://github.com/MajicDesigns/MD_MAX72XX) library by MajicDesigns (LGPL-2.1).

The face hardware is by FurBitten Studios. Their firmware is not included here.
