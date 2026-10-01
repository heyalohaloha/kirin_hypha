#pragma once
#include "../src/HyphaObservatoryView.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
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
}
}
