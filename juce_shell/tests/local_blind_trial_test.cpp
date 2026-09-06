#include "../src/local_blind/LocalBlindSlot.h"
#include "../src/local_blind/LocalBlindEpochSnapshot.h"
#include <array>
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
        require (t->view().activeStimulus == 1 && t->view().revealedOneSide == -1, "receipt without identity");
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
    for (int error = 0; error < 10; ++error)
    {
        auto t = trial();
        require (t->start(), "failure fixture start");
        Buffer buffer;
        buffer.render (*t, block());
        auto next = block (0);
        if (error == 0) next.positionValid = false;
        if (error == 1) next.playing = false;
        if (error == 2) next.realtime = false;
        if (error == 3) next.bypassed = true;
        if (error == 4) next.sampleRate = 96000;
        if (error == 5) ++next.epochs.pair;
        if (error == 6) ++next.epochs.capture;
        if (error == 7) next.epochs.scope = 0;
        if (error == 8) next.position = 1; // seek inside cue is still a discontinuity
        if (error == 9) next.position = 160; // partial block past cue: no modulo or stale tail
        buffer.fill();
        require (buffer.render (*t, next) == TrialOutput::untouched && buffer.original(), "invalid copy preserves input");
        require (t->view().phase == TrialPhase::returnPending && ! t->select (2), "failure latches until return");
        require (! t->reveal() && t->view().revealedOneSide == -1, "no invalidation reveal");
        require (buffer.render (*t, block (0)) == TrialOutput::untouched, "conditions returning do not restart trial");
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
            std::this_thread::yield();
        }
    });
    for (int round = 0; round < 100; ++round)
    {
        while (! slot.collect()) std::this_thread::yield();
        require (slot.publish (trial()), "no replacement of pending PCM");
        slot.control()->start();
        slot.control()->stop();
        slot.control()->requestNormalReturn();
        while (! slot.control()->normalReturnConfirmed()) std::this_thread::yield();
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

int main()
{
    selectionAndAnswers();
    timeAndFailures();
    heldLevelAndRetirement();
    validation();
    concurrentRetirement();
    coherentEpochs();
    require (rtAllocations.load() == 0 && rtDeletes.load() == 0, "RT allocation/destruction must be zero");
    std::cout << "Local Blind trial: PASS (selection/receipt, 10 invalidations, exact loop, lower-level return, 9 bounds, 100 retire races, 10000 epoch writes; RT new/delete=0)\n";
}
