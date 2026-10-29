#include "HyphaPairLabels.h"
#include "AutomaticPairNotice.h"
#include "PluginEditor.h"
#include <cstring>

namespace
{
constexpr double firstRetrySeconds = 1.05; // The worker admits at most one demand per second.
constexpr double maximumRetrySeconds = 8.0;
constexpr double resultWatchdogSeconds = 2.0;
constexpr double exactCandidateRefreshSeconds = 8.0;
}

void KirinHyphaEditor::refreshPairPreview (bool demand)
{
    const auto now = nowSecs();
    if (! demand && isShowing() && now - pairPreviewRefreshAt < 0.1) return;
    pairPreviewRefreshAt = now;
    const auto available = isPost && isShowing() && nameField.isVisible()
        && processorRef.pairStatus() == KIRIN_PAIR_STATUS_UNPAIRED;
    if (! available)
    {
        pairPreview.reset(); pairPreviewShown = {}; pairPreviewWasFocused = false;
        pairPreviewNextDemandAt = 0;
        pairPreviewRetrySeconds = firstRetrySeconds;
        pairPreviewObservedGeneration = 0;
        nameField.setSelectionPreview ({}, 0);
        if (isPost) nameField.setEnabledTooltip (hypha::pair_labels::choose);
        return;
    }
    const bool focused = getPeer() != nullptr && getPeer()->isFocused();
    demand = demand || (focused && ! pairPreviewWasFocused);
    pairPreviewWasFocused = focused;
    if (! pairPreview || ! processorRef.pairPreviewMatches (pairPreview.get()))
    {
        pairPreview = processorRef.createPairPreview();
        pairPreviewShown = {};
        pairPreviewNextDemandAt = 0;
        pairPreviewRetrySeconds = firstRetrySeconds;
        pairPreviewObservedGeneration = 0;
        nameField.setSelectionPreview ({}, 0);
        demand = true;
    }

    // Discovery is advisory and stays off the audio thread. An empty first scan is common while
    // a newly-created PRE is still publishing its identity, so retry with bounded backoff rather
    // than making the user reopen the selector. A known exact candidate is checked occasionally;
    // the final pairing command always rechecks identity and ownership.
    if (pairPreview && (demand || (pairPreviewNextDemandAt > 0 && now >= pairPreviewNextDemandAt)))
    {
        pairPreviewNextDemandAt = now + (hypha::pair_preview::request (pairPreview)
            ? resultWatchdogSeconds : firstRetrySeconds);
    }

    KirinPairPreviewValue value {};
    if (pairPreview && kirin_hypha_pair_preview_poll (pairPreview.get(), &value))
    {
        if (value.generation != pairPreviewObservedGeneration)
        {
            pairPreviewObservedGeneration = value.generation;
            if (value.complete && value.has_single)
            {
                const auto id = juce::String::fromUTF8 (value.candidate.instance_id);
                const auto name = juce::String::fromUTF8 (value.candidate.name);
                if (processorRef.tryAutomaticPair (pairPreview.get(), value))
                {
                    pairPreview.reset();
                    pairPreviewShown = {};
                    comparisonActionAwaitingResult = true;
                    comparisonActionAfterGeneration = comparisonObservedGeneration;
                   #if ! KIRIN_HYPHA_PRE_DISPLAY
                    spectrumView.clearSnapshot();
                    perceptualView.clearSnapshot();
                    absoluteView.clearSnapshot();
                   #endif
                    nameField.setModelName (processorRef.pairDisplayName());
                    showToast ("Paired with PRE  " + (name.isNotEmpty() ? name : id.substring (0, 8)));
                    automaticPairNotice = true;
                    automaticPairNoticeId = id;
                    updateFeedback (nowSecs(), false, {});
                    return;
                }
                const auto text = hypha::pair_labels::candidateLabel (name, id);
                if (nameField.setSelectionPreview (text, value.generation))
                {
                    pairPreviewShown = value;
                    pairPreviewRetrySeconds = firstRetrySeconds;
                    pairPreviewNextDemandAt = now + exactCandidateRefreshSeconds;
                    nameField.setEnabledTooltip (
                        hypha::pair_labels::candidateTooltip (name, id));
                    return;
                }
            }

            pairPreviewShown = {};
            nameField.setSelectionPreview ({}, 0);
            pairPreviewNextDemandAt = now + pairPreviewRetrySeconds;
            pairPreviewRetrySeconds = juce::jmin (
                maximumRetrySeconds, pairPreviewRetrySeconds * 2.0);
        }
    }

    // Keep a displayed exact receipt while its non-blocking refresh is pending. A click still
    // goes through the authoritative pairing command, which fails closed if the PRE disappeared.
    if (! pairPreviewShown.has_single)
        nameField.setEnabledTooltip (hypha::pair_labels::choose);
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
    if (pairPreview && processorRef.pairPreviewMatches (pairPreview.get())
        && ! kirin_hypha_pair_preview_poll (pairPreview.get(), &current))
    {
        refreshPairPreview (true);
        showCandidateMenu();
        return;
    }
    if (! pairPreview || ! processorRef.pairPreviewMatches (pairPreview.get())
        || ! current.complete || ! current.has_single || current.generation != shown.generation
        || std::strcmp (current.candidate.instance_id, shown.candidate.instance_id) != 0)
    {
        refreshPairPreview (false);
        showToast (hypha::pair_labels::changed);
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

void KirinHyphaEditor::unpairAutomaticNotice()
{
    hypha::automatic_pair::unpairNotice (
        [this] { return ! (processorRef.isPlaying() && processorRef.heartbeatLive())
            && processorRef.pairedPreInstanceId() == automaticPairNoticeId; },
        [this] { handleCandidateMenu (11, {}); },
        [this] { return processorRef.pairStatus() == KIRIN_PAIR_STATUS_UNPAIRED; },
        [this] { showToast ("Unpaired"); });
}
