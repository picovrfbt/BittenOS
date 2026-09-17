// Draws a 128x64 status-screen layout (screen.json) the way the mask's SSD1306 does.
// Shared by the screen designer (screen.html) and the tester dashboard's preview.
//
// Text uses the same 5x7 font as the scrolling text (MD_MAX72XX system font, LGPL-2.1), drawn
// pixel for pixel at 1x, 2x or 3x. src/screen_font.h is generated from the tables below by
// tools/make_screen_font.py, so the firmware draws exactly what the designer shows.
(function () {
  const W = 128, H = 64;

  // ASCII 32-126, each character a list of columns, bit 0 the top row (8 rows with descenders)
  const FONT = [[0,0],[95],[7,0,7],[20,127,20,127,20],[68,74,255,74,50],[99,19,8,100,99],[54,73,73,54,72],[7],[62,65,65],[65,65,62],[8,42,28,42,8],[8,8,62,8,8],[96,224],[8,8,8,8],[96,96],[96,16,8,4,3],[62,81,73,69,62],[4,2,127],[113,73,73,73,70],[65,73,73,73,54],[15,8,8,8,127],[79,73,73,73,49],[62,73,73,73,48],[3,1,1,1,127],[54,73,73,73,54],[6,73,73,73,62],[108,108],[108,236],[8,20,34],[20,20,20,20],[34,20,8],[1,89,9,9,6],[62,65,93,89,78],[126,9,9,9,126],[127,73,73,73,54],[62,65,65,65,65],[127,65,65,65,62],[127,73,73,73,65],[127,9,9,9,1],[62,65,65,73,121],[127,8,8,8,127],[65,127,65],[48,65,65,65,63],[127,8,20,34,65],[127,64,64,64,64],[127,2,12,2,127],[127,4,8,16,127],[62,65,65,65,62],[127,9,9,9,6],[62,65,65,97,126],[127,9,25,41,70],[70,73,73,73,49],[1,1,127,1,1],[63,64,64,64,63],[31,32,64,32,31],[63,64,56,64,63],[99,20,8,20,99],[3,4,120,4,3],[97,81,73,69,67],[127,65,65],[3,4,8,16,96],[65,65,127],[4,2,1,2,4],[128,128,128,128],[1,2,4],[56,68,68,124],[127,68,68,56],[56,68,68,68],[56,68,68,127],[56,84,84,88],[4,126,5,1],[24,164,164,124],[127,4,4,120],[125],[132,133,124],[127,16,40,68],[127],[124,4,120,4,120],[124,4,4,120],[56,68,68,56],[252,36,36,24],[24,36,36,252],[124,4,4,8],[88,84,84,52],[4,127,4],[60,64,64,124],[28,32,64,124],[60,64,48,64,60],[108,16,16,108],[28,160,160,124],[100,84,84,76],[8,54,65,65],[127],[65,65,54,8],[2,1,2,1]];

  // 8x8 icons, one byte per row, top row first, bit 7 the leftmost pixel
  const ICONS = {
    paw:     [0x24,0xA5,0x81,0x3C,0x7E,0x7E,0x3C,0x00],
    wifi:    [0x00,0x3C,0x42,0x99,0x24,0x00,0x18,0x18],
    moon:    [0x3C,0x70,0xE0,0xE0,0xE0,0xE0,0x70,0x3C],
    heart:   [0x66,0xFF,0xFF,0xFF,0x7E,0x3C,0x18,0x00],
    bolt:    [0x0C,0x18,0x30,0x7E,0x0C,0x18,0x30,0x00],
    eye:     [0x00,0x3C,0x42,0x99,0x99,0x42,0x3C,0x00],
    check:   [0x00,0x01,0x03,0x86,0xCC,0x78,0x30,0x00],
    cross:   [0xC3,0x66,0x3C,0x18,0x3C,0x66,0xC3,0x00],
    sun:     [0x10,0x54,0x38,0xFE,0x38,0x54,0x10,0x00],
    temp:    [0x18,0x24,0x24,0x24,0x5A,0x5A,0x3C,0x00],
  };

  function fmtUptime(s) {
    s = Math.floor(s || 0);
    const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
    return d ? d + 'd' + h + 'h' : h ? h + 'h' + m + 'm' : m + 'm' + (s % 60) + 's';
  }

  // what a "value" element can show; d is the normalised data from normalise()
  // The order here is the order src/screen.cpp expects, so keep them in step.
  const SOURCES = {
    face:        {label: 'Face name',        get: d => d.face},
    boops:       {label: 'Boop count',       get: d => String(d.boops)},
    wifiClients: {label: 'WiFi devices',     get: d => String(d.wifiClients)},
    wifiName:    {label: 'WiFi name',        get: d => d.wifiName},
    brightness:  {label: 'Brightness (0-15)',get: d => String(d.brightness)},
    uptime:      {label: 'Uptime',           get: d => fmtUptime(d.uptime)},
    temp:        {label: 'Chip temperature', get: d => Math.round(d.tempC) + 'C'},
    closeness:   {label: 'Boop closeness',   get: d => String(d.closeness)},
    shade:       {label: 'Shadow %',         get: d => d.shade + '%'},
    tilt:        {label: 'Tilt sensor ok/--',get: d => d.tilt ? 'ok' : '--'},
    boopSensor:  {label: 'Boop sensor ok/--',get: d => d.boopSensor ? 'ok' : '--'},
    displays:    {label: 'Displays on/off',  get: d => d.displayOn ? 'on' : 'off'},
    clock:       {label: 'Clock',            get: d => {
      // Blinks 00:00 until a phone has set it, the way an unset appliance clock does
      if (d.clockMins < 0) return (Math.floor(Date.now() / 600) % 2) ? '00:00' : '     ';
      let h = Math.floor(d.clockMins / 60);
      const m = d.clockMins % 60;
      if (!d.clock24) { h = h % 12; if (h === 0) h = 12; }
      return (d.clock24 && h < 10 ? '0' + h : String(h)) + ':' + (m < 10 ? '0' + m : m);
    }},
    clockAmPm:   {label: 'am / pm',          get: d => d.clockMins < 0 ? '' : (d.clockMins < 720 ? 'am' : 'pm')},
  };

  // what a "bar" element can fill with: fraction 0-1, and where its marker line goes
  const BARS = {
    closeness:  {label: 'Boop closeness', get: d => [d.closeness / Math.max(10, d.boopThresh * 2), d.boopThresh / Math.max(10, d.boopThresh * 2)]},
    shade:      {label: 'Shadow',         get: d => [d.shade / 100, d.shadeNeed / 100]},
    brightness: {label: 'Brightness',     get: d => [d.brightness / 15, null]},
  };

  const SHOW = {
    always: {label: 'Always', test: () => true},
    booped: {label: 'While booped', test: d => d.booping},
    notBooped: {label: 'When not booped', test: d => !d.booping},
    sleeping: {label: 'While sleeping', test: d => d.sleeping},
    awake: {label: 'While awake', test: d => !d.sleeping},
    displaysOff: {label: 'While displays are off', test: d => !d.displayOn},
  };

  const TYPES = {
    text:  'Text',
    value: 'Live value',
    bar:   'Bar',
    icon:  'Icon',
    rect:  'Box / line',
  };

  const EXAMPLE_ELEMENTS = [
    {type: 'value', source: 'face', prefix: '', suffix: '', x: 64, y: 4, size: 2, align: 'center', show: 'notBooped'},
    {type: 'text', text: 'BOOP!', x: 64, y: 0, size: 3, align: 'center', show: 'booped'},
    {type: 'rect', x: 0, y: 25, w: 128, h: 1, fill: true, show: 'always'},
    {type: 'icon', icon: 'paw', x: 2, y: 30, size: 1, show: 'always'},
    {type: 'value', source: 'boops', prefix: '', suffix: '', x: 13, y: 30, size: 1, align: 'left', show: 'always'},
    {type: 'icon', icon: 'wifi', x: 52, y: 30, size: 1, show: 'always'},
    {type: 'value', source: 'wifiClients', prefix: '', suffix: '', x: 63, y: 30, size: 1, align: 'left', show: 'always'},
    {type: 'icon', icon: 'moon', x: 118, y: 30, size: 1, show: 'sleeping'},
    {type: 'text', text: 'close', x: 2, y: 43, size: 1, align: 'left', show: 'always'},
    {type: 'bar', source: 'closeness', x: 36, y: 44, w: 90, h: 6, marker: true, show: 'always'},
    {type: 'text', text: 'shade', x: 2, y: 54, size: 1, align: 'left', show: 'always'},
    {type: 'bar', source: 'shade', x: 36, y: 55, w: 90, h: 6, marker: true, show: 'always'},
  ];

  // The starting point: a status page, plus a boop page that takes over for a moment when booped
  const EXAMPLE = {version: 2, panel: 'yellowBlue', yellowAt: 'top', cycle: 0, pages: [
    {name: 'Status', show: 'always', hold: 0, elements: EXAMPLE_ELEMENTS},
    {name: 'Boop', show: 'booped', hold: 1500, elements: [
      {type: 'text', text: 'BOOP!', x: 64, y: 6, size: 3, align: 'center', show: 'always'},
      {type: 'icon', icon: 'paw', x: 2, y: 40, size: 2, show: 'always'},
      {type: 'value', source: 'boops', prefix: '#', suffix: '', x: 124, y: 44, size: 2, align: 'right', show: 'always'},
    ]},
  ]};

  function glyph(ch) {
    const code = ch.charCodeAt(0);
    return FONT[(code >= 32 && code <= 126 ? code : 63) - 32];
  }

  function textWidth(str, size) {
    if (!str) return 0;
    let cols = 0;
    for (const ch of str) cols += glyph(ch).length + 1;
    return (cols - 1) * size;
  }

  // Turns the mask's /stats (plus a few /config.json fields) into what the sources read. The
  // tester's /__screendata is the same shape, so either works.
  function normalise(raw) {
    raw = raw || {};
    const pick = (...names) => { for (const n of names) if (raw[n] !== undefined) return raw[n]; };
    const face = String(pick('anim') || 'default.json');
    const num = v => (typeof v === 'number' ? v : 0);
    return {
      face: face === 'POSTAnimLoad' ? 'text' : face.replace(/\.json$/, ''),
      boops: num(pick('boops', 'boopCount')),
      wifiClients: num(pick('wifiClients')),
      wifiName: pick('wifiName', 'wifi') || 'ProtoWiFi',
      brightness: num(pick('bright', 'brightness')),
      uptime: num(pick('uptime')),
      tempC: num(pick('tempC')),
      closeness: num(pick('boopSig')),
      boopThresh: num(pick('boopThresh')) || 15,
      shade: num(pick('shade')),
      shadeNeed: num(pick('boopShade')) || 30,
      booping: !!pick('booping'),
      sleeping: !!pick('sleeping'),
      displayOn: pick('displayOn', 'display') !== false,
      tilt: !!pick('tilt'),
      boopSensor: !!pick('boop'),
      clockMins: pick('clockMins') === undefined ? -1 : num(pick('clockMins')),
      clock24: !!pick('clock24'),
    };
  }

  // Draws the layout into a 128x64 buffer. Returns the buffer and every element's box (drawn or
  // not, before clipping), for hit-testing in the designer.
  // A layout saved before pages existed is a flat list of elements; one saved since has pages, each
  // with its own list. Everything reading a layout goes through here so both shapes work.
  function pagesOf(layout) {
    if (!layout) return [];
    if (Array.isArray(layout.pages)) return layout.pages;
    if (Array.isArray(layout.elements)) return [{name: 'Page 1', show: 'always', hold: 0, elements: layout.elements}];
    return [];
  }

  function elementsOf(layout, page) {
    const pages = pagesOf(layout);
    const p = pages[page || 0];
    return (p && Array.isArray(p.elements)) ? p.elements : [];
  }

  function renderBuffer(layout, d, page) {
    const buf = new Uint8Array(W * H);
    const set = (x, y) => { if (x >= 0 && x < W && y >= 0 && y < H) buf[y * W + x] = 1; };
    const boxes = [];
    const els = elementsOf(layout, page || 0);
    els.forEach((el, i) => {
      const visible = (SHOW[el.show] || SHOW.always).test(d);
      const size = Math.max(1, Math.min(3, el.size | 0 || 1));
      let box = {i, x: el.x | 0, y: el.y | 0, w: 1, h: 1, visible};
      if (el.type === 'text' || el.type === 'value') {
        const str = el.type === 'text' ? String(el.text || '')
          : (el.prefix || '') + (SOURCES[el.source] ? SOURCES[el.source].get(d) : '?') + (el.suffix || '');
        const w = Math.max(1, textWidth(str, size));
        const x0 = el.align === 'center' ? (el.x | 0) - Math.floor(w / 2) : el.align === 'right' ? (el.x | 0) - w + 1 : (el.x | 0);
        box = {i, x: x0, y: el.y | 0, w, h: 8 * size, visible};
        if (visible) {
          let cx = x0;
          for (const ch of str) {
            const g = glyph(ch);
            g.forEach((col, c) => {
              for (let r = 0; r < 8; r++) if ((col >> r) & 1)
                for (let sy = 0; sy < size; sy++) for (let sx = 0; sx < size; sx++) set(cx + c * size + sx, (el.y | 0) + r * size + sy);
            });
            cx += (g.length + 1) * size;
          }
        }
      } else if (el.type === 'icon') {
        const rows = ICONS[el.icon] || ICONS.cross;
        box = {i, x: el.x | 0, y: el.y | 0, w: 8 * size, h: 8 * size, visible};
        if (visible) rows.forEach((row, r) => {
          for (let c = 0; c < 8; c++) if ((row >> (7 - c)) & 1)
            for (let sy = 0; sy < size; sy++) for (let sx = 0; sx < size; sx++) set((el.x | 0) + c * size + sx, (el.y | 0) + r * size + sy);
        });
      } else if (el.type === 'bar') {
        const w = Math.max(3, el.w | 0), h = Math.max(3, el.h | 0);
        box = {i, x: el.x | 0, y: el.y | 0, w, h, visible};
        if (visible) {
          const [frac, mark] = (BARS[el.source] || BARS.brightness).get(d);
          for (let x = 0; x < w; x++) { set(el.x + x, el.y); set(el.x + x, el.y + h - 1); }
          for (let y = 0; y < h; y++) { set(el.x, el.y + y); set(el.x + w - 1, el.y + y); }
          const inner = w - 4, fill = Math.round(Math.max(0, Math.min(1, frac || 0)) * inner);
          for (let x = 0; x < fill; x++) for (let y = 2; y < h - 2; y++) set(el.x + 2 + x, el.y + y);
          if (el.marker && mark !== null && mark !== undefined) {
            const mx = el.x + 2 + Math.round(Math.max(0, Math.min(1, mark)) * (inner - 1));
            for (let y = -1; y <= h; y++) set(mx, el.y + y);
          }
        }
      } else if (el.type === 'rect') {
        const w = Math.max(1, el.w | 0), h = Math.max(1, el.h | 0);
        box = {i, x: el.x | 0, y: el.y | 0, w, h, visible};
        if (visible) for (let y = 0; y < h; y++) for (let x = 0; x < w; x++)
          if (el.fill || y === 0 || y === h - 1 || x === 0 || x === w - 1) set(el.x + x, el.y + y);
      }
      boxes.push(box);
    });
    return {buf, boxes};
  }

  // The common two-colour panels have a 16-row yellow strip. Which edge it ends up on depends on
  // how the screen is mounted and whether the mask is drawing upside down, so it's a setting.
  const YELLOW_ROWS = 16;
  const COLOURS = {
    white: () => '#eef6ff',
    blue: () => '#55c6ff',
    yellowBlue: (y, at) => ((at === 'bottom' ? y >= H - YELLOW_ROWS : y < YELLOW_ROWS) ? '#ffd84a' : '#55c6ff'),
  };

  // Paints a buffer onto a canvas at `scale` screen pixels per OLED pixel
  function paint(canvas, buf, scale, colour, yellowAt) {
    const dpr = window.devicePixelRatio || 1;
    const cw = W * scale, ch = H * scale;
    if (canvas.width !== cw * dpr) { canvas.width = cw * dpr; canvas.height = ch * dpr; }
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.fillStyle = '#05070a';
    ctx.fillRect(0, 0, cw, ch);
    const colourAt = COLOURS[colour] || COLOURS.white;
    const gap = scale >= 4 ? 1 : 0;
    for (let y = 0; y < H; y++) {
      ctx.fillStyle = colourAt(y, yellowAt || 'top');
      for (let x = 0; x < W; x++) if (buf[y * W + x]) ctx.fillRect(x * scale, y * scale, scale - gap, scale - gap);
    }
    return ctx;
  }

  // Which page the mask would be showing, so the dashboard preview matches the real screen: a page
  // with a condition takes over while it holds, otherwise the rotation's current page.
  function pickPage(layout, d, rotation) {
    const pages = pagesOf(layout);
    for (let i = 0; i < pages.length; i++) {
      const show = pages[i].show || 'always';
      if (show === 'always') continue;
      if ((SHOW[show] || SHOW.always).test(d)) return i;
    }
    const always = [];
    pages.forEach((p, i) => { if ((p.show || 'always') === 'always') always.push(i); });
    if (!always.length) return 0;
    return always[(rotation || 0) % always.length];
  }

  window.ScreenSim = {W, H, YELLOW_ROWS, FONT, ICONS, SOURCES, BARS, SHOW, TYPES, EXAMPLE,
                      normalise, renderBuffer, paint, textWidth, pagesOf, elementsOf, pickPage};
})();
