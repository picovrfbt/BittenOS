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

## Uses the boop sensor, which is wired

The sensor fitted is an **APDS-9930**, not the 9960 originally planned - a different chip that
answers on the same address. It does proximity and ambient light. It has **no gesture engine**,
so gesture control is off the table unless the chip is swapped.

- [x] **Boop counter** — done: counts, pops up on the nose once the paw moves away, resettable.
- [x] **Light-confirmed boops** — done: the light sensor's shadow has to agree with proximity.
- ~~**Auto-brightness**~~ — dropped (2026-09-14). The light sensor is now used to confirm boops,
  and brightness stays manual; a separate brightness solution may come later.

## Needs parts

- [ ] **Fan curve** — the PWM is already set up on GPIO 13 at 25 kHz, it's just fixed-duty. Make it
      follow the chip temperature the System card already reads. Only needs the fan fitted.
- [ ] **Microphone (MAX9814)** — the mouth moves with your voice. The biggest visual payoff left,
      and the thing that makes people ask how it works.
- [ ] **INA219 power monitor** — real watts on the System card. Would answer the
      brightness-versus-supply question properly, which matters before auto-brightness.
- [ ] **OLED** — small status screen inside the helmet.
- [ ] **BLE remote** — ProtoESP's own remote project, already in the repo and unused.
