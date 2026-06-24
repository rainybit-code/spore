// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  main.cpp  --  Versatile generative synth / FX for Daisy Seed + Hothouse.
// =============================================================================
//  Wiring only: pick the active mode from TOGGLE 1, read controls + modulation
//  + sensors at block rate, dispatch USB MIDI, and run the active mode's audio.
//  All DSP lives in modes/, modulation in mod/, hardware I/O in io/, and every
//  tunable number in config/params.h.
//
//  CONTROL SURFACE (see config/params.h for the full per-mode map):
//    TOGGLE 1 : MODE  (UP=Synth  MIDDLE=Granular  DOWN=Generative)  [3-pos switch]
//    TOGGLE 3 : FX    (UP=off    MIDDLE=delay      DOWN=reverb)
//    FOOTSW 1 : tap = engage/bypass | HOLD = edit FX (knobs -> FX, soft-takeover)
//    FOOTSW 2 : mode action (freeze / re-seed)
//    Hold BOTH footswitches 2s -> reboot to DFU for flashing.
// =============================================================================
#include "daisy_seed.h"
#include "hothouse.h"
#include "util/CpuLoadMeter.h"

#include "config/params.h"
#include "config/gran_params.h"
#include "mod/modulation.h"
#include "io/controls.h"
#include "io/sensors.h"
#include "io/knobs.h"
#include "io/midi_in.h"
#include "fx/effects.h"
#include "fx/master.h"
#include "modes/mode.h"
#include "modes/synth_mode.h"
#include "modes/granular_mode.h"
#include "modes/generative_mode.h"

using namespace daisy;
using namespace synthbox;
using clevelandmusicco::Hothouse;

Hothouse        hw;
MidiUsbHandler  midi;
ModEngine       g_mod;
AnalogSensors   g_sensors;
ShiftKnobs      g_shift;
GlobalFx        DSY_SDRAM_BSS g_fx;  // ReverbSc ~400KB -> must live in SDRAM
MasterChain     g_master;            // master filter (LP/BP/HP) + volume on the final mix
MidiClock       g_clock;             // local tempo, locks to incoming MIDI clock
int             g_delaySync = 0;     // delay tempo-sync division (0 = free)
daisy::CpuLoadMeter g_cpu;           // audio-callback load; reported over SysEx (cmd 0x02)

SynthMode       synth_mode;
GranularMode    granular_mode;
GenerativeMode  generative_mode;
IMode*          g_modes[MODE_COUNT];

volatile int    g_active = MODE_SYNTH;
volatile bool   g_overload = false;       // CPU watchdog: sustained-overload -> shed load
bool            g_bypass = false;
bool            g_fx_edit_latch = false;  // distinguishes FS1 tap vs hold-to-edit
uint32_t        g_last_midi_ms = 0;       // for the onboard MIDI-activity LED

// TEMP bare-Seed bench: with no Hothouse, the mode toggle floats to "middle".
// Leave false now that mode is selectable over MIDI (CC 16) from the web tool.
static constexpr bool kBenchForceSynth = false;

// Mode + FX select. -1 = follow the physical toggle; >=0 = forced (web CC or a
// toggle that has been moved). Boot into a KNOWN-QUIET state (Synth + FX off)
// rather than self-playing whatever a floating/parked toggle happens to read;
// the physical toggles take over the instant they're actually moved (below).
int g_modeSel = MODE_SYNTH;
int g_fxSel = 0;  // GlobalFx::OFF
int g_varSel = -1;  // TOGGLE 2 variant: -1 = follow toggle, >=0 = forced (web CC 93)

// extended synth params (set over MIDI CC 40+ from the Propagator synth panel)
synthbox::SynthParams synthbox::g_synthParams;
// generative steering params (set over MIDI CC 32+ from the Propagator GENERATIVE pod)
synthbox::GenParams synthbox::g_genParams;
synthbox::GranParams synthbox::g_granParams;

