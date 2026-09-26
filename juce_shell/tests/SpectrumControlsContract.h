#pragma once

#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaTheme.h"
#include "SpectrumInteractionContractTest.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

// FREQ's controls at one size. From 125% every channel mode names itself in its segment and the
// full interaction contract runs (modes, M/S, PSB, MARK, focus lock). 100% only views: no control
// has bounds, a click where one stands at 125% operates none, and the plot takes the control rows
// and the right-hand axis. The plot itself still reads on hover and locks a frequency.
namespace hypha::tests
{
inline void verifySpectrumControlsAt (SpectrumComponent& spectrum, const KirinSpectrumView& snapshot,
                                      int editorWidth, int editorHeight, juce::Time eventTime)
{
    const auto require = [] (bool ok, const char* what)
    {
        if (ok)
            return;
        std::cerr << "Spectrum controls: " << what << '\n';
        std::exit (EXIT_FAILURE);
    };
    const auto bounds = spectrum.getLocalBounds().toFloat();
    const auto outer = spectrum_geometry::plotBoundsFor (bounds);
    const auto scale = spectrum_geometry::visualScaleFor (bounds);
    const auto context = presentation::forEditor (editorWidth, editorHeight);
    if (! spectrum_geometry::viewOnly (scale))
    {
        const auto modeFont = monoFont (context, typography::TextRole::navigation,
                                        typography::Composition::visualization);
        constexpr std::array<const char*, 4> labels { "LR", "MID", "SIDE", "M/S" };
        for (size_t index = 0u; index < labels.size(); ++index)
            require (spectrum_geometry::displayModeBoundsFor (index, outer, scale).getWidth()
                         >= std::ceil (modeFont.getStringWidthFloat (labels[index]) + modeFont.getHeight() * 0.5f),
                     "a channel mode names itself in its segment");
        verifySpectrumInteractionContract (spectrum, snapshot, spectrum.getWidth(), spectrum.getHeight(),
                                           eventTime);
        return;
    }
    const auto readoutFont = monoFont (context, typography::TextRole::readout,
                                       typography::Composition::visualization);
    require (70.0f * scale >= std::ceil (tabularTextWidth (readoutFont, "M -144.0") + readoutFont.getHeight() * 0.5f),
             "the compact M/S readout holds its value");
    for (size_t index = 0u; index < 4u; ++index)
        require (spectrum_geometry::displayModeBoundsFor (index, outer, scale).isEmpty(), "100% has no channel modes");
    require (spectrum_geometry::markBoundsFor (outer, scale).isEmpty()
                 && spectrum_geometry::subviewBoundsFor (outer, scale).isEmpty(), "100% has no MARK or PSB");
    require (std::abs (spectrum_geometry::dataPlotBoundsFor (bounds).getY() - outer.getY()) < 0.01f,
             "the plot takes the control rows");
    require (outer.getRight() >= bounds.getRight() - spectrum_geometry::viewOnlyRightInset * scale - 0.5f,
             "the plot takes the right-hand axis");
    const auto click = [&spectrum, eventTime] (juce::Point<float> at)
    {
        spectrum.mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(), at, {}, 0.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, &spectrum, &spectrum, eventTime, at, eventTime, 0, false });
    };
    spectrum.setAbsoluteObservation (false);
    spectrum.setSnapshot (snapshot);
    for (const auto at : { juce::Point<float> { outer.getX() + 30.0f * scale, outer.getY() + 7.0f * scale },
                           juce::Point<float> { outer.getRight() - 20.0f * scale, outer.getY() + 7.0f * scale },
                           juce::Point<float> { outer.getRight() - 90.0f * scale, outer.getY() + 7.0f * scale } })
    {
        click (at);
        require (! spectrum.hasMark() && ! spectrum.isPsbObservationForTest(),
                 "a click where a 125% control stands operates no control at 100%");
    }
    click (spectrum_geometry::dataPlotBoundsFor (bounds).getCentre());
    require (spectrum.hasFocusLock(), "the 100% plot still locks a frequency");
}
}
