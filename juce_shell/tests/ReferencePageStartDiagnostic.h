#pragma once
#include "ReferenceVersionFormatFixture.h"
#include "ReferencePageWaitTestAccess.h"
#include "reference_rt_probe.h"

enum class PageWaitChange { none, transportStop, newSelection, negative, atEnd, pastEnd, slowPages, readFailure, sourceGone, seekPublication, beforeAlignment };
namespace hypha::tests::reference_control
{
struct PageIntentFacts { std::uint64_t active = 0, intent = 0, generation = 0; int switchingSlot = 0; };
struct PageIntentTag {};
PageIntentFacts pageIntentFactsFor (Comparison&, PageIntentTag);
template <auto Lock, auto Active, auto Pending, auto Switch> struct PageIntentAccess
{
    friend PageIntentFacts pageIntentFactsFor (Comparison& c, PageIntentTag)
    {
        const juce::ScopedLock lock (c.*Lock);
        const auto& pending = c.*Pending;
        return { (c.*Active).load (std::memory_order_acquire), pending.intentId,
                 pending.startGeneration, (c.*Switch).load (std::memory_order_acquire) };
    }
};
template struct PageIntentAccess<&Comparison::selectionLock, &Comparison::activePendingIntent,
    &Comparison::pendingAudition, &Comparison::switchSlot>;
}

#include "ReferencePageWaitFailureTest.h"
#include "ReferencePageSeekPublicationTest.h"

