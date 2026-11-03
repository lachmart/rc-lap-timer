// RC Lap Timer – client. Data over WebSocket (port 81), HTTP polling as fallback.
// One car (beam only) or several cars with IR transponders; training and races (race.js).

const $ = id => document.getElementById(id);
const fmt = ms => (ms / 1000).toFixed(3);
const esc = t => String(t).replace(/[&<>"']/g, c => `&#${c.charCodeAt(0)};`);
const WS_PORT = new URLSearchParams(location.search).get('ws') || 81;   // ?ws= for the mock server

// lap entry fields: [ms, driver, deleted, continues, car, num, precise, endMs]
const L = { MS: 0, DRIVER: 1, DELETED: 2, CONT: 3, CAR: 4, NUM: 5, PRECISE: 6, END: 7 };
const STATS_CAR = 0x100;
const NEC_ID_MIN = 0x1000;   // transponder ids from here on are RiCino codes, below are NE556 blink rates
const MAX_DRIVERS = 8;

let S = null;          // latest state from the server
let clockOffset = 0;   // server ms − performance.now()
let curAt = 0;         // performance.now() when per-car current times arrived
let curMs = [];        // lap in progress per car slot, from the server
let seen = -1;         // lap count in the previous state, to detect a new lap
let cfgLoaded = false; // settings form is filled only once so it doesn't overwrite typing
let beepOn = false, voiceOn = false, ac = null;
let sock = null, pollT = null;
let lastSsid = null;   // home network shown in the Wi-Fi form
let rssi = 0;          // home Wi-Fi signal (dBm), 0 = not connected
let suggested = 0;     // sensitivity suggested by the sensor
let brokenSince = 0;   // performance.now() when the beam got broken, 0 = clear
let openLap = 0;       // session lap number whose actions are shown in the list
let openCar = -1;      // car slot whose actions are shown in the track table
let medians = {};      // stats key -> median of counted laps (3+ laps), for "short?"
let raceKind = 'laps';
let lastCount = null;  // countdown second last announced
let finishSaid = false;

const serverNow = () => performance.now() + clockOffset;

// ---------------------------------------------------------------- names
const driverName = id => {
  const d = S && S.drivers.find(d => d.id === id);
  return d ? d.name : '';
};
const carBySlot = slot => S && S.cars.find(c => c.slot === slot);
const txLabel = tx => tx >= NEC_ID_MIN ? '#' + tx.toString(16).toUpperCase() : tx + ' Hz';
function carLabel(car) {
  if (!car) return 'removed car';
  if (car.name) return car.name;
  if (!car.tx) return 'Car';
  return txLabel(car.tx);
}
// Who is on a car: the driver if assigned, else the car's label.
const whoOn = slot => {
  const car = carBySlot(slot);
  return (car && driverName(car.driver)) || carLabel(car);
};
const lapKey = lap => lap[L.DRIVER] || (STATS_CAR | lap[L.CAR]);
const keyName = key => key & STATS_CAR ? carLabel(carBySlot(key & 0xff)) : driverName(key);
const statsOf = key => S.stats.find(s => s.key === key);
const countedLaps = slot => S.laps.filter(l => l[L.CAR] === slot && !l[L.DELETED]);
const avg = (total, count) => count ? total / count : 0;
const multi = () => S && S.multi;
const trackCars = () => S.cars.filter(c => multi() ? c.slot > 0 : c.slot === 0);
const carCur = slot => {
  const car = carBySlot(slot);
  return car && car.running ? (curMs[slot] || 0) + (performance.now() - curAt) : null;
};

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

// Remembered per phone. Browser storage can be unavailable (private mode), then it just isn't kept.
const prefs = {
  get: k => { try { return localStorage.getItem(k) === '1'; } catch (e) { return false; } },
  set: (k, v) => { try { localStorage.setItem(k, v ? '1' : '0'); } catch (e) { /* not kept */ } },
};

function unlockAudio() {
  try {
    if (!ac) ac = new (window.AudioContext || window.webkitAudioContext)();
    if (ac.state === 'suspended') ac.resume();
  } catch (e) { /* no WebAudio */ }
}

function setBeep(on) {
  beepOn = on;
  setToggle($('snd'), on, 'Beep');
  prefs.set('beep', on);
  if (on) unlockAudio();
}

function setVoice(on, greet) {
  if (!('speechSynthesis' in window)) { $('voice').textContent = 'No voice'; return; }
  voiceOn = on;
  setToggle($('voice'), on, 'Voice');
  prefs.set('voice', on);
  if (on && greet) say('Voice on');
}

$('snd').onclick   = () => setBeep(!beepOn);
$('voice').onclick = () => setVoice(!voiceOn, true);

// Browsers only allow sound after the user touched the page: restore the saved switches
// and unlock audio and speech on the first tap anywhere.
setBeep(prefs.get('beep'));
setVoice(prefs.get('voice'), false);
document.addEventListener('pointerdown', () => {
  if (beepOn) unlockAudio();
  if (voiceOn) speechSynthesis.speak(new SpeechSynthesisUtterance(''));
}, { once: true });

function tone(freq, secs) {
  if (!beepOn || !ac) return;
  const o = ac.createOscillator(), g = ac.createGain();
  o.frequency.value = freq;
  g.gain.value = .2;
  o.connect(g); g.connect(ac.destination);
  o.start(); o.stop(ac.currentTime + secs);
}

function beep(best) {
  if (navigator.vibrate) navigator.vibrate(best ? [80, 60, 80] : 80);
  tone(best ? 1320 : 880, best ? .35 : .15);
}

function say(text) {
  if (!voiceOn) return;
  speechSynthesis.cancel();                            // a new lap interrupts the previous announcement
  const u = new SpeechSynthesisUtterance(text);
  u.lang = 'en-US';
  speechSynthesis.speak(u);
}

function announce(lap, kind) {
  const name = multi() || S.drivers.length ? whoOn(lap[L.CAR]) : '';
  let text = (name ? name + ', ' : '') + (lap[L.MS] / 1000).toFixed(2);
  if (kind === 'best') text += ', best lap';
  else if (kind === 'pb') text += ', personal best';
  say(text);
}

// ---------------------------------------------------------------- lap classification
function computeMedians() {
  const by = {};
  for (const l of S.laps) if (!l[L.DELETED]) (by[lapKey(l)] = by[lapKey(l)] || []).push(l[L.MS]);
  medians = {};
  for (const [key, list] of Object.entries(by)) {
    if (list.length < 3) continue;
    list.sort((a, b) => a - b);
    medians[key] = list[Math.floor(list.length / 2)];
  }
}

// Much shorter than the usual lap of that driver/car: probably a false trigger split a lap.
function suspicious(lap) {
  const m = medians[lapKey(lap)];
  return m !== undefined && lap[L.MS] < m * 0.6;
}

// 'best' = best of the session, 'pb' = personal best of the driver/car, '' = neither
function lapKind(lap) {
  if (lap[L.DELETED] || suspicious(lap)) return '';
  if (lap[L.MS] === S.best) return 'best';
  const st = statsOf(lapKey(lap));
  return (S.drivers.length || multi()) && st && lap[L.MS] === st.best ? 'pb' : '';
}

// The same car's next lap that lap i (oldest-first index) can be joined with, -1 if none.
function joinTarget(i) {
  const a = S.laps[i];
  for (let k = i + 1; k < S.laps.length; k++) {
    const b = S.laps[k];
    if (b[L.CAR] !== a[L.CAR]) continue;
    return !a[L.DELETED] && !b[L.DELETED] && a[L.DRIVER] === b[L.DRIVER] && b[L.CONT] === 1 ? k : -1;
  }
  return -1;
}
function joinSource(i) {
  for (let k = i - 1; k >= 0; k--) if (S.laps[k][L.CAR] === S.laps[i][L.CAR]) return joinTarget(k) === i ? k : -1;
  return -1;
}

// ---------------------------------------------------------------- rendering
function conn(state) {
  const [color, text] = { ws: ['var(--ok)', 'live'], poll: ['var(--gold)', 'fallback mode'], off: ['var(--acc)', 'offline'] }[state];
  $('dot').style.background = color;
  $('ctxt').textContent = text;
}

function flash(isBest) {
  for (const [id, base] of [['curCard', 'card'], ['trackCard', 'card'], ['raceCard', 'card'], ['bigView', 'bigview']]) {
    const c = $(id);
    c.className = base;
    void c.offsetWidth;                                // restart the animation
    c.className = base + ' flash' + (isBest ? ' b' : '');
  }
}

const racing = () => S && S.race.state !== 'off';

function renderLayout() {
  const m = multi(), r = racing();
  $('raceCard').hidden = !r;
  $('curCard').hidden = r || m;
  $('trackCard').hidden = r || !m;
  $('lastBest').hidden = m || r;
  $('driversTitle').textContent = m ? 'Drivers' : 'Driving now';
  $('raceSetup').parentElement.hidden = r;
}

function renderDrivers() {
  const m = multi();
  const car0 = carBySlot(0);
  $('chips').hidden = m;
  $('chips').innerHTML = S.drivers.length
    ? S.drivers.map(d => `<button class="chip${car0 && d.id === car0.driver ? ' on' : ''}" data-id="${d.id}">${esc(d.name)}</button>`).join('')
    : '<span class="hint">Nobody yet. Add drivers under Edit drivers to get a leaderboard.</span>';

  // several cars: who drives which car (a driver drives one car at a time)
  $('carDrivers').hidden = !m;
  const cars = S.cars.filter(c => c.slot > 0);
  $('carDrivers').innerHTML = !m ? '' : cars.length
    ? cars.map(c => `<label>${esc(carLabel(c))}
        <select data-car="${c.slot}">
          <option value="0">–</option>
          ${S.drivers.map(d => `<option value="${d.id}"${d.id === c.driver ? ' selected' : ''}>${esc(d.name)}</option>`).join('')}
        </select></label>`).join('')
    : '<p class="hint">No cars yet: drive a car with a transponder past the gate.</p>';

  $('dlist').innerHTML = S.drivers
    .map(d => `<li><span>${esc(d.name)}</span><button data-id="${d.id}" title="Remove">✕</button></li>`).join('');
  $('addForm').hidden = S.drivers.length >= MAX_DRIVERS;
}

function renderBoard() {
  const rows = S.stats.filter(s => s.count > 0 && keyName(s.key)).sort((a, b) => a.best - b.best);
  $('boardCard').hidden = racing() || !(S.drivers.length || multi()) || !rows.length;
  const lead = rows.length ? rows[0].best : 0;
  const nowKeys = S.cars.filter(c => c.driver).map(c => c.driver);
  $('board').innerHTML = rows.map((s, i) => `
    <tr class="${i === 0 ? 'lead' : ''} ${nowKeys.includes(s.key) ? 'now' : ''}">
      <td>${i + 1}.</td>
      <td>${esc(keyName(s.key))}</td>
      <td>${fmt(s.best)}</td>
      <td>${i ? '+' + fmt(s.best - lead) : ''}</td>
      <td>${s.count}</td>
      <td>${fmt(avg(s.total, s.count))}</td>
    </tr>`).join('');
}

function renderLaps() {
  const first = S.count - S.laps.length + 1;           // session number of the oldest lap in the list
  const named = S.drivers.length > 0 || multi();
  if (openLap && (openLap < first || openLap > S.count || S.laps[openLap - first][L.DELETED])) openLap = 0;

  $('laps').innerHTML = S.laps.map((lap, i) => [first + i, i, lap]).reverse().map(([n, i, lap]) => {
    const deleted = lap[L.DELETED];
    const kind = lapKind(lap);
    const short = !deleted && suspicious(lap);
    const cls = deleted ? 'del' : short ? 'short' : kind === 'best' ? 'b' : kind === 'pb' ? 'pb' : '';
    const diff = deleted ? 'deleted' : short ? 'short?' : kind === 'best' ? '★' : kind === 'pb' ? 'PB' : '+' + fmt(lap[L.MS] - S.best);
    const num = multi() ? lap[L.NUM] : n;               // several cars: lap number of that car
    const who = !named ? '' : driverName(lap[L.DRIVER]) || carLabel(carBySlot(lap[L.CAR]));
    const approx = lap[L.PRECISE] ? '' : '<span class="approx" title="Timed by the transponder only (±35 ms)">~</span>';
    let row = `<li class="lap ${cls}">
      <span>${num}.</span>
      <span class="who">${esc(who)}</span>
      <span>${approx}${fmt(lap[L.MS])}</span>
      <span class="d">${diff}</span>
      ${deleted ? '<span></span>' : `<button class="x${openLap === n ? ' on' : ''}" data-open="${n}" title="Fix this lap">⋯</button>`}
    </li>`;
    if (openLap === n) {
      const next = joinTarget(i), prev = joinSource(i);
      const numOf = k => multi() ? S.laps[k][L.NUM] : first + k;
      row += `<li class="acts">
        ${next >= 0 ? `<button data-join="${n}">Join with lap ${numOf(next)}</button>` : ''}
        ${prev >= 0 ? `<button data-join="${first + prev}">Join with lap ${numOf(prev)}</button>` : ''}
        <button class="r" data-del="${n}">Delete lap ${num}</button>
        <p class="hint">${next >= 0 || prev >= 0
          ? 'Join when a false trigger split one real lap into two.'
          : 'Delete a lap that shouldn\'t count (car cut the track, crash).'}</p>
      </li>`;
    }
    return row;
  }).join('');
  $('noLaps').hidden = S.laps.length > 0;
}

// Several cars, training: one row per car. The "now" column is updated by tick().
function renderTrack() {
  if (!multi() || racing()) return;
  const cars = trackCars();
  if (openCar >= 0 && !carBySlot(openCar)) openCar = -1;
  $('track').innerHTML = cars.map(c => {
    const laps = countedLaps(c.slot);
    const last = laps.length ? laps[laps.length - 1][L.MS] : 0;
    const best = laps.length ? Math.min(...laps.map(l => l[L.MS])) : 0;
    let row = `<tr class="${best && best === S.best ? 'lead' : ''}">
      <td>${esc(whoOn(c.slot))}</td><td>${c.running ? c.laps + 1 : '–'}</td>
      <td class="now" data-car="${c.slot}">${c.running ? '' : 'waiting'}</td>
      <td>${last ? fmt(last) : '–'}</td><td>${best ? fmt(best) : '–'}</td>
      <td><button class="x" data-car="${c.slot}" title="Fix this car's lap">⋯</button></td></tr>`;
    if (openCar === c.slot) {
      row += `<tr class="acts"><td colspan="6">
        <button data-undo="${c.slot}" ${c.canUndo ? '' : 'disabled'}>↶ Undo pass</button>
        <button data-restart="${c.slot}">⟲ Restart lap</button></td></tr>`;
    }
    return row;
  }).join('') || '<tr><td colspan="6" class="hint">No cars yet: drive a car with a transponder past the gate.</td></tr>';
  $('trackHint').textContent = S.unident ? `Passes without a transponder: ${S.unident}` : '';
}

const clock = ms => {
  const s = Math.floor(ms / 1000);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
};

// Race standings: recomputed on every state and a few times per second for the clock.
function renderRace() {
  if (!racing() || !window.Race) return null;
  const r = Race.standings(S, serverNow());
  const lapsRace = S.race.laps > 0;
  const lead = r.rows[0];

  $('raceTitle').textContent = r.phase === 'countdown' ? 'Get ready'
    : r.allIn ? 'Race finished'
    : r.finished ? 'Finished, waiting for the others'
    : lapsRace ? `Race · lap ${Math.min((lead ? lead.laps : 0) + 1, S.race.laps)} / ${S.race.laps}`
    : `Race · ${S.race.minutes} min`;
  $('raceClock').textContent = r.phase !== 'running' ? ''
    : r.finished ? clock(r.finishMs)                    // stopped at the winner's time
    : r.remainingMs !== null ? clock(r.remainingMs) : clock(r.elapsedMs);

  const secs = Math.ceil(r.countdownMs / 1000);
  $('countdown').hidden = r.phase !== 'countdown';
  $('countdown').textContent = secs > 3 ? 'Ready' : secs > 0 ? secs : 'GO';
  $('standings').innerHTML = r.phase === 'countdown' ? '' : standingsRows(r);
  $('raceHint').textContent = r.phase === 'countdown' ? 'Crossings before "go" are ignored.'
    : r.rows.some(x => !x.started) ? 'Each car\'s first crossing starts its timing.' : '';
  $('raceStop').textContent = r.allIn ? 'Back to training' : 'Stop race';
  return r;
}

function standingsRows(r) {
  return r.rows.map(x => {
    const last = S.laps.filter(l => l[L.CAR] === x.car && !l[L.DELETED] && l[L.END] > S.race.go).pop();
    const gap = x.pos === 1 ? '' : x.gapLaps ? `+${x.gapLaps} lap${x.gapLaps > 1 ? 's' : ''}` : x.gapMs ? '+' + fmt(x.gapMs) : '';
    return `<tr class="${x.pos === 1 && x.started ? 'lead' : ''}">
      <td>${x.started ? x.pos + '.' : ''}</td>
      <td>${esc(whoOn(x.car))}${x.finished ? ' 🏁' : ''}</td>
      <td>${x.laps}</td>
      <td>${last ? fmt(last[L.MS]) : '–'}</td>
      <td>${x.bestMs ? fmt(x.bestMs) : '–'}</td>
      <td>${x.started ? gap : 'not started'}</td></tr>`;
  }).join('');
}

function renderCars() {
  $('multi').checked = !!S.multi;
  const cars = S.cars.filter(c => c.slot > 0);
  $('csum').textContent = S.multi ? `· ${cars.length} car${cars.length === 1 ? '' : 's'}` : '';
  if ($('cars').contains(document.activeElement)) return;   // don't rebuild while a name is being typed
  $('cars').innerHTML = cars.map(c => `<li>
      <span class="tx">${txLabel(c.tx)}</span>
      <input data-rename="${c.slot}" value="${esc(c.name)}" placeholder="name" maxlength="16">
      <button data-remove="${c.slot}" title="Remove">✕</button></li>`).join('');
}

function renderUsb() {
  const z = S.zround;
  $('zround').checked = z.on;
  $('usum').textContent = z.on ? (z.racing ? '· ZRound racing' : z.connected ? '· ZRound connected' : '· waiting for ZRound') : '';
  $('zstat').className = 'wstat ' + (z.on ? (z.connected ? 'ok' : 'busy') : '');
  $('zstat').textContent = !z.on ? 'USB port: debug log (115200 baud).'
    : z.racing ? 'ZRound is running a race, passings are sent.'
    : z.connected ? 'ZRound is connected.' : 'Waiting for ZRound to connect (19200 baud).';
}

const signal = dbm => dbm >= -60 ? 4 : dbm >= -70 ? 3 : dbm >= -80 ? 2 : 1;
const bars = dbm => `<span class="bars" title="${dbm} dBm">${[1, 2, 3, 4]
  .map(i => `<i class="${i <= signal(dbm) ? 'on' : ''}"></i>`).join('')}</span>`;

function renderWifi() {
  const ssid = `<b>${esc(S.ssid)}</b>`;
  const err = {
    'wrong password':    'wrong password',
    'network not found': 'network not found (out of range, or the name is wrong)',
    'failed':            'couldn\'t connect',
  }[S.staErr];
  const [cls, text, sum] = {
    off:        ['', 'Not on a home network. Phones join <b>RC_Lap_Timer</b>.', ''],
    connecting: ['busy', `Connecting to ${ssid}…`, 'connecting…'],
    connected:  ['ok', `Connected to ${ssid} <span id="wbars"></span><br>
                 On home Wi-Fi open <a href="http://laptimer.local">laptimer.local</a> or <a href="http://${S.staIp}">${S.staIp}</a>`,
                 '✓ ' + S.ssid],
    waiting:    ['bad', `Not connected to ${ssid}: ${err || 'not in range'}.<br>Retrying every 30 s while no phone is on RC_Lap_Timer.`,
                 err === 'wrong password' ? 'wrong password' : 'not connected'],
  }[S.sta] || ['', '', ''];
  $('wstat').className = 'wstat ' + cls;
  $('wstat').innerHTML = text;
  $('wsum').textContent = sum ? '· ' + sum : '';
  $('wifiForget').hidden = !S.ssid;
  renderBars();
  updatePassHint();
}

function renderBars() {
  const b = $('wbars');
  if (b) b.innerHTML = rssi ? bars(rssi) : '';
}

// Same network as saved: an empty password field keeps the saved password.
function updatePassHint() {
  const same = S && S.hasPass && $('ssid').value.trim() === S.ssid;
  $('pass').placeholder = same ? 'saved' : '';
}

function renderNets(list) {
  $('nets').hidden = false;
  $('nets').innerHTML = list.length
    ? list.map(n => `<li><button type="button" data-ssid="${esc(n.ssid)}" data-free="${n.open ? 1 : 0}">
        <span>${esc(n.ssid)}</span><span>${n.open ? '' : '🔒 '}${bars(n.rssi)}</span></button></li>`).join('')
    : '<li class="hint">No networks found. The ESP32 only sees 2.4 GHz Wi-Fi.</li>';
}

function renderSingle() {
  const car0 = carBySlot(0);
  const mine = S.laps.filter(l => l[L.CAR] === 0);
  const lastLap = mine.length ? mine[mine.length - 1] : null;
  const last = lastLap && !lastLap[L.DELETED] ? lastLap[L.MS] : 0;
  const kind = lastLap ? lapKind(lastLap) : '';
  $('last').textContent  = last ? fmt(last) : '--';
  $('lastd').textContent = last && last !== S.best ? '+' + fmt(last - S.best) : ' ';
  $('best').textContent  = S.best ? fmt(S.best) : '--';
  const holder = S.best ? mine.find(l => !l[L.DELETED] && l[L.MS] === S.best) : null;
  const valid = mine.filter(l => !l[L.DELETED]);
  $('bestd').textContent = holder && driverName(holder[L.DRIVER])
    ? driverName(holder[L.DRIVER])
    : valid.length ? 'ø ' + fmt(valid.reduce((a, l) => a + l[L.MS], 0) / valid.length) : ' ';
  $('undo').disabled = !(car0 && car0.canUndo);
  for (const id of ['last', 'lastd', 'best', 'bestd']) $('bv' + id[0].toUpperCase() + id.slice(1)).textContent = $(id).textContent;
}

function onState(s) {
  const newLap = seen >= 0 && s.count > seen;
  seen = s.count; S = s;
  clockOffset = s.now - performance.now();
  curAt = performance.now();
  curMs = [];
  for (const c of s.cars) curMs[c.slot] = c.cur;
  if (s.race.state !== 'running') finishSaid = false;
  computeMedians();

  const lastLap = s.laps.length ? s.laps[s.laps.length - 1] : null;
  const kind = lastLap ? lapKind(lastLap) : '';
  if (newLap && lastLap) {
    beep(kind !== '');
    if (!s.multi || kind === 'best') flash(kind === 'best');   // with several cars only a best lap flashes
    announce(lastLap, kind);
  }

  renderLayout();
  renderSingle();
  renderDrivers();
  renderBoard();
  renderLaps();
  renderTrack();
  renderRace();
  renderCars();
  renderUsb();
  renderWifi();
  $('ign').textContent = s.ignored;

  if (!cfgLoaded) {
    $('minLap').value = s.minLap / 1000;
    $('delta').value = s.delta;
    raceKind = s.race.setMin && !s.race.setLaps ? 'minutes' : 'laps';
    $('raceLen').value = raceKind === 'minutes' ? s.race.setMin : s.race.setLaps;
    renderRaceKind();
    cfgLoaded = true;
  }
  if (s.ssid !== lastSsid && document.activeElement !== $('ssid')) {   // also when changed from another phone
    $('ssid').value = s.ssid;
    lastSsid = s.ssid;
  }
}

function onLive(l) {
  curMs = l.cur; curAt = performance.now();
  $('raw').textContent = l.raw;
  $('base').textContent = l.base;
  $('beam').textContent = l.broken ? 'broken' : 'clear';
  $('mr').style.left = (l.raw / 40.95) + '%';          // ADC 0–4095 → 0–100 %
  $('mb').style.left = (l.base / 40.95) + '%';
  if (l.rssi !== rssi) { rssi = l.rssi || 0; renderBars(); }
  renderTuning(l);
  renderBeamWarning(l.broken);
  renderIr(l);
}

function renderIr(l) {
  const ago = l.irAgo;
  const id = txLabel(l.irId);
  $('irStat').className = 'wstat ' + (l.irId && ago < 2000 ? 'ok' : '');
  $('irStat').textContent = (!l.irId ? 'IR receiver: no transponder seen yet.'
    : `IR receiver: last transponder ${id}, ${ago < 1000 ? 'just now' : Math.round(ago / 1000) + ' s ago'}.`)
    + (l.unident ? ` Passes without a transponder: ${l.unident}.` : '');
}

// Band around idle where nothing triggers; the green marker leaving it = beam broken.
function renderTuning(l) {
  if (!S) return;
  const lo = Math.max(0, l.base - S.delta), hi = Math.min(4095, l.base + S.delta);
  $('band').style.left = (lo / 40.95) + '%';
  $('band').style.width = ((hi - lo) / 40.95) + '%';

  suggested = l.suggest;
  // the suggestion moves a little with every pass; within ±20 % the current setting is fine
  const fine = l.suggest > 0 && Math.abs(S.delta - l.suggest) <= l.suggest * 0.2;
  $('noise').textContent = '±' + l.noise;
  $('peak').textContent = l.peak || '–';
  $('suggest').textContent = l.suggest > 0 ? l.suggest : l.suggest < 0 ? 'too weak' : '–';
  $('tuneHint').textContent =
      l.suggest < 0 ? 'A pass barely differs from the noise. Aim the laser better, shade the LDR tube from the sun, or move the gate.'
    : fine ? `Sensitivity ${S.delta} is fine.`
    : l.suggest > 0 ? `Sensitivity ${S.delta} is ${S.delta < l.suggest ? 'too sensitive (false triggers)' : 'too dull (missed passes)'} for this signal.`
    : 'Drive (or wave a hand) through the beam once to measure how deep a pass goes.';
  $('useSuggest').hidden = !(l.suggest > 0 && !fine);
}

// Beam broken for a long time: the laser got knocked off aim or something sits in the gate.
function renderBeamWarning(broken) {
  const now = performance.now();
  if (!broken) brokenSince = 0;
  else if (!brokenSince) brokenSince = now;
  const secs = brokenSince ? Math.round((now - brokenSince) / 1000) : 0;
  const blocked = secs >= 3;
  const msg = `Beam blocked for ${secs} s, ${multi() ? 'passes are timed by transponder only' : 'laps aren\'t counted'}. `
    + 'Check the laser aim or clear the gate.';
  for (const id of ['beamWarn', 'beamWarn2', 'bvWarn']) {
    $(id).hidden = !blocked;
    if (blocked) $(id).textContent = msg;
  }
  $('ssum').textContent = blocked ? '· beam blocked' : '';
}

// ---------------------------------------------------------------- every frame
function tick() {
  if (S) {
    const car0 = carBySlot(0);
    const who = car0 ? driverName(car0.driver) : '';
    const t0 = carCur(0);
    const time = t0 !== null ? fmt(t0) : '--.---';
    const lap = t0 !== null ? 'Lap ' + ((car0 ? car0.laps : 0) + 1) : 'Waiting for start pass';
    $('cur').textContent = time;
    $('state').textContent = lap + (who ? ' · ' + who : '');
    for (const td of document.querySelectorAll('#track td.now')) {
      const t = carCur(+td.dataset.car);
      if (t !== null) td.textContent = fmt(t);
    }
    const bigMulti = multi() || racing();
    if (!$('bigView').hidden) {
      $('bvSingle').hidden = bigMulti;
      $('bvMulti').hidden = !bigMulti;
      if (!bigMulti) {
        $('bvCur').textContent = time;
        $('bvLap').textContent = lap;
        $('bvDriver').textContent = who;
      }
    }
  }
  requestAnimationFrame(tick);
}

// Race clock, countdown voice and the big table: a few times per second is enough.
setInterval(() => {
  if (!S) return;
  const r = renderRace();
  if (r && r.phase === 'countdown') {
    const secs = Math.ceil(r.countdownMs / 1000);
    if (secs !== lastCount && secs <= 3) { lastCount = secs; say(String(secs)); tone(660, .15); }
  } else if (r && r.phase === 'running' && lastCount !== null) {
    lastCount = null;
    say('Go'); tone(1320, .5);
    if (navigator.vibrate) navigator.vibrate(300);
  }
  if (r && r.finished && !finishSaid) {
    finishSaid = true;
    say(`Race finished. Winner, ${whoOn(r.rows[0].car)}`);
  }
  if (!$('bigView').hidden && (multi() || racing())) renderBigTable(r);
}, 250);

function renderBigTable(r) {
  if (r) {
    $('bvTitle').textContent = $('raceTitle').textContent;
    $('bvClock').textContent = $('raceClock').textContent;
    $('bvCount').hidden = $('countdown').hidden;
    $('bvCount').textContent = $('countdown').textContent;
    $('bvRows').innerHTML = r.rows.filter(x => x.started).map(x => `<tr class="${x.pos === 1 ? 'lead' : ''}">
      <td>${x.pos}.</td><td class="name">${esc(whoOn(x.car))}${x.finished ? ' 🏁' : ''}</td><td>${x.laps}</td>
      <td>${x.pos === 1 ? '' : x.gapLaps ? '+' + x.gapLaps + 'L' : '+' + fmt(x.gapMs)}</td></tr>`).join('');
  } else {
    $('bvTitle').textContent = 'Training';
    $('bvClock').textContent = '';
    $('bvCount').hidden = true;
    $('bvRows').innerHTML = '<tr><th>Car</th><th>Now</th><th>Last</th></tr>' + trackCars().map(c => {
      const laps = countedLaps(c.slot);
      const last = laps.length ? laps[laps.length - 1][L.MS] : 0;
      const t = carCur(c.slot);
      return `<tr><td class="name">${esc(whoOn(c.slot))}</td><td>${t !== null ? fmt(t).slice(0, -2) : '–'}</td>
        <td>${last ? fmt(last) : '–'}</td></tr>`;
    }).join('');
  }
}

// ---------------------------------------------------------------- big view
$('bigOpen').onclick = () => {
  $('bigView').hidden = false;
  const el = document.documentElement;                // real fullscreen where the browser allows it
  if (el.requestFullscreen) el.requestFullscreen().catch(() => {});
};
$('bigView').onclick = () => {
  $('bigView').hidden = true;
  if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
};

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
$('undo').onclick    = () => api('undo', { car: 0 });
$('restart').onclick = () => api('restart', { car: 0 });
$('reset').onclick   = () => api('reset', {}, 'Start a new session? All laps and the leaderboard are cleared, drivers and cars stay.');
$('calibrate').onclick = () => api('calibrate');
$('useSuggest').onclick = async () => {
  if (suggested > 0 && await api('config', { minLap: S.minLap, delta: suggested })) cfgLoaded = false;
};

$('laps').onclick = e => {
  const { open, join, del } = e.target.dataset;
  if (open) { openLap = openLap === +open ? 0 : +open; renderLaps(); }
  if (join) { openLap = 0; api('lap/join', { n: join }); }
  if (del)  { openLap = 0; api('lap/delete', { n: del }); }
};

$('track').onclick = e => {
  const b = e.target.closest('button');
  if (!b) return;
  if (b.dataset.car !== undefined) { openCar = openCar === +b.dataset.car ? -1 : +b.dataset.car; renderTrack(); }
  if (b.dataset.undo !== undefined) { openCar = -1; api('undo', { car: b.dataset.undo }); }
  if (b.dataset.restart !== undefined) { openCar = -1; api('restart', { car: b.dataset.restart }); }
};

$('chips').onclick = e => {
  const id = e.target.dataset.id;
  const car0 = carBySlot(0);
  if (id && car0 && +id !== car0.driver) api('driver/select', { id, car: 0 });
};

$('carDrivers').onchange = e => {
  const sel = e.target.closest('select');
  if (sel) api('driver/select', { id: sel.value, car: sel.dataset.car });
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
    $('dmsg').textContent = 'Switching driver drops that car\'s lap in progress, its next pass starts the new driver\'s lap.';
  } else {
    $('dmsg').textContent = `Couldn't add "${name}": the name is taken or there are already ${MAX_DRIVERS} drivers.`;
  }
};

$('multi').onchange  = e => api('mode', { multi: e.target.checked ? 1 : 0 });
$('zround').onchange = e => api('usb', { zround: e.target.checked ? 1 : 0 });

$('cars').onchange = e => {
  const slot = e.target.dataset.rename;
  if (slot) api('car/rename', { slot, name: e.target.value.trim() });
};
$('cars').onclick = e => {
  const slot = e.target.dataset.remove;
  if (slot) api('car/remove', { slot }, `Remove ${carLabel(carBySlot(+slot))}? It comes back when its transponder passes the gate again.`);
};

function renderRaceKind() {
  for (const b of $('raceKind').children) b.classList.toggle('on', b.dataset.kind === raceKind);
  $('raceLenLbl').textContent = raceKind === 'laps' ? 'Number of laps' : 'Minutes';
}
$('raceKind').onclick = e => {
  const kind = e.target.dataset.kind;
  if (!kind) return;
  raceKind = kind;
  $('raceLen').value = kind === 'laps' ? (S.race.setLaps || 10) : (S.race.setMin || 5);
  renderRaceKind();
};
$('raceStart').onclick = () => {
  const n = Math.max(1, Math.round(+$('raceLen').value || 0));
  const params = raceKind === 'laps' ? { laps: n, minutes: 0 } : { laps: 0, minutes: n };
  unlockAudio();
  api('race/start', params, `Start a ${n} ${raceKind === 'laps' ? 'lap' : 'minute'} race? The current session is cleared.`);
};
$('raceStop').onclick = () => {
  const done = $('raceStop').textContent === 'Back to training';
  api('race/stop', {}, done ? null : 'Stop the race? The laps stay.');
};

$('save').onclick = async () => {
  await api('config', { minLap: Math.round($('minLap').value * 1000), delta: $('delta').value });
  cfgLoaded = false;
};

$('wifiForm').onsubmit = async e => {
  e.preventDefault();
  const ssid = $('ssid').value.trim(), pass = $('pass').value;
  if (!ssid) { $('ssid').focus(); return; }
  const params = { ssid };
  if (pass || ssid !== S.ssid) params.pass = pass;     // otherwise the saved password stays
  if (await api('wifi', params)) { $('pass').value = ''; lastSsid = null; }
};

$('wifiForget').onclick = () =>
  api('wifi', { ssid: '', pass: '' }, `Forget ${S.ssid}? The timer stays on RC_Lap_Timer only.`);

$('ssid').oninput = updatePassHint;

$('scan').onclick = async () => {
  const btn = $('scan');
  if (!await api('wifi/scan')) {
    btn.textContent = 'Busy connecting, try again in a moment';
    setTimeout(() => { btn.textContent = 'Scan networks'; }, 3000);
    return;
  }
  btn.disabled = true;
  btn.textContent = 'Scanning…';
  let res = null;
  for (let i = 0; i < 20; i++) {                       // a scan takes ~2 s
    await new Promise(r => setTimeout(r, 700));
    try { res = await (await fetch('/api/wifi/scan', { cache: 'no-store' })).json(); } catch (e) { res = null; }
    if (res && !res.scanning) break;
  }
  btn.disabled = false;
  btn.textContent = 'Scan again';
  renderNets(res && !res.scanning ? res.networks : []);
};

$('nets').onclick = e => {
  const b = e.target.closest('button');
  if (!b) return;
  $('ssid').value = b.dataset.ssid;
  $('pass').value = '';
  updatePassHint();
  $('nets').hidden = true;
  if (b.dataset.free === '1') $('wifiForm').requestSubmit();
  else $('pass').focus();
};

connect();
tick();
