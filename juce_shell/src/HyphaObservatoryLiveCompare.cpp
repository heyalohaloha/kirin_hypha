#include "HyphaObservatoryView.h"

#include <array>
#include <cstdlib>

// The footer controls of the two explicit PRE / POST comparisons: Blind (one exact four second
// capture, INV-S25) and the live compare (AGENTS R-12, INV-LC4). Both are POST entries at the
// large sizes. A running live session replaces the footer actions at every size, so returning to
// POST and ending it never need a larger editor.
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
    for (auto* button : { &localBlindButton, &liveCompareButton, &livePreButton, &livePostButton,
                          &liveMatchButton, &liveEndButton, &liveReturnButton, &livePinButton })
    {
        button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        addChildComponent (*button);
    }
    localBlindButton.setComponentID ("observatory-local-blind");
    localBlindButton.setTitle ("PRE / POST Blind Compare");
    localBlindButton.setDescription ("Capture and compare one exact four second PRE and POST range");
    localBlindButton.setTooltip (localBlindButton.getDescription());
    localBlindButton.onClick = [this] { if (onLocalBlind) onLocalBlind(); };

    liveCompareButton.setComponentID ("observatory-live-compare");
    liveCompareButton.setTitle ("PRE / POST Listen");
    liveCompareButton.setDescription ("Switch between PRE and POST of this chain while the song plays. "
                                      "Keep this window open (pin it in Studio One / Studio Pro, turn off "
                                      "Target in Pro Tools); closing or replacing it returns to POST");
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
    liveEndButton.setDescription ("End PRE / POST listening and return to POST");
    liveEndButton.setTooltip (liveEndButton.getDescription());
    liveEndButton.onClick = [this] { if (onLiveCompareEnd) onLiveCompareEnd(); };
    liveReturnButton.setComponentID ("observatory-live-return");
    liveReturnButton.setTitle ("RETURN");
    liveReturnButton.setDescription ("Return POST to its normal level; it rises by the amount shown");
    liveReturnButton.setTooltip (liveReturnButton.getDescription());
    liveReturnButton.onClick = [this] { if (onLiveCompareReturn) onLiveCompareReturn(); };
    livePinButton.setComponentID ("observatory-live-pin");
    livePinButton.setTitle ("PIN 4 S");
    livePinButton.setDescription ("Fix the last four seconds of PRE and POST and open them in PRE / POST Blind");
    livePinButton.setTooltip (livePinButton.getDescription());
    livePinButton.onClick = [this] { if (onLiveComparePin) onLiveComparePin(); };
}

void View::setLiveCompareFooter (const LiveCompareFooter& next)
{
    if (liveCompareState == next)
        return;
    liveCompareState = next;
    resized();
    repaint();
}

