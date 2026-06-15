// =============================================================================
//  synth_mode.h  --  MODE 1: playable synth (Phase 2: polyphonic).
// =============================================================================
//  A pool of voices (note allocation + steal), each: 2 detuned oscillators + sub
//  -> Moog ladder filter -> ADSR, with a dedicated filter envelope. Velocity ->
//  loudness + brightness. Voices are panned by the Width param for a stereo
//  spread. Knobs 1..6: cutoff, resonance, attack, decay, LFO mod depth, drive.
//  Waveform + detune/sub/sustain/release/filter-env/width come from the
//  Propagator synth panel (g_synthParams). (Glide is a mono feature; unused here.)
// =============================================================================
#pragma once

#include "daisysp.h"
#include "daisysp-lgpl.h"  // MoogLadder
#include "modes/mode.h"
#include "config/params.h"
#include "config/synth_params.h"

namespace synthbox {

// ---- one polyphonic voice ----
class SynthVoice {
 public:
  void Init(float sr) {
    osc_[0].Init(sr); osc_[1].Init(sr); sub_.Init(sr);
    sub_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SQUARE);
    flt_.Init(sr);
    amp_.Init(sr);
    fenv_.Init(sr); fenv_.SetMin(0.0f); fenv_.SetMax(1.0f);
    fenv_.SetTime(daisysp::ADENV_SEG_ATTACK, 0.005f);
  }
  void SetWave(int wf) { osc_[0].SetWaveform(wf); osc_[1].SetWaveform(wf); }
  void SetFenvDecay(float t) { fenv_.SetTime(daisysp::ADENV_SEG_DECAY, t); }

  void NoteOn(float note, float vel) {
    note_ = note; freq_ = daisysp::mtof(note); vel_ = vel;
    gate_ = true; fenv_.Trigger();
  }
  void NoteOff() { gate_ = false; }
  bool Active() const { return gate_ || amp_.IsRunning(); }
  bool Gate() const { return gate_; }
  float Note() const { return note_; }

  // per-block params from the mode
  float cutoff = 1000.f, detune = 0.006f, subLvl = 0.4f, fenvAmt = 0.5f;

  inline float Process() {
    osc_[0].SetFreq(freq_ * (1.0f - detune));
    osc_[1].SetFreq(freq_ * (1.0f + detune));
    sub_.SetFreq(freq_ * 0.5f);
    float env = amp_.Process(gate_);
    float fe = fenv_.Process();
    float vb = 0.4f + 0.6f * vel_;
    float fc = daisysp::fclamp(cutoff * vb * (1.0f + fenvAmt * 5.0f * fe), 20.0f, 18000.0f);
    flt_.SetFreq(fc);
    float sig = (osc_[0].Process() + osc_[1].Process()) * 0.5f + sub_.Process() * subLvl;
    return flt_.Process(sig * 0.6f) * env * vel_;
  }

  daisysp::Adsr      amp_;
  daisysp::MoogLadder flt_;

 private:
  daisysp::Oscillator osc_[2], sub_;
  daisysp::AdEnv      fenv_;
  float note_ = 0.f, freq_ = 220.f, vel_ = 0.8f;
  bool  gate_ = false;
};

// ---- polyphonic mode ----
class SynthMode : public IMode {
 public:
  static constexpr int kVoices = 6;

  void Init(float sample_rate, Hothouse& /*hw*/) override {
    for (int i = 0; i < kVoices; ++i) v_[i].Init(sample_rate);
  }

