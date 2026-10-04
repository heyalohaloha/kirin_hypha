#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "ReferenceVisualTimeline.h"
#include "ReferenceDynamicsRange.h"
#include "ReferenceAnalysis.h"
#include <array>
#include <functional>
namespace hypha::reference_audition
{
class VisualObservation final : private juce::Thread
{
public:
    using Binding = std::function<VisualBinding()>;
    explicit VisualObservation (Binding, std::shared_ptr<ReferenceAnalysis> = std::make_shared<ReferenceAnalysis>());
    ~VisualObservation() override;
    void configure (double sampleRate, int channels);
    void setPresented (bool);
    static constexpr size_t inputQueueBytes() { return sizeof(Block)*queueSize; }
    bool pendingInput() const noexcept { return readIndex.load()!=writeIndex.load(); }
    // Non-RT admission transfer. Neither method waits for source reads.
    void pauseAdmission();
    void resumeObservation();
    std::uint64_t inputGeneration() const noexcept { return accepting.load(std::memory_order_acquire) ? generation.load(std::memory_order_acquire) : 0; }
    void enqueue(const float*, int, int, std::int64_t, std::uint64_t, std::uint64_t) noexcept;
    std::shared_ptr<const VisualTimeline> snapshot() const;
private:
    struct Block
    {
        std::array<float, 512> pcm {};
        std::int64_t position = 0;
        std::uint64_t generation = 0, discontinuity = 0;
        int frames = 0, channels = 0;
    };
    static constexpr size_t queueSize = 480;
    static_assert (sizeof (Block) * queueSize <= 1024 * 1024, "Display queue budget");
    std::unique_ptr<std::array<Block, queueSize>> queue = std::make_unique<std::array<Block, queueSize>>();
    std::atomic<size_t> writeIndex { 0 }, readIndex { 0 };
    std::atomic<bool> accepting { false };
    std::atomic<std::uint64_t> generation { 1 };
    std::uint64_t rtDiscontinuity = 0;
    mutable juce::CriticalSection controlLock, snapshotLock;
    bool presented = false, paused = false;
    int configuredRate = 0, configuredChannels = 0;  // controlLock
    int runRate = 0, runChannels = 0;  // 観測スレッドだけ：周期の頭で controlLock の中から写した値
    std::shared_ptr<ReferenceAnalysis> analysis;
    ReferenceAnalysis::Lease admission;
    Binding binding;
    VisualTimeline timeline;
    std::shared_ptr<const VisualTimeline> published;
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::AudioFormatManager formats;
    juce::AudioBuffer<float> bAudio, scratch;
    std::array<float, 512> bPcm {};
    KirinReferenceVisualMeter* aMeter = nullptr;
    KirinReferenceVisualMeter* bMeter = nullptr;
    KirinSpectrumMeter kirinMeter;        // A を Kirin OS の Cue と同じ定義で
    KirinSpectrumMeter pairAMeter, pairVMeter; // 位置合わせで対応した A と V（同じフレーム）
    KirinFingerprintMeter printMeter;     // A の Kirin 指紋（直近 30 秒）
    DynamicsTicks aTickMeter;              // 範囲の帯：A の 100 ms の bin（Kirin OS の区間の値と同じ定義）
    DynamicsTicks pairATicks { 300 }, pairVTicks { 300 }; // V の画面：同じ区間の A と V（直近 30 秒）
    std::int64_t printEndSample = -1;
    std::int64_t pairKirinExpected = -1;
    std::int64_t kirinExpected = -1;
    std::uint64_t kirinDiscontinuity = 0;
    std::int64_t expected = -1;
    std::uint64_t previousDiscontinuity = 0;
    bool completeBin = false, measuring = false, dirty = true;
    void run() override;
    void clearMeters();
    bool resetMeters();
    void consumePair (const Block&);
    void consumeKirin (const Block&, int rate);
    void publish();
};
}
