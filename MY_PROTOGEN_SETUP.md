# My Protogen Setup

ProtoESP firmware ([NCPlyn/ProtogenHelmet-ESP32](https://github.com/NCPlyn/ProtogenHelmet-ESP32), GPL-3.0) running a FurBitten Studios premade face (14 × MAX7219) on an ESP32-S3.

Last updated: 2026-09-11. Everything below was tested on this mask and this PC.

---

## 1. Hardware

| Part | Details |
|---|---|
| Controller | Lonely Binary ESP32-S3 "Gold Edition", module ESP32-S3-N16R8 (16MB flash, 8MB PSRAM) |
| Displays | FurBitten premade face, 14 × MAX7219 8×8 panels, already chained together |
| Original controller | Arduino Nano with FurBitten's closed firmware. **Unplugged and kept as a backup. Don't reflash it**; their firmware can't be recovered. |
| Connected right now | Displays and a BMI270 tilt sensor. The APDS9960 boop sensor is set up in the firmware but not wired yet. No mic, OLED or cheek LEDs. |

### USB ports on the board

| Port | Shows up as | Use it for |
|---|---|---|
| Left USB-C, labelled **UART** | `USB-SERIAL CH340K`, was **COM113** | Flashing **and** reading the startup messages |
| Right USB-C, labelled **USB** | `303A:1001`, was **COM114** | Flashing only (no startup messages on this one) |

COM numbers can change. Run `python -m platformio device list` (see section 5) to check.
You never need the BOOT button. Both ports put the board into flashing mode automatically.

---

## 2. Wiring

### Displays (the only thing wired right now)

Connect to the **first panel in the chain: left eye, back panel**.

| ESP32-S3 pin | MAX7219 pad |
|---|---|
| GPIO 11 | DIN |
| GPIO 10 | CS (sometimes labelled LOAD) |
| GPIO 12 | CLK |
| GND | GND |

- **Panel power:** the panels currently run from the board's `5V0` pin with the board on USB. That measured about 3W with the stock faces at brightness 5, which is fine. Each lit pixel draws roughly 1.7mA at brightness 5. The bsod face lights every pixel (about 1.5A, 7.6W), and that, or much higher brightness, is more than USB can supply through the board. For the helmet, wire a power bank's 5V to both the board's `5V0` pin and the panels in parallel, so the panel current doesn't go through the board. Worst case, with every pixel on at full brightness, is about 4.5A.
- **Ground must be shared** between the supply and the ESP32. With no shared ground you get garbage or nothing.
- GPIO 10, 11 and 12 sit next to each other on the left header, with GND at the bottom of the same header.

### Level shifter (not fitted, currently works without)

The ESP32 outputs 3.3V; the MAX7219 officially wants 3.5V at 5V supply. It works on the bench. If you ever see glitches, especially once the helmet is warm, fit a **74AHCT125** between the ESP32 and the first panel:

```
ESP32 GPIO 11 -> pin 2  (1A)    pin 3  (1Y) -> DIN
ESP32 GPIO 10 -> pin 5  (2A)    pin 6  (2Y) -> CS
ESP32 GPIO 12 -> pin 9  (3A)    pin 8  (3Y) -> CLK
pins 1, 4, 10 -> GND            pin 14 -> 5V     pin 7 -> GND
pin 13 -> 5V, pin 12 -> GND     0.1uF capacitor between pins 14 and 7
```

### Other pins, for when you add parts later

| GPIO | Part |
|---|---|
| 21 + 47 | Momentary button across these two neighbouring pins on the right header (47 is directly above 21). The firmware holds 47 low as the button's ground, so no GND wire or resistor is needed. Short press: next face. Hold 1.5 s: displays off/on. |
| 5 | Cheek/blush WS2812 LEDs |
| 8 / 9 | I2C SDA / SCL, shared by all I2C parts: BMI270 tilt sensor (address 0x68 or 0x69), APDS9960 boop sensor (0x39), and later an OLED. Power the sensors from **3V3**, not 5V. |
| 1 | Microphone (MAX9814) |
| 2 / 42 | KY-032 boop sensor in / enable |
| 13 | 4-pin fan PWM |

**Never use GPIO 35, 36 or 37.** They're printed on the board, but the PSRAM uses them internally.

---

## 3. Panel layout

Panel order along the chain (t0 = first):

1. t0: left eye, back
2. t1: left eye, front
3. t2 to t5: left mouth, back to front
4. t6: left nose
5. t7: right nose
6. t8 to t11: right mouth, front to back
7. t12: right eye, front
8. t13: right eye, back

How the Animator draws it (as someone facing the mask sees it, so the left of the screen is the wearer's right):

```
t13 t12  .   .   .   .   .   .  t1  t0      eyes
 .   .   .  t7   .   .  t6   .   .   .      noses
t11 t10 t9  t8   .   .  t5  t4  t3  t2      mouth
```

When you tested with numbers, panel "1" was t0, "2" was t1, and so on.

---

## 4. What's changed from the stock project

| File | Setting | Value | Why |
|---|---|---|---|
| `src/main.cpp:16` | `visorType` | `"MAX72XX"` | MAX7219 panels, not RGB |
| `src/main.cpp:17` | `HARDWARE_TYPE` | `PAROLA_HW` | Stock value mirrored every panel left-to-right |
| `src/main.cpp:18` | `MATRIXESNUM` | `14` | 14 panels |
| `src/main.cpp` | `boopMode` | `"APDS9960"` | APDS9960 boop sensor on the shared I2C bus |
| `platformio.ini`, `src/main.cpp` | Tilt sensor | BMI270 (SparkFun library) instead of the LSM6DS3 | Any mounting orientation; calibrate with the Read buttons. Tries I2C address 0x68, then 0x69. |
| `src/main.cpp`: `/change` handlers, BLE handler, `loop()` | Web and remote face changes are queued; `loop()` does the load | Changing faces from the homepage crashed the board (see section 9) |
| `data/index.html` | Face buttons change the face in the background | Tapping a face used to jump to a "Saved/changed OK" page and back |
| `data/index.html` | RGB-only button and settings removed; brightness is a 0–15 slider | This mask only has MAX7219 panels |
| `data/animator.html` | Ear, colour and other-helmet layout controls hidden; opens straight into this layout | Same. They're hidden rather than deleted because loading a face still writes its ear data into them. |
| `data/visType.txt` | Cheek-LED (blush) cells removed | No RGB cheek LEDs on this mask |
| `src/main.cpp` | `/rgb` endpoint replaced by `/brightness` | Live brightness for the slider |
| `src/fileOp.cpp` | Default brightness 5 instead of 100 | 100 was an RGB value; MAX7219 brightness only goes to 15 |
| `data/index.html`, `data/animator.html`, `data/saved.html` | Redesigned for phones: face previews, cards, switches, bigger touch targets | The old pages were built for desktop screens. The pre-redesign pages are kept in `ui_backup/before-redesign-2026-09-11/` and git tag `ui-before-redesign`. |
| `src/main.cpp`, `data/index.html` | `/display` endpoint and a top-bar power button | Turns the panels off (MAX7219 shutdown mode) while the ESP32 keeps running. Not saved, so the displays are always on after a restart. |
| `src/main.cpp` | Optional `then` field in animations | When a one-shot animation (scrolling text set to Play once) reaches its last frame, the firmware loads that face |
| `src/main.cpp` | Frame index widened from 8 to 16 bits | Animations over 255 frames (scrolling text over about 30 characters) restarted early and Play once never fired |
| `src/main.cpp`, `data/index.html` | `/stats` endpoint and a System card | Resource monitor on the homepage; it only reports stored values, so it never touches the sensor wires |
| `data/anims/sleep.json` | New face: closed eyes and a breathing mouth | Built to match a reference clip: 14 frames, about 1.7 s per breath. Rebuild it with `tools/make_sleep_face.py`. |
| `src/main.cpp`, `src/fileOp.*`, `data/index.html` | Sleep mode: `/sleep`, Auto sleep, Sleep after, Sleeping face | Shows the sleeping face on demand or after the mask is left still; moving it or picking a face wakes it |
| `src/main.cpp` | Button: a short press goes to the face after the current one; holding 1.5 s toggles the displays. Moved from GPIO 4 to GPIO 21, with GPIO 47 held low as its ground. | The original skipped a press on every lap of the face list and kept its own place in the list. The new pins put both button wires side by side on the right header, away from the display wires. GPIO 19 and 20 were avoided because they're the USB data lines. |
| `platformio.ini:12` | partitions | `8MB_20-20-40.csv` | More room: 2MB app, 3.9MB for faces |
| `data/visType.txt` | layout | your chain order | See section 3 |
| `data/visTypeMouth.txt` | mouth mask | 14 entries | Which panels move when talking |
| `data/config.json` | speech, boop, BLE, OLED | off | Parts not fitted (speech on with no mic makes the mouth flap randomly) |
| `data/anims/*.json` | all 8 faces | converted to 14 panels | See section 7 |

Build size: RAM 18.6%, flash 62.9%.

---

## 5. Building and flashing

There are two ways. Both do the same thing.

### Option A: PowerShell commands (what was used to set this up)

Open **PowerShell** (not Git Bash, see the warning below) and run:

```powershell
$env:PLATFORMIO_CORE_DIR = 'C:\pio'
cd C:\Users\MainPC\Documents\protogen-fw\ProtoESP-Controller

# find the board's COM port
python -m platformio device list

# 1. build and flash the firmware (the code)
python -m platformio run -e esp32-s3 -t upload --upload-port COM114

# 2. flash the filesystem (web pages, layout, faces, settings)
python -m platformio run -e esp32-s3 -t uploadfs --upload-port COM114

# watch the startup messages (left UART port only, Ctrl+C to stop)
python -m platformio device monitor -p COM113 -b 115200
```

- The `$env:` line only lasts for that PowerShell window. Run it again in a new window.
- To build without flashing, use `python -m platformio run -e esp32-s3`.

### Option B: VS Code

1. Install VS Code and the **PlatformIO IDE** extension.
2. Before the first use, delete `C:\Users\MainPC\.platformio`. It's a broken half-install from the first attempt. The extension will download a fresh copy.
3. `File > Open Folder` and choose `protogen-fw\ProtoESP-Controller` (the inner folder).
4. In the PlatformIO sidebar under `esp32-s3`: **Upload** for the firmware, then **Platform > Upload Filesystem Image** for the filesystem.

### Rules that matter

- **Firmware and filesystem are separate.** Changes in `src/` or `platformio.ini` need **Upload**. Changes in `data/` (faces, layout, `config.json`) need **Upload Filesystem Image**.
- **Uploading the filesystem replaces everything stored on the board.** Settings changed in the web page go back to what's in `data/config.json`, and any face saved with "Save to SPIFFS" is deleted. Copy faces into `data/anims/` first if you want to keep them.
- **Don't build from Git Bash.** Espressif's installer refuses to run there, and the build then fails with `'xtensa-esp32s3-elf-g++' is not recognized`. Use PowerShell or VS Code.
- If you edit `data/config.json` by hand, its checksum is regenerated automatically when the filesystem is built. If the startup log says the CRC check failed, run `python genCRC-manual.py` and upload the filesystem again.

---

## 6. Using the mask

1. Power up. The board creates a WiFi network.
2. On your phone, turn **mobile data off** and join `ProtoWiFi`, password `Proto1234`.
3. Open `192.168.4.1`. Tap a face tile to show that face; each tile previews the face. Since nearly every face is a mirror image, the tile shows just the left half at twice the size, so it's readable on a phone; a face whose halves really are different is shown whole. The **Brightness** slider (0–15) changes the panels live and saves when you let go. The feature switches, Advanced settings and Tilt calibration each have their own Save button.
4. Open `192.168.4.1/animator.html` to draw faces.
5. To show a message, type it into **Scrolling text** and press Enter (up to 64 characters). It scrolls across the mouth panels while the eyes keep the current face. Before sending, pick a speed (Very slow, Slow, Normal or Fast) and **Repeat** or **Play once**. Repeat keeps going until you tap a face. Play once goes back to the face you had after one pass; the mask does that itself, so it works even if your phone locks.
6. The **power button** at the top right turns the displays off while the mask stays on. WiFi, the web page and the current face keep running, and tapping it again brings the face straight back. The displays always come back on after a restart.
7. The physical button across GPIO 21 and 47 (the 13mm button that came with the face) works without your phone. A short press goes to the next face after the one showing. Holding it for 1.5 seconds turns the displays off or on. The homepage doesn't refresh by itself, so reload it to see the change.
8. **Tilt:** open Tilt calibration. Hold your head normally and press Read on Idle, tilt your head sideways and press Read on Tilt, look up and press Read on Up, then press Save tilt. Then turn on **Tilt** under Features and press Save features. The Read buttons work even with Tilt switched off.
9. **Boop** (once the APDS9960 is wired): in Advanced settings, press **Read** next to Boop distance with nothing near the visor, then again with your hand on the visor. Set Boop distance between the two numbers and press Save settings; a boop triggers when the reading drops below it. Then turn on **Boop** under Features. With Boop on, the mask looks for the sensor every 5 seconds, so it's picked up without a restart.
10. The **System** card shows how the mask is doing: uptime, chip temperature, loop speed (how many times a second the main loop runs; a lower number means it's busier), connected WiFi devices, memory, PSRAM and storage use, the current face frame, and whether the tilt and boop sensors were found. It updates every 2 seconds while the page is open.
11. **Sleep:** the moon button in the top bar shows the sleeping face straight away, and tapping it again goes back to the face you had. For it to happen by itself, turn on **Auto sleep** under Features and set **Sleep after** in Advanced settings to the minutes of stillness to wait. Stillness is measured by the tilt sensor, so this needs the BMI270. Moving the mask wakes it, and so does picking any face (it then stays on that face). **Sleeping face** in Advanced settings chooses which face it uses; the included `sleep.json` is closed eyes with a slowly breathing mouth. **Wake sensitivity** in Advanced settings is how much movement counts as awake, in g: lower wakes more easily, higher needs a firmer movement. It starts at 0.2, and the movement has to hold across three readings in a row (about 300 ms) so a single knock or a noisy reading can't wake it. If it still wakes on its own, raise it; if it ignores you picking the mask up, lower it.

12. **Managing faces:** press **Manage** on the Faces card to put Rename and Delete on every tile, and press **Done** when finished. Renaming is done by the web page (the firmware has no rename): it saves the face under the new name, then deletes the old one, so a name is only ever freed once the copy has been written. Two faces are protected — `default.json`, which the mask loads at startup, and whichever face is showing right now; switch to another face first if you want to delete that one. Deleting or renaming a face that's picked in Advanced settings (tilt, boop or sleeping face) leaves that setting pointing at a name that's gone, so the page warns you to choose again. **Upload faces** and **Download faces** in Tools take a single `.json` or a `.zip` of them.
13. **Transitions:** changing face dips the panels dark, swaps, and brings them back, rather than cutting. It's on by default; turn it off with **Transitions** under Features and press Save features. The **Transition** slider on the Display card sets how long the dim takes, 20 to 400 ms, and the same again coming back — it saves as soon as you let go of it. The dip is done by stepping the panel brightness down one level at a time, so the time you set is shared out over however many brightness levels you're running; at brightness 5 the default 80 ms means a step roughly every 16 ms.
14. **Putting it on your home screen:** with the page open, use your browser's **Add to Home Screen**. On iPhone (Safari: Share → Add to Home Screen) it opens fullscreen with no browser bars, like an app. On Android it becomes a shortcut that opens in the browser — the fullscreen version needs a service worker, and browsers only allow those over HTTPS or on localhost, which a mask on `192.168.4.1` can't offer. Either way you still have to be joined to `ProtoWiFi` for it to load.

15. **Mouth bob:** turn on **Mouth bob** under Features and press Save features to make the mouth lift a pixel every second or so. It applies to whichever face is showing, including ones you draw yourself, because the firmware does it while drawing rather than it being part of the animation - so it can be switched on and off without reflashing anything. The timing is `bobPeriod` and `bobLift` near the top of `src/main.cpp` (1100 ms between lifts, held for 220 ms). Only panels flagged as mouth panels move, which is the same flag speech uses.

16. **Boop counter:** on by default (**Boop counter** under Features). After each boop the running total pops up over both nose panels for about a second and a half, then the face carries on. The **System** card shows the lifetime total. Two digits fit on a nose panel, so once you pass 99 the nose shows the last two digits and the System card shows the real number. The total survives a restart, but it is only written to flash about 20 seconds after the boops stop, so a boop session doesn't wear the flash. Needs the APDS9960 wired and **Boop** switched on, same as the boop face.

If you change the WiFi name or password, both must be at least 9 characters. To reset the board to its defaults, hold BOOT for 10 seconds while it's powered.

### Drawing and testing a face in the Animator

1. The Animator opens straight into your 14-panel layout. On a phone, turn it sideways for bigger pixels. The drawing area scrolls sideways if it doesn't fit.
2. Tap pixels to toggle them one at a time, or press **Select**, tap a panel, then **Toggle** to light the whole panel. On a phone, drag your finger across pixels to draw. Start the drag on a lit pixel to erase instead. Drags that start between panels scroll the grid.
3. Set **Timespan** (milliseconds the frame shows), then **Add frame**. Repeat for more frames.
4. **1. Generate final JSON**
5. **3. Try animation** plays it on the mask right away. It's lost on restart.
6. To keep it, type a name in **2.** and press **4. Save to SPIFFS**.

To edit an existing face: pick it under **Load animation**, press **Load selected anim**, edit, then save it again.

### Drawing without the mask

The online Animator at <https://foxxo.cz/proto/animator.html> works with no hardware. Set **Config** to **custom**, paste these two lines, then press **Load config**:

```
t13;t12;-;-;-;-;-;-;t1;t0;_;-;-;-;t7;-;-;t6;-;-;-;_;t11;t10;t9;t8;-;-;t5;t4;t3;t2
```
```
[false,false,true,true,true,true,false,false,true,true,true,true,false,false]
```

Save the file to your PC, put it in `data/anims/`, then upload the filesystem.

---

## 7. Tools

### `tools/convert_mk2_to_14.py`: convert 11-panel faces to 14 panels

The stock ProtoESP faces (and many shared online) are drawn for the 11-panel "M16 mk2" layout. On this mask they show junk on the last panels. This script converts them.

```powershell
cd C:\Users\MainPC\Documents\protogen-fw\ProtoESP-Controller
python tools\convert_mk2_to_14.py
```

- Converts every 11-panel file in `data/anims/` in place. Skips files that are already 14-panel.
- The first time it runs, it copies the originals to `anims_original_11panel/`. That folder is outside `data/`, so it isn't uploaded to the board.
- How it adapts a face:
  - **Eyes:** copied as-is (2 panels per eye on both layouts).
  - **Mouth:** 3 panels per side become 4 by repeating the panel nearest the centre, which adds one tooth.
  - **Nose:** the single centre nose is split in half, and each half is stretched to fill one nose panel.
- It refuses faces that use per-pixel colour. The stock faces don't have any.
- After running it, upload the filesystem.

### `tools/virtual_tester.py`: test the web pages without the mask

Runs the homepage and Animator on your PC with a pretend mask behind them, so you can try web page changes without uploading to the board.

```powershell
cd C:\Users\MainPC\Documents\protogen-fw\ProtoESP-Controller
python tools\virtual_tester.py
```

- It opens the dashboard at `http://localhost:8080/__tester`. The mask's web page is on the left. On the right, a virtual 14-panel face plays the current face at the current brightness. The board log underneath shows every request the page makes.
- It answers the page's requests the way the firmware does: changing faces, Try animation, saving settings, brightness, fan, and saving and deleting faces.
- **Web pages come live from `data/`.** Edit `index.html`, `animator.html` or `visType.txt`, then press **Reload** to see the change.
- **Faces and settings belong to a virtual board** in `tools\.virtual_board\`, copied from `data/` the first time it runs. Saving there never changes `data/`, and the folder isn't committed. Start it with `--reset` to copy them from `data/` again.
- Stop it with Ctrl+C. Other options: `--port 9000` and `--no-browser`.
- It doesn't run the firmware, so changes in `src/` still need a real build and flash. The mic, boop sensor and tilt aren't simulated.

### `tools/make_faces.py`: redraw the faces

Generates the faces in `data/anims/`. Each is drawn on the left half of an 80x24 canvas from curves, then mirrored, so the halves always match.

```powershell
python tools\make_faces.py --list
python tools\make_faces.py --preview happy     # print it as text, write nothing
python tools\make_faces.py                     # write the files
```

Eyes marked `original` in the `FACES` table are read from `anims_14panel_v1/`, so they are the art the mask came with, blinks included; `HOLD` caps how long one can sit still before the sequence moves on, and an eye that never moves gets a slow one-pixel lift. Mouths are the `mouth_*` functions above the table. `step` is the frame length in ms - keep it shorter than the quickest frame in a face's original eye sequence, or that frame falls between samples and is lost (angry has a 25 ms twitch, which is why its step is 30). Frames that come out identical (sub-pixel motion often rounds to the same pixels) are merged and their times added, so files stay small. It does not touch `bsod.json`; `sleep.json` comes from `make_sleep_face.py`. Upload the filesystem afterwards.

### `tools/render_faces.py`: look at the faces without the mask

Draws the faces to a PNG, one dot per LED, for checking shapes before flashing. Needs Pillow.

```powershell
python tools
ender_faces.py                   # every face, first frame
python tools
ender_faces.py --frames heart    # every frame of one face
```

### `tools/make_icons.py`: redraw the home screen icons

Draws the icons the web page uses on a phone home screen, into `data/`. Only needed if you want to change the icon; the artwork is the `ART` grid near the top of the script.

```powershell
python tools\make_icons.py
```

It writes `icon-192.png`, `icon-512.png`, `icon-180.png` (iPhone) and `icon-maskable.png` (Android, drawn smaller so it survives being cropped into a circle). Needs Pillow (`pip install pillow`). Upload the filesystem afterwards.

### `genCRC-manual.py`: fix the settings checksum

Only needed if the startup log reports a CRC failure after editing `data/config.json`:

```powershell
python genCRC-manual.py
```

Then upload the filesystem.

### `python -m platformio device list`: find the board

Lists COM ports. The board is the `CH340K` entry (left port) or `303A:1001` (right port). Ignore "Standard Serial over Bluetooth" entries.

---

## 8. Adding parts later

Some features are switched on in the web page, but some need a firmware change first:

| Part | Firmware change (`src/main.cpp`, then Upload) | Then in the web page |
|---|---|---|
| APDS9960 boop sensor | already set up | calibrate, then turn Boop on (section 6, step 9) |
| KY-032 boop sensor | `boopMode "KY-032"` | turn Boop on |
| Cheek LEDs (WS2812) | `blushPresent = true`, set `blushLedsNum`, and add the `blush0`–`blush3` cells back to `data/visType.txt` | none |
| INA219 power meter | `INApresent = true` | none |
| Microphone | none | turn Speech on |
| OLED | none | turn OLED on |
| BMI270 tilt sensor | already set up | calibrate, then turn Tilt on (section 6, step 8) |
| Wireless remote | none | turn BLE on |

See `ProtoESP-Controller/readme.md` and `touchSensors.md` for sensor wiring.

---

## 9. Troubleshooting

| Problem | Fix |
|---|---|
| Panels mirrored or upside down | Change `HARDWARE_TYPE` (`src/main.cpp:17`): `PAROLA_HW` flips left-right, `DR1CR0RR1_HW` flips up-down, `ICSTATION_HW` flips both, `FC16_HW` is stock. Then Upload. |
| Right shapes on the wrong panels | Panel order in `data/visType.txt` is wrong. Fix it, then upload the filesystem. |
| Nothing lights | Check the top-bar power button isn't off. Then check the shared GND, whether CS and CLK are swapped, and 5V at the panels. |
| Random glitches, worse when warm | Fit the 74AHCT125 (section 2) and check the 5V under load. |
| Junk on some panels with one face | It's an 11-panel face. Run the converter. |
| No web page, or "file not found" in the log | The filesystem wasn't uploaded. Upload it. |
| Can't open `192.168.4.1` | Phone must be on `ProtoWiFi` with mobile data off. |
| Mouth moves by itself | Speech is on with no microphone. Turn Speech off. |
| Tilt Read says "Tilt sensor (BMI270) not found" | Check the BMI270's 3V3, GND, SDA (GPIO 8) and SCL (GPIO 9) wires. The firmware looks for it at addresses 0x68 and 0x69 at startup, and again every 5 s while Tilt is on. |
| Boop Read says "Boop sensor not found" | Same checks for the APDS9960 (address 0x39). It shares SDA and SCL with the BMI270. |
| Tilt or boop triggers at the wrong time | Recalibrate (section 6, steps 8 and 9). Raise Tolerance for tilt if it triggers too easily, or lower it if it doesn't trigger. |
| Board restarts when you change faces | Fixed on 2026-09-11. The web server used to replace a face's frames while the drawing loop was still reading them. If it ever happens again, read the crash report (below). |
| Upload can't connect | Check the COM port. As a last resort, hold BOOT, tap RESET, release BOOT. |
| Mask stays dark after flashing through the right port | Tap RESET. |
| USB disconnects soon after the mask boots | The panels and WiFi draw through the same USB cable, and the voltage dip resets the board's USB chip. Flash through the right-hand (native USB) port, turn the displays off first, or power the panels from a separate 5V supply. The one-connection flash in the memory notes also avoids the gap between steps. |
| Build fails: `xtensa-esp32s3-elf-g++ is not recognized` or `MSys/Mingw is not supported` | You built from Git Bash. Use PowerShell or VS Code. |
| Build fails: `WindowsLongPathError` | Windows long paths are off. Already turned on for this PC on 2026-09-09. Using `C:\pio` as the PlatformIO folder also avoids it. |

Harmless build warnings you can ignore: `CONFIG_BT_NIMBLE_MAX_CONNECTIONS redefined`, `ARDUINO SPI interface selected`, and warnings from the SparkFun and Adafruit libraries.

### Reading a crash report

When the firmware crashes, it saves a report in flash (the `coredump` area). A power dip resets the board without saving one, so an empty report after a restart points to power instead. With the board plugged in, in PowerShell:

```powershell
cd C:\Users\MainPC\Documents\protogen-fw\ProtoESP-Controller
& C:\pio\penv\Scripts\python.exe -m esptool --chip esp32s3 -p COM114 read-flash 0x7f0000 0x10000 "$env:TEMP\coredump.bin"
python -c "import struct,os;b=open(os.environ['TEMP']+'/coredump.bin','rb').read();n=struct.unpack('<I',b[:4])[0];open(os.environ['TEMP']+'/core.elf','wb').write(b[24:n]) if b[24:28]==b'\x7fELF' else print('no crash report saved')"
& C:\pio\packages\tool-xtensa-esp-elf-gdb\bin\xtensa-esp32s3-elf-gdb.exe -batch -ex "bt" .pio\build\esp32-s3\firmware.elf "$env:TEMP\core.elf"
```

The last line prints the file and line number where it crashed. This only works while `.pio\build\esp32-s3\firmware.elf` matches the firmware on the board, so read the report before you rebuild. To clear an old report:

```powershell
& C:\pio\penv\Scripts\python.exe -m esptool --chip esp32s3 -p COM114 erase-region 0x7f0000 0x10000
```

---

## 10. Housekeeping and undo

- **The real project is `C:\Users\MainPC\Documents\protogen-fw`.** The folder `protogen fw` (with a space) is an old copy. Delete it; a space in the path breaks PlatformIO.
- `C:\pio` is a working PlatformIO install (over 1GB). Keep it for the PowerShell commands, or delete it if you switch to VS Code only.
- `C:\Users\MainPC\.platformio` is a broken half-install. Safe to delete.
- **Backed up on GitHub:** private repo <https://github.com/picovrfbt/protogen-fw>, branch `furbitten-14panel`. After you change something, save and upload a new snapshot:
  ```powershell
  cd C:\Users\MainPC\Documents\protogen-fw
  git add -A
  git commit -m "describe what you changed"
  git push
  ```
  Your repo is the remote called `mine`. `origin` still points at NCPlyn's repository, so you can check for their updates with `git fetch origin`.
- **Undo options:**
  - Original faces are in `anims_original_11panel/`.
  - The web pages from before the redesign are in `ProtoESP-Controller/ui_backup/before-redesign-2026-09-11/` (see `RESTORE.txt` there), and in git tag `ui-before-redesign`.
  - `git checkout ProtoESP -- <file>` restores any file to NCPlyn's original.
  - The Arduino Nano still has FurBitten's firmware. Plug it back in for the original mask.
- License: GPL-3.0. Keep NCPlyn's credit line in `index.html` and `animator.html`.
