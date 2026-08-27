#include "../src/HyphaWidgets.h"
#include "../src/HyphaSpectrumBallistics.h"
#include "../src/HyphaSpectrumComponent.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace ui = hypha::ui_contract;

static_assert (sizeof (KirinSpectrumView) == 3'088,
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

    KirinSpectrumView flatSpectrum (float preDbfs, float postDbfs, float deltaDb)
    {
        KirinSpectrumView view {};
        view.status = KIRIN_SPECTRUM_ACTIVE;
        view.has_data = 1;
        view.sample_rate = 48'000;
        view.min_hz = 10.0f;
        view.max_hz = 22'000.0f;
        for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
        {
            view.pre_dbfs[index] = preDbfs;
            view.post_dbfs[index] = postDbfs;
            view.display_db[index] = deltaDb;
        }
        return view;
    }

    void verifySpectrumBallistics()
    {
        constexpr size_t lowBand = 0;
        constexpr size_t highBand = KIRIN_SPECTRUM_BAND_COUNT - 1u;
        constexpr float tickSeconds = 0.1f;
        const auto neutral = flatSpectrum (-60.0f, -60.0f, 0.0f);

        hypha::SpectrumBallistics positive;
        KIRIN_REQUIRE (positive.setTarget (neutral));
        auto positiveTarget = flatSpectrum (-20.0f, -20.0f, 12.0f);
        KIRIN_REQUIRE (! positive.setTarget (positiveTarget));
        KIRIN_REQUIRE (positive.advance (tickSeconds));
        KIRIN_REQUIRE (positive.frame().display_db[lowBand] > 0.0f);
        KIRIN_REQUIRE (positive.frame().display_db[highBand]
                       > positive.frame().display_db[lowBand]);
        KIRIN_REQUIRE (positive.frame().pre_dbfs[highBand]
                       > positive.frame().pre_dbfs[lowBand]);

        hypha::SpectrumBallistics negative;
        KIRIN_REQUIRE (negative.setTarget (neutral));
        auto negativeTarget = flatSpectrum (-20.0f, -20.0f, -12.0f);
        KIRIN_REQUIRE (! negative.setTarget (negativeTarget));
        KIRIN_REQUIRE (negative.advance (tickSeconds));
        KIRIN_REQUIRE (std::abs (positive.frame().display_db[highBand]
                                + negative.frame().display_db[highBand]) < 0.0001f);

        hypha::SpectrumBallistics returning;
        KIRIN_REQUIRE (returning.setTarget (flatSpectrum (-20.0f, -20.0f, 12.0f)));
        KIRIN_REQUIRE (! returning.setTarget (flatSpectrum (-60.0f, -60.0f, 0.0f)));
        KIRIN_REQUIRE (returning.advance (tickSeconds));
        const float awayTravel = positive.frame().display_db[highBand];
        const float returnTravel = 12.0f - returning.frame().display_db[highBand];
        KIRIN_REQUIRE (awayTravel > returnTravel);
        KIRIN_REQUIRE (positive.frame().pre_dbfs[highBand] + 60.0f
                       > -20.0f - returning.frame().pre_dbfs[highBand]);

        KIRIN_REQUIRE (! returning.advance (0.0f));
        KIRIN_REQUIRE (! returning.advance (std::numeric_limits<float>::quiet_NaN()));
        auto changedDomain = flatSpectrum (-48.0f, -42.0f, -6.0f);
        changedDomain.sample_rate = 44'100;
        KIRIN_REQUIRE (returning.setTarget (changedDomain));
        KIRIN_REQUIRE (std::abs (returning.frame().display_db[highBand] + 6.0f) < 0.0001f);
        returning.reset();
        KIRIN_REQUIRE (! returning.hasFrame());
        KIRIN_REQUIRE (! returning.advance (tickSeconds));

        hypha::SpectrumBallistics benchmark;
        benchmark.setTarget (neutral);
        auto movingTarget = positiveTarget;
        constexpr int motionIterations = 2'000;
        const double motionStartedMs = juce::Time::getMillisecondCounterHiRes();
        for (int iteration = 0; iteration < motionIterations; ++iteration)
        {
            movingTarget.display_db[highBand] = iteration % 2 == 0 ? 12.0f : -12.0f;
            benchmark.setTarget (movingTarget);
            benchmark.advance (tickSeconds);
        }
        const double motionMs = (juce::Time::getMillisecondCounterHiRes() - motionStartedMs)
                              / motionIterations;
        KIRIN_REQUIRE (motionMs < 0.5);
        std::cout << "Spectrum motion: " << motionMs << " ms/tick\n";
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    verifySpectrumBallistics();

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
    KIRIN_REQUIRE (fits (hypha::monoFont (ui::spectrumLegendFontHeight), "PRE",
                         ui::spectrumPreLegendLabelWidth));
    KIRIN_REQUIRE (fits (hypha::monoFont (ui::spectrumLegendFontHeight), "POST",
                         ui::spectrumPostLegendLabelWidth));
    KIRIN_REQUIRE (fits (hypha::monoFont (8.5f), "22.0 kHz",
                         ui::spectrumHoverFrequencyWidth));
    KIRIN_REQUIRE (fits (hypha::monoFont (8.5f), juce::CharPointer_UTF8 ("Δ+18.0"),
                         ui::spectrumHoverDeltaWidth));
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
    spectrumSnapshot.sample_rate = 48'000;
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
    // The translucent hairline can form a few colour-probe runs through antialiasing,
    // but must remain visually continuous rather than returning to a dashed encoding.
    KIRIN_REQUIRE (preCurveRuns >= 1 && preCurveRuns <= 5);
    KIRIN_REQUIRE (postCurveRuns == 1);

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
    KIRIN_REQUIRE (largeSpectrum.paintMs * ui::spectrumPresentationHz < 240.0);

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
