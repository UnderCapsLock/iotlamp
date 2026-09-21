const $ = (id) => document.getElementById(id);

const currentState = {
  dark: false, presence: 'none', mode: 'auto', brightness: 0, cct: 2700,
  dark_threshold: 550, ldr_raw: 0, energy_kwh: 0, cost_myr: 0, color_src: 'cct',
  rgb_r: 0, rgb_g: 0, rgb_b: 0, sleep_timer_s: 0, uptime_s: 0, timestamp: 0,
  in_window: false, fw: '', bs_h: 22, bs_m: 0, bs_d: 3600, ws_h: 6, ws_m: 0, ws_d: 1800,
  radar_ok: false, radar_frames: 0, radar_status: 255,
  radar_rx: 0, radar_mdist: 0, radar_sdist: 0, radar_msig: 0, radar_ssig: 0, radar_out: 0
};

let ws = null;
let reconnectTimer = null;
let lastHeartbeat = 0;
let manualClose = false;
let optimisticMode = null;
let sliderDragging = false;
let briDebounce = null;
let cctDebounce = null;
let offlineToasted = false;
let cmdLog = [];
let ldrHistory = [];
let targetLvl = 0;
let shownLvl = 0;
let lastBrightness = 255;
const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
const MAX_LDR_POINTS = 60;
const RATE_MYR = 0.27;

const SCENES = [
  { name: 'Reading', bri: 255, cct: 4000, tip: 'Bright, neutral light for books and screens' },
  { name: 'Relax', bri: 90, cct: 2700, tip: 'Soft warm glow for winding down' },
  { name: 'Movie', bri: 30, cct: 2200, tip: 'Very dim warm light so your eyes stay adjusted' },
  { name: 'Focus', bri: 255, cct: 5000, tip: 'Cool daylight for work and concentration' }
];

const GATE_LEVELS = [
  { name: 'Very low', moving: 70, stationary: 80, tip: 'Closest range only — fewest false triggers' },
  { name: 'Low', moving: 55, stationary: 65, tip: 'Short range — good for small rooms with reflections' },
  { name: 'Balanced', moving: 40, stationary: 50, tip: 'Recommended setting for most rooms' },
  { name: 'High', moving: 25, stationary: 35, tip: 'Picks up subtle movement from further away' },
  { name: 'Very high', moving: 10, stationary: 20, tip: 'Most sensitive — may react to fans or curtains' }
];
let gateLevel = 2;

/* ---------- toasts / log ---------- */

function toast(text, kind = 'ok', ms = 2600) {
  const el = document.createElement('div');
  el.className = 'toast ' + kind;
  el.textContent = text;
  const box = $('toasts');
  box.appendChild(el);
  while (box.children.length > 3) box.removeChild(box.firstChild);
  setTimeout(() => {
    el.classList.add('out');
    setTimeout(() => el.remove(), 300);
  }, ms);
}

function logCmd(dir, text) {
  cmdLog.push({ dir, text, ts: new Date().toLocaleTimeString() });
  if (cmdLog.length > 20) cmdLog.shift();
  renderLog();
}

function renderLog() {
  $('logBody').innerHTML = cmdLog.map((e) =>
    '<div class="log-entry ' + e.dir + '">' + e.ts + ' ' + e.text + '</div>').join('');
}

/* ---------- websocket ---------- */

function setConn(state, text) {
  $('connDot').className = 'dot ' + state;
  $('connText').textContent = text;
  document.body.classList.toggle('offline', state === 'offline');
  if (state === 'online') offlineToasted = false;
}

