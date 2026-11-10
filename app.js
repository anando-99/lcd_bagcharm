// ─────────── Protocol (must match firmware 2.x) ───────────
const SVC = '6e400001-c4a7-4d2e-9b3c-c4a2d7c0ffee';
const RX  = '6e400002-c4a7-4d2e-9b3c-c4a2d7c0ffee';
const ST  = '6e400003-c4a7-4d2e-9b3c-c4a2d7c0ffee';
const CMD = {MODE:1, TEXT:2, TIME:3, NOWPLAYING:4, XFER_BEGIN:5, XFER_DATA:6, XFER_END:7, BRIGHT:8, PET:9, CARD:10, SETTINGS:11, PING:12, BUZZ:13, FEED:14, CLIP_LIST:15, CLIP_PLAY:16, CLIP_DELETE:17, LOOK:18, TIMER:19, TOY:20};
const MODE = {CLOCK:0, TEXT:1, PHOTO:2, CLIP:3, MUSIC:4, PET:5, TOY:6, CARD:7};
const ui = window.ui = { mode: 0, bg: 0, face: 0, toy: 0 };
const CHUNK = 495, WINDOW = 16;
const W = 240, H = 280;

const $ = id => document.getElementById(id);
const enc = new TextEncoder();
let device = null, rxChar = null, stChar = null, manualDisconnect = false;
let queue = Promise.resolve();
let state = { fsUsed: 0, fsTotal: 0, clipId: 0, clips: [] };

function hud(msg){ const t = $('hud'); t.textContent = msg; t.classList.add('on'); if (window.kickCharm) kickCharm(0.5); clearTimeout(t._h); t._h = setTimeout(() => t.classList.remove('on'), 2200); }
const act = fn => async (...a) => { try { await fn(...a); } catch (e) { hud(e.message === 'not connected' ? 'Connect your Larp Charm first.' : e.message); } };

function enqueue(fn){ const p = queue.then(fn); queue = p.catch(() => {}); return p; }
function write(bytes){ return enqueue(async () => { if (!rxChar) throw new Error('not connected'); await rxChar.writeValueWithResponse(bytes); }); }

const u32 = v => [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255];
const i16 = v => [v & 255, (v >> 8) & 255];
const hexRgb = h => [parseInt(h.slice(1,3),16), parseInt(h.slice(3,5),16), parseInt(h.slice(5,7),16)];
const rgb565 = h => { const [r,g,b] = hexRgb(h); const v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); return [v >> 8, v & 255]; };
const ascii = s => s.normalize('NFKD').replace(/[̀-ͯ]/g, '').replace(/[^\x20-\x7E\n]/g, '?');
function packet(cmd, header = [], str = ''){ const s = enc.encode(str); const out = new Uint8Array(1 + header.length + s.length); out[0] = cmd; out.set(header, 1); out.set(s, 1 + header.length); return out; }
const fmtBytes = b => b >= 1048576 ? (b / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(b / 1024)) + ' KB';
const fmtTime = s => { s = Math.max(0, Math.round(s)); return s >= 60 ? Math.floor(s / 60) + ' min ' + (s % 60) + ' s' : s + ' s'; };
const sleep = ms => new Promise(r => setTimeout(r, ms));

// ─────────── Connection ───────────
if (!navigator.bluetooth) $('noBle').style.display = 'block';

async function connect(){
  if (device && device.gatt.connected) { manualDisconnect = true; device.gatt.disconnect(); return; }
  try {
    device = await navigator.bluetooth.requestDevice({ filters: [{ services: [SVC] }] });
    device.addEventListener('gattserverdisconnected', onDisconnected);
    manualDisconnect = false;
    await connectGatt();
  } catch (e) { if (e.name !== 'NotFoundError') hud(e.message); }
}
$('connectBtn').onclick = connect;
$('heroConnect').onclick = connect;

async function connectGatt(){
  $('connText').textContent = 'Connecting…';
  const server = await device.gatt.connect();
  const svc = await server.getPrimaryService(SVC);
  rxChar = await svc.getCharacteristic(RX);
  stChar = await svc.getCharacteristic(ST);
  await stChar.startNotifications();
  stChar.addEventListener('characteristicvaluechanged', e => onStatus(new TextDecoder().decode(e.target.value)));
  document.body.classList.add('connected');
  $('dot').classList.add('on');
  $('connText').textContent = device.name || 'Larp Charm';
  $('connectBtn').textContent = 'Disconnect';
  $('heroConnect').textContent = 'Connected';
  await syncTime();
  await write(packet(CMD.PING));
  await refreshClips();
  hud('Connected to ' + (device.name || 'Larp Charm'));
  if (spotifyToken()) startSpotifyLoop();
}

async function onDisconnected(){
  rxChar = stChar = null;
  document.body.classList.remove('connected');
  $('dot').classList.remove('on');
  $('connText').textContent = 'Not connected';
  $('connectBtn').textContent = 'Connect';
  $('heroConnect').textContent = 'Connect';
  $('bat').classList.remove('on');
  rejectWaiters(new Error('Disconnected'));
  if (manualDisconnect || !device) return;
  for (let i = 0; i < 5; i++) {
    await sleep(1500 * (i + 1));
    try { await connectGatt(); return; } catch (_) {}
  }
}

