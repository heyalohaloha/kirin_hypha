#include "../../src/live_compare/LiveCompareSession.h"
#include "../../src/live_compare/LiveCompareSharedRing.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

using namespace hypha::live_compare;
namespace
{
void require (bool ok, const char* why)
{
    if (! ok) { std::fprintf (stderr, "clock preparation: %s\n", why); std::abort(); }
}
}
#include "LiveCompareOwnerLifetimeTest.h"
namespace
{
constexpr std::uint64_t pair = 0x202610012222;
constexpr std::uint32_t rate = 48000;
constexpr int frames = 128;
enum class ClockMode { independent, certifiedContent, presentationAu, boundedAax };

struct Fixture
{
    std::unique_ptr<Ring> ring = std::make_unique<Ring>();
    PreFeeder feeder;
    TimingObserver observer;
    std::int64_t emitted = 0, loopStart = 0;
    int length = 24000, delay = 4096;
    ClockMode clockMode = ClockMode::independent;
    bool looping = false;
    std::array<std::array<float, frames>, 2> input {}, expected {}, scratch {};
    std::array<std::vector<float>, 2> physicalDelay;
    std::size_t head = 0;
    BlockClock pre, post;
    TimingEvidence evidence;

    explicit Fixture (int loopLength = 24000, int physicalLatency = 4096,
                      ClockMode mode = ClockMode::independent)
        : length (loopLength), delay (physicalLatency), clockMode (mode)
    {
        ring->initialise (pair, rate);
        for (auto& channel : physicalDelay) channel.resize (static_cast<std::size_t> (delay));
        for (auto& sample : ring->samples) sample.store (-0.125f);
    }
    static float original (std::int64_t emission, int channel)
    {
        const auto token = static_cast<std::uint32_t> (emission + 1) * 0x9e3779b1u;
        return static_cast<float> (((token >> (channel == 0 ? 0 : 16)) & 0xffffu) + 1) / 131072.0f;
    }
    std::int64_t project (std::int64_t emission) const
    {
        return looping && emission >= loopStart + length
            ? loopStart + (emission - loopStart) % length : emission;
    }
    BlockClock block (std::int64_t emission, std::int64_t origin) const
    {
        BlockClock value;
        value.clock = emitted + origin; value.project = project (emission); value.frames = frames;
        value.clockValid = value.projectValid = value.playing = true;
        if (looping) value.loop = { true, true, static_cast<double> (value.project) / 24000.0,
            static_cast<double> (loopStart) / 24000.0,
            static_cast<double> (loopStart + length) / 24000.0, 120.0 };
        return value;
    }
    void enableLoop() { loopStart = emitted; looping = true; }
    void step (bool snapshotReady = true)
    {
        pre = block (emitted, 10000);
        post = block (emitted - delay, 777000);
        if (clockMode == ClockMode::certifiedContent)
        {
            pre.clock = emitted;
            post.clock = emitted - delay;
            pre.clockBasis = post.clockBasis = static_cast<std::uint8_t> (ClockBasis::vst3Continuous);
            pre.clockAuthority = post.clockAuthority
                = static_cast<std::uint8_t> (ClockAuthority::certifiedContent);
        }
        else if (clockMode == ClockMode::boundedAax)
        {
            pre.clock = post.clock = emitted;
            pre.clockBasis = post.clockBasis = static_cast<std::uint8_t> (ClockBasis::aaxEngine);
            pre.clockAuthority = post.clockAuthority
                = static_cast<std::uint8_t> (ClockAuthority::boundedAaxEngine);
            pre.maximumDelaySamples = post.maximumDelaySamples = 16383;
        }
        else if (clockMode == ClockMode::presentationAu)
        {
            pre.clockBasis = post.clockBasis = static_cast<std::uint8_t> (ClockBasis::audioUnitRender);
            pre.presentationSource = post.presentationSource = 2;
            pre.outputPresentationValid = post.outputPresentationValid = true;
            pre.outputPresentationSamples = static_cast<std::uint32_t> (delay);
            post.outputPresentationSamples = 0;
        }
        for (int f = 0; f < frames; ++f)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float current = original (emitted + f, c);
                input[static_cast<std::size_t> (c)][static_cast<std::size_t> (f)] = current;
                expected[static_cast<std::size_t> (c)][static_cast<std::size_t> (f)] = delay == 0 ? current
                    : physicalDelay[static_cast<std::size_t> (c)][head];
                if (delay != 0) physicalDelay[static_cast<std::size_t> (c)][head] = current;
            }
            if (delay != 0) head = (head + 1) % static_cast<std::size_t> (delay);
        }
        const float* source[] { input[0].data(), input[1].data() };
        feeder.feed (*ring, pre, source, 2);
        TimingSnapshot snapshot;
        require (readTiming (ring->header.timing, snapshot), "sequential metadata read is coherent");
        evidence = snapshotReady ? observer.observe (snapshot, post, rate, ringCapacityFrames)
                                 : observer.unavailable (post, rate, ringCapacityFrames);
        emitted += frames;
    }
    Decision read (Consumer& consumer)
    {
        float* out[] { scratch[0].data(), scratch[1].data() };
        return consumer.process (*ring, pair, rate, post, out, 2);
    }
    void checkPcm() const
    {
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < frames; ++f)
                require (scratch[static_cast<std::size_t> (c)][static_cast<std::size_t> (f)]
                    == expected[static_cast<std::size_t> (c)][static_cast<std::size_t> (f)],
                    "every accepted PRE frame matches the independent physical delay line");
    }
};