function connectWS(ip) {
  if (ws) { try { ws.close(); } catch {} ws = null; }
  if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
  manualClose = false;
  setConn('stale', 'Connecting…');

  try { ws = new WebSocket('ws://' + ip + '/ws'); }
  catch { setConn('offline', 'Bad address'); scheduleReconnect(ip); return; }

  ws.onopen = () => { setConn('online', 'Online'); lastHeartbeat = Date.now(); };

  ws.onmessage = (e) => {
    let msg;
    try { msg = JSON.parse(e.data); } catch { logCmd('err', 'bad json'); return; }
    if (msg.result !== undefined || msg.error !== undefined) {
      if (msg.error) { logCmd('err', 'error: ' + msg.error); toast(msg.error, 'err', 3400); }
      else { logCmd('recv', 'ack: ' + (msg.cmd || '')); }
      return;
    }
    if (msg.dark !== undefined) { lastHeartbeat = Date.now(); handleState(msg); }
  };

  ws.onclose = () => {
    setConn('offline', 'Disconnected');
    ws = null;
    if (!manualClose) scheduleReconnect(ip);
  };
  ws.onerror = () => { setConn('offline', 'Error'); };
}

function scheduleReconnect(ip) {
  if (reconnectTimer) return;
  reconnectTimer = setTimeout(() => { reconnectTimer = null; connectWS(ip); }, 4000);
}

function sendObj(obj, toastLabel) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    toast('Not connected', 'err');
    return false;
  }
  ws.send(JSON.stringify(obj));
  logCmd('send', JSON.stringify(obj));
  if (toastLabel) toast(toastLabel, 'ok', 1800);
  return true;
}

/* ---------- state ---------- */

function handleState(msg) {
  for (const k of Object.keys(currentState))
    if (msg[k] !== undefined) currentState[k] = msg[k];

  if (optimisticMode !== null && msg.mode !== undefined) optimisticMode = null;
  if (msg.brightness !== undefined && msg.brightness > 0) lastBrightness = msg.brightness;

  if (msg.ldr_raw !== undefined) {
    ldrHistory.push(msg.ldr_raw);
    if (ldrHistory.length > MAX_LDR_POINTS) ldrHistory.shift();
  }

  updateUI();
}

function effectiveMode() {
  return optimisticMode || currentState.mode;
}

function fwShort(fw) {
  return fw ? 'v' + fw : '—';
}

function fmtUptime(s) {
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
  return h > 0 ? h + 'h ' + m + 'm' : m + 'm ' + (s % 60) + 's';
}

/* ---------- UI ---------- */

