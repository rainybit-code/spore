# Ideas & Backlog

Forward-looking backlog for Spore — open ideas and parked work, tagged by priority.
Shipped features live in [`CHANGELOG.md`](../CHANGELOG.md), not here.

Legend: 🔜 next · 🅰️ tier-1 (core) · 🅱️ tier-2 (deferred) · 🅲 tier-3 (ambitious) · 💡 idea

---

## Inputs

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
    Daisy can be the **receiver directly over SPI** (no bridge MCU on the device).
    Top = small MCU + IMU + nRF24 + LiPo (e.g. a Seeed XIAO).
  - **ESP-NOW** — ESP32 in the top + ESP32 on the device, bridged to Daisy over
    UART. Very simple, good latency, but adds a chip on the device side.
  - **BLE** — XIAO nRF52840 *Sense* (IMU built in) is tidy on the top side, but
    Daisy can't easily be a BLE central → needs a receiver module. Most work.
- **Mapping (the "wobble"):** spin rate → modulation rate/intensity; precession /
  nutation (wobble) → a wandering LFO that naturally grows as the top slows;
  topple/stop → gesture trigger or mode nudge.
- **Architecture fit:** slots into `io/` like the IMU — a `WirelessSensor` feeding
  `ModContext`. The firmware is already shaped for an extra modulation source.

## Modes & sound

- 🔜 **Master output stage: filter + volume** — a global LP/BP/HP filter (one
  `daisysp::Svf` gives all three outputs → type-select) + cutoff/res, then a master
  **volume**, placed after the global FX and before the limiter in `main.cpp`'s callback.
  CC-controlled (+ Propagator knobs/segment). Cheap; the SVF outputs map straight to LP/BP/HP.
- 💡 **More FX / FX modes** — shimmer (pitch-shifted reverb), modulated/chorus delay,
  ping-pong, per-mode FX presets, or a second FX slot (delay → reverb in series).
- 💡 **More modes** — implement `IMode` (`modes/mode.h`) and add to the array in
  `main.cpp`. Candidates: drone/oscillator bank, resonator, wavefolder, sample looper.
- 💡 **Mod-matrix extensions** — let a wireless source and the Generative seed wire
  the matrix slots (the 6-slot matrix itself exists — see [`MODULATION.md`](MODULATION.md)).
- 💡 **Granular v2** — stereo grains, panning spread, reverse grains, pitch-quantize
  to a scale, grain-density envelope.
- 💡 **Generative v2** — Euclidean rhythm engine, probabilistic gates, multiple
  voices, chord/scale-aware walks.

## Connectivity & management (USB MIDI ↔ browser)

Full protocol spec: [`MIDI_PROTOCOL.md`](MIDI_PROTOCOL.md).

