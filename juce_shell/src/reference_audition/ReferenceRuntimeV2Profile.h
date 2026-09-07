#pragma once

#include <map>
#include <optional>

#include "ReferenceRuntimeV2Model.h"

namespace hypha::reference_audition
{
    struct RuntimeProfileSourceReceipt
    {
        juce::String profileId;
        juce::String revisionId;
        juce::String relativePath;
        juce::String sha256;
        std::int64_t bytes = 0;
    };

    struct RuntimeProfileDistribution
    {
        std::vector<std::int64_t> contributorCount;
        std::vector<std::optional<std::int64_t>> p10;
        std::vector<std::optional<std::int64_t>> median;
        std::vector<std::optional<std::int64_t>> p90;
    };

    struct RuntimeProfileView
    {
        std::vector<double> axis;
        std::map<std::string, RuntimeProfileDistribution> series;
    };

    struct RuntimeProfile
    {
        RuntimeProfileSourceReceipt sourceProfileArtifact;
        juce::String name;
        std::int64_t sourceCount = 0;
        std::map<std::string, RuntimeProfileView> views;
    };

    struct RuntimeProfileLoadResult
    {
        std::shared_ptr<const RuntimeProfile> profile;
        juce::String rejectionCode;

        bool accepted() const noexcept { return profile != nullptr; }
    };

    class RuntimeV2ProfileRepository final
    {
    public:
        explicit RuntimeV2ProfileRepository (juce::File transportRootIn);
        RuntimeProfileLoadResult load (const RuntimeContentReceipt&) const;

    private:
        const juce::File root;
    };
}
