// RC Lap Timer – race card (instruction, live time, standings), race voice, race setup and the big view.
// Part of the client, see app.js; standings come from race.js.
//
// The race card has one big instruction (setCall): who does what right now. Everything else on the card
// is information that doesn't repeat it: the title stays put ("Heat 2 of 3", "Race · 10 laps").

let raceKind = 'laps';
let heatPick = null;   // heats: driver ids in the chosen order, null = all drivers in list order
let heatAdded = '';    // name just added in race setup: it joins the end of the order
let prepSec = 0;       // heats: time between drivers, 0 = a Start button (remembered per phone)
try { prepSec = +localStorage.getItem('prep') || 0; } catch (e) { /* not kept */ }

const clock = ms => {
  const s = Math.floor(ms / 1000);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
};
const signed = ms => (ms < 0 ? '−' : '+') + fmt(Math.abs(ms));
const plural = (n, word) => `${n} ${word}${n === 1 ? '' : 's'}`;

// Who a standings row is: the driver in heats, else whoever is on the car.
const rowName = x => x.heat !== undefined ? driverName(x.driver) : whoOn(x.car);

// The instruction. kind: 'ready' (yellow: act now – line up, get ready, final lap, finish),
// 'go' (green), 'done' or '' (plain).
let call = { kind: '', main: '' };
function setCall(kind, main, sub) {
  call = { kind, main };
  $('raceCall').className = 'call' + (kind ? ' ' + kind : '');
  $('callMain').textContent = main;
  $('callSub').textContent = sub || '';
}

// Countdown digits only: 3, 2, 1, GO (longer prep times count down from their seconds)
function renderCountdown(ms) {
  $('countdown').hidden = ms === null;
  if (ms !== null) { const s = Math.ceil(ms / 1000); $('countdown').textContent = s > 0 ? s : 'GO'; }
}

// Recomputed on every state and a few times per second for the clock.
function renderRace() {
  if (!racing()) return null;
  const r = Race.standings(S, serverNow());
  $('standings').parentElement.hidden = r.phase === 'countdown';
  $('raceUndo').hidden = multi();
  const car0 = carBySlot(0);
  $('raceUndo').disabled = !(car0 && car0.canUndo);
  $('raceAgain').hidden = !r.allIn;
  $('raceStop').textContent = r.allIn ? 'Back to training' : 'Stop race';
  $('raceStop').className = 'wide' + (r.allIn ? '' : ' danger');
  if (r.heats) renderHeats(r); else renderLapsRace(r);
  return r;
}

// Several cars, or one car without drivers in turn: a laps or a time race.
function renderLapsRace(r) {
  $('heatNow').hidden = $('hnNext').hidden = $('heatStart').hidden = true;
  const N = S.race.laps;
  const lead = r.rows[0];
  $('raceTitle').textContent = N > 0 ? `Race · ${plural(N, 'lap')}` : `Race · ${S.race.minutes} min`;
  $('raceClock').textContent = r.phase !== 'running' ? ''
    : r.finished ? clock(r.finishMs)                    // stopped at the winner's race time
    : r.remainingMs !== null ? clock(r.remainingMs) : clock(r.elapsedMs);
  renderCountdown(r.phase === 'countdown' ? r.countdownMs : null);

  const one = !multi();
  if (r.phase === 'countdown') setCall('ready', 'Line up', `${one ? 'The' : 'Each car\'s'} time starts at the first crossing after GO.`);
  else if (r.allIn) setCall('done', 'Race finished', lead && lead.started ? `${rowName(lead)} wins` : '');
  else if (r.finished) setCall('ready', 'Finish', `${one ? 'The car finishes' : 'Every car finishes'} at its next crossing.`);
  else if (r.rows.some(x => !x.started)) setCall('go', 'GO', `${one ? 'The' : 'Each car\'s'} time starts at the first crossing.`);
  else if (N > 0) setCall('', `Lap ${Math.min((lead ? lead.laps : 0) + 1, N)} of ${N}`, '');
  else setCall('', `${clock(r.remainingMs || 0)} left`, '');

  $('standLast').textContent = 'Time';
  $('standGap').textContent = 'Gap';
  $('standings').innerHTML = r.phase === 'countdown' ? '' : r.rows.map(x => {
    const gap = !x.started ? '' : x.dnf ? 'DNF' : x.pos === 1 ? ''
      : x.gapLaps ? `+${plural(x.gapLaps, 'lap')}` : x.gapMs ? '+' + fmt(x.gapMs) : '';
    return `<tr class="${x.pos === 1 && x.started ? 'lead' : ''}">
      <td>${x.started ? x.pos + '.' : ''}</td>
      <td>${esc(rowName(x))}${x.finished ? ' ✓' : ''}</td>
      <td>${x.laps}</td>
      <td>${x.laps ? fmt(x.totalMs) : '–'}</td>
      <td>${x.bestMs ? fmt(x.bestMs) : '–'}</td>
      <td>${x.started ? gap : 'not started'}</td></tr>`;
  }).join('');
  $('raceHint').textContent = '';
}

