#include "../src/HyphaWidgets.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace ui = hypha::ui_contract;

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

    std::cout << "UI render contract passed: label="
              << label.getTypefaceName().toStdString()
              << ", mono=" << mono.getTypefaceName().toStdString()
              << ", POST=" << postTitleWidth << '/' << postLayout.title.width << "px"
              << ", delta=" << deltaWidth << '/' << deltaLayout.deltaPrefixWidth << "px"
              << " (" << deltaPixels << " pixels)"
              << ", vector-arrow=" << arrowPixels << " pixels\n";
    return 0;
}
