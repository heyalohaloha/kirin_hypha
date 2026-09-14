#include "ReferencePersistedState.h"

#include <cmath>

namespace hypha::reference_audition
{
namespace
{
bool optionalId (const juce::String& value) noexcept
{
    return value.isEmpty() || (value.length() <= 160 && safeId (value));
}

bool optionalUuid (const juce::String& value) noexcept
{
    return value.isEmpty() || safeUuid (value);
}

juce::String boundedAttribute (const juce::XmlElement& xml, const char* name)
{
    const auto value = xml.getStringAttribute (name);
    return value.getNumBytesAsUTF8() <= 256 ? value : juce::String {};
}
}

bool TonalDisplayState::valid() const noexcept
{
    const bool rangeValid = (rangeStart == 0.0 && rangeEnd == 0.0)
        || (std::isfinite (rangeStart) && std::isfinite (rangeEnd)
            && rangeStart >= 0.0 && rangeEnd > rangeStart && rangeEnd <= 86'400.0);
    return (source == Source::live || source == Source::captured)
        && selectedBand >= -1 && selectedBand < 60
        && rangeValid
        && optionalId (genreId)
        && optionalUuid (captureId)
        && (artifactSha256.isEmpty() || safeSha256 (artifactSha256))
        && optionalId (publicationRevision)
        && (source != Source::captured || captureId.isNotEmpty());
}

void TonalDisplayState::write (juce::XmlElement& parent) const
{
    if (! valid()) return;
    auto* xml = parent.createNewChildElement ("ReferenceTonal");
    xml->setAttribute ("version", 1);
    xml->setAttribute ("source", static_cast<int> (source));
    xml->setAttribute ("band", selectedBand);
    xml->setAttribute ("range_start", rangeStart);
    xml->setAttribute ("range_end", rangeEnd);
    xml->setAttribute ("genre", genreId);
    xml->setAttribute ("capture_id", captureId);
    xml->setAttribute ("artifact_sha256", artifactSha256);
    xml->setAttribute ("publication_revision", publicationRevision);
}

TonalDisplayState TonalDisplayState::read (const juce::XmlElement& parent)
{
    TonalDisplayState result;
    if (const auto* xml = parent.getChildByName ("ReferenceTonal");
        xml != nullptr && xml->getIntAttribute ("version") == 1)
    {
        result.source = xml->getIntAttribute ("source") == 1 ? Source::captured : Source::live;
        result.selectedBand = xml->getIntAttribute ("band", -1);
        result.rangeStart = xml->getDoubleAttribute ("range_start");
        result.rangeEnd = xml->getDoubleAttribute ("range_end");
        result.genreId = boundedAttribute (*xml, "genre");
        result.captureId = boundedAttribute (*xml, "capture_id");
        result.artifactSha256 = boundedAttribute (*xml, "artifact_sha256");
        result.publicationRevision = boundedAttribute (*xml, "publication_revision");
    }
    return result.valid() ? result : TonalDisplayState {};
}

bool WorkflowResumeState::valid() const noexcept
{
    if (mode != Mode::idle && mode != Mode::review && mode != Mode::bookmark) return false;
    for (const auto* value : { &reviewId, &reviewRevisionId, &attemptId, &checkpointId,
                              &conditionRevisionId, &bookmarkId })
        if (! optionalUuid (*value)) return false;
    if (! journalHeadSha256.isEmpty() && ! safeSha256 (journalHeadSha256)) return false;
    if (returnSlot < 0 || returnSlot > 2) return false;
    if (mode == Mode::idle)
        return reviewId.isEmpty() && attemptId.isEmpty() && bookmarkId.isEmpty();
    if (mode == Mode::review)
        return reviewId.isNotEmpty() && reviewRevisionId.isNotEmpty()
            && attemptId.isNotEmpty() && checkpointId.isNotEmpty();
    return bookmarkId.isNotEmpty();
}

void WorkflowResumeState::write (juce::XmlElement& parent) const
{
    if (! valid() || mode == Mode::idle) return;
    auto* xml = parent.createNewChildElement ("ReferenceWorkflow");
    xml->setAttribute ("version", 1);
    xml->setAttribute ("mode", static_cast<int> (mode));
    xml->setAttribute ("review_id", reviewId);
    xml->setAttribute ("review_revision_id", reviewRevisionId);
    xml->setAttribute ("attempt_id", attemptId);
    xml->setAttribute ("checkpoint_id", checkpointId);
    xml->setAttribute ("condition_revision_id", conditionRevisionId);
    xml->setAttribute ("bookmark_id", bookmarkId);
    xml->setAttribute ("journal_head_sha256", journalHeadSha256);
    xml->setAttribute ("return_slot", returnSlot);
}

WorkflowResumeState WorkflowResumeState::read (const juce::XmlElement& parent)
{
    WorkflowResumeState result;
    if (const auto* xml = parent.getChildByName ("ReferenceWorkflow");
        xml != nullptr && xml->getIntAttribute ("version") == 1)
    {
        result.mode = static_cast<Mode> (xml->getIntAttribute ("mode"));
        result.reviewId = boundedAttribute (*xml, "review_id");
        result.reviewRevisionId = boundedAttribute (*xml, "review_revision_id");
        result.attemptId = boundedAttribute (*xml, "attempt_id");
        result.checkpointId = boundedAttribute (*xml, "checkpoint_id");
        result.conditionRevisionId = boundedAttribute (*xml, "condition_revision_id");
        result.bookmarkId = boundedAttribute (*xml, "bookmark_id");
        result.journalHeadSha256 = boundedAttribute (*xml, "journal_head_sha256");
        result.returnSlot = xml->getIntAttribute ("return_slot");
    }
    return result.valid() ? result : WorkflowResumeState {};
}
}
