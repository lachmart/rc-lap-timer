// Turns beam breaks and IR transponder sightings into passings. No hardware dependencies, unit-tested.
//
// Single-car mode: every beam break is a passing of the one car (id 0).
//
// Multi-car mode: each car's transponder repeats its id every ~70 ms while it is near the gate,
// so one pass = a cluster of sightings of that id (gaps < CLUSTER_GAP). When a cluster ends,
// it takes the beam break closest to its middle (within MARGIN) as a precise time; without one
// the passing is still counted at the cluster's middle, marked imprecise (IR alone is ±35 ms).
// Beam breaks no cluster claims are counted as unidentified (person, car without transponder).
#pragma once
#include <stdint.h>

struct Passing {
  uint16_t id;         // transponder id, 0 = the beam-only car (single-car mode)
  int64_t  tUs;
  bool     precise;    // time from the beam, not from IR only
};

class PassMatcher {
public:
  static constexpr int64_t CLUSTER_GAP = 250000;    // µs without a sighting = the car has left
  static constexpr int64_t MAX_CLUSTER = 2000000;   // µs, a car parked under the receiver
  static constexpr int64_t MARGIN      = 200000;    // µs a beam break may lie outside the cluster

  void setMulti(bool multi);
  bool multi() const { return multi_; }

  void beam(int64_t tUs);
  void ir(uint16_t id, int64_t tUs);

  // Finished passings up to now, oldest first. Returns how many were written to out.
  int poll(int64_t nowUs, Passing* out, int max);

  uint32_t unidentified() const { return unidentified_; }
  void     resetCounters() { unidentified_ = 0; }

private:
  static constexpr int MAX_CLUSTERS = 8;
  static constexpr int MAX_BEAMS    = 8;
  struct Cluster { uint16_t id; int64_t first, last; };

  bool     multi_ = false;
  Cluster  clusters_[MAX_CLUSTERS];
  int      nClusters_ = 0;
  int64_t  beams_[MAX_BEAMS];        // unclaimed beam breaks, oldest first
  int      nBeams_ = 0;
  uint32_t unidentified_ = 0;

  void dropBeam(int i);
};
