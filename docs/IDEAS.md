# Ideas & Backlog

Running list of ideas for the Daisy Seed + Hothouse synth. Tagged by status so
it's easy to see what's done vs. what's parked.

Legend: ✅ done · 🔜 next · 🅰️ tier-1 (core) · 🅱️ tier-2 (deferred) · 🅲 tier-3 (ambitious) · 💡 idea

---

## Inputs

- ✅ **Built-in audio in** — the Hothouse has stereo 1/4" in & out. Granular mode
  records and reuses it; bypass passes it through. (Synth & Generative ignore it.)
- ✅ **Analog sensor input** — real ADC channel on free pin **A0/D15** (`io/sensors.h`).
  Works with LDR / FSR / flex / expression pedal / CV via a voltage divider.
- 💡 **More analog inputs** — A9/D24 and A11/D28 are also free; bump
  `AnalogSensors::kNumSensors` and add pins. Could host an expression-pedal jack
  or a couple of light/touch sensors at once.
- 🅱️ **Motion IMU over I2C (D11/D12)** — deferred (tier 2). `io/imu.h` is a ready
  slot. Tilt → filter sweep, shake → grain spray / added randomness.
- 🅲 **Wireless "spinning top" modulator** — a battery puck (MCU + 6-axis IMU +
  radio) you spin to bring the sound alive; as it slows it precesses/wobbles
  (deepening modulation) then topples (a trigger). A physical, decaying,
  generative control source. See "Wireless accessory" below.

## Wireless accessory (spinning top) — design notes

- Daisy has **no built-in radio** → needs a link.
- **Transport options (recommended first):**
  - **nRF24L01+ (2.4 GHz, SPI)** — *recommended*. Cheap (~$2/pair), low latency,
    Daisy can be the **receiver directly over SPI** (no bridge MCU on the pedal).
    Top = small MCU + IMU + nRF24 + LiPo (e.g. a Seeed XIAO).
  - **ESP-NOW** — ESP32 in the top + ESP32 on the pedal bridged to Daisy over
    UART. Very simple, good latency, but adds a chip on the pedal side.
  - **BLE** — XIAO nRF52840 *Sense* (IMU built in) is tidy on the top side, but
    Daisy can't easily be a BLE central → needs a receiver module. Most work.
- **Mapping (the "wobble"):** spin rate → modulation rate/intensity; precession /
  nutation (wobble) → a wandering LFO that naturally grows as the top slows;
  topple/stop → gesture trigger or mode nudge.
- **Architecture fit:** slots into `io/` like the IMU — a `WirelessSensor` feeding
  `ModContext`. The firmware is already shaped for an extra modulation source.

## Modes & sound

- ✅ **Synth** (USB MIDI), **Granular** (with freeze), **Generative/Krell** (self-play).
- ✅ **Global FX block** — decoupled delay + reverb (`fx/effects.h`), selected by
  Toggle 3, edited via the Footswitch-1 shift-layer (soft-takeover). Processes the
  active mode's output.
- 💡 **More FX / FX modes** — shimmer (pitch-shifted reverb), modulated/chorus delay,
  ping-pong, per-mode FX presets, or a second FX slot (delay → reverb in series).
- 💡 More modes are easy — implement `IMode` (`modes/mode.h`) and add to the array
  in `main.cpp`. Candidates: drone/oscillator bank, resonator, wavefolder,
  sample looper.
- 💡 **Mod routing matrix** — make source→destination→depth assignments data-driven
  (extend `config/params.h`) so any input (knob/sensor/LFO/wireless) can drive any
  parameter without code changes.
- 💡 **Granular v2** — stereo grains, panning spread, reverse grains, pitch-quantize
  to a scale, grain-density envelope.
- 💡 **Generative v2** — Euclidean rhythm engine, probabilistic gates, multiple
  voices, chord/scale-aware walks, tempo from tap or MIDI clock.

## Connectivity & management (USB MIDI ↔ browser)

