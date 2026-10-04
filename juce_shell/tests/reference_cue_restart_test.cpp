// B（別の曲）は、選んだときの DAW の位置を起点に Cue の頭から進む。選んだ後に DAW を頭へ戻す・Cue を過ぎてから
// 押す・鳴らしている B を止めて起点より前へ戻す、のどれでも「Cue範囲外」にせず、今の位置から Cue の頭を鳴らす
// （2026-10-03。選んだ後に頭へ戻すと B が鳴らせなくなっていた）。
#include "reference_runtime_v2_analysis_test_support.h"
#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"
#include "reference_rt_probe.h"

void testReferenceCueRestart (const juce::File&);

namespace
{
constexpr int songFrames = 576'000;  // 12 秒（ページ 6 枚＝6 秒より長く、Cue の頭が追い出される）
float rampAt (std::int64_t frame) { return 0.1f + 0.3f * static_cast<float> (frame) / static_cast<float> (songFrames); }

juce::var restartSets (const juce::String& setId, const juce::File& song, const juce::String& hash, const juce::String& pcm,
                       const ref::RuntimeContentReceipt& source)
{
    auto* identity = new juce::DynamicObject();
    identity->setProperty ("catalog_reference_id", "catalog:source-test");
    identity->setProperty ("sha256_file", hash);
    identity->setProperty ("sha256_pcm", pcm);
    auto* artifact = new juce::DynamicObject();
    artifact->setProperty ("relative_path", source.relativePath);
    artifact->setProperty ("sha256", source.sha256);
    artifact->setProperty ("bytes", source.bytes);
    auto* cue = new juce::DynamicObject();
    cue->setProperty ("cue_id", "45454545-4545-4545-8545-454545454545");
    cue->setProperty ("label", "Full track");
    cue->setProperty ("sample_rate_hz", 48'000);
    cue->setProperty ("start_sample", 0);
    cue->setProperty ("end_sample", songFrames);
    cue->setProperty ("loop_enabled", false);
    auto* candidate = new juce::DynamicObject();
    candidate->setProperty ("candidate_id", "56565656-5656-4656-8656-565656565656");
    candidate->setProperty ("display_name", song.getFileNameWithoutExtension());
    candidate->setProperty ("source_kind", "catalog_track");
    candidate->setProperty ("source_identity", juce::var (identity));
    candidate->setProperty ("source_artifact", juce::var (artifact));
    candidate->setProperty ("cues", juce::Array<juce::var> { juce::var (cue) });
    candidate->setProperty ("default_cue_id", "45454545-4545-4545-8545-454545454545");
    candidate->setProperty ("preparation_status", "prepared");
    // 2 曲目は Kirin OS がまだ確かめられていない曲（ファイルが見つからないなど）。
    auto* pendingCue = new juce::DynamicObject();
    pendingCue->setProperty ("cue_id", "46464646-4646-4646-8646-464646464646");
    pendingCue->setProperty ("label", "Full track");
    pendingCue->setProperty ("sample_rate_hz", 48'000);
    pendingCue->setProperty ("start_sample", 0);
    pendingCue->setProperty ("end_sample", songFrames);
    pendingCue->setProperty ("loop_enabled", false);
    auto* pendingIdentity = new juce::DynamicObject();
    pendingIdentity->setProperty ("catalog_reference_id", "catalog:pending-song");
    pendingIdentity->setProperty ("sha256_file", juce::String::repeatedString ("8", 64));
    pendingIdentity->setProperty ("sha256_pcm", juce::String::repeatedString ("9", 64));
    auto* pending = new juce::DynamicObject();
    pending->setProperty ("candidate_id", "57575757-5757-4757-8757-575757575757");
    pending->setProperty ("display_name", "Pending song");
    pending->setProperty ("source_kind", "catalog_track");
    pending->setProperty ("source_identity", juce::var (pendingIdentity));
    pending->setProperty ("source_artifact", juce::var());
    pending->setProperty ("cues", juce::Array<juce::var> { juce::var (pendingCue) });
    pending->setProperty ("default_cue_id", "46464646-4646-4646-8646-464646464646");
    pending->setProperty ("preparation_status", "pending");
    auto* set = new juce::DynamicObject();
    set->setProperty ("song_set_id", setId);
    set->setProperty ("revision_id", "67676767-6767-4767-8767-676767676767");
    set->setProperty ("rank", 1);
    set->setProperty ("name", "Refs");
    set->setProperty ("songs", juce::Array<juce::var> { juce::var (candidate), juce::var (pending) });
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
}

void testReferenceCueRestart (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("cue-restart");
    require (root.createDirectory().wasOk(), "cue restart fixture directory");
    const auto file = root.getChildFile ("ramp.wav");
    {
        juce::WavAudioFormat format;
        auto stream = file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.release(), 48'000, 2, 32, {}, 0));
        juce::AudioBuffer<float> data (2, songFrames);
        for (int channel = 0; channel < 2; ++channel)
            for (int frame = 0; frame < songFrames; ++frame) data.setSample (channel, frame, rampAt (frame));
        require (writer && writer->writeFromAudioSampleBuffer (data, 0, songFrames), "ramp samples");
    }
    const auto hash = juce::SHA256 (file).toHexString();
    const auto pcm = juce::String::repeatedString ("7", 64);
    auto runtimeSource = makeRuntimeV2Source (file, hash, pcm);
    runtimeSource["audio"].getDynamicObject()->setProperty ("total_sample_frames", static_cast<juce::int64> (songFrames));
    addRuntimeV2MeasurementSummary (runtimeSource, -14.0, -6.0);
    const auto source = stageRuntimeV2Artifact (root, "sources", runtimeSource);
    auto preset = bindRuntimeV2PresetToSource ("88888888-8888-4888-8888-888888888881", "99999999-9999-4999-8999-999999999991",
                                               source, hash, pcm);
    auto* object = preset.getDynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0");
    object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    require (writeJson (root.getChildFile ("library/sets.json"),
                        restartSets ("78787878-7878-4878-8878-787878787878", file, hash, pcm, source))
                 && writeJson (root.getChildFile ("library/manifest.json"), libraryManifest (root, preset, 1)),
             "a library with one B song whose Cue does not loop");

    ref::ReferenceComparisonController controller (root, [] (bool) { return true; });
    controller.configure ({ "cue-restart", {}, 43, true }, 48'000, 2);
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
        require (endReferenceRtProbe() == 0, "restarting the Cue adds no heap operations to the audio callback");
        if (playing) position += block.getNumSamples();
        return rendered;
    };
    for (int attempt = 0; attempt < 1500 && ! controller.snapshot().referenceReady; ++attempt)
    { host (true); juce::Thread::sleep (10); }
    if (! controller.snapshot().referenceReady)
    {
        const auto s = controller.snapshot();
        std::cerr << "cue restart: B " << (s.referenceSelection ? s.referenceSelection->rejectionCode : "-")
                  << " sets " << s.songSets.size() << " issue " << s.songSetsIssue << '\n';
    }
    require (controller.snapshot().referenceReady, "the B song prepares");
    const auto nearCueStart = [&] { const auto value = block.getSample (0, 479); return value > 0.1f && value < rampAt (4'800); };

    // 選んでから曲の終わりまで再生し（Cue の頭のページは追い出される）、Cue を過ぎた。範囲外とは言わず、Cue の頭を
    // 先に読み直して「準備できた」になり、押せば今の位置から Cue の頭が鳴る。
    while (position < songFrames + 48'000)
    { host (true); if (position % 48'000 < 480) juce::Thread::sleep (20); }  // 1 秒ごとに読み込みを待つ（頭を追い出す）
    for (int attempt = 0; attempt < 1500 && ! (controller.snapshot().referenceSelection != nullptr
                                              && controller.snapshot().referenceSelection->auditionBuffered); ++attempt)
    { host (true); juce::Thread::sleep (10); }
    const auto beyond = controller.snapshot();
    require (beyond.referenceSelection != nullptr && ! beyond.referenceSelection->auditionOutsideCue
                 && beyond.referenceSelection->auditionBuffered,
             "past the Cue, B is not reported outside its Cue; it is ready to start at the Cue's head");
    require (controller.requestAudition (3, -14.0, -6.0), "B plays past the Cue");
    host (true); host (true);
    require (controller.snapshot().audibleComparisonSlot == 3 && nearCueStart(), "B starts at the head of its Cue");

    // 鳴らしている B を止め、起点より前（DAW の頭）へ戻して再生する。自動で B に戻り、Cue の頭から鳴る。
    const auto unknown = std::numeric_limits<double>::quiet_NaN();
    require (! host (false) && controller.auditionHeld(), "stopping keeps B");
    controller.servicePendingAudition (unknown, unknown, false);
    position = 0;
    for (int attempt = 0; attempt < 1500 && ! controller.snapshot().referenceReady; ++attempt)
    { host (true); juce::Thread::sleep (10); }
    require (controller.snapshot().referenceReady && ! controller.snapshot().referenceSelection->auditionOutsideCue,
             "before the start point, B is ready again and not outside its Cue");
    controller.servicePendingAudition (unknown, unknown, true);
    host (true); host (true);
    require (controller.snapshot().audibleComparisonSlot == 3 && nearCueStart(),
             "after returning before the start point, B comes back at the head of its Cue");

    // 準備中（Kirin OS がまだ確かめられていない）の曲を選んでも、見出しは役の名前ではなくその曲の名前。
    const auto songs = controller.snapshot().songSets[0].songs;
    require (songs.size() == 2 && controller.selectSong (songs[1].id), "choose the song Kirin OS has not verified");
    for (int attempt = 0; attempt < 1500 && ! (controller.snapshot().referenceSelection != nullptr
            && controller.snapshot().referenceSelection->rejectionCode == "reference_source_unavailable"); ++attempt)
    { host (true); juce::Thread::sleep (10); }
    require (controller.snapshot().referenceSelection->rejectionCode == "reference_source_unavailable"
                 && controller.snapshot().referenceSelection->title == "Pending song",
             "a song still being prepared keeps its name as the B title");
    std::cout << "Reference B restarts its Cue at the playhead after seeks and past its end PASS\n";
}