void prepareWithoutCopyingPcm()
{
    unsigned accepted = 0, cases = 0;
    for (int length : { 6000, 24000, 192000 })
    for (int delay : { 0, 64, 4096 })
    {
        Fixture fixture (length, delay);
        for (int n = 0; n < 100; ++n) fixture.step();
        require (fixture.evidence.valid && fixture.evidence.k == 767000 + delay,
                 "idle preparation establishes the independent clock offset");
        fixture.enableLoop();
        for (int n = 0; n < 1500; ++n) fixture.step();
        require (fixture.evidence.valid, "normal loop retains the earlier linear clock evidence before a click");
        require (fixture.ring->header.published.load() == 0 && fixture.ring->header.writeEnd.load() == 0,
                 "clock preparation never claims a PCM block was published");
        for (const auto& sample : fixture.ring->samples)
            require (sample.load() == -0.125f, "no-demand preparation leaves every PCM slot untouched");
        fixture.ring->header.demand.store (1);
        fixture.step();
        Consumer consumer;
        require (consumer.adoptInitialTiming (*fixture.ring, fixture.post, fixture.evidence),
                 "explicit session can adopt the current, generation-bound initial evidence");
        const auto first = fixture.read (consumer);
        require (delay == 0 ? first.verdict == Verdict::accepted : first.verdict != Verdict::accepted,
                 "known offset must not manufacture unwritten earlier PCM");
        for (int n = 0; n < 1600; ++n)
        {
            fixture.step();
            const auto decision = fixture.read (consumer);
            if (n * frames < delay) continue;
            require (decision.verdict == Verdict::accepted, "after PCM arrives every normal loop block is accepted");
            fixture.checkPcm(); ++accepted;
        }
        ++cases;
    }
    std::printf ("idle linear -> loop -> explicit audition: %u cases, %u accepted full-frame blocks\n", cases, accepted);
}

void initialLoopDoesNotInventAnOrigin()
{
    Fixture fixture;
    fixture.enableLoop();
    for (int n = 0; n < 1500; ++n)
    {
        fixture.step();
        require (! fixture.evidence.valid, "initial LOOP alone never manufactures a linear-origin proof");
    }
}

#include "LiveComparePendingEntryTest.h"

