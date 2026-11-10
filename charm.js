// The dangling charm: a pendulum you can poke, with a live preview of what the real one will show.
(() => {
  const $ = id => document.getElementById(id);
  const cv = $('heroCv'), c = cv.getContext('2d'), rig = $('rig');
  const W = 240, H = 280, ui = window.ui;
  const calm = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const MONO = 'ui-monospace, Menlo, Consolas, "Courier New", monospace';

  // ── pendulum ──
  let th = calm ? 0 : 0.42, om = 0, rest = 0, frame = 0, last = performance.now();
  window.kickCharm = (a = 0.6) => { if (!calm) om += (Math.random() < 0.5 ? -1 : 1) * a * 3; };
  let lastY = scrollY;
  addEventListener('scroll', () => { const d = scrollY - lastY; lastY = scrollY; if (!calm) om += Math.max(-1.6, Math.min(1.6, d * 0.012)); }, { passive: true });
  addEventListener('deviceorientation', e => { if (e.gamma != null && !calm) rest = Math.max(-0.45, Math.min(0.45, -e.gamma / 110)); });
  rig.addEventListener('pointerdown', e => {
    const r = rig.getBoundingClientRect();
    om += (e.clientX < r.left + r.width / 2 ? -1 : 1) * (calm ? 1.2 : 4.5);
    $('pokeNote').classList.add('gone');
  });

  // ── drawing helpers ──
  const hex = (r, g, b) => `rgb(${r | 0},${g | 0},${b | 0})`;
  function grad(a, b) { const g = c.createLinearGradient(0, 0, 0, H); g.addColorStop(0, a); g.addColorStop(1, b); c.fillStyle = g; c.fillRect(0, 0, W, H); }
  function txt(s, x, y, px, col, shadow = true, align = 'center') {
    c.font = `bold ${px}px ${MONO}`; c.textAlign = align; c.textBaseline = 'middle';
    if (shadow) { c.fillStyle = '#000'; c.fillText(s, x + Math.max(1, px / 14), y + Math.max(1, px / 14)); }
    c.fillStyle = col; c.fillText(s, x, y);
  }
  function wrap(s, px, maxW) {
    c.font = `bold ${px}px ${MONO}`;
    const out = [];
    for (const para of s.split('\n')) {
      let line = '';
      for (const w of para.split(' ')) {
        const t = line ? line + ' ' + w : w;
        if (c.measureText(t).width > maxW && line) { out.push(line); line = w; } else line = t;
      }
      out.push(line);
    }
    return out;
  }
  const val = id => $(id).value;

  // ── backgrounds ──
  const stars = Array.from({ length: 60 }, () => ({ x: Math.random() * 2 - 1, y: Math.random() * 2 - 1, z: Math.random() }));
  const flakes = Array.from({ length: 60 }, () => ({ x: Math.random() * W, y: Math.random() * H, r: 1 + Math.random() * 2.5 }));
  const cols = Array.from({ length: 24 }, () => ({ y: Math.random() * -30, v: 0.15 + Math.random() * 0.3 }));
  function background(bg) {
    const t = frame;
    if (bg === 1) {
      c.fillStyle = '#040212'; c.fillRect(0, 0, W, H);
      for (const s of stars) {
        s.z -= 0.012; if (s.z <= 0.05) { s.x = Math.random() * 2 - 1; s.y = Math.random() * 2 - 1; s.z = 1; }
        const b = 255 * (1 - s.z); c.fillStyle = hex(b, b, b + 60);
        c.fillRect(120 + s.x / s.z * 120, 140 + s.y / s.z * 140, s.z < 0.3 ? 2.5 : 1.5, s.z < 0.3 ? 2.5 : 1.5);
      }
    } else if (bg === 2) {
      const g = c.createLinearGradient(0, 0, 0, 170); g.addColorStop(0, '#14063c'); g.addColorStop(1, '#ff5aaa'); c.fillStyle = g; c.fillRect(0, 0, W, 170);
      const sg = c.createLinearGradient(0, 66, 0, 170); sg.addColorStop(0, '#ffe65a'); sg.addColorStop(1, '#ff508c'); c.fillStyle = sg;
      c.beginPath(); c.arc(120, 118, 52, 0, 7); c.fill();
      c.fillStyle = '#b03a8e'; for (let y = 124 + (t / 3 % 10); y < 170; y += 10) c.fillRect(60, y, 120, 3);
      c.fillStyle = '#0e0428'; c.fillRect(0, 170, W, 110);
      c.strokeStyle = '#5adcff'; c.lineWidth = 1; c.beginPath();
      for (let i = 0; i < 9; i++) { const k = (i + (t % 20) / 20) / 9, y = 170 + k * k * 110; c.moveTo(0, y); c.lineTo(W, y); }
      for (let i = -8; i <= 8; i++) { c.moveTo(120 + i * 14, 170); c.lineTo(120 + i * 90, H); }
      c.stroke(); c.fillStyle = '#f472b6'; c.fillRect(0, 170, W, 1.5);
    } else if (bg === 3) {
      c.fillStyle = '#000'; c.fillRect(0, 0, W, H); c.font = `bold 10px ${MONO}`; c.textAlign = 'left'; c.textBaseline = 'top';
      cols.forEach((col, i) => {
        col.y += col.v; if (col.y > 40) { col.y = Math.random() * -12; col.v = 0.15 + Math.random() * 0.3; }
        for (let k = 0; k < 12; k++) {
          const row = (col.y | 0) - k; if (row < 0 || row > 28) continue;
          c.fillStyle = k === 0 ? '#c8ffdc' : hex(0, 220 - k * 18, 50 - k * 4);
          c.fillText(String.fromCharCode(33 + (i * 31 + row * 17 + (t >> 4)) % 90), i * 10, row * 10);
        }
      });
    } else if (bg === 4 || bg === 5) {
      c.fillStyle = bg === 4 ? '#2e1065' : '#28083c'; c.fillRect(0, 0, W, H);
      const set = bg === 4 ? ['#ff5ad2', '#5ad2ff', '#ffe14a', '#7be36b'] : ['#ff8c3c', '#ff3c78', '#ffe178', '#ff8c3c'];
      set.forEach((col, i) => {
        const x = 120 + Math.sin(t * 0.021 + i * 2.1) * 80, y = 140 + Math.cos(t * 0.017 + i * 1.3) * 100, r = bg === 4 ? 130 : 46 + i * 8;
        const g = c.createRadialGradient(x, y, 0, x, y, r); g.addColorStop(0, col); g.addColorStop(bg === 4 ? 1 : 0.75, bg === 4 ? 'transparent' : col); g.addColorStop(1, 'transparent');
        c.globalAlpha = bg === 4 ? 0.75 : 1; c.fillStyle = g; c.fillRect(0, 0, W, H);
      });
      c.globalAlpha = 1;
    } else if (bg === 6) {
      grad('#08143c', '#285aa0'); c.fillStyle = '#fff';
      const gx = Math.sin(th), gy = Math.cos(th);            // falls toward real "down" as the charm swings
      for (const f of flakes) {
        f.x += gx * f.r * 0.9 + (Math.random() - 0.5) * 0.6; f.y += gy * f.r * 0.9;
        if (f.y > H) f.y -= H; if (f.x < 0) f.x += W; if (f.x > W) f.x -= W;
        c.fillRect(f.x, f.y, f.r, f.r);
      }
    } else if (bg === 7) {
      const off = (t % 80) / 2;
      for (let ty = -1; ty < 8; ty++) for (let tx = -1; tx < 7; tx++) { c.fillStyle = (tx + ty) & 1 ? '#ffaad7' : '#aae1ff'; c.fillRect(tx * 40 + off, ty * 40 + off, 40, 40); }
    } else if (bg === 8) {
      c.drawImage($('photoCv'), 0, 0);
    } else {
      grad('#2e1065', '#0c4a6e'); c.fillStyle = '#c8b4ff';
      [[24, 52], [210, 70], [40, 230], [200, 214], [120, 40], [18, 150], [224, 150], [90, 250]].forEach(([x, y], i) => {
        const p = (t + i * 9) % 40, r = p < 20 ? p / 5 : (40 - p) / 5;
        if (r > 0.5) { c.fillRect(x - r, y - 0.5, 2 * r, 1.5); c.fillRect(x - 0.5, y - r, 1.5, 2 * r); }
      });
    }
  }

  // ── scenes ──
  const HN = ['twelve', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten', 'eleven'];
  const bounce = { x: 40, y: 60, vx: 1.6, vy: 1.2, col: '#fff' };
  function clock() {
    background(ui.bg);
    const d = new Date(), hh = String(d.getHours()).padStart(2, '0'), mm = String(d.getMinutes()).padStart(2, '0');
    const date = d.toLocaleDateString('en-GB', { weekday: 'short', day: '2-digit', month: 'short' }).toUpperCase().replace(',', '');
    if (ui.face === 1) {
      c.strokeStyle = '#fff'; c.lineWidth = 2; c.beginPath(); c.arc(120, 140, 97, 0, 7); c.stroke();
      for (let i = 0; i < 12; i++) { const a = i * Math.PI / 6, r0 = i % 3 ? 88 : 82; c.beginPath(); c.moveTo(120 + Math.sin(a) * r0, 140 - Math.cos(a) * r0); c.lineTo(120 + Math.sin(a) * 95, 140 - Math.cos(a) * 95); c.stroke(); }
      const hand = (a, len, col, w) => { c.strokeStyle = col; c.lineWidth = w; c.lineCap = 'round'; c.beginPath(); c.moveTo(120, 140); c.lineTo(120 + Math.sin(a) * len, 140 - Math.cos(a) * len); c.stroke(); };
      hand((d.getHours() % 12 + d.getMinutes() / 60) * Math.PI / 6, 50, '#fff', 6);
      hand((d.getMinutes() + d.getSeconds() / 60) * Math.PI / 30, 76, '#7dd3fc', 4);
      hand(d.getSeconds() * Math.PI / 30, 86, '#f472b6', 1.5);
      c.fillStyle = '#f472b6'; c.beginPath(); c.arc(120, 140, 5, 0, 7); c.fill();
      txt(date, 120, 262, 11, '#fff');
    } else if (ui.face === 2) {
      let m5 = Math.round(d.getMinutes() / 5) * 5, h = d.getHours(); const to = m5 > 30; if (to) h++; if (m5 === 60) m5 = 0;
      const k = to && m5 ? 60 - m5 : m5, pre = { 5: 'five', 10: 'ten', 15: 'quarter', 20: 'twenty', 25: 'twenty five', 30: 'half' }[k];
      if (!k) { txt(HN[h % 12], 120, 124, 38, '#fff'); txt("o'clock", 120, 166, 28, '#7dd3fc'); }
      else { txt(pre, 120, 102, 28, '#7dd3fc'); txt(to ? 'to' : 'past', 120, 138, 28, '#f472b6'); txt(HN[h % 12], 120, 178, 38, '#fff'); }
    } else if (ui.face === 3) {
      const b = bounce; b.x += b.vx; b.y += b.vy; let hit = false;
      if (b.x < 0 || b.x > W - 150) { b.vx = -b.vx; hit = true; } if (b.y < 26 || b.y > H - 44) { b.vy = -b.vy; hit = true; }
      if (hit) b.col = `hsl(${Math.random() * 360 | 0} 100% 65%)`;
      txt(hh + ':' + mm, b.x, b.y + 20, 50, b.col, true, 'left');
    } else {
      txt(hh + (d.getMilliseconds() < 500 ? ':' : ' ') + mm, 120, 116, 66, '#fff');
      txt(date, 120, 170, 19, '#7dd3fc');
      c.strokeStyle = '#fff'; c.lineWidth = 1; c.strokeRect(40.5, 214.5, 160, 7); c.fillStyle = '#f472b6'; c.fillRect(41, 215, Math.max(6, 159 * d.getSeconds() / 59), 6);
    }
  }
  function text() {
    const fg = val('txtFg'), bg = val('txtBg'), style = +$('txtStyle').value, s = (val('txt') || ' ').slice(0, 300);
    c.fillStyle = bg; c.fillRect(0, 0, W, H);
    if (style === 1) { const one = s.replace(/\n/g, ' '); c.font = `bold 58px ${MONO}`; const w = c.measureText(one).width; txt(one, W - (frame * 5) % (w + W), 140, 58, fg, false, 'left'); return; }
    let px = 58, lines;
    for (; px >= 12; px -= 4) { lines = wrap(s, px, 220); if (lines.length * px * 1.15 <= 210 && lines.every(l => c.measureText(l).width <= 224)) break; }
    let y = 140 - (lines.length - 1) * px * 1.15 / 2;
    for (const [li, l] of lines.entries()) {
      if (style === 2) { c.font = `bold ${px}px ${MONO}`; let x = 120 - c.measureText(l).width / 2; for (const [i, ch] of [...l].entries()) { txt(ch, x, y, px, `hsl(${(i * 25 + li * 40 + frame * 6) % 360} 100% 60%)`, false, 'left'); x += c.measureText(ch).width; } }
      else txt(l, 120, y, px, fg);
      y += px * 1.15;
    }
  }
  function music() {
    const col = val('mColor') || '#f472b6', title = $('spTitle').textContent || val('mTitle') || 'nothing playing', artist = $('spArtist').textContent || val('mArtist');
    grad('#16081f', '#3a1838'); c.strokeStyle = col; c.lineWidth = 1.5;
    for (let i = 0; i < 3; i++) { c.globalAlpha = 0.8 - i * 0.25; c.beginPath(); c.arc(120, 102, 78 + i * 8 + Math.sin(frame * 0.35) * 3, 0, 7); c.stroke(); }
    c.globalAlpha = 1; c.fillStyle = '#000'; c.beginPath(); c.arc(120, 102, 62, 0, 7); c.fill();
    c.strokeStyle = '#2a2a32'; for (let r = 58; r > 24; r -= 6) { c.beginPath(); c.arc(120, 102, r, 0, 7); c.stroke(); }
    c.fillStyle = col; c.beginPath(); c.arc(120, 102, 20, 0, 7); c.fill();
    txt(title.slice(0, 16), 120, 186, 19, '#fff'); if (artist) txt(artist.slice(0, 28), 120, 206, 11, col);
    for (let i = 0; i < 16; i++) { const h = 3 + Math.abs(Math.sin(frame * 0.3 + i * 0.9) * Math.sin(i * 1.7 + frame * 0.11)) * 18; c.fillStyle = col; c.fillRect(24 + i * 12, 240 - h, 8, h); }
  }
  const SPR = [
    ['K..........K', 'KK........KK', 'KBK......KBK', 'KBBKKKKKKBBK', 'KBBBBBBBBBBK', 'KBBKBBBBKBBK', 'KBBKBBBBKBBK', 'KBPBBKKBBPBK', 'KBBBBBBBBBBK', '.KBBBBBBBBK.', '..KBBBBBBK..', '...KKKKKK...'],
    ['...KKKKKK...', '..KBBBBBBK..', '.KBBBBBBBBK.', 'KBBBBBBBBBBK', 'KBBKBBBBKBBK', 'KBBKBBBBKBBK', 'KBBBBBBBBBBK', 'KBPBBBBBBPBK', 'KBBBBKKBBBBK', 'KBBBBBBBBBBK', 'KBKBBKKBBKBK', 'KK.KK..KK.KK'],
    ['.KKK....KKK.', 'KWWKK..KKWWK', 'KWKWKKKKWKWK', 'KBBBBBBBBBBK', 'KBBBBBBBBBBK', 'KBPBBBBBBPBK', 'KBBKBBBBKBBK', 'KBBBKKKKBBBK', 'KBBBBBBBBBBK', '.KBBBBBBBBK.', 'KBBK....KBBK', 'KKK......KKK']];
  function pet() {
    const col = val('petCol'), spr = SPR[+$('petSp').value] || SPR[0];
    grad('#fff3e0', col); c.fillStyle = 'rgba(255,255,255,.35)';
    for (let y = 200; y < H; y += 16) for (let x = ((y / 16) % 2) * 16; x < W; x += 32) c.fillRect(x, y, 16, 16);
    txt(val('petName') || 'pet', 120, 44, 26, '#2e1065', false);
    const b = Math.abs(Math.sin(frame * 0.18)) * 14, lean = Math.max(-24, Math.min(24, -th * 60));     // leans as the charm swings
    c.fillStyle = 'rgba(46,16,101,.3)'; c.beginPath(); c.ellipse(120 + lean, 198, 46 - b, 7, 0, 0, 7); c.fill();
    spr.forEach((row, y) => [...row].forEach((ch, x) => { if (ch === '.') return; c.fillStyle = ch === 'K' ? '#1e1432' : ch === 'W' ? '#fff' : ch === 'P' ? '#f472b6' : col; c.fillRect(60 + lean + x * 10, 74 - b + y * 10, 10, 10); }));
    c.strokeStyle = '#2e1065'; c.lineWidth = 2; c.strokeRect(51, 229, 138, 10); c.fillStyle = '#34c759'; c.fillRect(53, 231, 96, 6);
  }
  function toy() {
    const k = ui.toy;
    if (k === 0) {
      grad('#1e0a46', '#080418'); c.fillStyle = '#000'; c.beginPath(); c.arc(120, 160, 94, 0, 7); c.fill();
      c.fillStyle = '#3c3c50'; c.beginPath(); c.arc(86, 112, 13, 0, 7); c.fill();
      c.fillStyle = '#1428a0'; c.beginPath(); c.arc(120, 164, 60, 0, 7); c.fill(); c.strokeStyle = '#7dd3fc'; c.lineWidth = 1.5; c.stroke();
      txt('shake', 120, 154, 20, '#fff', false); txt('me', 120, 176, 20, '#fff', false);
    } else if (k === 1) {
      grad('#0a5a3c', '#04281e');
      [[26, [0, 2, 4, 6, 8]], [126, [0, 8]]].forEach(([x, pips]) => {
        c.fillStyle = '#000'; c.beginPath(); c.roundRect(x + 4, 108, 88, 88, 12); c.fill(); c.fillStyle = '#fff'; c.beginPath(); c.roundRect(x, 104, 88, 88, 12); c.fill();
        c.fillStyle = '#2e1065'; pips.forEach(i => { c.beginPath(); c.arc(x + 22 * (1 + i % 3), 104 + 22 * (1 + (i / 3 | 0)), 8, 0, 7); c.fill(); });
      });
      txt('= 7', 120, 234, 34, '#fff');
    } else {
      grad('#461e0a', '#180828'); c.fillStyle = '#be8214'; c.beginPath(); c.arc(120, 158, 72, 0, 7); c.fill();
      c.fillStyle = '#ffc83c'; c.beginPath(); c.ellipse(120, 158, 66 * Math.abs(Math.cos(frame * 0.03)), 66, 0, 0, 7); c.fill();
      txt('heads or tails?', 120, 252, 15, '#fff');
    }
    txt(['< 8-ball >', '< dice >', '< coin >'][k], 120, 38, 19, '#fff');
  }
  function card() {
    const acc = val('cAcc');
    grad('#000', '#101a2a'); c.fillStyle = acc; for (let i = 0; i < 4; i++) { c.globalAlpha = 0.4 + i * 0.2; c.fillRect(0, 40 + i * 7, W, 3); } c.globalAlpha = 1;
    txt((val('cName') || '').slice(0, 20), 120, 104, (val('cName') || '').length <= 10 ? 30 : 20, '#fff');
    txt((val('cSub') || '').slice(0, 19), 120, 136, 17, acc);
    c.strokeStyle = acc; c.lineWidth = 2; for (let i = 0; i < 3; i++) { c.globalAlpha = i < (frame / 6 | 0) % 4 ? 1 : 0.25; c.beginPath(); c.arc(120, 205, 16 + i * 12, 0, 7); c.stroke(); }
    c.globalAlpha = 1; c.fillStyle = '#fff'; c.beginPath(); c.arc(120, 205, 9, 0, 7); c.fill();
    txt('TAP PHONE HERE', 120, 256, 10, '#fff', false);
  }
  const scenes = [clock, text, () => c.drawImage($('photoCv'), 0, 0), () => c.drawImage($('clipCv'), 0, 0), music, pet, toy, card];

  function tick(now) {
    const dt = Math.min(0.033, (now - last) / 1000); last = now; frame++;
    om += (-16 * Math.sin(th - rest) - 1.3 * om) * dt; th += om * dt;
    if (Math.abs(th - rest) > 0.0005 || Math.abs(om) > 0.0005) rig.style.transform = `rotate(${th}rad)`;
    c.save(); c.globalAlpha = 1;
    try { (scenes[ui.mode] || clock)(); } catch (e) { /* a scene must never stop the swing */ }
    c.restore();
    // battery + link dot, like the real status bar
    c.fillStyle = document.body.classList.contains('connected') ? '#7dd3fc' : '#5a5a6e'; c.beginPath(); c.arc(30, 14, 3, 0, 7); c.fill();
    requestAnimationFrame(tick);
  }
  requestAnimationFrame(tick);
})();
