// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// Minimal bring-up test: boot the bare Daisy Seed and blink the onboard LED.
// No Hothouse init, no codec audio, no USB, no SDRAM/FX. If THIS blinks, the
// board boots our flashed code fine and the bug is later in the real firmware's
// init. If it stays dark, it's a boot/hardware/flash-image problem.
#include "daisy_seed.h"

using namespace daisy;

DaisySeed hw;

int main(void) {
  hw.Init();
  bool on = false;
  while (true) {
    hw.SetLed(on);
    on = !on;
    hw.DelayMs(200);  // ~2.5 Hz blink
  }
}
