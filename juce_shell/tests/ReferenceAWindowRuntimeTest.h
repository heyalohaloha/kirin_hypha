#pragma once

// B・C の図の A の窓を、controller と観測スレッドで確かめる（画面は ReferenceAWindowTest.h）。
//  - リストの回答は、開いた後に役の切替・復元・形式の変更・Blind の開始があれば何も変えない。
//  - RESET の境界は受け口の末尾をまたいでも、受け口が満ちていても、追い付く途中でもう一度押しても働く。
#include <array>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>

namespace
{
// VERSION BLIND の始まりは曲の準備が要るので、始まりの入口だけを直接通す。
struct BlindGuardTag {};
bool startBlindGuard (ref::ReferenceComparisonController&, BlindGuardTag);
template <auto Member> struct BlindGuardAccess
{ friend bool startBlindGuard (ref::ReferenceComparisonController& c, BlindGuardTag) { return (c.*Member)(); } };
template struct BlindGuardAccess<&ref::ReferenceComparisonController::beginBlindGuard>;

// 受け口の書き込み位置を試験から決めるための入口（Audio Thread の塊は enqueue で普通に入れる）。
struct RingTag {};
size_t writePosition (const ref::VisualObservation&, RingTag);
constexpr size_t ringSize (RingTag);
template <auto Write, auto Size> struct RingAccess
{
    friend size_t writePosition (const ref::VisualObservation& o, RingTag) { return (o.*Write).load(); }
    friend constexpr size_t ringSize (RingTag) { return *Size; }
};
template struct RingAccess<&ref::VisualObservation::writeIndex, &ref::VisualObservation::queueSize>;

// 別のスレッドの復元と交差した古い回答。回答の前の確かめ（trialActive）を止めている間に復元を終えるので、回答は必ず
// 復元の後に確かめることになり、復元した値を上書きしない。
void testAWindowRestoreCrossing (ref::ReferenceComparisonController& controller)
{
    namespace control = hypha::tests::reference_control;
    auto& held = control::lockFor (control::roleFor (controller, { 1 }), {});
    controller.selectVisualSlot (3);
    for (const bool reset : { false, true })
        for (int round = 0; round < 20; ++round)
        {
            const auto opened = controller.snapshot().aWindowTarget;
            auto restored = controller.savedSettings();
            restored.viewedSlot = 3;
            restored.aWindows.reference = ref::AWindowChoice::seconds30;
            std::atomic<bool> started { false }, answered { false };
            std::thread answer;
            {
                const juce::ScopedLock stop (held);
                answer = std::thread ([&] {
                    started = true;
                    answered = reset ? controller.resetAWindow (opened) : controller.selectAWindow (opened, ref::AWindowChoice::all);
                });
                while (! started) std::this_thread::yield();
                controller.restoreSettings (restored);
            }
            answer.join();
            require (! answered, "an answer crossing a restore is refused");
            require (controller.savedSettings().aWindows.reference == ref::AWindowChoice::seconds30, "a restore keeps what it restored");
        }
}

// RESET の境界が受け口の末尾から先頭へまたぐとき、受け口が満ちているとき、追い付く途中でもう一度 RESET したとき。
// 観測スレッドは binding を呼ぶ所で止まるので、周回を数えて進められる。100 ms は 240 フレームの塊 20 個。
class AWindowRing
{
    // 観測スレッドが読むので observation より先に作り、後に壊す。
    static constexpr int running = std::numeric_limits<int>::max();
    std::array<float, 480> pcm {};
    std::mutex mutex;
    std::condition_variable wake;
    int stepsLeft = running;
    bool parked = false;
    std::int64_t position = 0;
    int displayBlocks = 100;
    bool song = false;  // C の SONG（A FROM START を集計する）
public:
    ref::VisualObservation observation { [this] { return step(); } };
    static constexpr int blocksPerFrame = 20;
    AWindowRing()
    {
        observation.configure (48'000, 2);
        observation.setPresented (true);
        hypha::tests::reference_control::await ([this] { const auto state = observation.snapshot(); return state && state->observing; },
                                                "the ring test must reach observing");
        park();
    }
    ~AWindowRing() { release(); observation.setPresented (false); }
    void enqueue (int count, float amplitude = 0.0f)
    {
        enqueueSamples (count, [amplitude] (std::int64_t at, int) {
            return amplitude * static_cast<float> (std::sin (6.283185307179586 * (at % 48) / 48.0));
        });
    }
    void enqueueSamples (int count, const std::function<float(std::int64_t, int)>& sample)
    {
        for (int index = 0; index < count; ++index, position += 240)
        {
            for (int n = 0; n < 240; ++n)
                for (int channel = 0; channel < 2; ++channel)
                    pcm[static_cast<size_t> (n * 2 + channel)] = sample (position + n, channel);
            observation.enqueue (pcm.data(), 240, 2, position, observation.inputGeneration(), 1);
        }
    }
    // 止まっている観測スレッドを `iterations` 周だけ進めて、また止める。
    void advance (int iterations)
    {
        { const std::lock_guard<std::mutex> lock (mutex); stepsLeft = iterations; parked = false; }
        wake.notify_all();
        waitParked();
    }
    void park() { { const std::lock_guard<std::mutex> lock (mutex); stepsLeft = 0; } waitParked(); }
    void seek (std::int64_t next) { position = next; }  // 次に入れる塊の DAW の位置
    void setSong (bool value) { const std::lock_guard<std::mutex> lock (mutex); song = value; }
    void release() { { const std::lock_guard<std::mutex> lock (mutex); stepsLeft = running; parked = false; } wake.notify_all(); }
    // 走らせて受け口を空にし、測った物が publish されるまで待って止める。
    std::shared_ptr<const ref::VisualTimeline> drain()
    {
        // binding で止まった周の RESET 確認は済んでいる。次の周の確認まで進めてから publish を待つ。
        advance (1);
        release();
        hypha::tests::reference_control::await ([this] { return ! observation.pendingInput(); }, "the ring test must drain its queue");
        // 窓を替えると必ず publish されるので、これを待てば受け口を読み終えた後の値が見える。
        int marker = 0;
        { const std::lock_guard<std::mutex> lock (mutex); marker = ++displayBlocks; }
        hypha::tests::reference_control::await ([&] {
            const auto state = observation.snapshot();
            return state && state->binding.aDisplayWindowBlocks == marker;
        }, "the ring test must publish what it read");
        park();
        return observation.snapshot();
    }
    void moveWriteTo (size_t place)
    {
        while (writePosition (observation, {}) != place)
        {
            const auto distance = (place + ringSize ({}) - writePosition (observation, {})) % ringSize ({});
            enqueue (static_cast<int> (std::min<size_t> (distance, 100)));
            drain();
        }
    }
    std::shared_ptr<const ref::VisualTimeline> window (int count)
    {
        { const std::lock_guard<std::mutex> lock (mutex); displayBlocks = count; }
        release();
        hypha::tests::reference_control::await ([&] {
            const auto state = observation.snapshot(); return state && state->binding.aDisplayWindowBlocks == count;
        }, "the selected A window is published");
        park(); return observation.snapshot();
    }
private:
    ref::VisualBinding step()
    {
        std::unique_lock<std::mutex> lock (mutex);
        if (stepsLeft == 0)
        {
            parked = true;
            ref::notifyReferenceTestCommit();
            wake.wait (lock, [this] { return stepsLeft != 0; });
        }
        if (stepsLeft != running) --stepsLeft;
        ref::VisualBinding binding;
        binding.aDisplayWindowBlocks = displayBlocks;
        binding.songWanted = song;
        return binding;
    }
    void waitParked()
    {
        hypha::tests::reference_control::await ([this] { const std::lock_guard<std::mutex> lock (mutex); return parked; }, "the ring test must stop its worker");
    }
};

// C の SONG の A を観測スレッドで：DAW の位置ごとに溜め（同じ位置は上書き）、RESET と形式の変化（塊が来る前でも）で
// 消え、SONG を見ていなければ出さない。
void testASongRing()
{
    AWindowRing ring;
    ring.setSong (true);
    const auto holds = [&ring] (int slots, const char* why)
    {
        ring.release();
        hypha::tests::reference_control::await ([&] {
            const auto state = ring.observation.snapshot();
            return state && state->aSong && state->aSong->slots == slots;
        }, why);
        ring.park();
    };
    ring.enqueue (200, 0.1f);  // 1 秒
    holds (10, "SONG publishes A from the DAW's start");
    ring.seek (0);
    ring.enqueue (200, 0.1f);  // 同じ 1 秒をもう一度
    ring.seek (5 * 48'000);
    ring.enqueue (200, 0.1f);  // 新しい 1 秒
    holds (20, "a replayed second keeps its place, a new one adds to A from the start");
    ring.observation.resetAWindow();
    holds (0, "RESET A WINDOW empties A from the start");
    ring.enqueue (100, 0.1f);
    holds (5, "A from the start fills again after RESET");
    // 止めている間に形式が替わって戻った（48 → 96 → 48 kHz、次の塊はまだ無い）：その時に空になる。
    ring.observation.configure (96'000, 2);
    ring.observation.configure (48'000, 2);
    holds (0, "a format change empties A from the start before the next block arrives");
    ring.enqueue (100, 0.1f);
    holds (5, "A from the start fills again after the format change");
    ring.setSong (false);
    ring.release();
    hypha::tests::reference_control::await ([&] { const auto state = ring.observation.snapshot(); return state && ! state->aSong; },
                                            "CUE does not publish A from the start");
    ring.park();
}

// C の CUE／SONG は保存する選択：変わった時だけ host に知らせ、state を往復し、復元で戻る。前の版の state は CUE。
void testCheckSongNotifies (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("check-song");
    require (root.createDirectory().wasOk(), "CUE / SONG fixture directory");
    int changes = 0;
    ref::ReferenceComparisonController controller (root, {}, {}, [&] { ++changes; });
    require (! controller.savedSettings().checkSong, "CUE is the default");
    require (controller.setCheckSong (true) && changes == 1 && controller.savedSettings().checkSong
                 && controller.snapshot().visualPreferences && controller.snapshot().visualPreferences->checkSong(),
             "SONG is saved and the host is told");
    require (! controller.setCheckSong (true) && changes == 1, "SONG again is not a change");
    juce::XmlElement saved ("State");
    controller.savedSettings().write (saved);
    require (ref::ReferenceComparisonSettings::read (saved).checkSong && ! ref::ReferenceComparisonSettings::read (saved).followA,
             "SONG round trips through the project state, apart from how B and C play");
    auto restored = controller.savedSettings();
    restored.checkSong = false;
    controller.restoreSettings (restored);
    require (! controller.savedSettings().checkSong, "a restore brings back CUE");
    juce::XmlElement older ("State");
    ref::ReferenceComparisonSettings {}.write (older);
    if (auto* choices = older.getChildByName ("ReferenceChoices")) choices->setAttribute ("check_values", "song-later");
    require (! ref::ReferenceComparisonSettings::read (older).checkSong, "an unknown value reads as CUE");
}

void testAWindowResetRing()
{
    testASongRing();
    const auto queue = static_cast<int> (ringSize ({}));
    const auto drop = [] (AWindowRing& ring, int blocks, const char* why)
    {
        ring.enqueue (blocks);
        ring.observation.resetAWindow();
        const auto state = ring.drain();
        require (! state->aKirin, why);
    };
    const auto restarts = [] (AWindowRing& ring, const char* why)
    {
        ring.enqueue (AWindowRing::blocksPerFrame);
        const auto state = ring.drain();
        require (state->aKirin && state->aKirin->frames == 1, why);
    };
    {
        AWindowRing ring;
        ring.moveWriteTo (ringSize ({}) - 30);
        drop (ring, 60, "a boundary across the end of the ring keeps the old blocks out");
        require (writePosition (ring.observation, {}) == 30, "the old blocks really crossed the end of the ring");
        restarts (ring, "the first block after a wrapped boundary starts A's window");
    }
    {
        AWindowRing ring;
        drop (ring, queue - 1, "a full queue stays out of A's window");
        restarts (ring, "the block after a full queue starts A's window");
    }
    {
        AWindowRing ring;
        ring.enqueue (40);
        ring.observation.resetAWindow();
        ring.advance (4);  // 最初の RESET を受けて、古い塊を読み始めたところで止まる
        require (ring.observation.pendingInput(), "the worker stopped while catching up");
        drop (ring, 40, "a second RESET moves the boundary past the newer blocks");
        restarts (ring, "A's window starts after the second boundary");
    }
    {
        AWindowRing ring;
        ring.observation.resetAWindow();  // 空の受け口：次の塊から測る
        ring.advance (2);
        restarts (ring, "a RESET over an empty queue skips nothing");
    }
}

// B の Balance の重ね比べは他の保存する選択と同じく、変わった時だけ state が変わったと知らせる（DAW が保存を促す）。
void testOverlayNotifies (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("overlay-notifies");
    require (root.createDirectory().wasOk(), "overlay fixture directory");
    int changes = 0;
    ref::ReferenceComparisonController controller (root, {}, {}, [&] { ++changes; });
    ref::BalanceOverlay overlay;
    overlay.all = true; overlay.lines = { { "set/check/song-1", 0 } };
    require (controller.setOverlay (overlay) && changes == 1 && controller.savedSettings().overlay == overlay,
             "an overlay change is saved and the host is told");
    require (! controller.setOverlay (overlay) && changes == 1, "the same overlay again is not a change");
    overlay.drawing = ref::BalanceOverlay::Drawing::allShape;
    require (controller.setOverlay (overlay) && changes == 2 && controller.savedSettings().overlay.drawing == overlay.drawing,
             "a drawing change is a change");
}

// 開いたリストの回答は、開いた後に役の切替・復元・形式の変更・Blind の開始があれば何も変えない。RESET も同じ。
void testAWindowTargets (const juce::File& sandbox)
{
    testOverlayNotifies (sandbox);
    testCheckSongNotifies (sandbox);
    const auto root = sandbox.getChildFile ("a-window-targets");
    require (root.createDirectory().wasOk(), "A window fixture directory");
    ref::ReferenceComparisonController controller (root, [] (bool) { return true; });
    const ref::RuntimeIdentity identity { "a-window", {}, 42, true };
    controller.configure (identity, 48'000, 2);
    testAWindowRestoreCrossing (controller);
    const auto target = [&] { return controller.snapshot().aWindowTarget; };
    const auto dropped = [&] (ref::AWindowTarget opened, const char* why)
    {
        const auto before = controller.savedSettings().aWindows;
        require (! controller.selectAWindow (opened, ref::AWindowChoice::seconds3) && ! controller.resetAWindow (opened), why);
        const auto after = controller.savedSettings().aWindows;
        require (after.check == before.check && after.reference == before.reference, why);
    };
    controller.selectVisualSlot (3);
    auto opened = target();
    require (opened.slot == 3, "the target names the role on the page");
    require (controller.selectAWindow (opened, ref::AWindowChoice::seconds30) && controller.resetAWindow (opened), "a current answer is taken");
    controller.selectVisualSlot (3);
    require (target().epoch == opened.epoch, "pressing the same role keeps an open list valid");
    controller.selectVisualSlot (2);
    dropped (opened, "a role change drops the older answer");
    opened = target();
    controller.restoreSettings (controller.savedSettings());
    dropped (opened, "a restore drops the older answer");
    opened = target();
    controller.configure (identity, 44'100, 2);
    dropped (opened, "a new format drops the older answer");
    opened = target();
    require (controller.reserveLocalBlind(), "a local Blind can start here");
    dropped (opened, "a local Blind drops the older answer");
    controller.bindLocalBlind (1); controller.releaseLocalBlind (1);
    opened = target();
    require (startBlindGuard (controller, {}), "VERSION BLIND can start here");
    dropped (opened, "VERSION BLIND drops the older answer");
}
}
