# MIDI management protocol (USB MIDI ↔ WebMIDI tool)

The firmware is a USB MIDI device; a browser tool (WebMIDI) configures it, manages
presets, and uploads samples. This is the contract both sides build against.

> **Status legend:** ✅ implemented · 🔜 specced, not built yet

WebMIDI notes: use a **Chromium** browser (Chrome/Edge). SysEx needs a **secure
context** (https or localhost) + a permission prompt. The device is only reachable
while plugged into the computer running the browser.

Manufacturer SysEx ID: **`0x7D`** (the reserved "non-commercial / educational" ID —
correct for a DIY device). All SysEx frames: `F0 7D <cmd> <payload…> F7`.

---

## 1. Live parameter control — Control Change ✅ (in firmware)

CC value `0..127` maps to the normalized `0..1` parameter value. The 6 MODE/FX
knobs are applied with **soft-takeover** (the value sticks until the matching
physical knob is moved to it); synth-panel params apply immediately. What "knob N"
means depends on the active mode. All numbers are defined in
[`config/params.h`](../src/config/params.h) (`namespace midi`).

| CC#    | Target                          |
|--------|---------------------------------|
| 14     | Internal clock **tempo** (0..127 → 40..200 BPM) — see §1a |
| 15     | **Delay sync** division (0 off · 1 = 1/4 · 2 = 1/8 · 3 = dotted-1/8 · 4 = 1/16) |
| 16     | Mode select (thirds: 0..42 synth / 43..85 granular / 86..127 generative) |
| 17     | FX select (thirds: off / delay / reverb) |
| 18     | **Chaos speed** (Lorenz evolution rate, 0..127 → slow…busy) — global |
| 20–25  | MODE-layer knobs 1–6 (active mode's macros, soft-takeover) |
| 26–31  | FX-layer knobs 1–6 (mix / delay time / fb / tone / rev decay / damp) |
| 40–87  | Synth panel params (`SP_*`, `kCcSynthBase + index`; full list below) |
| 119    | Reboot to **DFU** bootloader when value ≥ 64 (remote flashing) |

**Synth-panel CCs** (`CC = 40 + SP_* index`, see
[`config/synth_params.h`](../src/config/synth_params.h)):

| CC | param | CC | param |
|----|-------|----|-------|
| 40 | detune | 60 | drive |
| 41 | sub level | 61 | filter type (Svf / Moog) |
| 42 | sustain | 62 | unison |
| 43 | release | 63 | sub octave |
| 44 | filter-env amount | 64 | sub waveform |
| 45 | filter-env time | 65 | LFO2 rate |
| 46 | glide | 66 | LFO2 shape |
| 47 | stereo width | 67–69 | matrix slot 1 (src / dst / amt) |
| 48 | waveform | 70–72 | matrix slot 2 |
| 49 | LFO1 rate | 73–75 | matrix slot 3 |
| 50 | LFO1 depth | 76–78 | matrix slot 4 |
| 51 | LFO1 shape | 79–81 | matrix slot 5 |
| 52 | LFO1 dest | 82–84 | matrix slot 6 |
| 53 | voices (1..6) | 85 | LFO2 depth |
| 54 | engine (analog / wavetable) | 86 | LFO1 sync |
| 55 | WT scan | 87 | LFO2 sync |
| 56 | FM amount | | |
| 57 | FM ratio | | |
| 58 | wavefold | | |
| 59 | WT bank | | |

Mod-matrix source/destination lists and encoding are in
[`MODULATION.md`](MODULATION.md). NoteOn/NoteOff play the synth voice.

> Resolution is 7-bit. Params that want finer control can later use 14-bit NRPN;
> the canonical full-resolution path is the SysEx patch dump/load below.

## 1a. Tempo & transport — MIDI clock ✅ (in firmware)

The device runs a local clock that **free-runs** at an internal BPM but **locks to
incoming MIDI clock** when it arrives (≈500 ms timeout back to internal). Either the
browser or an external MIDI source can be master.

- **`0xF8` Timing Clock** (24 ppqn) → drives/locks the tempo.
- **`0xFA` Start · `0xFB` Continue · `0xFC` Stop** → transport.
- **CC 14** sets the internal/free-run BPM (40..200) when no external clock is present.

Tempo feeds the synced delay (CC 15 division) and the clock-synced LFO rates
(`SP_LFO_SYNC` / `SP_LFO2_SYNC`).

## 2. Device handshake — SysEx ◐ (identify/version ✅)

- `0x01` **Identify request** (web→device), no payload: `F0 7D 01 F7`.
- `0x41` **Identify reply** (device→web): `F0 7D 41 <version ASCII> F7` — the firmware
  version (semver, no leading `v`), e.g. `F0 7D 41 30 2E 31 2E 30 F7` = `"0.1.0"`.
  Propagator uses this to flag when a newer release is available (pulsing the DFU
  button). 🔜 Fuller capabilities (protocol rev, mode/knob/slot counts, free QSPI)
  can extend the same reply later.
- `0x02` **CPU-load query** (web→device), no payload: `F0 7D 02 F7`.
- `0x42` **CPU-load reply** (device→web): `F0 7D 42 <avg%> <max%> F7` — the
  audio-callback load from libDaisy's `CpuLoadMeter`, two bytes each `0..127`
  (percent, capped; `0` before the first audio block). Propagator polls this once a
  second and shows `CPU <avg>% · peak <max>%` in the MIDI monitor (peak ≥ 90 % turns
  red — at risk of dropouts). A diagnostic for judging headroom while tuning patches.
- `0x03` **Chaos-state query** (web→device), no payload: `F0 7D 03 F7`.
- `0x43` **Chaos-state reply** (device→web): `F0 7D 43 <x> <z> F7` — the Lorenz
  attractor's X/Z pair (the classic butterfly projection), each `-1..1` mapped to
  `0..127`. The chaos already runs every block (it's the mod engine), so this just
  reports the cached value — **no extra audio-CPU cost**. Propagator polls it (~20 Hz,
  only while the attractor canvas is visible) to draw the live chaos in the MOD pod.

## 3. Full patch dump / load — SysEx 🔜 (Phase 2 → enables presets)

A "patch" = every live value: MODE-layer knobs ×6, FX-layer knobs ×6, mode select,
FX select, and (later) mod/sensor routing. Encoded 14-bit per value (two 7-bit
bytes) so there's no resolution loss.

- `0x10` **Dump request** (web→device) → device replies `0x50` with the full patch.
- `0x50` **Patch dump** (device→web).
- `0x11` **Patch load** (web→device): set all live values at once.

This is what keeps the web UI in sync with the device, and is the unit a preset
stores.

## 4. Presets — SysEx + QSPI 🔜 (Phase 3, needs hardware)

Presets are patches (§3). Two stores:
- **Browser librarian**: presets saved as JSON in `localStorage` / exported files.
- **On-device**: saved to **QSPI flash** (`daisy::PersistentStorage` / `QSPIHandle`)
  so they survive power-off and recall without the computer.

- `0x20` **Save to slot** `N` (device writes current patch to QSPI slot N).
- `0x21` **Recall slot** `N` (device loads slot N; also emits a `0x50` dump so the
  UI follows). Program Change `N` is an alias for recall.
- `0x22` **List slots** → reply `0x52` with slot names/occupancy.

## 5. Sample upload — SysEx + QSPI 🔜 (Phase 4, biggest; needs hardware)

Stream an audio sample into **QSPI** (8 MB) for a sample-player source (feeds the
granular engine or a dedicated sample mode). The sample is read back via
**memory-mapped QSPI** (`QSPIHandle::GetData`) so playback reads it like RAM.

USB-MIDI is **not** limited to the 31.25 kbps of legacy DIN MIDI — it's USB bulk,
so a few hundred KB transfers in seconds. Constraints:
- libDaisy's inbound SysEx buffer is **128 bytes**, so data is chunked (~96 audio
  bytes/packet after 7→8-bit encoding and framing), each chunk ACKed.
- SysEx bytes must be 0–127, so 8-bit audio is sent **7-to-8 encoded** (7 data
  bytes → 8 SysEx bytes carrying the high bits).

Flow:
- `0x30` **Upload begin**: slot, total bytes, sample rate, channels, format.
  Device erases the QSPI region and replies `0x40` ACK (ready).
- `0x31` **Chunk**: 16-bit sequence + encoded data. Device writes to QSPI, replies
  `0x40` ACK(seq) or NAK(seq) for retransmit.
- `0x32` **Upload end**: total checksum. Device verifies, commits, replies `0x40`.
- `0x40` **ACK/NAK**: seq + status (used for flow control / resend).

---

## Build phases

1. ✅ **Live control (CC) + MIDI clock** — firmware and the full Propagator editor
   are done (synth panel, mod matrix, sequencer, tempo/clock).
2. 🔜 **Handshake + patch dump/load (SysEx)** — 2-way sync; central `Patch` store.
3. 🔜 **Presets** — QSPI save/recall on device + browser librarian.
4. 🔜 **Sample upload** — chunked SysEx → QSPI + a sample-player source.

The web tool, **Propagator**, lives in a separate repo
(<https://github.com/rainybit-code/propagator>, live at
<https://rainybit-code.github.io/propagator/>). This file is the contract it builds
against.
