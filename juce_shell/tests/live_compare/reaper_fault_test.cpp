#include "ReaperFixture.h"
#include <new>
static thread_local bool audioThread = false;
static int allocations = 0, frees = 0;
void* operator new (std::size_t size)
{ if (audioThread) ++allocations; if (auto* p = std::malloc (size ? size : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t size) { return ::operator new (size); }
void operator delete (void* p) noexcept { if (audioThread) ++frees; std::free (p); }
void operator delete[] (void* p) noexcept { ::operator delete (p); }
void operator delete (void* p, std::size_t) noexcept { ::operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { ::operator delete (p); }
using namespace reaper_fixture;
int main()
{
    for (int channels : { 1, 2 })
    for (bool missingPre : { false, true })
    {
        Host h; h.channels = channels; h.warm(); h.blind.start (true);
        auto& available = missingPre ? h.preProjectAvailable : h.postProjectAvailable;
        available = false;
        audioThread = true; h.consume(); audioThread = false;
        require (! h.report.preAudible && h.blind.view().invalidated,
                 "one PRE or POST project hole immediately stops Blind in mono and stereo");
        available = true;
        for (int n = 0; n < 40; ++n)
        {
            audioThread = true; h.consume(); audioThread = false;
            require (! h.report.preAudible && h.blind.view().invalidated, "fresh proof cannot revive the trial");
            for (int c = 0; c < channels; ++c) for (int f = 0; f < frames; ++f)
                require (h.output[c][f] == Host::sample (h.postPosition - frames + f, c) * 0.5f,
                         "every recovery frame stays POST even after timing recalibrates");
        }
    }
    for (int fault = 0; fault < 5; ++fault)
    {
        Host h; h.warm(); h.blind.start (true);
        switch (fault)
        {
            case 0: h.playing = false; break;
            case 1: h.postPosition += frames; break;
            case 2: h.ring->header.pairKey.store (pair + 1); break;
            case 3: h.ring->header.timing.ownerA.fetch_add (1); break;
            case 4: h.prePosition += frames; break;
        }
        audioThread = true; h.consume(); audioThread = false;
        require (!h.report.preAudible && h.blind.view().invalidated, "stop/seek/pair/owner/PRE skip stops Blind");
    }
    {
        Host h; h.warm();
        h.now += 50000000; h.wallGap = true; h.consume();
        require (h.report.preAudible, "both continuous counters and project witnesses explain a burst pause");
        h.wallGap = false;
        ReaperCallbackContinuity local;
        auto b = h.post; local.breaks (b, false, rate);
        b.clock += frames; b.project += frames; b.wallNanos += 50000000; b.wallGap = true;
        require (!local.breaks (b, true, rate), "linear compensated burst pause has project witness");
        b.clock += frames; b.project += 2 * frames; b.wallNanos += 50000000;
        require (local.breaks (b, true, rate), "a missing block is not a burst pause");
        TimingSnapshot p; readTiming (h.ring->header.timing, p);
        ReaperPairContinuity pairCheck;
        require (pairCheck.observe (p, h.post) == TimelineBreak::none, "initial pair");
        auto post = h.post; post.clock += frames; post.project += frames; post.wallGap = true;
        require (pairCheck.observe (p, post) == TimelineBreak::callbackGap, "one side gap without peer progress is not excused");
        p.sequence += 2; // falsely advancing sequence without any processed samples
        post.clock += frames; post.wallGap = false;
        require (pairCheck.observe (p, post) == TimelineBreak::callbackGap, "sequence alone is no counter proof");
    }
    {
        Host h; h.warm(); h.checkAudio = false; h.audioOffset = -4096;
        for (int n = 0; n < 20; ++n) h.consume();
        require (h.report.preAudible && h.wrongFrames > 0,
                 "negative: an unreported delay is not detectable from unchanged clocks");
    }
    for (auto length : { ReaperTimingLimits::age - 1, ReaperTimingLimits::age, ReaperTimingLimits::age + 1 })
    {
        Host h; h.length = length;
        for (int n = 0; n < 320; ++n) h.consume();
        require (!h.report.preAudible, "no initial-loop permission from experimental bound alone");
        require (h.evidence.failure == (length <= ReaperTimingLimits::age
            ? LoopEntryFailure::loopTooShort : LoopEntryFailure::reaperDelayUnverified), "exact loop bound, including non-block boundary");
    }
    {
        Host h;
        audioThread = true;
        for (int n = 0; n < 300; ++n) h.consume();
        audioThread = false;
        require (h.renderer.historyView().kValid, "MATCH history comes from verified correspondence");
    }
    {
        Host h; h.warm(); h.loopStart = h.postPosition; h.length = 384000;
        for (int n = 0; n < 1600; ++n) h.consume();
        h.now += 4000000000; h.wallGap = true;
        h.prePosition += h.length; h.postPosition += h.length;
        h.blind.start (true); h.consume();
        require (!h.report.preAudible && h.blind.view().invalidated, "both sides' whole-lap gap is not a normal burst");
    }
    require (allocations == 0 && frees == 0, "RT alloc/free remain zero");
    std::puts ("REAPER faults: stop/seek/pair/owner/gap, bursts, negative, boundary, RT allocation PASS");
}
