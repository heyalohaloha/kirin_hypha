#pragma once
#include "HyphaAttackV2Presentation.h"
#include "HyphaAttackV2Geometry.h"
#include "HyphaKeyLight.h"

namespace hypha::attack_v2
{
std::vector<LocatedEvent> visibleEvents (const State&, const Geometry&);
void paintV2 (juce::Graphics&, const State&, const Geometry&, const presentation::Context&);
void paintEnvelopes (juce::Graphics&, const Presentation&, const Geometry&,
                     const presentation::Context&, bool overlay);
void paintEvidence (juce::Graphics&, const Presentation&, juce::Rectangle<int>,
                    const presentation::Context&, int scrollOffset);
}
