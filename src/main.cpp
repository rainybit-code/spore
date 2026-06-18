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
#include "mod/modulation.h"
#include "io/controls.h"
#include "io/sensors.h"
#include "io/knobs.h"
#include "io/midi_in.h"
#include "fx/effects.h"
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
MidiClock       g_clock;             // local tempo, locks to incoming MIDI clock
int             g_delaySync = 0;     // delay tempo-sync division (0 = free)
daisy::CpuLoadMeter g_cpu;           // audio-callback load; reported over SysEx (cmd 0x02)

SynthMode       synth_mode;
GranularMode    granular_mode;
GenerativeMode  generative_mode;
IMode*          g_modes[MODE_COUNT];

volatile int    g_active = MODE_SYNTH;
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

// extended synth params (set over MIDI CC 40+ from the Propagator synth panel)
synthbox::SynthParams synthbox::g_synthParams;

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
  int tog_mode = CurrentMode(hw);
  int tog_fx   = TogglePos(hw, Hothouse::TOGGLESWITCH_3);
  if (tog_mode != last_tog_mode) { last_tog_mode = tog_mode; g_modeSel = -1; }
  if (tog_fx   != last_tog_fx)   { last_tog_fx = tog_fx;     g_fxSel = -1; }

  // Mode: forced select (web CC 16 / moved toggle) wins, else follow TOGGLE 1.
  int sel = kBenchForceSynth ? MODE_SYNTH
                             : (g_modeSel >= 0 ? g_modeSel : tog_mode);
  if (sel != g_active) {
    g_active = sel;
    g_modes[g_active]->OnEnter();
  }

  // FX: forced select (web CC 17 / moved toggle) wins, else follow TOGGLE 3.
  int fxsel = g_fxSel >= 0 ? g_fxSel : tog_fx;
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

  // Mode params from the (latched) MODE layer.
  IMode*     m = g_modes[g_active];
  ModContext ctx{g_mod, g_sensors, g_shift.Values(ShiftKnobs::MODE), g_clock.Bpm()};
  m->Control(hw, ctx);

  if (g_bypass) {
    for (size_t i = 0; i < size; ++i) {
      out[0][i] = in[0][i];
      out[1][i] = in[1][i];
    }
  } else {
    m->ProcessBlock(in, out, size);
    g_fx.Process(out[0], out[1], size);  // decoupled global FX on the active mode
    // Global safety limiter: hard-clamp to [-1, 1] so a resonant filter, stacked
    // grains, or runaway delay feedback can never blast the output.
    for (size_t i = 0; i < size; ++i) {
      out[0][i] = out[0][i] > 1.0f ? 1.0f : (out[0][i] < -1.0f ? -1.0f : out[0][i]);
      out[1][i] = out[1][i] > 1.0f ? 1.0f : (out[1][i] < -1.0f ? -1.0f : out[1][i]);
    }
  }
  g_cpu.OnBlockEnd();
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
    if (PumpMidi(midi, g_modes[g_active], g_shift, g_modeSel, g_fxSel, g_clock, g_delaySync, g_cpu)) g_last_midi_ms = System::GetNow();
    g_clock.Update(System::GetNow());   // drop back to internal tempo if clock stops
    // Onboard LED: ~1 Hz heartbeat = app is alive; goes solid while MIDI arrives.
    {
      uint32_t now = System::GetNow();
      bool heartbeat = ((now / 500) % 2) == 0;
      bool midi_active = (now - g_last_midi_ms) < 100;
      hw.seed.SetLed(heartbeat || midi_active);
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
