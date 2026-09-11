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

# Settings /saveconfig accepts, in the same order as src/main.cpp
SAVECONFIG_KEYS = [
    'boopEna', 'speechEna', 'tiltEna', 'bleEna', 'oledEna',
    'bEar', 'bVisor', 'bOled', 'rbSpeed', 'rbWidth', 'spMin', 'spMax', 'spTrig',
    'aTilt', 'aUp', 'aBoop',
    'neutralX', 'neutralY', 'neutralZ', 'tiltX', 'tiltY', 'tiltZ', 'upX', 'upY', 'upZ', 'tiltTol',
    'visColor', 'wifiName', 'wifiPass', 'boopThresh',
]

CONTENT_TYPES = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'application/javascript',
    '.json': 'application/json',
    '.txt': 'text/plain; charset=utf-8',
    '.css': 'text/css',
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
                'brightness': min(15, max(0, to_int(str(cfg.get('bVisor', 5))))),
                'fan': cfg.get('fanDuty'),
                'toggles': {k: cfg.get(k) for k in ('boopEna', 'speechEna', 'tiltEna', 'bleEna', 'oledEna')},
                'wifi': cfg.get('wifiName'),
                'log': board.log_lines[-80:],
            }), 'application/json')
        if path == '/__anim':
            return self.reply(200, board.current_anim_text(), 'application/json')
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
            for key in SAVECONFIG_KEYS:
                if key not in q:
                    continue
                v, old = q[key], cfg.get(key)
                if isinstance(old, bool):
                    cfg[key] = v in ('true', '1', 'on')
                elif isinstance(old, int):
                    cfg[key] = to_int(v)
                elif isinstance(old, float):
                    cfg[key] = to_float(v)
                else:
                    cfg[key] = v
            board.save_config()
            board.log('[I] Config saved')
            return self.redirect('/saved.html?main')

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

        if path == '/fanpwm':
            if 'duty' not in q:
                return self.text('No valid parameters detected!')
            duty = to_int(q['duty'])
            if not 0 <= duty < 256:
                return self.text('Invalid duty cycle')
            cfg['fanDuty'] = duty
            board.save_config()
            return self.text('Set PWM to: %d' % duty)

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
