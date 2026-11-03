// Race standings, computed from the laps in the state (docs/multi-car.md). Pure functions, used by
// the app and unit-tested with node (test/js/race.test.js).
//
// Lap entries: [ms, driver, deleted, continues, car, num, precise, endMs], oldest first.
// L is the one definition of that layout for the app; src/web.cpp and tools/mock_server.py write it.
(function (root) {
  'use strict';

  const L = { MS: 0, DRIVER: 1, DELETED: 2, CONT: 3, CAR: 4, NUM: 5, PRECISE: 6, END: 7 };

  // Cars in the race: transponder cars with several cars, else the one beam-only car (slot 0).
  const racers = state => state.cars.filter(c => state.multi ? c.slot > 0 : c.slot === 0);

  // Counted race laps per car: after "go", not deleted, oldest first.
  function lapsByCar(state) {
    const go = state.race.go;
    const by = new Map();
    for (const car of racers(state)) by.set(car.slot, []);
    for (const lap of state.laps) {
      if (lap[L.DELETED] || lap[L.END] <= go || !by.has(lap[L.CAR])) continue;
      by.get(lap[L.CAR]).push(lap);
    }
    return by;
  }

  // When the race is decided, or null: the winner's finishing crossing.
  function winnerAt(state, by) {
    const race = state.race;
    if (race.laps > 0) {
      let at = null;
      for (const laps of by.values()) {
        if (laps.length >= race.laps) {
          const t = laps[race.laps - 1][L.END];
          if (at === null || t < at) at = t;
        }
      }
      return at;
    }
    if (race.minutes > 0) {
      const deadline = race.go + race.minutes * 60000;
      let leader = null;                      // most laps by the deadline, earlier last crossing wins ties
      for (const laps of by.values()) {
        const done = laps.filter(l => l[L.END] <= deadline);
        if (!done.length) continue;
        const cand = { n: done.length, last: done[done.length - 1][L.END], laps };
        if (!leader || cand.n > leader.n || (cand.n === leader.n && cand.last < leader.last)) leader = cand;
      }
      if (!leader) return null;
      const after = leader.laps.find(l => l[L.END] > deadline);
      return after ? after[L.END] : null;
    }
    return null;
  }

  function standings(state, nowMs) {
    const race = state.race;
    const phase = race.state === 'countdown' ? 'countdown' : race.state === 'running' ? 'running' : 'off';
    const result = {
      phase,
      countdownMs: phase === 'countdown' ? Math.max(0, race.go - nowMs) : 0,
      elapsedMs: phase === 'running' ? Math.max(0, nowMs - race.go) : 0,
      remainingMs: phase === 'running' && race.minutes > 0 ? Math.max(0, race.go + race.minutes * 60000 - nowMs) : null,
      finished: false,
      finishMs: null,                         // winner's race time once decided
      allIn: false,
      rows: [],
    };
    if (phase !== 'running') return result;

    const by = lapsByCar(state);
    const end = winnerAt(state, by);
    result.finished = end !== null;
    if (end !== null) result.finishMs = end - race.go;

    for (const car of racers(state)) {
      let laps = by.get(car.slot);
      let finished = false;
      if (end !== null) {                     // each car finishes at its first crossing from the winner's on
        const k = laps.findIndex(l => l[L.END] >= end);
        if (k >= 0) { laps = laps.slice(0, k + 1); finished = true; }
      }
      const first = laps[0], last = laps[laps.length - 1];
      result.rows.push({
        car: car.slot,
        driver: car.driver,
        laps: laps.length,
        started: laps.length > 0 || car.running,
        lastAt: last ? last[L.END] : null,
        totalMs: first ? last[L.END] - (first[L.END] - first[L.MS]) : 0,
        bestMs: laps.length ? Math.min(...laps.map(l => l[L.MS])) : 0,
        finished,
      });
    }

    result.rows.sort((a, b) =>
      b.laps - a.laps
      || (a.lastAt ?? Infinity) - (b.lastAt ?? Infinity)
      || (b.started - a.started)
      || a.car - b.car);

    const lead = result.rows[0];
    result.rows.forEach((r, i) => {
      r.pos = i + 1;
      r.gapLaps = lead ? lead.laps - r.laps : 0;
      r.gapMs = i && lead && r.gapLaps === 0 && r.lastAt !== null ? r.lastAt - lead.lastAt : 0;
    });
    result.allIn = result.finished && result.rows.every(r => r.finished || !r.started);
    return result;
  }

  const api = { standings, racers, L };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.Race = api;
})(this);
