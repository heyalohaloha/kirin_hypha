#pragma once

#include <tuple>
#include <vector>

#include "HyphaLanguage.h"
#include "HyphaObservatoryWorld.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
#include "HyphaUiPreferences.h"
#include "KirinTestStorage.h"

// SAVE IMAGE on the BLIND RESULT (2026-11-11): one square image of a finished blind listen to post.
// It holds only what the RESULT says (which source was which, the guess, the RESULT's own notes)
// under the names the user typed, never a song, track or PRE name. Labels and prose are in the
// screen's language; the last sentence is always the same one, then the address.
namespace hypha::blind_image
{
constexpr int pixelSize = 1080;       // square, so every feed shows it whole
constexpr float logicalSize = 540.0f; // drawn in editor units at twice their size, as Capture is
constexpr int nameLimit = 16;
inline const char* const closingSentence = "One blind listen at matched level.";
inline const char* const address = "kirinmastering.com/hypha";

struct Facts
{
    bool candidate = false;  // A/B Blind (A and B), otherwise PRE/POST Blind
    bool firstPre = false;   // SOURCE 1 was A (PRE)
    int selected = 0;        // the source selected on the RESULT, 1 or 2
    int guess = 0;           // the GUESS answer, 1 or 2; 0 when none was given
    double loweredDb = 0.0;  // both sources played this much lower to avoid clipping
    juce::String note;       // the RESULT's own note: a source that stood in, an audio estimate
    juce::String nameA, nameB;
};

inline juce::String defaultName (bool candidate, bool b) { return candidate ? (b ? "B" : "A") : (b ? "POST" : "PRE"); }

// What a typed name becomes in the image: one line, at most 16 characters, the default when empty.
inline juce::String cleanName (const juce::String& typed, const juce::String& fallback)
{
    juce::String name;
    for (auto t = typed.getCharPointer(); ! t.isEmpty(); ++t)
        if (const auto c = *t; c >= 0x20 && c != 0x7f && ! (c >= 0x80 && c < 0xa0) && c != 0x2028 && c != 0x2029)
            name += juce::String::charToString (c);
    name = name.trim();
    if (name.length() > nameLimit) name = name.substring (0, nameLimit).trimEnd();
    return name.isEmpty() ? fallback : name;
}

// GUESS and the names live with the other shared screen settings (ui-preferences.txt), or under the
// test storage root when a test sets one, so a test never reads or writes the user's settings.
inline juce::File preferenceFile()
{
    if (const auto test = storage::testPluginDataRoot())
        return test->getParentDirectory().getParentDirectory().getChildFile ("Kirin/Kirin Hypha/ui-preferences.txt");
    return ui_preferences::defaultFile();
}
inline bool guessPreferred (const juce::File& file) { return ui_preferences::read (file, "blind-guess") == "on"; }
inline bool preferGuess (const juce::File& file, bool on) { return ui_preferences::write (file, "blind-guess", on ? "on" : "off"); }
inline juce::String nameKey (bool candidate, bool b)
{
    return juce::String ("blind-image-") + (candidate ? "ab-" : "prepost-") + (b ? "b" : "a");
}
inline juce::String rememberedName (const juce::File& file, bool candidate, bool b)
{
    return cleanName (ui_preferences::read (file, nameKey (candidate, b)), defaultName (candidate, b));
}
inline bool rememberName (const juce::File& file, bool candidate, bool b, const juce::String& typed)
{
    const auto name = cleanName (typed, {});
    return ui_preferences::write (file, nameKey (candidate, b), name == defaultName (candidate, b) ? juce::String() : name);
}

// One line of text as the image draws it: already in the screen's language and in its font.
struct Item
{
    juce::String text;
    juce::Rectangle<float> area;
    juce::Font font;
    juce::Colour colour;
    juce::Justification justification = juce::Justification::centred;
    typography::BlindImageText role = typography::BlindImageText::small;
    int nameSize = 34;
    float kerning = 0.0f;
    bool nativeBold = false;
};

struct Layout
{
    juce::Rectangle<float> panel, cardOne, cardTwo, chip, badge;
    float rule = 0.0f;
    std::vector<Item> items;  // top to bottom; the last two are the closing sentence and the address
};

namespace detail
{
inline juce::Font imageFont (typography::BlindImageText role, int nameSize = 34, float kerning = 0.0f)
{
    return labelFont (presentation::forOutput (540, 540, presentation::OutputTarget::capture),
                      role, nameSize).withExtraKerningFactor (kerning);
}

inline juce::Font shownFont (const juce::String& text, const juce::Font& font)
{
    return requiresJapaneseGlyphs (text) ? nativeTextFontLike (font) : font;
}

// A sentence around a name: the sentence is translated, the name is kept as typed.
inline juce::String around (const char* sentence, const juce::String& name)
{
    return i18n::tr (sentence).replace ("%1", displayableText (name));
}

// The RESULT's PRE/POST notes use role names. In the image those roles use the typed names,
// without replacing text inside a name that happens to contain PRE or POST.
inline juce::String noteWithNames (const Facts& facts)
{
    const auto note = i18n::tr (facts.note);
    if (facts.candidate)
        return note.startsWith ("Audio estimate: A may")
            ? note.replace ("Audio estimate: A may", "Audio estimate: " + facts.nameA + " may")
            : note.replace (juce::String::fromUTF8 (u8"音の推定：Aが"),
                            juce::String::fromUTF8 (u8"音の推定：") + facts.nameA + juce::String::fromUTF8 (u8"が"));
    juce::String named;
    for (int i = 0; i < note.length();)
    {
        if (note.substring (i, i + 4) == "POST") { named += facts.nameB; i += 4; }
        else if (note.substring (i, i + 3) == "PRE") { named += facts.nameA; i += 3; }
        else { named += juce::String::charToString (note[i]); ++i; }
    }
    return named;
}

inline void addLine (Layout& layout, const juce::String& text, juce::Rectangle<float> area, typography::BlindImageText role,
                     juce::Colour colour, juce::Justification justification = juce::Justification::centred,
                     float kerning = 0.0f, int nameSize = 34, bool nativeBold = false)
{
    const auto font = shownFont (text, imageFont (role, nameSize, kerning));
    layout.items.push_back ({ text, area, nativeBold ? font.boldened() : font, colour, justification,
                              role, nameSize, kerning, nativeBold });
}

// Lines of `shown` at `width`: Japanese breaks between phrases, English between words.
inline juce::StringArray wrap (const juce::String& shown, const juce::Font& font, float width)
{
    if (requiresJapaneseGlyphs (shown)) return text_style::japaneseLines (shown, font, width);
    juce::StringArray lines;
    juce::String line;
    for (const auto& word : juce::StringArray::fromTokens (shown, " ", ""))
    {
        const auto next = line.isEmpty() ? word : line + " " + word;
        if (line.isNotEmpty() && font.getStringWidthFloat (next) > width) { lines.add (line); line = word; }
        else line = next;
    }
    if (line.isNotEmpty()) lines.add (line);
    return lines;
}

// A name at the largest size from 34 down that fits the card on one line, or on two lines below that.
inline juce::StringArray nameLines (const juce::String& name, float width, float& height)
{
    for (height = 34.0f; height >= 22.0f; height -= 1.0f)
        if (shownFont (name, imageFont (typography::BlindImageText::name, (int) height)).getStringWidthFloat (name) <= width) return { name };
    for (height = 21.0f; height >= 14.0f; height -= 1.0f)
    {
        const auto shownName = shownFont (name, imageFont (typography::BlindImageText::name, (int) height));
        auto lines = wrap (name, shownName, width);
        if (lines.size() > 2 || (lines.size() == 1 && shownName.getStringWidthFloat (name) > width))
        {
            // One unbroken word: split it where both halves fit.
            const auto half = (name.length() + 1) / 2;
            lines = { name.substring (0, half), name.substring (half) };
        }
        if (lines.size() <= 2 && shownName.getStringWidthFloat (lines[0]) <= width
            && shownName.getStringWidthFloat (lines[lines.size() - 1]) <= width)
            return lines;
    }
    return { name };
}
}

// Everything the image says, where, in the current language. The painter draws exactly these.
inline Layout layoutFor (const Facts& facts)
{
    using namespace detail;
    using ImageText = typography::BlindImageText;
    Layout layout;
    const float margin = 34.0f, pad = 26.0f;
    const auto secondary = COL_TEXT_SECONDARY.brighter (0.25f);
    addLine (layout, "KIRIN HYPHA", { margin, 30.0f, 230.0f, 22.0f }, ImageText::small, COL_SPECTRUM_DELTA_BR, juce::Justification::centredLeft, 0.22f);
    const auto mode = i18n::tr (facts.candidate ? "A/B BLIND" : "PRE/POST BLIND");
    const auto modeWidth = std::ceil (imageFont (ImageText::small, 34, 0.12f).getStringWidthFloat (mode));
    layout.badge = { logicalSize - margin - modeWidth - 20.0f, 24.0f, modeWidth + 20.0f, 34.0f };
    addLine (layout, mode, { layout.badge.getX() + 10.0f, 30.0f, modeWidth, 22.0f },
             ImageText::small, secondary, juce::Justification::centredRight, 0.12f);
    layout.panel = { margin, 66.0f, logicalSize - margin * 2.0f, logicalSize - 66.0f - margin };
    auto body = layout.panel.reduced (pad, 0.0f);
    float y = layout.panel.getY() + 20.0f;
    addLine (layout, i18n::tr ("BLIND RESULT"), { body.getX(), y, body.getWidth(), 38.0f }, ImageText::title, COL_NORMAL,
             juce::Justification::centred, 0.10f);
    y += 38.0f + 16.0f;

    const auto cardWidth = (body.getWidth() - 16.0f) * 0.5f;
    layout.cardOne = { body.getX(), y, cardWidth, 124.0f };
    layout.cardTwo = layout.cardOne.withX (body.getRight() - cardWidth);
    const auto one = facts.firstPre ? facts.nameA : facts.nameB, two = facts.firstPre ? facts.nameB : facts.nameA;
    float heightOne = 0.0f, heightTwo = 0.0f;
    const auto inner = cardWidth - 24.0f;
    const auto linesOne = nameLines (displayableText (one), inner, heightOne);
    const auto linesTwo = nameLines (displayableText (two), inner, heightTwo);
    // Both names at one size, so neither source looks favoured.
    const auto nameHeight = juce::jmin (heightOne, heightTwo);
    for (auto [card, label, lines, source] : { std::tuple { layout.cardOne, "SOURCE 1", linesOne, 1 }, std::tuple { layout.cardTwo, "SOURCE 2", linesTwo, 2 } })
    {
        const auto selected = facts.selected == source;
        const auto selectedText = i18n::tr ("Selected");
        if (selected && requiresJapaneseGlyphs (selectedText))
        {
            const auto prefix = i18n::tr (label) + " / ";
            const auto latin = imageFont (ImageText::small, 34, 0.14f);
            const auto japanese = nativeTextFontLike (latin).boldened();
            const auto prefixWidth = std::ceil (latin.getStringWidthFloat (prefix));
            const auto selectedWidth = std::ceil (japanese.getStringWidthFloat (selectedText));
            const auto x = card.getCentreX() - (prefixWidth + selectedWidth) * 0.5f;
            addLine (layout, prefix, { x, card.getY() + 14.0f, prefixWidth, 20.0f }, ImageText::small, secondary,
                     juce::Justification::centredLeft, 0.14f);
            addLine (layout, selectedText, { x + prefixWidth, card.getY() + 14.0f, selectedWidth, 20.0f }, ImageText::small,
                     secondary, juce::Justification::centredLeft, 0.0f, 34, true);
        }
        else
            addLine (layout, i18n::tr (label) + (selected ? juce::String (" / ") + selectedText : juce::String()),
                     { card.getX(), card.getY() + 14.0f, card.getWidth(), 20.0f }, ImageText::small, secondary,
                     juce::Justification::centred, 0.14f);
        const auto lineHeight = std::ceil (nameHeight * 1.18f);
        auto top = card.getY() + 36.0f + (card.getHeight() - 36.0f - 12.0f - lineHeight * (float) lines.size()) * 0.5f;
        for (const auto& line : lines)
        {
            addLine (layout, line, { card.getX() + 12.0f, top, inner, lineHeight }, ImageText::name, COL_NORMAL,
                     juce::Justification::centred, 0.0f, (int) nameHeight);
            top += lineHeight;
        }
    }
    y = layout.cardOne.getBottom() + 18.0f;

    if (facts.guess == 1 || facts.guess == 2)
    {
        const auto b = facts.nameB;
        addLine (layout, around ("Which one is %1?", b), { body.getX(), y, body.getWidth(), 24.0f }, ImageText::question, COL_NORMAL);
        y += 28.0f;
        const auto right = facts.guess == (facts.firstPre ? 2 : 1);
        const auto answer = i18n::tr (facts.guess == 1 ? "SOURCE 1" : "SOURCE 2");
        const auto outcome = i18n::tr (juce::String::fromUTF8 (right ? u8"— right" : u8"— not this time"));
        const auto answerFont = shownFont (answer, imageFont (ImageText::answer, 34, 0.10f));
        const auto outcomeFont = shownFont (outcome, imageFont (ImageText::outcome));
        const auto chipWidth = std::ceil (answerFont.getStringWidthFloat (answer)) + 26.0f;
        const auto outcomeWidth = std::ceil (outcomeFont.getStringWidthFloat (outcome)) + 4.0f;
        const auto total = chipWidth + 12.0f + outcomeWidth;
        layout.chip = { body.getCentreX() - total * 0.5f, y, chipWidth, 30.0f };
        addLine (layout, answer, layout.chip, ImageText::answer, COL_NORMAL, juce::Justification::centred, 0.10f);
        addLine (layout, outcome, { layout.chip.getRight() + 12.0f, y, outcomeWidth, 30.0f }, ImageText::outcome,
                 right ? COL_SPECTRUM_DELTA_BR : COL_OBSERVATORY_VALUE, juce::Justification::centredLeft);
        y += 30.0f + 12.0f;
    }

    juce::StringArray notes;
    if (facts.loweredDb > 0.05) notes.add (i18n::tr ("Lowered " + juce::String (facts.loweredDb, 1) + " dB to avoid clipping"));
    if (facts.note.isNotEmpty()) notes.add (noteWithNames (facts));
    const auto noteFont = imageFont (ImageText::small);
    for (const auto& note : notes)
        for (const auto& line : wrap (note, shownFont (note, noteFont), body.getWidth()))
        {
            addLine (layout, line, { body.getX(), y, body.getWidth(), 19.0f }, ImageText::small, secondary);
            y += 19.0f;
        }

    // The closing: one sentence, then the address, at the foot of the panel.
    const auto foot = layout.panel.getBottom() - 20.0f;
    layout.rule = foot - 62.0f;
    addLine (layout, i18n::tr (closingSentence), { body.getX(), foot - 50.0f, body.getWidth(), 24.0f }, ImageText::closing, COL_NORMAL);
    addLine (layout, address, { body.getX(), foot - 22.0f, body.getWidth(), 22.0f }, ImageText::address, COL_SPECTRUM_DELTA_BR,
             juce::Justification::centred, 0.06f);
    return layout;
}

// The image: the observatory's Jungle ground, a quiet recessed panel and two raised source plates
// under the shared key light, as the BLIND screen draws them.
inline void paint (juce::Graphics& g, const Facts& facts)
{
    const auto layout = layoutFor (facts);
    g.fillAll (BG);
    {
        // The understory keeps its figure in the upper right and its low ground at the foot.
        observatory_world::State world;
        world.density = observatory::Density::observatory;
        world.active = world.capture = world.jungle = true;
        const juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (0, 0, (int) logicalSize, (int) logicalSize);
        observatory_world::Backdrop().draw (g, { -270, 0, 810, 540 }, world);
    }
    g.setColour (BG);
    g.fillRoundedRectangle (layout.badge, 6.0f);
    surface_material::uncached::paintPanel (g, layout.panel, 0.93f, 10.0f, false);
    const auto light = key_light::overEditor (logicalSize, logicalSize);
    for (const auto& card : { layout.cardOne, layout.cardTwo, layout.chip })
        if (! card.isEmpty())
            surface_material::uncached::paintPanel (g, card, 0.96f, card == layout.chip ? 5.0f : 8.0f, true,
                                                    key_light::landingOn (card, light).fraction);
    g.setColour (surface_material::graphiteEdge().withAlpha (0.55f));
    g.fillRect (layout.panel.getX() + 26.0f, layout.rule, layout.panel.getWidth() - 52.0f, 1.0f);
    for (const auto& item : layout.items)
    {
        g.setColour (item.colour);
        g.setFont (labelFont (presentation::forOutput (540, 540, presentation::OutputTarget::capture),
                              item.role, item.nameSize).withExtraKerningFactor (item.kerning));
        text_style::drawText (g, item.text, item.area, item.justification, false,
                              item.nativeBold ? text_style::LabelPolicy::fixedNativeBold : text_style::LabelPolicy::fixed);
    }
}

inline juce::Image render (const Facts& facts, int pixels = pixelSize)
{
    juce::Image image (juce::Image::RGB, pixels, pixels, true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale ((float) pixels / logicalSize));
    paint (g, facts);
    return image;
}
}
