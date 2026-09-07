#pragma once

#include "ReferenceRuntimeV2Model.h"

namespace hypha::reference_audition::runtime_v2_parsing
{
bool parseSourcePresetReceipt (const juce::var&, RuntimeSourcePresetReceipt&);
bool parseGlobalPresetCatalog (const juce::var&, RuntimeGlobalPresetCatalog&);
bool parsePresetReceipt (const juce::var&, const juce::String& workId,
                         RuntimePresetReceipt&);
}
