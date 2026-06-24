// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  granular_mode.h  --  MODE 2: granular texture from the BUILT-IN audio input.
// =============================================================================
//  Records the Hothouse audio input (1/4" jack) into an SDRAM buffer and sprays
//  overlapping, pitch-shifted, windowed grains from it. FOOTSWITCH 2 freezes the
//  buffer (stops recording) so you can play with a captured moment forever.
//
//    KNOB 1: grain size    KNOB 2: density     KNOB 3: pitch (center)
//    KNOB 4: pitch spread  KNOB 5: scatter     KNOB 6: dry/wet mix
//    TOGGLE 2: pitch quantize (UP free / MID octaves / DOWN unison)
//    FOOTSWITCH 2: freeze
//
//  This is a clean v1 engine -- good starting point to refine on hardware.
// =============================================================================
#pragma once

#include <cmath>

#include "daisysp.h"
#include "modes/mode.h"
#include "config/params.h"
#include "config/gran_params.h"

namespace synthbox {

// SDRAM capture buffer (mono). Header-only + 'static' = single TU (main.cpp).
static constexpr size_t kGranBufLen =
    static_cast<size_t>(48000.0f * params::granular::kBufSeconds);
static float DSY_SDRAM_BSS s_gran_buf[kGranBufLen];

// Precomputed Hann window (power-of-two for cheap masked indexing) -- avoids a
// per-grain-per-sample cosf() in the hot loop (up to kMaxGrains cosf/sample).
static constexpr int kHannLen = 1024;
static float s_hann[kHannLen];

class GranularMode : public IMode {
 public:
  void Init(float sample_rate, Hothouse& /*hw*/) override {
    sr_ = sample_rate;
    rng_.Init();
    write_pos_ = 0;
    valid_len_ = 0;
    frozen_ = false;
    spawn_phase_ = 0.0f;
    next_spawn_ = 1.0f;
    for (int i = 0; i < params::granular::kMaxGrains; ++i) grains_[i].active = false;
    for (size_t i = 0; i < kGranBufLen; ++i) s_gran_buf[i] = 0.0f;
    for (int i = 0; i < kHannLen; ++i)
      s_hann[i] = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * i / kHannLen));
  }

  void Action() override { frozen_ = !frozen_; }  // toggle freeze

  // Entering Granular: drop any in-flight grains so it starts clean (keep the buffer).
  void OnEnter() override {
    for (int i = 0; i < params::granular::kMaxGrains; ++i) grains_[i].active = false;
    spawn_phase_ = 0.0f;
  }

  void Control(Hothouse& hw, ModContext& ctx) override {
    using namespace params::granular;
    quantize_ = ctx.variant;   // TOGGLE 2 (or MIDI override): free / octaves / unison

    float k_size = ctx.knob[Hothouse::KNOB_1];
    float k_dens = ctx.knob[Hothouse::KNOB_2];
    float k_pitch = ctx.knob[Hothouse::KNOB_3];
    float k_spread = ctx.knob[Hothouse::KNOB_4];
    float k_scatter = ctx.knob[Hothouse::KNOB_5];
    float k_mix = ctx.knob[Hothouse::KNOB_6];

    grain_len_ = (kGrainSizeMinMs + k_size * (kGrainSizeMaxMs - kGrainSizeMinMs)) *
                 0.001f * sr_;
    // Density rides the chaos source a touch for organic, never-repeating motion.
    float dens = kDensityMinHz + k_dens * (kDensityMaxHz - kDensityMinHz);
    density_ = dens * (1.0f + 0.2f * ctx.mod.ChaosX());
    // Pitch quantized to whole semitones so it stays in tune with what you play
    // (continuous mapping over +/-24 semis detuned against everything); a small
    // center detent snaps cleanly to unison.
    if (k_pitch > 0.48f && k_pitch < 0.52f) {
      pitch_semi_ = 0.0f;
    } else {
      pitch_semi_ = roundf(kPitchSemiMin + k_pitch * (kPitchSemiMax - kPitchSemiMin));
    }
    // Analog sensor (e.g. pressure/light) adds to pitch spread when wired.
    spread_semi_ = k_spread * kPitchSpreadMax + (ctx.sensors.Pressure() - 0.5f) * 4.0f;
    if (spread_semi_ < 0.0f) spread_semi_ = 0.0f;
    scatter_ = k_scatter;
    mix_ = k_mix;

    // Web-pod engine controls (gran_params.h); defaults reproduce the stock sound.
    rev_       = g_granParams.v[GR_REVERSE];
    width_     = g_granParams.v[GR_WIDTH];
    shape_gain_ = 1.0f + g_granParams.v[GR_SHAPE] * 7.0f;          // 1 soft Hann .. 8 hard gate
    scale_     = static_cast<int>(g_granParams.v[GR_SCALE] * 3.99f);  // 0 off/1 maj/2 min/3 penta
  }

  void ProcessBlock(AudioHandle::InputBuffer in,
                    AudioHandle::OutputBuffer out, size_t size) override {
    for (size_t i = 0; i < size; ++i) {
      float dry = 0.5f * (in[0][i] + in[1][i]);

      // Record into the circular buffer unless frozen.
      if (!frozen_) {
        s_gran_buf[write_pos_] = dry;
        if (++write_pos_ >= kGranBufLen) write_pos_ = 0;   // branch wrap (cheaper than %)
        if (valid_len_ < kGranBufLen) valid_len_++;
      }

      // Spawn grains at the requested density, with a randomized interval so short
      // grains form a smooth cloud instead of a periodic buzz at the spawn rate.
      spawn_phase_ += density_ / sr_;
      while (spawn_phase_ >= next_spawn_) {
        spawn_phase_ -= next_spawn_;
        next_spawn_ = 0.6f + 0.8f * rng_.Unipolar();   // 0.6..1.4 x the nominal period
        SpawnGrain();
      }

      // Sum active grains (stereo: each grain has its own pan).
      float wetL = 0.0f, wetR = 0.0f;
      for (int g = 0; g < params::granular::kMaxGrains; ++g) {
        Grain& gr = grains_[g];
        if (!gr.active) continue;
        float w = s_hann[static_cast<int>(gr.phase * kHannLen) & (kHannLen - 1)];  // Hann (LUT)
        w = fminf(1.0f, w * shape_gain_);   // shape: scale + clip = soft Hann -> flat-top gate
        float sig = ReadInterp(gr.pos) * w;
        wetL += sig * gr.panL;
        wetR += sig * gr.panR;
        gr.pos += gr.inc;
        if (gr.pos >= static_cast<float>(kGranBufLen)) gr.pos -= kGranBufLen;
        if (gr.pos < 0.0f) gr.pos += kGranBufLen;
        gr.phase += gr.phase_inc;
        if (gr.phase >= 1.0f) gr.active = false;
      }
      wetL *= 0.5f; wetR *= 0.5f;  // headroom for overlaps

      out[0][i] = dry * (1.0f - mix_) + wetL * mix_;
      out[1][i] = dry * (1.0f - mix_) + wetR * mix_;
    }
  }

 private:
  struct Grain {
    bool  active = false;
    float pos = 0.0f;        // read index into buffer
    float inc = 1.0f;        // playback rate (pitch)
    float phase = 0.0f;      // 0..1 across grain
    float phase_inc = 0.0f;  // per-sample phase increment
    float panL = 1.0f;       // stereo gains (both 1 = centered/mono)
    float panR = 1.0f;
  };

  void SpawnGrain() {
    if (valid_len_ < 64) return;  // nothing recorded yet
    for (int g = 0; g < params::granular::kMaxGrains; ++g) {
      if (grains_[g].active) continue;
      Grain& gr = grains_[g];
      // Random start position within the captured region (scatter widens it).
      float anchor = static_cast<float>(write_pos_);
      float back = (0.1f + scatter_ * 0.9f) * valid_len_ * rng_.Unipolar();
      float start = anchor - back;
      while (start < 0.0f) start += kGranBufLen;
      gr.pos = start;
      // Pitch: dialed center +/- random spread, with optional quantize.
      float semi = pitch_semi_ + rng_.Bipolar() * spread_semi_;
      if (scale_ > 0) semi = SnapToScale(semi, scale_);   // scale-lock (off/maj/min/penta)
      int   oct = 0;
      if (quantize_ == 1) {            // octaves: keep the dialed pitch, snap the spread to octaves
        oct = static_cast<int>(roundf((semi - pitch_semi_) / 12.0f));
        semi = pitch_semi_;
      } else if (quantize_ == 2) {     // unison: ignore spread entirely
        semi = pitch_semi_;
      }
      gr.inc = powf(2.0f, semi / 12.0f);
      if (oct != 0) gr.inc = ldexpf(gr.inc, oct);      // exact x2^oct so octaves stay perfectly in tune
      if (rng_.Unipolar() < rev_) gr.inc = -gr.inc;    // reverse a fraction of grains (shimmer/texture)
      // Stereo pan: x in -1..1 scaled by width (x=0 => both channels full = mono).
      float x = rng_.Bipolar() * width_;
      gr.panL = 1.0f - fmaxf(0.0f, x);
      gr.panR = 1.0f + fminf(0.0f, x);
      gr.phase = 0.0f;
      gr.phase_inc = 1.0f / fmaxf(grain_len_, 1.0f);
      gr.active = true;
      return;
    }
  }

  float ReadInterp(float pos) const {
    int   i0 = static_cast<int>(pos);
    float frac = pos - i0;
    int   i1 = i0 + 1; if (i1 >= static_cast<int>(kGranBufLen)) i1 = 0;   // branch wrap
    return s_gran_buf[i0] + frac * (s_gran_buf[i1] - s_gran_buf[i0]);
  }

  // Snap a semitone offset to the nearest degree of a scale (1 major / 2 minor /
  // 3 pentatonic), keeping the octave. Used for grain pitch when scale-lock is on.
  static float SnapToScale(float semi, int sc) {
    static const int kMaj[7] = {0, 2, 4, 5, 7, 9, 11};
    static const int kMin[7] = {0, 2, 3, 5, 7, 8, 10};
    static const int kPen[5] = {0, 3, 5, 7, 10};
    const int* tbl = (sc == 1) ? kMaj : (sc == 2) ? kMin : kPen;
    const int  n   = (sc == 3) ? 5 : 7;
    float oct = floorf(semi / 12.0f);
    float within = semi - oct * 12.0f;                 // 0..12
    float best = static_cast<float>(tbl[0]);
    float bd = fabsf(within - best);
    for (int i = 1; i < n; ++i) {
      float d = fabsf(within - tbl[i]);
      if (d < bd) { bd = d; best = static_cast<float>(tbl[i]); }
    }
    if (fabsf(within - 12.0f) < bd) best = 12.0f;      // wrap up to the octave
    return oct * 12.0f + best;
  }

  Grain  grains_[params::granular::kMaxGrains];
  Rng    rng_;
  float  sr_ = 48000.0f;
  size_t write_pos_ = 0;
  size_t valid_len_ = 0;
  float  spawn_phase_ = 0.0f;
  float  next_spawn_ = 1.0f;   // jittered spawn threshold (set per grain)
  bool   frozen_ = false;

  // staged from Control()
  float grain_len_ = 4800.0f;
  float density_ = 20.0f;
  float pitch_semi_ = 0.0f;
  float spread_semi_ = 0.0f;
  float scatter_ = 0.5f;
  float mix_ = 0.5f;
  int   quantize_ = 0;
  // web-pod engine controls (gran_params.h)
  float rev_ = 0.30f;          // reverse-grain probability
  float width_ = 0.0f;         // stereo width (per-grain pan spread)
  float shape_gain_ = 1.0f;    // grain-window shape (1 soft Hann .. 8 hard gate)
  int   scale_ = 0;            // pitch scale-lock (0 off / 1 maj / 2 min / 3 penta)
};

}  // namespace synthbox
