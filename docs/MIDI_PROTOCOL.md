# MIDI management protocol (USB MIDI ↔ WebMIDI tool)

The firmware is a USB MIDI device; a browser tool (WebMIDI) configures it, manages
presets, and uploads samples. This is the contract both sides build against.

> **Status legend:** ✅ implemented · 🔜 specced, not built yet

WebMIDI notes: use a **Chromium** browser (Chrome/Edge). SysEx needs a **secure
context** (https or localhost) + a permission prompt. The pedal is only reachable
while plugged into the computer running the browser.

Manufacturer SysEx ID: **`0x7D`** (the reserved "non-commercial / educational" ID —
correct for a DIY device). All SysEx frames: `F0 7D <cmd> <payload…> F7`.

---

## 1. Live parameter control — Control Change ✅ (Phase 1, in firmware)

CC value `0..127` maps to the normalized `0..1` knob value and is applied with
**soft-takeover**: the value sticks until the matching physical knob is moved to it
(motorized-fader feel). What "knob N" means depends on the active mode (see
`config/params.h`).

| CC#    | Target                          |
|--------|---------------------------------|
| 16     | Mode select (0/64/127 → synth / granular / generative) |
| 17     | FX select (0/64/127 → off / delay / reverb) |
| 20–25  | MODE-layer knobs 1–6 (active mode's macros) |
| 26–31  | FX-layer knobs 1–6 (mix / delay time / fb / tone / rev decay / damp) |
| 40–53  | Synth panel params (`SP_*`, see `config/synth_params.h`) |

Synth-panel CCs (CC `kCcSynthBase` + index): 40 detune · 41 sub · 42 sustain ·
43 release · 44 f.env amt · 45 f.env time · 46 glide · 47 width · 48 wave · 49 LFO
rate · 50 LFO depth · 51 LFO shape · 52 LFO dest · **53 voices** (0..1 → 1..6;
fewer voices = more CPU headroom for reverb/LFO, 1 = mono).

Notes (NoteOn/NoteOff) play the synth voice as before.

> Resolution is 7-bit. Params that want finer control can later use 14-bit NRPN;
> the canonical full-resolution path is the SysEx patch dump/load below.

## 2. Device handshake — SysEx 🔜

- `0x01` **Identify request** (web→pedal), no payload.
- `0x41` **Identify reply** (pedal→web): firmware name + version + protocol rev +
  capabilities (num modes, num knobs, sample slots, free QSPI bytes).

## 3. Full patch dump / load — SysEx 🔜 (Phase 2 → enables presets)

A "patch" = every live value: MODE-layer knobs ×6, FX-layer knobs ×6, mode select,
FX select, and (later) mod/sensor routing. Encoded 14-bit per value (two 7-bit
bytes) so there's no resolution loss.

- `0x10` **Dump request** (web→pedal) → pedal replies `0x50` with the full patch.
- `0x50` **Patch dump** (pedal→web).
- `0x11` **Patch load** (web→pedal): set all live values at once.

This is what keeps the web UI in sync with the pedal, and is the unit a preset
stores.

## 4. Presets — SysEx + QSPI 🔜 (Phase 3, needs hardware)

Presets are patches (§3). Two stores:
- **Browser librarian**: presets saved as JSON in `localStorage` / exported files.
- **On-pedal**: saved to **QSPI flash** (`daisy::PersistentStorage` / `QSPIHandle`)
  so they survive power-off and recall without the computer.

- `0x20` **Save to slot** `N` (pedal writes current patch to QSPI slot N).
- `0x21` **Recall slot** `N` (pedal loads slot N; also emits a `0x50` dump so the
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
  Pedal erases the QSPI region and replies `0x40` ACK (ready).
- `0x31` **Chunk**: 16-bit sequence + encoded data. Pedal writes to QSPI, replies
  `0x40` ACK(seq) or NAK(seq) for retransmit.
- `0x32` **Upload end**: total checksum. Pedal verifies, commits, replies `0x40`.
- `0x40` **ACK/NAK**: seq + status (used for flow control / resend).

---

## Build phases

1. ✅ **Live control (CC)** — firmware done; needs the web page with sliders.
2. 🔜 **Handshake + patch dump/load (SysEx)** — 2-way sync; central `Patch` store.
3. 🔜 **Presets** — QSPI save/recall on pedal + browser librarian.
4. 🔜 **Sample upload** — chunked SysEx → QSPI + a sample-player source.

The web tool ("Propagator") lives in a **separate repo: `propagator-web`** (static;
run via `python -m http.server` on localhost; deploy to GitHub Pages later). This
file is the contract it builds against.
