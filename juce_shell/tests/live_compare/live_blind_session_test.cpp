#include "../../src/live_compare/LiveBlindSession.h"
#include "../../src/live_compare/LiveCompareRecovery.h"
#include "../../src/HyphaLiveCompareRecoveryText.h"
#include <cstdlib>
#include <iostream>

static void require (bool value, const char* message)
{
    if (! value) { std::cerr << message << '\n'; std::exit (1); }
}

int main()
{
    using namespace hypha::live_compare;
    NamedSelection selection;
    selection.select (true);
    const auto old = selection.command();
    selection.select (true);
    selection.fail (old, RecoveryReason::ceiling);
    require (selection.command().pre() && selection.command().reason() == RecoveryReason::none,
             "old fault cannot clear or diagnose a newer PRE selection");
    const auto currentSelection = selection.command();
    selection.fail (currentSelection, RecoveryReason::nonFinite);
    selection.end (RecoveryReason::preUnavailable);
    require (! selection.command().pre() && selection.command().reason() == RecoveryReason::nonFinite,
             "first failure and selection closure are one receipt");
    selection.select (true);
    const auto beforeEnd = selection.command();
    selection.end();
    selection.fail (beforeEnd, RecoveryReason::ceiling);
    require (! selection.command().pre() && selection.command().reason() == RecoveryReason::none,
             "explicit END defeats a delayed fault without fabricating a cause");

    using namespace hypha::live_compare_ui;
    Status status;
    status.compensationOff = true;
    require (namedPresentation (status).action == RecoveryAction::none,
             "untouched inactive comparison does not announce an internal PDC condition");
    status.compensationOff = false;
    for (auto reason : { RecoveryReason::restored, RecoveryReason::formatChanged, RecoveryReason::pairChanged })
    {
        status.reason = reason;
        require (namedPresentation (status).action != RecoveryAction::endBlind
            && std::string (namedRecovery (status)).find ("END") == std::string::npos,
            "inactive unity never directs a nonexistent END");
        status.postTarget = 0.5f;
        require (namedPresentation (status).action == RecoveryAction::returnLevel, "held output directs actual RETURN");
        status.postTarget = 1.0f;
    }
    status.contentHeld = true; status.reason = RecoveryReason::none;
    require (namedPresentation (status).action == RecoveryAction::stopPlay, "current hold survives acknowledged END");
    LiveBlindStatus stopped;
    stopped.reason = RecoveryReason::contentChanged; stopped.contentHeld = true;
    require (blindRecovery (stopped, true).action == RecoveryAction::stopPlay, "invalidated content hold requires DAW stop/play");
    stopped.contentHeld = false;
    require (blindRecovery (stopped, true).action == RecoveryAction::endBlind
        && blindRecovery (stopped, true).reason == RecoveryReason::contentChanged,
        "resolved blocker changes the action, never rewrites history");
    BlindSession unavailable;
    require (! unavailable.startWith ([] () -> bool { throw 1; })
        && ! unavailable.command().active(), "CSPRNG failure never publishes a fallback assignment");
    for (bool firstPre : { false, true })
    {
        BlindSession trial;
        trial.start (firstPre);
        const auto first = trial.command();
        require (first.pre() == firstPre && first.stimulus() == 1, "both random mappings work");
        trial.observe (first, false);
        require (! trial.reveal(), "a crossfade alone is not a played source");
        trial.observe (first, true);
        require (! trial.reveal(), "one source is not enough");
        require (trial.select (2), "select second");
        const auto second = trial.command();
        require (second.pre() != firstPre, "second is the other source");
        trial.observe (first, true);
        require (trial.view().audible == 0, "late first receipt is not a second receipt");
        require (! trial.reveal(), "late receipt does not unlock reveal");
        trial.observe (second, true);
        require (trial.view().played == 3 && trial.reveal(), "both sources permit reveal without an answer");
        require (trial.view().revealed && trial.view().firstPre == firstPre, "reveal agrees with rendered mapping");
        require (! trial.reveal(), "reveal only once");
        trial.invalidate (second, RecoveryReason::callbackGap);
        trial.invalidate (second, RecoveryReason::unknown);
        require (trial.view().invalidated && ! trial.select (1), "fault after reveal cannot restart");
        require (trial.view().reason == RecoveryReason::callbackGap, "reason and invalidation are inseparable after reveal");
        trial.end();
        require (trial.view().reason == RecoveryReason::callbackGap, "teardown preserves the terminal cause");
        trial.start (! firstPre);
        trial.observe (second, true);
        trial.invalidate (second, RecoveryReason::callbackGap);
        require (trial.view().played == 0 && ! trial.view().invalidated && trial.view().reason == RecoveryReason::none,
                 "old trial cannot affect new trial or its reason");
        const auto current = trial.command();
        trial.end();
        trial.observe (current, true);
        trial.invalidate (current, RecoveryReason::ceiling);
        require (trial.view().reason == RecoveryReason::none, "END seals a clean trial against late failure");
        require (! trial.command().active() && ! trial.reveal(), "END defeats delayed audio receipts");
        trial.start (firstPre);
        require (! trial.select (0) && ! trial.select (3) && ! trial.reveal(), "bad commands are refused");
    }
    std::cout << "Live Blind session: PASS (both mappings, late receipts, faults, END, restart)\n";
}
