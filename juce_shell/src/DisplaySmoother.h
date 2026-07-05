#pragma once

#include <array>
#include <cmath>
#include <limits>

#include "kirin_hypha_ffi.h"

namespace hypha
{
    class DisplaySmoother
    {
    public:
        KirinMeasureResult smoothMeasure (const KirinMeasureResult& raw, double nowSecs)
        {
            if (expired (lastMeasureSecs, nowSecs))
            {
                clearSlots (measure);
                lastMeasureSecs = -1.0;
            }
            const double dt = deltaTime (lastMeasureSecs, nowSecs);
            lastMeasureSecs = nowSecs;

            auto out = nanMeasure();
            out = raw;
            out.lufs_m        = update (measure[0], raw.lufs_m,        dt, false);
            out.true_peak     = update (measure[1], raw.true_peak,     dt, true);
            out.crest         = update (measure[2], raw.crest,         dt, false);
            out.psr           = update (measure[3], raw.psr,           dt, false);
            out.n_prime_total = update (measure[4], raw.n_prime_total, dt, false);
            out.sharpness     = update (measure[5], raw.sharpness,     dt, false);
            return out;
        }

        KirinDelta smoothDelta (const KirinDelta& raw, double nowSecs)
        {
            if (expired (lastDeltaSecs, nowSecs))
            {
                clearSlots (delta);
                lastDeltaSecs = -1.0;
            }
            const double dt = deltaTime (lastDeltaSecs, nowSecs);
            lastDeltaSecs = nowSecs;

            auto out = nanDelta();
            out.mode          = raw.mode;
            out.lufs          = update (delta[0], raw.lufs,          dt, false);
            out.true_peak     = update (delta[1], raw.true_peak,     dt, false);
            out.crest         = update (delta[2], raw.crest,         dt, false);
            out.psr           = update (delta[3], raw.psr,           dt, false);
            out.n_prime_total = update (delta[4], raw.n_prime_total, dt, false);
            out.sharpness     = update (delta[5], raw.sharpness,     dt, false);
            return out;
        }

        bool heldMeasure (KirinMeasureResult& out, double nowSecs) const
        {
            if (! held (lastMeasureSecs, nowSecs) || ! hasCore (measure))
                return false;

            out = nanMeasure();
            out.lufs_m        = valueOrNan (measure[0]);
            out.true_peak     = valueOrNan (measure[1]);
            out.crest         = valueOrNan (measure[2]);
            out.psr           = valueOrNan (measure[3]);
            out.n_prime_total = valueOrNan (measure[4]);
            out.sharpness     = valueOrNan (measure[5]);
            return true;
        }

        bool heldDelta (KirinDelta& out, double nowSecs) const
        {
            if (! held (lastDeltaSecs, nowSecs) || ! hasCore (delta))
                return false;

            out = nanDelta();
            out.mode          = 0; // Active-shaped values rendered muted by the editor.
            out.lufs          = valueOrNan (delta[0]);
            out.true_peak     = valueOrNan (delta[1]);
            out.crest         = valueOrNan (delta[2]);
            out.psr           = valueOrNan (delta[3]);
            out.n_prime_total = valueOrNan (delta[4]);
            out.sharpness     = valueOrNan (delta[5]);
            return true;
        }

        void reset()
        {
            clearSlots (measure);
            clearSlots (delta);
            lastMeasureSecs = -1.0;
            lastDeltaSecs = -1.0;
        }

    private:
        struct Slot
        {
            double value = std::numeric_limits<double>::quiet_NaN();
            bool valid = false;
        };

        static constexpr double kTauSecs = 1.5;
        static constexpr double kHoldSecs = 18.0;

        std::array<Slot, 6> measure {};
        std::array<Slot, 6> delta {};
        double lastMeasureSecs = -1.0;
        double lastDeltaSecs = -1.0;

        static double nan() { return std::numeric_limits<double>::quiet_NaN(); }
        static bool finite (double v) { return std::isfinite (v); }

        static double deltaTime (double previous, double now)
        {
            if (previous < 0.0 || now < previous)
                return -1.0;
            const double dt = now - previous;
            return dt < 0.0 ? 0.0 : (dt > 1.0 ? 1.0 : dt);
        }

        static double update (Slot& slot, double raw, double dt, bool peakHold)
        {
            if (! finite (raw))
                return slot.valid ? slot.value : nan();
            if (! slot.valid || dt < 0.0)
            {
                slot.value = raw;
                slot.valid = true;
                return raw;
            }
            if (dt <= std::numeric_limits<double>::epsilon())
                return slot.value;
            if (peakHold && raw > slot.value)
            {
                slot.value = raw;
                return raw;
            }
            const double alpha = 1.0 - std::exp (-dt / kTauSecs);
            slot.value += (raw - slot.value) * alpha;
            return slot.value;
        }

        static bool held (double previous, double now)
        {
            return previous >= 0.0 && now >= previous && (now - previous) <= kHoldSecs;
        }

        static bool expired (double previous, double now)
        {
            return previous >= 0.0 && now >= previous && (now - previous) > kHoldSecs;
        }

        static void clearSlots (std::array<Slot, 6>& slots)
        {
            for (auto& s : slots) s = {};
        }

        static bool hasCore (const std::array<Slot, 6>& slots)
        {
            return slots[0].valid || slots[1].valid || slots[2].valid;
        }

        static double valueOrNan (const Slot& slot)
        {
            return slot.valid ? slot.value : nan();
        }

        static KirinMeasureResult nanMeasure()
        {
            KirinMeasureResult r {};
            r.lufs_m = r.true_peak = r.crest = r.psr = r.n_prime_total = r.sharpness = nan();
            r.psb_low = r.psb_mid = r.psb_high = r.tp_session_max = nan();
            for (auto& v : r.n_prime) v = nan();
            for (auto& v : r.psb_bark) v = nan();
            r.dropped_samples = 0;
            return r;
        }

        static KirinDelta nanDelta()
        {
            KirinDelta d {};
            d.mode = 2;
            d.lufs = d.true_peak = d.crest = d.psr = d.n_prime_total = d.sharpness = nan();
            return d;
        }
    };
}
