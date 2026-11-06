#pragma once

#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaObservatoryContract.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaLanguage.h"
#include <cstring>
#include <vector>

namespace hypha::tests
{
inline void verifyFocusTrailHistoryPixels()
{
    // Identical latest frames, focus and chrome: only earlier exact deltas differ. Comparing
    // inside the Trail cannot pass on a changed main curve, heading, background or newest dot.
    std::vector<juce::Point<int>> sizes { { 355, 129 } };
    for (const auto& preset : observatory::sizePresets)
    {
        const auto layout = observatory::shellLayout (observatory::Role::post, preset, observatory::GuidePresence::absent);
        sizes.emplace_back (layout.body.width, layout.body.height);
    }
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        for (const auto size : sizes)
            for (const auto option : spectrum_average::choices)
            {
                const i18n::ScopedLanguage scoped (language);
                const auto render = [&] (float sign)
                {
                    SpectrumComponent component;
                    component.setSize (size.x, size.y);
                    component.setAverageChoice (option);
                    component.setSignalActive (true);
                    // Consecutive 30 Hz observations: a sparse poll is a gap, not a trace.
                    for (int index = 0; index <= 180; ++index)
                    {
                        auto frame = lowFrame (48'000 + index * 1600, true);
                        const auto delta = index == 180 ? 0.0f : sign * (3.0f + 3.0f * std::sin (index * 0.2f));
                        std::fill (std::begin (frame.display_db), std::end (frame.display_db), delta);
                        component.setSnapshot (frame);
                    }
                    if (component.focusTrailSizeForTest() != spectrum_focus::focusTrailCapacity)
                    { std::cerr << "FOCUS history: missing consecutive source frames\n"; std::exit (EXIT_FAILURE); }
                    const auto point = spectrum_geometry::dataPlotBoundsFor (component.getLocalBounds().toFloat()).getCentre();
                    component.mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(), point, {}, 0, 0, 0, 0, 0,
                        &component, &component, {}, point, {}, 0, false });
                    if (! component.hasFocusLock()) { std::cerr << "FOCUS history: no lock\n"; std::exit (EXIT_FAILURE); }
                    return component.createComponentSnapshot (component.getLocalBounds());
                };
                const auto positive = render (1.0f), negative = render (-1.0f);
                const auto region = spectrum_geometry::focusTrailBoundsFor (
                    juce::Rectangle<float> (0, 0, static_cast<float> (size.x), static_cast<float> (size.y))).reduced (3).toNearestInt();
                int changed = 0;
                for (int y = region.getY(); y < region.getBottom(); ++y)
                    for (int x = region.getX(); x < region.getRight(); ++x)
                        if (positive.getPixelAt (x, y) != negative.getPixelAt (x, y)) ++changed;
                if (changed < 8)
                {
                    std::cerr << "FOCUS history trace missing: " << size.x << 'x' << size.y << ", pixels=" << changed << '\n';
                    std::exit (EXIT_FAILURE);
                }
            }
}

