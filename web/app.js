// RC Lap Timer – client. Data over WebSocket (port 81), HTTP polling as fallback.
// One car (beam only) or several cars with IR transponders; training and races (race.js).
// Loaded after race.js, sound.js, wifi.js and raceview.js, which only define functions; everything starts at the end.

const $ = id => document.getElementById(id);
const fmt = ms => (ms / 1000).toFixed(3);
const esc = t => String(t).replace(/[&<>"']/g, c => `&#${c.charCodeAt(0)};`);
const WS_PORT = new URLSearchParams(location.search).get('ws') || 81;   // ?ws= for the mock server

const { L, racers } = Race;   // lap entry fields and the cars in a race, from race.js
const STATS_CAR = 0x100;
const NEC_ID_MIN = 0x1000;   // transponder ids from here on are RiCino codes, below are NE556 blink rates
const MAX_DRIVERS = 8;

let S = null;          // latest state from the server
let clockOffset = 0;   // server ms − performance.now()
let curAt = 0;         // performance.now() when per-car current times arrived
let curMs = [];        // lap in progress per car slot, from the server
let seen = -1;         // lap count in the previous state, to detect a new lap
let cfgLoaded = false; // settings form is filled only once so it doesn't overwrite typing
let sock = null, pollT = null;
let suggested = 0;     // sensitivity suggested by the sensor
let brokenSince = 0;   // performance.now() when the beam got broken, 0 = clear
let openLap = 0;       // session lap number whose actions are shown in the list
let openCar = -1;      // car slot whose actions are shown in the track table
let medians = {};      // stats key -> median of counted laps (3+ laps), for "short?"

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
const trackCars = () => racers(S);
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
  const [color, text] = { ws: ['var(--pb)', 'live'], poll: ['var(--flag)', 'slow link'], off: ['var(--stop)', 'offline'] }[state];
  $('dot').style.background = color;
  $('ctxt').textContent = text;
  $('conn').classList.toggle('live', state === 'ws');  // the word only shows when it isn't live
  document.body.classList.toggle('offline', state === 'off');   // times dim: they aren't live
  $('offlineWarn').hidden = state !== 'off';
}

function flash(isBest) {
  for (const id of ['curCard', 'trackCard', 'raceCard', 'bigView']) {
    const c = $(id);
    c.classList.remove('flash', 'b');
    void c.offsetWidth;                                // restart the animation
    c.classList.add('flash');
    if (isBest) c.classList.add('b');
  }
}

const racing = () => S && S.race.state !== 'off';

// ---------------------------------------------------------------- big view
// Everything in it is sized in em: start from 7 % of the shorter screen side and shrink until it all
// fits (phone upright or sideways, laptop, TV). Runs when its content changes, not every frame.
function fitBig() {
  const v = $('bigView');
  if (v.hidden) return;
  let f = Math.min(innerWidth, innerHeight) * 0.07;
  v.style.fontSize = f + 'px';
  const fits = () => v.scrollHeight <= v.clientHeight && v.scrollWidth <= v.clientWidth;
  for (let i = 0; i < 14 && !fits(); i++) { f *= 0.9; v.style.fontSize = f + 'px'; }
}
addEventListener('resize', fitBig);

// ---------------------------------------------------------------- tabs
// Training (default), Race (setup or the running race) and Settings. The laps list follows the
// race onto its tab, so laps can be fixed there too.
let tab = 'Training';
let wasRacing = null;  // race state in the previous state message, to open the Race tab when one starts

function showTab(name) {
  tab = name;
  for (const t of ['Training', 'Race', 'Settings']) $('tab' + t).hidden = t !== name;
  for (const b of $('tabs').children) b.classList.toggle('on', b.dataset.tab === name);
  placeLaps();
  try { sessionStorage.setItem('tab', name); } catch (e) { /* not kept */ }
  window.scrollTo(0, 0);
}

function placeLaps() {
  const inRace = tab === 'Race' && racing();
  const home = inRace ? $('tabRace') : $('tabTraining');
  if ($('lapsCard').parentElement !== home) home.insertBefore($('lapsCard'), inRace ? null : $('reset'));
  // Several cars: who drives which car is also set up before a race (one car: the heat order does it)
  const setup = tab === 'Race' && !racing() && multi();
  const dHome = setup ? $('tabRace') : $('tabTraining');
  if ($('driversCard').parentElement !== dHome) dHome.insertBefore($('driversCard'), setup ? null : $('lastBest'));
}

function renderLayout() {
  const m = multi(), r = racing();
  $('raceCard').hidden = !r;
  $('raceSetup').hidden = r;
  $('curCard').hidden = r || m;
  $('trackCard').hidden = r || !m;
  $('lastBest').hidden = m || r;
  $('raceLive').hidden = !r;
  // During a race the Race tab is where everything is (laps too); Training waits until it's over
  const tr = document.querySelector('#tabs [data-tab="Training"]');
  tr.disabled = r;
  tr.title = r ? 'A race is running' : '';
  if (r && tab === 'Training') showTab('Race');
  $('driversTitle').textContent = m ? 'Drivers' : 'Who\'s driving';
  if (wasRacing !== null && r && !wasRacing) showTab('Race');   // a race started, here or on another phone
  wasRacing = r;
  placeLaps();
}

// Drivers as chips. One car: tap a name to put them on the car. "+ Add driver" opens a name field
// in place; Edit turns the chips into remove buttons.
let editingDrivers = false;

function renderDrivers() {
  const m = multi();
  const car0 = carBySlot(0);
  if (!S.drivers.length) editingDrivers = false;
  $('driversCard').classList.toggle('editing', editingDrivers);
  $('editDrivers').hidden = !S.drivers.length;
  $('editDrivers').textContent = editingDrivers ? 'Done' : 'Edit';
  $('editDrivers').setAttribute('aria-pressed', editingDrivers);
  const canAdd = S.drivers.length < MAX_DRIVERS && $('addForm').hidden;
  $('chips').innerHTML = S.drivers.map(d => {
    const on = !m && car0 && d.id === car0.driver;
    return `<button class="chip${on ? ' on' : ''}" data-id="${d.id}"${editingDrivers ? ` aria-label="Remove ${esc(d.name)}"` : ''}>`
      + `${esc(d.name)}${editingDrivers ? '<span class="rm" aria-hidden="true">✕</span>' : ''}</button>`;
  }).join('') + (canAdd && !editingDrivers ? '<button class="chip add" data-add="1">+ Add driver</button>' : '');
  if (editingDrivers) $('dmsg').textContent = 'Tap a name to remove that driver. Their laps stay.';
  else if ($('dmsg').textContent.startsWith('Tap a name')) $('dmsg').textContent = '';
  if (!S.drivers.length && $('addForm').hidden) {
    $('dmsg').textContent = m ? 'Add drivers, then pick who drives which car.' : 'Add the drivers to time each of them separately.';
  }

  // several cars: who drives which car (a driver drives one car at a time)
  $('carDrivers').hidden = !m;
  const cars = S.cars.filter(c => c.slot > 0);
  $('carDrivers').innerHTML = !m ? '' : cars.length
    ? '<h3>Who drives which car</h3>' + cars.map(c => `<label>${esc(carLabel(c))}
        <select data-car="${c.slot}">
          <option value="0">Nobody</option>
          ${S.drivers.map(d => `<option value="${d.id}"${d.id === c.driver ? ' selected' : ''}>${esc(d.name)}</option>`).join('')}
        </select></label>`).join('')
    : '<p class="hint">No cars yet: drive a car with a transponder past the gate.</p>';
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
        <button data-undo="${c.slot}" ${c.canUndo ? '' : 'disabled'}>↶ Undo crossing</button>
        <button data-restart="${c.slot}">⟲ Restart lap</button></td></tr>`;
    }
    return row;
  }).join('') || '<tr><td colspan="6" class="hint">No cars yet: drive a car with a transponder past the gate.</td></tr>';
  $('trackHint').textContent = S.unident ? `Crossings without a transponder: ${S.unident}` : '';
}

function renderCars() {
  $('multi').checked = !!S.multi;
  const cars = S.cars.filter(c => c.slot > 0);
  $('csum').textContent = S.multi ? `${cars.length} car${cars.length === 1 ? '' : 's'}` : 'one car';
  if ($('cars').contains(document.activeElement)) return;   // don't rebuild while a name is being typed
  $('cars').innerHTML = cars.map(c => `<li>
      <span class="tx">${txLabel(c.tx)}</span>
      <input data-rename="${c.slot}" value="${esc(c.name)}" placeholder="name" maxlength="16">
      <button data-remove="${c.slot}" title="Remove">✕</button></li>`).join('');
}

function renderUsb() {
  const z = S.zround;
  $('zround').checked = z.on;
  $('usum').textContent = z.on ? (z.racing ? 'ZRound racing' : z.connected ? 'ZRound connected' : 'waiting for ZRound') : '';
  $('zstat').className = 'wstat ' + (z.on ? (z.connected ? 'ok' : 'busy') : '');
  $('zstat').textContent = !z.on ? 'USB port: debug log (115200 baud).'
    : z.racing ? 'ZRound is running a race, passings are sent.'
    : z.connected ? 'ZRound is connected.' : 'Waiting for ZRound to connect (19200 baud).';
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
  fitBig();
}

function onState(s) {
  const newLap = seen >= 0 && s.count > seen;
  seen = s.count; S = s;
  clockOffset = s.now - performance.now();
  curAt = performance.now();
  curMs = [];
  for (const c of s.cars) curMs[c.slot] = c.cur;
  computeMedians();

  const lastLap = s.laps.length ? s.laps[s.laps.length - 1] : null;
  const kind = lastLap ? lapKind(lastLap) : '';
  if (newLap && lastLap) {
    beep(kind !== '');
    if (!s.multi || kind === 'best') flash(kind === 'best');   // with several cars only a best lap flashes
    if (!(racing() && Race.isHeats(s))) announce(lastLap, kind);   // heats: raceview.js says it all at once
  }

  renderLayout();
  renderSingle();
  renderDrivers();
  renderBoard();
  renderLaps();
  renderTrack();
  raceOnState(s);
  renderCars();
  renderUsb();
  renderWifi();
  $('ign').textContent = s.ignored;

  if (!cfgLoaded) {
    $('minLap').value = s.minLap / 1000;
    $('delta').value = s.delta;
    raceFillSetup(s);
    cfgLoaded = true;
  }
  wifiShowSsid(s.ssid);
}

function onLive(l) {
  curMs = l.cur; curAt = performance.now();
  $('raw').textContent = l.raw;
  $('base').textContent = l.base;
  $('beam').textContent = l.broken ? 'broken' : 'clear';
  $('mr').style.left = (l.raw / 40.95) + '%';          // ADC 0–4095 → 0–100 %
  $('mb').style.left = (l.base / 40.95) + '%';
  wifiRssi(l.rssi);
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
    + (l.unident ? ` Crossings without a transponder: ${l.unident}.` : '');
}

// Band around idle where nothing triggers; the green marker leaving it = beam broken.
// Away from the suggestion, but how bad is it? Same limits as beamSuggestDelta() in src/beam.cpp:
// above 2·noise+50 noise can't trigger, below 0.7·depth a pass like the last one clears it with margin.
function tuneAdvice(delta, l) {
  const lo = 2 * l.noise + 50, hi = Math.round(l.peak * 0.7);
  if (delta >= l.peak) return `Sensitivity ${delta} misses crossings like the last one (depth ${l.peak}). Use ${l.suggest}.`;
  if (delta > hi) return `Sensitivity ${delta} works, but a shallower (faster) crossing could be missed. ${l.suggest} is safer.`;
  if (delta < lo) return `Sensitivity ${delta} is close to the noise (±${l.noise}): false triggers are likely. Use ${l.suggest}.`;
  return `Sensitivity ${delta} works. ${l.suggest} would leave more margin on both sides.`;
}

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
    : l.suggest > 0 ? tuneAdvice(S.delta, l)
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
  // A stale idle value looks the same: calibrated without the laser on the sensor (e.g. at power-up)
  const msg = `Beam blocked for ${secs} s, ${multi() ? 'passes are timed by transponder only' : 'laps aren\'t counted'}. `
    + 'Clear the gate or check the laser aim. If the laser is on the sensor, calibrate again.';
  for (const id of ['beamWarn', 'beamWarn2', 'beamWarn3', 'bvWarn']) {
    $(id).hidden = !blocked;
    if (blocked) (id === 'bvWarn' ? $(id) : $(id).firstElementChild).textContent = msg;
  }
  $('ssum').textContent = blocked ? 'beam blocked' : '';
  $('setAlert').hidden = !blocked;
}

// ---------------------------------------------------------------- every frame
function tick() {
  if (S) {
    const car0 = carBySlot(0);
    const who = car0 ? driverName(car0.driver) : '';
    const t0 = carCur(0);
    const time = t0 !== null ? fmt(t0) : '--.---';
    const lap = t0 !== null ? 'Lap ' + ((car0 ? car0.laps : 0) + 1) : 'Waiting for the first crossing';
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
        if (time.length !== $('bvCur').textContent.length) { $('bvCur').textContent = time; fitBig(); }   // 9.999 → 10.000
        else $('bvCur').textContent = time;
        $('bvLap').textContent = lap;
        $('bvDriver').textContent = who;
      }
    }
  }
  raceFrame();
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
$('undo').onclick    = () => api('undo', { car: 0 });
$('restart').onclick = () => api('restart', { car: 0 });
$('reset').onclick   = () => api('reset', {}, 'Start a new session? All laps and the leaderboard are cleared, drivers and cars stay.');
$('calibrate').onclick = () => api('calibrate');
for (const b of document.querySelectorAll('.calnow')) b.onclick = () => api('calibrate');
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
  const b = e.target.closest('button');
  if (!b) return;
  if (b.dataset.add) { openAddDriver(); return; }
  const id = +b.dataset.id;
  if (editingDrivers) { api('driver/remove', { id }, `Remove ${driverName(id)}? Their laps stay in the list.`); return; }
  const car0 = carBySlot(0);
  if (!multi() && car0 && id !== car0.driver) {
    api('driver/select', { id, car: 0 });
    $('dmsg').textContent = car0.running ? `${driverName(id)} is driving. Their lap starts at the next pass.` : '';
  }
};

$('editDrivers').onclick = () => { editingDrivers = !editingDrivers; closeAddDriver(); };

function openAddDriver() {
  $('addForm').hidden = false;
  $('dmsg').textContent = '';
  renderDrivers();
  $('newName').focus();
}
function closeAddDriver() {
  $('addForm').hidden = true;
  $('newName').value = '';
  renderDrivers();
}
$('addCancel').onclick = closeAddDriver;

$('carDrivers').onchange = e => {
  const sel = e.target.closest('select');
  if (sel) api('driver/select', { id: sel.value, car: sel.dataset.car });
};

$('addForm').onsubmit = async e => {
  e.preventDefault();
  const name = $('newName').value.trim();
  if (!name) return;
  if (await api('driver/add', { name })) {
    $('dmsg').textContent = '';
    closeAddDriver();
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

$('save').onclick = async () => {
  await api('config', { minLap: Math.round($('minLap').value * 1000), delta: $('delta').value });
  cfgLoaded = false;
};

$('tabs').onclick = e => { const b = e.target.closest('button'); if (b) showTab(b.dataset.tab); };
try { const t = sessionStorage.getItem('tab'); if (t && $('tab' + t)) showTab(t); } catch (e) { /* not kept */ }

soundInit();
wifiInit();
raceViewInit();
connect();
tick();
