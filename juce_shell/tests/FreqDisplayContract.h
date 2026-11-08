#pragma once
#include "FreqFrameDeliveryTest.h"
#include "../src/HyphaSpectrumDisplay.h"
#include "../src/HyphaMonotoneCurve.h"
#include "../src/HyphaLevelReadout.h"
#include "../src/HyphaSpectrumMagnitudeChrome.h"
#include "../src/HyphaPsbPainter.h"

namespace hypha::tests
{
inline void verifyPsbAxisMotion()
{
    const auto check = [] (bool ok) { if (! ok) { std::cerr << "PSB axis continuity failed\n"; std::exit (1); } };
    for (const bool delta : { false, true })
    {
        SpectrumComponent view; view.setSize (576, 280); view.setAbsoluteObservation (! delta); view.setSignalActive (true);
        const auto bounds = view.getLocalBounds().toFloat();
        const auto point = spectrum_geometry::subviewBoundsFor (spectrum_geometry::plotBoundsFor (bounds),
                            spectrum_geometry::visualScaleFor (bounds)).getCentre();
        view.mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(), point, {}, 0, 0, 0, 0, 0,
                          &view, &view, {}, point, {}, 0, false });
        KirinPsbView frame {}; frame.status = KIRIN_SPECTRUM_ACTIVE; frame.has_data = 1;
        frame.is_delta = delta; frame.channels = 2; frame.sample_rate = 48000; frame.aperture_samples = 4800;
        frame.presentation_end_samples = 4800;
        const auto peak = delta ? 0.02 : 0.10;
        frame.shares[0] = peak;
        for (size_t i = 1; i < 20; ++i) frame.shares[i] = delta ? -peak / 19 : (1 - peak) / 19;
        view.setPsbSnapshot (frame);
        const auto region = psb_painter::dataBounds (bounds, presentation::defaultContext()).reduced (2).toNearestInt();
        auto time = juce::Time::getMillisecondCounterHiRes();
        for (const auto next : { peak + 0.01, peak })
        {
            const auto before = view.createComponentSnapshot (view.getLocalBounds());
            frame.presentation_end_samples += 4800; frame.shares[0] = next;
            for (size_t i = 1; i < 20; ++i) frame.shares[i] = delta ? -next / 19 : (1 - next) / 19;
            view.setPsbSnapshot (frame);
            const auto after = view.createComponentSnapshot (view.getLocalBounds());
            for (int y = region.getY(); y < region.getBottom(); ++y)
                for (int x = region.getX(); x < region.getRight(); ++x)
                    check (before.getPixelAt (x, y) == after.getPixelAt (x, y));
            const auto hover = juce::Point<float> (static_cast<float> (region.getX()) + region.getWidth() / 40.0f,
                                                   static_cast<float> (region.getCentreY()));
            const juce::MouseEvent event { juce::Desktop::getInstance().getMainMouseSource(), hover, {}, 0, 0, 0, 0, 0,
                                          &view, &view, {}, hover, {}, 0, false };
            view.mouseMove (event);
            {
                const text_style::ShownTextLog log;
                const auto readout = view.createComponentSnapshot (view.getLocalBounds());
                check (log.texts().joinIntoString ("|").contains (juce::String (next * 100, 1) + (delta ? " PP" : "%")));
                juce::ignoreUnused (readout);
            }
            view.mouseExit (event);
            const auto initial = view.displayedPsbCeilingForTest();
            const auto target = spectrum_display::PsbMotion::ceilingFor (
                [&] { std::array<double, 20> values; std::copy (std::begin (frame.shares), std::end (frame.shares), values.begin()); return values; }(), delta);
            view.presentationTickAt (time + 16);
            check (view.displayedPsbCeilingForTest() > std::min (initial, target)
                && view.displayedPsbCeilingForTest() < std::max (initial, target));
            const text_style::ShownTextLog axisLog;
            const auto animated = view.createComponentSnapshot (view.getLocalBounds());
            bool axisFound = false;
            for (const auto& text : axisLog.texts())
                if (text.startsWith (delta ? "+/- " : "0 - "))
                {
                    axisFound = true;
                    const auto number = text.fromFirstOccurrenceOf (delta ? "+/- " : "0 - ", false, false)
                        .upToFirstOccurrenceOf (delta ? " PP" : "%", false, false);
                    const auto dot = number.indexOfChar ('.');
                    check (text.length() <= 12 && (dot < 0 || number.length() - dot - 1 <= 1));
                }
            check (axisFound);
            check (animated.getPixelAt (region.getCentreX(), region.getBottom() - 2).getAlpha() > 0);
            if (const auto* directory = std::getenv ("KIRIN_HYPHA_DISPLAY_PREVIEWS"))
            {
                const juce::File folder (directory); check (folder.createDirectory().wasOk());
                const auto file = folder.getChildFile (juce::String (delta ? "psb-delta-" : "psb-post-")
                    + (next > peak ? "rising.png" : "falling.png"));
                file.deleteFile(); juce::FileOutputStream stream (file);
                check (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (animated, stream));
            }
            view.presentationTickAt (time + 2000);
            time += 2000;
        }
    }
}

