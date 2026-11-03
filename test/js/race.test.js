// Race standings tests: node --test test/js/*.test.js
const test = require('node:test');
const assert = require('node:assert/strict');
const { standings } = require('../../web/race.js');

const GO = 10000;

// Builds a state: laps given as [car, endMs, ms] in any order.
function state({ laps = [], minutes = 0, raceLaps = 0, cars = [1, 2, 3], deleted = [], raceState = 'running' } = {}) {
  const sorted = laps.slice().sort((a, b) => a[1] - b[1]);
  const num = {};
  return {
    multi: true,
    race: { state: raceState, go: GO, laps: raceLaps, minutes },
    cars: cars.map(slot => ({ slot, driver: 0, running: true, laps: 0 })),
    laps: sorted.map(([car, end, ms], i) => {
      num[car] = (num[car] || 0) + 1;
      return [ms, 0, deleted.includes(i) ? 1 : 0, 1, car, num[car], 1, end];
    }),
  };
}

// Laps of one car: first crossing at `start`, then the given lap times.
function car(slot, start, ...times) {
  const out = [];
  let t = start;
  for (const ms of times) { t += ms; out.push([slot, t, ms]); }
  return out;
}

test('off and countdown have no rows', () => {
  assert.equal(standings(state({ raceState: 'off' }), 0).phase, 'off');
  const s = standings(state({ raceState: 'countdown' }), GO - 2500);
  assert.equal(s.phase, 'countdown');
  assert.equal(s.countdownMs, 2500);
  assert.equal(s.rows.length, 0);
});

test('ordered by laps, then by who crossed first', () => {
  const s = standings(state({ raceLaps: 10, laps: [
    ...car(1, 10500, 8000, 8000),
    ...car(2, 10200, 7900, 8000, 8100),
    ...car(3, 10800, 8000, 8000),
  ] }), 40000);
  assert.deepEqual(s.rows.map(r => r.car), [2, 1, 3]);
  assert.equal(s.rows[0].laps, 3);
  assert.equal(s.rows[1].gapLaps, 1);
  assert.equal(s.rows[2].gapLaps, 1);
  assert.equal(s.rows[2].gapMs, 0);                    // gap in laps, not time
});

test('time gap when on the same lap', () => {
  const s = standings(state({ raceLaps: 10, laps: [...car(1, 10000, 8000), ...car(2, 10300, 8000)] }), 30000);
  assert.equal(s.rows[1].gapMs, 300);
});

test('laps before go and deleted laps do not count', () => {
  const st = state({ raceLaps: 10, laps: [[1, 9000, 8000], ...car(1, 10500, 8000, 8000)], deleted: [1] });
  const s = standings(st, 40000);
  assert.equal(s.rows.find(r => r.car === 1).laps, 1);
});

test('laps race: winner, others finish at their next crossing', () => {
  const s = standings(state({ raceLaps: 3, laps: [
    ...car(1, 10000, 8000, 8000, 8000, 8000),   // finishes lap 3 at 34000, keeps driving
    ...car(2, 10500, 8500, 8500, 8500, 8500),   // crossings 19000, 27500, 36000 (finish), 44500 (ignored)
    ...car(3, 11000, 9000, 9000),               // last crossing 29000, none after the winner's finish
  ] }), 50000);
  assert.ok(s.finished);
  const [a, b, c] = s.rows;
  assert.equal(a.car, 1); assert.equal(a.laps, 3); assert.ok(a.finished);
  assert.equal(a.totalMs, 24000);
  assert.equal(s.finishMs, 24000);                   // 34000 − go (10000)
  assert.equal(b.car, 2); assert.equal(b.laps, 3); assert.ok(b.finished);
  assert.equal(b.gapMs, 2000);                  // same laps, crossed 2 s later
  assert.equal(c.car, 3); assert.equal(c.laps, 2); assert.equal(c.finished, false);
  assert.equal(s.allIn, false);
});

test('laps race: finishing crossing is the first one at or after the winner', () => {
  const s = standings(state({ raceLaps: 2, laps: [
    ...car(1, 10000, 8000, 8000, 8000),          // wins at 26000
    ...car(2, 10200, 8000, 8100, 8000),          // crossings 18200, 26300 (finish), 34300 (ignored)
  ], cars: [1, 2] }), 40000);
  const two = s.rows.find(r => r.car === 2);
  assert.equal(two.laps, 2);
  assert.ok(two.finished);
  assert.ok(s.allIn);
  assert.equal(s.rows.find(r => r.car === 1).laps, 2);  // laps after the finish are ignored
});

