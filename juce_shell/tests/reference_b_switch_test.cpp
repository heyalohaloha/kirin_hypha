#include "ReferenceSongSwitchRig.h"
#include "ReferenceSwitchBenchmark.h"
#include <array>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

namespace hypha::reference_audition
{
namespace
{
std::mutex verificationLock;
std::condition_variable verificationChanged;
bool holdVerification = false, verificationEntered = false, holdCopy = false;
void waitWhileCopyHeld()
{
    std::unique_lock<std::mutex> lock (verificationLock);
    verificationEntered = true;
    verificationChanged.notify_all();
    verificationChanged.wait (lock, [] { return ! holdCopy; });
}
void release (bool& flag)
{
    { const std::lock_guard<std::mutex> lock (verificationLock); flag = false; }
    verificationChanged.notify_all();
}
bool awaitEntered()
{
    std::unique_lock<std::mutex> lock (verificationLock);
    return verificationChanged.wait_for (lock, std::chrono::seconds (15), [] { return verificationEntered; });
}
}
void observeReferenceSwitchVerification (const RuntimeV2Controller*, std::uint64_t, const RuntimeSource&, bool start)
{
    if (! start) return;
    std::unique_lock<std::mutex> lock (verificationLock);
    if (! holdVerification) return;
    verificationEntered = true;
    verificationChanged.notify_all();
    verificationChanged.wait (lock, [] { return ! holdVerification; });
}
void observeReferenceSwitchPages (const RuntimeV2Controller*, std::uint64_t, const RuntimeSource&, bool) {}
// The audio callback stops right after it has read the path of the song it plays.
namespace { bool holdPathRead = false, pathReadEntered = false; }
void observeReferenceNormalPathRead (const RuntimeV2Controller*)
{
    std::unique_lock<std::mutex> lock (verificationLock);
    if (! holdPathRead) return;
    pathReadEntered = true;
    verificationChanged.notify_all();
    verificationChanged.wait (lock, [] { return ! holdPathRead; });
}
}

