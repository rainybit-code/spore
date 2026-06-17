// =============================================================================
//  synth_mode.h  --  MODE 1: polyphonic synth (Phase 2/3: poly + LFO).
// =============================================================================
//  Voice pool (note alloc + graceful steal). Each voice: 2 detuned osc + sub ->
//  state-variable lowpass (cheap) -> ADSR + filter envelope; velocity drives
//  loudness + brightness. A global LFO (rate/depth/shape) routes to vibrato,
//  filter, or tremolo. Voices panned by Width for stereo.
//    KNOB 1: cutoff  2: resonance  3: attack  4: decay  5: LFO depth  6: drive
//  Waveform + the extended/LFO params come from the Propagator panels
//  (g_synthParams). Lighter filter + fewer voices to coexist with the reverb.
// =============================================================================
#pragma once

#include <cmath>

#include "daisysp.h"
#include "modes/mode.h"
#include "config/params.h"
#include "config/synth_params.h"

namespace synthbox {

// ---- shared wavetables: several banks of single-cycle frames, each morphing
// from sine (frame 0) to a richer timbre. Read-only after Init, shared by all
// voices, in fast internal RAM (.bss), generated procedurally (no flash cost).
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

// ---- one polyphonic voice ----
class SynthVoice {
 public:
  void Init(float sr) {
    sr_ = sr;
    osc_[0].Init(sr); osc_[1].Init(sr); sub_.Init(sr);
    sub_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SQUARE);
    flt_.Init(sr);
    amp_.Init(sr);
    fenv_.Init(sr); fenv_.SetMin(0.0f); fenv_.SetMax(1.0f);
    fenv_.SetTime(daisysp::ADENV_SEG_ATTACK, 0.005f);
  }
  void SetWave(int wf) { osc_[0].SetWaveform(wf); osc_[1].SetWaveform(wf); }
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

  // per-block params set by the mode
  float cutoff = 1000.f, detune = 0.006f, subLvl = 0.4f, fenvAmt = 0.5f, pitchMod = 1.f;
  // digital engine (per-block): engine<0.5 = analog, else wavetable
  float engine = 0.f, wtPos = 0.3f, fmAmt = 0.f, fmRatio = 1.f, fold = 0.f, wtBank = 0.f;

  inline float Process() {
    freq_ += (targetFreq_ - freq_) * glideCoef_;   // portamento toward the target pitch
    float f = freq_ * pitchMod;
    float env = amp_.Process(gate_);
    float fe = fenv_.Process();
    float vb = 0.4f + 0.6f * vel_;
    float fc = daisysp::fclamp(cutoff * vb * (1.0f + fenvAmt * 5.0f * fe), 20.0f, 16000.0f);
    flt_.SetFreq(fc);
    sub_.SetFreq(f * 0.5f);
    float sig;
    if (engine < 0.5f) {                            // ---- analog: 2 detuned osc + sub ----
      osc_[0].SetFreq(f * (1.0f - detune));
      osc_[1].SetFreq(f * (1.0f + detune));
      sig = (osc_[0].Process() + osc_[1].Process()) * 0.5f + sub_.Process() * subLvl;
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
    flt_.Process(sig * 0.6f);
    return flt_.Low() * env * vel_;
  }

  daisysp::Adsr amp_;
  daisysp::Svf  flt_;

 private:
  daisysp::Oscillator osc_[2], sub_;
  daisysp::AdEnv      fenv_;
  float note_ = 0.f, freq_ = 220.f, targetFreq_ = 220.f, vel_ = 0.8f;
  float glideCoef_ = 1.0f;
  float sr_ = 48000.f;
  float wtPhase_ = 0.f, fmPhase_ = 0.f;   // wavetable carrier + FM modulator phases
  bool  gate_ = false;
};

// ---- polyphonic mode ----
class SynthMode : public IMode {
 public:
  static constexpr int kVoices = 6;   // max pool; active count is runtime (SP_VOICES)

