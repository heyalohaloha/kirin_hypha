#pragma once
#include "ReferenceAWindowTest.h"
#include "EditorReachabilityCheck.h"
#include "../src/HyphaReferenceOverlay.h"
#include "../src/HyphaReferenceTargetView.h"

#include <array>
#include <iostream>
#include <utility>

// B の画面の Balance の重ね比べ（ONE／ALL）：ONE は今のまま、ALL は A と番号で出した線。線の色は出した時に決まり
// 入れ替わらない。目標はセットの曲の後に入れた順で並び、押しても鳴らずに理由を言う。16 曲と目標 16 で描け、
// どの大きさでも操作に届く。
namespace hypha::tests::reference_overlay
{
using reference_guide_contract::require;
namespace ref = reference_audition;

inline ref::MeasuredTarget target (int index)
{
    ref::MeasuredTarget result;
    result.id = "target-" + juce::String::toHexString (0x100 + index);
    result.name = "Drop " + juce::String (index + 1);
    result.spectrum = *reference_a_window::cPage().cueKirin;
    for (auto& level : result.spectrum.medianDb) level -= static_cast<float> (index);
    for (auto& level : result.spectrum.p10Db) level -= static_cast<float> (index) + 1.0f;
    result.duration = 30.0; result.integrated = -18.0 - index * 0.5; result.maximumTruePeak = -1.0;
    require (result.valid(), "target fixture");
    return result;
}

// The B page with `songs` songs of the set and `targets` dropped targets.
inline reference_ui::State page (int songs, int targets)
{
    auto state = reference_a_window::bPage();
    const auto fact = state.songFacts.front();
    state.songs.clear(); state.songFacts.clear();
    for (int index = 0; index < songs; ++index)
    {
        state.songs.push_back ({ "set/check/song-" + juce::String (index + 1), "Song " + juce::String (index + 1) });
        auto copy = fact;
        for (auto& level : copy.medianDb) level += static_cast<float> (index % 5) - 2.0f;
        copy.lufsI = -20.0 - index * 0.25;
        state.songFacts.push_back (copy);
    }
    state.songId = state.songs.front().id;
    state.targetsEnabled = true;
    for (int index = 0; index < targets; ++index) state.targets.collection.items.push_back (target (index));
    state.visualPreferences = std::make_shared<ref::VisualPreferences>();
    return state;
}

inline reference_ui::SongList& listOf (reference_ui::Component& panel)
{
    auto* list = dynamic_cast<reference_ui::SongList*> (panel.findChildWithID ("reference-song-list"));
    require (list != nullptr && list->isVisible(), "the B list is shown at 300%");
    return *list;
}

inline std::vector<reference_ui::NumberToggle*> boxesOf (reference_ui::SongList& list)
{
    std::vector<reference_ui::NumberToggle*> boxes;
    for (auto* child : list.getChildren())
        if (auto* box = dynamic_cast<reference_ui::NumberToggle*> (child); box != nullptr && box->isVisible()) boxes.push_back (box);
    return boxes;
}

inline juce::Button& switchHalf (reference_ui::Component& panel, const char* id)
{
    auto* overlay = panel.findChildWithID ("reference-overlay");
    auto* half = dynamic_cast<juce::Button*> (overlay->findChildWithID (id));
    require (half != nullptr && half->isVisible() && overlay->isVisible() && ! overlay->getBounds().isEmpty()
             && panel.getLocalBounds().contains (overlay->getBounds()), juce::String ("the switch shows ") + id);
    return *half;
}

inline void press (reference_ui::SongList& list, juce::Point<float> at)
{
    list.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f,
                                      0.0f, 0.0f, &list, &list, juce::Time(), at, juce::Time(), 1, false));
}

