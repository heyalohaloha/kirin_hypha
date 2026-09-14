#include "PluginEditor.h"
#include <cstring>

void KirinHyphaEditor::refreshPairPreview (bool demand)
{
    const auto now = nowSecs();
    if (! demand && isShowing() && now - pairPreviewRefreshAt < 0.1) return;
    pairPreviewRefreshAt = now;
    const auto available = isPost && isShowing() && nameField.isVisible()
        && processorRef.pairStatus() == KIRIN_PAIR_STATUS_UNPAIRED
        && ! (processorRef.isPlaying() && processorRef.heartbeatLive());
    if (! available)
    {
        pairPreview.reset(); pairPreviewShown = {}; pairPreviewWasFocused = false;
        nameField.setSelectionPreview ({}, 0);
        return;
    }
    const bool focused = getPeer() != nullptr && getPeer()->isFocused();
    demand = demand || (focused && ! pairPreviewWasFocused);
    pairPreviewWasFocused = focused;
    if (! pairPreview || ! processorRef.pairPreviewMatches (pairPreview.get()))
    {
        pairPreview = processorRef.createPairPreview();
        demand = true;
    }
    if (demand) hypha::pair_preview::request (pairPreview);
    KirinPairPreviewValue value {};
    if (pairPreview && kirin_hypha_pair_preview_poll (pairPreview.get(), &value)
        && value.complete && value.has_single)
    {
        const auto id = juce::String::fromUTF8 (value.candidate.instance_id);
        const auto name = juce::String::fromUTF8 (value.candidate.name);
        const auto text = "CONNECT " + (name.isNotEmpty() ? name + " / " : juce::String ("PRE "))
                        + id.substring (0, 8);
        if (nameField.setSelectionPreview (text, value.generation))
        {
            pairPreviewShown = value;
            nameField.setEnabledTooltip ("Connect this PRE: " + id + ". The arrow opens all candidates.");
            return;
        }
    }
    pairPreviewShown = {};
    nameField.setSelectionPreview ({}, 0);
    nameField.setEnabledTooltip ("Click to choose one exact PRE.");
}

void KirinHyphaEditor::selectPairPreview()
{
    const auto shown = pairPreviewShown; // Latch the actual displayed identity before any new result.
    if (! shown.has_single || nameField.paintedSelectionGeneration() != shown.generation)
    {
        showCandidateMenu();
        return;
    }
    KirinPairPreviewValue current {};
    if (! pairPreview || ! processorRef.pairPreviewMatches (pairPreview.get())
        || ! kirin_hypha_pair_preview_poll (pairPreview.get(), &current)
        || ! current.complete || ! current.has_single || current.generation != shown.generation
        || std::strcmp (current.candidate.instance_id, shown.candidate.instance_id) != 0)
    {
        refreshPairPreview (false);
        showToast ("PRE preview changed. Choose the PRE again.");
        showCandidateMenu();
        return;
    }
    const auto& candidate = shown.candidate;
    juce::Array<KirinHyphaProcessorBase::PreCandidate> chosen;
    chosen.add ({ juce::String::fromUTF8 (candidate.instance_id),
                  juce::String::fromUTF8 (candidate.name), candidate.has_name != 0 });
    // Existing command rechecks live identity, concurrent ownership and host playback.
    handleCandidateMenu (100, chosen);
    refreshPairPreview (true);
}
