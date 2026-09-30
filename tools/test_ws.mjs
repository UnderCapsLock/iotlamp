// LightPlus WebSocket test suite — run from the repo root:
//   node tools/test_ws.mjs [device-ip]
// Exits non-zero if any check fails.

import { readFileSync } from 'node:fs';

const IP = process.argv[2] || '192.168.0.5';
const BASE = `http://${IP}`;
const results = [];
const log = (name, ok, detail = '') => {
  results.push({ name, ok });
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? ' -- ' + detail : ''}`);
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

let served = null;
try {
  const res = await fetch(BASE + '/', { signal: AbortSignal.timeout(5000) });
  served = await res.text();
  log('HTTP GET /', res.status === 200, `status=${res.status}`);
  log('Dashboard shell served', served.includes('LightPlus'), `${served.length} bytes`);
  const local = readFileSync('data/index.html', 'utf8');
  log('LittleFS in sync with repo', served === local,
      served === local ? 'byte-identical' : `served=${served.length} local=${local.length}`);
} catch (e) {
  log('HTTP GET /', false, e.message);
}

const ws = new WebSocket(`ws://${IP}/ws`);
let gotState = false;
let lastState = null;
const pendingQ = [];
let ackCount = 0;

const sendCmd = (cmd, timeoutMs = 4000) => new Promise((resolve, reject) => {
  const json = typeof cmd === 'string' ? cmd : JSON.stringify(cmd);
  const cmdName = (typeof cmd === 'object' && cmd !== null) ? cmd.cmd : undefined;
  const entry = { cmdName, resolve, reject,
    timer: setTimeout(() => {
      const i = pendingQ.indexOf(entry);
      if (i >= 0) pendingQ.splice(i, 1);
      reject(new Error('timeout: ' + json));
    }, timeoutMs) };
  pendingQ.push(entry);
  ws.send(json);
});

ws.onmessage = (e) => {
  let msg;
  try { msg = JSON.parse(e.data); } catch { return; }
  if (msg.result !== undefined || msg.error !== undefined) {
    ackCount++;
    const idx = msg.cmd !== undefined ? pendingQ.findIndex((en) => en.cmdName === msg.cmd) : 0;
    if (idx === -1 || pendingQ.length === 0) return;
    const entry = pendingQ.splice(idx, 1)[0];
    clearTimeout(entry.timer);
    entry.resolve(msg);
    return;
  }
  if (msg.dark !== undefined) {
    gotState = true;
    lastState = msg;
  }
};

await new Promise((resolve, reject) => {
  ws.onopen = resolve;
  ws.onerror = () => reject(new Error('ws connection failed'));
  setTimeout(() => reject(new Error('ws connect timeout')), 5000);
});
log('WebSocket connect', true, `ws://${IP}/ws`);
await sleep(2500);
log('State broadcast received', gotState);

if (lastState) {
  const s = lastState;
  log('State fields valid',
      s.brightness >= 0 && s.brightness <= 255 && s.ldr_raw >= 0 && s.ldr_raw <= 4095 &&
      ['auto', 'force_on', 'force_off'].includes(s.mode) &&
      ['none', 'moving', 'stationary'].includes(s.presence),
      `mode=${s.mode} bri=${s.brightness} pres=${s.presence} ldr=${s.ldr_raw}`);
  log('NTP synced', s.timestamp > 0, `ts=${s.timestamp}`);
  log('Firmware version reported', typeof s.fw === 'string' && s.fw.length > 0, `fw=${s.fw}`);
  console.log(`  BASELINE: mode=${s.mode} thr=${s.dark_threshold} bri=${s.brightness} ` +
              `radar_ok=${s.radar_ok} frames=${s.radar_frames} uptime=${s.uptime_s}s`);
}

const expectErr = async (name, cmd) => {
  try { const ack = await sendCmd(cmd); log(name, ack.error !== undefined, ack.error || ''); }
  catch (e) { log(name, false, e.message); }
};
const expectOk = async (name, cmd) => {
  try { const ack = await sendCmd(cmd); log(name, ack.result === 'ok', ack.cmd || ''); }
  catch (e) { log(name, false, e.message); }
};