function updateUI() {
  const s = currentState;
  const mode = effectiveMode();
  const hasPresence = s.presence !== 'none';

  // hero
  const briPct = Math.round(s.brightness / 255 * 100);
  if (optimisticMode === null) targetLvl = s.brightness / 255;
  else if (optimisticMode === 'force_off') targetLvl = 0;
  else targetLvl = Math.max(lastBrightness / 255, 0.02);
  let heroState = 'Off';
  if (mode === 'force_on') heroState = 'On';
  else if (mode === 'force_off') heroState = 'Off';
  else if (s.brightness > 0) heroState = 'On';
  $('heroState').textContent = heroState;
  $('heroSub').textContent = (mode === 'auto' ? 'Auto' : 'Manual') +
    ' · ' + (hasPresence ? (s.presence === 'moving' ? 'movement' : 'still presence') : 'no presence');

  // mode segmented
  document.querySelectorAll('#modeSeg button').forEach((b) =>
    b.classList.toggle('active', b.dataset.mode === mode));

  // chips
  const pres = $('chipPresence');
  pres.className = 'chip ' + (hasPresence ? 'good' : '');
  pres.querySelector('.chip-v').textContent = hasPresence ? (s.presence === 'moving' ? 'Moving' : 'Still') : 'None';

  const light = $('chipLight');
  light.className = 'chip ' + (s.dark ? 'warn' : '');
  light.querySelector('.chip-v').textContent = s.dark ? 'Dark' : 'Bright';

  const sched = $('chipSchedule');
  sched.className = 'chip ' + (s.in_window ? 'good' : '');
  sched.querySelector('.chip-v').textContent = s.in_window ? 'Active' : 'Idle';

  $('chipTime').querySelector('.chip-v').textContent = s.timestamp > 0
    ? new Date(s.timestamp * 1000).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
    : '—:—';

  // brightness
  if (!sliderDragging) {
    animateVal($('briVal'), s.brightness, (v) => Math.round(v));
    animateVal($('briPct'), briPct, (v) => Math.round(v) + '%');
    $('briSlider').value = s.brightness;
    $('briSlider').style.setProperty('--fill', briPct + '%');
  }

  // color
  if (s.color_src === 'rgb') {
    $('cctVal').textContent = 'RGB';
    $('colorHint').textContent = '(' + s.rgb_r + ',' + s.rgb_g + ',' + s.rgb_b + ')';
    $('cctSwatch').style.background = 'rgb(' + s.rgb_r + ',' + s.rgb_g + ',' + s.rgb_b + ')';
  } else {
    $('cctVal').textContent = s.cct;
    $('colorHint').textContent = '';
    $('cctSwatch').style.background = kelvinToCSS(s.cct);
    if (document.activeElement !== $('cctSlider')) {
      $('cctSlider').value = s.cct;
      $('cctSlider').style.setProperty('--fill', Math.round((s.cct - 2000) / 4500 * 100) + '%');
    }
  }
  document.querySelectorAll('.preset[data-k]').forEach((b) =>
    b.classList.toggle('active', s.color_src === 'cct' && +b.dataset.k === s.cct));

  // energy
  animateVal($('kwhVal'), s.energy_kwh || 0, (v) => v.toFixed(5), 900);
  animateVal($('costVal'), s.cost_myr || 0, (v) => v.toFixed(4), 900);

  // sleep timer
  const tm = s.sleep_timer_s || 0;
  $('timerDisplay').textContent = tm > 0
    ? Math.floor(tm / 60) + ':' + String(tm % 60).padStart(2, '0')
    : '--:--';
  $('timerState').textContent = tm > 0 ? 'active' : 'idle';

  // ldr
  $('ldrLive').textContent = s.ldr_raw;
  $('ldrThresh').textContent = s.dark_threshold;
  $('setThreshBtn').disabled = s.ldr_raw === 0;

  // diagnostics
  $('dRadar').textContent = s.radar_ok ? 'Working' : 'Not responding';
  $('dDist').textContent = fmtDist(s.radar_mdist || s.radar_sdist);
  $('dSig').textContent = (s.radar_msig || s.radar_ssig) ? (s.radar_msig + ' / ' + s.radar_ssig) : '—';
  $('dUptime').textContent = fmtUptime(s.uptime_s || 0);
  $('dFw').textContent = fwShort(s.fw);

  // schedule timeline
  const bs = (s.bs_h || 0) * 3600 + (s.bs_m || 0) * 60;
  const wsS = (s.ws_h || 0) * 3600 + (s.ws_m || 0) * 60;
  const band = (el, start, dur) => {
    el.style.left = (start / 86400 * 100) + '%';
    el.style.width = Math.max(0.6, Math.min(100 - start / 86400 * 100, dur / 86400 * 100)) + '%';
  };
  band($('tlBed'), bs, s.bs_d || 0);
  band($('tlWake'), wsS, s.ws_d || 0);
  const nowDate = s.timestamp > 0 ? new Date(s.timestamp * 1000) : new Date();
  $('tlNow').style.left = ((nowDate.getHours() * 3600 + nowDate.getMinutes() * 60) / 86400 * 100) + '%';
  const hhmm = (h, m) => String(h).padStart(2, '0') + ':' + String(m).padStart(2, '0');
  $('schedText').textContent = 'Bedtime ' + hhmm(s.bs_h || 0, s.bs_m || 0) + ' +' + Math.round((s.bs_d || 0) / 60) +
    'm · Wake ' + hhmm(s.ws_h || 0, s.ws_m || 0) + ' +' + Math.round((s.ws_d || 0) / 60) + 'm';

  // night mode
  const hour = nowDate.getHours();
  document.body.classList.toggle('night', hour >= 23 || hour < 6);

  // ambient tint follows the lamp color
  const col = s.color_src === 'rgb' ? { r: s.rgb_r, g: s.rgb_g, b: s.rgb_b } : kelvinToRGB(s.cct);
  const dim = 0.25 + 0.75 * (s.brightness / 255);
  document.documentElement.style.setProperty('--lamp-rgb',
    Math.round(col.r * dim) + ',' + Math.round(col.g * dim) + ',' + Math.round(col.b * dim));
  $('fwVer').textContent = fwShort(s.fw);
  $('footerIp').textContent = $('espIP').value || '—';

  drawLDRChart();
}

