// RC Lap Timer – client. Data over WebSocket (port 81), HTTP polling as fallback.

const $ = id => document.getElementById(id);
const fmt = ms => (ms / 1000).toFixed(3);

let S = null;          // latest state from the server
let cur = 0, at = 0;   // current lap time from the server and when it arrived (extrapolated locally)
let seen = -1;         // lap count in the previous state, to detect a new lap
let cfgLoaded = false; // settings form is filled only once so it doesn't overwrite typing
let sound = false, ac = null;
let sock = null, pollT = null;

// ---------------------------------------------------------------- sound and vibration
function toggleSound() {
  sound = !sound;
  $('snd').textContent = (sound ? '🔊' : '🔇') + ' Sound';
  if (sound && !ac) ac = new (window.AudioContext || window.webkitAudioContext)();
}

function beep(best) {
  if (navigator.vibrate) navigator.vibrate(best ? [80, 60, 80] : 80);
  if (!sound || !ac) return;
  const o = ac.createOscillator(), g = ac.createGain();
  o.frequency.value = best ? 1320 : 880;
  g.gain.value = .2;
  o.connect(g); g.connect(ac.destination);
  o.start(); o.stop(ac.currentTime + (best ? .35 : .15));
}

// ---------------------------------------------------------------- rendering
function conn(state) {
  const [color, text] = { ws: ['var(--ok)', 'live'], poll: ['var(--gold)', 'fallback mode'], off: ['var(--acc)', 'offline'] }[state];
  $('dot').style.background = color;
  $('ctxt').textContent = text;
}

function flash(isBest) {
  const c = $('curCard');
  c.className = 'card';
  void c.offsetWidth;                                  // restart the animation
  c.className = 'card flash' + (isBest ? ' b' : '');
}

function onState(s) {
  const newLap = seen >= 0 && s.count > seen;
  seen = s.count; S = s; cur = s.cur; at = performance.now();

  const last = s.laps.length ? s.laps[s.laps.length - 1] : 0;
  const isBest = last && last === s.best;
  if (newLap) { beep(isBest); flash(isBest); }

  $('last').textContent  = last ? fmt(last) : '--';
  $('best').textContent  = s.best ? fmt(s.best) : '--';
  $('lastd').textContent = last && !isBest ? '+' + fmt(last - s.best) : ' ';
  $('avg').textContent   = s.laps.length ? 'ø ' + fmt(s.laps.reduce((a, b) => a + b, 0) / s.laps.length) : ' ';

  const first = s.count - s.laps.length + 1;          // number of the oldest lap in the list
  $('laps').innerHTML = s.laps.map((ms, i) => [first + i, ms]).reverse()
    .map(([n, ms]) => `<li class="${ms === s.best ? 'b' : ''}"><span>${n}.</span><span>${fmt(ms)}</span><span>${ms === s.best ? '★' : '+' + fmt(ms - s.best)}</span></li>`)
    .join('');

  $('ign').textContent = s.ignored;
  $('wstat').textContent = s.ssid
    ? (s.staIp ? `Connected to ${s.ssid}, IP ${s.staIp}` : `Network ${s.ssid} was not in range at startup`)
    : 'Home network not set';

  if (!cfgLoaded) {
    $('minLap').value = s.minLap / 1000;
    $('delta').value = s.delta;
    $('ssid').value = s.ssid;
    cfgLoaded = true;
  }
}

function onLive(l) {
  cur = l.cur; at = performance.now();
  $('raw').textContent = l.raw;
  $('base').textContent = l.base;
  $('beam').textContent = l.broken ? 'broken' : 'clear';
  $('mr').style.left = (l.raw / 40.95) + '%';          // ADC 0–4095 → 0–100 %
  $('mb').style.left = (l.base / 40.95) + '%';
}

function tick() {
  if (S && S.running) {
    $('cur').textContent = fmt(cur + (performance.now() - at));
    $('state').textContent = 'Lap ' + (S.count + 1);
  } else {
    $('cur').textContent = '--.---';
    $('state').textContent = 'Waiting for first pass';
  }
  requestAnimationFrame(tick);
}

// ---------------------------------------------------------------- connection
// WebSocket with automatic reconnect, HTTP polling as fallback while it's down
function connect() {
  try { sock = new WebSocket(`ws://${location.hostname}:81/`); } catch (e) { return startPoll(); }
  const t = setTimeout(() => { if (sock.readyState !== 1) sock.close(); }, 3000);
  sock.onopen = () => { clearTimeout(t); stopPoll(); conn('ws'); };
  sock.onmessage = e => { const m = JSON.parse(e.data); m.t === 'state' ? onState(m) : onLive(m); };
  sock.onclose = () => { clearTimeout(t); startPoll(); setTimeout(connect, 2000); };
}

async function poll() {
  try {
    const r = await fetch('/api/state', { cache: 'no-store' });
    onState(await r.json());
    conn('poll');
  } catch (e) { conn('off'); }
}
function startPoll() { if (!pollT) { conn('off'); poll(); pollT = setInterval(poll, 1000); } }
function stopPoll() { clearInterval(pollT); pollT = null; }

// ---------------------------------------------------------------- button actions
async function act(a, question) {
  if (question && !confirm(question)) return;
  await fetch('/api/' + a, { method: 'POST' });
  if (pollT) poll();
}

async function save() {
  const p = new URLSearchParams({ minLap: Math.round($('minLap').value * 1000), delta: $('delta').value });
  await fetch('/api/config?' + p, { method: 'POST' });
  cfgLoaded = false;
  if (pollT) poll();
}

async function wifi(forget) {
  if (forget && !confirm('Forget home Wi-Fi?')) return;
  const p = new URLSearchParams(forget ? { ssid: '', pass: '' } : { ssid: $('ssid').value, pass: $('pass').value });
  await fetch('/api/wifi?' + p, { method: 'POST' });
  $('wstat').textContent = 'Restarting… reconnect in a few seconds.';
}

connect();
tick();
