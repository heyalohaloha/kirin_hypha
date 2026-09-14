#pragma once

#include "ReferenceWorkflowModel.h"

namespace hypha::reference_audition
{
class ReferenceWorkflowRepository final
{
public:
    explicit ReferenceWorkflowRepository (juce::File runtimeRoot);
    std::shared_ptr<const WorkflowCatalog> refresh (
        std::shared_ptr<const WorkflowCatalog> previous = {}) const;
    WorkflowEventCommit appendReviewEvent (const WorkflowEventRequest&) const;

private:
    juce::File root;
};
}