namespace
{
namespace audition = hypha::reference_audition;
namespace revision = hypha::reference_audition::revision_detail;
using reference_switch_rig::Rig;
using reference_switch_rig::songLevel;
using reference_switch_rig::Stereo;
using Runtime = reference_switch_rig::test_control::Runtime;
struct SwitchSerialTag {};
std::atomic<std::uint64_t>& switchSerialFor (Runtime&, SwitchSerialTag);
template <auto Member> struct SwitchSerialAccess
{ friend std::atomic<std::uint64_t>& switchSerialFor (Runtime& r, SwitchSerialTag) { return r.*Member; } };
template struct SwitchSerialAccess<&Runtime::normalSwitchSerial>;

void hold (bool& flag)
{
    const std::lock_guard<std::mutex> lock (audition::verificationLock);
    flag = true; audition::verificationEntered = false;
}

void requireDirectTransition (Rig& rig, int from, int to, const char* message)
{
    const auto facts = rig.transition (from, to, message);
    require (facts.outsideFrames == 0, "every sample of a song switch belongs to the old or new song at its own MATCH");
    require (facts.mixedFrames <= 241, "a song switch crosses within the short fade");
    rig.drive ([&] { return rig.controller.snapshot().audibleSongId == rig.id (to); }, "PLAYING follows the actual output");
}

// 旧曲は新しい曲の確かめのあいだも全 sample が鳴り続け、受領の失敗でも止まらず、直れば自動で完了する。
void verifyHeldOldSong (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("b-switch");
    reference_switch_fixture::createLibrary (root);
    Rig rig (root, "b-switch-fixture");
    rig.start();
    const auto sets = juce::JSON::parse (root.getChildFile ("library/sets.json"));
    const auto receipt = sets["song_sets"][0]["songs"][1]["source_artifact"]["sha256"].toString();
    const auto metadata = root.getChildFile ("sources").getChildFile (receipt + ".json");
    const auto aside = metadata.getSiblingFile ("receipt-aside");
    require (metadata.moveFileTo (aside), "the fixture takes the next source receipt away");
    hold (audition::holdVerification);
    require (rig.press (1), "switch fixture selects another source");
    auto state = rig.controller.snapshot();
    require (state.selectedSongId == rig.id (1) && state.audibleSongId == rig.id (0),
             "the pressed row is selected at once and the sounding song keeps PLAYING");
    rig.drive ([&] {
        require (rig.blockIs (songLevel (0)) || rig.position == 0, "the old song stays whole through a receipt failure");
        return rig.bRuntime().snapshot().pendingSourceFailure.isNotEmpty();
    }, "invalid next source receipt is reported while the previous source stays selected");
    require (aside.moveFileTo (metadata), "the fixture restores the source receipt");
    require (audition::awaitEntered(), "new source verification reaches the test barrier");
    for (int n = 0; n < 4; ++n)
    {
        rig.host();
        require (rig.blockIs (songLevel (0)), "every sample keeps the original B while the next source is checked");
    }
    audition::release (audition::holdVerification);
    requireDirectTransition (rig, 0, 1, "the switch fixture starts the selected source");
    require (rig.press (2), "switch fixture selects the third source");
    requireDirectTransition (rig, 1, 2, "the switch fixture starts the third source");
    const auto copies = revision::snapshotCopiesForTest.load();
    const auto reads = revision::snapshotReadBytesForTest.load();
    require (rig.press (0), "switch fixture revisits the first source");
    requireDirectTransition (rig, 2, 0, "the switch fixture revisits the first source");
    require (revision::snapshotCopiesForTest.load() == copies && revision::snapshotReadBytesForTest.load() == reads,
             "revisiting B neither reads the whole file nor copies it again");
}

// B2 の確かめが終わらないうちに B3 を押すと、B3 が鳴る。遅れて終わった B2 は何も変えない。
void verifyLateCompletionLoses (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("b-switch-late");
    reference_switch_fixture::createLibrary (root);
    Rig rig (root, "b-switch-late");
    rig.start();
    hold (audition::holdCopy);
    revision::snapshotCopyHookForTest.store (&audition::waitWhileCopyHeld);
    require (rig.press (1), "the second song is pressed");
    rig.drive ([&] {
        const std::lock_guard<std::mutex> lock (audition::verificationLock);
        return audition::verificationEntered;
    }, "the second song's verification is held");
    require (rig.press (2), "the third song is pressed before the second finishes");
    requireDirectTransition (rig, 0, 2, "the last press plays while the earlier check is held");
    audition::release (audition::holdCopy);
    for (int n = 0; n < 40; ++n)
    {
        rig.host();
        require (rig.blockIs (songLevel (2)), "a late completion of an older press never wins");
    }
}

// 止められない確かめが 2 本走っているあいだ、次の押下は積まずに待ち、空いたら自動で完了する。
void verifyStuckChecksDoNotPileUp (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("b-switch-stuck");
    reference_switch_fixture::createLibrary (root, false, false, true);
    Rig rig (root, "b-switch-stuck");
    rig.start();
    const auto holdNextCheck = [&] (int song)
    {
        hold (audition::holdCopy);
        revision::snapshotCopyHookForTest.store (&audition::waitWhileCopyHeld);
        require (rig.press (song), "a song is pressed");
        rig.drive ([&] {
            const std::lock_guard<std::mutex> lock (audition::verificationLock);
            return audition::verificationEntered;
        }, "the song's check is held");
    };
    holdNextCheck (1);
    holdNextCheck (2);
    const auto copies = revision::snapshotCopiesForTest.load();
    require (rig.press (3), "a third unverified song is pressed");
    const auto wanted = rig.bRuntime().requestedGeneration();
    for (int n = 0; n < 30; ++n)
    {
        rig.host();
        require (rig.blockIs (songLevel (0)) && rig.bRuntime().snapshot().selectionGeneration < wanted
                     && revision::snapshotCopiesForTest.load() == copies,
                 "a third press waits for a free check without losing the audible song");
    }
    audition::release (audition::holdCopy);
    requireDirectTransition (rig, 0, 3, "the last press completes by itself when a check frees");
    require (rig.press (0), "the first song can be pressed while the stuck checks are gone");
    requireDirectTransition (rig, 3, 0, "the verified first song returns");
}

// 準備の途中で DAW が止まったら A。再生に戻ると、押した曲が新しい MATCH で鳴る。
void verifyStopDuringPreparation (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("b-switch-stop");
    reference_switch_fixture::createLibrary (root);
    Rig rig (root, "b-switch-stop");
    rig.start();
    hold (audition::holdVerification);
    require (rig.press (1), "the second song is pressed");
    require (audition::awaitEntered(), "the second song's check waits");
    rig.playing = false;
    rig.drive ([&] { return rig.blockIs ({}); }, "stopping returns the output to A");
    audition::release (audition::holdVerification);
    rig.drive ([&] { return rig.controller.snapshot().referenceReady || rig.bRuntime().snapshot().state == hypha::reference_audition::RuntimeState::ready; },
               "the chosen song finishes preparing while stopped");
    rig.playing = true;
    rig.drive ([&] { return rig.blockIs (songLevel (1)); }, "the chosen song plays at its own MATCH after the stop");
}

// 新しい MATCH を作れない曲は渡さない。旧曲を保ち、理由を言い、別の曲を押せば替わる。
void verifyMissingLevelKeepsOldSong (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("b-switch-level");
    reference_switch_fixture::createLibrary (root, false, true);
    Rig rig (root, "b-switch-level");
    rig.start();
    require (rig.press (2), "a song without level facts can be pressed");
    rig.drive ([&] {
        require (rig.blockIs (songLevel (0)), "a song without a new MATCH never reaches the output");
        return rig.bRuntime().snapshot().pendingSourceFailure == "reference_source_level_unavailable";
    }, "the missing level is reported while the old song plays");
    for (int n = 0; n < 20; ++n)
    {
        rig.host();
        require (rig.blockIs (songLevel (0)), "the old song keeps playing");
    }
    require (rig.press (1), "another song can be pressed");
    requireDirectTransition (rig, 0, 1, "the next press completes after a refused MATCH");
}

// B の一覧を見ているだけで確かめ終わり、押しても全体の hash もコピーも起きない。
void verifyPreloadedPress (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("b-switch-preload");
    reference_switch_fixture::createLibrary (root);
    Rig rig (root, "b-switch-preload");
    rig.start();
    rig.controller.selectVisualSlot (3);
    rig.drive ([&] { return rig.controller.snapshot().preloadedSongs.size() == 3; }, "the listed songs are verified before a press");
    const auto copies = revision::snapshotCopiesForTest.load();
    const auto reads = revision::snapshotReadBytesForTest.load();
    require (rig.press (1), "a verified song is pressed");
    requireDirectTransition (rig, 0, 1, "a verified song switches");
    require (revision::snapshotCopiesForTest.load() == copies && revision::snapshotReadBytesForTest.load() == reads,
             "a verified song needs no whole-file work at the press");
}

// 準備の途中で A を押したら、準備が終わっても鳴らない。選択は残り、押せば新しい MATCH で鳴る。
void verifyReturnDuringPreparation (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("b-switch-return");
    reference_switch_fixture::createLibrary (root);
    Rig rig (root, "b-switch-return");
    rig.start();
    hold (audition::holdVerification);
    require (rig.press (1), "the second song is pressed");
    require (audition::awaitEntered(), "the second song's check waits");
    rig.controller.selectA();
    rig.drive ([&] { return rig.blockIs ({}); }, "A returns during preparation");
    audition::release (audition::holdVerification);
    rig.drive ([&] {
        require (rig.block.getSample (0, 255) == 0.0f, "finishing the preparation never starts the song");
        return rig.controller.snapshot().referenceReady;
    }, "the chosen song finishes preparing");
    require (rig.controller.snapshot().selectedSongId == rig.id (1), "the song stays chosen");
    require (rig.controller.requestAudition (3, -14.0, -2.0), "the chosen song can be pressed");
    rig.drive ([&] { return rig.blockIs (songLevel (1)); }, "the chosen song plays at its own MATCH");
}
}

