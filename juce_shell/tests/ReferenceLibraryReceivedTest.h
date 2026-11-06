#pragma once
#include "../src/reference_audition/ReferenceLibraryReceived.h"
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace
{
ref::RuntimeIdentity receiptIdentity()
{ return { "12345678-1234-4234-8234-123456789abc", {}, 42, true, "1.1.55" }; }

juce::File receiptFile (const juce::File& root, const ref::RuntimeIdentity& id)
{ return root.getChildFile ("library/received/" + id.runtimeInstanceId + ".json"); }

// A notification barrier holds a real store call, without scheduler-dependent polling.
struct ReceiptStoreBarrier
{
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    int calls = 0, completed = 0, active = 0, maximumActive = 0;
    bool store (const juce::File& file, const juce::String& text)
    {
        {
            std::unique_lock<std::mutex> lock (mutex);
            ++calls; maximumActive = std::max (maximumActive, ++active);
            entered = true; changed.notify_all();
            changed.wait (lock, [&] { return released; });
        }
        const bool written = hypha::storage::writeText (file, text, false, false, "\n");
        { const std::lock_guard<std::mutex> lock (mutex); --active; ++completed; changed.notify_all(); }
        return written;
    }
    void awaitEntry()
    {
        std::unique_lock<std::mutex> lock (mutex);
        require (changed.wait_for (lock, std::chrono::seconds (30), [&] { return entered; }),
                 "the receipt store entered behind the worker");
    }
    void release()
    { const std::lock_guard<std::mutex> lock (mutex); released = true; changed.notify_all(); }
    void hold()
    { const std::lock_guard<std::mutex> lock (mutex); released = false; entered = false; }
    bool awaitCompleted (int count)
    {
        std::unique_lock<std::mutex> lock (mutex);
        return changed.wait_for (lock, std::chrono::seconds (5), [&] { return completed >= count; });
    }
};

void testLibraryReceivedWriter (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("receipt-writer");
    auto id = receiptIdentity();
    ref::RuntimeWorkspace workspace;
    workspace.library = true; workspace.manifest.revision = 7;
    workspace.librarySkipped = { {"Unreadable name", true} };
    workspace.setsSkipped = { {"Unavailable item", false} };
    const auto now = juce::Time::currentTimeMillis();
    const auto file = receiptFile (root, id);
    int writes = 0;
    ref::LibraryReceivedWriter writer (root, [&] (const auto& target, const auto& text) {
        ++writes; return hypha::storage::writeText (target, text, false, false, "\n");
    });
    writer.observe (id, workspace, now); writer.finish();
    auto json = juce::JSON::parse (file);
    require (json.getDynamicObject() && hasExactKeys (*json.getDynamicObject(), {
        "format", "version", "runtime_instance_id", "manifest_revision", "received_at_ms", "hypha_version", "skipped" }),
        "the receipt has exactly the agreed fields");
    require (json["format"] == "kirin_hypha_reference_library_received" && json["version"] == "1.0"
        && json["runtime_instance_id"].toString() == file.getFileNameWithoutExtension()
        && static_cast<juce::int64> (json["manifest_revision"]) == 7
        && static_cast<juce::int64> (json["received_at_ms"]) == now && json["hypha_version"] == "1.1.55",
        "the receipt binds the instance, accepted revision, time and product version");
    require (json["skipped"].size() == 2 && json["skipped"][0]["name"] == "Unreadable name"
        && json["skipped"][0]["reason"] == "name_unreadable" && json["skipped"][1]["reason"] == "unreadable",
        "both manifest and sets items are returned with the agreed reasons");
    const auto original = file.loadFileAsString();
    writer.observe (id, workspace, now + 1); writer.finish();
    require (writes == 1 && file.loadFileAsString() == original, "unchanged content is not written again");
    workspace.setsSkipped.clear();
    writer.observe (id, workspace, now + 2); writer.finish();
    require (writes == 2 && juce::JSON::parse (file)["skipped"].size() == 1,
             "sets arriving at the same manifest revision update the receipt");
    const auto updated = file.loadFileAsString();
    require (file.deleteFile(), "simulate another instance completing stale cleanup after this receipt was replaced");
    writer.observe (id, workspace, now + 3); writer.finish();
    require (writes == 3 && file.loadFileAsString() == updated, "the next refresh restores the deleted receipt unchanged");
    workspace.manifest.revision = 6;
    writer.observe (id, workspace, now + 4); writer.finish();
    require (writes == 3 && file.loadFileAsString() == updated, "a lower revision cannot overwrite the instance maximum");
    auto other = id; other.runtimeInstanceId = "87654321-4321-4321-8321-cba987654321";
    writer.observe (other, workspace, now + 5); writer.finish();
    require (writes == 4 && receiptFile (root, other).existsAsFile(), "the maximum belongs to each instance separately");
    writer.observe (id, workspace, now + 6); writer.finish();
    require (writes == 4 && file.loadFileAsString() == updated, "returning to an instance retains its maximum revision");

    const auto limited = sandbox.getChildFile ("receipt-limits");
    ref::LibraryReceivedWriter limits (limited);
    workspace.manifest.revision = 8; workspace.setsSkipped.clear();
    workspace.librarySkipped.assign (300, { "Item", false });
    limits.observe (id, workspace, now); limits.finish();
    require (juce::JSON::parse (receiptFile (limited, id))["skipped"].size() == 256, "receipt rows stop at 256");
    workspace.manifest.revision = 9;
    const auto unicode = juce::String::repeatedString (juce::String::charToString (0x2000b), 300);
    workspace.librarySkipped.assign (256, { unicode, false });
    limits.observe (id, workspace, now); limits.finish();
    json = juce::JSON::parse (receiptFile (limited, id));
    require (receiptFile (limited, id).getSize() <= 64 * 1024 && json["skipped"].size() > 0
        && json["skipped"].size() < 256 && json["skipped"][0]["name"] == unicode,
        "UTF-8 bytes bound the receipt, retaining whole rows and non-BMP names");
    workspace.manifest.revision = 10;
    workspace.librarySkipped = { { juce::String::charToString (0xd800), true } };
    limits.observe (id, workspace, now); limits.finish();
    require (juce::JSON::parse (receiptFile (limited, id))["skipped"][0]["name"] == "", "an unpaired surrogate is not emitted");

    ref::LibraryReceivedWriter invalid (sandbox.getChildFile ("receipt-invalid"));
    for (const auto& bad : { id.runtimeInstanceId.toUpperCase(), juce::String ("../escape"),
                           juce::String ("12345678-1234-1234-8234-123456789abc") })
    { auto rejected = id; rejected.runtimeInstanceId = bad; invalid.observe (rejected, workspace, now); }
    workspace.library = false; invalid.observe (id, workspace, now); invalid.finish();
    require (! sandbox.getChildFile ("receipt-invalid/library/received").exists(), "Work and invalid UUID identities write nothing");
}

void testLibraryReceivedBehindWorker (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("receipt-queue");
    auto id = receiptIdentity(); ref::RuntimeWorkspace workspace;
    workspace.library = true; workspace.manifest.revision = 1;
    ReceiptStoreBarrier barrier;
    ref::LibraryReceivedWriter writer (root, [&] (const auto& file, const auto& text) { return barrier.store (file, text); });
    const auto now = juce::Time::currentTimeMillis();
    writer.observe (id, workspace, now); barrier.awaitEntry();
    for (const auto revision : { 2, 3, 2 })
    { workspace.manifest.revision = revision; writer.observe (id, workspace, now + revision); }
    require (barrier.calls == 1, "pending updates do not start another store while I/O is held");
    barrier.release(); writer.finish();
    writer.observe (id, workspace, now + 4); writer.finish();
    require (barrier.calls == 2 && barrier.maximumActive == 1
        && static_cast<juce::int64> (juce::JSON::parse (receiptFile (root, id))["manifest_revision"]) == 3,
        "one pending value coalesces updates and cannot roll back while the earlier write completes");

    ReceiptStoreBarrier stoppedBarrier;
    ref::LibraryReceivedWriter stopped (sandbox.getChildFile ("receipt-stop"),
        [&] (const auto& file, const auto& text) { return stoppedBarrier.store (file, text); });
    stopped.observe (id, workspace, now); stoppedBarrier.awaitEntry();
    workspace.manifest.revision = 4; stopped.observe (id, workspace, now);
    auto stop = std::async (std::launch::async, [&] { stopped.stop(); });
    require (stop.wait_for (std::chrono::seconds (5)) == std::future_status::ready,
             "stop returns while the independent store job is still held");
    stop.get(); stoppedBarrier.release(); stopped.finish(); stopped.observe (id, workspace, now);
    require (stoppedBarrier.calls == 1, "stopping never starts the pending write");

    ref::LibraryReceivedWriter failed (sandbox.getChildFile ("receipt-exception"),
        [] (const auto&, const auto&) -> bool { throw std::runtime_error ("fixture I/O failure"); });
    failed.observe (id, workspace, now); failed.finish(); failed.observe (id, workspace, now);
}

void testLibraryReceivedCleanup (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("receipt-cleanup");
    auto id = receiptIdentity(); ref::RuntimeWorkspace workspace;
    workspace.library = true; workspace.manifest.revision = 1;
    const auto folder = root.getChildFile ("library/received");
    require (folder.createDirectory(), "disposable receipt cleanup folder");
    // Some file timestamp APIs retain whole seconds only; keep the exact boundary representable.
    const auto now = juce::Time::currentTimeMillis() / 1000 * 1000;
    const auto stale = juce::Time (now - ref::LibraryReceivedWriter::lifetimeMs - 1000);
    const auto expired = folder.getChildFile ("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa.json");
    const auto boundary = folder.getChildFile ("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb.json");
    const auto uppercase = folder.getChildFile ("CCCCCCCC-CCCC-4CCC-8CCC-CCCCCCCCCCCC.json");
    const auto unrelated = folder.getChildFile ("notes.json");
    const auto wrongVersion = folder.getChildFile ("dddddddd-dddd-1ddd-8ddd-dddddddddddd.json");
    const auto directory = folder.getChildFile ("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee.json");
    for (const auto& file : { expired, boundary, uppercase, unrelated, wrongVersion })
        require (hypha::storage::writeText (file, "fixture") && file.setLastModificationTime (stale), "age a disposable file");
    require (boundary.setLastModificationTime (juce::Time (now - ref::LibraryReceivedWriter::lifetimeMs))
        && directory.createDirectory(), "retain the exact lifetime boundary and directories");
    ref::LibraryReceivedWriter writer (root); writer.observe (id, workspace, now); writer.finish();
    require (! expired.exists() && boundary.existsAsFile() && uppercase.existsAsFile() && unrelated.existsAsFile()
        && wrongVersion.existsAsFile() && directory.isDirectory() && receiptFile (root, id).existsAsFile(),
        "only expired files of the lowercase UUID v4 name shape are removed");
    for (int i = 0; i < 300; ++i)
    {
        const auto file = folder.getChildFile ("00000000-0000-4000-8000-" + juce::String (i).paddedLeft ('0', 12) + ".json");
        require (hypha::storage::writeText (file, "fixture") && file.setLastModificationTime (stale), "bounded cleanup fixture");
    }
    workspace.manifest.revision = 2; writer.observe (id, workspace, now); writer.finish();
    int remaining = 0;
    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, "00000000-*.json"))
        if (file.existsAsFile()) ++remaining;
    require (remaining >= 44 && remaining < 300, "one cleanup scans at most 256 files");
}
}

#include "ReferenceLibraryReceivedRuntimeTest.h"

namespace
{
void testLibraryReceived (const juce::File& sandbox)
{
    testLibraryReceivedWriter (sandbox);
    testLibraryReceivedBehindWorker (sandbox);
    testLibraryReceivedCleanup (sandbox);
    testLibraryReceivedRoles (sandbox);
    testLibraryReceiptBlockedKeepsAudition (sandbox);
    testLibraryReceiptControllerDestruction (sandbox);
    for (bool queued : { false, true }) testLibraryReceiptPreparedMaintenance (sandbox, queued);
    std::cout << "Reference library received: schema, queue, recovery, revision, cleanup and C audio PASS\n";
}
}
