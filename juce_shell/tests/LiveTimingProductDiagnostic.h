#pragma once

// Opt-in, non-shipping fixture instrumentation. Only this test's translation unit uses
// Clang -fno-access-control. No product class, layout, output policy or clock is changed.
// RT-owned fields are read by the fixture's SAME audio thread, never by the GUI thread.
#if defined (KIRIN_HYPHA_TIMING_PRODUCT_DIAGNOSTIC)
#include <array>
#include <atomic>
#include <iostream>
#include <memory>

class LiveTimingProductDiagnostic
{
    struct Row
    {
        int phase = 0, block = 0, streak = 0;
        std::uint32_t bits = 0;
        std::uint64_t preGeneration = 0, observerGeneration = 0, preparedGeneration = 0;
        std::uint64_t run = 0, sessionGeneration = 0;
        std::int64_t preClock = 0, postClock = 0, origin = 0, runStart = 0, writeEnd = 0;
        std::int64_t observerK = 0, consumerK = 0;
        std::uint64_t preInterval = 0, postInterval = 0, maximumPreInterval = 0, maximumPostInterval = 0;
        unsigned verdict = 0, observation = 0;
        bool preGap = false, postGap = false;
    };
public:
    LiveTimingProductDiagnostic() : rows (std::make_unique<std::array<Row, 8192>>()) {}
    void setPhase (int value) noexcept { phase.store (value, std::memory_order_relaxed); }

    // Immediately AFTER the real PRE and POST processBlock calls, on their owning thread.
    // Fixed storage, one producer, immutable published rows, bounded atomics only; no logging,
    // allocation, locks or I/O here. This is not included in production performance evidence.
    void observe (KirinHyphaProcessorBase& pre, KirinHyphaProcessorBase& post, int block) noexcept
    {
        const auto count = published.load (std::memory_order_relaxed);
        if (count >= rows->size()) return;
        Row row;
        row.phase = phase.load (std::memory_order_relaxed);
        row.block = block;
        const auto interval = [] (std::uint64_t now, std::uint64_t before)
        { return before != 0 && now >= before ? now - before : 0; };
        row.preInterval = interval (pre.liveCompare.gaps.previousNanos, previousPre);
        row.postInterval = interval (post.liveCompare.gaps.previousNanos, previousPost);
        previousPre = pre.liveCompare.gaps.previousNanos;
        previousPost = post.liveCompare.gaps.previousNanos;
        const auto gap = [] (const hypha::live_compare::GapDetector& detector, std::uint64_t value)
        {
            const double duration = detector.previousFrames / 48000.0 * 1.0e9;
            return static_cast<double> (value) > std::max (duration * detector.profile.factor,
                                                          detector.profile.floorSeconds * 1.0e9);
        };
        row.preGap = gap (pre.liveCompare.gaps, row.preInterval);
        row.postGap = gap (post.liveCompare.gaps, row.postInterval);
        maximumPre = std::max (maximumPre, row.preInterval);
        maximumPost = std::max (maximumPost, row.postInterval);
        row.maximumPreInterval = maximumPre;
        row.maximumPostInterval = maximumPost;
        const auto bit = [&row] (unsigned index, bool value)
        { if (value) row.bits |= std::uint32_t (1) << index; };
        pre.liveCompare.ring.withRealtime ([&] (hypha::live_compare::SharedRingMapping& mapping)
        {
            hypha::live_compare::TimingSnapshot snapshot;
            if (mapping.ring() == nullptr) return;
            bit (0, true);
            if (! readTiming (mapping.ring()->header.timing, snapshot)) return;
            bit (1, snapshot.active); bit (2, snapshot.anchor.linearKnown); bit (3, snapshot.anchor.loopKnown);
            row.preGeneration = snapshot.generation;
            row.preClock = snapshot.block.clock; row.origin = snapshot.origin;
            row.runStart = snapshot.anchor.runStart;
        });
        post.liveCompare.preparation.peers.withRealtime ([&] (auto& peer)
        {
            bit (4, true); bit (5, peer.observer.valid); bit (6, peer.observer.havePrevious);
            bit (7, peer.observer.previous.anchor.linearKnown); bit (8, peer.observer.previous.anchor.loopKnown);
            bit (9, peer.authority == post.liveCompare.authority.ticket());
            row.streak = peer.observer.streak;
            row.observerGeneration = peer.observer.generation;
            row.observerK = peer.observer.k;
            row.postClock = peer.observer.timeline.clock;
        });
        const auto& consumer = post.liveCompare.renderer.consumer;
        bit (10, consumer.kValid); bit (11, consumer.initialAdmission);
        bit (12, post.liveCompare.preparation.initialRequested.load (std::memory_order_acquire));
        bit (13, post.liveCompare.authority.permitted()); bit (14, post.liveCompare.authority.restoring());
        bit (15, post.liveCompare.contentHold.load (std::memory_order_acquire));
        bit (16, post.liveCompare.compensationOff.load (std::memory_order_acquire));
        bit (17, pre.writesEnabled.load (std::memory_order_acquire));
        bit (18, post.writesEnabled.load (std::memory_order_acquire));
        row.preparedGeneration = consumer.preparedGeneration; row.consumerK = consumer.k;
        row.sessionGeneration = post.liveCompare.sessionGeneration.load (std::memory_order_acquire);
        row.verdict = post.liveCompare.verdict.load (std::memory_order_acquire);
        row.observation = static_cast<unsigned> (post.liveCompare.observationReason.load (std::memory_order_acquire));
        post.liveCompare.ring.withRealtime ([&] (hypha::live_compare::SharedRingMapping& mapping)
        {
            if (mapping.ring() == nullptr) return;
            bit (19, true);
            row.run = mapping.ring()->header.run.load (std::memory_order_acquire);
            row.writeEnd = mapping.ring()->header.writeEnd.load (std::memory_order_acquire);
            bit (20, mapping.ring()->header.timingGeneration.load (std::memory_order_acquire) == row.preGeneration);
        });
        (*rows)[count] = row;
        published.store (count + 1, std::memory_order_release);
    }