/* ---------- charts ---------- */

function kelvinToRGB(kelvin) {
  const t = kelvin / 100;
  let r, g, b;
  if (t <= 66) { r = 255; g = 99.4708025861 * Math.log(t) - 161.1195681661; }
  else { r = 329.698727446 * Math.pow(t - 60, -0.1332047592); g = 288.1221695283 * Math.pow(t - 60, -0.0755148492); }
  if (t <= 66) { b = t <= 19 ? 0 : 138.5177312231 * Math.log(t - 10) - 305.0447927307; }
  else { b = 255; }
  const c = (v) => Math.round(Math.max(0, Math.min(255, v)));
  return { r: c(r), g: c(g), b: c(b) };
}

function kelvinToCSS(kelvin) {
  const { r, g, b } = kelvinToRGB(kelvin);
  return 'rgb(' + r + ',' + g + ',' + b + ')';
}

function fmtDist(cm) {
  if (!cm) return '—';
  return cm >= 100 ? (cm / 100).toFixed(1) + ' m' : Math.round(cm) + ' cm';
}

function animateVal(el, target, fmt, dur = 400) {
  const from = parseFloat(el.dataset.v || '0');
  if (from === target) { el.textContent = fmt(target); return; }
  el.dataset.v = target;
  const t0 = performance.now();
  const step = (t) => {
    const p = Math.min(1, (t - t0) / dur);
    const v = from + (target - from) * (1 - Math.pow(1 - p, 3));
    el.textContent = fmt(v);
    if (p < 1) requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
}

function drawRadar() {
  const canvas = $('radarCanvas');
  if (!canvas) return;
  const rect = canvas.getBoundingClientRect();
  if (rect.width === 0) return;
  const dpr = window.devicePixelRatio || 1;
  canvas.width = rect.width * dpr;
  canvas.height = rect.height * dpr;
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const w = rect.width, h = rect.height;
  ctx.clearRect(0, 0, w, h);

  const cx = w / 2, cy = h - 16;
  const maxR = Math.min(w / 2 - 20, h - 30);
  const MAX_CM = 300;
  const s = currentState;

  [1, 2, 3].forEach((m) => {
    const r = maxR * m / 3;
    ctx.strokeStyle = m === 3 ? 'rgba(139, 147, 167, 0.30)' : 'rgba(139, 147, 167, 0.22)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(cx, cy, r, Math.PI, 2 * Math.PI);
    ctx.stroke();
    ctx.fillStyle = 'rgba(139, 147, 167, 0.55)';
    ctx.font = '10px ui-monospace, monospace';
    ctx.fillText(m + 'm', cx + r * 0.03 + 4, cy - r + 3);
  });

  const t = (performance.now() % 2600) / 2600;
  const a = Math.PI + t * Math.PI;
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.arc(cx, cy, maxR, a - 0.45, a);
  ctx.closePath();
  ctx.fillStyle = 'rgba(245, 167, 66, 0.08)';
  ctx.fill();
  ctx.strokeStyle = 'rgba(245, 167, 66, 0.75)';
  ctx.lineWidth = 2;
  ctx.shadowColor = 'rgba(245, 167, 66, 0.9)';
  ctx.shadowBlur = 6;
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(cx + Math.cos(a) * maxR, cy + Math.sin(a) * maxR);
  ctx.stroke();
  ctx.shadowBlur = 0;

  ctx.fillStyle = 'rgba(139, 147, 167, 0.8)';
  ctx.beginPath();
  ctx.arc(cx, cy, 3, 0, 2 * Math.PI);
  ctx.fill();

  const addTarget = (cm, color, sig) => {
    if (!cm) return;
    const r = Math.min(1, cm / MAX_CM) * maxR;
    const x = cx, y = cy - Math.max(12, r);
    const pulse = 1 + 0.18 * Math.sin(performance.now() / 300);
    ctx.fillStyle = color;
    ctx.shadowColor = color;
    ctx.shadowBlur = 14;
    ctx.beginPath();
    ctx.arc(x, y, (5 + 2.5 * (sig / 100)) * pulse, 0, 2 * Math.PI);
    ctx.fill();
    ctx.shadowBlur = 0;
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.35)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(x, y, 8, 0, 2 * Math.PI);
    ctx.stroke();
  };

  if (s.radar_status & 1) addTarget(s.radar_mdist, '#f5a742', s.radar_msig);
  if (s.radar_status & 2) addTarget(s.radar_sdist, '#5aa9ff', s.radar_ssig);

  const parts = [];
  if ((s.radar_status & 1) && s.radar_mdist) parts.push('Moving at ' + fmtDist(s.radar_mdist));
  if ((s.radar_status & 2) && s.radar_sdist) parts.push('Still at ' + fmtDist(s.radar_sdist));
  $('radarLabel').textContent = parts.length ? parts.join(' · ') : (s.radar_ok ? 'No one detected' : 'Sensor offline');
}

