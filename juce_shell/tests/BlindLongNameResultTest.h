#pragma once
#include "../src/HyphaBlindScreen.h"
#include "../src/HyphaBlindPreferenceText.h"
#include "../src/HyphaCapturePngFile.h"
#include <vector>
#include <iostream>

namespace hypha::tests
{
template<typename Require>
void verifyBlindProbabilityResultRows (Require require)
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    for (const int width : { 300, 600, 1350 })
    for (const auto counts : { std::pair { 6u, 4u }, std::pair { 5u, 5u }, std::pair { 4u, 1u } })
    for (const bool withLoweringNote : { false, true })
    {
        i18n::ScopedLanguage lang (language);
        const bool japanese = language == i18n::Language::japanese;
        const auto a = japanese && counts.first == 6 ? 4u : counts.first;
        const auto b = japanese && counts.first == 6 ? 6u : counts.second;
        const blind_preference::Summary summary { a, b, blind_preference::probability (a, b) };
        blind_ui::Screen state;
        state.title = "BLIND RESULT"; state.sourcesShown = true;
        state.sourceOneEnabled = state.sourceTwoEnabled = true;
        state.saveImageShown = true; state.sourceOne = "1: PRE"; state.sourceTwo = "2: POST";
        state.preferenceResultText = blind_preference::summaryLine (summary, false);
        const auto chance = blind_preference::probabilityLine (summary);
        if (chance.isNotEmpty()) state.preferenceResultText += "\n" + chance;
        else require (counts.first == counts.second, "only the tied case has no probability line");
        state.preferenceResultNote = i18n::tr ("Sources revealed; keep comparing");
        const auto note = japanese ? juce::String::fromUTF8 (u8"終了すると通常の音量に戻ります")
                                   : juce::String ("Lowered 8.0 dB to avoid clipping; END restores it");
        if (withLoweringNote)
        {
            state.preferenceResultNote += "\n" + note;
            state.matchedGainText = japanese ? juce::String::fromUTF8 (u8"音量の補正：PRE −0.9 dB、POST 0.0 dB")
                                             : juce::String::fromUTF8 (u8"Matched level: PRE −0.9 dB; POST 0.0 dB");
        }
        blind_ui::ScreenComponent view ("preference-probability");
        view.setSize (width, width * 2 / 3);
        auto live = state;
        live.preferenceResultText.clear(); live.preferenceResultNote.clear(); live.matchedGainText.clear();
        live.guidanceShown = true;
        live.cause = i18n::tr ("Preparing the sources");
        live.recovery = i18n::tr ("Try both sources while playing");
        view.setScreen (live);
        // A surviving guidance belongs to measured notes, never stale centre bounds.
        if (withLoweringNote)
        {
            state.guidanceShown = live.guidanceShown;
            state.cause = live.cause;
            state.recovery = live.recovery;
        }
        view.setScreen (state);
        std::vector<juce::Component*> visible;
        int shownActions = 0;
        for (auto* child : view.getChildren())
        {
            if (! child->isVisible() || child->getBounds().isEmpty()) continue;
            visible.push_back (child);
            shownActions += dynamic_cast<juce::Button*> (child) != nullptr ? 1 : 0;
            require (view.getLocalBounds().contains (child->getBounds()), "visible result controls stay in the viewport");
            if (auto* text = dynamic_cast<juce::TextEditor*> (child))
            {
                require (text->getTextHeight() <= text->getHeight(), "the entire result row fits vertically");
                require (text->getTextWidth() <= text->getWidth(), "the entire result row fits horizontally");
            }
        }
        require (shownActions == 4, "source one, source two, save and end are available");
        for (size_t i = 0; i < visible.size(); ++i)
            for (size_t j = i + 1; j < visible.size(); ++j)
                require (! visible[i]->getBounds().intersects (visible[j]->getBounds()), "no visible result lines or actions intersect");
        auto* result = dynamic_cast<juce::TextEditor*> (view.findChildWithID ("preference-probability-preference-result"));
        auto* notes = dynamic_cast<juce::TextEditor*> (view.findChildWithID ("preference-probability-preference-note"));
        require (result && result->getText() == state.preferenceResultText, "full unchanged summary and probability");
        require (notes && (! withLoweringNote || (notes->getText().contains (note)
                    && notes->getText().contains (state.recovery))), "level and continuing guidance are not lost");
    }
}

