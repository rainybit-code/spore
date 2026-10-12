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
    static constexpr int kHistoryTicks = 24;  // 1 quarter note (24 PPQN)

    void Init() {
        bpm_ = 120.0f;
        est_ = 120.0f;
        ext_ = false;
        running_ = false;
        tick_count_ = 0;
        head_ = 0;
        last_seen_ms_ = 0;
        for (int i = 0; i < kHistoryTicks; ++i) {
            history_us_[i] = 0;
        }
    }

    // Called on each incoming MIDI timing-clock tick (24 per quarter note).
    // now_us is in microseconds (from daisy::System::GetUs()).
    void Tick(uint32_t now_us) {
        last_seen_ms_ = daisy::System::GetNow();
        ext_ = true;
        running_ = true;

        if (tick_count_ > 0) {
            // Measure time over the sliding window (up to 24 ticks == 1 quarter note).
            int span = tick_count_ < kHistoryTicks ? tick_count_ : kHistoryTicks;
            int oldest_idx = (head_ - span + kHistoryTicks) % kHistoryTicks;
            uint32_t dt_us = now_us - history_us_[oldest_idx];

            // 1 beat = 60,000,000 us. span ticks = (span / 24) beats.
            // Expected dt_us = (60,000,000 * span) / (BPM * 24).
            // Therefore inst BPM = (60,000,000 * span) / (dt_us * 24).
            if (dt_us > 1000 && dt_us < 10000000) {
                float inst = (60000000.0f * span) / (static_cast<float>(dt_us) * 24.0f);
                if (inst >= 20.0f && inst <= 300.0f) {
                    if (tick_count_ < 6) {
                        est_ = inst;  // lock on fast when playback starts
                    } else {
                        est_ = est_ * 0.90f + inst * 0.10f;  // smooth any residual jitter
                    }
                }
            }
        }

        history_us_[head_] = now_us;
        head_ = (head_ + 1) % kHistoryTicks;
        if (tick_count_ < kHistoryTicks) {
            tick_count_++;
        }
    }
    void Start() {
        running_ = true;
        tick_count_ = 0;
    }
    void Continue() { running_ = true; }
    void Stop() { running_ = false; }

    void SetInternalBpm(float b) { bpm_ = b < 20.0f ? 20.0f : (b > 300.0f ? 300.0f : b); }

    // Call at control rate: drop back to internal tempo if clock stops arriving.
    void Update(uint32_t now_ms) {
        if (ext_ && (now_ms - last_seen_ms_) > 500) {
            ext_ = false;
            tick_count_ = 0;
        }
    }

    float Bpm() const { return ext_ ? est_ : bpm_; }
    bool External() const { return ext_; }
    bool Running() const { return running_; }

  private:
    float bpm_, est_;
    bool ext_, running_;
    int tick_count_;
    int head_;
    uint32_t history_us_[kHistoryTicks];
    uint32_t last_seen_ms_;
};

}  // namespace synthbox
