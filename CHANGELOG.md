# Changelog

All notable changes to the Spore firmware are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); this project
uses [Semantic Versioning](https://semver.org/) (`vMAJOR.MINOR.PATCH`).

## [Unreleased]
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
