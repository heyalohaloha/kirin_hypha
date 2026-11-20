#include "reference_audition/ReferenceMetricJob.h"
#include <iostream>
#include <stdexcept>
using namespace hypha::reference_audition::metrics;
void require (bool value, const char* message) { if (!value) throw std::runtime_error (message); }
struct Gate
{
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    ScanResult run (const std::atomic<bool>& cancelled)
    {
        std::unique_lock<std::mutex> lock (mutex);
        entered = true; changed.notify_all();
        if (!changed.wait_for (lock, std::chrono::seconds(5), [&] { return released; }))
            throw std::runtime_error("test gate release timeout");
        return {cancelled.load() ? ScanState::cancelled : ScanState::complete, {}, 42};
    }
    void wait() { std::unique_lock<std::mutex> lock(mutex); if (!changed.wait_for(lock, std::chrono::seconds(5), [&] {return entered;})) throw std::runtime_error("test gate entry timeout"); }
    void release() { const std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
};
struct DestructionBlock
{
    std::shared_ptr<Gate> gate;
    ~DestructionBlock() { std::atomic<bool> cancel{false}; gate->run(cancel); }
};
int main()
{
    try
    {
        auto gate = std::make_shared<Gate>();
        Job first, second;
        require(first.start(1, [gate](const auto& cancel) {return gate->run(cancel);}) == Job::Start::started, "first starts");
        gate->wait();
        require(!first.take(1), "running has no result");
        require(second.start(2, [](const auto&) {return ScanResult{};}) == Job::Start::busy, "global admission while I/O waits");
        first.cancel(); gate->release(); first.waitForTest();
        require(!first.take(1), "cancelled result never accepted");
        require(second.start(2, [](const auto&) -> ScanResult {throw std::runtime_error("allocation/read failure");}) == Job::Start::started, "exception job starts");
        second.waitForTest();
        auto failed = second.take(2);
        require(failed && failed->scan.state == ScanState::analysisFailed && failed->scan.frames.empty(), "worker exception contained");
        require(!second.take(2), "completion taken once");
        require(second.start(3, [](const auto&) {return ScanResult{ScanState::complete, {}, 123};}) == Job::Start::started, "next generation starts");
        second.waitForTest();
        require(!second.take(4) && !second.take(3), "stale generation retired");
        auto closing = std::make_unique<Job>();
        auto blocked = std::make_shared<Gate>();
        require(closing->start(5, [blocked](const auto& cancel) {return blocked->run(cancel);}) == Job::Start::started, "closing job starts");
        auto completed = closing->completionForTest(); blocked->wait();
        closing.reset(); // Must return without waiting for the blocked worker.
        require(first.start(6, [](const auto&) {return ScanResult{};}) == Job::Start::busy, "close does not reopen admission early");
        blocked->release(); completed();
        require(first.start(6, [](const auto&) {return ScanResult{ScanState::complete, {}, 7};}) == Job::Start::started, "cleanup returns admission");
        first.waitForTest();
        auto good = first.take(6);
        require(good && good->generation == 6 && good->scan.sourceFrames == 7, "exact generation completion");
        auto workGate = std::make_shared<Gate>();
        auto destructionGate = std::make_shared<Gate>();
        auto resource = std::make_shared<DestructionBlock>(); resource->gate = destructionGate;
        Job cleanup;
        require(cleanup.start(7, [resource, workGate](const auto& cancel) { return workGate->run(cancel); }) == Job::Start::started, "resource job starts");
        workGate->wait(); resource.reset(); workGate->release(); destructionGate->wait();
        require(second.start(8, [](const auto&) {return ScanResult{};}) == Job::Start::busy, "factory destructor retains admission");
        destructionGate->release(); cleanup.waitForTest();
        require(cleanup.take(7).has_value(), "cleanup completion");
        require(second.start(8, [](const auto&) {return ScanResult{ScanState::complete, {}, 8};}) == Job::Start::started, "after cleanup starts");
        second.waitForTest(); second.cancel();
        require(!second.take(8), "finished then cancelled result is rejected");
        Job::failNextDetachForTest();
        require(first.start(9, [](const auto&) {return ScanResult{ScanState::complete, {}, 9};}) == Job::Start::started, "detach fault does not terminate or join host");
        first.waitForTest(); require(first.take(9).has_value(), "already-started fault job finishes safely");
        require(second.start(10, [](const auto&) {return ScanResult{};}) == Job::Start::unavailable, "detach fault fails future admission closed");
        std::cout << "Reference metric job contracts passed\n"; return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
