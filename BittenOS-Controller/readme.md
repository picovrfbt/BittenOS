# BittenOS — Controller

The firmware that runs the mask. An ESP32-S3-N16R8 driving 14 MAX7219 8×8 panels over SPI, with
its own WiFi network and a phone-friendly web page for everything else.

Full documentation for this build — wiring, pins, settings, faces, flashing, recovery — is in
[`MY_PROTOGEN_SETUP.md`](../MY_PROTOGEN_SETUP.md) at the top of the repo. The
[README](../README.md) has the feature list.

## Layout

| | |
|---|---|
| `src/` | the firmware. `main.cpp` is the bulk of it; `fileOp` is config, `oled` the status screen |
| `data/` | what goes on the mask's filesystem: the web page, the faces in `anims/`, `config.json`, and the panel layout in `visType.txt` |
| `tools/` | the virtual tester, the standalone face maker, and the face converter |
| `lib/`, `boards/` | vendored libraries and the board definition |
| `anims_14panel_v1/`, `anims_original_11panel/` | face sets kept for reference |
| `img/` | panel-order and schematic pictures, inherited from the project this forked from |
| `ui_backup/` | a snapshot of the web page before the redesign |

## Build

PlatformIO, from PowerShell — **not** Git Bash, which does not get the toolchain paths right:

```powershell
$env:PLATFORMIO_CORE_DIR='C:\pio'
python -m platformio run -d BittenOS-Controller                 # firmware
python -m platformio run -d BittenOS-Controller -t buildfs      # filesystem
```

Two things that will bite you if changed:

- **`board_build.flash_mode = dio`** in `platformio.ini`. One of the two boards here refuses to
  boot in QIO mode; both boot in DIO, so DIO it is.
- **The 16 MB partition table** (`16MB_ota.csv`): two 3 MB app slots for OTA, the filesystem at
  `0x610000`, and a staging area above it. `configCRC.txt` is regenerated automatically when the
  filesystem is built.

## Sensors

Boop and tilt wiring, and the alternatives to the sensors fitted here, are in
[`touchSensors.md`](touchSensors.md). This mask uses an APDS-9930 for boops and ambient light,
and a BMI270 for tilt, both on the I²C bus shared with the OLED.