  void Init(float sample_rate, Hothouse& /*hw*/) override {
    sr_ = sample_rate;
    GenerateWavetables();   // fill all shared wavetable banks (once)
    for (int i = 0; i < kVoices; ++i) v_[i].Init(sample_rate);
    lfo_.Init(sample_rate / params::audio::kBlockSize);  // LFO ticked once per block
  }

  void Control(Hothouse& hw, ModContext& ctx) override {
    using namespace params::synth;
    const SynthParams& p = g_synthParams;

    static const int waves[4] = {daisysp::Oscillator::WAVE_SIN,
                                 daisysp::Oscillator::WAVE_POLYBLEP_TRI,
                                 daisysp::Oscillator::WAVE_POLYBLEP_SAW,
                                 daisysp::Oscillator::WAVE_POLYBLEP_SQUARE};
    int wf = waves[clampi(static_cast<int>(p.v[SP_WAVE] * 3.99f), 0, 3)];

    float cutoff = daisysp::fmap(ctx.knob[Hothouse::KNOB_1], kCutoffMinHz, kCutoffMaxHz, daisysp::Mapping::EXP);
    float res = daisysp::fmap(ctx.knob[Hothouse::KNOB_2], 0.0f, 0.85f, daisysp::Mapping::LINEAR);
    float atk = daisysp::fmap(ctx.knob[Hothouse::KNOB_3], kAttackMinS, kAttackMaxS, daisysp::Mapping::EXP);
    float dec = daisysp::fmap(ctx.knob[Hothouse::KNOB_4], kDecayMinS, kDecayMaxS, daisysp::Mapping::EXP);
    float lfoKnob = ctx.knob[Hothouse::KNOB_5];
    drive_ = daisysp::fmap(ctx.knob[Hothouse::KNOB_6], 1.0f, 1.8f, daisysp::Mapping::EXP);

    float sus = p.v[SP_SUSTAIN];
    float rel = daisysp::fmap(p.v[SP_RELEASE], 0.02f, 3.0f, daisysp::Mapping::EXP);
    float fenvTime = daisysp::fmap(p.v[SP_FENV_TIME], 0.03f, 2.5f, daisysp::Mapping::EXP);
    float detune = p.v[SP_DETUNE] * 0.012f;
    float subLvl = p.v[SP_SUB];
    float fenvAmt = p.v[SP_FENV_AMT];
    spread_ = p.v[SP_SPREAD];
    voices_ = clampi(1 + static_cast<int>(p.v[SP_VOICES] * 5.0f + 0.5f), 1, kVoices);

    // ---- digital / wavetable engine params ----
    float engine = p.v[SP_ENGINE];
    float wtPos  = p.v[SP_WT_POS];
    float fmAmt  = p.v[SP_FM_AMT];
    static const float ratios[4] = {0.5f, 1.0f, 2.0f, 3.0f};
    float fmRatio = ratios[clampi(static_cast<int>(p.v[SP_FM_RATIO] * 3.99f), 0, 3)];
    float fold = p.v[SP_FOLD];
    float wtBank = (float)clampi(static_cast<int>(p.v[SP_WT_BANK] * (kWtBanks - 1) + 0.5f), 0, kWtBanks - 1);
    // Glide: SP_GLIDE -> portamento time (0..0.4s). Per-sample one-pole coefficient
    // toward the target pitch; ~0 time => coef 1 (instant).
    float gtime = daisysp::fmap(p.v[SP_GLIDE], 0.0f, 0.4f, daisysp::Mapping::EXP);
    float gcoef = (gtime <= 5e-4f) ? 1.0f : 1.0f - expf(-1.0f / (gtime * sr_));

    // ---- global LFO ----
    static const int lshapes[4] = {daisysp::Oscillator::WAVE_SIN,
                                   daisysp::Oscillator::WAVE_TRI,
                                   daisysp::Oscillator::WAVE_SAW,
                                   daisysp::Oscillator::WAVE_SQUARE};
    lfo_.SetWaveform(lshapes[clampi(static_cast<int>(p.v[SP_LFO_SHAPE] * 3.99f), 0, 3)]);
    lfo_.SetFreq(daisysp::fmap(p.v[SP_LFO_RATE], 0.05f, 18.0f, daisysp::Mapping::EXP));
    float lfo = lfo_.Process();
    float depth = daisysp::fclamp(p.v[SP_LFO_DEPTH] + lfoKnob, 0.0f, 1.0f);
    int dest = clampi(static_cast<int>(p.v[SP_LFO_DEST] * 3.99f), 0, 3);  // off/vibrato/filter/tremolo
    float pitchMul = (dest == 1) ? (1.0f + lfo * depth * 0.06f) : 1.0f;
    float cutMul   = (dest == 2) ? (1.0f + lfo * depth * 0.9f) : 1.0f;
    trem_          = (dest == 3) ? (1.0f - depth * 0.5f * (0.5f - 0.5f * lfo)) : 1.0f;

    for (int i = 0; i < kVoices; ++i) {
      v_[i].SetWave(wf);
      v_[i].SetGlide(gcoef);
      v_[i].SetFenvDecay(fenvTime);
      v_[i].amp_.SetAttackTime(atk);
      v_[i].amp_.SetDecayTime(dec);
      v_[i].amp_.SetSustainLevel(sus);
      v_[i].amp_.SetReleaseTime(rel);
      v_[i].flt_.SetRes(res);
      v_[i].cutoff = cutoff * cutMul;
      v_[i].detune = detune;
      v_[i].subLvl = subLvl;
      v_[i].fenvAmt = fenvAmt;
      v_[i].pitchMod = pitchMul;
      v_[i].engine = engine;
      v_[i].wtPos = wtPos;
      v_[i].fmAmt = fmAmt;
      v_[i].fmRatio = fmRatio;
      v_[i].fold = fold;
      v_[i].wtBank = wtBank;
      float pos = (voices_ > 1 ? (float)i / (voices_ - 1) - 0.5f : 0.0f) * 2.0f * spread_;
      panL_[i] = 0.5f * (1.0f - pos);
      panR_[i] = 0.5f * (1.0f + pos);
    }
  }

