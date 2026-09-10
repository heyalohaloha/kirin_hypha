#pragma once
#include "ReferenceRuntimeV2Model.h"
#include "ReferenceRuntimeV2PresetParsing.h"
#include <set>

namespace hypha::reference_audition
{
    inline bool runtimeManifestKeys (const juce::DynamicObject& object)
    {
        const bool progressive = object.getProperty ("version") == "4.0";
        if ((! progressive && object.getProperty ("version") != "3.0")
            || object.getProperties().size() != (progressive ? 9 : 8)) return false;
        for (const auto* field : { "format", "version", "work_id", "revision", "source_state_artifact",
                                  "global_preset_catalog_artifact", "active_preset", "preset_artifacts" })
            if (! object.hasProperty (field)) return false;
        return ! progressive || object.hasProperty ("pending_presets");
    }

    inline bool parseRuntimePendingPresets (const juce::DynamicObject& object, RuntimeManifest& manifest)
    {
        if (object.getProperty ("version") != "4.0") return true;
        const auto* values = object.getProperty ("pending_presets").getArray();
        if (values == nullptr || static_cast<size_t> (values->size()) + manifest.presetArtifacts.size() > 128)
            return false;
        std::set<juce::String> ids, revisions;
        for (const auto& ready : manifest.presetArtifacts) { ids.insert (ready.presetId); revisions.insert (ready.revisionId); }
        for (const auto& value : *values)
        {
            const auto* item = value.getDynamicObject();
            RuntimePendingPreset pending;
            if (item == nullptr || item->getProperties().size() != 3
                || ! item->getProperty ("name").isString()
                || ! runtime_v2_parsing::parseSourcePresetReceipt (item->getProperty ("source_template_artifact"), pending.sourceTemplateArtifact)
                || ! runtime_v2_parsing::parseSourcePresetReceipt (item->getProperty ("source_preset_artifact"), pending.sourcePresetArtifact)) return false;
            pending.name = item->getProperty ("name").toString();
            const auto& source = pending.sourcePresetArtifact;
            if (pending.name.isEmpty() || pending.name.length() > 80 || pending.name.trim() != pending.name
                || source.presetId != pending.sourceTemplateArtifact.presetId
                || source.revisionId == pending.sourceTemplateArtifact.revisionId
                || ! ids.insert (source.presetId).second || ! revisions.insert (source.revisionId).second) return false;
            for (auto character : pending.name)
                if (character < 0x20 || (character >= 0x7f && character <= 0x9f)
                    || character == 0x2028 || character == 0x2029) return false;
            manifest.pendingPresets.push_back (std::move (pending));
        }
        return true;
    }

    inline juce::String runtimePresetOptionIdentity (const juce::String& id)
    {
        return id.startsWith ("work:") ? id.substring (5) : id;
    }
}