Led led1, led2;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  g_cpu.OnBlockStart();
  hw.ProcessAllControls();
  g_sensors.Process();
  g_mod.Process();

  // Hand control to a physical toggle the moment it's MOVED from its boot
  // position (so Spore boots quiet but the hardware switches still work).
  static int last_tog_mode = CurrentMode(hw);
  static int last_tog_fx   = TogglePos(hw, Hothouse::TOGGLESWITCH_3);
  static int last_tog_var  = TogglePos(hw, Hothouse::TOGGLESWITCH_2);
  int tog_mode = CurrentMode(hw);
  int tog_fx   = TogglePos(hw, Hothouse::TOGGLESWITCH_3);
  int tog_var  = TogglePos(hw, Hothouse::TOGGLESWITCH_2);
  if (tog_mode != last_tog_mode) { last_tog_mode = tog_mode; g_modeSel = -1; }
  if (tog_fx   != last_tog_fx)   { last_tog_fx = tog_fx;     g_fxSel = -1; }
  if (tog_var  != last_tog_var)  { last_tog_var = tog_var;   g_varSel = -1; }

  // Mode: forced select (web CC 16 / moved toggle) wins, else follow TOGGLE 1.
  int sel = kBenchForceSynth ? MODE_SYNTH
                             : (g_modeSel >= 0 ? g_modeSel : tog_mode);
  if (sel != g_active) {
    g_active = sel;
    g_modes[g_active]->OnEnter();   // new mode clears its own voices/grains/reverb
    g_fx.Reset();                   // clear the global FX tail so nothing lingers across
    g_overload = false;             // a deliberate mode change = the user regaining control
  }

  // FX: forced select (web CC 17 / moved toggle) wins, else follow TOGGLE 3.
  int fxsel = g_fxSel >= 0 ? g_fxSel : tog_fx;
  static int last_fxsel = -1;
  if (fxsel != last_fxsel) { last_fxsel = fxsel; g_overload = false; }   // FX change = regain control
  g_fx.SetMode(static_cast<GlobalFx::Mode>(fxsel));
  g_fx.SetTempo(g_clock.Bpm());   // tempo-synced delay follows the local clock
  g_fx.SetSync(g_delaySync);

  // FOOTSWITCH 1: hold = edit FX (knobs -> FX layer); quick tap = bypass toggle.
  bool fx_editing = hw.switches[Hothouse::FOOTSWITCH_1].Pressed() &&
                    hw.switches[Hothouse::FOOTSWITCH_1].TimeHeldMs() >
                        params::fx::kEditHoldMs;
  if (fx_editing) g_fx_edit_latch = true;
  if (hw.switches[Hothouse::FOOTSWITCH_1].FallingEdge()) {
    if (!g_fx_edit_latch) g_bypass = !g_bypass;  // it was a tap
    g_fx_edit_latch = false;
  }

  // FOOTSWITCH 2: mode-specific action.
  if (Footswitch2Pressed(hw)) g_modes[g_active]->Action();

  // Shift-layer: route knobs to FX while editing, to the mode otherwise.
  g_shift.SetLayer(fx_editing ? ShiftKnobs::FX : ShiftKnobs::MODE);
  g_shift.Update(hw);

  // FX params from the (latched) FX layer.
  const float* fk = g_shift.Values(ShiftKnobs::FX);
  g_fx.SetParams(fk[Hothouse::KNOB_1], fk[Hothouse::KNOB_2], fk[Hothouse::KNOB_3],
                 fk[Hothouse::KNOB_4], fk[Hothouse::KNOB_5], fk[Hothouse::KNOB_6]);

  // Mode params from the (latched) MODE layer. VAR = forced (web CC 93) or TOGGLE 2.
  int variant = g_varSel >= 0 ? g_varSel : tog_var;
  IMode*     m = g_modes[g_active];
  ModContext ctx{g_mod, g_sensors, g_shift.Values(ShiftKnobs::MODE), g_clock.Bpm(), variant, g_overload};
  m->Control(hw, ctx);

  if (g_bypass) {
    for (size_t i = 0; i < size; ++i) {
      out[0][i] = in[0][i];
      out[1][i] = in[1][i];
    }
  } else {
    m->ProcessBlock(in, out, size);
    // Global FX runs for Synth/Granular only: Generative has its own reverb (running both
    // = double ReverbSc = overload), and the CPU watchdog sheds it under sustained load.
    if (g_active != MODE_GENERATIVE && !g_overload)
      g_fx.Process(out[0], out[1], size);
    g_master.Process(out[0], out[1], size);  // master filter (LP/BP/HP) + volume + limiter
    // Final backstop behind the master limiter: catch the sub-millisecond attack
    // slip so the output can never leave [-1, 1], whatever the limiter is doing.
    for (size_t i = 0; i < size; ++i) {
      out[0][i] = out[0][i] > 1.0f ? 1.0f : (out[0][i] < -1.0f ? -1.0f : out[0][i]);
      out[1][i] = out[1][i] > 1.0f ? 1.0f : (out[1][i] < -1.0f ? -1.0f : out[1][i]);
    }
  }
  g_cpu.OnBlockEnd();

  // CPU watchdog: if the callback stays overloaded for ~150 ms, latch g_overload so we shed
  // the global FX (above) and fast-blink the LED. Cleared by a mode/FX change (regain control).
  {
    static int over = 0, under = 0;
    const float load = g_cpu.GetAvgCpuLoad();
    if (load > 0.95f)      { under = 0; if (++over > 150) g_overload = true; }   // ~150 ms hot -> shed
    else if (load < 0.78f) { over = 0; if (++under > 400) g_overload = false; }  // ~400 ms cool -> recover
    else                   { over = 0; under = 0; }
  }
}