test('time race: every car finishes at its own first crossing after the time is up', () => {
  // 1 minute race: go at 10000, deadline 70000
  const s = standings(state({ minutes: 1, laps: [
    ...car(1, 10000, 9000, 9000, 9000, 9000, 9000, 9000, 9000, 9000),   // 7th crossing 73000, 8th 82000 (ignored)
    ...car(2, 10500, 9500, 9500, 9500, 9500, 9500, 9500, 9500),         // 6 laps by 67500, 7th at 77000
    ...car(3, 10000, 12000, 12000, 12000, 12000, 12000, 12000),        // 5th crossing exactly at 70000 finishes
  ] }), 85000);
  assert.ok(s.finished);
  assert.ok(s.allIn);
  assert.deepEqual(s.rows.map(r => [r.car, r.laps, r.finished]), [[1, 7, true], [2, 7, true], [3, 5, true]]);
  assert.equal(s.rows[0].totalMs, 63000);             // race time from "go"
  assert.equal(s.finishMs, 63000);
  assert.equal(s.remainingMs, 0);
});

test('time race: a slower car crossing before the leader still finishes then (no extra lap)', () => {
  const s = standings(state({ minutes: 1, laps: [
    ...car(1, 10000, 9000, 9000, 9000, 9000, 9000, 9000, 9000),          // 73000
    ...car(2, 10000, 10000, 10000, 10000, 10000, 10000, 11000, 10000),   // 6 laps at 70000 (= deadline), 81000 ignored
  ], cars: [1, 2] }), 90000);
  assert.equal(s.rows.find(r => r.car === 2).laps, 6);
});

test('time race: time up is the finish phase even before anyone crosses', () => {
  const s = standings(state({ minutes: 1, laps: car(1, 10000, 9000, 9000) }), 72000);
  assert.equal(s.finished, true);
  assert.equal(s.allIn, false);
  assert.equal(s.remainingMs, 0);
});

test('a car that stops crossing after the race is decided is out (DNF), so the race ends', () => {
  const laps = [...car(1, 10000, 8000, 8000), ...car(2, 10000, 9000)];   // 1 wins at 26000; 2 last crossed at 19000
  let s = standings(state({ raceLaps: 2, laps, cars: [1, 2] }), 40000);
  assert.equal(s.allIn, false);                        // still within 30 s of the finish
  s = standings(state({ raceLaps: 2, laps, cars: [1, 2] }), 57000);
  assert.equal(s.rows.find(r => r.car === 2).dnf, true);
  assert.equal(s.allIn, true);
});

test('car that has not started yet is listed last', () => {
  const st = state({ raceLaps: 5, laps: car(2, 10000, 8000), cars: [1, 2, 3] });
  st.cars[0].running = false;
  st.cars[2].running = true;                           // crossed once, no lap yet
  const s = standings(st, 30000);
  assert.deepEqual(s.rows.map(r => r.car), [2, 3, 1]);
});

test('several cars: the beam-only car (slot 0) is not in the race', () => {
  const st = state({ raceLaps: 5, laps: car(1, 10000, 8000), cars: [0, 1] });
  assert.deepEqual(standings(st, 30000).rows.map(r => r.car), [1]);
});

test('one car: only the beam-only car races', () => {
  const st = state({ raceLaps: 5, laps: car(0, 10000, 8000), cars: [0, 1] });
  st.multi = false;
  assert.deepEqual(standings(st, 30000).rows.map(r => r.car), [0]);
});

test('racers: transponder cars with several cars, else the beam-only car', () => {
  const { racers } = require('../../web/race.js');
  const cars = [{ slot: 0 }, { slot: 2 }, { slot: 5 }];
  assert.deepEqual(racers({ multi: true, cars }).map(c => c.slot), [2, 5]);
  assert.deepEqual(racers({ multi: false, cars }).map(c => c.slot), [0]);
});

// ---------------------------------------------------------------- heats (one car, drivers in turn)
// Laps of car 0 given as [driver, endMs, ms]; drivers 1, 2, 3 race 3 laps each in the order given.
function heats({ laps = [], order = [1, 2, 3], heat = 0, raceLaps = 3, deleted = [], running = true } = {}) {
  return {
    multi: false,
    race: { state: 'running', go: GO, laps: raceLaps, minutes: 0, order, heat },
    cars: [{ slot: 0, driver: order[Math.min(heat, order.length - 1)], running, laps: 0 }],
    laps: laps.map(([driver, end, ms], i) => [ms, driver, deleted.includes(i) ? 1 : 0, 1, 0, i + 1, 1, end]),
  };
}
// One heat: the driver's first crossing at `start`, then the lap times.
function heat(driver, start, ...times) {
  const out = [];
  let t = start;
  for (const ms of times) { t += ms; out.push([driver, t, ms]); }
  return out;
}

test('heats: the driver on the track is current, the others wait in order', () => {
  const s = standings(heats({ laps: heat(1, 11000, 9000) }), 25000);
  assert.equal(s.heats, true);
  assert.equal(s.finished, false);
  assert.equal(s.current.driver, 1);
  assert.deepEqual(s.rows.map(r => [r.driver, r.laps, r.started]), [[1, 1, true], [2, 0, false], [3, 0, false]]);
});

