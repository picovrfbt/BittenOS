"""Convert stock ProtoESP animations (M16 mk2, 11 panels) to the 14-panel
FurBitten layout wired left-eye-first (see data/visType.txt).

Eyes map 1:1. Each maw side gains a 4th panel by repeating its innermost
panel. The single centre nose is split in half and each half is stretched
2x wide onto its own nose panel. Originals are backed up once to
anims_original_11panel/ (outside data/, so they are not uploaded).

Usage: python tools/convert_mk2_to_14.py
"""
import json, glob, os, shutil

MOUTH14 = [False, False, True, True, True, True, False, False,
           True, True, True, True, False, False]

def rows(h):
    h = ('0' * 16 + h)[-16:]
    return [[(int(h[16 - 2 * i:18 - 2 * i], 16) >> (j - 1)) & 1 for j in range(1, 9)]
            for i in range(1, 9)]

def tohex(r):
    return ''.join('%02x' % sum(r[i - 1][j - 1] << (j - 1) for j in range(1, 9))
                   for i in range(8, 0, -1))

def stretch_half(h, half):
    """Take the viewer-left (0) or viewer-right (1) 4 columns, double each column."""
    out = []
    for r in rows(h):
        cols = r[0:4] if half == 0 else r[4:8]
        out.append([c for c in cols for _ in (0, 1)])
    return tohex(out)

def remap(s, nose_l, nose_r):
    # target index: 0 L-eye outer .. 13 R-eye outer (chain order, wearer POV)
    return [s[10], s[9], s[8], s[7], s[6], s[6], nose_r, nose_l,
            s[4], s[4], s[3], s[2], s[1], s[0]]

def convert_frame(fr):
    s = fr['leds']
    fr['leds'] = remap(s, stretch_half(s[5], 0), stretch_half(s[5], 1))
    if isinstance(fr.get('fColor'), list) and len(fr['fColor']) == 11:
        c = fr['fColor']
        fr['fColor'] = remap(c, c[5], c[5])
    if fr.get('ppColor'):
        raise SystemExit('ppColor present - not handled by this script')

def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src = os.path.join(here, 'data', 'anims')
    bak = os.path.join(here, 'anims_original_11panel')
    if not os.path.isdir(bak):
        shutil.copytree(src, bak)
    for path in sorted(glob.glob(os.path.join(src, '*.json'))):
        with open(path) as f:
            d = json.load(f)
        v = d['visor']
        if len(v['isMouth']) == 14:
            print('skip (already 14):', os.path.basename(path))
            continue
        if len(v['isMouth']) != 11:
            print('skip (not 11-panel):', os.path.basename(path))
            continue
        for fr in v['frames']:
            convert_frame(fr)
        v['isMouth'] = MOUTH14
        with open(path, 'w') as f:
            json.dump(d, f, separators=(',', ':'))
        print('converted:', os.path.basename(path), '-', len(v['frames']), 'frames')

if __name__ == '__main__':
    main()
