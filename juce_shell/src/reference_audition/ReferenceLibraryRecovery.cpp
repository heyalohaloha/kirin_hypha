#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceLibraryOpenOutcome.h"
#include "ReferenceRuntimeRepositoryParsing.h"
#include "ReferenceReceiptModuleLifetime.h"
#include "../AtomicFile.h"
#include <chrono>
#include <system_error>
#include <thread>
namespace hypha::reference_audition
{
namespace
{
juce::var receivedRow (const RuntimeSkippedItem& item)
{
    auto* row = new juce::DynamicObject();
    const bool writable = RuntimeEventTransport::canonicalJson (item.name).isNotEmpty();  // 読めない名前は空
    row->setProperty ("name", writable ? item.name : juce::String {});
    row->setProperty ("reason", item.nameUnreadable ? "name_unreadable" : "unreadable");
    return juce::var (row);
}

juce::String receivedJson (const RuntimeIdentity& identity, std::int64_t revision,
                           const std::vector<RuntimeSkippedItem>& skipped, std::int64_t nowMs)
{
    juce::Array<juce::var> rows;
    auto* object = new juce::DynamicObject();
    const juce::var document (object);
    object->setProperty ("format", "kirin_hypha_reference_library_received");
    object->setProperty ("version", "1.0");
    object->setProperty ("runtime_instance_id", identity.runtimeInstanceId);
    object->setProperty ("manifest_revision", static_cast<juce::int64> (revision));
    object->setProperty ("received_at_ms", static_cast<juce::int64> (nowMs));
    object->setProperty ("hypha_version", identity.hyphaVersion);
    object->setProperty ("skipped", rows);
    auto bytes = RuntimeEventTransport::canonicalJson (document).getNumBytesAsUTF8();
    for (const auto& item : skipped)
    {
        const auto row = receivedRow (item);
        bytes += RuntimeEventTransport::canonicalJson (row).getNumBytesAsUTF8() + 1;
        if (rows.size() == LibraryReceivedWriter::maximumRows
            || bytes > static_cast<size_t> (LibraryReceivedWriter::maximumBytes)) break;
        rows.add (row);
    }
    object->setProperty ("skipped", rows);
    const auto text = RuntimeEventTransport::canonicalJson (document);
    return text.getNumBytesAsUTF8() <= static_cast<size_t> (LibraryReceivedWriter::maximumBytes) ? text : juce::String {};
}

void removeExpiredReceipts (const juce::File& folder, std::int64_t nowMs, const std::atomic<bool>& stopped)
{
    int scanned = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFiles))
    {
        if (stopped.load (std::memory_order_acquire)) return;
        const auto file = entry.getFile();
        if (file.getFileExtension() == ".json" && runtime_repository_parsing::uuidV4 (file.getFileNameWithoutExtension())
            && nowMs - entry.getModificationTime().toMilliseconds() > LibraryReceivedWriter::lifetimeMs)
            file.deleteFile();
        if (++scanned == LibraryReceivedWriter::maximumScannedFiles) return;
    }
}
}

LibraryReceivedWriter::LibraryReceivedWriter (juce::File transportRoot, Store storeIn)
    : folder (transportRoot.getChildFile ("library/received")),
      store (storeIn ? std::move (storeIn) : Store { [] (const juce::File& file, const juce::String& text)
          { return hypha::storage::writeText (file, text, false, false, "\n"); } })
{
}

LibraryReceivedWriter::~LibraryReceivedWriter()
{
    stop();
}

void LibraryReceivedWriter::stop() noexcept { stopped->store (true, std::memory_order_release); }

void LibraryReceivedWriter::finish()
{
    if (running.valid()) running.wait();
}

void LibraryReceivedWriter::observe (const RuntimeIdentity& identity, const RuntimeWorkspace& workspace, std::int64_t nowMs)
{
    if (stopped->load (std::memory_order_acquire)) return;
    const auto& id = identity.runtimeInstanceId;
    if (! workspace.library || identity.hyphaVersion.isEmpty() || ! runtime_repository_parsing::uuidV4 (id)) return;
    auto skipped = workspace.librarySkipped;
    skipped.insert (skipped.end(), workspace.setsSkipped.begin(), workspace.setsSkipped.end());
    const auto revision = workspace.manifest.revision;
    auto& highest = highestRevisions[id];
    if (revision >= highest && (id != latestId || revision != latestRevision || skipped != latestSkipped))
    {
        highest = revision;
        latestId = id;
        latestRevision = revision;
        latestSkipped = std::move (skipped);
        latestText = receivedJson (identity, revision, latestSkipped, nowMs);
        writePending = latestText.isNotEmpty();
        latestStored = false;
    }
    service (nowMs);
}

