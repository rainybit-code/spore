// =============================================================================
//  synth_mode.h  --  MODE 1: playable synth voice (Phase 1: fat analog mono).
// =============================================================================
//  3 detuned oscillators + sub-osc -> stereo Moog ladder filters -> ADSR, with a
//  dedicated filter envelope, glide, and velocity (loudness + brightness).
//    KNOB 1: cutoff   KNOB 2: resonance   KNOB 3: attack   KNOB 4: decay
//    KNOB 5: LFO mod depth   KNOB 6: drive
//    TOGGLE 2: waveform (UP sine / MID square / DOWN saw)
//  Extended params (detune, sub, sustain, release, filter-env amt/time, glide,
//  width) come from the Propagator synth panel over MIDI -> g_synthParams.
// =============================================================================
#pragma once

#include "daisysp.h"
#include "daisysp-lgpl.h"  // MoogLadder
#include "modes/mode.h"
#include "config/params.h"
#include "config/synth_params.h"

namespace synthbox {

class SynthMode : public IMode {
 public:
  void Init(float sample_rate, Hothouse& /*hw*/) override {
    sr_ = sample_rate;
    for (int i = 0; i < 3; ++i) {
      osc_[i].Init(sample_rate);
      osc_[i].SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
    }
    sub_.Init(sample_rate);
    sub_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SQUARE);
    fltL_.Init(sample_rate);
    fltR_.Init(sample_rate);
    ampEnv_.Init(sample_rate);
    fenv_.Init(sample_rate);
    fenv_.SetMin(0.0f);
    fenv_.SetMax(1.0f);
    fenv_.SetTime(daisysp::ADENV_SEG_ATTACK, 0.005f);
    port_.Init(sample_rate, 0.02f);
  }

  void Control(Hothouse& hw, ModContext& ctx) override {
    using namespace params::synth;
    const SynthParams& p = g_synthParams;

    static const int waves[3] = {daisysp::Oscillator::WAVE_SIN,
                                 daisysp::Oscillator::WAVE_POLYBLEP_SQUARE,
                                 daisysp::Oscillator::WAVE_POLYBLEP_SAW};
    int wf = waves[TogglePos(hw, Hothouse::TOGGLESWITCH_2)];
    for (int i = 0; i < 3; ++i) osc_[i].SetWaveform(wf);

    cutoff_ = daisysp::fmap(ctx.knob[Hothouse::KNOB_1], kCutoffMinHz, kCutoffMaxHz,
                            daisysp::Mapping::EXP);
    res_ = daisysp::fmap(ctx.knob[Hothouse::KNOB_2], kResMin, kResMax,
                         daisysp::Mapping::LINEAR);
    ampEnv_.SetAttackTime(daisysp::fmap(ctx.knob[Hothouse::KNOB_3], kAttackMinS,
                                        kAttackMaxS, daisysp::Mapping::EXP));
    ampEnv_.SetDecayTime(daisysp::fmap(ctx.knob[Hothouse::KNOB_4], kDecayMinS,
                                       kDecayMaxS, daisysp::Mapping::EXP));
    modDepth_ = ctx.knob[Hothouse::KNOB_5] * kModDepthMax;
    drive_ = daisysp::fmap(ctx.knob[Hothouse::KNOB_6], 1.0f, 2.2f,
                           daisysp::Mapping::EXP);

    // Extended params from the synth panel (normalized 0..1).
    ampEnv_.SetSustainLevel(p.v[SP_SUSTAIN]);
    ampEnv_.SetReleaseTime(daisysp::fmap(p.v[SP_RELEASE], 0.02f, 3.0f, daisysp::Mapping::EXP));
    fenv_.SetTime(daisysp::ADENV_SEG_DECAY,
                  daisysp::fmap(p.v[SP_FENV_TIME], 0.03f, 2.5f, daisysp::Mapping::EXP));
    port_.SetHtime(daisysp::fmap(p.v[SP_GLIDE], 0.0f, 0.4f, daisysp::Mapping::LINEAR));
    detune_ = p.v[SP_DETUNE] * 0.012f;   // up to ~1.2% (~20 cents) spread
    subLevel_ = p.v[SP_SUB];
    fenvAmt_ = p.v[SP_FENV_AMT];
    spread_ = p.v[SP_SPREAD];

    fltL_.SetRes(res_);
    fltR_.SetRes(res_);

    float lfo = ctx.mod.Lfo1();
    float sensor = (ctx.sensors.Value(0) - 0.5f) * 2.0f;  // neutral until wired
    modCut_ = 1.0f + modDepth_ * lfo + 0.3f * modDepth_ * sensor;
    velBright_ = 0.35f + 0.65f * amp_;
  }

  void ProcessBlock(AudioHandle::InputBuffer /*in*/,
                    AudioHandle::OutputBuffer out, size_t size) override {
    const float wHi = 0.5f + spread_ * 0.5f, wLo = 0.5f - spread_ * 0.5f;
    for (size_t i = 0; i < size; ++i) {
      float base = port_.Process(targetFreq_);
      osc_[0].SetFreq(base * (1.0f - detune_));
      osc_[1].SetFreq(base);
      osc_[2].SetFreq(base * (1.0f + detune_));
      sub_.SetFreq(base * 0.5f);

      float env = ampEnv_.Process(gate_);
      float fe = fenv_.Process();
      float fc = daisysp::fclamp(
          cutoff_ * velBright_ * modCut_ * (1.0f + fenvAmt_ * 6.0f * fe), 20.0f,
          18000.0f);
      fltL_.SetFreq(fc);
      fltR_.SetFreq(fc);

      float s0 = osc_[0].Process(), s1 = osc_[1].Process(), s2 = osc_[2].Process();
      float ss = sub_.Process() * subLevel_;
      float center = s1 * 0.5f + ss;
      float l = (center + s0 * wHi + s2 * wLo) * 0.33f * drive_;
      float r = (center + s2 * wHi + s0 * wLo) * 0.33f * drive_;
      out[0][i] = fltL_.Process(l) * env * amp_ * 0.6f;
      out[1][i] = fltR_.Process(r) * env * amp_ * 0.6f;
    }
  }

  void NoteOn(float note, float velocity) override {
    targetFreq_ = daisysp::mtof(note);
    if (held_ == 0) { gate_ = true; fenv_.Trigger(); }  // retrigger only on first key
    held_++;
    amp_ = velocity;
  }
  void NoteOff(float /*note*/) override {
    if (held_ > 0) held_--;
    if (held_ == 0) gate_ = false;  // release when the last key lifts
  }

 private:
  daisysp::Oscillator osc_[3], sub_;
  daisysp::MoogLadder fltL_, fltR_;
  daisysp::Adsr       ampEnv_;
  daisysp::AdEnv      fenv_;
  daisysp::Port       port_;
  float sr_ = 48000.0f;
  float targetFreq_ = 220.0f, amp_ = 0.8f;
  int   held_ = 0;
  bool  gate_ = false;
  float cutoff_ = 1000.0f, res_ = 0.4f, modDepth_ = 0.0f, drive_ = 1.0f;
  float detune_ = 0.005f, subLevel_ = 0.4f, fenvAmt_ = 0.5f, spread_ = 0.6f;
  float modCut_ = 1.0f, velBright_ = 1.0f;
};

}  // namespace synthbox
