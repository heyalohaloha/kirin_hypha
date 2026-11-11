#pragma once

#include "../src/HyphaLiveCompareActionResult.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "../src/HyphaHelpLineText.h"
#include "../src/HyphaQueuedStartText.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTextStyle.h"
#include "FooterNoticeContractChecks.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
// A control that cannot be used says why (2026-11-11): a greyed LISTEN and A/B keep their reason, a click
// on a disabled control hands that reason to the editor, and a greyed menu item gets a reason line.
inline void verifyGreyedEntriesSayWhy()
{
    const auto fail = [] (const char* what) { std::cerr << "Greyed entries: " << what << '\n'; std::abort(); };
    observatory::View post (observatory::Role::post); post.setVisible (true);
    post.setSize (900, 600);
    observatory::LiveCompareFooter state;
    state.entrySupported = true; state.entryRefusal = "Finish Keep / Record first";
    post.setLiveCompareFooter (state);
    for (const char* id : { "observatory-live-compare", "observatory-candidate-compare" })
        if (auto* entry = post.findChildWithID (id); entry == nullptr || ! entry->isVisible() || entry->isEnabled()
            || unavailable::reasonAt (entry) != "Finish Keep / Record first") fail ("greyed entry keeps its reason");
    state.entryEnabled = true; state.entryRefusal = "";
    post.setLiveCompareFooter (state);
    if (auto* entry = post.findChildWithID ("observatory-live-compare"); ! entry->isEnabled() || unavailable::reasonAt (entry).isNotEmpty())
        fail ("an open entry has no reason");
    post.setLiveCompareFooter ({});
    if (post.findChildWithID ("observatory-live-compare")->isVisible()) fail ("an unsupported POST offers no footer entry");
    juce::Component parent; juce::TextButton child; parent.addAndMakeVisible (child);
    unavailable::set (child, false, "Play A for 3 s to make a target");
    juce::String shown;
    unavailable::ClickReasons clicks ([&shown] (const juce::String& reason) { shown = reason; });
    parent.addMouseListener (&clicks, true);
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent press (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  &child, &child, now, {}, now, 1, false);
    clicks.mouseDown (press);
    if (shown != "Play A for 3 s to make a target") fail ("a click on a disabled control shows its reason");
    unavailable::set (child, true, "Play A for 3 s to make a target"); shown.clear(); clicks.mouseDown (press);
    if (shown.isNotEmpty()) fail ("a usable control says nothing");
    parent.removeMouseListener (&clicks);
    juce::PopupMenu menu; unavailable::addItem (menu, 7, "BLIND", false, "Finish Keep / Record first");
    int items = 0; bool reasonLine = false;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    { ++items; reasonLine = reasonLine || (it.getItem().text == "Finish Keep / Record first" && ! it.getItem().isEnabled); }
    if (items != 2 || ! reasonLine) fail ("a greyed menu item has its reason line");
}

// No DAW reports a realtime bounce, which records what a comparison plays (2026-11-11): the help of LISTEN, BLIND
// and A/B says so on its second line. The help line keeps the button's own first line; both lines have Japanese.
inline void verifyComparisonHelpSaysBounce()
{
    const auto fail = [] (const char* what) { std::cerr << "Realtime bounce help: " << what << '\n'; std::abort(); };
    observatory::View post (observatory::Role::post); post.setVisible (true);
    observatory::LiveCompareFooter state; state.entryEnabled = true;
    post.setLiveCompareFooter (state);
    post.setLocalBlindEntryEnabled (true);
    post.setSize (900, 600);
    for (const char* id : { "observatory-live-compare", "observatory-candidate-compare", "observatory-local-blind" })
    {
        auto* client = dynamic_cast<juce::TooltipClient*> (post.findChildWithID (id));
        const auto help = client != nullptr ? client->getTooltip() : juce::String();
        const auto first = help.upToFirstOccurrenceOf ("\n", false, false);
        const auto japanese = i18n::translate (help, i18n::Language::japanese);
        if (! help.endsWith (juce::String ("\n") + live_compare_ui::realtimeBounceNotice)) fail ("the help says what a bounce records");
        if (help_line::forLine (help, false).contains ("bounce")) fail ("the help line keeps the first line");
        if (! japanese.contains (juce::String::fromUTF8 (u8"リアルタイム書き出し")) || japanese.contains ("Realtime")
            || japanese.contains (first)) fail ("both lines of the help have Japanese");
    }
}

inline void verifyLiveCompareActionNotices()
{
    verifyGreyedEntriesSayWhy();
    verifyComparisonHelpSaysBounce();
    observatory::View post (observatory::Role::post); post.setVisible (true);
    observatory::LiveCompareFooter rail;
    rail.active = rail.matched = true;
    rail.preGainTenthsDb = -125;
    rail.postHeldTenthsDb = -100;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (auto preset : observatory::sizePresets)
            for (auto readiness : { live_compare_ui::AutoReadiness::timing,
                                    live_compare_ui::AutoReadiness::match, live_compare_ui::AutoReadiness::limited })
            {
                post.setSize (preset.width, preset.height);
                post.setLiveCompareFooter (rail);
                const auto* notice = live_compare_ui::autoReadinessNotice (readiness);
                post.setFeedback (notice);
                const auto shown = text_style::shownText (notice);
                if (shown.isEmpty() || ! footer_notice::retainedInFooter (post, notice)
                    || (language == i18n::Language::japanese && shown == notice))
                {
                    std::cerr << "AUTO refusal screen lint: " << preset.width << " " << shown
                              << " footer=" << post.statusStripBounds().toString() << '\n';
                    std::abort();
                }
            }
    }
}
}