void certifiedInitialLoopStartsWithoutAnotherGesture()
{
    LoopAnchor noCycleYet;
    BlockClock vstPre, auPost;
    vstPre.clockBasis = static_cast<std::uint8_t> (ClockBasis::vst3Continuous);
    auPost.clockBasis = static_cast<std::uint8_t> (ClockBasis::audioUnitRender);
    const auto mixed = initialLoopCandidate (noCycleYet, vstPre, auPost, 0, rate, ringCapacityFrames);
    require (! mixed.valid && mixed.failure == LoopEntryFailure::clockUnavailable,
             "different explicit wrapper clocks fail immediately instead of waiting another lap");

    Fixture delayedAdoption (24000, 4096, ClockMode::certifiedContent);
    delayedAdoption.enableLoop();
    delayedAdoption.ring->header.demand.store (1);
    Consumer waiting;
    for (int n = 0; n < 160; ++n)
    {
        delayedAdoption.step();
        require (delayedAdoption.read (waiting).verdict != Verdict::accepted,
                 "initial LOOP remains on POST until its certified evidence is adopted");
    }
    require (delayedAdoption.evidence.valid
             && waiting.adoptInitialTiming (*delayedAdoption.ring, delayedAdoption.post,
                                            delayedAdoption.evidence),
             "expected initial LOOP wraps cannot race and close the pending admission");
    delayedAdoption.step();
    require (delayedAdoption.read (waiting).verdict == Verdict::accepted,
             "the same pending entry starts after delayed certified adoption without another gesture");
    delayedAdoption.checkPcm();

    for (const auto mode : { ClockMode::certifiedContent, ClockMode::presentationAu,
                             ClockMode::boundedAax })
    {
        Fixture fixture (24000, 4096, mode);
        fixture.enableLoop();
        fixture.ring->header.demand.store (1);
        Consumer consumer;
        unsigned accepted = 0;
        for (int n = 0; n < 800; ++n)
        {
            fixture.step();
            if (! consumer.calibrated())
                consumer.adoptInitialTiming (*fixture.ring, fixture.post, fixture.evidence);
            const auto decision = fixture.read (consumer);
            if (decision.verdict == Verdict::accepted)
            {
                fixture.checkPcm();
                ++accepted;
            }
        }
        require (accepted > 500, "certified initial LOOP automatically reaches verified PRE");
        const auto expected = mode == ClockMode::certifiedContent ? 0
                            : mode == ClockMode::presentationAu ? 771096 : 4096;
        require (consumer.offset() == expected,
                 "the certified clock model derives the independent physical delay");
    }

    Fixture shortLoop (6000, 4096, ClockMode::boundedAax);
    shortLoop.enableLoop();
    for (int n = 0; n < 400; ++n) shortLoop.step();
    require (! shortLoop.evidence.valid
        && shortLoop.evidence.failure == LoopEntryFailure::loopTooShort,
        "AAX rejects a loop shorter than the host's documented delay bound");

    Fixture changedProof (24000, 4096, ClockMode::presentationAu);
    changedProof.enableLoop();
    changedProof.ring->header.demand.store (1);
    Consumer admitted;
    for (int n = 0; n < 400; ++n)
    {
        changedProof.step();
        if (! admitted.calibrated())
            admitted.adoptInitialTiming (*changedProof.ring, changedProof.post, changedProof.evidence);
        changedProof.read (admitted);
    }
    require (admitted.calibrated(), "presentation proof admits the initial AU loop");
    changedProof.delay = 2048;
    changedProof.step();
    require (! changedProof.evidence.valid,
             "a presentation-latency change fences the old initial-loop proof");
}

void faultsFenceEvidence()
{
    using Mutate = void (*) (TimingSnapshot&, BlockClock&);
    const Mutate cases[] {
        [] (TimingSnapshot& a, BlockClock&) { a.active = false; },
        [] (TimingSnapshot& a, BlockClock&) { ++a.generation; },
        [] (TimingSnapshot& a, BlockClock&) { ++a.ownerA; },
        [] (TimingSnapshot& a, BlockClock&) { ++a.ownerB; },
        [] (TimingSnapshot&, BlockClock& b) { b.playing = false; },
        [] (TimingSnapshot&, BlockClock& b) { b.afterGap = true; },
        [] (TimingSnapshot&, BlockClock& b) { b.clockValid = false; },
        [] (TimingSnapshot&, BlockClock& b) { b.projectValid = false; },
        [] (TimingSnapshot&, BlockClock& b) { b.clock += 24000; },
        [] (TimingSnapshot&, BlockClock& b) { b.project += 123; },
        [] (TimingSnapshot&, BlockClock& b) { b.loop.end += 1; },
        [] (TimingSnapshot&, BlockClock& b) { b.loop.bpm += 1; }
    };
    for (auto mutate : cases)
    {
        Fixture fixture;
        for (int n = 0; n < 100; ++n) fixture.step();
        fixture.enableLoop(); fixture.step();
        TimingSnapshot snapshot;
        require (readTiming (fixture.ring->header.timing, snapshot), "fault fixture metadata");
        auto post = fixture.post; post.clock += frames; post.project += frames;
        post.loop.ppq += static_cast<double> (frames) / 24000.0;
        snapshot.block.clock += frames;
        mutate (snapshot, post);
        require (! fixture.observer.observe (snapshot, post, rate, ringCapacityFrames).valid,
                 "a discontinuity cannot be adopted as current evidence");
    }
    Fixture fixture;
    for (int n = 0; n < 100; ++n) fixture.step();
    fixture.ring->header.demand.store (1); fixture.step();
    const auto old = fixture.evidence;
    Consumer closed;
    fixture.ring->header.ownerClosed.store (1);
    require (! closed.adoptInitialTiming (*fixture.ring, fixture.post, old), "closed PRE rejects cached timing");
    fixture.ring->header.ownerClosed.store (0);
    Consumer other;
    fixture.ring->header.timing.ownerA.fetch_add (1);
    require (! other.adoptInitialTiming (*fixture.ring, fixture.post, old), "another owner lifetime rejects cached timing");
    Consumer newer;
    fixture.ring->header.timing.ownerA.fetch_sub (1);
    fixture.ring->header.timing.generation.fetch_add (1);
    require (! newer.adoptInitialTiming (*fixture.ring, fixture.post, old), "new PRE generation rejects cached timing");
    fixture.ring->header.timing.sequence.fetch_add (1);
    TimingSnapshot torn;
    require (! readTiming (fixture.ring->header.timing, torn), "writer-in-progress metadata is not a snapshot");
}

