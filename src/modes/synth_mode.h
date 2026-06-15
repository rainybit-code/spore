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

#include "daisysp.h"
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

  // per-block params set by the mode
  float cutoff = 1000.f, detune = 0.006f, subLvl = 0.4f, fenvAmt = 0.5f, pitchMod = 1.f;

  inline float Process() {
    float f = freq_ * pitchMod;
    osc_[0].SetFreq(f * (1.0f - detune));
    osc_[1].SetFreq(f * (1.0f + detune));
    sub_.SetFreq(f * 0.5f);
    float env = amp_.Process(gate_);
    float fe = fenv_.Process();
    float vb = 0.4f + 0.6f * vel_;
    float fc = daisysp::fclamp(cutoff * vb * (1.0f + fenvAmt * 5.0f * fe), 20.0f, 16000.0f);
    flt_.SetFreq(fc);
    float sig = (osc_[0].Process() + osc_[1].Process()) * 0.5f + sub_.Process() * subLvl;
    flt_.Process(sig * 0.6f);
    return flt_.Low() * env * vel_;
  }

  daisysp::Adsr amp_;
  daisysp::Svf  flt_;

 private:
  daisysp::Oscillator osc_[2], sub_;
  daisysp::AdEnv      fenv_;
  float note_ = 0.f, freq_ = 220.f, vel_ = 0.8f;
  bool  gate_ = false;
};

// ---- polyphonic mode ----
class SynthMode : public IMode {
 public:
  static constexpr int kVoices = 6;   // max pool; active count is runtime (SP_VOICES)

  void Init(float sample_rate, Hothouse& /*hw*/) override {
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
  float drive_ = 1.0f, spread_ = 0.6f, trem_ = 1.0f;
  int   voices_ = 4;
  int   steal_ = 0;
};

}  // namespace synthbox
