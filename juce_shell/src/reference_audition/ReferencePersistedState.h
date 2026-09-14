#pragma once

#include "ReferenceAuditionProtocol.h"

namespace hypha::reference_audition
{
constexpr int referenceStateHardLimitBytes = 1'048'576;
constexpr int referenceStateMaximumEncodedBytes = 1'032'192;
constexpr int referenceCaptureMaximumEncodedBytes = 988'172;
constexpr int referenceTonalMaximumEncodedBytes = 8'192;
constexpr int referenceWorkflowMaximumEncodedBytes = 2'048;
constexpr int referenceStateMinimumMarginBytes = 16'384;

struct TonalDisplayState
{
    enum class Source : int { live = 0, captured = 1 };

    Source source = Source::live;
    int selectedBand = -1;
    double rangeStart = 0.0;
    double rangeEnd = 0.0;
    juce::String genreId;
    juce::String captureId;
    juce::String artifactSha256;
    juce::String publicationRevision;

    bool valid() const noexcept;
    void write (juce::XmlElement&) const;
    static TonalDisplayState read (const juce::XmlElement&);
};

struct WorkflowResumeState
{
    enum class Mode : int { idle = 0, review = 1, bookmark = 2 };

    Mode mode = Mode::idle;
    juce::String reviewId;
    juce::String reviewRevisionId;
    juce::String attemptId;
    juce::String checkpointId;
    juce::String conditionRevisionId;
    juce::String bookmarkId;
    juce::String journalHeadSha256;
    int returnSlot = 0;

    bool valid() const noexcept;
    void write (juce::XmlElement&) const;
    static WorkflowResumeState read (const juce::XmlElement&);
};
}
