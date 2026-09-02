#pragma once

#include <cstdint>
#include <memory>

#include <juce_core/juce_core.h>

#include "pre_display/PreDisplayModel.h"

namespace hypha::capture
{
    struct WorkAttachmentDescriptor
    {
        int pixelWidth = 0;
        int pixelHeight = 0;
        juce::String domain;
        juce::String observationTarget;
        std::int64_t capturedAtMs = 0;

        bool valid() const noexcept;
    };

    enum class WorkAttachmentSubmit
    {
        accepted,
        busy,
        invalidReference,
        invalidCapture,
    };

    enum class WorkAttachmentResultState
    {
        none,
        attached,
        rejected,
    };

    struct WorkAttachmentResult
    {
        WorkAttachmentResultState state = WorkAttachmentResultState::none;
        juce::String requestId;
        juce::String code;

        bool terminal() const noexcept
        {
            return state == WorkAttachmentResultState::attached
                || state == WorkAttachmentResultState::rejected;
        }
    };

    class WorkAttachmentController final : private juce::Thread
    {
    public:
        explicit WorkAttachmentController (juce::File transportRootIn = transportRoot());
        ~WorkAttachmentController() override;

        WorkAttachmentSubmit submit (const pre_display::WorkReference& expectedWork,
                                     juce::MemoryBlock pngBytes,
                                     WorkAttachmentDescriptor descriptor);
        WorkAttachmentResult takeResult();

        static juce::File transportRoot();

    private:
        struct Job;

        void run() override;
        bool publish (Job&);
        WorkAttachmentResult readReceipt (const Job&) const;
        void finish (WorkAttachmentResult);
        void cleanup (const Job&, bool removeReceipt) const;

        const juce::File root;
        mutable juce::CriticalSection stateLock;
        std::unique_ptr<Job> pending;
        WorkAttachmentResult completion;
        bool inFlight = false;
    };
}
