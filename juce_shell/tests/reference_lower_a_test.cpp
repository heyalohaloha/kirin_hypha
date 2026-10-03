// 2026-10-03（Daisuke 承認、R-12）：B・C・V の MATCH が上限（True Peak）を超えるとき、承認すれば A（POST の
// 出力全体）を差だけ下げて合わせる。参照は元の音量のまま。下げ終わってから参照を鳴らし（一瞬大きく聴こえない）、
// 試聴の後も RETURN まで下げたまま、RETURN は役を止めてから 0.5 秒で上げる。書き出し・bypass には掛けない。
#include "reference_runtime_v2_analysis_test_support.h"
#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"
#include "reference_rt_probe.h"

void testReferenceLowerA (const juce::File&);

namespace
{
constexpr int songFrames = 480'000;  // 10 秒
constexpr float songLevel = 0.1f, aLevel = 0.5f;
bool closeTo (double value, double expected, double tolerance = 1.0e-4) { return std::abs (value - expected) <= tolerance; }

juce::var lowerASets (const juce::String& setId, const juce::File& song, const juce::String& hash, const juce::String& pcm,
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
    cue->setProperty ("cue_id", "47474747-4747-4747-8747-474747474747");
    cue->setProperty ("label", "Full track");
    cue->setProperty ("sample_rate_hz", 48'000);
    cue->setProperty ("start_sample", 0);
    cue->setProperty ("end_sample", songFrames);
    cue->setProperty ("loop_enabled", true);
    auto* candidate = new juce::DynamicObject();
    candidate->setProperty ("candidate_id", "58585858-5858-4858-8858-585858585858");
    candidate->setProperty ("display_name", song.getFileNameWithoutExtension());
    candidate->setProperty ("source_kind", "catalog_track");
    candidate->setProperty ("source_identity", juce::var (identity));
    candidate->setProperty ("source_artifact", juce::var (artifact));
    candidate->setProperty ("cues", juce::Array<juce::var> { juce::var (cue) });
    candidate->setProperty ("default_cue_id", "47474747-4747-4747-8747-474747474747");
    candidate->setProperty ("preparation_status", "prepared");
    auto* set = new juce::DynamicObject();
    set->setProperty ("song_set_id", setId);
    set->setProperty ("revision_id", "68686868-6868-4868-8868-686868686868");
    set->setProperty ("rank", 1);
    set->setProperty ("name", "Quiet refs");
    set->setProperty ("songs", juce::Array<juce::var> { juce::var (candidate) });
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

void verifyCeilingRules()
{
    // A を下げていなければ今までどおり：+8 dB は参照の peak −2 dBTP で上限（−1 dBTP）を超える。
    require (ref::referenceGainExceedsCeiling (8.0, -2.0, -12.0) && ! ref::referenceGainExceedsCeiling (1.0, -2.0, -12.0),
             "without a held attenuation the ceiling is unchanged");
    // 差だけ下げれば（−8 dB）参照は元の音量で、上限を超えない。少しだけ下げても足りなければ超える。
    require (! ref::referenceGainExceedsCeiling (8.0, -2.0, -12.0, -8.0) && ref::referenceGainExceedsCeiling (8.0, -2.0, -12.0, -4.0),
             "lowering A by the difference keeps the reference at its own level within the ceiling");
    require (closeTo (ref::referenceAttenuationToMatch (8.0), -8.0) && ref::referenceAttenuationToMatch (-3.0) == 0.0,
             "the approval asks for the whole difference, and never for a match that lowers the reference");
    // 追従も下げた後の音で上限を見る：下げた A に対して +1 dB までは動き、それを超えると止まる。
    using Action = ref::TrackingAction;
    require (ref::trackingStep (9.0, 8.0, -2.0, -20.0, 8.0, -8.0).action == Action::move
                 && ref::trackingStep (10.0, 8.0, -2.0, -20.0, 8.0, -8.0).action == Action::stopCeiling,
             "following A checks the ceiling after the held attenuation");
    // 掛ける部品：bypass・書き出し（usable でない）には掛けない。戻すときは 0.5 秒の直線で上げる。
    ref::HeldAttenuation held;
    held.prepare (48'000.0);
    held.hold (-6.0);
    held.hold (-3.0);  // 浅くはしない
    require (closeTo (held.targetDb(), -6.0) && ! held.settled(), "an approval only deepens the attenuation");
    juce::AudioBuffer<float> block (2, 480);
    for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (block.getWritePointer (c), 1.0f, 480);
    held.apply (block, false);
    require (block.getSample (0, 479) == 1.0f, "an offline or bypassed block is never lowered");
    for (int index = 0; index < 10; ++index)
    {
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (block.getWritePointer (c), 1.0f, 480);
        held.apply (block, true);
    }
    require (held.settled() && closeTo (block.getSample (1, 479), std::pow (10.0, -6.0 / 20.0)), "A settles 6 dB lower");
    held.release();
    for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (block.getWritePointer (c), 1.0f, 480);
    held.apply (block, true);
    const auto first = block.getSample (0, 479);
    require (first > 0.5f && first < 0.53f && ! held.settled(), "RETURN rises over half a second, never jumps");
}
}

void testReferenceLowerA (const juce::File& sandbox)
{
    verifyCeilingRules();
    const auto root = sandbox.getChildFile ("lower-a");
    require (root.createDirectory().wasOk(), "lower A fixture directory");
    const auto file = root.getChildFile ("quiet.wav");
    {
        juce::WavAudioFormat format;
        auto stream = file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.release(), 48'000, 2, 32, {}, 0));
        juce::AudioBuffer<float> data (2, songFrames);
        for (int channel = 0; channel < 2; ++channel) juce::FloatVectorOperations::fill (data.getWritePointer (channel), songLevel, songFrames);
        require (writer && writer->writeFromAudioSampleBuffer (data, 0, songFrames), "quiet song samples");
    }
    const auto hash = juce::SHA256 (file).toHexString();
    const auto pcm = juce::String::repeatedString ("6", 64);
    auto runtimeSource = makeRuntimeV2Source (file, hash, pcm);
    runtimeSource["audio"].getDynamicObject()->setProperty ("total_sample_frames", static_cast<juce::int64> (songFrames));
    addRuntimeV2MeasurementSummary (runtimeSource, -18.0, -2.0);  // 静かな参照曲（peak は −2 dBTP）
    const auto source = stageRuntimeV2Artifact (root, "sources", runtimeSource);
    auto preset = bindRuntimeV2PresetToSource ("88888888-8888-4888-8888-888888888882", "99999999-9999-4999-8999-999999999992",
                                               source, hash, pcm);
    auto* object = preset.getDynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0");
    object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    require (writeJson (root.getChildFile ("library/sets.json"),
                        lowerASets ("79797979-7979-4979-8979-797979797979", file, hash, pcm, source))
                 && writeJson (root.getChildFile ("library/manifest.json"), libraryManifest (root, preset, 1)),
             "a library with one quiet B song");

    ref::ReferenceComparisonController controller (root, [] (bool) { return true; });
    controller.configure ({ "lower-a", {}, 44, true }, 48'000, 2);
    controller.setPresented (true);
    juce::AudioBuffer<float> block (2, 480);
    std::int64_t position = 0;
    // A は一定の 0.5（大きなマスター）。usable は書き出し・bypass でないこと。
    const auto host = [&] (bool usable = true)
    {
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (block.getWritePointer (c), aLevel, 480);
        beginReferenceRtProbe();
        controller.observeTransport (position, true, true);
        controller.observeAInput (block, position, true, true, true);
        const bool rendered = controller.renderSelectedB (block, position, true, usable, usable);
        require (endReferenceRtProbe() == 0, "lowering A adds no heap operations to the audio callback");
        position += block.getNumSamples();
        return rendered;
    };
    for (int attempt = 0; attempt < 1500 && ! controller.snapshot().referenceReady; ++attempt)
    { host(); juce::Thread::sleep (10); }
    require (controller.snapshot().referenceReady, "the quiet B song prepares");

    // A −10 LUFS（peak −12 dBTP）に −18 LUFS の参照：+8 dB が要り、参照の peak −2 dBTP で上限を超える。
    require (! controller.requestAudition (3, -10.0, -12.0), "a MATCH over the ceiling does not play B");
    const auto refused = *controller.snapshot().referenceSelection;
    require (refused.matchFailure == ref::MatchFailure::ceilingExceeded && closeTo (refused.neededAttenuationDb, -8.0),
             "the refusal names how far A must be lowered to match");

    // 承認は承認のボタンに出した量で下げる（状態が作り直されても、利用者が見た量）。量の無い承認・鳴らす待ちを
    // 立てられない承認（ローカル Blind の準備中）では A を下げない（下げたまま「下げていない」と言わない）。
    require (! controller.approveLowerAAndPlay (3, 0.0) && controller.heldAttenuationDb() == 0.0,
             "an approval without an amount lowers nothing");
    require (controller.reserveLocalBlind(), "a local Blind takes the audition gate");
    require (! controller.approveLowerAAndPlay (3, refused.neededAttenuationDb) && controller.heldAttenuationDb() == 0.0,
             "an approval that cannot queue the role leaves A at its level");
    controller.releaseLocalBlind (0);

    // 承認：A を 8 dB 下げ、下げ終わってから B を鳴らす。B は元の音量（0.1）、下がっているあいだ B は聴こえない。
    require (controller.approveLowerAAndPlay (3, refused.neededAttenuationDb) && closeTo (controller.heldAttenuationDb(), -8.0),
             "the approval holds A 8 dB lower");
    // 下げるのは 50 ms の直線（1 → 0.398 は約 1445 サンプル、480 のブロックで 4 つ目に着く）。B を選ぶのはその後。
    const auto lowered = static_cast<float> (aLevel * std::pow (10.0, -8.0 / 20.0));
    int blocks = 0;
    float aBeforeB = aLevel;
    for (int attempt = 0; attempt < 400 && controller.snapshot().audibleComparisonSlot != 3; ++attempt)
    {
        host();
        ++blocks;
        aBeforeB = block.getSample (0, 479);
        controller.servicePendingAudition (-10.0, -12.0, true);
        juce::Thread::sleep (2);
    }
    require (blocks >= 4 && closeTo (aBeforeB, lowered), "B is chosen only after A has settled lower");
    host(); host();
    require (controller.snapshot().audibleComparisonSlot == 3 && closeTo (block.getSample (0, 479), songLevel),
             "B plays at its own level, matched to the lowered A");
    require (closeTo (controller.snapshot().heldAttenuationDb, -8.0), "the snapshot reports the held attenuation");

    // 試聴の後も A は下がったまま。書き出し・bypass のブロックには掛けない。
    controller.selectA();
    host(); host();
    require (closeTo (block.getSample (0, 479), lowered), "after B, A stays lowered until RETURN");
    require (! controller.reserveLocalBlind(), "a local Blind waits for RETURN while A is lowered (POST is never lowered twice)");
    host (false);
    require (closeTo (block.getSample (0, 479), aLevel), "an offline or bypassed block keeps A untouched");
    for (int index = 0; index < 12; ++index) host();
    require (closeTo (block.getSample (0, 479), lowered), "after the bypass A is lowered again with a ramp");

    // RETURN：0.5 秒で上げる（急に上げない）。
    controller.returnAToNormalLevel();
    host();
    const auto rising = block.getSample (0, 479);
    require (rising > lowered && rising < aLevel && closeTo (controller.heldAttenuationDb(), 0.0), "RETURN starts a gradual rise");
    for (int index = 0; index < 60; ++index) host();
    require (closeTo (block.getSample (0, 479), aLevel), "A is back at its normal level");
    std::cout << "Reference lowers A to match a quiet reference only after approval, and RETURN restores it PASS\n";
}
