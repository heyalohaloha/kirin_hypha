#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

namespace hypha::reference_audition
{
    struct RuntimeContentReceipt
    {
        juce::String relativePath;
        juce::String sha256;
        std::int64_t bytes = 0;
    };

    struct RuntimeSourcePresetReceipt
    {
        juce::String presetId;
        juce::String revisionId;
        juce::String relativePath;
        juce::String sha256;
        std::int64_t bytes = 0;
    };

    struct RuntimePresetReceipt : RuntimeSourcePresetReceipt {};

    struct RuntimeCue
    {
        juce::String cueId;
        juce::String label;
        std::int64_t sampleRateHz = 0;
        std::int64_t startSample = 0;
        std::int64_t endSample = 0;
        bool loopEnabled = false;
    };

    struct RuntimeCandidate
    {
        juce::String candidateId;
        juce::String displayName;
        juce::String sourceKind;
        juce::String sourceIdentityKey;
        juce::String sourceWorkId;
        juce::String sourceRecordingId;
        juce::String sourceVersionId;
        RuntimeContentReceipt sourceArtifact;
        std::vector<RuntimeCue> cues;
        juce::String defaultCueId;
    };

    struct RuntimeProfileBinding
    {
        RuntimeContentReceipt profileArtifact;
        std::int64_t weightBasisPoints = 0;
    };

    struct RuntimeCheck
    {
        juce::String checkId;
        juce::String label;
        juce::String mode;
        std::vector<juce::String> viewBindings;
        juce::String comparisonMode;
        std::vector<RuntimeCandidate> candidates;
        std::vector<RuntimeProfileBinding> profileBindings;
    };

    struct RuntimePreset
    {
        juce::String workId;
        RuntimeSourcePresetReceipt sourcePresetArtifact;
        juce::String name;
        std::vector<RuntimeCheck> checks;
    };

    struct RuntimeManifest
    {
        juce::String workId;
        std::int64_t revision = 0;
        RuntimeContentReceipt sourceStateArtifact;
        juce::String activePresetId;
        juce::String activePresetRevisionId;
        std::vector<RuntimePresetReceipt> presetArtifacts;
    };

    struct RuntimeWorkspace
    {
        RuntimeManifest manifest;
        std::vector<RuntimePreset> presets;
    };

    enum class RuntimeWorkspaceLoadState
    {
        missing,
        unchanged,
        updated,
        retainedPrevious,
        rejected,
    };

    struct RuntimeWorkspaceLoadResult
    {
        RuntimeWorkspaceLoadState state = RuntimeWorkspaceLoadState::missing;
        std::shared_ptr<const RuntimeWorkspace> workspace;
        juce::String rejectionCode;

        bool usable() const noexcept { return workspace != nullptr; }
    };
}
