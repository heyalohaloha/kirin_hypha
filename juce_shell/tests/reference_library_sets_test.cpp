// Hypha が Kirin OS の library/sets.json（Hypha に出した B セット・CHECK セットの順位）と、
// Cue の値のファイル（ranges/<sha256>.json）を読む。
// tests/fixtures/kirin_os_library_abcv は、Kirin OS の本物の書き出し処理（Kirin OS の
// publishReferenceLibrary、2026-10-03 の本流）で作ったデータ一式。組み込み Preset ＋保存した Preset 1 つ、
// B セット 1 つ（準備済みの Catalog の曲と未準備の Works の曲）、CHECK セットの順位 2 つ、Version 1 つ。
#include "reference_runtime_test_support.h"
#include "KirinLibraryFixture.h"
#include "../src/reference_audition/ReferenceLibrarySets.h"
#include "../src/reference_audition/ReferenceRuntimeV2Repository.h"
#include "../src/reference_audition/ReferenceSourceRanges.h"
#include "../src/reference_audition/ReferenceLibrarySongs.h"
#include "../src/reference_audition/ReferenceCueMatch.h"
#include "../src/reference_audition/ReferenceRuntimeV2Controller.h"
#include "../src/AtomicFile.h"
#include <algorithm>

void testReferenceLibrarySets (const juce::File&);
void testReferenceLibrarySetsOsFixture (const juce::File& root, int minimumVerified = 0);