  void ProcessBlock(AudioHandle::InputBuffer /*in*/,
                    AudioHandle::OutputBuffer out, size_t size) override {
    for (size_t n = 0; n < size; ++n) {
      float l = 0.0f, r = 0.0f;
      for (int i = 0; i < voices_; ++i) {
        if (!v_[i].Active()) continue;
        float s = v_[i].Process() * drive_;
        l += s * panL_[i];
        r += s * panR_[i];
      }
      out[0][n] = l * 0.4f * trem_;
      out[1][n] = r * 0.4f * trem_;
    }
  }

  void NoteOn(float note, float velocity) override {
    for (int i = 0; i < voices_; ++i)
      if (v_[i].Gate() && v_[i].Note() == note) { v_[i].NoteOn(note, velocity); return; }
    for (int i = 0; i < voices_; ++i)
      if (!v_[i].Active()) { v_[i].NoteOn(note, velocity); return; }
    for (int i = 0; i < voices_; ++i)
      if (!v_[i].Gate()) { v_[i].NoteOn(note, velocity); return; }
    if (steal_ >= voices_) steal_ = 0;
    v_[steal_].NoteOn(note, velocity);
    steal_ = (steal_ + 1) % voices_;
  }
  void NoteOff(float note) override {
    for (int i = 0; i < voices_; ++i)
      if (v_[i].Gate() && v_[i].Note() == note) { v_[i].NoteOff(); break; }
  }

 private:
  static int clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }

  SynthVoice v_[kVoices];
  daisysp::Oscillator lfo_;
  float panL_[kVoices] = {0}, panR_[kVoices] = {0};
  float sr_ = 48000.0f;
  float drive_ = 1.0f, spread_ = 0.6f, trem_ = 1.0f;
  int   voices_ = 4;
  int   steal_ = 0;
};

}  // namespace synthbox
