#pragma once

// B・C の図の A の窓（HyphaReferenceAWindowControl.h）を、実際に描いた画面で確かめる。
//  - 操作は A の窓の線を描く図の凡例の上にだけ出る（B の Balance は 300%、C の Cue の図は 200% と 300%）。
//  - リストの即選択・秒数の入力・RESET は見ている役の窓だけを替える。役ごとの選択は別に保存される。
//  - 凡例は実際に入っている長さと目標の範囲を切らずに言う。帯が無いときは目標の範囲と言わない。
//  - 図を指すと A・比べる側・目標の範囲の値が読める。
#include "ReferenceTextRepairsTest.h"

#include <optional>

namespace hypha::tests::reference_a_window
{
namespace ref = reference_audition;
using Control = reference_ui::AWindowControl;
using reference_guide_contract::require;
using reference_text_repairs::requireWhole;
using reference_text_repairs::sizePanel;

inline Control* controlOf (reference_ui::Component& panel, int index = 0)
{
    return dynamic_cast<Control*> (panel.findChildWithID ("reference-a-window-" + juce::String (index)));
}

inline reference_ui::State bPage()
{
    auto state = reference_text_repairs::bState();
    state.viewBindings.clear();
    state.cuePart = reference_audition::CuePart::chorus;
    state.aWindowTarget = { 3, 7 };
    for (auto& level : state.songFacts.back().medianDb) level += 6.0f;  // 帯に幅を持たせる
    return state;
}

inline reference_ui::State cPage()
{
    auto state = a_direction::checkState();
    state.blindLargeScreen = true;
    state.viewBindings = { "spectrum_full", "spectrum_low" };
    state.cuePart = reference_audition::CuePart::chorus;
    state.aWindowTarget = { 2, 7 };
    return state;
}

inline void verifyShownWhereTheChartIs()
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const auto& size : observatory::sizePresets)
        {
            observatory::View shell (observatory::Role::post);
            shell.setSize (size.width, size.height); shell.setDomain (observatory::Domain::reference);
            shell.setExternalAnalysisBodyActive (true);
            const bool inspection = size.density == observatory::Density::inspection;
            for (const auto page : { 2, 3 })
            {
                auto state = page == 3 ? bPage() : cPage();
                state.aWindows.reference = state.aWindows.check = ref::AWindowChoice::custom;
                state.aWindows.referenceSeconds = state.aWindows.checkSeconds = 600;
                state.aKirin = kirinWindow (0.0f, { -20.0, -14.0, -18.0, -30.0 }, 6000);
                state.blindLargeScreen = inspection;
                reference_ui::Component panel;
                panel.setVisible (true);
                panel.setPresentationContext (presentation::forEditor (size.width, size.height));
                panel.setSize (shell.analysisBodyBounds().getWidth(), shell.analysisBodyBounds().getHeight());
                panel.setState (state);
                const bool expected = page == 3 ? inspection : observatory::isFullDensity (size.density);
                const auto where = juce::String (page == 3 ? "B " : "C ") + size.label;
                auto* first = controlOf (panel);
                require (first != nullptr && first->isVisible() == expected, "the A window control follows its chart at " + where);
                require (controlOf (panel, 2) == nullptr || ! controlOf (panel, 2)->isVisible(), "a chart without the A line has no control at " + where);
                if (! expected) continue;
                const text_style::ShownTextLog log;
                juce::Image image (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
                juce::Graphics graphics (image);
                panel.paintEntireComponent (graphics, true);
                const auto previews = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_WINDOW_PREVIEWS", {});
                if (previews.isNotEmpty())
                {
                    const juce::File root (previews); require (root.createDirectory().wasOk(), "preview directory");
                    const auto name = juce::String (page == 3 ? "reference-b-" : "reference-c-") + juce::String (size.width)
                        + (language == i18n::Language::english ? "-en.png" : "-ja.png");
                    const auto output = root.getChildFile (name).createOutputStream();
                    require (output != nullptr && juce::PNGImageFormat().writeImageToStream (image, *output), "write A window preview");
                }
                requireWhole (log.texts(), "A LAST 10:00");
                requireWhole (log.texts(), "TARGET RANGE");
                const auto shown = text_style::shownText ("A LAST 10:00");
                const auto font = labelFont (presentation::forEditor (size.width, size.height), typography::TextRole::legend,
                                             typography::Composition::visualization);
                const auto textWidth = text_style::shownWidth (font, "A LAST 10:00");
                for (int chart = 0; chart < (page == 2 ? 2 : 1); ++chart)
                {
                    auto* control = controlOf (panel, chart);
                    require (control != nullptr && control->isVisible() && panel.getLocalBounds().contains (control->getBounds()),
                             "each chart's control stays on the page at " + where);
                    bool onLabel = false;
                    for (int index = 0; index < log.texts().size(); ++index)
                    {
                        const auto area = log.areas()[static_cast<size_t> (index)];
                        if (log.texts()[index] == shown && control->getBounds().toFloat().expanded (1.0f).contains (area))
                            onLabel = area.getX() + textWidth <= static_cast<float> (control->getRight() - Control::arrowWidth + 2);
                    }
                    require (onLabel, "the control sits on A's label with its arrow clear of the text at " + where);
                }
            }
        }
    }
}

