#pragma once
#include "ReferenceSourceCtimeTest.h"
#include "ReferenceVersionFormatFixture.h"
#include "ReferenceManualService.h"
#include "reference_rt_probe.h"

namespace hypha::tests::reference_control
{
struct StagedLimitTag {};
std::uint32_t& stagedLimitFor (Runtime&, StagedLimitTag);
template <auto Member> struct StagedLimitAccess
{ friend std::uint32_t& stagedLimitFor (Runtime& r, StagedLimitTag) { return r.*Member; } };
template struct StagedLimitAccess<&Runtime::stagedVersionLimitMs>;
struct PositionSearchFacts { bool searching = false, window = false, staged = false; };
struct PositionSearchTag {};
PositionSearchFacts positionSearchFor (Runtime&, PositionSearchTag);
void waitPositionSearchFor (Runtime&, PositionSearchTag);
void waitBehindWorkerFor (Runtime&, PositionSearchTag);
void waitSourceCheckFor (Runtime&, PositionSearchTag);
template <auto Member, auto Prepared, auto Source, auto Retired, auto RetiredChecks> struct PositionSearchAccess
{
    friend PositionSearchFacts positionSearchFor (Runtime& r, PositionSearchTag)
    {
        const auto& running = r.*Member;
        const auto& prepared = r.*Prepared;
        return { running.search.valid() && running.search.wait_for (std::chrono::seconds (0)) == std::future_status::timeout,
                 running.capture != nullptr, prepared && prepared->versionPosition.search.valid() };
    }
    friend void waitPositionSearchFor (Runtime& r, PositionSearchTag)
    {
        const auto& running = r.*Member;
        if (running.search.valid()) running.search.wait();
    }
    friend void waitSourceCheckFor (Runtime& r, PositionSearchTag)
    {
        if (const auto& check = (r.*Source).check; check.valid()) check.wait();
    }
    // 同期で回す block は実時間より速い。裏の探索と hash の終わりを待たないと、遅い host で素材の終わりまで回り切る。
    friend void waitBehindWorkerFor (Runtime& r, PositionSearchTag)
    {
        waitPositionSearchFor (r, {});
        if (const auto& prepared = r.*Prepared; prepared && prepared->versionPosition.search.valid())
            prepared->versionPosition.search.wait();
        waitSourceCheckFor (r, {});
        // 取り消した探索も枠を持つ。残っていると次の探索が busy で空振りする。
        for (const auto& search : r.*Retired) if (search.valid()) search.wait();
        for (const auto& check : r.*RetiredChecks) if (check.valid()) check.wait();
    }
};
template struct PositionSearchAccess<&Runtime::positionSearch, &Runtime::preparedWorkspace, &Runtime::sourceVerification,
                                     &Runtime::retiredVersionSearches, &Runtime::retiredSourceVerifications>;
}