test('heats: ranked by total time of the finished heats, gaps to the fastest', () => {
  const laps = [...heat(1, 11000, 9000, 9000, 9000), ...heat(2, 50000, 8000, 8000, 8500), ...heat(3, 90000, 9500)];
  const s = standings(heats({ laps, heat: 2 }), 100000);
  assert.deepEqual(s.rows.map(r => [r.pos, r.driver, r.totalMs, r.gapMs]),
    [[1, 2, 24500, 0], [2, 1, 27000, 2500], [3, 3, 9500, 0]]);
  assert.equal(s.current.driver, 3);
  assert.equal(s.rows[0].bestMs, 8000);
});

test('heats: laps beyond the heat and before "go" do not count', () => {
  const laps = [[1, 9000, 5000], ...heat(1, 11000, 9000, 9000, 9000, 7000)];
  const s = standings(heats({ laps, heat: 1 }), 60000);
  const r1 = s.rows.find(r => r.driver === 1);
  assert.equal(r1.laps, 3);
  assert.equal(r1.totalMs, 27000);
});

test('heats: all heats done = race finished', () => {
  const laps = [...heat(1, 11000, 9000, 9000), ...heat(2, 40000, 8000, 8000)];
  const s = standings(heats({ laps, order: [1, 2], heat: 2, raceLaps: 2 }), 60000);
  assert.equal(s.finished, true);
  assert.equal(s.allIn, true);
  assert.equal(s.current, null);
  assert.deepEqual(s.rows.map(r => r.driver), [2, 1]);
});

test('heats: a heat the timer moved on from counts as finished even after a lap was deleted', () => {
  const laps = [...heat(1, 11000, 9000, 9000, 9000), ...heat(2, 50000, 8000)];
  const s = standings(heats({ laps, heat: 1, deleted: [1] }), 60000);
  const r1 = s.rows.find(r => r.driver === 1);
  assert.equal(r1.finished, true);
  assert.equal(r1.laps, 2);
  assert.equal(s.rows[0].driver, 1);                   // not a full heat, so not ranked by time, listed first anyway
});

test('heats: equal totals go to the better best lap', () => {
  const laps = [...heat(1, 11000, 9000, 9000), ...heat(2, 40000, 9500, 8500)];
  const s = standings(heats({ laps, order: [1, 2], heat: 2, raceLaps: 2 }), 70000);
  assert.deepEqual(s.rows.map(r => r.driver), [2, 1]);
});

test('heats need one car and a laps race', () => {
  const { isHeats } = require('../../web/race.js');
  assert.equal(isHeats(heats()), true);
  assert.equal(isHeats({ ...heats(), multi: true }), false);
  assert.equal(isHeats(heats({ order: [] })), false);
  assert.equal(isHeats(heats({ raceLaps: 0 })), false);
});

test('heats: who is next, and the split against the fastest heat after the same laps', () => {
  const laps = [...heat(1, 11000, 9000, 9000, 9000), ...heat(2, 50000, 8000, 8500)];
  const s = standings(heats({ laps, heat: 1 }), 70000);
  assert.equal(s.next, 3);
  assert.deepEqual(s.after, []);
  assert.equal(s.vs, 1);
  assert.equal(s.splitMs, 16500 - 18000);
  assert.deepEqual(s.current.lapMs, [8000, 8500]);
});

test('heats: no split for the first driver or before the first lap', () => {
  assert.equal(standings(heats({ laps: heat(1, 11000, 9000) }), 25000).splitMs, undefined);
  const laps = heat(1, 11000, 9000, 9000, 9000);
  assert.equal(standings(heats({ laps, heat: 1 }), 50000).splitMs, undefined);
});

test('heats: during the countdown the first driver is up next', () => {
  const st = heats();
  st.race.state = 'countdown';
  const s = standings(st, GO - 1000);
  assert.equal(s.heats, true);
  assert.equal(s.next, 1);
  assert.deepEqual(s.after, [2, 3]);
});

test('heats: between drivers the next one waits for a start, or counts down', () => {
  const laps = heat(1, 11000, 9000, 9000, 9000);
  const st = heats({ laps, heat: 1 });
  st.race.heatState = 'waiting';
  let s = standings(st, 50000);
  assert.equal(s.heatState, 'waiting');
  assert.equal(s.current.driver, 2);
  assert.equal(s.current.started, false);              // the car still runs, but not for driver 2 yet
  st.race.heatState = 'countdown';
  st.race.heatGo = 60000;
  s = standings(st, 57500);
  assert.equal(s.heatCountdownMs, 2500);
  st.race.heatState = 'running';
  assert.equal(standings(st, 61000).current.started, true);
});
