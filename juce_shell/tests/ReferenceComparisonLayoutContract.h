#pragma once

#include "../src/HyphaReferenceComponent.h"
#include "../src/HyphaReferenceWindowMaterial.h"
#include "ReferenceGuideStates.h"

#include <cstdlib>
#include <iostream>

namespace hypha::tests::reference_comparison_layout
{
inline void expect (bool ok, const juce::String& why)
{
    if (ok) return;
    std::cerr << "Reference comparison layout: " << why << '\n';
    std::exit (EXIT_FAILURE);
}

inline std::shared_ptr<const reference_audition::VisualTimeline> timeline()
{
    auto source = std::make_shared<reference_audition::RuntimeSource>();
    source->audio = { 48000, 2, 48000 * 20 };
    auto measurement = std::make_shared<reference_audition::RuntimeDetailedMeasurement>();
    measurement->waveform.emplace(); measurement->waveform->framesPerBin = 4800;
    measurement->waveform->samplePeakMillidbfs.resize (2);
    measurement->waveform->rmsMillidbfs.resize (2);
    auto result = std::make_shared<reference_audition::VisualTimeline>();
    result->binding.source = source; result->binding.overview = measurement;
    result->binding.key = "axis-layout-fixture"; result->binding.channels = 2;
    result->binding.hostRate = 48000; result->binding.aligned = true;
    result->pass = result->revision = 1; result->hop = 4800;
    for (int index = 0; index < 200; ++index)
    {
        const auto amplitude = 0.35 + 0.05 * std::sin (index * 0.2);
        reference_audition::VisualPairBin pair;
        pair.pass = 1; pair.a.frames = pair.b.frames = 4800;
        pair.a.short_lufs = -14.0; pair.b.short_lufs = -13.5;
        pair.a.crest_db = 7.0; pair.b.crest_db = 6.8;
        for (size_t channel = 0; channel < 2; ++channel)
        {
            pair.a.peak[channel] = amplitude * 0.9; pair.b.peak[channel] = amplitude;
            pair.a.rms[channel] = amplitude * 0.4; pair.b.rms[channel] = amplitude * 0.45;
            measurement->waveform->samplePeakMillidbfs[channel].push_back (
                std::llround (20000.0 * std::log10 (amplitude)));
            measurement->waveform->rmsMillidbfs[channel].push_back (
                std::llround (20000.0 * std::log10 (amplitude * 0.45)));
        }
        result->bins.push_back (pair);
    }
    return result;
}

inline juce::Image render (juce::Component& component)
{
    juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    juce::Graphics g (image); g.fillAll (BG); component.paintEntireComponent (g, true);
    return image;
}

inline int brightness (juce::Colour colour)
{
    return colour.getRed() + colour.getGreen() + colour.getBlue();
}

inline void requireVisibleAxisInk (reference_ui::ComparisonView& view, const juce::Image& actual,
                                  presentation::Context context, bool crest)
{
    juce::Image glass (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
    juce::Graphics plain (glass); plain.fillAll (BG);
    const key_light::Scope light (view);
    surface_material::paintObservationWell (plain, view.getLocalBounds().toFloat(), false);
    reference_ui::window_material::paintInterior (plain, view.getLocalBounds().toFloat(), view.getLocalBounds().toFloat());
    const auto& geometry = view.visualLayout();
    for (int index = 0; index < geometry.axisLabelCount; ++index)
    {
        const auto& label = geometry.axisLabels[size_t (index)];
        // Fixed, real bin values give -17..-10 LUFS (or 3..10 dB crest). Compare the visible
        // glyph footprint, so an empty/ellipsized label cannot satisfy a bounds-only check.
        const auto value = crest ? 10.0 - 7.0 * label.fraction : -10.0 - 7.0 * label.fraction;
        juce::Image mask (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
        juce::Graphics text (mask); text.setColour (juce::Colours::white);
        text.setFont (labelFont (context, typography::TextRole::captureMetadata,
                                typography::Composition::visualization));
        text_style::drawText (text, juce::String (value, 1), label.bounds, juce::Justification::centredLeft);
        int glyphs = 0, visible = 0;
        const auto bounds = label.bounds.getSmallestIntegerContainer();
        for (int y = bounds.getY(); y < bounds.getBottom(); ++y)
            for (int x = bounds.getX(); x < bounds.getRight(); ++x)
                if (mask.getPixelAt (x, y).getAlpha() >= 160)
                {
                    ++glyphs;
                    visible += brightness (actual.getPixelAt (x, y)) - brightness (glass.getPixelAt (x, y)) > 30;
                }
        expect (glyphs > 5 && visible * 5 >= glyphs * 4, "each reserved axis value is actually readable ink");
    }
}

inline void requireGeometry (reference_ui::ComparisonView& view, presentation::Context context)
{
    const auto& geometry = view.visualLayout();
    expect (view.getLocalBounds().toFloat().contains (geometry.waveform)
                && geometry.waveform.getHeight() > 0, "the A/B overview remains inside its real pane");
    for (const auto* id : { "reference-loudness", "reference-crest" })
        expect (view.findChildWithID (id)->isVisible() == geometry.detailed(),
                "detail controls and actual graph admission agree");
    if (!geometry.detailed())
    {
        expect (geometry.axisLabelCount == 0, "a compact overview never paints orphaned axis values");
        return;
    }
    expect (geometry.waveform.getHeight() >= reference_ui::comparison_layout::minimumWaveform
                && !geometry.waveform.intersects (geometry.tabs)
                && !geometry.tabs.intersects (geometry.graph)
                && !geometry.plot.intersects (geometry.readout),
            "waveform, controls, plot and measurement readout retain separate space");
    for (int index = 0; index < geometry.axisLabelCount; ++index)
    {
        const auto& label = geometry.axisLabels[size_t (index)];
        expect (geometry.plot.contains (label.bounds)
                    && label.bounds.getHeight() >= reference_ui::comparison_layout::axisLineHeight (context),
                "actual painter bounds contain the full unchanged text role");
        for (int previous = 0; previous < index; ++previous)
            expect (!label.bounds.intersects (geometry.axisLabels[size_t (previous)].bounds),
                    "adjacent loudness or crest labels never overlap");
    }
}

inline void requireTraces (const juce::Image& image, juce::Rectangle<float> region)
{
    const auto bounds = region.getSmallestIntegerContainer().getIntersection (image.getBounds());
    int blue = 0, gold = 0;
    for (int y = bounds.getY(); y < bounds.getBottom(); ++y)
        for (int x = bounds.getX(); x < bounds.getRight(); ++x)
        {
            const auto colour = image.getPixelAt (x, y);
            blue += colour.getBlue() > colour.getRed() + 12 && colour.getGreen() > colour.getRed() + 12;
            gold += colour.getRed() > colour.getBlue() + 20 && colour.getGreen() > colour.getBlue() + 10;
        }
    expect (blue > 10 && gold > 10, "both original A and B traces remain visibly present");
}

inline void verify()
{
    const auto data = timeline();
    for (const auto& preset : observatory::sizePresets)
    {
        const auto context = presentation::forEditor (preset.width, preset.height);
        juce::Component root; root.getProperties().set (key_light::rootProperty, true);
        root.setSize (preset.width, preset.height);
        reference_ui::Component panel; root.addAndMakeVisible (panel);
        panel.setPresentationContext (context);
        // This is the actual data-review route that exposed the 600 px overlap, including the
        // page's frame reservation, source selectors and its current waveform controls.
        panel.setBounds (6, 50, preset.width - 12, preset.width == 900 ? 470 : preset.height - 64);
        auto state = reference_review::playing (reference_review::library());
        state.comparisonSlot = 1; state.versionId = "v4";
        state.versionReady = state.checkReady = true;
        state.versions = { { "v4", "Mix v4" } }; state.presets = { { "preset-a", "Quick Reference" } };
        state.checks = { { "low/ref-a", "Dynamics" } }; state.cues.clear();
        state.visualTimeline = data; state.visualPositionSeconds = 8;
        panel.setState (state);
        auto* view = dynamic_cast<reference_ui::ComparisonView*> (panel.findChildWithID ("reference-comparison-view"));
        expect (view && view->isVisible(), "actual comparison is shown at every size");
        const auto range = view->selectedRange();
        for (const bool crest : { false, true })
        {
            auto* mode = dynamic_cast<juce::Button*> (view->findChildWithID (crest ? "reference-crest" : "reference-loudness"));
            if (mode->isVisible()) mode->onClick();
            const auto image = render (*view);
            requireGeometry (*view, context);
            const auto& geometry = view->visualLayout();
            requireTraces (image, geometry.waveform);
            if (geometry.detailed())
            {
                auto traceRegion = geometry.plot;
                traceRegion.removeFromLeft (geometry.axisLabels[0].bounds.getWidth() + 5.0f);
                requireTraces (image, traceRegion);
                requireVisibleAxisInk (*view, image, context, crest);
            }
            if (preset.width == 600)
                expect (geometry.axisLabelCount == 3
                            && geometry.plot.getHeight() >= reference_ui::comparison_layout::fullPlotHeight (context),
                        "600 px reserves an honest three-label plot instead of squeezing it under the waveform");
            expect (view->selectedRange() == range, "layout and metric selection preserve the viewed time interval");
        }
    }
    // Capacity boundaries exercise the admitted detail route itself, including the fallbacks
    // that keep one or two truthful quarter-grid values when a whole axis cannot fit.
    reference_ui::ComparisonView view;
    const auto context = presentation::forEditor (600, 400);
    using namespace reference_ui::comparison_layout;
    const auto minimum = [&] (int count) { return int (std::ceil (minimumPaneHeight (context, count))); };
    const std::array<juce::Point<int>, 5> panes {{
        { 500, minimum (1) - 1 }, { 500, minimum (1) }, { 500, minimum (2) },
        { 500, minimum (3) }, { int (minimumDetailWidth) - 1, minimum (3) } }};
    const std::array<int, 5> expectedCounts {{ 0, 1, 2, 3, 0 }};
    for (size_t index = 0; index < panes.size(); ++index)
    {
        const auto pane = panes[index];
        view.setSize (pane.x, pane.y); view.update (data, 8, context, false);
        const auto image = render (view);
        requireGeometry (view, context);
        const auto& geometry = view.visualLayout();
        expect (geometry.axisLabelCount == expectedCounts[index],
                "detail admission follows real capacity, including one/two/three values");
        if (geometry.detailed()) requireVisibleAxisInk (view, image, context, false);
    }
}
}