inline void testReferenceVersionSwitch (const juce::File& sandbox, bool edited = false)
{
    namespace control = hypha::tests::reference_control;
    // 192 kHz の Version は host の 1 sample が音源の 2 sample。探した結果のこの差で V を止めない。
    require (! ref::referenceAlignmentMoved (13553526, 13553524, 192000, 96000) && ref::referenceAlignmentMoved (13553527, 13553524, 192000, 96000),
             "a higher source rate tolerates one host sample");
    require (! ref::referenceAlignmentMoved (1005, 1000, 192000, 44100) && ref::referenceAlignmentMoved (1006, 1000, 192000, 44100),
             "a fractional rate ratio rounds up to one host sample");
    require (! ref::referenceAlignmentMoved (1001, 1000, 48000, 96000) && ref::referenceAlignmentMoved (1002, 1000, 48000, 96000),
             "a source below the host rate keeps the one sample limit");
    const auto root = sandbox.getChildFile (edited ? "version-switch-edited" : "version-switch");
    require (root.createDirectory().wasOk(), "version switch fixture directory");
    const auto base = makeWholeSongFixture (root, root.getChildFile ("input.wav"),
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333");
    juce::Array<juce::var> entries;
    juce::StringArray ids;
    juce::var manifest;
    for (int n = 1; n <= 3; ++n)
    {
        const int rate = n == 3 ? 192000 : 48000;
        const bool shifted = n == 2 || (edited && n == 3);
        manifest = hypha::tests::version_formats::publish (root, base.audio, rate, n == 2,
                                                         shifted ? rate * 137 / 1000 : 0, n,
                                                         edited && n == 3 ? 0.06f : 0.0f);
        const auto entry = manifest["versions"][0];
        const auto id = entry["entry_id"].toString();
        entries.add (entry); ids.add (id + "/" + id + "/" + id);
    }
    manifest.getDynamicObject()->setProperty ("versions", entries);
    require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "publish all format versions together");
    ref::ReferenceComparisonController c (root);
    control::stopWorkers (c); c.configure ({"switch-post", {}, 42, true}, 48000, 2);
    control::service (c); require (c.selectVersion (ids[0]), "select initial Version");
    c.setPresented (true); control::service (c);
    juce::AudioBuffer<float> input (2, 256);
    int cursor = 0;
    bool playing = true;
    int contentShift = 0;
    const auto feed = [&] (bool render)
    {
        for (int ch = 0; ch < 2; ++ch) input.copyFrom (ch, 0, base.audio, ch, cursor + contentShift, 256);
        beginReferenceRtProbe();
        c.observeTransport (cursor, true, playing); c.observeAInput (input, cursor, true, playing, true);
        const bool sounded = render && c.renderSelectedB (input, cursor, true, true, true);
        require (endReferenceRtProbe() == 0, "version switch callback allocates nothing");
        if (sounded && !edited)
        {
            double error = 0.0, energy = 0.0;
            for (int ch = 0; ch < 2; ++ch) for (int frame = 0; frame < 256; ++frame)
            {
                const double a = base.audio.getSample (ch, cursor + frame);
                const double delta = input.getSample (ch, frame) - a;
                energy += a * a; error += delta * delta;
            }
            require (energy > 0.0 && error < energy * 0.25,
                     "decoded and converted V plays the current passage, not unrelated future PCM");
        }
        cursor += playing ? 256 : 0; control::service (c); c.servicePendingAudition (-22, -6, true);
        return sounded;
    };
    // 新しい Version が鳴るまで回し、V が鳴らなかった block を数える。前の V は位置が確かめられるまで鳴り続ける。
    // block ごとに同期で回すので、数は替える順番の確かめで、時間の測定ではない。
    const auto switchTo = [&] (const juce::String& id)
    {
        int quiet = 0, sounding = 0;
        for (int n = 0; n < 1200 && sounding < 3; ++n)
        {
            const bool sounded = feed (true);
            control::waitBehindWorkerFor (control::roleFor (c, {1}), {});
            const auto now = c.snapshot();
            quiet += sounded ? 0 : 1;
            sounding = sounded && now.selectedVersionId == id && now.versionReady && now.switchingSlot == 0 ? sounding + 1 : 0;
        }
        require (sounding >= 3, "the chosen Version sounds without another click");
        return quiet;
    };
    const auto verifyOutput = [&] (int number)
    {
        const int ratio = number == 3 ? 4 : 1;
        const bool shifted = number == 2 || (edited && number == 3);
        const auto file = root.getChildFile ("version-" + juce::String (number) + (number == 2 ? ".flac" : ".wav"));
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        std::vector<float> expected;
        require (reader && ref::readReferenceProbe (*reader,
            static_cast<std::int64_t> (cursor - 256 + (shifted ? 6576 : 0)) * ratio,
            256, 48000, 2, expected), "read the independently known current passage of the chosen version");
        const auto gain = std::pow (10.0, c.snapshot().versionSelection->appliedGainDb / 20.0);
        for (int frame = 0; frame < 256; ++frame) for (int ch = 0; ch < 2; ++ch)
            require (std::abs (input.getSample (ch, frame) - expected[static_cast<size_t> (frame * 2 + ch)] * gain) < 1.0e-4,
                     "the chosen format actually sounds at its known aligned position and gain");
    };
    while (cursor < 192000) feed (false);
    require (c.snapshot().versionReady && c.requestAudition (1, -22, -6), "initial V is matched and explicitly started");
    require (feed (true), "positive control: initial V is audible");
    // 初めて選ぶ Version の hash は作業スレッドの外。終わるまで前の V は鳴り続ける（止まると A の窓も消える）。
    auto& launch = control::stagedLaunchFor (control::roleFor (c, {1}), {});
    launch = std::launch::async;
    sourceHashEntered = false; sourceHashRelease = false;
    ref::revision_detail::hashReadHook.store (&pauseSourceHash);
    require (c.selectVersion (ids[1]), "choose a Version not read yet");
    const auto hashStarted = [] (std::chrono::milliseconds limit)
    {
        std::unique_lock<std::mutex> lock (sourceHashPauseMutex);
        return sourceHashPauseCondition.wait_for (lock, limit, [] { return sourceHashEntered; });
    };
    for (int n = 0; n < 400 && ! hashStarted (std::chrono::milliseconds (1)); ++n) require (feed (true), "V sounds while the hash starts");
    require (hashStarted (std::chrono::seconds (30)), "the first read of the Version hashes its file");
    for (int n = 0; n < 32; ++n) require (feed (true), "V sounds while the hash runs behind the worker");
    // hash の途中で鳴っている Version が Library から消えたら、hash を待たずに止める。
    const auto publish = [&] (const juce::Array<juce::var>& versions)
    {
        manifest.getDynamicObject()->setProperty ("versions", versions);
        manifest.getDynamicObject()->setProperty ("revision", static_cast<int> (manifest["revision"]) + 1);
        require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "publish the Version list");
    };
    auto withoutAudible = entries;
    withoutAudible.remove (0);
    publish (withoutAudible);
    feed (true);  // 5 ms で A へ戻る
    for (int n = 0; n < 8; ++n) require (! feed (true), "a withdrawn Version stops without waiting for the hash");
    publish (entries);
    ref::revision_detail::hashReadHook.store (nullptr);
    { std::lock_guard<std::mutex> lock (sourceHashPauseMutex); sourceHashRelease = true; }
    sourceHashPauseCondition.notify_all();
    require (ref::revision_detail::service().waitForIdle (std::chrono::seconds (10)), "the hash finishes");
    launch = std::launch::deferred;
    require (switchTo (ids[1]) >= 0, "the Version follows its hash without another press");
    // Change again while the previous choice is still fading/preparing. The last
    // explicit choice must own the pending start, never an older publication.
    require (c.selectVersion (ids[1]), "select a shifted FLAC while V plays");
    require (c.selectVersion (ids[2]), "select a 192 kHz version before FLAC preparation finishes");
    require (c.snapshot().audibleComparisonSlot == 1 && c.snapshot().switchingSlot == 1 && feed (true),
             "the Version that sounds keeps sounding while the choice prepares");
    const auto quietBefore = switchTo (ids[2]);
    require (quietBefore <= 8, "V keeps sounding while the replacement prepares; only the swap itself reaches A");
    require (c.snapshot().versionSelection->sourceSampleRateHz == 192000, "the latest 192 kHz choice aligns");
    verifyOutput (3);
    require (c.selectVersion (ids[1]), "return to the shifted FLAC during playback");
    require (switchTo (ids[1]) <= 8, "FLAC with a known leading silence aligns and resumes after repeated switches");
    verifyOutput (2);
    const auto alignment = control::calibrationFor (control::roleFor (c, {1}), {});
    require (alignment.bStartSample - alignment.aStartSample == 6576,
             "the independent 137 ms fixture construction determines the expected FLAC position");
    // 替えている途中で鳴っている Version へ選び直すと、音は切れず、待ちの印も残らない。
    require (c.selectVersion (ids[0]) && c.selectVersion (ids[1]), "choose another Version and come back");
    for (int n = 0; n < 8; ++n) require (feed (true), "the audible Version never stopped");
    require (c.snapshot().switchingSlot == 0 && c.snapshot().versionReady, "nothing is left waiting after coming back");
    verifyOutput (2);
    // V を押し直しても、替えている途中の V は止めない（C と同じ）。A を押せば止まり、公開されても勝手に鳴らない。
    require (c.selectVersion (ids[0]) && c.requestAudition (1, -22, -6) && feed (true), "pressing V keeps the switch");
    require (c.snapshot().switchingSlot == 1, "the switch is still under way");
    c.selectA();
    feed (true);  // 5 ms で A へ戻る
    for (int n = 0; n < 16; ++n) require (! feed (true), "A ends the audition and the switch");
    require (c.snapshot().switchingSlot == 0 && c.snapshot().selectedVersionId == ids[0], "the choice stays without sounding");
    for (int n = 0; n < 1200 && ! c.snapshot().versionReady; ++n) feed (false);
    require (c.requestAudition (1, -22, -6), "the chosen Version starts when pressed");
    require (switchTo (ids[0]) <= 8, "the pressed Version sounds");
    // 止めると保つ音は無くなり、替える先の準備は続く。再生で新しい Version が鳴る。
    require (c.selectVersion (ids[1]), "choose during playback");
    playing = false;
    for (int n = 0; n < 32; ++n) require (! feed (true), "a stopped transport plays nothing");
    playing = true; cursor = 0;  // A の窓がたまる長さを残す
    require (switchTo (ids[1]) > 0, "playback brings the new Version");
    verifyOutput (2);
    // シークの直後は A の窓がたまるまで位置を確かめられない。そのあいだ前の V を鳴らし続ける。
    cursor = 24000;
    require (c.selectVersion (ids[0]), "choose right after a seek");
    require (switchTo (ids[0]) <= 8 && cursor - 24000 >= 3 * 48000, "the old Version sounds until the new window is ready");
    require (c.selectVersion (ids[1]) && switchTo (ids[1]) <= 8, "the next choice swaps at once");
    // 位置が確かめられないまま上限を過ぎたら、今までどおり A で待って鳴らす（ここでは上限 0 で A の待ちへ進める）。
    auto& limit = control::stagedLimitFor (control::roleFor (c, {1}), {});
    const auto ordinary = limit;
    limit = 0;
    require (c.selectVersion (ids[0]), "choose with no time to keep V");
    require (switchTo (ids[0]) >= 0, "the A wait still brings the new Version");
    limit = ordinary;
    // 本番と同じく別の thread で探す。探している途中で選び直しても、取り消した探索を待たずに最後の選択へ進む。
    launch = std::launch::async;
    const auto periodic = [&] { return control::positionSearchFor (control::roleFor (c, {1}), {}); };
    require (c.selectVersion (ids[2]), "choose during the search");
    for (int n = 0; n < 1200 && ! periodic().staged && c.snapshot().switchingSlot != 0; ++n)
    {
        require (feed (true), "V sounds until the search starts");
        control::waitSourceCheckFor (control::roleFor (c, {1}), {});
    }
    require (c.selectVersion (ids[1]), "choose again during the search");
    require (switchTo (ids[1]) <= 8, "the last choice swaps");
    require (c.selectVersion (ids[0]) && switchTo (ids[0]) <= 8, "the search also finishes on its own thread");
    // 鳴っている間の定期の確かめも裏で探す。終わっていなくても作業は進み、V は鳴り続ける。
    cursor = 24000;  // 素材の終わりまでに窓がたまる長さを残す
    int quietAfterSeek = 0;
    for (int n = 0; n < 1200 && ! periodic().searching; ++n) quietAfterSeek += feed (true) ? 0 : 1;
    require (quietAfterSeek <= 8, "the periodic search runs behind the worker");  // the map test below proves its result
    require (feed (true) && feed (true), "V keeps sounding while the worker has no result yet");
    // シークの前の窓の結果は使わない。
    cursor = 24000;
    for (int n = 0; n < 2 && periodic().window; ++n) feed (true);
    require (! periodic().window, "a seek drops the search of the earlier window");
    // 同じ位置のまま A の内容がずれたら、その写像では鳴らさず A へ戻り、曲の全体を裏で探して写像を替える。
    // 加工した Version は PCM の照合が効かないので、確かめた写像だけが頼り。
    const auto audible = ids[edited ? 2 : 0];
    launch = std::launch::deferred;
    require (c.selectVersion (audible) && switchTo (audible) <= 8, "the Version to move sounds");
    launch = std::launch::async;
    const auto offset = [&]
    {
        const auto map = control::calibrationFor (control::roleFor (c, {1}), {});
        return map.bStartSample * 48000 / map.bSampleRateHz - map.aStartSample;
    };
    const auto moveContent = [&] (int shift)
    {
        const auto expected = offset() + shift - contentShift;
        contentShift = shift; cursor = 24000;
        const auto settled = [&] { return control::calibrationFor (control::roleFor (c, {1}), {}).eligible && std::abs (offset() - expected) <= 2; };
        int withdrawnBlocks = 0, soundedBlocks = 0;
        for (int n = 0; n < 1200 && ! settled(); ++n)
        {
            const bool withdrawn = ! control::calibrationFor (control::roleFor (c, {1}), {}).eligible;
            const bool sounded = feed (true);
            withdrawnBlocks += withdrawn ? 1 : 0;
            soundedBlocks += withdrawn && sounded ? 1 : 0;
            control::waitPositionSearchFor (control::roleFor (c, {1}), {});
        }
        require (withdrawnBlocks > 0 && soundedBlocks <= 1, "V fades out at once from a map known to be wrong");
        require (settled(), "the whole song search behind the worker replaces the map");
    };
    moveContent (48000);
    moveContent (0);
    launch = std::launch::deferred;
    for (int n = 0; n < 1200 && ! c.snapshot().versionReady; ++n) feed (false);
    require (c.requestAudition (1, -22, -6) && switchTo (audible) <= 8, "V starts on the replaced map");
    require (c.selectVersion (ids[0]) && switchTo (ids[0]) <= 8, "back to the first Version");
    // 替えている途中で鳴っている Version が Library から消えたら、残さず止めて、新しい Version へ進む。
    require (c.selectVersion (ids[1]) && c.snapshot().audibleComparisonSlot == 1, "choose while the old Version sounds");
    entries.remove (0);
    manifest.getDynamicObject()->setProperty ("versions", entries);
    manifest.getDynamicObject()->setProperty ("revision", static_cast<int> (manifest["revision"]) + 1);
    require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "the audible Version leaves the library");
    require (switchTo (ids[1]) > 0, "a withdrawn Version stops at once and the new one follows");
    // 鳴っている V の替え先が Library から消えたら、替えている印を手放す（A のボタンが消えたまま残らない）。
    require (c.selectVersion (ids[2]) && c.snapshot().switchingSlot == 1, "the switch shows while the new Version prepares");
    entries.remove (1);
    manifest.getDynamicObject()->setProperty ("versions", entries);
    manifest.getDynamicObject()->setProperty ("revision", static_cast<int> (manifest["revision"]) + 1);
    require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "the Version leaves the library");
    for (int n = 0; n < 16 && c.snapshot().switchingSlot != 0; ++n) feed (true);
    require (c.snapshot().switchingSlot == 0, "a switch to a Version that left the library ends");
    c.selectA();
}
