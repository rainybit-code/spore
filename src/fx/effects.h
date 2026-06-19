// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  effects.h  --  global FX block (delay + reverb), decoupled from the modes.
// =============================================================================
//  Processes whatever the active mode outputs. Selected by Toggle 3 (off / delay
//  / reverb) and edited via the footswitch shift-layer (see main.cpp). All ranges
//  live in params::fx.
//
//  ReverbSc carries a ~400 KB internal buffer, and the delay lines are 2 s each,
//  so the GlobalFx instance MUST be placed in SDRAM (DSY_SDRAM_BSS) by the owner
//  (see main.cpp). The delay-line buffers below are SDRAM statics for the same
//  reason.
// =============================================================================
#pragma once

#include <cmath>

#include "daisysp.h"
#include "daisysp-lgpl.h"  // ReverbSc
#include "config/params.h"

namespace synthbox {

// Delay-line buffers in SDRAM (header-only + static => single TU, main.cpp).
static constexpr size_t kFxDelayMax =
    static_cast<size_t>(48000.0f * params::fx::kDelayBufSeconds);
static daisysp::DelayLine<float, kFxDelayMax> DSY_SDRAM_BSS s_fx_del_l;
static daisysp::DelayLine<float, kFxDelayMax> DSY_SDRAM_BSS s_fx_del_r;

class GlobalFx {
 public:
  enum Mode { OFF = 0, DELAY = 1, REVERB = 2 };

  void Init(float sample_rate) {
    // NOTE: all member state is set HERE (not via default member initializers),
    // so GlobalFx has a trivial constructor and is safe to place in SDRAM
    // (DSY_SDRAM_BSS) -- otherwise its constructor would run before main(), i.e.
    // before SDRAM is initialized, and bus-fault on boot.
    sr_ = sample_rate;
    mix_ = 0.3f;
    fb_ = 0.0f;
    tone_coef_ = 1.0f;
    mode_ = OFF;
    wet_ramp_ = 1.0f;
    bpm_ = 120.0f;
    sync_ = 0;
    lp_l_ = lp_r_ = 0.0f;
    s_fx_del_l.Init();
    s_fx_del_r.Init();
    reverb_.Init(sample_rate);
    reverb_.SetFeedback(0.85f);
    reverb_.SetLpFreq(12000.0f);
  }

  void SetMode(Mode m) {
    if (m == mode_) return;
    // Entering delay: wipe the SDRAM buffer so stale content can't screech out.
    if (m == DELAY) {
      s_fx_del_l.Reset();
      s_fx_del_r.Reset();
      lp_l_ = lp_r_ = 0.0f;
    }
    wet_ramp_ = 0.0f;   // fade the newly-engaged effect in from silence (~30 ms)
    mode_ = m;
  }
  Mode GetMode() const { return mode_; }

  // Silence the FX entirely: clear the delay lines, reverb tail and filter state. Used on
  // a mode change so the previous mode's tail can't linger (or keep costing CPU) into the
  // next one. Params are re-applied by SetParams() on the next block.
  void Reset() {
    s_fx_del_l.Reset();
    s_fx_del_r.Reset();
    lp_l_ = lp_r_ = 0.0f;
    reverb_.Init(sr_);   // clears ReverbSc's internal state/tail
    wet_ramp_ = 0.0f;
  }

  // Tempo-synced delay: bpm + division (0 = free/knob, 1..4 = 1/4 1/8 1/8. 1/16).
  void SetTempo(float bpm) { bpm_ = bpm; }
  void SetSync(int division) { sync_ = division; }

  // All inputs are normalized knob values (0..1); mapped to ranges here.
  void SetParams(float mix, float time, float fb, float tone, float decay,
                 float damp) {
    using namespace params::fx;
    mix_ = mix;

    float ds;
    if (sync_ > 0) {   // delay time locked to the clock at a musical division
      static const float kBeats[4] = {1.0f, 0.5f, 0.75f, 0.25f};   // 1/4 1/8 1/8. 1/16
      float beats = kBeats[sync_ > 4 ? 3 : sync_ - 1];
      ds = (60.0f / (bpm_ < 20.0f ? 20.0f : bpm_)) * beats * sr_;
    } else {
      ds = daisysp::fmap(time, kDelayMinMs, kDelayMaxMs, daisysp::Mapping::EXP) * 0.001f * sr_;
    }
    if (ds < 1.0f) ds = 1.0f;
    if (ds > static_cast<float>(kFxDelayMax - 1))
      ds = static_cast<float>(kFxDelayMax - 1);
    s_fx_del_l.SetDelay(ds);
    s_fx_del_r.SetDelay(ds);

    fb_ = daisysp::fmap(fb, 0.0f, kDelayFbMax, daisysp::Mapping::LINEAR);
    float tone_hz =
        daisysp::fmap(tone, kDelayToneMinHz, kDelayToneMaxHz, daisysp::Mapping::EXP);
    tone_coef_ = OnePoleCoef(tone_hz);

    reverb_.SetFeedback(
        daisysp::fmap(decay, kRevDecayMin, kRevDecayMax, daisysp::Mapping::LINEAR));
    reverb_.SetLpFreq(
        daisysp::fmap(damp, kRevDampMinHz, kRevDampMaxHz, daisysp::Mapping::EXP));
  }

  // In-place stereo process.
  void Process(float* l, float* r, size_t size) {
    if (mode_ == OFF) return;
    const float ramp_inc = 1.0f / (0.03f * sr_);   // ~30 ms wet fade-in on engage
    for (size_t i = 0; i < size; ++i) {
      if (wet_ramp_ < 1.0f) {
        wet_ramp_ += ramp_inc;
        if (wet_ramp_ > 1.0f) wet_ramp_ = 1.0f;
      }
      const float wmix = mix_ * wet_ramp_;   // ramped wet level avoids the engage transient
      float dry_l = l[i], dry_r = r[i];
      if (mode_ == DELAY) {
        float d_l = s_fx_del_l.Read();
        float d_r = s_fx_del_r.Read();
        // One-pole lowpass in the feedback path = darker repeats.
        lp_l_ += tone_coef_ * (d_l - lp_l_);
        lp_r_ += tone_coef_ * (d_r - lp_r_);
        s_fx_del_l.Write(dry_l + lp_l_ * fb_);
        s_fx_del_r.Write(dry_r + lp_r_ * fb_);
        l[i] = dry_l * (1.0f - wmix) + d_l * wmix;
        r[i] = dry_r * (1.0f - wmix) + d_r * wmix;
      } else {  // REVERB
        float w_l, w_r;
        reverb_.Process(dry_l, dry_r, &w_l, &w_r);
        l[i] = dry_l * (1.0f - wmix) + w_l * wmix;
        r[i] = dry_r * (1.0f - wmix) + w_r * wmix;
      }
    }
  }

 private:
  float OnePoleCoef(float hz) const {
    float c = 1.0f - expf(-2.0f * 3.14159265f * hz / sr_);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
  }

  // No default member initializers -> trivial construction -> SDRAM-safe.
  // All of these are set in Init() (and SetMode/SetParams each block).
  daisysp::ReverbSc reverb_;
  float sr_;
  float mix_;
  float fb_;
  float tone_coef_;
  float lp_l_, lp_r_;
  float wet_ramp_;   // 0..1 wet fade-in on engage (set to 0 by SetMode on a change)
  float bpm_;        // clock tempo for synced delay
  int   sync_;       // 0 = free (knob ms), 1..4 = tempo division
  Mode  mode_;
};

}  // namespace synthbox
