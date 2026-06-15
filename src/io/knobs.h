// =============================================================================
//  knobs.h  --  shift-layer knob manager with soft-takeover ("pickup").
// =============================================================================
//  The 6 physical knobs are shared between two layers:
//      layer 0 = MODE params   (normal)
//      layer 1 = FX params      (while a footswitch is held)
//  When you switch layers, that layer's knobs are "locked" to their stored value
//  and only start moving once the physical knob passes through the stored value
//  -- so nothing jumps when you let go of the shift footswitch.
// =============================================================================
#pragma once

#include <cmath>

#include "hothouse.h"
#include "config/params.h"

namespace synthbox {

class ShiftKnobs {
 public:
  static constexpr int kKnobs = 6;
  enum Layer { MODE = 0, FX = 1 };

  // fx_defaults: starting normalized (0..1) values for the FX layer's 6 knobs.
  void Init(const float fx_defaults[kKnobs]) {
    for (int k = 0; k < kKnobs; ++k) {
      val_[MODE][k] = 0.5f;
      val_[FX][k] = fx_defaults[k];
      locked_[MODE][k] = false;
      locked_[FX][k] = false;
    }
    active_ = MODE;
  }

  // Select the active layer; on a change, lock the newly active layer's knobs.
  void SetLayer(Layer layer) {
    if (layer != active_) {
      active_ = layer;
      for (int k = 0; k < kKnobs; ++k) locked_[layer][k] = true;
    }
  }

  Layer ActiveLayer() const { return active_; }

  // Read physical knobs and update the active layer (with pickup). Call once/block.
  void Update(clevelandmusicco::Hothouse& hw) {
    const Layer L = active_;
    for (int k = 0; k < kKnobs; ++k) {
      float p = hw.GetKnobValue(static_cast<clevelandmusicco::Hothouse::Knob>(k));
      if (locked_[L][k]) {
        if (fabsf(p - val_[L][k]) <= params::fx::kPickupBand) locked_[L][k] = false;
      }
      if (!locked_[L][k]) val_[L][k] = p;
    }
  }

  float Value(Layer layer, int knob) const { return val_[layer][knob]; }
  const float* Values(Layer layer) const { return val_[layer]; }

 private:
  float val_[2][kKnobs];
  bool  locked_[2][kKnobs];
  Layer active_ = MODE;
};

}  // namespace synthbox
