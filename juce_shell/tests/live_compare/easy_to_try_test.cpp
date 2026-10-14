#include "../../src/live_compare/LiveCompareReentry.h"
#include "../../src/live_compare/LiveBlindPreparation.h"
#include "../../src/live_compare/LiveBlindSession.h"
#include <cstdio>
#include <cstdlib>
using namespace hypha::live_compare;
static void require (bool ok, const char* message)
{ if (!ok) { std::fprintf (stderr, "%s\n", message); std::exit (1); } }
#include "EasyTimingFixture.h"
#include "../../src/live_compare/LiveCompareWaitNotice.h"
#include "../../src/live_compare/LiveCompareOffset.h"
int main()
{
    for (auto loss : { TimelineBreak::stopped, TimelineBreak::projectMoved,
          TimelineBreak::callbackGap, TimelineBreak::clockMissing, TimelineBreak::clockMoved,
          TimelineBreak::clockProofChanged, TimelineBreak::pcmRunChanged,
          TimelineBreak::loopEdited, TimelineBreak::unknown })
    {
        NamedSelection selected; selected.select (true);
        NamedReentry gate;
        gate.request (selected.command(), true, false); gate.accepted();
        gate.lost (loss, true);
        require (gate.request (selected.command(), true, true), "same authorised comparison retries fresh timing after every timing loss");
        require (selected.command().pre(), "a timing loss does not erase the user's choice");
        gate.request (selected.command(), false, true);
        require (!gate.waiting(), "END and revoked authority cancel all renewal");
    }
    for (auto loss : { RecoveryReason::stopped, RecoveryReason::clockMissing,
          RecoveryReason::callbackGap, RecoveryReason::positionChanged, RecoveryReason::writing })
    {
        BlindPreparation preparation; preparation.begin (7);
        preparation.observe (preparation.command(), true, RecoveryReason::none, 7);
        preparation.observe (preparation.command(), false, loss, 7);
        require (preparation.failure() == RecoveryReason::none, "timing loss does not terminate an accepted BLIND preparation");
        preparation.observe (preparation.command(), true, RecoveryReason::none, 7);
        require (!preparation.failedAndOpen(), "fresh timing continues the same preparation");
    }
    WaitNotice notice;
    notice.begin (1'000'000'000);
    require (!notice.prolonged (3'999'999'999), "no notice before three seconds");
    require (notice.prolonged (4'000'000'000), "long loss gets guidance at the boundary");
    notice.observe (false, 4'000'000'001);
    require (!notice.prolonged (4'000'000'002), "first recovered block clears guidance");
    require (notice.prolonged (7'000'000'001), "a host that stops calling back gets long-loss guidance");
    OffsetMonitor offsets;
    OffsetEstimate estimate;
    estimate.determined = true; estimate.sampleRate = 48000; estimate.peak = 0.4; estimate.dominance = 20;
    require (!offsets.observe (estimate, false).settled, "one content estimate grants nothing");
    require (offsets.observe (estimate, false).settled, "two strong estimates establish baseline");
    estimate.lagFrames = 96;
    require (!offsets.observe (estimate, false).jumped, "one offset jump never holds");
    require (offsets.observe (estimate, false).jumped, "two actual delay changes hold PRE");
    require (!offsets.observe (estimate, true).recovered, "a stable wrong offset cannot resume");
    estimate.lagFrames = 0; estimate.peak = 0.01;
    require (!offsets.observe (estimate, true).recovered, "weak content never recovers PRE");
    estimate.peak = 0.4;
    require (!offsets.observe (estimate, true).recovered, "first restored offset is still unproven");
    require (offsets.observe (estimate, true).recovered, "two strong original offsets recover without END");
    easy_timing::run();
    std::puts ("Easy to try: PASS (timing renewal, retained selection, preparation, authority)");
}
