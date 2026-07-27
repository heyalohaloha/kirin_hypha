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
        template <typename T>
        struct HeldDisplay
        {
            T value {};
            bool muted = false;
        };

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
            // LUFS-S is the exact 3 s engine window. Never EMA-smooth it or borrow an older
            // playback epoch while the new window is still unavailable.
            out.lufs_s        = updateDirect (measure[6], raw.lufs_s);
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
            out.lufs_s        = updateDirect (delta[6], raw.lufs_s);
            return out;
        }

        bool heldMeasure (KirinMeasureResult& out, double nowSecs) const
        {
            HeldDisplay<KirinMeasureResult> held {};
            if (! heldMeasureDisplay (held, nowSecs))
                return false;

            out = held.value;
            return true;
        }

        bool heldMeasureDisplay (HeldDisplay<KirinMeasureResult>& out, double nowSecs) const
        {
            if (! held (lastMeasureSecs, nowSecs) || ! hasCore (measure))
                return false;

            out.value = nanMeasure();
            out.value.lufs_m        = valueOrNan (measure[0]);
            out.value.true_peak     = valueOrNan (measure[1]);
            out.value.crest         = valueOrNan (measure[2]);
            out.value.psr           = valueOrNan (measure[3]);
            out.value.n_prime_total = valueOrNan (measure[4]);
            out.value.sharpness     = valueOrNan (measure[5]);
            out.value.lufs_s        = valueOrNan (measure[6]);
            out.muted = muted (lastMeasureSecs, nowSecs);
            return true;
        }

        bool heldDelta (KirinDelta& out, double nowSecs) const
        {
            HeldDisplay<KirinDelta> held {};
            if (! heldDeltaDisplay (held, nowSecs))
                return false;

            out = held.value;
            return true;
        }

        bool heldDeltaDisplay (HeldDisplay<KirinDelta>& out, double nowSecs) const
        {
            if (! held (lastDeltaSecs, nowSecs) || ! hasCore (delta))
                return false;

            out.value = nanDelta();
            out.value.mode          = 0; // Active-shaped values rendered by the editor.
            out.value.lufs          = valueOrNan (delta[0]);
            out.value.true_peak     = valueOrNan (delta[1]);
            out.value.crest         = valueOrNan (delta[2]);
            out.value.psr           = valueOrNan (delta[3]);
            out.value.n_prime_total = valueOrNan (delta[4]);
            out.value.sharpness     = valueOrNan (delta[5]);
            out.value.lufs_s        = valueOrNan (delta[6]);
            out.muted = muted (lastDeltaSecs, nowSecs);
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
        static constexpr double kHoldSecs = 9.0;
        static constexpr double kMutedAfterSecs = 5.0;

        std::array<Slot, 7> measure {};
        std::array<Slot, 7> delta {};
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

        static double updateDirect (Slot& slot, double raw)
        {
            if (! finite (raw))
            {
                slot = {};
                return nan();
            }
            slot.value = raw;
            slot.valid = true;
            return raw;
        }

        static bool held (double previous, double now)
        {
            return previous >= 0.0 && now >= previous && (now - previous) <= kHoldSecs;
        }

        static bool expired (double previous, double now)
        {
            return previous >= 0.0 && now >= previous && (now - previous) > kHoldSecs;
        }

        static bool muted (double previous, double now)
        {
            return previous >= 0.0 && now >= previous && (now - previous) >= kMutedAfterSecs;
        }

        static void clearSlots (std::array<Slot, 7>& slots)
        {
            for (auto& s : slots) s = {};
        }

        static bool hasCore (const std::array<Slot, 7>& slots)
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
            r.lufs_s = nan();
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
            d.lufs_s = nan();
            return d;
        }
    };
}
