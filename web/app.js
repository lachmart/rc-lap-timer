// RC Lap Timer – client. Data over WebSocket (port 81), HTTP polling as fallback.

const $ = id => document.getElementById(id);
const fmt = ms => (ms / 1000).toFixed(3);
const esc = t => String(t).replace(/[&<>"']/g, c => `&#${c.charCodeAt(0)};`);
const WS_PORT = new URLSearchParams(location.search).get('ws') || 81;   // ?ws= for the mock server

let S = null;          // latest state from the server
let cur = 0, at = 0;   // current lap time from the server and when it arrived (extrapolated locally)
let seen = -1;         // lap count in the previous state, to detect a new lap
let cfgLoaded = false; // settings form is filled only once so it doesn't overwrite typing
let beepOn = false, voiceOn = false, ac = null;
let sock = null, pollT = null;
let lastSsid = null;   // home network shown in the Wi-Fi form
let openLap = 0;       // lap number whose actions are shown in the list

// ---------------------------------------------------------------- helpers
const driverName = id => {
  const d = S && S.drivers.find(d => d.id === id);
  return d ? d.name : '';
};
const statsOf = id => S.stats.find(s => s.id === id);
const avg = (total, count) => count ? total / count : 0;

async function api(path, params = {}, question = null) {
  if (question && !confirm(question)) return false;
  let ok = false;
  try {
    const r = await fetch(`/api/${path}?${new URLSearchParams(params)}`, { method: 'POST' });
    ok = r.ok;
  } catch (e) { /* offline, the connection indicator shows it */ }
  if (pollT) poll();
  return ok;
}

// ---------------------------------------------------------------- sound, voice, vibration
function setToggle(btn, on, label) { btn.textContent = (on ? '🔊 ' : '🔇 ') + label; }

$('snd').onclick = () => {
  beepOn = !beepOn;
  setToggle($('snd'), beepOn, 'Beep');
  if (beepOn && !ac) ac = new (window.AudioContext || window.webkitAudioContext)();
};

$('voice').onclick = () => {
  if (!('speechSynthesis' in window)) { $('voice').textContent = 'No voice'; return; }
  voiceOn = !voiceOn;
  setToggle($('voice'), voiceOn, 'Voice');
  if (voiceOn) say('Voice on');                        // iOS only speaks after a tap, this unlocks it
};

function beep(best) {
  if (navigator.vibrate) navigator.vibrate(best ? [80, 60, 80] : 80);
  if (!beepOn || !ac) return;
  const o = ac.createOscillator(), g = ac.createGain();
  o.frequency.value = best ? 1320 : 880;
  g.gain.value = .2;
  o.connect(g); g.connect(ac.destination);
  o.start(); o.stop(ac.currentTime + (best ? .35 : .15));
}

function say(text) {
  if (!voiceOn) return;
  speechSynthesis.cancel();                            // a new lap interrupts the previous announcement
  const u = new SpeechSynthesisUtterance(text);
  u.lang = 'en-US';
  speechSynthesis.speak(u);
}

function announce(lap, kind) {
  const name = driverName(lap[1]);
  let text = (name ? name + ', ' : '') + (lap[0] / 1000).toFixed(2);
  if (kind === 'best') text += ', best lap';
  else if (kind === 'pb') text += ', personal best';
  say(text);
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

// 'best' = best of the session, 'pb' = driver's personal best, '' = neither
function lapKind(lap) {
  if (lap[2] || suspicious(lap)) return '';
  if (lap[0] === S.best) return 'best';
  const st = statsOf(lap[1]);
  return S.drivers.length && st && lap[0] === st.best ? 'pb' : '';
}

function renderDrivers() {
  $('chips').innerHTML = S.drivers.length
    ? S.drivers.map(d => `<button class="chip${d.id === S.driver ? ' on' : ''}" data-id="${d.id}">${esc(d.name)}</button>`).join('')
    : '<span class="hint">Nobody yet. Add drivers under Edit drivers to get a leaderboard.</span>';
  $('dlist').innerHTML = S.drivers
    .map(d => `<li><span>${esc(d.name)}</span><button data-id="${d.id}" title="Remove">✕</button></li>`).join('');
  $('addForm').hidden = S.drivers.length >= 8;
}

function renderBoard() {
  const rows = S.stats.filter(s => s.count > 0 && (s.id === 0 || driverName(s.id)))
    .sort((a, b) => a.best - b.best);
  $('boardCard').hidden = !S.drivers.length || !rows.length;
  const lead = rows.length ? rows[0].best : 0;
  $('board').innerHTML = rows.map((s, i) => `
    <tr class="${i === 0 ? 'lead' : ''} ${s.id === S.driver ? 'now' : ''}">
      <td>${i + 1}.</td>
      <td>${s.id ? esc(driverName(s.id)) : '<span class="hint">no driver</span>'}</td>
      <td>${fmt(s.best)}</td>
      <td>${i ? '+' + fmt(s.best - lead) : ''}</td>
      <td>${s.count}</td>
      <td>${fmt(avg(s.total, s.count))}</td>
    </tr>`).join('');
}

// Laps a and b (oldest-first indexes) can be joined: back to back, same driver, both counted.
function joinable(a, b) {
  const x = S.laps[a], y = S.laps[b];
  return x && y && !x[2] && !y[2] && x[1] === y[1] && y[3] === 1;
}

// Much shorter than the driver's typical lap: probably a false trigger split a lap.
function suspicious(lap) {
  const mine = S.laps.filter(l => !l[2] && l[1] === lap[1]).map(l => l[0]).sort((a, b) => a - b);
  if (mine.length < 3) return false;
  return lap[0] < mine[Math.floor(mine.length / 2)] * 0.6;
}

function renderLaps() {
  const first = S.count - S.laps.length + 1;           // number of the oldest lap in the list
  const named = S.drivers.length > 0;
  if (openLap && (openLap < first || openLap > S.count || S.laps[openLap - first][2])) openLap = 0;

  $('laps').innerHTML = S.laps.map((lap, i) => [first + i, i, lap]).reverse().map(([n, i, lap]) => {
    const [ms, driver, deleted] = lap;
    const kind = lapKind(lap);
    const short = !deleted && suspicious(lap);
    const cls = deleted ? 'del' : short ? 'short' : kind === 'best' ? 'b' : kind === 'pb' ? 'pb' : '';
    const diff = deleted ? 'deleted' : short ? 'short?' : kind === 'best' ? '★' : kind === 'pb' ? 'PB' : '+' + fmt(ms - S.best);
    let row = `<li class="lap ${cls}">
      <span>${n}.</span>
      <span class="who">${named ? esc(driverName(driver)) : ''}</span>
      <span>${fmt(ms)}</span>
      <span class="d">${diff}</span>
      ${deleted ? '<span></span>' : `<button class="x${openLap === n ? ' on' : ''}" data-open="${n}" title="Fix this lap">⋯</button>`}
    </li>`;
    if (openLap === n) {
      const prev = joinable(i - 1, i), next = joinable(i, i + 1);
      row += `<li class="acts">
        ${next ? `<button data-join="${n}">Join with lap ${n + 1}</button>` : ''}
        ${prev ? `<button data-join="${n - 1}">Join with lap ${n - 1}</button>` : ''}
        <button class="r" data-del="${n}">Delete lap ${n}</button>
        <p class="hint">${prev || next
          ? 'Join when a false trigger split one real lap into two.'
          : 'Delete a lap that shouldn\'t count (car cut the track, crash).'}</p>
      </li>`;
    }
    return row;
  }).join('');
  $('noLaps').hidden = S.laps.length > 0;
}

function renderWifi() {
  const ssid = esc(S.ssid);
  $('wstat').innerHTML = {
    off:        'Home network not set.',
    connecting: `Connecting to <b>${ssid}</b>…`,
    connected:  `Connected to <b>${ssid}</b>: <a href="http://laptimer.local">laptimer.local</a> or <a href="http://${S.staIp}">${S.staIp}</a>`,
    waiting:    `<b>${ssid}</b> not in range, retrying every 30 s while no phone is on RC_Lap_Timer.`,
  }[S.sta] || '';
}

function onState(s) {
  const newLap = seen >= 0 && s.count > seen;
  seen = s.count; S = s; cur = s.cur; at = performance.now();

  const lastLap = s.laps.length ? s.laps[s.laps.length - 1] : null;
  const last = lastLap && !lastLap[2] ? lastLap[0] : 0;
  const kind = lastLap ? lapKind(lastLap) : '';
  if (newLap && lastLap) { beep(kind !== ''); flash(kind === 'best'); announce(lastLap, kind); }

  $('last').textContent  = last ? fmt(last) : '--';
  $('lastd').textContent = last && kind !== 'best' ? '+' + fmt(last - s.best) : ' ';
  $('best').textContent  = s.best ? fmt(s.best) : '--';
  const holder = s.best ? s.laps.find(l => !l[2] && l[0] === s.best) : null;
  const valid = s.laps.filter(l => !l[2]);
  $('bestd').textContent = holder && driverName(holder[1])
    ? driverName(holder[1])
    : valid.length ? 'ø ' + fmt(valid.reduce((a, l) => a + l[0], 0) / valid.length) : ' ';

  $('undo').disabled = !s.canUndo;
  renderDrivers();
  renderBoard();
  renderLaps();
  renderWifi();
  $('ign').textContent = s.ignored;

  if (!cfgLoaded) {
    $('minLap').value = s.minLap / 1000;
    $('delta').value = s.delta;
    cfgLoaded = true;
  }
  if (s.ssid !== lastSsid && document.activeElement !== $('ssid')) {   // also when changed from another phone
    $('ssid').value = s.ssid;
    lastSsid = s.ssid;
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
  const who = S && driverName(S.driver);
  const suffix = who ? ' · ' + who : '';
  if (S && S.running) {
    $('cur').textContent = fmt(cur + (performance.now() - at));
    $('state').textContent = 'Lap ' + (S.count + 1) + suffix;
  } else {
    $('cur').textContent = '--.---';
    $('state').textContent = 'Waiting for start pass' + suffix;
  }
  requestAnimationFrame(tick);
}

// ---------------------------------------------------------------- connection
// WebSocket with automatic reconnect, HTTP polling as fallback while it's down
function connect() {
  try { sock = new WebSocket(`ws://${location.hostname}:${WS_PORT}/`); } catch (e) { return startPoll(); }
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

// ---------------------------------------------------------------- actions
$('undo').onclick    = () => api('undo');
$('restart').onclick = () => api('restart');
$('reset').onclick   = () => api('reset', {}, 'Start a new session? All laps and the leaderboard are cleared, drivers stay.');
$('calibrate').onclick = () => api('calibrate');

$('laps').onclick = e => {
  const { open, join, del } = e.target.dataset;
  if (open) { openLap = openLap === +open ? 0 : +open; renderLaps(); }
  if (join) { openLap = 0; api('lap/join', { n: join }); }
  if (del)  { openLap = 0; api('lap/delete', { n: del }); }
};

$('chips').onclick = e => {
  const id = e.target.dataset.id;
  if (id && +id !== S.driver) api('driver/select', { id });
};

$('dlist').onclick = e => {
  const id = e.target.dataset.id;
  if (id) api('driver/remove', { id }, `Remove ${driverName(+id)}? Their laps stay in the list.`);
};

$('addForm').onsubmit = async e => {
  e.preventDefault();
  const name = $('newName').value.trim();
  if (!name) return;
  if (await api('driver/add', { name })) {
    $('newName').value = '';
    $('dmsg').textContent = 'Switching driver drops the lap in progress, the next pass starts the new driver\'s lap.';
  } else {
    $('dmsg').textContent = `Couldn't add "${name}": the name is taken or there are already 8 drivers.`;
  }
};

$('save').onclick = async () => {
  await api('config', { minLap: Math.round($('minLap').value * 1000), delta: $('delta').value });
  cfgLoaded = false;
};

async function wifi(forget) {
  const params = forget ? { ssid: '', pass: '' } : { ssid: $('ssid').value, pass: $('pass').value };
  if (await api('wifi', params, forget ? 'Forget home Wi-Fi? The timer restarts.' : null)) {
    $('wstat').textContent = 'Restarting… reconnect in a few seconds.';
  }
}
$('wifiSave').onclick   = () => wifi(false);
$('wifiForget').onclick = () => wifi(true);

connect();
tick();
