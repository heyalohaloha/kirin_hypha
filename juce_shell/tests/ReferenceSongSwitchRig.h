#pragma once
#include "ReferenceSongSwitchFixture.h"
#include "ReferenceControllerTestAccess.h"
#include "reference_rt_probe.h"
#include <chrono>
#include <cmath>
#include <functional>
#include <thread>

// B の曲の切替の試験の台。音声 block を実時間の速さで回し（速く回すとページの読み込みが追いつかない）、全 sample を見る。
namespace reference_switch_rig
{
namespace test_control = hypha::tests::reference_control;
using Clock = std::chrono::steady_clock;
struct PairedTag {};
std::atomic<bool>& pairedSwitchFor (test_control::Runtime&, PairedTag);
template <auto Member> struct PairedAccess
{ friend std::atomic<bool>& pairedSwitchFor (test_control::Runtime& r, PairedTag) { return r.*Member; } };
template struct PairedAccess<&test_control::Runtime::pairedSongSwitch>;
struct PrepareFirstTag {};
std::atomic<bool>& prepareFirstFor (test_control::Runtime&, PrepareFirstTag);
template <auto Member> struct PrepareFirstAccess
{ friend std::atomic<bool>& prepareFirstFor (test_control::Runtime& r, PrepareFirstTag) { return r.*Member; } };
template struct PrepareFirstAccess<&test_control::Runtime::prepareBeforeReturn>;

struct Stereo { double left = 0.0, right = 0.0; };
inline Stereo songLevel (int index)
{
    if (index == 0) { const auto level = 0.1 * std::pow (10.0, 4.0 / 20.0); return { level, level }; }
    if (index == 1) { const auto level = 0.2 * std::pow (10.0, 2.0 / 20.0); return { level, -level }; }
    if (index == 3) return { 0.4, 0.2 };
    return { 0.3, -0.1 };
}

struct TransitionFacts
{
    int mixedFrames = 0, outsideFrames = 0, blocks = 0;
    bool otherSongHeard = false;
};

class Rig
{
public:
    Rig (const juce::File& root, const char* instance) : controller (root, [] (bool) { return true; })
    {
        controller.configure ({ instance, {}, 42, true }, 48'000, 2);
        controller.setPresented (true);
        nextBlock = Clock::now();
    }
    test_control::Runtime& bRuntime() { return test_control::roleFor (controller, { 3 }); }
    // 1 block。A は 0。待っている役の処理はメッセージスレッドの 50 ms の周期と同じ間隔で回す。
    void host()
    {
        nextBlock += std::chrono::nanoseconds (5'333'333);
        while (Clock::now() < nextBlock) std::this_thread::yield();
        if (Clock::now() - nextBlock > std::chrono::milliseconds (40)) nextBlock = Clock::now();
        block.clear();
        beginReferenceRtProbe();
        controller.observeTransport (position, true, playing);
        controller.observeAInput (block, position, true, playing, true);
        controller.renderSelectedB (block, position, true, true, true);
        require (endReferenceRtProbe() == 0, "switch fixture callback performs no heap operations");
        if (playing) position += block.getNumSamples();
        if (++blocksSinceService >= 10 && controller.pendingAuditionNeedsService())
        { blocksSinceService = 0; controller.servicePendingAudition (-14.0, -2.0, true); }
    }
    bool sample (int index, Stereo level) const
    { return std::abs (block.getSample (0, index) - level.left) < 1.0e-4 && std::abs (block.getSample (1, index) - level.right) < 1.0e-4; }
    bool blockIs (Stereo level) const
    {
        for (int index = 0; index < block.getNumSamples(); ++index) if (! sample (index, level)) return false;
        return true;
    }
    void drive (const std::function<bool()>& ready, const char* message)
    {
        const auto deadline = Clock::now() + std::chrono::minutes (2);
        while (! ready()) { require (Clock::now() < deadline, message); host(); }
    }
    void start (int song = 0)
    {
        drive ([&] { return controller.snapshot().referenceReady; }, "the B switch fixture prepares its first song");
        ids.clear();
        const auto state = controller.snapshot();
        for (const auto& item : state.songSets[0].songs) ids.push_back (item.id);
        require (ids.size() >= 3, "the B switch fixture exposes its songs");
        if (song != 0) { require (controller.selectSong (ids[static_cast<size_t> (song)]), "fixture chooses a first song");
                         drive ([&] { return controller.snapshot().referenceReady; }, "the fixture prepares its first song"); }
        require (controller.requestAudition (3, -14.0, -2.0), "switch fixture selects B");
        drive ([&] { return blockIs (songLevel (song)); }, "switch fixture identifies the first source after MATCH");
    }
    bool press (int song) { return controller.selectSong (ids[static_cast<size_t> (song)], -14.0, -2.0); }
    juce::String id (int song) const { return ids[static_cast<size_t> (song)]; }
    // 切替の全 sample が、前の曲から新しい曲への直線の上にあるか。A（0）も第三の音もここから外れる。
    TransitionFacts transition (int from, int to, const char* message)
    {
        TransitionFacts facts;
        const auto previous = songLevel (from), next = songLevel (to);
        drive ([&] {
            ++facts.blocks;
            for (int index = 0; index < block.getNumSamples(); ++index)
            {
                const auto left = block.getSample (0, index), right = block.getSample (1, index);
                const auto x = (left - previous.left) / (next.left - previous.left);
                if (x < -1.0e-3 || x > 1.0 + 1.0e-3
                    || std::abs (right - (previous.right + (next.right - previous.right) * x)) > 1.0e-3) ++facts.outsideFrames;
                else if (x > 1.0e-3 && x < 1.0 - 1.0e-3) ++facts.mixedFrames;
            }
            return blockIs (next);
        }, message);
        return facts;
    }
    ref::ReferenceComparisonController controller;
    juce::AudioBuffer<float> block { 2, 256 };
    std::int64_t position = 0;
    bool playing = true;
    std::vector<juce::String> ids;

private:
    Clock::time_point nextBlock;
    int blocksSinceService = 0;
};
}
