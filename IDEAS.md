# Ideas for later

Things worth adding to the mask, parked here so they don't get lost. Roughly ordered by
value-for-effort within each group.

## Done

- [x] **Face manager** — Manage button on the Faces card, with Rename and Delete per face.
- [x] **Face transitions** — a dip to black when the face changes, switchable under Features.
- [x] **Add to home screen** — manifest and icons. Fullscreen on iOS; on Android it stays a
      browser shortcut, because a service worker can't register over plain HTTP to an IP.

## Software only — nothing to buy

- [ ] **Idle blinks** — the current face blinks every so often. Generated the same way as the
      sleeping face, so `tools/make_sleep_face.py` is the starting point.
- [ ] **Quick phrases** — saved scrolling-text messages as one-tap buttons. The text engine
      already exists; this just stores favourites.
- [ ] **Sleep refinements** — dim gradually before dropping off, wake more gently, deeper sleep
      after longer.

## Needs the APDS9960 (bought for boop, not wired yet)

The chip has three sensors in one: proximity (the boop), gesture, and ambient light. Once it's
wired for boop, the other two come along free.

- [ ] **Auto-brightness** — read the ambient light sensor every ~500 ms, map it to MAX7219
      intensity 0–15 on a log curve, smooth it, apply only on a full step change with hysteresis,
      clamp to a Min and Max. Watch for: the sensor seeing the panels' own glow and running away
      (mount it facing outward, slow the response); the clear channel saturating in sunlight; and
      **power** — driving to brightness 15 outdoors pulls far more than the ~3 W at brightness 5,
      which can brown out the board when the panels are fed from its 5 V pin.
- [ ] **Gesture control** — swipe past the nose to change faces without the phone.
- [ ] **Boop counter** — total boops on the System card.

## Needs parts

- [ ] **Fan curve** — the PWM is already set up on GPIO 13 at 25 kHz, it's just fixed-duty. Make it
      follow the chip temperature the System card already reads. Only needs the fan fitted.
- [ ] **Microphone (MAX9814)** — the mouth moves with your voice. The biggest visual payoff left,
      and the thing that makes people ask how it works.
- [ ] **INA219 power monitor** — real watts on the System card. Would answer the
      brightness-versus-supply question properly, which matters before auto-brightness.
- [ ] **OLED** — small status screen inside the helmet.
- [ ] **BLE remote** — ProtoESP's own remote project, already in the repo and unused.