namespace
{
juce::File copyFixture (const juce::File& sandbox, const char* name)
{
    const auto root = kirin_library_fixture::copy (sandbox, name);
    require (root != juce::File(), "the Kirin OS library fixture must be copied");
    return root;
}

juce::var readSets (const juce::File& root)
{
    return juce::JSON::parse (root.getChildFile ("library/sets.json"));
}

void readsWhatKirinOsWrote (const juce::File& sandbox)
{
    const auto root = copyFixture (sandbox, "library-sets");
    ref::RuntimeV2Repository repository (root);
    const auto loaded = repository.refreshLibrary();
    require (loaded.usable() && loaded.workspace->librarySets.has_value(), "the sets beside the manifest must be read");
    const auto& sets = *loaded.workspace->librarySets;
    require (sets.songSets.size() == 1 && sets.songSets[0].rank == 1 && sets.songSets[0].name == "Mastering refs",
             "the B set ranked 1 for Hypha");
    const auto& songs = sets.songSets[0].songs;
    require (songs.size() == 2 && songs[0].prepared && ! songs[1].prepared && songs[1].sourceArtifact.sha256.isEmpty(),
             "a prepared song and a song Kirin OS has not prepared yet");
    require (sets.checkSets.size() == 2 && sets.checkSets[0].rank == 1 && sets.checkSets[1].rank == 2
                 && sets.checkSets[0].presetId == loaded.workspace->manifest.activePresetId,
             "CHECK rank 1 is the startup Preset");
    require (sets.sourceRanges.size() == 3, "the ranked songs, the CHECK track and the Version each have Cue values");

    const auto& song = songs[0];
    const auto entry = std::find_if (sets.sourceRanges.begin(), sets.sourceRanges.end(), [&] (const auto& item) {
        return item.sourceArtifactSha256 == song.sourceArtifact.sha256;
    });
    require (entry != sets.sourceRanges.end(), "the prepared song's Cue values are indexed by its source");
    const auto source = ref::RuntimeV2SourceRepository (root).load (song);
    require (source.accepted(), "the song's source descriptor is read");
    const auto verified = ref::readSourceRanges (root, sets, song.sourceArtifact, *source.source);
    require (verified.has_value(), "the Cue values file of this very source must be read");
    const auto& ranges = *verified;
    require (ranges.sampleRateHz == 48'000 && ranges.totalSampleFrames == 96'000 && ranges.spectrumBandCentersHz.size() == 12,
             "the audio facts and the spectrum bands");
    const auto* cue = ranges.find (song.cues[0].startSample, song.cues[0].endSample);
    require (cue != nullptr && cue->lufsIMilliLu.has_value() && cue->maxTruePeakMilliDbtp.has_value()
                 && cue->balanceMilliDbfs.has_value() && cue->spectrumFrameCount > 0
                 && cue->spectrumMedianMilliDbfs.size() == 12,
             "the song's Cue has its Integrated, True Peak, Balance and spectrum");
    require (ranges.hasSections && ranges.sectionsStatus == "found" && ranges.sectionFamilies.size() == 1
                 && ranges.sectionFamilies[0].chorusCandidate && ranges.sectionFamilies[0].occurrences.size() == 2,
             "the auto sections with the chorus candidate");
    require (ranges.fingerprintTicks == 20 && ranges.fingerprintChromaSigns.getSize() == 40
                 && ranges.fingerprintLoudness.getSize() == 20,
             "the Kirin fingerprint of 20 ticks");

    // B の一覧と Balance に出す、曲の既定の Cue の値（曲の Preset ごと）。準備前の曲は値なし。
    const auto& facts = sets.songFacts;
    const auto preparedFacts = facts.find (ref::referenceSongEntryId (sets.songSets[0].songSetId, song.candidateId));
    const auto pendingFacts = facts.find (ref::referenceSongEntryId (sets.songSets[0].songSetId, songs[1].candidateId));
    require (preparedFacts != facts.end() && std::abs (preparedFacts->second.lufsI - static_cast<double> (*cue->lufsIMilliLu) / 1000.0) < 1.0e-9
                 && preparedFacts->second.spectrumMedianDb.size() == 12 && pendingFacts != facts.end()
                 && ! std::isfinite (pendingFacts->second.lufsI) && pendingFacts->second.spectrumMedianDb.empty(),
             "each B song carries its Cue loudness and spectrum for the B page");

    // 2026-10-06：索引が別の音源の Cue の値を指していれば、一覧・Balance・MATCH のどれも受け付けない（同じ入口）。
    {
        auto other = sets;
        const auto foreign = std::find_if (other.sourceRanges.begin(), other.sourceRanges.end(), [&] (const auto& item) {
            return item.sourceArtifactSha256 != song.sourceArtifact.sha256;
        });
        require (foreign != other.sourceRanges.end(), "the fixture indexes another source");
        for (auto& item : other.sourceRanges)
            if (item.sourceArtifactSha256 == song.sourceArtifact.sha256) item.rangesArtifact = foreign->rangesArtifact;
        require (! ref::readSourceRanges (root, other, song.sourceArtifact, *source.source).has_value(),
                 "another source's Cue values are refused");
        auto workspace = *loaded.workspace;
        workspace.librarySets = other;
        ref::applyLibrarySongEntries (root, workspace);
        const auto facts = workspace.librarySets->songFacts.find (ref::referenceSongEntryId (sets.songSets[0].songSetId, song.candidateId));
        require (facts != workspace.librarySets->songFacts.end() && ! std::isfinite (facts->second.lufsI)
                     && facts->second.spectrumMedianDb.empty(),
                 "the B list and Balance show no value from another source's Cue values");
        require (! ref::readCueLevel (root, workspace, song, song.cues[0], *source.source).has_value(),
                 "MATCH takes no level from another source's Cue values");
    }

    // 受け取りと違うファイル（書き換えられた・壊れた）は読まない。
    const auto file = root.getChildFile ("ranges/" + entry->rangesArtifact.sha256 + ".json");
    require (file.appendText (" "), "a changed Cue values file");
    require (! ref::readSourceRanges (root, sets, song.sourceArtifact, *source.source).has_value(), "a changed file is refused");
}

void missingSongHasNoReadyFacts (const juce::File& sandbox)
{
    const auto root = copyFixture (sandbox, "missing-song");
    ref::RuntimeV2Repository repository (root);
    const auto loaded = repository.refreshLibrary();
    require (loaded.usable() && loaded.workspace->librarySets.has_value(), "load isolated song set");
    const auto& sets = *loaded.workspace->librarySets;
    const auto& song = sets.songSets.front().songs.front();
    const auto source = ref::RuntimeV2SourceRepository (root).load (song);
    require (source.accepted() && juce::File (source.source->absolutePath).deleteFile(), "remove only the copied song");
    auto refreshed = *loaded.workspace;
    ref::applyLibrarySongEntries (root, refreshed);
    const auto facts = refreshed.librarySets->songFacts.at (ref::referenceSongEntryId (
        sets.songSets.front().songSetId, song.candidateId));
    require (facts.sourceMissing && ! std::isfinite (facts.lufsI), "missing nonselected songs do not retain READY measurement facts");
}

void sourceFailureWaitsBeforeReadingAgain (const juce::File& sandbox)
{
    const auto root = copyFixture (sandbox, "source-retry-budget");
    const auto loaded = ref::RuntimeV2Repository (root).refreshLibrary();
    require (loaded.usable() && loaded.workspace->librarySets.has_value()
        && ! loaded.workspace->librarySets->songSets.empty(), "source retry has an isolated prepared song");
    const auto& set = loaded.workspace->librarySets->songSets.front();
    const auto id = ref::referenceSongSelectionId (
        ref::referenceSongEntryId (set.songSetId, set.songs.front().candidateId), set.songs.front().candidateId);
    ref::RuntimeV2Controller controller (root);
    controller.observeTransport (0, true, true);
    controller.configure ({ "source-retry-post", {}, 42, true }, 48000, 2);
    bool selected = false;
    for (int i = 0; i < 800 && ! selected; ++i)
    { selected = controller.selectLibrarySong (id); if (! selected) juce::Thread::sleep (10); }
    require (selected, "select the fixture's prepared song rather than its empty default Check");
    const auto wait = [&] (const char* reason) {
        for (int i = 0; i < 800; ++i)
        {
            if (controller.snapshot().rejectionCode == reason) return;
            juce::Thread::sleep (10);
        }
        const auto state = controller.snapshot();
        std::cerr << "Source retry expected " << reason << ", actual " << state.rejectionCode
                  << " state " << static_cast<int> (state.state) << " preset " << state.presetName << '\n';
        require (false, "source retry fixture deadline");
    };
    // These metadata receipts intentionally have synthetic audio hashes. The first
    // verification fails, then deleting the disposable file makes a new observable cause.
    wait ("reference_source_changed");
    const auto first = juce::Time::getMillisecondCounterHiRes();
    require (root.getChildFile ("fixture/source.wav").deleteFile(), "remove only retry fixture audio");
    juce::Thread::sleep (1100);
    require (controller.snapshot().rejectionCode == "reference_source_changed",
             "500 ms worker ticks retain the previous failure instead of reading audio again");
    wait ("reference_source_open_failed");
    require (juce::Time::getMillisecondCounterHiRes() - first >= 4000,
             "a new source failure is observed only after the five-second retry reservation");
}

void followsRankChangesAndPublication (const juce::File& sandbox)
{
    const auto root = copyFixture (sandbox, "library-sets-ranks");
    ref::RuntimeV2Repository repository (root);
    const auto loaded = repository.refreshLibrary();
    require (loaded.usable() && loaded.workspace->librarySets.has_value(), "the first read has the sets");

    // 順位だけを変えると、manifest は同じまま sets.json だけが書き換わる。
    auto sets = readSets (root);
    sets.getDynamicObject()->setProperty ("revision", static_cast<juce::int64> (sets["revision"]) + 1);
    sets.getDynamicObject()->setProperty ("song_sets", juce::Array<juce::var> {});
    require (writeJson (root.getChildFile ("library/sets.json"), sets), "Kirin OS rewrites the sets");
    const auto ranked = repository.refreshLibrary (loaded.workspace);
    require (ranked.state == ref::RuntimeWorkspaceLoadState::updated && ranked.workspace->librarySets.has_value()
                 && ranked.workspace->librarySets->songSets.empty(),
             "a rank change without a manifest change must be read");
    require (repository.refreshLibrary (ranked.workspace).state == ref::RuntimeWorkspaceLoadState::unchanged,
             "nothing changed after that");

    // 書き換えの途中（別の manifest の sets）や壊れた sets は、今の sets を保つ。
    sets.getDynamicObject()->setProperty ("manifest_revision", 99);
    require (writeJson (root.getChildFile ("library/sets.json"), sets), "sets for another manifest");
    const auto pending = repository.refreshLibrary (ranked.workspace);
    require (pending.state == ref::RuntimeWorkspaceLoadState::unchanged && pending.workspace->librarySets.has_value(),
             "sets written for another manifest keep the current ones");

    // sets.json の無い古い Kirin OS でも、manifest の library は今までどおり読める。
    require (root.getChildFile ("library/sets.json").deleteFile(), "an older Kirin OS writes no sets");
    ref::RuntimeV2Repository fresh (root);
    const auto older = fresh.refreshLibrary();
    require (older.usable() && ! older.workspace->librarySets.has_value() && ! older.workspace->presets.empty(),
             "without sets the library is read as before");
    juce::String rejection;
    std::vector<ref::RuntimeSkippedItem> skipped;
    require (! ref::readReferenceLibrarySets (root, *older.workspace, rejection, skipped) && rejection.isEmpty(),
             "a missing sets file is not a rejection");

    // 形の壊れた sets は理由つきで読まないが、library は使える。
    require (hypha::storage::writeText (root.getChildFile ("library/sets.json"), "{\"format\":\"kirin_hypha_reference_library_sets\"}"),
             "a broken sets file");
    ref::RuntimeV2Repository broken (root);
    const auto withBroken = broken.refreshLibrary();
    require (withBroken.usable() && ! withBroken.workspace->librarySets.has_value(), "a broken sets file leaves the library usable");
    require (! ref::readReferenceLibrarySets (root, *withBroken.workspace, rejection, skipped)
                 && rejection == "reference_library_sets_rejected",
             "a broken sets file is rejected with its reason");
}

void refusesWhatItCannotTrust (const juce::File& sandbox)
{
    const auto root = copyFixture (sandbox, "library-sets-refused");
    ref::RuntimeV2Repository repository (root);
    const auto loaded = repository.refreshLibrary();
    require (loaded.usable(), "the fixture library must be read");
    const auto write = [&] (const juce::var& sets) {
        require (writeJson (root.getChildFile ("library/sets.json"), sets), "a changed sets file");
    };
    // ファイルの形が違えば全体を読まない。
    const auto check = [&] (const juce::var& sets, const char* expected, const char* why) {
        write (sets);
        juce::String rejection;
        std::vector<ref::RuntimeSkippedItem> skipped;
        require (! ref::readReferenceLibrarySets (root, *loaded.workspace, rejection, skipped) && rejection == expected, why);
    };
    // 読めない項目は 1 つずつ飛ばし、残りを使って理由を残す（1 つの壊れた項目で B セット全体を失わない）。名前のある
    // B セットと曲は、どれを外したかを skipped に（理由の文は残さない）。
    std::vector<ref::RuntimeSkippedItem> lastSkipped;
    const auto skips = [&] (const juce::var& sets, const char* expected, const char* why) {
        write (sets);
        juce::String rejection;
        const auto read = ref::readReferenceLibrarySets (root, *loaded.workspace, rejection, lastSkipped);
        require (read.has_value() && rejection == expected, why);
        return *read;
    };
    const auto original = readSets (root);
    auto ranks = original.clone();
    ranks["check_sets"].getArray()->getReference (0).getDynamicObject()->setProperty ("rank", 2);
    const auto rankRead = skips (ranks, "reference_library_check_set_rejected", "ranks must count 1, 2, 3 in order");
    require (rankRead.checkSets.size() == 1 && rankRead.checkSets[0].rank == 2 && rankRead.songSets.size() == 1,
             "only the misplaced rank is skipped");
    auto unknown = original.clone();
    unknown["check_sets"].getArray()->getReference (1).getDynamicObject()->setProperty (
        "revision_id", "00000000-0000-4000-8000-000000000000");
    const auto unknownRead = skips (unknown, "reference_library_check_set_rejected",
                                    "a CHECK set must be a Preset revision in the same manifest");
    require (unknownRead.checkSets.size() == 1 && unknownRead.checkSets[0].rank == 1, "the other CHECK set stays");
    auto path = original.clone();
    path["source_ranges"].getArray()->getReference (0)["ranges_artifact"].getDynamicObject()->setProperty (
        "relative_path", "plugin_data/reference/v2/sources/" + original["source_ranges"][0]["ranges_artifact"]["sha256"].toString() + ".json");
    const auto pathRead = skips (path, "reference_library_source_ranges_rejected", "a Cue values receipt must point into ranges/");
    require (pathRead.sourceRanges.size() == 2, "the other Cue values stay");
    auto song = original.clone();
    song["song_sets"].getArray()->getReference (0)["songs"].getArray()->getReference (1).getDynamicObject()->setProperty ("unexpected", 1);
    const auto songRead = skips (song, "", "a song Hypha cannot read is skipped by itself");
    require (songRead.songSets.size() == 1 && songRead.songSets[0].songs.size() == 1, "the B set keeps its other song");
    require (lastSkipped.size() == 1 && lastSkipped[0].name == "Reference" && ! lastSkipped[0].nameUnreadable,
             "the skipped song is named, and its name is not the reason");

    // 外した曲は workspace に残り（知らせがどれを外したかを言う）、読めるように戻れば消える。
    const auto partial = repository.refreshLibrary (loaded.workspace);
    require (partial.state == ref::RuntimeWorkspaceLoadState::updated && partial.workspace->librarySetsIssue.isEmpty()
                 && partial.workspace->setsSkipped.size() == 1
                 && partial.workspace->librarySets->songSets[0].songs.size() == 1,
             "a skipped song is reported beside the songs that were read");
    write (original);
    const auto repaired = repository.refreshLibrary (partial.workspace);
    require (repaired.state == ref::RuntimeWorkspaceLoadState::updated && repaired.workspace->setsSkipped.empty()
                 && repaired.workspace->librarySets->songSets[0].songs.size() == 2,
             "the skipped song goes away once the sets are read whole");

    auto extra = original.clone();
    extra.getDynamicObject()->setProperty ("note", "unexpected");
    check (extra, "reference_library_sets_rejected", "unknown keys are refused");
}

// Kirin OS は manifest を先に書き、sets.json はその直後に続く。追いつくまで前の B セットを保つ（B の曲が一瞬
// 消えて、選んでいる曲や鳴っている B を失わない）。CHECK セットの順位は新しい manifest にある Preset だけ。
void keepsSetsUntilTheyCatchUp (const juce::File& sandbox)
{
    const auto root = copyFixture (sandbox, "library-sets-catch-up");
    ref::RuntimeV2Repository repository (root);
    const auto loaded = repository.refreshLibrary();
    require (loaded.usable() && loaded.workspace->librarySets.has_value()
                 && loaded.workspace->librarySets->songSets.size() == 1, "the first read has the B set");
    auto manifest = juce::JSON::parse (root.getChildFile ("library/manifest.json"));
    const auto revision = static_cast<juce::int64> (manifest["revision"]) + 1;
    manifest.getDynamicObject()->setProperty ("revision", revision);
    require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "Kirin OS publishes a new manifest first");
    const auto ahead = repository.refreshLibrary (loaded.workspace);
    require (ahead.state == ref::RuntimeWorkspaceLoadState::updated && ahead.workspace->librarySets.has_value()
                 && ahead.workspace->librarySets->songSets.size() == 1
                 && ahead.workspace->librarySets->checkSets.size() == loaded.workspace->librarySets->checkSets.size()
                 && ahead.workspace->librarySetsIssue.isEmpty(),
             "the B set stays while sets.json still names the previous manifest");
    require (std::any_of (ahead.workspace->presets.begin(), ahead.workspace->presets.end(),
                          [] (const auto& preset) { return preset.songEntry; }),
             "the B songs stay selectable");