inline void verifyMenu()
{
    ref::AWindowTarget answered;
    int resets = 0;
    std::optional<ref::AWindowChoice> chosen;
    reference_ui::Component panel;
    sizePanel (panel, 900, 600);
    panel.onSelectAWindow = [&] (ref::AWindowTarget target, ref::AWindowChoice choice, int) { answered = target; chosen = choice; };
    panel.onResetAWindow = [&] (ref::AWindowTarget target) { answered = target; ++resets; };
    const auto menuOf = [] (Control& control)
    {
        auto menu = control.makeMenu();
        std::pair<juce::StringArray, juce::String> result;
        for (juce::PopupMenu::MenuItemIterator item (menu); item.next();)
        {
            const auto& row = item.getItem();
            if (row.isSectionHeader || row.isSeparator) continue;
            result.first.add (row.text);
            if (row.isTicked) result.second = row.text;
        }
        return result;
    };
    auto b = bPage();
    b.aWindows.reference = ref::AWindowChoice::seconds30;
    b.aWindows.check = ref::AWindowChoice::all;
    panel.setState (b);
    auto* control = controlOf (panel);
    require (control != nullptr && control->isVisible(), "the B page has the control");
    const auto [rows, ticked] = menuOf (*control);
    require (rows == juce::StringArray { "3 S", "10 S", "30 S", "CUE", "ALL", "SECONDS", "RESET A WINDOW" }, "the list has the quick windows, seconds and RESET");
    require (ticked == "30 S", "the list marks B's window");
    const auto opened = control->currentTarget();
    require (opened.slot == 3 && opened.epoch == 7, "the list opens for the role and the state it shows");
    for (const auto option : ref::aWindowOptions)
    {
        control->choose (opened, Control::itemFor (option));
        require (answered.slot == 3 && answered.epoch == 7 && chosen == option, "choosing a window names the opened role and the window");
    }
    int customSeconds = 0;
    panel.onSelectAWindow = [&] (ref::AWindowTarget target, ref::AWindowChoice choice, int seconds)
    { answered = target; chosen = choice; customSeconds = seconds; };
    auto entry = control->makeSecondsEntry (opened);
    auto* input = dynamic_cast<juce::TextEditor*> (entry->findChildWithID ("a-window-seconds"));
    require (input != nullptr, "seconds has one input with its unit and range");
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const auto& size : observatory::sizePresets)
            require (entry->getWidth() + 40 < size.width, "the seconds entry fits even the smallest editor");
        auto* range = dynamic_cast<juce::Label*> (entry->getChildComponent (0));
        require (range != nullptr && text_style::shownWidth (range->getFont(), range->getText()) <= range->getWidth(),
                 "the seconds range and unit fit in both languages");
        const auto previews = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_WINDOW_PREVIEWS", {});
        if (previews.isNotEmpty())
        {
            juce::Image image (juce::Image::ARGB, entry->getWidth(), entry->getHeight(), true);
            juce::Graphics graphics (image); entry->paintEntireComponent (graphics, true);
            const auto output = juce::File (previews).getChildFile (language == i18n::Language::english ? "seconds-en.png" : "seconds-ja.png").createOutputStream();
            require (output != nullptr && juce::PNGImageFormat().writeImageToStream (image, *output), "write seconds entry preview");
        }
    }
    for (const auto* invalid : { "", "2", "601", "-3", "3.5", "NaN", "3 S", "999999999999" })
    {
        chosen.reset(); input->setText (invalid);
        require (! entry->submit() && ! chosen, "invalid input leaves the role's selection unchanged");
    }
    for (const auto seconds : { 3, 47, 600 })
    {
        input->setText (juce::String (seconds));
        require (entry->submit() && chosen == ref::AWindowChoice::custom && customSeconds == seconds
            && answered.slot == opened.slot && answered.epoch == opened.epoch, "the input sends seconds for the opened role");
    }
    for (const auto* text : { " 3 ", "\t600\n" })
    {
        input->setText (text);
        require (entry->submit() && customSeconds == ref::parseAWindowSeconds (text), "seconds use the shared edge-space handling");
    }
    // A custom input kept open also retains the original target after the page changes.
    panel.setState (cPage());
    input->setText ("45"); entry->submit();
    require (answered.slot == 3 && answered.epoch == 7, "an open seconds input keeps the original target");
    panel.setState (b);
    chosen.reset();
    control->choose (opened, 0);
    require (! chosen && resets == 0, "a list closed without a choice changes nothing");
    control->choose (opened, Control::resetItem);
    require (resets == 1 && ! chosen && answered.epoch == 7, "RESET only asks to drop A's window, for the opened target");
    auto c = cPage();
    c.aWindows = b.aWindows;
    c.aWindowTarget = { 2, 8 };
    panel.setState (c);
    require (menuOf (*control).second == "ALL", "C keeps its own window");
    control->choose (control->currentTarget(), Control::itemFor (ref::AWindowChoice::seconds3));
    require (answered.slot == 2 && answered.epoch == 8 && chosen == ref::AWindowChoice::seconds3, "the C page chooses C's window");
    // リストが開いているあいだに役が替わっても、回答は開いたときの役と世代のまま渡る（古いかは controller が決める）。
    control->choose (opened, Control::itemFor (ref::AWindowChoice::all));
    require (answered.slot == 3 && answered.epoch == 7, "a late answer keeps what it was opened for and never takes the page's role");
}

