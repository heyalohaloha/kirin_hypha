#include "LiveCompareFooterContractTest.h"

#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaLiveCompareRecoveryText.h"

#include <cstdlib>
#include <iostream>
#include <vector>

// The live PRE / POST compare in the Observatory footer (INV-LC4, plan section 9): the entry beside
// Blind at the large POST sizes, a running session's POST and END at every size, a PRE control that
// names WAIT and its MATCH gain in text, and slots that a boundary never moves.
namespace hypha::tests
{
namespace
{
void require (bool condition, const char* what)
{
    if (condition)
        return;
    std::cerr << "Live compare footer contract failed: " << what << '\n';
    std::exit (EXIT_FAILURE);
}

juce::Button* control (observatory::View& view, const char* id)
{
    auto* button = dynamic_cast<juce::Button*> (view.findChildWithID (id));
    require (button != nullptr && button->onClick != nullptr, id);
    return button;
}

bool readable (observatory::View& view, const juce::Button& button)
{
    const auto font = labelFont (view.presentationContext(), typography::TextRole::action);
    return text_style::shownWidth (font, button.getButtonText()) + 6.0f <= static_cast<float> (button.getWidth());
}

std::vector<juce::Rectangle<int>> boundsOf (const std::vector<juce::Component*>& controls)
{
    std::vector<juce::Rectangle<int>> bounds;
    for (const auto* component : controls)
        bounds.push_back (component->isVisible() ? component->getBounds() : juce::Rectangle<int>());
    return bounds;
}
}

void verifyLiveCompareFooterContract()
{
    observatory::View post (observatory::Role::post);
    observatory::View pre (observatory::Role::pre);
    bool recoveryFits = true;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (auto preset : observatory::sizePresets)
        {
            post.setSize (preset.width, preset.height);
            for (int code = 0; code <= static_cast<int> (live_compare::RecoveryReason::unknown); ++code)
                for (int phase = 0; phase < 4; ++phase)
                {
                    live_compare::Status recovery;
                    recovery.reason = static_cast<live_compare::RecoveryReason> (code);
                    recovery.observation = recovery.reason;
                    recovery.active = phase < 2;
                    if (phase == 3) recovery.postTarget = recovery.postActual = 0.5f;
                    recovery.preSelected = recovery.preWaiting = phase == 0;
                    recovery.interrupted = phase == 1;
                    recovery.contentHeld = recovery.reason == live_compare::RecoveryReason::contentChanged;
                    recovery.compensationOff = recovery.reason == live_compare::RecoveryReason::compensationOff;
                    const auto* notice = live_compare_ui::namedRecovery (recovery);
                    observatory::LiveCompareFooter footer;
                    footer.active = recovery.active;
                    footer.preSelected = recovery.preSelected;
                    footer.preWaiting = recovery.preWaiting;
                    footer.entryEnabled = true;
                    footer.postHeldTenthsDb = phase == 3 ? -60 : 0;
                    post.setLiveCompareFooter (footer);
                    post.setFeedback (notice);
                    if (juce::String (notice).contains ("RETURN"))
                        require (post.findChildWithID ("observatory-live-return")->isVisible(), "recovery RETURN exists");
                    if (! recovery.active)
                        require (! juce::String (notice).contains ("END"), "inactive recovery never points to missing END");
                    const auto font = monoFont (post.presentationContext(), post.statusStripFolded()
                        ? typography::TextRole::status : typography::TextRole::action);
                    if (text_style::shownWidth (font, notice) > post.statusStripBounds().getWidth() - 12)
                    {
                        std::cerr << preset.width << " recovery width=" << text_style::shownWidth (font, notice)
                            << " available=" << post.statusStripBounds().getWidth() - 12 << ": "
                            << text_style::shownText (notice) << '\n';
                        recoveryFits = false;
                    }
                }
        }
    }
    require (recoveryFits, "persistent recovery reads whole in both languages at every size");
    post.setFeedback ({});
    post.setLiveCompareFooter ({});
    auto* entry = control (post, "observatory-live-compare");
    auto* preEntry = control (pre, "observatory-live-compare");
    auto* preButton = control (post, "observatory-live-pre");
    auto* postButton = control (post, "observatory-live-post");
    auto* match = control (post, "observatory-live-match");
    auto* end = control (post, "observatory-live-end");
    auto* blind = control (post, "observatory-local-blind");
    auto* menu = &post.operationsMenuAnchor();

