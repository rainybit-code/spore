// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  generative_mode.h  --  MODE 3: self-playing AMBIENT landscape.
// =============================================================================
//  This mode is just the shared Voice pool (dsp/voice.h) driven by a randomizer
//  instead of MIDI: a slow clock seeds (occasionally chordal) notes from a
//  bounded random walk, each voice swells in and out over seconds, and the whole
//  thing runs through a big dedicated reverb. Because it reuses Voice, it gets
//  the same engine the Synth mode has (incl. the wavetable engine) for free.
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
#include "dsp/voice.h"
#include "modes/mode.h"
#include "config/params.h"
#include "config/gen_params.h"

namespace synthbox {

// Dedicated lush reverb for the ambient mode. Static => single TU (main.cpp);
// in SDRAM because ReverbSc carries a large internal buffer.
static daisysp::ReverbSc DSY_SDRAM_BSS s_gen_reverb;

class GenerativeMode : public IMode {
 public:
  static constexpr int kVoices = 5;

  void Init(float sample_rate, Hothouse& /*hw*/) override {
    sr_ = sample_rate;
    GenerateWavetables();   // shared with Synth; harmless to ensure it's filled
    for (int i = 0; i < kVoices; ++i) {
      voices_[i].Init(sample_rate);
      voices_[i].SetWave(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
      panL_[i] = panR_[i] = 0.5f;
      off_[i] = -1.0f;
    }
    rng_.Init();
    walk_.Init(&rng_);
    RollPatch();   // pick the starting timbre from the RNG
    s_gen_reverb.Init(sample_rate);
    s_gen_reverb.SetFeedback(0.9f);
    s_gen_reverb.SetLpFreq(8000.0f);
    timer_ = 0.0f;
    steal_ = 0;
    cutoff_ = 1200.0f;
    revMix_ = 0.6f;
  }

  void Action() override { walk_.Init(&rng_); RollPatch(); timer_ = 0.0f; }  // re-seed: new walk + timbre

  // Entering Generative: silence voices + clear its dedicated reverb tail so it starts clean.
  void OnEnter() override {
    for (int i = 0; i < kVoices; ++i) { voices_[i].NoteOff(); off_[i] = -1.0f; }
    s_gen_reverb.Init(sr_);   // params re-applied by Control() on the next block
    timer_ = 0.0f;
  }

  void Control(Hothouse& hw, ModContext& ctx) override {
    quantize_ = ctx.variant;   // TOGGLE 2 (or MIDI override): chromatic / penta / major

    float k_tempo = ctx.knob[Hothouse::KNOB_1];
    float k_range = ctx.knob[Hothouse::KNOB_2];
    float k_tone  = ctx.knob[Hothouse::KNOB_3];
    float k_rev   = ctx.knob[Hothouse::KNOB_4];
    float k_dens  = ctx.knob[Hothouse::KNOB_5];
    float k_drift = ctx.knob[Hothouse::KNOB_6];

    bpm_     = 20.0f + k_tempo * 70.0f;                 // slow end: 20..90 BPM
    range_   = 5.0f + k_range * 24.0f;                  // +/- semitone spread
    density_ = k_dens;                                  // chord chance + event rate
    drift_   = k_drift;
    revMix_  = 0.30f + k_rev * 0.65f;                   // lots of reverb at the top

    const float bright = g_genParams.v[GP_BRIGHT];   // live cutoff bias (dark .. bright)
    const float wander = g_genParams.v[GP_WANDER];   // continuous timbre-morph depth
    float tone = 300.0f + k_tone * k_tone * (9000.0f - 300.0f);
    tone *= 0.45f + bright * 1.25f;                              // Brightness biases the cutoff
    cutoff_ = tone * (1.0f + (drift_ * 0.3f + wander * 0.6f) * ctx.mod.ChaosX());  // chaotic sway
    center_ = 48.0f + (ctx.sensors.Light() - 0.5f) * 12.0f;    // sensor nudges pitch center

    s_gen_reverb.SetFeedback(0.84f + k_rev * 0.15f);
    s_gen_reverb.SetLpFreq(4000.0f + k_tone * 8000.0f);

    // Per-block voice params come from the seeded patch (timbre) + macro knobs.
    // Scan drifts slowly so a wavetable patch keeps evolving; drift widens detune.
    float scan = daisysp::fclamp(pScan_ + (drift_ * 0.15f + wander * 0.35f) * ctx.mod.ChaosY(), 0.0f, 1.0f);
    const float det = pDetune_ + drift_ * 0.008f;
    for (int i = 0; i < kVoices; ++i) {
      voices_[i].SetWave(pWave_);
      voices_[i].engine  = static_cast<float>(pEngine_);
      voices_[i].wtBank  = static_cast<float>(pBank_);
      voices_[i].wtPos   = scan;
      voices_[i].fmAmt   = pFmAmt_;
      voices_[i].fmRatio = pFmRatio_;
      voices_[i].fold    = pFold_;
      voices_[i].cutoff  = cutoff_;
      voices_[i].detune  = det;
      voices_[i].subLvl  = 0.22f;
      voices_[i].fenvAmt = 0.0f;
      voices_[i].pitchMod = 1.0f;
      voices_[i].res        = pRes_;
      voices_[i].drive      = pDrive_;
      voices_[i].filterType = static_cast<float>(pFilter_);
      voices_[i].uni        = static_cast<float>(pUni_);
      voices_[i].subOct     = pSubOct_;
      voices_[i].SetSubWave(pSubWave_);
    }

    // Schedule scheduled note-offs (the back half of each swell).
    const float dt = 1.0f / hw.AudioCallbackRate();
    for (int i = 0; i < kVoices; ++i) {
      if (off_[i] > 0.0f) {
        off_[i] -= dt;
        if (off_[i] <= 0.0f) { voices_[i].NoteOff(); off_[i] = -1.0f; }
      }
    }

    // Slow event clock: every 1-2 beats (sparser when density is low).
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
        float s = voices_[i].Process();
        l += s * panL_[i];
        r += s * panR_[i];
      }
      l *= 0.30f; r *= 0.30f;                     // headroom for overlapping swells
      float wl, wr;
      s_gen_reverb.Process(l, r, &wl, &wr);
      out[0][n] = l * (1.0f - revMix_) + wl * revMix_;
      out[1][n] = r * (1.0f - revMix_) + wr * revMix_;
    }
  }

