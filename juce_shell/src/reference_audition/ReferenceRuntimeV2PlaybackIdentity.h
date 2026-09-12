#pragma once

#include "ReferenceRuntimeV2Model.h"

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
        const RuntimeCandidate& candidate, const RuntimeCue& cue)
    {
        juce::String key;
        for (const auto& value : { preset.workId, preset.sourcePresetArtifact.presetId,
                                  check.checkId, check.mode, check.comparisonMode,
                                  candidate.candidateId, candidate.sourceKind,
                                  candidate.sourceIdentityKey, candidate.sourceWorkId,
                                  candidate.sourceRecordingId, candidate.sourceVersionId,
                                  candidate.sourceArtifact.relativePath,
                                  candidate.sourceArtifact.sha256,
                                  juce::String (candidate.sourceArtifact.bytes),
                                  runtimeCuePlaybackIdentity (cue) })
            appendPlaybackIdentity (key, value);
        return key;
    }
}