inline void verifyHidden()
{
    reference_ui::Component panel;
    sizePanel (panel, 900, 600);
    const auto hidden = [&] (const reference_ui::State& state, const char* why)
    {
        panel.setState (state);
        for (int index = 0; index < 3; ++index)
            require (controlOf (panel, index) != nullptr && ! controlOf (panel, index)->isVisible(), juce::String ("no control: ") + why);
    };
    auto state = cPage();
    panel.setState (state);
    require (controlOf (panel)->isVisible(), "the fixture shows the control");
    state.blindPhase = reference_ui::BlindPhase::active;
    hidden (state, "a Blind session hides the charts");
    state = cPage();
    state.comparisonSlot = 1;
    hidden (state, "V has no A window");
    state = cPage();
    state.cueKirin.reset();
    hidden (state, "C without Cue values does not draw the A window");
    state = cPage();
    state.listeningChecks = { "chk-low" };
    hidden (state, "a listening Check has no chart");
    state = cPage();
    state.viewBindings = { "dynamics" };
    hidden (state, "the range strips keep their own window");
    hidden (reference_guide_contract::named ("no_library"), "the guide takes the chart's place");
}

// 凡例の長さと、切れないこと。
inline void verifyLegend()
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        reference_ui::Component panel;
        sizePanel (panel, 900, 600);
        const auto drawnWith = [&] (reference_ui::State state, int frames)
        {
            state.aKirin = kirinWindow (0.0f, { -20.0, -14.0, -18.0, -30.0 }, frames);
            panel.setState (state);
            return a_direction::drawnTexts (panel);
        };
        auto b = bPage();
        b.songFacts.front().part = reference_audition::CuePart::loudest;
        for (const auto page : { 3, 2 })
        {
            const auto& state = page == 3 ? b : cPage();
            for (const auto& [frames, legend] : { std::pair { 40, "A LAST 4 S" }, std::pair { 590, "A LAST 59 S" },
                                                  std::pair { 2'450, "A LAST 4:05" }, std::pair { 6'000, "A LAST 10:00" } })
            {
                const auto drawn = drawnWith (state, frames);
                requireWhole (drawn, legend);
                requireWhole (drawn, "TARGET RANGE");
                if (page == 3) requireWhole (drawn, "B LOUDEST 30 S 1:02-1:24");
            }
            const auto waiting = drawnWith (state, 29);  // 3 秒に満たないあいだは線を出さない
            requireWhole (waiting, "A WAITING");
            require (controlOf (panel)->isVisible(), "the window can be chosen while A waits");
        }
        // 帯は 2 曲から。1 曲なら目標の範囲とは言わない。
        auto single = bPage();
        single.songs.resize (1);
        single.songFacts.resize (1);
        panel.setState (single);
        require (! a_direction::drawnTexts (panel).contains (text_style::shownText ("TARGET RANGE")), "no band, no target range");
        auto missing = bPage();
        missing.songFacts.back().medianDb.clear();
        panel.setState (missing);
        require (! a_direction::drawnTexts (panel).contains (text_style::shownText ("TARGET RANGE")), "one measured song draws no band");
    }
}

