#include "../../src/live_compare/LiveCompareAuthority.h"
#include "../../src/live_compare/LiveCompareCompletion.h"
#include "../../src/HyphaLiveCompareActionResult.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace hypha::live_compare;
static void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << message << '\n'; std::abort(); }
}

namespace
{
// Deterministic message-thread/RT publication order, using the same action boundary as the real
// editor. No scheduler race, timer or production test hook is needed to close each interleaving.
struct ActionFixture
{
    struct Status
    {
        bool active = true, finishing = false, matched = true, matchLimited = false;
        bool matchReady = true, interrupted = false, contentHeld = false, compensationOff = false;
        std::uint64_t sessionGeneration = 7;
        Verdict verdict = Verdict::accepted;
        RecoveryReason reason = RecoveryReason::none, observation = RecoveryReason::none;
    } status;
    bool seen = false, guard = false, ownerClosed = false, autoOn = false;
    int refreshes = 0, stopAutoAtRefresh = 0;
    std::string notice;

    bool refresh()
    {
        ++refreshes;
        if (ownerClosed)
        {
            status.active = status.matched = false;
            status.interrupted = true;
            status.reason = RecoveryReason::preUnavailable;
        }
        bool fault = guard || (status.interrupted && ! seen);
        if (fault) notice = "recovery";
        guard = false;
        seen = status.interrupted;
        if (stopAutoAtRefresh == refreshes)
        {
            autoOn = false;
            notice = "AUTO stopped";
            fault = true; // followStep may stop without changing MATCH, epoch or history
        }
        return fault;
    }
};

void actionNoticeBoundary()
{
    using namespace hypha::live_compare_ui;
    int causalCases = 0;
    for (bool match : { false, true })
    {
        ActionFixture old;
        old.ownerClosed = true; // apply/start succeeded, then ownerClosed before synchronous service
        old.refresh();
        old.notice = "success";
        require (old.seen && old.notice == "success", "negative control reproduces the old hidden new fault");
        ActionFixture fixed;
        bool applied = false;
        const auto published = performAction (ActionFaultScope::duringAction,
            [&] { applied = true; fixed.ownerClosed = true; return ActionOutcome::success; },
            [&] { return fixed.refresh(); },
            [&] { return currentNamedAction (fixed.status, 7, match); },
            [&] { fixed.notice = "success"; });
        require (applied && published == ActionCompletion::newFault && fixed.refreshes == 2 && fixed.seen
            && ! fixed.status.active && fixed.notice == "recovery",
            "successful LISTEN/MATCH cannot hide ownerClosed discovered by the closing refresh");
        ++causalCases;
    }
    ActionFixture newGuard;
    const auto autoPublished = performAction (ActionFaultScope::includingOpeningRefresh,
        [&]
        {
            newGuard.guard = newGuard.status.interrupted = true; // RT guard after action's opening refresh
            newGuard.status.reason = RecoveryReason::ceiling;
            return ActionOutcome::success;
        }, [&] { return newGuard.refresh(); }, [&] { return currentAutoAction (newGuard.status, 7); },
        [&] { newGuard.autoOn = true; newGuard.notice = "AUTO on"; });
    require (autoPublished == ActionCompletion::newFault && ! newGuard.autoOn && newGuard.seen && newGuard.notice == "recovery"
        && newGuard.status.matched && newGuard.status.sessionGeneration == 7,
        "a new guard discovered by closing refresh keeps its notice and never re-enables AUTO");
    ++causalCases;
    for (bool alreadySeen : { false, true })
    {
        ActionFixture rematch;
        rematch.status.interrupted = true;
        rematch.status.reason = RecoveryReason::ceiling;
        rematch.seen = alreadySeen;
        const auto published = performAction (ActionFaultScope::duringAction,
            [&] { rematch.status.matched = true; return ActionOutcome::success; },
            [&] { return rematch.refresh(); },
            [&] { return currentNamedAction (rematch.status, 7, true); },
            [&] { rematch.notice = "MATCH success"; });
        require (published == ActionCompletion::published && rematch.seen && rematch.notice == "MATCH success",
            "legal reMATCH succeeds after observed OR unobserved old retained ceiling history");
        ActionFixture guarded;
        guarded.status.interrupted = true;
        guarded.status.reason = RecoveryReason::ceiling;
        guarded.seen = alreadySeen;
        const auto autoPublished = performAction (ActionFaultScope::includingOpeningRefresh,
            [] { return ActionOutcome::success; }, [&] { return guarded.refresh(); },
            [&] { return currentAutoAction (guarded.status, 7); },
            [&] { guarded.autoOn = true; guarded.notice = "AUTO on"; });
        require (autoPublished != ActionCompletion::published && ! guarded.autoOn && guarded.status.matched
            && guarded.status.sessionGeneration == 7,
            "AUTO is ineligible for an unresolved guard even when MATCH and epoch never changed");
        ++causalCases;
    }
    for (int stoppedAt : { 1, 2 })
    {
        ActionFixture following;
        following.autoOn = true;
        following.stopAutoAtRefresh = stoppedAt;
        const auto published = performAction (ActionFaultScope::includingOpeningRefresh,
            [] { return ActionOutcome::success; }, [&] { return following.refresh(); },
            [&] { return currentAutoAction (following.status, 7); },
            [&] { following.autoOn = true; following.notice = "AUTO on"; });
        require (published == ActionCompletion::newFault && ! following.autoOn && following.notice == "AUTO stopped"
            && currentAutoAction (following.status, 7),
            "an opening OR closing AUTO follow stop is not undone by its own menu response");
        ++causalCases;
    }
    ActionFixture refusal;
    refusal.status.active = refusal.status.matched = false;
    refusal.status.interrupted = true;
    refusal.status.reason = RecoveryReason::pairChanged;
    require (performAction (ActionFaultScope::duringAction, [] { return ActionOutcome::refusal; },
        [&] { return refusal.refresh(); }, [] { return false; },
        [&] { refusal.notice = "PRE is multi-mono: insert it as stereo"; }) == ActionCompletion::published
        && refusal.seen && refusal.notice == "PRE is multi-mono: insert it as stereo",
        "direct refusal supersedes old ambient pair history without restoring authority");
    for (int ticks = 0; ticks < 16; ++ticks)
        require (! refusal.refresh() && refusal.notice == "PRE is multi-mono: insert it as stereo",
            "repeated refreshes cannot replay already-acknowledged history over a direct refusal");
    // Menu generation is checked AFTER opening service and BEFORE any MATCH/AUTO mutation.
    for (bool matchAgain : { false, true })
        for (bool replacedByOpeningService : { false, true })
        {
            ActionFixture scoped;
            const auto menuGeneration = scoped.status.sessionGeneration;
            if (! replacedByOpeningService) ++scoped.status.sessionGeneration; // old menu, new LISTEN/MATCH
            bool mutation = false;
            const auto published = performAction (ActionFaultScope::duringAction,
                [&]
                {
                    if (! currentNamedAction (scoped.status, menuGeneration, false)) return ActionOutcome::stale;
                    mutation = true; // measure/apply or Stop AUTO, neither may touch the replacement scope
                    return ActionOutcome::success;
                }, [&]
                {
                    if (replacedByOpeningService && scoped.refreshes == 0) ++scoped.status.sessionGeneration;
                    return scoped.refresh();
                }, [&] { return currentNamedAction (scoped.status, menuGeneration, matchAgain); },
                [&] { scoped.notice = "success"; });
            require (! mutation && published == ActionCompletion::stale && scoped.notice != "success",
                "obsolete MATCH-again OR Stop-AUTO menu cannot mutate a new session, including opening-service invalidation");
            ++causalCases;
        }
    ActionFixture sameScope;
    bool mutation = false;
    require (performAction (ActionFaultScope::duringAction,
        [&]
        {
            if (! currentNamedAction (sameScope.status, 7, false)) return ActionOutcome::stale;
            mutation = true;
            return ActionOutcome::success;
        }, [&] { return sameScope.refresh(); }, [&] { return currentNamedAction (sameScope.status, 7, true); },
        [&] { sameScope.notice = "success"; }) == ActionCompletion::published && mutation && sameScope.notice == "success",
        "same-scope menu positive control still mutates and reports its success");
    ActionFixture eligible;
    require (currentAutoAction (eligible.status, 7), "qualified current AUTO positive control");
    for (bool proofMissing : { false, true })
        for (bool newFault : { false, true })
        {
            ActionFixture waiting;
            waiting.status.matchReady = false;
            if (proofMissing) waiting.status.verdict = Verdict::calibrating;
            AutoReadiness readiness = AutoReadiness::stale;
            const auto result = performAction (ActionFaultScope::includingOpeningRefresh,
                [&]
                {
                    if (newFault) waiting.ownerClosed = true;
                    return ActionOutcome::success;
                }, [&] { return waiting.refresh(); },
                [&] { readiness = autoReadiness (waiting.status, 7); return readiness == AutoReadiness::ready; },
                [&] { waiting.autoOn = true; waiting.notice = "AUTO on"; });
            if (result == ActionCompletion::ineligible) waiting.notice = autoReadinessNotice (readiness);
            require (! waiting.autoOn && (newFault
                ? result == ActionCompletion::newFault && waiting.notice == "recovery"
                : result == ActionCompletion::ineligible && waiting.notice
                    == (proofMissing ? "AUTO not started: PRE timing pending" : "AUTO not started: MATCH pending")),
                "current AUTO receipt/proof refusal is explicit, but never hides a new terminal fault");
            ++causalCases;
        }
    for (int blocker = 0; blocker < 11; ++blocker)
    {
        auto blocked = eligible.status;
        switch (blocker)
        {
            case 0: blocked.active = false; break;
            case 1: blocked.finishing = true; break;
            case 2: ++blocked.sessionGeneration; break;
            case 3: blocked.matched = false; break;
            case 4: blocked.matchLimited = true; break;
            case 5: blocked.matchReady = false; break;
            case 6: blocked.verdict = Verdict::calibrating; break;
            case 7: blocked.interrupted = true; break;
            case 8: blocked.observation = RecoveryReason::ceiling; break;
            case 9: blocked.contentHeld = true; break;
            case 10: blocked.compensationOff = true; break;
            default: break;
        }
        require (! currentAutoAction (blocked, 7), "each current AUTO safety blocker independently refuses ON");
    }
    std::cout << "Live action boundary: PASS " << causalCases + 1
              << " causal interleavings, 11 current blockers, old-code masking negative control\n";
}
}