inline void verifySwitchAndColours()
{
    reference_ui::Component panel; reference_text_repairs::sizePanel (panel, 900, 600);
    auto state = page (4, 2);
    panel.setState (state);
    auto& list = listOf (panel);
    require (list.rows().size() == 4 && boxesOf (list).empty() && ! panel.state().overlay.all,
             "ONE lists the set's songs only, numbered as before, with no boxes");
    const auto one = a_direction::drawnTexts (panel);
    reference_text_repairs::requireWhole (one, "TARGET RANGE");
    reference_text_repairs::requireWhole (one, "B CHORUS 1:02-1:24");
    switchHalf (panel, "reference-overlay-all").onClick();
    const auto overlay = panel.state().overlay;
    require (overlay.all && overlay.lines.size() == 4 && state.visualPreferences->overlay() == overlay,
             "the first ALL shows the set's songs and keeps the choice where the project saves it");
    for (int index = 0; index < 4; ++index)
        require (overlay.find (state.songs[static_cast<size_t> (index)].id)->colour == index, "songs take colours in list order");
    require (list.rows().size() == 6 && list.rows()[4].target && list.rows()[5].target && list.rows()[4].title == "Drop 1"
             && ! list.rows()[4].shown && boxesOf (list).size() == 6, "ALL lists the targets after the songs, in the order added");
    auto boxes = boxesOf (list);
    boxes[1]->onClick();
    boxes = boxesOf (list);
    boxes[5]->onClick();
    const auto after = panel.state().overlay;
    require (after.find (state.songs[1].id) == nullptr && after.find (state.songs[0].id)->colour == 0
             && after.find (state.songs[2].id)->colour == 2 && after.find (state.songs[3].id)->colour == 3,
             "hiding a song keeps every other colour");
    require (after.find (state.targets.collection.items[1].id)->colour == 1 && list.rows()[5].shown && list.rows()[5].colour == 1,
             "a target shown next takes the free colour, and its box shows it");
    // A target row is not heard: the press says why and changes nothing.
    juce::String explained, chosen;
    panel.onExplain = [&] (const juce::String& text) { explained = text; };
    panel.onSelectSong = [&] (const juce::String& id) { chosen = id; };
    const auto rowY = [&] (int row) { return 6.0f + 18.0f + 34.0f + static_cast<float> (row - list.firstShownRow()) * 34.0f + 17.0f; };
    press (list, { 120.0f, rowY (4) });
    require (explained == reference_ui::targetSilentText && chosen.isEmpty() && panel.state().songId == state.songId,
             "pressing a target row to hear it says it has no audio and keeps B's song");
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        require (i18n::hasTranslation (reference_ui::targetSilentText) && i18n::hasTranslation (reference_ui::help_text::overlayNumber)
                 && i18n::hasTranslation (reference_ui::overlayEmptyText), "the overlay's words read in Japanese");
    }
    press (list, { 120.0f, rowY (2) });
    require (chosen == state.songs[2].id, "a song row still chooses the song in ALL");
    press (list, { 20.0f, rowY (2) });
    require (panel.state().overlay.find (state.songs[2].id) == nullptr, "in ALL the number hides the line instead of choosing");
    // Pointing a row names the line Balance strengthens; the chart then draws differently.
    const auto before = panel.createComponentSnapshot (panel.getLocalBounds());
    list.mouseMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { 120.0f, rowY (0) }, juce::ModifierKeys(),
                                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &list, &list, juce::Time(), { 120.0f, rowY (0) }, juce::Time(), 0, false));
    require (panel.state().overlayPointed == state.songs[0].id, "the pointed row is the strengthened line");
    const auto pointed = panel.createComponentSnapshot (panel.getLocalBounds());
    int changed = 0;
    for (int y = 0; y < before.getHeight(); y += 2)
        for (int x = before.getWidth() / 2; x < before.getWidth(); x += 2) changed += before.getPixelAt (x, y) != pointed.getPixelAt (x, y) ? 1 : 0;
    require (changed > 50, "the pointed line is drawn stronger than the others");
    switchHalf (panel, "reference-overlay-one").onClick();
    require (! panel.state().overlay.all && panel.state().overlay.lines.size() == after.lines.size() - 1 && list.rows().size() == 4,
             "ONE returns to today's list and chart and keeps the lines for the next ALL");
    reference_text_repairs::requireWhole (a_direction::drawnTexts (panel), "TARGET RANGE");
    // Help in the line at 300%: the switch and, in ALL, a number box say what they do.
    require (boxesOf (list).empty(), "ONE has no boxes");
    const auto& oneHalf = switchHalf (panel, "reference-overlay-one");
    require (panel.helpAt (panel.getLocalPoint (&oneHalf, oneHalf.getLocalBounds().getCentre())) == reference_ui::help_text::overlay,
             "the switch says what ONE and ALL show");
    switchHalf (panel, "reference-overlay-all").onClick();
    const auto* firstBox = boxesOf (list).front();
    require (panel.helpAt (panel.getLocalPoint (firstBox, firstBox->getLocalBounds().getCentre())) == reference_ui::help_text::overlayNumber,
             "a number box says it shows or hides the line and keeps B's song");
}

