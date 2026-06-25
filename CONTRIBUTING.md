# Contributing to Spore

Thanks for your interest in improving Spore! This guide covers how the project is
laid out, the conventions to follow, and how to get a change merged.

## Getting set up

```sh
git clone --recurse-submodules https://github.com/rainybit-code/spore.git
cd spore
scripts/setup.sh        # init submodules + build libDaisy & DaisySP (Windows: scripts\setup.ps1)
scripts/build.sh        # -> build/daisy_synth.bin
```

See [`README.md`](README.md#getting-started) for the toolchain install and flashing steps.
You can build the firmware without any hardware; you only need a Daisy Seed + Hothouse to
run it.

## Project layout

```
src/
  main.cpp            wiring: mode dispatch, controls, MIDI, audio callback
  hothouse.h/.cpp     Hothouse hardware proxy (vendored, GPL-3.0)
  config/             params.h (all tunables) + per-mode parameter structs
  modes/              one class per mode, all implementing IMode (mode.h)
  mod/                modulation engine (LFOs / S&H / chaos / RNG)
  fx/                 global FX (delay + reverb) and the master output stage
  io/                 controls, knobs, MIDI, clock, sensors
  dsp/                shared voice DSP
```

## Conventions

- **Language/style.** C++14 (`-std=gnu++14`). Formatting is enforced by
  [`.clang-format`](.clang-format) (Google C++, 4-space indent, 100-col). Run
  `clang-format -i` on any file you touch before committing — CI rejects unformatted code.
- **Header-only modules.** Everything under `modes/`, `io/`, `mod/`, `fx/`, and `dsp/` is
  header-only and pulled in through `main.cpp`; only `main.cpp`, `hothouse.cpp`, and
  `usb_identity.c` compile as translation units (see `Makefile`'s `CPP_SOURCES`).
- **Namespace.** Project code lives in `namespace synthbox`.
- **License header.** Start every source file with the two-line SPDX + copyright header
  used throughout the tree.
- **Tunable values go in `config/params.h`.** Ranges, rates, sizes, and thresholds belong
  there with a unit comment — not as literals inside DSP code.
- **Comments** explain *why* the current code is the way it is when that isn't obvious from
  reading it. Keep them concise; don't narrate change history.

## Adding a mode

Implement the [`IMode`](src/modes/mode.h) interface in a new `src/modes/<name>_mode.h`,
add the matching tunables to `config/params.h`, then register an instance in the
`g_modes[]` array in `main.cpp` and bump `MODE_COUNT` in `src/io/controls.h`.

## The MIDI contract

The firmware and the [Propagator](https://github.com/rainybit-code/propagator) browser
editor share a CC/SysEx protocol. If you add or change a control that the editor should
see, update both [`src/config/params.h`](src/config/params.h) (the `params::midi` map) and
[`docs/MIDI_PROTOCOL.md`](docs/MIDI_PROTOCOL.md).

## Concurrency & global state

Spore runs two contexts: the **audio ISR** (`AudioCallback`) and the **main loop**. State
shared between them lives in globals in `main.cpp`:

- The audio ISR owns the per-block DSP and writes the mode/FX selection and the CPU
  watchdog state.
- The main loop owns USB MIDI, the tempo clock, and the LEDs, and reads/writes the same
  selection state in response to MIDI.
- Globals touched by both contexts are `volatile` (e.g. `g_active`, `g_overload`). Keep
  cross-context shared state to single-word reads/writes; there is no locking.
- The `g_*Params` structs (`config/*_params.h`) are written by MIDI in the main loop and
  read by the ISR; they are plain value writes, safe to tear at the field level.

If you introduce new shared state, document which context owns it and prefer a single
writer.

## Build scripts

Each script exists as a `.sh`/`.ps1` pair (`scripts/build.{sh,ps1}`, etc.). They are thin
wrappers over the `Makefile` — keep the actual logic in the Makefile where possible, and
**update both files in the pair** when you change one.

## Submitting a change

1. Branch off `main`.
2. Make your change. Running `clang-format -i` and `scripts/build.sh` locally is the fast
   way to catch problems, but CI enforces both either way.
3. Add a line under `## [Unreleased]` in [`CHANGELOG.md`](CHANGELOG.md).
4. Open a PR against `main`. On every PR, CI builds the firmware, checks formatting, and
   requires a `CHANGELOG.md` entry (label the PR `skip-changelog` for changes that don't
   warrant one, e.g. CI-only tweaks).

Releases are cut from version tags — see the *Releases & versioning* section of the README.
