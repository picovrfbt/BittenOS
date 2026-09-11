# protogen-fw

Firmware for my protogen mask: a FurBitten Studios premade face (14 × MAX7219 8×8 LED panels) driven by an ESP32-S3, running a modified version of [ProtoESP](https://github.com/NCPlyn/ProtogenHelmet-ESP32) by NCPlyn.

The face originally came with an Arduino Nano running FurBitten's closed-source firmware. This repo replaces that controller with open firmware I can change. It contains no FurBitten code, and the Nano is kept untouched as a backup.

## What it does

- Drives all 14 panels as one face: 2 panels per eye, 4 per mouth side, 1 per nose side
- Creates its own WiFi network (`ProtoWiFi`) with a web page to switch faces, change brightness and turn features on and off
- Includes ProtoESP's web Animator for drawing faces pixel by pixel and testing them live on the mask
- Ships with 8 faces (default, happy, sad, angry, upset, confused, boop, bsod), converted to this 14-panel layout
- A redesigned, phone-friendly web page: face tiles with previews, a live brightness slider for the MAX7219 panels (0–15), feature switches, and no RGB, ear-LED or other-helmet controls
- Supports ProtoESP's optional extras: boop sensor, microphone, accelerometer, OLED and wireless remote. None are fitted yet, so they're turned off in this configuration and untested.

## Hardware

| Part | Details |
|---|---|
| Controller | ESP32-S3 board with an ESP32-S3-N16R8 module (16MB flash, 8MB PSRAM). PSRAM is required. Built on a Lonely Binary "Gold Edition" board. |
| Display | FurBitten Studios premade face: 14 × MAX7219 8×8 panels, already chained |
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

The face hardware is by FurBitten Studios. Their firmware is not included here.
