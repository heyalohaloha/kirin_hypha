#include "../src/local_blind/LocalBlindSlot.h"
#include "../src/local_blind/LocalBlindEpochSnapshot.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

// The RT test thread counts normal C++ allocation and destruction. Locks/I/O are audited in source.
static thread_local bool inRt = false;
static std::atomic<unsigned> rtAllocations { 0 }, rtDeletes { 0 };
void* operator new (std::size_t bytes)
{
    if (inRt) ++rtAllocations;
    if (auto* p = std::malloc (bytes == 0 ? 1 : bytes)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t bytes) { return ::operator new (bytes); }
void operator delete (void* p) noexcept { if (inRt) ++rtDeletes; std::free (p); }
void operator delete[] (void* p) noexcept { ::operator delete (p); }
void operator delete (void* p, std::size_t) noexcept { ::operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { ::operator delete (p); }

using namespace hypha::local_blind;
static void require (bool ok, const char* message)
{ if (! ok) { std::cerr << message << '\n'; std::abort(); } }

static TrialFormat format (int channels = 2)
{ return { { 1, 2, 3, 4 }, 48000, channels, -64, 256, 64, true }; }
static TrialBlock block (std::int64_t position = -64)
{ return { { 1, 2, 3, 4 }, 48000, position, true, true, true, false, true, -64, 192 }; }

static std::unique_ptr<LocalBlindTrial> trial (bool firstIsPre = false, TrialGain gain = {}, int channels = 2)
{
    return std::make_unique<LocalBlindTrial> (format (channels), gain,
        std::vector<float> (256 * channels, 0.25f), std::vector<float> (256 * channels, -0.125f),
        firstIsPre, 256 * channels * 8);
}

struct Buffer
{
    std::array<float, 64> left {}, right {};
    std::array<float*, 2> pointers { left.data(), right.data() };
    void fill() { left.fill (0.8f); right.fill (-0.4f); }
    bool original() const { return left[0] == 0.8f && right[63] == -0.4f; }
    TrialOutput render (LocalBlindTrial& t, TrialBlock b, int channels = 2)
    {
        inRt = true;
        const auto result = t.render (pointers.data(), channels, 64, b);
        inRt = false;
        return result;
    }
};

static void returnEvidence()
{
    for (bool played : { false, true })
    {
        auto t = trial (false, { 2.0f, 0.5f, true });
        Buffer buffer;
        t->start (true);
        require (! t->returnFacts().attenuationApplied, "approval alone is not applied gain");
        if (played) buffer.render (*t, block());
        t->stop();
        require (t->returnFacts().attenuationApplied == played, "return amount uses actual audio gain fact");
        t->requestNormalReturn();
        const auto requested = t->returnFacts();
        t->requestNormalReturn();
        require (requested.sameRequest (t->returnFacts()), "pending command is idempotent");
        auto bypass = block(); bypass.bypassed = true;
        buffer.render (*t, bypass);
        require (! t->returnFacts().confirmed, "bypass callback cannot confirm normal output");
        auto offline = block(); offline.realtime = false;
        buffer.render (*t, offline);
        t->render (buffer.pointers.data(), 2, 0, block());
        require (! t->returnFacts().confirmed, "empty/offline callback cannot confirm normal output");
        buffer.fill(); buffer.render (*t, block());
        require (buffer.original() && t->returnFacts().confirmed
            && requested.sameRequest (t->returnFacts()), "nonempty realtime callback confirms exact request unchanged");
    }
}

static void selectionAndAnswers()
{
    for (bool firstIsPre : { false, true })
    {
        auto t = trial (firstIsPre);
        Buffer buffer;
        buffer.fill();
        require (buffer.render (*t, block()) == TrialOutput::untouched && buffer.original(), "prepared is transparent");
        require (! t->answer (TrialAnswer::one) && ! t->reveal(), "no early answer or reveal");
        require (t->start(), "explicit start");
        require (! t->start(), "single-use trial cannot restart");
        require (t->view().activeStimulus == 0 && t->view().pendingStimulus == 1, "pending until callback");
        require (buffer.render (*t, block()) == TrialOutput::copy, "first copy");
        require (buffer.left[0] == (firstIsPre ? -0.125f : 0.25f), "random side maps to exact source");
        require (t->view().activeStimulus == 1 && t->view().pendingStimulus == 2
                 && t->view().revealedOneSide == -1,
                 "first completed pass arms source two without revealing identity");
        require (! t->answer (TrialAnswer::two), "both sides must have been output");
        require (t->select (2) && t->select (1) && t->select (2), "rapid requests");
        require (t->view().activeStimulus == 1 && t->view().pendingStimulus == 2, "old receipt is not new request");
        require (buffer.render (*t, block (0)) == TrialOutput::copy, "second contiguous callback");
        require (buffer.left[0] == (firstIsPre ? 0.25f : -0.125f), "second exact source");
        require (t->answer (TrialAnswer::noPreference) && t->reveal(), "answer then explicit reveal");
        require (t->view().revealedOneSide == (firstIsPre ? 1 : 0), "reveal maps correctly");
        require (! t->answer (TrialAnswer::one), "answer cannot be rewritten after reveal");
        t->stop();
        require (t->view().revealedOneSide == -1 && ! t->normalReturnConfirmed(), "stop is not return acknowledgement");
        t->requestNormalReturn();
        require (! t->normalReturnConfirmed(), "control intent is not an audio receipt");
        buffer.fill();
        buffer.render (*t, block (64));
        require (buffer.original() && t->normalReturnConfirmed(), "normal input returns unchanged");
    }
}

static void timeAndFailures()
{
    for (int error : { 0, 2, 3, 4, 5, 6, 7 })
    {
        auto t = trial();
        require (t->start(), "failure fixture start");
        Buffer buffer;
        buffer.render (*t, block());
        auto next = block (0);
        if (error == 0) next.positionValid = false;
        if (error == 2) next.realtime = false;
        if (error == 3) next.bypassed = true;
        if (error == 4) next.sampleRate = 96000;
        if (error == 5) ++next.epochs.pair;
        if (error == 6) ++next.epochs.capture;
        if (error == 7) next.epochs.scope = 0;
        buffer.fill();
        require (buffer.render (*t, next) == TrialOutput::untouched && buffer.original(), "invalid copy preserves input");
        require (t->view().phase == TrialPhase::returnPending && ! t->select (2), "failure latches until return");
        require (! t->reveal() && t->view().revealedOneSide == -1, "no invalidation reveal");
        require (buffer.render (*t, block (0)) == TrialOutput::untouched, "conditions returning do not restart trial");
    }
    // A stop or a seek partway through a pass is not a failure: the live input plays meanwhile and
    // the trial waits for playback (a seek inside the range or past it waits for the range start).
    for (int interruption = 0; interruption < 3; ++interruption)
    {
        auto t = trial();
        require (t->start(), "interruption fixture start");
        Buffer buffer;
        buffer.render (*t, block());
        auto next = block (interruption == 0 ? 0 : interruption == 1 ? 1 : 160);
        next.playing = interruption != 0;
        buffer.fill();
        require (buffer.render (*t, next) == TrialOutput::untouched && buffer.original(), "an interruption plays the live input");
        require (t->view().failure == TrialFailure::none && t->view().phase != TrialPhase::returnPending,
                 "a stop or seek partway through a pass keeps the trial");
    }
    for (bool exact : { false, true })
    {
        auto t = trial();
        t->start();
        Buffer buffer;
        for (int position : { -64, 0, 64, 128 })
            require (buffer.render (*t, block (position)) == TrialOutput::copy, "one complete native lap");
        auto loop = block();
        loop.exactLoopRangeValid = exact;
        const auto result = buffer.render (*t, loop);
        require (result == (exact ? TrialOutput::copy : TrialOutput::untouched), "only exact proven loop accepted");
    }
}

// A stop partway through a full-range pass keeps it: playing on from the same place continues that
// pass. A seek starts the pass again from the range start. Neither fails the anonymous trial.
static void stopAndSeekKeepTheTrial()
{
    auto f = format();
    f.minimumHeardFrames = static_cast<std::uint64_t> (f.frames);
    auto t = std::make_unique<LocalBlindTrial> (f, TrialGain {}, std::vector<float> (512, 0.25f),
        std::vector<float> (512, -0.125f), false, 4096);
    Buffer buffer;
    require (t->start(), "mid-pass fixture starts");
    for (int position : { -64, 0 })
        require (buffer.render (*t, block (position)) == TrialOutput::copy, "first half of side one");
    auto stopped = block (64);
    stopped.playing = false;
    buffer.fill();
    require (buffer.render (*t, stopped) == TrialOutput::untouched && buffer.original(), "a stop plays the live input");
    require (t->view().failure == TrialFailure::none && t->view().phase == TrialPhase::listening,
             "a stop partway through the pass keeps the trial");
    for (int position : { 64, 128 })
        require (buffer.render (*t, block (position)) == TrialOutput::copy, "playing on continues the same pass");
    require (t->view().heardOneComplete, "the continued pass completes side one");
    require (buffer.render (*t, block (-64)) == TrialOutput::copy, "side two starts at the range start");
    buffer.fill();
    require (buffer.render (*t, block (64)) == TrialOutput::untouched && buffer.original()
             && t->view().failure == TrialFailure::none, "a seek inside the range waits for the range start");
    for (int position : { -64, 0, 64, 128 })
        require (buffer.render (*t, block (position)) == TrialOutput::copy, "side two plays again from the range start");
    require (t->view().heardTwoComplete && t->view().canAnswer, "both sides heard after the interruptions");
}

static void completePassCannotBeAssembledFromFragments()
{
    auto f = format();
    f.minimumHeardFrames = static_cast<std::uint64_t> (f.frames);
    auto t = std::make_unique<LocalBlindTrial> (
        f, TrialGain {}, std::vector<float> (512, 0.25f),
        std::vector<float> (512, -0.125f), false, 4096);
    Buffer buffer;
    require (t->start(), "full-pass fixture starts");
    for (int position : { -64, 0 })
        require (buffer.render (*t, block (position)) == TrialOutput::copy, "first half of side one");
    require (t->select (2), "switch to side two halfway");
    for (int position : { 64, 128, -64, 0 })
        require (buffer.render (*t, block (position)) == TrialOutput::copy, "fragmented side two output");
    require (t->select (1), "switch back to side one halfway");
    for (int position : { 64, 128 })
        require (buffer.render (*t, block (position)) == TrialOutput::copy, "fragmented side one output");
    require (! t->answer (TrialAnswer::cannotDistinguish),
             "two aggregate ranges assembled from fragments are not complete passes");
    for (int position : { -64, 0, 64, 128 })
        require (buffer.render (*t, block (position)) == TrialOutput::copy, "complete side-one pass");
    require (t->select (2), "select side two for a complete pass");
    for (int position : { -64, 0, 64, 128 })
        require (buffer.render (*t, block (position)) == TrialOutput::copy, "complete side-two pass");
    require (t->answer (TrialAnswer::cannotDistinguish), "two complete native passes permit answer");
}

static void explicitlyArmedTransport()
{
    for (bool lower : { false, true })
    {
        auto t = trial (false, lower ? TrialGain { 2.0f, 0.5f, true } : TrialGain {});
        Buffer buffer;
        auto stopped = block (192); // Capture has finished; DAW has not yet returned to the cue.
        stopped.playing = false;
        buffer.fill();
        require (buffer.render (*t, stopped) == TrialOutput::untouched && buffer.original(), "ready stays A");
        require (t->start (lower) && t->view().phase == TrialPhase::armed, "start arms, not heard");
        for (int position : { 192, 0, -64 })
        {
            stopped.position = position;
            buffer.fill();
            require (buffer.render (*t, stopped) == TrialOutput::untouched && buffer.original(),
                     "stopped positioning does not emit copies or apply a never-heard level change");
            require (t->view().phase == TrialPhase::armed && t->view().activeStimulus == 0
                         && ! t->answer (TrialAnswer::one), "waiting is not a playback receipt");
        }
        require (buffer.render (*t, block()) == TrialOutput::copy, "first exact native callback starts audition");
        require (t->view().phase == TrialPhase::listening && t->view().activeStimulus == 1, "heard after callback");
        stopped.position = 0;
        buffer.fill();
        require (buffer.render (*t, stopped) == (lower ? TrialOutput::heldAttenuation : TrialOutput::untouched),
                 "stop after audition retains only an actually applied approved level");
        require (t->view().failure == TrialFailure::none && t->view().phase != TrialPhase::returnPending,
                 "a stop after the audition began keeps the trial");
        require (buffer.render (*t, block()) == TrialOutput::copy, "playing from the range start plays a pass again");
    }
    for (int failure = 0; failure < 5; ++failure)
    {
        auto t = trial (false, { 2.0f, 0.5f, true });
        t->start (true);
        auto first = block();
        if (failure == 0) first.positionValid = false;
        if (failure == 1) first.epochs.scope = 0;
        if (failure == 2) first.bypassed = true;
        if (failure == 3) first.realtime = false;
        if (failure == 4) first.sampleRate = 96000;
        Buffer buffer;
        buffer.fill();
        require (buffer.render (*t, first) == TrialOutput::untouched && buffer.original(), "failed arm is transparent");
        require (t->view().phase == TrialPhase::returnPending, "invalid first callback fails closed");
        require (buffer.render (*t, block()) == TrialOutput::untouched && buffer.original(),
                 "unheard approval must not attenuate on later realtime callbacks");
    }
}

static void heldLevelAndRetirement()
{
    LocalBlindSlot slot;
    require (slot.publish (trial (false, { 2.0f, 0.5f, true })), "publish off RT");
    auto* t = slot.control();
    require (! t->start() && t->view().lowerPostApprovalRequired, "lower POST needs explicit approval");
    require (t->start (true), "explicit lower POST");
    Buffer buffer;
    require (buffer.render (*t, block()) == TrialOutput::copy && buffer.left[0] == 0.125f, "lowered POST copy");
    t->stop();
    require (! slot.retireAfterNormalReceipt(), "stop cannot free PCM or permission");
    buffer.fill();
    require (buffer.render (*t, block (0)) == TrialOutput::heldAttenuation && buffer.left[0] == 0.4f,
             "stop holds approved level on input");
    auto offline = block (64);
    offline.realtime = false;
    buffer.fill();
    require (buffer.render (*t, offline) == TrialOutput::untouched && buffer.original(), "offline never attenuated");
    buffer.fill();
    require (buffer.render (*t, block (64), 1) == TrialOutput::heldAttenuation && buffer.left[0] == 0.4f,
             "channel change does not silently restore loudness");
    t->requestNormalReturn();
    require (! slot.retireAfterNormalReceipt(), "still pending without callback");
    require (t->render (nullptr, 1, 64, block()) == TrialOutput::untouched && ! t->normalReturnConfirmed(),
             "null buffer cannot acknowledge return");
    require (t->render (buffer.pointers.data(), 1, 0, block()) == TrialOutput::untouched && ! t->normalReturnConfirmed(),
             "empty callback cannot acknowledge return");
    require (buffer.render (*t, offline) == TrialOutput::untouched && ! t->normalReturnConfirmed(),
             "offline render cannot release the realtime attenuation hold");
    auto bypassed = block();
    bypassed.bypassed = true;
    require (buffer.render (*t, bypassed) == TrialOutput::untouched && ! t->normalReturnConfirmed(),
             "bypassed output cannot acknowledge normal realtime return");
    buffer.fill();
    require (slot.render (buffer.pointers.data(), 1, 64, block (128)) && buffer.left[0] == 0.8f, "return in changed layout");
    require (slot.retireAfterNormalReceipt() && ! slot.hasStorage(), "off-RT retirement after actual return");
    require (! slot.render (buffer.pointers.data(), 2, 64, block()), "idle slot has no output ownership");
}

static void validation()
{
    for (int invalid = 0; invalid < 9; ++invalid)
    {
        auto f = format();
        TrialGain g;
        auto post = std::vector<float> (512, 0.25f), pre = post;
        if (invalid == 0) f.epochs.scope = 0;
        if (invalid == 1) f.channels = 0;
        if (invalid == 2) f.start = std::numeric_limits<std::int64_t>::max() - 1;
        if (invalid == 3) f.minimumHeardFrames = 0;
        if (invalid == 4) pre.pop_back();
        if (invalid == 5) pre[0] = std::numeric_limits<float>::infinity();
        if (invalid == 6) g.fixedPre = std::numeric_limits<float>::quiet_NaN();
        if (invalid == 7) g.lowerPost = 0.5f; // implicit attenuation is not allowed
        bool refused = false;
        try { LocalBlindTrial t (f, g, std::move (post), std::move (pre), false, invalid == 8 ? 4095 : 4096); }
        catch (const std::invalid_argument&) { refused = true; }
        require (refused, "malformed or insufficient capacity refused");
    }
    auto same = std::vector<float> (512, -0.0f);
    LocalBlindTrial control (format(), {}, same, same, false, 4096);
    control.start();
    Buffer buffer;
    buffer.render (control, block());
    require (std::memcmp (buffer.left.data(), same.data(), 64 * sizeof (float)) == 0, "same-PCM signed-zero control is bit identical");
}

static void concurrentRetirement()
{
    LocalBlindSlot slot;
    std::atomic<bool> done { false };
    std::thread audio ([&]
    {
        Buffer buffer;
        int position = -64;
        while (! done.load (std::memory_order_acquire))
        {
            inRt = true;
            slot.render (buffer.pointers.data(), 2, 64, block (position));
            inRt = false;
            position = position == 128 ? -64 : position + 64;
            // A DAW callback always has a non-callback interval. Preserve that boundary here so
            // the non-RT owner can observe readers == 0 instead of racing an artificial tight loop.
            std::this_thread::sleep_for (std::chrono::microseconds (1));
        }
    });
    for (int round = 0; round < 100; ++round)
    {
        while (! slot.collect()) std::this_thread::yield();
        require (slot.publish (trial()), "no replacement of pending PCM");
        slot.control()->start();
        slot.control()->stop();
        slot.control()->requestNormalReturn();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
        while (! slot.control()->normalReturnConfirmed())
        {
            require (std::chrono::steady_clock::now() < deadline,
                     "explicit RETURN must survive concurrent automatic source advancement");
            std::this_thread::yield();
        }
        require (slot.retireAfterNormalReceipt(), "retire raced callback safely");
    }
    done.store (true, std::memory_order_release);
    audio.join();
    require (slot.collect() && ! slot.hasStorage(), "all storage reclaimed off RT");
}

static void coherentEpochs()
{
    LocalBlindEpochSnapshot snapshot;
    require (! snapshot.read().valid(), "no synthetic default authority");
    std::atomic<bool> done { false };
    std::thread admission ([&]
    {
        for (std::uint64_t n = 1; n <= 10000; ++n) snapshot.publish ({ n, n + 1, n + 2, n + 3 });
        done.store (true, std::memory_order_release);
    });
    while (! done.load (std::memory_order_acquire))
    {
        inRt = true;
        const auto e = snapshot.read();
        inRt = false;
        require (! e.valid() || (e.pair == e.scope + 1 && e.capture == e.scope + 2 && e.clock == e.scope + 3),
                 "reader must never see mixed epochs");
    }
    admission.join();
    snapshot.publish ({});
    auto t = trial();
    t->start();
    auto revoked = block();
    revoked.epochs = snapshot.read();
    Buffer buffer;
    buffer.fill();
    require (buffer.render (*t, revoked) == TrialOutput::untouched && buffer.original(), "revocation prevents copy");
    require (t->view().failure == TrialFailure::epochs, "revocation is latched");
}

#include "LocalBlindPlaybackContract.h"
#include "LocalBlindTransitionContract.h"
#include "LocalBlindCommandContract.h"

void verifyNamedAb(); // local_blind_named_trial_test.cpp (INV-LC17)

int main()
{
    localBlindCommandContract();
    nativeRangePlaybackContract();
    capturedClockPlaybackContract();
    stoppedHostClockContract();
    localBlindTransitionContract();
    returnEvidence();
    selectionAndAnswers();
    timeAndFailures();
    stopAndSeekKeepTheTrial();
    completePassCannotBeAssembledFromFragments();
    explicitlyArmedTransport();
    heldLevelAndRetirement();
    validation();
    concurrentRetirement();
    coherentEpochs();
    verifyNamedAb();
    require (rtAllocations.load() == 0 && rtDeletes.load() == 0, "RT allocation/destruction must be zero");
    std::cout << "Local Blind trial: PASS (selection/receipt, 7 invalidations, 3 interruptions kept, exact loop, lower-level return, 9 bounds, 100 retire races, 10000 epoch writes; RT new/delete=0)\n";
}