// PRE, POST, MATCH, END and MENU while a session runs. Where the rail is narrow MENU goes first,
// then MATCH, then PRE; POST and END stay at every size. The PRE control names what it plays: its
// MATCH gain, or WAIT while PRE is selected and POST still sounds, never by colour alone. MATCH
// reads TP LIMIT while its gain stopped at the true-peak ceiling. Each slot is as wide as its
// longest text, so a boundary that toggles WAIT never moves a control.
bool View::layoutLiveCompareFooter (juce::Rectangle<int> actions)
{
    for (auto* button : { &livePreButton, &livePostButton, &liveMatchButton, &liveEndButton, &liveReturnButton,
                          &livePinButton })
        button->setVisible (false);
    if (captureFrame)
        return false;
    if (! liveCompareState.active)
        return liveCompareState.postHeldTenthsDb < 0 && layoutHeldAttenuation (actions);
    for (auto* button : { &hybridVuButton, &stopButton, &noteButton, &localBlindButton, &liveCompareButton })
        button->setVisible (false);

    const auto& state = liveCompareState;
    const juce::String named = state.matched ? "PRE " + signedGain (state.preGainTenthsDb) : juce::String ("PRE");
    const int namedWidth = juce::jmax (footerButtonWidth (named), footerButtonWidth ("PRE WAIT"));
    const int briefWidth = juce::jmax (footerButtonWidth ("PRE"), footerButtonWidth ("WAIT"));
    const int matchWidth = juce::jmax (footerButtonWidth ("MATCH"), footerButtonWidth ("TP LIMIT"));
    const bool postLowered = state.postHeldTenthsDb < 0;
    const juce::String postNamed = postLowered ? "POST " + signedGain (state.postHeldTenthsDb) : juce::String ("POST");
    // PIN comes before MENU and after MATCH; without Blind in this host it never shows.
    auto withPin = [this] (juce::Array<juce::Button*> set)
    {
        if (liveCompareState.pinAvailable)
            set.insert (3, &livePinButton);
        return set;
    };
    const std::array<juce::Array<juce::Button*>, 5> sets {
        withPin ({ &livePreButton, &livePostButton, &liveMatchButton, &liveEndButton, &operationsButton }),
        withPin ({ &livePreButton, &livePostButton, &liveMatchButton, &liveEndButton }),
        juce::Array<juce::Button*> { &livePreButton, &livePostButton, &liveMatchButton, &liveEndButton },
        juce::Array<juce::Button*> { &livePreButton, &livePostButton, &liveEndButton },
        juce::Array<juce::Button*> { &livePostButton, &liveEndButton } };
    const auto widthsFor = [&] (const juce::Array<juce::Button*>& set, int preWidth, bool rich)
    {
        juce::Array<int> widths;
        for (auto* button : set)
            widths.add (button == &livePreButton ? preWidth
                        : button == &liveMatchButton ? matchWidth
                        : button == &livePostButton ? footerButtonWidth (rich ? postNamed : "POST")
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

    const juce::String preHelp = state.contentHeld
        ? "PRE is held because the latency changed. Stop and restart playback"
        : state.preWaiting ? "PRE is selected. POST plays until PRE is confirmed at this position"
        : state.matched ? "Listen to PRE, the input of this chain, at the level MATCH set"
                        : "Listen to PRE, the input of this chain. MATCH levels it to POST";
    livePreButton.setButtonText (state.preWaiting ? (brief ? "WAIT" : "PRE WAIT") : brief ? "PRE" : named);
    livePreButton.setDescription (preHelp);
    livePreButton.setTooltip (preHelp);
    livePreButton.setToggleState (state.preSelected, juce::dontSendNotification);
    const juce::String postHelp = postLowered ? "Listen to POST, lowered by the attenuation you approved"
                                              : "Listen to POST, the output of this chain";
    livePostButton.setButtonText (brief ? "POST" : postNamed);
    livePostButton.setDescription (postHelp);
    livePostButton.setTooltip (postHelp);
    livePostButton.setToggleState (! state.preSelected, juce::dontSendNotification);
    const juce::String matchHelp = state.matchLimited
        ? "MATCH stopped at the true-peak ceiling: PRE is still quieter than POST. Press to measure again"
        : "Match PRE to POST loudness over the latest four seconds";
    liveMatchButton.setButtonText (state.matchLimited ? "TP LIMIT" : "MATCH");
    liveMatchButton.setDescription (matchHelp);
    liveMatchButton.setTooltip (matchHelp);
    liveMatchButton.setToggleState (state.matched, juce::dontSendNotification);
    operationsButton.setVisible (chosen->contains (&operationsButton));
    for (auto* button : *chosen)
        button->setVisible (true);
    placeFooterButtons (*chosen, widthsFor (*chosen, brief ? briefWidth : namedWidth, ! brief), actions);
    return true;
}

// Out of a session, an approved POST attenuation stays until RETURN, which names how much POST
// rises and stays reachable at every size (INV-LC14). Blind waits for it; LISTEN can reopen a
// session that keeps it.
bool View::layoutHeldAttenuation (juce::Rectangle<int> actions)
{
    localBlindButton.setVisible (false);
    const juce::String named = "RETURN " + signedGain (-liveCompareState.postHeldTenthsDb);
    juce::Array<juce::Button*> everything;
    for (auto* button : { static_cast<juce::Button*> (&hybridVuButton), static_cast<juce::Button*> (&stopButton),
                          static_cast<juce::Button*> (&noteButton), static_cast<juce::Button*> (&liveReturnButton),
                          static_cast<juce::Button*> (&liveCompareButton), static_cast<juce::Button*> (&operationsButton) })
        if (button == &liveReturnButton || button->isVisible())
            everything.add (button);
    const std::array<juce::Array<juce::Button*>, 3> sets {
        everything,
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
    juce::String returnText = "RETURN";
    for (const auto& set : sets)
    {
        if (fits (set, named)) { chosen = &set; returnText = named; break; }
        if (fits (set, "RETURN")) { chosen = &set; break; }
    }
    for (auto* button : { static_cast<juce::Button*> (&hybridVuButton), static_cast<juce::Button*> (&stopButton),
                          static_cast<juce::Button*> (&noteButton), static_cast<juce::Button*> (&liveCompareButton),
                          static_cast<juce::Button*> (&operationsButton) })
        button->setVisible (chosen->contains (button));
    liveReturnButton.setButtonText (returnText);
    liveReturnButton.setVisible (true);
    placeFooterButtons (*chosen, widthsFor (*chosen, returnText), actions);
    return true;
}
}
