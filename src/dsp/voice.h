// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  dsp/voice.h  --  the shared synth VOICE block (used by every voiced mode).
// =============================================================================
//  One generic voice: an analog engine (2 detuned osc + sub) OR a wavetable
//  engine (scan + FM + fold, 5 banks) -> state-variable low-pass -> amp ADSR x
//  filter AdEnv; velocity drives loudness + brightness; glide for portamento.
//
//  Modes compose this: the Synth mode drives a pool from MIDI + knob params; the
//  Generative mode drives the same pool from a randomizer. A mode just sets the
//  public per-block params and calls NoteOn/NoteOff/Process.
//
//  The wavetable banks are shared (read-only) and generated once at boot via
//  GenerateWavetables() -- call it from any voiced mode's Init (idempotent).
// =============================================================================
#pragma once

#include <cmath>

#include "daisysp.h"
#include "daisysp-lgpl.h"  // MoogLadder (fat 24 dB filter option)

namespace synthbox {

// ---- shared wavetables: banks of single-cycle frames, each morphing from sine
// (frame 0) to a richer timbre. In fast internal RAM (.bss); no flash cost.
static constexpr int kWtBanks  = 5;            // Saw / Square / Organ / Vocal / Digital
static constexpr int kWtFrames = 8;
static constexpr int kWtLen    = 512;          // power of two => phase masking
static float g_wt[kWtBanks][kWtFrames][kWtLen];

inline void WtNormalizeFrame(int b, int f) {
  float peak = 1e-6f;
  for (int i = 0; i < kWtLen; ++i) { float a = fabsf(g_wt[b][f][i]); if (a > peak) peak = a; }
  float g = 1.0f / peak;
  for (int i = 0; i < kWtLen; ++i) g_wt[b][f][i] *= g;
}

inline void GenerateWavetables() {
  for (int f = 0; f < kWtFrames; ++f) {
    int harm = 1 << f; if (harm > 64) harm = 64;        // brighter frames
    float vf1 = 2.0f + f * 0.9f, vf2 = 6.0f + f * 1.7f; // moving formant peaks (Vocal)
    float fold = 1.0f + f * 0.7f;                       // increasing fold drive (Digital)
    for (int i = 0; i < kWtLen; ++i) {
      float ph = (float)i / kWtLen * 6.2831853f;
      // 0: Saw  (all harmonics, 1/n)
      { float s = 0.0f; for (int n = 1; n <= harm; ++n) s += sinf(n * ph) / n; g_wt[0][f][i] = s; }
      // 1: Square / pulse (odd harmonics, 1/n)
      { float s = 0.0f; for (int n = 1; n <= harm; n += 2) s += sinf(n * ph) / n; g_wt[1][f][i] = s; }
      // 2: Organ / additive (first f+1 harmonics at 1/sqrt(n))
      { float s = 0.0f; int H = f + 1; for (int n = 1; n <= H; ++n) s += sinf(n * ph) / sqrtf((float)n); g_wt[2][f][i] = s; }
      // 3: Vocal / formant (harmonics shaped by two moving formant peaks)
      { float s = 0.0f;
        for (int n = 1; n <= 24; ++n) {
          float d1 = (n - vf1) / 1.6f, d2 = (n - vf2) / 2.2f;
          s += (expf(-0.5f * d1 * d1) + 0.7f * expf(-0.5f * d2 * d2)) * sinf(n * ph) / n;
        }
        g_wt[3][f][i] = s; }
      // 4: Digital (a sine driven into a triangle folder, more each frame)
      { float x = sinf(ph) * fold;
        x = fabsf(x + 1.0f); x = x - 4.0f * floorf(x * 0.25f);
        g_wt[4][f][i] = fabsf(x - 2.0f) - 1.0f; }
    }
    for (int b = 0; b < kWtBanks; ++b) WtNormalizeFrame(b, f);
  }
}

// linear interpolation within a frame, blended across the scan position
inline float WtSample(int bank, float phase, float pos) {
  float fpos = pos * (kWtFrames - 1);
  int f0 = (int)fpos;
  if (f0 > kWtFrames - 2) f0 = kWtFrames - 2;
  if (f0 < 0) f0 = 0;
  float fr = fpos - f0;
  float idx = phase * kWtLen;
  int i0 = (int)idx & (kWtLen - 1);
  int i1 = (i0 + 1) & (kWtLen - 1);
  float xf = idx - (float)((int)idx);
  const float* fa = g_wt[bank][f0];
  const float* fb = g_wt[bank][f0 + 1];
  float a = fa[i0] + (fa[i1] - fa[i0]) * xf;
  float b = fb[i0] + (fb[i1] - fb[i0]) * xf;
  return a + (b - a) * fr;
}

// triangle wavefolder: drive in, reflect back into [-1, 1] for digital grit
inline float WaveFold(float x, float amt) {
  if (amt <= 0.001f) return x;
  x *= 1.0f + amt * 4.0f;
  x = fabsf(x + 1.0f);
  x = x - 4.0f * floorf(x * 0.25f);   // wrap into [0, 4)
  return fabsf(x - 2.0f) - 1.0f;
}

// ---- one generic voice ----
class Voice {
 public:
  static constexpr int kUni = 4;   // max unison oscillators (analog super-saw)

