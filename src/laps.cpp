#include "laps.h"

#ifdef ARDUINO
#include <Arduino.h>
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#define LOCK()   portENTER_CRITICAL(&mux)
#define UNLOCK() portEXIT_CRITICAL(&mux)
#else                                                 // host unit tests, single-threaded
#define LOCK()
#define UNLOCK()
#endif

enum class LastPass : uint8_t { None, Start, Lap };   // what the car's last pass did, for undo

struct Car {
  int64_t  lastCrossUs = 0;      // 0 = waiting for the start pass
  int64_t  prevCrossUs = 0;      // lastCrossUs before the last pass, for undo
  LastPass lastPass    = LastPass::None;
  uint8_t  driver      = 0;
  uint16_t num         = 0;      // laps numbered so far
};

static Car         cars[MAX_CARS];
static Lap         laps[MAX_LAPS];    // ring buffer, all cars
static int         lapCount    = 0;
static int         floorKept   = 0;   // laps below this index are no longer in the ring (see popAt)
static uint32_t    ignored     = 0;
static DriverStats stats[MAX_STATS];
static int         nStats      = 0;
static volatile uint32_t revision = 0;

static void changed() { revision = revision + 1; }    // ++ on volatile is deprecated in C++20
static bool validCar(int car) { return car >= 0 && car < MAX_CARS; }

static uint16_t statsKey(const Lap& l) { return l.driver ? l.driver : (uint16_t)(STATS_CAR | l.car); }

static DriverStats* statsFor(uint16_t key, bool create) {
  for (int i = 0; i < nStats; i++) if (stats[i].key == key) return &stats[i];
  if (!create || nStats >= MAX_STATS) return nullptr;
  stats[nStats] = { key, 0, 0, 0 };
  return &stats[nStats++];
}

// Index of the oldest lap still in the ring.
static int oldestKept() {
  int byRing = lapCount > MAX_LAPS ? lapCount - MAX_LAPS : 0;
  return byRing > floorKept ? byRing : floorKept;
}

static Lap& lapAt(int index) { return laps[index % MAX_LAPS]; }

// Remove the lap at index from the ring, later laps move down by one.
// When the ring is full, the slot before the oldest lap was already overwritten,
// so the window of kept laps must not grow backwards: floorKept pins it.
static void popAt(int index) {
  floorKept = oldestKept();
  for (int i = index; i < lapCount - 1; i++) lapAt(i) = lapAt(i + 1);
  lapCount--;
}

// Index of the car's newest lap in the ring, -1 if none.
static int lastLapOf(int car) {
  for (int i = lapCount - 1; i >= oldestKept(); i--) if (lapAt(i).car == car) return i;
  return -1;
}

// Best lap for a stats key from the laps still in memory (older laps have rotated out of the ring).
static void recomputeBest(DriverStats& s) {
  s.bestMs = 0;
  for (int i = oldestKept(); i < lapCount; i++) {
    const Lap& l = lapAt(i);
    if (!l.deleted && statsKey(l) == s.key && (s.bestMs == 0 || l.ms < s.bestMs)) s.bestMs = l.ms;
  }
}

static void addToStats(const Lap& l) {
  if (DriverStats* s = statsFor(statsKey(l), true)) {
    s->count++;
    s->totalMs += l.ms;
    if (s->bestMs == 0 || l.ms < s->bestMs) s->bestMs = l.ms;
  }
}

// Call after the lap is already marked deleted or popped off the ring.
static void removeFromStats(const Lap& l) {
  DriverStats* s = statsFor(statsKey(l), false);
  if (!s) return;
  s->count--;
  s->totalMs -= l.ms;
  if (s->bestMs == l.ms) recomputeBest(*s);
}

void lapsCrossing(int car, int64_t nowUs, uint32_t minLapMs, bool precise) {
  if (!validCar(car)) return;
  LOCK();
  Car& c = cars[car];
  if (c.lastCrossUs == 0) {
    c.lastCrossUs = nowUs;                            // the start pass only starts timing
    c.prevCrossUs = 0;
    c.lastPass    = LastPass::Start;
  } else {
    int64_t diffUs = nowUs - c.lastCrossUs;
    if (diffUs >= (int64_t)minLapMs * 1000) {
      uint32_t ms = (uint32_t)((diffUs + 500) / 1000);
      lapAt(lapCount) = { nowUs, ms, ++c.num, (uint8_t)car, c.driver, false, precise };
      lapCount++;
      addToStats(lapAt(lapCount - 1));
      c.prevCrossUs = c.lastCrossUs;
      c.lastCrossUs = nowUs;
      c.lastPass    = LastPass::Lap;
    } else {
      ignored++;
    }
  }
  changed();
  UNLOCK();
}

void lapsReset() {
  LOCK();
  for (Car& c : cars) { uint8_t d = c.driver; c = Car(); c.driver = d; }
  lapCount = 0; floorKept = 0; ignored = 0; nStats = 0;
  changed();
  UNLOCK();
}

