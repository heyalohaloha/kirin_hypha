#pragma once

#include "ReferenceAuditionProtocol.h"

namespace hypha::reference_audition
{
constexpr int referenceStateHardLimitBytes = 1'048'576;
constexpr int referenceStateMaximumEncodedBytes = 1'032'192;
constexpr int referenceCaptureMaximumEncodedBytes = 988'172;
constexpr int referenceTonalMaximumEncodedBytes = 8'192;
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
}