// ─────────── Notifications ───────────
let ackBytes = 0, ackWaiters = [], xferWaiter = null, listCollect = null;
function rejectWaiters(err){ ackWaiters.forEach(w => w.reject(err)); ackWaiters = []; if (xferWaiter) { xferWaiter.reject(err); xferWaiter = null; } }

function onStatus(s){
  if (s.startsWith('ack=')) {
    ackBytes = +s.slice(4);
    ackWaiters = ackWaiters.filter(w => { if (ackBytes >= w.n) { w.resolve(); return false; } return true; });
    return;
  }
  if (s.startsWith('item=')) {
    const p = s.slice(5).split(',');
    if (listCollect) listCollect.push({ id: +p[0], frames: +p[1], bytes: +p[2], name: p.slice(3).join(',') || 'Clip' });
    return;
  }
  const kv = Object.fromEntries(s.split(';').map(x => { const i = x.indexOf('='); return [x.slice(0, i), x.slice(i + 1)]; }));
  if (kv.b !== undefined) setBattery(+kv.b, kv.c === '1');
  if (kv.fw) $('fw').textContent = kv.fw;
  if (kv.m !== undefined) setActiveMode(+kv.m);
  if (kv.lk) { const [b, f] = kv.lk.split(',').map(Number); setLook(b, f, false); }
  if (kv.toy !== undefined) setToy(+kv.toy, false);
  if (kv.imu !== undefined) $('imuNote').classList.toggle('hidden', kv.imu === '1');
  if (kv.fs) { const [u, t] = kv.fs.split(',').map(Number); state.fsUsed = u; state.fsTotal = t; renderStorage(); }
  if (kv.clip !== undefined) { state.clipId = +kv.clip; renderClipList(); }
  if (kv.list === 'end' && listCollect) { state.clips = listCollect; listCollect = null; renderClipList(); }
  if (kv.xfer && xferWaiter) {
    const w = xferWaiter; xferWaiter = null;
    if (kv.xfer === 'ok') w.resolve();
    else w.reject(new Error({ space: 'Not enough space on the charm. Delete a clip first.', nofs: 'Storage isn’t set up on the charm. Update its firmware.', format: 'The clip didn’t arrive intact. Try again.' }[kv.why] || 'Transfer failed. Try again.'));
  }
}

function setBattery(pct, chg){
  $('bat').classList.add('on');
  $('bat').classList.toggle('charging', chg);
  $('batFill').style.width = Math.max(1, 17 * pct / 100) + 'px';
  $('batPct').textContent = pct + '%';
}

function setActiveMode(m){ ui.mode = m; document.querySelectorAll('#modes .tile').forEach(b => b.classList.toggle('active', +b.dataset.m === m)); }

async function syncTime(){
  const epoch = Math.floor(Date.now() / 1000), tz = -new Date().getTimezoneOffset();
  await write(packet(CMD.TIME, [...u32(epoch), ...i16(tz)]));
}

// ─────────── Controls ───────────
function seg(id, onChange){
  const el = $(id); el.value = el.querySelector('.on').dataset.v;
  el.querySelectorAll('button').forEach(b => b.onclick = () => {
    el.querySelectorAll('button').forEach(x => x.classList.remove('on'));
    b.classList.add('on'); el.value = b.dataset.v; onChange && onChange(el.value);
  });
}
function setSeg(id, v){ const b = $(id).querySelector(`[data-v="${v}"]`); if (b) b.click(); }

document.querySelectorAll('.swatches').forEach(box => {
  const target = $(box.dataset.target);
  const pick = v => { target.value = v; box.querySelectorAll('.sw').forEach(s => s.classList.toggle('on', s.dataset.c === v)); localStorage.setItem('f_' + target.id, v); };
  box.dataset.colors.split(',').forEach(c => { const b = document.createElement('button'); b.className = 'sw'; b.style.background = c; b.dataset.c = c; b.title = c; b.onclick = () => pick(c); box.appendChild(b); });
  const custom = document.createElement('label'); custom.className = 'sw-custom'; custom.title = 'Custom colour';
  const inp = document.createElement('input'); inp.type = 'color'; inp.oninput = () => pick(inp.value); custom.appendChild(inp); box.appendChild(custom);
  pick(localStorage.getItem('f_' + target.id) || target.value);
});

function rangeFill(el){ el.style.setProperty('--p', ((el.value - el.min) / (el.max - el.min || 1) * 100) + '%'); }
document.querySelectorAll('input[type=range]').forEach(r => { rangeFill(r); r.addEventListener('input', () => rangeFill(r)); });

document.querySelectorAll('#modes .tile').forEach(b => b.onclick = () => { setActiveMode(+b.dataset.m); if (rxChar) write(packet(CMD.MODE, [+b.dataset.m])).catch(() => {}); });

