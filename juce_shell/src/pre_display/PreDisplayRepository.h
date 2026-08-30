#pragma once

#include <utility>

#include <juce_core/juce_core.h>

#include "PreDisplayModel.h"

namespace hypha::pre_display
{
    class GuideRepository final
    {
    public:
        explicit GuideRepository (juce::File transportRootIn)
            : root (std::move (transportRootIn)) {}

        GuideReceipt refresh (GuideModel& retainedGuide) const;
        GuideReceipt refresh (GuideModel& retainedGuide, const RuntimeIdentity& identity) const;
        ConnectionRequest pendingConnection (std::int64_t nowMs) const;

    private:
        juce::File root;
    };
}
