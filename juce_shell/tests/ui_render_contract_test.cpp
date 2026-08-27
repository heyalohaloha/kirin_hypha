#include "../src/HyphaWidgets.h"
#include "../src/HyphaSpectrumComponent.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

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
}

int main()
{
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
    juce::Image spectrumImage (
        juce::Image::ARGB, spectrum.getWidth(), spectrum.getHeight(), true);
    {
        juce::Graphics graphics (spectrumImage);
        spectrum.paintEntireComponent (graphics, true);
    }
    KIRIN_REQUIRE (countDifferentPixels (warmingSpectrumImage, spectrumImage) > 100);

    juce::Image spectrumPreview (
        juce::Image::ARGB, spectrum.getWidth(), spectrum.getHeight(), true);
    constexpr int spectrumPaintIterations = 200;
    const double spectrumPaintStartedMs = juce::Time::getMillisecondCounterHiRes();
    for (int iteration = 0; iteration < spectrumPaintIterations; ++iteration)
    {
        spectrumPreview.clear (spectrumPreview.getBounds(), hypha::BG);
        juce::Graphics graphics (spectrumPreview);
        spectrum.paintEntireComponent (graphics, true);
    }
    const double spectrumPaintMs = (juce::Time::getMillisecondCounterHiRes()
                                  - spectrumPaintStartedMs) / spectrumPaintIterations;
    KIRIN_REQUIRE (spectrumPaintMs < 4.0);

    const auto outputPath = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_UI_RENDER_OUTPUT", {});
    if (outputPath.isNotEmpty())
    {
        auto output = juce::File (outputPath).createOutputStream();
        KIRIN_REQUIRE (output != nullptr);
        KIRIN_REQUIRE (juce::PNGImageFormat().writeImageToStream (spectrumPreview, *output));
    }

    std::cout << "UI render contract passed: label="
              << label.getTypefaceName().toStdString()
              << ", mono=" << mono.getTypefaceName().toStdString()
              << ", POST=" << postTitleWidth << '/' << postLayout.title.width << "px"
              << ", delta=" << deltaWidth << '/' << deltaLayout.deltaPrefixWidth << "px"
              << " (" << deltaPixels << " pixels)"
              << ", vector-arrow=" << arrowPixels << " pixels"
              << ", spectrum-paint=" << spectrumPaintMs << " ms/frame\n";
    return 0;
}