function drawLDRChart() {
  const canvas = $('ldrChart');
  const rect = canvas.getBoundingClientRect();
  if (rect.width === 0) return;
  const dpr = window.devicePixelRatio || 1;
  canvas.width = rect.width * dpr;
  canvas.height = rect.height * dpr;
  const ctx = canvas.getContext('2d');
  ctx.scale(dpr, dpr);
  const w = rect.width, h = rect.height;
  ctx.clearRect(0, 0, w, h);

  ctx.strokeStyle = '#1e2330';
  ctx.lineWidth = 1;
  for (let y = 0; y <= 4; y++) {
    const py = (h / 4) * y + 0.5;
    ctx.beginPath(); ctx.moveTo(0, py); ctx.lineTo(w, py); ctx.stroke();
  }

  const thr = currentState.dark_threshold || 550;
  const thrY = h - (thr / 4095) * h;
  ctx.strokeStyle = 'rgba(255, 93, 93, 0.5)';
  ctx.setLineDash([4, 6]);
  ctx.beginPath(); ctx.moveTo(0, thrY); ctx.lineTo(w, thrY); ctx.stroke();
  ctx.setLineDash([]);

  if (ldrHistory.length < 2) return;

  const stepX = w / (MAX_LDR_POINTS - 1);
  const offset = MAX_LDR_POINTS - ldrHistory.length;
  const pts = ldrHistory.map((v, i) => [(offset + i) * stepX, h - (v / 4095) * h]);

  ctx.beginPath();
  pts.forEach(([x, y], i) => { i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y); });
  ctx.lineTo(pts[pts.length - 1][0], h);
  ctx.lineTo(pts[0][0], h);
  ctx.closePath();
  const fill = ctx.createLinearGradient(0, 0, 0, h);
  fill.addColorStop(0, 'rgba(90, 169, 255, 0.28)');
  fill.addColorStop(1, 'rgba(90, 169, 255, 0)');
  ctx.fillStyle = fill;
  ctx.fill();

  ctx.beginPath();
  pts.forEach(([x, y], i) => { i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y); });
  ctx.strokeStyle = '#5aa9ff';
  ctx.lineWidth = 2;
  ctx.stroke();
}

/* ---------- heartbeat ---------- */

setInterval(() => {
  if (!lastHeartbeat) return;
  const ago = Math.floor((Date.now() - lastHeartbeat) / 1000);
  if (ago > 15) {
    setConn('offline', 'Offline');
    if (!offlineToasted) { offlineToasted = true; toast('Connection lost — reconnecting', 'err', 3200); }
  } else if (ago > 5) {
    setConn('stale', 'Last seen ' + ago + 's ago');
  } else if (ws && ws.readyState === WebSocket.OPEN) {
    setConn('online', 'Online');
  }
}, 2000);

/* ---------- orb animation ---------- */

