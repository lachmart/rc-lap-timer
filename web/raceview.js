// RC Lap Timer – race view (standings, clock, countdown voice), race setup and the big view overlay.
// Part of the client, see app.js; standings come from race.js.

let raceKind = 'laps';
let lastCount = null;  // countdown second last announced
let finishSaid = false;
let heatSaid = -1;     // heat whose driver was last called up
let finalSaid = -1;    // heat whose final lap was announced
let heatPick = null;   // heats: driver ids in the chosen order, null = all drivers in list order
let heatEditing = false;  // race setup: driver chips remove drivers
let heatAdded = '';    // name just added in race setup: it joins the end of the order

const clock = ms => {
  const s = Math.floor(ms / 1000);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
};

// Who a standings row is: the driver in heats, else whoever is on the car.
const rowName = x => x.heat !== undefined ? driverName(x.driver) : whoOn(x.car);

// Race standings: recomputed on every state and a few times per second for the clock.
function renderRace() {
  if (!racing()) return null;
  const r = Race.standings(S, serverNow());
  $('standLast').textContent = r.heats ? 'Total' : 'Last';
  $('standings').parentElement.hidden = r.phase === 'countdown';
  queueMicrotask(() => { $('raceBannerTxt').textContent = $('raceTitle').textContent; });
  renderRaceUndo();
  if (r.heats) return renderHeats(r);
  $('heatNow').hidden = $('hnNext').hidden = true;
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
  $('raceAgain').hidden = !r.allIn;
  return r;
}

// One car: undo the last pass from the race card (in heats also the handover it caused).
function renderRaceUndo() {
  const car0 = carBySlot(0);
  $('raceUndo').hidden = multi();
  $('raceUndo').disabled = !(car0 && car0.canUndo);
}

// Heats: one car, the drivers in turn. The card shows who drives (live lap and total to the ms),
// what comes next for them, and who has to get ready.
const signed = ms => (ms < 0 ? '−' : '+') + fmt(Math.abs(ms));

function renderHeats(r) {
  const cur = r.current;
  const n = S.race.order.length;
  const N = S.race.laps;
  const counting = r.phase === 'countdown';
  $('raceTitle').textContent = counting ? `Get ready · ${n} heat${n > 1 ? 's' : ''} × ${N} lap${N > 1 ? 's' : ''}`
    : r.allIn ? 'Race finished' : `Heat ${S.race.heat + 1} / ${n}`;
  const secs = Math.ceil(r.countdownMs / 1000);
  $('countdown').hidden = !counting;
  $('countdown').textContent = secs > 3 ? 'Ready' : secs > 0 ? secs : 'GO';

  $('heatNow').hidden = !cur;
  if (cur) {
    $('hnName').textContent = rowName(cur);
    $('hnLap').textContent = cur.started ? `lap ${Math.min(cur.laps + 1, N)} / ${N}` : 'not started';
    $('hnLast').textContent = cur.laps ? fmt(cur.lapMs[cur.laps - 1]) : '–';
    $('hnBest').textContent = cur.bestMs ? fmt(cur.bestMs) : '–';
    $('hnVs').textContent = r.vs ? `vs ${driverName(r.vs)}` : 'Split';
    $('hnSplit').textContent = r.splitMs !== undefined ? signed(r.splitMs) : '–';
    $('hnSplit').className = r.splitMs === undefined ? '' : r.splitMs <= 0 ? 'ahead' : 'behind';
    const left = N - cur.laps;
    const final = cur.started && left === 1;
    $('hnMsg').textContent = !cur.started ? (S.race.heat ? 'Take the transmitter – the time starts at the next crossing'
      : 'The time starts at the first crossing')
      : final ? 'FINAL LAP' : `${left} laps to go`;
    $('hnMsg').className = 'hn-msg' + (final ? ' final' : '');
  }

  // Who's next: highlighted while the driver before them is on the final lap (or before "go" for the first)
  const next = r.next;
  const ready = counting || (cur && cur.started && N - cur.laps === 1);
  $('hnNext').hidden = r.allIn || (!next && !counting);
  $('hnNext').className = 'hn-next' + (next && ready ? ' ready' : '');
  const then = r.after.map(driverName).join(', ');
  $('hnNext').innerHTML = !next ? '' : `${counting ? 'First' : 'Next'}: <b>${esc(driverName(next))}</b>${ready ? ' – get ready!' : ''}`
    + (then ? `<span class="then">then ${esc(then)}</span>` : '');
  nextLine = !next || r.allIn ? '' : `${counting ? 'First' : 'Next'}: ${driverName(next)}${ready ? ' – get ready!' : ''}`;

  $('raceClock').textContent = '';
  $('standings').innerHTML = counting ? '' : standingsRows(r);
  $('raceHint').textContent = counting ? 'Crossings before "go" are ignored. Each driver\'s time starts at a crossing.' : '';
  $('raceStop').textContent = r.allIn ? 'Back to training' : 'Stop race';
  $('raceAgain').hidden = !r.allIn;
  lastHeats = r;
  raceFrame();
  return r;
}

