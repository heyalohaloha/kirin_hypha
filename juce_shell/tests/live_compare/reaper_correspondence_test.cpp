#include "ReaperFixture.h"
using namespace reaper_fixture;
int main()
{
    require (Host::sample (0, 0) != Host::sample (65536, 0), "mono oracle distinguishes the full delay range");
    for (int channels : { 1, 2 })
    {
        Host host; host.channels = channels; host.warm();
        require (host.decision.k == 4 * frames, "K absorbs independent origins");
        const auto k = host.decision.k;
        // REAPER compensates an increase by processing upstream first, in a burst.
        for (int n = 0; n < 8; ++n) host.publish();
        for (int n = 0; n < 40; ++n) host.consume();
        require (host.decision.k == k, "reported increase keeps correspondence");
        host.gain = 0.5f;
        for (int n = 0; n < 20; ++n) host.consume();
        require (host.report.gainSettled, "MATCH gain settles on correct PRE copy");
        host.blind.start (true);
        host.postPosition += 4096; // reported decrease / bypass: projectJump in this block
        host.consume();
        require (!host.report.preAudible && !host.blind.view().invalidated, "same-block project move uses POST within the same BLIND");
        for (int n = 0; n < 30; ++n) host.consume();
        require (!host.blind.view().invalidated && host.report.preAudible, "fresh project proof automatically resumes the same BLIND");
    }
    {
        Host host; host.length = 32768;
        for (int n = 0; n < 180; ++n) host.consume();
        require (!host.report.preAudible && host.evidence.failure == LoopEntryFailure::loopTooShort,
                 "short initial loop has actionable refusal");
    }
    {
        Host host; host.length = 384000;
        for (int n = 0; n < 800; ++n) host.consume();
        require (!host.report.preAudible, "initial repeated timeline alone cannot create K");
    }
    {
        Host host; host.channels = 1; host.warm();
        host.loopStart = host.postPosition; host.length = 384000;
        for (int n = 0; n < 1600; ++n) host.consume();
        for (int n = 0; n < ReaperTimingLimits::age / frames - 1; ++n) host.publish();
        host.consume();
        require (host.report.preAudible, "known K accepts an entire block at the exact age bound");
        host.publish(); host.blind.start (true); host.consume();
        require (!host.report.preAudible && !host.blind.view().invalidated, "beyond the age bound plays POST without ending the trial");
    }
    {
        Host host; host.warm(); host.loopStart = host.postPosition; host.length = 384000;
        for (int n = 0; n < 1600; ++n) host.consume();
        require (host.report.preAudible, "known K survives measured long loops");
        // POST misses a full lap: its repeated project position looks unchanged. Peer progress
        // and the proven address's age must fence it, even with no wall-clock warning.
        for (int n = 0; n < 750; ++n) host.publish();
        host.postPosition += host.length;
        host.blind.start (true);
        host.consume();
        require (!host.report.preAudible && !host.blind.view().invalidated, "missing whole lap cannot play old PRE or clear the choice");
    }
    std::puts ("REAPER output: mono/stereo, compensated burst, MATCH, M1, BLIND, loop bound PASS");
}
