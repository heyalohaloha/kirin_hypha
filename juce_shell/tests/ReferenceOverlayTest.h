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

// A preview file written anew (a FileOutputStream appends to a file that is already there).
inline std::unique_ptr<juce::FileOutputStream> fresh (const juce::File& file)
{
    file.deleteFile();
    return file.createOutputStream();
}

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
    state.songId = state.songs.empty() ? juce::String() : state.songs.front().id;
    state.targetsEnabled = true;
    for (int index = 0; index < targets; ++index) state.targets.collection.items.push_back (target (index));
    state.visualPreferences = std::make_shared<ref::VisualPreferences>();
    return state;
}

// What the page asks the processor to save (and to tell the host about): each overlay change, once.
struct Saved
{
    ref::BalanceOverlay overlay;
    int count = 0;
    void track (reference_ui::Component& panel) { panel.onOverlayChange = [this] (const auto& next) { overlay = next; ++count; }; }
};

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
    Saved saved; saved.track (panel);
    switchHalf (panel, "reference-overlay-all").onClick();
    const auto overlay = panel.state().overlay;
    require (overlay.all && overlay.lines.size() == 4 && saved.overlay == overlay && saved.count == 1,
             "the first ALL shows the set's songs and asks the processor to save it (the host is told)");
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
                auto full = fresh (juce::File (preview).getChildFile (juce::String ("overlay-full-")
                    + (language == i18n::Language::japanese ? "ja.png" : "en.png")));
                require (full && juce::PNGImageFormat().writeImageToStream (panel.createComponentSnapshot (panel.getLocalBounds()), *full),
                         "full overlay preview");
                state.songId = state.songs.front().id;
                state.overlay.lines.resize (4);
                panel.setState (state); list.scrollTo (0);
                const auto image = panel.createComponentSnapshot (panel.getLocalBounds());
                auto stream = fresh (juce::File (preview).getChildFile (juce::String ("overlay-")
                    + (language == i18n::Language::japanese ? "ja.png" : "en.png")));
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
        state.overlay.drawing = language == i18n::Language::japanese ? ref::BalanceOverlay::Drawing::allShape
                                                                     : ref::BalanceOverlay::Drawing::shape;
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
    require ((luminance (COL_TEXT_SECONDARY) + 0.05f) / (ground + 0.05f) >= 4.5f, "a number not shown, or dimmed in ALL SHAPE, reads at 4.5:1");
    // What a press explains fits the footer's status at 300% in both languages.
    verifyReferenceHelpFits ({ reference_ui::targetSilentText, reference_ui::allShapeFixedText });
}

// SHAPE: the mean and the band from the lowest to the highest of the lines turned on, at B's level.
inline void verifyShapeValues()
{
    std::vector<double> centers (64);
    for (size_t band = 0; band < centers.size(); ++band) centers[band] = 20.0 * std::pow (1000.0, double (band) / 63.0);
    const std::vector<float> low (64, -40.0f), mid (64, -30.0f), other (64, -26.0f);
    auto shifted = centers; shifted.back() *= 1.5;  // another band layout is not mixed in
    const ref::BalanceOverlay::Line on { "x", 0 };
    const auto item = [&] (const std::vector<float>& levels, double gain, const std::vector<double>* bands = nullptr)
    {
        reference_ui::OverlayItem result;
        result.centers = bands != nullptr ? bands : &centers; result.medians = &levels; result.gainDb = gain; result.line = &on;
        return result;
    };
    const auto three = reference_ui::overlayShape ({ item (low, 2.0), item (mid, -4.0), item (other, std::numeric_limits<double>::quiet_NaN()),
                                                     item (mid, 0.0, &shifted) });
    require (three.count == 3 && three.unmatched && std::abs (three.low[10] + 38.0f) < 1.0e-4f && std::abs (three.high[10] + 26.0f) < 1.0e-4f
             && std::abs (three.mean[10] - (-38.0f - 34.0f - 26.0f) / 3.0f) < 1.0e-4f,
             "the mean and range follow each line at its level; a line at another band layout is left out");
    const auto one = reference_ui::overlayShape ({ item (mid, 1.0) });
    require (one.count == 1 && one.low == one.high && one.mean == one.high && std::abs (one.mean[0] + 29.0f) < 1.0e-4f,
             "with one line the band closes onto that line");
    auto hidden = item (mid, 0.0); hidden.line = nullptr;
    require (reference_ui::overlayShape ({ hidden }).count == 0, "a line turned off is not in the shape");
    require (reference_ui::overlayShape ({ hidden, item (low, 0.0) }, true).count == 2, "ALL SHAPE takes every item, on or off");
}

