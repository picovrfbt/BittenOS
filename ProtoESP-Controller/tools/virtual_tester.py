"""Virtual tester for the ProtoESP web UI.

Serves the real web pages from data/ and answers every request the pages make
the way the firmware does (src/main.cpp), so UI changes can be tried in a
browser without flashing the mask. The dashboard at /__tester shows the pages
next to a virtual 14-panel face that plays the current animation.

Web pages are served live from data/, so edits show up on reload. Faces and
settings live in tools/.virtual_board/ (copied from data/ on first run), so
testing never changes data/.

Usage:
    python tools/virtual_tester.py              # start, open the dashboard
    python tools/virtual_tester.py --reset      # recopy faces/settings from data/
    python tools/virtual_tester.py --port 9000 --no-browser
"""
import argparse
import email.parser
import email.policy
import json
import os
import random
import re
import shutil
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(os.path.dirname(HERE), 'data')
BOARD_DIR = os.path.join(HERE, '.virtual_board')
TESTER_PAGE = os.path.join(HERE, 'tester.html')
START = time.time()  # for the emulated uptime

# Settings /saveconfig accepts, in the same order as src/main.cpp, with the type the
# firmware stores them as. The type is declared rather than taken from whatever is already
# in the config, so a setting the config file doesn't have yet still lands as a number or
# a bool instead of a string (getBool/getInt/getFloat in src/fileOp.cpp).
SAVECONFIG_KEYS = {
    'boopEna': bool, 'speechEna': bool, 'tiltEna': bool, 'bleEna': bool, 'oledEna': bool,
    'sleepEna': bool, 'transEna': bool, 'bobEna': bool, 'boopCountEna': bool, 'boopLightEna': bool,
    'boopCount': int, 'boopBase': int, 'boopShade': int, 'transMs': int, 'sleepTol': float,
    'autoBriEna': bool, 'autoBriMode': str, 'autoBriSecs': int, 'autoBriMin': int, 'autoBriMax': int,
    'autoBriDark': int, 'autoBriBright': int, 'oledFlip': bool,
    'mouthMode': str, 'boopMilestone': int, 'aMilestone': str, 'blinkMode': str,
    'blinkDoublePct': int,
    'btnShort': str, 'btnDouble': str, 'btnHold': str,
    'btnShortFace': str, 'btnDoubleFace': str, 'btnHoldFace': str,
    'btnHoldMs': int, 'btnShortPhrase': int, 'btnDoublePhrase': int, 'btnHoldPhrase': int,
    'phrase0': str, 'phrase1': str, 'phrase2': str, 'phrase3': str, 'phrase4': str, 'phrase5': str,
    'phraseSpeed0': int, 'phraseSpeed1': int, 'phraseSpeed2': int,
    'phraseSpeed3': int, 'phraseSpeed4': int, 'phraseSpeed5': int,
    'phraseOnce0': bool, 'phraseOnce1': bool, 'phraseOnce2': bool,
    'phraseOnce3': bool, 'phraseOnce4': bool, 'phraseOnce5': bool,
    'sleepDeepEna': bool, 'sleepDeepMins': int,
    'staEna': bool, 'staName': str, 'staPass': str, 'mdnsName': str,
    'wakeFlickEna': bool, 'wakeFlickBoot': bool, 'wakeFlickStyle': str, 'wakeFlickMs': int, 'wakeFlickGlitch': int,
    'bootBarEna': bool, 'bootBarMs': int, 'dinoBest': int,
    'eggPresses': int, 'eggAction': str, 'slotWins': int, 'slotSpins': int,
    'sassBoops': int, 'sassSecs': int, 'aSass': str,
    'rmtMac': str,
    'rmt0': str, 'rmt0Face': str, 'rmt0Phrase': int,
    'rmt0H': str, 'rmt0HFace': str, 'rmt0HPhrase': int,
    'rmt1': str, 'rmt1Face': str, 'rmt1Phrase': int,
    'rmt1H': str, 'rmt1HFace': str, 'rmt1HPhrase': int,
    'rmt2': str, 'rmt2Face': str, 'rmt2Phrase': int,
    'rmt2H': str, 'rmt2HFace': str, 'rmt2HPhrase': int,
    'bEar': int, 'bVisor': int, 'bOled': int, 'rbSpeed': int, 'rbWidth': int,
    'spMin': int, 'spMax': int, 'spTrig': int,
    'aTilt': str, 'aTilt2': str, 'aBoop': str, 'aSleep': str,
    'neutralX': float, 'neutralY': float, 'neutralZ': float,
    'tiltX': float, 'tiltY': float, 'tiltZ': float,
    'tiltTrig': float,
    'visColor': str, 'wifiName': str, 'wifiPass': str, 'boopThresh': int, 'sleepMins': int,
    'boopConfirm': int, 'tilt2X': float, 'tilt2Y': float, 'tilt2Z': float,
}

