// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  sensors.h  --  analog "oddity" inputs on free ADC pins.  (REAL, not a stub)
// =============================================================================
//  Non-standard input: cheap analog parts (LDR photoresistor / FSR pressure pad
//  / flex sensor / expression pedal / CV) read into a FREE Daisy ADC pin.
//
//  The Hothouse configures the ADC for exactly its 6 knobs (A1..A6 = D16..D21)
//  in hothouse.cpp::InitAnalogControls. To add our own analog input we
//  RE-INITIALIZE the ADC to include the 6 knobs PLUS expansion channel(s), then
//  re-bind the knob controls and bind our sensor control(s).
//
//  FREE ADC-capable pins (verified against lib/libDaisy daisy_seed.h):
//        A0  = D15   <-- used here by default (the expansion analog input)
//        A9  = D24
//        A11 = D28
//  (A7/A8 = D22/D23 drive the LEDs; A10 = D25 is FOOTSWITCH 1.)
//
//  Wiring: sensor between 3V3 and the pin, fixed resistor from pin to GND (a
//  voltage divider). For an LDR ~10k fixed resistor is a good start. The pin
//  must stay within 0..3.3V.
//
//  USAGE (in main, AFTER hw.Init() and BEFORE hw.StartAdc()):
//        sensors.Init(hw);
//  Then call sensors.Process() once per block and read Value(i) / Light().
// =============================================================================
#pragma once

#include "daisy_seed.h"
#include "hothouse.h"

namespace synthbox {

class AnalogSensors {
  public:
    // Number of expansion analog inputs. Bump this and add pins below to add more.
    static constexpr int kNumSensors = 1;

    // Re-init the ADC with knobs + expansion sensor(s) and (re)bind all controls.
    void Init(clevelandmusicco::Hothouse& hw) {
        auto& seed = hw.seed;
        const float sr = hw.AudioCallbackRate();
        constexpr int kNumKnobs = clevelandmusicco::Hothouse::KNOB_LAST;  // 6

        // Knob pins -- MUST match hothouse.cpp order so knob channels stay 0..5.
        const daisy::Pin knob_pins[kNumKnobs] = {daisy::seed::D16, daisy::seed::D17,
                                                 daisy::seed::D18, daisy::seed::D19,
                                                 daisy::seed::D20, daisy::seed::D21};

        // Expansion analog input pin(s) on free ADC channels.
        const daisy::Pin sensor_pins[kNumSensors] = {daisy::seed::A0 /* D15 */};

        daisy::AdcChannelConfig cfg[kNumKnobs + kNumSensors];
        for (int i = 0; i < kNumKnobs; ++i) cfg[i].InitSingle(knob_pins[i]);
        for (int j = 0; j < kNumSensors; ++j) cfg[kNumKnobs + j].InitSingle(sensor_pins[j]);

        seed.adc.Init(cfg, kNumKnobs + kNumSensors);

        // Re-bind the knob controls (their pointers are stable across Init), and
        // bind our sensor controls. Light smoothing via slew.
        for (int i = 0; i < kNumKnobs; ++i) hw.knobs[i].Init(seed.adc.GetPtr(i), sr);
        for (int j = 0; j < kNumSensors; ++j) sensor_[j].Init(seed.adc.GetPtr(kNumKnobs + j), sr);

        present_ = true;
    }

    // Call once per block (control rate).
    void Process() {
        if (!present_) return;
        for (int j = 0; j < kNumSensors; ++j) sensor_[j].Process();
    }

    bool Present() const { return present_; }

    // Normalized 0..1 reading of expansion channel i (neutral 0.5 if unwired).
    float Value(int i) const { return present_ && i < kNumSensors ? sensor_[i].Value() : 0.5f; }

    // Convenience aliases for the default channel.
    float Light() const { return Value(0); }
    float Pressure() const { return Value(0); }

  private:
    daisy::AnalogControl sensor_[kNumSensors];
    bool present_ = false;
};

}  // namespace synthbox