// ─────────── Text ───────────
seg('txtStyle');
$('sendTxt').onclick = act(async () => {
  const t = ascii($('txt').value).slice(0, 300);
  await write(packet(CMD.TEXT, [+$('txtStyle').value, ...rgb565($('txtFg').value), ...rgb565($('txtBg').value)], t));
  setActiveMode(MODE.TEXT); hud('Message sent');
});

// ─────────── Images ───────────
function toRGB565(ctx, w, h){
  const d = ctx.getImageData(0, 0, w, h).data, out = new Uint8Array(w * h * 2);
  for (let i = 0, j = 0; i < d.length; i += 4, j += 2) {
    const v = ((d[i] & 0xF8) << 8) | ((d[i+1] & 0xFC) << 3) | (d[i+2] >> 3);
    out[j] = v & 255; out[j+1] = v >> 8;
  }
  return out;
}
function srcSize(img){ return [img.displayWidth || img.videoWidth || img.naturalWidth || img.width, img.displayHeight || img.videoHeight || img.naturalHeight || img.height]; }
function drawFitted(ctx, img, w, h, fit = 'cover', zoom = 1, bg = '#000'){
  const [iw, ih] = srcSize(img);
  ctx.fillStyle = bg; ctx.fillRect(0, 0, w, h);
  if (!iw || !ih) return;
  const s = (fit === 'cover' ? Math.max(w / iw, h / ih) : Math.min(w / iw, h / ih)) * zoom;
  const dw = iw * s, dh = ih * s;
  ctx.imageSmoothingQuality = 'high';
  ctx.drawImage(img, (w - dw) / 2, (h - dh) / 2, dw, dh);
}
function loadImage(file){ return new Promise((res, rej) => { const img = new Image(); img.onload = () => res(img); img.onerror = () => rej(new Error('Couldn’t open that image.')); img.src = URL.createObjectURL(file); }); }

// Windowed transfer: writes without response, waits for the charm to confirm every WINDOW chunks.
function waitAck(n, ms = 10000){
  if (ackBytes >= n) return Promise.resolve();
  return new Promise((resolve, reject) => {
    const w = { n, resolve, reject };
    ackWaiters.push(w);
    setTimeout(() => { if (ackWaiters.includes(w)) { ackWaiters = ackWaiters.filter(x => x !== w); reject(new Error('The charm stopped responding. Move closer and try again.')); } }, ms);
  });
}
function sendBlob(kind, bytes, id, onProgress){
  return enqueue(async () => {
    if (!rxChar) throw new Error('not connected');
    ackBytes = 0;
    const done = new Promise((resolve, reject) => { xferWaiter = { resolve, reject }; });
    done.catch(() => {});
    await rxChar.writeValueWithResponse(packet(CMD.XFER_BEGIN, [kind, ...u32(bytes.length), ...u32(id >>> 0)]));
    await sleep(60);
    if (!xferWaiter) return done;   // rejected immediately (no space etc.)
    let noResp = typeof rxChar.writeValueWithoutResponse === 'function';
    const t0 = performance.now();
    let n = 0;
    for (let off = 0; off < bytes.length; off += CHUNK) {
      if (!xferWaiter) return done;
      const part = bytes.subarray(off, off + CHUNK);
      const pkt = new Uint8Array(part.length + 1); pkt[0] = CMD.XFER_DATA; pkt.set(part, 1);
      if (noResp) {
        try { await rxChar.writeValueWithoutResponse(pkt); }
        catch (_) { noResp = false; await rxChar.writeValueWithResponse(pkt); }
      } else await rxChar.writeValueWithResponse(pkt);
      const sent = Math.min(bytes.length, off + CHUNK);
      if (++n % WINDOW === 0) await waitAck(sent);
      if (onProgress) { const el = (performance.now() - t0) / 1000; onProgress(sent / bytes.length, el > 1 ? (bytes.length - sent) / (sent / el) : null); }
    }
    await waitAck(bytes.length);
    await rxChar.writeValueWithResponse(Uint8Array.of(CMD.XFER_END));
    setTimeout(() => { if (xferWaiter) { xferWaiter.reject(new Error('No reply from the charm.')); xferWaiter = null; } }, 15000);
    return done;
  });
}
function progress(id){
  const box = $(id), bar = box.querySelector('i'), txt = box.querySelector('p');
  return {
    show(msg){ box.classList.add('on'); bar.style.width = '0'; txt.textContent = msg || ''; },
    set(f, msg){ bar.style.width = (f * 100).toFixed(1) + '%'; if (msg !== undefined) txt.textContent = msg; },
    hide(delay = 1200){ setTimeout(() => box.classList.remove('on'), delay); }
  };
}

