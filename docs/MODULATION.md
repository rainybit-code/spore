# Modulation

The synth voice has fixed per-note modulation (filter envelope -> cutoff, velocity
-> level/brightness) plus a flexible **mod matrix** for movement.

## LFOs
- **LFO1** — global LFO with its own quick macro destination (off / vibrato /
  filter / tremolo), set in the MOD pod; also usable as a matrix source.
- **LFO2** — a second global LFO (rate + shape + master depth), a matrix source.

Each LFO can run **free (Hz)** or **clock-synced** to the tempo, selected per-LFO via
`SP_LFO_SYNC` / `SP_LFO2_SYNC` (0 = free Hz, 1..5 → 1/1, 1/2, 1/4, 1/8, 1/16). The
clock is the local tempo, which locks to MIDI clock — see
[`MIDI_PROTOCOL.md` §1a](MIDI_PROTOCOL.md).

## Chaos sources
Deterministic-but-never-repeating modulation, shared via `ModEngine` (`mod/modulation.h`),
advanced once per block alongside the LFOs:
- **Lorenz attractor** (`ChaosX` / `ChaosY`) — a smooth chaotic orbit; organic, structured
  wander that's livelier than a random LFO. `ChaosX` and `ChaosY` are a decorrelated pair.
- **Logistic map** (`ChaosStep`) — stepped chaos (`x' = r·x·(1-x)`); like a sample & hold
  but with hidden banding/period-doubling structure that pure random lacks.

Rates/parameters live in `params::mod` (`kChaosSpeed`, `kLogisticHz`, `kLogisticR`). The
Lorenz **speed** is live-controllable over **CC 18** (`SetChaosSpeed`, range
`kChaosSpeedMin..Max`). Wired into **Generative** (chaotic filter sway + wavetable-scan
drift, scaled by the Drift knob), **Granular** (density drift), and the **Synth** matrix as
source **7** "Chaos" (`ChaosX`, smooth) and source **8** "Steps" (`ChaosStep`, stepped).
Propagator can also draw the live attractor: it polls the device's
X/Z via SysEx `0x03`→`0x43` (~20 Hz, only while the canvas is visible — see
[`MIDI_PROTOCOL.md` §2](MIDI_PROTOCOL.md)).

## Mod matrix (6 slots)
Each slot routes one **source** to one **destination** with a **bipolar amount**
(-1..+1). Slots sum per destination; the result is applied at block rate. In the
Propagator editor the 6 slots are the patchbay cables. Global sources apply to every
voice; the two **per-voice** sources (Velocity, Key) apply to dests 0–5 per note.

| # | Source (`src = round(v*8)`) | scope | # | Destination (`dst = round(v*8)`) |
|---|------------------------------|-------|---|-----------------------------------|
| 0 | Off                          | —     | 0 | Cutoff |
| 1 | LFO1                         | global| 1 | Pitch |
| 2 | LFO2                         | global| 2 | WT-Scan |
| 3 | Random (S&H)                 | global| 3 | Drive |
| 4 | Sensor (analog input)        | global| 4 | Sub level |
| 5 | Velocity                     | per-voice | 5 | FM amount |
| 6 | Key (note, centred at C4)    | per-voice | 6 | Amp (tremolo) |
| 7 | Chaos (Lorenz, smooth)       | global| 7 | LFO1 rate |
| 8 | Steps (logistic, stepped)    | global| 8 | LFO2 rate |

Applied as: cutoff / pitch / amp are trims folded into the existing LFO1 routing;
scan / drive / sub / FM and the LFO-rate dests are additive offsets clamped to 0..1.

## MIDI / params
Extends [`config/synth_params.h`](../src/config/synth_params.h) (CC base 40):

```
SP_LFO2_RATE  (CC 65)   SP_LFO2_SHAPE (CC 66)
SP_M1_SRC/DST/AMT (CC 67-69)   SP_M4_SRC/DST/AMT (CC 76-78)
SP_M2_SRC/DST/AMT (CC 70-72)   SP_M5_SRC/DST/AMT (CC 79-81)
SP_M3_SRC/DST/AMT (CC 73-75)   SP_M6_SRC/DST/AMT (CC 82-84)
SP_LFO2_DEPTH (CC 85)   SP_LFO_SYNC (CC 86)   SP_LFO2_SYNC (CC 87)
```
Source = `round(param * 8)` (0..8); destination = `round(param * 8)` (0..8);
amount = `(param - 0.5) * 2` (bipolar). Duplicate source→destination cables are
rejected by the editor (the firmware just sums slots regardless).

## Roadmap
- Tempo-synced envelope rates (LFO rates already sync).
- Let the Generative mode's seed also wire matrix slots.
