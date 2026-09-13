#pragma once
#include "ReferenceRuntimeV2Model.h"
namespace hypha::reference_audition::runtime_repository_parsing
{
bool workUuid (const juce::String&);
bool uuidV4 (const juce::String&);
bool sha256 (const juce::String&);
bool exactProperties (const juce::DynamicObject&, std::initializer_list<const char*>);
bool exactInteger (const juce::var&, std::int64_t, std::int64_t, std::int64_t&);
bool readJson (const juce::File&, std::int64_t, juce::MemoryBlock&, juce::var&);
bool parseManifest (const juce::var&, const juce::String&, RuntimeManifest&);
bool parsePreset (const juce::var&, const RuntimePresetReceipt&, const juce::String&, RuntimePreset&, bool library = false);
RuntimeWorkspaceLoadResult failure (juce::String, std::shared_ptr<const RuntimeWorkspace>);
inline constexpr std::int64_t maximumManifestBytes = 256 * 1024;
inline constexpr std::int64_t maximumGlobalPresetCatalogBytes = 64 * 1024;
inline constexpr std::int64_t maximumPresetBytes = 2 * 1024 * 1024;
}