void LibraryReceivedWriter::service (std::int64_t nowMs)
{
    if (stopped->load (std::memory_order_acquire)) return;
    if (running.valid() && running.wait_for (std::chrono::seconds (0)) == std::future_status::ready)
    {
        bool written = false;
        try { written = running.get(); } catch (...) {} // 通知の失敗を C の作業スレッドへ伝えない
        running = {};
        latestStored = written && ! writePending;
    }
    if (running.valid()) return;
    if (latestStored && ! folder.getChildFile (latestId + ".json").existsAsFile())
    {
        writePending = true;
        latestStored = false;
    }
    if (writePending) startWrite (nowMs);
}

void LibraryReceivedWriter::startWrite (std::int64_t nowMs)
{
    writePending = false;
    if (! receipt_detail::retainModule()) return; // 通知だけを見送る。C の準備と再生は続ける
    try
    {
        // 入力と停止フラグはジョブが所有し、コードは loader が保持する。破棄側は保存を待たない。
        std::packaged_task<bool()> job (
            [directory = folder, save = store, cancelled = stopped,
             target = folder.getChildFile (latestId + ".json"), text = latestText, nowMs]
        {
            if (cancelled->load (std::memory_order_acquire)) return false;
            bool written = false;
            try { written = directory.createDirectory() && save (target, text); } catch (...) {}
            if (!cancelled->load (std::memory_order_acquire)) removeExpiredReceipts (directory, nowMs, *cancelled);
            return written;
        });
        running = job.get_future().share();
        std::thread (std::move (job)).detach();
    }
    catch (const std::system_error&) { running = {}; } // thread を作れないときは書けないときと同じ
}

bool RuntimeV2Controller::requestLibraryRecovery()
{
    if (! libraryOnline.load (std::memory_order_acquire)) return false;
    if (pendingLibraryOpen != juce::File()) return true;
    const RuntimePreset* targetPreset = nullptr;
    if (workspace)
        for (const auto& preset : workspace->presets)
            if (preset.sourcePresetArtifact.presetId == currentSnapshot.presetId)
            { targetPreset = &preset; break; }
    const RuntimeCheck* targetCheck = nullptr;
    if (targetPreset != nullptr)
        for (const auto& check : targetPreset->checks)
            if (check.checkId == currentSnapshot.checkId)
            { targetCheck = &check; break; }
    // B の曲と V（Version の項目）は Kirin OS の Preset ではないので、Preset を開く依頼は送らない（Kirin OS は
    // Preset として見つけられない。直し方は状態の行が言う）。
    if (versionComparison || ! requestedConfiguration.identity.valid() || targetPreset == nullptr
        || targetCheck == nullptr || targetPreset->songEntry) return false;
    bool opensBalanceSettings = false;
    for (const auto& binding : targetCheck->viewBindings)
        opensBalanceSettings = opensBalanceSettings || binding == "balance";
    const auto id = juce::Uuid().toDashedString().toLowerCase();
    const auto requestedAt = juce::Time::currentTimeMillis();
    auto* object = new juce::DynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_open");
    object->setProperty ("version", "2.0");
    object->setProperty ("namespace", opensBalanceSettings ? "tonal-v1" : "reference-v1");
    object->setProperty ("request_id", id);
    object->setProperty ("requested_at_ms", requestedAt);
    object->setProperty ("runtime_instance_id",
                         requestedConfiguration.identity.runtimeInstanceId);
    object->setProperty ("preset_id", currentSnapshot.presetId);
    object->setProperty ("preset_revision_id", targetPreset->sourcePresetArtifact.revisionId);
    object->setProperty ("check_id", currentSnapshot.checkId);
    const auto file = root.getChildFile ("library/open").getChildFile (id + ".json");
    if (! file.getParentDirectory().createDirectory()) return false;
    if (! hypha::storage::writeText (file, juce::JSON::toString (juce::var (object)))) return false;
    pendingLibraryOpen = file;
    libraryOpenRequestedAt = requestedAt;
    currentSnapshot.recoveryStatus = "pending";
    notify();
    return true;
}

void RuntimeV2Controller::serviceLibraryRecovery()
{
    const juce::ScopedLock lock (stateLock);
    if (pendingLibraryOpen == juce::File()) return;
    const auto outcome = libraryOpenOutcome (! pendingLibraryOpen.existsAsFile(),
                                             juce::Time::currentTimeMillis() - libraryOpenRequestedAt);
    if (outcome == LibraryOpenOutcome::pending) return;
    currentSnapshot.recoveryStatus = outcome == LibraryOpenOutcome::opened ? "opened" : "timed_out";
    pendingLibraryOpen = juce::File {};
    recoveryStatusExpiresAtMs = juce::Time::currentTimeMillis() + 3000;
}
}
