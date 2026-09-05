#pragma once

#include "reference_runtime_test_support.h"

namespace
{
    [[maybe_unused]] juce::String canonicalPcmHash (const std::vector<float>& interleaved)
    {
        juce::MemoryBlock canonical (interleaved.size() * 4, true);
        auto* output = static_cast<std::uint8_t*> (canonical.getData());
        for (size_t index = 0; index < interleaved.size(); ++index)
        {
            const float normalized = interleaved[index] == 0.0f ? 0.0f : interleaved[index];
            std::uint32_t bits = 0;
            std::memcpy (&bits, &normalized, sizeof (bits));
            output[index * 4] = static_cast<std::uint8_t> (bits & 0xffu);
            output[index * 4 + 1] = static_cast<std::uint8_t> ((bits >> 8u) & 0xffu);
            output[index * 4 + 2] = static_cast<std::uint8_t> ((bits >> 16u) & 0xffu);
            output[index * 4 + 3] = static_cast<std::uint8_t> ((bits >> 24u) & 0xffu);
        }
        return juce::SHA256 (canonical).toHexString();
    }

    std::shared_ptr<ref::RuntimeACaptureAudio> makeBlindA()
    {
        auto audio = std::make_shared<ref::RuntimeACaptureAudio>();
        audio->dawRevisionId = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
        audio->sampleRateHz = 48'000;
        audio->channels = 2;
        audio->startSample = 384'000; // 120 BPM, 4/4の4小節後（5小節目）
        audio->frameCount = 192'000;
        audio->interleaved.resize (static_cast<size_t> (audio->frameCount * 2));
        std::uint32_t random = 0x7391u;
        for (std::int64_t frame = 0; frame < audio->frameCount; ++frame)
        {
            if (frame % 960 == 0)
                random = random * 1'664'525u + 1'013'904'223u;
            const auto amplitude = 0.025f + 0.12f * static_cast<float> (
                (random >> 8u) & 0xffffu) / 65'535.0f;
            const auto sample = amplitude * static_cast<float> (std::sin (
                juce::MathConstants<double>::twoPi * 317.0
                * (frame + 0.25) / 48'000.0));
            audio->interleaved[static_cast<size_t> (frame * 2)] = sample;
            audio->interleaved[static_cast<size_t> (frame * 2 + 1)] = sample * 0.77f;
        }
        audio->cuePcmSha256 = canonicalPcmHash (audio->interleaved);
        return audio;
    }

    [[maybe_unused]] bool writeBlindB (const juce::File& file, const ref::RuntimeACaptureAudio& a,
                      bool addHeadroomBlockingPeak = false, int tailFrames = 0)
    {
        auto stream = file.createOutputStream();
        if (stream == nullptr)
            return false;
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
            stream.release(), 48'000.0, 2, 32, {}, 0));
        if (writer == nullptr)
            return false;
        juce::AudioBuffer<float> audio (
            2, static_cast<int> (a.frameCount) + tailFrames);
        for (int frame = 0; frame < audio.getNumSamples(); ++frame)
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto sample = frame < a.frameCount
                    ? a.interleaved[static_cast<size_t> (frame * 2 + channel)] * 0.5f
                    : static_cast<float> (0.02 * std::sin (
                        juce::MathConstants<double>::twoPi * 173.0
                        * (frame - a.frameCount) / 48'000.0));
                audio.setSample (channel, frame, sample);
            }
        if (addHeadroomBlockingPeak)
        {
            audio.setSample (0, 48'123, 0.95f);
            audio.setSample (1, 48'123, 0.90f);
        }
        return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    [[maybe_unused]] void testRuntimeEventTransport (const juce::File& sandbox)
    {
        const auto root = sandbox.getChildFile ("runtime-events");
        ref::RuntimeEventContext context;
        context.identity = { runtimeId, workId, 42 };
        context.manifestRevision = 7;
        context.presetArtifact = {
            "88888888-8888-4888-8888-888888888888",
            "99999999-9999-4999-8999-999999999999",
            "reference/presets/88888888-8888-4888-8888-888888888888/"
                "99999999-9999-4999-8999-999999999999.v1.json",
            juce::String::repeatedString ("a", 64),
            1024,
        };
        context.presetName = "Mix Reference";
        context.checkId = "55555555-5555-4555-8555-555555555555";
        context.checkLabel = juce::String::fromUTF8 ("低音");
        context.candidateId = "66666666-6666-4666-8666-666666666666";
        context.candidateName = "Reference Mix";
        context.cueId = "77777777-7777-4777-8777-777777777777";
        context.cueLabel = "Chorus";
        context.comparisonMode = "loudness_match";

        ref::RuntimeEventTransport transport (root);
        const auto eventId = ref::RuntimeEventTransport::uuidV4();
        const auto runId = ref::RuntimeEventTransport::uuidV4();
        const auto started = transport.writeAuditionStarted (
            context, eventId, runId, 1'788'390'000'000);
        const auto file = transport.eventFile (runtimeId, eventId);
        require (started.written && file.existsAsFile()
                 && file.getSize() > 0 && file.getSize() <= ref::maximumRuntimeEventBytes,
                 "audible B must write one bounded immutable runtime event");
        const auto parsed = juce::JSON::parse (file);
        require (parsed.getDynamicObject() != nullptr
                 && parsed.getDynamicObject()->getProperty ("event_type")
                      == "audition_started"
                 && static_cast<juce::int64> (
                        parsed.getDynamicObject()->getProperty ("manifest_revision")) == 7,
                 "runtime event must retain exact Manifest authority and event type");
        require (! transport.writeAuditionStarted (
                     context, eventId, runId, 1'788'390'000'001).written,
                 "an event ID must never be overwritten with different bytes");

        auto unordered = new juce::DynamicObject();
        unordered->setProperty ("z", 1);
        unordered->setProperty ("a", 2);
        require (ref::RuntimeEventTransport::canonicalJson (juce::var (unordered))
                    == "{\"a\":2,\"z\":1}",
                 "event canonicalization must sort object keys deterministically");
    }

    [[maybe_unused]] void testRuntimeV2Blind (const juce::File& sandbox)
    {
        const auto safePositive = ref::planRuntimeV2BlindGain (4.0, -6.0, -12.0);
        require (! safePositive.lowerAApprovalRequired
                 && std::abs (safePositive.aGainDb) < 1.0e-9
                 && std::abs (safePositive.bGainDb - 4.0) < 1.0e-9
                 && std::abs (safePositive.preservedPeakCeilingDbtp + 1.0) < 1.0e-9,
                 "positive B gain must remain available when it preserves source headroom");
        const auto sourcePeakCeiling = ref::planRuntimeV2BlindGain (3.0, 0.2, -4.0);
        require (! sourcePeakCeiling.lowerAApprovalRequired
                 && std::abs (sourcePeakCeiling.bGainDb - 3.0) < 1.0e-9
                 && std::abs (sourcePeakCeiling.preservedPeakCeilingDbtp - 0.2) < 1.0e-9,
                 "pre-existing A peak above -1 dBTP must be preserved instead of rejecting B");
        const auto lowerA = ref::planRuntimeV2BlindGain (6.0, -8.0, -2.0);
        require (lowerA.lowerAApprovalRequired
                 && std::abs (lowerA.aGainDb) < 1.0e-9
                 && std::abs (lowerA.bGainDb - 6.0) < 1.0e-9
                 && std::abs (lowerA.requiredAAttenuationDb - 6.0) < 1.0e-9,
                 "insufficient B headroom must require approval before lowering A");

        const auto a = makeBlindA();
        const auto bFile = sandbox.getChildFile ("blind-b-trimmed.wav");
        require (writeBlindB (bFile, *a), "trimmed Blind B fixture must be written");
        ref::RuntimeCandidate candidate;
        candidate.sourceKind = "work_version";
        candidate.sourceWorkId = workId;
        candidate.sourceRecordingId = "22222222-2222-4222-8222-222222222222";
        candidate.sourceVersionId = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
        ref::RuntimeCue cue;
        cue.cueId = "77777777-7777-4777-8777-777777777777";
        cue.sampleRateHz = 48'000;
        cue.startSample = 0;
        cue.endSample = a->frameCount;
        auto source = std::make_shared<ref::RuntimeSource>();
        source->sourceKind = "work_version";
        source->absolutePath = bFile.getFullPathName();
        source->audio.sampleRateHz = 48'000;
        source->audio.channels = 2;
        source->audio.totalSampleFrames = a->frameCount;

        ref::RuntimeV2Blind blind;
        blind.prepare (a, candidate, cue, source, false);
        auto state = blind.snapshot();
        require (state.eligible && ! state.lowerAApprovalRequired
                 && state.pairedBlockCount >= 27
                 && state.aStartSample == a->startSample
                 && state.aEndSample == a->startSample + a->frameCount
                 && state.bStartSample == 0
                 && state.bEndSample == a->frameCount
                 && state.aSampleRateHz == 48'000 && state.bSampleRateHz == 48'000
                 && state.channels == 2 && state.dawRevisionId == a->dawRevisionId
                 && state.aCuePcmSha256 == a->cuePcmSha256
                 && state.bCuePcmSha256.length() == 64,
                 "four-bar DAW pre-roll must align automatically to a trimmed same-recording B");
        require (blind.start() && blind.ongoing(),
                 "prepared immutable A/B must start Blind without another OS action");
        state = blind.snapshot();
        require (state.trialId.length() == 36 && state.trialId[14] == '4'
                 && (state.trialId[19] == '8' || state.trialId[19] == '9'
                     || state.trialId[19] == 'a' || state.trialId[19] == 'b')
                 && state.assignmentCommitmentSha256.length() == 64
                 && state.revealedNonceHex.isEmpty(),
                 "Blind start must publish a UUIDv4 commitment without revealing its nonce");
        juce::AudioBuffer<float> output (2, 256);
        output.clear();
        require (blind.render (output, a->startSample, true),
                 "Blind stimulus 1 must render from frozen PCM at the DAW content sample");
        require (! blind.reveal(), "Blind reveal must require an explicit answer");
        require (blind.requestStimulus (2), "Blind stimulus 2 must be selectable");
        output.clear();
        require (blind.render (output, a->startSample + 256, true),
                 "Blind stimulus 2 must render at the same frozen cue position");
        state = blind.snapshot();
        require (state.stimulusOneAudibleFrames == 256
                 && state.stimulusTwoAudibleFrames == 256
                 && state.stimulusOneConfirmedSwitches == 1
                 && state.stimulusTwoConfirmedSwitches == 1
                 && state.firstCallbackSequence == 1
                 && state.lastCallbackSequence == 2,
                 "Blind must count only audio-callback-confirmed switches and audible frames");
        require (blind.answer (1) && blind.reveal(),
                 "an explicit answer after hearing both stimuli must permit reveal");
        state = blind.snapshot();
        require (state.phase == ref::BlindPhase::revealed
                 && state.answeredStimulus == 1
                 && state.revealedStimulusOneSide >= 0
                 && state.revealedNonceHex.length() == 64,
                 "revealed Blind state must retain the explicit answer and assignment");
        const auto first = state.revealedStimulusOneSide == 1 ? "b" : "a";
        const auto second = state.revealedStimulusOneSide == 1 ? "a" : "b";
        const auto preimage = "{\"nonce\":\"" + state.revealedNonceHex
            + "\",\"stimulus_1\":\"" + first
            + "\",\"stimulus_2\":\"" + second
            + "\",\"trial_id\":\"" + state.trialId + "\"}";
        juce::MemoryBlock committed;
        constexpr char commitmentDomain[] = "kirin_reference_assignment_v1";
        committed.append (commitmentDomain, sizeof (commitmentDomain) - 1);
        const std::uint8_t separator = 0;
        committed.append (&separator, sizeof (separator));
        committed.append (preimage.toRawUTF8(), preimage.getNumBytesAsUTF8());
        require (juce::SHA256 (committed).toHexString()
                    == state.assignmentCommitmentSha256,
                 "revealed assignment and nonce must verify the immutable Start commitment");
        blind.end();
        state = blind.snapshot();
        require (! blind.ongoing() && state.phase == ref::BlindPhase::inactive
                 && state.trialId.isEmpty()
                 && state.assignmentCommitmentSha256.isEmpty()
                 && state.revealedNonceHex.isEmpty(),
                 "ending Blind must return to live A without retaining a public assignment");

        require (blind.start(), "prepared Blind must be reusable as a new independent trial");
        output.clear();
        output.addFrom (0, 0, a->interleaved.data(), output.getNumSamples());
        const auto beforeEnd = output.getSample (0, 0);
        require (! blind.render (output, a->startSample + a->frameCount - 128, true)
                 && std::abs (output.getSample (0, 0) - beforeEnd) < 1.0e-9f,
                 "non-loop Blind must return to unchanged A when a callback crosses Cue end");
        blind.end();

        auto negativeA = std::make_shared<ref::RuntimeACaptureAudio> (*a);
        negativeA->startSample = -384'000;
        ref::RuntimeV2Blind negativePositionBlind;
        negativePositionBlind.prepare (negativeA, candidate, cue, source, false);
        require (negativePositionBlind.start(),
                 "a signed DAW pre-roll range must remain eligible for Blind");
        output.clear();
        require (negativePositionBlind.render (output, negativeA->startSample, true),
                 "Blind must render a valid negative DAW pre-roll position without signed overflow");
        negativePositionBlind.end();

        const auto limitedBFile = sandbox.getChildFile ("blind-b-limited.wav");
        require (writeBlindB (limitedBFile, *a, true),
                 "headroom-limited Blind B fixture must be written");
        auto limitedSource = std::make_shared<ref::RuntimeSource> (*source);
        limitedSource->absolutePath = limitedBFile.getFullPathName();
        ref::RuntimeV2Blind approvalBlind;
        approvalBlind.prepare (a, candidate, cue, limitedSource, false);
        state = approvalBlind.snapshot();
        require (state.eligible && state.lowerAApprovalRequired
                 && state.requiredAAttenuationDb > 0.0
                 && ! approvalBlind.start(),
                 "insufficient B headroom must keep A unchanged until explicit approval");
        require (approvalBlind.start (true),
                 "one explicit approval must start the current lower-A Blind trial");
        const auto approved = approvalBlind.snapshot();
        output.clear();
        output.setSample (0, 0, 0.5f);
        const auto unattenuated = output.getSample (0, 0);
        approvalBlind.invalidate();
        require (approvalBlind.renderInvalidatedA (output, true)
                 && std::abs (output.getSample (0, 0)
                    - unattenuated * std::pow (10.0f,
                                               static_cast<float> (approved.aGainDb / 20.0)))
                    < 1.0e-6f,
                 "an interrupted approved trial must hold lowered A until explicit return");
        approvalBlind.end();
        state = approvalBlind.snapshot();
        require (state.lowerAApprovalRequired && std::abs (state.aGainDb) < 1.0e-9
                 && ! approvalBlind.start(),
                 "ending a lower-A trial must restore A and require approval again");
    }

}
