#pragma once

#include <juce_graphics/juce_graphics.h>

#include "HyphaObservatoryContract.h"

// Presentation-only contract for an exported Hypha Observation Plate.
//
// Display names enter through explicit, human-readable sources only. Filesystem paths,
// project/work IDs, UUIDs and internal instance IDs are intentionally absent from this type, so
// capture code cannot silently substitute an implementation identity for unavailable metadata.
namespace hypha::capture
{
struct PrivacyOptions
{
    bool includeGuide = false;
    bool includePreName = false;
    bool includePostName = false;
    bool includeProjectName = false;
};

inline juce::String normalizeDisplayName (juce::String source, int maximumCharacters = 96)
{
    if (maximumCharacters <= 0)
        return {};
    source = source.substring (0, maximumCharacters * 4);
    juce::String normalized;
    bool previousWasSpace = true;
    auto cursor = source.getCharPointer();
    while (! cursor.isEmpty() && normalized.length() < maximumCharacters)
    {
        const auto character = cursor.getAndAdvance();
        const bool isSpace = character < 0x20 || character == 0x7f
                          || juce::CharacterFunctions::isWhitespace (character);
        if (isSpace)
        {
            if (! previousWasSpace)
                normalized += " ";
            previousWasSpace = true;
        }
        else
        {
            normalized += juce::String::charToString (character);
            previousWasSpace = false;
        }
    }
    return normalized.trimEnd();
}

struct DisplayMetadata
{
    juce::String preName;
    juce::String postName;
    juce::String projectName;

    DisplayMetadata normalized() const
    {
        return {
            normalizeDisplayName (preName, 64),
            normalizeDisplayName (postName, 96),
            normalizeDisplayName (projectName, 96),
        };
    }

    DisplayMetadata applying (PrivacyOptions privacy) const
    {
        const auto safe = normalized();
        return {
            privacy.includePreName ? safe.preName : juce::String {},
            privacy.includePostName ? safe.postName : juce::String {},
            privacy.includeProjectName ? safe.projectName : juce::String {},
        };
    }

    juce::String footerLine() const
    {
        juce::StringArray facts;
        if (preName.isNotEmpty())
            facts.add ("PRE  " + preName);
        if (postName.isNotEmpty())
            facts.add ("POST  " + postName);
        if (projectName.isNotEmpty())
            facts.add ("PROJECT  " + projectName);
        return facts.joinIntoString ("   |   ");
    }
};

struct Snapshot
{
    juce::Image image;
    observatory::Domain domain = observatory::Domain::level;
    observatory::ObservationTarget target = observatory::ObservationTarget::absolute;
    juce::String capturedAt;
    juce::String filenameStamp;
    std::int64_t capturedAtMs = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;

    bool complete() const noexcept
    {
        return image.isValid() && image.getWidth() == pixelWidth
            && image.getHeight() == pixelHeight && capturedAt.isNotEmpty()
            && filenameStamp.isNotEmpty() && capturedAtMs > 0;
    }
};
}
