#pragma once
#include "../src/HyphaObservatoryView.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace hypha::tests
{
inline void verifyLevelAvailability (const KirinObservatoryFrame& frame,
                                     const std::vector<KirinMeterHistoryEntry>& history)
{
    const auto check = [] (bool ok, const char* message)
    { if (! ok) { std::cerr << "LEVEL availability: " << message << '\n'; std::exit (1); } };
    const auto image = [] (observatory::View& view)
    {
        juce::Image result (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
        juce::Graphics graphics (result);
        view.paintEntireComponent (graphics, true);
        return result;
    };
    const auto capture = [] (const observatory::View& view)
    { return view.createCaptureImage (1200, 630, false, "2026-10-03 03:00:00", "LEVEL CONTRACT"); };
    const auto equal = [] (const juce::Image& a, const juce::Image& b)
    {
        if (a.getBounds() != b.getBounds()) return false;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y)) return false;
        return true;
    };
    const auto hold = [&] (observatory::View& view)
    {
        auto* previous = dynamic_cast<juce::Button*> (view.findChildWithID ("history-previous-tp"));
        if (previous == nullptr || ! previous->isVisible()) return false;
        previous->onClick();
        check (view.historyHeldForTest(), "threshold event creates an actual HOLD");
        return true;
    };
    constexpr std::array<std::pair<std::uint8_t, std::uint8_t>, 5> states {{
        { KIRIN_SIGNAL_STATE_INACTIVE, KIRIN_METER_SESSION_ACTIVE },
        { KIRIN_SIGNAL_STATE_INACTIVE, KIRIN_METER_SESSION_PAUSED },
        { KIRIN_SIGNAL_STATE_BYPASSED, KIRIN_METER_SESSION_PAUSED },
        { KIRIN_SIGNAL_STATE_ACTIVE, KIRIN_METER_SESSION_PAUSED },
        { KIRIN_SIGNAL_STATE_ACTIVE, KIRIN_METER_SESSION_ACTIVE }
    }};
    for (int width : { 300, 375, 450, 600, 900 })
    {
        observatory::View view (observatory::Role::post), reference (observatory::Role::post);
        view.setSize (width, width * 2 / 3);
        reference.setSize (width, width * 2 / 3);
        KirinLevelSnapshot packet {};
        packet.version = KIRIN_LEVEL_SNAPSHOT_VERSION;
        packet.frame = frame;
        packet.history_count = static_cast<std::uint32_t> (history.size());
        packet.chain_updated = 1;
        packet.chain.version = KIRIN_CHAIN_VERSION;
        packet.chain.revision = 42;
        view.setLevelObservation (&packet, history, nullptr);
        packet.chain_updated = 0;
        reference.setLevelObservation (&packet, history, nullptr);
        // Only availability may cross a failed snapshot acquisition. Neither a future nor an
        // older standalone numeric observation belongs to the retained history publication.
        for (int cycle = 0; cycle < 200; ++cycle)
            for (std::size_t at = 0; at < states.size(); ++at)
            {
                auto fallback = frame;
                fallback.signal_state = states[at].first;
                fallback.meter.state = states[at].second;
                const bool newer = (cycle + static_cast<int> (at)) % 2 == 0;
                fallback.meter.observed_frames = newer ? frame.meter.observed_frames + 4800 : 0u;
                fallback.meter.active_frames = newer ? frame.meter.active_frames + 9600 : 0u;
                fallback.meter.lufs_m = fallback.meter.lufs_s = fallback.meter.lufs_i = -3.0;
                fallback.meter.max_lufs_m = fallback.meter.max_true_peak = fallback.meter.true_peak = 2.0;
                fallback.meter.lra = fallback.meter.plr = 22.0;
                fallback.lra_elapsed_seconds = 0.0;
                fallback.lra_state = KIRIN_LRA_UNAVAILABLE;
                view.setLevelObservation (nullptr, {}, nullptr, &fallback);
                auto expected = packet;
                expected.frame.signal_state = fallback.signal_state;
                expected.frame.meter.state = fallback.meter.state;
                expected.frame.delta_available = 0u;
                reference.setLevelObservation (&expected, history, nullptr);
                check (view.captureHistoryEndpoint() == frame.meter.observed_frames,
                       "availability retains the complete packet's exact cutoff");
                check (view.levelChainRevision (false) == 0u, "availability retires obsolete PRE chain");
                // Bound full paints to the first and last cycles; setters still exercise all 200.
                if (cycle == 0 || cycle == 199)
                {
                    check (equal (image (view), image (reference)),
                           "all native pixels match complete old numeric/history plus current availability");
                    check (equal (capture (view), capture (reference)),
                           "Capture retains the actual historical curve at every size and state");
                }
            }
        observatory::View noHistory (observatory::Role::post);
        noHistory.setSize (width, width * 2 / 3);
        auto empty = packet;
        empty.history_count = 0;
        empty.frame.delta_available = 0u;
        noHistory.setLevelObservation (&empty, {}, nullptr);
        check (! equal (capture (reference), capture (noHistory)),
               "Capture oracle distinguishes retained historical samples from an empty graph");
        const auto finalImage = image (view), finalCapture = capture (view);
        view.setLevelObservation (nullptr, {}, nullptr);
        check (equal (finalImage, image (view)) && equal (finalCapture, capture (view)),
               "no fallback preserves all final rendered and captured pixels");
        auto invalid = frame;
        invalid.version = 0;
        view.setLevelObservation (nullptr, {}, nullptr, &invalid);
        check (equal (finalImage, image (view)) && equal (finalCapture, capture (view)),
               "an invalid fallback is not a new availability publication");

        view.setLevelObservation (&packet, history, nullptr);
        reference.setLevelObservation (&packet, history, nullptr);
        if (hold (view))
        {
            check (hold (reference), "reference HOLD is available at the same preset");
            for (const auto state : states)
            {
                auto fallback = frame;
                fallback.signal_state = state.first; fallback.meter.state = state.second;
                fallback.meter.observed_frames += 4800; fallback.meter.lufs_i = -3.0;
                view.setLevelObservation (nullptr, {}, nullptr, &fallback);
                auto expected = packet;
                expected.frame.signal_state = state.first; expected.frame.meter.state = state.second;
                expected.frame.delta_available = 0u;
                reference.setLevelObservation (&expected, history, nullptr);
                check (view.historyHeldForTest() && reference.historyHeldForTest(),
                       "same-session absolute HOLD survives pause, bypass and resume");
                check (equal (image (view), image (reference))
                           && equal (capture (view), capture (reference)),
                       "absolute HOLD retains its exact packet and captured history");
            }
        }
        // Every true identity component is a retirement boundary, even at an unchanged onset.
        for (int identity = 0; identity < 7; ++identity)
        {
            auto replacement = frame;
            switch (identity)
            {
                case 0: ++replacement.meter.generation; break;
                case 1: ++replacement.meter.measurement_epoch; break;
                case 2: replacement.meter.sample_rate += 48000; break;
                case 3: replacement.meter.channels = 1; break;
                case 4: ++replacement.meter.layout_id; break;
                case 5: ++replacement.meter.channel_positions[0]; break;
                default: replacement.meter.state = KIRIN_METER_SESSION_EMPTY; break;
            }
            view.setLevelObservation (&packet, history, nullptr);
            hold (view);
            view.setLevelObservation (nullptr, {}, nullptr, &replacement);
            check (! view.historyHeldForTest() && view.levelChainRevision (false) == 0u,
                   "real identity/EMPTY change retires all stale HOLD and chain");
            auto expected = empty;
            expected.frame = replacement;
            expected.frame.delta_available = 0u;
            noHistory.setLevelObservation (&expected, {}, nullptr);
            check (equal (image (view), image (noHistory))
                       && equal (capture (view), capture (noHistory)),
                   "real identity change has no old history pixels or mixed observation");
            view.setLevelObservation (&packet, history, nullptr);
            hold (view);
            // The complete replacement has its own history, never the previous format's HOLD.
            if (identity == 6)
            {
                expected.history_count = packet.history_count;
                view.setLevelObservation (&expected, history, nullptr);
            }
            else view.setLevelObservation (&expected, {}, nullptr);
            check (! view.historyHeldForTest(), "complete identity/EMPTY replacement retires old HOLD");
            check (equal (image (view), image (noHistory))
                       && equal (capture (view), capture (noHistory)),
                   "complete identity/EMPTY replacement cannot resurrect previous history");
        }

        // A HOLD that includes PRE-chain evidence is not an absolute-only inspection.
        if (auto* live = dynamic_cast<juce::Button*> (reference.findChildWithID ("history-live")))
            live->onClick();
        check (! reference.historyHeldForTest(), "chain retirement reference is a live absolute view");
        auto paired = packet;
        paired.chain_updated = 1;
        paired.chain.binding = 17;
        paired.chain.sample_rate = frame.meter.sample_rate;
        paired.chain.status = KIRIN_CHAIN_ACTIVE;
        paired.chain.count = 1;
        KirinChainPoint point {};
        point.post_epoch = frame.meter.measurement_epoch;
        point.post_generation = frame.meter.generation;
        point.post_observed = history.back().last_observed_frames;
        point.post_run = history.back().run_id;
        point.endpoint = history.back().last_timeline_endpoint_samples;
        point.pre_m = -15.0; point.post_m = -14.0; point.delta_m = 1.0;
        point.pre_tp = -2.0; point.post_tp = -0.5; point.delta_tp = 1.5;
        view.setLevelObservation (&paired, history, &point);
        if (hold (view))
        {
            auto paused = frame;
            paused.signal_state = KIRIN_SIGNAL_STATE_INACTIVE;
            paused.meter.state = KIRIN_METER_SESSION_PAUSED;
            view.setLevelObservation (nullptr, {}, nullptr, &paused);
            check (! view.historyHeldForTest() && view.levelChainRevision (false) == 0u,
                   "availability retires HOLD carrying obsolete PRE-chain evidence");
            auto expected = packet;
            expected.frame = paused; expected.frame.delta_available = 0u;
            reference.setLevelObservation (&expected, history, nullptr);
            check (equal (image (view), image (reference))
                       && equal (capture (view), capture (reference)),
                   "retiring chain HOLD still preserves valid absolute history");
        }
        view.setLevelObservation (&packet, history, nullptr);
        auto rebound = frame;
        ++rebound.comparison_identity; ++rebound.comparison_generation;
        view.setLevelObservation (nullptr, {}, nullptr, &rebound);
        auto expected = packet;
        expected.frame = rebound; expected.frame.delta_available = 0u;
        reference.setLevelObservation (&expected, history, nullptr);
        view.setTarget (observatory::ObservationTarget::delta);
        reference.setTarget (observatory::ObservationTarget::delta);
        check (equal (image (view), image (reference))
                   && equal (capture (view), capture (reference)),
               "a standalone rebind disables old delta rather than replaying stale comparison numbers");
    }
}

