# protogen-fw

Firmware for my protogen mask: a FurBitten Studios premade face (14 × MAX7219 8×8 LED panels) driven by an ESP32-S3, running a modified version of [ProtoESP](https://github.com/NCPlyn/ProtogenHelmet-ESP32) by NCPlyn.

The face originally came with an Arduino Nano running FurBitten's closed-source firmware. This repo replaces that controller with open firmware I can change. It contains no FurBitten code, and the Nano is kept untouched as a backup.

## What it does

- Drives all 14 panels as one face: 2 panels per eye, 4 per mouth side, 1 per nose side
- Creates its own WiFi network (`ProtoWiFi`) with a web page to switch faces, change brightness and turn features on and off
- Includes ProtoESP's web Animator for drawing faces pixel by pixel and testing them live on the mask
- Ships with 11 faces (default, happy, sad, angry, upset, confused, boop, heart, surprise, sleep, bsod), drawn from curves for this 14-panel layout and animated
- A redesigned, phone-friendly web page: face tiles with previews, a live brightness slider for the MAX7219 panels (0–15), feature switches, and no RGB, ear-LED or other-helmet controls
- Scrolling text: type a message on the web page and press Enter to scroll it across the mouth panels, on repeat or just once, at four speeds
- Touch drawing in the Animator: drag a finger across pixels to draw or erase
- A power button that turns the displays off while the mask keeps running, also on the physical button (hold 1.5 s)
- Head tilt (BMI270) and boop (APDS9960) sensors on one shared I2C bus, with calibration from the web page
- A System card on the homepage: uptime, chip temperature, loop speed, memory, storage and sensor status
- A sleeping face (closed eyes, breathing mouth) on demand, or by itself once the mask is left still, waking when it moves
- Face housekeeping from the phone: upload, download, rename and delete faces without reflashing
- A short dip to black when the face changes, instead of a hard cut
- Installs on a phone's home screen with its own icon
- Supports ProtoESP's other optional extras: microphone, OLED and wireless remote. None are fitted yet, so they're turned off in this configuration and untested.

## Hardware

| Part | Details |
|---|---|
| Controller | ESP32-S3 board with an ESP32-S3-N16R8 module (16MB flash, 8MB PSRAM). PSRAM is required. Built on a Lonely Binary "Gold Edition" board. |
| Display | FurBitten Studios premade face: 14 × MAX7219 8×8 panels, already chained |
| Tilt sensor | BMI270 on I2C (SDA GPIO 8, SCL GPIO 9, 3V3) |
| Boop sensor | APDS9960 on the same I2C wires (3V3) |
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

| Change | Where | Why |
|---|---|---|
| 14-panel MAX7219 configuration | `src/main.cpp` (`visorType`, `MATRIXESNUM`), `data/visType.txt`, `data/visTypeMouth.txt` | Upstream defaults to 11 RGB panels |
| `HARDWARE_TYPE` set to `PAROLA_HW` | `src/main.cpp` | Every panel was mirrored left to right with the stock setting |
| 8MB partition table | `platformio.ini` | 2MB for the firmware and about 3.9MB for faces, instead of the 4MB layout |
| Displays-only defaults | `data/config.json`, `boopMode` in `src/main.cpp` | Speech, boop, BLE and OLED stay off until those parts are fitted |
| Stock faces converted to 14 panels | `data/anims/`, `tools/convert_mk2_to_14.py` | The bundled faces are drawn for 11 panels. Originals are kept in `anims_original_11panel/`. |
| **Crash fix:** face changes are queued for the main loop | `src/main.cpp`: `/change` handlers, BLE handler, `loop()` | The web server freed the current face's frames while the drawing loop was still reading them, which crashed the board (null pointer at `main.cpp:886`) |
| Homepage face buttons work in place | `data/index.html` | Tapping a face used to jump to a "Saved" page and back |
| MAX7219-only web pages | `data/index.html`, `data/animator.html`, `data/visType.txt` | RGB, ear-LED, cheek-LED and other-helmet controls removed from view. Some Animator controls are hidden rather than deleted, because loading a face still writes its ear data into them. |
| Live brightness slider (0–15) | `data/index.html`; new `/brightness` endpoint replacing `/rgb` in `src/main.cpp`; default 5 in `src/fileOp.cpp` | The old setting was a 0–255 number box meant for RGB panels. The main loop applies the change, since the web server can't safely drive the panels mid-draw. |
| Phone-friendly redesign | `data/index.html`, `data/animator.html`, `data/saved.html` | Face tiles with previews, cards, switches and bigger touch targets; the Animator scales its drawing grid to the screen. The old pages are kept in `ui_backup/` and git tag `ui-before-redesign`. |
| Scrolling text | `data/index.html`, `src/main.cpp` | The browser turns the message into an animation (5×7 font from MD_MAX72XX) and sends it the same way as the Animator's "Try animation". "Play once" uses a new optional `then` field: when the animation reaches its end, the firmware loads that face. |
| Touch drawing | `data/animator.html` | Finger drags draw or erase, depending on the first pixel touched. Drags that start between panels still scroll. |
| Display power button | `src/main.cpp` (`/display`), `data/index.html` | Turns the panels off with the MAX7219 shutdown mode while the ESP32, WiFi and current face keep running, and brings the picture back instantly. Not saved, so the displays are always on after a restart. |
| Physical button | `src/main.cpp` | A short press goes to the face after the one showing (the original skipped a press every lap and kept its own place in the list). Holding it for 1.5 s turns the displays off or on. |
| BMI270 tilt sensor | `platformio.ini`, `src/main.cpp` | Replaces the LSM6DS3 driver (whose chip check also rejects common LSM6DS3TR-C clones). The loop reads it every 100 ms and the web page gets the stored value. It tries addresses 0x68 and 0x69, and the calibration Read buttons work with Tilt switched off. |
| Boop sensor | `src/main.cpp`, `data/index.html` | APDS9960 selected, started at power-on even with Boop off, and looked for every 5 s if missing. The original switched Boop off after one failed start. `/tof` fixed (it could send two replies) and a Read button added next to Boop distance. |
| Long animations | `src/main.cpp` | The frame counter was 8-bit, so animations over 255 frames (scrolling text over about 30 characters) restarted early; it's 16-bit now |
| System monitor | `src/main.cpp` (`/stats`), `data/index.html` | A System card showing uptime, chip temperature, loop speed, WiFi devices, memory, PSRAM, storage, the current frame, and whether each sensor was found |
| Sleep mode | `src/main.cpp`, `src/fileOp.*`, `data/index.html`, `data/anims/sleep.json` | A moon button shows the sleeping face; with Auto sleep on, the mask does it after a set number of minutes without movement (BMI270) and wakes when moved or when a face is picked. The face itself is generated by `tools/make_sleep_face.py`. |
| Face manager | `data/index.html` | A Manage button on the Faces card adds Rename and Delete to every tile. Upload and download already existed. The firmware has no rename, so the page saves the face under the new name and deletes the old one. Deleting `default.json` (loaded at startup) or the face currently showing is blocked. |
| Face transitions | `src/main.cpp`, `src/fileOp.*`, `data/index.html` | Changing face dips the panels dark, swaps, and comes back, 80 ms each way by default and adjustable from the Display card. The MAX7219 chain runs in wiring order rather than the order panels sit on the face, so a wipe would sweep raggedly across it; dimming is global and looks right whatever the layout. On by default, switchable under Features. |
| Face previews survive a dropped request | `data/index.html` | One failed `visType.txt` fetch used to `return` out of `loadPreviews()`, leaving every tile black with nothing reported, and an empty layout made `drawFace` set a negative canvas width, which throws. Both fetches now retry three times, an unusable layout is caught, and a failure says so instead of showing blank tiles. |
| Original eyes, redrawn mouths | `data/anims/`, `tools/make_faces.py`, `tools/render_faces.py` | The eyes are the ones the mask came with, read straight out of `anims_14panel_v1/` so they are that art exactly, including each face's own blink. What changed is the pacing: the originals held still for up to 10 seconds, and long holds are now capped so a face blinks or twitches every few seconds, with a slow one-pixel lift for the eyes that never moved at all. The mouths are redrawn from curves, two pixels thick, because the originals were converted from an 11-panel visor and came out blocky. The nose is the original, pixel for pixel. New faces: `heart` and `surprise`, the only two with eyes drawn here. `bsod` is left alone. |
| Half-face previews | `data/index.html` | Nearly every face is a mirror image, so a preview tile shows only the left half, at twice the size in the same tile. The two halves are compared pixel by pixel first, with a few pixels of slack for faces that are a pixel or two off perfect (happy is one); a face whose halves genuinely differ is drawn whole. |
| Mouth bob toggle | `src/main.cpp`, `src/fileOp.*`, `data/index.html` | A **Mouth bob** switch under Features lifts the mouth a row every second or so, on whatever face is showing. Done in the firmware rather than baked into the animations, so it can be switched on and off while running and works on any face, including ones drawn later. It uses the `isMouth` flags the speech feature already relies on. Off by default. |
| Boop counter | `src/main.cpp`, `src/fileOp.*`, `data/index.html` | Every boop is counted, and the running total pops up over both nose panels for about 1.4 s - a squashed bar, an overshoot, then the number - before the face returns. Drawn by the firmware over whatever face is showing, using a 3x5 font; two digits fit a nose panel, so past 99 the nose shows the last two while the System card shows the real total. The total is kept in the config but only written a while after the boops stop, so the flash isn't rewritten on every poke. The virtual tester has a Boop button, since the sensor can't be simulated. |
| Wake sensitivity | `src/main.cpp`, `src/fileOp.*`, `data/index.html` | Auto sleep used a hard-coded 0.08 g of movement to wake, which a knock on the table set off. It's now a **Wake sensitivity** setting (default 0.2 g) and movement has to hold for three readings in a row, so one noisy sample can't wake the mask. |
| Home screen icon | `data/manifest.json`, `data/icon-*.png`, `data/index.html`, `tools/make_icons.py` | The page installs on a phone with its own icon. iOS opens it fullscreen; Android needs a service worker for that, which can't be registered over plain HTTP to an IP, so there it stays a shortcut into the browser. |
| Setup guide | `MY_PROTOGEN_SETUP.md` | Everything needed to rebuild, flash and maintain this mask |

The crash fix and the homepage change aren't specific to this mask. They apply to upstream ProtoESP as well.

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

The full guide covers wiring, flashing, the Animator, the converter, adding sensors, reading crash reports and troubleshooting: **[MY_PROTOGEN_SETUP.md](MY_PROTOGEN_SETUP.md)**.

## Converting other 11-panel faces

Most ProtoESP faces you'll find are drawn for the 11-panel "M16 mk2" layout. To use them on this mask, put them in `data/anims/` and run:

```
cd ProtoESP-Controller
python tools/convert_mk2_to_14.py
```

It converts every 11-panel face in place and skips faces that are already 14-panel. Eyes are copied as-is. Each mouth side gains a fourth panel by repeating the panel nearest the centre, and the single centre nose is split across the two nose panels. Upload the filesystem afterwards.

## Testing the web pages without the mask

`tools/virtual_tester.py` runs the homepage and Animator on your PC against a pretend mask, so web page changes can be tried without uploading to the board:

```
cd ProtoESP-Controller
python tools/virtual_tester.py
```

The dashboard at `http://localhost:8080/__tester` shows the page next to a virtual 14-panel face that plays the current face at the current brightness, plus a log of every request. Pages are served live from `data/`. Faces and settings live on a separate virtual board (`tools/.virtual_board/`, not committed), so testing never changes `data/`. Firmware changes still need a real build and flash.

## Repository layout

| Path | Contents |
|---|---|
| `ProtoESP-Controller/` | The firmware (PlatformIO project), with the web pages and faces in `data/` |
| `ProtoESP-Controller/tools/` | The face converter and the virtual tester |
| `MY_PROTOGEN_SETUP.md` | Full setup and usage guide for this mask |
| `ProtoESP-Controller/ui_backup/` | The web pages from before the redesign |
| `README-ProtoESP-upstream.md` | NCPlyn's original README, with links to the upstream docs |
| `ProtoESP-Remote/`, `RGBMatrix/` | Upstream wireless remote and RGB panel projects, unchanged and not used here |

## Credits and license

Based on [ProtoESP (ProtogenHelmet-ESP32)](https://github.com/NCPlyn/ProtogenHelmet-ESP32) by [NCPlyn](https://github.com/NCPlyn), licensed under GPL-3.0. This repo is GPL-3.0 as well (see `LICENSE`). NCPlyn's credit line in `index.html` and `animator.html` is kept, as the original project asks. If ProtoESP is useful to you, consider supporting NCPlyn; the links are in the upstream README.

The scrolling-text font is the system font from the [MD_MAX72XX](https://github.com/MajicDesigns/MD_MAX72XX) library by MajicDesigns (LGPL-2.1).

The face hardware is by FurBitten Studios. Their firmware is not included here.
