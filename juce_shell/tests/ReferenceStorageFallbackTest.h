#pragma once
#include "ReferenceManualService.h"
#include "../src/AtomicFile.h"
#include "ReferenceCheckSwitchTest.h"
#include "../src/reference_audition/ReferenceVerifiedReader.h"
#include "../src/reference_audition/ReferenceVisualAudio.h"

inline juce::File fallbackHeaderTarget;
inline std::atomic<bool> fallbackHeaderChanged { false };
inline void overwriteFallbackHeader (const ref::RuntimeSource& source)
{
    if (source.absolutePath != fallbackHeaderTarget.getFullPathName()
        || ref::revision_detail::sourceReaderOpenHookForTest.exchange (nullptr) == nullptr) return;
    juce::FileOutputStream output (fallbackHeaderTarget);
    require (output.openedOk() && output.setPosition (0) && output.writeByte (0), "overwrite the next source only at its real decoder-open boundary");
    output.flush(); require (output.getStatus().wasOk(), "flush the fixture header");
    fallbackHeaderChanged.store (true);
}
inline bool runSnapshotOwnerChild (int argc, char** argv)
{
    if (argc != 3 || juce::String (argv[1]) != "--snapshot-owner-child") return false;
    ref::revision_detail::SnapshotLease lease (juce::File {juce::String (argv[2])});
    if (!lease.available() || !hypha::storage::writeText (lease.payload(), "fixture")) std::_Exit (2);
    std::cout << "ready" << std::flush;
    juce::WaitableEvent stopped; stopped.wait (-1);
    std::_Exit (3);
}
inline void testSnapshotRestartCleanup (const juce::File& sandbox)
{
    namespace revision = ref::revision_detail;
    const auto root = sandbox.getChildFile ("snapshot-leases");
    revision::SnapshotLease live (root);
    require (live.available() && hypha::storage::writeText (live.payload(), "active"), "active snapshot lease fixture");
    const auto old = juce::Time::getCurrentTime() - juce::RelativeTime::days (2);
    require (live.setModificationTimeForTest (old), "age an active owner's lease");
    const auto unrelated = root.getChildFile ("unrelated");
    require (unrelated.createDirectory().wasOk() && hypha::storage::writeText (unrelated.getChildFile ("audio.snapshot"), "keep"),
             "unrelated temporary material");
    juce::ChildProcess child;
    require (child.start (juce::StringArray { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
        "--snapshot-owner-child", root.getFullPathName() }), "start only the fixture child process");
    std::array<char, 5> ready {};
    int received = 0;
    while (received < 5)
    {
        const auto n = child.readProcessOutput (ready.data() + received, 5 - received);
        if (n <= 0) break;
        received += n;
    }
    require (received == 5 && std::string (ready.data(), ready.size()) == "ready", "child owns a real kernel lease before termination");
    require (child.kill() && child.waitForProcessToFinish (30000), "terminate only the owned child process without invoking destructors");
    juce::File abandoned;
    for (const auto& entry : juce::RangedDirectoryIterator (root, false, "*", juce::File::findDirectories))
        if (entry.getFile() != live.payload().getParentDirectory() && entry.getFile() != unrelated) abandoned = entry.getFile();
    require (abandoned.isDirectory() && abandoned.getChildFile ("audio.snapshot").existsAsFile(), "abnormal exit leaves its payload");
    require (abandoned.getChildFile ("owner.lock").setLastModificationTime (old), "age only the abandoned fixture");
    // A new owner runs the production startup collection, with no fabricated process status.
    revision::SnapshotLease restarted (root);
    require (restarted.available() && !abandoned.exists() && live.payload().existsAsFile()
        && unrelated.getChildFile ("audio.snapshot").existsAsFile(), "restart reclaims the orphan while protecting active and unrelated owners");
}

