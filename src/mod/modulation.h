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
#include <cmath>

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

// Lorenz attractor -- a CONTINUOUS chaotic system: deterministic yet never
// repeating, it traces a smooth "butterfly" orbit that wanders organically and
// occasionally flips lobes. Livelier and more structured than a random LFO.
// Integrated with stability-bounded forward-Euler sub-steps once per block.
class LorenzChaos {
 public:
  void Init(float control_rate_hz) {
    cr_    = control_rate_hz;
    speed_ = params::mod::kChaosSpeed;
    Reset();
  }
  void SetSpeed(float units_per_sec) { speed_ = units_per_sec; }  // evolution rate
  // Call once per block.
  void Process() {
    const float t = speed_ / cr_;                          // Lorenz-time this tick
    const int   n = 1 + static_cast<int>(t / 0.01f);       // sub-steps keep Euler stable
    const float h = t / static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
      const float dx = kSigma * (y_ - x_);
      const float dy = x_ * (kRho - z_) - y_;
      const float dz = x_ * y_ - kBeta * z_;
      x_ += dx * h; y_ += dy * h; z_ += dz * h;
    }
    if (!(std::isfinite(x_) && std::isfinite(y_) && std::isfinite(z_))) Reset();
  }
  float X() const { return Clamp(x_ * (1.0f / 20.0f)); }            // -1..1 (smooth)
  float Y() const { return Clamp(y_ * (1.0f / 28.0f)); }            // -1..1 (smooth)
  float Z() const { return Clamp((z_ - 25.0f) * (1.0f / 25.0f)); }  // -1..1 (smooth)

 private:
  void Reset() { x_ = 0.1f; y_ = 0.0f; z_ = 0.0f; }
  static float Clamp(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }
  static constexpr float kSigma = 10.0f, kRho = 28.0f, kBeta = 8.0f / 3.0f;
  float cr_ = 1000.0f, speed_ = 2.0f;
  float x_ = 0.1f, y_ = 0.0f, z_ = 0.0f;
};

// Logistic-map sample & hold -- DISCRETE chaos: x' = r*x*(1-x). For r in
// ~3.57..4.0 the sequence never settles. Stepped like a S&H, but carrying the
// hidden banding/period-doubling structure that pure random lacks.
class LogisticChaos {
 public:
  void Init(float control_rate_hz, Rng* rng) {
    cr_    = control_rate_hz;
    r_     = params::mod::kLogisticR;
    freq_  = params::mod::kLogisticHz;
    x_     = 0.5f + 0.01f * (rng ? rng->Bipolar() : 0.0f);   // off the fixed points
    phase_ = 0.0f;
    value_ = x_ * 2.0f - 1.0f;
  }
  void SetFreq(float hz) { freq_ = hz; }
  void SetR(float r) { r_ = r; }
  void Process() {
    phase_ += freq_ / cr_;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
      x_ = r_ * x_ * (1.0f - x_);
      if (x_ < 1e-6f) x_ = 0.5f;          // nudge off a collapse to zero
      value_ = x_ * 2.0f - 1.0f;          // -1..1 (stepped)
    }
  }
  float Value() const { return value_; }

 private:
  float cr_ = 1000.0f, r_ = 3.9f, freq_ = 4.0f;
  float x_ = 0.5f, phase_ = 0.0f, value_ = 0.0f;
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
    lorenz_.Init(control_rate_hz);
    logi_.Init(control_rate_hz, &rng_);
    lfo1_.SetFreq(0.3f);
    lfo2_.SetFreq(1.7f);
    sh_.SetFreq(params::mod::kSampleHoldHz);
  }
  // Advance all sources one control step.
  void Process() {
    lfo1_v_ = lfo1_.Process();
    lfo2_v_ = lfo2_.Process();
    sh_v_ = sh_.Process();
    lorenz_.Process();
    logi_.Process();
    chaosX_ = lorenz_.X();
    chaosY_ = lorenz_.Z();      // X and Z are the classic decorrelated pair
    chaosStep_ = logi_.Value();
  }

  Rng& rng() { return rng_; }
  float Lfo1() const { return lfo1_v_; }       // -1..1, slow drift
  float Lfo2() const { return lfo2_v_; }       // -1..1, faster
  float SH() const { return sh_v_; }           // -1..1, stepped
  float ChaosX() const { return chaosX_; }     // -1..1, smooth Lorenz (primary)
  float ChaosY() const { return chaosY_; }     // -1..1, smooth Lorenz (decorrelated)
  float ChaosStep() const { return chaosStep_; }  // -1..1, stepped logistic chaos

  void SetLfo1Freq(float hz) { lfo1_.SetFreq(hz); }
  void SetLfo2Freq(float hz) { lfo2_.SetFreq(hz); }
  void SetChaosSpeed(float units_per_sec) { lorenz_.SetSpeed(units_per_sec); }

 private:
  Rng          rng_;
  RandomLfo    lfo1_, lfo2_;
  SampleHold   sh_;
  LorenzChaos  lorenz_;
  LogisticChaos logi_;
  float lfo1_v_ = 0.0f, lfo2_v_ = 0.0f, sh_v_ = 0.0f;
  float chaosX_ = 0.0f, chaosY_ = 0.0f, chaosStep_ = 0.0f;
};

}  // namespace synthbox