CONTENT_TYPES = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'application/javascript',
    '.json': 'application/json',
    '.txt': 'text/plain; charset=utf-8',
    '.css': 'text/css',
    '.png': 'image/png',
    '.ico': 'image/x-icon',
    '.webmanifest': 'application/manifest+json',
}


def to_int(value):
    """Arduino String.toInt(): leading integer, 0 if none."""
    try:
        return int(float(value))
    except ValueError:
        return 0


def to_float(value):
    try:
        return float(value)
    except ValueError:
        return 0.0


def safe_name(name):
    return bool(name) and '/' not in name and '\\' not in name and '..' not in name


class VirtualBoard:
    """The mask's state: settings, stored faces, the face on screen, and a log."""

    def __init__(self):
        self.lock = threading.Lock()
        with open(os.path.join(BOARD_DIR, 'config.json'), encoding='utf-8') as f:
            self.config = json.load(f)
        self.current = 'default.json'  # the firmware loads this at boot
        self.post_anim = None          # JSON text from the Animator's "Try animation"
        self.version = 0               # bumps on any change the dashboard shows
        self.anim_version = 0          # bumps when the face changes
        self.display_on = True         # /display: panels powered down while the board keeps running
        self.then_timer = None         # one-shot animations: switches to their "then" face after one pass
        self.sleeping = False          # /sleep: the sleeping face is showing
        self.wake_to = ''              # the face to return to when it wakes
        self.boop_at = 0.0             # when the last simulated boop happened, for the counter pop
        self.boop_until = 0.0          # a simulated paw is on the snout until then
        self.ambient = 180             # room light the pretend sensor sees, set from the dashboard
        self.measured = 180            # the reading auto-brightness last took
        self.measure_at = 0.0
        self.measure_now = False
        self.mic_gain = 0              # 0 not measured, 1 about right, 2 turn up, 3 turn down
        self.mic_test_at = 0.0         # when the two-part mic test was started
        self.clock_mins = -1           # minutes past midnight a page last reported, -1 = never
        self.clock_at = 0.0
        self.clock_24 = False
        self.dino_on = False           # /dino: the game on the maw. The virtual face cannot play
        self.dino_score = 0            # it, so this is only enough state for the page to drive
        self.pair_until = 0.0          # /pair: a pairing window, with no radio to answer it
        self.slot_on = False           # /slots: likewise, a pretend spin the page can follow
        self.slot_outcome = -1
        self.slot_until = 0.0
        self.log_lines = []
        self.log('[I] Virtual board started, showing default.json')

    def log(self, text):
        self.log_lines.append(time.strftime('%H:%M:%S ') + text)
        del self.log_lines[:-200]
        self.version += 1

    def anims(self):
        folder = os.path.join(BOARD_DIR, 'anims')
        return sorted(f for f in os.listdir(folder) if f.endswith('.json'))

    def anim_path(self, name):
        return os.path.join(BOARD_DIR, 'anims', name)

    def save_config(self):
        with open(os.path.join(BOARD_DIR, 'config.json'), 'w', encoding='utf-8') as f:
            json.dump(self.config, f, indent=2)

    def show(self, name, post_json=None):
        """Like loadAnim(): skip if the same file is already showing."""
        if name == self.current and name != 'POSTAnimLoad':
            return
        self.current = name
        self.post_anim = post_json
        self.anim_version += 1
        self.log('[I] POST load' if post_json is not None else '[I] Animation file opened! ' + name)
        # "then": a one-shot animation hands over to that face after one pass, like the firmware
        if self.then_timer:
            self.then_timer.cancel()
            self.then_timer = None
        try:
            visor = json.loads(post_json)['visor'] if post_json is not None else None
        except (ValueError, KeyError, TypeError):
            visor = None
        then = visor.get('then') if isinstance(visor, dict) else None
        if then:
            total = sum(f.get('timespan', 0) for f in visor.get('frames', [])) / 1000.0
            self.then_timer = threading.Timer(total, self._play_then, [self.anim_version, then])
            self.then_timer.daemon = True
            self.then_timer.start()

    def _play_then(self, version, name):
        with self.lock:
            if self.anim_version == version and safe_name(name) and os.path.isfile(self.anim_path(name)):
                self.log('[I] Played once, back to ' + name)
                self.show(name)

    def current_anim_text(self):
        if self.current == 'POSTAnimLoad':
            return self.post_anim or '{}'
        try:
            with open(self.anim_path(self.current), encoding='utf-8') as f:
                return f.read()
        except OSError:
            return '{}'


