// Race standings tests: node --test test/js
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

test('time race: leader crosses after the time is up, then the rest', () => {
  // 1 minute race: go at 10000, deadline 70000
  const s = standings(state({ minutes: 1, laps: [
    ...car(1, 10000, 9000, 9000, 9000, 9000, 9000, 9000, 9000),   // 7th crossing 73000 > deadline
    ...car(2, 10500, 9500, 9500, 9500, 9500, 9500, 9500, 9500),   // 6 laps by 67500, 7th at 77000
  ], cars: [1, 2] }), 80000);
  assert.ok(s.finished);
  assert.equal(s.rows[0].car, 1);
  assert.equal(s.rows[0].laps, 7);
  assert.equal(s.rows[1].laps, 7);
  assert.ok(s.rows[1].finished);
  assert.equal(s.remainingMs, 0);
});

test('time race not finished before the leader crosses', () => {
  const s = standings(state({ minutes: 1, laps: car(1, 10000, 9000, 9000) }), 72000);
  assert.equal(s.finished, false);
  assert.equal(s.remainingMs, 0);
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
