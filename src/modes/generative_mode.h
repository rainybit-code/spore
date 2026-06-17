// =============================================================================
//  generative_mode.h  --  MODE 3: self-playing AMBIENT landscape.
// =============================================================================
//  An evolving pad: a small pool of voices swell in and out over seconds while a
//  slow clock seeds new (occasionally chordal) notes from a bounded random walk,
//  quantized to a scale. Everything runs through a big dedicated reverb so it
//  feels like a wash of sound. No input needed -- it plays itself.
//
//    KNOB 1: tempo (slow, ~20-90 BPM)   KNOB 2: pitch range
//    KNOB 3: tone (filter)              KNOB 4: reverb (size + wet)
//    KNOB 5: density (chords / motion)  KNOB 6: drift (detune + filter sway)
//    TOGGLE 2: scale (UP chromatic / MID minor-pentatonic / DOWN major)
//    FOOTSWITCH 2: re-seed the random walk
// =============================================================================
#pragma once

#include <cmath>

#include "daisysp.h"
#include "daisysp-lgpl.h"  // ReverbSc
#include "modes/mode.h"
#include "config/params.h"

namespace synthbox {

// Dedicated lush reverb for the ambient mode. Static => single TU (main.cpp);
// in SDRAM because ReverbSc carries a large internal buffer.
static daisysp::ReverbSc DSY_SDRAM_BSS s_gen_reverb;

// ---- one ambient voice: 2 detuned saws -> lowpass -> slow attack/decay swell ----
class GenVoice {
 public:
  void Init(float sr) {
    o1_.Init(sr); o2_.Init(sr);
    o1_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
    o2_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
    flt_.Init(sr); flt_.SetRes(0.18f);
    env_.Init(sr); env_.SetMin(0.0f); env_.SetMax(1.0f);
  }
  void Trigger(float note, float atk, float dec, float detune) {
    freq_ = daisysp::mtof(note);
    detune_ = detune;
    env_.SetTime(daisysp::ADENV_SEG_ATTACK, atk);
    env_.SetTime(daisysp::ADENV_SEG_DECAY, dec);
    env_.Trigger();
  }
  bool Active() const { return env_.IsRunning(); }

  float Process(float cutoff) {
    o1_.SetFreq(freq_ * (1.0f - detune_));
    o2_.SetFreq(freq_ * (1.0f + detune_));
    float e = env_.Process();
    flt_.SetFreq(daisysp::fclamp(cutoff, 40.0f, 12000.0f));
    flt_.Process((o1_.Process() + o2_.Process()) * 0.5f);
    return flt_.Low() * e;
  }

 private:
  daisysp::Oscillator o1_, o2_;
  daisysp::Svf        flt_;
  daisysp::AdEnv      env_;
  float freq_ = 220.0f, detune_ = 0.005f;
};

class GenerativeMode : public IMode {
 public:
  static constexpr int kVoices = 5;

  void Init(float sample_rate, Hothouse& /*hw*/) override {
    sr_ = sample_rate;
    for (int i = 0; i < kVoices; ++i) {
      voices_[i].Init(sample_rate);
      panL_[i] = panR_[i] = 0.5f;
    }
    rng_.Init();
    walk_.Init(&rng_);
    s_gen_reverb.Init(sample_rate);
    s_gen_reverb.SetFeedback(0.9f);
    s_gen_reverb.SetLpFreq(8000.0f);
    timer_ = 0.0f;
    steal_ = 0;
    cutoff_ = 1200.0f;
    revMix_ = 0.6f;
  }

  void Action() override { walk_.Init(&rng_); timer_ = 0.0f; }  // re-seed

  void Control(Hothouse& hw, ModContext& ctx) override {
    quantize_ = TogglePos(hw, Hothouse::TOGGLESWITCH_2);

    float k_tempo  = ctx.knob[Hothouse::KNOB_1];
    float k_range  = ctx.knob[Hothouse::KNOB_2];
    float k_tone   = ctx.knob[Hothouse::KNOB_3];
    float k_rev    = ctx.knob[Hothouse::KNOB_4];
    float k_dens   = ctx.knob[Hothouse::KNOB_5];
    float k_drift  = ctx.knob[Hothouse::KNOB_6];

    bpm_     = 20.0f + k_tempo * 70.0f;                 // slow end: 20..90 BPM
    range_   = 5.0f + k_range * 24.0f;                  // +/- semitone spread
    density_ = k_dens;                                  // chord chance + event rate
    drift_   = k_drift;
    revMix_  = 0.30f + k_rev * 0.65f;                   // lots of reverb at the top

    // Tone with a slow filter sway from a random LFO; analog sensor nudges pitch.
    float tone = 300.0f + k_tone * k_tone * (9000.0f - 300.0f);
    cutoff_ = tone * (1.0f + drift_ * 0.6f * ctx.mod.Lfo1());
    center_ = 48.0f + (ctx.sensors.Light() - 0.5f) * 12.0f;

    s_gen_reverb.SetFeedback(0.84f + k_rev * 0.15f);    // bigger space with more reverb
    s_gen_reverb.SetLpFreq(4000.0f + k_tone * 8000.0f);

    // Slow event clock: every 1-2 beats (sparser when density is low).
    const float dt = 1.0f / hw.AudioCallbackRate();
    timer_ -= dt;
    if (timer_ <= 0.0f) {
      SeedEvent();
      const float beat = 60.0f / bpm_;
      const float mult = (rng_.Unipolar() < (0.35f + density_ * 0.4f)) ? 1.0f : 2.0f;
      timer_ = beat * mult * (0.85f + 0.3f * rng_.Unipolar());
    }
  }