inline juce::Button& drawingPart (reference_ui::Component& panel, const char* id)
{
    auto* part = dynamic_cast<juce::Button*> (panel.findChildWithID ("reference-overlay")->findChildWithID (id));
    require (part != nullptr, juce::String ("the drawing switch has ") + id);
    return *part;
}

// In ALL, LINES / SHAPE / ALL SHAPE beside ONE / ALL: one is on and it shows which.
inline void verifyShape()
{
    verifyShapeValues();
    using Drawing = ref::BalanceOverlay::Drawing;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        reference_ui::Component panel; reference_text_repairs::sizePanel (panel, 900, 600);
        auto state = page (4, 2);
        panel.setState (state);
        auto& lines = drawingPart (panel, "reference-overlay-lines");
        auto& shape = drawingPart (panel, "reference-overlay-shape");
        auto& every = drawingPart (panel, "reference-overlay-all-shape");
        require (! lines.isVisible() && ! shape.isVisible() && ! every.isVisible(), "ONE has no drawing switch");
        Saved saved; saved.track (panel);
        switchHalf (panel, "reference-overlay-all").onClick();
        for (auto* part : { &lines, &shape, &every })
            require (part->isVisible() && panel.getLocalBounds().contains (panel.getLocalArea (part, part->getLocalBounds()))
                     && panel.helpAt (panel.getLocalPoint (part, part->getLocalBounds().getCentre())) == reference_ui::help_text::drawing,
                     "ALL shows LINES / SHAPE / ALL SHAPE in the page, each saying what it draws");
        require (lines.getToggleState() && ! shape.getToggleState() && ! every.getToggleState(), "ALL starts on LINES");
        reference_text_repairs::requireWhole (a_direction::drawnTexts (panel), "1 Song 1");
        shape.onClick();
        require (panel.state().overlay.drawing == Drawing::shape && saved.overlay.drawing == Drawing::shape
                 && shape.getToggleState() && ! lines.getToggleState(), "SHAPE is on, shown on the switch and kept where the project saves it");
        auto drawn = a_direction::drawnTexts (panel);
        reference_text_repairs::requireWhole (drawn, "MEAN OF 4");
        reference_text_repairs::requireWhole (drawn, "RANGE OF 4");
        reference_text_repairs::requireWhole (drawn, "20 Hz - 20 kHz");
        require (! drawn.contains (text_style::shownText ("1 Song 1")), "SHAPE draws no line of its own per song");
        auto& list = listOf (panel);
        const auto rowY = [&] (int row) { return 6.0f + 18.0f + 34.0f + static_cast<float> (row - list.firstShownRow()) * 34.0f + 17.0f; };
        press (list, { 20.0f, rowY (1) });
        press (list, { 20.0f, rowY (4) });  // a target joins the shape the same way
        reference_text_repairs::requireWhole (a_direction::drawnTexts (panel), "MEAN OF 4");
        require (panel.state().overlay.find (state.targets.collection.items[0].id) != nullptr, "the numbers still choose what goes in");
        // ALL SHAPE: every song and target, whatever the numbers say.
        every.onClick();
        drawn = a_direction::drawnTexts (panel);
        reference_text_repairs::requireWhole (drawn, "MEAN OF ALL 6");
        reference_text_repairs::requireWhole (drawn, "RANGE OF ALL 6");
        // The numbers are dimmed and a press says why it changes nothing; the choice of SHAPE and LINES is kept.
        const auto kept = panel.state().overlay.lines;
        juce::String explained;
        panel.onExplain = [&] (const juce::String& text) { explained = text; };
        press (list, { 20.0f, rowY (0) });
        auto boxes = boxesOf (list);
        require (explained == reference_ui::allShapeFixedText && panel.state().overlay.lines == kept,
                 "a number pressed in ALL SHAPE says ALL SHAPE uses every song and keeps what SHAPE uses");
        explained.clear();
        boxes[2]->onClick();
        require (explained == reference_ui::allShapeFixedText && panel.state().overlay.lines == kept && boxes[2]->getTooltip() == explained,
                 "a dimmed box says the same in its help");
        reference_text_repairs::requireWhole (a_direction::drawnTexts (panel), "MEAN OF ALL 6");
        require (panel.state().overlay.drawing == Drawing::allShape && every.getToggleState(), "ALL SHAPE stays on and counts every item");
        {
            juce::Image image (juce::Image::ARGB, boxes[0]->getWidth(), boxes[0]->getHeight(), true);
            juce::Graphics graphics (image); boxes[0]->paintEntireComponent (graphics, true);
            shape.onClick();
            const auto lit = boxes[0]->createComponentSnapshot (boxes[0]->getLocalBounds());
            bool differs = false;
            for (int y = 0; y < image.getHeight(); ++y)
                for (int x = 0; x < image.getWidth(); ++x) differs = differs || image.getPixelAt (x, y) != lit.getPixelAt (x, y);
            require (differs && boxes[0]->getTooltip() == reference_ui::help_text::overlayNumber, "the boxes are lit again in SHAPE");
            reference_text_repairs::requireWhole (a_direction::drawnTexts (panel), "MEAN OF 4");
            every.onClick();
        }
        // Pointing at a row draws that one line on top, thin, while pointed.
        const auto before = panel.createComponentSnapshot (panel.getLocalBounds());
        list.mouseMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { 120.0f, rowY (1) }, juce::ModifierKeys(),
                                          0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &list, &list, juce::Time(), { 120.0f, rowY (1) }, juce::Time(), 0, false));
        const auto pointed = panel.createComponentSnapshot (panel.getLocalBounds());
        int changed = 0;
        for (int y = 0; y < before.getHeight(); y += 2)
            for (int x = before.getWidth() / 2; x < before.getWidth(); x += 2) changed += before.getPixelAt (x, y) != pointed.getPixelAt (x, y) ? 1 : 0;
        require (changed > 30, "the pointed row's line is drawn over the shape");
        // The level warning is read whole beside the switches.
        auto unmatched = panel.state(); unmatched.aWindowLoudness = std::numeric_limits<double>::quiet_NaN();
        panel.setState (unmatched);
        reference_text_repairs::requireWhole (a_direction::drawnTexts (panel), "LEVEL NOT MATCHED");
        unmatched.aWindowLoudness = state.aWindowLoudness;
        panel.setState (unmatched);
        if (const auto preview = juce::SystemStats::getEnvironmentVariable ("KIRIN_OVERLAY_PREVIEW", {}); preview.isNotEmpty())
        {
            juce::Image image (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
            juce::Graphics graphics (image);
            graphics.fillAll (BG);  // the page's dark ground, as the editor draws it
            panel.paintEntireComponent (graphics, true);
            auto stream = fresh (juce::File (preview).getChildFile (juce::String ("all-shape-")
                + (language == i18n::Language::japanese ? "ja.png" : "en.png")));
            require (stream && juce::PNGImageFormat().writeImageToStream (image, *stream), "ALL SHAPE preview");
        }
        // SHAPE with nothing turned on says why.
        shape.onClick();
        auto none = panel.state(); none.overlay.lines.clear();
        panel.setState (none);
        drawn = a_direction::drawnTexts (panel);
        reference_text_repairs::requireWhole (drawn, reference_ui::shapeEmptyText);
        require (! drawn.contains (text_style::shownText ("MEAN OF 0")), "an empty shape names no mean");
        if (const auto preview = juce::SystemStats::getEnvironmentVariable ("KIRIN_OVERLAY_PREVIEW", {}); preview.isNotEmpty())
        {
            auto shown = panel.state(); shown.overlay.lines = { { state.songs[0].id, 0 }, { state.songs[1].id, 1 }, { state.songs[2].id, 2 } };
            panel.setState (shown);
            auto stream = fresh (juce::File (preview).getChildFile (juce::String ("shape-")
                + (language == i18n::Language::japanese ? "ja.png" : "en.png")));
            require (stream && juce::PNGImageFormat().writeImageToStream (panel.createComponentSnapshot (panel.getLocalBounds()), *stream),
                     "SHAPE preview");
        }
        // LINES is ALL as it was; ONE is ONE as it was.
        lines.onClick();
        require (panel.state().overlay.drawing == Drawing::lines, "LINES turns the shapes off");
        switchHalf (panel, "reference-overlay-one").onClick();
        require (! lines.isVisible() && ! shape.isVisible() && ! every.isVisible(), "ONE hides the drawing switch");
        reference_text_repairs::requireWhole (a_direction::drawnTexts (panel), "TARGET RANGE");
    }
}

