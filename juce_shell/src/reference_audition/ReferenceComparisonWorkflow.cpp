#include "ReferenceComparisonController.h"

namespace hypha::reference_audition
{
namespace
{
juce::String uuid()
{
    return juce::Uuid().toDashedString().toLowerCase();
}
}

bool ReferenceComparisonController::appendWorkflowEvent (
    const juce::String& kind, const WorkflowItem& item,
    const std::shared_ptr<const WorkflowDefinition>& definition,
    PendingWorkflowTransition::Action action, int nextIndex)
{
    if (definition == nullptr) return false;
    WorkflowEventRequest request;
    PendingWorkflowTransition transition;
    {
        const juce::ScopedLock lock (selectionLock);
        if (workflowState.mode != WorkflowResumeState::Mode::review
            || workflowState.reviewId != definition->id || receiverId.isEmpty()
            || activeWorkflow != definition || pendingWorkflowTransition.has_value()) return false;
        request.runtimeInstanceId = receiverId.toLowerCase();
        request.reviewId = workflowState.reviewId;
        request.attemptId = workflowState.attemptId;
        transition.action = action;
        transition.operationId = uuid();
        transition.definition = definition;
        transition.nextIndex = nextIndex;
        pendingWorkflowTransition = transition;
    }
    request.eventId = uuid();
    request.operationId = transition.operationId;
    request.kind = kind;
    request.conditionRevisionId = item.momentRevisionId;
    request.itemId = item.itemId;
    if (action == PendingWorkflowTransition::Action::move
        && nextIndex >= 0 && nextIndex < static_cast<int> (definition->items.size()))
        request.nextItemId = definition->items[static_cast<size_t> (nextIndex)].itemId;
    else if (action == PendingWorkflowTransition::Action::checkpointOnly)
        request.nextItemId = item.itemId;
    request.confirmation = kind == "confirmed" ? "confirmed"
        : kind == "deferred" ? "deferred" : "none";
    request.artifactSha256 = definition->sha256;
    if (check.appendWorkflowEvent (std::move (request)))
    {
        if (stateChanged) stateChanged();
        return true;
    }
    {
        const juce::ScopedLock lock (selectionLock);
        if (pendingWorkflowTransition
            && pendingWorkflowTransition->operationId == transition.operationId)
            pendingWorkflowTransition.reset();
    }
    return false;
}

void ReferenceComparisonController::workflowCommitted (const WorkflowEventCommit& commit)
{
    if (! commit.committed || ! acceptWorkflowCallbacks.load (std::memory_order_acquire)) return;
    std::optional<PendingWorkflowTransition> transition;
    {
        const juce::ScopedLock lock (selectionLock);
        if (workflowState.mode != WorkflowResumeState::Mode::review
            || workflowState.attemptId != commit.attemptId) return;
        workflowState.journalHeadSha256 = commit.artifactSha256;
        workflowState.checkpointId = commit.checkpointId;
        if (pendingWorkflowTransition
            && pendingWorkflowTransition->operationId == commit.operationId)
        {
            transition = pendingWorkflowTransition;
            pendingWorkflowTransition.reset();
        }
    }
    if (transition && transition->action == PendingWorkflowTransition::Action::move)
        prepareWorkflowItem (transition->definition, transition->nextIndex);
    else if (transition && transition->action == PendingWorkflowTransition::Action::finish)
        applyWorkflowFinish();
    if (stateChanged) stateChanged();
}

bool ReferenceComparisonController::prepareWorkflowItem (
    const std::shared_ptr<const WorkflowDefinition>& definition, int index)
{
    if (definition == nullptr || index < 0
        || index >= static_cast<int> (definition->items.size())) return false;
    const auto& item = definition->items[static_cast<size_t> (index)];
    const auto token = definition->revisionId + ":" + item.itemId;
    selectA();
    capture.access->capturedView = false;
    viewedSlot.store (2, std::memory_order_release);
    if (! check.selectWorkflowCondition (item.condition, token)) return false;
    const juce::ScopedLock lock (selectionLock);
    if (activeWorkflow != definition) return false;
    workflowItemIndex = index;
    workflowState.conditionRevisionId = item.momentRevisionId;
    return true;
}

bool ReferenceComparisonController::startWorkflow (
    std::shared_ptr<const WorkflowDefinition> definition, int itemIndex)
{
    if (definition == nullptr || definition->items.empty() || trialActive()) return false;
    const auto normal = check.savedChoice();
    {
        const juce::ScopedLock lock (selectionLock);
        if (activeWorkflow == nullptr) normalCheckChoice = normal;
        activeWorkflow = definition;
        workflowItemIndex = itemIndex;
        workflowState = {};
        workflowState.mode = definition->kind == WorkflowDefinition::Kind::review
            ? WorkflowResumeState::Mode::review : WorkflowResumeState::Mode::bookmark;
        workflowState.returnSlot = viewedSlot.load (std::memory_order_acquire);
        if (workflowState.mode == WorkflowResumeState::Mode::review)
        {
            workflowState.reviewId = definition->id;
            workflowState.reviewRevisionId = definition->revisionId;
            workflowState.attemptId = uuid();
        }
        else workflowState.bookmarkId = definition->id;
    }
    if (prepareWorkflowItem (definition, itemIndex))
    {
        if (definition->kind != WorkflowDefinition::Kind::review
            || appendWorkflowEvent ("started", definition->items[static_cast<size_t> (itemIndex)],
                                    definition, PendingWorkflowTransition::Action::checkpointOnly))
        { if (stateChanged) stateChanged(); return true; }
    }
    applyWorkflowFinish();
    return false;
}

bool ReferenceComparisonController::startLatestReview()
{
    const auto state = check.snapshot();
    if (state.workflowCatalog == nullptr || state.workflowCatalog->latestReview == nullptr) return false;
    const auto definition = state.workflowCatalog->latestReview;
    int index = 0;
    {
        const juce::ScopedLock lock (selectionLock);
        if (workflowState.mode == WorkflowResumeState::Mode::review)
        {
            if (workflowState.reviewId != definition->id
                || workflowState.reviewRevisionId != definition->revisionId) return false;
            for (size_t i = 0; i < definition->items.size(); ++i)
                if (definition->items[i].momentRevisionId == workflowState.conditionRevisionId)
                    index = static_cast<int> (i);
        }
    }
    return startWorkflow (definition, index);
}

bool ReferenceComparisonController::startLatestBookmark()
{
    const auto state = check.snapshot();
    if (state.workflowCatalog == nullptr || state.workflowCatalog->latestBookmark == nullptr) return false;
    const auto definition = state.workflowCatalog->latestBookmark;
    {
        const juce::ScopedLock lock (selectionLock);
        if (workflowState.mode == WorkflowResumeState::Mode::bookmark
            && workflowState.bookmarkId != definition->id) return false;
    }
    return startWorkflow (definition);
}

bool ReferenceComparisonController::moveWorkflow (
    int direction, bool confirmed, bool deferred)
{
    std::shared_ptr<const WorkflowDefinition> definition;
    WorkflowItem current;
    int next = 0;
    {
        const juce::ScopedLock lock (selectionLock);
        definition = activeWorkflow;
        next = workflowItemIndex + direction;
        if (definition != nullptr && workflowItemIndex >= 0
            && workflowItemIndex < static_cast<int> (definition->items.size()))
            current = definition->items[static_cast<size_t> (workflowItemIndex)];
    }
    if (definition == nullptr || direction == 0) return false;
    const auto eventKind = confirmed ? juce::String ("confirmed")
        : deferred ? juce::String ("deferred") : juce::String ("moved");
    if (next < 0) next = 0;
    if (next >= static_cast<int> (definition->items.size()))
        return definition->kind == WorkflowDefinition::Kind::review
            ? appendWorkflowEvent (eventKind, current, definition,
                                   PendingWorkflowTransition::Action::finish)
            : finishWorkflow (true);
    if (definition->kind == WorkflowDefinition::Kind::review)
    {
        selectA();
        return appendWorkflowEvent (eventKind, current, definition,
                                    PendingWorkflowTransition::Action::move, next);
    }
    const bool moved = prepareWorkflowItem (definition, next);
    if (moved && stateChanged) stateChanged();
    return moved;
}

void ReferenceComparisonController::endWorkflow()
{
    finishWorkflow (true);
}

void ReferenceComparisonController::applyWorkflowFinish()
{
    ReferenceChoice restore;
    int slot = 2;
    {
        const juce::ScopedLock lock (selectionLock);
        if (activeWorkflow == nullptr && workflowState.mode == WorkflowResumeState::Mode::idle) return;
        restore = normalCheckChoice;
        slot = workflowState.returnSlot;
        activeWorkflow.reset();
        pendingWorkflowTransition.reset();
        workflowItemIndex = 0;
        workflowState = {};
    }
    selectA();
    check.restoreChoice (restore);
    viewedSlot.store (slot == 1 ? 1 : 2, std::memory_order_release);
    if (stateChanged) stateChanged();
}

bool ReferenceComparisonController::finishWorkflow (bool completed)
{
    std::shared_ptr<const WorkflowDefinition> definition;
    WorkflowItem current;
    {
        const juce::ScopedLock lock (selectionLock);
        if (activeWorkflow == nullptr && workflowState.mode == WorkflowResumeState::Mode::idle)
            return true;
        definition = activeWorkflow;
        if (definition != nullptr && workflowItemIndex >= 0
            && workflowItemIndex < static_cast<int> (definition->items.size()))
            current = definition->items[static_cast<size_t> (workflowItemIndex)];
    }
    if (definition != nullptr && definition->kind == WorkflowDefinition::Kind::review)
    {
        selectA();
        return appendWorkflowEvent (completed ? "completed" : "paused", current, definition,
                                    PendingWorkflowTransition::Action::finish);
    }
    applyWorkflowFinish();
    return true;
}
}
