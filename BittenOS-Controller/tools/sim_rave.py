"""Line-for-line port of rave mode's maths in src/main.cpp, run on realistic mic counts.

Re-run it after changing any RAVE_ constant: python tools/sim_rave.py

The earlier mock-up worked in a tidy 0-100 loudness. The firmware does not: it sees raw counts off
the SPH0645 after >> MIC_I2S_SHIFT, roughly +-2048 full scale, where this mask measured its quiet
room as a swing of 0 and Speech treats a swing of 500 as 100%. At that scale the danger is the
opposite of the mock-up's: an auto-gain keen enough to wake a quiet bar will also wake silence.

What has to hold, checked rather than looked at:
  1. silence is dark - a quiet room must not light the maw
  2. quiet talking registers, but does not fill it
  3. music fills it and moves, frame to frame
  4. the spectrum puts the kick at the nose and the hats out at the ear
  5. the radix-2 FFT agrees with numpy, since it is hand-written
"""
import math
import numpy as np

RATE = 16000
WINDOW_MS = 25          # MIC_WINDOW_MS: the speech block's peak-to-peak window
FRAME_MS = 33           # RAVE_FRAME_MS
FFT_N = 256             # RAVE_FFT
BANDS = 16              # RAVE_BANDS
RING = 512              # RAVE_RING
# band edges in FFT bins (62.5 Hz each); log spaced, at least one bin a band. Bin 0 is DC.
EDGES = [1, 2, 3, 4, 5, 6, 7, 8, 11, 15, 21, 28, 38, 52, 70, 94, 128]
GATE = 4.0              # RAVE_GATE: counts of swing that never count as sound
VU_MIN_PEAK = 60.0      # RAVE_VU_MIN_PEAK
SPEC_MIN_PEAK = 1500.0  # RAVE_SPEC_MIN_PEAK
FALL = 0.94             # how fast the peak sinks back, per frame
CURVE = 0.5             # the hearing-shaped curve