// ONE draws the same list, chart and status line whatever ALL holds (lines, SHAPE, ALL SHAPE): an overlay
// saved for ALL never changes ONE.
inline void verifyOneUnchanged()
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        reference_ui::Component plain, held;
        reference_text_repairs::sizePanel (plain, 900, 600); reference_text_repairs::sizePanel (held, 900, 600);
        auto state = page (4, 2);
        plain.setState (state);
        for (const auto drawing : { ref::BalanceOverlay::Drawing::lines, ref::BalanceOverlay::Drawing::shape, ref::BalanceOverlay::Drawing::allShape })
        {
            auto saved = state;
            saved.overlay = { false, drawing, { { state.songs[1].id, 3 }, { state.targets.collection.items[0].id, 0 } } };
            held.setState (saved);
            const auto one = plain.statusTextLayout (plain.footerStatusStrip().getLocalBounds());
            const auto other = held.statusTextLayout (held.footerStatusStrip().getLocalBounds());
            require (one.line.text == other.line.text && one.line.kind == other.line.kind && one.text == other.text,
                     "ONE's status line is the same whatever ALL holds");
            const auto& rows = listOf (plain).rows(), & heldRows = listOf (held).rows();
            require (rows.size() == heldRows.size() && boxesOf (listOf (held)).empty(), "ONE's list has the same rows and no boxes");
            for (size_t index = 0; index < rows.size(); ++index)
                require (rows[index].id == heldRows[index].id && rows[index].title == heldRows[index].title && ! heldRows[index].shown
                         && ! heldRows[index].target, "each row of ONE's list is as before");
            const auto image = plain.createComponentSnapshot (plain.getLocalBounds()), heldImage = held.createComponentSnapshot (held.getLocalBounds());
            bool same = image.getWidth() == heldImage.getWidth();
            for (int y = 0; same && y < image.getHeight(); ++y)
                for (int x = 0; same && x < image.getWidth(); ++x) same = image.getPixelAt (x, y) == heldImage.getPixelAt (x, y);
            require (same, "ONE's page draws pixel for pixel the same whatever ALL holds");
        }
    }
}

inline void verify()
{
    verifyOneUnchanged();
    verifyPalette();
    verifyShape();
    verifySwitchAndColours();
    verifyFullSet();
    std::cout << "Reference Balance overlay (ONE / ALL, lines, targets, 16 + 16): PASS\n";
}
}
