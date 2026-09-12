"""Draw the home-screen icons for the mask's web page.

A visor face in LED dots on the page's own dark background, so the icon on the phone
matches the page it opens. Writes into data/, which gets flashed with the filesystem.

Usage:
    python tools/make_icons.py
"""
import os

from PIL import Image, ImageDraw

DATA = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'data')

BG = (11, 15, 23)        # --bg from index.html
ON = (77, 163, 255)      # --accent
OFF = (23, 32, 48)       # unlit dots, so it reads as a panel rather than floating shapes

# 12x12 grid: angled eyes over a small smile, the shapes the mask itself can draw
ART = [
    '............',
    '............',
    '.###....###.',
    '.####..####.',
    '..##....##..',
    '............',
    '..#......#..',
    '...#....#...',
    '....####....',
    '............',
    '............',
    '............',
]


def draw(size, inset):
    """inset: fraction of the edge left empty, so maskable icons survive being cropped round."""
    img = Image.new('RGB', (size, size), BG)
    d = ImageDraw.Draw(img)
    span = size * (1 - 2 * inset)
    cell = span / len(ART[0])
    dot = cell * 0.72
    for r, row in enumerate(ART):
        for c, ch in enumerate(row):
            cx = size * inset + (c + 0.5) * cell
            cy = size * inset + (r + 0.5) * cell
            box = [cx - dot / 2, cy - dot / 2, cx + dot / 2, cy + dot / 2]
            d.ellipse(box, fill=ON if ch == '#' else OFF)
    return img


def main():
    for name, size, inset in [('icon-192.png', 192, 0.06),
                              ('icon-512.png', 512, 0.06),
                              ('icon-180.png', 180, 0.06),      # iOS home screen
                              ('icon-maskable.png', 512, 0.18)]:  # Android crops to a circle
        path = os.path.join(DATA, name)
        draw(size, inset).save(path, optimize=True)
        print('wrote %s (%d bytes)' % (path, os.path.getsize(path)))


if __name__ == '__main__':
    main()
