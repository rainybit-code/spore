# 🌱 Spore — Versatile Generative Synth / FX Pedal

[![firmware build](https://github.com/rainybit-code/spore/actions/workflows/firmware.yml/badge.svg)](https://github.com/rainybit-code/spore/actions/workflows/firmware.yml)

**Spore** is firmware that turns an [Electrosmith Daisy Seed](https://daisy.audio) in a
[Cleveland Music Co. Hothouse](https://clevelandmusicco.com) enclosure into a multi-mode
instrument:

- **Synth** — playable voice over **USB MIDI** (analog/wavetable osc → filter → envelope)
- **Granular** — granular texture from the **built-in audio input**, with **freeze**
- **Generative** — self-playing "Krell" voice (random-walk pitch, evolving envelopes)

…plus a shared **modulation engine** (hardware-RNG LFOs / sample & hold / 6-slot mod
matrix), a **tempo-synced** delay + clock, and **non-standard inputs** (analog sensor on
a free ADC pin, optional I2C motion IMU).

Configure it live from its companion browser editor,
**[Propagator](https://github.com/rainybit-code/propagator)** (WebMIDI, no install) —
running at **<https://rainybit-code.github.io/propagator/>**. See
[`docs/MIDI_PROTOCOL.md`](docs/MIDI_PROTOCOL.md) for the CC/SysEx contract the two share.

> **Status:** experimental / work-in-progress. Builds clean with arm-none-eabi-gcc
> 12.3.1 → `build/daisy_synth.bin`. Treat hardware behaviour as unverified until you've
> flashed and tested on your own unit.

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

```sh
git clone --recurse-submodules <repo-url>    # pulls libDaisy + DaisySP too
```

1. **Toolchain** — install ARM GCC + `make` + `dfu-util`. Easiest is the official
   [Daisy toolchain](https://daisy.audio/tutorials/cpp-dev-env/); on Windows you can
   instead drop a portable xPack ARM GCC + windows-build-tools into a local
   `toolchain/` folder (gitignored) and `scripts/env.*` will put it on PATH
   automatically. `dfu-util` is only needed to flash, not to build. VS Code is
   optional (tasks + IntelliSense are wired in `.vscode/`).
2. **Libraries** — wired as git submodules under `lib/`; build them once:
   ```sh
   git submodule update --init --recursive   # if you didn't clone with --recurse-submodules
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

## Releases & versioning

CI (GitHub Actions, [`.github/workflows/firmware.yml`](.github/workflows/firmware.yml))
builds the firmware on every push/PR. Versioning is [SemVer](https://semver.org/) via git
tags; pushing a `vX.Y.Z` tag builds and publishes a **GitHub Release** with
`spore-vX.Y.Z.{bin,hex,elf}` attached, using the matching [`CHANGELOG.md`](CHANGELOG.md)
section as the release notes.

```sh
# 1. add your notes under "## [Unreleased]" in CHANGELOG.md
# 2. cut the release in one command (moves notes -> dated heading, commits, tags, pushes):
scripts/release.sh v0.2.0          # or:  scripts\release.ps1 v0.2.0   (Windows)
```

Flash a release `.bin` over USB DFU (hold **BOOT**, tap **RESET**, then
`scripts/flash.*`), or drag the `.bin` into the [Daisy Web Programmer](https://flash.daisy.audio).

## License & credits

**GPL-3.0-or-later.** Copyright (C) 2026 Joakim Langkilde. See [`LICENSE`](LICENSE).

The licence is GPL because the project compiles in the **Hothouse hardware proxy**
(`src/hothouse.cpp` / `.h`), which is GPL-3.0 © Cleveland Music Co. — included from
[HothouseExamples](https://github.com/clevelandmusicco/HothouseExamples) with its header
intact. [libDaisy](https://github.com/electro-smith/libDaisy) and
[DaisySP](https://github.com/electro-smith/DaisySP) (both MIT, © Electrosmith) are pulled
in as submodules under `lib/`, not redistributed here.

## AI disclosure

In the interest of transparency: this project was built with substantial help from AI.
Code, documentation, and design were generated and iterated with **Claude** (Claude Code)
under human direction and review.