// Photo
let photoImg = null;
const pctx = $('photoCv').getContext('2d', { willReadFrequently: true });
function placeholder(ctx, w, h, label){
  const g = ctx.createLinearGradient(0, 0, 0, h); g.addColorStop(0, '#1c1c1e'); g.addColorStop(1, '#2c2c2e');
  ctx.fillStyle = g; ctx.fillRect(0, 0, w, h);
  ctx.fillStyle = '#86868b'; ctx.font = '500 15px ' + getComputedStyle(document.body).fontFamily; ctx.textAlign = 'center'; ctx.fillText(label, w / 2, h / 2);
}
placeholder(pctx, W, H, 'No photo');
function redrawPhoto(){ if (photoImg) drawFitted(pctx, photoImg, W, H, $('photoFit').value, $('photoZoom').value / 100); $('zoomVal').textContent = $('photoZoom').value + '%'; }
seg('photoFit', redrawPhoto);
$('photoZoom').addEventListener('input', redrawPhoto);
$('photoFile').onchange = act(async e => { if (!e.target.files[0]) return; photoImg = await loadImage(e.target.files[0]); redrawPhoto(); $('sendPhoto').disabled = false; });
$('sendPhoto').onclick = act(async () => {
  const pr = progress('photoProg'); pr.show('Sending…'); $('sendPhoto').disabled = true;
  try {
    await sendBlob(0, toRGB565(pctx, W, H), 0, (f, eta) => pr.set(f, eta != null ? `Sending… ${fmtTime(eta)} left` : 'Sending…'));
    pr.set(1, 'Saved on your charm.'); setActiveMode(MODE.PHOTO);
  } catch (e) { pr.set(0, e.message); throw e; }
  finally { $('sendPhoto').disabled = false; pr.hide(2500); }
});

// ─────────── Clips (GIF / video → MJPEG) ───────────
const cctx = $('clipCv').getContext('2d', { willReadFrequently: true });
placeholder(cctx, W, H, 'No clip selected');
let clipSrc = null;   // { kind:'gif'|'video'|'image', file, video?, decoder?, frameCount?, duration? }
let previewTimer = null;
seg('clipFit', () => { drawClipPreview(); estimateClip(); });
seg('clipFps', estimateClip);
seg('clipQ', estimateClip);

function clipOpts(){
  return { fit: $('clipFit').value, q: +$('clipQ').value, fps: +$('clipFps').value, start: +$('clipStart').value, len: +$('clipLen').value };
}

async function jpegBlob(canvas, q){ return new Promise(r => canvas.toBlob(r, 'image/jpeg', q)); }

$('clipFile').onchange = act(async e => {
  const file = e.target.files[0]; if (!file) return;
  clearInterval(previewTimer);
  if (clipSrc && clipSrc.decoder) clipSrc.decoder.close();
  clipSrc = null;
  $('sendClip').disabled = true;
  $('clipName').value = file.name.replace(/\.[^.]+$/, '').slice(0, 20);
  if (file.type === 'image/gif' || /\.gif$/i.test(file.name)) {
    if (!('ImageDecoder' in window)) throw new Error('This browser can’t read GIF frames. Use Chrome.');
    const decoder = new ImageDecoder({ data: await file.arrayBuffer(), type: 'image/gif' });
    await decoder.tracks.ready;
    await decoder.completed;
    const frameCount = decoder.tracks.selectedTrack.frameCount;
    let duration = 0;
    for (let i = 0; i < Math.min(frameCount, 400); i++) { const { image } = await decoder.decode({ frameIndex: i }); duration += (image.duration || 100000) / 1e6; image.close(); }
    if (frameCount > 400) duration *= frameCount / 400;
    clipSrc = { kind: 'gif', file, decoder, frameCount, duration };
    $('videoOpts').classList.add('hidden');
    $('clipInfo').textContent = `GIF · ${frameCount} frames · ${duration.toFixed(1)} s`;
    let fi = 0;
    previewTimer = setInterval(async () => { if (!clipSrc || clipSrc.kind !== 'gif') return; const { image } = await decoder.decode({ frameIndex: fi }); drawFitted(cctx, image, W, H, $('clipFit').value); image.close(); fi = (fi + 1) % frameCount; }, 80);
  } else if (file.type.startsWith('video/') || /\.(mp4|mov|webm|m4v)$/i.test(file.name)) {
    const v = document.createElement('video');
    v.muted = true; v.playsInline = true; v.preload = 'auto'; v.src = URL.createObjectURL(file);
    await new Promise((res, rej) => { v.onloadeddata = res; v.onerror = () => rej(new Error('Couldn’t open that video. Try an MP4.')); });
    clipSrc = { kind: 'video', file, video: v, duration: v.duration };
    $('videoOpts').classList.remove('hidden');
    $('clipStart').max = Math.max(0, v.duration - 1).toFixed(1); $('clipStart').value = 0;
    $('clipLen').max = Math.max(1, Math.min(60, Math.floor(v.duration))); $('clipLen').value = Math.min(10, $('clipLen').max);
    [$('clipStart'), $('clipLen')].forEach(rangeFill);
    $('clipInfo').textContent = `Video · ${fmtTime(v.duration)} · ${v.videoWidth}×${v.videoHeight}`;
    updateVideoLabels();
    await seekVideo(v, 0); drawClipPreview();
  } else throw new Error('Choose a GIF or a video file.');
  $('sendClip').disabled = false;
  estimateClip();
});

