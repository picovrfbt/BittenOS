"""Pull the mask's own settings out of a filesystem image read back off the board.

Flashing the filesystem over USB replaces everything on it, settings included. The way round
that is to read the board's filesystem first, take its config.json, and build the new image on
top of that - so the faces and web pages are new and the settings are the ones the mask already
had. This does the middle step.

    python -m esptool --port COM7 --baud 460800 read-flash 0x610000 0x3E0000 board_fs.bin
    python tools/read_board_config.py board_fs.bin
    python -m platformio run -d BittenOS-Controller -t buildfs
    git checkout BittenOS-Controller/data/config.json BittenOS-Controller/data/configCRC.txt

The last line matters: the repo's own config.json is the defaults a fresh board should get, and
it should not be left holding one particular mask's calibration.

The trap this is written to avoid: if the board drops off the USB part way through the read,
esptool can still leave a short or zero-filled file behind, and buildfs will cheerfully build an
image from the repo defaults and flash those over the mask's settings. So the image is mounted
and checked here rather than trusted, and nothing is written unless config.json actually parses.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(os.path.dirname(HERE), 'data')
BLOCK = 4096                      # what the ESP32 Arduino core formats LittleFS with


def main():
    ap = argparse.ArgumentParser(description="Take config.json out of a board's filesystem image")
    ap.add_argument('image', help='the .bin read back from 0x610000')
    ap.add_argument('--out', default=os.path.join(DATA, 'config.json'))
    ap.add_argument('--list', action='store_true', help='just show what is on it')
    args = ap.parse_args()

    raw = open(args.image, 'rb').read()
    print('image %s, %d bytes (%d blocks)' % (args.image, len(raw), len(raw) // BLOCK))
    if len(raw) < BLOCK * 16:
        sys.exit('That is far too small to be the filesystem - did the read finish?')
    if not any(raw):
        sys.exit('The image is all zeros, so the read failed. Do not build from this.')

    try:
        from littlefs import LittleFS
    except ImportError:
        sys.exit('littlefs-python is needed: pip install littlefs-python')

    fs = LittleFS(block_size=BLOCK, block_count=len(raw) // BLOCK, mount=False)
    fs.context.buffer = bytearray(raw)
    try:
        fs.mount()
    except Exception as e:
        sys.exit('The image would not mount (%s). Read it again before building anything.' % e)

    names = sorted(fs.listdir('/'))
    print('on the board: %s' % ', '.join(names))
    if args.list:
        return
    if 'config.json' not in names:
        sys.exit('No config.json on that image.')

    with fs.open('/config.json', 'r') as f:
        text = f.read()
    if isinstance(text, bytes):
        text = text.decode('utf-8')

    try:
        cfg = json.loads(text)
    except ValueError as e:
        sys.exit('config.json on the board is not valid JSON (%s), so it is not safe to use.' % e)

    # A few settings worth seeing, because silently flashing the wrong ones is the whole risk.
    for k in ('wifiName', 'bVisor', 'boopCount', 'boopEna', 'speechEna', 'oledEna', 'rmtMac'):
        if k in cfg:
            print('  %-12s %s' % (k, cfg[k]))

    open(args.out, 'w', encoding='utf-8', newline='\n').write(text)
    print("\nwrote %s - now run buildfs, then put the repo's own config back with git checkout"
          % os.path.relpath(args.out, os.path.dirname(HERE)))


if __name__ == '__main__':
    main()
