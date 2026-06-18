// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  midi_in.h  --  USB MIDI input plumbing.
// =============================================================================
//  Connect the Daisy's USB to a computer/host; notes play the active mode (only
//  the synth voice responds to notes, others ignore them by default).
// =============================================================================
#pragma once

#include <cmath>

#include "daisy_seed.h"
#include "util/CpuLoadMeter.h"
#include "config/params.h"
#include "config/synth_params.h"
#include "mod/modulation.h"
#include "io/knobs.h"
#include "io/clock.h"
#include "modes/mode.h"

namespace synthbox {

inline void InitMidi(daisy::MidiUsbHandler& midi) {
  daisy::MidiUsbHandler::Config cfg;
  cfg.transport_config.periph = daisy::MidiUsbTransport::Config::INTERNAL;
  midi.Init(cfg);
}

// Drain pending MIDI events. Notes -> active mode; CC -> live knob values
// (the WebMIDI management interface, Phase 1). See docs/MIDI_PROTOCOL.md.
// Returns true if any event was processed (used to blink a MIDI-activity LED).
inline bool PumpMidi(daisy::MidiUsbHandler& midi, IMode* mode, ShiftKnobs& shift,
                     int& modeSel, int& fxSel, MidiClock& clock, int& delaySync,
                     daisy::CpuLoadMeter& cpu, ModEngine& modEngine) {
  bool active = false;
  midi.Listen();
  while (midi.HasEvents()) {
    active = true;
    auto msg = midi.PopEvent();
    switch (msg.type) {
      case daisy::SystemRealTime: {   // tempo clock / transport from GUI or device
        uint32_t now = daisy::System::GetNow();
        switch (msg.srt_type) {
          case daisy::TimingClock: clock.Tick(now); break;
          case daisy::Start:       clock.Start();    break;
          case daisy::Continue:    clock.Continue(); break;
          case daisy::Stop:        clock.Stop();     break;
          default: break;
        }
      } break;
      case daisy::NoteOn: {
        auto m = msg.AsNoteOn();
        if (m.velocity != 0)
          mode->NoteOn(static_cast<float>(m.note), m.velocity / 127.0f);
        else
          mode->NoteOff(static_cast<float>(m.note));
      } break;
      case daisy::NoteOff: {
        auto m = msg.AsNoteOff();
        mode->NoteOff(static_cast<float>(m.note));
      } break;
      case daisy::ControlChange: {
        auto  cc = msg.AsControlChange();
        int   n = cc.control_number;
        float v = cc.value / 127.0f;
        if (n >= params::midi::kCcModeKnobBase &&
            n < params::midi::kCcModeKnobBase + ShiftKnobs::kKnobs)
          shift.SetValue(ShiftKnobs::MODE, n - params::midi::kCcModeKnobBase, v);
        else if (n >= params::midi::kCcFxKnobBase &&
                 n < params::midi::kCcFxKnobBase + ShiftKnobs::kKnobs)
          shift.SetValue(ShiftKnobs::FX, n - params::midi::kCcFxKnobBase, v);
        else if (n == params::midi::kCcModeSelect)
          modeSel = cc.value < 43 ? 0 : (cc.value < 86 ? 1 : 2);
        else if (n == params::midi::kCcFxSelect)
          fxSel = cc.value < 43 ? 0 : (cc.value < 86 ? 1 : 2);
        else if (n >= params::midi::kCcSynthBase &&
                 n < params::midi::kCcSynthBase + SP_COUNT)
          g_synthParams.v[n - params::midi::kCcSynthBase] = v;
        else if (n == params::midi::kCcTempo)
          clock.SetInternalBpm(40.0f + v * 160.0f);            // 40..200 BPM
        else if (n == params::midi::kCcDelaySync)
          delaySync = static_cast<int>(v * 4.99f);             // 0 off / 1..4 divisions
        else if (n == params::midi::kCcChaosSpeed)
          modEngine.SetChaosSpeed(params::mod::kChaosSpeedMin +
              v * (params::mod::kChaosSpeedMax - params::mod::kChaosSpeedMin));
        else if (n == params::midi::kCcSysReboot && cc.value >= 64)
          daisy::System::ResetToBootloader(daisy::System::BootloaderMode::STM);
      } break;
      case daisy::SystemCommon: {
        if (msg.sc_type != daisy::SystemExclusive) break;
        // Identify request `F0 7D 01 F7` -> reply `F0 7D 41 <version ascii> F7`.
        // (0x7D = the non-commercial/educational manufacturer ID.) libDaisy may or
        // may not keep the 0xF0 framing in data[], so accept either.
        auto sx = msg.AsSystemExclusive();
        int  i  = (sx.length > 0 && sx.data[0] == 0xF0) ? 1 : 0;
        if (sx.length < i + 2 || sx.data[i] != 0x7D) break;   // not our manufacturer ID
        const uint8_t cmd = sx.data[i + 1];
        if (cmd == 0x01) {                                    // identify request
          uint8_t reply[32];
          int     j  = 0;
          reply[j++] = 0xF0; reply[j++] = 0x7D; reply[j++] = 0x41;   // identify reply
          for (const char* p = params::kFwVersion; *p && j < 30; ++p)
            reply[j++] = static_cast<uint8_t>(*p) & 0x7F;
          reply[j++] = 0xF7;
          midi.SendMessage(reply, j);
        } else if (cmd == 0x02) {                             // CPU-load query
          // Reply `F0 7D 42 <avg%> <max%> F7`. Each byte is 0..127 (% load, capped);
          // NaN before the first audio block reads as 0. Lets Propagator show headroom.
          auto pct = [](float load) -> uint8_t {
            if (std::isnan(load) || load < 0.0f) return 0;
            int p = static_cast<int>(load * 100.0f + 0.5f);
            return static_cast<uint8_t>(p > 127 ? 127 : p);
          };
          uint8_t reply[6] = {0xF0, 0x7D, 0x42,
                              pct(cpu.GetAvgCpuLoad()), pct(cpu.GetMaxCpuLoad()), 0xF7};
          midi.SendMessage(reply, 6);
        } else if (cmd == 0x03) {                             // chaos-state query
          // Reply `F0 7D 43 <x> <z> F7` -- the Lorenz X/Z pair (the classic butterfly
          // projection), each -1..1 mapped to 0..127. The chaos already runs every
          // block (it's the mod engine), so this just reports the cached value: no
          // extra audio-CPU cost. Propagator polls it to draw the live attractor.
          auto enc = [](float val) -> uint8_t {   // -1..1 -> 0..127
            int b = static_cast<int>((val + 1.0f) * 0.5f * 127.0f + 0.5f);
            return static_cast<uint8_t>(b < 0 ? 0 : (b > 127 ? 127 : b));
          };
          uint8_t reply[6] = {0xF0, 0x7D, 0x43,
                              enc(modEngine.ChaosX()), enc(modEngine.ChaosY()), 0xF7};
          midi.SendMessage(reply, 6);
        }
      } break;
      default:
        break;
    }
  }
  return active;
}

}  // namespace synthbox
