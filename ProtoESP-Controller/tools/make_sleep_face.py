"""Build sleep.json for the 14-panel layout: closed eyes and a slowly breathing mouth.

Modelled on the reference clip: the eyes stay shut as curved arcs while the mouth
inflates and deflates on a roughly 1.7 second loop. The nose panels are copied from
default.json so the sleeping face keeps the same nose as the rest of the faces.

Usage:
    python make_sleep_face.py --preview      # print frames as text, write nothing
    python make_sleep_face.py                # write data/anims/sleep.json
"""
import argparse
import json
import math
import os

CTRL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(CTRL, 'data')

BREATH = [0.22, 0.32, 0.46, 0.62, 0.78, 0.92, 1.0, 1.0, 0.92, 0.78, 0.62, 0.46, 0.32, 0.22]
FRAME_MS = 160          # 14 frames x 160 ms = 2.24 s per breath
BASE_ROW = 4            # vertical centre of the mouth
MOUTH_MAX = 6           # tallest the teardrop gets (its round end), in pixels
MOUTH_HEAD = 4          # round end, in pixels from the front of each mouth half
MOUTH_TAIL = 20         # how far the taper reaches back towards the ear


def blank(width):
    return [[0] * width for _ in range(8)]


def encode(grid):
    """8x8 on/off grid -> the Animator's hex (last byte is the top row, bit 0 the leftmost pixel)."""
    out = ''
    for r in range(7, -1, -1):
        byte = 0
        for c in range(8):
            if grid[r][c]:
                byte |= 1 << c
        out += '%02x' % byte
    return out.lstrip('0') or '0'


def split(canvas):
    """Wide canvas -> list of 8x8 panel grids, left to right."""
    width = len(canvas[0])
    return [[[canvas[r][p * 8 + c] for c in range(8)] for r in range(8)] for p in range(width // 8)]


def mirror(canvas):
    return [list(reversed(row)) for row in canvas]


def closed_eye():
    """16x8: a shallow arc, ends low and middle raised, like a contented shut eye."""
    c = blank(16)
    for x in range(16):
        y = 3.4 - 1.2 * math.sin(math.pi * (x + 0.5) / 16)
        for t in range(2):                     # 2 px thick
            row = int(round(y)) + t
            if 0 <= row < 8:
                c[row][x] = 1
    return c


def mouth(amount):
    # 32x8: a teardrop, round end towards the front of the face, tapering back.
    # amount (0..1) is how far through the breath it is. No baseline line.
    c = blank(32)
    height = MOUTH_MAX * amount
    if height < 1:
        return c
    radius = height / 2
    tail = MOUTH_TAIL * (0.55 + 0.45 * amount)
    for x in range(32):
        if x <= MOUTH_HEAD:                      # round end
            dx = (MOUTH_HEAD - x) / max(radius, 0.001)
            if dx > 1:
                continue
            h = height * math.sqrt(1 - dx * dx)
        else:                                    # taper to a point
            t = (x - MOUTH_HEAD) / tail
            if t > 1:
                continue
            h = height * (1 - t)          # straight taper
        top = int(round(BASE_ROW - h / 2))
        bottom = int(round(BASE_ROW + h / 2))
        for row in range(max(0, top), min(7, bottom) + 1):
            c[row][x] = 1
    return c

def panels_in(row):
    return [int(cell[1:]) for cell in row if cell.startswith('t')]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--preview', action='store_true')
    args = ap.parse_args()

    rows = [[]]
    for token in open(os.path.join(DATA, 'visType.txt')).read().strip().split(';'):
        if token == '_':
            rows.append([])
        else:
            rows[-1].append(token)
    mouth_row = max(rows, key=lambda r: len(panels_in(r)))
    eye_row = rows[0]
    nose_row = [r for r in rows if r is not mouth_row and r is not eye_row][0]

    eyes = panels_in(eye_row)          # viewer left to right, e.g. [13, 12, 1, 0]
    mouths = panels_in(mouth_row)      # e.g. [11, 10, 9, 8, 5, 4, 3, 2]
    noses = panels_in(nose_row)        # e.g. [7, 6]
    count = max(eyes + mouths + noses) + 1

    # keep the nose from the everyday face
    nose_art = {}
    try:
        with open(os.path.join(DATA, 'anims', 'default.json')) as f:
            leds = json.load(f)['visor']['frames'][0]['leds']
        for panel in noses:
            nose_art[panel] = leds[panel]
    except (OSError, ValueError, KeyError, IndexError):
        pass

    eye_left, eye_right = split(closed_eye()), split(mirror(closed_eye()))
    frames = []
    for amount in BREATH:
        art = ['0'] * count
        for panel, grid in zip(eyes[:2], eye_left):
            art[panel] = encode(grid)
        for panel, grid in zip(eyes[2:], eye_right):
            art[panel] = encode(grid)
        half = len(mouths) // 2
        for panel, grid in zip(mouths[:half], split(mirror(mouth(amount)))):
            art[panel] = encode(grid)
        for panel, grid in zip(mouths[half:], split(mouth(amount))):
            art[panel] = encode(grid)
        for panel, hexart in nose_art.items():
            art[panel] = hexart
        frames.append({'timespan': FRAME_MS, 'leds': art})

    is_mouth = [False] * count               # the mouth panels, so speech can drive them later
    for panel in mouths:
        is_mouth[panel] = True
    anim = {'ears': {'type': 'none'},
            'visor': {'type': 'custom', 'isMouth': is_mouth, 'frames': frames}}

    if args.preview:
        def bits(hexart):
            hexart = ('0' * 16 + hexart)[-16:]
            return [''.join('#' if (int(hexart[16 - 2 * i:18 - 2 * i], 16) >> j) & 1 else '.'
                            for j in range(8)) for i in range(1, 9)]
        for index in (0, 3, 6):
            print('frame %d (breath %.2f):' % (index, BREATH[index]))
            for row in rows:
                lines = [''] * 8
                for cell in row:
                    art = bits(frames[index]['leds'][int(cell[1:])]) if cell.startswith('t') else ['        '] * 8
                    for r in range(8):
                        lines[r] += art[r] + ' '
                print('\n'.join(line.rstrip() for line in lines))
                print()
        print('frames: %d, %d ms each, %.2f s per breath' % (len(frames), FRAME_MS, len(frames) * FRAME_MS / 1000))
        return

    path = os.path.join(DATA, 'anims', 'sleep.json')
    with open(path, 'w') as f:
        json.dump(anim, f, separators=(',', ':'))
    print('wrote %s: %d frames, %d bytes' % (path, len(frames), os.path.getsize(path)))


if __name__ == '__main__':
    main()
