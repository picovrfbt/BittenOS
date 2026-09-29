"""Render the faces in data/anims to a PNG, for looking at them without the mask.

Draws each face the way the panels do - one dot per LED - so shapes can be judged before
flashing. Needs Pillow.

Usage:
    python tools/render_faces.py                    # contact sheet of every face, frame 0
    python tools/render_faces.py --frames heart     # every frame of one face, to check motion
    python tools/render_faces.py --out somewhere.png
"""
import argparse
import glob
import json
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(os.path.dirname(HERE), 'data')
ANIMS = os.path.join(DATA, 'anims')

BG = (10, 10, 12)
PANEL_BG = (26, 22, 20)
ON = (255, 122, 26)
OFF = (48, 40, 36)
LABEL = (210, 210, 215)

DOT = 7          # pixels per LED
GAP = 2          # between panels


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


def panel_bits(hexart):
    hexart = ('0' * 16 + (hexart or '0'))[-16:]
    rows = []
    for i in range(1, 9):
        byte = int(hexart[16 - 2 * i:18 - 2 * i], 16)
        rows.append([(byte >> j) & 1 for j in range(8)])
    return rows


def face_size(rows):
    wide = max(len(r) for r in rows)
    return (wide * (8 * DOT + GAP) - GAP, len(rows) * (8 * DOT + GAP) - GAP)


def draw_face(img, ox, oy, rows, leds):
    d = ImageDraw.Draw(img)
    for ry, row in enumerate(rows):
        for cx, cell in enumerate(row):
            if not cell.startswith('t'):
                continue
            x0 = ox + cx * (8 * DOT + GAP)
            y0 = oy + ry * (8 * DOT + GAP)
            d.rectangle([x0, y0, x0 + 8 * DOT - 1, y0 + 8 * DOT - 1], fill=PANEL_BG)
            bits = panel_bits(leds[int(cell[1:])] if int(cell[1:]) < len(leds) else '0')
            for r in range(8):
                for k in range(8):
                    cxp, cyp = x0 + k * DOT, y0 + r * DOT
                    d.ellipse([cxp + 1, cyp + 1, cxp + DOT - 2, cyp + DOT - 2],
                              fill=ON if bits[r][k] else OFF)


def load(name):
    with open(os.path.join(ANIMS, name + '.json')) as f:
        return json.load(f)


def sheet(names, frame_of, rows, title_for):
    fw, fh = face_size(rows)
    pad, head = 14, 20
    cols = 2 if len(names) > 4 else 1
    lines = (len(names) + cols - 1) // cols
    img = Image.new('RGB', (cols * (fw + pad) + pad, lines * (fh + head + pad) + pad), BG)
    d = ImageDraw.Draw(img)
    for i, name in enumerate(names):
        cx, cy = i % cols, i // cols
        ox = pad + cx * (fw + pad)
        oy = pad + cy * (fh + head + pad)
        d.text((ox, oy), title_for(name), fill=LABEL)
        draw_face(img, ox, oy + head, rows, frame_of(name))
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames')
    ap.add_argument('--out')
    args = ap.parse_args()

    rows = layout_rows()

    if args.frames:
        anim = load(args.frames)
        frames = anim['visor']['frames']
        names = list(range(len(frames)))
        img = sheet(names,
                    lambda i: frames[i]['leds'],
                    rows,
                    lambda i: '%s  frame %d  (%d ms)' % (args.frames, i, frames[i]['timespan']))
        out = args.out or os.path.join(HERE, '..', 'faces_%s.png' % args.frames)
    else:
        names = sorted(os.path.splitext(os.path.basename(p))[0]
                       for p in glob.glob(os.path.join(ANIMS, '*.json')))
        cache = {n: load(n) for n in names}
        img = sheet(names,
                    lambda n: cache[n]['visor']['frames'][0]['leds'],
                    rows,
                    lambda n: '%s  (%d frames)' % (n, len(cache[n]['visor']['frames'])))
        out = args.out or os.path.join(HERE, '..', 'faces_all.png')

    out = os.path.abspath(out)
    img.save(out)
    print('wrote %s  (%dx%d)' % (out, img.width, img.height))


if __name__ == '__main__':
    main()