function seekVideo(v, t){ return new Promise(res => { if (Math.abs(v.currentTime - t) < 0.001 && v.readyState >= 2) return res(); const h = () => { v.removeEventListener('seeked', h); res(); }; v.addEventListener('seeked', h); v.currentTime = t; }); }
function updateVideoLabels(){ const s = +$('clipStart').value; $('startVal').textContent = Math.floor(s / 60) + ':' + String(Math.floor(s % 60)).padStart(2, '0'); $('lenVal').textContent = $('clipLen').value + ' s'; }
$('clipStart').addEventListener('input', act(async () => { updateVideoLabels(); if (clipSrc && clipSrc.kind === 'video') { await seekVideo(clipSrc.video, +$('clipStart').value); drawClipPreview(); } estimateClip(); }));
$('clipLen').addEventListener('input', () => { updateVideoLabels(); estimateClip(); });

function drawClipPreview(){ if (clipSrc && clipSrc.kind === 'video') drawFitted(cctx, clipSrc.video, W, H, $('clipFit').value); }

let estToken = 0;
async function estimateClip(){
  if (!clipSrc) return;
  const tok = ++estToken, o = clipOpts();
  const frames = clipSrc.kind === 'gif' ? clipSrc.frameCount : Math.max(1, Math.round(Math.min(o.len, clipSrc.duration - o.start) * o.fps));
  const sample = await jpegBlob($('clipCv'), o.q);
  if (tok !== estToken) return;
  const bytes = frames * (sample.size * 1.08 + 6) + 38;
  const free = state.fsTotal ? state.fsTotal - state.fsUsed : 0;
  let msg = `About ${fmtBytes(bytes)} · ${frames} frames · roughly ${fmtTime(bytes / 30000)} to send.`;
  if (free && bytes > free - 65536) msg += ` That’s more than the ${fmtBytes(free)} free. Shorten it or delete a clip.`;
  $('clipEstimate').textContent = msg;
}

async function buildClip(onProgress){
  const o = clipOpts(), cv = document.createElement('canvas'); cv.width = W; cv.height = H;
  const ctx = cv.getContext('2d');
  const frames = [];
  if (clipSrc.kind === 'gif') {
    const d = clipSrc.decoder, n = Math.min(clipSrc.frameCount, 1500);
    for (let i = 0; i < n; i++) {
      const { image } = await d.decode({ frameIndex: i });
      drawFitted(ctx, image, W, H, o.fit);
      const delay = Math.max(20, Math.min(10000, Math.round((image.duration || 100000) / 1000)));
      image.close();
      frames.push({ data: new Uint8Array(await (await jpegBlob(cv, o.q)).arrayBuffer()), delay });
      onProgress(i + 1, n);
    }
  } else {
    const v = clipSrc.video, len = Math.min(o.len, v.duration - o.start), n = Math.max(1, Math.round(len * o.fps)), delay = Math.round(1000 / o.fps);
    for (let i = 0; i < n; i++) {
      await seekVideo(v, Math.min(v.duration - 0.01, o.start + i / o.fps));
      drawFitted(ctx, v, W, H, o.fit);
      frames.push({ data: new Uint8Array(await (await jpegBlob(cv, o.q)).arrayBuffer()), delay });
      onProgress(i + 1, n);
    }
    await seekVideo(v, o.start); drawClipPreview();
  }
  // header: "LCLP" ver flags w h frames name[24]
  const total = 38 + frames.reduce((s, f) => s + 6 + f.data.length, 0);
  const out = new Uint8Array(total), dv = new DataView(out.buffer);
  out.set(enc.encode('LCLP'), 0); out[4] = 1; out[5] = 0;
  dv.setUint16(6, W, true); dv.setUint16(8, H, true); dv.setUint32(10, frames.length, true);
  const name = enc.encode(ascii($('clipName').value || 'Clip').replace(/[;,]/g, ' ').slice(0, 23));
  out.set(name, 14);
  let off = 38;
  for (const f of frames) { dv.setUint32(off, f.data.length, true); dv.setUint16(off + 4, f.delay, true); out.set(f.data, off + 6); off += 6 + f.data.length; }
  return out;
}

$('sendClip').onclick = act(async () => {
  if (!clipSrc) return;
  if (!rxChar) throw new Error('not connected');
  const pr = progress('clipProg'); pr.show('Preparing frames…');
  $('sendClip').disabled = true; clearInterval(previewTimer);
  try {
    const bytes = await buildClip((i, n) => pr.set(i / n * 0.25, `Preparing frames ${i} of ${n}…`));
    const free = state.fsTotal - state.fsUsed;
    if (state.fsTotal && bytes.length > free - 65536) throw new Error(`This clip is ${fmtBytes(bytes.length)} but only ${fmtBytes(free)} is free. Shorten it or delete a clip.`);
    const id = Math.floor(Date.now() / 1000);
    await sendBlob(2, bytes, id, (f, eta) => pr.set(0.25 + f * 0.75, `Sending ${Math.round(f * 100)}%` + (eta != null ? ` · ${fmtTime(eta)} left` : '')));
    pr.set(1, 'Playing on your charm.');
    setActiveMode(MODE.CLIP);
    await refreshClips();
  } catch (e) { pr.set(0, e.message); throw e; }
  finally { $('sendClip').disabled = false; pr.hide(3000); if (clipSrc && clipSrc.kind === 'gif') { let fi = 0; previewTimer = setInterval(async () => { if (!clipSrc || clipSrc.kind !== 'gif') return; const { image } = await clipSrc.decoder.decode({ frameIndex: fi }); drawFitted(cctx, image, W, H, $('clipFit').value); image.close(); fi = (fi + 1) % clipSrc.frameCount; }, 80); } }
});

