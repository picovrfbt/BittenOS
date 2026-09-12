"""Draw the mask's faces from curves, in a smoother and more animated style.

The stock ProtoESP faces were drawn for an 11-panel visor and machine-converted to this
14-panel one, which duplicated panels and left hard blocky edges. These are drawn from real
geometry instead - discs, arcs and thin flowing strokes - which is what stops them looking
carved out of a pumpkin.

Everything is built at true pixel coordinates rather than normalised ones. An eye box is 16
wide but only 8 tall, so normalised maths flattens every circle into a slab; working in
pixels keeps the curves round.

Each face is drawn on the left half of an 80x24 canvas and then mirrored, so the two halves
match exactly. That also keeps the web page's half-face previews working.

Usage:
    python tools/make_faces.py --list
    python tools/make_faces.py --preview happy     # print frames as text, write nothing
    python tools/make_faces.py                     # write data/anims/*.json
"""
import argparse
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(os.path.dirname(HERE), 'data')
ANIMS = os.path.join(DATA, 'anims')

W, H = 80, 24                 # 10 panels across, 3 rows down
EX0, EX1 = 1, 14              # left eye sits in x 0..15, inset a pixel each side
EYE_CX, EYE_BOT = 7.5, 7.5    # eye centre line and the row it rests on
NX0, NX1 = 24, 31             # left nose panel; x=31 is the inner edge, towards the centre
MX0, MX1 = 0, 31              # left mouth; x=0 is the ear end, x=31 the centre


def blank():
    return [[0] * W for _ in range(H)]


def put(c, x, y):
    x, y = int(round(x)), int(round(y))
    if 0 <= x < W and 0 <= y < H:
        c[y][x] = 1


def stroke(c, x0, x1, fy, thick=1, ymin=-99, ymax=99):
    """Draw y=fy(s) for s across 0..1, filling vertical gaps so steep parts stay joined.

    ymin/ymax clip to one panel row: a 2px stroke near the edge of the eye otherwise spills
    into the nose panel below it.
    """
    def mark(x, y):
        if ymin <= y <= ymax:
            put(c, x, y)
    prev = None
    for x in range(x0, x1 + 1):
        s = (x - x0) / (x1 - x0) if x1 > x0 else 0.0
        yi = int(round(fy(s)))
        if prev is not None and abs(yi - prev) > 1:
            step = 1 if yi > prev else -1
            for yy in range(prev + step, yi, step):
                for t in range(thick):
                    mark(x, yy + t)
        for t in range(thick):
            mark(x, yi + t)
        prev = yi


