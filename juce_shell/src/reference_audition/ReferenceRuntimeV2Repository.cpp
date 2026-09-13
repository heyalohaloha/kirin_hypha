#include "ReferenceRuntimeV2Repository.h"
#include "ReferenceRuntimeRepositoryParsing.h"
#include "ReferenceRuntimeV2PresetParsing.h"
#include "ReferenceRuntimePendingPresets.h"

#include <algorithm>
#include <limits>
#include <regex>
#include <set>
#include <utility>

#include <juce_cryptography/juce_cryptography.h>

namespace hypha::reference_audition
{
    using namespace runtime_repository_parsing;

    RuntimeV2Repository::RuntimeV2Repository (juce::File transportRootIn)
        : root (std::move (transportRootIn))
    {
    }

    juce::File RuntimeV2Repository::transportRoot()
    {
       #if JUCE_WINDOWS
        auto local = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
        return local.getChildFile ("Kirin OS").getChildFile ("plugin_data")
                    .getChildFile ("reference").getChildFile ("v2");
       #else
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Library").getChildFile ("Application Support")
            .getChildFile ("Kirin OS").getChildFile ("plugin_data")
            .getChildFile ("reference").getChildFile ("v2");
       #endif
    }

    RuntimeWorkspaceLoadResult RuntimeV2Repository::refresh (
        const juce::String& workId,
        std::shared_ptr<const RuntimeWorkspace> previous) const
    {
        if (! workUuid (workId) || root == juce::File()
            || (previous != nullptr && previous->manifest.workId != workId))
            return failure ("reference_work_invalid", {});
        const auto manifestFile = root.getChildFile ("manifests").getChildFile (workId + ".json");
        if (! manifestFile.exists())
        {
            if (previous != nullptr)
                return failure ("reference_manifest_missing", std::move (previous));
            return {};
        }
        juce::MemoryBlock manifestBytes;
        juce::var manifestJson;
        RuntimeManifest manifest;
        if (! readJson (manifestFile, maximumManifestBytes, manifestBytes, manifestJson)
            || (manifestBytes.getSize() > 64 * 1024 && manifestJson["version"] != "4.0")
            || ! parseManifest (manifestJson, workId, manifest))
            return failure ("reference_manifest_rejected", std::move (previous));
        if (previous != nullptr)
        {
            if (manifest.revision < previous->manifest.revision)
                return failure ("reference_manifest_rollback", std::move (previous));
            if (manifest.revision == previous->manifest.revision)
                return { RuntimeWorkspaceLoadState::unchanged, std::move (previous), {} };
        }

        auto workspace = std::make_shared<RuntimeWorkspace>();
        workspace->manifest = manifest;
        const auto catalogFile = root.getChildFile ("global_preset_catalogs")
                                   .getChildFile (
                                       manifest.globalPresetCatalogArtifact.sha256 + ".json");
        juce::MemoryBlock catalogBytes;
        juce::var catalogJson;
        if (! readJson (catalogFile, maximumGlobalPresetCatalogBytes,
                        catalogBytes, catalogJson)
            || catalogBytes.getSize()
                   != static_cast<size_t> (manifest.globalPresetCatalogArtifact.bytes)
            || juce::SHA256 (catalogBytes).toHexString()
                   != manifest.globalPresetCatalogArtifact.sha256
            || ! runtime_v2_parsing::parseGlobalPresetCatalog (
                    catalogJson, workspace->globalPresetCatalog))
            return failure ("reference_global_preset_catalog_rejected",
                            std::move (previous));
        for (const auto& receipt : manifest.presetArtifacts)
        {
            const auto file = root.getChildFile ("presets").getChildFile (workId)
                                  .getChildFile (receipt.presetId + ".json");
            juce::MemoryBlock bytes;
            juce::var json;
            if (! readJson (file, maximumPresetBytes, bytes, json)
                || bytes.getSize() != static_cast<size_t> (receipt.bytes)
                || juce::SHA256 (bytes).toHexString() != receipt.sha256)
                return failure ("reference_preset_receipt_rejected", std::move (previous));
            RuntimePreset preset;
            if (! parsePreset (json, receipt, workId, preset))
                return failure ("reference_preset_contract_rejected", std::move (previous));
            workspace->presets.push_back (std::move (preset));
        }
        return { RuntimeWorkspaceLoadState::updated,
                 std::shared_ptr<const RuntimeWorkspace> (std::move (workspace)), {} };
    }
}
