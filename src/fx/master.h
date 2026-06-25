// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  master.h  --  master output stage: switchable filter (LP/BP/HP) + volume
//                + peak limiter.
// =============================================================================
//  Applied to the final stereo mix AFTER the global FX. One state-variable
//  filter per channel gives all three responses at once, so the type select just
//  picks which output to use. The limiter rides the output gain down when a peak
//  would exceed the ceiling (fast attack, slow release) instead of hard-clipping,
//  so a resonant filter / stacked grains / runaway feedback get turned down
//  smoothly rather than crushed. All params arrive over MIDI (see config/params.h
//  `master` + `midi`).
// =============================================================================
#pragma once

#include <cmath>
#include <cstddef>

#include "daisysp.h"
#include "config/params.h"

namespace synthbox {

class MasterChain {
  public:
    enum FilterType { OFF = 0, LP = 1, BP = 2, HP = 3 };

    void Init(float sr) {
        sr_ = sr;
        lf_.Init(sr);
        rf_.Init(sr);
        type_ = OFF;
        cut01_ = 1.0f;
        res01_ = 0.0f;
        vol_ = 1.0f;
        gain_ = 1.0f;
        // One-pole smoothing coefficients: ~0.5 ms attack (pull gain down fast on a
        // peak), ~150 ms release (let it recover gently so you don't hear pumping).
        atk_ = 1.0f - expf(-1.0f / (0.0005f * sr_));
        rel_ = 1.0f - expf(-1.0f / (0.1500f * sr_));
        // Smooth the control params toward their targets (~15 ms, block-rate) so
        // stepped MIDI CC / fast knob moves don't zipper -- worst on resonant cutoff.
        sm_vol_ = vol_;
        sm_cut_ = cut01_;
        sm_res_ = res01_;
        par_ = 1.0f - expf(-static_cast<float>(params::audio::kBlockSize) / (0.015f * sr_));
        // DC blocker: one-pole high-pass ~20 Hz to strip any offset built up by
        // resonant filters / wavefolding / drive before it eats output headroom.
        dc_r_ = 1.0f - (2.0f * 3.14159265f * 20.0f / sr_);
        dcx_l_ = dcy_l_ = dcx_r_ = dcy_r_ = 0.0f;
    }

    // All normalized 0..1 (type is 0..3); mapped to ranges in Process().
    void SetVolume(float v01) { vol_ = v01; }
    void SetFilterType(int t) { type_ = t < 0 ? 0 : (t > 3 ? 3 : t); }
    void SetCutoff(float v01) { cut01_ = v01; }
    void SetRes(float v01) { res01_ = v01; }

    // Target values (pre-smoothing), for capturing into a preset.
    float GetVolume() const { return vol_; }
    int GetFilterType() const { return type_; }
    float GetCutoff() const { return cut01_; }
    float GetRes() const { return res01_; }

    // In-place stereo process.
    void Process(float* l, float* r, size_t size) {
        // Block-rate smoothing of the control params (de-zipper).
        sm_vol_ += (vol_ - sm_vol_) * par_;
        sm_cut_ += (cut01_ - sm_cut_) * par_;
        sm_res_ += (res01_ - sm_res_) * par_;
        const float g = sm_vol_;
        if (type_ == OFF) {  // volume only, no filtering
            for (size_t i = 0; i < size; ++i) {
                float a = l[i] * g, b = r[i] * g;
                DcBlock(a, b);
                Limit(a, b);
                l[i] = a;
                r[i] = b;
            }
            return;
        }
        const float fc = daisysp::fmap(sm_cut_, params::master::kCutMinHz,
                                       params::master::kCutMaxHz, daisysp::Mapping::EXP);
        const float res = sm_res_ * params::master::kResMax;
        lf_.SetFreq(fc);
        lf_.SetRes(res);
        rf_.SetFreq(fc);
        rf_.SetRes(res);
        for (size_t i = 0; i < size; ++i) {
            lf_.Process(l[i]);
            rf_.Process(r[i]);
            float lo, ro;
            if (type_ == LP) {
                lo = lf_.Low();
                ro = rf_.Low();
            } else if (type_ == BP) {
                lo = lf_.Band();
                ro = rf_.Band();
            } else {
                lo = lf_.High();
                ro = rf_.High();
            }
            float a = lo * g, b = ro * g;
            DcBlock(a, b);
            Limit(a, b);
            l[i] = a;
            r[i] = b;
        }
    }

  private:
    // Peak limiter: scale both channels by a shared gain that drops fast when the
    // louder channel would exceed the ceiling and recovers slowly. Keeps the mix
    // under the ceiling without the harshness of per-sample hard clipping. The final
    // clamp to [-1, 1] is the backstop for the sub-millisecond attack slip, so the
    // output can never leave range whatever the limiter gain is mid-transient.
    inline void Limit(float& l, float& r) {
        const float peak = fmaxf(fabsf(l), fabsf(r));
        const float desired = peak > kCeiling ? kCeiling / peak : 1.0f;
        gain_ += (desired - gain_) * (desired < gain_ ? atk_ : rel_);
        l = daisysp::fclamp(l * gain_, -1.0f, 1.0f);
        r = daisysp::fclamp(r * gain_, -1.0f, 1.0f);
    }

    // One-pole DC-blocking high-pass per channel: y = x - x1 + R*y1.
    inline void DcBlock(float& l, float& r) {
        float yl = l - dcx_l_ + dc_r_ * dcy_l_;
        dcx_l_ = l;
        dcy_l_ = yl;
        l = yl;
        float yr = r - dcx_r_ + dc_r_ * dcy_r_;
        dcx_r_ = r;
        dcy_r_ = yr;
        r = yr;
    }

    static constexpr float kCeiling = 0.98f;  // leave a hair of headroom below 0 dBFS

    daisysp::Svf lf_, rf_;
    float sr_ = 48000.0f, cut01_ = 1.0f, res01_ = 0.0f, vol_ = 1.0f;
    float gain_ = 1.0f, atk_ = 0.04f, rel_ = 0.0001f;
    float sm_vol_ = 1.0f, sm_cut_ = 1.0f, sm_res_ = 0.0f, par_ = 0.06f;  // smoothed params
    float dc_r_ = 0.9974f, dcx_l_ = 0.0f, dcy_l_ = 0.0f, dcx_r_ = 0.0f,
          dcy_r_ = 0.0f;  // DC blocker
    int type_ = OFF;
};

}  // namespace synthbox