// 16 songs and 16 targets, every line shown: drawn in both languages at every 300% size, the list scrolls
// to every row, and every control is reachable at every display scale.
inline void verifyFullSet()
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const auto& size : reference_text_repairs::inspectionSizes())
        {
            reference_ui::Component panel; reference_text_repairs::sizePanel (panel, size.width, size.height);
            auto state = page (16, 16);
            state.overlay.all = true;
            for (const auto& item : reference_ui::overlayItems (state)) state.overlay.lines.push_back ({ item.id, state.overlay.freeColour() });
            require (state.overlay.lines.size() == 32, "sixteen songs and sixteen targets can all be shown");
            panel.setState (state);
            auto& list = listOf (panel);
            require (list.rows().size() == 32 && list.shownRowCount() < 32, "the list holds every row and scrolls");
            const auto drawn = a_direction::drawnTexts (panel);
            reference_text_repairs::requireWhole (drawn, "BALANCE");
            bool more = false;
            for (const auto& text : drawn) more = more || (text.startsWith ("+") && text.substring (1).containsOnly ("0123456789"));
            require (more, "a legend that cannot hold every line ends with how many more");
            for (int first = 0; first < 32; first += list.shownRowCount())
            {
                list.scrollTo (first);
                const auto boxes = boxesOf (list);
                require (! boxes.empty() && boxes.front()->getName() == juce::String (list.firstShownRow() + 1),
                         "every row's number box is reached by scrolling");
            }
            state.songId = state.songs.back().id; panel.setState (state);
            require (list.firstShownRow() + list.shownRowCount() > 15, "choosing a song scrolls its row into view");
            const auto preview = juce::SystemStats::getEnvironmentVariable ("KIRIN_OVERLAY_PREVIEW", {});
            if (preview.isNotEmpty() && size.width == 900)
            {
                list.scrollTo (16);
                list.mouseMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { 120.0f, 100.0f }, juce::ModifierKeys(),
                    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &list, &list, juce::Time(), { 120.0f, 100.0f }, juce::Time(), 0, false));
                auto full = juce::File (preview).getChildFile (juce::String ("overlay-full-")
                    + (language == i18n::Language::japanese ? "ja.png" : "en.png")).createOutputStream();
                require (full && juce::PNGImageFormat().writeImageToStream (panel.createComponentSnapshot (panel.getLocalBounds()), *full),
                         "full overlay preview");
                state.songId = state.songs.front().id;
                state.overlay.lines.resize (4);
                panel.setState (state); list.scrollTo (0);
                const auto image = panel.createComponentSnapshot (panel.getLocalBounds());
                auto stream = juce::File (preview).getChildFile (juce::String ("overlay-")
                    + (language == i18n::Language::japanese ? "ja.png" : "en.png")).createOutputStream();
                require (stream && juce::PNGImageFormat().writeImageToStream (image, *stream), "overlay preview");
            }
        }
    }
    // ALL with no line says how to show one, in the screen's language.
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        reference_ui::Component panel; reference_text_repairs::sizePanel (panel, 900, 600);
        auto state = page (3, 0);
        state.overlay.all = true;
        panel.setState (state);
        reference_text_repairs::requireWhole (a_direction::drawnTexts (panel), reference_ui::overlayEmptyText);
    }
    // Every editor size, walked down from 300% and back as the editor resizes: below 300% there is no Balance,
    // so no switch and no boxes stay behind, and what shows is reachable at every display scale.
    constexpr std::array<std::pair<int, int>, 8> sizes {{
        { 900, 600 }, { 897, 598 }, { 750, 500 }, { 600, 400 }, { 450, 300 }, { 375, 250 }, { 300, 200 }, { 900, 600 } }};
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        reference_ui::Component panel;
        panel.setVisible (true);
        auto state = page (16, 16);
        state.overlay.all = true;
        state.overlay.lines = { { state.songs[2].id, 0 }, { state.targets.collection.items[3].id, 1 } };
        panel.setState (state);
        for (const auto& [width, height] : sizes)
        {
            observatory::View shell (observatory::Role::post);
            shell.setSize (width, height); shell.setDomain (observatory::Domain::reference); shell.setExternalAnalysisBodyActive (true);
            state.blindLargeScreen = width >= 900 && height >= 600;  // as the editor sets it
            panel.setState (state);
            panel.setPresentationContext (presentation::forEditor (width, height));
            panel.setSize (shell.analysisBodyBounds().getWidth(), shell.analysisBodyBounds().getHeight());
            const bool large = state.blindLargeScreen && presentation::forEditor (width, height).density == observatory::Density::inspection;
            require (panel.findChildWithID ("reference-overlay")->isVisible() == large
                     && (large || boxesOf (*dynamic_cast<reference_ui::SongList*> (panel.findChildWithID ("reference-song-list"))).empty()
                         || ! panel.findChildWithID ("reference-song-list")->isVisible()),
                     "ONE / ALL and the number boxes show exactly where Balance does (" + juce::String (width) + ")");
            editor_reachability::checkAtDisplayScales (panel, [&] (float scale, const juce::String& problem)
            {
                std::cerr << "REF B ALL " << width << "x" << height << " @" << scale << ": " << problem << '\n';
                require (false, "every overlay control is inside the page and reachable at every size");
            });
        }
    }
}