# ---------------------------------------------------------------- the FFT, as written in C
def fft_c(re, im):
    n = len(re)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            re[i], re[j] = re[j], re[i]
            im[i], im[j] = im[j], im[i]
    length = 2
    while length <= n:
        ang = -2.0 * math.pi / length
        wr, wi = math.cos(ang), math.sin(ang)
        for i in range(0, n, length):
            cr, ci = 1.0, 0.0
            for k in range(length // 2):
                a, b = i + k, i + k + length // 2
                xr = re[b] * cr - im[b] * ci
                xi = re[b] * ci + im[b] * cr
                re[b] = re[a] - xr
                im[b] = im[a] - xi
                re[a] += xr
                im[a] += xi
                t = cr * wr - ci * wi
                ci = cr * wi + ci * wr
                cr = t
        length <<= 1


HANN = [0.5 - 0.5 * math.cos(2 * math.pi * i / (FFT_N - 1)) for i in range(FFT_N)]
TILT = [math.sqrt((EDGES[b] + EDGES[b + 1] - 1) / 2.0) for b in range(BANDS)]


class Rave:
    def __init__(self):
        self.vu_floor = -1.0
        self.vu_peak = 0.0
        self.level = 0.0
        self.hold = 0.0
        self.band_floor = [-1.0] * BANDS
        self.spec_peak = 0.0
        self.band = [0.0] * BANDS

    def vu(self, swing):
        s = float(swing)
        if self.vu_floor < 0 or s < self.vu_floor:
            self.vu_floor = s                         # down at once
        else:
            self.vu_floor += (s - self.vu_floor) * 0.002   # up slowly: the room, not the music
        above = s - self.vu_floor - GATE
        if above < 0:
            above = 0.0
        self.vu_peak = max(above, self.vu_peak * FALL, VU_MIN_PEAK)
        self.level = min(1.0, above / self.vu_peak) ** CURVE
        cols = self.level * 32.0
        self.hold = max(cols, self.hold - 0.6)

    def spectrum(self, ring, at):
        re = [ring[(at - FFT_N + i) % RING] * HANN[i] for i in range(FFT_N)]
        im = [0.0] * FFT_N
        fft_c(re, im)
        mag = [math.hypot(re[k], im[k]) for k in range(FFT_N // 2 + 1)]
        raw = []
        for b in range(BANDS):
            v = max(mag[EDGES[b]:EDGES[b + 1]]) * TILT[b]
            if self.band_floor[b] < 0 or v < self.band_floor[b]:
                self.band_floor[b] = v
            else:
                self.band_floor[b] += (v - self.band_floor[b]) * 0.002
            s = v - self.band_floor[b] * 1.5 - GATE * 64.0
            raw.append(max(0.0, s))
        self.spec_peak = max(max(raw), self.spec_peak * FALL, SPEC_MIN_PEAK)
        self.band = [min(1.0, x / self.spec_peak) ** CURVE for x in raw]


# ---------------------------------------------------------------- the sound
def make(seconds, kind, rng):
    n = int(RATE * seconds)
    t = np.arange(n) / RATE
    a = np.zeros(n)
    if kind == 'music':
        for k in range(int(seconds / 0.5) + 1):              # 120 bpm kick
            s = int(k * 0.5 * RATE); d = np.arange(min(RATE // 6, n - s)) / RATE
            a[s:s + len(d)] += 900 * np.sin(2 * np.pi * 55 * d) * np.exp(-28 * d)
        for k in range(int(seconds / 0.125) + 1):            # hats
            s = int(k * 0.125 * RATE); d = np.arange(min(RATE // 40, n - s)) / RATE
            if len(d):
                a[s:s + len(d)] += 260 * np.sin(2 * np.pi * 7000 * d) * np.exp(-180 * d)
        a += 160 * np.sin(2 * np.pi * 110 * t)               # bass
        # Real music is broadband. Without these the test asked for mid bands lit by a signal
        # that had nothing in the mids, and failed the code for being right.
        for k in range(1, int(seconds / 0.5) + 1, 2):         # snare: broadband, on the off beat
            s = int(k * 0.5 * RATE); d = np.arange(min(RATE // 10, n - s)) / RATE
            if len(d):
                a[s:s + len(d)] += 380 * rng.normal(size=len(d)) * np.exp(-45 * d)
        chord = sum(np.sin(2 * np.pi * f * t) for f in (330, 415, 495, 990, 1980))
        a += 55 * chord * (0.6 + 0.4 * np.sin(2 * np.pi * 0.5 * t))   # a pad that swells
    elif kind == 'talk':
        env = 0.5 + 0.5 * np.sin(2 * np.pi * 4 * t)          # syllables
        a += 45 * env * np.sin(2 * np.pi * 180 * t) + 20 * env * np.sin(2 * np.pi * 900 * t)
    a += rng.normal(0, 0.8, n)                               # the mic's own hiss
    return np.clip(np.round(a), -2048, 2047).astype(int)      # 12-bit counts, as the firmware sees


def run(samples, mode):
    r = Rave()
    ring = [0] * RING
    at = 0
    wmin, wmax, wstart = 10 ** 9, -10 ** 9, 0
    swing = 0
    frames, last = [], 0
    for i, v in enumerate(samples):
        ring[at] = int(v); at = (at + 1) % RING
        wmin, wmax = min(wmin, v), max(wmax, v)
        ms = i * 1000 // RATE
        if ms - wstart >= WINDOW_MS:
            swing = wmax - wmin; wmin, wmax, wstart = 10 ** 9, -10 ** 9, ms
        if ms - last >= FRAME_MS:
            last = ms
            if mode == 'vu':
                r.vu(swing)
                frames.append((int(round(r.level * 32)), r.hold))
            else:
                r.spectrum(ring, at)
                frames.append([int(round(b * 8)) for b in r.band])
    return frames


if __name__ == '__main__':
    ok = True
    rng = np.random.default_rng(3)

    # 5. the FFT
    x = rng.normal(size=FFT_N)
    re, im = list(x), [0.0] * FFT_N
    fft_c(re, im)
    err = np.max(np.abs(np.array(re) + 1j * np.array(im) - np.fft.fft(x)))
    good = err < 1e-9
    ok &= good
    print('FFT against numpy: max error %.2e  %s' % (err, 'ok' if good else 'WRONG'))

    quiet = make(3, 'quiet', rng)
    talk = make(3, 'talk', rng)
    music = make(4, 'music', rng)
    # each clip is run after a few seconds of room, so the floors have settled the way they
    # would on a mask that has been on a while
    room = make(4, 'quiet', rng)

    print('\nVU bar (columns lit, of 32):')
    for name, clip, want in [('silence', quiet, 'dark'), ('quiet talk', talk, 'some'),
                             ('music', music, 'full')]:
        f = run(np.concatenate([room, clip]), 'vu')[len(run(room, 'vu')):]
        lit = np.array([c for c, _ in f])
        print('  %-11s mean %5.1f  max %2d  frames dark %3d%%' %
              (name, lit.mean(), lit.max(), 100 * np.mean(lit == 0)))
        if want == 'dark':
            good = lit.max() <= 1
        elif want == 'some':
            good = 2 <= lit.mean() <= 26
        else:
            good = lit.max() >= 28 and lit.std() > 4
        ok &= good
        print('  %-11s %s' % ('', 'ok' if good else 'WANTED %s' % want))

    print('\nSpectrum (bars lit, of 16 bands x 8 rows = 128):')
    for name, clip, want in [('silence', quiet, 'dark'), ('quiet talk', talk, 'some'),
                             ('music', music, 'full')]:
        f = run(np.concatenate([room, clip]), 'spec')[len(run(room, 'spec')):]
        tot = np.array([sum(b) for b in f])
        print('  %-11s mean %5.1f  max %3d  frames dark %3d%%' %
              (name, tot.mean(), tot.max(), 100 * np.mean(tot == 0)))
        if want == 'dark':
            good = tot.max() <= 2
        elif want == 'some':
            good = tot.mean() >= 2
        else:
            good = tot.max() >= 40 and tot.std() > 5
        ok &= good
        print('  %-11s %s' % ('', 'ok' if good else 'WANTED %s' % want))
        if name == 'music':
            avg = np.array(f).mean(axis=0)
            print('  where the music lands, nose -> ear:', ' '.join('%.1f' % v for v in avg))
            low, high = avg[:3].mean(), avg[-4:].mean()
            print('  bass bands %.1f, treble bands %.1f' % (low, high))
            good = low > 2 and high > 0.5
            ok &= good
            print('  kick at the nose and hats reaching the ear: %s' % ('ok' if good else 'NO'))

    print('\nresult:', 'PASS' if ok else 'FAIL')
