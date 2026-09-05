#include "ReferenceRuntimeV2Blind.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <juce_cryptography/juce_cryptography.h>

namespace hypha::reference_audition
{
    namespace
    {
        char hexDigit (std::uint8_t value) noexcept
        {
            return "0123456789abcdef"[value & 0x0fu];
        }

        juce::String lowercaseHex (const std::uint8_t* bytes, size_t count)
        {
            juce::String result;
            result.preallocateBytes (static_cast<size_t> (count * 2 + 1));
            for (size_t index = 0; index < count; ++index)
            {
                result += juce::String::charToString (hexDigit (bytes[index] >> 4u));
                result += juce::String::charToString (hexDigit (bytes[index]));
            }
            return result;
        }

        juce::String uuidV4 (std::array<std::uint8_t, 16> bytes)
        {
            bytes[6] = static_cast<std::uint8_t> ((bytes[6] & 0x0fu) | 0x40u);
            bytes[8] = static_cast<std::uint8_t> ((bytes[8] & 0x3fu) | 0x80u);
            const auto hex = lowercaseHex (bytes.data(), bytes.size());
            return hex.substring (0, 8) + "-" + hex.substring (8, 12) + "-"
                 + hex.substring (12, 16) + "-" + hex.substring (16, 20) + "-"
                 + hex.substring (20);
        }

        juce::String assignmentCommitment (const juce::String& trialId,
                                            const juce::String& nonce,
                                            bool stimulusOneIsB)
        {
            const auto first = stimulusOneIsB ? "b" : "a";
            const auto second = stimulusOneIsB ? "a" : "b";
            const auto preimage = "{\"nonce\":\"" + nonce
                + "\",\"stimulus_1\":\"" + first
                + "\",\"stimulus_2\":\"" + second
                + "\",\"trial_id\":\"" + trialId + "\"}";
            juce::MemoryBlock committed;
            constexpr char domain[] = "kirin_reference_assignment_v1";
            committed.append (domain, sizeof (domain) - 1);
            const std::uint8_t separator = 0;
            committed.append (&separator, sizeof (separator));
            committed.append (preimage.toRawUTF8(), preimage.getNumBytesAsUTF8());
            return juce::SHA256 (committed).toHexString();
        }
    }

    RuntimeV2BlindCommitment createRuntimeV2BlindCommitment()
    {
        std::array<std::uint8_t, 49> random {};
        secureRandomBytes (random.data(), random.size());
        RuntimeV2BlindCommitment result;
        result.stimulusOneIsB = (random[0] & 1u) != 0u;
        std::array<std::uint8_t, 16> trialBytes {};
        std::copy_n (random.begin() + 1, trialBytes.size(), trialBytes.begin());
        result.trialId = uuidV4 (trialBytes);
        result.nonceHex = lowercaseHex (random.data() + 17, 32);
        result.commitmentSha256 = assignmentCommitment (
            result.trialId, result.nonceHex, result.stimulusOneIsB);
        return result;
    }

    RuntimeV2BlindGainPlan planRuntimeV2BlindGain (
        double pairedLoudnessDeltaDb, double aCueTruePeakDbtp,
        double bCueTruePeakDbtp) noexcept
    {
        RuntimeV2BlindGainPlan result;
        if (! std::isfinite (pairedLoudnessDeltaDb)
            || ! std::isfinite (aCueTruePeakDbtp)
            || ! std::isfinite (bCueTruePeakDbtp))
            return result;
        result.preservedPeakCeilingDbtp = std::max (
            { -1.0, aCueTruePeakDbtp, bCueTruePeakDbtp });
        result.bGainDb = pairedLoudnessDeltaDb;
        if (result.bGainDb > 0.0
            && bCueTruePeakDbtp + result.bGainDb
                > result.preservedPeakCeilingDbtp + 1.0e-9)
        {
            result.requiredAAttenuationDb = result.bGainDb;
            result.lowerAApprovalRequired = true;
        }
        return result;
    }