  void ProcessBlock(AudioHandle::InputBuffer /*in*/,
                    AudioHandle::OutputBuffer out, size_t size) override {
    for (size_t n = 0; n < size; ++n) {
      float l = 0.0f, r = 0.0f;
      for (int i = 0; i < kVoices; ++i) {
        if (!voices_[i].Active()) continue;
        float s = voices_[i].Process(cutoff_);
        l += s * panL_[i];
        r += s * panR_[i];
      }
      l *= 0.22f; r *= 0.22f;                     // headroom for overlapping swells
      float wl, wr;
      s_gen_reverb.Process(l, r, &wl, &wr);
      out[0][n] = l * (1.0f - revMix_) + wl * revMix_;
      out[1][n] = r * (1.0f - revMix_) + wr * revMix_;
    }
  }

 private:
  // Seed a new note (sometimes a small chord) from the random walk.
  void SeedEvent() {
    const float w = walk_.Process(0.25f + drift_ * 0.5f);   // -1..1
    float root = center_ + w * range_;

    int chord = 1;
    if (rng_.Unipolar() < density_) chord = (rng_.Unipolar() < 0.45f) ? 3 : 2;

    // Longer swells when sparser; drift widens attack + detune for movement.
    const float atk = 1.2f + drift_ * 2.5f + rng_.Unipolar() * 1.0f;       // ~1.2-4.7 s
    const float dec = 5.0f + (1.0f - density_) * 5.0f + rng_.Unipolar() * 2.0f; // ~5-12 s
    const float det = 0.004f + drift_ * 0.012f;
    const float pan = rng_.Bipolar() * 0.85f;     // place the whole event in the field

    static const float kStack[3] = {0.0f, 7.0f, 4.0f};  // root, fifth, third
    for (int c = 0; c < chord; ++c) {
      float note = QuantizeToScale(root + kStack[c], quantize_);
      note = daisysp::fclamp(note, 24.0f, 90.0f);
      TriggerVoice(note, atk, dec, det, pan);
    }
  }

  void TriggerVoice(float note, float atk, float dec, float det, float pan) {
    int idx = -1;
    for (int i = 0; i < kVoices; ++i)
      if (!voices_[i].Active()) { idx = i; break; }
    if (idx < 0) { idx = steal_; steal_ = (steal_ + 1) % kVoices; }   // steal oldest-ish
    voices_[idx].Trigger(note, atk, dec, det);
    panL_[idx] = 0.5f * (1.0f - pan);
    panR_[idx] = 0.5f * (1.0f + pan);
  }

  // Snap a MIDI note to a scale. mode: 0 chromatic, 1 minor pentatonic, 2 major.
  static float QuantizeToScale(float note, int scale) {
    if (scale == 0) return note;  // chromatic
    static const int kPenta[5] = {0, 3, 5, 7, 10};
    static const int kMajor[7] = {0, 2, 4, 5, 7, 9, 11};
    const int* set = (scale == 1) ? kPenta : kMajor;
    const int  n = (scale == 1) ? 5 : 7;
    int midi = static_cast<int>(note + 0.5f);
    int octave = midi / 12;
    int pc = midi % 12;
    int best = set[0], bestd = 99;
    for (int i = 0; i < n; ++i) {
      int d = pc - set[i]; if (d < 0) d = -d;
      if (d < bestd) { bestd = d; best = set[i]; }
    }
    return static_cast<float>(octave * 12 + best);
  }

  GenVoice   voices_[kVoices];
  float      panL_[kVoices], panR_[kVoices];
  Rng        rng_;
  RandomWalk walk_;
  float sr_ = 48000.0f;
  float timer_ = 0.0f;
  float bpm_ = 50.0f, range_ = 12.0f, density_ = 0.4f, drift_ = 0.3f;
  float cutoff_ = 1200.0f, revMix_ = 0.6f, center_ = 48.0f;
  int   quantize_ = 0, steal_ = 0;
};

}  // namespace synthbox
