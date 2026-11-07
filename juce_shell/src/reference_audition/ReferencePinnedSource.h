#pragma once
#include "ReferenceSourceRevisionService.h"
#include "ReferenceSourceSnapshot.h"

namespace hypha::reference_audition::revision_detail
{
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER) || defined(KIRIN_HYPHA_TEST_WALL_CLOCK)
inline std::atomic<void (*) (const RuntimeSource&)> sourcePageOpenHookForTest { nullptr };
#endif
class PinnedSource final
{
public:
    explicit PinnedSource (std::shared_ptr<const RuntimeSource> sourceIn)
        : source (std::move (sourceIn)), authority (service().watch (*source)), snapshot (snapshotFor (*source))
    {
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER) || defined(KIRIN_HYPHA_TEST_WALL_CLOCK)
        if (auto hook = sourcePageOpenHookForTest.load()) hook (*source);
#endif
        file = snapshot->open();
    }
    std::unique_ptr<juce::InputStream> input() const
    {
        if (file) return std::make_unique<SharedInput> (file, snapshot);
        return std::make_unique<juce::MemoryInputStream> (nullptr, 0, false);
    }
    bool sameIdentity() const { return file != nullptr; }
    bool readSucceeded() const
    {
        auto* direct = dynamic_cast<VerifiedInput*> (file.get());
        if (direct == nullptr) return true;
        const auto failures = direct->failureCount();
        const bool succeeded = failures == observedFailures;
        observedFailures = failures; return succeeded;
    }
    bool verified (const RuntimeV2SourceRepository& repository, RuntimeFileRevision& revision) const
    {
        if (!sameIdentity()) return false;
        // The monitor is shared and asynchronous; no NAS stat is performed by a page fill.
        repository.verifySourceRevision (*source);
        if (authority->unavailable.load (std::memory_order_acquire) != 0) return false;
        revision = source->revision;
        return true;
    }
    std::shared_ptr<void> authorityOwner() const { return authority; }
    const std::atomic<std::uint64_t>* invalidation() const noexcept { return &authority->unavailable; }
    std::uint64_t initialInvalidation() const noexcept { return 0; }
    bool wasRejected() const noexcept { return file == nullptr; }
private:
    std::shared_ptr<const RuntimeSource> source;
    std::shared_ptr<Authority> authority;
    std::shared_ptr<SourceSnapshot> snapshot;
    std::shared_ptr<juce::InputStream> file;
    mutable std::uint64_t observedFailures = 0;
};
}