inline void pointAt (reference_ui::Component& panel, juce::Point<float> where)
{
    panel.mouseMove ({ juce::Desktop::getInstance().getMainMouseSource(), where, juce::ModifierKeys(), 0, 0, 0, 0, 0,
                       &panel, &panel, juce::Time(), where, juce::Time(), 0, false });
}

// 図を指した所の札に、周波数・A・比べる側・目標の範囲が全部、図の幅の中に出る。入らなければ次の段へ回る。
inline void requireReading (reference_ui::Component& panel, char side, juce::Rectangle<float> chart, const juce::String& where)
{
    pointAt (panel, chart.getCentre().withY (chart.getBottom() - 8.0f));
    const text_style::ShownTextLog log;
    juce::Image image (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
    juce::Graphics graphics (image);
    panel.paintEntireComponent (graphics, true);
    const auto words = i18n::translate ("TARGET %1 TO %2", i18n::current());
    const auto start = words.upToFirstOccurrenceOf ("%1", false, false);
    const auto between = words.fromFirstOccurrenceOf ("%1", false, false).upToFirstOccurrenceOf ("%2", false, false);
    bool a = false, compared = false, range = false;
    for (int index = 0; index < log.texts().size(); ++index)
    {
        const auto& text = log.texts()[index];
        const auto inside = log.areas()[static_cast<size_t> (index)].getRight() <= chart.getRight() + 1.0f;
        a = a || (text.startsWith ("A -") && inside);
        compared = compared || (text.startsWith (juce::String::charToString (side) + " -") && inside);
        range = range || (text.startsWith (start + "-") && text.contains (between) && inside);
    }
    const auto drew = log.texts().joinIntoString (" | ");
    require (a && compared, "the pointer reads A and the compared line at " + where + ": " + drew);
    require (range, "the pointer reads the target range at " + where + ": " + drew);
    pointAt (panel, { -5.0f, -5.0f });
}

inline void verifyReading()
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        {
            reference_ui::Component panel;
            sizePanel (panel, 900, 600);
            panel.setState (bPage());
            const auto label = controlOf (panel)->getBounds().toFloat();
            requireReading (panel, 'B', label.withY (label.getY() - 120.0f).withHeight (100.0f).withWidth (360.0f), "B 300%");
        }
        // C は図の数と並びごと（2 つ・3 つの図は狭い）。図が出る大きさすべてで。
        for (const auto& size : observatory::sizePresets)
        {
            if (! observatory::isFullDensity (size.density)) continue;
            observatory::View shell (observatory::Role::post);
            shell.setSize (size.width, size.height); shell.setDomain (observatory::Domain::reference);
            shell.setExternalAnalysisBodyActive (true);
            for (const auto* layout : { "auto", "equal", "main" })
                for (const auto& bindings : { std::vector<juce::String> { "spectrum_full" },
                                              std::vector<juce::String> { "spectrum_full", "spectrum_low" },
                                              std::vector<juce::String> { "spectrum_low", "spectrum_full", "spectrum_low" } })
                {
                    auto state = cPage();
                    state.blindLargeScreen = size.density == observatory::Density::inspection;
                    state.viewBindings = bindings;
                    state.presentationLayout = layout;
                    reference_ui::Component panel;
                    panel.setVisible (true);
                    panel.setPresentationContext (presentation::forEditor (size.width, size.height));
                    panel.setSize (shell.analysisBodyBounds().getWidth(), shell.analysisBodyBounds().getHeight());
                    panel.setState (state);
                    const auto area = panel.observationWindowBounds().toFloat();
                    require (! area.isEmpty(), "the C page shows its charts");
                    const auto cells = reference_ui::referenceVisualCells (area, static_cast<int> (bindings.size()), layout);
                    for (size_t index = 0; index < bindings.size(); ++index)
                        requireReading (panel, 'C', cells[index].reduced (10.0f, 8.0f),
                                        juce::String (size.label) + " " + layout + " chart " + juce::String (static_cast<int> (index))
                                            + " of " + juce::String (static_cast<int> (bindings.size())));
                }
        }
    }
}

