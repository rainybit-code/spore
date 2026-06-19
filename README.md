# 🌱 Spore - Versatile Generative Synth / FX

[![firmware build](https://github.com/rainybit-code/spore/actions/workflows/firmware.yml/badge.svg)](https://github.com/rainybit-code/spore/actions/workflows/firmware.yml)

**Spore** is firmware that turns an [Electrosmith Daisy Seed](https://daisy.audio) in a
[Cleveland Music Co. Hothouse](https://clevelandmusicco.com) enclosure into a multi-mode
instrument:

- **Synth** - playable voice over **USB MIDI** (analog/wavetable osc → filter → envelope)
- **Granular** - granular texture from the **built-in audio input**, with **freeze**
- **Generative** - self-playing "Krell" voice (random-walk pitch, evolving envelopes)

…plus a shared **modulation engine** (hardware-RNG LFOs / sample & hold / 6-slot mod
matrix), a **tempo-synced** delay + clock, and an **analog sensor input** on a free ADC pin.

Configure it live from its companion browser editor,
**[Propagator](https://github.com/rainybit-code/propagator)** -
running at **<https://rainybit-code.github.io/propagator/>**. See
[`docs/MIDI_PROTOCOL.md`](docs/MIDI_PROTOCOL.md) for the CC/SysEx contract the two share.

![Propagator - the companion browser editor for Spore](docs/propagator.png)

## Hardware / platform

Spore currently targets one platform:

- **MCU board:** [Electrosmith Daisy Seed](https://electro-smith.com/products/daisy-seed)
  (STM32H750, stereo codec, SDRAM).
- **Built with:** [libDaisy](https://github.com/electro-smith/libDaisy) +
  [DaisySP](https://github.com/electro-smith/DaisySP) (vendored as submodules).
- **Enclosure:** the [Cleveland Music Co. Hothouse](https://shop.clevelandmusicco.com/products/hothouse-digital-signal-processing-platform-kit)
  DIY kit - chosen for ease of use, since it brings out 6 knobs, 3 toggles, 2
  footswitches, 2 LEDs and stereo 1/4" I/O off the shelf.

## Control surface

| Control      | Function |
|--------------|----------|
| **Toggle 1** | Mode: **UP** Synth · **MIDDLE** Granular · **DOWN** Generative *(use a 3-position switch)* |
| **Toggle 2** | Per-mode variant (waveform / pitch-quantize / scale) |
| **Toggle 3** | FX: **UP** off · **MIDDLE** delay · **DOWN** reverb |
| **Knobs 1–6**| Per-mode macros - see [`src/config/params.h`](src/config/params.h) |
| **Footsw 1** | **Tap** = engage/bypass · **Hold** = edit FX (knobs become FX controls) |
| **Footsw 2** | Mode action - Granular **freeze**, Generative **re-seed** |
| **LED 1 / 2**| Engaged state (mid = editing FX) / active FX |
| Hold **both** footswitches 2s | Reboot to **DFU** for flashing |

Audio: Hothouse stereo **in** and **out** (1/4" jacks). Synth & Generative ignore the
input (they generate); Granular records and reuses it.

**Global FX** is a decoupled delay + reverb block on the **Synth** and **Granular** output
(selected by Toggle 3); **Generative** uses its own dedicated reverb instead, so the global
FX is bypassed there. Hold Footswitch 1 to remap the 6 knobs to FX (mix / delay time /
feedback / tone / reverb decay / damping) - **soft-takeover** means nothing jumps when you
let go. See `src/fx/effects.h`.

## Signal flow (per mode)

Each mode is its own small audio graph. Knobs/CC set the parameters; the shared **mod engine**
(LFOs · random S&H · chaos · analog sensor) drives the **6-slot mod matrix** that moves them.
A safety **limiter** hard-clamps the output of every mode.

### Mode 1 - Synth   ·   USB-MIDI played

```text
  IN · USB MIDI (note + velocity)
  │
  ▼
  ┌─ VOICE · polyphonic, up to 6 ────────────────────────────────────────────┐
  │   OSC     analog: 2-4 detuned unison + sub                               │
  │            or  wavetable: scan · FM · wavefold + sub                     │
  │   SHAPE   osc ─► drive (sat) ─► filter (SVF 2-pole | Moog 4-pole)        │
  │   AMP     ─► ADSR x velocity         ·   filter-env ─► cutoff            │
  └──────────────────────────────────────────────────────────────────────────┘
  │   voices summed + stereo-panned
  ▼
  ┌─ GLOBAL FX · Toggle 3 ───────────────────────────────────────────────────┐
  │   off   |   delay   |   reverb                                           │
  └──────────────────────────────────────────────────────────────────────────┘
  │
  ▼
  ┌─ LIMITER ────────────────────────────────────────────────────────────────┐
  │   hard-clamp +/-1    ─►    OUT · L / R                                   │
  └──────────────────────────────────────────────────────────────────────────┘

  ┌─ MOD MATRIX · 6 slots ───────────────────────────────────────────────────┐
  │   SRC   LFO1 · LFO2 · Rnd S&H · Chaos · Steps · Sensor · Vel · Key       │
  │   DST   cutoff · pitch · wt-scan · drive · sub · FM · amp · LFO-rate     │
  └──────────────────────────────────────────────────────────────────────────┘
```

### Mode 2 - Granular   ·   processes the audio input

```text
  IN · AUDIO (1/4" jack)
  │
  ▼
  ┌─ CAPTURE ────────────────────────────────────────────────────────────────┐
  │   record ─► SDRAM ring buffer            (FOOTSW 2 = freeze)             │
  └──────────────────────────────────────────────────────────────────────────┘
  │
  ▼
  ┌─ GRAINS · up to 12 ──────────────────────────────────────────────────────┐
  │   read @ pitch   ·   Hann window   ·   summed ─► wet                     │
  └──────────────────────────────────────────────────────────────────────────┘
  │
  ▼
  ┌─ DRY / WET MIX ──────────────────────────────────────────────────────────┐
  │   dry (live input)   +   wet (grains)                                    │
  └──────────────────────────────────────────────────────────────────────────┘
  │
  ▼
  ┌─ GLOBAL FX · Toggle 3 ───────────────────────────────────────────────────┐
  │   off | delay | reverb    ─►    LIMITER (+/-1)    ─►    OUT · L / R      │
  └──────────────────────────────────────────────────────────────────────────┘

  ┌─ MODULATION ─────────────────────────────────────────────────────────────┐
  │   density ◄ Chaos        ·        pitch-spread ◄ Sensor + random         │
  └──────────────────────────────────────────────────────────────────────────┘
```

### Mode 3 - Generative   ·   self-playing

```text
  ┌─ SEQUENCER (internal) ───────────────────────────────────────────────────┐
  │   slow clock ─► random-walk pitch ─► seed note / chord                   │
  └──────────────────────────────────────────────────────────────────────────┘
  │
  ▼
  ┌─ VOICE · up to 5  (same engine as Synth) ────────────────────────────────┐
  │   osc ─► drive ─► filter (SVF | Moog) ─► amp (ADSR)                      │
  └──────────────────────────────────────────────────────────────────────────┘
  │   voices summed
  ▼
  ┌─ DEDICATED REVERB · lush (no global FX) ─────────────────────────────────┐
  │   ─►    LIMITER (+/-1)    ─►    OUT · L / R                              │
  └──────────────────────────────────────────────────────────────────────────┘

  ┌─ MODULATION ─────────────────────────────────────────────────────────────┐
  │   filter sway ◄ Chaos    ·    scan drift ◄ Chaos    ·    centre ◄ Sensor │
  └──────────────────────────────────────────────────────────────────────────┘
```

## Getting started

```sh
# pulls libDaisy + DaisySP too
git clone --recurse-submodules https://github.com/rainybit-code/spore.git
```

1. **Toolchain** - install ARM GCC + `make` + `dfu-util`. Easiest is the official
   [Daisy toolchain](https://daisy.audio/tutorials/cpp-dev-env/); on Windows you can
   instead drop a portable xPack ARM GCC + windows-build-tools into a local
   `toolchain/` folder (gitignored) and `scripts/env.*` will put it on PATH
   automatically. `dfu-util` is only needed to flash, not to build. VS Code 
   tasks + IntelliSense are wired in `.vscode/`.
2. **Libraries** - wired as git submodules under `lib/`; build them once:
   ```sh
   # if you didn't clone with --recurse-submodules
   git submodule update --init --recursive
   # or scripts\build-libs.ps1 on Windows
   scripts/build-libs.sh
   ```
3. **Build**: `scripts/build.sh` (or `.ps1`, or VS Code task *build*) → `build/daisy_synth.bin`.
4. **Flash** (via USB DFU): put the Daisy in DFU mode (hold **BOOT**, tap
   **RESET**), then `scripts/flash.sh` (or `.ps1`).

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
                      clock (MIDI clock), sensors (analog ADC)
lib/                  libDaisy + DaisySP (git submodules)
scripts/              build / flash / build-libs / clean / release (.sh + .ps1)
pd/                   Pure Data sketches for prototyping DSP ideas
```

**Tuning:** every number worth changing lives in `src/config/params.h`. Edit a value
→ reflash, or turn a knob live (ranges are defined there too). Adding a mode = drop a
class implementing `IMode` (`src/modes/mode.h`) into the array in `main.cpp`.

## Non-standard inputs

- **Analog sensor** (LDR / FSR / flex / expression / CV) - wired to free **A0 = D15**;
  handled in `src/io/sensors.h` (extends the ADC; more channels: A9=D24, A11=D28).
  Feeds the modulation routing in each mode's `Control()`; neutral (no effect) until
  the hardware is connected.

## Ideas & roadmap

See [`docs/IDEAS.md`](docs/IDEAS.md) for the tiered backlog (more inputs, a wireless
modulator, future modes, presets…).

## Releases & versioning

CI (GitHub Actions, [`.github/workflows/firmware.yml`](.github/workflows/firmware.yml))
builds the firmware on every push/PR. Versioning is [SemVer](https://semver.org/) via git
tags; pushing a `vX.Y.Z` tag builds and publishes a **GitHub Release** with
`spore-vX.Y.Z.{bin,hex,elf}` attached, using the matching [`CHANGELOG.md`](CHANGELOG.md)
section as the release notes.

```sh
# 1. add your notes under "## [Unreleased]" in CHANGELOG.md
# 2. cut the release in one command (moves notes -> dated heading, commits, tags, pushes)
# Windows: scripts\release.ps1 v0.2.0
scripts/release.sh v0.2.0
```

Flash a release `.bin` over USB DFU (hold **BOOT**, tap **RESET**, then
`scripts/flash.*`), or drag the `.bin` into the [Daisy Web Programmer](https://flash.daisy.audio).

## License & credits

**GPL-3.0-or-later.** Copyright (C) 2026 Joakim Langkilde. See [`LICENSE`](LICENSE).

The licence is GPL because the project compiles in the **Hothouse hardware proxy**
(`src/hothouse.cpp` / `.h`), which is GPL-3.0 © Cleveland Music Co. - included from
[HothouseExamples](https://github.com/clevelandmusicco/HothouseExamples) with its header
intact. [libDaisy](https://github.com/electro-smith/libDaisy) and
[DaisySP](https://github.com/electro-smith/DaisySP) (both MIT, © Electrosmith) are pulled
in as submodules under `lib/`, not redistributed here.

## AI disclosure

In the interest of transparency: this project was built with substantial help from AI.
Code, documentation, and design were generated and iterated with **Claude** (Claude Code)
under human direction and review.