def disc(c, cx, cy, r, ymin=-99, ymax=99):
    for y in range(int(cy - r) - 1, int(cy + r) + 2):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            if ymin <= y <= ymax and (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                put(c, x, y)


def arc(c, cx, cy, r, a0, a1, thick=1, ymin=-99, ymax=99):
    """Sample an arc densely enough that it comes out as one unbroken line."""
    steps = max(12, int(abs(a1 - a0) * r / 8))
    for i in range(steps + 1):
        a = math.radians(a0 + (a1 - a0) * i / steps)
        for t in range(thick):
            y = cy + (r - t) * math.sin(a)
            if ymin <= round(y) <= ymax:          # eyes must not bleed into the nose row
                put(c, cx + (r - t) * math.cos(a), y)


def wedge(c, x0, x1, ytop, yslope, ybot):
    """Filled shape with a straight slanted top edge - reads as a brow."""
    for x in range(x0, x1 + 1):
        s = (x - x0) / (x1 - x0)
        top = ytop + yslope * s
        for y in range(int(round(top)), ybot + 1):
            put(c, x, y)


def mirror_half(c):
    for y in range(H):
        for x in range(W // 2):
            c[y][W - 1 - x] = c[y][x]


# ---------- eyes ----------

def eye_closed(c, ph):           # blink, and the resting face for upset
    stroke(c, 2, 13, lambda s: 3.5, thick=2, ymin=0, ymax=7)


def eye_dome(c, ph):             # calm, the default: a rounded dome resting on the base line
    r = 6.6 + 0.7 * math.sin(2 * math.pi * ph)     # has to move a whole pixel to show at all
    disc(c, EYE_CX, EYE_BOT, r, ymin=0, ymax=7)


def eye_arch(c, ph):             # happy / booped: a clean ^, two strokes meeting at the peak
    lift = 1.1 * math.sin(2 * math.pi * ph)
    peak, foot = 1.4 + lift, 6.0 + lift
    stroke(c, EX0, 7, lambda s: foot - (foot - peak) * s, thick=2, ymin=0, ymax=7)
    stroke(c, 8, EX1, lambda s: peak + (foot - peak) * s, thick=2, ymin=0, ymax=7)


def eye_droop(c, ph):            # sad: the lid droops towards the ear, the other way to angry
    sag = 0.4 * math.sin(2 * math.pi * ph)
    wedge(c, EX0, EX1, 4.8 + sag, -3.4, 7)


def eye_angry(c, ph):            # angry: a brow driving down towards the nose
    twitch = 0.35 * math.sin(4 * math.pi * ph)
    wedge(c, EX0, EX1, 0.6 + twitch, 4.2, 7)


def eye_chevron(c, ph):          # confused: a > pointing in towards the nose
    shift = 0.5 * math.sin(2 * math.pi * ph)
    stroke(c, 3, 11, lambda s: 0.6 + shift + 3.1 * s, thick=2, ymin=0, ymax=7)
    stroke(c, 3, 11, lambda s: 6.4 + shift - 3.1 * s, thick=2, ymin=0, ymax=7)


def eye_heart(c, ph):            # heart: two lobes over a point, beating twice a loop
    beat = 1.0 + 0.11 * math.sin(4 * math.pi * ph)
    lobe = 2.9 * beat
    disc(c, EYE_CX - 2.7, 2.7, lobe, ymin=0, ymax=7)
    disc(c, EYE_CX + 2.7, 2.7, lobe, ymin=0, ymax=7)
    top, bot = 2.7, 2.7 + 5.0 * beat
    halfw = 5.7 * beat
    for y in range(int(top), min(7, int(bot)) + 1):
        w = halfw * (1 - (y - top) / (bot - top))
        for x in range(int(EYE_CX - w), int(EYE_CX + w) + 1):
            put(c, x, y)


def eye_dot(c, ph):              # surprised
    disc(c, EYE_CX, 3.5, 3.5 + 0.8 * math.sin(2 * math.pi * ph), ymin=0, ymax=7)


# ---------- nose ----------

def nose(c):
    """A small hook in the top inner corner. Spanning the whole panel reads as a slash."""
    arc(c, NX1, 13.0, 4.2, 180, 272)


# ---------- mouths ----------

def mouth_wave(c, ph):           # default: one soft hump each side, breathing slowly
    a = 2.1 + 0.9 * math.sin(2 * math.pi * ph)
    stroke(c, MX0, MX1, lambda s: 20.2 - a * math.sin(math.pi * s))


def mouth_smile(c, ph):          # ends high, dipping to the centre
    d = 0.3 * math.sin(2 * math.pi * ph)
    stroke(c, MX0, MX1, lambda s: 18.3 + d + 3.4 * s ** 1.6)


def mouth_frown(c, ph):
    d = 0.25 * math.sin(2 * math.pi * ph)
    stroke(c, MX0, MX1, lambda s: 21.8 - d - 3.2 * s ** 1.6)


def mouth_flat(c, ph):           # upset: a slow sag towards the centre, then back
    t = 1.2 * math.sin(2 * math.pi * ph)
    stroke(c, MX0, MX1, lambda s: 20.0 + t * (0.2 + 0.8 * s ** 1.5))


def mouth_zigzag(c, ph):         # confused / angry: sharp peaks sliding along
    def fy(s):
        p = (s * 2.0 + ph) % 1.0
        return 21.4 - 3.0 * (1 - abs(2 * p - 1))
    stroke(c, MX0, MX1, fy)


def mouth_open(c, ph):           # booped: a filled shout
    grow = 1.0 + 0.30 * math.sin(2 * math.pi * ph)
    for x in range(6, MX1 + 1):
        s = (x - 6) / (MX1 - 6)
        half = 3.0 * grow * math.sin(math.pi * s / 2)     # closed at the ear, widest at the centre
        for y in range(int(round(20.0 - half)), int(round(20.0 + half)) + 1):
            put(c, x, y)


def mouth_grin(c, ph):           # heart: a wide, happy curve
    d = 0.3 * math.sin(2 * math.pi * ph)
    stroke(c, MX0, MX1, lambda s: 18.6 + d + 3.2 * s ** 1.4)


# ---------- the faces ----------

FACES = {
    'default':  dict(n=16, ms=110, eye=eye_dome,    mouth=mouth_wave,   blink=[13, 14]),
    'happy':    dict(n=14, ms=100, eye=eye_arch,    mouth=mouth_smile,  blink=[]),
    'sad':      dict(n=14, ms=170, eye=eye_droop,   mouth=mouth_frown,  blink=[11, 12]),
    'angry':    dict(n=12, ms=150, eye=eye_angry,   mouth=mouth_zigzag, blink=[]),
    'upset':    dict(n=12, ms=180, eye=eye_closed,  mouth=mouth_flat,   blink=[]),
    'confused': dict(n=14, ms=160, eye=eye_chevron, mouth=mouth_zigzag, blink=[12]),
    'boop':     dict(n=10, ms=90,  eye=eye_arch,    mouth=mouth_open,   blink=[]),
    'heart':    dict(n=14, ms=110, eye=eye_heart,   mouth=mouth_grin,   blink=[]),
    'surprise': dict(n=12, ms=120, eye=eye_dot,     mouth=mouth_open,   blink=[]),
}


def layout_rows():
    cells = open(os.path.join(DATA, 'visType.txt')).read().strip().split(';')
    rows, cur = [], []
    for tok in cells:
        if tok == '_':
            rows.append(cur)
            cur = []
        else:
            cur.append(tok)
    rows.append(cur)
    return rows


def encode(grid):
    out = ''
    for r in range(7, -1, -1):
        byte = 0
        for col in range(8):
            if grid[r][col]:
                byte |= 1 << col
        out += '%02x' % byte
    return out.lstrip('0') or '0'


def frame_leds(canvas, rows, count):
    art = ['0'] * count
    for ry, row in enumerate(rows):
        for cx, cell in enumerate(row):
            if not cell.startswith('t'):
                continue
            grid = [[canvas[ry * 8 + r][cx * 8 + k] for k in range(8)] for r in range(8)]
            art[int(cell[1:])] = encode(grid)
    return art


def draw_frame(spec, i):
    ph = i / spec['n']
    c = blank()
    (eye_closed if i in spec['blink'] else spec['eye'])(c, ph)
    nose(c)
    spec['mouth'](c, ph)
    mirror_half(c)
    return c


def dedupe(frames):
    """Sub-pixel motion rounds to the same pixels, so neighbouring frames often come out
    identical. Merge those and add their time together: same animation, smaller file."""
    out = []
    for f in frames:
        if out and out[-1]['leds'] == f['leds']:
            out[-1]['timespan'] += f['timespan']
        else:
            out.append(dict(f))
    if len(out) > 1 and out[0]['leds'] == out[-1]['leds']:
        out[0]['timespan'] += out.pop()['timespan']
    return out


def build(name, rows, count):
    spec = FACES[name]
    frames = dedupe([{'timespan': spec['ms'], 'leds': frame_leds(draw_frame(spec, i), rows, count)}
                     for i in range(spec['n'])])
    is_mouth = [False] * count
    for cell in rows[-1]:                      # the bottom row is the mouth
        if cell.startswith('t'):
            is_mouth[int(cell[1:])] = True
    return {'ears': {'type': 'none'},
            'visor': {'type': 'custom', 'isMouth': is_mouth, 'frames': frames}}


def show(canvas):
    for ry in range(3):
        for r in range(8):
            print(''.join('#' if canvas[ry * 8 + r][x] else '.' for x in range(W)))
        print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--preview')
    ap.add_argument('--frame', type=int)
    ap.add_argument('--list', action='store_true')
    args = ap.parse_args()

    if args.list:
        print(' '.join(sorted(FACES)))
        return

    rows = layout_rows()
    count = max(int(cell[1:]) for row in rows for cell in row if cell.startswith('t')) + 1

    if args.preview:
        spec = FACES[args.preview]
        wanted = [args.frame] if args.frame is not None else [0, spec['n'] // 3, spec['n'] // 2]
        for i in wanted:
            print('--- %s frame %d of %d ---' % (args.preview, i, spec['n']))
            show(draw_frame(spec, i))
        return

    for name in sorted(FACES):
        anim = build(name, rows, count)
        path = os.path.join(ANIMS, name + '.json')
        with open(path, 'w') as f:
            json.dump(anim, f, separators=(',', ':'))
        print('%-9s %2d frames  %5d bytes' % (name, len(anim['visor']['frames']), os.path.getsize(path)))


if __name__ == '__main__':
    main()