    auto sets = readSets (root);
    sets.getDynamicObject()->setProperty ("manifest_revision", revision);
    sets.getDynamicObject()->setProperty ("revision", static_cast<juce::int64> (sets["revision"]) + 1);
    require (writeJson (root.getChildFile ("library/sets.json"), sets), "sets.json catches up");
    const auto caught = repository.refreshLibrary (ahead.workspace);
    require (caught.state == ref::RuntimeWorkspaceLoadState::updated && caught.workspace->librarySets.has_value()
                 && caught.workspace->librarySets->hash != loaded.workspace->librarySets->hash,
             "the caught-up sets are read");
}

// Kirin OS の準備の状態（library/preparation.json、presence と同じ回・同じ期限）。期限の内だけ使い、
// 決まった言葉でない曲は飛ばす。無い（古い Kirin OS）・期限切れ（Kirin OS が閉じている）は無し。
void readsKirinOsPreparation (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("library-preparation");
    require (root.getChildFile ("library").createDirectory().wasOk(), "a library folder");
    ref::RuntimeV2Repository repository (root);
    const auto now = juce::Time::currentTimeMillis();
    const auto song = [] (const char* id, const char* state, juce::var step, juce::var reason, juce::var retry, int ahead) {
        auto* item = new juce::DynamicObject();
        item->setProperty ("candidate_id", id); item->setProperty ("state", state); item->setProperty ("step", step);
        item->setProperty ("reason", reason); item->setProperty ("retry", retry); item->setProperty ("ahead", ahead);
        return juce::var (item);
    };
    const auto write = [&] (std::int64_t updated, juce::Array<juce::var> songs) {
        auto* object = new juce::DynamicObject();
        object->setProperty ("format", "kirin_hypha_reference_library_preparation");
        object->setProperty ("version", "1.0");
        object->setProperty ("session_id", "0d77fc48-767c-4c4b-9940-4d640e46c8e6");
        object->setProperty ("updated_at_ms", updated);
        object->setProperty ("expires_at_ms", updated + 5000);
        object->setProperty ("phase", "working");
        object->setProperty ("songs", songs);
        require (writeJson (root.getChildFile ("library/preparation.json"), juce::var (object)), "a preparation file");
    };
    const auto queuedId = "5c9a4c2e-1f0b-4d7a-9e21-3b8f6a0d4c11", missingId = "7e1d2c3b-4a5f-4e6d-8c7b-9a0f1e2d3c4b";
    write (now, { song (queuedId, "pending", "queued", {}, {}, 2),
                  song (missingId, "pending", {}, "source_unavailable", "manual", 0),
                  song ("not-a-uuid", "pending", "queued", {}, {}, 0),
                  song ("9b8a7c6d-5e4f-4a3b-8c2d-1e0f9a8b7c6d", "pending", "decoding", {}, {}, 0) });
    const auto read = repository.libraryPreparation (now);
    require (read != nullptr && read->phase == "working" && read->songs.size() == 2, "the songs Kirin OS reports are read, others skipped");
    const auto queued = read->find (queuedId), missing = read->find (missingId);
    require (queued.state == "pending" && queued.step == "queued" && queued.reason.isEmpty() && queued.retry.isEmpty()
                 && queued.ahead == 2 && queued.phase == "working",
             "a queued song with the songs ahead of it");
    require (missing.reason == "source_unavailable" && missing.retry == "manual" && missing.step.isEmpty(),
             "a song Kirin OS cannot find, waiting for a retry");
    require (! read->find ("00000000-0000-4000-8000-000000000000").known(), "a song Kirin OS does not report is unknown");
    require (repository.libraryPreparation (now + 6000) == nullptr, "an expired state (Kirin OS closed) is not used");
    write (now + 10'000, {});
    require (repository.libraryPreparation (now) == nullptr, "a state from the future is not used");
    require (root.getChildFile ("library/preparation.json").deleteFile() && repository.libraryPreparation (now) == nullptr,
             "an older Kirin OS writes no state");
}

// Kirin OS 1.2.0 の受け渡しの節目（2026-10-18）：知らない版の sets は推測で読まずに理由を残す。sets.json が消えても
// library は使え（B セットだけが無くなる）、戻れば同じ世代をそのまま読み直す。知らない版を読んだ後も前の世代へ戻れる。
void followsTheHandoffLifecycle (const juce::File& sandbox)
{
    const auto root = copyFixture (sandbox, "library-sets-lifecycle");
    ref::RuntimeV2Repository repository (root);
    const auto first = repository.refreshLibrary();
    require (first.usable() && first.workspace->librarySets.has_value(), "the first read has the B sets");
    const auto original = readSets (root);
    const auto setsFile = root.getChildFile ("library/sets.json");
    const auto written = root.getChildFile ("sets-as-written.json");
    require (setsFile.copyFileTo (written), "keep the sets Kirin OS wrote, byte for byte");
    auto future = original.clone();
    future.getDynamicObject()->setProperty ("version", "2.0");
    require (writeJson (setsFile, future), "a sets file of a version this Hypha does not know");
    juce::String rejection;
    std::vector<ref::RuntimeSkippedItem> skipped;
    require (! ref::readReferenceLibrarySets (root, *first.workspace, rejection, skipped)
                 && rejection == "reference_library_sets_rejected",
             "an unknown sets version is refused with its reason, never guessed");
    require (setsFile.deleteFile(), "Kirin OS removes the sets");
    const auto removed = repository.refreshLibrary (first.workspace);
    require (removed.usable() && ! removed.workspace->librarySets.has_value() && ! removed.workspace->presets.empty(),
             "without sets.json the library stays usable and has no B sets");
    require (written.copyFileTo (setsFile), "Kirin OS writes the same sets again");
    const auto restored = repository.refreshLibrary (removed.workspace);
    require (restored.usable() && restored.workspace->librarySets.has_value(), "the returned sets are read again");
    require (restored.workspace->librarySets->hash == first.workspace->librarySets->hash
                 && restored.workspace->librarySets->revision == first.workspace->librarySets->revision
                 && restored.workspace->librarySets->songSets.size() == first.workspace->librarySets->songSets.size(),
             "the returned sets are the same generation as before");
}

// Kirin OS が閉じて戻る（2026-10-18）：presence.json は 5 秒ごとの印で、期限が切れたら切れたものとし、新しい回の印で
// 戻る。切れている間も受け取った B セットは同じ世代のまま残り、戻った後も読み直しで変わらない。形の違う印は使わない。
void followsKirinOsReconnect (const juce::File& sandbox)
{
    const auto root = copyFixture (sandbox, "library-reconnect");
    ref::RuntimeV2Repository repository (root);
    const auto first = repository.refreshLibrary();
    require (first.usable() && first.workspace->librarySets.has_value(), "the library is read while Kirin OS is open");
    const auto presence = [&] (const char* session, std::int64_t updated, std::int64_t lifetime)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("format", "kirin_hypha_reference_library_presence"); object->setProperty ("version", "1.0");
        object->setProperty ("session_id", session);
        object->setProperty ("updated_at_ms", updated); object->setProperty ("expires_at_ms", updated + lifetime);
        require (writeJson (root.getChildFile ("library/presence.json"), juce::var (object)), "a presence file");
    };
    const auto now = juce::Time::currentTimeMillis();
    presence ("77777777-7777-4777-8777-777777777777", now, 5000);
    require (repository.libraryOnline (now), "Kirin OS is open");
    require (! repository.libraryOnline (now + 6000), "an expired presence means Kirin OS closed");
    const auto closed = repository.refreshLibrary (first.workspace);
    require (closed.usable() && closed.workspace->librarySets.has_value()
                 && closed.workspace->librarySets->revision == first.workspace->librarySets->revision,
             "while Kirin OS is closed the received B sets stay, the same generation");
    presence ("88888888-8888-4888-8888-888888888888", now + 9000, 5000);
    require (repository.libraryOnline (now + 9000), "a new session's presence means Kirin OS is back");
    const auto back = repository.refreshLibrary (closed.workspace);
    require (back.usable() && back.workspace->librarySets.has_value()
                 && back.workspace->librarySets->hash == first.workspace->librarySets->hash,
             "after the reconnect the same B sets are read");
    presence ("not-a-uuid", now + 9000, 5000);
    require (! repository.libraryOnline (now + 9000), "a presence of the wrong form is not used");
    presence ("99999999-9999-4999-8999-999999999999", now + 9000, 60'000);
    require (! repository.libraryOnline (now + 9000), "a presence with another lifetime is not used");
    require (root.getChildFile ("library/presence.json").deleteFile() && ! repository.libraryOnline (now),
             "no presence file: Kirin OS is not open");
}
}

