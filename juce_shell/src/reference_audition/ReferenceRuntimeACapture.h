#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "ReferenceRuntimeABinding.h"

namespace hypha::reference_audition
{
    struct RuntimeACaptureReceipt
    {
        juce::String bindingId;
        juce::String runtimeInstanceId;
        std::uint32_t hostProcessId = 0;
        juce::String workId;
        juce::String recordingId;
        juce::String dawRevisionId;
        std::int64_t sampleRateHz = 0;
        int channels = 0;
        std::int64_t startSample = 0;
        std::int64_t frameCount = 0;
        juce::String cuePcmSha256;
        std::int64_t capturedAtMs = 0;
        std::int64_t leaseExpiresAtMs = 0;
    };

    struct RuntimeACaptureAudio
    {
        juce::String dawRevisionId;
        juce::String cuePcmSha256;
        std::int64_t sampleRateHz = 0;
        int channels = 0;
        std::int64_t startSample = 0;
        std::int64_t frameCount = 0;
        std::vector<float> interleaved;
    };

    class RuntimeACapture final
    {
    public:
        explicit RuntimeACapture (juce::File transportRootIn);
        ~RuntimeACapture();

        void observe (const juce::AudioBuffer<float>& input,
                      std::int64_t startSample,
                      bool positionValid,
                      bool playing,
                      bool aSelected) noexcept;
        void service (const std::optional<RuntimeABinding>& binding,
                      std::int64_t sampleRateHz,
                      int channels,
                      std::int64_t nowMs);
        void disconnect();

        const std::optional<RuntimeACaptureReceipt>& currentReceipt() const noexcept
        {
            return receipt;
        }
        std::shared_ptr<const RuntimeACaptureAudio> currentAudio() const noexcept
        {
            return std::atomic_load_explicit (&publishedAudio, std::memory_order_acquire);
        }

        juce::File captureFile (const juce::String& runtimeInstanceId) const;

    private:
        static constexpr int queueSlotCount = 32;
        static constexpr int maximumBlockFrames = 8'192;
        static constexpr std::int64_t maximumCaptureFrames = 2'097'152;

        struct Block
        {
            std::int64_t startSample = 0;
            int frames = 0;
            int channels = 0;
            std::uint64_t continuityGeneration = 0;
        };

        void markDiscontinuity() noexcept;
        void resetAccumulator();
        void discardQueued() noexcept;
        void consumeBlock (const Block&, const float* samples,
                           std::int64_t targetFrames, std::int64_t nowMs);
        void finishCapture (std::int64_t nowMs);
        void publishReceipt (const RuntimeABinding&, std::int64_t nowMs);
        void removeReceiptFile();

        const juce::File root;
        std::array<Block, queueSlotCount> blocks;
        std::vector<float> queueSamples;
        std::vector<float> capturedSamples;
        std::atomic<unsigned int> writeSlot { 0 };
        std::atomic<unsigned int> readSlot { 0 };
        std::atomic<std::uint64_t> continuityGeneration { 1 };
        bool producerActive = false;
        juce::String activeBindingId;
        juce::String activeRuntimeInstanceId;
        std::uint64_t consumedGeneration = 0;
        std::int64_t captureStartSample = 0;
        std::int64_t capturedFrames = 0;
        std::int64_t lastObservedEndSample = 0;
        std::int64_t activeSampleRateHz = 0;
        int activeChannels = 0;
        bool collecting = false;
        bool complete = false;
        juce::String previousHash;
        juce::String previousRevisionId;
        std::int64_t nextPublishAtMs = 0;
        std::optional<RuntimeACaptureReceipt> receipt;
        std::shared_ptr<const RuntimeACaptureAudio> publishedAudio;
    };
}
