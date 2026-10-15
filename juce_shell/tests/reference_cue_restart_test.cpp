// B（別の曲）は、選んだときの DAW の位置を起点に Cue の頭から進む。選んだ後に DAW を頭へ戻す・Cue を過ぎてから
// 押す・鳴らしている B を止めて起点より前へ戻す、のどれでも「Cue範囲外」にせず、今の位置から Cue の頭を鳴らす
// （2026-10-03。選んだ後に頭へ戻すと B が鳴らせなくなっていた）。
#include "reference_runtime_v2_analysis_test_support.h"
#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"
#include "reference_rt_probe.h"
#include "ReferenceControllerTestAccess.h"
#include <thread>

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

namespace
{
void verifyReferenceCueRestart (const juce::File& sandbox, bool forcePending)
{
    const auto root = sandbox.getChildFile (forcePending ? "cue-restart-pending" : "cue-restart");
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

    int auditions = 0;  // Rust の試聴の許可を取った回数（作り直すと増える）
    ref::ReferenceComparisonController controller (root, [&] (bool active) { if (active) ++auditions; return true; });
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
    namespace control = hypha::tests::reference_control;
    const auto deadline = [] { return std::chrono::steady_clock::now() + std::chrono::seconds (15); };
    host (true);
    require (control::waitUntil (deadline(), [&] { return controller.snapshot().referenceReady; }),
             "the B song prepares within the original deadline");
    const auto nearCueStart = [&] { const auto value = block.getSample (0, 479); return value > 0.1f && value < rampAt (4'800); };

    // 選んでから曲の終わりまで再生し、Cue を過ぎた。範囲外とは言わず、Cue の頭は読んだままで（周回して頭へ戻る役）
    // 「準備できた」になり、押せば今の位置から Cue の頭が鳴る。
    auto nextBlock = std::chrono::steady_clock::now();
    const auto pace = [&] {
        nextBlock += std::chrono::milliseconds (10); // 480 frames at 48 kHz; overdue blocks do not sleep.
        std::this_thread::sleep_until (nextBlock);
    };
    while (position < songFrames + 48'000) { host (true); pace(); }
    require (control::waitUntil (deadline(), [&] {
        const auto s = controller.snapshot();
        return s.referenceSelection != nullptr && s.referenceSelection->auditionBuffered;
    }), "the Cue head is buffered within the original deadline");
    const auto beyond = controller.snapshot();
    require (beyond.referenceSelection != nullptr && ! beyond.referenceSelection->auditionOutsideCue
                 && beyond.referenceSelection->auditionBuffered,
             "past the Cue, B is not reported outside its Cue; it is ready to start at the Cue's head");
    const auto pressedAt = position - 480;  // B の起点は、押したときに見えていた最後のブロックの位置
    auto& reference = control::roleFor (controller, { 3 });
    auto expectedGeneration = reference.requestedGeneration();
    if (forcePending)
    {
        const auto songId = controller.snapshot().selectedSongId;
        const juce::ScopedLock publicationBarrier (control::lockFor (reference, {}));
        require (controller.snapshot().audibleComparisonSlot == 0 && controller.selectSong (songId),
                 "reselect the same real song while A plays, before the worker can publish it");
        expectedGeneration = reference.requestedGeneration();
        require (expectedGeneration > reference.snapshot().selectionGeneration
                     && controller.requestAudition (3, -14.0, -6.0)
                     && controller.snapshot().audibleComparisonSlot == 0 && controller.pendingAuditionNeedsService(),
                 "one B request is accepted pending, without fabricated readiness or output");
    } // Release the publication barrier before any wait or control service.
    else require (controller.requestAudition (3, -14.0, -6.0), "B plays past the Cue");
    const auto finishSelection = [&] (double loudness, double peak)
    {
        return control::waitUntil (deadline(), [&] {
            const auto state = controller.snapshot();
            const auto role = state.referenceSelection;
            require (role != nullptr && role->state != ref::RuntimeState::rejected
                         && role->state != ref::RuntimeState::disconnected,
                     "selection preparation cannot fail silently");
            const auto stage = state.pendingAudition.stage;
            require (stage == ref::PendingAuditionView::Stage::none || state.pendingAudition.waiting(),
                     "a terminal pending failure cannot be retried away");
            if (role->selectionGeneration != expectedGeneration) return false;
            controller.servicePendingAudition (loudness, peak, true);
            return controller.snapshot().audibleComparisonSlot == 3;
        });
    };
    require (finishSelection (-14.0, -6.0) && position == pressedAt + 480,
             "control service completes the accepted request without advancing host time or clicking again");
    require (host (true) && nearCueStart() && std::abs (block.getSample (0, 479) - rampAt (959)) < 1.0e-4f,
             "the first selected callback renders the Cue head at the original playhead anchor");
    require (host (true) && controller.snapshot().audibleComparisonSlot == 3 && nearCueStart()
                 && std::abs (block.getSample (0, 479) - rampAt (1439)) < 1.0e-4f,
             "B starts at the head of its Cue in the original second callback");

    // 鳴っている B が Cue の終わりを越えても、A へ切らずに Cue の頭へ周回して鳴り続ける（鳴らない間を作らない・Rust の
    // 試聴を作り直さない）。背景の読み込みに 1 ページずつ追いつかせ、どのブロックも鳴ったかを確かめる。
    const auto auditionsBefore = auditions;
    const auto cueEndAt = pressedAt + songFrames;
    int gaps = 0;
    nextBlock = std::chrono::steady_clock::now();
    while (position < cueEndAt + 48'000)
    {
        if (! host (true)) ++gaps;
        pace();
    }
    require (gaps == 0 && auditions == auditionsBefore && controller.snapshot().audibleComparisonSlot == 3
                 && std::abs (block.getSample (0, 479) - rampAt (position - 1 - cueEndAt)) < 1.0e-4f,
             "past its Cue's end, B goes on from the Cue's head without a gap and without a new audition");

    // 鳴らしている B を止め、起点より前（DAW の頭）へ戻して再生する。自動で B に戻り、Cue の頭から鳴る。
    const auto unknown = std::numeric_limits<double>::quiet_NaN();
    require (! host (false) && controller.auditionHeld(), "stopping keeps B");
    controller.servicePendingAudition (unknown, unknown, false);
    position = 0;
    host (true);
    require (control::waitUntil (deadline(), [&] { return controller.snapshot().referenceReady; })
                 && ! controller.snapshot().referenceSelection->auditionOutsideCue,
             "before the start point, B is ready again and not outside its Cue");
    require (finishSelection (unknown, unknown), "the held selection resumes through control service");
    host (true); host (true);
    require (controller.snapshot().audibleComparisonSlot == 3 && nearCueStart(),
             "after returning before the start point, B comes back at the head of its Cue");

    // 準備中（Kirin OS がまだ確かめられていない）の曲を選んでも、見出しは役の名前ではなくその曲の名前。
    const auto songs = controller.snapshot().songSets[0].songs;
    require (songs.size() == 2 && controller.selectSong (songs[1].id), "choose the song Kirin OS has not verified");
    require (control::waitUntil (deadline(), [&] {
        const auto s = controller.snapshot();
        return s.referenceSelection != nullptr && s.referenceSelection->rejectionCode == "reference_source_unavailable";
    }), "the missing fixture song is rejected within the original deadline");
    require (controller.snapshot().referenceSelection->rejectionCode == "reference_source_unavailable"
                 && controller.snapshot().referenceSelection->title == "Pending song",
             "a song still being prepared keeps its name as the B title");
    std::cout << "Reference B Cue restart (" << (forcePending ? "forced pending" : "ordinary") << ") PASS\n";
}

}

void testReferenceCueRestart (const juce::File& sandbox)
{
    verifyReferenceCueRestart (sandbox, false);
    verifyReferenceCueRestart (sandbox, true);
}