void testReferenceLibrarySets (const juce::File& sandbox)
{
    readsWhatKirinOsWrote (sandbox);
    missingSongHasNoReadyFacts (sandbox);
    sourceFailureWaitsBeforeReadingAgain (sandbox);
    followsRankChangesAndPublication (sandbox);
    refusesWhatItCannotTrust (sandbox);
    keepsSetsUntilTheyCatchUp (sandbox);
    readsKirinOsPreparation (sandbox);
    followsTheHandoffLifecycle (sandbox);
    followsKirinOsReconnect (sandbox);
    // The source identity, the Cue range and the Cue values of every prepared B song, as Kirin OS 1.2.0
    // writes them, checked on the anonymous fixture in every run (a real export is checked only on request).
    testReferenceLibrarySetsOsFixture (copyFixture (sandbox, "library-os-shape"), 1);
}

// Kirin OS の書き出しの B セット（毎回は repo の匿名の fixture、頼まれたときは KIRIN_REFERENCE_LIBRARY_FIXTURE の
// 手元の写し）：曲ごとに音源の記述・Cue の区間（実際に鳴らす区間）・Cue の値を、Hypha が鳴らすときと同じ入口で確かめ、
// 音源ファイルがあれば hash と版も照らす。sets.json は今の manifest の版に結ばれている（古い世代を読まない）。
void testReferenceLibrarySetsOsFixture (const juce::File& root, int minimumVerified)
{
    ref::RuntimeV2Repository repository (root);
    const auto loaded = repository.refreshLibrary();
    require (loaded.usable(), "actual OS library must load");
    if (! loaded.workspace->librarySets) { std::cout << "OS library B sets: none\n"; return; }
    const auto& sets = *loaded.workspace->librarySets;
    const auto setsFile = juce::JSON::parse (root.getChildFile ("library/sets.json"));
    require (static_cast<juce::int64> (setsFile["manifest_revision"]) == loaded.workspace->manifest.revision
                 && sets.revision == static_cast<juce::int64> (setsFile["revision"]),
             "the B sets read are the generation of the manifest read");
    ref::RuntimeV2SourceRepository sources (root);
    int songs = 0, prepared = 0, verified = 0, missing = 0;
    for (const auto& set : sets.songSets)
        for (const auto& song : set.songs)
        {
            ++songs;
            if (! song.prepared) continue;
            ++prepared;
            const auto source = sources.load (song);
            require (source.accepted(), "a prepared B song's source record must be accepted");
            // The audio file itself is checked where it exists (a real export); the record, the Cue range and
            // the Cue values are checked for every prepared song.
            if (! sources.verifySourceFile (*source.source).isEmpty()) ++missing;
            require (! song.cues.empty(), "a prepared B song has its Cue");
            for (const auto& cue : song.cues)
                require (cue.sampleRateHz == source.source->audio.sampleRateHz && cue.startSample >= 0
                             && cue.endSample > cue.startSample && cue.endSample <= source.source->audio.totalSampleFrames,
                         "the Cue Hypha plays lies inside the source");
            require (ref::readSourceRanges (root, sets, song.sourceArtifact, *source.source).has_value(),
                     "the Cue values of this very source are read");
            ++verified;
        }
    require (verified >= minimumVerified, "the prepared B songs are checked against their records, Cues and values");
    std::cout << "OS library B sets: " << sets.songSets.size() << " sets, " << songs << " songs, " << prepared
              << " prepared, " << verified << " verified with record, Cue and values, " << missing
              << " files not found on disk; sets revision " << sets.revision << " for manifest " << loaded.workspace->manifest.revision << '\n';
}

