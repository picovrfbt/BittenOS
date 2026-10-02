"""Draw the mask's faces: the original eyes, redrawn mouths, and more animation.

The eyes are the ones the mask came with. They are read straight out of the original files in
anims_14panel_v1/ rather than copied into this script, so they are exactly the art that was
there - including each face's own blink, which several of them already had.

What changes is the pacing. The originals sat still for a long time: the default eye held for
7 seconds before blinking, boop and upset for 10. Long holds are capped (see HOLD), so a face
blinks or twitches every few seconds instead, and a face whose eye never moved at all gets a
slow one-pixel lift so it does not look frozen.

The mouths are drawn here from curves, two pixels thick, because the originals were drawn for
an 11-panel visor and machine-converted to this one, which duplicated panels and left blocky
edges. The nose is the original, copied pixel for pixel.

Everything is drawn on the left half of an 80x24 canvas and mirrored, so the two halves match
exactly. That is also what lets the web page show half a face at double size.

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
ROOT = os.path.dirname(HERE)
DATA = os.path.join(ROOT, 'data')
ANIMS = os.path.join(DATA, 'anims')
V1 = os.path.join(ROOT, 'anims_14panel_v1')      # the faces as they came, the source of the eyes

W, H = 80, 24                 # 10 panels across, 3 rows down
EYE_CX = 7.5
NX0 = 24                      # left nose panel starts here; x=31 is the inner edge
MX0, MX1 = 0, 31              # left mouth; x=0 is the ear end, x=31 the centre

HOLD = 1500                   # ms: longest a single eye frame is allowed to sit still


def blank():
    return [[0] * W for _ in range(H)]


def put(c, x, y):
    x, y = int(round(x)), int(round(y))
    if 0 <= x < W and 0 <= y < H:
        c[y][x] = 1


def stroke(c, x0, x1, fy, thick=1, ymin=-99, ymax=99):
    """Draw y=fy(s) for s across 0..1, filling vertical gaps so steep parts stay joined.

    ymin/ymax clip to one panel row: a 2px stroke near the edge of a row otherwise spills
    into the panel below it.
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


