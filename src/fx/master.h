// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  master.h  --  master output stage: switchable filter (LP/BP/HP) + volume
//                + peak limiter.
// =============================================================================
//  Applied to the final stereo mix AFTER the global FX. One state-variable
//  filter per channel gives all three responses at once, so the type select just
//  picks which output to use. The limiter rides the output gain down when a peak
//  would exceed the ceiling (fast attack, slow release) instead of hard-clipping,
//  so a resonant filter / stacked grains / runaway feedback get turned down
//  smoothly rather than crushed. All params arrive over MIDI (see config/params.h
//  `master` + `midi`).
// =============================================================================
#pragma once

#include <cmath>
#include <cstddef>

#include "daisysp.h"
#include "config/params.h"

namespace synthbox {

class MasterChain {
 public:
  enum FilterType { OFF = 0, LP = 1, BP = 2, HP = 3 };

  void Init(float sr) {
    sr_ = sr;
    lf_.Init(sr);
    rf_.Init(sr);
    type_ = OFF;
    cut01_ = 1.0f;
    res01_ = 0.0f;
    vol_ = 1.0f;
    gain_ = 1.0f;
    // One-pole smoothing coefficients: ~0.5 ms attack (pull gain down fast on a
    // peak), ~150 ms release (let it recover gently so you don't hear pumping).
    atk_ = 1.0f - expf(-1.0f / (0.0005f * sr_));
    rel_ = 1.0f - expf(-1.0f / (0.1500f * sr_));
  }

  // All normalized 0..1 (type is 0..3); mapped to ranges in Process().
  void SetVolume(float v01) { vol_ = v01; }
  void SetFilterType(int t) { type_ = t < 0 ? 0 : (t > 3 ? 3 : t); }
  void SetCutoff(float v01) { cut01_ = v01; }
  void SetRes(float v01) { res01_ = v01; }

  // In-place stereo process.
  void Process(float* l, float* r, size_t size) {
    const float g = vol_;
    if (type_ == OFF) {                       // volume only, no filtering
      for (size_t i = 0; i < size; ++i) { float a = l[i] * g, b = r[i] * g; Limit(a, b); l[i] = a; r[i] = b; }
      return;
    }
    const float fc = daisysp::fmap(cut01_, params::master::kCutMinHz,
                                   params::master::kCutMaxHz, daisysp::Mapping::EXP);
    const float res = res01_ * params::master::kResMax;
    lf_.SetFreq(fc); lf_.SetRes(res);
    rf_.SetFreq(fc); rf_.SetRes(res);
    for (size_t i = 0; i < size; ++i) {
      lf_.Process(l[i]); rf_.Process(r[i]);
      float lo, ro;
      if (type_ == LP)      { lo = lf_.Low();  ro = rf_.Low();  }
      else if (type_ == BP) { lo = lf_.Band(); ro = rf_.Band(); }
      else                  { lo = lf_.High(); ro = rf_.High(); }
      float a = lo * g, b = ro * g;
      Limit(a, b);
      l[i] = a;
      r[i] = b;
    }
  }

 private:
  // Peak limiter: scale both channels by a shared gain that drops fast when the
  // louder channel would exceed the ceiling and recovers slowly. Keeps the mix
  // under the ceiling without the harshness of per-sample hard clipping.
  inline void Limit(float& l, float& r) {
    const float peak = fmaxf(fabsf(l), fabsf(r));
    const float desired = peak > kCeiling ? kCeiling / peak : 1.0f;
    gain_ += (desired - gain_) * (desired < gain_ ? atk_ : rel_);
    l *= gain_;
    r *= gain_;
  }

  static constexpr float kCeiling = 0.98f;   // leave a hair of headroom below 0 dBFS

  daisysp::Svf lf_, rf_;
  float sr_ = 48000.0f, cut01_ = 1.0f, res01_ = 0.0f, vol_ = 1.0f;
  float gain_ = 1.0f, atk_ = 0.04f, rel_ = 0.0001f;
  int   type_ = OFF;
};

}  // namespace synthbox