board = None


def auto_brightness():
    """Auto-brightness the way src/main.cpp works it out: the room light from the tester's slider,
    measured every autoBriSecs (a blank shows as a short blink on the virtual face), mapped on a log
    scale between the dark and bright points, then clamped. Call with board.lock held."""
    import math
    cfg = board.config
    if not cfg.get('autoBriEna'):
        return {'bright': min(15, max(0, to_int(str(cfg.get('bVisor', 5))))), 'level': -1, 'ambient': -1, 'ago': -1, 'blanking': False}
    now = time.time()
    secs = max(5, int(cfg.get('autoBriSecs') or 60))
    if board.measure_at == 0 or now - board.measure_at >= secs or board.measure_now:
        board.measure_now = False
        board.measure_at = now
        board.measured = board.ambient
    dark = max(0, int(cfg.get('autoBriDark') or 0)) + 1.0
    bright = max(0, int(cfg.get('autoBriBright') or 400)) + 1.0
    bright = max(bright, dark * 1.5)
    t = (math.log(board.measured + 1.0) - math.log(dark)) / (math.log(bright) - math.log(dark))
    t = min(1.0, max(0.0, t))
    lo, hi = sorted((int(cfg.get('autoBriMin', 1)), int(cfg.get('autoBriMax', 8))))
    level = int(round(lo + t * (hi - lo)))
    blanking = cfg.get('autoBriMode', 'blank') != 'live' and now - board.measure_at < 0.24
    return {'bright': level, 'level': level, 'ambient': board.measured, 'ago': int(now - board.measure_at), 'blanking': blanking}


