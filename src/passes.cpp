#include "passes.h"

void PassMatcher::setMulti(bool multi) {
  multi_ = multi;
  nClusters_ = 0;
  nBeams_ = 0;
}

void PassMatcher::beam(int64_t tUs) {
  if (nBeams_ == MAX_BEAMS) { dropBeam(0); unidentified_++; }
  beams_[nBeams_++] = tUs;
}

void PassMatcher::ir(uint16_t id, int64_t tUs) {
  if (!multi_ || id == 0) return;
  for (int i = 0; i < nClusters_; i++) {
    if (clusters_[i].id == id) { clusters_[i].last = tUs; return; }
  }
  if (nClusters_ == MAX_CLUSTERS) return;             // more cars at the gate than we track: skip
  clusters_[nClusters_++] = { id, tUs, tUs };
}

void PassMatcher::dropBeam(int i) {
  for (; i < nBeams_ - 1; i++) beams_[i] = beams_[i + 1];
  nBeams_--;
}

int PassMatcher::poll(int64_t nowUs, Passing* out, int max) {
  int n = 0;

  if (!multi_) {
    while (nBeams_ > 0 && n < max) {
      out[n++] = { 0, beams_[0], true };
      dropBeam(0);
    }
    return n;
  }

  // Finished clusters (car has left, or parked too long) claim the nearest beam break.
  for (int c = 0; c < nClusters_ && n < max;) {
    Cluster& cl = clusters_[c];
    if (nowUs - cl.last < CLUSTER_GAP && nowUs - cl.first < MAX_CLUSTER) { c++; continue; }

    int64_t mid = (cl.first + cl.last) / 2;
    int best = -1;
    int64_t bestDist = 0;
    for (int b = 0; b < nBeams_; b++) {
      if (beams_[b] < cl.first - MARGIN || beams_[b] > cl.last + MARGIN) continue;
      int64_t dist = beams_[b] > mid ? beams_[b] - mid : mid - beams_[b];
      if (best < 0 || dist < bestDist) { best = b; bestDist = dist; }
    }
    if (best >= 0) { out[n++] = { cl.id, beams_[best], true }; dropBeam(best); }
    else           { out[n++] = { cl.id, mid, false }; }

    clusters_[c] = clusters_[--nClusters_];            // order of clusters doesn't matter
  }

  // Beam breaks nobody can claim any more: no open cluster near them, and too old for a new one.
  for (int b = 0; b < nBeams_;) {
    bool claimable = nowUs - beams_[b] <= MARGIN;
    for (int c = 0; c < nClusters_ && !claimable; c++) claimable = beams_[b] >= clusters_[c].first - MARGIN;
    if (claimable) { b++; continue; }
    dropBeam(b);
    unidentified_++;
  }

  // Passings of different cars may come out of order; sort by time (n is tiny).
  for (int i = 1; i < n; i++)
    for (int k = i; k > 0 && out[k].tUs < out[k - 1].tUs; k--) { Passing p = out[k]; out[k] = out[k - 1]; out[k - 1] = p; }
  return n;
}