// The line colours: every number reads on the dark ground (4.5:1), none is orange or A's gold, and the
// colours given first are far apart (a few lines are the common case).
inline void verifyPalette()
{
    const auto luminance = [] (juce::Colour colour)
    {
        const auto channel = [] (float value) { return value <= 0.03928f ? value / 12.92f : std::pow ((value + 0.055f) / 1.055f, 2.4f); };
        return 0.2126f * channel (colour.getFloatRed()) + 0.7152f * channel (colour.getFloatGreen()) + 0.0722f * channel (colour.getFloatBlue());
    };
    const auto ground = luminance (BG);
    for (int index = 0; index < 32; ++index)
    {
        const auto colour = reference_ui::overlayTextColour (index);
        require ((luminance (colour) + 0.05f) / (ground + 0.05f) >= 4.5f, "every line's number reads on the ground at 4.5:1");
        const auto hue = colour.getHue() * 360.0f;
        require (colour.getSaturation() < 0.2f || hue < 12.0f || hue > 65.0f, "no line is orange, yellow or A's gold");
    }
    for (int a = 0; a < 8; ++a)
        for (int b = a + 1; b < 8; ++b)
        {
            const auto one = reference_ui::overlayLineColour (a), two = reference_ui::overlayLineColour (b);
            const auto distance = std::hypot (one.getFloatRed() - two.getFloatRed(), one.getFloatGreen() - two.getFloatGreen(),
                                              one.getFloatBlue() - two.getFloatBlue());
            require (distance > 0.12f, "the first eight line colours are far apart");
        }
    require (reference_ui::overlayLineColour (16) != reference_ui::overlayLineColour (0), "the seventeenth line is told apart from the first");
}

inline void verify()
{
    verifyPalette();
    verifySwitchAndColours();
    verifyFullSet();
    std::cout << "Reference Balance overlay (ONE / ALL, lines, targets, 16 + 16): PASS\n";
}
}
