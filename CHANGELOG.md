# Changelog

All notable changes to the Spore firmware are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); this project
uses [Semantic Versioning](https://semver.org/) (`vMAJOR.MINOR.PATCH`).

## [Unreleased]
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
  **and** the global FX reverb (double reverb) → the audio callback overran and starved MIDI.
  The global FX path is now skipped in Generative (it has its own reverb).
- **Clear processing on mode change.** New `GlobalFx::Reset()` clears the FX delay/reverb
  tail, and each mode now implements `OnEnter()` to silence its voices/grains/reverb, so
  nothing from the previous mode lingers (or keeps costing CPU) into the next.
- **CPU watchdog.** If the callback stays overloaded (~150 ms), `g_overload` latches: the
  global FX is shed and the onboard LED fast-blinks (~5 Hz). Cleared by a mode/FX change —
  i.e. change something to regain control.

## [v0.2.1] - 2026-06-18
- **Steps mod-matrix source** (#8): the logistic-map (stepped) chaos `ChaosStep` is now a
  routable Synth matrix source (was computed but unused). Source encoding widened `*7`→`*8`
  (`synth_mode.h`); Propagator gains the jack with a v2→v3 patch migration.
- **Live Chaos controls + telemetry**: Lorenz **speed** is now tunable over **CC 18**
  (`ModEngine::SetChaosSpeed`, range in `params::mod`). New SysEx **chaos-state** query
  (`F0 7D 03 F7` → `0x43 <x> <z>`) reports the attractor's X/Z so Propagator can draw it
  live — reports the already-computed value, so **no extra audio-CPU cost**.

## [v0.2.0] - 2026-06-18
- **Chaos modulation sources**: a Lorenz attractor (smooth) + logistic map (stepped)
  added to the shared `ModEngine` (`mod/modulation.h`) — deterministic-but-never-repeating
  movement. Wired into **Generative** (chaotic filter sway + wavetable-scan drift) and
  **Granular** (density drift). Params in `params::mod`. See `docs/MODULATION.md`.
- **Chaos as a Synth mod-matrix source** (#7): route the Lorenz chaos to any destination
  via the patchbay. Source encoding widened from `*6`→`*7` (`synth_mode.h`); Propagator
  gains the matching jack (with a v1→v2 patch migration so old routings keep their meaning).

## [v0.1.2] - 2026-06-18
- **Fix reverb crackle**: enable the FPU's flush-to-zero (`FPSCR.FZ`) at boot. The
  Cortex-M7 handles denormal floats on a slow path and libDaisy only enables FPU
  *access*, so a decaying `ReverbSc` tail (no denormal guard of its own) stalled the
  FPU as it rang out. FZ also protects the delay/filter feedback paths.
- **CPU-load metering**: instrument the audio callback with `CpuLoadMeter` and report
  avg/peak load over SysEx (query `F0 7D 02 F7` → reply `F0 7D 42 <avg%> <max%> F7`).
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
