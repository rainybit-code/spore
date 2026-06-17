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
#include "dsp/voice.h"
#include "modes/mode.h"
#include "config/params.h"
#include "config/synth_params.h"

namespace synthbox {

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
    float res = ctx.knob[Hothouse::KNOB_2];   // 0..1; the Voice maps it per filter type
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

    // ---- tone shaping ----
    float drive = p.v[SP_DRIVE];
    float filterType = p.v[SP_FILTER];
    float uni = 1.0f + (float)clampi(static_cast<int>(p.v[SP_UNISON] * 3.0f + 0.5f), 0, 3);
    float subOct = (p.v[SP_SUB_OCT] < 0.5f) ? 0.5f : 0.25f;
    int subWave = (p.v[SP_SUB_WAVE] < 0.5f) ? daisysp::Oscillator::WAVE_POLYBLEP_SQUARE
                                            : daisysp::Oscillator::WAVE_SIN;
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
      v_[i].res = res;
      v_[i].drive = drive;
      v_[i].filterType = filterType;
      v_[i].uni = uni;
      v_[i].subOct = subOct;
      v_[i].SetSubWave(subWave);
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

  Voice v_[kVoices];
  daisysp::Oscillator lfo_;
  float panL_[kVoices] = {0}, panR_[kVoices] = {0};
  float sr_ = 48000.0f;
  float drive_ = 1.0f, spread_ = 0.6f, trem_ = 1.0f;
  int   voices_ = 4;
  int   steal_ = 0;
};

}  // namespace synthbox