Full protocol spec: [`MIDI_PROTOCOL.md`](MIDI_PROTOCOL.md).

- ✅ **Live param control over CC** — the pedal is a USB MIDI device; CC 20–31 drive
  the live knob values (soft-takeover). Built in firmware (`io/midi_in.h`).
- 🔜 **WebMIDI editor** — static page in `tools/webmidi-editor/` (run on localhost,
  GitHub Pages later) with sliders/dropdowns. Chromium only; SysEx needs https/localhost.
- 🔜 **2-way sync (SysEx)** — device identify + full patch dump/load so the UI
  mirrors the pedal. Needs the central `Patch` store (see mod-matrix idea above).
- 🔜 **Preset librarian** — patches saved in the browser (JSON/localStorage) **and**
  on the pedal in **QSPI** (`PersistentStorage`) so they survive power-off.
- 🅲 **Sample loading** — upload an audio sample over chunked SysEx into **QSPI**
  (8 MB), read back memory-mapped, played by a sample-player source (feeds granular
  or a new mode). Biggest piece; the real driver for using QSPI as data storage
  (note: data in QSPI does NOT require `BOOT_QSPI` — code can stay in internal flash).
- 💡 **protobuf for the protocol** — instead of hand-rolled SysEx byte layouts, define
  messages in `.proto` and codegen both ends (**nanopb** on firmware, **protobuf.js**
  in the browser); frame the encoded bytes in SysEx (7→8-bit). Schema-driven, versioned,
  less manual parsing. Parked — adds a dependency + codegen step; revisit once the
  message set stabilizes.

## Workflow & quality of life

- ✅ Centralized tunables in `config/params.h`; build/flash scripts; VS Code tasks.
- 🔜 **Build-verify on hardware** — install ARM toolchain, `scripts/build-libs`,
  then `scripts/flash`. Confirm audio passthrough + each mode (see plan
  Milestones / Verification).
- 💡 **Preset save/recall** — persist the live `Patch` to QSPI flash
  (`PersistentStorage`); footswitch combo or a knob to select slots.
- 💡 **MIDI CC control** — map incoming CCs to parameters (pairs well with the mod
  matrix); MIDI clock sync for generative timing.
- 💡 **LED feedback** — richer status (blink on generative triggers, freeze
  indicator, mode color/brightness language).
- 💡 **Pure Data prototyping** — sketch new DSP/generative ideas in `pd/` first,
  then port to a mode.

## Memory / boot layout — parked until flash gets tight

Currently `APP_TYPE = BOOT_NONE`: the app runs from the **128 KB internal flash**
(~84% today — delay + reverb only cost ~2 KB of flash because ReverbSc's big
buffer lives in SDRAM). Keep it this way until code actually outgrows 128 KB — no
bootloader, simplest flashing. Note: **preset-save does NOT require leaving
internal flash** (`PersistentStorage` keeps *data* in QSPI regardless of where
code runs).

When code outgrows 128 KB, libDaisy offers (set `APP_TYPE` + flash the Daisy
bootloader once via `make program-boot`):
- 🅱️ **`BOOT_SRAM`** — runs from AXI SRAM, **~512 KB**, full speed (no XIP jitter).
  *First choice* when we just need more room. Bootloader reloads app at power-up.
- 🅲 **`BOOT_QSPI`** — **execute-in-place from the 8 MB QSPI**, ~8 MB code space.
  M7 I-cache hides most of the QSPI latency (proven on shipping pedals) but can
  jitter on cold/large code paths; slower boot. *Reserve for when we want
  megabytes in flash* — e.g. baked-in wavetables / sample buffers for the
  granular & sample-playback ideas. That's the real future driver here.

Switching is a one-line `APP_TYPE` change in the `Makefile` + `dfu-util`.

- 💡 Bring expansion pins out cleanly — small header or jack(s) for the analog
  sensor / I2C IMU / wireless receiver; enclosure grommet for cabling.
- 💡 Decide mono vs stereo build to match the purchased Hothouse variant.
