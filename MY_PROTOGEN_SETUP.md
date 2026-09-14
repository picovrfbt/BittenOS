# My Protogen Setup

ProtoESP firmware ([NCPlyn/ProtogenHelmet-ESP32](https://github.com/NCPlyn/ProtogenHelmet-ESP32), GPL-3.0) running a FurBitten Studios premade face (14 × MAX7219) on an ESP32-S3.

Last updated: 2026-09-13. Everything below was tested on this mask and this PC, except where it says otherwise.

---

## 1. Hardware

| Part | Details |
|---|---|
| Controller | Lonely Binary ESP32-S3 "Gold Edition", module ESP32-S3-N16R8 (16MB flash, 8MB PSRAM) |
| Displays | FurBitten premade face, 14 × MAX7219 8×8 panels, already chained together |
| Original controller | Arduino Nano with FurBitten's closed firmware. **Unplugged and kept as a backup. Don't reflash it**; their firmware can't be recovered. |
| Connected right now | Displays, a BMI270 tilt sensor and an APDS-9930 boop sensor. No mic, OLED or cheek LEDs. |

### USB ports on the board

| Port | Shows up as | Use it for |
|---|---|---|
| Left USB-C, labelled **UART** | `USB-SERIAL CH340K` | Flashing **and** reading the startup messages |
| Right USB-C, labelled **USB** | `303A:1001` | Flashing only (no startup messages on this one) |

**COM numbers change** between plug-ins — the left port has been COM113 and COM115. Run `python -m platformio device list` (section 5) to check before every flash.
You never need the BOOT button. Both ports put the board into flashing mode automatically.

---

## 2. Wiring

### Displays

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

### Other pins

| GPIO | Part |
|---|---|
| 21 + 47 | Momentary button across these two neighbouring pins on the right header (47 is directly above 21). The firmware holds 47 low as the button's ground, so no GND wire or resistor is needed. Short press: next face. Hold 1.5 s: displays off/on. |
| 8 / 9 | I2C SDA / SCL, shared by all I2C parts: BMI270 tilt sensor (address 0x68 or 0x69), APDS-9930 boop sensor (0x39), and later an OLED. Power the sensors from **3V3**, not 5V — the APDS-9930 is not 5V tolerant. |
| 5 | Cheek/blush WS2812 LEDs (not fitted) |
| 1 | Microphone, MAX9814 (not fitted) |
| 2 / 42 | KY-032 boop sensor in / enable (not used) |
| 13 | 4-pin fan PWM (not fitted) |

**Never use GPIO 35, 36 or 37.** They're printed on the board, but the PSRAM uses them internally.

### Mounting the boop sensor

A proximity sensor behind a visor sees its own infrared bouncing off the inside, which raises its resting reading (section 6, step 9 covers calibrating for it). To keep that reflection small:

- Put a light barrier — a strip of foam or black tape — between the sensor's **LED** and its **detector**. They're a couple of millimetres apart on the chip. To tell which end is which, point a phone camera at the sensor while the mask is on: phone cameras see infrared, so the LED shows as a faint purple-white glow.
- Mount it right against the visor, with no air gap behind it. The gap is what creates the bounce. Dark visor plastic usually passes infrared well even when it looks opaque.

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

The full list, grouped by area, is in the [README](README.md#changes-from-upstream-protoesp). These are the settings you're most likely to look for:

| File | Setting | Value | Why |
|---|---|---|---|
| `src/main.cpp` | `visorType` | `"MAX72XX"` | MAX7219 panels, not RGB |
| `src/main.cpp` | `HARDWARE_TYPE` | `PAROLA_HW` | Stock value mirrored every panel left-to-right |
| `src/main.cpp` | `MATRIXESNUM` | `14` | 14 panels |
| `src/main.cpp` | `boopMode` | `"APDS9930"` | The fitted sensor is an APDS-9930, which has its own driver: the APDS9960 library refuses it. Set `"APDS9960"` if you ever fit a 9960. |
| `src/main.cpp` | `bobPeriod`, `bobLift` | `1100`, `220` | Mouth bob timing: ms between lifts, and how long each lasts |
| `src/main.cpp` | `boopStages` | table | The boop counter's pop animation, stage by stage |
| `platformio.ini` | partitions | `16MB_ota.csv` | Uses the whole 16MB: two 3MB firmware slots, the filesystem, and a staging area for safe over-the-air updates |
| `data/visType.txt` | layout | your chain order | See section 3 |
| `data/visTypeMouth.txt` | mouth mask | 14 entries | Which panels move when talking |
| `data/config.json` | speech, BLE, OLED | off | Parts not fitted (speech on with no mic makes the mouth flap randomly) |
| `data/anims/*.json` | 12 faces | built by `tools/make_faces.py` | See section 7 |
| `src/fileOp.cpp` | brightness default | `5` | MAX7219 brightness only goes to 15; stock used an RGB value of 100 |

The firmware is 1.31MB, 44% of its 3MB slot.

---

## 5. Building and flashing

### The easy way, once the mask is set up: over WiFi

In the web page, **Tools > Update firmware**, and choose a file from `ProtoESP-Controller\.pio\build\esp32-s3\`. See section 6, step 18. No cable, and settings are kept.

That needs the board to already have the 16MB partition table. Getting it there — and recovering a mask that won't boot — needs USB.

### Over USB: PowerShell commands (what was used to set this up)

Open **PowerShell** (not Git Bash, see the rules below) and run:

```powershell
$env:PLATFORMIO_CORE_DIR = 'C:\pio'
cd C:\Users\MainPC\Documents\protogen-fw\ProtoESP-Controller

# find the board's COM port - it changes, so check every time
python -m platformio device list

# 1. build and flash the firmware (also writes the bootloader and partition table)
python -m platformio run -e esp32-s3 -t upload --upload-port COM115

# 2. flash the filesystem (web pages, layout, faces, settings)
python -m platformio run -e esp32-s3 -t uploadfs --upload-port COM115

# watch the startup messages (left UART port only, Ctrl+C to stop)
python -m platformio device monitor -p COM115 -b 115200
```

- Replace `COM115` with whatever `device list` shows.
- The `$env:` line only lasts for that PowerShell window. Run it again in a new window.
- To build without flashing, use `python -m platformio run -e esp32-s3`. To build the filesystem image without flashing, add `-t buildfs`.

### Over USB: VS Code

1. Install VS Code and the **PlatformIO IDE** extension.
2. Before the first use, delete `C:\Users\MainPC\.platformio`. It's a broken half-install from the first attempt. The extension will download a fresh copy.
3. `File > Open Folder` and choose `protogen-fw\ProtoESP-Controller` (the inner folder).
4. In the PlatformIO sidebar under `esp32-s3`: **Upload** for the firmware, then **Platform > Upload Filesystem Image** for the filesystem.

### Keeping your settings through a USB flash

**Uploading the filesystem over USB replaces everything stored on the board.** Settings go back to `data/config.json`, and faces you saved from the phone are deleted. Over WiFi this doesn't happen — the update keeps them — so prefer that when you can. When you do need USB:

1. In the web page, **Tools > Download config** to save your settings to the phone, and **Download faces** if you've added any.
2. Flash as above.
3. **Tools > Load config**. The feature switches apply straight away; press Save in Advanced settings and Tilt calibration to keep the rest.
4. Upload your faces again if you downloaded them.
5. Press **Calibrate** next to Boop distance again. The resting level and the boop count aren't restored this way.

### Moving to the 16MB partition table (one time)

A mask still on the old 8MB table needs one USB flash before over-the-air updates work. `upload` writes the new partition table along with the firmware, and `uploadfs` puts the filesystem at its new address, so the commands above do it — use **Keeping your settings through a USB flash** so nothing is lost. For reference, the filesystem moved from `0x410000` to `0x610000`, and the crash report area from `0x7F0000` to `0xDD0000`.

### Rules that matter

- **Firmware and filesystem are separate.** Changes in `src/` or `platformio.ini` need **Upload** (or `firmware.bin` over WiFi). Changes in `data/` (faces, layout, web pages) need **Upload Filesystem Image** (or `littlefs.bin` over WiFi).
- **Don't build from Git Bash.** Espressif's installer refuses to run there, and the build then fails with `'xtensa-esp32s3-elf-g++' is not recognized`. Use PowerShell or VS Code.
- If you edit `data/config.json` by hand, its checksum is regenerated automatically when the filesystem is built. If the startup log says the CRC check failed, run `python genCRC-manual.py` and upload the filesystem again.

---

## 6. Using the mask

1. Power up. The board creates a WiFi network.
2. On your phone, turn **mobile data off** and join `ProtoWiFi`, password `Proto1234`.
3. Open `192.168.4.1`. Tap a face tile to show that face. Since nearly every face is a mirror image, each tile shows just the left half at twice the size, so it's readable on a phone; a face whose halves really are different is shown whole. The **Brightness** slider (0–15) changes the panels live and saves when you let go. **Feature switches take effect the moment you flip them** — there's no Save button, and if the mask doesn't accept a change, the switch flips back. Remote (BLE) is the one exception: it starts at the next restart. Advanced settings and Tilt calibration still have their own Save buttons.
4. Open `192.168.4.1/animator.html` to draw faces.
5. To show a message, type it into **Scrolling text** and press Enter (up to 64 characters). It scrolls across the mouth panels while the eyes keep the current face. Before sending, pick a speed (Very slow, Slow, Normal or Fast) and **Repeat** or **Play once**. Repeat keeps going until you tap a face. Play once goes back to the face you had after one pass; the mask does that itself, so it works even if your phone locks.
6. The **power button** at the top right turns the displays off while the mask stays on. WiFi, the web page and the current face keep running, and tapping it again brings the face straight back. The displays always come back on after a restart.
7. The physical button across GPIO 21 and 47 (the 13mm button that came with the face) works without your phone. A short press goes to the next face after the one showing. Holding it for 1.5 seconds turns the displays off or on. The homepage doesn't refresh by itself, so reload it to see the change.
8. **Tilt:** open Tilt calibration. Hold your head normally and press Read on Idle, tilt your head sideways and press Read on Tilt, look up and press Read on Up, then press Save tilt. Then flip on **Tilt** under Features. The Read buttons work even with Tilt switched off.
9. **Boop:** the sensor always sees some of its own infrared reflecting off the inside of the visor, so it never reads zero at rest. In Advanced settings:
    1. Press **Calibrate** with nothing near the visor. That measures the resting level, and readings are measured from it from then on.
    2. Press **Read** with nothing near the visor. It should be close to 0.
    3. Press **Read** again with your hand on the visor.
    4. Set **Boop distance** between the two numbers and press Save settings. A boop triggers when the reading rises above it.
    5. Flip on **Boop** under Features.

    **Boop reading** on the System card shows the signal, with the raw and resting values behind it. If raw sits near 255 with nothing near the visor, the reflection has used up the whole range and no setting will fix it — see *Mounting the boop sensor* in section 2.
10. The **System** card shows how the mask is doing, updating every 2 seconds while the page is open: uptime, chip temperature, loop speed (how many times a second the main loop runs; lower means busier), connected WiFi devices, memory, PSRAM and storage use, the current face frame, whether each sensor was found, the boop total, **I2C devices** (what answered on the sensor bus), **Boop reading**, and **Ambient light**. Ambient light is two raw numbers — visible plus infrared, and infrared alone — rather than a lux figure, because a tinted visor passes infrared far better than visible light and would make a lux number wrong.
11. **Sleep:** the moon button in the top bar shows the sleeping face straight away, and tapping it again goes back to the face you had. For it to happen by itself, flip on **Auto sleep** under Features and set **Sleep after** in Advanced settings to the minutes of stillness to wait. Stillness is measured by the tilt sensor, so this needs the BMI270. Moving the mask wakes it, and so does picking any face (it then stays on that face). **Sleeping face** in Advanced settings chooses which face it uses; `sleep.json` is closed eyes with a slowly breathing mouth. **Wake sensitivity** is how much movement counts, in g. Falling asleep uses the full value and needs the movement to hold for three readings, so a knock on the table doesn't count; waking uses half of it and two readings, so picking the mask up wakes it straight away. If it wakes on its own, raise it; if it ignores you picking it up, lower it.
12. **Managing faces:** press **Manage** on the Faces card to put Rename and Delete on every tile, and press **Done** when finished. Renaming saves the face under the new name, then deletes the old one, so a name is only freed once the copy has been written. Two faces are protected: `default.json`, which the mask loads at startup, and whichever face is showing right now — switch to another face first if you want to delete that one. Deleting or renaming a face that's picked in Advanced settings (tilt, boop or sleeping face) leaves that setting pointing at a name that's gone, so the page warns you to choose again. **Upload faces** and **Download faces** in Tools take a single `.json` or a `.zip` of them.
13. **Transitions:** changing face dips the panels dark, swaps, and brings them back, rather than cutting. It's on by default; flip **Transitions** under Features to turn it off. The **Transition** slider on the Display card sets how long the dim takes, 20 to 400 ms, and the same again coming back; it saves as soon as you let go. The dip steps the panel brightness down one level at a time, so the time is shared out over however many brightness levels you're running.
14. **Putting it on your home screen:** with the page open, use your browser's **Add to Home Screen**. On iPhone (Safari: Share → Add to Home Screen) it opens fullscreen with no browser bars. On Android it becomes a shortcut that opens in the browser — fullscreen needs a service worker, which browsers only allow over HTTPS or on localhost, and a mask on `192.168.4.1` is neither. Either way you have to be on `ProtoWiFi` for it to load.
15. **Mouth bob:** flip on **Mouth bob** under Features to make the mouth lift a pixel every second or so. It applies to whichever face is showing, including ones you draw yourself, because the firmware does it while drawing rather than it being part of the face. Only panels flagged as mouth panels move — the same flag speech uses.
16. **Boop counter:** on by default (**Boop counter** under Features). The boop face plays for as long as the paw is there; once it moves away, the running total pops up over both nose panels for about a second and a half, then the face carries on, so the number never covers the boop face. Every boop is counted the moment it lands, even if the paw stays put. **Reset**, next to the total in Advanced settings, sets it back to zero. Two digits fit on a nose panel, so past 99 the nose shows the last two while the System card shows the real total. The total survives a restart, but it's only written to flash about 20 seconds after the boops stop, so a boop session doesn't wear the flash.
17. **If a sensor drops out:** you don't need to restart. The mask sweeps the I2C bus every 10 seconds, and anything that stops answering is marked missing and restarted automatically within about 5 seconds of coming back — whether or not its feature is switched on. Check **I2C devices** on the System card: `0x39` is the boop sensor, `0x68` or `0x69` the tilt sensor. If the address is listed but the sensor still won't start, the chip isn't the one the driver expects; if it isn't listed, it's wiring or power.
18. **Updating over WiFi:** **Tools > Update firmware**. Choose `firmware.bin` or `littlefs.bin` from `.pio\build\esp32-s3\`, or drop one on the box; the page tells you which it is and refuses anything else, and won't send a file that's too big for the mask. Keep the page open until it says done.
    - **Firmware** goes into the slot that isn't running and only takes over once it has fully arrived and checked out. The new firmware runs on probation: if it resets within 30 seconds of starting, the mask goes back to the previous one by itself.
    - **Filesystem** replaces the faces and web pages with the ones in the file, but keeps your settings, calibration and boop count. It's checked, then copied in at the next boot, which takes a few seconds longer than usual. If the power goes during the copy, the next boot starts it again.

    The page waits for the mask to come back and shows which slot it's running from. This needs the 16MB partition table (section 5).

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

Save the file to your PC, then upload it from the phone with **Tools > Upload faces**, or put it in `data/anims/` and update the filesystem.

---

## 7. Tools

All of these are in `ProtoESP-Controller\tools\` and run from the `ProtoESP-Controller` folder:

```powershell
cd C:\Users\MainPC\Documents\protogen-fw\ProtoESP-Controller
```

### `tools/make_faces.py`: build the faces

Builds the faces in `data/anims/`. Each is drawn on the left half of an 80×24 canvas, then mirrored, so the halves always match.

```powershell
python tools\make_faces.py --list
python tools\make_faces.py --preview happy     # print it as text, write nothing
python tools\make_faces.py                     # write the files
```

- **Eyes and nose** are the ones the mask came with. Eyes marked `original` in the `FACES` table are read from `anims_14panel_v1/`, blinks included, so they're exactly that art. `HOLD` caps how long one eye frame can sit still (the originals held for up to 10 seconds), and an eye that never moved gets a slow one-pixel lift.
- **Mouths** are the `mouth_*` functions above the table. Most are drawn two pixels thick; `mouth_original` uses a face's original mouth squashed a row flatter (default uses it), and `mouth=None` leaves the mouth dark (boop2).
- `step` is the frame length in ms. Keep it shorter than the quickest frame in a face's original eye sequence, or that frame falls between samples and is lost — angry has a 25 ms twitch, which is why its step is 30.
- Frames that come out identical are merged and their times added, so files stay small.
- It doesn't touch `bsod.json`; `sleep.json` comes from `make_sleep_face.py`.

Update the filesystem afterwards.

### `tools/render_faces.py`: look at the faces without the mask

Draws the faces to a PNG, one dot per LED, for checking shapes before flashing. Needs Pillow (`pip install pillow`).

```powershell
python tools\render_faces.py                     # every face, first frame
python tools\render_faces.py --frames heart      # every frame of one face
```

### `tools/make_sleep_face.py`: build the sleeping face

Builds `data/anims/sleep.json`: closed eyes and a slowly breathing mouth, with the nose copied from `default.json`. Run it after `make_faces.py` if the nose changes, so the two match.

### `tools/make_icons.py`: redraw the home screen icons

Draws the icons the web page uses on a phone home screen, into `data/`. Only needed to change the icon; the artwork is the `ART` grid near the top of the script.

```powershell
python tools\make_icons.py
```

It writes `icon-192.png`, `icon-512.png`, `icon-180.png` (iPhone) and `icon-maskable.png` (Android, drawn smaller so it survives being cropped into a circle). Needs Pillow.

### `tools/convert_mk2_to_14.py`: convert 11-panel faces to 14 panels

Faces shared online are mostly drawn for ProtoESP's 11-panel "M16 mk2" layout. On this mask they show junk on the last panels. This converts them.

```powershell
python tools\convert_mk2_to_14.py
```

- Converts every 11-panel file in `data/anims/` in place, and skips files that are already 14-panel.
- The first time it runs, it copies the originals to `anims_original_11panel/`, outside `data/`, so they aren't uploaded to the board.
- How it adapts a face:
  - **Eyes:** copied as-is (2 panels per eye on both layouts).
  - **Mouth:** 3 panels per side become 4 by repeating the panel nearest the centre, which adds one tooth.
  - **Nose:** the single centre nose is split in half, and each half is stretched to fill one nose panel.
- It refuses faces that use per-pixel colour.

### `tools/virtual_tester.py`: test the web pages without the mask

Runs the homepage, Animator and update page on your PC against a pretend mask, so web page changes can be tried without uploading to the board.

```powershell
python tools\virtual_tester.py
```

- It opens the dashboard at `http://localhost:8080/__tester`. The mask's web page is on the left. On the right, a virtual 14-panel face plays the current face at the current brightness. The board log underneath shows every request the page makes.
- It answers the page the way the firmware does: changing faces, Try animation, settings, brightness, fan, saving and deleting faces, mouth bob and the boop counter.
- Buttons under the virtual face simulate the physical button (a press, or a 1.5 s hold) and boops (**Boop**, or **Boop and hold (4 s)** to watch the boop face loop).
- The update page works, but uploads are only logged, never written anywhere.
- **Web pages come live from `data/`.** Edit a page, then press **Reload** to see the change.
- **Faces and settings belong to a virtual board** in `tools\.virtual_board\`, copied from `data/` the first time it runs. Saving there never changes `data/`, and the folder isn't committed. Start it with `--reset` to copy them from `data/` again — do that after rebuilding the faces, or it keeps showing the old ones.
- Stop it with Ctrl+C. Other options: `--port 9000` and `--no-browser`.
- It doesn't run the firmware, so changes in `src/` still need a real build and flash. The microphone, tilt and the real sensor readings aren't simulated.

### `genCRC-manual.py`: fix the settings checksum

Only needed if the startup log reports a CRC failure after editing `data/config.json`:

```powershell
python genCRC-manual.py
```

Then update the filesystem.

### `python -m platformio device list`: find the board

Lists COM ports. The board is the `CH340K` entry (left port) or `303A:1001` (right port). Ignore "Standard Serial over Bluetooth" entries.

---

## 8. Adding parts later

Some features are switched on in the web page, but some need a firmware change first:

| Part | Firmware change (`src/main.cpp`, then update the firmware) | Then in the web page |
|---|---|---|
| APDS-9930 boop sensor | already set up | calibrate, then flip Boop on (section 6, step 9) |
| APDS-9960 boop sensor, instead | `boopMode "APDS9960"` | calibrate, then flip Boop on |
| KY-032 boop sensor, instead | `boopMode "KY-032"` | flip Boop on |
| BMI270 tilt sensor | already set up | calibrate, then flip Tilt on (section 6, step 8) |
| Cheek LEDs (WS2812) | `blushPresent = true`, set `blushLedsNum`, and add the `blush0`–`blush3` cells back to `data/visType.txt` | none |
| INA219 power meter | `INApresent = true` | none |
| Microphone | none | flip Speech on |
| OLED | none | flip OLED on |
| Wireless remote | none | flip Remote (BLE) on, then restart the mask |

See `ProtoESP-Controller/readme.md` and `touchSensors.md` for sensor wiring.

---

## 9. Troubleshooting

| Problem | Fix |
|---|---|
| Panels mirrored or upside down | Change `HARDWARE_TYPE` in `src/main.cpp`: `PAROLA_HW` flips left-right, `DR1CR0RR1_HW` flips up-down, `ICSTATION_HW` flips both, `FC16_HW` is stock. Then update the firmware. |
| Right shapes on the wrong panels | Panel order in `data/visType.txt` is wrong. Fix it, then update the filesystem. |
| Nothing lights | Check the top-bar power button isn't off, and that brightness isn't at 0. Then check the shared GND, whether CS and CLK are swapped, and 5V at the panels. |
| Random glitches, worse when warm | Fit the 74AHCT125 (section 2) and check the 5V under load. |
| Junk on some panels with one face | It's an 11-panel face. Run the converter. |
| No web page, or "file not found" in the log | The filesystem wasn't uploaded. Upload it. |
| Can't open `192.168.4.1` | Phone must be on `ProtoWiFi` with mobile data off. |
| Face previews are black | The mask dropped a request while the page loaded. The page retries and says if it still fails; reload it. |
| A feature switch flips back on its own | The mask didn't accept the change — usually the phone briefly lost the WiFi. Flip it again. |
| Mouth moves by itself | Speech is on with no microphone. Turn Speech off. |
| Tilt sensor shows as not found | Check the BMI270's 3V3, GND, SDA (GPIO 8) and SCL (GPIO 9), and look for `0x68` or `0x69` under **I2C devices** on the System card. The mask looks for it every 5 seconds, so it picks up a fixed wire by itself. |
| Boop sensor shows as not found | Look for `0x39` under **I2C devices**. Not there: check the APDS-9930's 3V3 (not 5V), GND, SDA and SCL, which it shares with the BMI270. There but not starting: the chip doesn't match `boopMode` in `src/main.cpp`. |
| Boop never triggers, or triggers constantly | Press **Calibrate** with nothing near the visor, then set Boop distance again (section 6, step 9). If **Boop reading** shows raw near 255 at rest, the visor reflection is too strong — fit a light barrier or close the air gap (section 2). |
| Tilt triggers at the wrong time | Recalibrate (section 6, step 8). Raise Tolerance if it triggers too easily, or lower it if it doesn't trigger. |
| Update page says there's no staging area | The mask is still on the old partition table. Flash once over USB (section 5). |
| Update page refuses the file | It's not a `firmware.bin` or `littlefs.bin`, it's too big for the slot, or a filesystem image built for a different partition layout. Rebuild and try again. |
| After a firmware update the mask is back on the old version | The new firmware reset within 30 seconds of starting, so the mask rolled it back. Check the startup log over USB for why. |
| Board restarts when you change faces | Fixed on 2026-09-11. If it ever happens again, read the crash report (below). |
| Upload can't connect | Check the COM port — it changes. As a last resort, hold BOOT, tap RESET, release BOOT. |
| Mask stays dark after flashing through the right port | Tap RESET. |
| USB disconnects soon after the mask boots | The panels and WiFi draw through the same USB cable, and the voltage dip resets the board's USB chip. Turn the displays off first, flash through the right-hand port, power the panels from a separate 5V supply, or try another cable. Once the mask is on the 16MB table, update over WiFi instead. |
| Build fails: `xtensa-esp32s3-elf-g++ is not recognized` or `MSys/Mingw is not supported` | You built from Git Bash. Use PowerShell or VS Code. |
| Build fails: `WindowsLongPathError` | Windows long paths are off. Already turned on for this PC on 2026-09-09. Using `C:\pio` as the PlatformIO folder also avoids it. |

Harmless build warnings you can ignore: `CONFIG_BT_NIMBLE_MAX_CONNECTIONS redefined`, `ARDUINO SPI interface selected`, and warnings from the SparkFun and Adafruit libraries.

### Reading a crash report

When the firmware crashes, it saves a report in flash (the `coredump` area). A power dip resets the board without saving one, so an empty report after a restart points to power instead. With the board plugged in, in PowerShell (replace `COM115` with your port):

```powershell
cd C:\Users\MainPC\Documents\protogen-fw\ProtoESP-Controller
& C:\pio\penv\Scripts\python.exe -m esptool --chip esp32s3 -p COM115 read-flash 0xdd0000 0x10000 "$env:TEMP\coredump.bin"
python -c "import struct,os;b=open(os.environ['TEMP']+'/coredump.bin','rb').read();n=struct.unpack('<I',b[:4])[0];open(os.environ['TEMP']+'/core.elf','wb').write(b[24:n]) if b[24:28]==b'\x7fELF' else print('no crash report saved')"
& C:\pio\packages\tool-xtensa-esp-elf-gdb\bin\xtensa-esp32s3-elf-gdb.exe -batch -ex "bt" .pio\build\esp32-s3\firmware.elf "$env:TEMP\core.elf"
```

The last line prints the file and line number where it crashed. This only works while `.pio\build\esp32-s3\firmware.elf` matches the firmware on the board, so read the report before you rebuild. On the old 8MB partition table the report is at `0x7f0000` instead. To clear an old report:

```powershell
& C:\pio\penv\Scripts\python.exe -m esptool --chip esp32s3 -p COM115 erase-region 0xdd0000 0x10000
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
- **Ideas not built yet**, and decisions already made about them, are in [IDEAS.md](IDEAS.md).
- **Undo options:**
  - The faces as they first came, converted to 14 panels, are in `anims_14panel_v1/`. The stock 11-panel originals are in `anims_original_11panel/`.
  - The web pages from before the redesign are in `ProtoESP-Controller/ui_backup/before-redesign-2026-09-11/` (see `RESTORE.txt` there), and in git tag `ui-before-redesign`.
  - `git checkout ProtoESP -- <file>` restores any file to NCPlyn's original.
  - The Arduino Nano still has FurBitten's firmware. Plug it back in for the original mask.
- License: GPL-3.0. Keep NCPlyn's credit line in `index.html` and `animator.html`.
