#include "ReferenceRuntimeV2Blind.h"

#include <cmath>
#include <limits>

namespace hypha::reference_audition
{
    namespace
    {
        float linearGain (double decibels)
        {
            return static_cast<float> (std::pow (10.0, decibels / 20.0));
        }

        std::int64_t positiveModulo (std::int64_t value,
                                     std::int64_t modulus) noexcept
        {
            const auto remainder = value % modulus;
            return remainder < 0 ? remainder + modulus : remainder;
        }

        std::int64_t positiveModuloDifference (std::int64_t value,
                                               std::int64_t origin,
                                               std::int64_t modulus) noexcept
        {
            const auto valueRemainder = positiveModulo (value, modulus);
            const auto originRemainder = positiveModulo (origin, modulus);
            return valueRemainder >= originRemainder
                ? valueRemainder - originRemainder
                : modulus - (originRemainder - valueRemainder);
        }

        bool checkedSubtract (std::int64_t left, std::int64_t right,
                              std::int64_t& result) noexcept
        {
            if ((right < 0 && left > std::numeric_limits<std::int64_t>::max() + right)
                || (right > 0 && left < std::numeric_limits<std::int64_t>::min() + right))
                return false;
            result = left - right;
            return true;
        }
    }

    bool RuntimeV2Blind::render (juce::AudioBuffer<float>& buffer,
                                 std::int64_t hostPosition,
                                 bool positionValid) noexcept
    {
        auto state = lifecycle.load (std::memory_order_acquire);
        if ((state != active && state != revealed) || ! positionValid
            || frameCount < 1 || buffer.getNumChannels() != channels)
            return false;
        callbacksInFlight.fetch_add (1, std::memory_order_acq_rel);
        state = lifecycle.load (std::memory_order_acquire);
        if (state != active && state != revealed)
        {
            callbacksInFlight.fetch_sub (1, std::memory_order_release);
            return false;
        }
        const auto stimulus = requestedStimulus.load (std::memory_order_acquire);
        const auto side = sideForStimulus (stimulus);
        const auto& source = side == 1 ? frozenB : frozenA;
        const auto gain = linearGain (side == 1 ? bGainDb : aGainDb);
        std::int64_t offset = 0;
        if (loopEnabled)
            offset = positiveModuloDifference (hostPosition, aStartSample, frameCount);
        else if (! checkedSubtract (hostPosition, aStartSample, offset)
                 || offset < 0 || offset >= frameCount
                 || static_cast<std::int64_t> (buffer.getNumSamples()) > frameCount - offset)
        {
            callbacksInFlight.fetch_sub (1, std::memory_order_release);
            return false;
        }
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        {
            const auto sourceFrame = (offset + frame) % frameCount;
            for (int channel = 0; channel < channels; ++channel)
                buffer.setSample (channel, frame, source[static_cast<size_t> (
                    sourceFrame * channels + channel)] * gain);
        }
        const auto sequence = callbackSequence.fetch_add (1, std::memory_order_acq_rel) + 1;
        auto first = firstCallbackSequence.load (std::memory_order_relaxed);
        if (first == 0)
            firstCallbackSequence.compare_exchange_strong (first, sequence,
                                                           std::memory_order_relaxed);
        lastCallbackSequence.store (sequence, std::memory_order_release);
        const auto frames = static_cast<std::uint64_t> (buffer.getNumSamples());
        (stimulus == 1 ? stimulusOneFrames : stimulusTwoFrames)
            .fetch_add (frames, std::memory_order_relaxed);
        const auto requested = requestSequence.load (std::memory_order_acquire);
        if (confirmedSequence.exchange (requested, std::memory_order_acq_rel) != requested)
            (stimulus == 1 ? stimulusOneSwitches : stimulusTwoSwitches)
                .fetch_add (1, std::memory_order_relaxed);
        activeStimulus.store (stimulus, std::memory_order_release);
        callbacksInFlight.fetch_sub (1, std::memory_order_release);
        return true;
    }

    bool RuntimeV2Blind::renderInvalidatedA (juce::AudioBuffer<float>& buffer,
                                             bool auditionAllowed) noexcept
    {
        if (! auditionAllowed
            || lifecycle.load (std::memory_order_acquire) != invalidated)
            return false;
        callbacksInFlight.fetch_add (1, std::memory_order_acq_rel);
        if (lifecycle.load (std::memory_order_acquire) != invalidated)
        {
            callbacksInFlight.fetch_sub (1, std::memory_order_release);
            return false;
        }
        const auto gainDb = aGainDb;
        if (gainDb >= 0.0)
        {
            callbacksInFlight.fetch_sub (1, std::memory_order_release);
            return false;
        }
        buffer.applyGain (linearGain (gainDb));
        callbacksInFlight.fetch_sub (1, std::memory_order_release);
        return true;
    }
}