inline void testReferenceStorageFallback (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    namespace revision = ref::revision_detail;
    for (int failure = -1; failure < 3; ++failure)
    {
        const auto root = sandbox.getChildFile ("storage-fallback-" + juce::String (failure));
        require (root.createDirectory().wasOk(), "isolated storage fallback fixture");
        publishTwoCheckLibrary (root, 2, true, 480000);
        ref::ReferenceComparisonController c (root);
        control::stopWorkers (c); c.configure ({"storage-post", {}, 42, true}, 48000, 2);
        require (((juce::Thread&) control::sourceVisual (c, {})).stopThread (-1), "deterministic visual owner");
        struct Reset { ~Reset() {
            revision::snapshotFreeBytesForTest.store (-1); revision::snapshotWriteLimitForTest.store (-1);
            revision::unchangedMetadataForTest.store (nullptr); revision::snapshotRootForTest.store (nullptr);
            revision::sourceReaderOpenHookForTest.store (nullptr); revision::observeOriginalReadsForTest ({});
        } } reset;
        const auto blockedRoot = root.getChildFile ("blocked-cache");
        if (failure == 1)
        {
            require (hypha::storage::writeText (blockedRoot, "not a directory"), "make the fixture cache destination unwritable");
            revision::snapshotRootForTest.store (&blockedRoot);
        }
        if (failure == 0) revision::snapshotFreeBytesForTest.store (128 * 1024 * 1024);
        else if (failure == 2) revision::snapshotWriteLimitForTest.store (64 * 1024 + 128);
        revision::observeOriginalReadsForTest (root.getChildFile ("source.wav").getFullPathName());
        const auto bytes = revision::sourceHashBytesForTest.load();
        c.setPresented (true); control::service (c); c.observeTransport (0, true, true); control::service (c);
        auto& runtime = control::roleFor (c, {2});
        auto source = runtime.visualBinding().source;
        require (source && c.snapshot().checkReady && source->preparedAudio->snapshot->hasPrivateCopy() == (failure < 0)
            && source->preparedAudio->snapshot->storageNotice().isEmpty() == (failure < 0), "actual C prepares by direct verification while storage remains unavailable");
        std::cerr << "initial source hash bytes=" << (revision::sourceHashBytesForTest.load() - bytes) << " expected=" << juce::File (source->absolutePath).getSize() << "\n";
        require (revision::sourceHashBytesForTest.load() - bytes == static_cast<std::uint64_t> (juce::File (source->absolutePath).getSize()),
                 "initial preparation reads the original exactly once for SHA, receipts and optional copy");
        const auto originalBytes = revision::originalReadBytesForTest.load();
        const auto sourceBytes = static_cast<std::uint64_t> (juce::File (source->absolutePath).getSize());
        require (failure < 0 ? originalBytes == sourceBytes : originalBytes < 2 * sourceBytes,
                 "actual source reads are one pass with a copy, or one verification plus bounded playback pages without a copy");
        std::cerr << "original preparation bytes=" << originalBytes << " source bytes=" << sourceBytes << "\n";
        for (int role = 1; role <= 3; ++role)
            control::stopPageRefill (control::currentPagesFor (control::roleFor (c, {role}), {}));
        juce::AudioBuffer<float> block (2, 512);
        const auto render = [&] (std::int64_t at = 0) {
            for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (block.getWritePointer (ch), 0.125f, 512);
            c.observeTransport (at, true, true); c.observeAInput (block, at, true, true, true);
            beginReferenceRtProbe(); const auto sounded = c.renderSelectedB (block, at, true, true, true);
            require (endReferenceRtProbe() == 0, "direct fallback does no I/O or allocation on the audio callback");
            if (!sounded) require (block.getSample (0, 511) == 0.125f, "unverified output preserves A");
            return sounded;
        };
        require (c.requestAudition (2, -22, -6) && render() && block.getSample (0, 511) == 0.25f,
                 "actual C PCM sounds without recovering any temporary capacity");
#if JUCE_WINDOWS
        require (revision::service().waitForIdle (std::chrono::seconds (30)), "source audits finish before the exclusive-writer boundary");
        const auto exclusive = CreateFileW (source->absolutePath.toWideCharPointer(), GENERIC_WRITE, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require (exclusive != INVALID_HANDLE_VALUE, "an idle real C decoder permits an exclusive Windows source writer");
        const unsigned char first = 'R'; DWORD written = 0;
        const bool overwritten = WriteFile (exclusive, &first, 1, &written, nullptr) && written == 1 && FlushFileBuffers (exclusive);
        const bool closed = CloseHandle (exclusive) != 0;
        require (overwritten && closed && render(), "the same path overwrites while C stays selected and audible");
#endif
        const auto choices = c.snapshot().checkTargets;
        juce::MemoryBlock nextOriginal;
        fallbackHeaderTarget = root.getChildFile ("alternate.wav");
        if (failure == 0)
        {
            require (fallbackHeaderTarget.loadFileAsData (nextOriginal), "retain the next source before the decoder-open race");
            fallbackHeaderChanged.store (false); revision::sourceReaderOpenHookForTest.store (&overwriteFallbackHeader);
        }
        require (c.selectCheck (choices[1].id), "switch C while temporary storage is still failing");
        if (failure == 0)
        {
            control::service (c);
            require (fallbackHeaderChanged.load() && render() && block.getSample (0, 511) == 0.25f,
                     "a failed decoder open after verification preserves the real previous C PCM and requested switch");
            juce::FileOutputStream output (fallbackHeaderTarget);
            require (output.openedOk() && output.setPosition (0) && output.write (nextOriginal.getData(), nextOriginal.getSize()), "restore the next source bytes");
            output.flush(); require (output.getStatus().wasOk(), "flush the restored next source");
        }
        for (int poll = 0; poll < 16; ++poll)
        { control::service (c); c.servicePendingAudition (-22, -6, true); render(); }
        require (runtime.snapshot().checkId.endsWith ("aaaaaaaaaaaa") && render()
            && std::abs (block.getSample (0, 511) - 0.5f) < 1.0e-6f, "the actual requested C switches through authenticated direct reads");
        if (failure < 0) { c.selectA(); continue; }
        source = runtime.visualBinding().source;
        auto& pages = control::currentPagesFor (runtime, {});
        revision::unchangedMetadataForTest.store (source.get());
        control::probeSourceNow (*source);
        // Mutate an uncached chunk with all observed metadata held equal.
        const juce::File file (source->absolutePath);
        juce::MemoryBlock original; require (file.loadFileAsData (original), "retain direct-read fixture bytes");
        auto altered = original;
        const auto target = static_cast<juce::int64> (pages.cachedPageFrames()) * 9;
        const auto offset = static_cast<size_t> (target) * 2 * sizeof (float) + 128;
        require (offset < altered.getSize(), "uncached direct page lies in the fixture");
        static_cast<char*> (altered.getData())[offset] ^= 0x40;
        const auto overwrite = [&] (const juce::MemoryBlock& value) {
            juce::FileOutputStream out (file);
            require (out.openedOk() && out.setPosition (0) && out.write (value.getData(), value.getSize()), "overwrite only the direct fixture");
            out.flush(); require (out.getStatus().wasOk(), "flush direct fixture");
        };
        const auto ticket = runtime.normalSelectionTicket();
        const auto pageFrames = pages.cachedPageFrames();
        pages.request (target - 2 * pageFrames); control::servicePages (pages, {});
        require (render() && block.getSample (0, 511) == 0.5f, "cached C remains audible after warming other verified pages");
        overwrite (altered);
        pages.request (target - pageFrames); control::servicePages (pages, {});
        require (render() && block.getSample (0, 511) == 0.5f && runtime.normalSelectionTicket() == ticket,
                 "a failed prefetch cannot interrupt the verified current C or discard its selection");
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        auto guarded = revision::snapshotReader (formats, *source);
        juce::AudioBuffer<float> observed, scratch;
        require (guarded && !ref::readReferenceVisualAudio (*guarded, target, 512, 48000, 2, observed, scratch),
                 "a real Reference analysis read rejects codec zero-padding of an unauthenticated source chunk");
        pages.request (target); control::servicePages (pages, {});
        require (!pages.render (block, target, 1.0f) && !pages.hasPersistentReadFailure (target), "a same-metadata overwrite cannot leak altered PCM through a direct page fill");
        overwrite (original);
        require (ref::readReferenceVisualAudio (*guarded, target, 512, 48000, 2, observed, scratch)
            && observed.getSample (0, 511) == 0.5f, "the same guarded analysis reader restarts its codec after source recovery");
        control::probeSourceNow (*source);
        pages.request (target); control::servicePages (pages, {});
        require (pages.render (block, target, 1.0f) && block.getSample (0, 511) == 0.5f && render(), "the held C resumes at its verified position after exact bytes return");
        c.selectA();
    }
    testSnapshotRestartCleanup (sandbox);
}
