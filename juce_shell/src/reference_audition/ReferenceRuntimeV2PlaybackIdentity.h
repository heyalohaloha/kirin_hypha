#pragma once

#include "ReferenceRuntimeV2Model.h"
#include "ReferenceRuntimeV2Source.h"

namespace hypha::reference_audition
{
    // Worker-only dependency keys. A Manifest revision is publication history,
    // not an audible condition: other Presets, candidates, labels and facts may
    // arrive while this exact selection is playing. Length prefixes avoid
    // ambiguous concatenation; immutable source receipts bind the gain facts.
    inline void appendPlaybackIdentity (juce::String& key, const juce::String& value)
    {
        key += juce::String (value.length()) + ":" + value;
    }

    // Verified media identity is independent of optional observations arriving later.
    inline juce::String runtimeSourceAudioIdentity (const RuntimeSource& source)
    {
        juce::String key;
        for (const auto& value : { source.sourceKind, source.sourceIdentityKey,
                source.sourceFileSha256, source.sourcePcmSha256, source.absolutePath,
                source.revision.deviceId, source.revision.fileId, source.revision.sizeBytes,
                source.revision.modifiedTimeNs, source.revision.changedTimeNs,
                juce::String (source.audio.sampleRateHz), juce::String (source.audio.channels),
                juce::String (source.audio.totalSampleFrames) })
            appendPlaybackIdentity (key, value);
        return key;
    }

    inline juce::String runtimeCuePlaybackIdentity (const RuntimeCue& cue)
    {
        juce::String key;
        for (const auto& value : { cue.cueId, juce::String (cue.sampleRateHz),
                                  juce::String (cue.startSample), juce::String (cue.endSample),
                                  juce::String (cue.loopEnabled ? 1 : 0) })
            appendPlaybackIdentity (key, value);
        return key;
    }

    inline juce::String runtimeSelectionPlaybackIdentity (
        const RuntimePreset& preset, const RuntimeCheck& check,
        const RuntimeCandidate& candidate, const RuntimeCue& cue,
        const juce::String& verifiedAudioIdentity = {})
    {
        juce::String key;
        for (const auto& value : { preset.workId, preset.sourcePresetArtifact.presetId,
                                  check.checkId, check.mode, check.comparisonMode,
                                  candidate.candidateId, candidate.sourceKind,
                                  candidate.sourceIdentityKey, candidate.sourceWorkId,
                                  candidate.sourceRecordingId, candidate.sourceVersionId })
            appendPlaybackIdentity (key, value);
        if (verifiedAudioIdentity.isEmpty())
            for (const auto& value : { candidate.sourceArtifact.relativePath,
                                      candidate.sourceArtifact.sha256,
                                      juce::String (candidate.sourceArtifact.bytes) })
                appendPlaybackIdentity (key, value);
        else appendPlaybackIdentity (key, verifiedAudioIdentity);
        appendPlaybackIdentity (key, runtimeCuePlaybackIdentity (cue));
        return key;
    }
}