function orbTick() {
  const diff = targetLvl - shownLvl;
  if (reduceMotion) shownLvl = targetLvl;
  else if (Math.abs(diff) < 0.003) shownLvl = targetLvl;
  else shownLvl += Math.sign(diff) * Math.min(Math.abs(diff) * 0.22, 0.02);
  const lvl = shownLvl;
  const glow = $('orbGlow'), core = $('orbCore');
  glow.style.opacity = (Math.pow(lvl, 0.75) * 0.95).toFixed(3);
  glow.style.transform = 'scale(' + (0.85 + 0.3 * lvl).toFixed(3) + ')';
  core.style.opacity = (0.06 + 0.94 * lvl).toFixed(3);
  drawRadar();
  requestAnimationFrame(orbTick);
}

/* ---------- bindings ---------- */

document.querySelectorAll('#modeSeg button').forEach((b) =>
    b.addEventListener('click', () => {
      optimisticMode = b.dataset.mode;
      updateUI();
      sendObj({ cmd: 'override', mode: b.dataset.mode },
        b.dataset.mode === 'auto' ? 'Auto' : b.dataset.mode === 'force_on' ? 'On' : 'Off');
    }));

const briSlider = $('briSlider');
briSlider.addEventListener('pointerdown', () => { sliderDragging = true; });
window.addEventListener('pointerup', () => {
  if (sliderDragging) { sliderDragging = false; updateUI(); }
});
briSlider.addEventListener('input', () => {
  const v = +briSlider.value;
  targetLvl = v / 255;
  $('briVal').textContent = v;
  $('briVal').dataset.v = v;
  $('briPct').textContent = Math.round(v / 255 * 100) + '%';
  $('briPct').dataset.v = Math.round(v / 255 * 100);
  briSlider.style.setProperty('--fill', Math.round(v / 255 * 100) + '%');
  clearTimeout(briDebounce);
  briDebounce = setTimeout(() => sendObj({ cmd: 'set_brightness', value: +briSlider.value }), 200);
});

const cctSlider = $('cctSlider');
cctSlider.addEventListener('input', () => {
  const v = +cctSlider.value;
  $('cctVal').textContent = v;
  $('cctSwatch').style.background = kelvinToCSS(v);
  cctSlider.style.setProperty('--fill', Math.round((v - 2000) / 4500 * 100) + '%');
  clearTimeout(cctDebounce);
  cctDebounce = setTimeout(() => sendObj({ cmd: 'set_cct', value: +cctSlider.value }), 200);
});

document.querySelectorAll('.preset[data-k]').forEach((b) =>
  b.addEventListener('click', () => sendObj({ cmd: 'set_cct', value: +b.dataset.k }, 'Color set')));

$('rgbPicker').addEventListener('input', () => {
  const hex = $('rgbPicker').value;
  $('cctSwatch').style.background = hex;
  $('cctVal').textContent = 'RGB';
  $('colorHint').textContent = hex.toUpperCase();
});

$('rgbPicker').addEventListener('change', () => {
  const hex = $('rgbPicker').value;
  const r = parseInt(hex.slice(1, 3), 16), g = parseInt(hex.slice(3, 5), 16), b = parseInt(hex.slice(5, 7), 16);
  sendObj({ cmd: 'set_rgb', r, g, b }, 'Custom color set');
});

$('sleepStart').addEventListener('click', () =>
  sendObj({ cmd: 'start_sleep_timer', minutes: +$('sleepMins').value }, 'Sleep timer started'));
$('sleepCancel').addEventListener('click', () =>
  sendObj({ cmd: 'cancel_sleep_timer' }, 'Sleep timer cancelled'));

$('resetEnergy').addEventListener('click', () => {
  if (confirm('Reset energy tracking to zero?')) sendObj({ cmd: 'reset_energy' }, 'Energy reset');
});

$('setThreshBtn').addEventListener('click', () =>
  sendObj({ cmd: 'set_dark_threshold', value: currentState.ldr_raw }, 'Threshold updated'));

$('saveBedtime').addEventListener('click', () => {
  const start = $('bedtimeStart').value.split(':').map(Number);
  const dur = +$('bedtimeDur').value * 60;
  sendObj({ cmd: 'set_bedtime', start_h: start[0], start_m: start[1], duration_s: dur }, 'Bedtime saved');
});

