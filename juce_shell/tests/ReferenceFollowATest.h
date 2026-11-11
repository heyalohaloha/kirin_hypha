#pragma once
// B・C の流し方（LOOP／FOLLOW A）。LOOP（既定）は今までどおり Kirin OS の決めた Cue を繰り返す。FOLLOW A は A の
// DAW の位置＋手のずらしで曲全体を鳴らし、seek・停止と再開でも A と同じ位置にいる。曲の外（頭より前・終わりの後）は
// A のまま理由を出し、曲の中へ戻れば押し直さずに戻る。ずらしは曲・役が替わると 0。流し方は state に残り、復元だけでは
// 鳴らし始めない（R-12）。
#include "reference_runtime_v2_analysis_test_support.h"
#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_library_manifest_fixture.h"
#include "reference_rt_probe.h"
#include "ReferenceControllerTestAccess.h"
#include <cmath>
#include <functional>
#include <thread>
#include <utility>

namespace hypha::tests::follow_a
{
constexpr int songFrames = 576'000;                     // 12 秒
constexpr int cueStart = 96'000, cueEnd = 192'000;      // Cue は 2〜4 秒（繰り返す）
constexpr int second = 48'000, half = 24'000;
inline float rampAt (std::int64_t frame) { return 0.1f + 0.3f * static_cast<float> (frame) / static_cast<float> (songFrames); }

inline juce::DynamicObject* cueObject (const char* id)
{
    auto* cue = new juce::DynamicObject();
    cue->setProperty ("cue_id", id);
    cue->setProperty ("label", "Chorus");
    cue->setProperty ("sample_rate_hz", 48'000);
    cue->setProperty ("start_sample", cueStart);
    cue->setProperty ("end_sample", cueEnd);
    cue->setProperty ("loop_enabled", true);
    return cue;
}

// 試験だけが読む MATCH の基（loudness・peak）。FOLLOW A では Cue の値ではなく曲全体の値になる。
struct LevelsTag {};
std::pair<double, double> levelsFor (ref::RuntimeV2Controller&, const ref::RuntimeSource&, bool, double, double, LevelsTag);
template <auto Member> struct LevelsAccess
{
    friend std::pair<double, double> levelsFor (ref::RuntimeV2Controller& r, const ref::RuntimeSource& s, bool cue,
                                                double cueLoudness, double cuePeak, LevelsTag)
    { const auto levels = (r.*Member) (s, cue, cueLoudness, cuePeak, 0.0); return { levels.loudness, levels.peak }; }
};
template struct LevelsAccess<&ref::RuntimeV2Controller::matchLevels>;
// 試験だけが書く公開の値（Kirin OS の Cue の値を静かな区間として与える）。stateLock を持って使う。
struct SnapshotTag {};
ref::Snapshot& snapshotFor (ref::RuntimeV2Controller&, SnapshotTag);
template <auto Member> struct SnapshotAccess
{ friend ref::Snapshot& snapshotFor (ref::RuntimeV2Controller& r, SnapshotTag) { return r.*Member; } };
template struct SnapshotAccess<&ref::RuntimeV2Controller::currentSnapshot>;
// 流し方・ずらしを書いている途中（mappingGeneration が奇数の間）に一度だけ呼ぶ（reference_cue_restart_test.cpp）。
inline std::function<void()> followWriteProbe;

inline juce::var followSets (const juce::String& hash, const juce::String& pcm, const ref::RuntimeContentReceipt& source)
{
    const auto song = [] (const char* candidateId, const char* cueId, const char* name, const juce::var& identity,
                          const juce::var& artifact, const char* status)
    {
        auto* candidate = new juce::DynamicObject();
        candidate->setProperty ("candidate_id", candidateId);
        candidate->setProperty ("display_name", name);
        candidate->setProperty ("source_kind", "catalog_track");
        candidate->setProperty ("source_identity", identity);
        candidate->setProperty ("source_artifact", artifact);
        candidate->setProperty ("cues", juce::Array<juce::var> { juce::var (cueObject (cueId)) });
        candidate->setProperty ("default_cue_id", cueId);
        candidate->setProperty ("preparation_status", status);
        return juce::var (candidate);
    };
    auto* identity = new juce::DynamicObject();
    identity->setProperty ("catalog_reference_id", "catalog:source-test");
    identity->setProperty ("sha256_file", hash);
    identity->setProperty ("sha256_pcm", pcm);
    auto* artifact = new juce::DynamicObject();
    artifact->setProperty ("relative_path", source.relativePath);
    artifact->setProperty ("sha256", source.sha256);
    artifact->setProperty ("bytes", source.bytes);
    auto* other = new juce::DynamicObject();  // 2 曲目は Kirin OS がまだ確かめていない曲（選ぶと曲が替わる）
    other->setProperty ("catalog_reference_id", "catalog:follow-a-other");
    other->setProperty ("sha256_file", juce::String::repeatedString ("8", 64));
    other->setProperty ("sha256_pcm", juce::String::repeatedString ("9", 64));
    auto* set = new juce::DynamicObject();
    set->setProperty ("song_set_id", "79797979-7979-4979-8979-797979797979");
    set->setProperty ("revision_id", "68686868-6868-4868-8868-686868686868");
    set->setProperty ("rank", 1);
    set->setProperty ("name", "Refs");
    set->setProperty ("songs", juce::Array<juce::var> {
        song ("58585858-5858-4858-8858-585858585858", "47474747-4747-4747-8747-474747474747", "Ramp",
              juce::var (identity), juce::var (artifact), "prepared"),
        song ("59595959-5959-4959-8959-595959595959", "48484848-4848-4848-8848-484848484848", "Other",
              juce::var (other), juce::var(), "pending") });
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

inline void testReferenceFollowA (const juce::File& sandbox)
{
    using namespace hypha::tests::follow_a;
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile ("follow-a");
    require (root.createDirectory().wasOk(), "follow A fixture directory");
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
    const auto pcm = juce::String::repeatedString ("6", 64);
    auto runtimeSource = makeRuntimeV2Source (file, hash, pcm);
    runtimeSource["audio"].getDynamicObject()->setProperty ("total_sample_frames", static_cast<juce::int64> (songFrames));
    addRuntimeV2MeasurementSummary (runtimeSource, -14.0, -7.96);  // max TP は傾斜の最大 0.4（-7.96 dBFS）
    const auto source = stageRuntimeV2Artifact (root, "sources", runtimeSource);
    auto preset = bindRuntimeV2PresetToSource ("88888888-8888-4888-8888-888888888882", "99999999-9999-4999-8999-999999999992",
                                               source, hash, pcm);
    auto* object = preset.getDynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0");
    object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    if (auto* checkCue = preset["checks"][0]["candidates"][0]["cues"][0].getDynamicObject())  // C も同じ 2〜4 秒を繰り返す
    { checkCue->setProperty ("start_sample", cueStart); checkCue->setProperty ("end_sample", cueEnd); checkCue->setProperty ("loop_enabled", true); }
    require (writeJson (root.getChildFile ("library/sets.json"), followSets (hash, pcm, source))
                 && writeJson (root.getChildFile ("library/manifest.json"), libraryManifest (root, preset, 1)),
             "a library with one B song whose Cue loops, and a C");

    ref::ReferenceComparisonController controller (root, [] (bool) { return true; });
    controller.configure ({ "follow-a", {}, 44, true }, 48'000, 2);
    controller.setPresented (true);
    juce::AudioBuffer<float> block (2, 480);
    std::int64_t position = 0, rendered = -1;  // rendered：最後に鳴った block の頭の DAW の位置
    float loudest = 0.0f;                      // 出した音の最大（上限の確かめ）
    double serviceLoudness = -14.0, servicePeak = -6.0;  // 制御の処理に渡す A（鳴らす時の A）
    const auto host = [&] (bool playing)
    {
        block.clear();
        beginReferenceRtProbe();
        controller.observeTransport (position, true, playing);
        controller.observeAInput (block, position, true, playing, true);
        const bool sounded = controller.renderSelectedB (block, position, true, true, true);
        require (endReferenceRtProbe() == 0, "FOLLOW A adds no heap operations to the audio callback");
        rendered = sounded ? position : -1;
        for (int ch = 0; ch < 2; ++ch) loudest = std::max (loudest, block.getMagnitude (ch, 0, block.getNumSamples()));
        if (playing) position += block.getNumSamples();
        return sounded;
    };
    auto nextBlock = std::chrono::steady_clock::now();
    // 再生の時間に追いつくまでだけ眠る（遅れた block は眠らない）。制御の処理は block ごとに回す。
    const auto play = [&] (int blocks, const std::function<bool()>& done)
    {
        nextBlock = std::chrono::steady_clock::now();
        for (int index = 0; index < blocks; ++index)
        {
            host (true);
            controller.servicePendingAudition (serviceLoudness, servicePeak, true);
            if (done && done()) return true;
            nextBlock += std::chrono::milliseconds (10);
            std::this_thread::sleep_until (nextBlock);
        }
        return ! done;
    };
    const auto last = [&] { return block.getSample (0, 479); };
    constexpr int wait = 1500;  // 待ちの上限（15 秒ぶんの block）。条件が揃えばすぐ抜ける
    const auto at = [&] (std::int64_t offset)  // 鳴った block の最後の sample が、A の位置＋offset の曲の sample
    { return rendered >= 0 && std::abs (last() - rampAt (rendered + 479 + offset)) < 1.0e-4f; };
    const auto reference = [&] { return controller.snapshot().referenceSelection; };
    const auto deadline = [] { return std::chrono::steady_clock::now() + std::chrono::minutes (5); };

    host (true);
    require (control::waitUntil (deadline(), [&] { return controller.snapshot().referenceReady; }),
             "the B song prepares before the worker watchdog");
    require (! controller.followA() && ! controller.savedSettings().followA && ! reference()->followA,
             "LOOP is the default for B and C");

    // LOOP（今まで通り）：B は Cue（2〜4 秒）の中だけを繰り返す。A の位置は追わない。
    require (play (wait, [&] { return reference()->auditionBuffered; }), "the looped Cue is buffered");
    require (controller.requestAudition (3, -14.0, -6.0), "B is pressed in LOOP");
    require (play (wait, [&] { return controller.snapshot().audibleComparisonSlot == 3 && rendered >= 0; }), "B sounds in LOOP");
    for (int index = 0; index < 150; ++index)
    {
        require (play (1, {}) && rendered >= 0 && last() >= rampAt (cueStart) - 1.0e-4f && last() < rampAt (cueEnd) + 1.0e-4f,
                 "in LOOP, every block of B stays inside its Cue whatever A's position");
    }
    require (reference()->cueWraps && ! reference()->followA, "LOOP is reported as the repeated Cue");
    require (reference()->tracking == ref::TrackingState::following && controller.trackingNeedsService()
                 && controller.liveWindowBlocks (3) == 100,
             "in LOOP B's level follows A's last 10 s, as before");
    // MATCH の基：LOOP は Cue の値、FOLLOW A は曲全体の値（Kirin OS の LUFS-I と max TP）。
    ref::RuntimeSource measured;
    ref::RuntimeMeasurementSummary summary;
    summary.loudnessLufsI = -14.0; summary.maximumTruePeakDbtp = -6.0;
    measured.measurementSummary = summary;
    auto& referenceRole = control::roleFor (controller, { 3 });
    require (levelsFor (referenceRole, measured, true, -20.0, -3.0, LevelsTag {}) == std::pair { -20.0, -3.0 },
             "LOOP matches the Cue's values");
    // 書いている途中の block も前の写像を鳴らし切る（いきなり A にしない）：ずらしと流し方の書き込みの両方。
    const auto midWrite = [&] (const std::function<void()>& write)
    {
        bool sounded = false, probed = false;
        followWriteProbe = [&] { probed = true; sounded = host (true); };
        write();
        followWriteProbe = nullptr;
        return probed && sounded;
    };

    // FOLLOW A に替える：鳴ったまま、A の位置の曲の音へ移る（ページを読む間だけ A）。合わせ方は新しい流し方で先に決め、
    // 写像と一緒に渡す（曲全体と A の ALL の窓、固定）。書いている途中の block は前の写像（LOOP）を鳴らす。
    require (midWrite ([&] { controller.setFollowA (true, -14.0, -6.0); }), "a block during the mode write plays the previous path");
    require (controller.followA() && reference()->followA && ! reference()->cueWraps,
             "FOLLOW A applies to the playing B at once and is no longer a repeated Cue");
    require (levelsFor (referenceRole, measured, true, -20.0, -3.0, LevelsTag {}) == std::pair { -14.0, -6.0 },
             "FOLLOW A matches the whole song's values");
    // 鳴っている B は新しい流し方の決まりで合わせ直す：曲全体と A の ALL の窓、固定（追従しない）。
    require (controller.liveWindowBlocks (3) == 6'000 && controller.liveWindowBlocks (2) == 6'000,
             "FOLLOW A measures A over its ALL window (up to 10 minutes)");
    // （gain 0 dB：下の確かめは鳴った sample をそのまま曲の値と比べる。-3 dB は下の押し直しで確かめる）
    require (std::abs (reference()->appliedGainDb) < 1.0e-9 && reference()->tracking == ref::TrackingState::fixed
                 && ! controller.trackingNeedsService()
                 && controller.followAudition ({}, -6.0) == ref::TrackingAction::keep,
             "FOLLOW A matches the whole song to A once and holds the gain");
    require (play (wait, [&] { return at (0); }), "FOLLOW A plays the song at A's DAW position");
    for (int index = 0; index < 50; ++index)
        require (play (1, {}) && at (0), "FOLLOW A follows A block by block without gaps");
    require (controller.snapshot().audibleComparisonSlot == 3, "switching the mode keeps B selected");

    // 手のずらし：正で曲が先へ、負で後ろへ。
    require (midWrite ([&] { controller.nudgeFollowOffset (3, 0.5); }) && std::abs (reference()->followOffsetSeconds - 0.5) < 1.0e-9,
             "the offset nudges by half a second, and a block during the write plays the previous path");
    require (play (wait, [&] { return at (half); }), "a positive offset plays the song ahead of A");
    require (controller.nudgeFollowOffset (3, -1.0) && std::abs (reference()->followOffsetSeconds + 0.5) < 1.0e-9,
             "the offset can go below zero");
    require (play (wait, [&] { return at (-half); }), "a negative offset plays the song behind A");

    // seek：A と同じ所へ移る。
    position = 6 * second;
    require (play (wait, [&] { return at (-half); }), "after a seek B is at A's new position with the same offset");

    // 停止と再開：止めている間は鳴らさず、別の位置から再生すれば押し直さずにその位置で戻る。
    require (! host (false) && controller.auditionHeld(), "stopping keeps B chosen");
    controller.servicePendingAudition (-14.0, -6.0, false);
    position = 5 * second;
    require (play (wait, [&] { return controller.snapshot().audibleComparisonSlot == 3 && at (-half); }),
             "starting again resumes B at the new position");

    // 曲の終わりの後：A のまま理由を出し、曲の中へ戻れば押し直さずに戻る。
    position = songFrames + second;
    require (play (wait, [&] {
                 const auto state = controller.snapshot();
                 return state.audibleComparisonSlot == 0 && state.referenceSelection->followOutside == 1
                     && state.pendingAudition.reasonCode == "reference_follow_past_end";
             }) && rendered < 0,
             "past the song's end A plays and the reason is published");
    for (int ch = 0; ch < 2; ++ch)
        require (block.findMinMax (ch, 0, 480).getLength() == 0.0f && block.getSample (ch, 0) == 0.0f,
                 "past the song's end the block is A, untouched");
    position = 3 * second;
    require (play (wait, [&] { return controller.snapshot().audibleComparisonSlot == 3 && at (-half); }),
             "moving A back into the song brings B back without pressing it again");

    // 曲の頭より前（負のずらし）：A のまま理由を出し、A が進んで曲の頭に届けば B が入る。
    require (controller.nudgeFollowOffset (3, -9.5) && std::abs (reference()->followOffsetSeconds + 10.0) < 1.0e-9, "a long negative offset");
    position = 0;
    require (play (900, [&] {
                 const auto state = controller.snapshot();
                 return state.referenceSelection->followOutside == -1
                     && state.pendingAudition.reasonCode == "reference_follow_before_start";
             }) && rendered < 0, "before the song's start A plays and the reason is published");
    require (play (wait, [&] { return at (-10 * second); }) && rendered >= 10 * second, "B joins once A reaches the song's start");

    // 曲の外で押した B・C は覚えて待ち、A の位置が曲に入れば押し直さずに鳴る。A・別の役・REF を離れると消える。
    const auto outsideBoth = [&] { return reference()->followOutside == 1 && controller.snapshot().checkSelection->followOutside == 1; };
    const auto waitingFor = [&] (int slot, const char* reason)
    {
        const auto state = controller.snapshot();
        return state.pendingAudition.waiting() && state.pendingAudition.slot == slot && state.pendingAudition.reasonCode == reason
            && state.audibleComparisonSlot == 0;
    };
    const auto staysA = [&] (const char* why) { for (int index = 0; index < 150; ++index) require (play (1, {}) && rendered < 0, why); };
    controller.selectA();
    position = songFrames + 11 * second;  // B は -10 秒ずらしで曲の終わりの後、C（ずらし 0）も曲の外
    require (play (wait, outsideBoth), "A's position is past the end of B's and C's song");
    require (controller.requestAudition (3, -14.0, -6.0) && play (wait, [&] { return waitingFor (3, "reference_follow_past_end"); })
                 && play (20, {}) && rendered < 0 && controller.snapshot().pendingAudition.waiting(),
             "a B press past the song's end is remembered, not refused, and A keeps playing");
    controller.selectA();
    require (! controller.snapshot().pendingAudition.waiting(), "pressing A cancels the remembered press");
    position = 12 * second;
    staysA ("after A cancelled it, moving into the song never starts B");
    position = songFrames + 11 * second;
    require (play (wait, outsideBoth) && controller.requestAudition (3, -14.0, -6.0) && play (wait, [&] { return waitingFor (3, "reference_follow_past_end"); }),
             "B is remembered again");
    position = 12 * second;
    require (play (wait, [&] { return controller.snapshot().audibleComparisonSlot == 3 && at (-10 * second); }),
             "the remembered B starts by itself once A's position is in the song");
    controller.selectA();
    position = songFrames + 11 * second;
    require (play (wait, outsideBoth) && controller.requestAudition (3, -14.0, -6.0) && play (wait, [&] { return waitingFor (3, "reference_follow_past_end"); })
                 && controller.requestAudition (2, -14.0, -6.0) && play (wait, [&] { return waitingFor (2, "reference_follow_past_end"); }),
             "pressing C replaces the remembered B with a remembered C");
    position = 5 * second;
    require (play (wait, [&] { require (controller.snapshot().audibleComparisonSlot != 3, "the replaced B never starts");
                               return controller.snapshot().audibleComparisonSlot == 2 && at (0); }),
             "the remembered C starts once A is in the song");
    controller.selectA();
    position = songFrames + second;
    require (play (wait, outsideBoth) && controller.requestAudition (3, -14.0, -6.0) && play (wait, [&] { return waitingFor (3, "reference_follow_past_end"); }),
             "B is remembered before leaving REF");
    controller.suspendAudition();
    require (! controller.snapshot().pendingAudition.waiting(), "leaving REF cancels the remembered press");
    position = 5 * second;
    staysA ("after leaving REF, moving into the song never starts B");

    // 曲が替わると 0：選んだ曲が鳴らせなくても、戻した曲のずらしは 0 から。
    require (controller.nudgeFollowOffset (3, 0.3), "B has an offset before the song changes");
    const auto songs = controller.snapshot().songSets[0].songs;
    require (songs.size() == 2 && controller.selectSong (songs[1].id) && std::abs (reference()->followOffsetSeconds) < 1.0e-12,
             "choosing another song resets the offset");
    require (controller.selectSong (songs[0].id), "choose the first song again");
    require (control::waitUntil (deadline(), [&] { return reference()->state == ref::RuntimeState::ready; }), "the first song is published again");

    // 役が替わると 0：B のずらしは C を押せば 0 に戻る。
    require (play (wait, [&] { return reference()->auditionBuffered && controller.snapshot().checkReady; }), "B and C are ready");
    require (controller.requestAudition (3, -17.0, -6.0) && play (wait, [&] { return controller.snapshot().audibleComparisonSlot == 3; }),
             "B plays again");
    require (std::abs (reference()->appliedGainDb + 3.0) < 1.0e-9 && reference()->tracking == ref::TrackingState::fixed,
             "a press in FOLLOW A matches the whole song to A at the press and holds it");
    require (controller.nudgeFollowOffset (3, 0.25) && std::abs (reference()->followOffsetSeconds - 0.25) < 1.0e-9, "B is nudged");
    require (controller.requestAudition (2, -14.0, -6.0), "C is pressed");
    require (std::abs (reference()->followOffsetSeconds) < 1.0e-12, "leaving B for C resets B's offset");
    require (play (wait, [&] { return controller.snapshot().audibleComparisonSlot == 2 && at (0); }),
             "C follows A too: one mode for B and C");

    // LOOP へ戻す：C は Cue の中だけを鳴らす（今まで通り）。合わせ方も LOOP の決まりに戻る（C は Cue の窓で固定）。
    controller.setFollowA (false, -14.0, -6.0);
    require (controller.liveWindowBlocks (2) != 6'000 && controller.liveWindowBlocks (3) == 100
                 && controller.snapshot().audibleComparisonSlot == 2
                 && controller.snapshot().checkSelection->tracking == ref::TrackingState::fixed,
             "back in LOOP the windows and the match are LOOP's again");
    require (play (wait, [&] { return rendered >= 0 && last() >= rampAt (cueStart) - 1.0e-4f && last() < rampAt (cueEnd) + 1.0e-4f; }),
             "back in LOOP the Cue repeats again");

    // 上限（-1 dBTP）を一度も超えない：流し方を替える時、鳴っている間も止めている間も、合わせられない時も。B の Cue を
    // 静かな区間として Kirin OS の値を与える（Cue -20 LUFS・max TP -13.98、曲全体 -14・-7.96）。A は -9 LUFS・max TP -6。
    // LOOP は +11 dB、FOLLOW A は +5 dB。前の +11 dB が曲全体（8 秒の所で 0.3）で鳴れば 1.06 になり上限 0.891 を超える。
    constexpr float ceiling = 0.8913f;
    const auto gainOf = [] (double db) { return static_cast<float> (std::pow (10.0, db / 20.0)); };
    const auto playsAt = [&] (double db) { return rendered >= 0 && std::abs (last() - rampAt (rendered + 479) * gainOf (db)) < 1.0e-3f; };
    const auto injectQuietCue = [&]
    {
        const juce::ScopedLock lock (control::lockFor (referenceRole, {}));
        auto& published = snapshotFor (referenceRole, SnapshotTag {});
        published.cueLevelAvailable = true; published.cueIntegratedLoudness = -20.0; published.cueMaximumTruePeakDbtp = -13.98;
    };
    controller.selectA();
    controller.setFollowA (false);
    serviceLoudness = -9.0; servicePeak = -6.0;
    position = 8 * second;
    require (play (wait, [&] { return reference()->auditionBuffered && reference()->state == ref::RuntimeState::ready; }), "B is ready in LOOP");
    injectQuietCue();
    require (controller.requestAudition (3, -9.0, -6.0) && play (wait, [&] { return controller.snapshot().audibleComparisonSlot == 3 && rendered >= 0; })
                 && std::abs (reference()->appliedGainDb - 11.0) < 1.0e-6,
             "B plays its quiet Cue in LOOP at +11 dB");
    position = 8 * second;
    loudest = 0.0f;
    controller.setFollowA (true, -9.0, -6.0);
    require (std::abs (reference()->appliedGainDb - 5.0) < 1.0e-6 && reference()->tracking == ref::TrackingState::fixed,
             "switching while B plays publishes the whole-song MATCH with the new mapping");
    require (play (wait, [&] { return playsAt (5.0); }) && loudest <= ceiling,
             ("switching to FOLLOW A while B plays never exceeds the ceiling: " + juce::String (loudest)).toRawUTF8());
    injectQuietCue();
    controller.setFollowA (false, -9.0, -6.0);
    require (std::abs (reference()->appliedGainDb - 11.0) < 1.0e-6 && play (50, {}) && controller.snapshot().audibleComparisonSlot == 3,
             "back in LOOP B plays its Cue at +11 dB again");
    require (! host (false) && controller.auditionHeld(), "B stops with its LOOP gain held");
    controller.servicePendingAudition (serviceLoudness, servicePeak, false);
    controller.setFollowA (true);  // 止めている間：鳴っている役は無いので A の値は無い
    position = 8 * second;
    loudest = 0.0f;
    require (play (wait, [&] { return controller.snapshot().audibleComparisonSlot == 3 && playsAt (5.0); })
                 && std::abs (reference()->appliedGainDb - 5.0) < 1.0e-6 && loudest <= ceiling,
             ("after a switch while stopped, B comes back with a new whole-song MATCH and never exceeds the ceiling: "
                 + juce::String (loudest)).toRawUTF8());
    // 合わせられない（A の値が無い）：新しい写像は鳴らさず A へ戻し、A を測っていると言って待つ。測れれば新しい MATCH で鳴る。
    serviceLoudness = std::numeric_limits<double>::quiet_NaN();
    loudest = 0.0f;
    controller.setFollowA (false);
    require (play (wait, [&] {
                 const auto state = controller.snapshot();
                 return state.audibleComparisonSlot == 0 && state.pendingAudition.slot == 3 && state.pendingAudition.waiting()
                     && state.pendingAudition.stage == ref::PendingAuditionView::Stage::level;
             }) && play (50, {}) && rendered < 0 && loudest <= ceiling,
             ("a switch that cannot be matched returns to A and waits for A's level: " + juce::String (loudest)).toRawUTF8());
    serviceLoudness = -9.0;
    require (play (wait, [&] { return controller.snapshot().audibleComparisonSlot == 3 && rendered >= 0; }) && play (100, {})
                 && loudest <= ceiling,
             ("once A's level is known, B plays in LOOP with a new MATCH under the ceiling: " + juce::String (loudest)).toRawUTF8());
    // 曲の外で押して覚えた B は、押した時の A（-9）で合わせる。鳴り始める時の A（-14）では合わせない。
    controller.selectA();
    controller.setFollowA (true);
    position = songFrames + 9 * second;
    serviceLoudness = -14.0;
    require (play (wait, [&] { return reference()->followOutside == 1; }) && controller.requestAudition (3, -9.0, -6.0)
                 && play (wait, [&] { return waitingFor (3, "reference_follow_past_end"); }),
             "B pressed past the song's end is remembered with A at the press");
    position = 6 * second;
    require (play (wait, [&] { return controller.snapshot().audibleComparisonSlot == 3 && rendered >= 0; })
                 && std::abs (reference()->appliedGainDb - 5.0) < 1.0e-6,
             ("the remembered B is matched with A at the press: " + juce::String (reference()->appliedGainDb)).toRawUTF8());
    serviceLoudness = -14.0; servicePeak = -6.0;

    // state：流し方を保存して読み戻す。復元は鳴らし始めない。前の版の state は LOOP。
    controller.setFollowA (true);
    auto saved = controller.savedSettings();
    require (saved.followA, "FOLLOW A is saved");
    juce::XmlElement xml ("STATE");
    saved.write (xml);
    const auto read = ref::ReferenceComparisonSettings::read (xml);
    require (read.followA, "FOLLOW A is read back");
    auto* choices = xml.getChildByName ("ReferenceChoices");
    require (choices != nullptr, "the Reference choices are written");
    choices->removeAttribute ("playback");
    require (! ref::ReferenceComparisonSettings::read (xml).followA, "a state without the mode (an earlier Hypha) restores LOOP");
    controller.setFollowA (false);
    controller.restoreSettings (read);
    require (controller.followA() && reference()->followA, "restoring brings FOLLOW A back");
    play (50, {});  // 復元は鳴っていた C を A へ戻す（短いフェード）
    for (int index = 0; index < 100; ++index)
        require (play (1, {}) && rendered < 0, "restoring the mode never starts B or C");
    require (controller.snapshot().audibleComparisonSlot == 0 && ! controller.auditionHeld(), "restoring leaves A playing");
    std::cout << "Reference FOLLOW A PASS\n";
}