inline void verifySavedChoices()
{
    using Choices = ref::AWindowChoices;
    const auto saved = [] (const Choices& choices)
    {
        auto xml = std::make_unique<juce::XmlElement> ("ReferenceChoices");
        choices.write (*xml);
        return xml;
    };
    const auto standard = saved ({});
    require (standard->getNumChildElements() == 0, "the defaults add nothing to the state");
    const auto read = Choices::read (*standard);
    require (read.reference == ref::AWindowChoice::seconds10 && read.check == ref::AWindowChoice::cue,
             "a project without the value opens at the defaults");
    for (const auto option : ref::aWindowOptions)
    {
        Choices changed;
        changed.reference = option;
        const auto round = Choices::read (*saved (changed));
        require (round.reference == option && round.check == ref::AWindowChoice::cue, "B's window round trips alone");
        changed.check = option;
        const auto both = Choices::read (*saved (changed));
        require (both.reference == option && both.check == option, "each role keeps its window");
    }
    Choices changed;
    changed.reference = ref::AWindowChoice::all;
    auto broken = saved (changed);
    broken->getChildByName ("AWindow")->setAttribute ("ref", "soon");
    require (Choices::read (*broken).reference == ref::AWindowChoice::seconds10, "an unknown value falls back to the default");
}

inline void verifyHelpFits()
{
    Control control;
    verifyReferenceHelpFits ({ control.getTooltip() });
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        reference_ui::Component panel;
        sizePanel (panel, 900, 600);
        panel.setState (cPage());
        const auto centre = controlOf (panel)->getBounds().getCentre();
        require (panel.helpAt (centre) == control.getTooltip(), "the control says what it does in the help line");
    }
}

inline void verify()
{
    verifyShownWhereTheChartIs();
    verifyMenu();
    verifyHidden();
    verifyLegend();
    verifyReading();
    verifySavedChoices();
    verifyHelpFits();
}
}
