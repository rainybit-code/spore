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
//    FOOTSW 1 : engage / bypass        FOOTSW 2 : mode action (freeze / re-seed)
//    Hold BOTH footswitches 2s -> reboot to DFU for flashing.
// =============================================================================
#include "daisy_seed.h"
#include "hothouse.h"

#include "config/params.h"
#include "mod/modulation.h"
#include "io/controls.h"
#include "io/sensors.h"
#include "io/midi_in.h"
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

SynthMode       synth_mode;
GranularMode    granular_mode;
GenerativeMode  generative_mode;
IMode*          g_modes[MODE_COUNT];

volatile int    g_active = MODE_SYNTH;
bool            g_bypass = false;

Led led1, led2;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  hw.ProcessAllControls();
  g_sensors.Process();
  g_mod.Process();

  // Mode select from TOGGLE 1.
  int sel = CurrentMode(hw);
  if (sel != g_active) {
    g_active = sel;
    g_modes[g_active]->OnEnter();
  }

  // Footswitches: 1 = bypass toggle, 2 = mode-specific action.
  if (Footswitch1Pressed(hw)) g_bypass = !g_bypass;
  if (Footswitch2Pressed(hw)) g_modes[g_active]->Action();

  IMode*     m = g_modes[g_active];
  ModContext ctx{g_mod, g_sensors};
  m->Control(hw, ctx);

  if (g_bypass) {
    for (size_t i = 0; i < size; ++i) {
      out[0][i] = in[0][i];
      out[1][i] = in[1][i];
    }
  } else {
    m->ProcessBlock(in, out, size);
    // Global safety limiter: hard-clamp to [-1, 1] so a resonant filter or
    // stacked grains can never blast the output. (Modes aim well below this.)
    for (size_t i = 0; i < size; ++i) {
      out[0][i] = out[0][i] > 1.0f ? 1.0f : (out[0][i] < -1.0f ? -1.0f : out[0][i]);
      out[1][i] = out[1][i] > 1.0f ? 1.0f : (out[1][i] < -1.0f ? -1.0f : out[1][i]);
    }
  }
}

int main() {
  hw.Init();
  hw.SetAudioBlockSize(params::audio::kBlockSize);
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

  const float sr = hw.AudioSampleRate();
  g_mod.Init(hw.AudioCallbackRate());

  synth_mode.Init(sr, hw);
  granular_mode.Init(sr, hw);
  generative_mode.Init(sr, hw);
  g_modes[MODE_SYNTH]      = &synth_mode;
  g_modes[MODE_GRANULAR]   = &granular_mode;
  g_modes[MODE_GENERATIVE] = &generative_mode;

  // Non-standard input: analog sensor on a free ADC pin.
  // (Motion IMU is tier-2 / deferred -- see io/imu.h.)
  g_sensors.Init(hw);       // re-inits ADC to add analog input on A0/D15

  // Footswitch LEDs (pins 22/23 are free outside the DFU flash gesture).
  led1.Init(hw.seed.GetPin(Hothouse::LED_1), false);
  led2.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  InitMidi(midi);

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  while (true) {
    PumpMidi(midi, g_modes[g_active]);

    // LED feedback: LED1 = engaged (lit) / bypassed (off);
    //               LED2 brightness indicates the active mode.
    led1.Set(g_bypass ? 0.0f : 1.0f);
    led2.Set(0.2f + 0.4f * static_cast<float>(g_active));
    led1.Update();
    led2.Update();

    hw.DelayMs(2);
    hw.CheckResetToBootloader();
  }
  return 0;
}
