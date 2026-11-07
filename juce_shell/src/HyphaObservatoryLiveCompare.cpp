#include "HyphaObservatoryView.h"
#include "HyphaShortcutBindings.h"

#include <array>
#include <cstdlib>

// POST comparisons: live BLIND or named LISTEN. 1.1.52 shows no Exact 4 S (PIN 4 S) entry.
// Active/recovery controls remain reachable at every size and always disclose a level rise.
namespace hypha::observatory
{
namespace
{
juce::String signedGain (int tenths)
{
    if (tenths == 0)
        return "0.0 dB";
    const auto magnitude = std::abs (tenths);
    return juce::String (tenths > 0 ? "+" : "-") + juce::String (magnitude / 10) + "."
         + juce::String (magnitude % 10) + " dB";
}
}

void View::configureComparisonEntries()
{
    for (auto* button : { &localBlindButton, &liveCompareButton, &candidateCompareButton, &livePreButton, &livePostButton,
                          &liveMatchButton, &liveEndButton, &liveReturnButton })
    {
        button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        addChildComponent (*button);
    }
    localBlindButton.setComponentID ("observatory-local-blind");
    localBlindButton.setTitle ("PRE / POST Blind Compare");
    localBlindButton.setDescription ("Match levels and compare while the song plays");
    localBlindButton.setTooltip (localBlindButton.getDescription());
    localBlindButton.onClick = [this] { if (onLocalBlind) onLocalBlind(); };

    candidateCompareButton.setComponentID ("observatory-candidate-compare");
    candidateCompareButton.setTitle ("A/B");
    candidateCompareButton.setDescription ("Compare A and B on one track");
    candidateCompareButton.setTooltip (candidateCompareButton.getDescription());
    candidateCompareButton.onClick = [this] { if (onCandidateCompare) onCandidateCompare(); };
    liveCompareButton.setComponentID ("observatory-live-compare");
    liveCompareButton.setTitle ("PRE / POST Listen");
    liveCompareButton.setDescription ("Switch between PRE and POST while playing. Comparison continues when this window closes");
    liveCompareButton.setTooltip (liveCompareButton.getDescription());
    liveCompareButton.onClick = [this] { if (onLiveCompareStart) onLiveCompareStart(); };
    livePreButton.setComponentID ("observatory-live-pre");
    livePreButton.setTitle ("PRE");
    livePreButton.onClick = [this] { if (onLiveCompareSelect) onLiveCompareSelect (true); };
    livePostButton.setComponentID ("observatory-live-post");
    livePostButton.setTitle ("POST");
    livePostButton.onClick = [this] { if (onLiveCompareSelect) onLiveCompareSelect (false); };
    liveMatchButton.setComponentID ("observatory-live-match");
    liveMatchButton.setTitle ("MATCH");
    liveMatchButton.onClick = [this] { if (onLiveCompareMatch) onLiveCompareMatch(); };
    liveEndButton.setComponentID ("observatory-live-end");
    liveEndButton.setTitle ("END");
    liveEndButton.setColour (juce::TextButton::textColourOffId, COL_NORMAL);
    liveEndButton.setDescription ("End listening and restore normal POST level");
    liveEndButton.setTooltip (liveEndButton.getDescription());
    liveEndButton.onClick = [this] { if (onLiveCompareEnd) onLiveCompareEnd(); };
    liveReturnButton.setComponentID ("observatory-live-return");
    liveReturnButton.setTitle ("RETURN");
    liveReturnButton.setColour (juce::TextButton::textColourOffId, COL_NORMAL);
    liveReturnButton.setDescription ("Return POST to its normal level; it rises by the amount shown");
    liveReturnButton.setTooltip (liveReturnButton.getDescription());
    liveReturnButton.onClick = [this] { if (onLiveCompareReturn) onLiveCompareReturn(); };
}

void View::setLiveCompareFooter (const LiveCompareFooter& next)
{
    if (liveCompareState == next)
        return;
    liveCompareState = next;
    resized();
    repaint();
}

// PRE, POST, MATCH, BLIND, END and MENU while a session runs. Narrow rails move secondary actions
// to MENU; END and MENU never disappear. The PRE control names what it plays: its
// MATCH gain and the selected source stay visible while timing renews. MATCH
// reads TP LIMIT while its gain stopped at the true-peak ceiling, and AUTO while PRE follows POST.
// Fixed slot widths keep controls steady across ordinary gain text changes.
bool View::layoutLiveCompareFooter (juce::Rectangle<int> actions)
{
    for (auto* button : { &livePreButton, &livePostButton, &liveMatchButton, &liveEndButton, &liveReturnButton })
        button->setVisible (false);
    localBlindButton.setTitle (liveCompareState.candidate ? "A/B Blind Compare" : "PRE / POST Blind Compare");
    if (captureFrame)
        return false;
    if (liveCompareState.finishing)
    {
        // The comparison is closed. Keep the pending rise and MENU, not disabled trial controls.
        for (auto* button : { &hybridVuButton, &stopButton, &noteButton, &localBlindButton, &liveCompareButton, &candidateCompareButton })
            button->setVisible (false);
        const auto pending = liveCompareState.postHeldTenthsDb < 0
            ? "RETURNING " + signedGain (-liveCompareState.postHeldTenthsDb) : juce::String ("RETURNING");
        liveReturnButton.setButtonText (pending);
        liveReturnButton.setTitle (pending);
        liveReturnButton.setDescription ("Ended; normal level returns with audio");
        liveReturnButton.setTooltip (liveReturnButton.getDescription());
        liveReturnButton.setStatusOnly (true);
        liveReturnButton.setVisible (true);
        operationsButton.setVisible (true);
        placeFooterButtons ({ &liveReturnButton, &operationsButton },
            { footerButtonWidth (pending), footerButtonWidth (operationsButton.getButtonText()) }, actions);
        return true;
    }
    if (! liveCompareState.active)
        return liveCompareState.postHeldTenthsDb < 0 && layoutHeldAttenuation (actions);
    for (auto* button : { &hybridVuButton, &stopButton, &noteButton, &localBlindButton, &liveCompareButton, &candidateCompareButton })
        button->setVisible (false);

    const auto& state = liveCompareState;
    for (auto* button : { &livePreButton, &livePostButton, &liveEndButton })
        button->getProperties().set (hypha::shortcuts::table::candidateKeysProperty, state.candidate);
    liveEndButton.setDescription (state.candidate ? "End A/B and restore the chain" : "End listening and restore normal POST level");
    liveEndButton.setTooltip (liveEndButton.getDescription());
    const juce::String preLabel = state.candidate ? "A" : "PRE";
    const juce::String postLabel = state.candidate ? "B" : "POST";
    const juce::String named = state.matched ? preLabel + " " + signedGain (state.preGainTenthsDb) : preLabel;
    const int namedWidth = state.candidate ? footerButtonWidth (named)
        : juce::jmax (footerButtonWidth (named), footerButtonWidth ("PRE WAIT"));
    const int briefWidth = state.candidate ? juce::jmax (footerButtonWidth (preLabel), footerButtonWidth (postLabel))
        : juce::jmax (footerButtonWidth ("PRE"), footerButtonWidth ("WAIT"));
    const int matchWidth = juce::jmax (footerButtonWidth ("MATCH"), footerButtonWidth ("TP LIMIT"),
                                       footerButtonWidth ("AUTO"));
    const bool postLowered = state.postHeldTenthsDb < 0;
    const juce::String postNamed = postLowered ? postLabel + " " + signedGain (state.postHeldTenthsDb) : postLabel;
    liveEndButton.setButtonText (state.finishing ? "RETURNING"
        : postLowered ? "END " + signedGain (-state.postHeldTenthsDb) : juce::String ("END"));
    liveEndButton.setEnabled (! state.finishing);
    liveEndButton.setTitle (liveEndButton.getButtonText());
    localBlindButton.setEnabled (state.blindAvailable && ! state.finishing);
    for (auto* button : { &livePreButton, &livePostButton, &liveMatchButton })
        button->setEnabled (! state.finishing);
    auto withBlind = [this] (juce::Array<juce::Button*> set)
    {
        if (liveCompareState.blindAvailable) set.insert (3, &localBlindButton);
        return set;
    };
    const std::array<juce::Array<juce::Button*>, 5> sets {
        withBlind ({ &livePreButton, &livePostButton, &liveMatchButton, &liveEndButton, &operationsButton }),
        juce::Array<juce::Button*> { &livePreButton, &livePostButton, &liveMatchButton, &liveEndButton, &operationsButton },
        juce::Array<juce::Button*> { &livePreButton, &livePostButton, &liveEndButton, &operationsButton },
        juce::Array<juce::Button*> { &livePostButton, &liveEndButton, &operationsButton },
        juce::Array<juce::Button*> { &liveEndButton, &operationsButton } };
    const auto widthsFor = [&] (const juce::Array<juce::Button*>& set, int preWidth, bool rich)
    {
        juce::Array<int> widths;
        const auto pairWidth = state.candidate ? juce::jmax (preWidth, footerButtonWidth (rich ? postNamed : postLabel)) : preWidth;
        for (auto* button : set)
            widths.add (button == &livePreButton ? pairWidth
                        : button == &liveMatchButton ? matchWidth
                        : button == &livePostButton ? (state.candidate ? pairWidth : footerButtonWidth (rich ? postNamed : postLabel))
                                                    : footerButtonWidth (button->getButtonText()));
        return widths;
    };
    const auto total = [] (const juce::Array<int>& widths)
    {
        int sum = 0;
        for (const auto width : widths)
            sum += width;
        return sum;
    };
    const juce::Array<juce::Button*>* chosen = &sets.back();
    bool brief = true;
    for (const auto& set : sets)
    {
        if (total (widthsFor (set, namedWidth, true)) <= actions.getWidth()) { chosen = &set; brief = false; break; }
        if (total (widthsFor (set, briefWidth, false)) <= actions.getWidth()) { chosen = &set; break; }
    }

    const juce::String preHelp = *state.recoveryHelp != 0 ? state.recoveryHelp
        : state.matched ? "Listen to PRE, the input of this chain, at the level MATCH set"
                        : "Listen to PRE, the input of this chain. MATCH levels it to POST";
    livePreButton.setButtonText (brief ? preLabel : named);
    livePreButton.setTitle (preLabel);
    livePreButton.setDescription (state.candidate ? "Listen to A" : preHelp);
    livePreButton.setTooltip (livePreButton.getDescription());
    livePreButton.setToggleState (state.preSelected, juce::dontSendNotification);
    const juce::String postHelp = postLowered ? "Listen to POST, lowered by the attenuation you approved"
                                              : "Listen to POST, the output of this chain";
    livePostButton.setButtonText (brief ? postLabel : postNamed);
    livePostButton.setTitle (postLabel);
    livePostButton.setDescription (state.candidate ? "Listen to B" : postHelp);
    livePostButton.setTooltip (livePostButton.getDescription());
    livePostButton.setToggleState (! state.preSelected, juce::dontSendNotification);
    const juce::String matchHelp = state.following
        ? "AUTO: PRE follows POST loudness within 0.5 dB, up to 6 dB from your MATCH, lower if the TP ceiling needs it. Press to MATCH again or stop AUTO"
        : state.matchLimited
        ? "MATCH stopped at the true-peak ceiling: PRE is still quieter than POST. Press to measure again"
        : state.matched ? "Press to MATCH again or to let PRE follow POST (AUTO)"
                        : "Match PRE to POST loudness over the latest four seconds";
    liveMatchButton.setButtonText (state.following ? "AUTO" : state.matchLimited ? "TP LIMIT" : "MATCH");
    const juce::String candidateMatchHelp = state.following
        ? "AUTO: A follows B loudness within 0.5 dB, up to 6 dB from your MATCH, lower if the TP ceiling needs it. Press AUTO to match again or stop AUTO"
        : state.matchLimited
        ? "MATCH stopped at the true-peak ceiling: A is still quieter than B. Press to measure again"
        : state.matched ? "Press MATCH to match again or start AUTO (A follows B)"
                        : "Match A to B loudness over the latest four seconds";
    liveMatchButton.setTitle (state.candidate ? liveMatchButton.getButtonText() : "MATCH");
    liveMatchButton.setDescription (state.candidate ? candidateMatchHelp : matchHelp);
    liveMatchButton.setTooltip (liveMatchButton.getDescription());
    liveMatchButton.setToggleState (state.matched, juce::dontSendNotification);
    operationsButton.setVisible (chosen->contains (&operationsButton));
    for (auto* button : *chosen)
        button->setVisible (true);
    placeFooterButtons (*chosen, widthsFor (*chosen, brief ? briefWidth : namedWidth, ! brief), actions);
    if (state.candidate && livePreButton.isVisible() && livePostButton.isVisible())
    {
        // The rail's integer remainder can give one slot an extra pixel; keep A/B identical.
        const auto width = juce::jmin (livePreButton.getWidth(), livePostButton.getWidth());
        livePreButton.setSize (width, livePreButton.getHeight());
        livePostButton.setSize (width, livePostButton.getHeight());
    }
    return true;
}

// Out of a session, an approved POST attenuation stays until RETURN, which names how much POST
// rises and stays reachable at every size (INV-LC14). BLIND returns it first; LISTEN can reopen a
// session that keeps it.
bool View::layoutHeldAttenuation (juce::Rectangle<int> actions)
{
    candidateCompareButton.setVisible (liveCompareState.entryEnabled);
    localBlindButton.setVisible (localBlindEntryEnabled && liveCompareState.blindAvailable);
    localBlindButton.setEnabled (liveCompareState.blindAvailable && ! keepActive);
    liveReturnButton.setStatusOnly (false);
    liveReturnButton.setDescription ("Return POST to its normal level; it rises by the amount shown");
    liveReturnButton.setTooltip (liveReturnButton.getDescription());
    const juce::String named = "RETURN " + signedGain (-liveCompareState.postHeldTenthsDb);
    juce::Array<juce::Button*> everything;
    for (auto* button : { static_cast<juce::Button*> (&hybridVuButton), static_cast<juce::Button*> (&stopButton),
                          static_cast<juce::Button*> (&noteButton), static_cast<juce::Button*> (&liveReturnButton),
                          static_cast<juce::Button*> (&liveCompareButton), static_cast<juce::Button*> (&candidateCompareButton), static_cast<juce::Button*> (&localBlindButton),
                          static_cast<juce::Button*> (&operationsButton) })
        if (button == &liveReturnButton || button->isVisible())
            everything.add (button);
    juce::Array<juce::Button*> auditions { &liveReturnButton };
    for (auto* button : { &liveCompareButton, &candidateCompareButton, &localBlindButton, &operationsButton })
        if (button->isVisible()) auditions.add (button);
    juce::Array<juce::Button*> candidateEntry { &liveReturnButton };
    if (candidateCompareButton.isVisible()) candidateEntry.add (&candidateCompareButton);
    candidateEntry.add (&operationsButton);
    const std::array<juce::Array<juce::Button*>, 5> sets {
        everything,
        auditions,
        candidateEntry,
        juce::Array<juce::Button*> { &liveReturnButton, &operationsButton },
        juce::Array<juce::Button*> { &liveReturnButton } };
    const auto widthsFor = [&] (const juce::Array<juce::Button*>& set, const juce::String& returnText)
    {
        juce::Array<int> widths;
        for (auto* button : set)
            widths.add (footerButtonWidth (button == &liveReturnButton ? returnText : button->getButtonText()));
        return widths;
    };
    const auto fits = [&] (const juce::Array<juce::Button*>& set, const juce::String& returnText)
    {
        int sum = 0;
        for (const auto width : widthsFor (set, returnText))
            sum += width;
        return sum <= actions.getWidth();
    };
    const juce::Array<juce::Button*>* chosen = &sets.back();
    juce::String returnText = named;
    for (const auto& set : sets)
    {
        if (fits (set, named)) { chosen = &set; returnText = named; break; }
    }
    for (auto* button : { static_cast<juce::Button*> (&hybridVuButton), static_cast<juce::Button*> (&stopButton),
                          static_cast<juce::Button*> (&noteButton), static_cast<juce::Button*> (&liveCompareButton), static_cast<juce::Button*> (&candidateCompareButton),
                          static_cast<juce::Button*> (&localBlindButton), static_cast<juce::Button*> (&operationsButton) })
        button->setVisible (chosen->contains (button));
    liveReturnButton.setButtonText (returnText);
    liveReturnButton.setTitle (returnText);
    liveReturnButton.setVisible (true);
    placeFooterButtons (*chosen, widthsFor (*chosen, returnText), actions);
    return true;
}
}
