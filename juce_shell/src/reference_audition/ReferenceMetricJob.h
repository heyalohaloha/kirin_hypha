#pragma once
#include "ReferenceMetricScan.h"
#include "ReferenceReceiptModuleLifetime.h"
#include <mutex>
#include <condition_variable>
#include <optional>
#include <thread>
#include <system_error>
#include <chrono>
#include <stdexcept>

namespace hypha::reference_audition::metrics
{
// One non-RT worker per loaded image. Closing an editor cancels, never joins an I/O
// worker on the UI thread. A blocked previous job prevents another job from spawning.
class Job final
{
    struct State
    {
        explicit State (std::uint64_t value) : generation (value) {}
        const std::uint64_t generation;
        std::atomic<bool> cancel {false}, finished {false};
        std::mutex mutex;
        std::condition_variable completed;
        bool launched = false;
        std::optional<ScanResult> result;
    };
    inline static std::atomic<bool> occupied {false}, admissionFailed {false};
    std::shared_ptr<State> state;
#if defined(KIRIN_REFERENCE_METRIC_JOB_TEST)
    inline static bool failDetach = false;
#endif
public:
    enum class Start { started, busy, unavailable };
    // Capture a lightweight immutable description, never live AudioPages/PinnedSource
    // or a last-owner I/O resource. All reader construction/destruction belongs inside
    // Work: a refused start destroys the description on the caller's thread.
    using Work = std::function<ScanResult (const std::atomic<bool>&)>;
    struct Completion { std::uint64_t generation; ScanResult scan; };
    Job() = default;
    Job (const Job&) = delete;
    Job& operator= (const Job&) = delete;
    ~Job() { cancel(); }
    void cancel() noexcept
    { if (state) state->cancel.store (true, std::memory_order_release); }
    bool finished() const noexcept
    { return state && state->finished.load (std::memory_order_acquire); }
    Start start (std::uint64_t generation, Work work)
    {
        if (admissionFailed.load (std::memory_order_acquire) || !work || generation == 0) return Start::unavailable;
        if (state && !finished()) return Start::busy;
        if (!receipt_detail::retainModule()) return Start::unavailable;
        bool expected = false;
        if (!occupied.compare_exchange_strong (expected, true, std::memory_order_acq_rel)) return Start::busy;
        try
        {
            auto next = std::make_shared<State> (generation);
            auto worker = std::make_unique<std::thread> ([next, work = std::move (work)] () mutable {
                {
                    std::unique_lock<std::mutex> lock (next->mutex);
                    next->completed.wait (lock, [&] { return next->launched; });
                }
                ScanResult result;
                try { result = work (next->cancel); }
                catch (...) { result.state = ScanState::analysisFailed; result.frames.clear(); }
                if (next->cancel.load (std::memory_order_acquire))
                { result.state = ScanState::cancelled; result.frames.clear(); }
                {
                    const std::lock_guard<std::mutex> lock (next->mutex);
                    next->result = std::move (result);
                }
                // Destroy the job's source/reader factory on this worker before admission
                // reopens. A blocked destructor must not permit a second I/O job.
                work = {};
                occupied.store (false, std::memory_order_release);
                {
                    const std::lock_guard<std::mutex> lock (next->mutex);
                    next->finished.store (true, std::memory_order_release);
                }
                next->completed.notify_all();
            });
            try
            {
#if defined(KIRIN_REFERENCE_METRIC_JOB_TEST)
                if (failDetach) { failDetach = false; throw std::system_error (std::make_error_code (std::errc::invalid_argument)); }
#endif
                worker->detach();
            }
            catch (...)
            {
                // A joinable thread destructor would terminate the host, while joining
                // on the UI could hang it. Quarantine this one handle until process exit
                // and refuse all future admission. The retained module keeps its code alive.
                admissionFailed.store (true, std::memory_order_release);
                if (worker->joinable()) (void) worker.release();
            }
            state = next;
            {
                const std::lock_guard<std::mutex> lock (next->mutex);
                next->launched = true;
            }
            next->completed.notify_all();
            return Start::started;
        }
        catch (...) { occupied.store (false, std::memory_order_release); return Start::unavailable; }
    }
#if defined(KIRIN_REFERENCE_METRIC_JOB_TEST)
    static void failNextDetachForTest() { failDetach = true; }
    std::function<void()> completionForTest()
    {
        return [saved = state] {
            std::unique_lock<std::mutex> lock (saved->mutex);
            if (!saved->completed.wait_for (lock, std::chrono::seconds(5), [&] { return saved->finished.load (std::memory_order_acquire); }))
                throw std::runtime_error("metric job completion timeout");
        };
    }
    void waitForTest()
    {
        std::unique_lock<std::mutex> lock (state->mutex);
        if (!state->completed.wait_for (lock, std::chrono::seconds(5), [&] { return finished(); }))
            throw std::runtime_error("metric job completion timeout");
    }
#endif
    // Caller is the single control-thread owner. Source authority, SHA, host format,
    // and selection generation must STILL match at acceptance, not just at start.
    std::optional<Completion> take (std::uint64_t currentGeneration)
    {
        if (!finished()) return std::nullopt;
        const std::unique_lock<std::mutex> lock (state->mutex, std::try_to_lock);
        if (!lock.owns_lock() || !state->result) return std::nullopt;
        if (state->generation != currentGeneration || state->cancel.load (std::memory_order_acquire))
        { state->result.reset(); return std::nullopt; }
        Completion completion {state->generation, std::move (*state->result)};
        state->result.reset();
        return completion;
    }
};
}
