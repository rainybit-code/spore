# Daisy Seed + Hothouse — Versatile Generative Synth / FX

A multi-mode instrument for the [Electrosmith Daisy Seed](https://daisy.audio) in a
[Cleveland Music Co. Hothouse](https://clevelandmusicco.com) pedal:

- **Synth** — playable voice over **USB MIDI** (osc → Moog filter → envelope)
- **Granular** — granular texture from the **built-in audio input**, with **freeze**
- **Generative** — self-playing "Krell" voice (random-walk pitch, evolving envelopes)

…plus a shared **random modulation engine** (hardware-RNG LFOs / sample & hold) and
**non-standard inputs** (analog sensor on a free ADC pin, optional I2C motion IMU).

> Status: firmware **builds clean** (arm-none-eabi-gcc 12.3.1) →
> `build/daisy_synth.bin` (~110 KB, 84% of internal flash; `BOOT_NONE`). Not yet run
> on hardware (Daisy + Hothouse in transit). A portable, no-admin toolchain is
> installed under `toolchain/` and the build scripts use it automatically.

## Control surface

| Control      | Function |
|--------------|----------|
| **Toggle 1** | Mode: **UP** Synth · **MIDDLE** Granular · **DOWN** Generative *(use a 3-position switch)* |
| **Toggle 2** | Per-mode variant (waveform / pitch-quantize / scale) |
| **Toggle 3** | FX: **UP** off · **MIDDLE** delay · **DOWN** reverb |
| **Knobs 1–6**| Per-mode macros — see [`src/config/params.h`](src/config/params.h) |
| **Footsw 1** | **Tap** = engage/bypass · **Hold** = edit FX (knobs become FX controls) |
| **Footsw 2** | Mode action — Granular **freeze**, Generative **re-seed** |
| **LED 1 / 2**| Engaged state (mid = editing FX) / active FX |
| Hold **both** footswitches 2s | Reboot to **DFU** for flashing |

Audio: Hothouse stereo **in** and **out** (1/4" jacks). Synth & Generative ignore the
input (they generate); Granular records and reuses it.

**Global FX** is a decoupled delay + reverb block that processes whatever the active
mode outputs (selected by Toggle 3). Hold Footswitch 1 to remap the 6 knobs to FX
(mix / delay time / feedback / tone / reverb decay / damping) — **soft-takeover**
means nothing jumps when you let go. See `src/fx/effects.h`.

## Getting started

1. **Toolchain** — a portable ARM GCC + `make` is already installed under
   `toolchain/` (no admin needed); `scripts/env.*` puts it on PATH automatically.
   To recreate on another machine, either re-download the xPack ARM GCC +
   windows-build-tools into `toolchain/`, or install the official Daisy toolchain
   (<https://daisy.audio/tutorials/cpp-dev-env/>). For flashing you'll also want
   `dfu-util` (not required to build). Install VS Code for the tasks/IntelliSense.
2. **Libraries** (already wired as submodules under `lib/`, and already built):
   ```sh
   git submodule update --init --recursive   # if cloning fresh
   scripts/build-libs.sh      # or scripts\build-libs.ps1 on Windows
   ```
3. **Build**: `scripts/build.sh` (or `.ps1`, or VS Code task *build*) → `build/daisy_synth.bin`.
4. **Flash** (no programmer needed): put the Daisy in DFU mode (hold **BOOT**, tap
   **RESET**), then `scripts/flash.sh` (or `.ps1`).
5. **Debug**: `daisy.PrintLine(...)` over USB serial → `scripts/monitor.ps1`.

## Project layout

```
src/
  main.cpp            wiring: mode dispatch, controls, MIDI, audio callback
  hothouse.h/.cpp     Hothouse hardware proxy (vendored, GPL-3.0)
  config/params.h     ★ ALL tunable parameters (ranges, rates, sizes)
  modes/              synth_mode / granular_mode / generative_mode (one each)
  mod/modulation.h    random LFO / sample&hold / random walk / RNG
  fx/effects.h        decoupled global FX: delay + reverb (Toggle 3)
  io/                 controls, knobs (shift-layer soft-takeover), midi_in,
                      imu (I2C, tier-2), sensors (analog ADC)
lib/                  libDaisy + DaisySP (git submodules)
scripts/              build / flash / build-libs / clean / monitor (.sh + .ps1)
pd/                   Pure Data sketches for prototyping DSP ideas
```

**Tuning:** every number worth changing lives in `src/config/params.h`. Edit a value
→ reflash, or turn a knob live (ranges are defined there too). Adding a mode = drop a
class implementing `IMode` (`src/modes/mode.h`) into the array in `main.cpp`.

## Non-standard inputs

- **Analog sensor** (LDR / FSR / flex / expression / CV) — wired to free **A0 = D15**;
  handled in `src/io/sensors.h` (extends the ADC; more channels: A9=D24, A11=D28).
  Feeds the modulation routing in each mode's `Control()`; neutral (no effect) until
  the hardware is connected.
- **Motion IMU** over I2C1 (D11/D12) — **tier 2, deferred**. `src/io/imu.h` is a ready
  slot but is not part of the current build.

## Ideas & roadmap

See [`docs/IDEAS.md`](docs/IDEAS.md) for the tiered backlog (analog inputs, the
deferred motion IMU, the wireless spinning-top modulator, future modes, presets…).

License: GPL-3.0 (inherited from the Hothouse hardware proxy).