- 🔜 **MIDI control of the footswitches + VAR switch** — CCs for FOOTSW 1 (engage/bypass),
  FOOTSW 2 (mode action: freeze / re-seed), and TOGGLE 2 (per-mode variant). FS1/FS2 are a
  CC handler in `midi_in.h` (needs access to `g_bypass` + the active mode's `Action()`);
  the VAR switch is cleanest as an overridable `variant` on `ModContext` (like `g_modeSel`),
  so modes read `ctx.variant` instead of `TogglePos(TOGGLESWITCH_2)` directly. + Propagator
  buttons/segment so the whole control surface is drivable from the editor.
- 🔜 **2-way sync (SysEx)** — full patch dump/load so the editor mirrors the device.
  Needs the central `Patch` store.
- 🔜 **Preset librarian** — patches saved in the browser (JSON/localStorage) **and**
  on the device in **QSPI** (`PersistentStorage`) so they survive power-off.
- 🅲 **Sample loading** — upload an audio sample over chunked SysEx into **QSPI**
  (8 MB), read back memory-mapped, played by a sample-player source (feeds granular
  or a new mode). Biggest piece; the real driver for using QSPI as data storage
  (note: data in QSPI does NOT require `BOOT_QSPI` — code can stay in internal flash).
- 🅲 **USB audio (UAC, composite with MIDI)** — make the device a USB audio
  interface (record dry/wet into a DAW, use as a computer FX processor). Feasible:
  the STM32H750 USB is full-speed (stereo 48k/24-bit fits) and the ST **AUDIO**
  class + **CompositeBuilder** source is already vendored in libDaisy — but libDaisy
  doesn't wire it up (it only wraps CDC/MIDI/MSC). Build-it-yourself: add the
  AudioControl/AudioStreaming descriptors, an `usbd_audio_if` implementation, a ring
  buffer between the SAI callback and USB endpoints, and an **async feedback endpoint**
  to sync the codec clock to USB SOF (the hard part — drift/clicks otherwise), all
  composed with MIDIStreaming. Substantial + experimental; can only be tuned on
  hardware; adds buffering latency vs the analog jacks. Not needed for standalone
  use (the Hothouse has analog stereo I/O).
- 💡 **protobuf for the protocol** — instead of hand-rolled SysEx byte layouts, define
  messages in `.proto` and codegen both ends (**nanopb** on firmware, **protobuf.js**
  in the browser); frame the encoded bytes in SysEx (7→8-bit). Schema-driven, versioned,
  less manual parsing. Parked — adds a dependency + codegen step; revisit once the
  message set stabilizes.

## Workflow & quality of life

- 🔜 **Build-verify on hardware** — install ARM toolchain, `scripts/build-libs`,
  then `scripts/flash`. Confirm audio passthrough + each mode.
- 💡 **Preset save/recall** — persist the live `Patch` to QSPI flash
  (`PersistentStorage`); footswitch combo or a knob to select slots.
- 💡 **LED feedback** — richer status (blink on generative triggers, freeze
  indicator, mode color/brightness language).
- 💡 **Pure Data prototyping** — sketch new DSP/generative ideas in `pd/` first,
  then port to a mode.

## Performance / CPU headroom — parked levers

Audio runs a **48-sample block @ 48 kHz** → ~1 ms / ~480k cycles per callback on the
M7. Shipped this round: **FPU flush-to-zero** (`FPSCR.FZ` in `main.cpp` — fixed the
reverb-tail denormal crackle) and a **`CpuLoadMeter`** reported over SysEx `0x02`/`0x42`
and shown live in Propagator. **Measure with that meter before/after any of the below.**

- 🔜 **`-ffast-math` and/or `-O3`** *(next experiment — try with the meter)*. Currently
  `-O2` (libDaisy default). Relaxed float math is typically a few-to-~15 % on this DSP;
  rounding differs slightly but inaudible here. Add to `CFLAGS`/`OPT` in the project
  `Makefile`; A/B with the CPU meter. Safer subset: `-fno-math-errno -ffp-contract=fast`.
- 🔜 **Bigger audio block** *(next experiment)*. `params::audio::kBlockSize` 48 → 64/96
  amortizes per-block fixed overhead (control reads, LFO ticks, call setup). Cost is
  latency: 48→96 = 1 ms→2 ms (still low). One-line change.
- 💡 **Cap voices / unison.** Synth's dominant cost is oscillators: 6 voices × up to
  4 unison + sub ≈ 30 osc/sample worst case (`dsp/voice.h`). Lower the max, or auto-reduce
  when the global reverb is engaged. Linear CPU savings.
- 💡 **Cheaper filter / oscillator per patch.** `MoogLadder` ≈ 2× the `Svf`;
  `WAVE_POLYBLEP_*` pays for anti-aliasing the wavetable engine already bakes in.
  Defaulting toward the cheaper option where a patch allows trims per-voice cost.
- 🅱️ **ITCM hot-loop placement** — *do this with a device in hand* (boot-copy bug =
  hard-fault, unverifiable blind). The linker script has **no `.itcm_text` section**, so
  it needs: a vendored copy of `STM32H750IB_flash.lds` (point `LDSCRIPT` at it from the
  Makefile — `?=`, same no-submodule-edit trick as `usb_identity.c`), an `.itcm_text`
  section `> ITCMRAM AT > FLASH`, a boot-time copy loop in `main()` (startup only inits
  `.data`/`.bss`), and a `section(".itcm_text")` attr on the hot fn (e.g. `Voice::Process`,
  forced non-inline). NOTE: **DTCM data is *not* worth it** — libDaisy's own
  `daisy_core.h` says DTCM is "on par with internal SRAM w/ cache enabled", and the
  wavetables already live in cached SRAM; DTCM also shares the 128 KB region with the
  stack. ITCM *code* is the only placement lever with a real (jitter) upside here.

## Memory / boot layout — parked until flash gets tight

Currently `APP_TYPE = BOOT_NONE`: the app runs from the **128 KB internal flash**
(~94% today — delay + reverb only cost ~2 KB of flash because ReverbSc's big
buffer lives in SDRAM). Keep it this way until code actually outgrows 128 KB — no
bootloader, simplest flashing. Note: **preset-save does NOT require leaving
internal flash** (`PersistentStorage` keeps *data* in QSPI regardless of where
code runs).

When code outgrows 128 KB, libDaisy offers (set `APP_TYPE` + flash the Daisy
bootloader once via `make program-boot`):
- 🅱️ **`BOOT_SRAM`** — runs from AXI SRAM, **~512 KB**, full speed (no XIP jitter).
  *First choice* when we just need more room. Bootloader reloads app at power-up.
- 🅲 **`BOOT_QSPI`** — **execute-in-place from the 8 MB QSPI**, ~8 MB code space.
  M7 I-cache hides most of the QSPI latency (proven on shipping products) but can
  jitter on cold/large code paths; slower boot. *Reserve for when we want
  megabytes in flash* — e.g. baked-in wavetables / sample buffers for the
  granular & sample-playback ideas. That's the real future driver here.

Switching is a one-line `APP_TYPE` change in the `Makefile` + `dfu-util`.

- 💡 Bring expansion pins out cleanly — small header or jack(s) for the analog
  sensor / I2C IMU / wireless receiver; enclosure grommet for cabling.
- 💡 Decide mono vs stereo build to match the purchased Hothouse variant.
