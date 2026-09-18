// Display smoothing boundary — pure C++ contract.
//
// The live Watch numbers are the one place where Hypha shows a value that is not the raw
// measurement: short drum transients make an unfiltered readout unreadable in a 300x200 editor,
// so the current cells run through a 1.5 s EMA. Everything that is kept, recorded, held as a
// maximum, or read back later stays raw.
//
// This test pins that boundary so the smoothed set cannot quietly grow and the raw set cannot
// quietly shrink. It deliberately builds before any JUCE target, exactly like ui_contract_test.

#include "../src/DisplaySmoother.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace
{
    constexpr double kTau = 1.5;
    constexpr double kHold = 9.0;
    constexpr double kMutedAfter = 5.0;

    constexpr double nan() { return std::numeric_limits<double>::quiet_NaN(); }

    bool near (double a, double b, double tolerance = 1e-9)
    {
        return std::isfinite (a) && std::isfinite (b) && std::fabs (a - b) <= tolerance;
    }

    double ema (double previous, double raw, double dt)
    {
        return previous + (raw - previous) * (1.0 - std::exp (-dt / kTau));
    }

    // Every field carries a distinct finite value so a dropped or zeroed field is visible.
    KirinMeasureResult filledMeasure (double seed)
    {
        KirinMeasureResult m {};
        m.lufs_m = seed;
        m.true_peak = seed + 1.0;
        m.crest = seed + 2.0;
        m.psr = seed + 3.0;
        m.n_prime_total = seed + 4.0;
        m.sharpness = seed + 5.0;
        m.psb_low = seed + 6.0;
        m.psb_mid = seed + 7.0;
        m.psb_high = seed + 8.0;
        m.tp_session_max = seed + 9.0;
        m.lufs_s = seed + 10.0;
        m.dropped_samples = 7;
        for (int i = 0; i < 20; ++i)
        {
            m.n_prime[i] = seed + 100.0 + i;
            m.psb_bark[i] = seed + 200.0 + i;
        }
        return m;
    }

    KirinDelta filledDelta (double seed)
    {
        KirinDelta d {};
        d.mode = KIRIN_DELTA_MODE_ACTIVE;
        d.lufs = seed;
        d.true_peak = seed + 1.0;
        d.crest = seed + 2.0;
        d.psr = seed + 3.0;
        d.n_prime_total = seed + 4.0;
        d.sharpness = seed + 5.0;
        d.lufs_s = seed + 6.0;
        for (int i = 0; i < 20; ++i)
            d.psb_bark[i] = seed + 300.0 + i;
        return d;
    }

    // ── The smoothed set ──────────────────────────────────────────────────────────────────

    void live_numbers_approach_the_measurement_on_a_fixed_time_constant()
    {
        hypha::DisplaySmoother s;
        auto m = filledMeasure (-30.0);

        // The first observation of a run is the measurement itself. Nothing to blend with yet.
        assert (near (s.smoothMeasure (m, 0.0).lufs_m, -30.0));

        m.lufs_m = -10.0;
        const double after = s.smoothMeasure (m, 0.1).lufs_m;
        assert (near (after, ema (-30.0, -10.0, 0.1)));
        assert (after > -30.0 && after < -10.0);

        // The same constant applies to every other smoothed scalar.
        hypha::DisplaySmoother t;
        auto a = filledMeasure (0.0);
        t.smoothMeasure (a, 0.0);
        auto b = filledMeasure (20.0);
        const auto out = t.smoothMeasure (b, 0.1);
        assert (near (out.crest, ema (2.0, 22.0, 0.1)));
        assert (near (out.psr, ema (3.0, 23.0, 0.1)));
        assert (near (out.n_prime_total, ema (4.0, 24.0, 0.1)));
        assert (near (out.sharpness, ema (5.0, 25.0, 0.1)));
    }

    void true_peak_rises_at_once_and_only_its_release_is_smoothed()
    {
        hypha::DisplaySmoother s;
        auto m = filledMeasure (0.0);
        m.true_peak = -1.0;
        assert (near (s.smoothMeasure (m, 0.0).true_peak, -1.0));

        m.true_peak = -12.0;
        const double released = s.smoothMeasure (m, 0.1).true_peak;
        assert (near (released, ema (-1.0, -12.0, 0.1)));
        assert (released > -12.0 && released < -1.0);

        // A higher peak is never delayed: an overshoot must be visible on the observation itself.
        m.true_peak = -0.5;
        assert (near (s.smoothMeasure (m, 0.2).true_peak, -0.5));
    }

    void the_gap_between_observations_is_bounded_at_one_second()
    {
        // A stalled UI inside the hold window must not jump the display straight to the newest
        // value on resume: the step is capped at one second of the time constant.
        hypha::DisplaySmoother s;
        auto m = filledMeasure (-40.0);
        s.smoothMeasure (m, 0.0);
        m.lufs_m = 0.0;
        assert (near (s.smoothMeasure (m, 5.0).lufs_m, ema (-40.0, 0.0, 1.0)));
    }

    // ── The raw set ───────────────────────────────────────────────────────────────────────

    void short_term_loudness_is_never_time_smoothed()
    {
        // LUFS-S is the exact 3 s engine window. Smoothing it would filter a filter.
        hypha::DisplaySmoother s;
        auto m = filledMeasure (0.0); // lufs_s = 10.0
        assert (near (s.smoothMeasure (m, 0.0).lufs_s, 10.0));
        m.lufs_s = -60.0;
        assert (near (s.smoothMeasure (m, 0.1).lufs_s, -60.0));
        assert (near (s.smoothMeasure (m, 0.2).lufs_s, -60.0));
    }

    void session_maximum_and_integrity_facts_pass_through_untouched()
    {
        hypha::DisplaySmoother s;
        auto first = filledMeasure (0.0);
        s.smoothMeasure (first, 0.0);

        auto m = filledMeasure (50.0);
        const auto out = s.smoothMeasure (m, 0.1);

        // tp_session_max is the Record canonical session maximum. The display must not invent a
        // value between two of them.
        assert (near (out.tp_session_max, m.tp_session_max));
        assert (near (out.psb_low, m.psb_low));
        assert (near (out.psb_mid, m.psb_mid));
        assert (near (out.psb_high, m.psb_high));
        assert (out.dropped_samples == m.dropped_samples);
        for (int i = 0; i < 20; ++i)
        {
            assert (near (out.n_prime[i], m.n_prime[i]));
            assert (near (out.psb_bark[i], m.psb_bark[i]));
        }
    }

    void the_smoothed_delta_still_carries_every_measured_band()
    {
        hypha::DisplaySmoother s;
        s.smoothDelta (filledDelta (0.0), 0.0);

        auto d = filledDelta (10.0);
        const auto out = s.smoothDelta (d, 0.1);
        assert (out.mode == KIRIN_DELTA_MODE_ACTIVE);
        assert (near (out.lufs, ema (0.0, 10.0, 0.1)));
        for (int i = 0; i < 20; ++i)
            assert (near (out.psb_bark[i], d.psb_bark[i]));
    }

    void an_absent_value_is_nan_and_never_zero()
    {
        // 0.0 is a legitimate share and a legitimate dB difference. "No value" must stay NaN so a
        // held or unavailable cell cannot be read as a measurement.
        hypha::DisplaySmoother s;
        s.smoothDelta (filledDelta (0.0), 0.0);

        hypha::DisplaySmoother::HeldDisplay<KirinDelta> held {};
        assert (s.heldDeltaDisplay (held, 1.0));
        for (int i = 0; i < 20; ++i)
            assert (std::isnan (held.value.psb_bark[i]));
    }

    void a_value_that_stops_arriving_keeps_the_last_displayed_one()
    {
        hypha::DisplaySmoother s;
        auto m = filledMeasure (0.0);
        assert (near (s.smoothMeasure (m, 0.0).crest, 2.0));
        m.crest = nan();
        assert (near (s.smoothMeasure (m, 0.5).crest, 2.0));
    }

    // ── Hold and release ──────────────────────────────────────────────────────────────────

    void a_held_display_dims_after_five_seconds_and_ends_after_nine()
    {
        hypha::DisplaySmoother s;
        s.smoothMeasure (filledMeasure (-18.0), 10.0);

        hypha::DisplaySmoother::HeldDisplay<KirinMeasureResult> held {};
        assert (s.heldMeasureDisplay (held, 10.0 + kMutedAfter - 0.1) && ! held.muted);
        assert (s.heldMeasureDisplay (held, 10.0 + kMutedAfter) && held.muted);
        assert (s.heldMeasureDisplay (held, 10.0 + kHold - 0.1) && held.muted);
        assert (! s.heldMeasureDisplay (held, 10.0 + kHold + 0.1));
    }

    void a_run_that_resumes_after_the_hold_window_starts_from_the_measurement()
    {
        hypha::DisplaySmoother s;
        auto m = filledMeasure (-30.0);
        assert (near (s.smoothMeasure (m, 0.0).lufs_m, -30.0));

        m.lufs_m = -10.0;
        assert (near (s.smoothMeasure (m, kHold + 0.2).lufs_m, -10.0));
    }

    void an_explicit_reset_leaves_nothing_to_hold()
    {
        hypha::DisplaySmoother s;
        s.smoothMeasure (filledMeasure (-18.0), 1.0);
        s.reset();

        hypha::DisplaySmoother::HeldDisplay<KirinMeasureResult> held {};
        assert (! s.heldMeasureDisplay (held, 1.1));
    }
}

int main()
{
    live_numbers_approach_the_measurement_on_a_fixed_time_constant();
    true_peak_rises_at_once_and_only_its_release_is_smoothed();
    the_gap_between_observations_is_bounded_at_one_second();
    short_term_loudness_is_never_time_smoothed();
    session_maximum_and_integrity_facts_pass_through_untouched();
    the_smoothed_delta_still_carries_every_measured_band();
    an_absent_value_is_nan_and_never_zero();
    a_value_that_stops_arriving_keeps_the_last_displayed_one();
    a_held_display_dims_after_five_seconds_and_ends_after_nine();
    a_run_that_resumes_after_the_hold_window_starts_from_the_measurement();
    an_explicit_reset_leaves_nothing_to_hold();
    return 0;
}