template<typename Require>
void verifyBlindLongNameResult (Require require, const juce::String& preview)
{
    verifyBlindProbabilityResultRows (require);
    int cases = 0;
    for (const bool longName : { false, true })
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    for (const int rounds : { 1, 5, 10 }) for (const int width : { 300, 450, 600, 900 })
    {
        i18n::ScopedLanguage lang (language);
        const bool japanese = language == i18n::Language::japanese;
        const auto a = !longName ? juce::String ("A") : japanese ? juce::String::fromUTF8 (u8"録音編集確認比較音源長名十六文字") : juce::String ("WWWWMMMMWWWWMMMM");
        const auto b = !longName ? juce::String ("B") : japanese ? juce::String::fromUTF8 (u8"調整後確認比較音源長名十六文字名") : juce::String ("MMMMWWWWMMMMWWWW");
        blind_preference::Summary summary { static_cast<unsigned>(rounds), 0, blind_preference::probability (rounds, 0) };
        blind_ui::Screen state;
        state.title = "BLIND RESULT"; state.sourcesShown = true;
        state.sourceOneEnabled = state.sourceTwoEnabled = true;
        state.saveImageShown = true; state.sourceOne = "SOURCE 1"; state.sourceTwo = "SOURCE 2";
        state.preferenceResultText = blind_preference::summaryLine (summary, true, a, b, true);
        const auto chance = blind_preference::probabilityLine (summary);
        if (chance.isNotEmpty()) state.preferenceResultText += "\n" + chance;
        state.matchedGainText = japanese ? juce::String::fromUTF8 (u8"音量の補正：−12.34 dB") : juce::String ("Matched gain: -12.34 dB");
        state.preferenceResultNote = japanese ? juce::String::fromUTF8 (u8"好みの記録です。音質の優劣を示しません。") : juce::String ("A record of preference, not a verdict on sound quality.");
        blind_ui::ScreenComponent view ("long-name");
        view.setSize (width, width * 2 / 3); view.setScreen (state);
        const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
        if (preview.isNotEmpty())
        {
            const auto file = juce::File (preview).getChildFile (juce::String(longName ? "long-name-" : "short-name-") + juce::String(japanese?"ja-":"en-")
                + juce::String(rounds) + "-" + juce::String(width) + ".png");
            if (!file.exists()) require (capture::writePng (image, file) == capture::PngSave::saved, "long name diagnostic image saved");
        }
        std::vector<juce::Component*> bodies;
        for (auto* child : view.getChildren())
            if (auto* text = dynamic_cast<juce::TextEditor*> (child); text && text->isVisible())
            {
                require (text->getFont().getHeight() >= 12.0f, "long name text remains at least 12px");
                require (view.getLocalBounds().contains (text->getBounds()), "long name body inside screen");
                require (text->getTextHeight() <= text->getHeight(), "long name body has no vertical overflow");
                bodies.push_back (text);
            }
        require (bodies.size() == 3, "result probability, gain and note bodies all visible");
        for (std::size_t i=0; i<bodies.size(); ++i) for(std::size_t j=i+1; j<bodies.size(); ++j)
            require (!bodies[i]->getBounds().intersects (bodies[j]->getBounds()), "long result gain and note never overlap");
        const juce::Component* heading = nullptr;
        const juce::Component* source = nullptr;
        const juce::Component* finish = nullptr;
        for (auto* child : view.getChildren())
        {
            if (auto* label = dynamic_cast<juce::Label*> (child); label && label->isVisible()
                && label->getText() == "BLIND RESULT") heading = label;
            if (auto* button = dynamic_cast<juce::Button*> (child); button && button->isVisible())
            {
                if (button->getButtonText() == "SOURCE 1") source = button;
                if (button->getButtonText() == "END") finish = button;
            }
        }
        require (heading && heading->getY() == 24, "normal and dense retain title top inset");
        if (!japanese && !longName && rounds == 1 && width == 450)
            require (source && finish && source->getY() < finish->getY(), "short 450 result keeps normal two-row actions");
        // Children are result, gain, note in declaration order; identify note
        // by its bottom position so ordering cannot hide a failed layout.
        const auto bottom = *std::max_element (bodies.begin(), bodies.end(), [] (auto* a, auto* b) { return a->getY() < b->getY(); });
        juce::Rectangle<int> content;
        for (auto* body : bodies) if (body != bottom) content = content.getUnion (body->getBounds());
        require (heading && content.getY() >= heading->getBottom(), "result group stays below title");
        require (std::abs (2 * content.getCentreY() - heading->getBottom() - bottom->getY()) <= 2,
                 "result group is centred between title and note");
        ++cases;
    }
    std::cout << "Blind long-name result: " << cases << " cases PASS\n";
}
}
