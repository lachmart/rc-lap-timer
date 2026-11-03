// RC Lap Timer – race view (standings, clock, countdown voice), race setup and the big view overlay.
// Part of the client, see app.js; standings come from race.js.

let raceKind = 'laps';
let lastCount = null;  // countdown second last announced
let finishSaid = false;

const clock = ms => {
  const s = Math.floor(ms / 1000);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
};

// Race standings: recomputed on every state and a few times per second for the clock.
function renderRace() {
  if (!racing()) return null;
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

function raceOnState(s) {
  if (s.race.state !== 'running') finishSaid = false;
  renderRace();
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
  if (r && r.finished && !finishSaid) {
    finishSaid = true;
    say(`Race finished. Winner, ${whoOn(r.rows[0].car)}`);
  }
  if (!$('bigView').hidden && (multi() || racing())) renderBigTable(r);
}

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

function renderRaceKind() {
  for (const b of $('raceKind').children) b.classList.toggle('on', b.dataset.kind === raceKind);
  $('raceLenLbl').textContent = raceKind === 'laps' ? 'Number of laps' : 'Minutes';
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
}