async function refreshClips(){
  if (!rxChar) return;
  listCollect = [];
  await write(packet(CMD.CLIP_LIST));
}
$('refreshClips').onclick = act(refreshClips);

function renderClipList(){
  const ul = $('clipList'); ul.innerHTML = '';
  if (!rxChar) { ul.innerHTML = '<li class="empty">Connect to see your saved clips.</li>'; return; }
  if (!state.clips.length) { ul.innerHTML = '<li class="empty">No clips yet. Send a GIF or video above.</li>'; return; }
  for (const c of state.clips) {
    const li = document.createElement('li');
    const meta = document.createElement('div'); meta.className = 'meta';
    const b = document.createElement('b'); b.textContent = c.name;
    const sp = document.createElement('span'); sp.textContent = `${c.frames} frames · ${fmtBytes(c.bytes)}`;
    meta.append(b, sp);
    li.append(meta);
    if (c.id === state.clipId) { const now = document.createElement('span'); now.className = 'now'; now.textContent = 'Now showing'; li.append(now); }
    else { const play = document.createElement('button'); play.className = 'btn sm'; play.textContent = 'Play'; play.onclick = act(async () => { await write(packet(CMD.CLIP_PLAY, u32(c.id))); setActiveMode(MODE.CLIP); }); li.append(play); }
    const del = document.createElement('button'); del.className = 'btn sm grey'; del.textContent = 'Delete';
    del.onclick = act(async () => { if (!confirm(`Delete “${c.name}” from your charm?`)) return; listCollect = []; await write(packet(CMD.CLIP_DELETE, u32(c.id))); });
    li.append(del);
    ul.append(li);
  }
}
function renderStorage(){
  if (!state.fsTotal) return;
  $('storageText').textContent = `${fmtBytes(state.fsUsed)} of ${fmtBytes(state.fsTotal)} used`;
  $('storageFree').textContent = `${fmtBytes(state.fsTotal - state.fsUsed)} free`;
  $('storageFill').style.width = (state.fsUsed / state.fsTotal * 100).toFixed(1) + '%';
}

// ─────────── Music ───────────
seg('musicSrc', v => { $('spotifyBox').classList.toggle('hidden', v !== 'spotify'); $('manualBox').classList.toggle('hidden', v !== 'manual'); });
function nowPlayingPacket(rgb, playing, posMs, durMs, title, artist){
  return packet(CMD.NOWPLAYING, [...rgb, playing ? 1 : 0, ...u32(posMs), ...u32(durMs)], ascii(title).slice(0, 80) + '\x1F' + ascii(artist).slice(0, 60));
}
function vibeColor(ctx, w, h){
  const d = ctx.getImageData(0, 0, w, h).data; let r = 0, g = 0, b = 0, t = 0;
  for (let i = 0; i < d.length; i += 16) {
    const R = d[i], G = d[i+1], B = d[i+2], mx = Math.max(R, G, B), mn = Math.min(R, G, B), sat = mx ? (mx - mn) / mx : 0;
    const wgt = 0.05 + sat * sat * (mx / 255);
    r += R * wgt; g += G * wgt; b += B * wgt; t += wgt;
  }
  const c = [r / t, g / t, b / t], mx = Math.max(...c) || 1;
  return c.map(v => Math.round(Math.min(255, v * (220 / mx))));
}
const actx = $('artCv').getContext('2d', { willReadFrequently: true });
let manualArt = null;
$('mArtFile').onchange = act(async e => { if (!e.target.files[0]) return; manualArt = await loadImage(e.target.files[0]); drawFitted(actx, manualArt, 120, 120); $('mColor').value = '#' + vibeColor(actx, 120, 120).map(v => v.toString(16).padStart(2, '0')).join(''); hud('Album art ready'); });
$('sendMusic').onclick = act(async () => {
  const [m, s] = $('mLen').value.split(':').map(Number);
  const dur = ((m || 0) * 60 + (s || 0)) * 1000;
  await write(nowPlayingPacket(hexRgb($('mColor').value), $('mPlaying').checked, 0, dur, $('mTitle').value || 'Untitled', $('mArtist').value));
  if (manualArt) await sendBlob(1, toRGB565(actx, 120, 120), 0);
  await write(packet(CMD.MODE, [MODE.MUSIC])); setActiveMode(MODE.MUSIC); hud('Now playing sent');
});

