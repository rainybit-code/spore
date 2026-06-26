# Changelog

All notable changes to the Spore firmware are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); this project
uses [Semantic Versioning](https://semver.org/) (`vMAJOR.MINOR.PATCH`).

## [Unreleased]
- **Fix: intermittent crackle in Synth mode under load.** The crackle was a per-block CPU
  spike, not sustained load (so the watchdog, which only sheds after ~150 ms, never caught
  it): a moving filter envelope changed the cutoff every sample, forcing a per-sample
  `SetFreq` (Svf `sinf`+`powf` / Moog polynomial) on every voice — and a chord put all 6
  voices on that path at once, tipping a block past its deadline. The voice filter now
  recomputes its coefficients at **control rate** (every 8 samples, ~6 kHz — inaudible for
  sweeps) while keeping the existing "skip when unchanged" fast path for static patches.
  The audio **block size also goes 48 → 64** (~1.3 ms @ 48 kHz) for more headroom against
  transient spikes.

## [v0.4.0] - 2026-06-25
- **Presets** (`io/presets.h`): three per mode, stored in QSPI. Hold Footswitch 2 to enter
  preset mode, flip Toggle 2 to recall slot 1/2/3 (the slot is the variant position), or tap
  Footswitch 1 (while holding FS2) to save the current sound. Recalled knob values use the
  existing soft-takeover, so physical pots don't jump. LEDs show the active preset
  (right = 1, left = 2, both = 3).
- **Run from SRAM via the Daisy bootloader** (`APP_TYPE=BOOT_SRAM`). The firmware outgrew the
  128 KB internal flash, so it now loads from QSPI into SRAM — hundreds of KB of headroom. Uses
  the fast 10 ms bootloader (near-instant boot). **Flashing changed**: install the bootloader
  once with `scripts/install-bootloader.{sh,ps1}`, then flash the app with
  `scripts/flash.{sh,ps1}` — or let Propagator do it. The wavetables moved to SDRAM to fit the
  SRAM build's smaller default `.bss`.
- **Automated/remote flashing.** **CC 118 ≥ 64** reboots into the Daisy bootloader with an
  infinite DFU window, so Propagator can reflash the app over WebUSB without any button timing.
  (CC 119 still reboots to the STM ROM DFU, now used to update the bootloader itself.) Releases
  also publish `spore-vX.Y.Z-bootloader.bin` so the editor can install the bootloader too.
- **Removed the "hold both footswitches → DFU" gesture.** Enter DFU over MIDI (CC 118 for the
  app, CC 119 for the bootloader) or with the Daisy Seed's BOOT+RESET buttons. Frees the
  footswitch combinations for presets.
- **Lower audio-CPU on the voice path** (no audible change): the per-voice filter only
  recomputes its coefficients when cutoff / resonance / filter-type actually change, so
  static-filter patches skip a `sinf`+`powf` (Svf) or coefficient polynomial (Moog) every
  sample; unison detune offsets are precomputed per block instead of dividing per sample;
  and the final hard clamp is folded into the master limiter (one less buffer pass).
- **Link-time optimization** (`-flto`) trims ~1.5 KB of internal flash (98.2% → 97.1%);
  `usb_identity.c` is kept out of LTO so its USB-descriptor override stays deterministic.
- **CI checks a CHANGELOG entry** on every PR (skippable via a `skip-changelog` /
  `dependencies` label), and the PR template now lists only the manual items CI can't
  verify (formatting, build, and changelog are enforced automatically).
- **Contributor tooling.** Added `CONTRIBUTING.md`, GitHub issue/PR templates, a
  `.clang-format` (Google C++, 4-space, 100-col) + `.editorconfig`, and a CI job that
  enforces formatting. New `scripts/setup.{sh,ps1}` one-shot bootstrap (submodules + libs).
- **Internal cleanup** (no behaviour change): `PumpMidi` takes a `MidiContext` struct
  instead of 11 positional args; per-mode parameter defaults are assigned by enum name so
  reordering can't silently shift them; CPU-watchdog and LED thresholds moved into
  `params::watchdog` / `params::ui`.

## [v0.3.4] - 2026-06-24
- **New Granular engine controls** (`config/gran_params.h`, CC 94-97) for the Propagator
  GRANULAR pod, beyond the 6 physical knobs: **Reverse** (backwards-grain probability, was a
  fixed 30%), **Width** (per-grain stereo pan spread — granular is now stereo), **Shape**
  (grain window soft Hann -> hard flat-top gate), and **Scale-lock** (snap grain pitches to
  off / major / minor / pentatonic). Defaults reproduce the previous sound.

## [v0.3.3] - 2026-06-24
- **Cleaner master output**: a DC blocker (one-pole ~20 Hz high-pass per channel) strips any
  offset built up by resonant filters / wavefolding / drive before it eats headroom, and the
  master volume / cutoff / resonance are now smoothed (~15 ms) so stepped MIDI CC and fast knob
  moves no longer zipper (worst on resonant cutoff).
- **Reverse grains** in Granular: ~30% of grains now play backwards for a shimmering, less
  directional texture.
- **Granular octaves stay in tune.** The octave-quantize (VAR position 2) used `powf(2, k)` for
  the rate, which isn't bit-exact (worse under `-ffast-math`) so octaves beat against the dry
  signal. Now the octave shift is an exact `ldexpf` x2^n, and it's taken relative to the dialed
  Pitch (so the Pitch knob works in octave mode instead of being snapped away).

## [v0.3.2] - 2026-06-24
- **Granular pitch is now in tune.** The pitch knob quantizes to whole semitones (it used to
  sweep +/-24 semitones continuously, so it detuned against anything you played unless dialed
  exactly), with a small center detent that snaps to unison.
- **Smoother short grains.** The grain spawn interval is now randomized (+/-40%) so small grains
  form a cloud instead of a periodic buzz at the spawn rate. (Overlap still depends on Density vs
  Size: very short grains at low density are sparse by nature -- raise Density for a denser wash.)
- **Master limiter instead of a hard clip.** The output stage now rides the gain down when a
  peak would exceed the ceiling (fast ~0.5 ms attack, slow ~150 ms release) and lets it recover
  smoothly, so a resonant filter / stacked grains / runaway feedback get turned *down* rather
  than crushed. A hard clamp to [-1, 1] stays only as an inaudible backstop for the attack slip.
- **Generative no longer locks up on re-seed.** The Brightness/Texture roll could land on a
  timbre too heavy for the budget (Moog 4-pole + unison + FM + fold across all 5 voices), and
  since Generative skips the global FX the CPU watchdog had nothing to shed -> it overran,
  starved MIDI, and needed a reboot. Now: the roll caps its worst case (no unison, Moog rare),
  and the watchdog **sheds voices** in the active mode and **auto-recovers** when load drops
  (~400 ms cool-down) instead of latching until a mode change.
- **Lower CPU across the board** via `-ffast-math -fno-finite-math-only` on the DSP build
  (reassociation / reciprocals / fast fp-contract; NaN+Inf handling kept for the chaos guard
  and CPU meter), plus a cheaper Granular hot loop -- Hann window from a LUT (was a per-grain
  `cosf` every sample) and branch-wrap instead of `%` on the buffer indices.

## [v0.3.1] - 2026-06-19
- **Generative steering** (`config/gen_params.h`, CC 32-37): the timbre still rolls
  **randomly** on re-seed, but the roll is now *biased* by **Brightness** + **Texture**, and
  behaviour is steerable via **Chord** (stacking), **Swell** (note length) and **Motion**
  (walk step), plus a **Wander** morph-depth. Synth params stay synth-only (no reuse).

## [v0.3.0] - 2026-06-19
- **Control echo (device -> editor)**: the firmware now transmits a CC when a hardware
  control changes -- mode/FX/VAR toggles (CC 16/17/93), the 12 shift-layer knobs (CC 20-31),
  and bypass (CC 91) -- so Propagator mirrors the physical surface. Change-detected, sent
  from the main loop (not the audio ISR). First half of live 2-way sync.
- **Master output stage**: a switchable **LP/BP/HP filter** (cutoff + resonance) and a
  **master volume**, applied after the global FX and before the limiter (`fx/master.h`).
  Filter type 0 = off (volume only). CC 7 (vol), 88 (type), 89 (cutoff), 90 (res).
- **MIDI control of the footswitches + VAR switch**: CC 91 = bypass, CC 92 = mode action
  (freeze / re-seed), CC 93 = TOGGLE 2 variant. Modes now read an overridable `ctx.variant`
  instead of reading Toggle 2 directly, so the whole control surface is drivable over MIDI.
- **Fix Generative-mode CPU overload / MIDI lockup.** Generative ran its own `ReverbSc`
  **and** the global FX reverb (double reverb) â†’ the audio callback overran and starved MIDI.
  The global FX path is now skipped in Generative (it has its own reverb).
- **Clear processing on mode change.** New `GlobalFx::Reset()` clears the FX delay/reverb
  tail, and each mode now implements `OnEnter()` to silence its voices/grains/reverb, so
  nothing from the previous mode lingers (or keeps costing CPU) into the next.
- **CPU watchdog.** If the callback stays overloaded (~150 ms), `g_overload` latches: the
  global FX is shed and the onboard LED fast-blinks (~5 Hz). Cleared by a mode/FX change â€”
  i.e. change something to regain control.

## [v0.2.1] - 2026-06-18
- **Steps mod-matrix source** (#8): the logistic-map (stepped) chaos `ChaosStep` is now a
  routable Synth matrix source (was computed but unused). Source encoding widened `*7`â†’`*8`
  (`synth_mode.h`); Propagator gains the jack with a v2â†’v3 patch migration.
- **Live Chaos controls + telemetry**: Lorenz **speed** is now tunable over **CC 18**
  (`ModEngine::SetChaosSpeed`, range in `params::mod`). New SysEx **chaos-state** query
  (`F0 7D 03 F7` â†’ `0x43 <x> <z>`) reports the attractor's X/Z so Propagator can draw it
  live â€” reports the already-computed value, so **no extra audio-CPU cost**.

## [v0.2.0] - 2026-06-18
- **Chaos modulation sources**: a Lorenz attractor (smooth) + logistic map (stepped)
  added to the shared `ModEngine` (`mod/modulation.h`) â€” deterministic-but-never-repeating
  movement. Wired into **Generative** (chaotic filter sway + wavetable-scan drift) and
  **Granular** (density drift). Params in `params::mod`. See `docs/MODULATION.md`.
- **Chaos as a Synth mod-matrix source** (#7): route the Lorenz chaos to any destination
  via the patchbay. Source encoding widened from `*6`â†’`*7` (`synth_mode.h`); Propagator
  gains the matching jack (with a v1â†’v2 patch migration so old routings keep their meaning).

## [v0.1.2] - 2026-06-18
- **Fix reverb crackle**: enable the FPU's flush-to-zero (`FPSCR.FZ`) at boot. The
  Cortex-M7 handles denormal floats on a slow path and libDaisy only enables FPU
  *access*, so a decaying `ReverbSc` tail (no denormal guard of its own) stalled the
  FPU as it rang out. FZ also protects the delay/filter feedback paths.
- **CPU-load metering**: instrument the audio callback with `CpuLoadMeter` and report
  avg/peak load over SysEx (query `F0 7D 02 F7` â†’ reply `F0 7D 42 <avg%> <max%> F7`).
  Propagator shows it live. See `docs/MIDI_PROTOCOL.md`.

## [v0.1.1] - 2026-06-17
- Add version exchange cmd

## [v0.1.0] - 2026-06-17
- First tagged release: multi-mode synth / granular / generative firmware with the
  shared modulation engine, tempo-synced delay + MIDI clock, and analog-sensor input.
- USB device name: Spore now enumerates as **"Spore"** (manufacturer "rainybit")
  instead of "Daisy Seed Built In", via a vendored `src/usb_identity.c` descriptor
  override. VID/PID kept at the stock Daisy/STM values (a real custom VID/PID is a
  one-line edit there, deferred until productization).
- Public-release prep: GPL-3.0 licensing + SPDX headers, build/release CI, README.

<!--
Releasing:  scripts/release.sh vX.Y.Z      (or  scripts\release.ps1 vX.Y.Z  on Windows)
  Add your notes under [Unreleased] above, then run that one command: it moves them
  under a dated [vX.Y.Z] heading, commits, tags, and pushes. CI builds
  spore-vX.Y.Z.{bin,hex,elf} and publishes a Release whose body IS this CHANGELOG section.
-->
