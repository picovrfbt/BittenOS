# BittenOS app (Android)

A native companion app for the mask. The web page stays as it is, for anyone without the app and as
a backup.

## What it does

- **Knows when it's on the mask's WiFi.** The bar at the top says *On the mask's WiFi*, *Mask found
  on this network* (a house network or hotspot), or *Not on the mask's WiFi*. It finds the mask by
  asking: whatever answers `/stats` as BittenOS is the mask. It never reads the network's name, so
  it needs no location permission and still works if you rename the mask's network.
  It looks in three places: the mask's own address (`192.168.4.1`), the last address that worked,
  and anything announcing itself over mDNS.
- **Keeps the mask's no-internet WiFi.** It asks Android to keep that network up and sends its
  traffic over it, so Android's habit of falling back to mobile data can't take the mask away.
- **Faces, brightness, panels, sleep, rave** — the everyday controls. Everything else is on the
  full page under *More* (inside the app or in the browser).
- **Changes wait for the mask.** Brightness and voice gain can be changed while the mask is out of
  reach. A bar under the top one says how many are waiting and lists them (each can be discarded).
  When the mask is back they go across by themselves. If the same setting was changed on the mask
  in the meantime, the app asks which one to keep instead of overwriting it. Faces, sleep, panels
  and rave still need the mask in reach: they are things you want now, not later. Controls and the
  face list show what the mask had last time while it is away.
- **Voice.** Plays the mask's mic out of the phone, through **Aux** (the headphone socket, quickest) or **Bluetooth** (no cable, adds the speaker's own radio delay). It shows which output is really playing and moves to the chosen one as soon as it is plugged in or connected. The voice goes:
  - over UDP rather than the page's WebSocket, so a lost packet is skipped instead of stalling the
    sound
  - in 10 ms packets, with a 20/40/80 ms jitter buffer (*Smoothing*)
  - through Android's low-latency audio path
  - as a foreground service, so it keeps playing with the screen off.

  The Voice tab shows the delay it can measure, split into the jitter buffer and the phone's output
  stage.

UDP voice needs firmware from this commit or later. Older firmware still works for everything else,
and the Voice tab says it is waiting for sound.

## Install on the phone

The built app is `BittenOS.apk` in this folder. It is not committed to git; rebuild it with the
command below.

**USB (easiest):** on the phone, open Settings → About phone → Software information and tap
*Build number* seven times. Then turn on Developer options → *USB debugging*. Plug the phone into
the PC and run:

```powershell
& "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" install -r BittenOS.apk
```

**Without a cable:** copy `BittenOS.apk` to the phone and open it. Allow *Install unknown apps* for
whatever app you opened it from.

## Build

```powershell
.\gradlew.bat assembleDebug
```

The output is `app\build\outputs\apk\debug\app-debug.apk`. `local.properties` (the Android SDK path)
is per-PC and not committed.

## Testing without the mask

Run `BittenOS-Controller/tools/virtual_tester.py --port 8090`. Then, in the Android emulator, open
More → Mask address and enter `10.0.2.2:8090`. The tester also serves the UDP voice stream on port
7373, playing a sweeping test tone.

## Layout

The UI is kept apart from the plumbing so a redesign only touches `ui/`:

| File | What |
|---|---|
| `MaskLink.kt` | Finding the mask, connection state, every HTTP call |
| `VoiceEngine.kt` | UDP receive, jitter buffer, clock-drift trim, low-latency AudioTrack |
| `VoiceService.kt` | Foreground service, WiFi and wake locks |
| `ui/Screens.kt`, `ui/Theme.kt` | All of the UI (a placeholder look until the redesign) |