const REDIRECT = location.origin + location.pathname;
$('redirUri').textContent = REDIRECT;
$('spClient').value = localStorage.getItem('sp_client') || '';
$('spClient').onchange = () => localStorage.setItem('sp_client', $('spClient').value.trim());
const b64url = buf => btoa(String.fromCharCode(...new Uint8Array(buf))).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
function spotifyToken(){ return localStorage.getItem('sp_access'); }
$('spLogin').onclick = act(async () => {
  const client = $('spClient').value.trim(); if (!client) throw new Error('Paste your Spotify Client ID first.');
  localStorage.setItem('sp_client', client);
  const verifier = b64url(crypto.getRandomValues(new Uint8Array(48)));
  localStorage.setItem('sp_verifier', verifier);
  const challenge = b64url(await crypto.subtle.digest('SHA-256', enc.encode(verifier)));
  const q = new URLSearchParams({ response_type: 'code', client_id: client, redirect_uri: REDIRECT, scope: 'user-read-currently-playing user-read-playback-state', code_challenge_method: 'S256', code_challenge: challenge });
  location.href = 'https://accounts.spotify.com/authorize?' + q;
});
$('spLogout').onclick = () => { ['sp_access', 'sp_refresh', 'sp_exp'].forEach(k => localStorage.removeItem(k)); $('spNow').style.display = 'none'; hud('Signed out of Spotify'); };
async function tokenRequest(params){
  const r = await fetch('https://accounts.spotify.com/api/token', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams(params) });
  const j = await r.json(); if (!r.ok) throw new Error(j.error_description || 'Spotify sign-in failed');
  localStorage.setItem('sp_access', j.access_token);
  if (j.refresh_token) localStorage.setItem('sp_refresh', j.refresh_token);
  localStorage.setItem('sp_exp', Date.now() + (j.expires_in - 60) * 1000);
}
async function ensureToken(){
  if (Date.now() < +localStorage.getItem('sp_exp')) return true;
  const rt = localStorage.getItem('sp_refresh'); if (!rt) return false;
  await tokenRequest({ grant_type: 'refresh_token', refresh_token: rt, client_id: localStorage.getItem('sp_client') });
  return true;
}
(async () => {
  const code = new URLSearchParams(location.search).get('code');
  if (!code) return;
  try {
    await tokenRequest({ grant_type: 'authorization_code', code, redirect_uri: REDIRECT, client_id: localStorage.getItem('sp_client'), code_verifier: localStorage.getItem('sp_verifier') });
    hud('Spotify connected. Now connect your charm.');
  } catch (e) { hud(e.message); }
  history.replaceState(null, '', REDIRECT);
})();
let spTimer = null, lastTrack = null, lastPlaying = null, lastSent = 0;
function startSpotifyLoop(){ clearInterval(spTimer); spTimer = setInterval(pollSpotify, 5000); pollSpotify(); }
async function pollSpotify(){
  if (!$('spLive').checked || !rxChar || document.hidden) return;
  try {
    if (!(await ensureToken())) return;
    const r = await fetch('https://api.spotify.com/v1/me/player/currently-playing', { headers: { Authorization: 'Bearer ' + spotifyToken() } });
    if (r.status === 204) return;
    if (r.status === 401) { localStorage.setItem('sp_exp', 0); return; }
    const j = await r.json(); const it = j.item; if (!it) return;
    const title = it.name, artist = (it.artists || []).map(a => a.name).join(', ');
    const changed = it.id !== lastTrack;
    if (!changed && j.is_playing === lastPlaying && Date.now() - lastSent < 20000) return;
    let rgb = [244, 114, 182], artBytes = null;
    const art = it.album && it.album.images && (it.album.images.find(i => i.width <= 320) || it.album.images[0]);
    if (art) {
      try {
        const img = new Image(); img.crossOrigin = 'anonymous';
        await new Promise((res, rej) => { img.onload = res; img.onerror = rej; img.src = art.url; });
        drawFitted(actx, img, 120, 120);
        rgb = vibeColor(actx, 120, 120);
        if (changed) artBytes = toRGB565(actx, 120, 120);
        $('spArt').src = art.url;
      } catch (_) {}
    }
    await write(nowPlayingPacket(rgb, j.is_playing, j.progress_ms || 0, it.duration_ms || 0, title, artist));
    if (artBytes) await sendBlob(1, artBytes, 0);
    lastTrack = it.id; lastPlaying = j.is_playing; lastSent = Date.now();
    $('spNow').style.display = 'flex'; $('spTitle').textContent = title; $('spArtist').textContent = artist;
  } catch (e) { console.warn(e); }
}

// ─────────── Pet / Card / NFC ───────────
seg('petSp');
$('sendPet').onclick = act(async () => { await write(packet(CMD.PET, [+$('petSp').value, ...hexRgb($('petCol').value)], ascii($('petName').value).slice(0, 12))); setActiveMode(MODE.PET); hud('Pet updated'); });
$('feedPet').onclick = act(async () => { await write(packet(CMD.FEED)); setActiveMode(MODE.PET); hud('Fed'); });
$('sendCard').onclick = act(async () => { await write(packet(CMD.CARD, hexRgb($('cAcc').value), ascii($('cName').value).slice(0, 20) + '\x1F' + ascii($('cSub').value).slice(0, 24))); setActiveMode(MODE.CARD); hud('Card shown'); });

