// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  modulation.h  --  reusable random/generative modulation sources.
// =============================================================================
//  These run at CONTROL rate (once per audio block) and produce smooth or
//  stepped random values to modulate any parameter in any mode. Seeded from the
//  Daisy's hardware RNG for genuine (non-repeating) randomness.
//
//  All sources are header-only and dependency-light so they're easy to read,
//  reuse, and audition.
// =============================================================================
#pragma once

#include <cstdint>

#include "daisy_seed.h"
#include "per/rng.h"       // daisy::Random (hardware RNG; not pulled by daisy_seed.h)
#include "config/params.h"

namespace synthbox {

// Fast PRNG (xorshift32) seeded once from the hardware RNG. We use the hardware
// RNG for the seed, then a cheap PRNG per step to avoid blocking in the loop.
class Rng {
 public:
  void Init() { state_ = daisy::Random::GetValue() | 1u; }
  uint32_t Next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
  }
  float Unipolar() { return static_cast<float>(Next()) / 4294967295.0f; }  // 0..1
  float Bipolar() { return Unipolar() * 2.0f - 1.0f; }                     // -1..1

 private:
  uint32_t state_ = 0x1234567u;
};

// Smooth random LFO: drifts toward a fresh random target each cycle, then
// interpolates -- an organic "drunk" wander rather than a clean sine.
class RandomLfo {
 public:
  void Init(float control_rate_hz, Rng* rng) {
    cr_ = control_rate_hz;
    rng_ = rng;
    value_ = 0.0f;
    target_ = rng_->Bipolar();
    phase_ = 0.0f;
  }
  void SetFreq(float hz) { freq_ = hz; }
  // Call once per block. Returns -1..1.
  float Process() {
    phase_ += freq_ / cr_;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
      value_ = target_;
      target_ = rng_->Bipolar();
    }
    // smoothstep interpolation between value_ and target_
    float t = phase_ * phase_ * (3.0f - 2.0f * phase_);
    return value_ + (target_ - value_) * t;
  }

 private:
  Rng*  rng_ = nullptr;
  float cr_ = 1000.0f, freq_ = 1.0f;
  float value_ = 0.0f, target_ = 0.0f, phase_ = 0.0f;
};

// Sample & hold: jumps to a new random value at a fixed rate, holds between.
class SampleHold {
 public:
  void Init(float control_rate_hz, Rng* rng) {
    cr_ = control_rate_hz;
    rng_ = rng;
    value_ = rng_->Bipolar();
    phase_ = 0.0f;
  }
  void SetFreq(float hz) { freq_ = hz; }
  float Process() {
    phase_ += freq_ / cr_;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
      value_ = rng_->Bipolar();
    }
    return value_;  // -1..1, stepped
  }
  float Value() const { return value_; }

 private:
  Rng*  rng_ = nullptr;
  float cr_ = 1000.0f, freq_ = 1.0f;
  float value_ = 0.0f, phase_ = 0.0f;
};

// Bounded random walk -- returns a value that drifts by random steps but stays
// within [-1, 1]. Useful for evolving pitch/timbre that doesn't run away.
class RandomWalk {
 public:
  void Init(Rng* rng) {
    rng_ = rng;
    value_ = 0.0f;
  }
  // step: max magnitude of a single move (in the -1..1 space).
  float Process(float step) {
    value_ += rng_->Bipolar() * step;
    if (value_ > 1.0f) value_ = 1.0f;
    if (value_ < -1.0f) value_ = -1.0f;
    return value_;
  }
  float Value() const { return value_; }

 private:
  Rng*  rng_ = nullptr;
  float value_ = 0.0f;
};

// Bundle of shared modulation sources, advanced once per block in main().
// Modes read whatever they need via the accessors.
class ModEngine {
 public:
  void Init(float control_rate_hz) {
    rng_.Init();
    lfo1_.Init(control_rate_hz, &rng_);
    lfo2_.Init(control_rate_hz, &rng_);
    sh_.Init(control_rate_hz, &rng_);
    lfo1_.SetFreq(0.3f);
    lfo2_.SetFreq(1.7f);
    sh_.SetFreq(params::mod::kSampleHoldHz);
  }
  // Advance all sources one control step.
  void Process() {
    lfo1_v_ = lfo1_.Process();
    lfo2_v_ = lfo2_.Process();
    sh_v_ = sh_.Process();
  }

  Rng& rng() { return rng_; }
  float Lfo1() const { return lfo1_v_; }   // -1..1, slow drift
  float Lfo2() const { return lfo2_v_; }   // -1..1, faster
  float SH() const { return sh_v_; }       // -1..1, stepped

  void SetLfo1Freq(float hz) { lfo1_.SetFreq(hz); }
  void SetLfo2Freq(float hz) { lfo2_.SetFreq(hz); }

 private:
  Rng        rng_;
  RandomLfo  lfo1_, lfo2_;
  SampleHold sh_;
  float lfo1_v_ = 0.0f, lfo2_v_ = 0.0f, sh_v_ = 0.0f;
};

}  // namespace synthbox
