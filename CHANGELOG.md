# Changelog

All notable changes to the Spore firmware are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); this project
uses [Semantic Versioning](https://semver.org/) (`vMAJOR.MINOR.PATCH`).

## [Unreleased]
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