$('nfcUrl').value = localStorage.getItem('nfc_url') || '';
$('writeNfc').onclick = act(async () => {
  if (!('NDEFReader' in window)) throw new Error('NFC writing needs Chrome on Android.');
  const url = $('nfcUrl').value.trim(); if (!/^https?:\/\//.test(url)) throw new Error('Enter a full link starting with https://');
  localStorage.setItem('nfc_url', url);
  hud('Hold the tag to the back of your phone…');
  await new NDEFReader().write({ records: [{ recordType: 'url', data: url }] });
  hud('Tag written');
});

// ─────────── Settings ───────────
let brightT;
function brightLabel(){ $('brightVal').textContent = Math.round($('bright').value / 255 * 100) + '%'; }
brightLabel();
$('bright').addEventListener('input', () => { brightLabel(); clearTimeout(brightT); brightT = setTimeout(() => write(packet(CMD.BRIGHT, [+$('bright').value])).catch(() => {}), 120); });
const OPT_IDS = ['swap', 'optSound', 'optShake', 'optChime', 'tiltX', 'tiltY', 'tiltSwap'];
OPT_IDS.forEach(id => { const v = localStorage.getItem('o_' + id); if (v !== null) $(id).checked = v === '1'; });
{ const v = localStorage.getItem('o_sleep'); if (v !== null) $('sleep').value = v; }
const sendSettings = act(async () => {
  OPT_IDS.forEach(id => localStorage.setItem('o_' + id, $(id).checked ? '1' : '0'));
  localStorage.setItem('o_sleep', $('sleep').value);
  const flags = ($('optSound').checked ? 1 : 0) | ($('optShake').checked ? 2 : 0) | ($('optChime').checked ? 4 : 0);
  const tilt = ($('tiltX').checked ? 1 : 0) | ($('tiltY').checked ? 2 : 0) | ($('tiltSwap').checked ? 4 : 0);
  await write(packet(CMD.SETTINGS, [...i16(+$('sleep').value), $('swap').checked ? 1 : 0, flags, tilt]));
  hud('Saved');
});
$('sleep').onchange = sendSettings; OPT_IDS.forEach(id => $(id).onchange = sendSettings);
$('syncTime').onclick = act(async () => { await syncTime(); hud('Time synced'); });
$('findMe').onclick = act(() => write(packet(CMD.BUZZ, [5])));

// ─────────── Remember inputs ───────────
['txt', 'mTitle', 'mArtist', 'petName', 'cName', 'cSub', 'clipName'].forEach(id => {
  const el = $(id), v = localStorage.getItem('f_' + id); if (v !== null) el.value = v;
  el.addEventListener('change', () => localStorage.setItem('f_' + id, el.value));
});

// ─────────── Clock look ───────────
function setLook(bg, face, send = true){
  ui.bg = bg; ui.face = face;
  document.querySelectorAll('#bgs .bgt').forEach(b => b.classList.toggle('on', +b.dataset.v === bg));
  document.querySelectorAll('#faceSeg button').forEach(b => b.classList.toggle('on', +b.dataset.v === face));
  if (send) { setActiveMode(MODE.CLOCK); if (rxChar) write(packet(CMD.LOOK, [bg, face])).catch(() => {}); }
}
document.querySelectorAll('#bgs .bgt').forEach(b => b.onclick = () => setLook(+b.dataset.v, ui.face));
document.querySelectorAll('#faceSeg button').forEach(b => b.onclick = () => setLook(ui.bg, +b.dataset.v));

// ─────────── Toys ───────────
function setToy(k, send = true){
  ui.toy = k;
  document.querySelectorAll('#toySeg button').forEach(b => b.classList.toggle('on', +b.dataset.v === k));
  if (send) { setActiveMode(MODE.TOY); if (rxChar) write(packet(CMD.TOY, [k, 0])).catch(() => {}); }
}
document.querySelectorAll('#toySeg button').forEach(b => b.onclick = () => setToy(+b.dataset.v));
$('rollToy').onclick = act(async () => { await write(packet(CMD.TOY, [ui.toy, 1])); setActiveMode(MODE.TOY); hud(['Asking the ball…', 'Rolling…', 'Flipping…'][ui.toy]); });

// ─────────── Timer ───────────
const startTimer = act(async min => {
  if (!(min > 0) || min > 1080) throw new Error('Pick between 1 minute and 18 hours.');
  await write(packet(CMD.TIMER, i16(Math.round(min * 60)))); hud(`Timer set for ${min} min`);
});
document.querySelectorAll('#timerPresets button').forEach(b => b.onclick = () => startTimer(+b.dataset.min));
$('startTimer').onclick = () => startTimer(+$('timerMin').value);
$('cancelTimer').onclick = act(async () => { await write(packet(CMD.TIMER, [0, 0])); hud('Timer cancelled'); });