inline void testReferencePageStart (const juce::File& sandbox, bool switching, bool approvalDiagnostic = false, bool cancelWait = false, bool startBoundary = false, PageWaitChange change = PageWaitChange::none)
{
    namespace control = hypha::tests::reference_control;
    const auto suffix = cancelWait ? "-cancel" : startBoundary ? "-boundary"
        : int (change) >= int (PageWaitChange::negative) ? "-audit-" + juce::String (int (change))
        : change == PageWaitChange::transportStop ? "-stop" : change == PageWaitChange::newSelection ? "-replace" : "";
    const auto root = sandbox.getChildFile (juce::String (switching ? "page-switch" : "page-first") + suffix);
    require (root.createDirectory().wasOk(), "isolated page-start fixture");
    const auto base = makeWholeSongFixture (root, root.getChildFile ("input.wav"),
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333");
    juce::Array<juce::var> entries;
    juce::StringArray ids;
    juce::var manifest;
    for (int n = 1; n <= 2; ++n)
    {
        manifest = hypha::tests::version_formats::publish (root, base.audio, 48000, false, 0, n);
        const auto entry = manifest["versions"][0];
        entries.add (entry);
        const auto id = entry["entry_id"].toString();
        ids.add (id + "/" + id + "/" + id);
    }
    manifest.getDynamicObject()->setProperty ("versions", entries);
    require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "publish two immutable versions");
    bool allowOutput = true;
    ref::AudioPages* retiringPages = nullptr;
    bool retireOnDecision = false;
    ref::ReferenceComparisonController c (root, [&] (bool selected) { return !selected || allowOutput; }, {}, {}, [&]
    {
        if (retireOnDecision)
        {
            retireOnDecision = false;
            require (retiringPages && control::retirePagesFor (*retiringPages, {}), "retire at the activation boundary");
        }
        return hypha::output_owner::States { 0 };
    });
    control::stopWorkers (c);
    c.configure ({ "page-start-post", {}, 42, true }, 48000, 2);
    control::service (c);
    if (change == PageWaitChange::beforeAlignment)
    { testReferencePageWaitBeforeAlignment (c, root, ids[0], base.audio); return; }
    require (c.selectVersion (ids[0]), "choose initial version");
    c.setPresented (true); control::service (c);
    if (!switching && !approvalDiagnostic) require (c.requestAudition (1, -22, -6), "queue first V while stopped");
    juce::AudioBuffer<float> input (2, 256), original (2, 256);
    int cursor = 0;
    const int hostOffset = change == PageWaitChange::seekPublication ? 192256 : 0;
    const auto feed = [&] (bool render, bool workspace)
    {
        for (int ch = 0; ch < 2; ++ch) input.copyFrom (ch, 0, base.audio, ch, cursor, 256);
        if (approvalDiagnostic) input.applyGain (4.0f);
        original.makeCopyOf (input, true);
        beginReferenceRtProbe();
        c.observeTransport (cursor + hostOffset, true, true);
        c.observeAInput (input, cursor + hostOffset, true, true, true);
        const bool sounded = render && c.renderSelectedB (input, cursor + hostOffset, true, true, true);
        require (endReferenceRtProbe() == 0, "page-start callback allocates nothing");
        if (!sounded) for (int ch = 0; ch < 2; ++ch)
            require (std::memcmp (input.getReadPointer (ch), original.getReadPointer (ch), 256 * sizeof (float)) == 0,
                     "waiting preserves A bit for bit");
        cursor += 256;
        if (workspace) control::service (c);
        return sounded;
    };
    while (cursor < 192000) feed (false, true);
    require (c.snapshot().versionReady, "real capture and calibration are ready");
    if (approvalDiagnostic)
    {
        require (!c.requestAudition (1, -22, -6), "louder live A requires explicit approval for V");
        const auto role = *c.snapshot().versionSelection;
        require (role.matchFailure == ref::MatchFailure::ceilingExceeded, "real V MATCH creates a ceiling offer");
        const ref::LowerAOffer shown { 1, role.neededAttenuationDb, role.playbackIdentity,
                                      role.selectionGeneration, role.matchFailureSerial };
        require (!c.requestAudition (1, -22, -6), "a later V MATCH fails at the same ceiling");
        const auto updated = *c.snapshot().versionSelection;
        const auto stale = c.approveLowerAAndPlay (shown);
        require (stale == ref::LowerAApproval::stale && c.heldAttenuationDb() == 0.0,
                 "old offer is refused without attenuating A");
        const ref::LowerAOffer fresh { 1, updated.neededAttenuationDb, updated.playbackIdentity,
                                      updated.selectionGeneration, updated.matchFailureSerial };
        const auto accepted = c.approveLowerAAndPlay (fresh);
        std::cout << "APPROVAL_IDENTITY oldSerial=" << shown.failureSerial << " newSerial=" << fresh.failureSerial
                  << " generationSame=" << (shown.selectionGeneration == fresh.selectionGeneration)
                  << " identitySame=" << (shown.playbackIdentity == fresh.playbackIdentity)
                  << " staleResult=" << int (stale) << " freshResult=" << int (accepted)
                  << " heldDb=" << c.heldAttenuationDb() << std::endl;
        require (accepted == ref::LowerAApproval::lowered, "current V offer is accepted");
        c.selectA(); return;
    }
    if (switching)
    {
        require (c.requestAudition (1, -22, -6) && feed (true, true), "positive control actually hears initial V");
        require (c.selectVersion (ids[1]), "switch while V is audible");
        for (int n = 0; n < 4; ++n) feed (true, true);
        require (c.snapshot().selectedVersionId == ids[1] && c.snapshot().versionReady,
                 "replacement publication and calibration are ready before pending start");
    }
    auto& runtime = control::roleFor (c, { 1 });
    auto& pages = control::currentPagesFor (runtime, {});
    control::stopPageRefill (pages);
    require (control::retirePagesFor (pages, {}) && !pages.readyAt (cursor, 256),
             "current publication pages are genuinely absent");
    if (change == PageWaitChange::seekPublication)
    { testReferencePageSeekPublication (c, runtime, pages, base.audio, cursor, switching); return; }
    if (int (change) >= int (PageWaitChange::negative))
    { testReferencePageWaitFailure (c, runtime, pages, root, base.audio, cursor, switching, change); return; }
    const auto before = c.snapshot();
    const auto generation = runtime.normalSelectionTicket();
    const auto identity = before.versionSelection->playbackIdentity;
    require (!before.versionSelection->auditionBuffered, "calibration alone cannot make an absent page buffered");
    require (runtime.startB (-22, -6, generation, identity) == ref::RuntimeV2Controller::StartResult::preparing,
             "typed activation reports a transient page wait");
    require (runtime.normalSelectionTicket() == generation, "page wait keeps the selection generation");
    if (startBoundary)
    {
        control::servicePages (pages, {});
        require (c.snapshot().versionSelection->auditionBuffered, "a ready snapshot precedes the activation race");
        retiringPages = &pages; retireOnDecision = true;
    }
    c.servicePendingAudition (-22, -6, true); // No synchronous workspace/page service here.
    require (!retireOnDecision, "the activation boundary was exercised");
    const auto failed = c.snapshot();
    const bool waiting = failed.pendingAudition.waiting() && c.pendingAuditionNeedsService();
    const auto intent = control::pageIntentFactsFor (c, {});
    require (intent.active != 0 && intent.active == intent.intent && intent.switchingSlot == (switching ? 1 : 0),
             "the current explicit intent and switch stay owned during a page wait");
    require (!feed (true, false), "absent page cannot output V");
    for (int n = 0; n < 3; ++n) c.servicePendingAudition (-22, -6, true);
    require (c.snapshot().pendingAudition.waiting() && runtime.normalSelectionTicket() == generation,
             "repeated service retains the intent and generation while pages are absent");
    const auto repeated = control::pageIntentFactsFor (c, {});
    require (repeated.active == intent.active && repeated.intent == intent.intent
                 && repeated.generation == intent.generation && repeated.switchingSlot == intent.switchingSlot,
             "service cannot consume and recreate an explicit intent while it waits");
    if (change == PageWaitChange::transportStop)
    {
        c.observeTransport (cursor, true, false);
        c.servicePendingAudition (-22, -6, false);
        const auto stopped = c.snapshot();
        require (switching ? stopped.pendingAudition.waiting()
                           : stopped.pendingAudition.stage == ref::PendingAuditionView::Stage::safetyChanged,
                 "a stopped switch stays armed and a first-play safety cancellation stays cancelled");
        c.observeTransport (cursor, true, true);
    }
    bool supplied = false, resumed = false;
    if (change == PageWaitChange::newSelection)
    {
        const auto newest = ids[switching ? 0 : 1];
        require (c.selectVersion (newest), "choose another version while the previous intent waits");
        require (runtime.startB (-22, -6, generation, identity) == ref::RuntimeV2Controller::StartResult::stale
                     && !feed (true, false), "the superseded attempt cannot start the old publication");
        for (int n = 0; n < 8 && !c.snapshot().bSelected; ++n)
        { feed (true, true); c.servicePendingAudition (-22, -6, true); }
        supplied = control::currentPagesFor (runtime, {}).readyAt (cursor, 256);
        resumed = feed (true, false);
        const auto newestIntent = control::pageIntentFactsFor (c, {});
        require (c.snapshot().selectedVersionId == newest && newestIntent.intent > intent.intent,
                 "only a new intent for the latest explicit choice can take the output");
    }
    else
    {
        if (cancelWait) c.selectA();
        control::servicePages (pages, {});
        supplied = pages.readyAt (cursor, 256);
        c.servicePendingAudition (-22, -6, true);
        resumed = feed (true, false);
    }
    const bool expectedResume = !cancelWait && (change != PageWaitChange::transportStop || switching);
    const auto finishedIntent = control::pageIntentFactsFor (c, {});
    require (finishedIntent.active == 0 && finishedIntent.switchingSlot == 0,
             "success or cancellation consumes the pending intent and switch exactly once");
    std::cout << "PAGE_START switching=" << switching << " cancelled=" << cancelWait << " boundary=" << startBoundary << " change=" << int (change) << " eligible=" << before.versionSelection->blindEligible
              << " buffered=" << before.versionSelection->auditionBuffered
              << " stageBefore=" << int (before.pendingAudition.stage)
              << " stageAfter=" << int (failed.pendingAudition.stage)
              << " matchFailure=" << int (failed.versionSelection->matchFailure)
              << " pending=" << waiting << " supplied=" << supplied << " resumed=" << resumed << std::endl;
    c.selectA();
    runtime.selectA (false);
    require (waiting && failed.pendingAudition.stage != ref::PendingAuditionView::Stage::startFailed,
             "a page wait must preserve the pending explicit start instead of failing");
    require (supplied && resumed == expectedResume, "refill resumes an active intent and cannot revive a cancelled intent");
    require (runtime.startB (-22, -6, generation, identity) == ref::RuntimeV2Controller::StartResult::stale,
             "a cancelled generation cannot start even after refill");
    allowOutput = false;
    const auto gateIdentity = runtime.snapshot().playbackIdentity;
    require (runtime.startB (-22, -6, runtime.normalSelectionTicket(), gateIdentity)
                 == ref::RuntimeV2Controller::StartResult::refused
                 && runtime.snapshot().matchFailure == ref::MatchFailure::none,
             "an output gate refusal is final even without a MATCH failure");
    require (!c.requestAudition (1, -22, -6) && !c.pendingAuditionNeedsService(),
             "a final refusal with no MATCH failure cannot become an automatic retry");
}