inline void verifyLevelPublication (KirinObservatoryFrame frame,
                                    std::vector<KirinMeterHistoryEntry> history)
{
    const auto check = [] (bool ok, const char* message)
    { if (! ok) { std::cerr << "LEVEL publication: " << message << '\n'; std::exit (1); } };
    const auto image = [] (observatory::View& view)
    {
        juce::Image result (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
        juce::Graphics graphics (result);
        view.paintEntireComponent (graphics, true);
        return result;
    };
    const auto equal = [] (const juce::Image& a, const juce::Image& b)
    {
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y)) return false;
        return true;
    };
    check (! history.empty(), "real history fixture required");
    // The shared rendering fixture's peaks are below -1 dBTP. Supply one actual threshold
    // event so TP navigation tests a held observation rather than a disabled command.
    history.back().true_peak.min = history.back().true_peak.max = history.back().true_peak.mean = -0.5;
    frame.meter.observed_frames = history.back().last_observed_frames;
    for (int width : { 300, 375, 450, 600, 900 })
    {
        observatory::View view (observatory::Role::post);
        view.setSize (width, width * 2 / 3);
        KirinLevelSnapshot packet {};
        packet.version = KIRIN_LEVEL_SNAPSHOT_VERSION;
        packet.frame = frame;
        packet.history_count = static_cast<std::uint32_t> (history.size());
        packet.chain_updated = 1;
        packet.chain.version = KIRIN_CHAIN_VERSION;
        packet.chain.revision = 42;
        view.setLevelObservation (&packet, history, nullptr);
        const auto original = image (view);
        auto independentlyNewer = frame;
        independentlyNewer.meter.observed_frames += 4800;
        independentlyNewer.meter.lufs_m += 4.0;
        ++independentlyNewer.comparison_generation;
        independentlyNewer.comparison_state = KIRIN_COMPARISON_STATE_POST_ABSOLUTE;
        for (int tick = 0; tick < 20; ++tick)
        {
            view.setLevelObservation (nullptr, {}, nullptr, &independentlyNewer);
            check (view.captureHistoryEndpoint() == frame.meter.observed_frames,
                   "busy snapshot must not mix a newer frame with retained history");
            check (view.levelChainRevision (false) == 42, "busy snapshot retains chain revision");
        }
        check (equal (original, image (view)), "busy acquisitions never erase/repaint graph contents");
        view.setLevelObservation (nullptr, {}, nullptr);
        check (equal (original, image (view)), "missing fallback retains last complete packet");
        packet.chain_updated = 0;
        view.setLevelObservation (&packet, history, nullptr);
        check (equal (original, image (view)), "identical recovery has no visual flash");
        check (view.levelChainRevision (true) == 0, "size mode asks for matching chain detail");

        if (auto* previousPeak = dynamic_cast<juce::Button*> (view.findChildWithID ("history-previous-tp"));
            previousPeak != nullptr && previousPeak->isVisible())
        {
            previousPeak->onClick();
            check (view.historyHeldForTest(), "fixture can inspect a peak");
            const auto held = image (view);
            view.setLevelObservation (nullptr, {}, nullptr, &independentlyNewer);
            check (view.historyHeldForTest() && equal (held, image (view)),
                   "busy acquisition preserves the user's held inspection");
        }

        auto reset = frame;
        ++reset.meter.generation;
        reset.meter.observed_frames = 0;
        reset.meter.state = KIRIN_METER_SESSION_EMPTY;
        view.setLevelObservation (nullptr, {}, nullptr, &reset);
        check (view.captureHistoryEndpoint() == 0, "real reset is not hidden by retained frame");
        check (view.levelChainRevision (false) == 0, "reset retires old chain and requests it again");
        check (! view.historyHeldForTest(), "real reset releases held inspection");
        check (! equal (original, image (view)), "real reset changes the display");
        view.setLevelObservation (&packet, history, nullptr);
        auto paused = frame;
        paused.signal_state = KIRIN_SIGNAL_STATE_BYPASSED;
        paused.meter.state = KIRIN_METER_SESSION_PAUSED;
        view.setLevelObservation (nullptr, {}, nullptr, &paused);
        check (! equal (original, image (view)), "bypass is observed even when history is busy");
        check (view.levelChainRevision (false) == 0, "bypass clears obsolete chain");
        packet.chain_updated = 1;
        view.setLevelObservation (&packet, history, nullptr);
        check (view.levelChainRevision (false) == 42, "valid publication restores the chain");
        auto otherPair = frame;
        ++otherPair.comparison_identity;
        ++otherPair.comparison_generation;
        view.setLevelObservation (nullptr, {}, nullptr, &otherPair);
        check (view.levelChainRevision (false) == 0,
               "pair changes retire the old chain even while the history writer is busy");
        check (view.captureHistoryEndpoint() == frame.meter.observed_frames && equal (original, image (view)),
               "a PRE binding/pass change must not clear POST absolute values and history");

        packet.chain_updated = 1;
        view.setLevelObservation (&packet, history, nullptr);
        check (view.levelChainRevision (false) == 42, "fixture has the previous PRE binding");
        packet.chain_updated = 0;
        packet.frame = otherPair;
        view.setLevelObservation (&packet, history, nullptr);
        check (view.levelChainRevision (false) == 0,
               "a complete POST packet cannot retain a previous PRE chain while its writer is busy");
        check (view.captureHistoryEndpoint() == frame.meter.observed_frames && equal (original, image (view)),
               "a complete binding change preserves POST's absolute history without a visual flash");
    }
    verifyLevelAvailability (frame, history);
}
}