// Heats: one car, the drivers in turn. Between drivers the Start button is the main thing.
function renderHeats(r) {
  const cur = r.current;
  const n = S.race.order.length;
  const N = S.race.laps;
  const counting = r.phase === 'countdown';
  const between = r.phase === 'running' && !r.allIn && r.heatState !== 'running';
  const heatSecs = Math.ceil(r.heatCountdownMs / 1000);
  $('raceTitle').textContent = r.allIn ? `Results · ${plural(N, 'lap')} each`
    : counting ? `${plural(n, 'driver')} · ${plural(N, 'lap')} each` : `Heat ${S.race.heat + 1} of ${n}`;
  $('raceClock').textContent = '';
  renderCountdown(counting ? r.countdownMs : r.heatState === 'countdown' && !r.allIn ? r.heatCountdownMs : null);

  const name = cur ? rowName(cur) : '';
  const left = cur ? N - cur.laps : 0;
  const nextName = r.next ? driverName(r.next) : '';
  if (counting) setCall('ready', `${driverName(S.race.order[0])}, line up`, 'Your time starts at the first crossing after GO.');
  else if (r.allIn) setCall('done', 'Race finished', r.rows[0] ? `${rowName(r.rows[0])} wins` : '');
  else if (r.heatState === 'waiting') setCall('ready', `${name}, take the transmitter`, 'Tap Start when you are ready.');
  else if (r.heatState === 'countdown') setCall('ready', `${name}, get ready`,
    heatSecs > 3 ? `Starts in ${heatSecs} s` : 'Your time starts at the first crossing after GO.');
  else if (!cur.started) setCall('go', `${name}, GO`, 'Your time starts when the car crosses the line.');
  else if (left === 1) setCall('ready', `${name}: final lap`, nextName ? `${nextName}, get ready.` : '');
  else setCall('', `${name}: ${plural(left, 'lap')} to go`, '');

  // Start the next driver: while waiting, or earlier than the prep time
  $('heatStart').hidden = !(between && (r.heatState === 'waiting' || heatSecs > 4));
  $('heatStart').textContent = r.heatState === 'waiting' ? `Start ${name}` : `Start ${name} now`;

  // The driver on the track: live lap, total, last, best, split (not between drivers)
  $('heatNow').hidden = !cur || between || counting;
  if (cur) {
    $('hnLast').textContent = cur.laps ? fmt(cur.lapMs[cur.laps - 1]) : '–';
    $('hnBest').textContent = cur.bestMs ? fmt(cur.bestMs) : '–';
    $('hnVs').textContent = r.vs ? `vs ${driverName(r.vs)}` : 'Split';
    $('hnSplit').textContent = r.splitMs !== undefined ? signed(r.splitMs) : '–';
    $('hnSplit').className = r.splitMs === undefined ? '' : r.splitMs <= 0 ? 'ahead' : 'behind';
  }

  // The order after the driver on the track (before the start: after the first one)
  const queue = (counting ? S.race.order.slice(1) : [r.next, ...r.after].filter(Boolean)).map(driverName);
  $('hnNext').hidden = r.allIn || !queue.length;
  $('hnNext').innerHTML = !queue.length ? '' : `${between ? 'Then' : 'Up next'}: <b>${esc(queue[0])}</b>`
    + (queue.length > 1 ? `<span class="then">then ${esc(queue.slice(1).join(', '))}</span>` : '');

  $('standLast').textContent = 'Total';
  $('standGap').textContent = 'Result';
  $('standings').innerHTML = counting ? '' : r.rows.map(x => {
    const full = x.finished && x.laps === N;
    return `<tr class="${x.pos === 1 && full ? 'lead' : ''}${x.current ? ' now' : ''}">
      <td>${full ? x.pos + '.' : ''}</td>
      <td>${esc(rowName(x))}</td>
      <td>${x.laps}</td>
      <td>${x.laps ? fmt(x.totalMs) : '–'}</td>
      <td>${x.bestMs ? fmt(x.bestMs) : '–'}</td>
      <td>${full ? (x.pos === 1 ? '✓' : '+' + fmt(x.gapMs)) : x.finished ? 'stopped' : ''}</td></tr>`;
  }).join('');
  $('raceHint').textContent = between ? 'Crossings don\'t count until the next GO.' : '';
  lastHeats = r;
  raceFrame();
}

