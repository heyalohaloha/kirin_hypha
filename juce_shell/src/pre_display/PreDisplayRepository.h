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

        void refresh (GuideModel& retainedGuide) const;

    private:
        juce::File root;
    };
}
