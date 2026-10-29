#pragma once
#include "ReferenceSourceRevisionService.h"

namespace hypha::reference_audition::revision_detail
{
// Keep the decoder on the exact handle whose identity is checked before and after each page read.
class PinnedSource final
{
public:
    explicit PinnedSource (std::shared_ptr<const RuntimeSource> sourceIn)
        : source (std::move (sourceIn)), authority (service().watch (*source)),
          observedInvalidation (authority->invalidation.load (std::memory_order_acquire)),
          file (std::make_shared<OpenFile> (juce::File (source->absolutePath))) {}
    std::unique_ptr<juce::InputStream> input() const { return std::make_unique<SharedInput> (file); }
    bool sameIdentity() const
    {
        RuntimeFileRevision open;
        if (!file->revision (open)) return false;
        const bool same = sameObject (open, source->revision);
        if (!same) { service().noteFailure (*source); rejected.store (true, std::memory_order_release); }
        return same && !wasRejected();
    }
    bool verified (const RuntimeV2SourceRepository& repository, RuntimeFileRevision& revision) const
    {
        const auto checked = repository.checkSourceRevision (*source);
        if (checked.state == SourceRevisionState::refused || wasRejected()) rejected.store (true, std::memory_order_release);
        if (checked.state != SourceRevisionState::matched || wasRejected()) return false;
        RuntimeFileRevision open, path;
        if (!file->revision (open) || !repository.readSourceRevision (*source, path))
            return false;
        if (!sameObject (open, source->revision) || !sameObject (path, source->revision))
        { service().noteFailure (*source); rejected.store (true, std::memory_order_release); return false; }
        if (!equal (open, checked.revision) || !equal (path, checked.revision) || wasRejected()) return false;
        revision = checked.revision;
        return true;
    }
    std::shared_ptr<void> authorityOwner() const { return authority; }
    const std::atomic<std::uint64_t>* invalidation() const noexcept { return &authority->invalidation; }
    std::uint64_t initialInvalidation() const noexcept { return observedInvalidation; }
    bool wasRejected() const noexcept
    {
        return rejected.load (std::memory_order_acquire)
            || authority->invalidation.load (std::memory_order_acquire) != observedInvalidation;
    }
private:
    mutable std::atomic<bool> rejected { false };
    std::shared_ptr<const RuntimeSource> source;
    std::shared_ptr<Authority> authority;
    std::uint64_t observedInvalidation = 0;
    std::shared_ptr<OpenFile> file;
};
}
