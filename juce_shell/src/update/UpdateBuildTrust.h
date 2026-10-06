#pragma once
#include <juce_core/juce_core.h>
namespace hypha::update
{
// Public build input, not a private credential. The retained marker ties every
// actual format/architecture payload to the release-approved verification key.
juce::String productionPublicKey();
}
