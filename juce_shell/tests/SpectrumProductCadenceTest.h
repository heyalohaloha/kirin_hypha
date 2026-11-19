#pragma once
#include "../src/HyphaSpectrumFrameCadence.h"
#include "../src/HyphaSpectrumGlowLayers.h"
#include "../src/HyphaSpectrumPainter.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "SpectrumPerformanceFixture.h"
#include <limits>
#include <stdexcept>

namespace hypha::tests
{
inline void verifySpectrumProductCadence()
{
    const auto check = [] (bool ok, const char* what)
    { if (! ok) throw std::runtime_error (what); };
    using spectrum_display::FrameCadence;
    FrameCadence cadence;
    check (cadence.due (0), "first spectrum presentation is immediate");
    check (! cadence.due (49.999), "no spectrum frame before 50 ms");
    check (cadence.due (50), "spectrum frame at 50 ms");
    check (! cadence.due (50), "no duplicate frame at one timestamp");
    check (cadence.due (100), "next spectrum frame at 100 ms");
    check (cadence.due (10'125), "stalled presentation resumes once");
    check (! cadence.due (10'125) && ! cadence.due (10'149), "no catch-up burst");
    check (cadence.due (10'150), "deadline phase survives stall");
    check (cadence.due (5) && ! cadence.due (54), "clock rewind restarts cadence");
    cadence.reset();
    check (cadence.due (6), "hide or disable reset permits immediate show");
    check (! cadence.due (std::numeric_limits<double>::quiet_NaN()), "invalid clock rejected");
    check (! cadence.due (std::numeric_limits<double>::infinity()), "infinite clock rejected");
    check (! cadence.due (-1) && cadence.due (7), "negative clock resets safely");
    for (const int displayHz : { 60, 120, 144 })
    {
        FrameCadence display;
        int ticks = 0;
        for (int frame = 0; frame <= displayHz * 10; ++frame)
            ticks += display.due (1'000.0 * frame / displayHz) ? 1 : 0;
        check (ticks == 201, "ten seconds has 200 intervals plus first frame at every display rate");
    }
}

inline void verifySpectrumProductGlowLayers()
{
    const auto check = [] (bool ok, const char* what)
    { if (! ok) throw std::runtime_error (what); };
    using namespace spectrum_painter;
    static_assert (glowLayers.size() == 3 && glowLayers[0] == 0
                   && glowLayers[1] == 2 && glowLayers[2] == 5);
    SpectrumBins pre {}, post {}, delta {};
    SpectrumValidity valid {};
    pre.fill (-30); post.fill (-24); delta.fill (6); valid.fill (1);
    PathWorkspace paths;
    juce::Image image (juce::Image::ARGB, 600, 300, true);
    juce::Graphics graphics (image);
    paintCurves (graphics, { 10, 10, 580, 280 }, 1.0f,
                 pre, post, delta, valid, nullptr, nullptr, 96, 1, false, &paths);
    int populated = 0;
    for (size_t layer = 0; layer < PathWorkspace::layers; ++layer)
    {
        bool hasRibbon = false;
        for (size_t bucket = 1; bucket < PathWorkspace::levels; ++bucket)
            hasRibbon = hasRibbon || ! paths.existing (
                PathWorkspace::tipBase + layer * PathWorkspace::levels + bucket).isEmpty();
        const bool selected = layer == 0 || layer == 2 || layer == 5;
        check (hasRibbon == selected, "only original glow layers zero, two, five are generated");
        populated += hasRibbon ? 1 : 0;
    }
    check (populated == 3, "three glow ribbons remain");
    valid.fill (0);
    paintCurves (graphics, { 10, 10, 580, 280 }, 1.0f,
                 pre, post, delta, valid, nullptr, nullptr, 96, 1, false, &paths);
    for (size_t slot = PathWorkspace::tipBase; slot < PathWorkspace::highlightBase; ++slot)
        check (paths.existing (slot).isEmpty(), "invalid delta clears retained glow ribbons");
}

inline void verifySpectrumImmediateInteraction()
{
    const auto check = [] (bool ok, const char* what)
    { if (! ok) throw std::runtime_error (what); };
    SpectrumComponent view;
    view.setSize (576, 280);
    view.setSignalActive (true);
    view.setAnimationVisible (false); // No presentation tick can supply an accidental repaint.
    view.setSnapshot (spectrumPerformanceFixture());
    const auto event = [&view] (juce::Point<float> position)
    {
        return juce::MouseEvent {
            juce::Desktop::getInstance().getMainMouseSource(), position, {}, 0, 0, 0, 0, 0,
            &view, &view, {}, position, {}, 0, false };
    };
    const auto bounds = view.getLocalBounds().toFloat();
    const auto scale = spectrum_geometry::visualScaleFor (bounds);
    const auto outer = spectrum_geometry::plotBoundsFor (bounds);
    auto before = view.interactionRepaintsForTest();
    view.mouseDown (event (spectrum_geometry::markBoundsFor (outer, scale).getCentre()));
    check (view.hasMark() && view.interactionRepaintsForTest() > before,
           "MARK changes state and requests repaint before next frame");
    const auto plot = spectrum_geometry::dataPlotBoundsFor (bounds, false);
    const auto pointer = plot.getCentre();
    before = view.interactionRepaintsForTest();
    const auto lastCurve = view.curveUpdateTimeForTest();
    view.mouseMove (event (pointer));
    check (view.interactionRepaintsForTest() > before, "hover requests immediate repaint");
    before = view.interactionRepaintsForTest();
    view.mouseMove (event (pointer));
    check (view.interactionRepaintsForTest() == before, "unchanged hover does not request another repaint");
    view.mouseExit (event (pointer));
    check (view.interactionRepaintsForTest() > before, "hover exit requests immediate repaint");
    before = view.interactionRepaintsForTest();
    view.mouseDown (event (pointer));
    check (view.focusLockFrequencyHz() > 0 && view.interactionRepaintsForTest() > before,
           "plot click locks focus and requests immediate repaint");
    check (view.curveUpdateTimeForTest() == lastCurve, "interaction does not force a model tick");
    const auto readout = spectrum_geometry::readoutBoundsFor (outer, scale, scale > 1.1f, true);
    before = view.interactionRepaintsForTest();
    view.mouseDown (event (spectrum_geometry::focusClearBoundsFor (readout, scale).getCentre()));
    check (view.focusLockFrequencyHz() <= 0 && view.interactionRepaintsForTest() > before,
           "clearing LOCK requests immediate repaint");
    before = view.interactionRepaintsForTest();
    view.setSize (577, 281);
    check (view.interactionRepaintsForTest() > before, "resize requests immediate repaint");
}
}