$('saveWake').addEventListener('click', () => {
  const start = $('wakeStart').value.split(':').map(Number);
  const dur = +$('wakeDur').value * 60;
  sendObj({ cmd: 'set_wake', start_h: start[0], start_m: start[1], duration_s: dur }, 'Wake ramp saved');
});

function renderScenes() {
  $('scenes').innerHTML = SCENES.map((sc, i) =>
    '<button class="scene" data-i="' + i + '" data-tip="' + sc.tip + '">' + sc.name + '</button>').join('');
  document.querySelectorAll('.scene').forEach((b) =>
    b.addEventListener('click', () => {
      const sc = SCENES[+b.dataset.i];
      optimisticMode = 'force_on';
      targetLvl = sc.bri / 255;
      sendObj({ cmd: 'override', mode: 'force_on' });
      sendObj({ cmd: 'set_brightness', value: sc.bri });
      sendObj({ cmd: 'set_cct', value: sc.cct });
      toast('Scene: ' + sc.name);
      updateUI();
    }));
}

function renderGateLevels() {
  $('gateLevels').innerHTML = GATE_LEVELS.map((g, i) =>
    '<button data-i="' + i + '" data-tip="' + g.tip + '"' + (i === gateLevel ? ' class="active"' : '') + '>' + g.name + '</button>').join('');
  document.querySelectorAll('#gateLevels button').forEach((b) =>
    b.addEventListener('click', () => {
      gateLevel = +b.dataset.i;
      document.querySelectorAll('#gateLevels button').forEach((x) => x.classList.toggle('active', x === b));
    }));
}

$('saveGates').addEventListener('click', () => {
  const g = GATE_LEVELS[gateLevel];
  sendObj({ cmd: 'set_gate_params', gate: 255, moving: g.moving, stationary: g.stationary }, 'Sensitivity saved');
});

$('settingsBtn').addEventListener('click', () => $('settingsSheet').classList.remove('hidden'));
$('closeSheet').addEventListener('click', () => $('settingsSheet').classList.add('hidden'));
$('settingsSheet').addEventListener('click', (e) => {
  if (e.target === $('settingsSheet')) $('settingsSheet').classList.add('hidden');
});

$('connectBtn').addEventListener('click', () => {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
    manualClose = true;
    ws.close();
    setConn('offline', 'Disconnected');
    return;
  }
  const ip = $('espIP').value.trim();
  if (!ip) { toast('Enter the device IP', 'err'); return; }
  localStorage.setItem('lightplus_ip', ip);
  $('settingsSheet').classList.add('hidden');
  connectWS(ip);
});

/* ---------- PWA ---------- */

let installPrompt = null;
window.addEventListener('beforeinstallprompt', (e) => {
  e.preventDefault();
  installPrompt = e;
  $('installBtn').classList.remove('hidden');
});

$('installBtn').addEventListener('click', async () => {
  if (!installPrompt) return;
  installPrompt.prompt();
  await installPrompt.userChoice;
  installPrompt = null;
  $('installBtn').classList.add('hidden');
});

if ('serviceWorker' in navigator && window.isSecureContext) {
  navigator.serviceWorker.register('sw.js').catch(() => {});
}

/* ---------- init ---------- */

(function init() {
  const ipField = $('espIP');
  const saved = localStorage.getItem('lightplus_ip');
  const host = window.location.hostname;

  if (saved) ipField.value = saved;

  if (saved && saved !== host) {
    connectWS(saved);
  } else if (host && host !== 'localhost' && host !== '127.0.0.1' && !host.endsWith('.local')) {
    ipField.value = host;
    connectWS(host);
  } else if (ipField.value) {
    connectWS(ipField.value);
  } else {
    setConn('offline', 'Not connected');
    $('settingsSheet').classList.remove('hidden');
  }

  window.addEventListener('resize', drawLDRChart);
  renderScenes();
  renderGateLevels();
  drawLDRChart();
  requestAnimationFrame(orbTick);
})();