def stats_dict():
    """The /stats reply: plausible numbers shaped like the firmware's. Call with board.lock held."""
    try:
        frames = len(json.loads(board.current_anim_text())['visor']['frames'])
    except (ValueError, KeyError, TypeError):
        frames = 0
    anims_bytes = sum(os.path.getsize(board.anim_path(f)) for f in board.anims())
    # A pretend boop sensor: a few counts of noise at rest, and a paw that's close and
    # shading the sensor while a Boop button's hold lasts.
    paw = time.time() < board.boop_until
    noise = int(time.time() * 7) % 4
    sig = 60 + noise if paw else noise
    shade = 70 + noise if paw else noise
    auto = auto_brightness()
    # A pretend microphone: a level that breathes up and down so the meter has something to show.
    import math
    if board.config.get('speechEna'):
        mic = int(35 + 30 * math.sin(time.time() * 1.7))
        mic = max(0, min(100, mic))
        mic_raw = 1540 + mic * 3
    else:
        mic, mic_raw = -1, 0
    # the two-part mic test: two seconds quiet, three talking, then the result stays on screen
    if board.mic_test_at:
        elapsed = time.time() - board.mic_test_at
        mic_test_phase = 1 if elapsed < 2 else 2 if elapsed < 5 else 3
    else:
        mic_test_phase = 0
    return {
        # the clock counts on from whenever a page last set it, like the firmware does
        'clockMins': -1 if board.clock_mins < 0 else
                     int((board.clock_mins + (time.time() - board.clock_at) // 60) % 1440),
        'clock24': board.clock_24,
        'mic': mic, 'micRaw': 1860, 'micQuiet': 40, 'micCal': False, 'micLoud': False,
        'micSwing': 40 + mic * 4 if mic > 0 else 40,
        'micGain': board.mic_gain,
        'micTest': mic_test_phase, 'micTestQuiet': 38 if mic_test_phase == 3 else -1,
        'micTestLoud': 420 if mic_test_phase == 3 else 0,
        'speaking': mic > int(board.config.get('spTrig') or 10),
        'bright': auto['bright'], 'autoLevel': auto['level'], 'ambient': auto['ambient'],
        'ambientLive': board.ambient + 12, 'measuredAgo': auto['ago'], 'oled': bool(board.config.get('oledEna')),
        'uptime': int(time.time() - START), 'heapFree': 196000, 'heapSize': 327680,
        'psramFree': 8200000, 'psramSize': 8388608, 'fsUsed': 160000 + anims_bytes, 'fsTotal': 4063232,
        'tempC': 41.5, 'loopHz': 5200, 'wifiClients': 1, 'tilt': True, 'boop': True,
        # Joining a network: pretend it got on a few seconds after being switched on, so the
        # page's "trying to join" and "joined" states can both be seen without real hardware.
        'staOn': bool(board.config.get('staEna')),
        'staJoined': bool(board.config.get('staEna')) and bool(board.config.get('staName')),
        'staIp': '192.168.1.42' if board.config.get('staEna') and board.config.get('staName') else '',
        'staRssi': -54 if board.config.get('staEna') and board.config.get('staName') else 0,
        'staName': board.config.get('staName') or '',
        'mdns': board.config.get('mdnsName') or 'protogen',
        'boopRaw': 40 + sig, 'boopBase': 40, 'boopSig': sig, 'booping': paw,
        'als0': int(180 * (100 - shade) / 100), 'als1': 420, 'lightNormal': 180, 'shade': shade,
        'lightOk': True, 'boopsIgnored': 0, 'i2c': '0x39 0x68 (virtual)',
        'boops': int(board.config.get('boopCount') or 0),
        'display': board.display_on, 'sleeping': board.sleeping,
        'anim': board.current, 'frame': 0, 'frames': frames,
    }


def end_boop(back_to):
    """The paw has gone: put the face back and start the counter pop."""
    with board.lock:
        board.log('[I] ToF unBOOP')
        if back_to and back_to != 'POSTAnimLoad':
            board.show(back_to)
        board.boop_at = time.time()



class Handler(BaseHTTPRequestHandler):
    server_version = 'ProtoESP-virtual'

    def log_message(self, fmt, *args):
        pass  # requests go to the board log instead

    # ---- responses -----------------------------------------------------
    def reply(self, status, body, ctype='text/plain; charset=utf-8', headers=None):
        data = body.encode('utf-8') if isinstance(body, str) else body
        self.status = status
        try:
            self.send_response(status)
            self.send_header('Content-Type', ctype)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            for k, v in (headers or {}).items():
                self.send_header(k, v)
            self.end_headers()
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def text(self, body):
        self.reply(200, body)

    def redirect(self, location):
        self.reply(302, '', headers={'Location': location})

    def static(self, folder, rel):
        path = os.path.normpath(os.path.join(folder, rel.lstrip('/')))
        if not path.startswith(os.path.normpath(folder)):
            return self.reply(404, 'Not found')
        ext = os.path.splitext(path)[1].lower()
        ctype = CONTENT_TYPES.get(ext, 'application/octet-stream')
        if os.path.isfile(path):
            with open(path, 'rb') as f:
                return self.reply(200, f.read(), ctype)
        if os.path.isfile(path + '.gz'):  # ESPAsyncWebServer serves file.gz for file
            with open(path + '.gz', 'rb') as f:
                return self.reply(200, f.read(), ctype, {'Content-Encoding': 'gzip'})
        self.reply(404, 'Not found')

    # ---- request parsing -----------------------------------------------
    def read_form(self):
        length = int(self.headers.get('Content-Length') or 0)
        body = self.rfile.read(length) if length else b''
        ctype = self.headers.get('Content-Type', '')
        if 'multipart/form-data' in ctype:
            msg = email.parser.BytesParser(policy=email.policy.HTTP).parsebytes(
                b'Content-Type: ' + ctype.encode() + b'\r\n\r\n' + body)
            form = {}
            for part in msg.iter_parts():
                name = part.get_param('name', header='content-disposition')
                if name:
                    form[name] = (part.get_payload(decode=True) or b'').decode('utf-8', 'replace')
            return form
        return {k: v[0] for k, v in parse_qs(body.decode('utf-8', 'replace'), keep_blank_values=True).items()}

    def do_GET(self):
        self.route('GET')

    def do_POST(self):
        self.route('POST')

    def route(self, method):
        url = urlsplit(self.path)
        path = url.path
        q = {k: v[0] for k, v in parse_qs(url.query, keep_blank_values=True).items()}
        form = self.read_form() if method == 'POST' else {}
        self.status = 0
        with board.lock:
            self.dispatch(method, path, q, form)
            if not path.startswith('/__'):
                shown = path + ('?' + url.query if url.query else '')
                board.log('%s %s -> %s' % (method, shown[:90], self.status))

    # ---- the firmware's endpoints --------------------------------------
    def dispatch(self, method, path, q, form):
        cfg = board.config

        # tester-only endpoints
        if path == '/__tester':
            return self.static(HERE, 'tester.html')
        if path == '/__state':
            return self.reply(200, json.dumps({
                'version': board.version,
                'animVersion': board.anim_version,
                'anim': board.current,
                'displayOn': board.display_on,
                'sleeping': board.sleeping,
                'brightness': auto_brightness()['bright'],
                'blanking': auto_brightness()['blanking'],
                'ambient': board.ambient,
                'toggles': {k: cfg.get(k) for k in ('boopEna', 'speechEna', 'tiltEna', 'bleEna', 'oledEna', 'bobEna', 'boopCountEna')},
                'boopCount': int(cfg.get('boopCount') or 0),
                'boopAgo': round(time.time() - board.boop_at, 3) if board.boop_at else 9999,
                'wifi': cfg.get('wifiName'),
                'log': board.log_lines[-80:],
            }), 'application/json')
        if path == '/__anim':
            return self.reply(200, board.current_anim_text(), 'application/json')
        if path == '/__boop' and method == 'POST':   # the sensor isn't simulated, so boop by hand
            # route() already holds board.lock, and it is not reentrant - taking it here hangs
            board.config['boopCount'] = int(board.config.get('boopCount') or 0) + 1
            board.save_config()
            board.log('[I] Boop #%d' % board.config['boopCount'])
            # Like the firmware: the boop face plays while the paw is there, and the counter
            # only pops once it moves away, so the number never covers the boop face.
            face = board.config.get('aBoop') or 'boop.json'
            back_to = board.current
            if safe_name(face) and os.path.isfile(board.anim_path(face)):
                board.show(face)
            held = max(1.0, min(10.0, float(q.get('hold', 1.0))))   # how long the paw stays
            board.boop_until = time.time() + held
            timer = threading.Timer(held, end_boop, [back_to])
            timer.daemon = True
            timer.start()
            return self.text('OK')

        if path == '/__light' and method == 'POST':   # the dashboard's room light slider
            board.ambient = max(0, min(5000, to_int(q.get('value', '180'))))
            return self.text('OK')

        if path == '/__button' and method == 'POST':
            if 'hold' in q:   # held 1.5 s: displays off/on, like the firmware
                board.display_on = not board.display_on
                board.log('[I] Button held: displays ' + ('on' if board.display_on else 'off'))
                return self.text('OK')
            anims = board.anims()
            if anims:
                i = anims.index(board.current) + 1 if board.current in anims else 0
                board.log('[I] Expression button pressed')
                board.show(anims[i % len(anims)])
            return self.text('OK')

        if path == '/getfiles':
            return self.text(board.current + ';' + ''.join(a + ';' for a in board.anims()))

        if path == '/change' and method == 'GET':
            if 'anim' not in q:
                return self.text('No valid parameters detected!')
            name = q['anim']
            if safe_name(name) and os.path.isfile(board.anim_path(name)):
                if board.sleeping:   # picking a face wakes it and stays on that face
                    board.sleeping = False
                    board.wake_to = ''
                board.show(name)
                return self.redirect('/saved.html?main')
            return self.text('Loading animation has failed!')

        if path == '/change' and method == 'POST':
            if 'anim' not in form:
                return self.text('No valid parameters detected!')
            try:
                json.loads(form['anim'])
                board.show('POSTAnimLoad', form['anim'])
            except ValueError as e:
                board.log('[E] Failed to deserialize animation file! : %s' % e)
            return self.redirect('/saved.html?main')

        if path == '/saveconfig':
            for key, kind in SAVECONFIG_KEYS.items():
                if key not in q:
                    continue
                v = q[key]
                if kind is bool:
                    value = v in ('true', '1', 'on')
                elif kind is int:
                    value = to_int(v)
                elif kind is float:
                    value = to_float(v)
                else:
                    value = v
                # Quick phrases arrive one slot at a time (phrase0, phraseSpeed0, phraseOnce0) but the
                # firmware stores them as arrays, and the page reads them back as arrays. Fold them the
                # same way here, or the tester quietly disagrees with the mask.
                m = re.match(r'^(phrase|phraseSpeed|phraseOnce)([0-5])$', key)
                if m:
                    name, slot = m.group(1), int(m.group(2))
                    blank = '' if name == 'phrase' else (False if name == 'phraseOnce' else 55)
                    arr = cfg.get(name)
                    if not isinstance(arr, list) or len(arr) != 6:
                        arr = [blank] * 6
                    arr[slot] = value
                    cfg[name] = arr
                    cfg.pop(key, None)
                else:
                    cfg[key] = value
            board.save_config()
            board.log('[I] Config saved')
            return self.redirect('/saved.html?main')

        if path == '/stats':   # system monitor: plausible numbers shaped like the firmware's reply
            return self.reply(200, json.dumps(stats_dict()), 'application/json')

        # ---- status screen ----
        # screen.html and screen_render.js are served from data/ like any other web file, the way
        # the mask serves them; only the save endpoint needs answering.
        if path == '/savescreen' and method == 'POST':
            try:
                layout = json.loads(form.get('layout', ''))
                # Either shape is valid: pages, or the flat element list saved before pages existed
                if not isinstance(layout, dict):
                    raise ValueError('not an object')
                pages = layout.get('pages')
                if isinstance(pages, list):
                    count = sum(len(p.get('elements') or []) for p in pages if isinstance(p, dict))
                    where = '%d page%s' % (len(pages), '' if len(pages) == 1 else 's')
                elif isinstance(layout.get('elements'), list):
                    count = len(layout['elements'])
                    where = '1 page'
                else:
                    raise ValueError('no pages or elements')
            except ValueError as e:
                return self.reply(400, 'Bad layout: %s' % e)
            with open(os.path.join(DATA, 'screen.json'), 'w', encoding='utf-8') as f:
                json.dump(layout, f, indent=1)
            board.log('[I] Screen layout saved (%d elements on %s)' % (count, where))
            return self.text('ok')
        if path == '/__screendata':   # everything the screen can show, in one request, without logging
            s = stats_dict()
            return self.reply(200, json.dumps({
                'anim': board.current, 'sleeping': board.sleeping, 'displayOn': board.display_on,
                'brightness': min(15, max(0, to_int(str(cfg.get('bVisor', 5))))),
                'boopCount': int(cfg.get('boopCount') or 0), 'wifi': cfg.get('wifiName'),
                'boopThresh': int(cfg.get('boopThresh') or 15), 'boopShade': int(cfg.get('boopShade') or 30),
                'ip': s['staIp'],
                'mic': s['mic'], 'speaking': s['speaking'],
                # a milestone while the Boop button is held and the total lands on the interval
                'milestone': bool(s['booping']) and int(cfg.get('boopMilestone') or 0) > 0
                             and int(cfg.get('boopCount') or 0) % int(cfg.get('boopMilestone') or 100) == 0,
                'spTrig': int(cfg.get('spTrig') or 10),
                'wifiClients': s['wifiClients'], 'uptime': s['uptime'], 'tempC': s['tempC'],
                'boopSig': s['boopSig'], 'shade': s['shade'], 'booping': s['booping'],
                'tilt': s['tilt'], 'boop': s['boop'],
            }), 'application/json')

        if path == '/dino':   # the game on the maw. The virtual face cannot play it, so this only
            # tracks the state the page reads, enough to exercise Start, Jump, the score and Stop.
            if 'go' in q:
                board.dino_on = True
                board.dino_score = 0
                board.log('[I] Dino: go')
            elif 'stop' in q and board.dino_on:
                board.dino_on = False
                best = max(int(cfg.get('dinoBest') or 0), board.dino_score)
                cfg['dinoBest'] = best
                board.save_config()
                board.log('[I] Dino: %d (best %d)' % (board.dino_score, best))
            elif 'jump' in q and board.dino_on:
                board.dino_score += 3          # a pretend run, so the score visibly moves
            return self.reply(200, json.dumps({
                'on': board.dino_on,
                'score': board.dino_score,
                'best': int(cfg.get('dinoBest') or 0),
            }), 'application/json')

        if path == '/pair':   # pairing a remote. No radio here, so 'go' just opens a window that
            # times out; a real remote would answer it and land its MAC in the config.
            if 'go' in q:
                board.pair_until = time.time() + 30
                board.log('[I] Listening for a remote to pair')
            elif 'stop' in q:
                board.pair_until = 0
            elif 'forget' in q:
                cfg['rmtMac'] = ''
                board.save_config()
                board.log('[I] Remote unpaired')
            left = max(0, int(board.pair_until - time.time()))
            if not left:
                board.pair_until = 0
            return self.reply(200, json.dumps({
                'mac': cfg.get('rmtMac') or '',
                'pairing': left > 0,
                'secsLeft': left,
            }), 'application/json')

        if path == '/slots':  # the slot machine on the maw, stubbed the same way the dino is:
            # a real spin takes a few seconds and lands on triple sevens one time in three.
            if board.slot_on and time.time() >= board.slot_until:
                board.slot_on = False
                if board.slot_outcome == 1:
                    cfg['slotWins'] = int(cfg.get('slotWins') or 0) + 1
                board.save_config()
                board.log('[I] Slots: %s' % ('won' if board.slot_outcome == 1 else 'lost'))
            if 'go' in q and not board.slot_on:
                board.slot_on = True
                board.slot_outcome = -1
                board.slot_until = time.time() + 3.0
                cfg['slotSpins'] = int(cfg.get('slotSpins') or 0) + 1
                board.save_config()
                board.log('[I] Slots: spinning')
            elif 'stop' in q and board.slot_on:
                board.slot_on = False
                board.log('[I] Slots: stopped')
            if board.slot_on and board.slot_outcome < 0 and time.time() > board.slot_until - 0.6:
                board.slot_outcome = 1 if random.randrange(3) == 0 else 0
            return self.reply(200, json.dumps({
                'on': board.slot_on,
                'outcome': board.slot_outcome,
                'wins': int(cfg.get('slotWins') or 0),
                'spins': int(cfg.get('slotSpins') or 0),
            }), 'application/json')

        if path == '/wakeflick':   # the deep-sleep warm-up. No panels here, so it only says what the mask would do.
            if not cfg.get('wakeFlickEna', True):
                return self.text('Wake-up flicker is switched off')
            style = str(cfg.get('wakeFlickStyle') or 'crt')
            win = min(8000, max(300, to_int(str(cfg.get('wakeFlickMs', 1800)))))
            board.log('[I] Waking up: %s warm-up over %d ms' % (style, win))
            board.log('[I] Warm-up done')
            return self.text('ok')

        if path == '/sleep':   # state=on|off|toggle, or no state to report
            if 'state' in q:
                state = q['state']
                if state not in ('on', 'off', 'toggle'):
                    return self.text('Invalid state')
                want = (not board.sleeping) if state == 'toggle' else state == 'on'
                if want and not board.sleeping:
                    board.wake_to = board.current
                    board.sleeping = True
                    board.log('[I] Sleeping')
                    face = cfg.get('aSleep', 'sleep.json')
                    if safe_name(face) and os.path.isfile(board.anim_path(face)):
                        board.show(face)
                    else:
                        board.log('[E] Sleeping face missing: ' + str(face) + ' (run with --reset to copy new faces from data/)')
                elif not want and board.sleeping:
                    board.sleeping = False
                    board.log('[I] Woke up')
                    if board.wake_to:
                        board.show(board.wake_to)
            return self.text('on' if board.sleeping else 'off')

        if path == '/display':
            if 'state' in q:
                state = q['state']
                if state not in ('on', 'off', 'toggle'):
                    return self.text('Invalid state')
                board.display_on = (not board.display_on) if state == 'toggle' else state == 'on'
                board.log('[I] Displays on' if board.display_on else '[I] Displays off')
            return self.text('on' if board.display_on else 'off')

        if path == '/brightness':
            if 'value' not in q:
                return self.text('No valid parameters detected!')
            value = to_int(q['value'])
            if not 0 <= value <= 15:
                return self.text('Invalid brightness')
            cfg['bVisor'] = value
            if 'save' in q:
                board.save_config()
            board.version += 1
            return self.text('Set brightness to: %d' % value)

        if path == '/settime':   # the page tells the mask what time it is
            if 'mins' not in q:
                return self.text('No time given')
            mins = to_int(q['mins'])
            if not 0 <= mins <= 1439:
                return self.text('Minutes must be 0 to 1439')
            board.clock_mins = mins
            board.clock_at = time.time()
            board.clock_24 = q.get('h24') == '1'
            return self.text('ok')

        if path == '/mictest':   # two seconds quiet, three talking
            board.mic_test_at = time.time()
            board.log('[I] Mic test: keep quiet')
            return self.text('ok')

        if path == '/micloud':   # listen to a speaking voice and set Range from it
            cfg['spMax'] = 420
            board.mic_gain = 1   # the pretend mic is always about right
            board.save_config()
            board.log('[I] Range set to 420 from a peak of 336')
            return self.text('ok')

        if path == '/miccal':   # listen to the silence and store it as the quiet level
            cfg['spMin'] = 38     # the swing of a quiet room, peak to peak
            board.save_config()
            board.log('[I] Noise floor measured: 38')
            return self.text('ok')

        if path == '/autobri':   # measure=1: take a reading now
            if 'measure' in q:
                board.measure_now = True
            return self.text('ok')

        if path == '/ota' and method == 'POST':   # accepts the upload but writes nothing
            data = form.get('file', '')
            kind = 'filesystem' if 'littlefs' in data[:32] else 'firmware'
            board.log('[I] Update received (%s, %d bytes) - the tester writes nothing' % (kind, len(data)))
            return self.text(kind)

        if path == '/otainfo':   # roughly what the new partition table reports
            return self.reply(200, json.dumps({
                'running': 'app0', 'next': 'app1', 'appSize': 1379936, 'appSlot': 3145728,
                'fsSize': 4063232, 'staging': True, 'confirmed': True, 'built': 'virtual tester'}),
                'application/json')

        if path == '/update':
            return self.redirect('/update.html')

        if path == '/savefile' and method == 'POST':
            if 'file' not in form or 'content' not in form:
                return self.text('No valid parameters detected!')
            name = form['file'] + '.json'
            if not safe_name(name):
                return self.text('Error opening file for writing!')
            with open(board.anim_path(name), 'w', encoding='utf-8') as f:
                f.write(form['content'])
            board.log('[I] File saved! ' + name)
            return self.redirect('/saved.html?anim')

        if path == '/deletefile':
            if 'file' not in q:
                return self.text("Parameter 'file' not present!")
            if safe_name(q['file']) and os.path.isfile(board.anim_path(q['file'])):
                os.remove(board.anim_path(q['file']))
            return self.redirect('/saved.html?main')

        if path == '/heap':
            return self.text('180000')
        if path == '/gyro':
            return self.text('%.2f;%.2f;%.2f' % (cfg.get('neutralX', 0), cfg.get('neutralY', 0), cfg.get('neutralZ', 0)))
        if path == '/tof':
            return self.text('238')   # APDS9960-style reading with nothing near the visor
        if path == '/log':
            return self.text('\n'.join(board.log_lines))
        if path == '/update':
            return self.reply(200, '<body style="font-family:Verdana;background:#333743;color:white;text-align:center">'
                                   '<h2>OTA update</h2><p>Not available in the virtual tester.</p>'
                                   '<a href="/" style="color:#8cf">Back</a></body>', 'text/html; charset=utf-8')

        # the board's LittleFS: settings and faces from the virtual board, everything else live from data/
        if path == '/config.json':
            return self.static(BOARD_DIR, 'config.json')
        if path.startswith('/anims/'):
            return self.static(os.path.join(BOARD_DIR, 'anims'), path[len('/anims/'):])
        if path == '/':
            return self.static(DATA, 'index.html')
        return self.static(DATA, path)


def main():
    global board
    ap = argparse.ArgumentParser(description='Virtual tester for the ProtoESP web UI')
    ap.add_argument('--port', type=int, default=8080)
    ap.add_argument('--reset', action='store_true', help='recopy faces and settings from data/')
    ap.add_argument('--no-browser', action='store_true', help="don't open the dashboard")
    args = ap.parse_args()

    if args.reset and os.path.isdir(BOARD_DIR):
        shutil.rmtree(BOARD_DIR)
    if not os.path.isdir(BOARD_DIR):
        shutil.copytree(os.path.join(DATA, 'anims'), os.path.join(BOARD_DIR, 'anims'))
        shutil.copy2(os.path.join(DATA, 'config.json'), BOARD_DIR)
        print('Virtual board created from data/ (faces and settings)')

    board = VirtualBoard()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    url = 'http://localhost:%d/__tester' % args.port
    print('ProtoESP virtual tester running.')
    print('  Dashboard:  ' + url)
    print('  Homepage:   http://localhost:%d/' % args.port)
    print('Press Ctrl+C to stop.')
    if not args.no_browser:
        threading.Timer(0.5, webbrowser.open, [url]).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print('Stopped.')


if __name__ == '__main__':
    main()
