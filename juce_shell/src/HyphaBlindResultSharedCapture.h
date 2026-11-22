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
    // Square file format continues; its inner panel shares the RESULT component rather than
    // an alternative visual layout. The two export-only caption lines stay outside the panel.
    blind_ui::ScreenComponent surface ("result-png", blind_ui::ScreenComponent::NoteLanguage::prelocalized);
    surface.setBounds (0, 0, 600, 400);
    surface.setScreen (captureScreen (facts));
    if (auto* end = surface.findChildWithID ("result-png-end")) end->setVisible (false);
    const auto content = surface.createComponentSnapshot (surface.getLocalBounds(), true, 2.0f);
    juce::Image result (juce::Image::RGB, pixels, pixels, true);
    juce::Graphics g (result);
    g.addTransform (juce::AffineTransform::scale (static_cast<float> (pixels) / logicalSize));
    g.fillAll (BG);
    g.drawImage (content, juce::Rectangle<float> (12.0f, 62.0f, 516.0f, 344.0f));
    g.setColour (COL_NORMAL);
    g.setFont (labelFont (presentation::forOutput (540, 540, presentation::OutputTarget::capture), typography::BlindImageText::closing, 34));
    const auto caption = facts.preference.total() >= 2
        ? i18n::tr ("%1 blind rounds at matched level.").replace ("%1", juce::String (facts.preference.total()))
        : i18n::tr (closingSentence);
    text_style::drawText (g, caption, juce::Rectangle<int> (18, 423, 504, 32), juce::Justification::centred);
    g.setColour (COL_SPECTRUM_DELTA_BR);
    g.setFont (labelFont (presentation::forOutput (540, 540, presentation::OutputTarget::capture), typography::BlindImageText::address, 34));
    text_style::drawText (g, address, juce::Rectangle<int> (18, 459, 504, 27), juce::Justification::centred);
    return result;
}
}