    void RuntimeV2Blind::resetSession() noexcept
    {
        requestedStimulus.store (0, std::memory_order_relaxed);
        activeStimulus.store (0, std::memory_order_relaxed);
        answeredStimulus.store (0, std::memory_order_relaxed);
        requestSequence.store (0, std::memory_order_relaxed);
        confirmedSequence.store (0, std::memory_order_relaxed);
        callbackSequence.store (0, std::memory_order_relaxed);
        firstCallbackSequence.store (0, std::memory_order_relaxed);
        lastCallbackSequence.store (0, std::memory_order_relaxed);
        stimulusOneFrames.store (0, std::memory_order_relaxed);
        stimulusTwoFrames.store (0, std::memory_order_relaxed);
        stimulusOneSwitches.store (0, std::memory_order_relaxed);
        stimulusTwoSwitches.store (0, std::memory_order_relaxed);
    }

    BlindPhase RuntimeV2Blind::publicPhase (int state) noexcept
    {
        if (state == active) return BlindPhase::active;
        if (state == revealed) return BlindPhase::revealed;
        if (state == invalidated) return BlindPhase::invalidated;
        return BlindPhase::inactive;
    }

    RuntimeV2BlindSnapshot RuntimeV2Blind::snapshot() const
    {
        RuntimeV2BlindSnapshot result;
        snapshotReadersInFlight.fetch_add (1, std::memory_order_acq_rel);
        const auto state = lifecycle.load (std::memory_order_acquire);
        if (state == preparing)
        {
            snapshotReadersInFlight.fetch_sub (1, std::memory_order_release);
            return result;
        }
        result.phase = publicPhase (state);
        result.eligible = state == prepared || state == approvalRequired
                       || state == active || state == revealed;
        result.lowerAApprovalRequired = state == approvalRequired;
        result.requiredAAttenuationDb = requiredAAttenuationDb;
        result.activeStimulus = activeStimulus.load (std::memory_order_acquire);
        const auto requested = requestedStimulus.load (std::memory_order_acquire);
        result.pendingStimulus = result.activeStimulus == requested ? 0 : requested;
        result.answeredStimulus = answeredStimulus.load (std::memory_order_acquire);
        result.revealedStimulusOneSide = state == revealed ? sideForStimulus (1) : -1;
        result.aGainDb = aGainDb;
        result.bGainDb = bGainDb;
        result.aCueTruePeakDbtp = aCueTruePeakDbtp;
        result.bCueTruePeakDbtp = bCueTruePeakDbtp;
        result.pairedLoudnessDeltaDb = pairedLoudnessDeltaDb;
        result.preservedPeakCeilingDbtp = preservedPeakCeilingDbtp;
        result.pairedBlockCount = pairedBlockCount;
        result.aStartSample = aStartSample;
        result.aEndSample = aStartSample + frameCount;
        result.bStartSample = bStartSample;
        result.bEndSample = bEndSample;
        result.aSampleRateHz = sampleRateHz;
        result.bSampleRateHz = bSampleRateHz;
        result.channels = channels;
        result.dawRevisionId = dawRevisionId;
        result.aCuePcmSha256 = aCuePcmSha256;
        result.bCuePcmSha256 = bCuePcmSha256;
        result.stimulusOneAudibleFrames = stimulusOneFrames.load (std::memory_order_acquire);
        result.stimulusTwoAudibleFrames = stimulusTwoFrames.load (std::memory_order_acquire);
        result.stimulusOneConfirmedSwitches = stimulusOneSwitches.load (std::memory_order_acquire);
        result.stimulusTwoConfirmedSwitches = stimulusTwoSwitches.load (std::memory_order_acquire);
        result.firstCallbackSequence = firstCallbackSequence.load (std::memory_order_acquire);
        result.lastCallbackSequence = lastCallbackSequence.load (std::memory_order_acquire);
        if (state == active || state == revealed)
        {
            result.trialId = trialId;
            result.assignmentCommitmentSha256 = assignmentCommitmentSha256;
        }
        if (state == revealed)
            result.revealedNonceHex = assignmentNonceHex;
        result.rejectionCode = rejectionCode;
        snapshotReadersInFlight.fetch_sub (1, std::memory_order_release);
        return result;
    }

    bool RuntimeV2Blind::ongoing() const noexcept
    {
        const auto state = lifecycle.load (std::memory_order_acquire);
        return state == active || state == revealed;
    }
}