int main()
{
    actionNoticeBoundary();
    Authority authority;
    Completion completion;
    const auto old = authority.ticket();
    require (authority.arm (old), "explicit initial session arms");
    const auto end = completion.request();
    {
        const auto restore = authority.restoringState();
        require (! authority.permitted() && authority.restoring(), "restore revokes before parsing");
        require (! authority.arm (old) && ! authority.arm (authority.ticket()), "no admission during restore");
        {
            const auto nested = authority.restoringState();
            require (authority.generation() == 2, "overlapping restores have distinct generations");
        }
        require (authority.restoring() && ! authority.permitted(), "inner restore cannot release outer fence");
    }
    require (! authority.restoring() && ! authority.permitted(), "restore completion never resumes output");
    require (! authority.arm (old), "delayed old start cannot arm");
    require (completion.pending() && completion.command() == end, "restore preserves explicit END");
    completion.observe (end, false, false, 1.0f);
    require (completion.pending(), "an ineligible callback still cannot complete END");
    completion.observe (end, true, false, 1.0f);
    require (! completion.pending(), "normal RT completion still works after restore");
    require (authority.arm (authority.ticket()) && authority.permitted(), "new explicit session may arm");
    const auto beforePair = authority.ticket();
    require (authority.revoke() == authority.generation() && ! authority.permitted() && ! authority.restoring(),
             "explicit pair mutation revokes RT permission without requiring a service callback");
    require (! authority.arm (beforePair), "old pair start or approval cannot arm after pair mutation");
    require (authority.arm (authority.ticket()), "only a new explicit session can own the new pair epoch");

    int cases = 0;
    for (bool active : { false, true })
        for (bool reuse : { false, true })
            for (bool restoring : { false, true })
                for (bool finishing : { false, true })
                    for (bool owned : { false, true })
                        for (float actual : { 1.0f, 0.1f })
                            for (float target : { 1.0f, 0.1f })
                            {
                                const auto result = entryAdmission (reuse, active, restoring, finishing, owned, actual, target);
                                const auto expected = restoring ? StartResult::notReady
                                    : finishing ? StartResult::returnPending
                                    : ((! reuse || ! active) && (actual != 1.0f || target != 1.0f)) ? StartResult::returnRequired
                                    : owned ? StartResult::comparisonBusy : StartResult::started;
                                require (result == expected, "admission matrix disagrees");
                                ++cases;
                            }
    std::cout << "Live compare authority: PASS restore/END and " << cases << " admission cases\n";
}
