#include "SpectrumRenderTest.h"
#include "../src/HyphaSpectrumGeometry.h"
#include <stdexcept>
namespace hypha::tests
{
bool isReferenceCurveInk (juce::Colour pixel, juce::Colour target)
{
    // Reference strokes are intentionally translucent (PRE .92, POST .70) and
    // antialias across neighbouring rows. Compare chroma at observed coverage,
    // not against the uncomposited opaque palette swatch. Dark grid ink fails.
    if (target.getBlue() == 0 || pixel.getAlpha() <= 16) return false;
    const auto coverage = static_cast<float> (pixel.getBlue()) / target.getBlue();
    return coverage >= 0.30f && coverage <= 1.02f
        && std::abs (pixel.getRed() - coverage * target.getRed()) <= 6.0f
        && std::abs (pixel.getGreen() - coverage * target.getGreen()) <= 6.0f;
}
namespace ui = ui_contract;
#define KIRIN_REQUIRE(x) do { if (!(x)) throw std::runtime_error (#x); } while (false)
    SpectrumRenderResult renderSpectrumAtSize (
        const KirinSpectrumView& snapshot,
        const ui::SpectrumSizePreset& preset,
        const char* outputEnvironmentVariable)
    {
        hypha::SpectrumComponent component;
        component.setSignalActive (true);
        const auto bounds = ui::spectrumPlotBounds (preset.width, preset.height);
        component.setSize (bounds.width, bounds.height);
        component.setSnapshot (snapshot);
        hypha::guide_frequency::Overlay guideOverlay;
        guideOverlay.count = 1;
        guideOverlay.bands[0].emphasis = hypha::guide_frequency::Emphasis::active;
        guideOverlay.bands[0].lowHz = 3'150.0;
        guideOverlay.bands[0].highHz = 3'700.0;
        component.setGuideFrequencyOverlay (guideOverlay);

        const float scale = ui::spectrumVisualScale (bounds.width);
        const float leftInset = (float) ui::spectrumPlotLeftInset * scale;
        const float rightInset = (float) ui::spectrumPlotRightInset * scale;
        const float hoverX = leftInset + 0.70f * ((float) bounds.width
                                                 - leftInset - rightInset);
        const float hoverY = spectrum_geometry::dataPlotBoundsFor (component.getLocalBounds().toFloat()).getCentreY();
        const auto eventTime = juce::Time::getCurrentTime();
        const juce::MouseEvent hoverEvent (
            juce::Desktop::getInstance().getMainMouseSource(),
            { hoverX, hoverY }, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            &component, &component, eventTime,
            { hoverX, hoverY }, eventTime, 0, false);
        component.mouseMove (hoverEvent);
        component.presentationTick();

        SpectrumRenderResult result {
            juce::Image (juce::Image::ARGB, bounds.width, bounds.height, true), 0.0
        };
        constexpr int paintIterations = 200;
        const double startedMs = juce::Time::getMillisecondCounterHiRes();
        for (int iteration = 0; iteration < paintIterations; ++iteration)
        {
            result.image.clear (result.image.getBounds(), hypha::BG);
            juce::Graphics graphics (result.image);
            component.paintEntireComponent (graphics, true);
        }
        result.paintMs = (juce::Time::getMillisecondCounterHiRes() - startedMs)
                       / paintIterations;
        const auto outputPath = juce::SystemStats::getEnvironmentVariable (
            outputEnvironmentVariable, {});
        if (outputPath.isNotEmpty())
        {
            auto output = juce::File (outputPath).createOutputStream();
            KIRIN_REQUIRE (output != nullptr);
            KIRIN_REQUIRE (juce::PNGImageFormat().writeImageToStream (result.image, *output));
        }
        return result;
    }
}