// Every frame (from tick() in app.js): the running lap and total of the driver on the track, to the ms.
let lastHeats = null;
let nextLine = '';     // "Next: Tom – get ready!" for the big view
function raceFrame() {
  const r = lastHeats;
  if (!r || !r.current || !racing() || !Race.isHeats(S)) return;
  const t0 = r.current.started ? carCur(0) : null;
  const cur = t0 !== null ? fmt(t0) : '--.---';
  const total = r.current.started ? fmt(r.current.totalMs + (t0 || 0)) : '–';
  $('hnCur').textContent = cur;
  $('hnTotal').textContent = total;
  if (!$('bigView').hidden) {
    $('bvHCur').textContent = cur;
    $('bvHTotal').textContent = r.current.started ? 'Σ ' + total : '';
  }
}

function standingsRows(r) {
  if (r.heats) return r.rows.map(x => {
    const full = x.finished && x.laps === S.race.laps;
    const res = full ? (x.pos === 1 ? fmt(x.totalMs) : '+' + fmt(x.gapMs)) : x.current ? 'driving' : x.finished ? 'stopped' : 'waiting';
    return `<tr class="${x.pos === 1 && full ? 'lead' : ''}">
      <td>${full ? x.pos + '.' : ''}</td>
      <td>${esc(rowName(x))}${full ? ' 🏁' : ''}</td>
      <td>${x.laps}</td>
      <td>${x.laps ? fmt(x.totalMs) : '–'}</td>
      <td>${x.bestMs ? fmt(x.bestMs) : '–'}</td>
      <td>${res}</td></tr>`;
  }).join('');
  return r.rows.map(x => {
    const last = S.laps.filter(l => l[L.CAR] === x.car && !l[L.DELETED] && l[L.END] > S.race.go).pop();
    const gap = x.pos === 1 ? '' : x.gapLaps ? `+${x.gapLaps} lap${x.gapLaps > 1 ? 's' : ''}` : x.gapMs ? '+' + fmt(x.gapMs) : '';
    return `<tr class="${x.pos === 1 && x.started ? 'lead' : ''}">
      <td>${x.started ? x.pos + '.' : ''}</td>
      <td>${esc(rowName(x))}${x.finished ? ' 🏁' : ''}</td>
      <td>${x.laps}</td>
      <td>${last ? fmt(last[L.MS]) : '–'}</td>
      <td>${x.bestMs ? fmt(x.bestMs) : '–'}</td>
      <td>${x.started ? gap : 'not started'}</td></tr>`;
  }).join('');
}

function raceOnState(s) {
  if (s.race.state !== 'running') { finishSaid = false; heatSaid = -1; finalSaid = -1; }
  renderRace();
  renderRaceKind();
}

