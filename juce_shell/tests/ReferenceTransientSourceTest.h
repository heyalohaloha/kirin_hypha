#pragma once
inline void testTransientSourceFailures (const juce::File& root, const std::shared_ptr<const ref::RuntimeSource>& source)
{
    require (source != nullptr, "source available for transient verification");
    const auto path = [] (const juce::File& f, ref::RuntimeFileRevision& r)
        { ref::revision_detail::OpenFile opened (f); return opened.revision (r); };
    {
        ref::RuntimeV2SourceRepository repository (root);
        ref::revision_detail::PinnedSource pinned (source);
        ref::RuntimeFileRevision revision;
        ref::revision_detail::pathFailureForTest.store (true);
        require (!pinned.verified (repository, revision), "unavailable path pauses page");
        require (!pinned.wasRejected(), "unavailable path does not reject page");
        require (pinned.verified (repository, revision), "page recovers after path returns");
    }
    for (int failure : { 1, 2 })
    {
        ref::revision_detail::Service isolated;
        const auto watched = isolated.watch (*source);
        const auto before = watched->invalidation.load();
        ref::revision_detail::verifyFailureForTest.store (failure);
        require (isolated.verifyInitial (*source, source->revision, path) == "reference_source_read_failed",
                 "transient read failure is retryable");
        require (watched->invalidation.load() == before, "transient read does not invalidate pages");
        require (isolated.verifyInitial (*source, source->revision, path).isEmpty(),
                 "verification retries and recovers");
    }
    auto older = *source;
    older.revision.changedTimeNs = "0";
    ref::revision_detail::Service worker;
    const auto watched = worker.watch (older);
    const auto before = watched->invalidation.load();
    ref::revision_detail::verifyFailureForTest.store (2);
    require (worker.check (older, source->revision, path).state == ref::SourceRevisionState::revalidate,
             "metadata change starts worker verification");
    require (worker.waitForCompletion (older, std::chrono::seconds (30)), "worker failure completes");
    require (worker.check (older, source->revision, path).reason == "reference_source_read_failed"
             && watched->invalidation.load() == before, "worker failure waits without rejecting pages");
    worker.retry (older);
    require (worker.check (older, source->revision, path).state == ref::SourceRevisionState::revalidate,
             "retry starts a fresh hash");
    require (worker.waitForCompletion (older, std::chrono::seconds (30)), "worker retry completes");
    require (worker.check (older, source->revision, path).state == ref::SourceRevisionState::matched,
             "worker retry restores verified source");
}
