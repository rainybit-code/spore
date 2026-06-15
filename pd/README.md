# Pure Data prototyping sketches

Use desktop [Pure Data](https://puredata.info) (vanilla) to prototype DSP and
generative ideas quickly before porting them to C++ in `../src/modes/`.

Workflow:
1. Build/audition the algorithm here with Pd's GUI and audio.
2. Port the working idea into the matching mode (e.g. a grain engine tweak →
   `granular_mode.h`, a new modulation shape → `mod/modulation.h`).

Optional: `pd2dsy` can compile a Pd patch directly to a Daisy using a custom
Hothouse board JSON — handy for experiments, but the main firmware (USB MIDI,
I2C IMU, multi-mode) lives in `../src`.

## Patches

- **`krell.pd`** — prototype of the generative ("Krell") mode
  (`../src/modes/generative_mode.h`): a self-clocking `del` loop gives random
  timing, `random`→`mtof`→`osc~` picks pitches, a `vline~` makes the AD envelope,
  and a slow `osc~`→`vcf~` sweeps the filter. Open it, click **audio ON**, flip the
  toggle, and tweak the number boxes (pitch range/base, interval, Q). Needs vanilla
  Pd (no externals).

- **`synth_reverb.pd`** — prototype of the synth voice (`../src/modes/synth_mode.h`)
  into the global reverb FX (`../src/fx/effects.h`): saw `phasor~` → `lop~` (cutoff)
  → AR `vline~` envelope → **`rev3~`** reverb with a wet/dry mix. Play a MIDI
  controller (`notein`) or use the **TEST** toggle (set a test freq first), then dial
  cutoff / reverb amount / liveness. `rev3~` is from Pd's bundled **extra** library
  (included in plugdata and vanilla Pd).

Drop new `.pd` files in this folder.