  void Control(Hothouse& hw, ModContext& ctx) override {
    using namespace params::synth;
    const SynthParams& p = g_synthParams;

    static const int waves[4] = {daisysp::Oscillator::WAVE_SIN,
                                 daisysp::Oscillator::WAVE_POLYBLEP_TRI,
                                 daisysp::Oscillator::WAVE_POLYBLEP_SAW,
                                 daisysp::Oscillator::WAVE_POLYBLEP_SQUARE};
    int wi = static_cast<int>(p.v[SP_WAVE] * 3.99f);
    int wf = waves[wi < 0 ? 0 : (wi > 3 ? 3 : wi)];

    float cutoff = daisysp::fmap(ctx.knob[Hothouse::KNOB_1], kCutoffMinHz, kCutoffMaxHz, daisysp::Mapping::EXP);
    float res = daisysp::fmap(ctx.knob[Hothouse::KNOB_2], kResMin, kResMax, daisysp::Mapping::LINEAR);
    float atk = daisysp::fmap(ctx.knob[Hothouse::KNOB_3], kAttackMinS, kAttackMaxS, daisysp::Mapping::EXP);
    float dec = daisysp::fmap(ctx.knob[Hothouse::KNOB_4], kDecayMinS, kDecayMaxS, daisysp::Mapping::EXP);
    float modDepth = ctx.knob[Hothouse::KNOB_5] * kModDepthMax;
    drive_ = daisysp::fmap(ctx.knob[Hothouse::KNOB_6], 1.0f, 2.2f, daisysp::Mapping::EXP);

    float sus = p.v[SP_SUSTAIN];
    float rel = daisysp::fmap(p.v[SP_RELEASE], 0.02f, 3.0f, daisysp::Mapping::EXP);
    float fenvTime = daisysp::fmap(p.v[SP_FENV_TIME], 0.03f, 2.5f, daisysp::Mapping::EXP);
    float detune = p.v[SP_DETUNE] * 0.012f;
    float subLvl = p.v[SP_SUB];
    float fenvAmt = p.v[SP_FENV_AMT];
    spread_ = p.v[SP_SPREAD];

    float lfo = ctx.mod.Lfo1();
    float sensor = (ctx.sensors.Value(0) - 0.5f) * 2.0f;
    float modCut = 1.0f + modDepth * lfo + 0.3f * modDepth * sensor;

    for (int i = 0; i < kVoices; ++i) {
      v_[i].SetWave(wf);
      v_[i].amp_.SetAttackTime(atk);
      v_[i].amp_.SetDecayTime(dec);
      v_[i].amp_.SetSustainLevel(sus);
      v_[i].amp_.SetReleaseTime(rel);
      v_[i].flt_.SetRes(res);
      v_[i].SetFenvDecay(fenvTime);
      v_[i].cutoff = cutoff * modCut;
      v_[i].detune = detune;
      v_[i].subLvl = subLvl;
      v_[i].fenvAmt = fenvAmt;
      // per-voice pan from Width (voices fanned across the stereo field)
      float pos = (kVoices > 1 ? (float)i / (kVoices - 1) - 0.5f : 0.0f) * 2.0f * spread_;
      panL_[i] = 0.5f * (1.0f - pos);
      panR_[i] = 0.5f * (1.0f + pos);
    }
  }

  void ProcessBlock(AudioHandle::InputBuffer /*in*/,
                    AudioHandle::OutputBuffer out, size_t size) override {
    for (size_t n = 0; n < size; ++n) {
      float l = 0.0f, r = 0.0f;
      for (int i = 0; i < kVoices; ++i) {
        if (!v_[i].Active()) continue;
        float s = v_[i].Process() * drive_;
        l += s * panL_[i];
        r += s * panR_[i];
      }
      out[0][n] = l * 0.5f;
      out[1][n] = r * 0.5f;
    }
  }

  void NoteOn(float note, float velocity) override {
    int idx = -1;
    for (int i = 0; i < kVoices; ++i) if (!v_[i].Active()) { idx = i; break; }
    if (idx < 0) { idx = steal_; steal_ = (steal_ + 1) % kVoices; }  // steal round-robin
    v_[idx].NoteOn(note, velocity);
  }
  void NoteOff(float note) override {
    for (int i = 0; i < kVoices; ++i)
      if (v_[i].Gate() && v_[i].Note() == note) { v_[i].NoteOff(); break; }
  }

 private:
  SynthVoice v_[kVoices];
  float panL_[kVoices] = {0}, panR_[kVoices] = {0};
  float drive_ = 1.0f, spread_ = 0.6f;
  int   steal_ = 0;
};

}  // namespace synthbox
