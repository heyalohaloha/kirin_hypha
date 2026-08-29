#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"

namespace hypha
{
class AbsoluteComponent final : public juce::Component
{
public:
    AbsoluteComponent();

    void setBatch (const KirinAbsoluteBatch& next);
    void setBatchAt (const KirinAbsoluteBatch& next, double nowMs);
    void clearSnapshot();
    void setAnalysisOwnerNames (const juce::String& names);
    void paint (juce::Graphics&) override;

    uint32_t frameCountForTest() const noexcept { return batch.count; }
    int64_t newestEndpointForTest() const noexcept
    {
        return batch.count == 0u ? 0 : batch.latest.presentation_end_samples;
    }
    uint64_t curvePresentationCountForTest() const noexcept
    {
        return curvePresentationCount;
    }
    uint64_t numericPresentationCountForTest() const noexcept
    {
        return numericPresentationCount;
    }

private:
    KirinAbsoluteBatch batch {};
    KirinAbsoluteBatch pendingBatch {};
    KirinAbsoluteView numericSnapshot {};
    bool haveBatch = false;
    bool havePendingBatch = false;
    bool haveNumericSnapshot = false;
    juce::String analysisOwnerNames;
    double lastCurvePresentationMs = 0.0;
    double lastNumericPresentationMs = 0.0;
    uint64_t curvePresentationCount = 0u;
    uint64_t numericPresentationCount = 0u;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AbsoluteComponent)
};
}
