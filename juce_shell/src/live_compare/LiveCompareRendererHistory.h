// Class-scope implementation included by PostRenderer.
    // Audio Thread: the POST input (A, before any output mixing) indexed by POST's continuous clock.
    void recordHistory (const BlockClock& block, float* const* io, int channels, const Decision& decision) noexcept
    {
        if (history == nullptr || ! block.clockValid)
        {
            invalidateHistory();
            return;
        }
        historySequence.fetch_add (1);
        const auto end = historyEnd.load (std::memory_order_relaxed);
        const bool proven = decision.verdict == Verdict::accepted;
        const bool restarted = block.afterGap || ! block.playing || block.clock != end || end == 0
            || ! proven || ! matchOffsetValid.load (std::memory_order_relaxed)
            || decision.k != matchOffset.load (std::memory_order_relaxed)
            || decision.run != historyPreRun.load();
        if (restarted)
        {
            historyStart.store (block.clock, std::memory_order_release);
            historyProof.fetch_add (1);
        }
        historyPreRun.store (decision.run);
        matchOffset.store (decision.k, std::memory_order_release);
        matchOffsetValid.store (proven, std::memory_order_release);
        const auto offset = block.project - block.clock;
        if (restarted || ! block.projectValid || ! projectKnown.load (std::memory_order_relaxed)
            || offset != projectOffset.load (std::memory_order_relaxed))
        {
            projectOffset.store (offset, std::memory_order_relaxed);
            projectKnown.store (block.projectValid && block.playing, std::memory_order_relaxed);
            projectRunStart.store (block.clock, std::memory_order_release);
        }
        const auto mask = historyFrames - 1;
        for (std::int32_t i = 0; i < block.frames; ++i)
        {
            const auto slot = static_cast<std::size_t> ((block.clock + i) & mask) * 2;
            history[slot].store (io[0][i], std::memory_order_relaxed);
            history[slot + 1].store (io[std::min (1, channels - 1)][i], std::memory_order_relaxed);
        }
        historyBlockFrames.store (block.frames, std::memory_order_relaxed);
        historyEnd.store (block.clock + block.frames, std::memory_order_release);
        historySequence.fetch_add (1);
    }

    void invalidateHistory() noexcept
    {
        historySequence.fetch_add (1);
        matchOffsetValid.store (false, std::memory_order_release);
        historyProof.fetch_add (1);
        historySequence.fetch_add (1);
    }
