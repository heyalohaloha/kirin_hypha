#include "../../src/live_compare/LiveCompareSession.h"
#include "../../src/live_compare/LiveCompareReentry.h"
#include "../../src/live_compare/LiveCompareGainApproval.h"
#include "../../src/live_compare/LiveBlindPreparation.h"
#include "../../src/live_compare/LiveCompareInterruption.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
using namespace hypha::live_compare;
static bool inRt = false;
static int rtAllocations = 0, rtDeletions = 0;
void* operator new (std::size_t bytes)
{ if (inRt) ++rtAllocations; if (auto* p = std::malloc (bytes)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t bytes) { return ::operator new (bytes); }
void operator delete (void* p) noexcept { if (inRt && p) ++rtDeletions; std::free (p); }
void operator delete[] (void* p) noexcept { ::operator delete (p); }
void operator delete (void* p, std::size_t) noexcept { ::operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { ::operator delete (p); }
static void require (bool ok, const char* why)
{ if (! ok) { std::fprintf (stderr, "named re-entry: %s\n", why); std::abort(); } }
namespace
{
constexpr int frames = 128, length = 24000, delay = 4096;
constexpr std::uint64_t key = 0x202610020123;
struct Pair
{
    std::unique_ptr<Ring> ring = std::make_unique<Ring>();
    PreFeeder feeder; TimingObserver observer; PostRenderer renderer; PostLevel postLevel;
    NamedReentry reentry; NamedSelection selection;
    std::array<std::array<float, frames>, 2> input {}, expected {}, output {};
    std::array<std::array<float, delay>, 2> physicalDelay {};
    std::int64_t emitted = 0, shift = 0;
    int head = 0, verified = 0;
    bool initialRequested = true, eligible = true;
    BlockClock pre, post;
    TimingEvidence evidence;
    Pair()
    {
        ring->initialise (key, 48000); ring->header.demand.store (1);
        renderer.prepare (frames, 48000); postLevel.configure (48000);
        selection.select (true);
    }
    static float value (std::int64_t frame, int channel)
    {
        const auto token = static_cast<std::uint32_t> (frame + 1) * 0x9e3779b1u;
        return static_cast<float> (((token >> (channel == 0 ? 0 : 16)) & 0xffffu) + 1) / 131072.0f;
    }
    BlockClock block (std::int64_t clock, bool playing, bool gap) const
    {
        BlockClock b; b.clock = clock; b.project = (clock + shift) % length;
        b.clockValid = b.projectValid = true; b.playing = playing; b.frames = frames; b.afterGap = gap;
        b.clockBasis = static_cast<std::uint8_t> (ClockBasis::vst3Continuous);
        b.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::certifiedContent);
        b.loop = { true, true, b.project / 24000.0, 0, 1, 120.0 };
        return b;
    }
    RenderReport step (bool playing = true, bool gap = false, bool missingClock = false)
    {
        pre = block (emitted, playing, gap); post = block (emitted - delay, playing, gap);
        if (missingClock) pre.clockValid = post.clockValid = false;
        for (int f = 0; f < frames; ++f)
        {
            for (int c = 0; c < 2; ++c)
            {
                input[c][f] = value (emitted + f, c);
                expected[c][f] = physicalDelay[c][head];
                physicalDelay[c][head] = input[c][f];
                output[c][f] = expected[c][f] * 0.5f;
            }
            head = (head + 1) % delay;
        }
        const float* in[] { input[0].data(), input[1].data() };
        float* io[] { output[0].data(), output[1].data() };
        inRt = true;
        feeder.feed (*ring, pre, in, 2);
        TimingSnapshot snapshot;
        require (readTiming (ring->header.timing, snapshot), "sequential clock metadata is coherent");
        evidence = observer.observe (snapshot, post, 48000, ringCapacityFrames);
        const auto command = selection.command();
        if (reentry.request (command, eligible, renderer.needsNewAdmission()))
        { renderer.renewNamedTiming(); initialRequested = true; }
        if (initialRequested && renderer.adoptInitialTiming (*ring, post, evidence)) initialRequested = false;
        const auto result = renderer.render (*ring, key, 48000, post, io, 2, command.pre(), 0.3f,
                                             postLevel, 1.0f, 1.0f);
        if (result.timelineChanged)
        {
            reentry.lost (result.loss, eligible);
            if (eligible && result.loss != TimelineBreak::metadataPending
                && ! namedTransportRestart (result.loss)) selection.fail (command, result.reason);
        }
        if (result.verdict == Verdict::accepted) reentry.accepted();
        inRt = false;
        if (result.stableSource && result.preAudible)
        {
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < frames; ++f)
                    require (output[c][f] == expected[c][f] * 0.3f, "every PRE frame matches independent delay and held gain");
            ++verified;
        }
        if (result.verdict != Verdict::accepted)
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < frames; ++f)
                    require (output[c][f] == expected[c][f] * 0.5f, "every unproven frame is exact POST");
        emitted += frames;
        return result;
    }
    void establish()
    {
        for (int n = 0; n < 500; ++n) step();
        require (verified > 400 && selection.command().pre(), "initial LOOP establishes PRE without linear playback");
    }
    void recover()
    {
        const auto before = verified;
        for (int n = 0; n < 500; ++n) step();
        require (verified > before + 300 && selection.command().pre(), "fresh proof recovers PRE without rematch or extra gesture");
    }
};
void knownTransportMovesRecover()
{
    for (bool stop : { false, true })
    {
        Pair pair; pair.establish();
        const auto old = pair.renderer.historyView();
        if (stop)
        {
            require (pair.step (false).loss == TimelineBreak::stopped, "ordinary stopped callback has an explicit cause");
            for (int n = 0; n < 20; ++n) pair.step (false);
        }
        else
        {
            pair.shift = 1234;
            require (pair.step().loss == TimelineBreak::projectMoved, "project seek is not a continuous clock failure");
        }
        pair.recover();
        require (! pair.renderer.historyStillValid (old, old.start), "old measurement history never survives re-entry");
        const auto before = pair.verified;
        for (int n = 0; n < 200 * length / frames + 1; ++n) pair.step();
        require (pair.verified > before + 37000, "held gain and exact PCM survive 200 subsequent laps");
    }
}
void unexplainedLossNeedsNewSelection()
{
    for (int fault = 0; fault < 3; ++fault)
    {
        Pair pair; pair.establish();
        if (fault == 2) pair.ring->header.timing.ownerA.fetch_add (1);
        const auto loss = pair.step (true, fault == 0, fault == 1);
        require (loss.timelineChanged && ! namedTransportRestart (loss.loss), "gap/clock/owner loss never masquerades as transport");
        require (! pair.selection.command().pre(), "the failed named command is sealed");
        const auto before = pair.verified;
        for (int n = 0; n < 500; ++n) pair.step();
        require (pair.verified == before, "fresh clocks cannot revive a sealed selection");
        pair.selection.select (true);
        pair.recover();
    }
}
void blindEndAndUnknownProducerNeverGrant()
{
    NamedReentry gate; NamedSelection selection;
    selection.select (true); gate.request (selection.command(), false, true);
    gate.lost (TimelineBreak::stopped, false);
    require (! gate.request (selection.command(), true, true), "invalidated Blind cannot revive on later clocks");
    selection.end(); gate.lost (TimelineBreak::stopped, false);
    require (! gate.request (selection.command(), false, true), "END/restore cannot grant re-entry");
    auto ring = std::make_unique<Ring>(); ring->initialise (key, 48000);
    ring->header.timing.flags.store (7); // v6 producer without typed generation cause
    TimingSnapshot snapshot;
    require (readTiming (ring->header.timing, snapshot) && snapshot.cause == TimelineBreak::unknown,
             "old protocol producer is compatible but never grants automatic recovery");
    require (! namedTransportRestart (snapshot.cause), "absence of a typed cause is not a stop certificate");
}
void knownCompensationDiffersFromUnexplainedGap()
{
    require (! callbackGapBreaksContinuity (true, ClockBasis::vst3Continuous)
        && ! callbackGapBreaksContinuity (true, ClockBasis::audioUnitRender)
        && ! callbackGapBreaksContinuity (true, ClockBasis::aaxEngine)
        && callbackGapBreaksContinuity (true, ClockBasis::pluginFrames),
        "a qualified host sample clock proves delayed callback time; unexplained local-counter holes do not");
    Pair pair; pair.establish();
    pair.renderer.revokeTiming();
    pair.eligible = false;
    pair.reentry.lost (TimelineBreak::compensationChanged, false);
    require (pair.selection.command().pre(), "known DC OFF keeps the named PRE selection");
    pair.eligible = true;
    pair.renderer.revokeTiming();
    pair.reentry.lost (TimelineBreak::compensationChanged, true);
    pair.recover();
    require (pair.step (true, true).loss == TimelineBreak::callbackGap,
             "an unexplained gap is not a compensation notification");
    require (! pair.selection.command().pre(), "unknown callback gap still seals PRE");
    LoopTimeline timeline;
    auto block = pair.block (1000, false, true);
    require (timeline.observe (block, 48000).cause == TimelineBreak::stopped,
             "a gap while the host explicitly reports STOP has a known transport cause");
    block = pair.block (1128, true, true);
    require (timeline.observe (block, 48000).cause == TimelineBreak::stopped,
             "resume after an observed STOP does not become an unexplained gap");
}
void writerOverlapCannotEraseNamedIntent()
{
    for (int boundary = 0; boundary < 3; ++boundary)
    {
        NamedReentry gate; NamedSelection selection;
        selection.select (true); gate.request (selection.command(), true, false); gate.accepted();
        // Eligibility is named output authority, NOT momentary gain snapshot readiness.
        gate.lost (TimelineBreak::projectMoved, true);
        if (boundary == 0) require (gate.waiting(), "known loss during an odd writer preserves its grant");
        require (gate.request (selection.command(), true, true), "pending seek renews once despite an odd gain writer");
        if (boundary == 1) gate.request (selection.command(), true, false);
        require (! gate.accepted (false) && gate.waiting(), "fresh timing under an odd writer does not consume approval revalidation");
        gate.request (selection.command(), true, false);
        require (gate.accepted (true) && ! gate.waiting(), "the next current coherent approval completes named recovery");
        gate.lost (TimelineBreak::projectMoved, true);
        gate.request (selection.command(), false, true);
        require (! gate.waiting(), "END/Blind/restore authority revocation still cancels a pending grant");
    }
}
void metadataPendingDoesNotInventAnUnknownOrKnownCause()
{
    require (recoveryReason (TimelineBreak::clockMissing) == RecoveryReason::clockMissing
        && recoveryReason (TimelineBreak::metadataPending) == RecoveryReason::writing,
        "source-side clock cause is projected directly; writer overlap is not a terminal interruption");
    Pair pair; pair.establish();
    Consumer consumer;
    require (consumer.adoptInitialTiming (*pair.ring, pair.post, pair.evidence), "fresh pure consumer adopts actual current proof");
    float* out[] { pair.output[0].data(), pair.output[1].data() };
    require (consumer.process (*pair.ring, key, 48000, pair.post, out, 2).verdict == Verdict::accepted,
             "control already admitted the complete current PCM");
    auto& timing = pair.ring->header.timing;
    const auto seq = timing.sequence.load(); timing.sequence.store (seq + 1);
    timing.generation.fetch_add (1);
    auto next = pair.block (pair.post.clock + frames, true, false);
    const auto pending = consumer.process (*pair.ring, key, 48000, next, out, 2);
    require (pending.timelineChanged && pending.loss == TimelineBreak::metadataPending
        && pending.verdict != Verdict::accepted, "in-progress source cause withholds all PRE, not a fabricated gap or seek");
    NamedReentry gate;
    gate.lost (pending.loss, true);
    require (! gate.waiting(), "metadata pending itself grants no fresh admission");
    const auto flags = timing.flags.load();
    timing.flags.store ((flags & 0xffu) | (static_cast<std::uint32_t> (TimelineBreak::projectMoved) << 8u));
    timing.sequence.store (seq + 2);
    const auto resolved = consumer.process (*pair.ring, key, 48000, next, out, 2);
    require (resolved.loss == TimelineBreak::projectMoved && resolved.verdict != Verdict::accepted,
             "only a later coherent typed source fact classifies the lost origin");
    gate.lost (resolved.loss, true);
    require (gate.waiting(), "a resolved known seek then owns fresh admission, not audible output");
    Pair runPair; runPair.establish(); Consumer runConsumer;
    require (runConsumer.adoptInitialTiming (*runPair.ring, runPair.post, runPair.evidence), "run control adopts current timing");
    require (runConsumer.process (*runPair.ring, key, 48000, runPair.post, out, 2).verdict == Verdict::accepted, "run control is audible");
    runPair.ring->header.run.fetch_add (1);
    const auto runNext = runPair.block (runPair.post.clock + frames, true, false);
    require (runConsumer.process (*runPair.ring, key, 48000, runNext, out, 2).loss == TimelineBreak::pcmRunChanged,
             "a PCM-only run change cannot reuse a historical generation's known stop/seek cause");
    LoopTimeline transition;
    auto linear = pair.block (0, true, false); linear.loop = {};
    transition.observe (linear, 48000);
    auto incomplete = pair.block (frames, true, false); incomplete.loop.valid = false;
    const auto invalid = transition.observe (incomplete, 48000);
    require (invalid.broken && invalid.cause == TimelineBreak::unknown && ! namedTransportRestart (invalid.cause),
             "incomplete LOOP metadata cannot masquerade as an identified project move");
}
void blindPreparationDistinguishesStartupWaitFromARevokedProof()
{
    BlindPreparation preparation;
    constexpr std::uint64_t admission = 42;
    preparation.begin (admission);
    preparation.observe (preparation.command(), false, RecoveryReason::stopped, admission);
    require (preparation.failure() == RecoveryReason::none, "unestablished startup STOP remains a wait");
    preparation.observe (preparation.command(), true, RecoveryReason::none, admission);
    const auto established = preparation.command();
    preparation.observe (established, false, RecoveryReason::clockMissing, admission);
    require (preparation.failure() == RecoveryReason::clockMissing, "once-established preparation retains its first real clock loss");
    preparation.observe (preparation.command(), true, RecoveryReason::none, admission);
    require (preparation.failure() == RecoveryReason::clockMissing, "fresh clocks do not revive invalidated preparation");
    preparation.end(); preparation.begin (admission);
    preparation.observe (established, false, RecoveryReason::callbackGap, admission);
    require (preparation.failure() == RecoveryReason::none, "old preparation epoch cannot fail a new explicit admission");
    preparation.observe (established, true, RecoveryReason::none, admission);
    preparation.observe (preparation.command(), false, RecoveryReason::stopped, admission);
    require (preparation.failure() == RecoveryReason::none, "old callback proof cannot establish the new preparation epoch");
    preparation.begin (admission);
    preparation.observe (preparation.command(), true, RecoveryReason::none, admission);
    preparation.observe (preparation.command(), false, RecoveryReason::stopped, admission);
    require (preparation.failure() == RecoveryReason::stopped, "known STOP after established preparation also ends that preparation, not its anonymous successor");
    require (preparation.failedAndOpen(), "only the open failed trial seals its own named recovery");
    preparation.end();
    require (! preparation.failedAndOpen() && preparation.failure() == RecoveryReason::stopped,
             "closed first-cause history cannot disable a later named admission");
    Interruption state;
    state.compensationOff = true;
    require (state.reason (RecoveryReason::callbackGap) == RecoveryReason::compensationOff,
             "synthetic DC reset is reported as the actual host notification");
    state.callbackGap = true;
    require (state.reason() == RecoveryReason::callbackGap, "a real simultaneous gap outranks DC notification");
    state = {}; state.contentHeld = true;
    require (state.reason() == RecoveryReason::contentChanged, "content hold is terminal after established preparation");
    preparation.begin (admission);
    preparation.observe (preparation.command(), true, state.reason(), admission);
    state = {}; state.playing = false;
    preparation.observe (preparation.command(), false, state.reason(), admission);
    require (preparation.failure() == RecoveryReason::none, "blocked startup timing is never falsely marked established");
    for (auto expected : { RecoveryReason::bypassed, RecoveryReason::offline, RecoveryReason::formatChanged,
                           RecoveryReason::outputTaken, RecoveryReason::stopped, RecoveryReason::projectClockMissing,
                           RecoveryReason::clockMissing, RecoveryReason::callbackGap, RecoveryReason::compensationOff,
                           RecoveryReason::contentChanged })
    {
        state = {};
        state.bypassed = expected == RecoveryReason::bypassed; state.offline = expected == RecoveryReason::offline;
        state.usable = expected != RecoveryReason::formatChanged; state.outputTaken = expected == RecoveryReason::outputTaken;
        state.playing = expected != RecoveryReason::stopped; state.projectValid = expected != RecoveryReason::projectClockMissing;
        state.clockValid = expected != RecoveryReason::clockMissing; state.callbackGap = expected == RecoveryReason::callbackGap;
        state.compensationOff = expected == RecoveryReason::compensationOff; state.contentHeld = expected == RecoveryReason::contentChanged;
        preparation.begin (admission); preparation.observe (preparation.command(), true, RecoveryReason::none, admission);
        preparation.observe (preparation.command(), false, state.reason(), admission);
        require (preparation.failure() == expected, "all established preparation losses use the active anonymous cause contract");
    }
}
}
int main()
{
    knownTransportMovesRecover(); unexplainedLossNeedsNewSelection(); blindEndAndUnknownProducerNeverGrant();
    knownCompensationDiffersFromUnexplainedGap();
    writerOverlapCannotEraseNamedIntent();
    metadataPendingDoesNotInventAnUnknownOrKnownCause();
    blindPreparationDistinguishesStartupWaitFromARevokedProof();
    require (rtAllocations == 0 && rtDeletions == 0, "RT renewal has zero new/delete");
    std::puts ("Named re-entry: PASS (stop/seek, 400 laps full-frame PCM, unknown/owner/gap fences, history, RT heap)");
}