  void Init(float sr) {
    sr_ = sr;
    for (int i = 0; i < kUni; ++i) osc_[i].Init(sr);
    sub_.Init(sr);
    SetWave(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
    sub_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SQUARE);
    flt_.Init(sr);
    mflt_.Init(sr);
    amp_.Init(sr);
    fenv_.Init(sr); fenv_.SetMin(0.0f); fenv_.SetMax(1.0f);
    fenv_.SetTime(daisysp::ADENV_SEG_ATTACK, 0.005f);
  }
  void SetWave(int wf) { for (int i = 0; i < kUni; ++i) osc_[i].SetWaveform(wf); }
  void SetSubWave(int wf) { sub_.SetWaveform(wf); }
  void SetFenvDecay(float t) { fenv_.SetTime(daisysp::ADENV_SEG_DECAY, t); }
  void SetGlide(float coef) { glideCoef_ = coef; }  // 1 = instant, <1 = portamento

  void NoteOn(float note, float vel) {
    bool fresh = !Active();   // a brand-new voice snaps; a legato re-trigger glides
    note_ = note; targetFreq_ = daisysp::mtof(note); vel_ = vel;
    if (fresh) { freq_ = targetFreq_; wtPhase_ = 0.0f; fmPhase_ = 0.0f; }
    gate_ = true; fenv_.Trigger();
  }
  void NoteOff() { gate_ = false; }
  bool Active() const { return gate_ || amp_.IsRunning(); }
  bool Gate() const { return gate_; }
  float Note() const { return note_; }
  float Vel() const { return vel_; }

  // per-block params set by the mode
  float cutoff = 1000.f, detune = 0.006f, subLvl = 0.4f, fenvAmt = 0.5f, pitchMod = 1.f;
  // digital engine (per-block): engine<0.5 = analog, else wavetable
  float engine = 0.f, wtPos = 0.3f, fmAmt = 0.f, fmRatio = 1.f, fold = 0.f, wtBank = 0.f;
  // tone shaping: drive (pre-filter saturation), resonance, filter type, unison, sub octave
  float drive = 0.f, res = 0.3f, filterType = 0.f, uni = 2.f, subOct = 0.5f;

  inline float Process() {
    freq_ += (targetFreq_ - freq_) * glideCoef_;   // portamento toward the target pitch
    float f = freq_ * pitchMod;
    float env = amp_.Process(gate_);
    float fe = fenv_.Process();
    float vb = 0.4f + 0.6f * vel_;
    float fc = daisysp::fclamp(cutoff * vb * (1.0f + fenvAmt * 5.0f * fe), 20.0f, 16000.0f);
    sub_.SetFreq(f * subOct);
    float sig;
    if (engine < 0.5f) {                            // ---- analog: super-saw unison + sub ----
      int u = (int)(uni + 0.5f);
      if (u < 1) u = 1; else if (u > kUni) u = kUni;
      sig = 0.0f;
      for (int i = 0; i < u; ++i) {
        float off = (u > 1) ? ((float)i / (u - 1) - 0.5f) * 2.0f * detune : 0.0f;
        osc_[i].SetFreq(f * (1.0f + off));
        sig += osc_[i].Process();
      }
      sig *= 1.0f / (float)u;
      sig += sub_.Process() * subLvl;
    } else {                                        // ---- wavetable: scan + FM + fold ----
      float pinc = f / sr_;
      wtPhase_ += pinc; wtPhase_ -= floorf(wtPhase_);
      float ph = wtPhase_;
      if (fmAmt > 0.001f) {                         // FM through the sine frame (bank 0, frame 0)
        fmPhase_ += pinc * fmRatio; fmPhase_ -= floorf(fmPhase_);
        float mod = g_wt[0][0][(int)(fmPhase_ * kWtLen) & (kWtLen - 1)];
        ph += fmAmt * mod * 0.5f; ph -= floorf(ph);
      }
      int bank = (int)(wtBank + 0.5f);
      if (bank < 0) bank = 0; else if (bank >= kWtBanks) bank = kWtBanks - 1;
      sig = WaveFold(WtSample(bank, ph, wtPos), fold) + sub_.Process() * subLvl;
    }
    // Pre-filter saturation -> grit (and dirties the filter for fat/acid tones).
    float drv = Saturate(sig, drive) * 0.6f;
    if (filterType < 0.5f) {                         // clean 2-pole Svf
      flt_.SetFreq(fc); flt_.SetRes(res * 0.85f);
      flt_.Process(drv);
      return flt_.Low() * env * vel_;
    }
    mflt_.SetFreq(fc); mflt_.SetRes(res * 0.95f);    // fat 4-pole MoogLadder
    return mflt_.Process(drv) * env * vel_;
  }

  daisysp::Adsr amp_;   // public: modes set ADSR times directly

 private:
  // Fast bounded tanh-ish waveshaper (Pade approx, clamped so it saturates to +/-1).
  static inline float Saturate(float x, float d) {
    if (d <= 0.001f) return x;
    float xk = x * (1.0f + d * 8.0f);
    if (xk > 3.0f) xk = 3.0f; else if (xk < -3.0f) xk = -3.0f;
    return xk * (27.0f + xk * xk) / (27.0f + 9.0f * xk * xk);
  }

  daisysp::Oscillator osc_[kUni], sub_;
  daisysp::Svf        flt_;
  daisysp::MoogLadder mflt_;
  daisysp::AdEnv      fenv_;
  float note_ = 0.f, freq_ = 220.f, targetFreq_ = 220.f, vel_ = 0.8f;
  float glideCoef_ = 1.0f;
  float sr_ = 48000.f;
  float wtPhase_ = 0.f, fmPhase_ = 0.f;   // wavetable carrier + FM modulator phases
  bool  gate_ = false;
};

}  // namespace synthbox