void timingMappingDoesNotOwnAuditionDemand()
{
    SharedRingMapping owner, audition, timing;
    require (owner.create (pair, rate) && audition.open (pair, rate) && timing.open (pair, rate, false),
             "both leases open the same live ring");
    owner.ring()->header.demand.store (1);
    timing.close();
    require (owner.ring()->header.demand.load() == 1, "retiring clock preparation cannot stop an audition");
    audition.close();
    require (owner.ring()->header.demand.load() == 0, "audition lease still releases its own demand");
    owner.close();
    require (! audition.open (pair, rate), "a closed PRE mapping cannot admit a new reader");
}

void livePreMappingHasOneWriter()
{
    SharedRingMapping first, reader, duplicate, independent;
    require (first.create (pair + 1, rate) && reader.open (pair + 1, rate, false), "first PRE has a reader");
    require (independent.create (pair + 7, rate), "other pair identities retain independent writers");
    const auto identity = first.ring()->header.timing.ownerA.load();
    require (! duplicate.create (pair + 1, rate), "a duplicate live PRE cannot restamp or replace the same pair ring");
    require (first.ring()->header.timing.ownerA.load() == identity
        && reader.ring()->header.ownerClosed.load() == 0, "rejected duplicate leaves the live lifetime untouched");
    first.close();
    require (duplicate.create (pair + 1, rate), "a replacement can acquire ownership after PRE closes");
    require (reader.ring()->header.ownerClosed.load() != 0
        && duplicate.ring()->header.timing.ownerA.load() != identity, "replacement never restamps the retired reader's object");
}

void entryWaitsButNeverResurrectsAProof()
{
    Fixture fixture;
    for (int n = 0; n < 100; ++n) fixture.step();
    fixture.enableLoop();
    for (int n = 0; n < 80; ++n) fixture.step();
    Consumer consumer;
    require (! consumer.adoptInitialTiming (*fixture.ring, fixture.post, fixture.evidence),
             "metadata without demanded PCM cannot open the initial admission");
    require (fixture.read (consumer).verdict != Verdict::accepted, "POST-first entry leaves the output unproven");
    fixture.ring->header.demand.store (1);
    fixture.step();
    require (consumer.adoptInitialTiming (*fixture.ring, fixture.post, fixture.evidence),
             "first demanded PRE callback can complete the same pending entry without a second click");
    fixture.read (consumer);
    for (int n = 0; n < 40; ++n) { fixture.step(); fixture.read (consumer); }
    fixture.checkPcm();
    fixture.step();
    fixture.ring->header.timing.sequence.fetch_add (1);
    require (fixture.read (consumer).verdict == Verdict::accepted,
             "clock-only writer overlap is not a new PCM proof failure after admission");
    fixture.checkPcm();
    fixture.ring->header.timing.sequence.fetch_add (1);
    const auto generation = fixture.ring->header.timing.generation.load();
    fixture.ring->header.timing.generation.store (generation + 1);
    require (fixture.read (consumer).verdict != Verdict::accepted, "new PRE generation cannot use a prepared K");
    fixture.ring->header.timing.generation.store (generation);
    require (! consumer.adoptInitialTiming (*fixture.ring, fixture.post, fixture.evidence),
             "a broken completed proof cannot be re-adopted without an explicit new session");
    consumer.reset();
    require (consumer.adoptInitialTiming (*fixture.ring, fixture.post, fixture.evidence),
             "an explicit new session has its own entry admission");

    Fixture overlap;
    for (int n = 0; n < 100; ++n) overlap.step();
    overlap.enableLoop();
    for (int n = 0; n < 80; ++n) overlap.step();
    overlap.step (false);
    require (! overlap.evidence.valid,
             "writer overlap provides no evidence for this callback");
    overlap.step();
    require (overlap.evidence.valid, "a coherent next callback retains K after metadata writer overlap");
    overlap.ring->header.demand.store (1); overlap.step();
    Consumer delayedAdmission;
    require (overlap.read (delayedAdmission).verdict == Verdict::loopUnproven,
             "an initial callback without adoption still remains inaudible");
    overlap.step();
    require (delayedAdmission.adoptInitialTiming (*overlap.ring, overlap.post, overlap.evidence),
             "pending initial admission may complete after an unavailable first snapshot");
    overlap.read (delayedAdmission);
}

