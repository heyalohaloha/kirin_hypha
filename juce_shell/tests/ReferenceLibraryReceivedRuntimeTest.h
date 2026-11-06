#pragma once
#include "ReferenceCheckSwitchTest.h"

namespace hypha::tests::reference_control
{
struct ReceiptTag {};
reference_audition::LibraryReceivedWriter& receiptFor (Runtime&, ReceiptTag);
template <auto Member> struct ReceiptAccess
{
    friend reference_audition::LibraryReceivedWriter& receiptFor (Runtime& runtime, ReceiptTag)
    { return runtime.*Member; }
};
template struct ReceiptAccess<&Runtime::libraryReceivedWriter>;
struct ReceiptCompletionTag {};
std::shared_future<bool> completionFor (const reference_audition::LibraryReceivedWriter&, ReceiptCompletionTag);
template <auto Member> struct ReceiptCompletionAccess
{
    friend std::shared_future<bool> completionFor (const reference_audition::LibraryReceivedWriter& writer, ReceiptCompletionTag)
    { return writer.*Member; }
};
template struct ReceiptCompletionAccess<&reference_audition::LibraryReceivedWriter::running>;
}

namespace
{
void testLibraryReceivedRoles (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    const auto root = kirin_library_fixture::copy (sandbox, "receipt-roles");
    require (root != juce::File(), "copy a disposable OS library for the real comparison entry");
    const auto identity = receiptIdentity();
    const auto manifestFile = root.getChildFile ("library/manifest.json");
    auto manifest = juce::JSON::parse (manifestFile);
    manifest.getDynamicObject()->setProperty ("revision", 2);
    const auto setsFile = root.getChildFile ("library/sets.json");
    auto initialSets = juce::JSON::parse (setsFile);
    initialSets.getDynamicObject()->setProperty ("manifest_revision", 2);
    require (writeJson (manifestFile, manifest) && writeJson (setsFile, initialSets)
        && writeJson (root.getChildFile ("library/manifests/2.json"), manifest), "publish a disposable revision above the minimum");
    const auto revision = static_cast<juce::int64> (manifest["revision"]);
    ref::ReferenceComparisonController comparison (root);
    control::stopWorkers (comparison);
    comparison.configure (identity, 48000, 2);
    auto& check = control::roleFor (comparison, {2});
    auto& writer = control::receiptFor (check, {});
    const auto refresh = [&] { control::service (comparison); writer.finish(); };
    refresh();
    const auto file = receiptFile (root, identity);
    const auto original = file.loadFileAsString();
    require (juce::JSON::parse (original)["manifest_revision"] == manifest["revision"]
        && root.getChildFile ("library/received").findChildFiles (juce::File::findFiles, false).size() == 1,
        "C alone reports the accepted library through the real comparison entry; B and V do not duplicate it");
    require (file.deleteFile(), "simulate cleanup removing another instance's freshly replaced receipt");
    refresh();
    require (file.loadFileAsString() == original, "an ordinary runtime refresh restores the exact deleted receipt");

    auto sets = juce::JSON::parse (setsFile);
    const auto cleanSets = sets.clone();
    sets["song_sets"][0]["songs"][1].getDynamicObject()->setProperty (
        "display_name", "Line" + juce::String::charToString (0x2028) + "break");
    require (writeJson (setsFile, sets), "publish a skipped set item at the same manifest revision");
    refresh();
    auto json = juce::JSON::parse (file);
    require (json["manifest_revision"] == manifest["revision"] && json["skipped"].size() == 1
        && json["skipped"][0]["name"] == "Line?break" && json["skipped"][0]["reason"] == "name_unreadable",
        "the real repository's skipped sets reach the receipt without a manifest revision change");
    require (writeJson (setsFile, cleanSets), "the sets publication catches up");
    refresh();
    require (juce::JSON::parse (file)["skipped"].size() == 0, "the next refresh clears the skipped row");
    const auto accepted = file.loadFileAsString();
    auto rejected = manifest.clone();
    rejected.getDynamicObject()->setProperty ("revision", revision + 1);
    rejected.getDynamicObject()->setProperty ("format", "unrecognised");
    require (writeJson (manifestFile, rejected), "publish a rejected newer manifest");
    refresh();
    require (file.loadFileAsString() == accepted, "a rejected publication never becomes a received receipt");

    auto older = manifest.clone(); older.getDynamicObject()->setProperty ("revision", revision - 1);
    require (revision > 1 && writeJson (manifestFile, older), "publish a valid older revision before reconfigure");
    comparison.configure (identity, 44100, 2);
    refresh();
    require (check.snapshot().manifestRevision == revision - 1 && file.loadFileAsString() == accepted,
        "reconfigure really reloads the older library but cannot roll back the instance receipt");
    require (file.deleteFile(), "remove the receipt while the workspace has an older revision");
    refresh();
    require (file.loadFileAsString() == accepted, "missing receipt recovery retains the instance maximum after reconfigure");
}

void testLibraryReceiptBlockedKeepsAudition (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    for (int failure = 0; failure < 3; ++failure)
    {
        const auto root = sandbox.getChildFile ("receipt-audio-" + juce::String (failure));
        require (root.createDirectory(), "isolated receipt failure audio fixture");
        publishTwoCheckLibrary (root, 2, false);
        const auto id = receiptIdentity();
        const auto file = receiptFile (root, id);
        ReceiptStoreBarrier barrier;
        std::atomic<int> writes {0};
        std::atomic<bool> stored {true};
        ref::LibraryReceivedWriter::Store store;
        if (failure == 0)
            require (hypha::storage::writeText (file.getParentDirectory(), "fixture blocker"), "block the receipt directory with a file");
        else
            store = [&] (const auto& target, const auto& text) {
                bool result = false;
                if (failure == 1) result = barrier.store (target, text);
                else result = hypha::storage::writeTextWithStream (target, text, false, false, "\n", [&] (const auto& temporary) {
                    auto stream = std::make_unique<juce::FileOutputStream> (temporary);
                    require (target.createDirectory() && hypha::storage::writeText (target.getChildFile ("blocker"), "fixture"),
                        "block atomic replacement only after the staging stream has opened");
                    return stream;
                });
                stored.store (result); writes.fetch_add (1); return result;
            };
        ref::RuntimeV2Controller runtime (root, {}, false, std::move (store));
        require (control::stopFor (runtime, {}), "stop the ordinary worker before deterministic servicing");
        control::stagedLaunchFor (runtime, {}) = std::launch::deferred;
        runtime.prepareSelectionsBeforeReturn(); runtime.reportLibraryReceived();
        runtime.configure (id, 48000, 2);
        runtime.observeTransport (0, true, true);
        if (failure == 1)
        {
            require (((juce::Thread&) runtime).startThread (juce::Thread::Priority::low), "restart the real C worker");
            barrier.awaitEntry(); // Keep I/O held throughout preparation, MATCH, playback and worker stop.
            const bool prepared = control::waitUntil (std::chrono::steady_clock::now() + std::chrono::seconds (30),
                [&] { return runtime.snapshot().auditionBuffered; });
            if (!prepared) barrier.release();
            require (prepared, "the real C worker prepares playback while the receipt store is held");
        }
        else
        {
            control::serviceFor (runtime, {});
            control::receiptFor (runtime, {}).finish();
            control::serviceFor (runtime, {});
        }
        require (runtime.snapshot().libraryReceived && runtime.snapshot().auditionBuffered,
            "receipt failure or held I/O cannot delay C preparation");
        require (runtime.selectB (-25, -6) && std::abs (runtime.snapshot().appliedGainDb + 3.0) < 1.0e-9,
            "C still MATCHes the measured live A with receipt I/O unavailable");
        juce::AudioBuffer<float> block (2, 512);
        for (int at = 0; at < 2048; at += 512)
        {
            for (int channel = 0; channel < 2; ++channel)
                juce::FloatVectorOperations::fill (block.getWritePointer (channel), 0.125f, 512);
            runtime.observeTransport (at, true, true);
            beginReferenceRtProbe();
            const bool rendered = runtime.renderSelectedB (block, at, true, true);
            require (endReferenceRtProbe() == 0 && rendered, "receipt failure keeps actual C audio allocation-free");
        }
        const auto expected = 0.25f * static_cast<float> (std::pow (10.0, -3.0 / 20.0));
        for (int channel = 0; channel < 2; ++channel)
            for (int frame = 0; frame < 512; ++frame)
                require (std::abs (block.getSample (channel, frame) - expected) < 1.0e-6f,
                    "the reference PCM and MATCH gain reach the output despite receipt I/O failure");
        if (failure == 1)
        {
            auto stop = std::async (std::launch::async, [&] { return control::stopFor (runtime, {}); });
            const bool completed = stop.wait_for (std::chrono::seconds (5)) == std::future_status::ready;
            barrier.release();
            require (completed && stop.get(), "worker shutdown never waits for the held receipt store");
        }
        control::receiptFor (runtime, {}).finish();
        require (failure == 0 ? !file.exists() : writes.load() == 1 && stored.load() == (failure == 1),
            "the failure fixture exercised the requested folder, held write or atomic replacement outcome");
        runtime.selectA();
    }
}

void testLibraryReceiptControllerDestruction (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile ("receipt-destruction");
    require (root.createDirectory(), "isolated controller destruction fixture");
    publishTwoCheckLibrary (root, 2, false);
    auto barrier = std::make_shared<ReceiptStoreBarrier>();
    auto runtime = std::make_unique<ref::RuntimeV2Controller> (root, ref::RuntimeV2Controller::SelectionGate {}, false,
        [barrier] (const auto& file, const auto& text) { return barrier->store (file, text); });
    runtime->reportLibraryReceived();
    const auto id = receiptIdentity();
    runtime->configure (id, 48000, 2);
    barrier->awaitEntry();
    require (control::stopFor (*runtime, {}), "stop the actual worker while notification I/O remains held");
    auto completion = control::completionFor (control::receiptFor (*runtime, {}), {});
    ref::RuntimeWorkspace next; next.library = true; next.manifest.revision = 2;
    control::receiptFor (*runtime, {}).observe (id, next, juce::Time::currentTimeMillis());
    auto destroy = std::async (std::launch::async, [owned = std::move (runtime)] () mutable { owned.reset(); });
    const bool destroyed = destroy.wait_for (std::chrono::seconds (5)) == std::future_status::ready;
    // Always release before asserting, so a regression fails rather than hanging the suite.
    barrier->release(); destroy.get();
    require (completion.get(), "the independent job safely completes using owned inputs after controller destruction");
    require (destroyed && barrier->calls == 1 && barrier->active == 0
        && juce::JSON::parse (receiptFile (root, id))["manifest_revision"] == juce::var (1),
        "actual controller destruction returns before held I/O and never launches its pending receipt");
}

void testLibraryReceiptPreparedMaintenance (const juce::File& sandbox, bool queued)
{
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile (queued ? "receipt-prepared-queue" : "receipt-prepared-deleted");
    require (root.createDirectory(), "isolated C preparation receipt fixture");
    const auto preset = publishTwoCheckLibrary (root, 2, false);
    const auto id = receiptIdentity();
    ReceiptStoreBarrier barrier;
    barrier.release();
    ref::RuntimeV2Controller runtime (root, {}, false,
        [&barrier] (const auto& file, const auto& text) { return barrier.store (file, text); });
    require (control::stopFor (runtime, {}), "stop C before deterministic initial preparation");
    control::stagedLaunchFor (runtime, {}) = std::launch::deferred;
    runtime.prepareSelectionsBeforeReturn(); runtime.reportLibraryReceived();
    runtime.configure (id, 48000, 2); runtime.observeTransport (0, true, true);
    auto& writer = control::receiptFor (runtime, {});
    control::serviceFor (runtime, {}); writer.finish(); control::serviceFor (runtime, {});
    require (runtime.selectB (-22, -6), "explicitly MATCH and start C before switching its Check");
    const auto audible = runtime.snapshot();
    if (queued)
    {
        barrier.hold();
        for (int revision : { 2, 3 })
        {
            require (writeJson (root.getChildFile ("library/manifest.json"), libraryManifest (root, preset, revision)),
                "publish a real newer library without changing the audible C");
            control::serviceFor (runtime, {});
            if (revision == 2) barrier.awaitEntry();
        }
    }
    require (runtime.selectCheck (preset["checks"][1]["check_id"].toString()), "select the next C Check");
    control::serviceFor (runtime, {});
    require (runtime.awaitingPreparedLevel(), "withhold the next MATCH window with the old C still selected");
    const auto ticket = runtime.normalSelectionTicket();
    const auto file = receiptFile (root, id);
    const auto receipt = file.loadFileAsString();
    if (!queued) require (file.deleteFile(), "delete the accepted receipt during C's measurement wait");
    require (((juce::Thread&) runtime).startThread (juce::Thread::Priority::low), "run the real worker while C waits for MATCH");
    barrier.release();
    const bool maintained = barrier.awaitCompleted (queued ? 3 : 2);
    require (control::stopFor (runtime, {}), "stop servicing before reading writer-owned state");
    writer.finish();
    require (maintained && runtime.awaitingPreparedLevel() && runtime.normalSelectionTicket() == ticket,
        "receipt recovery and pending submission progress without releasing the C measurement wait or selection");
    require (queued ? juce::JSON::parse (file)["manifest_revision"] == juce::var (3) : file.loadFileAsString() == receipt,
        "the worker restores the exact receipt or submits the coalesced latest revision during preparation");
    const auto state = runtime.snapshot();
    require (state.bSelected && state.checkId == audible.checkId
        && std::memcmp (&state.appliedGainDb, &audible.appliedGainDb, sizeof (double)) == 0,
        "notification maintenance retains the original C and its approved MATCH gain");
    juce::AudioBuffer<float> block (2, 512);
    for (int at = 0; at < 2048; at += 512)
    {
        for (int channel = 0; channel < 2; ++channel)
            juce::FloatVectorOperations::fill (block.getWritePointer (channel), 0.125f, 512);
        runtime.observeTransport (at, true, true);
        beginReferenceRtProbe();
        const bool rendered = runtime.renderSelectedB (block, at, true, true);
        require (endReferenceRtProbe() == 0 && rendered, "the retained C renders without callback allocation during maintenance");
    }
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < 512; ++frame)
            require (block.getSample (channel, frame) == 0.25f, "receipt maintenance never plays the unapproved next Cue");
    require (barrier.maximumActive == 1, "preparation maintenance keeps one notification store active");
    runtime.selectA();
}
}
