# When something goes wrong

Things that have actually gone wrong on this mask, and what fixed them. The full guide is
[`MY_PROTOGEN_SETUP.md`](MY_PROTOGEN_SETUP.md); this is the short list to check first.

---

## Building and flashing

**Build from PowerShell, not Git Bash.** Git Bash does not get the toolchain paths right and the
build fails in ways that look like missing libraries. Every command in this repo assumes:

```powershell
$env:PLATFORMIO_CORE_DIR='C:\pio'
python -m platformio run -d BittenOS-Controller
```

`C:\pio` is deliberate — the default core directory sits under a user profile, and a space in the
path breaks the toolchain. Turn on Windows long path support too, or the install fails partway.

**The board won't boot after flashing, and the log stops at `ets_loader.c 78` or
`TG0WDT_SYS_RST`.** Flash mode. `platformio.ini` sets `board_build.flash_mode = dio` because the
HiLetgo DevKitC will not boot in QIO. The Lonely Binary Gold Edition boots either way, so DIO
covers both — don't "fix" it back to QIO. If you are flashing by hand with esptool, pass
`--flash-mode dio`.

Worth knowing how this one was diagnosed: the hang happens *before* any bootloader output, which
ruled out PSRAM and the partition table before either was touched.

**Serial garbage or a failed write at high speed.** Use **460800 baud**. 921600 produced corrupted
reads on this setup. If you pass `--after no-reset` the stub stays resident, so the next esptool
call needs `--before default-reset`.

**`file not found` on the page, or a blank UI.** The filesystem was not uploaded, or the firmware
was uploaded without it. Upload code first, then the filesystem.

**Upload order matters after a partition change.** `upload` writes the bootloader and partition
table along with the firmware; the filesystem then has to go to its new address. The filesystem
lives at `0x610000` on the 16MB table.

---

## Don't lose your settings

`uploadfs`, and writing a filesystem image over USB, **replace everything on the board including
your settings**. On a mask that is already configured, read them off first:

1. Read the partition table at `0x8000` and check it has `fsstage` — that confirms the 16MB table
2. Read the filesystem from `0x610000`, length `0x3E0000`
3. Unpack that image and copy the board's `config.json` over `data/config.json` (the helper that
   does this is not checked in — ask and it can be added to `tools/`)
4. `buildfs`, which regenerates `configCRC.txt` to match
5. `git checkout` `data/config.json` and `data/configCRC.txt` afterwards, so the repo keeps its
   own defaults

**The trap:** if the board drops off the USB mid-read, `buildfs` still succeeds and still produces
a `littlefs.bin` — one containing the *repo defaults*, not your settings. Flashing that quietly
wipes the mask. Always confirm the read finished before building.

Over-the-air filesystem updates keep settings, so once the mask is on the 16MB table this whole
problem goes away.

**Settings look wrong or reset themselves after a reboot.** Check `configCRC.txt` matches — a
mismatch makes the firmware fall back to defaults, and the next save then writes those defaults
over what you had. `genCRC-manual.py` rebuilds it.

---

## Displays

**Panels glitch, flicker or show garbage.**

1. Wire gauge: at least 0.25 mm² / 22 AWG, good copper. Thin wire is the most common cause.
2. Every ground must be connected — the panels' and the board's.
3. Feed the panels from the 5V supply directly rather than through the board's 5V pin if you are
   running bright or all-pixels-on faces.

**All or most panels fully lit.** Usually the chain never initialised. Re-upload firmware and
filesystem, and check `192.168.4.1/log` for modules failing to start.

**Every panel mirrored left to right.** `HARDWARE_TYPE` — this build uses `PAROLA_HW`.

**One face looks wrong and every other face looks right.** The fault is in that face file, not the
mask. This cost four wasted flashes once. Render the face with `tools/render_faces.py`, or load
`paneltest.json` which shows each panel its own number, before changing any firmware.

**The face stutters or pauses.** If the status screen is on, suspect it: a full 1024-byte OLED blit
blocks I²C for about 30 ms, and the same loop drives the panels. The firmware now sends only the
rows that changed. Turning the screen off in Settings is the quick way to confirm the cause.

**Cheap MAX7219 clones** vary between batches. If one panel is consistently bad, swap it.

---

## Sensors

**Boops fire by themselves, or not at all.** Settings > Boop has a live meter. With nothing near
the visor, press **Calibrate** — Closeness should sit near 0. If the raw reading sits near 255 with
nothing there, the sensor is seeing its own reflection off the visor and no setting will fix it;
it needs shielding or moving.

**Boop confirmations sitting at 0.** This one bit hard: `boopConfirm` was saved but never loaded,
so it came up 0, which bypassed the proximity check entirely *and* overwrote the real setting on
the next save. Fixed, but if boops behave strangely after an old config is loaded, set it back to 5.

**A sensor drops out.** The I²C bus is swept every 10 seconds and a sensor that stops answering is
restarted automatically, without a reboot. Check **I2C devices** on the System tab for what is
actually answering — 0x39 is the APDS-9930, 0x68 the BMI270, 0x3C the screen.

**The APDS-9930 is rejected by the library.** It answers on the same address as the APDS-9960 and
the Adafruit library refuses it by device ID. This firmware has its own driver, which logs the ID
instead of enforcing it.

---

## The remote

**Presses do nothing and the remote blinks three times.** Three blinks means it got no
acknowledgement. Either it is not paired, or the mask and remote are on different protocol
versions — **both have to be flashed together** when the packet format changes, because each end
drops a packet of the wrong length.

**Pairing.** Settings > Button remote > **Pair** on the mask, then hold **all three buttons** on
the remote for three seconds. No power cycle needed, so the case can stay shut.

**The first press after turning on *Join a network* is slow.** Expected. The mask's access point
follows the router's channel, so the remote walks the channels to find it again — about 800 ms,
once — then writes the new channel down and is quick again.

**A hold runs the press action as well.** Fixed: press, hold and double tap are decided before
anything is sent. If it comes back, the remote is on old firmware.

**Flashing a sleeping remote.** It is only awake for a moment. Start the upload, then hold a button
to keep it awake — the retry window waits up to 90 seconds.

---

## Reading a crash

`192.168.4.1/log` shows the log from startup. A crash report is kept in the coredump partition at
`0xDD0000`; section 9 of the setup guide covers pulling it off and decoding it.