await expectErr('reject invalid json', 'not json {{{');
await expectErr('reject missing cmd', {});
await expectErr('reject unknown cmd', { cmd: 'nonsense' });
await expectErr('reject bad override mode', { cmd: 'override', mode: 'banana' });
await expectErr('reject brightness > 255', { cmd: 'set_brightness', value: 300 });
await expectErr('reject cct < 2000', { cmd: 'set_cct', value: 1000 });
await expectErr('reject rgb > 255', { cmd: 'set_rgb', r: 999, g: 0, b: 0 });
await expectErr('reject sleep > 120 min', { cmd: 'start_sleep_timer', minutes: 200 });
await expectErr('reject threshold > 4095', { cmd: 'set_dark_threshold', value: 5000 });
await expectErr('reject presence hold 0', { cmd: 'set_presence', hold_s: 0, lost: 'off' });
await expectErr('reject min distance > 600', { cmd: 'set_min_distance', value: 700 });
await expectErr('reject max distance > 600', { cmd: 'set_max_distance', value: 700 });

const baseThr = lastState.dark_threshold;
const baseHold = lastState.ph_s || 6;
const baseLost = lastState.pl_act === 1 ? 'dim' : 'off';
const baseMinDist = lastState.min_dist || 0;
const baseMaxDist = lastState.max_dist || 0;

await expectOk('override force_on', { cmd: 'override', mode: 'force_on' });
await sleep(2500);
log('force_on applied', lastState.mode === 'force_on', `bri=${lastState.brightness}`);
await expectOk('set_brightness 64', { cmd: 'set_brightness', value: 64 });
await sleep(2500);
log('brightness applied', lastState.brightness === 64);
await expectOk('set_cct 4000', { cmd: 'set_cct', value: 4000 });
await sleep(2500);
log('cct applied', lastState.cct === 4000 && lastState.color_src === 'cct');
await expectOk('set_rgb 200,50,50', { cmd: 'set_rgb', r: 200, g: 50, b: 50 });
await sleep(2500);
log('rgb applied', lastState.color_src === 'rgb' && lastState.rgb_r === 200);
await expectOk('set_cct 2700', { cmd: 'set_cct', value: 2700 });
await expectOk('set_brightness 255', { cmd: 'set_brightness', value: 255 });
await expectOk('override auto', { cmd: 'override', mode: 'auto' });
await sleep(2500);
log('auto restored', lastState.mode === 'auto');
await expectOk('sleep timer start', { cmd: 'start_sleep_timer', minutes: 1 });
await sleep(2500);
log('sleep timer running', lastState.sleep_timer_s > 0 && lastState.sleep_timer_s <= 60, `${lastState.sleep_timer_s}s`);
await expectOk('sleep timer cancel', { cmd: 'cancel_sleep_timer' });
await sleep(2500);
log('sleep timer cancelled', lastState.sleep_timer_s === 0);
await expectOk('clear manual flags (thr=0)', { cmd: 'set_dark_threshold', value: 0 });
await sleep(2500);
await expectOk(`restore threshold ${baseThr}`, { cmd: 'set_dark_threshold', value: baseThr });
let thrRestored = false;
for (let i = 0; i < 10 && !thrRestored; i++) {
  await sleep(500);
  thrRestored = lastState.dark_threshold === baseThr;
}
log('threshold restored', thrRestored);
await expectOk('set presence behaviour', { cmd: 'set_presence', hold_s: 10, lost: 'off' });
await sleep(2500);
log('presence behaviour applied', lastState.ph_s === 10 && lastState.pl_act === 0);
await expectOk('restore presence behaviour', { cmd: 'set_presence', hold_s: baseHold, lost: baseLost });
await sleep(2500);
log('presence behaviour restored', lastState.ph_s === baseHold);
await expectOk('set min distance 50', { cmd: 'set_min_distance', value: 50 });
await sleep(2500);
log('min distance applied', lastState.min_dist === 50);
await expectOk(`restore min distance ${baseMinDist}`, { cmd: 'set_min_distance', value: baseMinDist });
let minDistOk = false;
for (let i = 0; i < 10 && !minDistOk; i++) {
  await sleep(500);
  minDistOk = lastState.min_dist === baseMinDist;
}
log('min distance restored', minDistOk);
await expectOk('set max distance 200', { cmd: 'set_max_distance', value: 200 });
await sleep(2500);
log('max distance applied', lastState.max_dist === 200);
await expectOk(`restore max distance ${baseMaxDist}`, { cmd: 'set_max_distance', value: baseMaxDist });
let maxDistOk = false;
for (let i = 0; i < 10 && !maxDistOk; i++) {
  await sleep(500);
  maxDistOk = lastState.max_dist === baseMaxDist;
}
log('max distance restored', maxDistOk);

const fails = results.filter((r) => !r.ok).length;
console.log(`\n${results.length - fails}/${results.length} passed, ${fails} failed, ${ackCount} acks`);
ws.close();
process.exit(fails ? 1 : 0);