// Send a CC out (channel 1) to mirror our state to the editor.
static inline void TxCc(MidiUsbHandler& m, int cc, float v01) {
  int v = static_cast<int>(v01 * 127.0f + 0.5f);
  uint8_t msg[3] = {0xB0, static_cast<uint8_t>(cc),
                    static_cast<uint8_t>(v < 0 ? 0 : (v > 127 ? 127 : v))};
  m.SendMessage(msg, 3);
}

// Device -> editor mirror: when a hardware control (mode/FX/VAR toggle, the 12
// shift-layer knobs, or bypass) changes, transmit the matching CC so Propagator
// follows the physical surface. Change-detected; runs in the main loop (not the ISR).
static void EchoControls(MidiUsbHandler& m, Hothouse& hw) {
  using namespace params::midi;
  static int   lMode = -1, lFx = -1, lVar = -1, lByp = -1;
  static float lMK[ShiftKnobs::kKnobs] = {-1, -1, -1, -1, -1, -1};
  static float lFK[ShiftKnobs::kKnobs] = {-1, -1, -1, -1, -1, -1};

  int mode = g_active;
  int fx   = static_cast<int>(g_fx.GetMode());
  int var  = (g_varSel >= 0) ? g_varSel : TogglePos(hw, Hothouse::TOGGLESWITCH_2);
  int byp  = g_bypass ? 1 : 0;
  if (mode != lMode) { lMode = mode; TxCc(m, kCcModeSelect, mode / 2.0f); }
  if (fx   != lFx)   { lFx = fx;     TxCc(m, kCcFxSelect,   fx   / 2.0f); }
  if (var  != lVar)  { lVar = var;   TxCc(m, kCcVar,        var  / 2.0f); }
  if (byp  != lByp)  { lByp = byp;   TxCc(m, kCcFootsw1,    byp ? 1.0f : 0.0f); }

  const float* mk = g_shift.Values(ShiftKnobs::MODE);
  const float* fk = g_shift.Values(ShiftKnobs::FX);
  for (int i = 0; i < ShiftKnobs::kKnobs; ++i) {
    float dm = mk[i] - lMK[i]; if (dm < 0) dm = -dm;
    float df = fk[i] - lFK[i]; if (df < 0) df = -df;
    if (dm > 0.008f) { lMK[i] = mk[i]; TxCc(m, kCcModeKnobBase + i, mk[i]); }
    if (df > 0.008f) { lFK[i] = fk[i]; TxCc(m, kCcFxKnobBase + i, fk[i]); }
  }
}

