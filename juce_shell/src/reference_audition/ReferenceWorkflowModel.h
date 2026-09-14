#pragma once

#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

namespace hypha::reference_audition
{
struct WorkflowCondition
{
    juce::String comparison;
    juce::String presetId, presetRevisionId, presetSha256;
    juce::String checkId, candidateId, sourceKind, sourceIdentityKey;
    juce::String cueId, cueLabel;
    std::int64_t cueStart = 0, cueEnd = 0, cueRate = 0;
    bool cueLoops = false;
};

struct WorkflowItem
{
    juce::String itemId, momentId, momentRevisionId, title, purpose;
    WorkflowCondition condition;
};

struct WorkflowDefinition
{
    enum class Kind { review, bookmark };
    Kind kind = Kind::review;
    juce::String id, revisionId, sha256, title;
    std::vector<WorkflowItem> items;
};

struct WorkflowCatalog
{
    std::shared_ptr<const WorkflowDefinition> latestReview;
    std::shared_ptr<const WorkflowDefinition> latestBookmark;
    juce::String rejectionCode;
    std::int64_t indexModifiedMs = 0, indexBytes = 0;
};

struct WorkflowEventRequest
{
    juce::String runtimeInstanceId, reviewId, attemptId, eventId, operationId;
    juce::String kind, conditionRevisionId, itemId, nextItemId, confirmation, artifactSha256;
};

struct WorkflowEventCommit
{
    juce::String attemptId, eventId, operationId, artifactSha256, checkpointId;
    std::int64_t sequence = 0;
    bool committed = false;
};

struct WorkflowView
{
    enum class Mode { normal, review, bookmark };
    enum class Status { unavailable, available, preparing, ready, saving, rejected, resumeAvailable };

    Mode mode = Mode::normal;
    Status status = Status::unavailable;
    juce::String definitionId, definitionTitle, itemId, itemTitle, purpose, message;
    int itemIndex = 0, itemCount = 0;
    bool canMoveBack = false, canAdvance = false, canEnd = false;
    bool reviewAvailable = false, bookmarkAvailable = false;
};
}
