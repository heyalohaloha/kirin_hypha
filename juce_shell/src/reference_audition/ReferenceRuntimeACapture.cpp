#include "ReferenceRuntimeACapture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <juce_cryptography/juce_cryptography.h>

#if JUCE_MAC || JUCE_LINUX
 #include <sys/stat.h>
#endif

namespace hypha::reference_audition
{
    namespace
    {
        constexpr float silenceThreshold = 1.0e-7f;
        constexpr std::int64_t captureSeconds = 4;
        constexpr std::int64_t receiptLeaseMs = 1'500;
        constexpr std::int64_t maximumReceiptAgeMs = 10'000;
        constexpr std::int64_t publishIntervalMs = 500;
        constexpr std::int64_t maximumSafeInteger = 9'007'199'254'740'991;

        bool safeRuntimeId (const juce::String& value)
        {
            if (value.isEmpty() || value.length() > 160)
                return false;
            for (auto character : value)
                if (! ((character >= 'a' && character <= 'z')
                       || (character >= 'A' && character <= 'Z')
                       || (character >= '0' && character <= '9')
                       || character == '.' || character == '_'
                       || character == ':' || character == '-'))
                    return false;
            return true;
        }

        bool replaceJsonAtomically (const juce::File& target, const juce::var& value)
        {
            const auto parent = target.getParentDirectory();
            if (target == juce::File() || ! parent.createDirectory()
                || parent.isSymbolicLink() || target.isSymbolicLink())
                return false;
            const auto contents = juce::JSON::toString (value, true) + "\n";
            if (contents.getNumBytesAsUTF8() > 8'192)
                return false;
            juce::TemporaryFile temporary (target);
            {
                auto stream = temporary.getFile().createOutputStream();
                if (stream == nullptr || ! stream->openedOk()
                    || ! stream->writeText (contents, false, false, "\n"))
                    return false;
                stream->flush();
                if (stream->getStatus().failed())
                    return false;
            }
            if (! temporary.overwriteTargetFileWithTemporary())
                return false;
           #if JUCE_MAC || JUCE_LINUX
            if (::chmod (target.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR) != 0)
            {
                target.deleteFile();
                return false;
            }
           #endif
            return true;
        }

        bool activeSample (float value)
        {
            return std::isfinite (value) && std::abs (value) >= silenceThreshold;
        }

        bool finiteSample (float value)
        {
            return std::isfinite (value);
        }
    }

    RuntimeACapture::RuntimeACapture (juce::File transportRootIn)
        : root (std::move (transportRootIn)),
          queueSamples (static_cast<size_t> (queueSlotCount * maximumBlockFrames * 2), 0.0f),
          capturedSamples (static_cast<size_t> (maximumCaptureFrames * 2), 0.0f)
    {
    }

    RuntimeACapture::~RuntimeACapture()
    {
        disconnect();
    }

    juce::File RuntimeACapture::captureFile (const juce::String& runtimeInstanceId) const
    {
        return safeRuntimeId (runtimeInstanceId)
            ? root.getChildFile ("a_captures").getChildFile (runtimeInstanceId + ".json")
            : juce::File {};
    }

    void RuntimeACapture::markDiscontinuity() noexcept
    {
        continuityGeneration.fetch_add (1, std::memory_order_release);
    }

    void RuntimeACapture::observe (const juce::AudioBuffer<float>& input,
                                   std::int64_t startSample,
                                   bool positionValid,
                                   bool playing,
                                   bool aSelected) noexcept
    {
        const auto frames = input.getNumSamples();
        const auto channels = input.getNumChannels();
        const bool active = positionValid && playing && aSelected && startSample >= 0
                         && frames > 0 && frames <= maximumBlockFrames
                         && (channels == 1 || channels == 2)
                         && startSample <= maximumSafeInteger - frames;
        if (! active)
        {
            if (producerActive)
                markDiscontinuity();
            producerActive = false;
            return;
        }

        const auto currentWrite = writeSlot.load (std::memory_order_relaxed);
        const auto nextWrite = (currentWrite + 1u) % queueSlotCount;
        if (nextWrite == readSlot.load (std::memory_order_acquire))
        {
            markDiscontinuity();
            producerActive = false;
            return;
        }
        if (producerActive)
        {
            const auto previousSlot = (currentWrite + queueSlotCount - 1u) % queueSlotCount;
            const auto& previous = blocks[previousSlot];
            if (previous.frames > 0
                && previous.startSample + previous.frames != startSample)
                markDiscontinuity();
        }
        producerActive = true;
        auto& block = blocks[currentWrite];
        block.startSample = startSample;
        block.frames = frames;
        block.channels = channels;
        block.continuityGeneration = continuityGeneration.load (std::memory_order_acquire);
        auto* destination = queueSamples.data()
            + static_cast<size_t> (currentWrite * maximumBlockFrames * 2);
        for (int frame = 0; frame < frames; ++frame)
            for (int channel = 0; channel < channels; ++channel)
                destination[static_cast<size_t> (frame * channels + channel)]
                    = input.getReadPointer (channel)[frame];
        writeSlot.store (nextWrite, std::memory_order_release);
    }

