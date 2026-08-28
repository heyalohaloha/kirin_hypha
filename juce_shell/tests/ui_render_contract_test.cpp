#include "../src/HyphaWidgets.h"
#include "../src/HyphaSpectrumComponent.h"
#include "SpectrumFocusTrailContractTest.h"
#include "SpectrumInteractionContractTest.h"
#include "SpectrumPresentationContractTest.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace ui = hypha::ui_contract;

static_assert (sizeof (KirinSpectrumView) == 3'096,
               "Rust/C Spectrum view ABI size must remain exact");

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

    bool fits (const juce::Font& font, const juce::String& text, int width)
    {
        return static_cast<int> (std::ceil (font.getStringWidthFloat (text))) <= width;
    }

    bool hasGlyph (const juce::Font& font, juce::juce_wchar codepoint)
    {
        juce::Array<int> glyphs;
        juce::Array<float> offsets;
        font.getGlyphPositions (juce::String::charToString (codepoint), glyphs, offsets);
        return glyphs.size() == 1 && glyphs[0] != 0
            && offsets.size() == 2 && offsets[1] > offsets[0];
    }

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
        constexpr int tolerance = 12;
        return pixel.getAlpha() > 16
            && std::abs ((int) pixel.getRed() - (int) target.getRed()) <= tolerance
            && std::abs ((int) pixel.getGreen() - (int) target.getGreen()) <= tolerance
            && std::abs ((int) pixel.getBlue() - (int) target.getBlue()) <= tolerance;
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

    float metricContentWidth (const juce::String& label,
                              const juce::String& value,
                              const juce::String& unit)
    {
        const auto labelWidth = juce::jmax (
            ui::metricMinimumLabelWidth,
            hypha::labelFont (ui::metricLabelFontHeight).getStringWidthFloat (label));
        const auto valueWidth = juce::jmax (
            ui::metricMinimumLabelWidth,
            hypha::monoFont (ui::metricValueFontHeight).getStringWidthFloat (value));
        return labelWidth + ui::metricHorizontalSpacing
             + valueWidth + ui::metricHorizontalSpacing
             + hypha::labelFont (ui::metricUnitFontHeight).getStringWidthFloat (unit);
    }

    struct SpectrumRenderResult
    {
        juce::Image image;
        double paintMs = 0.0;
    };

    SpectrumRenderResult renderSpectrumAtSize (
        const KirinSpectrumView& snapshot,
        const ui::SpectrumSizePreset& preset,
        const char* outputEnvironmentVariable)
    {
        hypha::SpectrumComponent component;
        const auto bounds = ui::spectrumPlotBounds (preset.width, preset.height);
        component.setSize (bounds.width, bounds.height);
        component.setSnapshot (snapshot);

        const float scale = ui::spectrumVisualScale (bounds.width);
        const float leftInset = (float) ui::spectrumPlotLeftInset * scale;
        const float rightInset = (float) ui::spectrumPlotRightInset * scale;
        const float hoverX = leftInset + 0.70f * ((float) bounds.width
                                                 - leftInset - rightInset);
        const float hoverY = (float) ui::spectrumPlotTopInset * scale + 20.0f * scale;
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

int main()
{
    hypha::tests::verifySpectrumFocusTrailContract();
    hypha::tests::verifySpectrumPresentationContract();
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const auto label = hypha::labelFont (ui::titleFontHeight);
    const auto mono = hypha::monoFont (ui::pairStatusFontHeight);
   #if JUCE_WINDOWS
    KIRIN_REQUIRE (label.getTypefaceName().equalsIgnoreCase (ui::windowsLabelFontFamily));
    KIRIN_REQUIRE (mono.getTypefaceName().equalsIgnoreCase (ui::windowsMonoFontFamily));
   #else
    KIRIN_REQUIRE (label.getTypefaceName().equalsIgnoreCase (ui::labelFontFamily));
    KIRIN_REQUIRE (mono.getTypefaceName().equalsIgnoreCase (ui::monoFontFamily));
   #endif

    const auto preLayout = ui::editorLayout (false);
    const auto postLayout = ui::editorLayout (true);
    const int postTitleWidth = static_cast<int> (
        std::ceil (label.getStringWidthFloat (ui::postTitle)));
    KIRIN_REQUIRE (fits (label, ui::preTitle, preLayout.title.width));
    KIRIN_REQUIRE (postTitleWidth <= postLayout.title.width);
    KIRIN_REQUIRE (ui::right (postLayout.title) + ui::titlePairGap
                   == postLayout.pairStatus.x);

    const auto deltaFont = hypha::labelFont (ui::metricLabelFontHeight);
    const int deltaWidth = static_cast<int> (
        std::ceil (deltaFont.getStringWidthFloat (hypha::delta())));
    const auto deltaLayout = ui::loudnessSelectorLayout (true, deltaWidth);
    KIRIN_REQUIRE (deltaWidth <= deltaLayout.deltaPrefixWidth);
    KIRIN_REQUIRE (deltaLayout.momentary.width >= ui::loudnessSegmentMinimumWidth);
    KIRIN_REQUIRE (deltaLayout.shortTerm.width >= ui::loudnessSegmentMinimumWidth);

    // Audit every non-ASCII UI symbol still rendered by a font. The pair-menu arrow is excluded:
    // PairDropdownButton deliberately owns it as vector geometry.
    for (const auto codepoint : { (juce::juce_wchar) 0x0394 }) // Δ
        KIRIN_REQUIRE (hasGlyph (deltaFont, codepoint));
    for (const auto codepoint : { (juce::juce_wchar) 0x25CF, // ●
                                  (juce::juce_wchar) 0x25CC, // ◌
                                  (juce::juce_wchar) 0x2014 }) // —
        KIRIN_REQUIRE (hasGlyph (mono, codepoint));
    KIRIN_REQUIRE (hasGlyph (hypha::labelFont (ui::menuFontHeight),
                             (juce::juce_wchar) 0x00B7)); // ·

    KIRIN_REQUIRE (fits (mono, juce::CharPointer_UTF8 ("PAIR ●"), ui::pairStatusWidth));
    KIRIN_REQUIRE (fits (mono, juce::CharPointer_UTF8 ("PAIR ◌"), ui::pairStatusWidth));
    KIRIN_REQUIRE (fits (mono, juce::CharPointer_UTF8 ("PAIR —"), ui::pairStatusWidth));
    KIRIN_REQUIRE (fits (hypha::monoFont (ui::nameFontHeight), "WWWWWWWWWWWWWWWW",
                         preLayout.name.width));
    KIRIN_REQUIRE (fits (hypha::monoFont (ui::nameFontHeight),
                         "pair: WWWWWWWWWWWWWWWW", postLayout.name.width));
    KIRIN_REQUIRE (fits (hypha::monoFont (ui::spectrumLegendFontHeight),
                         juce::CharPointer_UTF8 ("\xCE\x94"),
                         ui::spectrumDeltaLegendLabelWidth));
    KIRIN_REQUIRE (fits (hypha::monoFont (ui::spectrumLegendFontHeight), "PRE",
                         ui::spectrumPreLegendLabelWidth));
    KIRIN_REQUIRE (fits (hypha::monoFont (ui::spectrumLegendFontHeight), "POST",
                         ui::spectrumPostLegendLabelWidth));
    KIRIN_REQUIRE (fits (hypha::monoFont (8.5f), "22.0 kHz",
                         ui::spectrumHoverFrequencyWidth));
    KIRIN_REQUIRE (fits (hypha::monoFont (8.5f), juce::CharPointer_UTF8 ("Δ+18.0"),
                         ui::spectrumHoverDeltaWidth));
    KIRIN_REQUIRE (fits (hypha::monoFont (8.0f), "PRE -144.0",
                         ui::spectrumExpandedPreWidth));
    KIRIN_REQUIRE (fits (hypha::monoFont (8.0f), "POST -144.0",
                         ui::spectrumExpandedPostWidth));
    KIRIN_REQUIRE (fits (hypha::monoFont (8.0f), juce::CharPointer_UTF8 ("Δ+18.0"),
                         ui::spectrumExpandedDeltaWidth));
    KIRIN_REQUIRE (std::abs (ui::spectrumStrokeScale (1.0f) - 1.0f) < 1.0e-6f);
    KIRIN_REQUIRE (std::abs (ui::spectrumStrokeScale (1.25f) - 1.12f) < 1.0e-6f);
    KIRIN_REQUIRE (std::abs (ui::spectrumStrokeScale (1.5f) - 1.22f) < 1.0e-6f);
    KIRIN_REQUIRE (std::abs (ui::spectrumGlowScale (1.5f) - 1.15f) < 1.0e-6f);
    for (const auto& preset : ui::spectrumSizePresets)
    {
        juce::TextButton sizeButton (preset.buttonText);
        sizeButton.setSize (ui::spectrumSizeToggleWidth, ui::spectrumToggleHeight);
        const auto buttonFont = sizeButton.getLookAndFeel().getTextButtonFont (
            sizeButton, ui::spectrumToggleHeight);
        KIRIN_REQUIRE (fits (buttonFont, preset.buttonText,
                             ui::spectrumSizeToggleWidth - 8));
    }
    // JUCE's TooltipWindow lays out 13 px bold text and adds 14 px of horizontal padding.
    // Keep the complete native tooltip inside the 300 px editor instead of relying on clipping.
    const juce::Font tooltipFont (13.0f, juce::Font::bold);
    const int tooltipMaximumWidth = ui::editorWidth - 2 * ui::margin - 14;
    KIRIN_REQUIRE (fits (tooltipFont, ui::spectrumTooltip (false), tooltipMaximumWidth));
    KIRIN_REQUIRE (fits (tooltipFont, ui::spectrumTooltip (true), tooltipMaximumWidth));

    const float metricWidth = static_cast<float> (
        ui::metricCellBounds (0, postLayout.metricTop).width);
    KIRIN_REQUIRE (metricContentWidth (hypha::delta() + "Crest", "-100.0", "dB")
                   <= metricWidth);
    KIRIN_REQUIRE (metricContentWidth ("Max TP", "-100.0", "dBTP") <= metricWidth);
    KIRIN_REQUIRE (metricContentWidth (hypha::delta() + "Sharp", "-100.0", "acum")
                   <= metricWidth);

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
    spectrumSnapshot.channel_mode = KIRIN_SPECTRUM_CHANNEL_LR;
    spectrumSnapshot.channels = 2;
    spectrumSnapshot.sample_rate = 48'000;
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
    spectrum.setSnapshot (spectrumSnapshot);
    hypha::tests::verifySpectrumFocusTrailRendering (spectrumSnapshot);
    const float previewHoverX = (float) ui::spectrumPlotLeftInset
                              + 0.70f * (float) (spectrumBounds.width
                                               - ui::spectrumPlotLeftInset
                                               - ui::spectrumPlotRightInset);
    const float previewHoverY = (float) ui::spectrumPlotTopInset + 20.0f;
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
    const float clearY = (float) ui::spectrumPlotTopInset + 8.0f;
    const juce::MouseEvent clearEvent (
        juce::Desktop::getInstance().getMainMouseSource(),
        { clearX, clearY }, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        &spectrum, &spectrum, eventTime,
        { clearX, clearY }, eventTime, 0, false);
    spectrum.mouseDown (clearEvent);
    KIRIN_REQUIRE (! spectrum.hasFocusLock());

    hypha::tests::verifySpectrumInteractionContract (
        spectrum, spectrumSnapshot, spectrumBounds.width, spectrumBounds.height, eventTime);

    hypha::SpectrumComponent lineEncodingSpectrum;
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
    const int innerPlotWidth = spectrumBounds.width - ui::spectrumPlotLeftInset
                            - ui::spectrumPlotRightInset;
    const int innerPlotHeight = spectrumBounds.height - ui::spectrumPlotTopInset
                             - ui::spectrumPlotBottomInset;
    const int preCurveY = ui::spectrumPlotTopInset
                        + juce::roundToInt ((32.0f / 96.0f) * (float) innerPlotHeight);
    const int postCurveY = ui::spectrumPlotTopInset
                         + juce::roundToInt ((72.0f / 96.0f) * (float) innerPlotHeight);
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
    const auto mediumBounds = ui::spectrumPlotBounds (375, 250);
    const auto largeBounds = ui::spectrumPlotBounds (450, 300);
    KIRIN_REQUIRE (mediumSpectrum.image.getWidth() == mediumBounds.width);
    KIRIN_REQUIRE (mediumSpectrum.image.getHeight() == mediumBounds.height);
    KIRIN_REQUIRE (largeSpectrum.image.getWidth() == largeBounds.width);
    KIRIN_REQUIRE (largeSpectrum.image.getHeight() == largeBounds.height);
    KIRIN_REQUIRE (countVisiblePixels (mediumSpectrum.image,
                                       mediumSpectrum.image.getBounds()) > 1'000);
    KIRIN_REQUIRE (countVisiblePixels (largeSpectrum.image,
                                       largeSpectrum.image.getBounds()) > 1'500);
    std::cout << "Spectrum paint samples: " << compactSpectrum.paintMs
              << '/' << mediumSpectrum.paintMs
              << '/' << largeSpectrum.paintMs << " ms/frame\n";
    KIRIN_REQUIRE (compactSpectrum.paintMs < 4.0);
    KIRIN_REQUIRE (mediumSpectrum.paintMs < 6.0);
    KIRIN_REQUIRE (largeSpectrum.paintMs < 8.0);

    std::cout << "UI render contract passed: label="
              << label.getTypefaceName().toStdString()
              << ", mono=" << mono.getTypefaceName().toStdString()
              << ", POST=" << postTitleWidth << '/' << postLayout.title.width << "px"
              << ", delta=" << deltaWidth << '/' << deltaLayout.deltaPrefixWidth << "px"
              << " (" << deltaPixels << " pixels)"
              << ", vector-arrow=" << arrowPixels << " pixels"
              << ", PRE-runs=" << preCurveRuns
              << ", POST-runs=" << postCurveRuns
              << ", spectrum-paint=" << compactSpectrum.paintMs
              << '/' << mediumSpectrum.paintMs
              << '/' << largeSpectrum.paintMs << " ms/frame\n";
    return 0;
}
