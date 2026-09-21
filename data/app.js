const $ = (id) => document.getElementById(id);

const currentState = {
  dark: false, presence: 'none', mode: 'auto', brightness: 0, cct: 2700,
  dark_threshold: 550, ldr_raw: 0, energy_kwh: 0, cost_myr: 0, color_src: 'cct',
  rgb_r: 0, rgb_g: 0, rgb_b: 0, sleep_timer_s: 0, uptime_s: 0, timestamp: 0,
  in_window: false, fw: '', radar_ok: false, radar_frames: 0, radar_status: 255,
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
const MAX_LDR_POINTS = 60;
const RATE_MYR = 0.27;
const MAX_WATTAGE = 3.0;

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

function radarStatusText(st) {
  switch (st) {
    case 0: return 'No target';
    case 1: return 'Moving';
    case 2: return 'Stationary';
    case 3: return 'Moving + still';
    default: return 'No data';
  }
}

/* ---------- UI ---------- */

function updateUI() {
  const s = currentState;
  const mode = effectiveMode();
  const hasPresence = s.presence !== 'none';

  // hero
  const briPct = Math.round(s.brightness / 255 * 100);
  $('orb').style.setProperty('--lvl', (s.brightness / 255).toFixed(2));
  let heroState = 'Off';
  if (mode === 'force_on') heroState = 'Forced On';
  else if (mode === 'force_off') heroState = 'Forced Off';
  else if (s.brightness > 0) heroState = 'On';
  $('heroState').textContent = heroState;
  $('heroSub').textContent = (mode === 'auto' ? 'Auto mode' : 'Manual override') +
    ' · ' + (hasPresence ? (s.presence === 'moving' ? 'movement' : 'still presence') : 'no presence');

  // mode segmented
  document.querySelectorAll('#modeSeg button').forEach((b) =>
    b.classList.toggle('active', b.dataset.mode === mode));

  // chips
  const pres = $('chipPresence');
  pres.className = 'chip ' + (hasPresence ? 'good' : '');
  pres.querySelector('.chip-v').textContent = hasPresence ? (s.presence === 'moving' ? 'Moving' : 'Stationary') : 'None';

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
  $('briVal').textContent = s.brightness;
  $('briPct').textContent = briPct + '%';
  if (!sliderDragging) {
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
  $('kwhVal').textContent = (s.energy_kwh || 0).toFixed(5);
  $('costVal').textContent = (s.cost_myr || 0).toFixed(4);
  $('wattVal').textContent = ((s.brightness / 255) * MAX_WATTAGE).toFixed(1);

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
  $('dRadar').textContent = s.radar_ok ? 'online' : 'offline';
  $('dFrames').textContent = s.radar_frames;
  $('dDist').textContent = s.radar_mdist || s.radar_sdist
    ? Math.round((s.radar_mdist || s.radar_sdist) / 100 * 100) + ' cm'
    : '—';
  $('dSig').textContent = (s.radar_msig || s.radar_ssig) ? (s.radar_msig + '/' + s.radar_ssig) : '—';
  $('dOut').textContent = s.radar_out ? 'HIGH' : 'low';
  $('dRx').textContent = s.radar_rx + ' B';
  $('dUptime').textContent = fmtUptime(s.uptime_s || 0);
  $('dFw').textContent = fwShort(s.fw);
  $('fwVer').textContent = fwShort(s.fw);
  $('footerIp').textContent = $('espIP').value || '—';

  drawLDRChart();
}

/* ---------- charts ---------- */

function kelvinToCSS(kelvin) {
  const t = kelvin / 100;
  let r, g, b;
  if (t <= 66) { r = 255; g = 99.4708025861 * Math.log(t) - 161.1195681661; }
  else { r = 329.698727446 * Math.pow(t - 60, -0.1332047592); g = 288.1221695283 * Math.pow(t - 60, -0.0755148492); }
  if (t <= 66) { b = t <= 19 ? 0 : 138.5177312231 * Math.log(t - 10) - 305.0447927307; }
  else { b = 255; }
  const c = (v) => Math.round(Math.max(0, Math.min(255, v)));
  return 'rgb(' + c(r) + ',' + c(g) + ',' + c(b) + ')';
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

  ctx.strokeStyle = '#5aa9ff';
  ctx.lineWidth = 2;
  ctx.beginPath();
  const stepX = w / (MAX_LDR_POINTS - 1);
  const offset = MAX_LDR_POINTS - ldrHistory.length;
  ldrHistory.forEach((v, i) => {
    const x = (offset + i) * stepX;
    const y = h - (v / 4095) * h;
    i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
  });
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

/* ---------- bindings ---------- */

document.querySelectorAll('#modeSeg button').forEach((b) =>
  b.addEventListener('click', () => {
    optimisticMode = b.dataset.mode;
    updateUI();
    sendObj({ cmd: 'override', mode: b.dataset.mode },
      b.dataset.mode === 'auto' ? 'Auto mode' : b.dataset.mode === 'force_on' ? 'Forced on' : 'Forced off');
  }));

const briSlider = $('briSlider');
briSlider.addEventListener('pointerdown', () => { sliderDragging = true; });
window.addEventListener('pointerup', () => {
  if (sliderDragging) { sliderDragging = false; updateUI(); }
});
briSlider.addEventListener('input', () => {
  const v = +briSlider.value;
  $('briVal').textContent = v;
  $('briPct').textContent = Math.round(v / 255 * 100) + '%';
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

$('gateMoving').addEventListener('input', () => { $('gateMovingVal').textContent = $('gateMoving').value; });
$('gateStationary').addEventListener('input', () => { $('gateStationaryVal').textContent = $('gateStationary').value; });

$('saveGates').addEventListener('click', () =>
  sendObj({ cmd: 'set_gate_params', gate: 255, moving: +$('gateMoving').value, stationary: +$('gateStationary').value }, 'Gate thresholds saved'));

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
  drawLDRChart();
})();
