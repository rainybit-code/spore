# Changelog

All notable changes to the Spore firmware are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); this project
uses [Semantic Versioning](https://semver.org/) (`vMAJOR.MINOR.PATCH`).

## [Unreleased]

## [v0.1.0] - 2026-06-17
- First tagged release: multi-mode synth / granular / generative firmware with the
  shared modulation engine, tempo-synced delay + MIDI clock, and analog-sensor input.
- USB device name: the pedal now enumerates as **"Spore"** (manufacturer "rainybit")
  instead of "Daisy Seed Built In", via a vendored `src/usb_identity.c` descriptor
  override. VID/PID kept at the stock Daisy/STM values (a real custom VID/PID is a
  one-line edit there, deferred until productization).
- Public-release prep: GPL-3.0 licensing + SPDX headers, build/release CI, README.

<!--
Releasing:
  1. Move the Unreleased items under a new `## [vX.Y.Z] - YYYY-MM-DD` heading.
  2. Commit, then tag:  git tag -a vX.Y.Z -m "vX.Y.Z"  &&  git push origin vX.Y.Z
  3. The `firmware` workflow builds and attaches spore-vX.Y.Z.{bin,hex,elf} to the Release.
-->
