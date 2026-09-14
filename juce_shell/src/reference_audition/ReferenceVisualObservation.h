#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "ReferenceVisualTimeline.h"
#include "ReferenceAnalysis.h"
#include <array>
#include <functional>
namespace hypha::reference_audition
{
class VisualObservation final : private juce::Thread
{
public:
    using Binding = std::function<VisualBinding()>;
    explicit VisualObservation (Binding, std::shared_ptr<ReferenceAnalysis> = std::make_shared<ReferenceAnalysis>(),
                                juce::File runtimeRoot = {});
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
    int configuredRate = 0, configuredChannels = 0;
    std::shared_ptr<ReferenceAnalysis> analysis;
    ReferenceAnalysis::Lease admission;
    ReferenceTonalRepository tonalRepository;
    Binding binding;
    VisualTimeline timeline;
    std::shared_ptr<const VisualTimeline> published;
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::AudioFormatManager formats;
    juce::AudioBuffer<float> bAudio, scratch;
    std::array<float, 512> bPcm {};
    KirinReferenceVisualMeter* aMeter = nullptr;
    KirinReferenceVisualMeter* bMeter = nullptr;
    KirinReferenceTonalMeter* tonalMeter = nullptr;
    std::int64_t expected = -1;
    std::int64_t tonalExpected = -1;
    std::uint64_t previousDiscontinuity = 0;
    std::uint64_t tonalDiscontinuity = 0;
    bool completeBin = false, measuring = false, dirty = true;
    void run() override;
    void clearMeters();
    void clearTonal();
    bool resetMeters();
    bool resetTonal();
    void consumePair (const Block&);
    void consumeTonal (const Block&);
    void publish();
};
}
