#include "../src/HyphaWidgets.h"
#include "../src/HyphaAnalysisNavigation.h"
#include "../src/HyphaAnalysisUiText.h"
#include "../src/HyphaHoverHelpPreference.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaSurfaceMaterial.h"
#include "../src/HyphaTooltipLookAndFeel.h"
#include "PerceptualHistoryContractTest.h"
#include "AbsoluteTimelineContractTest.h"
#include "AbsoluteSpectrumContractTest.h"
#include "SpectrumFocusTrailContractTest.h"
#include "SpectrumInteractionContractTest.h"
#include "SpectrumPresentationContractTest.h"
#include "GuideFrequencyOverlayContractTest.h"
#include "ObservatoryViewContractTest.h"
#include "ObservatoryCompositeContractTest.h"
#include "ObservationPageContractTest.h"
#include "SpectrumRenderTest.h"
#include "CaptureHistoryContractTest.h"
#include "TimeHistoryContractTest.h"
#include "TimePageNavigationContractTest.h"
#include "RunSummaryContractTest.h"
#include "SpaceFieldContractTest.h"
#include "ReferenceAuditionComponentContractTest.h"
#include "OsAccessUiContractTest.h"
#include "UiFeatureContracts.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
namespace ui = hypha::ui_contract;
static_assert (sizeof (KirinSpectrumView) == 3'112, "Spectrum view ABI size must remain exact");
static_assert (sizeof (KirinSpectrumBatch) == 28'016, "Spectrum batch ABI size must remain exact");
static_assert (sizeof (KirinMidSideSpectrumView) == 2'088,
               "Mid/Side Spectrum ABI size must remain exact");
static_assert (alignof (KirinMidSideSpectrumView) == 8);
static_assert (offsetof (KirinMidSideSpectrumView, mid_dbfs) == 16);
static_assert (offsetof (KirinMidSideSpectrumView, side_dbfs) == 1'040);
static_assert (offsetof (KirinMidSideSpectrumView, presentation_end_samples) == 2'064);
static_assert (sizeof (KirinMeterSession) == 872u, "Meter Session ABI size must remain exact");
static_assert (sizeof (KirinObservatoryFrame) == 1'112u, "Observatory frame ABI size must remain exact");
static_assert (sizeof (KirinMeterHistoryEntry) == 184u, "Meter history ABI size must remain exact");
namespace
{
    void require (bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "UI render contract failed at line " << line
                  << ": " << expression << '\n';
        std::exit (EXIT_FAILURE);
    }
#define KIRIN_REQUIRE(expression) require ((expression), #expression, __LINE__)
    int countVisiblePixels (const juce::Image& image, juce::Rectangle<int> requested)
    {
        const auto area = requested.getIntersection (image.getBounds());
        int count = 0;
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                if (image.getPixelAt (x, y).getAlpha() != 0)
                    ++count;
        return count;
    }

    int countExactPixels (const juce::Image& image, juce::Colour colour)
    {
        int count = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (image.getPixelAt (x, y).getARGB() == colour.getARGB())
                    ++count;
        return count;
    }

    bool nearRgb (juce::Colour pixel, juce::Colour target)
    {
        return hypha::tests::isReferenceCurveInk (pixel, target);
    }

    int countColourRunsAcross (const juce::Image& image,
                               juce::Rectangle<int> requested,
                               juce::Colour target)
    {
        const auto area = requested.getIntersection (image.getBounds());
        int runs = 0;
        bool previousColumn = false;
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            bool currentColumn = false;
            for (int y = area.getY(); y < area.getBottom(); ++y)
                currentColumn = currentColumn || nearRgb (image.getPixelAt (x, y), target);
            if (currentColumn && ! previousColumn)
                ++runs;
            previousColumn = currentColumn;
        }
        return runs;
    }

    int countColourColumnsAcross (const juce::Image& image,
                                  juce::Rectangle<int> requested,
                                  juce::Colour target)
    {
        const auto area = requested.getIntersection (image.getBounds());
        int columns = 0;
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            bool currentColumn = false;
            for (int y = area.getY(); y < area.getBottom(); ++y)
                currentColumn = currentColumn || nearRgb (image.getPixelAt (x, y), target);
            columns += currentColumn ? 1 : 0;
        }
        return columns;
    }

    int countDifferentPixels (const juce::Image& a, const juce::Image& b)
    {
        KIRIN_REQUIRE (a.getBounds() == b.getBounds());
        int count = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y).getARGB() != b.getPixelAt (x, y).getARGB())
                    ++count;
        return count;
    }

}
using hypha::tests::renderSpectrumAtSize;
using hypha::tests::renderMidSideSpectrumAtSize;
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    if (hypha::tests::verifyUiFeatureContracts (argc, argv)) return 0;
    {
        juce::Image panel (juce::Image::RGB, 120, 60, true);
        juce::Graphics panelGraphics (panel);
        panelGraphics.fillAll (hypha::BG);
        hypha::surface_material::paintPanel (
            panelGraphics, panel.getBounds().toFloat(), 0.96f, 5.0f);
        KIRIN_REQUIRE (panel.getPixelAt (60, 30) != hypha::BG);
        KIRIN_REQUIRE (panel.getPixelAt (60, 2).getPerceivedBrightness()
                       > panel.getPixelAt (60, 30).getPerceivedBrightness());

        juce::Image frame (juce::Image::RGB, 300, 200, true);
        juce::Graphics frameGraphics (frame);
        frameGraphics.fillAll (hypha::BG);
        hypha::surface_material::paintInstrumentFrame (
            frameGraphics, frame.getBounds().toFloat(), false);
        KIRIN_REQUIRE (frame.getPixelAt (150, 1) != hypha::BG);
        KIRIN_REQUIRE (frame.getPixelAt (150, 100) == hypha::BG);
    }
    constexpr auto compactPresentation = hypha::presentation::forEditor (300, 200);
    const auto preferenceDirectory = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("kirin-hypha-hover-help-contract", {}, false);
    KIRIN_REQUIRE (preferenceDirectory.createDirectory().wasOk());
    const auto preferenceFile = preferenceDirectory.getChildFile ("ui-preferences.txt");
    {
        hypha::HoverHelpPreference first (preferenceFile);
        KIRIN_REQUIRE (first.isEnabled()); // missing file keeps the discoverable default
        KIRIN_REQUIRE (first.setEnabled (false));
        KIRIN_REQUIRE (! first.isEnabled());

        hypha::HoverHelpPreference second (preferenceFile);
        KIRIN_REQUIRE (! second.isEnabled()); // PRE/POST module recreation sees the same setting
        KIRIN_REQUIRE (second.setEnabled (true));
        first.refreshNowForTest();
        KIRIN_REQUIRE (first.isEnabled());

        const auto blockedParent = preferenceDirectory.getChildFile ("not-a-directory");
        KIRIN_REQUIRE (blockedParent.replaceWithText ("blocker"));
        hypha::HoverHelpPreference fallback (blockedParent.getChildFile ("ui-preferences.txt"));
        KIRIN_REQUIRE (! fallback.setEnabled (false));
        fallback.refreshNowForTest();
        KIRIN_REQUIRE (! fallback.isEnabled()); // failed persistence keeps the session choice
    }
    KIRIN_REQUIRE (preferenceDirectory.deleteRecursively());

    KIRIN_REQUIRE (std::abs (ui::spectrumStrokeScale (1.0f) - 1.0f) < 1.0e-6f);
    KIRIN_REQUIRE (std::abs (ui::spectrumStrokeScale (1.25f) - 1.12f) < 1.0e-6f);
    KIRIN_REQUIRE (std::abs (ui::spectrumStrokeScale (1.5f) - 1.22f) < 1.0e-6f);
    KIRIN_REQUIRE (std::abs (ui::spectrumGlowScale (1.5f) - 1.15f) < 1.0e-6f);
    using AnalysisPage = hypha::analysis_navigation::Page;
    for (const auto previous : {
             AnalysisPage::attack, AnalysisPage::spectrum,
             AnalysisPage::perceptual, AnalysisPage::absolute })
    {
        KIRIN_REQUIRE (hypha::analysis_navigation::releasesSlot (
            previous, AnalysisPage::meters));
        for (const auto next : {
                 AnalysisPage::attack, AnalysisPage::spectrum,
                 AnalysisPage::perceptual, AnalysisPage::absolute })
            KIRIN_REQUIRE (! hypha::analysis_navigation::releasesSlot (previous, next));
    }
    KIRIN_REQUIRE (! hypha::analysis_navigation::releasesSlot (
        AnalysisPage::meters, AnalysisPage::spectrum));

    const auto deltaFont = hypha::labelFont (
        compactPresentation, hypha::typography::TextRole::metricLabel,
        hypha::typography::Composition::facts);
    const auto deltaWidth = static_cast<int> (
        std::ceil (deltaFont.getStringWidthFloat (hypha::delta())));
    const auto deltaLayout = ui::loudnessSelectorLayout (true, deltaWidth);
    hypha::LoudnessSelector selector;
    selector.setSize (ui::loudnessSelectorWidth, ui::metricRowHeight);
    selector.setDeltaMode (true);
    juce::Image selectorImage (juce::Image::ARGB, selector.getWidth(), selector.getHeight(), true);
    {
        juce::Graphics graphics (selectorImage);
        selector.paintEntireComponent (graphics, true);
    }
    const int deltaPixels = countVisiblePixels (
        selectorImage, { 0, 0, deltaLayout.deltaPrefixWidth, selector.getHeight() });
    KIRIN_REQUIRE (deltaPixels > 0);

    hypha::PairDropdownButton pairDropdown;
    pairDropdown.setSize (ui::pairDropdownWidth, ui::nameFieldHeight);
    pairDropdown.setColour (juce::TextButton::buttonColourId, hypha::kFieldFill);
    pairDropdown.setColour (juce::TextButton::textColourOnId, hypha::COL_FLORA);
    pairDropdown.setColour (juce::TextButton::textColourOffId, hypha::COL_FLORA);
    juce::Image dropdownImage (
        juce::Image::ARGB, pairDropdown.getWidth(), pairDropdown.getHeight(), true);
    {
        juce::Graphics graphics (dropdownImage);
        pairDropdown.paintEntireComponent (graphics, true);
    }
    const int arrowPixels = countExactPixels (dropdownImage, hypha::COL_FLORA);
    KIRIN_REQUIRE (arrowPixels >= 8);

    hypha::SpectrumComponent spectrum;
    spectrum.setPresentationContext (hypha::presentation::forEditor (
        ui::editorWidth, ui::editorHeight));
    spectrum.setSignalActive (true);
    const auto spectrumBounds = ui::spectrumPlotBounds();
    spectrum.setSize (spectrumBounds.width, spectrumBounds.height);
    juce::Image warmingSpectrumImage (
        juce::Image::ARGB, spectrum.getWidth(), spectrum.getHeight(), true);
    {
        juce::Graphics graphics (warmingSpectrumImage);
        spectrum.paintEntireComponent (graphics, true);
    }
    KirinSpectrumView spectrumSnapshot {};
    spectrumSnapshot.status = KIRIN_SPECTRUM_ACTIVE;
    spectrumSnapshot.has_data = 1;
    spectrumSnapshot.post_has_data = 1;
    spectrumSnapshot.channel_mode = KIRIN_SPECTRUM_CHANNEL_LR;
    spectrumSnapshot.channels = 2;
    spectrumSnapshot.sample_rate = 48'000;
    spectrumSnapshot.aperture_samples = 4'096;
    spectrumSnapshot.fft_size = 8'192;
    spectrumSnapshot.approximate_below_hz = 35.15625f;
    spectrumSnapshot.presentation_end_samples = 48'000;
    spectrumSnapshot.min_hz = 10.0f;
    spectrumSnapshot.max_hz = 22'000.0f;
    for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        const float position = static_cast<float> (index)
                             / static_cast<float> (KIRIN_SPECTRUM_BAND_COUNT - 1u);
        const float body = -78.0f + 62.0f * std::exp (-std::pow ((position - 0.53f) / 0.42f, 2.0f));
        const float strongRegion = 0.38f
                                 + 0.62f * std::exp (-std::pow ((position - 0.61f) / 0.24f, 2.0f));
        spectrumSnapshot.display_db[index] = 14.0f * strongRegion
                                           * std::sin ((float) index * 0.065f);
        spectrumSnapshot.pre_dbfs[index] = body + 2.0f * std::sin ((float) index * 0.045f);
        spectrumSnapshot.post_dbfs[index] = spectrumSnapshot.pre_dbfs[index]
                                           + spectrumSnapshot.display_db[index];
    }
    KirinMidSideSpectrumView midSideSnapshot {};
    midSideSnapshot.status = KIRIN_SPECTRUM_ACTIVE;
    midSideSnapshot.has_data = 1u;
    midSideSnapshot.channels = 2u;
    midSideSnapshot.sample_rate = spectrumSnapshot.sample_rate;
    midSideSnapshot.min_hz = spectrumSnapshot.min_hz;
    midSideSnapshot.max_hz = spectrumSnapshot.max_hz;
    midSideSnapshot.presentation_end_samples = spectrumSnapshot.presentation_end_samples;
    midSideSnapshot.aperture_samples = spectrumSnapshot.aperture_samples;
    midSideSnapshot.fft_size = spectrumSnapshot.fft_size;
    midSideSnapshot.approximate_below_hz = spectrumSnapshot.approximate_below_hz;
    for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        midSideSnapshot.mid_dbfs[index] = spectrumSnapshot.pre_dbfs[index];
        midSideSnapshot.side_dbfs[index] = spectrumSnapshot.post_dbfs[index] - 18.0f;
    }
    spectrum.setSnapshot (spectrumSnapshot);
    const auto previewPlot = hypha::spectrum_geometry::dataPlotBoundsFor (spectrum.getLocalBounds().toFloat());
    const float previewHoverX = previewPlot.getX() + 0.70f * previewPlot.getWidth();
    const float previewHoverY = previewPlot.getCentreY();
    const auto eventTime = juce::Time::getCurrentTime();
    const juce::MouseEvent hoverEvent (
        juce::Desktop::getInstance().getMainMouseSource(),
        { previewHoverX, previewHoverY }, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        &spectrum, &spectrum, eventTime,
        { previewHoverX, previewHoverY }, eventTime, 0, false);
    spectrum.mouseMove (hoverEvent);
    spectrum.presentationTick();
    juce::Image spectrumImage (
        juce::Image::ARGB, spectrum.getWidth(), spectrum.getHeight(), true);
    {
        juce::Graphics graphics (spectrumImage);
        spectrum.paintEntireComponent (graphics, true);
    }
    KIRIN_REQUIRE (countDifferentPixels (warmingSpectrumImage, spectrumImage) > 100);
    spectrum.mouseExit (hoverEvent);
    spectrum.presentationTick();
    juce::Image spectrumWithoutHover (
        juce::Image::ARGB, spectrum.getWidth(), spectrum.getHeight(), true);
    {
        juce::Graphics graphics (spectrumWithoutHover);
        spectrum.paintEntireComponent (graphics, true);
    }
    KIRIN_REQUIRE (countDifferentPixels (spectrumImage, spectrumWithoutHover) > 30);
    spectrum.mouseMove (hoverEvent);
    spectrum.presentationTick();
    spectrum.mouseDown (hoverEvent);
    KIRIN_REQUIRE (spectrum.hasFocusLock());
    const float lockedFrequency = spectrum.focusLockFrequencyHz();
    KIRIN_REQUIRE (lockedFrequency > 1'000.0f && lockedFrequency < 22'000.0f);
    spectrum.mouseExit (hoverEvent);
    spectrum.presentationTick();
    juce::Image spectrumWithFocusLock (
        juce::Image::ARGB, spectrum.getWidth(), spectrum.getHeight(), true);
    {
        juce::Graphics graphics (spectrumWithFocusLock);
        spectrum.paintEntireComponent (graphics, true);
    }
    KIRIN_REQUIRE (countDifferentPixels (spectrumWithoutHover, spectrumWithFocusLock) > 30);
    const float clearX = (float) spectrumBounds.width
                       - (float) ui::spectrumPlotRightInset - 3.0f;
    const float clearY = (float) ui::spectrumPlotTopInset + 24.0f;
    const juce::MouseEvent clearEvent (
        juce::Desktop::getInstance().getMainMouseSource(),
        { clearX, clearY }, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        &spectrum, &spectrum, eventTime,
        { clearX, clearY }, eventTime, 0, false);
    spectrum.mouseDown (clearEvent);
    KIRIN_REQUIRE (! spectrum.hasFocusLock());
    for (const auto& preset : ui::spectrumSizePresets)
    {
        hypha::SpectrumComponent markSpectrum;
        const auto presentation = hypha::presentation::forEditor (preset.width, preset.height);
        markSpectrum.setPresentationContext (presentation);
        markSpectrum.setSignalActive (true);
        const auto markSpectrumBounds = ui::spectrumPlotBounds (preset.width, preset.height);
        markSpectrum.setSize (markSpectrumBounds.width, markSpectrumBounds.height);
        markSpectrum.setSnapshot (spectrumSnapshot);
        const auto outer = hypha::spectrum_geometry::plotBoundsFor (
            markSpectrum.getLocalBounds().toFloat());
        const auto scale = hypha::spectrum_geometry::visualScaleFor (
            markSpectrum.getLocalBounds().toFloat());
        const auto modeFont = hypha::monoFont (
            presentation, hypha::typography::TextRole::navigation,
            hypha::typography::Composition::visualization);
        constexpr std::array<const char*, 4> labels { "LR", "MID", "SIDE", "M/S" };
        for (size_t index = 0u; index < labels.size(); ++index)
            KIRIN_REQUIRE (hypha::spectrum_geometry::displayModeBoundsFor (
                index, outer, scale).getWidth() >= std::ceil (
                    modeFont.getStringWidthFloat (labels[index]) + modeFont.getHeight() * 0.5f));
        const auto readoutFont = hypha::monoFont (
            presentation, hypha::typography::TextRole::readout,
            hypha::typography::Composition::visualization);
        if (scale <= 1.1f)
            KIRIN_REQUIRE (70.0f * scale >= std::ceil (
                hypha::tabularTextWidth (readoutFont, "M -144.0")
                + readoutFont.getHeight() * 0.5f));
        hypha::tests::verifySpectrumInteractionContract (
            markSpectrum, spectrumSnapshot,
            markSpectrumBounds.width, markSpectrumBounds.height, eventTime);
    }
    // Keep the performance-sensitive trail gate after all five MARK size contracts.
    hypha::tests::verifySpectrumFocusTrailRendering (spectrumSnapshot);
    hypha::SpectrumComponent lineEncodingSpectrum;
    lineEncodingSpectrum.setPresentationContext (hypha::presentation::forEditor (
        ui::editorWidth, ui::editorHeight));
    lineEncodingSpectrum.setSignalActive (true);
    lineEncodingSpectrum.setSize (spectrumBounds.width, spectrumBounds.height);
    KirinSpectrumView lineEncodingSnapshot = spectrumSnapshot;
    for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        lineEncodingSnapshot.display_db[index] = 0.0f;
        lineEncodingSnapshot.pre_dbfs[index] = -32.0f;
        lineEncodingSnapshot.post_dbfs[index] = -72.0f;
    }
    lineEncodingSpectrum.setSnapshot (lineEncodingSnapshot);
    juce::Image lineEncodingImage (
        juce::Image::ARGB, lineEncodingSpectrum.getWidth(), lineEncodingSpectrum.getHeight(), true);
    {
        juce::Graphics graphics (lineEncodingImage);
        lineEncodingSpectrum.paintEntireComponent (graphics, true);
    }
    const auto referencePlot = hypha::spectrum_geometry::dataPlotBoundsFor (
        lineEncodingSpectrum.getLocalBounds().toFloat());
    const int innerPlotWidth = juce::roundToInt (referencePlot.getWidth());
    const int preCurveY = juce::roundToInt (
        referencePlot.getY() + (32.0f / 96.0f) * referencePlot.getHeight());
    const int postCurveY = juce::roundToInt (
        referencePlot.getY() + (72.0f / 96.0f) * referencePlot.getHeight());
    const juce::Rectangle<int> curveProbe (
        ui::spectrumPlotLeftInset, preCurveY - 2, innerPlotWidth, 5);
    const juce::Rectangle<int> postCurveProbe (
        ui::spectrumPlotLeftInset, postCurveY - 2, innerPlotWidth, 5);
    const int preCurveRuns = countColourRunsAcross (
        lineEncodingImage, curveProbe, hypha::COL_SPECTRUM_PRE);
    const int postCurveRuns = countColourRunsAcross (
        lineEncodingImage, postCurveProbe, hypha::COL_SPECTRUM_POST);
    const int preCurveColumns = countColourColumnsAcross (
        lineEncodingImage, curveProbe, hypha::COL_SPECTRUM_PRE);
    const int postCurveColumns = countColourColumnsAcross (
        lineEncodingImage, postCurveProbe, hypha::COL_SPECTRUM_POST);
    std::cout << "Spectrum reference continuity: PRE-runs=" << preCurveRuns
              << ", PRE-columns=" << preCurveColumns << '/' << innerPlotWidth
              << ", POST-runs=" << postCurveRuns
              << ", POST-columns=" << postCurveColumns << '/' << innerPlotWidth << '\n';
    // Software rasterizers can leave a handful of one-pixel colour-probe gaps where a
    // translucent antialiased hairline crosses a pixel centre. Require near-full coverage
    // and only a few runs, which rejects a dashed encoding without assuming identical
    // subpixel rasterization on CoreGraphics and Windows.
    constexpr float minimumContinuousCoverage = 0.90f;
    KIRIN_REQUIRE (preCurveRuns >= 1 && preCurveRuns <= 5);
    KIRIN_REQUIRE (postCurveRuns >= 1 && postCurveRuns <= 5);
    KIRIN_REQUIRE ((float) preCurveColumns
                       >= (float) innerPlotWidth * minimumContinuousCoverage);
    KIRIN_REQUIRE ((float) postCurveColumns
                       >= (float) innerPlotWidth * minimumContinuousCoverage);

    const auto compactSpectrum = renderSpectrumAtSize (
        spectrumSnapshot, ui::spectrumSizePresets[0], "KIRIN_UI_RENDER_OUTPUT");
    const auto mediumSpectrum = renderSpectrumAtSize (
        spectrumSnapshot, ui::spectrumSizePresets[1], "KIRIN_UI_RENDER_OUTPUT_MEDIUM");
    const auto largeSpectrum = renderSpectrumAtSize (
        spectrumSnapshot, ui::spectrumSizePresets[2], "KIRIN_UI_RENDER_OUTPUT_LARGE");
    const auto extraLargeSpectrum = renderSpectrumAtSize (
        spectrumSnapshot, ui::spectrumSizePresets[3], "KIRIN_UI_RENDER_OUTPUT_XLARGE");
    const auto inspectionSpectrum = renderSpectrumAtSize (
        spectrumSnapshot, ui::spectrumSizePresets[4], "KIRIN_UI_RENDER_OUTPUT_INSPECTION");
    hypha::tests::writeFrequencyObservatoryPreview (spectrumSnapshot);
    const auto mediumBounds = ui::spectrumPlotBounds (375, 250);
    const auto largeBounds = ui::spectrumPlotBounds (450, 300);
    const auto extraLargeBounds = ui::spectrumPlotBounds (600, 400);
    const auto inspectionBounds = ui::spectrumPlotBounds (900, 600);
    KIRIN_REQUIRE (mediumSpectrum.image.getWidth() == mediumBounds.width);
    KIRIN_REQUIRE (mediumSpectrum.image.getHeight() == mediumBounds.height);
    KIRIN_REQUIRE (largeSpectrum.image.getWidth() == largeBounds.width);
    KIRIN_REQUIRE (largeSpectrum.image.getHeight() == largeBounds.height);
    KIRIN_REQUIRE (extraLargeSpectrum.image.getWidth() == extraLargeBounds.width);
    KIRIN_REQUIRE (extraLargeSpectrum.image.getHeight() == extraLargeBounds.height);
    KIRIN_REQUIRE (inspectionSpectrum.image.getWidth() == inspectionBounds.width);
    KIRIN_REQUIRE (inspectionSpectrum.image.getHeight() == inspectionBounds.height);
    KIRIN_REQUIRE (countVisiblePixels (mediumSpectrum.image,
                                       mediumSpectrum.image.getBounds()) > 1'000);
    KIRIN_REQUIRE (countVisiblePixels (largeSpectrum.image,
                                       largeSpectrum.image.getBounds()) > 1'500);
    KIRIN_REQUIRE (countVisiblePixels (extraLargeSpectrum.image,
                                       extraLargeSpectrum.image.getBounds()) > 2'500);
    KIRIN_REQUIRE (countVisiblePixels (inspectionSpectrum.image,
                                       inspectionSpectrum.image.getBounds()) > 4'000);
    std::cout << "Spectrum paint samples: " << compactSpectrum.paintMs
              << '/' << mediumSpectrum.paintMs
              << '/' << largeSpectrum.paintMs
              << '/' << extraLargeSpectrum.paintMs
              << '/' << inspectionSpectrum.paintMs << " ms/frame\n";
    KIRIN_REQUIRE (compactSpectrum.paintMs < 4.5);
    KIRIN_REQUIRE (mediumSpectrum.paintMs < 6.5);
    KIRIN_REQUIRE (largeSpectrum.paintMs < 8.5);
    KIRIN_REQUIRE (extraLargeSpectrum.paintMs < 12.5);
    KIRIN_REQUIRE (inspectionSpectrum.paintMs < 22.0);
    std::array<hypha::tests::SpectrumRenderResult, 5> midSideRenders;
    hypha::tests::verifyMidSideUsesSolidCurvesAtAllSizes();
    constexpr std::array<const char*, 5> midSideOutputs {
        "KIRIN_UI_MID_SIDE_OUTPUT", "KIRIN_UI_MID_SIDE_OUTPUT_MEDIUM",
        "KIRIN_UI_MID_SIDE_OUTPUT_LARGE", "KIRIN_UI_MID_SIDE_OUTPUT_XLARGE",
        "KIRIN_UI_MID_SIDE_OUTPUT_INSPECTION"
    };
    constexpr std::array<double, 5> midSideBudgets { 4.5, 6.5, 8.5, 12.5, 22.0 };
    std::cout << "Mid/Side Spectrum paint samples:";
    for (size_t index = 0; index < midSideRenders.size(); ++index)
    {
        midSideRenders[index] = renderMidSideSpectrumAtSize (
            midSideSnapshot, ui::spectrumSizePresets[index], midSideOutputs[index]);
        KIRIN_REQUIRE (midSideRenders[index].paintMs < midSideBudgets[index]);
        KIRIN_REQUIRE (countVisiblePixels (
            midSideRenders[index].image, midSideRenders[index].image.getBounds()) > 500);
        std::cout << (index == 0u ? " " : "/") << midSideRenders[index].paintMs;
    }
    std::cout << " ms/frame\n";

    std::cout << "UI render contract passed: delta="
              << deltaWidth << '/' << deltaLayout.deltaPrefixWidth << "px"
              << " (" << deltaPixels << " pixels)"
              << ", vector-arrow=" << arrowPixels << " pixels"
              << ", PRE-runs=" << preCurveRuns
              << ", POST-runs=" << postCurveRuns
              << ", spectrum-paint=" << compactSpectrum.paintMs
              << '/' << mediumSpectrum.paintMs
              << '/' << largeSpectrum.paintMs
              << '/' << extraLargeSpectrum.paintMs
              << '/' << inspectionSpectrum.paintMs << " ms/frame\n";
    return 0;
}
