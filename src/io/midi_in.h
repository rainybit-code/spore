// =============================================================================
//  midi_in.h  --  USB MIDI input plumbing.
// =============================================================================
//  Connect the Daisy's USB to a computer/host; notes play the active mode (only
//  the synth voice responds to notes, others ignore them by default).
// =============================================================================
#pragma once

#include "daisy_seed.h"
#include "modes/mode.h"

namespace synthbox {

inline void InitMidi(daisy::MidiUsbHandler& midi) {
  daisy::MidiUsbHandler::Config cfg;
  cfg.transport_config.periph = daisy::MidiUsbTransport::Config::INTERNAL;
  midi.Init(cfg);
}

// Drain pending MIDI events into the active mode. Call from the main loop.
inline void PumpMidi(daisy::MidiUsbHandler& midi, IMode* mode) {
  midi.Listen();
  while (midi.HasEvents()) {
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
      default:
        break;
    }
  }
}

}  // namespace synthbox
