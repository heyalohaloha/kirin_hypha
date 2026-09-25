#pragma once

#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaCaptureHistoryGeometry.h"
#include "../src/HyphaObservationEquality.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests::always_visible
{
inline void require (bool ok, const char* message)
{
    if (ok) return;
    std::cerr << "Always visible: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

inline juce::Button& button (observatory::View& view, const char* id)
{
    auto* child = dynamic_cast<juce::Button*> (view.findChildWithID (id));
    require (child != nullptr, id);
    return *child;
}

inline juce::Image render (observatory::View& view)
{
    juce::Image result (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
    juce::Graphics g (result); view.paintEntireComponent (g, true); return result;
}

inline void verify()
{
    using namespace history_inspection;
    require (! strongPeak (-1.0) && ! strongPeak (-0.001) && ! strongPeak (0.0), "no glow at/below zero");
    require (strongPeak (0.00001) && strongPeak (1.0), "all strictly positive peaks glow");
    require (! strongPeak (std::numeric_limits<double>::quiet_NaN())
             && ! strongPeak (std::numeric_limits<double>::infinity()), "no missing-value glow");
    require (peakText (0.00001) == "+<0.01" && peakText (0.01) == "+0.01", "positive rounding stays visible");

    KirinObservatoryFrame frame {};
    frame.version = KIRIN_OBSERVATORY_FRAME_VERSION;
    frame.signal_state = KIRIN_SIGNAL_STATE_ACTIVE;
    frame.meter.state = KIRIN_METER_SESSION_ACTIVE;
    frame.meter.measurement_epoch = 41; frame.meter.generation = 7;
    frame.meter.sample_rate = 48000;
    frame.meter.true_peak = -3.6; frame.meter.max_true_peak = -1.2;
    frame.meter.lufs_m = -13.8; frame.meter.lufs_i = -14.3;
    std::vector<KirinMeterHistoryEntry> live (600);
    for (std::size_t i = 0; i < live.size(); ++i)
    {
        auto& entry = live[i];
        entry.measurement_epoch = 41; entry.generation = 7; entry.run_id = 1;
        entry.first_observed_frames = entry.last_observed_frames = (i + 1) * 4800;
        entry.first_timeline_endpoint_samples = entry.last_timeline_endpoint_samples
            = static_cast<std::int64_t> (entry.last_observed_frames) + 48000;
        entry.observation_count = 1; entry.resolution = KIRIN_METER_HISTORY_10_HZ;
        entry.true_peak.max = i == 100 ? 0.4 : i == 300 ? -0.5 : -5.0;
        entry.lufs_m.mean = -18.0; entry.lufs_s.mean = -19.0;
    }
    require (positionText (live[0], 48000) == "HOST ~00:01.100", "endpoint without invented project provenance");
    auto unknown = live[0]; unknown.last_timeline_endpoint_samples = INT64_MIN;
    require (positionText (unknown, 48000) == "ELAPSED 00:00.100", "unknown clock uses elapsed, not DAW");
    unknown.last_timeline_endpoint_samples = -48000;
    require (positionText (unknown, 48000) == "HOST ~-00:01.000", "negative preroll");
    require (positionText (unknown, 0) == "POSITION UNAVAILABLE", "invalid rate");

    Selection selection;
    require (! selection.pin ({}, 0, frame.meter), "empty pin refused");
    require (selection.event (live, 48000, -1) == 300, "previous begins with newest event");
    require (selection.pin (live, 300, frame.meter), "pin snapshot");
    require (selection.event (live, 48000, -1) == 100, "previous excursion");
    require (! selection.event (live, 48000, 1), "no wrap at newest");
    auto changed = live; changed[300].true_peak.max = 3.0;
    require (std::abs (selection.snapshot[300].true_peak.max + 0.5) < 1e-10, "live updates cannot mutate hold");
    require (selection.matches (frame.meter), "same generation");
    auto reset = frame.meter; ++reset.generation;
    require (! selection.matches (reset), "reset invalidates old selection");
    Selection invalid;
    require (! invalid.pin (live, 100, reset), "old history cannot pin against a new session");
    reset = frame.meter; reset.state = KIRIN_METER_SESSION_EMPTY;
    require (! invalid.pin (live, 100, reset), "empty session cannot pin old history");
    reset = frame.meter; ++reset.measurement_epoch;
    require (! selection.matches (reset), "new engine invalidates repeated generation");
    selection.clear(); require (! selection.held(), "resume");

    KirinChainSnapshot chain {};
    chain.version = KIRIN_CHAIN_VERSION;
    chain.status = KIRIN_CHAIN_ACTIVE;
    chain.sample_rate = 48000;
    chain.binding = 99;
    chain.count = 1;
    KirinChainPoint chainPoint {};
    chainPoint.post_epoch = 41; chainPoint.post_generation = 7; chainPoint.post_run = 1;
    chainPoint.post_observed = live[300].last_observed_frames + 2400;
    chainPoint.endpoint = 123456;
    std::vector<KirinChainPoint> paired { chainPoint };
    require (selection.pin (live, 300, frame.meter, &chain, &paired), "absolute hold with comparison");
    require (selection.selectedChain() == nullptr, "half-tick neighbor is not the same 400 ms window");
    require (selection.pinChain (live, 0, frame.meter, chain, paired),
             "the exact comparison point has a separate one-click hold");
    require (selection.held() && selection.selectedAbsolute() == nullptr
             && selection.selectedChain()->endpoint == chainPoint.endpoint,
             "comparison hold preserves point identity without inventing an absolute endpoint");
    require (selection.event (live, 48000, -1) == 300,
             "TP navigation from a comparison point uses its observed endpoint");
    const auto area = juce::Rectangle<int> (0, 0, 900, 240);
    const auto plot = capture_history::layoutFor (area).sharedPlot;
    const auto chainView = chain_action::View { &chain, &paired, live.back().last_observed_frames };
    const auto chainX = chain_action::xFor (plot, chainView, paired[0]);
    require (chainX.has_value(), "comparison point has a visible 60-second coordinate");
    require (capture_history::hitTestChain (area, chain, paired, live.back().last_observed_frames,
             { *chainX, plot.getBottom() - 4.0f }) == 0,
             "one click in the comparison band selects its actual point");
    require (! capture_history::hitTestChain (area, chain, paired, live.back().last_observed_frames,
             { *chainX, plot.getY() + 4.0f }), "loudness plot is not a chain-selection shortcut");
    selection.select (300);
    require (selection.selectedChain() == nullptr && selection.selectedAbsolute() != nullptr,
             "navigating to a different absolute point drops unrelated comparison detail");
    paired[0].post_observed = live[300].last_observed_frames;
    require (selection.pin (live, 300, frame.meter, &chain, &paired)
             && selection.selectedChain() != nullptr, "exact run and endpoint may share the detail");
    paired[0].post_run = 2;
    require (selection.pin (live, 300, frame.meter, &chain, &paired)
             && selection.selectedChain() == nullptr, "equal endpoint in another run is rejected");
    frame.meter.observed_frames = live.back().last_observed_frames;
    require (selection.pin (live, 300, frame.meter, &chain, &paired, &frame)
             && selection.packetFrameAvailable, "hold retains the packet cutoff for Capture");
    const auto capturedMeter = selection.packetFrame.meter.observed_frames;
    frame.meter.observed_frames += 4800;
    require (selection.packetFrame.meter.observed_frames == capturedMeter,
             "later live meter polls cannot enter a held Capture");
    chain.status = KIRIN_CHAIN_AMBIGUOUS;
    require (! capture_history::hitTestChain (area, chain, paired, live.back().last_observed_frames,
             { *chainX, plot.getBottom() - 4.0f }), "ambiguous comparison cannot be selected");
    selection.clear();

    for (auto role : { observatory::Role::pre, observatory::Role::post })
    {
        observatory::View view (role);
        view.setSize (600, 400); view.setObservatoryFrame (frame, true); view.setHistory (live);
        KirinWatchDisplay watch {};
        watch.current.lufs_m = -13.8; watch.current.lufs_s = -14.2; watch.current.crest = 12.7;
        watch.maximum.lufs_m = -10.6; watch.maximum.lufs_s = -11.4; watch.maximum.crest = 16.3;
        view.setWatchDisplay (watch, true);
        render (view); // Resolve the same plot bounds used by the shipping painter.
        const juce::Point<float> point (200.0f,
            static_cast<float> (button (view, "history-live").getBottom() + 60));
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(),
            point, juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0,
            &view, &view, now, point, now, 1, false);
        view.mouseDown (click); require (view.historyHeldForTest(), "plot click holds selected observation");
        view.mouseExit (click); require (view.historyHeldForTest(), "mouse exit preserves hold");
        button (view, "history-live").onClick();
        require (view.footerStatusForTest() == "LIVE", "live state, not development label");
        button (view, "history-previous-tp").onClick();
        require (view.historyHeldForTest(), "one click holds newest peak");
        view.setHistory (changed); require (view.historyHeldForTest(), "held while live updates");
        button (view, "history-live").onClick(); require (! view.historyHeldForTest(), "one click resumes");
        button (view, "history-previous-tp").onClick();
        auto next = frame; ++next.meter.generation; view.setObservatoryFrame (next, true);
        require (! view.historyHeldForTest(), "UI reset releases stale history");
        view.setObservatoryFrame (frame, true);
        auto paused = frame; paused.signal_state = KIRIN_SIGNAL_STATE_INACTIVE;
        paused.meter.state = KIRIN_METER_SESSION_PAUSED; view.setObservatoryFrame (paused, true);
        require (view.footerStatusForTest() == "HOLD", "paused facts are held, not live");
        paused.signal_state = KIRIN_SIGNAL_STATE_BYPASSED; view.setObservatoryFrame (paused, true);
        require (view.footerStatusForTest() == "BYPASSED", "bypass distinguished");

        KirinRecordDisplay display {};
        const auto before = view.recordInvalidationsForTest();
        for (int tick = 0; tick < 1000; ++tick) view.setRecordDisplay (display, false);
        require (view.recordInvalidationsForTest() == before, "1000 absent Record ticks: zero body invalidations");
        display.phase = KIRIN_RECORD_DISPLAY_FINALIZING; display.generation = 1;
        view.setRecordDisplay (display, true);
        const auto finalizing = view.recordInvalidationsForTest();
        require (finalizing == before + 1, "enter finalizing redraws once");
        require (! button (view, "history-live").isVisible(), "Record owns its body");
        for (int tick = 0; tick < 1000; ++tick) view.setRecordDisplay (display, true);
        require (view.recordInvalidationsForTest() == finalizing, "1000 same finalizing ticks: zero redraws");
        display.phase = KIRIN_RECORD_DISPLAY_RESULT_HOLD; display.has_session = 1;
        display.session.lufs_i = -14.1; display.session.lra = std::numeric_limits<double>::quiet_NaN();
        view.setRecordDisplay (display, true);
        const auto held = view.recordInvalidationsForTest();
        view.setRecordDisplay (display, true);
        require (view.recordInvalidationsForTest() == held, "repeated NaN is unchanged");
        display.session.lufs_i = -14.0; view.setRecordDisplay (display, true);
        require (view.recordInvalidationsForTest() == held + 1, "changed result redraws");
        display.phase = KIRIN_RECORD_DISPLAY_UNAVAILABLE; view.setRecordDisplay (display, true);
        view.setRecordDisplay ({}, false);
        require (view.recordInvalidationsForTest() == held + 3, "failure and release redraw");
        view.setObservatoryFrame (frame, true);
        for (auto preset : observatory::sizePresets)
        {
            view.setSize (preset.width, preset.height);
            for (auto* child : view.getChildren())
                if (auto* control = dynamic_cast<observatory::Button*> (child))
                {
                    require (! control->getMouseClickGrabsKeyboardFocus(), "mouse click leaves DAW focus alone");
                    if (control->isEnabled()) require (control->getWantsKeyboardFocus(), "keyboard accessibility retained");
                    if (control->isVisible()) require (view.getLocalBounds().contains (control->getBounds()), "control in bounds");
                }
            const auto image = render (view);
            require (image.isValid(), "all size surfaces render");
            const auto out = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
            if (out.isNotEmpty())
            {
                auto stream = juce::File (out).getChildFile (juce::String (role == observatory::Role::pre ? "pre" : "post")
                    + "-always-visible-" + juce::String (preset.width) + ".png").createOutputStream();
                require (stream && stream->setPosition (0) && stream->truncate().wasOk(), "preview output");
                require (juce::PNGImageFormat().writeImageToStream (image, *stream), "preview png");
            }
        }
        view.setSize (600, 400);
        button (view, "history-previous-tp").onClick();
        const auto heldImage = render (view);
        const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
        if (output.isNotEmpty())
        {
            auto stream = juce::File (output).getChildFile (
                juce::String (role == observatory::Role::pre ? "pre" : "post") + "-history-held.png").createOutputStream();
            require (stream && stream->setPosition (0) && stream->truncate().wasOk(), "held output");
            require (juce::PNGImageFormat().writeImageToStream (heldImage, *stream), "held png");
        }
    }
    std::cout << "Always visible: PASS (thresholds, endpoint, freeze/reset, all sizes, 1000 unchanged ticks = 0 Record redraws)\n";
}
}
