// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  master.h  --  master output stage: switchable filter (LP/BP/HP) + volume.
// =============================================================================
//  Applied to the final stereo mix AFTER the global FX and BEFORE the safety
//  limiter (see main.cpp). One state-variable filter per channel gives all three
//  responses at once, so the type select just picks which output to use. All
//  params arrive over MIDI (see config/params.h `master` + `midi`).
// =============================================================================
#pragma once

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
      for (size_t i = 0; i < size; ++i) { l[i] *= g; r[i] *= g; }
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
      l[i] = lo * g;
      r[i] = ro * g;
    }
  }

 private:
  daisysp::Svf lf_, rf_;
  float sr_ = 48000.0f, cut01_ = 1.0f, res01_ = 0.0f, vol_ = 1.0f;
  int   type_ = OFF;
};

}  // namespace synthbox
