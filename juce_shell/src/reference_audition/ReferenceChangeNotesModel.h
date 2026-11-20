#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace hypha::reference_audition
{
struct ChangeNotesReceipt
{
    juce::String relativePath, sha256;
    std::int64_t bytes = 0;
};
struct ChangeNote
{
    juce::String noteId, originId, text;
    std::int64_t number = 0, atSample = 0;
    double atSeconds = 0.0;
    bool done = false;
};
struct ChangeNotes
{
    juce::String artifactSha256;
    std::int64_t sampleRateHz = 0, totalSampleFrames = 0, openCount = 0;
    std::vector<ChangeNote> notes;
};
struct ChangeNotesReadResult
{
    std::shared_ptr<const ChangeNotes> notes;
    juce::String issue;
};
}
