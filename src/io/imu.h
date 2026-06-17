// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  imu.h  --  motion / IMU input over I2C   *** TIER 2 -- NOT IN THE BUILD ***
// =============================================================================
//  Deferred for now. This file is intentionally NOT included by main.cpp or the
//  modes. Kept as a ready slot: when you want motion control, add `Imu& imu` back
//  to ModContext (modes/mode.h), include + init it in main.cpp, and fill in the
//  I2C reads below. The rest of the architecture already supports an extra input.
//
//  Non-standard input: a motion sensor (e.g. MPU6050 / LSM6DS3 / ICM-series)
//  wired to the Daisy Seed's FREE I2C1 pins, which the Hothouse leaves open:
//        SCL = D11        SDA = D12
//
//  STATUS: STUB. Until the hardware is wired and a chip is chosen this returns
//  neutral values (0.5 / 0) and Present() == false, so it compiles and runs
//  harmlessly on bare hardware. Fill in Init()/Process() against the chosen chip.
//
//  Reference for the real implementation:
//    - daisy::I2CHandle (lib/libDaisy) to talk to the chip
//    - read WHO_AM_I to confirm presence, then accel registers each Process()
//    - normalize: tilt -> 0..1, shake (accel magnitude delta) -> 0..1
// =============================================================================
#pragma once

#include "daisy_seed.h"

namespace synthbox {

class Imu {
 public:
  // Returns true if a sensor was detected on the bus.
  bool Init(daisy::DaisySeed& /*seed*/) {
    // TODO(Milestone 6): configure I2CHandle on D11/D12, probe WHO_AM_I.
    present_ = false;
    return present_;
  }

  // Call at control rate (once per block) from main().
  void Process() {
    if (!present_) return;
    // TODO(Milestone 6): read accel/gyro, update tilt_x_/tilt_y_/shake_.
  }

  bool  Present() const { return present_; }
  // Normalized control sources (neutral when absent):
  float TiltX() const { return present_ ? tilt_x_ : 0.5f; }  // 0..1 (left..right)
  float TiltY() const { return present_ ? tilt_y_ : 0.5f; }  // 0..1 (back..fwd)
  float Shake() const { return present_ ? shake_ : 0.0f; }   // 0..1 (motion energy)

 private:
  bool  present_ = false;
  float tilt_x_ = 0.5f, tilt_y_ = 0.5f, shake_ = 0.0f;
};

}  // namespace synthbox
