#include "GuideFrequencyOverlayContractTest.h"

#include "../src/HyphaGuideFrequencyOverlay.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaSpectrumUiContract.h"
#include "../src/HyphaUiContract.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace hypha::tests
{
namespace
{
    void require (bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "Guide frequency overlay contract failed at line " << line
                  << ": " << expression << '\n';
        std::exit (EXIT_FAILURE);
    }

   #define KIRIN_GUIDE_OVERLAY_REQUIRE(expression) \
        require ((expression), #expression, __LINE__)

    int countPaintedPixels (const juce::Image& image)
    {
        int count = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                count += image.getPixelAt (x, y).getAlpha() > 0 ? 1 : 0;
        return count;
    }

    juce::Image renderOverlay (const guide_frequency::Overlay& overlay,
                               juce::Rectangle<float> componentBounds)
    {
        juce::Image image (juce::Image::ARGB,
                           juce::roundToInt (componentBounds.getWidth()),
                           juce::roundToInt (componentBounds.getHeight()), true);
        juce::Graphics graphics (image);
        const auto plot = spectrum_geometry::dataPlotBoundsFor (componentBounds);
        guide_frequency::paint (
            graphics, plot, spectrum_geometry::visualScaleFor (componentBounds),
            overlay, 10.0f, 22'000.0f);
        return image;
    }

    KirinSpectrumView spectrumSnapshot()
    {
        KirinSpectrumView snapshot {};
        snapshot.status = KIRIN_SPECTRUM_ACTIVE;
        snapshot.has_data = 1;
        snapshot.channel_mode = KIRIN_SPECTRUM_CHANNEL_LR;
        snapshot.channels = 2;
        snapshot.sample_rate = 48'000;
        snapshot.aperture_samples = 4'096;
        snapshot.fft_size = 8'192;
        snapshot.approximate_below_hz = 35.15625f;
        snapshot.presentation_end_samples = 48'000;
        snapshot.min_hz = 10.0f;
        snapshot.max_hz = 22'000.0f;
        for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
        {
            const auto position = (float) index
                                / (float) (KIRIN_SPECTRUM_BAND_COUNT - 1u);
            snapshot.pre_dbfs[index] = -42.0f + 18.0f * position;
            snapshot.display_db[index] = 2.5f * std::sin ((float) index * 0.08f);
            snapshot.post_dbfs[index] = snapshot.pre_dbfs[index]
                                      + snapshot.display_db[index];
        }
        return snapshot;
    }

    juce::Image renderComponent (SpectrumComponent& component)
    {
        juce::Image image (juce::Image::ARGB,
                           component.getWidth(), component.getHeight(), true);
        image.clear (image.getBounds(), BG);
        juce::Graphics graphics (image);
        component.paintEntireComponent (graphics, true);
        return image;
    }

    int countDifferentPixels (const juce::Image& left, const juce::Image& right,
                              juce::Rectangle<int> area)
    {
        int count = 0;
        area = area.getIntersection (left.getBounds()).getIntersection (right.getBounds());
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                count += left.getPixelAt (x, y).getARGB()
                           != right.getPixelAt (x, y).getARGB() ? 1 : 0;
        return count;
    }
}

void verifyGuideFrequencyOverlayContract()
{
    pre_display::GuidePresentationSnapshot presentation;
    presentation.targetRole = pre_display::GuideTargetRole::post;
    presentation.status = pre_display::DisplayStatus::active;
    presentation.guideId = "guide_1";
    presentation.payloadKind = "inspect";
    presentation.guideAvailable = true;
    presentation.hasPrimary = true;
    presentation.primary.kind = pre_display::GuidePresentationFactKind::inspectEvent;
    presentation.primary.phase = pre_display::GuideFactPhase::active;
    presentation.primary.itemId = "event_1";
    presentation.primary.label = "True Peak";
    presentation.primary.lowHz = 3'150.0;
    presentation.primary.highHz = 3'700.0;
    presentation.primary.hasBand = true;

    const auto active = guide_frequency::fromGuidePresentation (presentation);
    KIRIN_GUIDE_OVERLAY_REQUIRE (active.count == 1);
    KIRIN_GUIDE_OVERLAY_REQUIRE (active.band (0) != nullptr);
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        active.band (0)->emphasis == guide_frequency::Emphasis::active);
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        active.band (0)->role == guide_frequency::BandRole::inspect);
    KIRIN_GUIDE_OVERLAY_REQUIRE (active.guideId == "guide_1");
    KIRIN_GUIDE_OVERLAY_REQUIRE (active.band (0)->itemId == "event_1");
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        std::abs (active.band (0)->lowHz - 3'150.0) < 0.001);
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        std::abs (active.band (0)->highHz - 3'700.0) < 0.001);

    for (const auto& preset : ui_contract::spectrumSizePresets)
    {
        const auto area = ui_contract::spectrumPlotBounds (preset.width, preset.height);
        const juce::Rectangle<float> componentBounds (
            0.0f, 0.0f, (float) area.width, (float) area.height);
        const auto plot = spectrum_geometry::dataPlotBoundsFor (componentBounds);
        const auto band = guide_frequency::bandBoundsFor (
            *active.band (0), 10.0f, 22'000.0f, plot);
        KIRIN_GUIDE_OVERLAY_REQUIRE (! band.isEmpty());
        KIRIN_GUIDE_OVERLAY_REQUIRE (std::abs (
            band.getX() - spectrum_geometry::xForFrequency (
                              3'150.0f, 10.0f, 22'000.0f, plot)) < 0.001f);
        KIRIN_GUIDE_OVERLAY_REQUIRE (std::abs (
            band.getRight() - spectrum_geometry::xForFrequency (
                                  3'700.0f, 10.0f, 22'000.0f, plot)) < 0.001f);

        const auto activeImage = renderOverlay (active, componentBounds);
        KIRIN_GUIDE_OVERLAY_REQUIRE (countPaintedPixels (activeImage) > 60);

        auto cue = active;
        cue.bands[0].emphasis = guide_frequency::Emphasis::cue;
        const auto cueImage = renderOverlay (cue, componentBounds);
        const int cuePixels = countPaintedPixels (cueImage);
        KIRIN_GUIDE_OVERLAY_REQUIRE (cuePixels > 4);
        KIRIN_GUIDE_OVERLAY_REQUIRE (
            countPaintedPixels (activeImage) > cuePixels * 2);

        auto hidden = active;
        hidden.bands[0].emphasis = guide_frequency::Emphasis::hidden;
        KIRIN_GUIDE_OVERLAY_REQUIRE (
            countPaintedPixels (renderOverlay (hidden, componentBounds)) == 0);

        SpectrumComponent component;
        component.setSize (area.width, area.height);
        component.setSnapshot (spectrumSnapshot());
        const auto baseline = renderComponent (component);
        component.setGuideFrequencyOverlay (active);
        const auto guided = renderComponent (component);
        const auto changeArea = band.expanded (
            8.0f * spectrum_geometry::visualScaleFor (componentBounds), 1.0f)
                                .toNearestInt();
        KIRIN_GUIDE_OVERLAY_REQUIRE (
            countDifferentPixels (baseline, guided, changeArea) > 60);
        const auto outsideLeft = juce::Rectangle<int> (
            0, 0, juce::jmax (0, changeArea.getX()), guided.getHeight());
        const auto outsideRight = juce::Rectangle<int> (
            juce::jmin (guided.getWidth(), changeArea.getRight()), 0,
            juce::jmax (0, guided.getWidth() - changeArea.getRight()),
            guided.getHeight());
        KIRIN_GUIDE_OVERLAY_REQUIRE (
            countDifferentPixels (baseline, guided, outsideLeft) == 0);
        KIRIN_GUIDE_OVERLAY_REQUIRE (
            countDifferentPixels (baseline, guided, outsideRight) == 0);
        if (preset.width == ui_contract::spectrumSizePresets.back().width)
        {
            const auto outputPath = juce::SystemStats::getEnvironmentVariable (
                "KIRIN_UI_GUIDE_OUTPUT", {});
            if (outputPath.isNotEmpty())
            {
                auto output = juce::File (outputPath).createOutputStream();
                KIRIN_GUIDE_OVERLAY_REQUIRE (output != nullptr);
                KIRIN_GUIDE_OVERLAY_REQUIRE (
                    juce::PNGImageFormat().writeImageToStream (guided, *output));
            }
        }
    }

    presentation.primary.phase = pre_display::GuideFactPhase::held;
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        ! guide_frequency::fromGuidePresentation (presentation).visible());
    presentation.primary.phase = pre_display::GuideFactPhase::next;
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        ! guide_frequency::fromGuidePresentation (presentation).visible());
    presentation.primary.phase = pre_display::GuideFactPhase::cue;
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        guide_frequency::fromGuidePresentation (presentation).band (0)->emphasis
            == guide_frequency::Emphasis::cue);

    presentation.payloadKind = "masking";
    presentation.primary.kind
        = pre_display::GuidePresentationFactKind::maskingMeasuredInterval;
    presentation.primary.phase = pre_display::GuideFactPhase::active;
    presentation.primary.lowHz = 120.0;
    presentation.primary.highHz = 180.0;
    presentation.hasMaskingFocus = true;
    presentation.maskingFocus.kind
        = pre_display::GuidePresentationFactKind::maskingReviewSelection;
    presentation.maskingFocus.phase = pre_display::GuideFactPhase::active;
    presentation.maskingFocus.itemId = "review_1";
    presentation.maskingFocus.lowHz = 100.0;
    presentation.maskingFocus.highHz = 200.0;
    presentation.maskingFocus.hasBand = true;
    auto masking = guide_frequency::fromGuidePresentation (presentation);
    KIRIN_GUIDE_OVERLAY_REQUIRE (masking.count == 2);
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        masking.band (0)->role == guide_frequency::BandRole::maskingFocus);
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        masking.band (1)->role == guide_frequency::BandRole::maskingMeasured);
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        masking.band (0)->lowHz == 100.0 && masking.band (0)->highHz == 200.0);
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        masking.band (1)->lowHz == 120.0 && masking.band (1)->highHz == 180.0);
    const juce::Rectangle<float> comparisonBounds (0.0f, 0.0f, 560.0f, 260.0f);
    auto focusOnly = masking;
    focusOnly.count = 1;
    auto measuredOnly = masking;
    measuredOnly.bands[0] = measuredOnly.bands[1];
    measuredOnly.count = 1;
    const auto focusImage = renderOverlay (focusOnly, comparisonBounds);
    const auto measuredImage = renderOverlay (measuredOnly, comparisonBounds);
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        countPaintedPixels (measuredImage) > countPaintedPixels (focusImage) * 2);
    const auto maskingPreset = ui_contract::spectrumSizePresets.back();
    const auto maskingArea = ui_contract::spectrumPlotBounds (
        maskingPreset.width, maskingPreset.height);
    SpectrumComponent maskingComponent;
    maskingComponent.setSize (maskingArea.width, maskingArea.height);
    maskingComponent.setSnapshot (spectrumSnapshot());
    maskingComponent.setGuideFrequencyOverlay (masking);
    const auto maskingOutputPath = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_UI_MASKING_GUIDE_OUTPUT", {});
    if (maskingOutputPath.isNotEmpty())
    {
        auto output = juce::File (maskingOutputPath).createOutputStream();
        KIRIN_GUIDE_OVERLAY_REQUIRE (output != nullptr);
        KIRIN_GUIDE_OVERLAY_REQUIRE (juce::PNGImageFormat().writeImageToStream (
            renderComponent (maskingComponent), *output));
    }

    presentation.hasPrimary = false;
    masking = guide_frequency::fromGuidePresentation (presentation);
    KIRIN_GUIDE_OVERLAY_REQUIRE (masking.count == 1);
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        masking.band (0)->role == guide_frequency::BandRole::maskingFocus);

    presentation.targetRole = pre_display::GuideTargetRole::pre;
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        ! guide_frequency::fromGuidePresentation (presentation).visible());
    presentation.targetRole = pre_display::GuideTargetRole::post;
    presentation.maskingFocus.lowHz = std::numeric_limits<double>::quiet_NaN();
    KIRIN_GUIDE_OVERLAY_REQUIRE (
        ! guide_frequency::fromGuidePresentation (presentation).visible());
}
}

#if defined (KIRIN_GUIDE_OVERLAY_STANDALONE) && KIRIN_GUIDE_OVERLAY_STANDALONE
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    hypha::tests::verifyGuideFrequencyOverlayContract();
    std::cout << "Guide frequency overlay contract passed\n";
    return EXIT_SUCCESS;
}
#endif
