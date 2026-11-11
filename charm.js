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
    for (let i = 0; i < 5; i++) { if (i < 4) R(176 + i * 6, 8, 5, 8, ACC); else { c.strokeStyle = mixk(ACC, .45); c.strokeRect(176.5 + i * 6, 8.5, 4, 7); } }
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
  const TX = [8, 8, 160, 8, 86, 18, 88, 158], TY = [26, 104, 104, 174, 174, 238, 238, 238], TW = [224, 146, 72, 72, 146, 64, 64, 64], TH = [72, 64, 64, 58, 58, 22, 22, 22];
  const TMODE = [0, 4, 5, 6, 1, 2, 3, 7];
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
    ['IMG', 'VID', 'ID'].forEach((l, i) => { const k = 5 + i; hudPanel(TX[k], TY[k], TW[k], TH[k], ACC, .75); T(l, TX[k] + (TW[k] - l.length * 12) / 2, TY[k] + 4, 2, ACC); });
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
  const clip = () => $('sendClip').disabled ? empty('//VID', 'NO CLIPS', 'send a GIF or video') : c.drawImage($('clipCv'), 0, 0);
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
  const scenes = { 0: clock, 1: text, 2: photo, 3: clip, 4: music, 5: pet, 6: toy, 7: card, 8: deck };

  // ── tap a block to open it, tap again to go back ──
  cv.addEventListener('click', e => {
    const r = cv.getBoundingClientRect(), x = (e.clientX - r.left) * W / r.width, y = (e.clientY - r.top) * H / r.height;
    if (ui.mode !== 8) return window.charmGoto(8);
    for (let i = 0; i < 8; i++) if (x >= TX[i] - 3 && x < TX[i] + TW[i] + 3 && y >= TY[i] - 3 && y < (i >= 5 ? H : TY[i] + TH[i] + 3)) return window.charmGoto(TMODE[i]);
  });

  let shownMode = ui.mode, zoom = null;       // zoom: { ti, s, opening }
  function tick() {
    frame++;
    rx += (trx - rx) * 0.08; ry += (tryy - ry) * 0.08; tiltX = ry; tiltY = 0.4 + rx * 0.6;
    if (!calm) rig.style.transform = `rotateX(${(-rx * 9).toFixed(2)}deg) rotateY(${(ry * 13).toFixed(2)}deg)`;
    const s = song(), [r, g, b] = rgb(val('mColor') || '#f472b6'), mx = Math.max(r, g, b);
    ACC = ui.playing && s.title && mx >= 40 ? `rgb(${r * 255 / mx | 0},${g * 255 / mx | 0},${b * 255 / mx | 0})` : THEMES[ui.theme] || THEMES[0];
    if (ACC[0] !== '#') ACC = '#' + ACC.match(/\d+/g).map(v => (+v).toString(16).padStart(2, '0')).join('');

    if (ui.mode !== shownMode && !zoom) {
      const opening = shownMode === 8, ti = TMODE.indexOf(opening ? ui.mode : shownMode);
      if (ti >= 0 && (opening || ui.mode === 8) && !calm) zoom = { ti, s: 0, opening, to: ui.mode }; else { shownMode = ui.mode; glitch = Math.max(glitch, 3); }
    }
    c.save(); c.globalAlpha = 1;
    try {
      if (zoom) {
        deck(); zoom.s++;
        let t = zoom.s / 7; t = 1 - (1 - t) * (1 - t); if (!zoom.opening) t = 1 - t;
        const i = zoom.ti, x = TX[i] * (1 - t), y = TY[i] * (1 - t), w = TW[i] + (W - TW[i]) * t, h = TH[i] + (H - TH[i]) * t;
        R(x, y, w, h, '#000'); c.strokeStyle = ACC; c.lineWidth = 2; c.strokeRect(x + 1, y + 1, w - 2, h - 2);
        if (zoom.s >= 7) { shownMode = zoom.to; zoom = null; glitch = Math.max(glitch, 3); }
      } else {
        (scenes[shownMode] || deck)();
        if ([0, 5, 6, 7].includes(shownMode)) ticker();
        if (![2, 3, 4].includes(shownMode) && $('optFx').checked) { c.fillStyle = 'rgba(0,0,0,.22)'; for (let y = 1; y < H; y += 2) c.fillRect(0, y, W, 1); }
      }
    } catch (e) { /* one bad frame must not stop the preview */ }
    c.restore();
    if (glitch > 0) {
      glitch--;
      for (let k = 0; k < 5; k++) { const y = Math.random() * (H - 16) | 0, h = 2 + Math.random() * 12 | 0, off = (Math.random() * 56 - 28) | 0; c.drawImage(cv, 0, y, W, h, off, y, W, h); }
      R(0, Math.random() * H | 0, W, 1, ACC);
    } else if (shownMode === 8 && !calm && $('optFx').checked && Math.random() < 1 / 220) glitch = 2;
    requestAnimationFrame(tick);
  }
  requestAnimationFrame(tick);
})();