def mirror_half(c):
    for y in range(H):
        for x in range(W // 2):
            c[y][W - 1 - x] = c[y][x]


def paint(c, art, x0, y0, dy=0, ymin=0, ymax=7):
    """Stamp a bitmap given as rows of '#' and '.'."""
    for row_i, row in enumerate(art):
        y = y0 + row_i + dy
        if ymin <= y <= ymax:
            for dx, ch in enumerate(row):
                if ch == '#':
                    put(c, x0 + dx, y)


# ---------- the original eyes ----------

def _bits(hexart):
    hexart = ('0' * 16 + (hexart or '0'))[-16:]
    return [[(int(hexart[16 - 2 * i:18 - 2 * i], 16) >> j) & 1 for j in range(8)]
            for i in range(1, 9)]


def original_eye_frames(face):
    """(art, ms) per frame of the original face: the left eye only, as rows of '#' and '.'.

    Only the left eye is read; the right is produced by mirroring. A couple of the originals
    are a pixel or two out of true between their halves, and this quietly straightens them.
    """
    with open(os.path.join(V1, face + '.json')) as f:
        frames = json.load(f)['visor']['frames']
    out = []
    for fr in frames:
        leds = fr['leds']
        left = [_bits(leds[13])[y] + _bits(leds[12])[y] for y in range(8)]   # t13 then t12
        art = tuple(''.join('#' if v else '.' for v in row) for row in left)
        out.append((art, fr['timespan']))
    return out


def eye_timeline(face, step_ms, hold=HOLD):
    """(art, lift) to show on each frame of the loop.

    The original timings are kept, except that any frame sitting still longer than `hold` is
    cut back to it - that is what stops a face staring for ten seconds. The loop is made
    exactly as long as the shortened sequence, so it never drifts at the seam.
    """
    seq = [(art, min(ms, hold)) for art, ms in original_eye_frames(face)]
    total = sum(ms for _, ms in seq)
    n = max(8, int(round(total / step_ms)))
    track = []
    for i in range(n):
        t = (i * step_ms) % total
        acc = 0
        chosen = seq[-1][0]
        for art, ms in seq:
            acc += ms
            if t < acc:
                chosen = art
                break
        track.append(chosen)
    if len(set(track)) == 1:            # this eye never moves on its own: give it a slow lift
        return [(art, -1 if math.sin(2 * math.pi * i / n) > 0.55 else 0)
                for i, art in enumerate(track)]
    return [(art, 0) for art in track]


# ---------- eyes drawn here, for the faces with no original ----------

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


# The booped face, redrawn from a photo of the same expression on a 32x16 panel: a bold
# chevron pointing in towards the nose, two thick diagonal strokes meeting at a point. The
# panel it was photographed on is a different shape to this mask, so the shape is kept and the
# size is not - it is widened to fill the 16 columns an eye has here.
EYE_CHEVRON = (
    '.##.............',
    '.####...........',
    '...####.........',
    '.....####.......',
    '.....####.......',
    '...####.........',
    '.####...........',
    '.##.............',
)


def eye_chevron(c, ph):          # booped: a sharp > aimed at the nose
    # The point pushes in and back by a pixel, so a face that is held still is not dead still.
    paint(c, EYE_CHEVRON, 1 if math.sin(2 * math.pi * ph) > 0 else 0, 0, ymin=0, ymax=7)


def eye_dot(c, ph):              # surprised
    disc(c, EYE_CX, 3.5, 3.5 + 0.8 * math.sin(2 * math.pi * ph), ymin=0, ymax=7)


# ---------- nose ----------

# The nose the mask came with, copied pixel for pixel. It sits in the outer top corner, and
# the right nose is an exact mirror of this, so mirroring reproduces it exactly.
NOSE_ART = (
    '######..',
    '######..',
    '####....',
    '##......',
)

# A smaller snout, worn by the booped face alone. Every other face keeps NOSE_ART.
NOSE_SMALL_ART = (
    '####....',
    '####....',
    '##......',
)


def nose(c, art=NOSE_ART):
    paint(c, art, NX0, 8, ymin=8, ymax=15)


# ---------- mouths ----------

def seg(points):
    """Straight lines between (s, y) points: sharp corners, no curves.

    s runs 0 at the ear end to 1 at the centre of the face, y is the canvas row.
    """
    def fy(s):
        for i in range(len(points) - 1):
            s0, y0 = points[i]
            s1, y1 = points[i + 1]
            if s0 <= s <= s1:
                t = (s - s0) / (s1 - s0) if s1 > s0 else 0.0
                return y0 + (y1 - y0) * t
        return points[-1][1]
    return fy


def original_mouth_art(face):
    """The left half of the original mouth, 32 wide by 8 tall, as rows of '#' and '.'."""
    with open(os.path.join(V1, face + '.json')) as f:
        leds = json.load(f)['visor']['frames'][0]['leds']
    rows = []
    for y in range(8):
        row = []
        for panel in (11, 10, 9, 8):        # the left mouth, ear to centre
            row += _bits(leds[panel])[y]
        rows.append(''.join('#' if v else '.' for v in row))
    return rows


def squash(art, merges):
    """Drop rows out of a bitmap by OR-ing pairs together.

    Flattening by scaling would thin the strokes; merging keeps every lit pixel and only
    loses height, so the mouth sits lower without getting any lighter.
    `merges` is the indexes of rows to fold into the row below them.
    """
    out, skip = [], set()
    for y, row in enumerate(art):
        if y in skip:
            continue
        if y in merges and y + 1 < len(art):
            nxt = art[y + 1]
            out.append(''.join('#' if (a == '#' or b == '#') else '.' for a, b in zip(row, nxt)))
            skip.add(y + 1)
        else:
            out.append(row)
    return out


def mouth_original(c, ph, face='default', merges=(6,)):
    """The original mouth, squashed a little flatter. Nothing is thinned or redrawn.

    The rows it loses come off the top: the art is sat on the bottom of the panel, so the
    mouth ends up lower and shallower with every lit pixel still there. merges=() gives the
    original untouched, (6,) takes one row off, (0, 6) takes two.
    """
    art = squash(original_mouth_art(face), set(merges))
    paint(c, art, 0, 16 + (8 - len(art)), ymin=16, ymax=23)


def mouth_jagged(c, ph):
    """The default: starts high at the ear, one sharp jag, then steps down to the centre.

    Mirrored, both sides fall towards the middle of the face, so it reads as a smile - but
    built from straight segments with hard corners rather than a curve. Alternates that were
    drawn alongside it are below; swap the one named in FACES to change the shape.
    """
    jag = 0.8 * math.sin(2 * math.pi * ph)      # the peak breathes by about a pixel
    stroke(c, MX0, MX1, seg([
        (0.00, 18.4),
        (0.28, 18.4),
        (0.42, 16.8 + jag),
        (0.58, 20.0),
        (0.80, 21.0),
        (1.00, 21.0),
    ]), thick=2, ymin=16, ymax=23)


def mouth_jagged_step(c, ph):    # alternate: one diagonal step down, no peak
    d = 0.5 * math.sin(2 * math.pi * ph)
    stroke(c, MX0, MX1, seg([(0.00, 18.2 + d), (0.38, 18.2 + d), (0.62, 21.2), (1.00, 21.2)]),
           thick=2, ymin=16, ymax=23)


def mouth_jagged_twin(c, ph):    # alternate: two smaller jags on the way down
    d = 0.6 * math.sin(2 * math.pi * ph)
    stroke(c, MX0, MX1, seg([(0.00, 18.6), (0.20, 17.4 + d), (0.40, 19.6), (0.60, 18.6 + d),
                             (0.80, 21.2), (1.00, 21.2)]), thick=2, ymin=16, ymax=23)


def mouth_wave(c, ph):           # default: one soft hump each side, breathing slowly
    a = 2.1 + 0.9 * math.sin(2 * math.pi * ph)
    stroke(c, MX0, MX1, lambda s: 20.2 - a * math.sin(math.pi * s), thick=2, ymin=16, ymax=23)


def mouth_smile(c, ph):          # ends high, dipping to the centre
    d = 0.3 * math.sin(2 * math.pi * ph)
    stroke(c, MX0, MX1, lambda s: 18.3 + d + 3.4 * s ** 1.6, thick=2, ymin=16, ymax=23)


def mouth_frown(c, ph):
    d = 0.25 * math.sin(2 * math.pi * ph)
    stroke(c, MX0, MX1, lambda s: 21.8 - d - 3.2 * s ** 1.6, thick=2, ymin=16, ymax=23)


def mouth_flat(c, ph):           # upset: a slow sag towards the centre, then back
    t = 1.2 * math.sin(2 * math.pi * ph)
    stroke(c, MX0, MX1, lambda s: 20.0 + t * (0.2 + 0.8 * s ** 1.5), thick=2, ymin=16, ymax=23)


#How steep the booped maw is, measured off the reference photo rather than judged by eye: the
#board was rectified to kill the perspective, the LED lattice found inside the flat image, and the
#lit LEDs read off it. The sawtooth there swings about six rows peak to trough with a peak every
#six or seven columns - far sharper than a first eyeball of the picture suggested.
#The booped maw. The shape came off the reference photo - board warped flat, LED lattice found in
#the rectified image, centre row of the lit run measured column by column - and is kept as the
#corners it turns at rather than as the twenty measured samples. Interpolating the samples rounded
#every tooth off, because the measurement has flat pairs in it and each one stretches to three
#columns on a maw this wide. Straight runs between corners keep the points sharp.
#
#Reading it: a long ramp down from the ear, three teeth biting downwards, then a lift at the nose.
#The teeth point DOWN - an earlier version had them as peaks, which is upside down.
MAW_TOP = 17.2                   # the row the ramp starts and ends on
MAW_BOT = 22.0                   # how far the teeth bite
MAW_TIP = 17.8                   # how far it comes back up between them


def mouth_zigzag_bold(c, ph):    # booped: the reference maw
    # Breathes by a fraction of a pixel rather than changing shape; the shape is the point here.
    d = 0.35 * math.sin(2 * math.pi * ph)
    stroke(c, MX0, MX1, seg([
        (0.000, MAW_TOP - d),
        (0.263, MAW_BOT),            # first tooth
        (0.421, MAW_TIP - d),
        (0.579, MAW_BOT),            # second
        (0.737, MAW_TIP - d),
        (0.895, MAW_BOT),            # third
        (1.000, MAW_TOP - d),
    ]), thick=3, ymin=16, ymax=23)


def mouth_zigzag(c, ph):         # confused / angry: sharp peaks sliding along
    def fy(s):
        p = (s * 2.0 + ph) % 1.0
        return 21.0 - 3.0 * (1 - abs(2 * p - 1))
    stroke(c, MX0, MX1, fy, thick=2, ymin=16, ymax=23)


def mouth_open(c, ph):           # booped: a filled shout
    grow = 1.0 + 0.30 * math.sin(2 * math.pi * ph)
    for x in range(6, MX1 + 1):
        s = (x - 6) / (MX1 - 6)
        half = 3.0 * grow * math.sin(math.pi * s / 2)     # closed at the ear, widest at the centre
        for y in range(int(round(20.0 - half)), int(round(20.0 + half)) + 1):
            put(c, x, y)


def mouth_grin(c, ph):           # heart: a wide, happy curve
    d = 0.3 * math.sin(2 * math.pi * ph)
    stroke(c, MX0, MX1, lambda s: 18.6 + d + 3.2 * s ** 1.4, thick=2, ymin=16, ymax=23)


# ---------- the faces ----------
#
# eye 'original' reads it from anims_14panel_v1/. step is the frame length in ms; how many
# frames a face gets follows from how long its eye sequence runs. hold overrides how long a
# still eye may sit before the sequence moves on.

FACES = {
    'default':  dict(eye='original', step=90,  mouth=mouth_original),
    'happy':    dict(eye='original', step=90,  mouth=mouth_smile),
    'sad':      dict(eye='original', step=100, mouth=mouth_frown),
    'angry':    dict(eye='original', step=30,  mouth=mouth_zigzag),   # its twitch frame is 25ms
    'upset':    dict(eye='original', step=120, mouth=mouth_flat,  hold=1800),
    'confused': dict(eye='original', step=90,  mouth=mouth_zigzag),
    'boop':     dict(eye=eye_chevron, step=90,  mouth=mouth_zigzag_bold, n=10, smallNose=True),
    #the same boop eyes and snout with the mouth left dark
    'boop2':    dict(eye=eye_chevron, step=90,  mouth=None, n=10, smallNose=True),
    'heart':    dict(eye=eye_heart,  step=110, mouth=mouth_grin,  n=14),
    'surprise': dict(eye=eye_dot,    step=120, mouth=mouth_open,  n=12),
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


def face_frames(name):
    """The canvases for one face, already mirrored."""
    spec = FACES[name]
    track = None
    if spec['eye'] == 'original':
        # eyeFrom lets a face borrow another's original eye, for ones with no original of their own
        track = eye_timeline(spec.get('eyeFrom', name), spec['step'], spec.get('hold', HOLD))
        n = len(track)
    else:
        n = spec['n']
    out = []
    for i in range(n):
        ph = i / n
        c = blank()
        if track is None:
            spec['eye'](c, ph)
        else:
            art, lift = track[i]
            paint(c, art, 0, 0, dy=lift)
        nose(c, NOSE_SMALL_ART if spec.get('smallNose') else NOSE_ART)
        if spec['mouth'] is not None:      # some faces are eyes and snout only
            spec['mouth'](c, ph)
        mirror_half(c)
        out.append(c)
    return out


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
    frames = dedupe([{'timespan': spec['step'], 'leds': frame_leds(c, rows, count)}
                     for c in face_frames(name)])
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
        canvases = face_frames(args.preview)
        wanted = ([args.frame] if args.frame is not None
                  else [0, len(canvases) // 3, len(canvases) // 2])
        for i in wanted:
            print('--- %s frame %d of %d ---' % (args.preview, i, len(canvases)))
            show(canvases[i])
        return

    for name in sorted(FACES):
        anim = build(name, rows, count)
        path = os.path.join(ANIMS, name + '.json')
        with open(path, 'w') as f:
            json.dump(anim, f, separators=(',', ':'))
        total = sum(fr['timespan'] for fr in anim['visor']['frames'])
        print('%-9s %2d frames  %.2fs loop  %5d bytes'
              % (name, len(anim['visor']['frames']), total / 1000, os.path.getsize(path)))


if __name__ == '__main__':
    main()