    // Message thread only. Finished rows never change while the audio producer appends.
    void printReady()
    {
        const auto end = published.load (std::memory_order_acquire);
        while (printed < end)
        {
            const auto& row = (*rows)[printed++];
            const bool changed = row.phase != previous.phase || row.bits != previous.bits
                || row.preGeneration != previous.preGeneration || row.observerGeneration != previous.observerGeneration
                || row.preparedGeneration != previous.preparedGeneration || row.run != previous.run
                || row.streak != previous.streak || row.verdict != previous.verdict || row.observation != previous.observation;
            // Paused callbacks deliberately fence generations. Don't flood a useful diagnostic
            // with that expected inactivity; retain the first row and all playing transitions.
            if (havePrinted && ! row.preGap && ! row.postGap && (! changed || (row.bits & 2u) == 0)) continue;
            std::cout << "TIMING_DIAGNOSTIC phase=" << row.phase << " block=" << row.block
                      << " bits=" << row.bits << " preGen=" << row.preGeneration
                      << " obsGen=" << row.observerGeneration << " preparedGen=" << row.preparedGeneration
                      << " streak=" << row.streak << " obsK=" << row.observerK << " consumerK=" << row.consumerK
                      << " preClock=" << row.preClock << " postClock=" << row.postClock
                      << " origin=" << row.origin << " runStart=" << row.runStart
                      << " run=" << row.run << " writeEnd=" << row.writeEnd
                      << " sessionGen=" << row.sessionGeneration << " verdict=" << row.verdict
                      << " observation=" << row.observation << " preGap=" << row.preGap << " postGap=" << row.postGap
                      << " preIntervalUs=" << row.preInterval / 1000 << " postIntervalUs=" << row.postInterval / 1000
                      << " maxPreUs=" << row.maximumPreInterval / 1000 << " maxPostUs=" << row.maximumPostInterval / 1000
                      << '\n';
            previous = row; havePrinted = true;
        }
        std::cout.flush();
    }
private:
    std::unique_ptr<std::array<Row, 8192>> rows;
    std::atomic<std::size_t> published { 0 };
    std::atomic<int> phase { 0 };
    std::size_t printed = 0;
    Row previous;
    bool havePrinted = false;
    std::uint64_t previousPre = 0, previousPost = 0, maximumPre = 0, maximumPost = 0;
};
#else
struct LiveTimingProductDiagnostic
{
    void setPhase (int) noexcept {}
    void observe (KirinHyphaProcessorBase&, KirinHyphaProcessorBase&, int) noexcept {}
    void printReady() noexcept {}
};
#endif
