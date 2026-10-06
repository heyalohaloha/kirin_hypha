#pragma once

#include "../src/HyphaReferenceSelectorLookAndFeel.h"
#include "../src/HyphaSelectMenu.h"

#include <cstdlib>
#include <functional>
#include <iostream>

// Every popup (PAIR, MENU and the REF selectors) is one Kirin Select drawing: the same warm window,
// rounded rows, a champagne bar and check on the chosen row and gold section headers. The actual
// pixels are checked, at native DPI 1 and 2, for the common look and the REF selector look.
namespace hypha::tests::select_menu_contract
{
inline void require (bool condition, const juce::String& message)
{
    if (condition) return;
    std::cerr << "Select menu contract failed: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

inline juce::Image paint (float dpi, int width, int height, const std::function<void (juce::Graphics&)>& draw)
{
    juce::Image image (juce::Image::ARGB, int (width * dpi), int (height * dpi), true, juce::NativeImageType {});
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (dpi));
    draw (g);
    return image;
}

inline int countNear (const juce::Image& image, juce::Rectangle<int> area, juce::Colour wanted, int tolerance)
{
    int count = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto c = image.getPixelAt (x, y);
            if (c.getAlpha() > 200 && std::abs (c.getRed() - wanted.getRed()) <= tolerance
                && std::abs (c.getGreen() - wanted.getGreen()) <= tolerance
                && std::abs (c.getBlue() - wanted.getBlue()) <= tolerance) ++count;
        }
    return count;
}

inline int differentPixels (const juce::Image& first, const juce::Image& second)
{
    int count = 0;
    for (int y = 0; y < first.getHeight(); ++y)
        for (int x = 0; x < first.getWidth(); ++x)
            count += first.getPixelAt (x, y) != second.getPixelAt (x, y) ? 1 : 0;
    return count;
}

inline juce::Image menu (TextLookAndFeel& look, float dpi, const juce::String& chosen)
{
    constexpr int width = 260, height = select_menu::border * 2 + 22 + select_menu::rowHeight * 3;
    return paint (dpi, width, height, [&] (juce::Graphics& g) {
        look.drawPopupMenuBackground (g, width, height);
        const auto inner = juce::Rectangle<int> (width, height).reduced (select_menu::border);
        look.drawPopupMenuSectionHeader (g, inner.withHeight (22), "B / SONG");
        for (int row = 0; row < 3; ++row)
        {
            const auto area = inner.withTop (inner.getY() + 22 + row * select_menu::rowHeight)
                                  .withHeight (select_menu::rowHeight);
            look.drawPopupMenuItem (g, area, false, true, row == 1, row == 2, false,
                                    row == 2 ? chosen : juce::String (row == 0 ? "Reference 01" : "Reference 02"),
                                    {}, nullptr, nullptr);
        }
    });
}

inline void writeReview (const juce::Image& image, const juce::String& name)
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_BLIND_LIGHT_REVIEW_DIR", {});
    if (path.isEmpty()) return;
    const juce::File directory (path);
    require (directory.createDirectory().wasOk(), "select menu review directory");
    juce::FileOutputStream stream (directory.getChildFile ("select_menu_" + name + ".png"));
    require (stream.setPosition (0) && stream.truncate().wasOk()
                 && juce::PNGImageFormat().writeImageToStream (image, stream), "select menu review written");
}

inline void verify()
{
    const i18n::ScopedLanguage language (i18n::Language::english);
    TextLookAndFeel common;
    reference_ui::ReferenceSelectorLookAndFeel reference;
    require (common.findColour (juce::PopupMenu::backgroundColourId).isTransparent()
                 && reference.findColour (juce::PopupMenu::backgroundColourId).isTransparent(),
             "popups ask JUCE for a transparent window, so the window keeps its rounded corners");
    require (common.getPopupMenuBorderSize() == select_menu::border, "the window keeps Kirin Select's padding");
    int width = 0, height = 0;
    common.getIdealPopupMenuItemSize ("Reference 01", false, 28, width, height);
    require (height >= select_menu::rowHeight, "rows keep Kirin Select's height even when a caller asks for 28");
    for (const float dpi : { 1.0f, 2.0f })
    {
        const auto window = [dpi] (TextLookAndFeel& look) {
            return paint (dpi, 260, 120, [&look] (juce::Graphics& g) { look.drawPopupMenuBackground (g, 260, 120); });
        };
        require (differentPixels (window (common), window (reference)) == 0,
                 "REF selectors draw the same window as every other popup");
        for (auto* look : { &common, static_cast<TextLookAndFeel*> (&reference) })
        {
        const auto shared = menu (*look, dpi, "Reference 03");
        writeReview (shared, (look == &common ? "common_dpi" : "reference_dpi") + juce::String (int (dpi)));
        const auto s = [dpi] (int value) { return juce::roundToInt ((float) value * dpi); };
        // Warm everywhere: never JUCE's cyan highlight or blue scan lines.
        for (int y = 0; y < shared.getHeight(); ++y)
            for (int x = 0; x < shared.getWidth(); ++x)
            {
                const auto c = shared.getPixelAt (x, y);
                require (c.getAlpha() < 16 || (c.getRed() >= c.getGreen() && c.getGreen() >= c.getBlue()),
                         "every popup pixel stays warm at DPI " + juce::String (dpi));
            }
        const auto centre = shared.getPixelAt (shared.getWidth() / 2, s (select_menu::border + 22 + 4));
        require (centre.getRed() <= 30 && centre.getAlpha() > 230, "the window is Kirin Select's near-black");
        if (select_menu::roundedWindow (common))
            require (shared.getPixelAt (0, 0).getAlpha() < 16, "the corners stay rounded where the window can be transparent");
        const int top = select_menu::border + 22;
        const auto chosenRow = juce::Rectangle<int> (s (select_menu::border), s (top + 2 * select_menu::rowHeight),
                                                     s (260 - 2 * select_menu::border), s (select_menu::rowHeight));
        require (countNear (shared, chosenRow.withWidth (s (3)), select_menu::champagne, 40) > 0,
                 "the chosen row carries the champagne bar");
        require (countNear (shared, chosenRow.withLeft (chosenRow.getRight() - s (select_menu::trailing)),
                            select_menu::champagne, 60) > 0, "the chosen row carries the champagne check");
        const auto header = juce::Rectangle<int> (0, s (select_menu::border), shared.getWidth(), s (22));
        require (countNear (shared, header, select_menu::goldLabel, 70) > 0, "section headers are small gold capitals");
        const auto plain = shared.getClippedImage ({ s (select_menu::border), s (top), s (40), s (6) });
        const auto pointed = shared.getClippedImage ({ s (select_menu::border), s (top + select_menu::rowHeight), s (40), s (6) });
        require (differentPixels (plain, pointed) > 0, "the pointed row is lifted from the window");
        }
    }
}
}
