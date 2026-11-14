// A working copy of the charm's screen. It draws the same deck and blocks as firmware 4.0,
// follows the controls on this page, and opens a block when you tap it.
(() => {
  const $ = id => document.getElementById(id);
  const cv = $('heroCv'), c = cv.getContext('2d'), rig = $('rig');
  const W = 240, H = 280, ui = window.ui;
  const calm = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const MONO = 'ui-monospace, Menlo, Consolas, "Courier New", monospace';
  const THEMES = ['#00E5FF', '#FF2BD6', '#FFB000', '#39FF14', '#FF3B30', '#C8DCFF'];
  const VOID = '#03050c';
  const val = id => $(id).value;
  let frame = 0, glitch = 0, tiltX = 0, tiltY = 0.4, ACC = THEMES[0];

  // ── tilt toward the pointer (or with the phone) ──
  let rx = 0, ry = 0, trx = 0, tryy = 0;
  addEventListener('pointermove', e => {
    if (calm) return;
    const r = rig.getBoundingClientRect();
    tryy = Math.max(-1, Math.min(1, (e.clientX - (r.left + r.width / 2)) / 420));
    trx = Math.max(-1, Math.min(1, (e.clientY - (r.top + r.height / 2)) / 420));
  }, { passive: true });
  addEventListener('deviceorientation', e => { if (e.gamma != null && !calm) { tryy = Math.max(-1, Math.min(1, e.gamma / 40)); trx = Math.max(-1, Math.min(1, (e.beta - 45) / 40)); } });
  window.kickCharm = (n = 3) => { if (!calm) glitch = Math.max(glitch, Math.round(n)); };

  // ── drawing helpers, named after the firmware's ──
  const rgb = h => [parseInt(h.slice(1, 3), 16), parseInt(h.slice(3, 5), 16), parseInt(h.slice(5, 7), 16)];
  const mixk = (h, k) => { const [r, g, b] = rgb(h); return `rgb(${r * k | 0},${g * k | 0},${b * k | 0})`; };
  const R = (x, y, w, h, col) => { c.fillStyle = col; c.fillRect(x, y, w, h); };
  const dim = (x, y, w, h, a) => R(x, y, w, h, `rgba(0,0,0,${a})`);
  function T(s, x, y, size, col) {            // top-left anchored, 6 px per character like the charm's font
    s = String(s); c.font = `bold ${10 * size}px ${MONO}`; c.textAlign = 'left'; c.textBaseline = 'top'; c.fillStyle = col;
    c.fillText(s, x, y - size, s.length * 6 * size);
  }
  const TC = (s, y, size, col) => T(s, (W - String(s).length * 6 * size) / 2, y, size, col);
  function line(x0, y0, x1, y1, col) { c.strokeStyle = col; c.lineWidth = 1; c.beginPath(); c.moveTo(x0 + .5, y0 + .5); c.lineTo(x1 + .5, y1 + .5); c.stroke(); }
  function circle(x, y, r, col, fill) { c.beginPath(); c.arc(x, y, r, 0, 7); if (fill) { c.fillStyle = col; c.fill(); } else { c.strokeStyle = col; c.lineWidth = 1; c.stroke(); } }
  function hudPanel(x, y, w, h, col, a) {
    if (a) dim(x, y, w, h, a);
    c.strokeStyle = col; c.lineWidth = 1; c.beginPath();
    c.moveTo(x + 6.5, y + .5); c.lineTo(x + w - .5, y + .5); c.lineTo(x + w - .5, y + h - 6.5); c.lineTo(x + w - 6.5, y + h - .5); c.lineTo(x + .5, y + h - .5); c.lineTo(x + .5, y + 6.5); c.closePath(); c.stroke();
  }
  function brackets(x, y, w, h, k, col) {
    c.strokeStyle = col; c.lineWidth = 1; c.beginPath();
    [[x, y, 1, 1], [x + w - 1, y, -1, 1], [x, y + h - 1, 1, -1], [x + w - 1, y + h - 1, -1, -1]].forEach(([px, py, sx, sy]) => { c.moveTo(px + .5 + sx * k, py + .5); c.lineTo(px + .5, py + .5); c.lineTo(px + .5, py + .5 + sy * k); });
    c.stroke();
  }
  const tag = (s, x, y) => { R(x, y, s.length * 6 + 5, 9, ACC); T(s, x + 3, y + 1, 1, '#000'); };
  const SEG7 = [0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F];
  function segDigit(x, y, w, h, t, d, on, off) {
    const m = d >= 0 && d <= 9 ? SEG7[d] : 0x40, vl = ((h - 3 * t) / 2 | 0) - 2, hl = w - 2 * t - 2, yg = y + t + vl + 2, yd = yg + t + vl + 2;
    R(x + t + 1, y, hl, t, m & 1 ? on : off); R(x + w - t, y + t + 1, t, vl, m & 2 ? on : off); R(x + w - t, yg + t + 1, t, vl, m & 4 ? on : off);
    R(x + t + 1, yd, hl, t, m & 8 ? on : off); R(x, yg + t + 1, t, vl, m & 16 ? on : off); R(x, y + t + 1, t, vl, m & 32 ? on : off); R(x + t + 1, yg, hl, t, m & 64 ? on : off);
  }
  function segTime(x, y, w, h, t, hh, mm, colon, on, off) {
    segDigit(x, y, w, h, t, hh / 10 | 0, on, off); segDigit(x + w + t, y, w, h, t, hh % 10, on, off);
    const cx = x + 2 * w + 2 * t;
    R(cx, y + (h / 3 | 0) - (t >> 1), t, t, colon ? on : off); R(cx, y + (2 * h / 3 | 0) - (t >> 1), t, t, colon ? on : off);
    segDigit(cx + 2 * t, y, w, h, t, mm / 10 | 0, on, off); segDigit(cx + 3 * t + w, y, w, h, t, mm % 10, on, off);
  }
  const pad = n => String(n).padStart(2, '0');
  function topBar(label, showTime) {
    dim(0, 0, W, 23, .5); T(label, 34, 9, 1, ACC);
    const d = new Date();
    if (showTime) T(pad(d.getHours()) + ':' + pad(d.getMinutes()), 105, 9, 1, '#fff');
    const on = document.body.classList.contains('connected');
    for (let i = 0; i < 3; i++) R(148 + i * 4, 15 - i * 2, 3, 2 + i * 2, on ? ACC : '#5a5a6e');
    const bt = (ui.bat || 76) + '%'; T(bt, 214 - bt.length * 6, 9, 1, ui.charging ? '#34c759' : ACC);
  }
  function marquee(s, chars, div) { if (s.length <= chars) return s; const loop = s + '   '; const off = (frame / div | 0) % loop.length; return (loop + loop).slice(off, off + chars); }
  const song = () => ({ title: $('spTitle').textContent || val('mTitle') || '', artist: $('spArtist').textContent || val('mArtist') || '' });
  const eq = (i, n) => ui.playing ? Math.abs(Math.sin(frame * 0.3 + i * 0.9) * Math.sin(i * 1.7 + frame * 0.11)) : 0.06;

  // ── backdrops ──
  const stars = Array.from({ length: 60 }, () => ({ x: Math.random() * 2 - 1, y: Math.random() * 2 - 1, z: Math.random() }));
  const flakes = Array.from({ length: 60 }, () => ({ x: Math.random() * W, y: Math.random() * H, r: 1 + Math.random() * 2.5 }));
  const cols = Array.from({ length: 24 }, () => ({ y: Math.random() * -30, v: 0.15 + Math.random() * 0.3 }));
  const nodes = Array.from({ length: 16 }, () => ({ x: 10 + Math.random() * 220, y: 10 + Math.random() * 260, vx: Math.random() * .8 - .4, vy: Math.random() * .8 - .4 }));
  function grad(a, b) { const g = c.createLinearGradient(0, 0, 0, H); g.addColorStop(0, a); g.addColorStop(1, b); c.fillStyle = g; c.fillRect(0, 0, W, H); }
  function background(bg) {
    const t = frame;
    if (bg === 9) {
      R(0, 0, W, H, VOID);
      for (const n of nodes) { n.x += n.vx + tiltX * .4; n.y += n.vy; if (n.x < 4 || n.x > 235) { n.vx = -n.vx; n.x = Math.max(4, Math.min(235, n.x)); } if (n.y < 4 || n.y > 275) { n.vy = -n.vy; n.y = Math.max(4, Math.min(275, n.y)); } }
      for (let i = 0; i < 16; i++) for (let j = i + 1; j < 16; j++) { const dx = nodes[i].x - nodes[j].x, dy = nodes[i].y - nodes[j].y, d2 = dx * dx + dy * dy; if (d2 < 6400) { c.globalAlpha = 1 - d2 / 6400; line(nodes[i].x | 0, nodes[i].y | 0, nodes[j].x | 0, nodes[j].y | 0, ACC); } }
      c.globalAlpha = 1; for (const n of nodes) R((n.x | 0) - 1, (n.y | 0) - 1, 3, 3, ACC);
    } else if (bg === 10) {
      R(0, 0, W, H, '#000'); for (let y = 10; y < H; y += 20) for (let x = 10; x < W; x += 20) R(x, y, 1, 1, mixk(ACC, .45));
      const sy = (t * 2) % (H + 60) - 30; for (let k = 0; k < 24; k++) R(0, sy - k, W, 1, mixk(ACC, .37 - k * .015));
    } else if (bg === 1) {
      R(0, 0, W, H, '#040212');
      for (const s of stars) { s.z -= 0.012; if (s.z <= 0.05) { s.x = Math.random() * 2 - 1; s.y = Math.random() * 2 - 1; s.z = 1; } const b = 255 * (1 - s.z); R(120 + tiltX * 40 + s.x / s.z * 120, 140 + s.y / s.z * 140, s.z < 0.3 ? 2 : 1, s.z < 0.3 ? 2 : 1, `rgb(${b | 0},${b | 0},${Math.min(255, b + 60) | 0})`); }
    } else if (bg === 2) {
      const g = c.createLinearGradient(0, 0, 0, 170); g.addColorStop(0, '#14063c'); g.addColorStop(1, '#ff5aaa'); c.fillStyle = g; c.fillRect(0, 0, W, 170);
      const sg = c.createLinearGradient(0, 66, 0, 170); sg.addColorStop(0, '#ffe65a'); sg.addColorStop(1, '#ff508c'); circle(120, 118, 52, sg, true);
      for (let y = 124 + (t / 3 % 10); y < 170; y += 10) R(60, y, 120, 3, '#b03a8e');
      R(0, 170, W, 110, '#0e0428'); c.strokeStyle = '#5adcff'; c.lineWidth = 1; c.beginPath();
      for (let i = 0; i < 9; i++) { const k = (i + (t % 20) / 20) / 9, y = 170 + k * k * 110; c.moveTo(0, y); c.lineTo(W, y); }
      for (let i = -8; i <= 8; i++) { c.moveTo(120 + i * 14, 170); c.lineTo(120 + i * 90, H); }
      c.stroke(); R(0, 170, W, 1.5, '#f472b6');
    } else if (bg === 3) {
      R(0, 0, W, H, '#000'); c.font = `bold 10px ${MONO}`; c.textAlign = 'left'; c.textBaseline = 'top';
      cols.forEach((col, i) => { col.y += col.v; if (col.y > 40) { col.y = Math.random() * -12; col.v = 0.15 + Math.random() * 0.3; } for (let k = 0; k < 12; k++) { const row = (col.y | 0) - k; if (row < 0 || row > 28) continue; c.fillStyle = k === 0 ? '#c8ffdc' : `rgb(0,${220 - k * 18},${50 - k * 4})`; c.fillText(String.fromCharCode(33 + (i * 31 + row * 17 + (t >> 4)) % 90), i * 10, row * 10); } });
    } else if (bg === 4 || bg === 5) {
      R(0, 0, W, H, bg === 4 ? '#2e1065' : '#28083c');
      (bg === 4 ? ['#ff5ad2', '#5ad2ff', '#ffe14a', '#7be36b'] : ['#ff8c3c', '#ff3c78', '#ffe178', '#ff8c3c']).forEach((col, i) => {
        const x = 120 + Math.sin(t * 0.021 + i * 2.1) * 80, y = 140 + Math.cos(t * 0.017 + i * 1.3) * 100, r = bg === 4 ? 130 : 46 + i * 8;
        const g = c.createRadialGradient(x, y, 0, x, y, r); g.addColorStop(0, col); g.addColorStop(bg === 4 ? 1 : 0.75, bg === 4 ? 'transparent' : col); g.addColorStop(1, 'transparent');
        c.globalAlpha = bg === 4 ? 0.75 : 1; c.fillStyle = g; c.fillRect(0, 0, W, H);
      });
      c.globalAlpha = 1;
    } else if (bg === 6) {
      grad('#08143c', '#285aa0');
      for (const f of flakes) { f.x += tiltX * f.r * 1.4 + (Math.random() - 0.5) * 0.6; f.y += f.r * 0.9; if (f.y > H) f.y -= H; if (f.x < 0) f.x += W; if (f.x > W) f.x -= W; R(f.x, f.y, f.r, f.r, '#fff'); }
    } else if (bg === 7) {
      const off = (t % 80) / 2; for (let ty = -1; ty < 8; ty++) for (let tx = -1; tx < 7; tx++) R(tx * 40 + off, ty * 40 + off, 40, 40, (tx + ty) & 1 ? '#ffaad7' : '#aae1ff');
    } else if (bg === 8) c.drawImage($('photoCv'), 0, 0);
    else {
      grad('#2e1065', '#0c4a6e');
      [[24, 52], [210, 70], [40, 230], [200, 214], [120, 40], [18, 150], [224, 150], [90, 250]].forEach(([x, y], i) => { const p = (t + i * 9) % 40, r = p < 20 ? p / 5 : (40 - p) / 5; if (r > 0.5) { R(x - r, y - 0.5, 2 * r, 1.5, '#c8b4ff'); R(x - 0.5, y - r, 1.5, 2 * r, '#c8b4ff'); } });
    }
  }

  // ── the deck ──
  const TX = [8, 8, 160, 8, 86, 9, 54, 99, 144, 189], TY = [26, 104, 104, 174, 174, 238, 238, 238, 238, 238], TW = [224, 146, 72, 72, 146, 42, 42, 42, 42, 42], TH = [72, 64, 64, 58, 58, 22, 22, 22, 22, 22];
  const TMODE = [0, 4, 5, 6, 1, 2, 11, 7, 9, 10];
  const DAYS = ['SUN', 'MON', 'TUE', 'WED', 'THU', 'FRI', 'SAT'], MON = ['JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUN', 'JUL', 'AUG', 'SEP', 'OCT', 'NOV', 'DEC'];
  const SPR = [
    ['K..........K', 'KK........KK', 'KBK......KBK', 'KBBKKKKKKBBK', 'KBBBBBBBBBBK', 'KBBKBBBBKBBK', 'KBBKBBBBKBBK', 'KBPBBKKBBPBK', 'KBBBBBBBBBBK', '.KBBBBBBBBK.', '..KBBBBBBK..', '...KKKKKK...'],
    ['...KKKKKK...', '..KBBBBBBK..', '.KBBBBBBBBK.', 'KBBBBBBBBBBK', 'KBBKBBBBKBBK', 'KBBKBBBBKBBK', 'KBBBBBBBBBBK', 'KBPBBBBBBPBK', 'KBBBBKKBBBBK', 'KBBBBBBBBBBK', 'KBKBBKKBBKBK', 'KK.KK..KK.KK'],
    ['.KKK....KKK.', 'KWWKK..KKWWK', 'KWKWKKKKWKWK', 'KBBBBBBBBBBK', 'KBBBBBBBBBBK', 'KBPBBBBBBPBK', 'KBBKBBBBKBBK', 'KBBBKKKKBBBK', 'KBBBBBBBBBBK', '.KBBBBBBBBK.', 'KBBK....KBBK', 'KKK......KKK']];
  function sprite(ox, oy, sc) {
    const col = val('petCol'), spr = SPR[+$('petSp').value] || SPR[0];
    spr.forEach((row, y) => [...row].forEach((ch, x) => { if (ch !== '.') R(ox + x * sc, oy + y * sc, sc, sc, ch === 'K' ? '#1e1432' : ch === 'W' ? '#fff' : ch === 'P' ? '#f472b6' : col); }));
  }
  function deck() {
    background(ui.bg); dim(0, 0, W, H, .5); topBar('LARP//OS', false);
    T(((($('stepCount') || {}).textContent || '').replace(/[^0-9]/g, '') || '0') + ' STP', 88, 9, 1, '#fff');
    const LOW = mixk(ACC, .2), DIM = mixk(ACC, .45), d = new Date();
    brackets(TX[0], TY[0], TW[0], TH[0], 10, ACC);
    segTime(16, 34, 30, 52, 6, d.getHours(), d.getMinutes(), d.getMilliseconds() < 500, ACC, LOW);
    segDigit(180, 34, 14, 25, 3, d.getSeconds() / 10 | 0, '#fff', LOW); segDigit(198, 34, 14, 25, 3, d.getSeconds() % 10, '#fff', LOW);
    T(DAYS[d.getDay()] + ' ' + pad(d.getDate()), 180, 66, 1, '#fff'); T(MON[d.getMonth()], 180, 77, 1, ACC);
    for (let i = 0; i < 60; i++) R(16 + (i * 208 / 60 | 0), 91, 2, 3, i <= d.getSeconds() ? ACC : LOW);
    // audio
    let x = TX[1], y = TY[1]; hudPanel(x, y, TW[1], TH[1], ACC, .5);
    const s = song(), tx = x + 62;
    if (ui.artReady) c.drawImage($('artCv'), 0, 20, 240, 240, x + 8, y + 10, 48, 48);
    else for (let j = 0; j < 48; j += 4) for (let i = 0; i < 48; i += 4) R(x + 8 + i, y + 10 + j, 4, 4, Math.random() < .25 ? DIM : '#000');
    c.strokeStyle = DIM; c.strokeRect(x + 7.5, y + 9.5, 49, 49);
    if (s.title) { T(marquee(s.title, 13, 8), tx, y + 10, 1, '#fff'); T(marquee(s.artist, 13, 11), tx, y + 22, 1, ACC); }
    else { T('NO SIGNAL', tx, y + 10, 1, '#fff'); T('play a song', tx, y + 22, 1, DIM); }
    for (let i = 0; i < 13; i++) { const h = 2 + (eq(i) * 14 | 0); R(tx + i * 6, y + 50 - h, 4, h, ACC); }
    R(tx, y + 55, 78, 1, LOW); if (s.title) R(tx, y + 54, 30 + (frame / 8) % 48, 3, '#fff');
    tag('AUDIO', x + 10, y - 4);
    // daemon
    x = TX[2]; y = TY[2]; hudPanel(x, y, TW[2], TH[2], ACC, .5);
    sprite(x + 18 + Math.round(tiltX * 8), y + 16 - (Math.abs(Math.sin(frame * 0.18)) * 4 | 0), 3);
    for (let i = 0; i < 8; i++) R(x + 8 + i * 7, y + 55, 5, 4, i < 6 ? ACC : LOW);
    tag('DAEMON', x + 10, y - 4);
    // rng
    x = TX[3]; y = TY[3]; hudPanel(x, y, TW[3], TH[3], ACC, .5);
    const gx = x + 36, gy = y + 32; circle(gx, gy, 19, DIM); R(gx - 23, gy, 47, 1, LOW); R(gx, gy - 23, 1, 47, LOW);
    const px = gx + Math.round(tiltX * 18), py = gy + Math.round(tiltY * 18); R(px - 2, py - 2, 5, 5, '#fff'); c.strokeStyle = ACC; c.strokeRect(px - 3.5, py - 3.5, 8, 8);
    tag('RNG', x + 10, y - 4);
    // msg
    x = TX[4]; y = TY[4]; hudPanel(x, y, TW[4], TH[4], ACC, .5);
    T(marquee((val('txt') || ' ').replace(/\n/g, ' '), 11, 5), x + 8, y + 16, 2, '#fff'); T(frame % 48 < 24 ? '> _' : '>', x + 8, y + 41, 1, ACC);
    tag('MSG', x + 10, y - 4);
    ['IMG', 'LUX', 'ID', 'EYE', 'JBX'].forEach((l, i) => { const k = 5 + i; hudPanel(TX[k], TY[k], TW[k], TH[k], ACC, .75); T(l, TX[k] + (TW[k] - l.length * 12) / 2, TY[k] + 4, 2, ACC); });
  }

  // ── full-screen blocks ──
  const HN = ['twelve', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten', 'eleven'];
  const bounce = { x: 40, y: 60, vx: 1.6, vy: 1.2, col: '#fff' };
  function clock() {
    background(ui.bg); topBar('//CHRONO', false);
    const d = new Date(), date = DAYS[d.getDay()] + ' ' + pad(d.getDate()) + ' ' + MON[d.getMonth()];
    if (ui.face === 1) {
      circle(120, 140, 98, ACC); circle(120, 140, 97, ACC);
      for (let i = 0; i < 12; i++) { const a = i * Math.PI / 6, r0 = i % 3 ? 88 : 82; line(120 + Math.sin(a) * r0, 140 - Math.cos(a) * r0, 120 + Math.sin(a) * 95, 140 - Math.cos(a) * 95, '#fff'); }
      const hand = (a, len, col, w) => { c.strokeStyle = col; c.lineWidth = w; c.beginPath(); c.moveTo(120, 140); c.lineTo(120 + Math.sin(a) * len, 140 - Math.cos(a) * len); c.stroke(); };
      hand((d.getHours() % 12 + d.getMinutes() / 60) * Math.PI / 6, 50, '#fff', 5); hand((d.getMinutes() + d.getSeconds() / 60) * Math.PI / 30, 76, ACC, 3); hand(d.getSeconds() * Math.PI / 30, 86, '#fff', 1);
      R(116, 136, 9, 9, ACC); TC(date, 248, 1, '#fff');
    } else if (ui.face === 2) {
      let m5 = Math.round(d.getMinutes() / 5) * 5, h = d.getHours(); const to = m5 > 30; if (to) h++; if (m5 === 60) m5 = 0;
      const k = to && m5 ? 60 - m5 : m5, pre = { 5: 'five', 10: 'ten', 15: 'quarter', 20: 'twenty', 25: 'twenty five', 30: 'half' }[k];
      if (!k) { TC(HN[h % 12], 108, 4, '#fff'); TC("o'clock", 152, 3, ACC); } else { TC(pre, 90, 3, ACC); TC(to ? 'to' : 'past', 126, 3, '#fff'); TC(HN[h % 12], 162, 4, '#fff'); }
    } else if (ui.face === 3) {
      const b = bounce; b.x += b.vx; b.y += b.vy; let hit = false;
      if (b.x < 0 || b.x > W - 154) { b.vx = -b.vx; hit = true; } if (b.y < 26 || b.y > H - 60) { b.vy = -b.vy; hit = true; }
      if (hit) b.col = `hsl(${Math.random() * 360 | 0} 100% 65%)`;
      T(pad(d.getHours()) + ':' + pad(d.getMinutes()), b.x, b.y, 5, b.col);
    } else {
      dim(8, 74, 224, 102, .5); brackets(8, 74, 224, 102, 12, ACC);
      segTime(16, 86, 42, 78, 8, d.getHours(), d.getMinutes(), d.getMilliseconds() < 500, '#fff', mixk(ACC, .16));
      TC(date, 188, 2, ACC); for (let i = 0; i < 60; i++) R(20 + (i * 200 / 60 | 0), 214, 2, 6, i <= d.getSeconds() ? '#fff' : mixk(ACC, .45));
    }
  }
  function layout(s) {                         // same wrapping as the charm: biggest size that fits
    for (let sz = 6; sz >= 1; sz--) {
      const max = 220 / (6 * sz) | 0, lines = [];
      for (const para of s.split('\n')) { let ln = ''; for (let w of para.split(' ')) { while (w.length > max) { if (ln) { lines.push(ln); ln = ''; } lines.push(w.slice(0, max)); w = w.slice(max); } if (!ln) ln = w; else if (ln.length + 1 + w.length <= max) ln += ' ' + w; else { lines.push(ln); ln = w; } } lines.push(ln); }
      if (lines.length * (8 * sz + 4) <= 200 || sz === 1) return { sz, lines };
    }
  }
  let typeAt = 0, lastStyle = -1;
  function text() {
    const fg = val('txtFg'), bg = val('txtBg'), style = +$('txtStyle').value, s = (val('txt') || ' ').slice(0, 300);
    if (style !== lastStyle) { lastStyle = style; typeAt = frame; }
    if (style === 3) {
      R(0, 0, W, H, '#000'); topBar('//MSG', true);
      const { sz, lines } = layout(s), lh = 8 * sz + 4; let y = (H - lines.length * lh) / 2 + 4, shown = (frame - typeAt) / 2 | 0, count = 0, cx = 10, cy = y;
      for (const ln of lines) { if (count >= shown) break; const part = ln.slice(0, shown - count); T(part, 10, y, sz, fg); cx = 10 + part.length * 6 * sz; cy = y; count += ln.length + 1; y += lh; }
      if (frame % 36 < 18) R(cx, cy, 5 * sz, 7 * sz, fg);
      if (shown > count + 90) typeAt = frame;
      return;
    }
    R(0, 0, W, H, bg);
    if (style === 1) { const one = s.replace(/\n/g, ' '), w = one.length * 36; T(one, W - (frame * 5) % (w + W), 116, 6, fg); return; }
    const { sz, lines } = layout(s), lh = 8 * sz + 4; let y = (H - lines.length * lh) / 2 + 4;
    lines.forEach((ln, li) => {
      const x = (W - ln.length * 6 * sz) / 2;
      if (style === 2) [...ln].forEach((ch, i) => T(ch, x + i * 6 * sz, y, sz, `hsl(${(i * 25 + li * 40 + frame * 6) % 360} 100% 60%)`));
      else T(ln, x, y, sz, fg);
      y += lh;
    });
  }
  function empty(label, big, l1) { R(0, 0, W, H, VOID); topBar(label, true); brackets(34, 84, 172, 100, 14, ACC); TC(big, 106, 2, ACC); TC(l1, 138, 1, '#fff'); TC('from the app', 152, 1, '#fff'); }
  const photo = () => $('sendPhoto').disabled && !ui.photoShown ? empty('//IMG', 'NO IMAGE', 'send a photo') : c.drawImage($('photoCv'), 0, 0);
  function music() {
    const s = song();
    if (ui.artReady) c.drawImage($('artCv'), 0, 0);
    else { R(0, 0, W, H, VOID); const beat = ui.playing ? Math.sin(frame * 0.35) * 3 + 3 : 1; for (let i = 0; i < 6; i++) circle(120, 108, 20 + i * 14 + beat, mixk(ACC, .8 - i * .12)); const a = frame * (ui.playing ? 0.08 : 0); line(120, 108, 120 + Math.cos(a) * 96, 108 + Math.sin(a) * 96, ACC); R(116, 104, 9, 9, ACC); }
    const g = c.createLinearGradient(0, 140, 0, H); g.addColorStop(0, 'rgba(0,0,0,0)'); g.addColorStop(1, 'rgba(0,0,0,.92)'); c.fillStyle = g; c.fillRect(0, 140, W, 140);
    topBar(ui.playing ? '//AUDIO' : '//PAUSED', true);
    const title = s.title || 'NO SIGNAL', tw = title.length * 12;
    c.save(); c.beginPath(); c.rect(0, 180, W, 30); c.clip();
    if (tw <= 212) T(title, 14, 190, 2, '#fff'); else { const o = (frame * 2) % (tw + 60); T(title, 14 - o, 190, 2, '#fff'); T(title, 14 - o + tw + 60, 190, 2, '#fff'); }
    c.restore();
    T((s.artist || (s.title ? '' : 'play something on your phone')).slice(0, 35), 14, 212, 1, ACC);
    const p = s.title ? ((frame / 900) % 1) : 0, w = Math.max(2, 212 * p | 0);
    R(14, 228, 212, 2, '#46465a'); R(14, 227, w, 4, ACC); R(12 + w, 224, 3, 10, '#fff');
    T('0:' + pad(p * 59 | 0), 14, 240, 1, '#fff'); T(val('mLen') || '', 226 - (val('mLen') || '').length * 6, 240, 1, '#fff');
    for (let i = 0; i < 20; i++) { const h = 2 + (eq(i) * 16 | 0); R(60 + i * 6, 258 - h, 4, h, ACC); }
  }
  function floor(hz, col) { c.strokeStyle = col; c.lineWidth = 1; c.beginPath(); for (let i = 0; i < 9; i++) { const y = hz + (i / 9) ** 2 * (H - hz); c.moveTo(0, y); c.lineTo(W, y); } for (let i = -8; i <= 8; i++) { c.moveTo(120 + i * 14, hz); c.lineTo(120 + i * 90, H); } c.stroke(); }
  function pet() {
    grad(VOID, mixk(val('petCol'), .2)); floor(204, mixk(ACC, .2)); topBar('//DAEMON', true);
    TC((val('petName') || 'pet').slice(0, 12), 34, 3, '#fff');
    const b = Math.abs(Math.sin(frame * 0.18)) * 14 | 0, lean = Math.round(tiltX * 24);
    c.fillStyle = mixk(ACC, .2); c.beginPath(); c.ellipse(120 + lean, 200, 46 - b, 6, 0, 0, 7); c.fill();
    sprite(60 + lean, 74 - b, 10);
    for (let i = 0; i < 10; i++) R(51 + i * 14, 226, 11, 8, i < 7 ? ACC : mixk(ACC, .2));
    TC('STATE: HAPPY', 242, 1, '#fff');
  }
  function toy() {
    const k = ui.toy; R(0, 0, W, H, VOID); for (let y = 50; y < H; y += 20) for (let x = 10; x < W; x += 20) R(x, y, 1, 1, mixk(ACC, .45));
    if (k === 0) { circle(120, 160, 92, '#000', true); circle(120, 160, 92, ACC); circle(120, 160, 86, mixk(ACC, .2)); circle(120, 164, 60, mixk(ACC, .22), true); circle(120, 164, 60, ACC); TC('shake', 146, 2, '#fff'); TC('me', 166, 2, '#fff'); }
    else if (k === 1) {
      [[26, [0, 2, 4, 6, 8]], [126, [0, 8]]].forEach(([x, pips]) => { c.strokeStyle = ACC; c.strokeRect(x + 5.5, 105.5, 88, 88); R(x, 100, 88, 88, '#fff'); pips.forEach(i => R(x + 22 * (1 + i % 3) - 8, 100 + 22 * (1 + (i / 3 | 0)) - 8, 16, 16, '#000')); });
      TC('= 7', 208, 4, ACC);
    } else { circle(120, 150, 72, '#be8214', true); c.fillStyle = '#ffc83c'; c.beginPath(); c.ellipse(120, 150, 66 * Math.abs(Math.cos(frame * 0.03)), 66, 0, 0, 7); c.fill(); TC('shake to flip', 234, 2, '#fff'); }
    topBar('//RNG', true); TC(['< ORACLE >', '< DICE >', '< COIN >'][k], 30, 2, ACC);
  }
  function card() {
    const a = val('cAcc'), name = (val('cName') || '').slice(0, 20), sub = (val('cSub') || '').slice(0, 24);
    R(0, 0, W, H, VOID); topBar('//ID', true); hudPanel(12, 32, 216, 112, a, 0); R(12, 38, 4, 100, a);
    T('OPERATOR', 26, 42, 1, mixk(a, .65)); T(name, 26, 58, name.length <= 10 ? 3 : name.length <= 16 ? 2 : 1, '#fff'); T(sub, 26, 90, sub.length <= 16 ? 2 : 1, a);
    let h = 2166136261; for (const ch of name) h = Math.imul(h ^ ch.charCodeAt(0), 16777619) >>> 0;
    for (let x = 26, i = 0; x < 214; i++) { h = (Math.imul(h, 1664525) + 1013904223) >>> 0; const w = 1 + ((h >>> 28) & 3); if (!(i & 1)) R(x, 116, Math.min(w, 214 - x), 18, a); x += w; }
    for (let i = 0; i < 3; i++) { const on = i < (frame / 8 | 0) % 4; circle(120, 198, 14 + i * 11, on ? a : mixk(a, .25)); circle(120, 198, 15 + i * 11, on ? a : mixk(a, .25)); }
    R(115, 193, 11, 11, '#fff'); TC('>> TAP PHONE HERE <<', 244, 1, '#fff');
  }
  function ticker() { const s = song(); if (!ui.playing || !s.title) return; dim(44, 260, 152, 13, .75); T('♪', 48, 263, 1, ACC); T(marquee(s.title + (s.artist ? ' - ' + s.artist : ''), 22, 7), 58, 263, 1, '#fff'); }
  // ── the Aero look (mirrors firmware 5.0) ──
  const AERO_ACC = ['#0084FF', '#00A8D6', '#E840A8'], INK = '#08264e';
  const NUN = '"Nunito", "Trebuchet MS", sans-serif';
  const ATITLE = { 0: 'Clock', 1: 'Message', 2: 'Photo', 4: 'Music', 5: 'Pet', 6: 'Dice', 7: 'ID card', 8: 'Larp Charm', 9: 'Eyes', 10: 'Jukebox', 11: 'Torch', 12: 'Power' };
  const bgCache = {};
  function lg(x, y0, y1, stops) { const g = x.createLinearGradient(0, y0, 0, y1); stops.forEach(([p, col]) => g.addColorStop(p, col)); return g; }
  function aeroBg(v) {
    if (bgCache[v]) return bgCache[v];
    const o = document.createElement('canvas'); o.width = W; o.height = H; const x = o.getContext('2d');
    const ribbon = (yc, amp, f, ph, w, a) => { x.save(); x.strokeStyle = `rgba(255,255,255,${a})`; x.lineWidth = w; x.shadowColor = '#fff'; x.shadowBlur = 14; x.beginPath();
      for (let i = 0; i <= W; i += 4) { const yy = yc + amp * Math.sin(i * f + ph); i ? x.lineTo(i, yy) : x.moveTo(i, yy); } x.stroke(); x.restore(); };
    if (v === 1) {
      x.fillStyle = lg(x, 0, H, [[0, '#96e8ff'], [0.5, '#1496dc'], [1, '#023478']]); x.fillRect(0, 0, W, H);
      x.save(); x.globalCompositeOperation = 'lighter';
      for (let i = 0; i < 6; i++) { x.save(); x.translate(30 + i * 42, -10); x.rotate(-0.4); x.fillStyle = lg(x, 0, 260, [[0, 'rgba(255,255,255,.22)'], [1, 'rgba(255,255,255,0)']]); x.fillRect(-6 - i % 3 * 4, 0, 12 + i % 3 * 8, 300); x.restore(); }
      x.restore();
      x.fillStyle = lg(x, 258, H, [[0, '#f0dea9'], [1, '#c8b078']]); x.beginPath(); x.moveTo(0, H);
      for (let i = 0; i <= W; i += 4) x.lineTo(i, 262 + 5 * Math.sin(i * 0.035 + 0.7) + 3 * Math.sin(i * 0.09)); x.lineTo(W, H); x.fill();
    } else if (v === 2) {
      x.fillStyle = lg(x, 0, H, [[0, '#ffd0ee'], [0.5, '#cda2ff'], [1, '#6440c8']]); x.fillRect(0, 0, W, H);
      [[48, 70, 40], [196, 150, 56], [120, 238, 34]].forEach(([bx, by, br]) => { x.fillStyle = 'rgba(255,255,255,.16)'; x.beginPath(); x.arc(bx, by, br, 0, 7); x.fill(); x.strokeStyle = 'rgba(255,255,255,.3)'; x.stroke(); });
      ribbon(136, 24, 0.02, 0.4, 5, 0.45);
    } else {
      x.fillStyle = lg(x, 0, 210, [[0, '#0e5ccc'], [0.55, '#56b2fa'], [1, '#d6f2ff']]); x.fillRect(0, 0, W, H);
      const sun = x.createRadialGradient(198, 30, 0, 198, 30, 90); sun.addColorStop(0, 'rgba(255,252,230,.95)'); sun.addColorStop(0.15, 'rgba(255,250,220,.55)'); sun.addColorStop(1, 'rgba(255,250,220,0)');
      x.fillStyle = sun; x.fillRect(0, 0, W, H);
      ribbon(126, 26, 0.018, 0.6, 6, 0.42); ribbon(150, 18, 0.026, 2.2, 4, 0.26);
      const hill = (base, amp, f, ph, top, bot) => { x.fillStyle = lg(x, base - amp, base + 50, [[0, top], [1, bot]]); x.beginPath(); x.moveTo(0, H);
        for (let i = 0; i <= W; i += 3) x.lineTo(i, base + amp * Math.sin(i * f + ph)); x.lineTo(W, H); x.fill();
        x.strokeStyle = 'rgba(255,255,255,.45)'; x.lineWidth = 2; x.beginPath(); for (let i = 0; i <= W; i += 3) { const yy = base + 1 + amp * Math.sin(i * f + ph); i ? x.lineTo(i, yy) : x.moveTo(i, yy); } x.stroke(); };
      hill(206, 10, 0.021, 0.4, '#80d24a', '#3a962c'); hill(238, 8, 0.03, 2.0, '#60c038', '#1e6e22');
    }
    return (bgCache[v] = o);
  }
  const bubs = Array.from({ length: 12 }, () => ({ x: Math.random() * W, y: Math.random() * H + 20, r: 3 + Math.random() * 9, v: 0.25 + Math.random() * 0.6, ph: Math.random() * 6.28 }));
  function bubble(x, y, r) {
    c.fillStyle = 'rgba(255,255,255,.07)'; c.beginPath(); c.arc(x, y, r, 0, 7); c.fill();
    c.strokeStyle = 'rgba(255,255,255,.6)'; c.lineWidth = 1; c.stroke();
    c.fillStyle = 'rgba(255,255,255,.85)'; c.beginPath(); c.ellipse(x - r * 0.38, y - r * 0.42, r * 0.3, r * 0.2, -0.5, 0, 7); c.fill();
  }
  function aeroBackdrop() {
    c.drawImage(aeroBg(ui.aero), 0, 0);
    const n = ui.aero === 1 ? 12 : 7;
    for (let i = 0; i < n; i++) { const b = bubs[i]; b.y -= b.v * (ui.aero === 1 ? 1.4 : 1); b.x += Math.sin(frame * 0.03 + b.ph) * 0.35 + tiltX * 0.3; if (b.y < -b.r - 4) { b.y = H + b.r + Math.random() * 60; b.x = 10 + Math.random() * 220; } bubble(b.x, b.y, b.r); }
  }
  const shade = (h, k) => { const [r, g, b] = rgb(h); return k > 0 ? `rgb(${r + (255 - r) * k | 0},${g + (255 - g) * k | 0},${b + (255 - b) * k | 0})` : `rgb(${r * (1 + k) | 0},${g * (1 + k) | 0},${b * (1 + k) | 0})`; };
  function orb(x, y, r, col, fade = 1) {
    c.save(); c.globalAlpha = fade;
    const g = c.createRadialGradient(x, y + r * 0.55, r * 0.05, x, y, r);
    g.addColorStop(0, shade(col, 0.55)); g.addColorStop(0.55, col); g.addColorStop(1, shade(col, -0.45));
    c.fillStyle = g; c.beginPath(); c.arc(x, y, r, 0, 7); c.fill();
    const hg = c.createLinearGradient(0, y - r * 0.9, 0, y - r * 0.05); hg.addColorStop(0, 'rgba(255,255,255,.92)'); hg.addColorStop(1, 'rgba(255,255,255,.08)');
    c.fillStyle = hg; c.beginPath(); c.ellipse(x, y - r * 0.47, r * 0.74, r * 0.4, 0, 0, 7); c.fill();
    c.restore();
  }
  function glass(x, y, w, h, r, a0 = 0.42, a1 = 0.16) {
    c.save(); c.beginPath(); c.roundRect(x + 0.5, y + 0.5, w - 1, h - 1, r);
    const g = c.createLinearGradient(0, y, 0, y + h); g.addColorStop(0, `rgba(255,255,255,${a0 + 0.16})`); g.addColorStop(0.479, `rgba(255,255,255,${(a0 + a1) / 2 + 0.16})`);
    g.addColorStop(0.48, `rgba(255,255,255,${(a0 + a1) / 2})`); g.addColorStop(1, `rgba(255,255,255,${a1})`);
    c.fillStyle = g; c.fill(); c.strokeStyle = 'rgba(0,40,90,.3)'; c.lineWidth = 1; c.stroke();
    c.beginPath(); c.roundRect(x + 1.5, y + 1.5, w - 3, h - 3, Math.max(1, r - 1)); c.strokeStyle = 'rgba(255,255,255,.75)'; c.stroke(); c.restore();
  }
  function NT(s, x, y, px, col, align = 'left', shadow = false, wgt = 800) {
    c.font = `${wgt} ${px}px ${NUN}`; c.textAlign = align; c.textBaseline = 'alphabetic';
    if (shadow) { c.fillStyle = 'rgba(0,30,70,.45)'; c.fillText(s, x + 1, y + 2); }
    c.fillStyle = col; c.fillText(s, x, y);
  }
  const NW = (s, px, wgt = 800) => { c.font = `${wgt} ${px}px ${NUN}`; return c.measureText(s).width; };
  function aeroTop(showTime) {
    const title = ATITLE[shownMode] || 'Larp Charm', tw = NW(title, 14, 700);
    glass(26, 5, tw + 20, 20, 10, 0.62, 0.36); NT(title, 36, 20, 14, INK, 'left', false, 700);
    const b = (ui.bat || 76) + '%', bw = NW(b, 14, 700) + (ui.charging ? 28 : 20), bx = 214 - bw;
    glass(bx, 5, bw, 20, 10, 0.62, 0.36); NT(b, bx + (ui.charging ? 18 : 10), 20, 14, INK, 'left', false, 700);
    orb(bx - 9, 15, 4, document.body.classList.contains('connected') ? '#28c83c' : '#9696a0');
    if (showTime) { const d = new Date(); NT(d.getHours() + ':' + pad(d.getMinutes()), (26 + tw + 20 + bx - 14) / 2, 20, 14, '#fff', 'center', true, 700); }
  }

  // orbit home
  const ORB = [[0, 'Clock', '#008cff'], [4, 'Music', '#ff46a0'], [9, 'Eyes', '#145aeb'], [10, 'Jukebox', '#8c46ff'], [5, 'Pet', '#ff8c1e'], [6, 'Dice', '#28be46'],
    [1, 'Message', '#00b9be'], [2, 'Photo', '#ffc400'], [7, 'ID card', '#505adc'], [11, 'Torch', '#aabed2']];
  const ON = ORB.length, STEP = Math.PI * 2 / ON, OCX = 120, OCY = 184, ORX = 90, ORY = 26;
  const orbit = { a: 0, v: 0, goal: null, drag: false, front: 0, pos: [] };
  function orbitFocus(i, instant) { let t = -i * STEP; t += Math.round((orbit.a - t) / (Math.PI * 2)) * Math.PI * 2; orbit.v = 0; if (instant) { orbit.a = t; orbit.goal = null; } else orbit.goal = t; }
  function glyph(m, x, y, r) {
    const s = r * 10 / 26; c.save(); c.fillStyle = c.strokeStyle = '#fff'; c.lineWidth = Math.max(1.5, s / 5); c.lineCap = 'round';
    const P = (f) => { c.beginPath(); f(); };
    if (m === 0) { P(() => c.arc(x, y, s, 0, 7)); c.stroke(); P(() => { c.moveTo(x, y); c.lineTo(x, y - s * 0.7); c.moveTo(x, y); c.lineTo(x + s * 0.5, y); }); c.stroke(); }
    else if (m === 4 || m === 10) { P(() => c.arc(x - s * 0.35, y + s * 0.55, s * 0.38, 0, 7)); c.fill(); c.fillRect(x - s * 0.05, y - s * 0.8, s * 0.2, s * 1.35); if (m === 10) { P(() => c.arc(x + s * 0.55, y + s * 0.35, s * 0.3, 0, 7)); c.fill(); c.fillRect(x + s * 0.75, y - s, s * 0.18, s * 1.3); c.fillRect(x - s * 0.05, y - s, s * 0.95, s * 0.22); } }
    else if (m === 9) { c.beginPath(); c.roundRect(x - s * 0.8, y - s * 0.6, s * 0.6, s * 1.2, s * 0.25); c.roundRect(x + s * 0.2, y - s * 0.6, s * 0.6, s * 1.2, s * 0.25); c.fill(); }
    else if (m === 5) { P(() => c.arc(x, y + s * 0.15, s * 0.7, 0, 7)); c.fill(); P(() => { c.moveTo(x - s * 0.7, y - s * 0.1); c.lineTo(x - s * 0.6, y - s); c.lineTo(x - s * 0.15, y - s * 0.5); c.moveTo(x + s * 0.7, y - s * 0.1); c.lineTo(x + s * 0.6, y - s); c.lineTo(x + s * 0.15, y - s * 0.5); }); c.fill(); }
    else if (m === 6) { c.beginPath(); c.roundRect(x - s * 0.8, y - s * 0.8, s * 1.6, s * 1.6, s * 0.3); c.fill(); }
    else if (m === 1) { c.beginPath(); c.roundRect(x - s, y - s * 0.7, s * 2, s * 1.2, s * 0.3); c.fill(); P(() => { c.moveTo(x - s * 0.5, y + s * 0.4); c.lineTo(x - s * 0.5, y + s); c.lineTo(x, y + s * 0.4); }); c.fill(); }
    else if (m === 2) { c.strokeRect(x - s, y - s * 0.7, s * 2, s * 1.5); P(() => { c.moveTo(x - s * 0.8, y + s * 0.6); c.lineTo(x - s * 0.2, y - s * 0.1); c.lineTo(x + s * 0.4, y + s * 0.6); }); c.fill(); }
    else if (m === 3) { P(() => { c.moveTo(x - s * 0.5, y - s * 0.8); c.lineTo(x - s * 0.5, y + s * 0.8); c.lineTo(x + s * 0.9, y); }); c.fill(); }
    else if (m === 7) { c.beginPath(); c.roundRect(x - s, y - s * 0.7, s * 2, s * 1.4, s * 0.2); c.fill(); }
    else { P(() => c.arc(x, y - s * 0.2, s * 0.6, 0, 7)); c.fill(); c.fillRect(x - s * 0.3, y + s * 0.3, s * 0.6, s * 0.5); }
    c.restore();
  }
  function aeroHome() {
    if (!orbit.drag) {
      if (orbit.goal != null) { orbit.a += (orbit.goal - orbit.a) * 0.24; if (Math.abs(orbit.goal - orbit.a) < 0.003) { orbit.a = orbit.goal; orbit.goal = null; } }
      else { orbit.a += orbit.v; orbit.v *= 0.9; if (Math.abs(orbit.v) < 0.006) { const t = Math.round(orbit.a / STEP) * STEP; orbit.a += (t - orbit.a) * 0.2; } }
    }
    orbit.front = ((Math.round(-orbit.a / STEP) % ON) + ON) % ON;
    aeroBackdrop(); aeroTop(false);
    glass(16, 32, 208, 92, 22);
    const d = new Date(); NT(d.getHours() + ':' + pad(d.getMinutes()), 120, 94, 64, '#fff', 'center', true, 900);
    NT(['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'][d.getDay()] + ' ' + d.getDate() + ' ' + MON[d.getMonth()].charAt(0) + MON[d.getMonth()].slice(1).toLowerCase(), 32, 115, 14, INK, 'left', false, 700);
    NT((($('stepCount') || {}).textContent || '0').replace('–', '0') + ' steps', 208, 115, 14, INK, 'right', false, 700);
    c.strokeStyle = 'rgba(255,255,255,.5)'; c.lineWidth = 1; c.beginPath(); c.ellipse(OCX, OCY, ORX, ORY, 0, 0, 7); c.stroke();
    const items = ORB.map((o, i) => { const th = orbit.a + i * STEP, dd = (Math.cos(th) + 1) / 2; return { i, x: OCX + Math.sin(th) * ORX, y: OCY + Math.cos(th) * ORY, r: 9 + 19 * Math.pow(dd, 1.6), d: dd }; }).sort((a, b) => a.d - b.d);
    orbit.pos = items;
    for (const it of items) {
      if (it.d > 0.45) { c.fillStyle = `rgba(0,30,60,${0.27 * it.d})`; c.beginPath(); c.ellipse(it.x, it.y + it.r + 2, it.r * 0.9, it.r * 0.22, 0, 0, 7); c.fill(); }
      orb(it.x, it.y, it.r, ORB[it.i][2], 0.5 + 0.5 * it.d);
      if (it.r >= 13) glyph(ORB[it.i][0], it.x, it.y - it.r / 12, it.r);
    }
    const nm = ORB[orbit.front][1], w = NW(nm, 19) + 34;
    glass(120 - w / 2, 246, w, 28, 14, 0.7, 0.42); NT(nm, 120, 266, 19, INK, 'center');
    const ph = (frame / 3 | 0) % 12, nu = ph < 6 ? ph / 2 : (12 - ph) / 2;
    NT('<', 120 - w / 2 - 16 - nu, 266, 19, '#fff', 'left', true); NT('>', 120 + w / 2 + 6 + nu, 266, 19, '#fff', 'left', true);
  }
  function aeroClock() {
    aeroBackdrop(); aeroTop(false);
    const d = new Date(), hm = d.getHours() + ':' + pad(d.getMinutes());
    if (ui.face === 1) {
      orb(120, 150, 96, shade(ACC, 0.35));
      c.strokeStyle = '#fff'; c.lineWidth = 2; for (let i = 0; i < 12; i++) { const a = i * Math.PI / 6, r0 = i % 3 ? 80 : 74; c.beginPath(); c.moveTo(120 + Math.sin(a) * r0, 150 - Math.cos(a) * r0); c.lineTo(120 + Math.sin(a) * 86, 150 - Math.cos(a) * 86); c.stroke(); }
      const hand = (a, l, col, w) => { c.strokeStyle = col; c.lineWidth = w; c.lineCap = 'round'; c.beginPath(); c.moveTo(120, 150); c.lineTo(120 + Math.sin(a) * l, 150 - Math.cos(a) * l); c.stroke(); };
      hand((d.getHours() % 12 + d.getMinutes() / 60) * Math.PI / 6, 46, INK, 5); hand((d.getMinutes() + d.getSeconds() / 60) * Math.PI / 30, 68, INK, 3); hand(d.getSeconds() * Math.PI / 30, 76, '#e6285a', 1.2);
      orb(120, 150, 7, '#e6285a'); return;
    }
    glass(14, 66, 212, 136, 26); NT(hm, 120, 134, 64, '#fff', 'center', true, 900);
    NT(['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'][d.getDay()] + ' ' + d.getDate() + ' ' + MON[d.getMonth()].charAt(0) + MON[d.getMonth()].slice(1).toLowerCase(), 120, 164, 19, INK, 'center');
    glass(40, 176, 160, 12, 6, 0.3, 0.12); c.fillStyle = ACC; c.beginPath(); c.roundRect(42, 178, Math.max(8, 156 * d.getSeconds() / 59), 8, 4); c.fill();
    const s = song(); if (ui.playing && s.title) { const t = s.title + (s.artist ? ' - ' + s.artist : ''), w = Math.min(190, NW(t, 14, 700)) + 30; glass(120 - w / 2, 250, w, 22, 11, 0.6, 0.35); orb(120 - w / 2 + 12, 261, 4, ACC); NT(t, 120 - w / 2 + 20, 266, 14, INK, 'left', false, 700); }
  }
  function aeroMusic() {
    const s = song();
    if (ui.artReady) c.drawImage($('artCv'), 0, 0); else { aeroBackdrop(); orb(120, 106, 56 + (ui.playing ? Math.sin(frame * 0.3) * 3 : 0), ACC); glyph(4, 120, 106, 56); }
    aeroTop(true);
    const g = c.createLinearGradient(0, 168, 0, H); g.addColorStop(0, 'rgba(236,246,255,.6)'); g.addColorStop(1, 'rgba(236,246,255,.92)'); c.fillStyle = g; c.fillRect(0, 168, W, 112);
    R(0, 168, W, 1, '#fff');
    NT(s.title || 'Nothing playing', 18, 192, 19, INK); NT(s.artist || (s.title ? '' : 'Play something on your phone'), 18, 210, 14, shade(ACC, -0.25), 'left', false, 700);
    glass(18, 216, 204, 10, 5, 0.3, 0.12); c.fillStyle = ACC; c.beginPath(); c.roundRect(20, 218, s.title ? 50 + (frame / 8) % 150 : 6, 6, 3); c.fill();
    orb(84, 254, 13, '#788ca0'); orb(120, 254, 17, ACC); orb(156, 254, 13, '#788ca0');
    c.fillStyle = '#fff'; if (ui.playing) { c.fillRect(113, 246, 5, 16); c.fillRect(122, 246, 5, 16); } else { c.beginPath(); c.moveTo(115, 245); c.lineTo(115, 263); c.lineTo(129, 254); c.fill(); }
    c.beginPath(); c.moveTo(88, 248); c.lineTo(88, 260); c.lineTo(79, 254); c.fill(); c.fillRect(78, 248, 2, 13);
    c.beginPath(); c.moveTo(152, 248); c.lineTo(152, 260); c.lineTo(161, 254); c.fill(); c.fillRect(161, 248, 2, 13);
  }
  function aeroPet() {
    aeroBackdrop(); aeroTop(true);
    NT((val('petName') || 'pet').slice(0, 12), 120, 62, 28, '#fff', 'center', true);
    const b = Math.abs(Math.sin(frame * 0.18)) * 14, lean = Math.round(tiltX * 24);
    c.fillStyle = 'rgba(0,30,60,.3)'; c.beginPath(); c.ellipse(120 + lean, 200, 46 - b, 6, 0, 0, 7); c.fill();
    sprite(60 + lean, 74 - b, 10);
    glass(50, 212, 140, 14, 7, 0.3, 0.12); c.fillStyle = '#3cc846'; c.beginPath(); c.roundRect(52, 214, 96, 10, 5); c.fill();
    NT('Happy', 120, 248, 14, '#fff', 'center', true, 700);
  }
  function aeroToy() {
    aeroBackdrop();
    const k = ui.toy;
    if (k === 0) { orb(120, 160, 90, '#28225a'); orb(120, 168, 50, shade(ACC, -0.45)); NT('shake', 120, 166, 19, '#fff', 'center', true); NT('me', 120, 188, 19, '#fff', 'center', true); }
    else if (k === 1) { [[26, [0, 2, 4, 6, 8]], [126, [0, 8]]].forEach(([x, pips]) => { c.fillStyle = '#fff'; c.beginPath(); c.roundRect(x, 100, 88, 88, 14); c.fill(); c.fillStyle = INK; pips.forEach(i => { c.beginPath(); c.arc(x + 22 * (1 + i % 3), 100 + 22 * (1 + (i / 3 | 0)), 8, 0, 7); c.fill(); }); }); NT('= 7', 120, 240, 28, '#fff', 'center', true); }
    else { orb(120, 150, 70, '#ffc83c'); NT('?', 120, 172, 64, '#be8214', 'center', false, 900); }
    aeroTop(true);
    const t = ['Oracle', 'Dice', 'Coin'][k], w = NW(t, 19) + 30; glass(120 - w / 2, 32, w, 26, 13, 0.6, 0.35); NT(t, 120, 51, 19, INK, 'center');
  }
  function aeroCard() {
    const a = val('cAcc'), name = (val('cName') || '').slice(0, 20), sub = (val('cSub') || '').slice(0, 24);
    aeroBackdrop(); aeroTop(true); glass(14, 38, 212, 120, 20, 0.6, 0.34);
    NT(name, 30, 82, name.length > 12 ? 19 : 28, INK); NT(sub, 30, 108, 19, shade(a, -0.3));
    let h = 2166136261; for (const ch of name) h = Math.imul(h ^ ch.charCodeAt(0), 16777619) >>> 0;
    c.fillStyle = INK; for (let x = 30, i = 0; x < 210; i++) { h = (Math.imul(h, 1664525) + 1013904223) >>> 0; const w = 1 + ((h >>> 28) & 3); if (!(i & 1)) c.fillRect(x, 122, Math.min(w, 210 - x), 22); x += w; }
    const p = (frame % 40) / 40; for (let i = 0; i < 3; i++) { c.strokeStyle = `rgba(255,255,255,${0.8 * (1 - (i + p) / 3)})`; c.beginPath(); c.arc(120, 206, 16 + (i + p) * 14, 0, 7); c.stroke(); }
    orb(120, 206, 14, a); NT('Tap your phone here', 120, 266, 14, '#fff', 'center', true, 700);
  }
  function aeroCharge() {
    aeroBackdrop(); aeroTop(true);
    const p = ui.bat || 76; NT(p + '%', 120, 100, 64, '#fff', 'center', true, 900);
    glass(26, 124, 178, 70, 24, 0.34, 0.14); glass(206, 146, 12, 26, 6, 0.55, 0.3);
    c.save(); c.beginPath(); c.roundRect(32, 130, 166, 58, 18); c.clip();
    c.fillStyle = lg(c, 130, 188, [[0, '#9be58a'], [1, '#2e9640']]); c.beginPath(); c.moveTo(32, 130);
    const edge = 32 + 166 * p / 100; for (let y = 130; y <= 188; y += 3) c.lineTo(edge + Math.sin(y * 0.18 + frame * 0.15) * 3, y); c.lineTo(32, 188); c.fill(); c.restore();
    NT('Charging', 120, 232, 28, '#fff', 'center', true); NT('4.05 volts', 120, 254, 14, INK, 'center', false, 700);
  }
  function aeroJuke() {
    aeroBackdrop(); aeroTop(true);
    const playing = ui.jbSong >= 0 || !!window.previewNotes, idx = ui.jbSong >= 0 ? ui.jbSong : (window.previewNotes ? window.previewNotes.song : 0);
    c.save(); c.translate(120, 92); c.rotate(playing ? frame * 0.09 : 0);
    const cg = c.createConicGradient ? c.createConicGradient(0, 0, 0) : null;
    if (cg) { ['#e6e6f0', '#ffc8f0', '#c8e6ff', '#fff6c8', '#e6e6f0', '#d2c8ff', '#c8fff0', '#e6e6f0'].forEach((col, i, a) => cg.addColorStop(i / (a.length - 1), col)); c.fillStyle = cg; } else c.fillStyle = '#ddd';
    c.beginPath(); c.arc(0, 0, 56, 0, 7); c.fill(); c.strokeStyle = 'rgba(0,0,0,.08)'; for (let r = 26; r < 56; r += 3) { c.beginPath(); c.arc(0, 0, r, 0, 7); c.stroke(); } c.restore();
    orb(120, 92, 20, ACC); c.fillStyle = '#1e283c'; c.beginPath(); c.arc(120, 92, 5, 0, 7); c.fill();
    const sg = (window.SONGS || [])[idx], nm = sg ? sg.name : 'My song', cn = sg ? (window.SONG_CATS || [])[sg.cat] : 'My songs';
    glass(16, 152, 208, 42, 18, 0.72, 0.45); NT(cn, 120, 168, 14, shade(ACC, -0.3), 'center', false, 700); NT(nm, 120, 188, 19, INK, 'center'); NT('<', 22, 182, 19, ACC, 'left', true); NT('>', 208, 182, 19, ACC);
    glass(20, 208, 200, 58, 18, 0.5, 0.28);
    if (ui.jbQueue) { NT('Up next', 34, 226, 14, shade(ACC, -0.3), 'left', false, 700); NT(ui.jbQueue + ' more ' + (ui.jbQueue > 1 ? 'songs' : 'song'), 34, 243, 14, INK, 'left', false, 700); }
    else { NT(playing ? 'Tap here to queue the' : 'Tap the disc to play,', 120, 232, 14, INK, 'center', false, 700); NT(playing ? 'song shown above' : 'or here to queue it', 120, 250, 14, INK, 'center', false, 700); }
  }

  // ── eyes ──
  const NEU = { w: 62, h: 74, rad: 18, lidT: 0, slope: 0, lidB: 0 };
  const EX = [{}, { lidB: 0.5 }, { lidB: 0.62, h: 66 }, { lidT: 0.3, slope: 0.42, h: 66 }, { lidT: 0.36, slope: -0.55, h: 64 }, { w: 70, h: 86, rad: 33 }, { lidT: 0.62, slope: 0.12 },
    { lidT: 0.46, lidB: 0.1 }, { L: { lidT: 0.52 }, R: { lidT: 0.16, h: 70 } }, { L: { lidB: 0.5 }, R: { h: 9, rad: 4 } }, {}, { w: 50, h: 50, rad: 25 }];
  const ey = { L: { ...NEU }, R: { ...NEU }, lx: 0, ly: 0, tx: 0, ty: 0, sac: 0, blink: 0, nextBlink: 0, shown: -1, idle: 0, idleUntil: 0, nextIdle: 0, TL: { ...NEU }, TR: { ...NEU } };
  function eyes() {
    const now = performance.now();
    let want = ui.expr !== 255 && ui.exprAt && now - ui.exprAt < 10000 ? ui.expr : null;
    if (want == null) {
      if (ui.jbSong >= 0 || window.previewNotes) want = (frame / 40 | 0) % 2 ? 1 : 2;
      else { if (now > ey.nextIdle) { ey.idle = [0, 1, 8, 7, 5, 9, 1, 0][Math.random() * 8 | 0]; ey.idleUntil = now + 1400 + Math.random() * 1800; ey.nextIdle = now + 5000 + Math.random() * 6000; } want = now < ey.idleUntil ? ey.idle : 0; }
    }
    if (want !== ey.shown) { ey.shown = want; const e = EX[want] || {}; ey.TL = { ...NEU, ...e, ...(e.L || {}) }; ey.TR = { ...NEU, ...e, ...(e.R || {}) }; delete ey.TL.L; delete ey.TL.R; delete ey.TR.L; delete ey.TR.R; }
    if (now > ey.sac) { if (Math.random() < 0.33) { ey.tx = 0; ey.ty = 0; } else { ey.tx = Math.random() * 52 - 26; ey.ty = Math.random() * 32 - 16; } ey.sac = now + 900 + Math.random() * 2300; }
    if (Math.abs(tryy) > 0.05 || Math.abs(trx) > 0.05) { ey.tx = Math.max(-30, Math.min(30, tryy * 60)); ey.ty = Math.max(-20, Math.min(20, trx * 40)); }
    if (ey.shown === 6) ey.ty = 10;
    ey.lx += (ey.tx - ey.lx) * 0.32; ey.ly += (ey.ty - ey.ly) * 0.32;
    for (const k of Object.keys(NEU)) { ey.L[k] += (ey.TL[k] - ey.L[k]) * 0.28; ey.R[k] += (ey.TR[k] - ey.R[k]) * 0.28; }
    if (now > ey.nextBlink) { ey.blink = now; ey.nextBlink = now + (Math.random() < 0.2 ? 280 : 2200 + Math.random() * 3800); }
    const el = now - ey.blink, bf = el < 90 ? 1 - el / 90 * 0.92 : el < 180 ? 0.08 + (el - 90) / 90 * 0.92 : 1;
    const aero = ui.skin !== 0, bgFill = aero ? lg(c, 0, H, [[0, '#000e28'], [1, '#000']]) : '#000';
    c.fillStyle = bgFill; c.fillRect(0, 0, W, H);
    if (!aero) for (let y = 10; y < H; y += 20) for (let x = 10; x < W; x += 20) R(x, y, 1, 1, '#001a34');
    let cy = 140 + ey.ly; if (ey.shown === 2) cy += Math.sin(now / 1000 * 22) * 3; if (ui.playing) cy += Math.sin(now / 1000 * 7.5) * 3;
    if (ey.shown === 10) {
      const s = 30 * (1 + 0.1 * Math.sin(now / 1000 * 9));
      [70, 170].forEach(x0 => { const x = x0 + ey.lx; c.save(); c.shadowColor = '#ff3c9a'; c.shadowBlur = 16; c.fillStyle = '#ff50a0'; c.beginPath();
        c.moveTo(x, cy + s * 0.9); c.bezierCurveTo(x - s * 1.6, cy - s * 0.1, x - s * 0.8, cy - s * 1.3, x, cy - s * 0.45); c.bezierCurveTo(x + s * 0.8, cy - s * 1.3, x + s * 1.6, cy - s * 0.1, x, cy + s * 0.9); c.fill(); c.restore(); });
      return;
    }
    const t = now / 1000, dz = ey.shown === 11;
    [[ey.L, 70, true, dz ? Math.cos(t * 8) * 12 : 0, dz ? Math.sin(t * 8) * 12 : 0], [ey.R, 170, false, dz ? Math.cos(-t * 8 + 1) * 12 : 0, dz ? Math.sin(-t * 8 + 1) * 12 : 0]].forEach(([e, x0, left, dx, dy]) => {
      const w = e.w * (1 + (left ? -1 : 1) * ey.lx * 0.004), h = Math.max(3, e.h * (e.h > 12 ? bf : 1)), x = x0 + ey.lx + dx, y = cy + dy, r = Math.min(e.rad, w / 2, h / 2);
      c.save(); c.shadowColor = '#0060ff'; c.shadowBlur = 18;
      c.fillStyle = lg(c, y - h / 2, y + h / 2, [[0, '#aaecff'], [1, '#1478ff']]); c.beginPath(); c.roundRect(x - w / 2, y - h / 2, w, h, r); c.fill(); c.restore();
      c.fillStyle = 'rgba(255,255,255,.55)'; c.beginPath(); c.ellipse(x - w * 0.18, y - h * 0.25, w * 0.15, h * 0.1, 0, 0, 7); c.fill();
      c.fillStyle = bgFill;
      if (e.lidT > 0.01 || Math.abs(e.slope) > 0.01) { c.beginPath(); c.moveTo(x - w / 2 - 20, y - h / 2 - 30); for (let i = -1.3; i <= 1.31; i += 0.1) { const u = left ? -i : i; c.lineTo(x + i * w / 2, y + (-1 + 2 * e.lidT + e.slope * u) * h / 2); } c.lineTo(x + w / 2 + 20, y - h / 2 - 30); c.fill(); }
      if (e.lidB > 0.01) { c.beginPath(); c.moveTo(x - w / 2 - 20, y + h / 2 + 30); for (let i = -1.3; i <= 1.31; i += 0.1) c.lineTo(x + i * w / 2, y + h / 2 - e.lidB * h * (1 - 0.85 * i * i)); c.lineTo(x + w / 2 + 20, y + h / 2 + 30); c.fill(); }
    });
  }

  // ── jukebox (deck), torch, charging (deck) ──
  function jukeDeck() {
    R(0, 0, W, H, VOID); topBar('//JUKEBOX', true);
    const playing = ui.jbSong >= 0 ? ui.jbSong : (window.previewNotes ? window.previewNotes.song : -1);
    const cat = +(localStorage.getItem('o_songcat') || 0), cats = window.SONG_CATS || [];
    hudPanel(10, 27, 220, 19, ACC, .5); TC((cats[cat] || '').toUpperCase(), 33, 1, '#fff'); T('<', 18, 33, 1, ACC); T('>', 216, 33, 1, ACC);
    (window.SONGS || []).map((sg, i) => [sg, i]).filter(([sg]) => sg.cat === cat).forEach(([sg, i], k) => { const y = 50 + k * 17; if (i === playing) R(14, y, 212, 15, mixk(ACC, .33)); T(String(k + 1).padStart(2, '0') + ' ' + sg.name.toUpperCase().slice(0, 28), 20, y + 4, 1, i === playing ? '#fff' : '#b9b9c0'); if (i === playing) T('>', 212, y + 4, 1, ACC); });
    const py = 192; hudPanel(10, py, 220, 58, ACC, .5); T(playing >= 0 ? 'E5' : '--', 20, py + 10, 3, playing >= 0 ? '#fff' : mixk(ACC, .45));
    if (playing >= 0) for (let i = 0; i < 20; i++) { const h = 4 + Math.abs(Math.sin(i * 1.7 + frame * 0.2)) * 26; R(100 + i * 6, py + 36 - h, 4, h, ACC); }
    if (playing >= 0) { T('+' + ui.jbQueue + ' QUEUED', 20, py + 48, 1, '#fff'); T('STOP', 132, py + 48, 1, ACC); T('SKIP >', 184, py + 48, 1, ACC); } else T('TAP A SONG TO PLAY OR QUEUE', 20, py + 48, 1, '#fff');
  }
  function torch() { R(0, 0, W, H, ['#ffffff', '#ffce96', '#ff0000', '#78e6ff'][ui.torch || 0]); }
  function chargeDeck() {
    R(0, 0, W, H, VOID); topBar('//POWER', true);
    const p = ui.bat || 76, col = '#34c759', s = String(p), x = (W - (s.length * 42 - 8 + 30)) / 2;
    [...s].forEach((ch, i) => segDigit(x + i * 42, 48, 34, 64, 7, +ch, col, mixk(col, .14))); T('%', x + s.length * 42, 88, 3, col);
    hudPanel(22, 132, 186, 52, col, .5); R(208, 148, 8, 20, col);
    for (let i = 0; i < 10; i++) R(30 + i * 17, 140, 14, 36, i < Math.ceil(p / 10) - ((frame / 12 | 0) % 2 && i === Math.ceil(p / 10) - 1 ? 1 : 0) ? col : mixk(col, .16));
    TC('CHARGING  4.05V', 244, 1, '#fff');
  }
  const scenes = { 0: clock, 1: text, 2: photo, 4: music, 5: pet, 6: toy, 7: card, 8: deck, 9: eyes, 10: jukeDeck, 11: torch, 12: chargeDeck };
  // Quest: frames rendered by the firmware itself (desktop simulator), packed in one sheet.
  // d00..d10 day village, n00..n10 night village, then the screens behind the doors.
  const QSHEET = new Image(); QSHEET.src = 'assets/quest-sheet.png';
  const QMODE = [0, 4, 10, 9, 5, 6, 1, 2, 3, 7, 11];
  const QSCREEN = { 0: 22, 4: 23, 5: 24, 6: 25, 7: 26, 10: 27, 12: 28 };
  // Ronin: t00..t10 the landings (index 29..39), then its screens
  const RSCREEN = { 0: 40, 4: 41, 10: 42, 5: 43, 6: 44, 7: 45, 12: 46 };
  let qloc = 0, qslide = null, rloc = 0, rslide = null;
  function qFrameY(i, dy) { c.imageSmoothingEnabled = false; if (QSHEET.complete && QSHEET.naturalWidth) c.drawImage(QSHEET, (i % 8) * 240, (i / 8 | 0) * 280, 240, 280, 0, dy, W, H); }
  function roninHome() {
    if (rslide) { const p = Math.min(1, (performance.now() - rslide.t) / 420), e = p < .5 ? 2 * p * p : 1 - (-2 * p + 2) ** 2 / 2;
      qFrameY(29 + rslide.from, rslide.dir * H * e); qFrameY(29 + rloc, -rslide.dir * H * (1 - e)); if (p >= 1) rslide = null; }
    else qFrameY(29 + rloc, 0);
  }
  function roninScene(m) { if (m === 8) return roninHome(); if (RSCREEN[m] != null) return qFrameY(RSCREEN[m], 0); (scenes[m] || deck)(); }
  window.roninStep = d => { const to = Math.max(0, Math.min(10, rloc + d)); if (to !== rloc) { rslide = { from: rloc, dir: d, t: performance.now() }; rloc = to; } };
  function qFrame(i, dx = 0) { c.imageSmoothingEnabled = false; if (QSHEET.complete && QSHEET.naturalWidth) c.drawImage(QSHEET, (i % 8) * 240, (i / 8 | 0) * 280, 240, 280, dx, 0, W, H); else R(0, 0, W, H, '#1a2050'); }
  function questHome() {
    const h = new Date().getHours(), night = h >= 20 || h < 6, base = night ? 11 : 0;
    if (qslide) { const p = Math.min(1, (performance.now() - qslide.t) / 380), e = p < .5 ? 2 * p * p : 1 - (-2 * p + 2) ** 2 / 2;
      qFrame(base + qslide.from, -qslide.dir * W * e); qFrame(base + qloc, qslide.dir * W * (1 - e)); if (p >= 1) qslide = null; }
    else qFrame(base + qloc);
  }
  function questScene(m) { if (m === 8) return questHome(); if (QSCREEN[m] != null) return qFrame(QSCREEN[m]); (scenes[m] || deck)(); }
  const aeroScenes = { 0: aeroClock, 4: aeroMusic, 5: aeroPet, 6: aeroToy, 7: aeroCard, 8: aeroHome, 9: eyes, 10: aeroJuke, 11: torch, 12: aeroCharge };

  // ── tap a block to open it, tap again to go back ──
  // Aero: drag sideways to spin the ring
  let drag = null;
  cv.addEventListener('pointerdown', e => { drag = { x0: e.clientX, x: e.clientX, y0: e.clientY, moved: false }; try { cv.setPointerCapture(e.pointerId); } catch (_) {} });
  cv.addEventListener('pointermove', e => {
    if (!drag) return;
    if (Math.abs(e.clientX - drag.x0) > 6 || Math.abs(e.clientY - drag.y0) > 6) drag.moved = true;
    if (drag.moved && ui.skin === 1 && shownMode === 8) { const d = (e.clientX - drag.x) * W / cv.getBoundingClientRect().width * 0.012; orbit.a += d; orbit.v = d; orbit.drag = true; orbit.goal = null; }
    drag.x = e.clientX;
  });
  cv.addEventListener('pointerup', e => {
    const d = drag; drag = null; orbit.drag = false;
    if (!d) return;
    if (!d.moved) return tapAt(e);
    const dy = e.clientY - d.y0;                       // Ronin: swipe up to climb, down to descend
    if (ui.skin === 3 && shownMode === 8 && Math.abs(dy) > 24 && Math.abs(dy) > Math.abs(e.clientX - d.x0)) window.roninStep(dy < 0 ? 1 : -1);
  });
  cv.addEventListener('pointercancel', () => { drag = null; orbit.drag = false; });
  function tapAt(e) {
    const r = cv.getBoundingClientRect(), x = (e.clientX - r.left) * W / r.width, y = (e.clientY - r.top) * H / r.height;
    if (ui.mode !== 8) return window.charmGoto(8);
    if (ui.skin === 3) {
      if (y < 92) return window.roninStep(1);
      if (y > 196) return window.roninStep(-1);
      if (QMODE[rloc] === 3) return;                  // the stage is empty
      return window.charmGoto(QMODE[rloc]);
    }
    if (ui.skin === 2) {
      if (x < 66 || x > 174) { const d = x < 66 ? -1 : 1, from = qloc; qloc = (qloc + d + 11) % 11; qslide = { from, dir: d, t: performance.now() }; return; }
      if (QMODE[qloc] === 3) return;                  // the theatre is closed
      return window.charmGoto(QMODE[qloc]);
    }
    if (ui.skin === 1) {
      for (const it of [...orbit.pos].reverse()) if ((x - it.x) ** 2 + (y - it.y) ** 2 <= (it.r + 5) ** 2) { if (it.i === orbit.front && orbit.goal == null) return window.charmGoto(ORB[it.i][0]); return orbitFocus(it.i, false); }
      if (y < 128) return window.charmGoto(0);
      if (y > 240) return window.charmGoto(ORB[orbit.front][0]);
      return;
    }
    for (let i = 0; i < 10; i++) if (x >= TX[i] - 3 && x < TX[i] + TW[i] + 3 && y >= TY[i] - 3 && y < (i >= 5 ? H : TY[i] + TH[i] + 3)) return window.charmGoto(TMODE[i]);
  }

  let shownMode = ui.mode, zoom = null;       // zoom: { ti, s, opening }
  function tick() {
    frame++;
    rx += (trx - rx) * 0.08; ry += (tryy - ry) * 0.08; tiltX = ry; tiltY = 0.4 + rx * 0.6;
    if (!calm) rig.style.transform = `rotateX(${(-rx * 9).toFixed(2)}deg) rotateY(${(ry * 13).toFixed(2)}deg)`;
    const s = song(), [r, g, b] = rgb(val('mColor') || '#f472b6'), mx = Math.max(r, g, b);
    ACC = ui.playing && s.title && mx >= 40 ? `rgb(${r * 255 / mx | 0},${g * 255 / mx | 0},${b * 255 / mx | 0})` : ui.skin === 1 ? AERO_ACC[ui.aero] : ui.skin === 2 ? '#FAD25A' : ui.skin === 3 ? '#E64638' : THEMES[ui.theme] || THEMES[0];
    if (ACC[0] !== '#') ACC = '#' + ACC.match(/\d+/g).map(v => (+v).toString(16).padStart(2, '0')).join('');

    if (ui.mode !== shownMode && !zoom) {
      const opening = shownMode === 8, ti = TMODE.indexOf(opening ? ui.mode : shownMode);
      if (ui.skin >= 2) { const l = QMODE.indexOf(opening ? ui.mode : shownMode); if (!opening && l >= 0) { if (ui.skin === 2) qloc = l; else rloc = l; } shownMode = ui.mode; }
      else if (ui.skin === 1) {
        const oi = ORB.findIndex(o => o[0] === (opening ? ui.mode : shownMode));
        if (!opening && oi >= 0) orbitFocus(oi, true);
        const it = orbit.pos.find(p => p.i === oi);
        if (oi >= 0 && (opening || ui.mode === 8) && !calm) zoom = { aero: true, x: it ? it.x : OCX, y: it ? it.y : OCY + ORY, r: it ? it.r : 28, col: ORB[oi][2], s: 0, opening, to: ui.mode };
        else shownMode = ui.mode;
      } else if (ti >= 0 && (opening || ui.mode === 8) && !calm) zoom = { ti, s: 0, opening, to: ui.mode }; else { shownMode = ui.mode; glitch = Math.max(glitch, 3); }
    }
    c.save(); c.globalAlpha = 1;
    try {
      if (zoom && zoom.aero) {
        aeroHome(); zoom.s++;
        let t = zoom.s / 7; t = 1 - (1 - t) * (1 - t); if (!zoom.opening) t = 1 - t;
        const rr = zoom.r + (200 - zoom.r) * t, x = zoom.x + (120 - zoom.x) * t, y = zoom.y + (140 - zoom.y) * t;
        c.fillStyle = zoom.col; c.beginPath(); c.arc(x, y, rr, 0, 7); c.fill(); c.strokeStyle = '#fff'; c.lineWidth = 2; c.stroke();
        if (zoom.s >= 7) { shownMode = zoom.to; zoom = null; }
      } else if (zoom) {
        deck(); zoom.s++;
        let t = zoom.s / 7; t = 1 - (1 - t) * (1 - t); if (!zoom.opening) t = 1 - t;
        const i = zoom.ti, x = TX[i] * (1 - t), y = TY[i] * (1 - t), w = TW[i] + (W - TW[i]) * t, h = TH[i] + (H - TH[i]) * t;
        R(x, y, w, h, '#000'); c.strokeStyle = ACC; c.lineWidth = 2; c.strokeRect(x + 1, y + 1, w - 2, h - 2);
        if (zoom.s >= 7) { shownMode = zoom.to; zoom = null; glitch = Math.max(glitch, 3); }
      } else {
        if (ui.skin === 3) roninScene(shownMode);
        else if (ui.skin === 2) questScene(shownMode);
        else if (ui.skin === 1) {
          const f = aeroScenes[shownMode];
          if (f) f(); else { (scenes[shownMode] || deck)(); aeroTop(true); }
        } else {
          (scenes[shownMode] || deck)();
          if ([0, 5, 6, 7].includes(shownMode)) ticker();
        }
        if (ui.skin === 0 && ![2, 3, 4, 11].includes(shownMode) && $('optFx').checked) { c.fillStyle = 'rgba(0,0,0,.22)'; for (let y = 1; y < H; y += 2) c.fillRect(0, y, W, 1); }
      }
    } catch (e) { /* one bad frame must not stop the preview */ }
    c.restore();
    if (glitch > 0 && ui.skin === 0) {
      glitch--;
      for (let k = 0; k < 5; k++) { const y = Math.random() * (H - 16) | 0, h = 2 + Math.random() * 12 | 0, off = (Math.random() * 56 - 28) | 0; c.drawImage(cv, 0, y, W, h, off, y, W, h); }
      R(0, Math.random() * H | 0, W, 1, ACC);
    } else if (shownMode === 8 && !calm && $('optFx').checked && Math.random() < 1 / 220) glitch = 2;
    requestAnimationFrame(tick);
  }
  requestAnimationFrame(tick);
})();
