// =============================================================================
//  generative_mode.h  --  MODE 3: self-playing "Krell" voice.
// =============================================================================
//  No input needed -- it plays itself. A bounded random walk picks each new
//  pitch; attack/decay are randomized per event; the event rate, pitch range,
//  tone and decay are set by knobs; a random LFO keeps the filter moving.
//  Motion/analog sensors nudge the system (see Control()). FOOTSWITCH 2 re-seeds.
//
//    KNOB 1: event rate   KNOB 2: pitch range   KNOB 3: tone   KNOB 4: decay
//    KNOB 5: randomness    KNOB 6: LFO mod depth
//    TOGGLE 2: scale (UP chromatic / MID minor-pentatonic / DOWN major)
// =============================================================================
#pragma once

#include "daisysp.h"
#include "daisysp-lgpl.h"  // MoogLadder
#include "modes/mode.h"
#include "config/params.h"

namespace synthbox {

class GenerativeMode : public IMode {
 public:
  void Init(float sample_rate, Hothouse& /*hw*/) override {
    sr_ = sample_rate;
    osc_.Init(sample_rate);
    osc_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_TRI);
    flt_.Init(sample_rate);
    flt_.SetRes(0.5f);
    env_.Init(sample_rate);
    env_.SetMin(0.0f);
    env_.SetMax(1.0f);
    rng_.Init();
    walk_.Init(&rng_);
    timer_ = 0.0f;
  }

  void Action() override { walk_.Init(&rng_); timer_ = 0.0f; }  // re-seed

  void Control(Hothouse& hw, ModContext& ctx) override {
    using namespace params::generative;
    const float dt = 1.0f / hw.AudioCallbackRate();

    const float k_rate = hw.GetKnobValue(Hothouse::KNOB_1);
    const float k_range = hw.GetKnobValue(Hothouse::KNOB_2);
    const float k_tone = hw.GetKnobValue(Hothouse::KNOB_3);
    const float k_decay = hw.GetKnobValue(Hothouse::KNOB_4);
    const float k_rand = hw.GetKnobValue(Hothouse::KNOB_5);
    const float k_modwet = hw.GetKnobValue(Hothouse::KNOB_6);

    const float rate = kRateMinHz + k_rate * (kRateMaxHz - kRateMinHz);
    const float range = kNoteRangeMin + k_range * (kNoteRangeMax - kNoteRangeMin);
    const float tone = kCutoffMinHz +
                       k_tone * k_tone * (kCutoffMaxHz - kCutoffMinHz);  // ~log

    // Analog sensor gently biases the system: it raises/lowers the pitch center
    // (neutral 0.5 = no bias until hardware is wired).
    const float center = kNoteCenter + (ctx.sensors.Light() - 0.5f) * 12.0f;
    const float randomness = daisysp::fclamp(k_rand, 0.0f, 1.0f);

    // Filter movement from a slow random LFO.
    const float lfo = ctx.mod.Lfo1();
    cutoff_ = tone * (1.0f + k_modwet * 0.8f * lfo);

    // Time for the next event?
    timer_ -= dt;
    if (timer_ <= 0.0f && !env_.IsRunning()) {
      // New pitch via bounded random walk.
      const float step = (kWalkStepSemi / 24.0f) * (0.2f + randomness);
      const float w = walk_.Process(step);           // -1..1
      float note = center + w * range;
      note = QuantizeToScale(note, TogglePos(hw, Hothouse::TOGGLESWITCH_2));
      note = daisysp::fclamp(note, 24.0f, 96.0f);  // keep pitch musical/audible
      osc_.SetFreq(daisysp::mtof(note));

      // Randomized envelope shape.
      const float atk = kAttackMinS +
                        rng_.Unipolar() * randomness * (kAttackMaxS - kAttackMinS);
      const float dec = kDecayMinS +
                        (k_decay * (0.5f + 0.5f * rng_.Unipolar())) *
                            (kDecayMaxS - kDecayMinS);
      env_.SetTime(daisysp::ADENV_SEG_ATTACK, atk);
      env_.SetTime(daisysp::ADENV_SEG_DECAY, dec);
      env_.Trigger();

      // Schedule next event with timing jitter.
      const float jitter = 1.0f + rng_.Bipolar() * randomness * 0.8f;
      timer_ = (1.0f / rate) * daisysp::fclamp(jitter, 0.1f, 3.0f);
    }
  }

  void ProcessBlock(AudioHandle::InputBuffer /*in*/,
                    AudioHandle::OutputBuffer out, size_t size) override {
    for (size_t i = 0; i < size; ++i) {
      float env_out = env_.Process();
      float fc = daisysp::fclamp(cutoff_ * (0.2f + 0.8f * env_out), 20.0f,
                                 16000.0f);
      flt_.SetFreq(fc);
      float s = flt_.Process(osc_.Process()) * env_out * 0.7f;
      out[0][i] = out[1][i] = s;
    }
  }

 private:
  // Snap a MIDI note to a scale. mode: 0 chromatic, 1 minor pentatonic, 2 major.
  static float QuantizeToScale(float note, int scale) {
    if (scale == 0) return note;  // chromatic
    static const int kPenta[5] = {0, 3, 5, 7, 10};
    static const int kMajor[7] = {0, 2, 4, 5, 7, 9, 11};
    const int* set = (scale == 1) ? kPenta : kMajor;
    const int  n = (scale == 1) ? 5 : 7;
    int   midi = static_cast<int>(note + 0.5f);
    int   octave = midi / 12;
    int   pc = midi % 12;
    int   best = set[0];
    int   bestd = 99;
    for (int i = 0; i < n; ++i) {
      int d = pc - set[i];
      if (d < 0) d = -d;
      if (d < bestd) { bestd = d; best = set[i]; }
    }
    return static_cast<float>(octave * 12 + best);
  }

  daisysp::Oscillator osc_;
  daisysp::MoogLadder flt_;
  daisysp::AdEnv      env_;
  Rng                 rng_;
  RandomWalk          walk_;
  float sr_ = 48000.0f;
  float timer_ = 0.0f;
  float cutoff_ = 1000.0f;
};

}  // namespace synthbox