int main() {
  hw.Init();

  // FPU flush-to-zero: the Cortex-M7 handles denormal floats on a slow path, and
  // libDaisy's SystemInit only enables FPU *access* (CPACR), never FZ. A decaying
  // ReverbSc tail (no denormal guard of its own) drifts into denormal range and
  // stalls the FPU -> crackle as the reverb rings out. Set FPSCR.FZ once here so
  // denormals flush to zero for every context, including the audio ISR.
  __set_FPSCR(__get_FPSCR() | (1u << 24));  // FZ = bit 24

  hw.SetAudioBlockSize(params::audio::kBlockSize);
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

  // Bring-up: light the onboard LED the instant the app boots, and start USB MIDI
  // EARLY (before the heavier inits) so it enumerates even if something later misbehaves.
  hw.seed.SetLed(true);
  InitMidi(midi);

  const float sr = hw.AudioSampleRate();
  g_cpu.Init(sr, hw.AudioBlockSize());   // audio-load meter (avg/max), read over SysEx
  g_mod.Init(hw.AudioCallbackRate());

  synth_mode.Init(sr, hw);
  granular_mode.Init(sr, hw);
  generative_mode.Init(sr, hw);
  g_modes[MODE_SYNTH]      = &synth_mode;
  g_modes[MODE_GRANULAR]   = &granular_mode;
  g_modes[MODE_GENERATIVE] = &generative_mode;

  // Global FX (in SDRAM) + shift-layer with sensible FX starting points:
  //   mix .30 | time .40 | feedback .35 | tone .70 | rev decay .60 | rev damp .70
  g_fx.Init(sr);
  g_master.Init(sr);   // master filter + volume (filter OFF, volume unity by default)
  g_clock.Init();
  const float fx_defaults[ShiftKnobs::kKnobs] = {0.30f, 0.40f, 0.35f,
                                                 0.70f, 0.60f, 0.70f};
  g_shift.Init(fx_defaults);

  // Non-standard input: analog sensor on a free ADC pin.
  // (Motion IMU is tier-2 / deferred -- see io/imu.h.)
  g_sensors.Init(hw);       // re-inits ADC to add analog input on A0/D15

  // Footswitch LEDs (pins 22/23 are free outside the DFU flash gesture).
  led1.Init(hw.seed.GetPin(Hothouse::LED_1), false);
  led2.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  while (true) {
    if (PumpMidi(midi, g_modes[g_active], g_shift, g_modeSel, g_fxSel, g_clock, g_delaySync, g_cpu, g_mod, g_master, g_bypass, g_varSel)) g_last_midi_ms = System::GetNow();
    EchoControls(midi, hw);             // mirror physical control changes back to the editor
    g_clock.Update(System::GetNow());   // drop back to internal tempo if clock stops
    // Onboard LED: ~1 Hz heartbeat = app is alive; solid while MIDI arrives; a fast
    // ~5 Hz blink warns that the CPU watchdog tripped (change mode/FX to recover).
    {
      uint32_t now = System::GetNow();
      bool heartbeat = ((now / 500) % 2) == 0;
      bool midi_active = (now - g_last_midi_ms) < 100;
      hw.seed.SetLed(g_overload ? (((now / 100) % 2) == 0) : (heartbeat || midi_active));
    }

    // LED feedback: LED1 = engaged (lit) / bypassed (off), mid while editing FX;
    //               LED2 brightness indicates the active FX (off/delay/reverb).
    led1.Set(g_fx_edit_latch ? 0.5f : (g_bypass ? 0.0f : 1.0f));
    led2.Set(0.15f * static_cast<float>(g_fx.GetMode()) * 2.0f);
    led1.Update();
    led2.Update();

    hw.DelayMs(1);  // poll USB MIDI often so the RX FIFO never backs up (dropped notes)
    hw.CheckResetToBootloader();
  }
  return 0;
}