    observatory::LiveCompareFooter state;
    for (const auto preset : observatory::sizePresets)
    {
        post.setSize (preset.width, preset.height);
        require (! entry->isVisible(), "no entry until the processor offers the live compare");
    }
    state.entryEnabled = true;
    post.setLiveCompareFooter (state);
    pre.setLiveCompareFooter (state);
    post.setLocalBlindEntryEnabled (true);
    for (const auto preset : observatory::sizePresets)
    {
        post.setSize (preset.width, preset.height);
        pre.setSize (preset.width, preset.height);
        require (entry->isVisible() == (preset.width >= 600), "the entry sits beside Blind at the large sizes");
        require (! preEntry->isVisible(), "PRE never offers the live compare");
        require (! preButton->isVisible() && ! postButton->isVisible() && ! end->isVisible(),
                 "no session controls before a session");
        if (entry->isVisible())
            require (readable (post, *entry) && blind->isVisible()
                         && ! entry->getBounds().intersects (blind->getBounds())
                         && ! entry->getBounds().intersects (menu->getBounds()),
                     "the entry reads whole and overlaps neither Blind nor MENU");
    }

    int started = 0, ended = 0, matched = 0, selectedPre = 0, selectedPost = 0;
    post.onLiveCompareStart = [&] { ++started; };
    post.onLiveCompareEnd = [&] { ++ended; };
    post.onLiveCompareMatch = [&] { ++matched; };
    post.onLiveCompareSelect = [&] (bool choosePre) { ++(choosePre ? selectedPre : selectedPost); };
    post.setSize (900, 600);
    entry->onClick();
    require (started == 1, "the entry asks the editor to start a session");

    state.active = true;
    post.setLiveCompareFooter (state);
    const std::vector<juce::Component*> rail { preButton, postButton, match, end, menu };
    for (const auto preset : observatory::sizePresets)
    {
        post.setSize (preset.width, preset.height);
        require (postButton->isVisible() && end->isVisible(), "POST and END stay at every size");
        require (! entry->isVisible() && ! blind->isVisible(), "a session replaces the comparison entries");
        if (preset.width >= 600)
            require (preButton->isVisible() && match->isVisible() && menu->isVisible(),
                     "the large rail keeps PRE, MATCH and MENU");
        std::cout << "Live compare footer " << preset.label << ":";
        for (auto* component : rail)
            if (component->isVisible())
                std::cout << ' ' << component->getComponentID();
        std::cout << '\n';
        for (std::size_t i = 0; i < rail.size(); ++i)
        {
            if (! rail[i]->isVisible())
                continue;
            require (post.getLocalBounds().contains (rail[i]->getBounds()), "each control lies in the editor");
            if (auto* button = dynamic_cast<juce::Button*> (rail[i]); button != nullptr && rail[i] != menu)
                require (readable (post, *button), "each control reads whole");
            for (std::size_t j = i + 1; j < rail.size(); ++j)
                require (! rail[j]->isVisible() || ! rail[i]->getBounds().intersects (rail[j]->getBounds()),
                         "controls never overlap");
        }
        require (postButton->getToggleState() && ! preButton->getToggleState(), "POST is the selected source");

        const auto before = boundsOf (rail);
        state.preSelected = true;
        state.preWaiting = true;
        post.setLiveCompareFooter (state);
        require (boundsOf (rail) == before, "WAIT never moves a control");
        require (! preButton->isVisible() || preButton->getButtonText().contains ("WAIT"),
                 "PRE selected while POST sounds reads WAIT in text");
        require (preButton->getToggleState() && ! postButton->getToggleState(), "PRE is the selected source");
        state.preWaiting = false;
        post.setLiveCompareFooter (state);
        require (boundsOf (rail) == before, "the proof returning never moves a control");
        require (! preButton->isVisible() || preButton->getButtonText() == "PRE", "PRE reads PRE once proven");
        state.preSelected = false;
        post.setLiveCompareFooter (state);
    }

    post.setSize (900, 600);
    state.matched = true;
    state.preGainTenthsDb = 32;
    post.setLiveCompareFooter (state);
    require (preButton->getButtonText() == "PRE +3.2 dB" && match->getToggleState(),
             "the PRE control names its MATCH gain and MATCH shows it is applied");
    state.preGainTenthsDb = 0;
    post.setLiveCompareFooter (state);
    require (preButton->getButtonText() == "PRE 0.0 dB", "a unity MATCH has no sign");
    state.preGainTenthsDb = -60;
    post.setLiveCompareFooter (state);
    require (preButton->getButtonText() == "PRE -6.0 dB", "a cut keeps its sign");
    state.preGainTenthsDb = -125;
    post.setLiveCompareFooter (state);
    require (preButton->getButtonText() == "PRE -12.5 dB" && readable (post, *preButton),
             "a two-digit gain still reads whole");

