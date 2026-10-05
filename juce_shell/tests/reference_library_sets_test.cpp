// Hypha が Kirin OS の library/sets.json（Hypha に出した B セット・CHECK セットの順位）と、
// Cue の値のファイル（ranges/<sha256>.json）を読む。
// tests/fixtures/kirin_os_library_abcv は、Kirin OS の本物の書き出し処理（Kirin OS の
// publishReferenceLibrary、2026-10-03 の本流）で作ったデータ一式。組み込み Preset ＋保存した Preset 1 つ、
// B セット 1 つ（準備済みの Catalog の曲と未準備の Works の曲）、CHECK セットの順位 2 つ、Version 1 つ。
#include "reference_runtime_test_support.h"
#include "../src/reference_audition/ReferenceLibrarySets.h"
#include "../src/reference_audition/ReferenceRuntimeV2Repository.h"
#include "../src/reference_audition/ReferenceSourceRanges.h"
#include "../src/reference_audition/ReferenceLibrarySongs.h"
#include "../src/reference_audition/ReferenceCueMatch.h"

#include <algorithm>

void testReferenceLibrarySets (const juce::File&);

namespace
{
juce::File copyFixture (const juce::File& sandbox, const char* name)
{
    const auto source = juce::File (KIRIN_REFERENCE_FIXTURE_DIR).getChildFile ("kirin_os_library_abcv");
    const auto root = sandbox.getChildFile (name);
    require (source.isDirectory() && source.copyDirectoryTo (root), "the Kirin OS library fixture must be copied");
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
    require (root.getChildFile ("library/sets.json").replaceWithText ("{\"format\":\"kirin_hypha_reference_library_sets\"}"),
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
}

void testReferenceLibrarySets (const juce::File& sandbox)
{
    readsWhatKirinOsWrote (sandbox);
    followsRankChangesAndPublication (sandbox);
    refusesWhatItCannotTrust (sandbox);
    keepsSetsUntilTheyCatchUp (sandbox);
    readsKirinOsPreparation (sandbox);
}