void lapsSetDriver(int car, uint8_t id) {
  if (!validCar(car)) return;
  LOCK();
  Car& c = cars[car];
  if (id != c.driver) {
    c.driver = id;
    c.lastCrossUs = 0;                                // the lap in progress would mix two drivers
    c.lastPass = LastPass::None;
    changed();
  }
  UNLOCK();
}

uint8_t lapsDriver(int car) { return validCar(car) ? cars[car].driver : 0; }

void lapsForgetDriver(uint8_t id) {
  LOCK();
  for (int i = 0; i < nStats; i++) {
    if (stats[i].key == id) { stats[i] = stats[--nStats]; break; }
  }
  changed();
  UNLOCK();
}

bool lapsUndo(int car) {
  if (!validCar(car)) return false;
  LOCK();
  Car& c = cars[car];
  bool ok = true;
  if (c.lastPass == LastPass::Start) {
    c.lastCrossUs = 0;
  } else if (c.lastPass == LastPass::Lap && lastLapOf(car) >= 0) {
    int i = lastLapOf(car);
    Lap l = lapAt(i);
    popAt(i);
    c.num--;
    if (!l.deleted) removeFromStats(l);
    c.lastCrossUs = c.prevCrossUs;                    // the lap continues from the real previous pass
  } else {
    ok = false;
  }
  c.lastPass = LastPass::None;                        // only one level of undo
  if (ok) changed();
  UNLOCK();
  return ok;
}

void lapsRestart(int car) {
  if (!validCar(car)) return;
  LOCK();
  cars[car].lastCrossUs = 0;
  cars[car].lastPass = LastPass::None;
  changed();
  UNLOCK();
}

bool lapsDelete(int seq) {
  LOCK();
  int i = seq - 1;
  bool ok = seq >= 1 && seq <= lapCount && i >= oldestKept();
  if (ok) {
    Lap& l = lapAt(i);
    ok = !l.deleted;
    if (ok) {
      l.deleted = true;
      removeFromStats(l);
      Car& c = cars[l.car];
      if (i == lastLapOf(l.car) && c.lastPass == LastPass::Lap) c.lastPass = LastPass::None;
      changed();
    }
  }
  UNLOCK();
  return ok;
}

bool lapsContinues(const Lap& prev, const Lap& next) {
  int64_t gap = next.endUs - (int64_t)next.ms * 1000 - prev.endUs;   // ms is rounded, so ±0.5 ms
  return gap >= -1000 && gap <= 1000;
}

bool lapsJoin(int seq) {
  LOCK();
  int i = seq - 1;                                    // index of the first piece
  bool ok = seq >= 1 && seq <= lapCount && i >= oldestKept();
  int j = -1;                                         // the same car's next lap
  if (ok) {
    for (int k = i + 1; k < lapCount; k++) if (lapAt(k).car == lapAt(i).car) { j = k; break; }
    ok = j > 0;
  }
  if (ok) {
    Lap a = lapAt(i), b = lapAt(j);
    ok = !a.deleted && !b.deleted && a.driver == b.driver && lapsContinues(a, b);
    if (ok) {
      Car& c = cars[a.car];
      bool bWasLast = j == lastLapOf(a.car);
      lapAt(i).deleted = true;                        // out of the best-lap search while stats update
      removeFromStats(a);
      popAt(j);
      removeFromStats(b);
      for (int k = j; k < lapCount; k++) if (lapAt(k).car == a.car) lapAt(k).num--;
      c.num--;
      lapAt(i) = { b.endUs, a.ms + b.ms, a.num, a.car, a.driver, false, a.precise && b.precise };
      addToStats(lapAt(i));
      if (bWasLast && c.lastPass == LastPass::Lap) c.prevCrossUs = a.endUs - (int64_t)a.ms * 1000;
      changed();
    }
  }
  UNLOCK();
  return ok;
}

static long sinceMs(int64_t last, int64_t nowUs) {
  return last ? (long)((nowUs - last) / 1000) : 0;
}

void lapsSnapshot(LapSnapshot& out, int64_t nowUs) {
  LOCK();
  for (int k = 0; k < MAX_CARS; k++) {
    const Car& c = cars[k];
    out.cars[k] = { c.lastCrossUs != 0, sinceMs(c.lastCrossUs, nowUs), c.lastPass != LastPass::None, c.driver, c.num };
  }
  out.count   = lapCount;
  out.ignored = ignored;
  out.n = lapCount - oldestKept();
  for (int i = 0; i < out.n; i++) out.laps[i] = lapAt(oldestKept() + i);
  out.nStats = nStats;
  out.bestMs = 0;
  for (int i = 0; i < nStats; i++) {
    out.stats[i] = stats[i];
    uint32_t b = stats[i].bestMs;
    if (b && (out.bestMs == 0 || b < out.bestMs)) out.bestMs = b;
  }
  UNLOCK();
}

long lapsCurrentMs(int car, int64_t nowUs) {
  if (!validCar(car)) return 0;
  LOCK();
  int64_t last = cars[car].lastCrossUs;
  UNLOCK();
  return sinceMs(last, nowUs);
}

uint32_t lapsRevision() { return revision; }