 private:
  // Roll a whole timbre from the RNG: which engine + waveform/bank + the digital
  // params + detune/resonance. Called at Init and on re-seed (FS2), so each seed
  // is a fresh sonic landscape that the macro knobs then play on top of.
  void RollPatch() {
    static const int kWaves[3] = {daisysp::Oscillator::WAVE_POLYBLEP_SAW,
                                  daisysp::Oscillator::WAVE_POLYBLEP_TRI,
                                  daisysp::Oscillator::WAVE_POLYBLEP_SQUARE};
    static const float kRatios[4] = {0.5f, 1.0f, 2.0f, 3.0f};
    // Still a random roll -- just WEIGHTED by the Brightness / Texture biases so re-seed
    // keeps surprising you within the character you've dialed in (GP_BRIGHT / GP_TEXTURE).
    const float br = g_genParams.v[GP_BRIGHT];     // 0 dark .. 1 bright
    const float tx = g_genParams.v[GP_TEXTURE];    // 0 clean .. 1 wild
    pEngine_  = (rng_.Unipolar() < 0.5f) ? 0 : 1;                 // analog or wavetable
    pWave_    = kWaves[static_cast<int>(rng_.Unipolar() * 2.999f)];
    pBank_    = static_cast<int>(rng_.Unipolar() * 4.999f);       // wavetable bank 0..4
    pScan_    = daisysp::fclamp(rng_.Unipolar() * 0.6f + (br - 0.5f) * 0.9f + 0.2f, 0.0f, 1.0f);
    pFmAmt_   = (rng_.Unipolar() < 0.20f + tx * 0.55f) ? rng_.Unipolar() * (0.12f + tx * 0.45f) : 0.0f;
    pFmRatio_ = kRatios[static_cast<int>(rng_.Unipolar() * 3.999f)];
    pFold_    = (rng_.Unipolar() < 0.12f + tx * 0.45f) ? rng_.Unipolar() * (0.20f + tx * 0.45f) : 0.0f;
    pDetune_  = 0.003f + rng_.Unipolar() * 0.012f;
    pRes_     = daisysp::fclamp(0.10f + rng_.Unipolar() * 0.30f + tx * 0.20f, 0.0f, 0.85f);
    pDrive_   = (rng_.Unipolar() < 0.30f + tx * 0.50f) ? rng_.Unipolar() * (0.30f + tx * 0.55f) : 0.0f;
    pFilter_  = (rng_.Unipolar() < 0.30f + tx * 0.45f) ? 1 : 0;              // Svf or fat Moog
    pUni_     = (rng_.Unipolar() < 0.35f + tx * 0.55f) ? 2 : 1;              // keep CPU sane
    pSubOct_  = (rng_.Unipolar() < 0.5f) ? 0.5f : 0.25f;
    pSubWave_ = (rng_.Unipolar() < 0.5f) ? daisysp::Oscillator::WAVE_POLYBLEP_SQUARE
                                         : daisysp::Oscillator::WAVE_SIN;
  }

