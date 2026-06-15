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

namespace synthbox {

// SDRAM capture buffer (mono). Header-only + 'static' = single TU (main.cpp).
static constexpr size_t kGranBufLen =
    static_cast<size_t>(48000.0f * params::granular::kBufSeconds);
static float DSY_SDRAM_BSS s_gran_buf[kGranBufLen];

class GranularMode : public IMode {
 public:
  void Init(float sample_rate, Hothouse& /*hw*/) override {
    sr_ = sample_rate;
    rng_.Init();
    write_pos_ = 0;
    valid_len_ = 0;
    frozen_ = false;
    spawn_phase_ = 0.0f;
    for (int i = 0; i < params::granular::kMaxGrains; ++i) grains_[i].active = false;
    for (size_t i = 0; i < kGranBufLen; ++i) s_gran_buf[i] = 0.0f;
  }

  void Action() override { frozen_ = !frozen_; }  // toggle freeze

  void Control(Hothouse& hw, ModContext& ctx) override {
    using namespace params::granular;
    quantize_ = TogglePos(hw, Hothouse::TOGGLESWITCH_2);

    float k_size = hw.GetKnobValue(Hothouse::KNOB_1);
    float k_dens = hw.GetKnobValue(Hothouse::KNOB_2);
    float k_pitch = hw.GetKnobValue(Hothouse::KNOB_3);
    float k_spread = hw.GetKnobValue(Hothouse::KNOB_4);
    float k_scatter = hw.GetKnobValue(Hothouse::KNOB_5);
    float k_mix = hw.GetKnobValue(Hothouse::KNOB_6);

    grain_len_ = (kGrainSizeMinMs + k_size * (kGrainSizeMaxMs - kGrainSizeMinMs)) *
                 0.001f * sr_;
    // Density also rides the random LFO a touch for organic motion.
    float dens = kDensityMinHz + k_dens * (kDensityMaxHz - kDensityMinHz);
    density_ = dens * (1.0f + 0.2f * ctx.mod.Lfo2());
    pitch_semi_ = kPitchSemiMin + k_pitch * (kPitchSemiMax - kPitchSemiMin);
    // Analog sensor (e.g. pressure/light) adds to pitch spread when wired.
    spread_semi_ = k_spread * kPitchSpreadMax + (ctx.sensors.Pressure() - 0.5f) * 4.0f;
    if (spread_semi_ < 0.0f) spread_semi_ = 0.0f;
    scatter_ = k_scatter;
    mix_ = k_mix;
  }

  void ProcessBlock(AudioHandle::InputBuffer in,
                    AudioHandle::OutputBuffer out, size_t size) override {
    for (size_t i = 0; i < size; ++i) {
      float dry = 0.5f * (in[0][i] + in[1][i]);

      // Record into the circular buffer unless frozen.
      if (!frozen_) {
        s_gran_buf[write_pos_] = dry;
        write_pos_ = (write_pos_ + 1) % kGranBufLen;
        if (valid_len_ < kGranBufLen) valid_len_++;
      }

      // Spawn grains at the requested density.
      spawn_phase_ += density_ / sr_;
      while (spawn_phase_ >= 1.0f) {
        spawn_phase_ -= 1.0f;
        SpawnGrain();
      }

      // Sum active grains.
      float wet = 0.0f;
      for (int g = 0; g < params::granular::kMaxGrains; ++g) {
        Grain& gr = grains_[g];
        if (!gr.active) continue;
        float w = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * gr.phase));  // Hann
        wet += ReadInterp(gr.pos) * w;
        gr.pos += gr.inc;
        if (gr.pos >= static_cast<float>(kGranBufLen)) gr.pos -= kGranBufLen;
        if (gr.pos < 0.0f) gr.pos += kGranBufLen;
        gr.phase += gr.phase_inc;
        if (gr.phase >= 1.0f) gr.active = false;
      }
      wet *= 0.5f;  // headroom for overlaps

      float s = dry * (1.0f - mix_) + wet * mix_;
      out[0][i] = out[1][i] = s;
    }
  }

 private:
  struct Grain {
    bool  active = false;
    float pos = 0.0f;        // read index into buffer
    float inc = 1.0f;        // playback rate (pitch)
    float phase = 0.0f;      // 0..1 across grain
    float phase_inc = 0.0f;  // per-sample phase increment
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
      // Pitch: center +/- random spread, optional quantize.
      float semi = pitch_semi_ + rng_.Bipolar() * spread_semi_;
      if (quantize_ == 1) semi = 12.0f * roundf(semi / 12.0f);  // octaves
      else if (quantize_ == 2) semi = pitch_semi_;              // unison
      gr.inc = powf(2.0f, semi / 12.0f);
      gr.phase = 0.0f;
      gr.phase_inc = 1.0f / fmaxf(grain_len_, 1.0f);
      gr.active = true;
      return;
    }
  }

  float ReadInterp(float pos) const {
    int   i0 = static_cast<int>(pos);
    float frac = pos - i0;
    int   i1 = (i0 + 1) % kGranBufLen;
    return s_gran_buf[i0] + frac * (s_gran_buf[i1] - s_gran_buf[i0]);
  }

  Grain  grains_[params::granular::kMaxGrains];
  Rng    rng_;
  float  sr_ = 48000.0f;
  size_t write_pos_ = 0;
  size_t valid_len_ = 0;
  float  spawn_phase_ = 0.0f;
  bool   frozen_ = false;

  // staged from Control()
  float grain_len_ = 4800.0f;
  float density_ = 20.0f;
  float pitch_semi_ = 0.0f;
  float spread_semi_ = 0.0f;
  float scatter_ = 0.5f;
  float mix_ = 0.5f;
  int   quantize_ = 0;
};

}  // namespace synthbox
