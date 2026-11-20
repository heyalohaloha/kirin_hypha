#pragma once
// Diagnostic build only. No component layout or shipping code changes.
// Thread-local aggregate: reset around one fixture/view on its GUI thread.
// Not a global product poll rate, unique tuple set, or pixel-change counter.
#if defined(KIRIN_FREQ_PHASE_PROFILE)
#include <cstdint>
namespace hypha::freq_cadence
{
struct Counts
{
    std::uint64_t batchCalls=0;       // All setBatch entries, including rejected/status-only batches.
    std::uint64_t batchSeed=0;        // First frame setSnapshot on reset; not every setSnapshot call.
    std::uint64_t batchForwarded=0;   // Validated frame with endpoint > pending, sent to queueSnapshot.
    std::uint64_t batchOldSkip=0;     // Validated frame not newer than pending, skipped in the loop.
    std::uint64_t batchReseed=0;      // Continuity-change latest reseed; NOT an additional new frame.
    std::uint64_t queueCalls=0;       // All queueSnapshot entries, including direct fixture calls.
    std::uint64_t queueIdentical=0;   // Existing same-continuity/memcmp early-return branch.
    std::uint64_t tickCalls=0;        // All manual or driver presentationTickAt entries.
    std::uint64_t curveBranch=0;      // Curve update branch executed, not proof of changed pixels.
    std::uint64_t numericBranch=0;    // Numeric copy branch executed, not proof of changed values.
    std::uint64_t tickRepaint=0;      // needsRepaint branch only; excludes setter/parent/OS paint.
};
inline thread_local Counts counts;
inline void reset() noexcept { counts=Counts{}; }
inline Counts read() noexcept { return counts; }
}
#define KIRIN_FREQ_CADENCE(field) do { ++::hypha::freq_cadence::counts.field; } while(false)
#else
#define KIRIN_FREQ_CADENCE(field)
#endif
