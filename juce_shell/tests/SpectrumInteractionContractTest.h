#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../src/HyphaSpectrumComponent.h"

namespace hypha::tests
{
    void verifySpectrumInteractionContract (SpectrumComponent& spectrum,
                                            const KirinSpectrumView& snapshot,
                                            int width,
                                            int height,
                                            juce::Time eventTime);
}
