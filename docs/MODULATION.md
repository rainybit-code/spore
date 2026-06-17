# Modulation

The synth voice has fixed per-note modulation (filter envelope -> cutoff, velocity
-> level/brightness) plus a flexible **mod matrix** for movement.

## LFOs
- **LFO1** — the original global LFO with its own quick macro destination
  (off / vibrato / filter / tremolo), set in the MOD pod.
- **LFO2** — a second global LFO (rate + shape), intended as a matrix source.

Both are free-running for now. Tempo-sync (lock to the sequencer / MIDI clock) is
a planned phase.

## Mod matrix (3 slots)
Each slot routes one **source** to one **destination** with a **bipolar amount**
(-1..+1). Slots sum per destination; the result is applied to every voice at block
rate (global movement).

| Sources | Destinations |
|---------|--------------|
| Off     | Cutoff       |
| LFO1    | Pitch        |
| LFO2    | WT-Scan      |
| Random (S&H) | Drive   |
| Sensor  | Sub level    |
|         | FM amount    |
|         | Amp (tremolo)|

Applied as: cutoff/pitch/amp are multiplicative trims folded into the existing
LFO1 routing; scan/drive/sub/FM are additive offsets clamped to 0..1.

## MIDI / params
Extends `config/synth_params.h` (CC base 40):

```
SP_LFO2_RATE (CC 65)  SP_LFO2_SHAPE (CC 66)
SP_M1_SRC/DST/AMT (CC 67-69)
SP_M2_SRC/DST/AMT (CC 70-72)
SP_M3_SRC/DST/AMT (CC 73-75)
```
Source = param * 4 (0..4); destination = param * 6 (0..6); amount = (param-0.5)*2.

## Roadmap
- Tempo-synced LFO/env rates.
- Per-voice sources (velocity, key-track) -> destination amounts.
- Let the Generative mode's seed also wire matrix slots.
