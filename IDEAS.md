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

- [ ] **Auto-brightness** — map the ambient reading to MAX7219 intensity 0–15 and apply it
      slowly, with hysteresis, clamped to a Min and Max you set.

      Decisions already made, so they don't get re-litigated:

      - **No lux figure.** A tinted visor passes infrared far better than visible light, so the
        standard lux formula is wrong through one. Calibrate two points instead — read indoors
        for the bottom of the range, outdoors for the top — the way the boop baseline works.
      - **The panels leak into the sensor**, which is a feedback loop, not just noise: brighter
        panels raise the reading, which raises brightness again. A light barrier so the sensor
        can't see the panels is the first fix and helps the boop sensor too.
      - **If blanking is needed** — briefly shutting the panels off to read true ambient — the
        user wants it **once a minute, not every few seconds**. The blink is the cost, so it has
        to be rare. Worth knowing: that means up to a minute's lag walking outdoors. A hybrid
        avoids it — watch the unblanked reading continuously, which is free, and only spend a
        blank when it suggests the light has really changed.
      - **Power.** Driving to brightness 15 outdoors pulls far more than the ~3 W at brightness 5
        and can brown out the board when the panels are fed from its 5 V pin, so the Max setting
        has to be capped to what the supply can take.

      First step is measurement, not code: with the sensor working, compare the Ambient light
      reading on the System card at brightness 0 and 15 in a dark room. If it barely moves the
      shroud is doing its job; if it jumps, blanking is required.
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