inline void verifySpectrumAverageContract()
{
    verifyFocusTrailHistoryPixels();
    using Average = spectrum_average::Choice;
    const auto require = [] (bool ok, const char* message)
    {
        if (! ok) { std::cerr << "Spectrum average: " << message << '\n'; std::exit (EXIT_FAILURE); }
    };
    spectrum_average::Preference preference;
    juce::XmlElement empty ("State");
    preference.write (empty);
    require (! empty.hasAttribute ("freq_curve_average"), "default is absent from state");
    require (spectrum_average::Preference::read (empty) == Average::live, "old state retains LIVE");
    for (const auto option : spectrum_average::choices)
    {
        preference.set (option);
        juce::XmlElement xml ("State"); preference.write (xml);
        require (spectrum_average::Preference::read (xml) == option, "every average round trips");
    }
    for (const auto* invalid : { "", "NaN", "-1", "600 S", "1", "bogus" })
    {
        empty.setAttribute ("freq_curve_average", invalid);
        require (spectrum_average::Preference::read (empty) == Average::live, "unknown state retains LIVE");
    }
    for (const auto option : spectrum_average::choices)
    {
        if (option == Average::live) continue;
        const auto duration = spectrum_average::milliseconds (option);
        std::array<float, 2> one {}, split {};
        const std::array<float, 2> target { 12, -12 };
        spectrum_presentation::advanceAbsoluteCurve (one, target, duration, duration);
        spectrum_presentation::advanceAbsoluteCurve (split, target, duration / 2, duration);
        spectrum_presentation::advanceAbsoluteCurve (split, target, duration / 2, duration);
        require (std::abs (one[0] - 12 * (1 - std::exp (-1.0))) < 0.00001, "each time constant covers 63 percent of either direction");
        require (std::abs (one[0] + one[1]) < 0.00001 && std::abs (one[0] - split[0]) < 0.00001, "averaging is symmetric and independent of tick subdivision");
        const auto held = one;
        spectrum_presentation::advanceAverageCurve (one, target, NAN, duration);
        require (one == held, "invalid elapsed time does not move the curve");
    }
    // Even a short component body leaves space to lock either end of the curve with FOCUS.
    const juce::Rectangle<float> shortBody (0, 0, 355, 129);
    const auto focusedPlot = spectrum_geometry::dataPlotBoundsFor (shortBody, true);
    require (focusedPlot.getHeight() >= 15.0f, "a short focused plot keeps its hit area");
    require (! focusedPlot.intersects (spectrum_geometry::averageBoundsFor (spectrum_geometry::plotBoundsFor (shortBody), 1.25f)), "the average control stays clear of the focused plot");
    // Feed the real component, with the same source frames for all filters and channel modes.
    // Only the drawn arrays may differ; history, numeric readout and incoming bytes agree.
    for (const auto channel : { KIRIN_SPECTRUM_CHANNEL_LR, KIRIN_SPECTRUM_CHANNEL_MID, KIRIN_SPECTRUM_CHANNEL_SIDE })
        for (const bool low : { false, true })
            for (const auto option : spectrum_average::choices)
            {
                auto first = lowFrame (48'000, true);
                first.channel_mode = static_cast<uint8_t> (channel);
                if (! low) { first.min_hz = 10; first.max_hz = 22'000; first.aperture_samples = 4096; first.fft_size = 8192; first.approximate_below_hz = 35.15625f; }
                auto step = first;
                step.presentation_end_samples += 1600;
                for (auto& bin : step.post_dbfs) bin += 12.0f;
                for (auto& bin : step.display_db) bin += 12.0f;
                for (auto& bin : step.shape_db) bin += 12.0f;
                const auto original = step;
                SpectrumComponent component;
                component.setSize (576, 280);
                component.setLowScope (low);
                component.setAverageChoice (option);
                component.setSignalActive (true);
                component.setSnapshot (first);
                const auto start = component.displayedPostForTest (220);
                const auto deltaStart = component.displayedDeltaForTest (220);
                const auto scale = spectrum_geometry::visualScaleFor (component.getLocalBounds().toFloat());
                const auto mark = spectrum_geometry::markBoundsFor (spectrum_geometry::plotBoundsFor (component.getLocalBounds().toFloat()), scale);
                const auto point = mark.getCentre(); const auto eventTime = juce::Time::getCurrentTime();
                component.mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(), point, {}, 0, 0, 0, 0, 0,
                    &component, &component, eventTime, point, eventTime, 0, false });
                require (component.hasMark(), "MARK takes the source before the display average");
                const auto heldMark = component.markValueForTest (220);
                const auto now = component.curveUpdateTimeForTest();
                component.queueSnapshot (step);
                component.presentationTickAt (now + 500.0);
                const auto time = spectrum_average::milliseconds (option);
                const auto expected = time <= 0 ? 12.0 : 12.0 * -std::expm1 (-500.0 / time);
                require (std::abs (component.displayedPostForTest (220) - start - expected) < 0.01, "absolute response follows the selected time");
                const auto deltaExpected = time <= 0 ? 12.0 * -std::expm1 (-500.0 / 150.0) : expected;
                require (std::abs (component.displayedDeltaForTest (220) - deltaStart - (low && time <= 0 ? 12.0 : deltaExpected)) < 0.01,
                         "signed delta keeps the same symmetric average");
                require (std::abs (component.readoutDeltaForTest (220) - step.display_db[220]) < 0.001, "numeric delta bypasses averaging");
                require (component.hasMark() && std::abs (component.markValueForTest (220) - heldMark) < 0.000001f, "averaging never changes MARK");
                require (std::abs (component.focusTrailValueForTest (1, 220.5f / 256.0f) - step.display_db[220]) < 0.001, "FOCUS keeps exact source values");
                require (component.focusTrailSizeForTest() == 2, "FOCUS stores the two source frames");
                require (component.absoluteHistorySizeForTest() == 2, "absolute history bypasses averaging");
                require (std::memcmp (&step, &original, sizeof (step)) == 0, "public source bytes are unchanged");
                for (const auto offset : { 600.0, 700.0, 815.0, 950.0, 1310.0, 1410.0 })
                {
                    step.presentation_end_samples += 1600;
                    component.queueSnapshot (step);
                    component.presentationTickAt (now + offset);
                    const auto response = time <= 0 ? 12.0 : 12.0 * -std::expm1 (-offset / time);
                    const auto deltaResponse = low && time <= 0 ? 12.0
                        : 12.0 * -std::expm1 (-offset / (time <= 0 ? 150.0 : time));
                    require (std::abs (component.displayedPostForTest (220) - start - response) < 0.001,
                             "repeated irregular ticks count each elapsed millisecond once");
                    require (std::abs (component.displayedDeltaForTest (220) - deltaStart - deltaResponse) < 0.001,
                             "signed averaging uses actual time across scheduled and delayed ticks");
                }
                component.clearSnapshot();
                require (component.averageChoice() == option, "a view reset retains the choice");
            }
    // The simultaneous MID / SIDE view uses the identical display filter on both curves.
    for (const auto option : spectrum_average::choices)
    {
        KirinMidSideSpectrumView frame {};
        frame.status = KIRIN_SPECTRUM_ACTIVE; frame.has_data = 1; frame.channels = 2;
        frame.sample_rate = 48'000; frame.aperture_samples = 4096; frame.fft_size = 8192;
        frame.approximate_below_hz = 35.15625f; frame.min_hz = 10; frame.max_hz = 22000;
        std::fill (std::begin (frame.mid_dbfs), std::end (frame.mid_dbfs), -40.0f);
        std::fill (std::begin (frame.side_dbfs), std::end (frame.side_dbfs), -50.0f);
        SpectrumComponent component;
        component.setAverageChoice (option); component.setMidSideSnapshot (frame);
        const auto now = component.curveUpdateTimeForTest();
        // Establish the display clock without sleeping.
        component.presentationTickAt (now);
        for (auto& value : frame.mid_dbfs) value += 12;
        for (auto& value : frame.side_dbfs) value += 12;
        frame.presentation_end_samples = 1600;
        component.setMidSideSnapshot (frame); component.presentationTickAt (now + 500);
        const auto time = spectrum_average::milliseconds (option);
        const auto expected = time <= 0 ? 12.0 : 12.0 * -std::expm1 (-500.0 / time);
        require (std::abs (component.displayedPreForTest (220) + 40 - expected) < 0.01, "M/S follows the selected time");
        require (std::abs ((component.displayedPreForTest (220) + 40) - (component.displayedPostForTest (220) + 50)) < 0.001,
                 "MID and SIDE have equal responses");
        require (std::abs (component.readoutMidForTest (220) + 28) < 0.000001f && std::abs (component.readoutSideForTest (220) + 38) < 0.000001f, "M/S numeric values stay exact");
        for (const auto offset : { 600.0, 700.0, 815.0, 950.0, 1310.0, 1410.0 })
        {
            frame.presentation_end_samples += 1600;
            component.setMidSideSnapshot (frame); component.presentationTickAt (now + offset);
            const auto response = time <= 0 ? 12.0 : 12.0 * -std::expm1 (-offset / time);
            require (std::abs (component.displayedPreForTest (220) + 40 - response) < 0.001
                && std::abs (component.displayedPostForTest (220) + 50 - response) < 0.001,
                     "simultaneous M/S averages follow accumulated real time");
        }
    }
    // Default LIVE falling ballistics must use the same actual elapsed time.
    {
        SpectrumComponent component;
        auto frame = lowFrame (48'000, true);
        component.setSnapshot (frame);
        const auto now = component.curveUpdateTimeForTest();
        const auto initial = component.displayedPostForTest (220);
        for (auto& value : frame.post_dbfs) value -= 12;
        for (const auto offset : { 100.0, 200.0 })
        {
            frame.presentation_end_samples += 1600;
            component.queueSnapshot (frame); component.presentationTickAt (now + offset);
            require (std::abs (component.displayedPostForTest (220) - initial + offset * 0.04) < 0.001,
                     "LIVE release never counts scheduling lag twice");
        }
    }
    // SHAPE validity is per band, including holes received between two drawing ticks.
    for (const bool low : { false, true })
        for (const auto option : spectrum_average::choices)
        {
            SpectrumComponent component;
            component.setSize (576, 280); component.setLowScope (low); component.setAverageChoice (option);
            auto frame = lowFrame (48'000, true);
            if (! low) { frame.min_hz = 10; frame.max_hz = 22'000; frame.aperture_samples = 4096; frame.fft_size = 8192; frame.approximate_below_hz = 35.15625f; }
            frame.shape_has_energy = 1; frame.shape_energy_delta_db = 0;
            std::fill (std::begin (frame.shape_db), std::end (frame.shape_db), 4.0f);
            std::fill (std::begin (frame.shape_valid), std::end (frame.shape_valid), 1u);
            frame.shape_valid[220] = 0;
            component.setSnapshot (frame);
            const auto bounds = component.getLocalBounds().toFloat();
            const auto selector = spectrum_geometry::deltaModeBoundsFor (spectrum_geometry::plotBoundsFor (bounds), spectrum_geometry::visualScaleFor (bounds));
            const auto point = selector.getCentre().withX (selector.getRight() - 2);
            component.mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(), point, {}, 0, 0, 0, 0, 0,
                &component, &component, {}, point, {}, 0, false });
            require (component.isShapeObservationForTest(), "SHAPE selected through the real control");
            const auto now = component.curveUpdateTimeForTest();
            const auto deliver = [&] (double offset, bool tick = true)
            {
                frame.presentation_end_samples += 1600;
                component.queueSnapshot (frame);
                if (tick) component.presentationTickAt (now + offset);
            };
            frame.shape_valid[220] = 1; frame.shape_db[220] = 12;
            deliver (100);
            require (std::abs (component.displayedDeltaForTest (220) - 12) < 0.00001,
                     "a band's first measured SHAPE value is never averaged with missing zero");
            require (std::abs (component.displayedDeltaForTest (221) - 4) < 0.00001, "other bands keep their own history");
            frame.shape_db[220] = -12;
            deliver (200);
            const auto time = spectrum_average::milliseconds (option);
            const auto expected = low && time <= 0 ? -12.0 : 12.0 - 24.0 * -std::expm1 (-100.0 / (time <= 0 ? 150.0 : time));
            require (std::abs (component.displayedDeltaForTest (220) - expected) < 0.001, "continuous valid SHAPE values retain the selected average");
            frame.shape_valid[220] = 0; deliver (300);
            require (! component.displayedDeltaValidForTest (220), "a missing SHAPE band stays unavailable");
            frame.shape_valid[220] = 1; frame.shape_db[220] = -6; deliver (400);
            require (std::abs (component.displayedDeltaForTest (220) + 6) < 0.00001, "a returning band starts from its new value");
            frame.shape_valid[220] = 0; deliver (0, false);
            frame.shape_valid[220] = 1; frame.shape_db[220] = 9; deliver (500);
            require (std::abs (component.displayedDeltaForTest (220) - 9) < 0.00001, "a missing frame between ticks also breaks the average");
            frame.shape_has_energy = 0; deliver (600);
            require (! component.displayedDeltaValidForTest (221), "whole-spectrum missing energy stays unavailable");
            frame.shape_has_energy = 1; frame.shape_db[220] = -3; frame.shape_db[221] = 7; deliver (700);
            require (std::abs (component.displayedDeltaForTest (220) + 3) < 0.00001
                && std::abs (component.displayedDeltaForTest (221) - 7) < 0.00001, "all bands restart after missing energy");
        }
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const auto& size : observatory::sizePresets)
            for (const auto option : spectrum_average::choices)
            {
                SpectrumComponent component;
                const auto layout = observatory::shellLayout (observatory::Role::post, size, observatory::GuidePresence::absent);
                component.setSize (layout.body.width, layout.body.height);
                component.setPresentationContext (presentation::forEditor (size.width, size.height));
                component.setAverageChoice (option); component.setSignalActive (true);
                component.setSnapshot (lowFrame (48'000, true));
                const auto bounds = component.getLocalBounds().toFloat();
                const auto scale = spectrum_geometry::visualScaleFor (bounds);
                const auto outer = spectrum_geometry::plotBoundsFor (bounds);
                const auto control = spectrum_geometry::averageBoundsFor (outer, scale);
                require (control.isEmpty() == (size.width == 300), "100 percent has no average control");
                auto* button = dynamic_cast<juce::Button*> (component.findChildWithID ("spectrum-average"));
                require (button != nullptr && button->isVisible() == ! control.isEmpty(), "AVG visibility matches its geometry");
                if (! control.isEmpty())
                    require (button->getBounds() == control.toNearestInt() && button->getWantsKeyboardFocus()
                        && button->getButtonText() == juce::String ("AVG ") + spectrum_average::label (option),
                             "the keyboard and accessible action occupy the drawn AVG control");
                const text_style::ShownTextLog log;
                juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
                juce::Graphics graphics (image); component.paintEntireComponent (graphics, true);
                const auto label = juce::String ("AVG ") + spectrum_average::label (option);
                const auto previews = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_WINDOW_PREVIEWS", {});
                if (option == Average::seconds10 && previews.isNotEmpty())
                {
                    const juce::File root (previews); require (root.createDirectory().wasOk(), "preview directory");
                    const auto name = "freq-" + juce::String (size.width) + (language == i18n::Language::english ? "-en.png" : "-ja.png");
                    const auto output = root.getChildFile (name).createOutputStream();
                    require (output != nullptr && juce::PNGImageFormat().writeImageToStream (image, *output), "write average preview");
                }
                if (! control.isEmpty())
                {
                    require (bounds.contains (control) && ! control.intersects (spectrum_geometry::dataPlotBoundsFor (bounds)), "average control keeps the plot clear");
                    require (! control.intersects (spectrum_geometry::deltaModeBoundsFor (outer, scale)), "RAW and SHAPE stay separate");
                    require (log.texts().contains (label), "the whole average label is drawn in both languages");
                    const auto font = monoFont (presentation::forEditor (size.width, size.height), typography::TextRole::navigation, typography::Composition::visualization);
                    require (text_style::shownWidth (font, label) <= control.getWidth(), "the longest label fits");
                }
            }
    }
    std::cout << "Spectrum average: state, step response, exact history and all sizes in both languages PASS\n";
}
}
