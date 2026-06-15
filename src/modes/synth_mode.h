// =============================================================================
//  synth_mode.h  --  MODE 1: playable MIDI synth voice.
// =============================================================================
//  osc -> Moog ladder filter -> AD envelope. Played over USB MIDI. The filter
//  cutoff tracks the envelope and is swept by a random LFO (depth = KNOB 5),
//  giving even held notes some life. See params::synth for all ranges.
// =============================================================================
#pragma once

#include "daisysp.h"
#include "daisysp-lgpl.h"  // MoogLadder
#include "modes/mode.h"
#include "config/params.h"

namespace synthbox {

class SynthMode : public IMode {
 public:
  void Init(float sample_rate, Hothouse& hw) override {
    osc_.Init(sample_rate);
    osc_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
    flt_.Init(sample_rate);
    env_.Init(sample_rate);
    env_.SetTime(daisysp::ADENV_SEG_ATTACK, 0.01f);
    env_.SetTime(daisysp::ADENV_SEG_DECAY, 0.4f);
    env_.SetMin(0.0f);
    env_.SetMax(1.0f);

    using namespace params::synth;
    p_cutoff_.Init(hw.knobs[Hothouse::KNOB_1], kCutoffMinHz, kCutoffMaxHz,
                   daisy::Parameter::LOGARITHMIC);
    p_res_.Init(hw.knobs[Hothouse::KNOB_2], kResMin, kResMax,
                daisy::Parameter::LINEAR);
    p_attack_.Init(hw.knobs[Hothouse::KNOB_3], kAttackMinS, kAttackMaxS,
                   daisy::Parameter::LOGARITHMIC);
    p_decay_.Init(hw.knobs[Hothouse::KNOB_4], kDecayMinS, kDecayMaxS,
                  daisy::Parameter::LOGARITHMIC);
  }

  void Control(Hothouse& hw, ModContext& ctx) override {
    static const int waves[3] = {daisysp::Oscillator::WAVE_SIN,
                                 daisysp::Oscillator::WAVE_POLYBLEP_SQUARE,
                                 daisysp::Oscillator::WAVE_POLYBLEP_SAW};
    osc_.SetWaveform(waves[TogglePos(hw, Hothouse::TOGGLESWITCH_2)]);

    env_.SetTime(daisysp::ADENV_SEG_ATTACK, p_attack_.Process());
    env_.SetTime(daisysp::ADENV_SEG_DECAY, p_decay_.Process());
    flt_.SetRes(p_res_.Process());

    float base = p_cutoff_.Process();
    float depth = hw.GetKnobValue(Hothouse::KNOB_5) * params::synth::kModDepthMax;
    // Random-LFO sweep + a gentle nudge from the analog sensor (neutral when
    // unwired -- 0.5 maps to no bias).
    float lfo = ctx.mod.Lfo1();
    float sensor = (ctx.sensors.Value(0) - 0.5f) * 2.0f;  // -1..1
    cutoff_ = base * (1.0f + depth * lfo + 0.4f * depth * sensor);
  }

  void ProcessBlock(AudioHandle::InputBuffer /*in*/,
                    AudioHandle::OutputBuffer out, size_t size) override {
    for (size_t i = 0; i < size; ++i) {
      float env_out = env_.Process();
      if (!env_.IsRunning()) note_active_ = false;
      float fc = daisysp::fclamp(cutoff_ * (0.15f + 0.85f * env_out), 20.0f,
                                 18000.0f);
      flt_.SetFreq(fc);
      float s = flt_.Process(osc_.Process()) * env_out * amp_;
      out[0][i] = out[1][i] = s;
    }
  }

  void NoteOn(float note, float velocity) override {
    osc_.SetFreq(daisysp::mtof(note));
    amp_ = velocity;
    env_.Trigger();
    note_active_ = true;
  }
  void NoteOff(float /*note*/) override { /* AD envelope free-runs to silence */ }

 private:
  daisysp::Oscillator osc_;
  daisysp::MoogLadder flt_;
  daisysp::AdEnv      env_;
  daisy::Parameter  p_cutoff_, p_res_, p_attack_, p_decay_;
  float cutoff_ = 1000.0f;
  float amp_ = 0.8f;
  bool  note_active_ = false;
};

}  // namespace synthbox