inline void verifyFreqDisplayContract()
{
    const auto check = [] (bool ok, const char* text)
    { if (! ok) { std::cerr << "Display contract: " << text << '\n'; std::exit (1); } };
    verifyPsbAxisMotion();
    // Use the shipping view's ingestion and frame entry points, without timer sleeps.
    for (const bool low : { false, true })
    {
        SpectrumComponent view;
        view.setLowScope (low); view.setAbsoluteObservation (true); view.setSignalActive (true);
        auto source = lowFrame (48'000, false);
        if (! low) { source.min_hz = 10; source.max_hz = 22000; source.aperture_samples = 4096;
            source.fft_size = 8192; source.approximate_below_hz = 35.15625f; }
        std::fill (std::begin (source.post_dbfs), std::end (source.post_dbfs), -60.0f);
        view.setSnapshot (source);
        const auto start = view.curveUpdateTimeForTest();
        source.presentation_end_samples += 1600;
        std::fill (std::begin (source.post_dbfs), std::end (source.post_dbfs), -20.0f);
        const auto original = source;
        view.queueSnapshot (source);
        view.presentationTickAt (start + 16);
        const auto first = view.displayedPostForTest (128);
        view.presentationTickAt (start + 32);
        check (first > -60 && first < -20 && view.displayedPostForTest (128) > first,
               "VBlank moves between source polls, including rising absolute curves");
        if (low) check (first < -55, "LOW follows slowly enough to read");
        view.presentationTickAt (start + 510);
        check (view.readoutSideForTest (128) == source.post_dbfs[128], "readouts retain raw POST");
        check (std::memcmp (&source, &original, sizeof (source)) == 0, "source bytes are immutable");
        auto missing = source; missing.post_has_data = 0; missing.has_data = 0;
        missing.status = KIRIN_SPECTRUM_WARMING_UP;
        view.queueSnapshot (missing); view.queueSnapshot (source);
        check (view.displayedPostForTest (128) == -20, "whole-frame missing data breaks interpolation");
        source.presentation_end_samples += 3200;
        std::fill (std::begin (source.post_dbfs), std::end (source.post_dbfs), -50.0f);
        view.queueSnapshot (source);
        check (view.displayedPostForTest (128) == -50, "clock gaps seed a new presentation");
    }
    for (const bool low : { false, true })
        for (const bool absolute : { false, true })
            for (const auto mode : { KIRIN_SPECTRUM_CHANNEL_LR, KIRIN_SPECTRUM_CHANNEL_MID, KIRIN_SPECTRUM_CHANNEL_SIDE })
            {
                SpectrumComponent view; view.setLowScope (low); view.setAbsoluteObservation (absolute);
                view.setAverageChoice (spectrum_average::Choice::seconds10);
                auto frame = lowFrame (48000, true); frame.channel_mode = mode;
                if (! low) { frame.min_hz = 10; frame.max_hz = 22000; frame.aperture_samples = 4096;
                    frame.fft_size = 8192; frame.approximate_below_hz = 35.15625f; }
                std::fill (std::begin (frame.post_dbfs), std::end (frame.post_dbfs), -60);
                std::fill (std::begin (frame.display_db), std::end (frame.display_db), 0);
                KirinSpectrumBatch batch {}; batch.count = 1; batch.frames[0] = batch.latest = frame;
                view.setBatch (batch, 11, 22);
                const auto start = view.curveUpdateTimeForTest();
                batch.count = 8;
                for (int tick = 0; tick < 8; ++tick)
                {
                    frame.presentation_end_samples = 48000 + (17 + tick) * 1600;
                    std::fill (std::begin (frame.post_dbfs), std::end (frame.post_dbfs), -20);
                    std::fill (std::begin (frame.display_db), std::end (frame.display_db), 12);
                    batch.frames[tick] = frame;
                }
                batch.latest = frame; view.setBatch (batch, 11, 22);
                check (view.displayedPostForTest (128) == -60 && view.displayedDeltaForTest (128) == 0,
                       "an eight-frame recovery ring eviction is not missing analysis");
                view.presentationTickAt (start + 16);
                const auto fraction = -std::expm1 (-16.0 / 10000);
                check (std::abs (view.displayedPostForTest (128) + 60 - 40 * fraction) < 0.001,
                       "FULL/LOW LR/MID/SIDE keep AVG through GUI stalls");
                if (! absolute) check (std::abs (view.displayedDeltaForTest (128) - 12 * fraction) < 0.001,
                                       "paired deltas keep the same display average");
                view.setBatch (batch, 33, 44);
                check (view.displayedPostForTest (128) == -20,
                       "a gap that fell out of the recovery ring still cuts interpolation");
                check (view.averageChoice() == spectrum_average::Choice::seconds10, "gap recovery keeps AVG choice");
            }
    // The first poll of a segment must recover its earlier facts even though its curve snaps
    // to the latest frame. A peak only in the first frame cannot be recovered by later polls.
    for (const bool low : { false, true })
        for (const bool absolute : { false, true })
            for (const auto mode : { KIRIN_SPECTRUM_CHANNEL_LR, KIRIN_SPECTRUM_CHANNEL_MID, KIRIN_SPECTRUM_CHANNEL_SIDE })
            {
                SpectrumComponent view; view.setLowScope (low); view.setAbsoluteObservation (absolute);
                view.setAverageChoice (spectrum_average::Choice::seconds10);
                auto frame = lowFrame (48000, true); frame.channel_mode = mode;
                if (! low) { frame.min_hz = 10; frame.max_hz = 22000; frame.aperture_samples = 4096;
                    frame.fft_size = 8192; frame.approximate_below_hz = 35.15625f; }
                for (uint64_t segment = 1; segment <= 2; ++segment)
                {
                    KirinSpectrumBatch batch {}; batch.count = KIRIN_SPECTRUM_BATCH_CAPACITY;
                    for (uint32_t at = 0; at < batch.count; ++at)
                    {
                        frame.presentation_end_samples = 48000 + static_cast<int64_t> (segment * 32 + at) * 1600;
                        std::fill (std::begin (frame.post_dbfs), std::end (frame.post_dbfs), at == 0 ? -6.0f : -40.0f);
                        std::fill (std::begin (frame.display_db), std::end (frame.display_db), at == 0 ? 11.0f : 3.0f);
                        batch.frames[at] = frame;
                    }
                    batch.latest = frame;
                    const auto original = batch;
                    view.setBatch (batch, segment * 11, segment * 22);
                    const auto count = static_cast<size_t> (segment * batch.count);
                    check (view.absoluteHistorySizeForTest() == count && view.absolutePeakHoldForTest (128) == -6,
                           "first and changed segment polls retain every recovered absolute frame and its brief peak");
                    if (! absolute)
                        check (view.focusTrailSizeForTest() == count
                            && view.focusTrailValueForTest (count - batch.count, 128.5f / 256) == 11,
                               "Focus Trail retains the first exact delta of each recovered segment");
                    check (view.displayedPostForTest (128) == -40 && view.readoutSideForTest (128) == -40
                        && (absolute || view.displayedDeltaForTest (128) == 3),
                           "a new segment seeds its latest curve and raw readout without blending old observations");
                    view.setBatch (batch, segment * 11, segment * 22);
                    check (view.absoluteHistorySizeForTest() == count
                        && (absolute || view.focusTrailSizeForTest() == count), "repeated polls do not duplicate recovered history");
                    check (std::memcmp (&batch, &original, sizeof (batch)) == 0, "batch recovery preserves source bytes");
                }
                const auto start = view.curveUpdateTimeForTest();
                frame.presentation_end_samples += 1600;
                std::fill (std::begin (frame.post_dbfs), std::end (frame.post_dbfs), -20);
                view.queueSnapshot (frame, absolute ? 22 : 44); view.presentationTickAt (start + 16);
                check (view.displayedPostForTest (128) > -40 && view.displayedPostForTest (128) < -20
                    && view.averageChoice() == spectrum_average::Choice::seconds10,
                       "the next continuous observation resumes the selected display average");
            }
    {
        SpectrumComponent view; view.setLowScope (true); view.setAbsoluteObservation (true); view.setSignalActive (true);
        auto source = lowFrame (48'000, false); view.setSnapshot (source);
        const auto now = view.curveUpdateTimeForTest();
        const auto original = view.peakLabelsForTest();
        check (original.count > 0, "LOW exposes measured peak labels through its actual view");
        std::reverse (std::begin (source.post_dbfs), std::end (source.post_dbfs));
        for (int frame = 1; frame <= 60; ++frame)
        {
            if (frame % 2 == 0) { source.presentation_end_samples += 1600; view.queueSnapshot (source); }
            view.presentationTickAt (now + frame * (1000.0 / 60));
            if (frame < 60)
                check (view.peakLabelsForTest().found[0].hz == original.found[0].hz,
                       "the connected LOW view holds its note set between source frames");
        }
        check (view.peakLabelsForTest().found[0].hz != original.found[0].hz,
               "the connected LOW view replaces the label set at one second");
    }
    {
        SpectrumComponent view; view.setAbsoluteObservation (true); view.setSignalActive (true);
        view.setSize (576, 280);
        const auto bounds = view.getLocalBounds().toFloat();
        const auto point = spectrum_geometry::subviewBoundsFor (spectrum_geometry::plotBoundsFor (bounds),
                            spectrum_geometry::visualScaleFor (bounds)).getCentre();
        view.mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(), point, {}, 0, 0, 0, 0, 0,
                          &view, &view, {}, point, {}, 0, false });
        check (view.isPsbObservation(), "the actual PSB control selects the interpolated route");
        KirinPsbView frame {}; frame.status = KIRIN_SPECTRUM_ACTIVE; frame.has_data = 1;
        frame.channels = 2; frame.sample_rate = 48000; frame.aperture_samples = 4800;
        frame.presentation_end_samples = 4800;
        for (auto& share : frame.shares) share = 0.05;
        view.setPsbSnapshot (frame);
        frame.presentation_end_samples += 4800; frame.shares[0] = 0.10; frame.shares[1] = 0;
        const auto now = juce::Time::getMillisecondCounterHiRes(); view.setPsbSnapshot (frame);
        view.presentationTickAt (now + 16); const auto first = view.displayedPsbForTest (0);
        view.presentationTickAt (now + 32);
        check (first > 0.05 && first < 0.1 && view.displayedPsbForTest (0) > first,
               "10 Hz PSB values advance on frames between observations");
        frame.status = KIRIN_SPECTRUM_WARMING_UP; frame.has_data = 0; view.setPsbSnapshot (frame);
        frame.status = KIRIN_SPECTRUM_ACTIVE; frame.has_data = 1; frame.shares[0] = 0.05; frame.shares[1] = 0.05;
        view.setPsbSnapshot (frame);
        check (view.displayedPsbForTest (0) == 0.05, "PSB never interpolates across a missing window");
    }
    {
        std::array<float, 5> x { 0, 1, 2, 3, 4 }, y { 0, 10, -10, 4, 0 };
        std::array<uint8_t, 5> valid { 1, 1, 0, 1, 1 };
        const auto path = monotone_curve::make (x, y, &valid);
        juce::Path::Iterator iterator (path);
        int starts = 0, cubics = 0;
        while (iterator.next())
        {
            starts += iterator.elementType == juce::Path::Iterator::startNewSubPath;
            if (iterator.elementType == juce::Path::Iterator::cubicTo)
            {
                ++cubics;
                check (iterator.y1 >= 0 && iterator.y1 <= 10 && iterator.y2 >= 0 && iterator.y2 <= 10,
                       "cubic control points cannot overshoot the valid samples");
            }
        }
        check (starts == 2 && cubics == 2, "missing frequency bands stay disconnected");
        std::array<float, 256> straightX {}, straightY {};
        for (size_t i = 0; i < straightX.size(); ++i) { straightX[i] = static_cast<float> (i); straightY[i] = straightX[i] * 0.2f; }
        const auto straight = monotone_curve::make (straightX, straightY);
        juce::Path::Iterator segments (straight); int segmentCount = 0;
        while (segments.next()) segmentCount += segments.elementType == juce::Path::Iterator::cubicTo;
        check (segmentCount == 1, "invisible collinear detail is one monotone cubic segment");
        const std::array<uint8_t, 256>* everyBand = nullptr;
        const auto compact = monotone_curve::make (straightX, straightY, everyBand, true);
        juce::Path::Iterator compactSegments (compact); int lines = 0;
        while (compactSegments.next())
        {
            check (compactSegments.elementType != juce::Path::Iterator::cubicTo,
                   "an exactly straight drawing avoids cubic tessellation");
            lines += compactSegments.elementType == juce::Path::Iterator::lineTo;
        }
        check (lines == 1 && compact.getBounds() == straight.getBounds(), "straight rendering retains the measured geometry");
        straightY[128] = 90;
        const auto peak = monotone_curve::make (straightX, straightY);
        juce::Path::Iterator points (peak); bool keptPeak = false;
        while (points.next()) if (points.elementType == juce::Path::Iterator::cubicTo && std::abs (points.y3 - 90) < 0.0001f) keptPeak = true;
        check (keptPeak, "geometry reduction keeps even a one-band peak");
        spectrum_display::Bins bins; bins.fill (0); bins[128] = 24;
        spectrum_display::Validity mask; mask.fill (1); mask[127] = 0;
        const auto smooth = spectrum_display::spatial (bins, 20, 300, 4, &mask);
        check (smooth[126] == 0 && smooth[128] < 24 && smooth[128] > 0,
               "octave smoothing stops at missing bands");
    }
    {
        spectrum_display::PeakReadout read;
        const auto source = lowFrame (48'000, false);
        spectrum_display::Bins values;
        std::copy (std::begin (source.post_dbfs), std::end (source.post_dbfs), values.begin());
        check (read.update (values, 20, 300, 0), "first peak labels are available");
        const auto held = read.peaks;
        for (int tick = 1; tick < 60; ++tick)
        {
            values[100] += 0.1f;
            check (! read.update (values, 20, 300, tick * (1000.0 / 60)), "labels hold across every frame");
            check (read.peaks.found[0].hz == held.found[0].hz && read.peaks.found[0].level == held.found[0].level,
                   "note frequency level and order remain one reading");
        }
        check (read.update (values, 20, 300, 1000), "new label set at one second");
        std::cout << "LOW label interval: 1000 ms at 60 presentation frames/s\n";
    }
    {
        SpectrumComponent view; view.setSize (576, 280); view.setLowScope (true); view.setSignalActive (true);
        display_settings::Values settings; settings.selected[display_settings::smooth] = 4;
        view.setDisplaySettings (settings);
        auto source = lowFrame (48'000, true);
        for (size_t band = 0; band < KIRIN_SPECTRUM_BAND_COUNT; ++band)
        { source.pre_dbfs[band] = -45; source.post_dbfs[band] = -40; source.display_db[band] = 5; }
        source.post_dbfs[128] = -10; source.display_db[128] = 35;
        view.setSnapshot (source);
        check (view.displayedDeltaForTest (128) < 35 && view.readoutDeltaForTest (128) == 35,
               "frequency smoothing is confined to the drawing");
        check (view.absolutePeakHoldForTest (128) == -10,
               "the measured peak history bypasses spatial processing");
        const auto bounds = view.getLocalBounds().toFloat();
        const auto point = spectrum_geometry::markBoundsFor (spectrum_geometry::plotBoundsFor (bounds),
                            spectrum_geometry::visualScaleFor (bounds)).getCentre();
        view.mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(), point, {}, 0, 0, 0, 0, 0,
                          &view, &view, {}, point, {}, 0, false });
        check (view.hasMark() && view.markValueForTest (128) == 35,
               "the actual MARK control takes the raw measured delta");
        const auto before = view.createComponentSnapshot (view.getLocalBounds());
        settings.selected[display_settings::tilt] = 3; view.setDisplaySettings (settings);
        const auto after = view.createComponentSnapshot (view.getLocalBounds());
        for (int y = 0; y < before.getHeight(); ++y)
            for (int x = 0; x < before.getWidth(); ++x)
                check (before.getPixelAt (x, y) == after.getPixelAt (x, y), "TILT never changes a delta display");
        check (view.markValueForTest (128) == 35
            && std::abs (view.focusTrailValueForTest (0, 128.5f / 256) - 35) < 0.0001,
               "settings leave MARK and measured focus history intact");
    }
    {
        level_readout::Display read;
        read.setInterval (500);
        KirinObservatoryFrame frame {}; frame.meter.lufs_m = -20;
        check (read.read (frame, 0).meter.lufs_m == -20, "first LEVEL value");
        frame.meter.lufs_m = -10;
        check (read.read (frame, 499).meter.lufs_m == -20 && read.read (frame, 500).meter.lufs_m == -10,
               "LEVEL holds a factual value for the selected interval");
        frame.meter.generation++; frame.meter.lufs_m = -30;
        check (read.read (frame, 501).meter.lufs_m == -30, "LEVEL session boundaries replace the hold");
    }
    {
        level_readout::Display read; read.setInterval (1000);
        KirinObservatoryFrame frame {}; frame.meter.lufs_m = -20; read.read (frame, 0);
        KirinWatchDisplay missing {}, first {}; first.current.lufs_s = -15.8;
        read.watch (missing, false);
        check (read.watch (first, true).current.lufs_s == -15.8, "first Watch publication never displays cached invalid zero");
        auto next = first; next.current.lufs_s = -8.6;
        check (read.watch (next, true).current.lufs_s == -15.8, "valid Watch values follow the selected hold");
        read.watch (missing, false);
        check (read.watch (next, true).current.lufs_s == -8.6, "Watch availability recovery adopts a fresh reading");
        frame.lra_state = KIRIN_LRA_READY; frame.meter.lufs_m = -10;
        check (read.read (frame, 1).meter.lufs_m == -10, "LRA readiness is an immediate presentation boundary");
    }
    {
        level_readout::Display read; read.setInterval (1000);
        KirinObservatoryFrame frame {}; frame.meter.lufs_m = -20;
        frame.meter.lufs_s = frame.meter.channel_true_peak_dbtp[0] = frame.delta.lufs_s = NAN;
        read.read (frame, 0);
        frame.meter.lufs_m = -10; frame.meter.lufs_s = -17;
        frame.meter.channel_true_peak_dbtp[0] = -1; frame.delta.lufs_s = 2;
        const auto& first = read.read (frame, 1);
        check (first.meter.lufs_s == -17 && first.meter.channel_true_peak_dbtp[0] == -1
            && first.delta.lufs_s == 2 && first.meter.lufs_m == -20,
            "individual availability is immediate without releasing other finite numbers");
        frame.meter.lufs_s = frame.meter.channel_true_peak_dbtp[0] = frame.delta.lufs_s = NAN;
        const auto& missing = read.read (frame, 2);
        check (std::isnan (missing.meter.lufs_s) && std::isnan (missing.meter.channel_true_peak_dbtp[0])
            && std::isnan (missing.delta.lufs_s), "lost metrics cannot retain a numerical reading");
        KirinWatchDisplay watch {}; watch.current.crest = watch.maximum.psr = NAN;
        read.watch (watch, true); watch.current.crest = 12; watch.maximum.psr = 14;
        check (read.watch (watch, true).current.crest == 12 && read.watch (watch, true).maximum.psr == 14,
               "Watch current and maximum adopt individual returning values");
        watch.current.crest = watch.maximum.psr = NAN;
        check (std::isnan (read.watch (watch, true).current.crest)
            && std::isnan (read.watch (watch, true).maximum.psr), "Watch invalidation is immediate");
        auto summary = frame.meter; summary.max_true_peak = NAN;
        read.summary (summary); summary.max_true_peak = -0.8;
        check (read.summary (summary).max_true_peak == -0.8, "summary availability is immediate");
        summary.max_true_peak = NAN;
        check (std::isnan (read.summary (summary).max_true_peak), "summary cannot retain an invalid maximum");
        check (read.read (frame, 999).meter.lufs_m == -20 && read.read (frame, 1000).meter.lufs_m == -10,
               "availability changes do not postpone the ordinary number deadline");
    }
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        for (const auto& size : observatory::sizePresets)
            for (const bool absolute : { false, true })
            {
                const i18n::ScopedLanguage scoped (language);
                SpectrumComponent view;
                const auto body = ui_contract::spectrumPlotBounds (size.width, size.height);
                view.setSize (body.width, body.height);
                view.setPresentationContext (presentation::forEditor (size.width, size.height));
                view.setLowScope (true); view.setAbsoluteObservation (absolute); view.setSignalActive (true);
                view.setAverageChoice (SpectrumComponent::Average::seconds10);
                display_settings::Values settings;
                settings.selected[display_settings::range] = 3; settings.selected[display_settings::tilt] = 2;
                view.setDisplaySettings (settings); view.setSnapshot (lowFrame (48'000, true));
                const auto bounds = view.getLocalBounds().toFloat();
                const auto scale = spectrum_geometry::visualScaleFor (bounds);
                const auto plot = spectrum_geometry::dataPlotBoundsFor (bounds, false, 120, absolute);
                const auto context = presentation::forEditor (size.width, size.height);
                if (absolute || ! spectrum_geometry::viewOnly (scale))
                {
                    const auto font = monoFont (context, typography::TextRole::axis, typography::Composition::visualization);
                    const auto width = spectrum_magnitude_chrome::labelWidth (scale, 120);
                    check (text_style::shownWidth (font, "-120") <= width, "120 dB axis fits at every size and language");
                    if (absolute) check (width <= plot.getX(), "magnitude label stays outside the absolute plot");
                    else check (plot.getRight() + 3 * scale + width <= bounds.getRight(), "right magnitude label stays inside the component");
                }
                const text_style::ShownTextLog log;
                const auto image = view.createComponentSnapshot (view.getLocalBounds());
                const auto text = log.texts().joinIntoString ("|");
                check (! absolute || text.contains ("4.5"), "the actual view names absolute tilt without changing delta");
                if (absolute) check (text.contains ("/oct") || text.contains ("TILT"), "non-default tilt has a short visible label");
                juce::ignoreUnused (image);
            }
    verifyFreqFrameDeliveryIfRequested();
    std::cout << "Display interpolation, monotone holes, LOW labels and LEVEL cadence PASS\n";
}
}