    preButton->onClick();
    postButton->onClick();
    match->onClick();
    end->onClick();
    require (selectedPre == 1 && selectedPost == 1 && matched == 1 && ended == 1,
             "PRE, POST, MATCH and END reach the editor");

    // A MATCH that stopped at the true-peak ceiling says so in text, and its slot does not move.
    for (const auto preset : { observatory::sizePresets[3], observatory::sizePresets[4] })
    {
        post.setSize (preset.width, preset.height);
        state.preGainTenthsDb = 125;
        state.matchLimited = false;
        post.setLiveCompareFooter (state);
        const auto matchedBounds = boundsOf (rail);
        state.matchLimited = true;
        post.setLiveCompareFooter (state);
        require (match->getButtonText() == "TP LIMIT" && readable (post, *match) && readable (post, *preButton),
                 "TP LIMIT and the longest gain read whole");
        require (boundsOf (rail) == matchedBounds, "TP LIMIT never moves a control");
        require (preButton->isVisible() && menu->isVisible(), "the longest texts keep PRE, MATCH and MENU");
    }
    state.matchLimited = false;

    // INV-LC16: while PRE follows POST the MATCH slot reads AUTO, in the same place.
    for (const auto preset : { observatory::sizePresets[3], observatory::sizePresets[4] })
    {
        post.setSize (preset.width, preset.height);
        post.setLiveCompareFooter (state);
        const auto matchedBounds = boundsOf (rail);
        state.following = true;
        post.setLiveCompareFooter (state);
        require (match->getButtonText() == "AUTO" && match->getToggleState() && readable (post, *match),
                 "AUTO reads on the MATCH slot");
        require (boundsOf (rail) == matchedBounds, "AUTO never moves a control");
        state.following = false;
        post.setLiveCompareFooter (state);
        require (match->getButtonText() == "MATCH", "stopping AUTO reads MATCH again");
    }