// Every frame (from tick() in app.js): the running lap and total of the driver on the track, to the ms.
let lastHeats = null;
function raceFrame() {
  const r = lastHeats;
  if (!r || !r.current || !racing() || !Race.isHeats(S)) return;
  const t0 = r.current.started && r.heatState === 'running' ? carCur(0) : null;
  const cur = t0 !== null ? fmt(t0) : '--.---';
  const total = r.current.started ? fmt(r.current.totalMs + (t0 || 0)) : '–';
  $('hnCur').textContent = cur;
  $('hnTotal').textContent = total;
  if (!$('bigView').hidden) {
    const grew = cur.length !== $('bvHCur').textContent.length;
    $('bvHCur').textContent = cur;
    $('bvHTotal').textContent = total;
    if (grew) fitBig();
  }
}

function raceOnState(s) {
  if (s.race.state === 'off') rv = null;
  renderRace();
  renderRaceKind();
}

// Race clock, voice and the big view: a few times per second is enough.
function raceTick() {
  if (!S) return;
  const r = renderRace();
  raceVoice(r);
  if (!$('bigView').hidden && (multi() || racing())) renderBigTable(r);
}

// Voice in races, few short messages that queue up (sound.js), nothing else:
//   countdown: (heats: the driver's name,) 3, 2, 1, go
//   heats: each lap's time (app.js leaves heat laps to this), "final lap", "<next driver> next"
//   the end: "race finished"
// Other races' laps are read by app.js like in training.
let rv = null;         // what was said: { counting, count, named, laps, heat, done }

function raceVoice(r) {
  if (!r) { rv = null; return; }
  const heat = S.race.heat;
  const laps = r.heats ? countedLaps(0).filter(l => l[L.END] > S.race.go).length : 0;
  if (!rv) rv = { counting: false, count: 0, named: -1, laps, heat, done: false };

  const cdMs = r.phase === 'countdown' ? r.countdownMs : r.heats && r.heatState === 'countdown' ? r.heatCountdownMs : null;
  if (cdMs !== null) {
    const secs = Math.ceil(cdMs / 1000);
    const who = r.heats && (r.phase === 'countdown' ? S.race.order[0] : r.current && r.current.driver);
    if (who && secs <= 4 && rv.named !== heat) { rv.named = heat; say(driverName(who)); }
    if (secs <= 3 && secs > 0 && secs !== rv.count) { rv.count = secs; say(String(secs)); tone(660, .15); }
    rv.counting = true;
  } else if (rv.counting) {
    rv.counting = false;
    rv.count = 0;
    say('Go'); tone(1320, .5);
    if (navigator.vibrate) navigator.vibrate(300);
  }

  if (r.heats && laps > rv.laps) {                      // undo makes laps go down: nothing to say
    const lap = countedLaps(0).pop();
    let text = lapText(lap, lapKind(lap), false);
    if (heat > rv.heat && !r.allIn && r.current) text += `. ${rowName(r.current)} next`;
    else if (heat === rv.heat && r.current && r.current.started && S.race.laps - r.current.laps === 1) text += '. Final lap';
    say(text);
  }
  if (r.finished && !rv.done) { rv.done = true; say('Race finished'); }
  if (!r.finished) rv.done = false;                     // e.g. the last crossing was undone
  rv.laps = laps;
  rv.heat = heat;
}

