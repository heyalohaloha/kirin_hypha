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
bool parseLibraryVersionCandidate (const juce::var&, RuntimeCandidate&);
// `skipped` があれば（library の Preset）、受け付けない候補曲はそれだけを外してここに足す（無ければ 1 つでも断る）。
bool parsePreset (const juce::var&, const RuntimePresetReceipt&, const juce::String&, RuntimePreset&, bool library = false,
                  std::vector<RuntimeSkippedItem>* skipped = nullptr);
// 受け付けなかった項目の名前（`nameProperty`。画面に出せない字は「?」、長ければ切る）と、名前の字が原因か。
RuntimeSkippedItem skippedItem (const juce::var& item, const char* nameProperty);
RuntimeWorkspaceLoadResult failure (juce::String, std::shared_ptr<const RuntimeWorkspace>);
inline constexpr std::int64_t maximumManifestBytes = 256 * 1024;
inline constexpr std::int64_t maximumGlobalPresetCatalogBytes = 64 * 1024;
inline constexpr std::int64_t maximumPresetBytes = 2 * 1024 * 1024;
}
