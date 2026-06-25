// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  clock.h  --  Spore's local tempo clock (with MIDI-clock override).
// =============================================================================
//  Free-runs at an internal BPM (set over MIDI from Propagator) and locks to
//  incoming MIDI timing clock (0xF8, 24 PPQN) whenever it's present -- so either
//  the GUI or an external device can be master. If clock stops arriving it falls
//  back to the internal BPM after a short timeout. Drives tempo-synced delay.
// =============================================================================
#pragma once

#include "daisy_seed.h"

namespace synthbox {

class MidiClock {
  public:
    void Init() {
        bpm_ = 120.0f;
        est_ = 120.0f;
        ext_ = false;
        running_ = false;
        last_tick_ms_ = 0;
        last_seen_ms_ = 0;
    }

    // Called on each incoming MIDI timing-clock tick (24 per quarter note).
    void Tick(uint32_t now) {
        if (last_tick_ms_) {
            uint32_t dt = now - last_tick_ms_;
            if (dt > 1 && dt < 300) {
                float inst = 60000.0f / (dt * 24.0f);  // BPM from one tick interval
                est_ = est_ * 0.85f + inst * 0.15f;    // smooth jitter
            }
        }
        last_tick_ms_ = now;
        last_seen_ms_ = now;
        ext_ = true;
        running_ = true;
    }
    void Start() { running_ = true; }
    void Continue() { running_ = true; }
    void Stop() { running_ = false; }

    void SetInternalBpm(float b) { bpm_ = b < 20.0f ? 20.0f : (b > 300.0f ? 300.0f : b); }

    // Call at control rate: drop back to internal tempo if clock stops arriving.
    void Update(uint32_t now) {
        if (ext_ && (now - last_seen_ms_) > 500) ext_ = false;
    }

    float Bpm() const { return ext_ ? est_ : bpm_; }
    bool External() const { return ext_; }
    bool Running() const { return running_; }

  private:
    float bpm_, est_;
    bool ext_, running_;
    uint32_t last_tick_ms_, last_seen_ms_;
};

}  // namespace synthbox