    // Every live notice reads whole in the 300% footer, in both languages, beside the widest rail:
    // named PRE and POST gains, PIN and AUTO. The TP limit notice carries two values and may lose
    // the second; TP LIMIT stays on MATCH.
    post.setSize (900, 600);
    const auto plain = state;
    state.preGainTenthsDb = -125;
    state.postHeldTenthsDb = -100;
    state.pinAvailable = true;
    state.following = true;
    post.setLiveCompareFooter (state);
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        const auto font = labelFont (post.presentationContext(), typography::TextRole::action);
        const auto available = static_cast<float> (post.sessionBounds().getWidth() - 8);
        for (const char* notice : { "Closing returns to POST", "LISTEN could not start",
                                    "Choose the PRE first", "Mono / stereo only", "Paired PRE unavailable",
                                    "PRE is multi-mono: insert it as stereo",
                                    "Select PRE again", "LISTEN ended; POST plays", "MATCH: PRE -12.50 dB",
                                    "MATCH waits for PRE", "MATCH needs 3 s of play", "MATCH failed; try again",
                                    "MATCH needs more signal", "MATCH over 24 dB", "MATCH: POST -24.00 dB",
                                    "POST back to normal", "Press RETURN first", "PRE over TP ceiling",
                                    "PRE 170.67 ms early", "PRE 170.67 ms late", "PRE held: latency changed",
                                    "PIN waits for PRE", "PIN needs 4 s of play", "Last 4 s not one range",
                                    "PIN failed; try again", "AUTO on: within 0.5 dB", "AUTO off",
                                    "AUTO stopped: TP ceiling", "AUTO stopped: over 6 dB",
                                    "Delay compensation is off in Pro Tools" })
        {
            if (text_style::shownWidth (font, notice) > available)
                std::cerr << "too wide at 300%: " << text_style::shownText (notice) << '\n';
            require (text_style::shownWidth (font, notice) <= available, "a live notice reads whole at 300%");
        }
    }
    state = plain;
    post.setLiveCompareFooter (state);

    // A settled MATCH exposes the primary BLIND; PIN remains available through MENU.
    state.blindAvailable = true;
    post.setLiveCompareFooter (state);
    for (const auto preset : observatory::sizePresets)
    {
        post.setSize (preset.width, preset.height);
        require (menu->isVisible(), "small layouts never lose the menu");
        if (blind->isVisible())
            require (blind->isEnabled() && readable (post, *blind), "matched Blind is reachable and readable");
        if (preset.width >= 600) require (blind->isVisible(), "large MATCH footer exposes BLIND");
    }
    state.blindAvailable = false;
    post.setLiveCompareFooter (state);
    require (! blind->isVisible(), "an unsettled or limited MATCH never exposes direct BLIND");

    // An approved POST attenuation is named on POST during a session; after window close it is held, and
    // RETURN names how much POST rises, at every size, while Blind waits for it (INV-LC14).
    auto* returnButton = control (post, "observatory-live-return");
    int returned = 0;
    post.onLiveCompareReturn = [&] { ++returned; };
    post.setSize (900, 600);
    state.matchLimited = false;
    state.postHeldTenthsDb = -70;
    post.setLiveCompareFooter (state);
    require (postButton->getButtonText() == "POST -7.0 dB" && readable (post, *postButton) && ! returnButton->isVisible(),
             "a session names the approved POST attenuation");
    const auto held = state;
    for (const auto preset : observatory::sizePresets)
    {
        post.setSize (preset.width, preset.height);
        require (end->isVisible() && end->getButtonText() == "END +7.0 dB" && readable (post, *end),
                 "END announces the rise visibly at every size, never only in a tooltip");
        require (menu->isVisible() && ! menu->getBounds().intersects (end->getBounds()), "MENU remains reachable");
    }
    state = {};
    state.entryEnabled = true;
    state.postHeldTenthsDb = -70;
    post.setLiveCompareFooter (state);
    for (const auto preset : observatory::sizePresets)
    {
        post.setSize (preset.width, preset.height);
        require (returnButton->isVisible() && readable (post, *returnButton)
                     && returnButton->getButtonText() == "RETURN +7.0 dB"
                     && post.getLocalBounds().contains (returnButton->getBounds()),
                 "RETURN stays reachable and reads whole at every size");
        require (! blind->isVisible() && ! postButton->isVisible(), "Blind waits for RETURN; no session controls");
        if (preset.width >= 600)
            require (returnButton->getButtonText() == "RETURN +7.0 dB" && entry->isVisible()
                         && ! returnButton->getBounds().intersects (entry->getBounds()),
                     "the large rail names the rise and keeps LISTEN");
    }
    returnButton->onClick();
    require (returned == 1, "RETURN reaches the editor");
    state.postHeldTenthsDb = 0;
    post.setLiveCompareFooter (state);
    require (! returnButton->isVisible(), "without a held attenuation there is no RETURN");
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        for (const auto preset : observatory::sizePresets)
            for (const bool active : { false, true })
            {
                auto safety = held;
                safety.active = active;
                safety.postHeldTenthsDb = -240;
                post.setSize (preset.width, preset.height);
                post.setLiveCompareFooter (safety);
                auto* action = active ? end : returnButton;
                require (action->isVisible() && readable (post, *action)
                    && action->getTitle().contains ("+24.0 dB"), "maximum rise is visible and accessible in both languages");
                const auto preview = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
                if (preview.isNotEmpty())
                {
                    auto stream = juce::File (preview).getChildFile ("live-return-" + juce::String (static_cast<int> (language))
                        + "-" + juce::String (active ? 1 : 0) + "-" + juce::String (preset.width) + ".png").createOutputStream();
                    require (stream != nullptr && juce::PNGImageFormat().writeImageToStream (
                        post.createComponentSnapshot (post.getLocalBounds()), *stream), "END preview renders");
                }
            }
    }
    post.setSize (900, 600);
    state = held;
    state.postHeldTenthsDb = 0;
    post.setLiveCompareFooter (state);

    require (post.setManualHybridVuVisible (true), "Hybrid VU opens");
    require (! preButton->isVisible() && ! postButton->isVisible() && ! end->isVisible(),
             "the Hybrid VU page shows no footer controls");
    post.setManualHybridVuVisible (false);
    require (postButton->isVisible() && end->isVisible(), "leaving the Hybrid VU brings the session back");

    state = {};
    state.entryEnabled = true;
    post.setLiveCompareFooter (state);
    require (entry->isVisible() && ! postButton->isVisible() && ! end->isVisible(),
             "ending the session restores the entry");
    std::cout << "Live compare footer contract: PASS\n";
}
}