// Race clock, countdown voice and the big table: a few times per second is enough.
function raceTick() {
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
  if (r && r.heats && r.phase === 'running') heatVoice(r);
  if (r && r.finished && !finishSaid) {
    finishSaid = true;
    say(`Race finished. Winner, ${rowName(r.rows[0])}`, 'queue');
  }
  if (!$('bigView').hidden && (multi() || racing())) renderBigTable(r);
}

// Heats: the result of the heat just finished, the next driver up, the final lap.
function heatVoice(r) {
  const heat = S.race.heat;
  if (heat !== heatSaid) {                              // the timer handed the car over
    if (heatSaid >= 0) {
      const done = r.rows.find(x => x.heat === heatSaid);
      if (done && done.laps === S.race.laps) {
        say(`${rowName(done)}, ${(done.totalMs / 1000).toFixed(1)} seconds, position ${done.pos}`);
      }
    }
    heatSaid = heat;
    if (r.current) say(`${rowName(r.current)}, your turn`, 'queue');
  }
  const cur = r.current;
  if (cur && cur.started && S.race.laps - cur.laps === 1 && finalSaid !== heat) {
    finalSaid = heat;
    say('Final lap' + (r.next ? `. ${driverName(r.next)}, get ready` : ''), 'queue');
    tone(990, .2);
  }
}