// Big view: the title, the instruction, and in heats the driver's live lap, total and who's next
// (the standings stay on the card); otherwise a table. fitBig() makes it fit the screen.
function renderBigTable(r) {
  // heats: the driver on the track (not between drivers: then the instruction and Start are the point)
  const hn = r && r.heats && r.current && !r.allIn && r.phase === 'running' && r.heatState === 'running';
  const between = r && r.heats && !r.allIn && r.phase === 'running' && r.heatState !== 'running';
  $('bvHeat').hidden = !hn;
  $('bvRows').parentElement.hidden = !!hn || !!between || (r && r.phase === 'countdown');
  $('bvStart').hidden = $('heatStart').hidden;
  $('bvStart').textContent = $('heatStart').textContent;
  if (r) {
    $('bvTitle').textContent = $('raceTitle').textContent;
    $('bvCall').textContent = call.main;
    $('bvCall').className = 'bv-call' + (call.kind ? ' ' + call.kind : '');
    $('bvClock').textContent = $('raceClock').textContent;
    $('bvCount').hidden = $('countdown').hidden;
    $('bvCount').textContent = $('countdown').textContent;
    if (hn) $('bvHNext').textContent = r.next ? driverName(r.next) : '–';
    else $('bvRows').innerHTML = r.rows.filter(x => x.started).map(x => {
      const full = r.heats ? x.finished && x.laps === S.race.laps : x.finished;
      const right = r.heats ? fmt(x.totalMs) : x.dnf ? 'DNF' : x.pos === 1 ? '' : x.gapLaps ? '+' + x.gapLaps + 'L' : '+' + fmt(x.gapMs);
      return `<tr class="${x.pos === 1 && full ? 'lead' : ''}">
        <td>${full || !r.heats ? x.pos + '.' : ''}</td><td class="name">${esc(rowName(x))}</td><td>${x.laps}</td><td>${right}</td></tr>`;
    }).join('');
  } else {
    $('bvCall').textContent = '';
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
  fitBig();
}

// ---------------------------------------------------------------- race setup
function renderRaceKind() {
  for (const b of $('raceKind').children) b.classList.toggle('on', b.dataset.kind === raceKind);
  $('raceLenLbl').textContent = raceKind === 'laps' ? (heatOrder().length ? 'Laps per driver' : 'Number of laps') : 'Minutes';
  renderHeatOrder();
}

// Heats need one car, a laps race and drivers; the setup offers to add drivers when there are none.
// The order is the tap order; drivers tapped out sit out. Drivers are removed on the Training tab only.
const heatsPossible = () => S && !multi() && raceKind === 'laps' && S.drivers.length > 0;
const heatSetupShown = () => S && !multi() && raceKind === 'laps';
const heatOrder = () => !heatsPossible() ? []
  : heatPick ? heatPick.filter(id => S.drivers.some(d => d.id === id)) : S.drivers.map(d => d.id);

function renderHeatOrder() {
  const heats = heatOrder().length > 0;
  $('heatSetup').hidden = !heatSetupShown();
  $('rulesHeats').hidden = !heats;
  $('rulesLaps').hidden = heats || raceKind !== 'laps';
  $('rulesTime').hidden = raceKind !== 'minutes';
  if (!heatSetupShown()) return;
  const added = heatAdded && S.drivers.find(d => d.name === heatAdded);
  if (added) { if (heatPick && !heatPick.includes(added.id)) heatPick.push(added.id); heatAdded = ''; }
  for (const b of $('prepKind').children) b.classList.toggle('on', +b.dataset.prep === prepSec);
  const order = heatOrder();
  const rest = S.drivers.filter(d => !order.includes(d.id));
  const canAdd = S.drivers.length < MAX_DRIVERS && $('raceAddForm').hidden;
  $('heatOrder').innerHTML = order.map((id, i) => `<button type="button" class="chip on" data-id="${id}">${i + 1}. ${esc(driverName(id))}</button>`)
    .concat(rest.map(d => `<button type="button" class="chip" data-id="${d.id}">${esc(d.name)}</button>`))
    .concat(canAdd ? ['<button type="button" class="chip add" data-add="1">+ Add driver</button>'] : []).join('');
  if (!S.drivers.length && $('raceAddForm').hidden) {
    $('raceDmsg').textContent = 'Add drivers to race them in turn, or start without drivers and the car races alone.';
  } else if ($('raceDmsg').textContent.startsWith('Add drivers')) $('raceDmsg').textContent = '';
}

function openRaceAdd() {
  $('raceAddForm').hidden = false;
  renderHeatOrder();
  $('raceNewName').focus();
}
function closeRaceAdd() {
  $('raceAddForm').hidden = true;
  $('raceNewName').value = '';
  renderHeatOrder();
}

// Race setup form from the last race's settings (once, like the settings form).
function raceFillSetup(s) {
  raceKind = s.race.setMin && !s.race.setLaps ? 'minutes' : 'laps';
  $('raceLen').value = raceKind === 'minutes' ? s.race.setMin : s.race.setLaps;
  renderRaceKind();
}

function closeBig() {
  $('bigView').hidden = true;
  if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
}

function raceViewInit() {
  setInterval(raceTick, 250);
  $('bigOpen').onclick = () => {
    $('bigView').hidden = false;
    if (racing() || multi()) renderBigTable(renderRace());
    fitBig();
    const el = document.documentElement;                // real fullscreen where the browser allows it
    if (el.requestFullscreen) el.requestFullscreen().catch(() => {});
  };
  $('bvClose').onclick = closeBig;                      // only the ✕ closes: a propped-up phone gets bumped
  addEventListener('keydown', e => { if (e.key === 'Escape' && !$('bigView').hidden) closeBig(); });
  $('raceKind').onclick = e => {
    const kind = e.target.dataset.kind;
    if (!kind) return;
    raceKind = kind;
    $('raceLen').value = kind === 'laps' ? (S.race.setLaps || 10) : (S.race.setMin || 5);
    renderRaceKind();
  };
  $('heatOrder').onclick = e => {
    const b = e.target.closest('button');
    if (!b) return;
    if (b.dataset.add) { openRaceAdd(); return; }
    const id = +b.dataset.id;
    if (!id) return;
    const order = heatOrder();
    heatPick = order.includes(id) ? order.filter(x => x !== id) : [...order, id];
    renderRaceKind();
  };
  $('raceAddCancel').onclick = closeRaceAdd;
  $('raceAddForm').onsubmit = async e => {
    e.preventDefault();
    const name = $('raceNewName').value.trim();
    if (!name) return;
    if (await api('driver/add', { name })) {
      heatAdded = name;
      closeRaceAdd();
    } else {
      $('raceDmsg').textContent = `Couldn't add "${name}": the name is taken or there are already ${MAX_DRIVERS} drivers.`;
    }
  };
  $('raceStart').onclick = () => {
    const n = Math.max(1, Math.round(+$('raceLen').value || 0));
    const params = raceKind === 'laps' ? { laps: n, minutes: 0 } : { laps: 0, minutes: n };
    const order = heatOrder();
    if (order.length) { params.order = order.join(','); params.prep = prepSec; }
    unlockAudio();
    api('race/start', params, 'Start the race? The laps of this session are cleared.');
  };
  $('raceUndo').onclick = () => api('undo', { car: 0 });
  const startNext = () => { unlockAudio(); api('race/next'); };
  $('heatStart').onclick = startNext;
  $('bvStart').onclick = startNext;
  $('prepKind').onclick = e => {
    const v = e.target.dataset.prep;
    if (v === undefined) return;
    prepSec = +v;
    try { localStorage.setItem('prep', prepSec); } catch (err) { /* not kept */ }
    renderRaceKind();
  };
  $('raceAgain').onclick = () => {                    // the same race: length and heat order
    const params = { laps: S.race.laps, minutes: S.race.minutes };
    if (S.race.order.length) { params.order = S.race.order.join(','); params.prep = S.race.prep; }
    unlockAudio();
    api('race/start', params, 'Start the same race again? These results are cleared.');
  };
  $('raceStop').onclick = async () => {
    const r = Race.standings(S, serverNow());
    if (await api('race/stop', {}, r.allIn ? null : 'Stop the race? The laps stay.')) showTab('Training');
  };
  $('raceSetup').onclick = e => {                     // − / + around the race length
    const step = +(e.target.dataset.step || 0);
    if (!step) return;
    const max = raceKind === 'laps' ? 999 : 600;
    $('raceLen').value = Math.min(max, Math.max(1, Math.round(+$('raceLen').value || 0) + step));
  };
}
