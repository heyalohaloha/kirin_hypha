#pragma once

#include "HyphaBlindResultImage.h"
#include "HyphaBlindScreen.h"

namespace hypha::blind_image
{
// Capture only the frozen public model. Do not capture a hosted Component, or pass session,
// song, filepath, candidate or track names through to this export.
inline blind_ui::Screen captureScreen (const Facts& facts)
{
    blind_ui::Screen screen;
    screen.title = "BLIND RESULT";
    screen.sourcesShown = screen.sourceOneEnabled = screen.sourceTwoEnabled = true;
    screen.endEnabled = false;  // a picture has nothing to press
    screen.audible = facts.selected;
    const auto nameA = cleanName (facts.nameA, defaultName (facts.candidate, false));
    const auto nameB = cleanName (facts.nameB, defaultName (facts.candidate, true));
    screen.sourceOne = "1: " + (facts.firstPre ? nameA : nameB);
    screen.sourceTwo = "2: " + (facts.firstPre ? nameB : nameA);
    if (facts.preference.total() > 0)
    {
        screen.preferenceResultText = blind_preference::summaryLine (facts.preference, facts.candidate, nameA, nameB, true);
        const auto chance = blind_preference::probabilityLine (facts.preference);
        if (chance.isNotEmpty()) screen.preferenceResultText += "\n" + chance;
    }
    else if (facts.guess == 1 || facts.guess == 2)
    {
        screen.guessShown = true;
        screen.guessAnswer = facts.guess;
        screen.guessRight = facts.guess == (facts.firstPre ? 2 : 1);
        screen.guessQuestion = detail::around ("Which one is %1?", nameB);
        screen.guessOutcome = i18n::tr (juce::String::fromUTF8 (screen.guessRight ? u8"— right" : u8"— not this time"));
    }
    auto cleaned = facts;
    cleaned.nameA = nameA;
    cleaned.nameB = nameB;
    juce::StringArray notes;
    if (facts.loweredDb > 0.05) notes.add (i18n::tr ("Lowered " + juce::String (facts.loweredDb, 1) + " dB to avoid clipping"));
    if (facts.note.isNotEmpty()) notes.add (detail::noteWithNames (cleaned));
    if (screen.preferenceResultText.isNotEmpty())
        screen.preferenceResultNote = notes.joinIntoString ("\n");
    else if (notes.size() > 0)
    {
        screen.instruction = notes[0];
        if (notes.size() > 1) screen.detail = notes[1];
    }
    return screen;
}

inline juce::Image renderShared (const Facts& facts, int pixels = pixelSize)
{
    // The square file keeps the product's name, the mode and the observatory ground around the panel. The panel
    // itself is the RESULT component of the screen, so the two never tell the result differently.
    blind_ui::ScreenComponent surface ("result-png", blind_ui::ScreenComponent::NoteLanguage::prelocalized);
    surface.setBounds (0, 0, 600, 400);
    surface.setScreen (captureScreen (facts));
    if (auto* end = surface.findChildWithID ("result-png-end")) end->setVisible (false);
    const auto content = surface.createComponentSnapshot (surface.getLocalBounds(), true, 2.0f);
    juce::Image result (juce::Image::RGB, pixels, pixels, true);
    juce::Graphics g (result);
    g.addTransform (juce::AffineTransform::scale (static_cast<float> (pixels) / logicalSize));
    const auto words = layoutFor (facts);
    paintGround (g, words);
    paintItem (g, words.items[0]);  // KIRIN HYPHA
    paintItem (g, words.items[1]);  // A/B BLIND or PRE/POST BLIND
    {
        // The component's own frame starts 10 px inside it; only that panel is placed on the ground.
        constexpr float shown = 472.0f / 580.0f;
        const juce::Rectangle<float> panel (34.0f, 72.0f, 472.0f, 380.0f * shown);
        const juce::Graphics::ScopedSaveState saved (g);
        juce::Path edge;
        edge.addRoundedRectangle (panel, 7.0f * shown);
        g.reduceClipRegion (edge);
        g.drawImage (content, { panel.getX() - 10.0f * shown, panel.getY() - 10.0f * shown, 600.0f * shown, 400.0f * shown });
    }
    for (const auto offset : { size_t { 2 }, size_t { 1 } })
    {
        auto item = words.items[words.items.size() - offset];  // the closing sentence, then the address
        item.area = item.area.withY (offset == 2 ? 412.0f : 442.0f);
        paintItem (g, item);
    }
    return result;
}
}
