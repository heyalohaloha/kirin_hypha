#include "PluginEditor.h"
#include "HyphaSnapshotSource.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
void KirinHyphaEditor::refreshDrumSnapshots (bool liveInput)
{
    hypha::snapshots::Source source (processorRef);
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto requestedTarget = KIRIN_TARGET_DELTA;
    if (!drumNavigation) drumNavigation = std::make_unique<KirinAttackNavigationV2>();
    auto* navigation = drumNavigation.get();
    auto target = attackView.snapshotTargetV2();
    const auto navStatus = source.navigation (requestedTarget, *navigation);
    if (navStatus == KIRIN_SNAPSHOT_SUCCESS)
    {
        const auto count = std::min (navigation->count, KIRIN_ATTACK_NAVIGATION_CAPACITY);
        drumNavigationKeys.assign (navigation->events, navigation->events + count);
        attackView.setNavigationV2 (navigation->header, drumNavigationKeys, navigation->post, navigation->pre, now, liveInput && ! processorRef.isNonRealtime());
        observatoryView.setAttackPaired ((navigation->header.flags & 1u) != 0);
        target = navigation->header.target;
    }
    else if (navStatus == KIRIN_SNAPSHOT_RETIRED || navStatus == KIRIN_SNAPSHOT_UNSUPPORTED)
    {
        attackView.retireV2();
        observatoryView.setAttackPaired (false);
        nextDrumSummaryMs = 0.0;
    }
    if (attackView.band() != 0 && attackView.presentationSnapshotV2().live && now >= nextDrumSummaryMs)
    {
        nextDrumSummaryMs = now + 250.0;
        if (!drumSummary) drumSummary = std::make_unique<KirinAttackBandSummaryV2>();
        auto* summary = drumSummary.get();
        if (source.summary (target, attackView.band(), *summary) == KIRIN_SNAPSHOT_SUCCESS)
            attackView.setSummarySnapshotV2 (*summary, juce::Time::getMillisecondCounterHiRes());
    }
    const auto token = attackView.singleRequestToken();
    if (token != 0)
    {
        KirinAttackSingleSnapshotV2 single {};
        if (source.single (token, single) == KIRIN_SNAPSHOT_SUCCESS)
            attackView.setSingleSnapshotV2 (single, juce::Time::getMillisecondCounterHiRes());
    }
    attackView.presentationTick (liveInput);
}
#endif
