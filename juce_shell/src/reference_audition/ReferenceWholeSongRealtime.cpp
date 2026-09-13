#include "ReferenceRuntimeV2Blind.h"
#include "ReferenceAudioPages.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace hypha::reference_audition
{
    bool RuntimeV2Blind::renderPausedA (juce::AudioBuffer<float>& buffer) noexcept
    {
        callbacksInFlight.fetch_add (1, std::memory_order_acq_rel);
        const auto state = lifecycle.load (std::memory_order_acquire);
        const bool paused = (state == armed || state == active || state == revealed) && wholeSong;
        if (paused)
        {
            rtSourceBlend = 0.0f;
            activeStimulus.store (0, std::memory_order_release);
            if (attenuationHoldActive.load (std::memory_order_acquire))
                buffer.applyGain (heldALinearGain.load (std::memory_order_acquire));
        }
        callbacksInFlight.fetch_sub (1, std::memory_order_release);
        return paused;
    }

    void RuntimeV2Blind::confirmStoppedReturn() noexcept
    {
        // An explicit END during a host-reported pause does not need an invented
        // audible block. It authorizes unchanged live A on the next callback.
        snapshotReadersInFlight.fetch_add (1, std::memory_order_acq_rel);
        int expected = returnRequested;
        if (lifecycle.load (std::memory_order_acquire) == returnRequested && wholeSong)
            lifecycle.compare_exchange_strong (expected, normalConfirmed, std::memory_order_acq_rel);
        snapshotReadersInFlight.fetch_sub (1, std::memory_order_release);
    }

    // Called only after the lifecycle has pinned all preparation-owned fields.
    bool RuntimeV2Blind::renderWholeSong (juce::AudioBuffer<float>& buffer,
                                         AudioPages& pages, std::int64_t position) noexcept
    {
        const auto release = [this] { callbacksInFlight.fetch_sub (1, std::memory_order_release); };
        const int count = buffer.getNumSamples();
        const auto length = pages.lengthInSamples();
        if (count < 1 || count > liveScratch.getNumSamples() || length <= 0
            || position == std::numeric_limits<std::int64_t>::min())
        { release(); return false; }
        const int first = position < 0
            ? (position <= -static_cast<std::int64_t> (count) ? count : static_cast<int> (-position)) : 0;
        const auto sourceFirst = position < 0 ? 0 : position;
        const int available = sourceFirst < length
            ? static_cast<int> (juce::jmin<std::int64_t> (count - first, length - sourceFirst)) : 0;
        const int stimulus = requestedStimulus.load (std::memory_order_acquire);
        const int side = sideForStimulus (stimulus);
        const auto aGain = static_cast<float> (std::pow (10.0, aGainDb / 20.0));
        const auto bGain = static_cast<float> (std::pow (10.0, bGainDb / 20.0));
        for (int channel = 0; channel < channels; ++channel)
            std::memcpy (liveScratch.getWritePointer (channel), buffer.getReadPointer (channel),
                         static_cast<size_t> (count) * sizeof (float));
        if (available > 0 && (side == 1 || rtSourceBlend > 0.0f))
        {
            // JUCE keeps the two channel pointers in its inline array. This view
            // neither allocates sample memory nor resizes the prepared scratch.
            juce::AudioBuffer<float> region (buffer.getArrayOfWritePointers(), channels, first, available);
            if (!pages.render (region, sourceFirst, bGain)) { release(); return false; }
        }
        if (available == 0)
        {
            rtSourceBlend = 0.0f;
            activeStimulus.store (0, std::memory_order_release);
            if (attenuationHoldActive.load (std::memory_order_acquire)) buffer.applyGain (aGain);
            release(); return true;
        }
        const auto fadeFrames = juce::jmax (1, sampleRateHz / 200);
        const auto step = 1.0f / static_cast<float> (fadeFrames);
        std::uint64_t confirmedFrames = 0;
        for (int frame = 0; frame < count; ++frame)
        {
            const bool inRange = frame >= first && frame < first + available;
            const auto target = inRange && side == 1 ? 1.0f : 0.0f;
            if (target > rtSourceBlend) rtSourceBlend = juce::jmin (target, rtSourceBlend + step);
            else if (target < rtSourceBlend) rtSourceBlend = juce::jmax (target, rtSourceBlend - step);
            const auto sourceFrame = sourceFirst + frame - first;
            const auto rangeGain = inRange ? juce::jlimit (0.0f, 1.0f,
                static_cast<float> (juce::jmin (sourceFrame + 1, length - sourceFrame)) / fadeFrames) : 0.0f;
            const auto blend = rtSourceBlend * rangeGain;
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto liveA = liveScratch.getSample (channel, frame) * aGain;
                const auto output = blend > 0.0f
                    ? liveA + (buffer.getSample (channel, frame) - liveA) * blend : liveA;
                buffer.setSample (channel, frame, output);
            }
            if (inRange && ((side == 0 && blend == 0.0f) || (side == 1 && blend == 1.0f))) ++confirmedFrames;
        }
        if (aGainDb < 0.0)
        {
            heldALinearGain.store (aGain, std::memory_order_relaxed);
            attenuationHoldActive.store (true, std::memory_order_release);
        }
        normalReturnRequired.store (true, std::memory_order_release);
        if (confirmedFrames != 0)
        {
            const auto sequence = callbackSequence.fetch_add (1, std::memory_order_acq_rel) + 1;
            auto firstSequence = firstCallbackSequence.load (std::memory_order_relaxed);
            if (firstSequence == 0) firstCallbackSequence.compare_exchange_strong (firstSequence, sequence, std::memory_order_relaxed);
            lastCallbackSequence.store (sequence, std::memory_order_release);
            (stimulus == 1 ? stimulusOneFrames : stimulusTwoFrames).fetch_add (confirmedFrames, std::memory_order_relaxed);
            const auto requested = requestSequence.load (std::memory_order_acquire);
            if (confirmedSequence.exchange (requested, std::memory_order_acq_rel) != requested)
                (stimulus == 1 ? stimulusOneSwitches : stimulusTwoSwitches).fetch_add (1, std::memory_order_relaxed);
            activeStimulus.store (stimulus, std::memory_order_release);
            int expected = armed;
            lifecycle.compare_exchange_strong (expected, active, std::memory_order_acq_rel);
        }
        release(); return true;
    }
}
