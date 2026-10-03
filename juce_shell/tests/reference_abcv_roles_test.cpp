// H8: A／B／C／V の 4 役。B（REF）は Hypha に届いた B セットの曲を、A の直近 10 秒に追従する gain で
// 鳴らす。同時に鳴るのは 1 役。B のまま曲を替えると、新しい曲が準備でき次第 B のまま鳴る。
#include "reference_runtime_v2_analysis_test_support.h"
#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"
#include "reference_rt_probe.h"

void testReferenceRoles (const juce::File&);

namespace
{
struct Song { juce::File file; juce::String hash, pcm, candidateId; ref::RuntimeContentReceipt source; };

Song writeSong (const juce::File& root, const juce::String& name, float level, double lufs, const juce::String& candidateId)
{
    Song song { root.getChildFile (name + ".wav"), {}, juce::String::repeatedString (name.getLastCharacters (1), 64), candidateId, {} };
    {
        juce::WavAudioFormat format;
        auto stream = song.file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.release(), 48'000, 2, 32, {}, 0));
        juce::AudioBuffer<float> data (2, 96'000);
        for (int channel = 0; channel < 2; ++channel) juce::FloatVectorOperations::fill (data.getWritePointer (channel), level, 96'000);
        require (writer && writer->writeFromAudioSampleBuffer (data, 0, 96'000), "song samples");
    }
    song.hash = juce::SHA256 (song.file).toHexString();
    auto source = makeRuntimeV2Source (song.file, song.hash, song.pcm);
    addRuntimeV2MeasurementSummary (source, lufs, -6.0);
    song.source = stageRuntimeV2Artifact (root, "sources", source);
    return song;
}

juce::var candidate (const Song& song, const juce::String& name, const juce::String& cueId)
{
    auto* identity = new juce::DynamicObject();
    identity->setProperty ("catalog_reference_id", "catalog:source-test");
    identity->setProperty ("sha256_file", song.hash);
    identity->setProperty ("sha256_pcm", song.pcm);
    auto* artifact = new juce::DynamicObject();
    artifact->setProperty ("relative_path", song.source.relativePath);
    artifact->setProperty ("sha256", song.source.sha256);
    artifact->setProperty ("bytes", song.source.bytes);
    auto* cue = new juce::DynamicObject();
    cue->setProperty ("cue_id", cueId);
    cue->setProperty ("label", "Chorus");
    cue->setProperty ("sample_rate_hz", 48'000);
    cue->setProperty ("start_sample", 0);
    cue->setProperty ("end_sample", 96'000);
    cue->setProperty ("loop_enabled", true);
    auto* object = new juce::DynamicObject();
    object->setProperty ("candidate_id", song.candidateId);
    object->setProperty ("display_name", name);
    object->setProperty ("source_kind", "catalog_track");
    object->setProperty ("source_identity", juce::var (identity));
    object->setProperty ("source_artifact", juce::var (artifact));
    object->setProperty ("cues", juce::Array<juce::var> { juce::var (cue) });
    object->setProperty ("default_cue_id", cueId);
    object->setProperty ("preparation_status", "prepared");
    return juce::var (object);
}

juce::var songSets (const juce::String& setId, const juce::Array<juce::var>& songs)
{
    auto* set = new juce::DynamicObject();
    set->setProperty ("song_set_id", setId);
    set->setProperty ("revision_id", "77777777-7777-4777-8777-777777777771");
    set->setProperty ("rank", 1);
    set->setProperty ("name", "Mastering refs");
    set->setProperty ("songs", songs);
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "kirin_hypha_reference_library_sets");
    root->setProperty ("version", "1.0");
    root->setProperty ("revision", 1);
    root->setProperty ("manifest_revision", 1);
    root->setProperty ("song_sets", juce::Array<juce::var> { juce::var (set) });
    root->setProperty ("check_sets", juce::Array<juce::var>());
    root->setProperty ("source_ranges", juce::Array<juce::var>());
    return juce::var (root);
}

KirinMeterHistoryEntry window (double lufs)
{
    KirinMeterHistoryEntry value {};
    value.measurement_epoch = 1; value.run_id = 1; value.observation_count = 1;
    value.resolution = KIRIN_METER_HISTORY_10_HZ;
    value.lufs_m = { lufs, lufs, lufs };
    value.true_peak = { -3.0, -3.0, -3.0 };
    return value;
}
}

void testReferenceRoles (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("abcv-roles");
    require (root.createDirectory().wasOk(), "roles fixture directory");
    const juce::String setId = "66666666-6666-4666-8666-666666666661";
    const auto first = writeSong (root, "song1", 0.1f, -18.0, "11111111-1111-4111-8111-111111111111");
    const auto second = writeSong (root, "song2", 0.2f, -16.0, "22222222-2222-4222-8222-222222222222");
    const auto checkSong = writeSong (root, "check3", -0.25f, -18.0, "33333333-3333-4333-8333-333333333333");
    auto preset = bindRuntimeV2PresetToSource ("88888888-8888-4888-8888-888888888888", "99999999-9999-4999-8999-999999999999",
                                               checkSong.source, checkSong.hash, checkSong.pcm);
    auto* object = preset.getDynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0");
    object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    require (writeJson (root.getChildFile ("library/sets.json"), songSets (setId, {
                 candidate (first, "Hello", "44444444-4444-4444-8444-444444444441"),
                 candidate (second, "MONTERO", "44444444-4444-4444-8444-444444444442") }))
             && writeJson (root.getChildFile ("library/manifest.json"), libraryManifest (root, preset, 1)),
             "a library with one B set");

    std::atomic<int> owners { 0 };
    ref::ReferenceComparisonController controller (root, [&] (bool active)
    { owners += active ? 1 : -1; require (owners >= 0 && owners <= 1, "the roles share one audition owner"); return true; });
    controller.configure ({ "abcv-roles", {}, 42, true }, 48'000, 2);
    controller.setPresented (true);
    juce::AudioBuffer<float> block (2, 480);
    std::int64_t position = 0;
    const auto host = [&] (bool playing)
    {
        block.clear();
        beginReferenceRtProbe();
        controller.observeTransport (position, true, playing);
        controller.observeAInput (block, position, true, playing, true);
        const bool rendered = controller.renderSelectedB (block, position, true, true, true);
        require (endReferenceRtProbe() == 0, "three roles add no heap operations to the audio callback");
        if (playing) position += block.getNumSamples();
        return rendered;
    };
    const auto wait = [&] (const auto& condition, const char* what)
    {
        for (int attempt = 0; attempt < 1500; ++attempt)
        { host (true); if (condition (controller.snapshot())) return; juce::Thread::sleep (10); }
        const auto s = controller.snapshot();
        std::cerr << "roles: " << what << " / B " << (s.referenceSelection ? s.referenceSelection->rejectionCode : "-") << '\n';
        require (false, what);
    };
    const auto near = [] (float value, double expected) { return std::abs (value - expected) < 1.0e-4; };
    const auto gain = [] (double db) { return std::pow (10.0, db / 20.0); };

    // B の曲は B の一覧にだけ出て、C の一覧と V の一覧には出ない。最初の曲が選ばれ、前もって準備される。
    wait ([] (const auto& s) { return s.referenceReady && s.checkReady; }, "B and C prepare");
    auto state = controller.snapshot();
    require (state.songSets.size() == 1 && state.songSets[0].songs.size() == 2 && state.selectedSongSetId == setId
                 && state.selectedSongId == state.songSets[0].songs[0].id && state.versions.empty()
                 && std::none_of (state.checkTargets.begin(), state.checkTargets.end(),
                                  [] (const auto& option) { return option.label.contains ("MONTERO"); }),
             "the B set songs are listed for B only, and the first song is ready to play");

    // B を押すとすぐ鳴る（MATCH：A −14 − 曲 −18 = +4 dB）。C を押すと B は止まり C だけが鳴る。
    require (controller.requestAudition (3, -14.0, -2.0), "B plays");
    host (true); host (true);
    require (controller.snapshot().audibleComparisonSlot == 3 && near (block.getSample (0, 479), 0.1 * gain (4.0)),
             "B sounds at the matched gain");
    require (controller.requestAudition (2, -14.0, -2.0), "C plays");
    host (true); host (true);
    require (controller.snapshot().audibleComparisonSlot == 2 && near (block.getSample (0, 479), -0.25 * gain (4.0)),
             "only C sounds after C");
    require (controller.requestAudition (3, -14.0, -2.0) && host (true) && host (true)
                 && controller.snapshot().audibleComparisonSlot == 3, "back to B");

    // B のまま別の曲：新しい曲が準備でき次第、新しい MATCH（−14 − −16 = +2 dB）で B のまま鳴る。
    require (controller.selectSong (state.songSets[0].songs[1].id), "choose another song while B sounds");
    for (int attempt = 0; attempt < 1500 && controller.snapshot().audibleComparisonSlot != 3; ++attempt)
    {
        host (true);
        if (controller.pendingAuditionNeedsService()) controller.servicePendingAudition (-14.0, -2.0, true);
        juce::Thread::sleep (5);
    }
    host (true); host (true);
    require (controller.snapshot().audibleComparisonSlot == 3 && near (block.getSample (0, 479), 0.2 * gain (2.0)),
             "the new song sounds as B with its own MATCH");

    // B は A の直近 10 秒に追従する（−20 − −16 = −4 dB）。
    require (controller.followAudition (std::vector<KirinMeterHistoryEntry> (100, window (-20.0)), -2.0)
                 == ref::TrackingAction::move, "B follows A");
    for (int index = 0; index < 8; ++index) host (true);
    require (near (block.getSample (0, 479), 0.2 * gain (-4.0)), "B reaches the followed gain");

    // 止めても B のまま。再生すると同じ曲・同じ gain で戻る。
    require (! host (false) && controller.snapshot().audibleComparisonSlot == 0, "stopping returns to A");
    controller.servicePendingAudition (std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), false);
    wait ([] (const auto& s) { return s.referenceReady; }, "B prepares again");
    controller.servicePendingAudition (std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), true);
    host (true); host (true);
    require (controller.snapshot().audibleComparisonSlot == 3 && near (block.getSample (0, 479), 0.2 * gain (-4.0)),
             "B resumes with the same song and the same gain");

    // DAW の状態に B の曲が残る。B SET は選んだときだけ残す（選ぶまでは Kirin OS の 1 位に従う）。
    const auto saveAndRead = [&] {
        juce::XmlElement xml ("KirinHyphaState");
        controller.savedSettings().write (xml);
        return ref::ReferenceComparisonSettings::read (xml);
    };
    auto saved = saveAndRead();
    require (saved.reference.target() == state.songSets[0].songs[1].id && saved.songSetId.isEmpty(),
             "the host state keeps the B song, and the B set follows Kirin OS until one is chosen");
    require (! controller.selectSongSet ("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa") && controller.selectSongSet (setId),
             "only a delivered B set can be chosen");
    saved = saveAndRead();
    require (saved.songSetId == setId && controller.snapshot().selectedSongSetId == setId, "a chosen B set is kept");
    controller.selectA();
    host (true); host (true);
    require (! host (true) && controller.snapshot().audibleComparisonSlot == 0 && ! controller.pendingAuditionNeedsService(),
             "A ends B");
    std::cout << "Reference A/B/C/V roles: B songs, one role at a time, song switch, follow and resume PASS\n";
}