    void RuntimeACapture::discardQueued() noexcept
    {
        readSlot.store (writeSlot.load (std::memory_order_acquire),
                        std::memory_order_release);
    }

    void RuntimeACapture::resetAccumulator()
    {
        collecting = false;
        complete = false;
        captureStartSample = 0;
        capturedFrames = 0;
        lastObservedEndSample = 0;
        receipt.reset();
        std::atomic_store_explicit (
            &publishedAudio, std::shared_ptr<const RuntimeACaptureAudio> {},
            std::memory_order_release);
        nextPublishAtMs = 0;
    }

    void RuntimeACapture::removeReceiptFile()
    {
        const auto file = captureFile (activeRuntimeInstanceId);
        if (file.existsAsFile() && ! file.isSymbolicLink())
            file.deleteFile();
    }

    void RuntimeACapture::disconnect()
    {
        removeReceiptFile();
        resetAccumulator();
        discardQueued();
        activeBindingId.clear();
        activeRuntimeInstanceId.clear();
        activeSampleRateHz = 0;
        activeChannels = 0;
        previousHash.clear();
        previousRevisionId.clear();
    }

    void RuntimeACapture::consumeBlock (const Block& block, const float* samples,
                                        std::int64_t targetFrames, std::int64_t nowMs)
    {
        juce::ignoreUnused (nowMs);
        if (block.continuityGeneration != consumedGeneration
            || block.channels != activeChannels)
            return;
        int offset = 0;
        if (! collecting && ! complete)
        {
            while (offset < block.frames)
            {
                bool content = false;
                for (int channel = 0; channel < activeChannels; ++channel)
                    content = content || activeSample (
                        samples[static_cast<size_t> (offset * activeChannels + channel)]);
                if (content)
                    break;
                ++offset;
            }
            if (offset == block.frames)
                return;
            collecting = true;
            captureStartSample = block.startSample + offset;
            lastObservedEndSample = captureStartSample;
        }

        if (complete)
        {
            if (block.startSample != lastObservedEndSample)
                resetAccumulator();
            else
                lastObservedEndSample = block.startSample + block.frames;
            return;
        }
        if (! collecting || block.startSample + offset != lastObservedEndSample)
        {
            resetAccumulator();
            return;
        }

        const auto available = static_cast<std::int64_t> (block.frames - offset);
        const auto framesToCopy = static_cast<int> (
            std::min (available, targetFrames - capturedFrames));
        for (int frame = 0; frame < framesToCopy; ++frame)
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto value = samples[static_cast<size_t> (
                    (offset + frame) * activeChannels + channel)];
                if (! finiteSample (value))
                {
                    resetAccumulator();
                    return;
                }
                capturedSamples[static_cast<size_t> (
                    (capturedFrames + frame) * activeChannels + channel)] = value;
            }
        }
        capturedFrames += framesToCopy;
        lastObservedEndSample = block.startSample + offset + framesToCopy;
        if (capturedFrames == targetFrames)
        {
            finishCapture (nowMs);
            lastObservedEndSample = block.startSample + block.frames;
        }
    }

    void RuntimeACapture::finishCapture (std::int64_t nowMs)
    {
        const auto sampleCount = capturedFrames * activeChannels;
        if (sampleCount < 1 || sampleCount > maximumCaptureFrames * 2)
        {
            resetAccumulator();
            return;
        }
        juce::MemoryBlock canonical (static_cast<size_t> (sampleCount * 4), true);
        auto* output = static_cast<std::uint8_t*> (canonical.getData());
        for (std::int64_t index = 0; index < sampleCount; ++index)
        {
            const float normalized = capturedSamples[static_cast<size_t> (index)] == 0.0f
                ? 0.0f : capturedSamples[static_cast<size_t> (index)];
            std::uint32_t bits = 0;
            std::memcpy (&bits, &normalized, sizeof (bits));
            output[index * 4] = static_cast<std::uint8_t> (bits & 0xffu);
            output[index * 4 + 1] = static_cast<std::uint8_t> ((bits >> 8u) & 0xffu);
            output[index * 4 + 2] = static_cast<std::uint8_t> ((bits >> 16u) & 0xffu);
            output[index * 4 + 3] = static_cast<std::uint8_t> ((bits >> 24u) & 0xffu);
        }
        const auto hash = juce::SHA256 (canonical).toHexString();
        if (hash != previousHash || previousRevisionId.isEmpty())
        {
            previousHash = hash;
            previousRevisionId = juce::Uuid().toDashedString();
        }
        RuntimeACaptureReceipt next;
        next.bindingId = activeBindingId;
        next.runtimeInstanceId = activeRuntimeInstanceId;
        next.dawRevisionId = previousRevisionId;
        next.sampleRateHz = activeSampleRateHz;
        next.channels = activeChannels;
        next.startSample = captureStartSample;
        next.frameCount = capturedFrames;
        next.cuePcmSha256 = hash;
        next.capturedAtMs = nowMs;
        receipt = std::move (next);
        auto audio = std::make_shared<RuntimeACaptureAudio>();
        audio->dawRevisionId = receipt->dawRevisionId;
        audio->cuePcmSha256 = receipt->cuePcmSha256;
        audio->sampleRateHz = activeSampleRateHz;
        audio->channels = activeChannels;
        audio->startSample = captureStartSample;
        audio->frameCount = capturedFrames;
        audio->interleaved.assign (
            capturedSamples.begin(),
            capturedSamples.begin() + static_cast<std::ptrdiff_t> (sampleCount));
        std::atomic_store_explicit (
            &publishedAudio, std::shared_ptr<const RuntimeACaptureAudio> (std::move (audio)),
            std::memory_order_release);
        collecting = false;
        complete = true;
        nextPublishAtMs = 0;
    }

    void RuntimeACapture::publishReceipt (const RuntimeABinding& binding,
                                          std::int64_t nowMs)
    {
        if (! receipt || nowMs < nextPublishAtMs
            || nowMs > receipt->capturedAtMs + maximumReceiptAgeMs)
            return;
        const auto expiresAt = std::min ({
            binding.leaseExpiresAtMs,
            nowMs + receiptLeaseMs,
            receipt->capturedAtMs + maximumReceiptAgeMs,
        });
        if (expiresAt <= nowMs || expiresAt <= receipt->capturedAtMs)
            return;
        receipt->hostProcessId = binding.hostProcessId;
        receipt->workId = binding.workId;
        receipt->recordingId = binding.recordingId;
        receipt->leaseExpiresAtMs = expiresAt;

        auto object = new juce::DynamicObject();
        object->setProperty ("format", "kirin_hypha_reference_a_capture");
        object->setProperty ("version", "1.0");
        object->setProperty ("binding_id", receipt->bindingId);
        object->setProperty ("runtime_instance_id", receipt->runtimeInstanceId);
        object->setProperty ("host_process_id",
                             static_cast<juce::int64> (receipt->hostProcessId));
        object->setProperty ("work_id", receipt->workId);
        object->setProperty ("recording_id", receipt->recordingId);
        object->setProperty ("daw_revision_id", receipt->dawRevisionId);
        object->setProperty ("sample_rate_hz", receipt->sampleRateHz);
        object->setProperty ("channels", static_cast<juce::int64> (receipt->channels));
        object->setProperty ("start_sample", receipt->startSample);
        object->setProperty ("frame_count", receipt->frameCount);
        object->setProperty ("cue_pcm_sha256", receipt->cuePcmSha256);
        object->setProperty ("captured_at_ms", receipt->capturedAtMs);
        object->setProperty ("lease_expires_at_ms", receipt->leaseExpiresAtMs);
        const juce::var value (object);
        if (replaceJsonAtomically (captureFile (binding.runtimeInstanceId), value))
            nextPublishAtMs = nowMs + publishIntervalMs;
    }

    void RuntimeACapture::service (const std::optional<RuntimeABinding>& binding,
                                   std::int64_t sampleRateHz,
                                   int channels,
                                   std::int64_t nowMs)
    {
        const bool authorityValid = binding.has_value()
            && binding->leaseExpiresAtMs >= nowMs
            && sampleRateHz >= 8'000 && sampleRateHz <= 768'000
            && (channels == 1 || channels == 2)
            && nowMs >= 0 && nowMs <= maximumSafeInteger - receiptLeaseMs;
        if (! authorityValid)
        {
            if (receipt || ! activeBindingId.isEmpty())
                removeReceiptFile();
            resetAccumulator();
            discardQueued();
            activeBindingId.clear();
            activeRuntimeInstanceId.clear();
            return;
        }
        if (activeBindingId != binding->bindingId
            || activeRuntimeInstanceId != binding->runtimeInstanceId
            || activeSampleRateHz != sampleRateHz || activeChannels != channels)
        {
            removeReceiptFile();
            resetAccumulator();
            discardQueued();
            previousHash.clear();
            previousRevisionId.clear();
            activeBindingId = binding->bindingId;
            activeRuntimeInstanceId = binding->runtimeInstanceId;
            activeSampleRateHz = sampleRateHz;
            activeChannels = channels;
            consumedGeneration = continuityGeneration.load (std::memory_order_acquire);
        }

        const auto generation = continuityGeneration.load (std::memory_order_acquire);
        if (generation != consumedGeneration)
        {
            resetAccumulator();
            discardQueued();
            consumedGeneration = generation;
        }
        const auto targetFrames = std::min (
            maximumCaptureFrames, activeSampleRateHz * captureSeconds);
        for (;;)
        {
            const auto currentRead = readSlot.load (std::memory_order_relaxed);
            if (currentRead == writeSlot.load (std::memory_order_acquire))
                break;
            const auto block = blocks[currentRead];
            const auto* samples = queueSamples.data()
                + static_cast<size_t> (currentRead * maximumBlockFrames * 2);
            consumeBlock (block, samples, targetFrames, nowMs);
            readSlot.store ((currentRead + 1u) % queueSlotCount,
                            std::memory_order_release);
        }
        if (receipt && nowMs > receipt->capturedAtMs + maximumReceiptAgeMs)
        {
            removeReceiptFile();
            resetAccumulator();
        }
        publishReceipt (*binding, nowMs);
    }
}
