// 2026-10-03（R-12）：B・C・V の MATCH が上限（True Peak）を超えるとき、承認すれば A（POST の
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

// A の直近 10 秒（10 Hz のメーター履歴）が一定の音量。
std::vector<KirinMeterHistoryEntry> steadyA (double lufs)
{
    std::vector<KirinMeterHistoryEntry> history (100);
    for (auto& entry : history)
    {
        entry = {};
        entry.measurement_epoch = 1; entry.run_id = 1; entry.observation_count = 1;
        entry.resolution = KIRIN_METER_HISTORY_10_HZ;
        entry.lufs_m = { lufs, lufs, lufs };
        entry.true_peak = { -12.0, -12.0, -12.0 };
    }
    return history;
}

juce::var lowerASets (const juce::String& setId, const juce::File& song, const juce::String& hash, const juce::String& pcm,
                      const ref::RuntimeContentReceipt& source, bool prepared = true, int revision = 1,
                      const juce::String& setName = "Quiet refs")
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
    candidate->setProperty ("source_artifact", prepared ? juce::var (artifact) : juce::var());  // 準備中は音源の記録が無い
    candidate->setProperty ("cues", juce::Array<juce::var> { juce::var (cue) });
    candidate->setProperty ("default_cue_id", "47474747-4747-4747-8747-474747474747");
    candidate->setProperty ("preparation_status", prepared ? "prepared" : "pending");
    auto* set = new juce::DynamicObject();
    set->setProperty ("song_set_id", setId);
    set->setProperty ("revision_id", "68686868-6868-4868-8868-686868686868");
    set->setProperty ("rank", 1);
    set->setProperty ("name", setName);
    set->setProperty ("songs", juce::Array<juce::var> { juce::var (candidate) });
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "kirin_hypha_reference_library_sets");
    root->setProperty ("version", "1.0");
    root->setProperty ("revision", revision);
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
    held.apply (block, false, true);
    require (block.getSample (0, 479) == 1.0f, "an offline or bypassed block is never lowered");
    const auto run = [&] (bool rolesSilent)
    {
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (block.getWritePointer (c), 1.0f, 480);
        held.apply (block, true, rolesSilent);
        return block.getSample (0, 479);
    };
    for (int index = 0; index < 10; ++index) run (true);
    const auto settledLevel = static_cast<float> (std::pow (10.0, -6.0 / 20.0));
    require (held.settled() && closeTo (block.getSample (1, 479), settledLevel), "A settles 6 dB lower");
    // RETURN は、出力を持つ役が無いブロックで上げ始める（役は下げた A に合わせてあり、先に上げると自分の音量より大きく鳴る）。
    held.requestReturn();
    require (held.targetDb() == 0.0 && held.returning() && ! held.held() && ! held.settled(),
             "after RETURN the held amount is gone, and new roles wait until A is back");
    require (closeTo (run (false), settledLevel) && held.returning(), "A does not rise while a role still has the output");
    const auto first = run (true);
    require (first > settledLevel && first < settledLevel + 0.03f && held.returning(), "RETURN rises over half a second, never jumps");
    for (int index = 0; index < 60; ++index) run (true);
    require (held.settled() && ! held.returning() && closeTo (block.getSample (0, 479), 1.0f), "A is back at its normal level");
    // RETURN を待つあいだに承認で深くしたら、上げない（承認が先）。
    held.hold (-6.0);
    for (int index = 0; index < 10; ++index) run (true);
    held.requestReturn();
    held.hold (-9.0);
    run (true);
    require (held.held() && closeTo (held.targetDb(), -9.0) && ! held.returning(), "an approval after RETURN keeps A lowered");
    held.release();
    for (int index = 0; index < 60; ++index) run (true);
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
    const auto host = [&] (bool usable = true, bool playing = true)
    {
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (block.getWritePointer (c), aLevel, 480);
        beginReferenceRtProbe();
        controller.observeTransport (position, true, playing);
        controller.observeAInput (block, position, true, playing, true);
        const bool rendered = controller.renderSelectedB (block, position, true, usable, usable);
        require (endReferenceRtProbe() == 0, "lowering A adds no heap operations to the audio callback");
        position += block.getNumSamples();
        return rendered;
    };
    // 作業スレッドを待つ：条件が満ちるまでブロックを流して待ちを回す（期限 10 秒。決まった回数の sleep に頼らない）。
    bool sounded = false;  // 最後のブロックで役が鳴ったか
    const auto until = [&] (const auto& condition, const char* what, bool playing = true)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + 10'000;
        while (! condition())
        {
            require (juce::Time::getMillisecondCounter() < deadline, what);
            sounded = host (true, playing);
            controller.servicePendingAudition (-10.0, -12.0, true);
            juce::Thread::sleep (1);
        }
    };
    until ([&] { const auto s = controller.snapshot(); return s.referenceReady && s.checkReady; }, "the quiet B song prepares");

    // 押したときも 0.5 dB の決まり（B の peak −2 dBTP、上限 −1 dBTP）：上限に 0.3 dB 届かないなら上限まで上げて鳴らし
    // 届かない量を持つ。0.6 dB なら断り、承認すれば合う下げ幅を言う。B と C で同じ。
    for (const int slot : { 3, 2 })
    {
        const auto role = [&] { const auto s = controller.snapshot(); return slot == 3 ? *s.referenceSelection : *s.checkSelection; };
        require (controller.requestAudition (slot, -16.7, -12.0), "a role 0.3 dB short of the ceiling plays at the ceiling");
        require (closeTo (role().appliedGainDb, 1.0, 1.0e-6) && closeTo (role().peakShortfallDb, 0.3, 1.0e-6),
                 "it says how far short of A it plays");
        controller.selectA(); host(); host();
        require (! controller.requestAudition (slot, -16.4, -12.0) && role().matchFailure == ref::MatchFailure::ceilingExceeded
                     && closeTo (role().neededAttenuationDb, -1.6, 1.0e-6),
                 "a role 0.6 dB short is refused with the amount A must be lowered");
    }

    // 停止・シークで戻しても、追従で変わった「上限で届かない量」まで同じに戻る。
    require (controller.requestAudition (3, -18.5, -12.0) && closeTo (controller.snapshot().referenceSelection->appliedGainDb, -0.5),
             "B plays below the ceiling");
    host(); host();
    require (controller.followAudition (steadyA (-16.7), -12.0) == ref::TrackingAction::move
                 && closeTo (controller.snapshot().referenceSelection->peakShortfallDb, 0.3, 1.0e-6),
             "following A up to the ceiling says how far short it plays");
    for (int index = 0; index < 4; ++index) host (true, false);
    require (! host (true, false) && controller.auditionHeld(), "a stop keeps the B selection");
    sounded = false;
    until ([&] { return sounded; }, "B comes back after the stop");
    require (closeTo (controller.snapshot().referenceSelection->peakShortfallDb, 0.3, 1.0e-6)
                 && closeTo (controller.snapshot().referenceSelection->appliedGainDb, 1.0, 1.0e-6),
             "B comes back with the same gain and the same shortfall");

    // ローカル Blind が B の戻す控えを譲らせるのは、Rust の許可が通った後だけ（断られたら利用者の選択は残る）。
    until ([&] { return ! controller.outputDecision (hypha::output_owner::Activity::localBlind).refused(); },
           "B returns its output while stopped", false);
    require (controller.auditionHeld(), "B is held while stopped");
    require (controller.reserveLocalBlind(), "a local Blind reserves its slot");
    controller.releaseLocalBlind (0);  // Rust が断った
    require (controller.auditionHeld(), "a refused local Blind leaves the held B selection");
    require (controller.reserveLocalBlind(), "a local Blind reserves its slot again");
    controller.bindLocalBlind (7);     // Rust が許した
    require (! controller.auditionHeld(), "an admitted local Blind clears the held B selection");
    controller.releaseLocalBlind (7);

    // Kirin OS がライブラリを送り直し、B の曲をいったん「準備中」にしてから同じ音で戻しても、B は勝手に鳴り直さない。
    // 戻す控えと「鳴っている役」の印を消し、音源が変わったと言う。
    until ([&] { return controller.requestAudition (3, -16.7, -12.0) && controller.snapshot().audibleComparisonSlot == 3; },
           "B plays again");
    for (int index = 0; index < 4; ++index) host (true, false);
    require (controller.auditionHeld(), "B is held while stopped before the library changes");
    require (writeJson (root.getChildFile ("library/sets.json"),
                        lowerASets ("79797979-7979-4979-8979-797979797979", file, hash, pcm, source, false, 2)),
             "Kirin OS republishes the B song as being prepared");
    until ([&] { return controller.snapshot().referenceSelection->rejectionCode == "reference_source_unavailable"; },
           "the B song becomes unavailable", false);
    require (writeJson (root.getChildFile ("library/sets.json"),
                        lowerASets ("79797979-7979-4979-8979-797979797979", file, hash, pcm, source, true, 3)),
             "Kirin OS republishes the same B song as prepared");
    until ([&] { return controller.snapshot().referenceReady; }, "the same B song is ready again", false);
    for (int index = 0; index < 40; ++index) { host(); controller.servicePendingAudition (-10.0, -12.0, true); }
    require (controller.snapshot().audibleComparisonSlot == 0 && ! controller.auditionHeld()
                 && controller.snapshot().pendingAudition.stage == ref::PendingAuditionView::Stage::sourceChanged,
             "a republished B never plays on its own; the selection ends as a source change");

    // A −10 LUFS（peak −12 dBTP）に −18 LUFS の参照：+8 dB が要り、参照の peak −2 dBTP で上限を超える。
    require (! controller.requestAudition (3, -10.0, -12.0), "a MATCH over the ceiling does not play B");
    const auto refused = *controller.snapshot().referenceSelection;
    require (refused.matchFailure == ref::MatchFailure::ceilingExceeded && closeTo (refused.neededAttenuationDb, -8.0),
             "the refusal names how far A must be lowered to match");

    // 承認はボタンに出した申し出（その役・その音・その選択・その MATCH の失敗）を、出した量で受ける。量の無い承認・
    // 鳴らす待ちを立てられない承認（ローカル Blind の準備中）・別の役や古い申し出では A を下げない。
    using Approval = ref::LowerAApproval;
    const auto offerFor = [&] (int slot, double db)
    {
        const auto s = *controller.snapshot().referenceSelection;
        return ref::LowerAOffer { slot, db, s.playbackIdentity, s.selectionGeneration, s.matchFailureSerial };
    };
    require (controller.approveLowerAAndPlay (offerFor (3, 0.0)) == Approval::refused && controller.heldAttenuationDb() == 0.0,
             "an approval without an amount lowers nothing");
    require (controller.approveLowerAAndPlay (offerFor (2, refused.neededAttenuationDb)) != Approval::lowered
                 && controller.heldAttenuationDb() == 0.0,
             "B's offer cannot be approved as another role's");
    const auto stale = offerFor (3, refused.neededAttenuationDb);
    require (! controller.requestAudition (3, -10.0, -12.0)
                 && controller.approveLowerAAndPlay (stale) == Approval::stale && controller.heldAttenuationDb() == 0.0,
             "an offer from before pressing the role again is not approved");
    require (controller.reserveLocalBlind(), "a local Blind takes the audition gate");
    require (controller.approveLowerAAndPlay (offerFor (3, refused.neededAttenuationDb)) != Approval::lowered
                 && controller.heldAttenuationDb() == 0.0,
             "an approval that cannot queue the role leaves A at its level");
    controller.releaseLocalBlind (0);

    // Kirin OS が同じ音のまま一覧を送り直しても、出した申し出はそのまま承認できる（失敗の番号は変わらない）。
    const auto shown = offerFor (3, refused.neededAttenuationDb);
    require (writeJson (root.getChildFile ("library/sets.json"),
                        lowerASets ("79797979-7979-4979-8979-797979797979", file, hash, pcm, source, true, 4, "Quiet refs 2")),
             "Kirin OS republishes the same B song");
    until ([&] { const auto s = controller.snapshot(); return ! s.songSets.empty() && s.songSets[0].name == "Quiet refs 2"; },
           "the library is read again");
    require (controller.snapshot().referenceSelection->matchFailureSerial == shown.failureSerial,
             "republishing the same source keeps the offer");

    // 承認：A を 8 dB 下げ、下げ終わってから B を鳴らす。B は元の音量（0.1）、下がっているあいだ B は聴こえない。
    require (controller.approveLowerAAndPlay (shown) == Approval::lowered && closeTo (controller.heldAttenuationDb(), -8.0),
             "the approval holds A 8 dB lower");
    // 下げるのは 50 ms の直線（1 → 0.398 は約 1445 サンプル、480 のブロックで 4 つ目に着く）。B を選ぶのはその後。
    const auto lowered = static_cast<float> (aLevel * std::pow (10.0, -8.0 / 20.0));
    int blocks = 0;
    float aBeforeB = aLevel;
    const auto deadline = juce::Time::getMillisecondCounter() + 10'000;
    while (controller.snapshot().audibleComparisonSlot != 3)
    {
        require (juce::Time::getMillisecondCounter() < deadline, "the approved B plays");
        host();
        ++blocks;
        aBeforeB = block.getSample (0, 479);
        controller.servicePendingAudition (-10.0, -12.0, true);
        juce::Thread::sleep (1);
    }
    require (blocks >= 4 && closeTo (aBeforeB, lowered), "B is chosen only after A has settled lower");
    host(); host();
    require (controller.snapshot().audibleComparisonSlot == 3 && closeTo (block.getSample (0, 479), songLevel),
             "B plays at its own level, matched to the lowered A");
    require (closeTo (controller.snapshot().heldAttenuationDb, -8.0), "the snapshot reports the held attenuation");
    // 承認した量は、Kirin OS が一覧を送り直した後も残る（2026-10-06）。
    require (writeJson (root.getChildFile ("library/sets.json"),
                        lowerASets ("79797979-7979-4979-8979-797979797979", file, hash, pcm, source, true, 5, "Quiet refs 3")),
             "Kirin OS republishes the list while A is lowered");
    until ([&] { const auto s = controller.snapshot(); return ! s.songSets.empty() && s.songSets[0].name == "Quiet refs 3"; },
           "the library is read again while A is lowered");
    require (closeTo (controller.heldAttenuationDb(), -8.0) && closeTo (controller.snapshot().heldAttenuationDb, -8.0),
             "the approved amount stays after the library is read again");

    // 試聴の後も A は下がったまま。書き出し・bypass のブロックには掛けない。
    controller.selectA();
    host(); host();
    require (closeTo (block.getSample (0, 479), lowered), "after B, A stays lowered until RETURN");
    require (! controller.reserveLocalBlind(), "a local Blind waits for RETURN while A is lowered (POST is never lowered twice)");
    // 出力の持ち主の表：下げているあいだ VERSION BLIND と live 比較は RETURN が先。B・C・V と、さらに深くする承認は通る。
    {
        using hypha::output_owner::Activity;
        using hypha::output_owner::Reason;
        const auto blind = controller.outputDecision (Activity::versionBlind);
        const auto listen = controller.outputDecision (Activity::liveCompare);
        require (blind.refused() && blind.reason == Reason::returnFirst && listen.refused() && listen.reason == Reason::returnFirst,
                 "VERSION BLIND and LISTEN wait for RETURN while A is lowered");
        require (! controller.outputDecision (Activity::audition).refused() && ! controller.outputDecision (Activity::lowerA).refused(),
                 "B, C and V still play against the lowered A, and a deeper approval is allowed");
    }
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

    // 待たせた役：A の音量がまだ無いときに押した B は待ち、合わせる時点で上限を超えて止まる。そのときも
    // 下げ幅が状態に残り、承認から鳴らせる（画面は押し直させずに承認を出す。2026-10-04）。
    require (controller.requestAudition (3, std::numeric_limits<double>::quiet_NaN(), -12.0)
                 && controller.snapshot().audibleComparisonSlot == 0,
             "a role pressed before A has a level waits (A stays live)");
    until ([&] { return controller.snapshot().pendingAudition.stage == ref::PendingAuditionView::Stage::ceilingExceeded; },
           "the waiting role reaches the ceiling");
    const auto stopped = controller.snapshot();
    require (stopped.pendingAudition.stage == ref::PendingAuditionView::Stage::ceilingExceeded
                 && stopped.referenceSelection->matchFailure == ref::MatchFailure::ceilingExceeded
                 && closeTo (stopped.referenceSelection->neededAttenuationDb, -8.0),
             "a waiting role stopped by the ceiling still names how far A must be lowered");
    // 承認の量が鳴らす時点の差より小さかった（再生を始めた直後の見積もり）ときは、その時点の差まで下げ直して鳴らす。
    require (controller.approveLowerAAndPlay (offerFor (3, -5.0)) == Approval::lowered && closeTo (controller.heldAttenuationDb(), -5.0),
             "the waiting role can be approved without pressing it again, first by the offered amount");
    until ([&] { return controller.snapshot().audibleComparisonSlot == 3; }, "the approved role plays");
    require (controller.snapshot().audibleComparisonSlot == 3 && closeTo (controller.heldAttenuationDb(), -8.0),
             "an approval smaller than the difference at play time lowers A to that difference and plays");

    // RETURN を B が鳴っているときに押す：B を止めてから A を上げる。B が出力を持つあいだ、出力は下げた A と B の
    // 音量を超えない（先に上げると B が自分の音量より大きく鳴る）。
    for (int index = 0; index < 4; ++index) host();
    require (closeTo (block.getSample (0, 479), songLevel), "B sounds at its own level before RETURN");
    const auto loweredNow = static_cast<float> (aLevel * std::pow (10.0, controller.heldAttenuationDb() / 20.0));
    controller.returnAToNormalLevel();
    require (controller.heldAttenuationDb() == 0.0 && ! controller.auditionHeld(), "RETURN gives up the held amount and the B selection");
    float loudestWhileB = 0.0f;
    int roleBlocks = 0;
    while (host())
    {
        loudestWhileB = std::max (loudestWhileB, block.getMagnitude (0, block.getNumSamples()));
        require (++roleBlocks < 100, "B fades out after RETURN");
    }
    require (roleBlocks > 0 && loudestWhileB <= std::max (loweredNow, songLevel) + 1.0e-4f,
             "A does not rise while B still has the output");
    float level = block.getSample (0, 479);
    for (int attempt = 0; attempt < 400 && ! closeTo (block.getSample (0, 479), aLevel); ++attempt)
    {
        host();
        controller.servicePendingAudition (-10.0, -12.0, true);
        require (block.getSample (0, 479) + 1.0e-6f >= level, "after B has stopped, A only rises");
        level = block.getSample (0, 479);
    }
    require (closeTo (block.getSample (0, 479), aLevel) && controller.snapshot().audibleComparisonSlot == 0,
             "A is back at its normal level and B stays stopped");
    for (int index = 0; index < 10; ++index) { host (true, false); controller.servicePendingAudition (-10.0, -12.0, true); }
    for (int index = 0; index < 20; ++index) { host(); controller.servicePendingAudition (-10.0, -12.0, true); }
    require (controller.snapshot().audibleComparisonSlot == 0 && ! controller.auditionHeld(), "after RETURN, stop and play keep A");

    // 下げ直しは深くする向きだけ、2 回まで（2026-10-06）。承認の後に A が大きくなり続けると、その時点の差まで 2 回下げ
    // 直し、3 回目は鳴らさずに理由を言う（下げた量はそのまま）。B の要る下げ幅は −(A の音量 + 18)。
    until ([&] { return controller.snapshot().referenceReady; }, "B is ready again");
    require (! controller.requestAudition (3, -16.4, -12.0) && closeTo (controller.snapshot().referenceSelection->neededAttenuationDb, -1.6, 1.0e-3),
             "B is refused 1.6 dB short");
    require (controller.approveLowerAAndPlay (offerFor (3, -1.6)) == Approval::lowered && closeTo (controller.heldAttenuationDb(), -1.6),
             "the offered amount is approved");
    const auto pendingStage = [&] { return controller.snapshot().pendingAudition.stage; };
    // 下げ終わってから鳴らす（50 ms の直線）ので、試みのたびに A が落ち着くまでブロックを流す。
    const auto settle = [&] { for (int index = 0; index < 8; ++index) host(); };
    for (const auto& [loudness, held] : { std::pair { -14.0, -4.0 }, std::pair { -12.0, -6.0 } })
    {
        settle();
        controller.servicePendingAudition (loudness, -12.0, true);
        require (closeTo (controller.heldAttenuationDb(), held, 1.0e-3) && controller.snapshot().audibleComparisonSlot == 0
                     && pendingStage() == ref::PendingAuditionView::Stage::checking,
                 ("A grew louder after the approval: A is lowered again to the difference at play time (held "
                  + juce::String (controller.heldAttenuationDb(), 2) + ", slot " + juce::String (controller.snapshot().audibleComparisonSlot)
                  + ", stage " + juce::String (static_cast<int> (pendingStage())) + ", needed "
                  + juce::String (controller.snapshot().referenceSelection->neededAttenuationDb, 2) + ")").toRawUTF8());
    }
    settle();
    controller.servicePendingAudition (-10.0, -12.0, true);
    require (closeTo (controller.heldAttenuationDb(), -6.0, 1.0e-3) && controller.snapshot().audibleComparisonSlot == 0
                 && pendingStage() == ref::PendingAuditionView::Stage::ceilingExceeded,
             "a third deeper difference is not chased: B stops with its reason and A stays where it was");
    controller.returnAToNormalLevel();
    for (int index = 0; index < 60; ++index) host();
    // 浅くはしない：鳴らす時点の差が承認した量より小さければ、承認した量のまま鳴らす。
    until ([&] { return controller.snapshot().referenceReady; }, "B is ready after RETURN");
    require (! controller.requestAudition (3, -14.0, -12.0), "B is refused 4 dB short");
    require (controller.approveLowerAAndPlay (offerFor (3, -4.0)) == Approval::lowered, "4 dB is approved");
    for (int attempt = 0; attempt < 400 && controller.snapshot().audibleComparisonSlot != 3; ++attempt)
    {
        host();
        controller.servicePendingAudition (-15.0, -12.0, true);
    }
    require (controller.snapshot().audibleComparisonSlot == 3 && closeTo (controller.heldAttenuationDb(), -4.0, 1.0e-3),
             "a smaller difference at play time plays at the approved amount, never shallower");
    controller.returnAToNormalLevel();
    for (int index = 0; index < 60; ++index) host();
    std::cout << "Reference lowers A to match a quiet reference only after approval, and RETURN restores it PASS\n";
}
