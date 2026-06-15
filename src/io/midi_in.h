// =============================================================================
//  midi_in.h  --  USB MIDI input plumbing.
// =============================================================================
//  Connect the Daisy's USB to a computer/host; notes play the active mode (only
//  the synth voice responds to notes, others ignore them by default).
// =============================================================================
#pragma once

#include "daisy_seed.h"
#include "config/params.h"
#include "io/knobs.h"
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
                     int& modeSel, int& fxSel) {
  bool active = false;
  midi.Listen();
  while (midi.HasEvents()) {
    active = true;
    auto msg = midi.PopEvent();
    switch (msg.type) {
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
      } break;
      default:
        break;
    }
  }
  return active;
}

}  // namespace synthbox
