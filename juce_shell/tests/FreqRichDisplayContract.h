#pragma once
#include "SpectrumPerformanceFixture.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumPeakFall.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
// Rules of the opt-in motion on the shipping component: every check runs, then all failures are named.
inline void verifyFreqRichMotionRules()
{
    int failures = 0;
    const auto check = [&failures] (bool ok, const char* what)
    { if (! ok) { std::cerr << "FREQ rich motion failed: " << what << '\n'; ++failures; } };
    namespace settings = display_settings;
    const auto slowView = [] (SpectrumComponent& view)
    {
        view.setSize (576, 280); view.setAbsoluteObservation (true); view.setSignalActive (true);
        settings::Values slow; slow.selected[settings::peak] = 2; view.setDisplaySettings (slow);
    };
    const auto quieter = [] (KirinSpectrumView frame, float db, int64_t later)
    { for (auto& value : frame.post_dbfs) value -= db; frame.presentation_end_samples += later; return frame; };
    {
        // PEAK SLOW chosen again while the line is still falling: the new peak starts on the line, not under it.
        SpectrumComponent view; slowView (view);
        view.setSnapshot (spectrumPerformanceFixture());
        view.queueSnapshot (quieter (spectrumPerformanceFixture(), 40, 1'600));
        view.presentationTickAt (view.curveUpdateTimeForTest() + 16);
        view.restartPeakLine();
        view.queueSnapshot (quieter (spectrumPerformanceFixture(), 40, 3'200));
        check (view.slowPeakForTest (128) >= view.displayedPostForTest (128) - 1.0e-4f,
               "a restarted slow peak is never below the visible line");
        view.presentationTickAt (view.curveUpdateTimeForTest() + 16);
        check (view.slowPeakForTest (128) >= view.displayedPostForTest (128) - 1.0e-4f
               && view.slowPeakForTest (128) >= view.pendingPostForTest (128), "nor on the frames that follow");
    }
    {
        // A value received long after the last drawn frame: the time that passed still counts as fall.
        double receivedAt = 1'000;
        SpectrumComponent view ([&receivedAt] { return receivedAt; }); slowView (view);
        const auto loud = spectrumPerformanceFixture().post_dbfs[128];
        view.setSnapshot (spectrumPerformanceFixture());
        const auto started = view.curveUpdateTimeForTest();
        check (std::abs (started - receivedAt) < 1.0e-6, "snapshot uses the supplied receipt clock");
        receivedAt += 5'000;
        view.queueSnapshot (quieter (spectrumPerformanceFixture(), 40, 1'600));
        view.presentationTickAt (started + 5'016);
        const auto expected = loud - 4.0f * (5.016f - 1.5f);
        check (std::abs (view.slowPeakForTest (128) - expected) < 0.2f,
               "after a stall the slow peak has fallen for all of the elapsed time");
    }
    {
        // Stopped: the slow peak line is still in the picture, with its clock standing still.
        SpectrumComponent view; slowView (view);
        view.setSnapshot (spectrumPerformanceFixture());
        view.queueSnapshot (quieter (spectrumPerformanceFixture(), 40, 1'600));
        view.presentationTickAt (view.curveUpdateTimeForTest() + 16);
        view.setSignalActive (false);
        const auto held = view.slowPeakForTest (128);
        const auto stopped = view.createComponentSnapshot (view.getLocalBounds(), false, 1.0f);
        view.presentationTickAt (view.curveUpdateTimeForTest() + 30'000);
        check (view.slowPeakForTest (128) == held, "the stopped line does not fall");
        view.restartPeakLine();
        const auto without = view.createComponentSnapshot (view.getLocalBounds(), false, 1.0f);
        int lineInk = 0;
        for (int y = 0; y < stopped.getHeight(); ++y)
            for (int x = 0; x < stopped.getWidth(); ++x) lineInk += stopped.getPixelAt (x, y) != without.getPixelAt (x, y);
        check (lineInk > 150, "the slow peak line stays visible while stopped");
    }
    {
        // FALL also limits how fast an averaged (AVG, LOW) line falls; 40 dB/s leaves averaging as it was.
        std::array<float, 2> averaged { 0, 0 }, established { 0, 0 }, slow { 0, 0 };
        const std::array<float, 2> target { -60, 6 };
        spectrum_presentation::advanceAverageCurve (averaged, target, 100.0, 250.0);
        spectrum_presentation::advanceAbsoluteCurve (established, target, 100.0, 250.0, true);
        spectrum_presentation::advanceAbsoluteCurve (slow, target, 100.0, 250.0, true, 6.0f);
        check (established == averaged, "the established fall leaves every averaged motion exactly as it was");
        check (std::abs (slow[0] + 0.6f) < 1.0e-4f && slow[1] == averaged[1], "FALL 6 dB/s limits an averaged fall and nothing else");
        float shown[2] {};
        for (const int fall : { 0, 3 })
        {
            SpectrumComponent view; view.setSize (576, 280); view.setAbsoluteObservation (true); view.setSignalActive (true);
            settings::Values chosen; chosen.selected[settings::fall] = fall; view.setDisplaySettings (chosen);
            view.setAverageChoice (spectrum_average::Choice::ms200);
            view.setSnapshot (spectrumPerformanceFixture());
            view.queueSnapshot (quieter (spectrumPerformanceFixture(), 40, 1'600));
            view.presentationTickAt (view.curveUpdateTimeForTest() + 100);
            shown[fall != 0] = view.displayedPostForTest (128);
        }
        check (shown[1] > shown[0] + 5.0f, "FREQ with AVG on falls more slowly when FALL 6 dB/s is chosen");
    }
    if (failures != 0) std::exit (1);
}

// The opt-in FREQ choices: SMOOTH that widens with frequency, FALL, FILL RICH and PEAK SLOW.
// Zero stays the established display in every field.
inline void verifyFreqRichDisplayContract()
{
    const auto check = [] (bool ok, const char* what)
    { if (! ok) { std::cerr << "FREQ rich display failed: " << what << '\n'; std::exit (1); } };
    namespace settings = display_settings;
    namespace motion = spectrum_presentation;
    using Bins = spectrum_display::Bins;
    {
        // Every field keeps three bits: all eight SMOOTH choices and the appended fields round trip.
        for (int field = 0; field < settings::fieldCount; ++field)
            for (int choice = 0; choice < 10; ++choice)
            {
                settings::Values values; values.selected[(size_t) field] = choice;
                settings::Preference preference; preference.set (values);
                check (preference.get().selected[(size_t) field] == (settings::valid (field, choice) ? choice : 0),
                       "a valid choice is kept and an unknown one restores the established display");
                juce::XmlElement xml ("state"); preference.write (xml);
                check (settings::Preference::read (xml) == preference.get(), "state round trips each choice");
                check (xml.hasAttribute (settings::keys[(size_t) field]) == (preference.get() != settings::Values {}),
                       "the established display writes nothing");
            }
        check (settings::valid (settings::smooth, 7) && ! settings::valid (settings::smooth, 8)
               && settings::valid (settings::peak, 2) && settings::valid (settings::fall, 3)
               && settings::valid (settings::fill, 1) && ! settings::valid (settings::fill, 2), "the new choices exist");
        check (settings::Values {}.fallDbPerSecond() == 40.0f && ! settings::Values {}.slowPeak()
               && ! settings::Values {}.richFill(), "zero is the established motion and look");
        check (settings::firstAction + settings::rate * 10 == 4050, "older menu actions keep their numbers");
    }
    {
        // Width: the chosen width in the lows, four times it in the highs, never past 1/2 octave.
        check (std::abs (motion::wideningOctaveAt (50, 1.0f / 12) - 1.0f / 12) < 1.0e-6f
               && std::abs (motion::wideningOctaveAt (200, 1.0f / 12) - 1.0f / 12) < 1.0e-6f
               && std::abs (motion::wideningOctaveAt (8000, 1.0f / 12) - 1.0f / 3) < 1.0e-6f
               && std::abs (motion::wideningOctaveAt (20000, 1.0f / 24) - 1.0f / 6) < 1.0e-6f
               && std::abs (motion::wideningOctaveAt (20000, 1.0f / 6) - 0.5f) < 1.0e-6f, "low and high widths match the labels");
        float previous = motion::wideningOctaveAt (10, 1.0f / 12);
        for (float hz = 10; hz < 22000; hz *= 1.01f)
        {
            const auto width = motion::wideningOctaveAt (hz, 1.0f / 12);
            check (width >= previous && width - previous < 0.004f, "the width grows without a step");
            previous = width;
        }
        check (motion::wideningHalfWidth (0.5f) == 0 && std::abs (motion::wideningHalfWidth (0.75f) - 0.5f) < 1.0e-6f
               && motion::wideningHalfWidth (1.0f) == 1.0f && motion::wideningHalfWidth (3.5f) == 3.5f,
               "the weight of a neighbour rises from nothing");
        for (const int choice : { 5, 6, 7 })
        {
            Bins flat; flat.fill (-30);
            const auto same = spectrum_display::spatial (flat, 10, 22000, choice);
            for (const auto value : same) check (std::abs (value + 30) < 1.0e-4f, "a level line stays level");
            // One band of 24 dB: its smoothed height may only change gradually from band to band.
            float last = 0; bool first = true, widerHigh = false;
            float low = 0;
            for (size_t band = 8; band < KIRIN_SPECTRUM_BAND_COUNT - 8; ++band)
            {
                Bins bins; bins.fill (0); bins[band] = 24;
                const auto height = spectrum_display::spatial (bins, 10, 22000, choice)[band];
                check (height > 0 && height <= 24, "smoothing only spreads what was measured");
                if (first) low = height;
                else check (std::abs (height - last) < 1.3f, "the texture of the line has no step along the axis");
                widerHigh = widerHigh || height < low * 0.6f;
                last = height; first = false;
            }
            check (widerHigh, "the highs are smoothed more widely than the lows");
            Bins bins; bins.fill (0); bins[240] = 24;
            spectrum_display::Validity mask; mask.fill (1); mask[239] = 0;
            const auto stopped = spectrum_display::spatial (bins, 10, 22000, choice, &mask);
            check (stopped[238] == 0 && stopped[239] == 0 && stopped[240] > 0 && stopped[241] > 0,
                   "widening smoothing stops at missing bands");
        }
    }
    {
        std::array<float, 4> shown { 0, 0, 0, 0 }, target { -60, -60, -1, 5 };
        motion::advanceAbsoluteCurve (shown, target, 500.0, 0.0, false, 6.0f);
        check (std::abs (shown[0] + 3) < 1.0e-4f && shown[2] == -1 && shown[3] == 5, "FALL 6 dB/s falls 3 dB in half a second");
        shown = { 0, 0, 0, 0 };
        motion::advanceAbsoluteCurve (shown, target, 500.0);
        check (std::abs (shown[0] + 20) < 1.0e-4f, "the established fall stays 40 dB/s");
    }
    {
        using Peak = motion::PeakFall<4>;
        using Values = Peak::Values;
        Peak peak; check (peak.empty(), "nothing is held before a value arrives");
        const Values loud { -10, -20, -30, -40 }, quiet { -50, -50, -50, -50 };
        Values line = loud;
        peak.accept (loud, line, 1000);
        line = quiet;                       // the drawn line may lag or lead: maxima never come from it
        peak.accept (quiet, line, 1033);
        check (peak.values() == loud, "the maximum is the value that was received");
        check (! peak.advance (line, 2499) && peak.values() == loud, "it stays for 1.5 s from the time it was received");
        check (peak.advance (line, 3000) && std::abs (peak.values()[0] + 12) < 1.0e-4f, "then it falls at 4 dB/s");
        Peak split; split.accept (loud, loud, 1000); split.accept (quiet, quiet, 1033);
        for (double now = 1040; now <= 3000; now += 7) split.advance (quiet, now);
        split.advance (quiet, 3000);
        check (std::abs (split.values()[0] - peak.values()[0]) < 1.0e-3f, "the fall depends on time, not on how often it is drawn");
        Values rising = quiet; rising[0] = -14;
        check (peak.advance (rising, 4000) && peak.values()[0] == -14, "it never falls below the visible line");
        check (peak.advance (quiet, 60000) && peak.values() == quiet, "nor below the last received value");
        check (! peak.advance (quiet, 61000), "a line resting on its floor reports no movement");
        peak.accept (loud, loud, 62000); peak.accept (quiet, quiet, 62033);
        peak.pause (quiet, 62500);
        check (! peak.advance (quiet, 90000) && peak.values() == loud, "while paused the clock stands still and the line remains");
        peak.resume (90000);
        check (! peak.advance (quiet, 90900) && peak.advance (quiet, 91500)
               && std::abs (peak.values()[0] + 12) < 1.0e-3f, "after the pause the same hold continues");
        peak.accept (loud, quiet, 100000);
        check (peak.values() == loud, "a new maximum restarts the hold");
        peak.clear(); check (peak.empty(), "clearing starts over");
    }
    {
        SpectrumComponent view; view.setSize (576, 280); view.setAbsoluteObservation (true); view.setSignalActive (true);
        settings::Values slow; slow.selected[settings::peak] = 2;
        view.setDisplaySettings (slow);
        auto frame = spectrumPerformanceFixture();
        const auto loud = frame.post_dbfs[128];
        view.setSnapshot (frame);
        check (! view.slowPeakEmptyForTest(), "PEAK SLOW starts with the first received value");
        for (auto& value : frame.post_dbfs) value -= 30;
        frame.presentation_end_samples += 1'600; view.queueSnapshot (frame);
        auto now = view.curveUpdateTimeForTest() + 16;
        view.presentationTickAt (now);
        check (view.displayedPostForTest (128) < loud, "the line itself has begun to fall");
        check (std::abs (view.slowPeakForTest (128) - spectrum_display::spatial ([&] { Bins b; auto f = spectrumPerformanceFixture();
                   std::copy (std::begin (f.post_dbfs), std::end (f.post_dbfs), b.begin()); return b; }(), 10, 22000, 0)[128]) < 1.0e-4f,
               "the held maximum is the received value as it is drawn, not the falling line");
        view.setSignalActive (false);
        check (! view.slowPeakEmptyForTest(), "stopping keeps the line");
        view.setSignalActive (true);
        frame.presentation_end_samples += 1'600; view.queueSnapshot (frame);   // resuming reseeds the curve, not the peak
        check (view.slowPeakForTest (128) > frame.post_dbfs[128] + 20, "the held line survives stop and resume");
        // Far later everything has fallen onto the line: further frames paint nothing.
        for (int tick = 0; tick < 3; ++tick) view.presentationTickAt (now += 60'000);
        const auto settled = view.tickRepaintsForTest();
        for (int tick = 0; tick < 120; ++tick) view.presentationTickAt (now += 8);
        check (view.tickRepaintsForTest() == settled, "a settled slow peak asks for no repaint");
        check (std::abs (view.slowPeakForTest (128) - view.displayedPostForTest (128)) < 0.01f, "it rests on the visible line");
        view.restartPeakLine(); check (view.slowPeakEmptyForTest(), "choosing PEAK SLOW again starts the line over");
        view.setSnapshot (spectrumPerformanceFixture()); check (! view.slowPeakEmptyForTest(), "and the next value begins it");
        auto other = spectrumPerformanceFixture(); other.channel_mode = KIRIN_SPECTRUM_CHANNEL_MID;
        for (auto& value : other.post_dbfs) value = -80;
        view.setSnapshot (other);
        check (view.slowPeakForTest (128) < -60, "another channel never inherits the held line");
        slow.selected[settings::smooth] = 6; view.setDisplaySettings (slow);
        check (view.slowPeakEmptyForTest(), "another band definition starts the line over");
        view.setDisplaySettings ({}); view.setSnapshot (spectrumPerformanceFixture());
        check (view.slowPeakEmptyForTest(), "the established PEAK keeps no slow line");
    }
    {
        // FILL RICH adds ink below and along POST in both views; FALL alone leaves a still picture as it was.
        for (const bool absolute : { true, false })
        {
            const auto render = [absolute] (settings::Values chosen)
            {
                SpectrumComponent view; view.setSize (576, 280); view.setAbsoluteObservation (absolute);
                view.setSignalActive (true); view.setDisplaySettings (chosen); view.setSnapshot (spectrumPerformanceFixture());
                return view.createComponentSnapshot (view.getLocalBounds(), false, 1.0f);
            };
            const auto differing = [] (const juce::Image& a, const juce::Image& b)
            {
                int count = 0;
                for (int y = 0; y < a.getHeight(); ++y)
                    for (int x = 0; x < a.getWidth(); ++x) count += a.getPixelAt (x, y) != b.getPixelAt (x, y);
                return count;
            };
            settings::Values rich, fall; rich.selected[settings::fill] = 1; fall.selected[settings::fall] = 3;
            const auto established = render ({});
            check (differing (established, render ({})) == 0, "the established picture repeats");
            check (differing (established, render (fall)) == 0, "FALL changes motion only");
            check (differing (established, render (rich)) > 400, "FILL RICH is visible in the picture");
        }
    }
    verifyFreqRichMotionRules();
    std::cout << "FREQ rich display: widening SMOOTH, FALL, FILL RICH and PEAK SLOW PASS\n";
}
}