void publisherFencesChangedPreTimelines()
{
    using Mutate = void (*) (BlockClock&);
    const Mutate cases[] {
        [] (BlockClock& b) { b.playing = false; },
        [] (BlockClock& b) { b.afterGap = true; },
        [] (BlockClock& b) { b.clockValid = false; },
        [] (BlockClock& b) { b.projectValid = false; },
        [] (BlockClock& b) { b.clock += 24000; },
        [] (BlockClock& b) { b.project += 123; },
        [] (BlockClock& b) { b.loop.end += 1; },
        [] (BlockClock& b) { b.loop.bpm += 1; },
        [] (BlockClock& b) { b.loop.valid = false; },
        [] (BlockClock& b) { ++b.clockAuthority; },
        [] (BlockClock& b) { ++b.maximumDelaySamples; },
        [] (BlockClock& b) { b.outputPresentationValid = true; ++b.outputPresentationSamples; }
    };
    for (auto mutate : cases)
    {
        Fixture fixture;
        for (int n = 0; n < 100; ++n) fixture.step();
        fixture.enableLoop();
        for (int n = 0; n < 80; ++n) fixture.step();
        const auto previous = fixture.ring->header.timing.generation.load();
        auto block = fixture.pre;
        block.clock += frames; block.project += frames;
        block.loop.ppq += static_cast<double> (frames) / 24000.0;
        mutate (block);
        fixture.feeder.feed (*fixture.ring, block, nullptr, 0);
        TimingSnapshot snapshot;
        require (readTiming (fixture.ring->header.timing, snapshot), "faulting PRE publishes coherent metadata");
        require (snapshot.generation != previous && ! snapshot.anchor.linearKnown,
                 "every PRE discontinuity fences the old linear origin even without PCM demand");
    }
}

void extremeHostClocksFailClosed()
{
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    LoopAnchor anchor;
    anchor.loopKnown = true;
    anchor.runStart = 0;
    anchor.clock = 0;
    anchor.project = 0;
    anchor.loopSamples = 24000;
    anchor.loop = { true, true, 0.0, 0.0, 1.0, 120.0 };
    BlockClock pre, post;
    for (auto* block : { &pre, &post })
    {
        block->clockValid = block->projectValid = block->playing = true;
        block->frames = frames;
        block->loop = anchor.loop;
        block->clockBasis = static_cast<std::uint8_t> (ClockBasis::aaxEngine);
        block->clockAuthority = static_cast<std::uint8_t> (ClockAuthority::boundedAaxEngine);
        block->maximumDelaySamples = 16383;
    }
    pre.clock = maximum;
    pre.project = minimum;
    post.clock = minimum;
    post.project = maximum;
    require (! initialLoopCandidate (anchor, pre, post, 24000, rate, ringCapacityFrames).valid,
             "overflowing host clock origins fail closed");

    LoopCycleMeter cycle;
    pre.clock = maximum - 64;
    pre.project = maximum - 64;
    cycle.observe (pre, rate);
    pre.clock = maximum;
    pre.project = maximum;
    require (cycle.observe (pre, rate).changed,
             "a native clock that cannot advance one block fences the measured loop cycle");
}
}

int main (int argc, char** argv)
{
    if (argc == 3 && std::strcmp (argv[1], "--abandon-pre") == 0)
        return abandonPreOwner (std::strtoull (argv[2], nullptr, 16));
    require (argc == 1, "standalone timing contract arguments");
    static_assert (sizeof (TimingHeader) <= 192 && sizeof (TimingEvidence) <= 72,
                   "preparation must remain fixed-size metadata, not an audio history");
    prepareWithoutCopyingPcm();
    initialLoopDoesNotInventAnOrigin();
    certifiedInitialLoopStartsWithoutAnotherGesture();
    pendingInitialLoopWaitsForFirstUsableClock();
    faultsFenceEvidence();
    timingMappingDoesNotOwnAuditionDemand();
    livePreMappingHasOneWriter();
    crashedPreCannotRestampOldReaders();
    entryWaitsButNeverResurrectsAProof();
    publisherFencesChangedPreTimelines();
    extremeHostClocksFailClosed();
    std::printf ("clock preparation PASS: metadata=%zu bytes, PCM capacity unchanged=%u frames\n",
                 sizeof (TimingHeader), ringCapacityFrames);
}