  // Seed a new note (sometimes a small chord) from the random walk.
  void SeedEvent() {
    const float motion = g_genParams.v[GP_MOTION];   // walk step: gentle drift -> wide leaps
    const float swell  = g_genParams.v[GP_SWELL];    // note length: short stabs -> long pads
    const float chordAmt = g_genParams.v[GP_CHORD];  // single notes -> triads

    const float w = walk_.Process(0.08f + motion * 0.7f);   // -1..1
    float root = center_ + w * range_;

    int chord = 1;
    if (rng_.Unipolar() < chordAmt) chord = (rng_.Unipolar() < 0.45f) ? 3 : 2;

    const float atk = (0.2f + swell * 3.5f) * (0.7f + 0.6f * rng_.Unipolar());   // stab .. long rise
    const float rel = (0.8f + swell * 10.0f) * (0.8f + 0.4f * rng_.Unipolar());  // short .. long fall
    const float pan = rng_.Bipolar() * 0.85f;     // place the whole event in the field

    static const float kStack[3] = {0.0f, 7.0f, 4.0f};  // root, fifth, third
    for (int c = 0; c < chord; ++c) {
      float note = QuantizeToScale(root + kStack[c], quantize_);
      note = daisysp::fclamp(note, 24.0f, 90.0f);
      TriggerVoice(note, atk, rel, pan);
    }
  }

  void TriggerVoice(float note, float atk, float rel, float pan) {
    int idx = -1;
    for (int i = 0; i < kVoices; ++i)
      if (!voices_[i].Active()) { idx = i; break; }
    if (idx < 0) { idx = steal_; steal_ = (steal_ + 1) % kVoices; }
    // Shape a swell: rise over the attack, hold at full, then NoteOff -> long release.
    voices_[idx].amp_.SetAttackTime(atk);
    voices_[idx].amp_.SetDecayTime(0.5f);
    voices_[idx].amp_.SetSustainLevel(1.0f);
    voices_[idx].amp_.SetReleaseTime(rel);
    voices_[idx].NoteOn(note, 0.7f + rng_.Unipolar() * 0.3f);
    off_[idx] = atk;                              // release begins once it reaches the peak
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

  Voice      voices_[kVoices];
  float      panL_[kVoices], panR_[kVoices];
  float      off_[kVoices];   // seconds until the scheduled NoteOff (-1 = none)
  Rng        rng_;
  RandomWalk walk_;
  float sr_ = 48000.0f;
  float timer_ = 0.0f;
  float bpm_ = 50.0f, range_ = 12.0f, density_ = 0.4f, drift_ = 0.3f;
  float cutoff_ = 1200.0f, revMix_ = 0.6f, center_ = 48.0f;
  int   quantize_ = 0, steal_ = 0;

  // seeded patch (timbre) — rolled from the RNG, persists until the next re-seed
  int   pEngine_ = 0, pWave_ = daisysp::Oscillator::WAVE_POLYBLEP_SAW, pBank_ = 0;
  float pScan_ = 0.3f, pFmAmt_ = 0.0f, pFmRatio_ = 1.0f, pFold_ = 0.0f;
  float pDetune_ = 0.006f, pRes_ = 0.18f;
  float pDrive_ = 0.0f, pSubOct_ = 0.5f;
  int   pFilter_ = 0, pUni_ = 1, pSubWave_ = daisysp::Oscillator::WAVE_POLYBLEP_SQUARE;
};

}  // namespace synthbox