namespace
{
// A sample is A (0), one song at its own MATCH, or on the short crossfade between two of them.
bool heardAsOneSongOrBlend (double left, double right)
{
    const std::array<Stereo, 4> levels { Stereo {}, songLevel (0), songLevel (1), songLevel (2) };
    for (size_t i = 0; i < levels.size(); ++i)
        for (size_t j = i; j < levels.size(); ++j)
        {
            const auto a = levels[i], b = levels[j];
            const bool flat = std::abs (b.left - a.left) < 1.0e-9;
            const auto x = flat ? (std::abs (b.right - a.right) < 1.0e-9 ? 0.0 : (right - a.right) / (b.right - a.right))
                                : (left - a.left) / (b.left - a.left);
            if (x >= -1.0e-3 && x <= 1.0 + 1.0e-3
                && std::abs (left - (a.left + (b.left - a.left) * x)) < 1.0e-3
                && std::abs (right - (a.right + (b.right - a.right) * x)) < 1.0e-3)
                return true;
        }
    return false;
}

// 音声 callback が曲の道を読んだ直後に止まり、その間に切替が 2 回進んで前の曲の面が次の曲に使われても、
// 再開した callback は次の曲を前の曲の gain で鳴らさない。
void verifyStalledCallbackNeverMixesSongs (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("b-switch-stall");
    reference_switch_fixture::createLibrary (root);
    Rig rig (root, "b-switch-stall");
    rig.start();
    auto& serial = switchSerialFor (rig.bRuntime(), SwitchSerialTag {});
    const auto first = serial.load (std::memory_order_acquire);
    {
        const std::lock_guard<std::mutex> lock (audition::verificationLock);
        audition::holdPathRead = true; audition::pathReadEntered = false;
    }
    std::thread callback ([&] {
        rig.block.clear();
        rig.controller.observeTransport (rig.position, true, true);
        rig.controller.observeAInput (rig.block, rig.position, true, true, true);
        rig.controller.renderSelectedB (rig.block, rig.position, true, true, true);
    });
    const auto check = [&] (bool ok, const char* message)
    {
        if (! ok) { audition::release (audition::holdPathRead); callback.join(); }
        require (ok, message);
    };
    {
        std::unique_lock<std::mutex> lock (audition::verificationLock);
        const bool entered = audition::verificationChanged.wait_for (lock, std::chrono::seconds (15),
                                                                     [] { return audition::pathReadEntered; });
        lock.unlock();
        check (entered, "the audio callback stops after reading the playing song's path");
    }
    // The message thread keeps running while the callback is stopped.
    const auto publishedWhileStopped = [&] (std::uint64_t target, const char* message)
    {
        const auto deadline = reference_switch_rig::Clock::now() + std::chrono::seconds (30);
        auto service = reference_switch_rig::Clock::now();
        while (serial.load (std::memory_order_acquire) < target)
        {
            check (reference_switch_rig::Clock::now() < deadline, message);
            if (reference_switch_rig::Clock::now() >= service)
            {
                service += std::chrono::milliseconds (50);
                if (rig.controller.pendingAuditionNeedsService()) rig.controller.servicePendingAudition (-14.0, -2.0, true);
            }
            std::this_thread::yield();
        }
    };
    check (rig.press (1), "the second song is pressed while the callback is stopped");
    publishedWhileStopped (first + 1, "the second song is published while the callback is stopped");
    check (rig.press (2), "the third song is pressed while the callback is stopped");
    publishedWhileStopped (first + 2, "the third song takes the retired bank while the callback is stopped");
    audition::release (audition::holdPathRead);
    callback.join();
    for (int index = 0; index < rig.block.getNumSamples(); ++index)
        require (heardAsOneSongOrBlend (rig.block.getSample (0, index), rig.block.getSample (1, index)),
                 "a resumed callback never plays the next song at the previous song's gain");
    rig.drive ([&] { return rig.blockIs (songLevel (2)); }, "the third song plays at its own MATCH after the stall");
}
}

void runReferenceBSwitchTests (const juce::File& sandbox)
{
    const auto phase = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_SWITCH_BENCHMARK_PHASE", {});
    if (phase.isNotEmpty()) { reference_switch_benchmark::run (sandbox, phase); return; }
    verifyHeldOldSong (sandbox);
    verifyLateCompletionLoses (sandbox);
    verifyStuckChecksDoNotPileUp (sandbox);
    verifyStopDuringPreparation (sandbox);
    verifyMissingLevelKeepsOldSong (sandbox);
    verifyPreloadedPress (sandbox);
    verifyReturnDuringPreparation (sandbox);
    verifyStalledCallbackNeverMixesSongs (sandbox);
    std::cout << "Reference B switch fixture: PASS\n";
}
