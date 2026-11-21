"""Publish the mask's firmware and web pages as a GitHub release the app can install from.

    python tools/release_mask.py "What changed, in a sentence or two"
    python tools/release_mask.py --dry-run "..."     # build the files, don't publish

Run after building the firmware (platformio run) and pushing. Tags mask-<BITTENOS_FW> from
src/version.h and attaches:
  firmware.bin  the firmware, sent to the mask's /ota
  web.zip       the web pages (sent one by one to /webfile) and the built-in faces under anims/
                (the app only adds faces the mask doesn't already have)
config.json, screen.json and the other settings files are never included, so an update can't
touch the person's settings.
"""
import argparse, os, re, shutil, subprocess, sys, tempfile, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'data')
FW = os.path.join(ROOT, '.pio', 'build', 'esp32-s3', 'firmware.bin')
WEB_ENDS = ('.html', '.js', '.js.gz', '.css', '.png', '.svg', '.ico')   # same list as webNameOk() in main.cpp


def web_name_ok(n):
    return (re.fullmatch(r'[A-Za-z0-9._-]{1,40}', n) and not n.startswith('.')
            and (n == 'manifest.json' or n.endswith(WEB_ENDS)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('notes')
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()

    h = open(os.path.join(ROOT, 'src', 'version.h'), encoding='utf-8').read()
    code = int(re.search(r'#define BITTENOS_FW (\d+)', h).group(1))
    name = re.search(r'#define BITTENOS_FW_NAME "([^"]*)"', h).group(1)
    if not os.path.isfile(FW):
        sys.exit('Build the firmware first: python -m platformio run')
    with open(FW, 'rb') as f:
        if f.read(1) != b'\xe9':
            sys.exit('firmware.bin does not look like an ESP32 image')

    out = tempfile.mkdtemp(prefix='mask-release-')
    shutil.copyfile(FW, os.path.join(out, 'firmware.bin'))
    zp = os.path.join(out, 'web.zip')
    pages, faces = [], []
    with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED) as z:
        for n in sorted(os.listdir(DATA)):
            if os.path.isfile(os.path.join(DATA, n)) and web_name_ok(n):
                z.write(os.path.join(DATA, n), n); pages.append(n)
        adir = os.path.join(DATA, 'anims')
        for n in sorted(os.listdir(adir)):
            if n.endswith('.json'):
                z.write(os.path.join(adir, n), 'anims/' + n); faces.append(n)
    print('mask-%d (v%s): firmware %d KB, %d web files, %d faces' % (
        code, name, os.path.getsize(FW) // 1024, len(pages), len(faces)))
    print('  pages:', ', '.join(pages))
    if a.dry_run:
        print('  dry run, files in', out)
        return
    branch = subprocess.check_output(['git', 'rev-parse', '--abbrev-ref', 'HEAD'], cwd=ROOT, text=True).strip()
    sys.exit(subprocess.call(['gh', 'release', 'create', 'mask-%d' % code,
                              os.path.join(out, 'firmware.bin'), zp,
                              '--repo', 'picovrfbt/BittenOS', '--target', branch,
                              '--title', 'BittenOS mask v%s' % name, '--notes', a.notes]))


if __name__ == '__main__':
    main()