function renderBigTable(r) {
  if (r) {
    const hn = r.heats && r.current;
    $('bvTitle').textContent = $('raceTitle').textContent + (hn ? ` · ${rowName(r.current)} · ${$('hnLap').textContent}` : '');
    $('bvClock').textContent = $('raceClock').textContent;
    $('bvCount').hidden = $('countdown').hidden;
    $('bvCount').textContent = $('countdown').textContent;
    $('bvHeat').hidden = !hn;
    if (hn) {
      $('bvHMsg').textContent = $('hnMsg').textContent;
      $('bvHNext').textContent = nextLine;
      $('bvHNext').className = 'bv-next' + ($('hnNext').classList.contains('ready') ? ' ready' : '');
    }
    $('bvRows').innerHTML = r.heats ? r.rows.filter(x => x.started).map(x => {
      const full = x.finished && x.laps === S.race.laps;
      return `<tr class="${x.pos === 1 && full ? 'lead' : ''}">
        <td>${full ? x.pos + '.' : ''}</td><td class="name">${esc(rowName(x))}${full ? ' 🏁' : ''}</td><td>${x.laps}</td>
        <td>${full ? (x.pos === 1 ? fmt(x.totalMs) : '+' + fmt(x.gapMs)) : fmt(x.totalMs)}</td></tr>`;
    }).join('') : r.rows.filter(x => x.started).map(x => `<tr class="${x.pos === 1 ? 'lead' : ''}">
      <td>${x.pos}.</td><td class="name">${esc(rowName(x))}${x.finished ? ' 🏁' : ''}</td><td>${x.laps}</td>
      <td>${x.pos === 1 ? '' : x.gapLaps ? '+' + x.gapLaps + 'L' : '+' + fmt(x.gapMs)}</td></tr>`).join('');
  } else {
    $('bvHeat').hidden = true;
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

function renderRaceKind() {
  for (const b of $('raceKind').children) b.classList.toggle('on', b.dataset.kind === raceKind);
  $('raceLenLbl').textContent = raceKind === 'laps' ? (heatOrder().length ? 'Laps per driver' : 'Number of laps') : 'Minutes';
  renderHeatOrder();
}

// Heats need one car, a laps race and drivers. The order is the tap order; untapped drivers sit out.
// Heats need one car, a laps race and drivers; the setup offers to add drivers when there are none.
const heatsPossible = () => S && !multi() && raceKind === 'laps' && S.drivers.length > 0;
const heatSetupShown = () => S && !multi() && raceKind === 'laps';
const heatOrder = () => !heatsPossible() ? []
  : heatPick ? heatPick.filter(id => S.drivers.some(d => d.id === id)) : S.drivers.map(d => d.id);

function renderHeatOrder() {
  $('heatSetup').hidden = !heatSetupShown();
  $('raceRules').hidden = heatOrder().length > 0;     // the several-cars rules don't apply to heats
  if (!heatSetupShown()) return;
  const added = heatAdded && S.drivers.find(d => d.name === heatAdded);
  if (added) { if (heatPick && !heatPick.includes(added.id)) heatPick.push(added.id); heatAdded = ''; }
  if (!S.drivers.length) heatEditing = false;
  $('heatSetup').classList.toggle('editing', heatEditing);
  $('heatEdit').hidden = !S.drivers.length;
  $('heatEdit').textContent = heatEditing ? 'Done' : 'Edit';
  $('heatEdit').setAttribute('aria-pressed', heatEditing);
  const order = heatOrder();
  const rest = S.drivers.filter(d => !order.includes(d.id));
  const rm = heatEditing ? '<span class="rm" aria-hidden="true">✕</span>' : '';
  const canAdd = !heatEditing && S.drivers.length < MAX_DRIVERS && $('raceAddForm').hidden;
  $('heatOrder').innerHTML = order.map((id, i) => `<button type="button" class="chip${heatEditing ? '' : ' on'}" data-id="${id}">${heatEditing ? '' : i + 1 + '. '}${esc(driverName(id))}${rm}</button>`)
    .concat(rest.map(d => `<button type="button" class="chip" data-id="${d.id}">${esc(d.name)}${rm}</button>`))
    .concat(canAdd ? ['<button type="button" class="chip add" data-add="1">+ Add driver</button>'] : []).join('');
  $('raceDmsg').textContent = heatEditing ? 'Tap a name to remove that driver. Their laps stay.'
    : !S.drivers.length && $('raceAddForm').hidden ? 'Add drivers to race them in turn, or start without drivers and the car races alone.' : '';
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

function raceViewInit() {
  setInterval(raceTick, 250);
  $('bigOpen').onclick = () => {
    $('bigView').hidden = false;
    const el = document.documentElement;                // real fullscreen where the browser allows it
    if (el.requestFullscreen) el.requestFullscreen().catch(() => {});
  };
  $('bigView').onclick = () => {
    $('bigView').hidden = true;
    if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
  };
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
    if (heatEditing) { api('driver/remove', { id }, `Remove ${driverName(id)}? Their laps stay in the list.`); return; }
    const order = heatOrder();
    heatPick = order.includes(id) ? order.filter(x => x !== id) : [...order, id];
    renderRaceKind();
  };
  $('heatEdit').onclick = () => { heatEditing = !heatEditing; closeRaceAdd(); };
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
    if (order.length) params.order = order.join(',');
    unlockAudio();
    api('race/start', params, order.length
      ? `Start heats: ${order.map(driverName).join(', ')}, ${n} lap${n > 1 ? 's' : ''} each? The current session is cleared.`
      : `Start a ${n} ${raceKind === 'laps' ? 'lap' : 'minute'} race? The current session is cleared.`);
  };
  $('raceUndo').onclick = () => api('undo', { car: 0 });
  $('raceAgain').onclick = () => {                    // the same race: length and heat order
    const params = { laps: S.race.laps, minutes: S.race.minutes };
    if (S.race.order.length) params.order = S.race.order.join(',');
    unlockAudio();
    api('race/start', params, 'Start the same race again? These results are cleared.');
  };
  $('raceStop').onclick = async () => {
    const done = $('raceStop').textContent === 'Back to training';
    if (await api('race/stop', {}, done ? null : 'Stop the race? The laps stay.')) showTab('Training');
  };
  $('raceSetup').onclick = e => {                     // − / + around the race length
    const step = +(e.target.dataset.step || 0);
    if (!step) return;
    const max = raceKind === 'laps' ? 999 : 600;
    $('raceLen').value = Math.min(max, Math.max(1, Math.round(+$('raceLen').value || 0) + step));
  };
}
