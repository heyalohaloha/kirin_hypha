#include "PreDisplayRepository.h"

#include <utility>
#include <set>

#include <juce_cryptography/juce_cryptography.h>

#include "PreDisplayProtocol.h"

namespace hypha::pre_display
{
    namespace
    {
        bool exactProperties (const juce::DynamicObject& object,
                              std::initializer_list<const char*> names)
        {
            std::set<std::string> expected;
            for (const auto* name : names)
                expected.emplace (name);
            const auto& properties = object.getProperties();
            if (properties.size() != static_cast<int> (expected.size()))
                return false;
            for (int index = 0; index < properties.size(); ++index)
                if (expected.find (properties.getName (index).toString().toStdString()) == expected.end())
                    return false;
            return true;
        }

        bool readBoundedFile (const juce::File& file, std::int64_t maximumBytes,
                              juce::MemoryBlock& bytes)
        {
            if (! file.existsAsFile() || file.isSymbolicLink())
                return false;
            const auto expectedSize = file.getSize();
            if (expectedSize <= 0 || expectedSize > maximumBytes)
                return false;
            auto input = file.createInputStream();
            if (input == nullptr || ! input->openedOk())
                return false;
            juce::MemoryBlock candidate;
            const auto bytesRead = input->readIntoMemoryBlock (candidate, maximumBytes + 1);
            if (bytesRead != static_cast<std::size_t> (expectedSize)
                || candidate.getSize() != static_cast<std::size_t> (expectedSize))
                return false;
            bytes = std::move (candidate);
            return true;
        }

        juce::var parseJson (const juce::MemoryBlock& bytes)
        {
            return juce::JSON::parse (juce::String::fromUTF8 (
                static_cast<const char*> (bytes.getData()), static_cast<int> (bytes.getSize())));
        }

        juce::var parseBoundedJson (const juce::File& file, std::int64_t maximumBytes)
        {
            juce::MemoryBlock bytes;
            if (! readBoundedFile (file, maximumBytes, bytes))
                return {};
            return parseJson (bytes);
        }

        bool validClearAuthority (const juce::File& file)
        {
            const auto value = parseBoundedJson (file, maxPointerBytes);
            const auto* clear = value.getDynamicObject();
            return clear != nullptr
                && exactProperties (*clear, { "format", "version", "scope", "group_id",
                                              "retired_at" })
                && objectString (*clear, "format") == "kirin_pre_display_retired"
                && objectString (*clear, "version") == "1.0"
                && objectString (*clear, "scope") == "group"
                && objectString (*clear, "group_id") == "kirin_os"
                && canonicalIsoInstant (objectString (*clear, "retired_at"));
        }
    }

    void GuideRepository::refresh (GuideModel& retainedGuide) const
    {
        if (validClearAuthority (root.getChildFile ("retired")
                                    .getChildFile ("kirin_os.clear.json")))
        {
            retainedGuide = {};
            return;
        }

        const auto pointerValue = parseBoundedJson (
            root.getChildFile ("active").getChildFile ("kirin_os.json"), maxPointerBytes);
        const auto* pointer = pointerValue.getDynamicObject();
        if (pointer == nullptr
            || ! exactProperties (*pointer, { "format", "version", "group_id", "guide_id",
                                              "revision", "content_hash", "artifact_sha256",
                                              "guide_file", "payload_kind", "activated_at" })
            || objectString (*pointer, "format") != "kirin_pre_display_active"
            || objectString (*pointer, "version") != "1.0")
            return;

        const auto guideFileName = objectString (*pointer, "guide_file");
        const auto artifactHash = objectString (*pointer, "artifact_sha256");
        const auto guideId = objectString (*pointer, "guide_id");
        const auto contentHash = objectString (*pointer, "content_hash");
        const auto pointerGroupId = objectString (*pointer, "group_id");
        const auto pointerPayloadKind = objectString (*pointer, "payload_kind");
        const auto activatedAt = objectString (*pointer, "activated_at");
        const auto cacheKey = guideFileName + ":" + artifactHash;
        std::int64_t revision = 0;
        if (! safeGuideFileName (guideFileName) || ! safeHash (artifactHash)
            || ! safeId (guideId) || ! safeHash (contentHash)
            || ! canonicalIsoInstant (activatedAt)
            || pointerGroupId != "kirin_os"
            || (pointerPayloadKind != "masking" && pointerPayloadKind != "inspect")
            || ! objectInteger (*pointer, "revision", 1, maxSafeJsonInteger, revision))
            return;

        if (retainedGuide.valid() && retainedGuide.cacheKey == cacheKey
            && retainedGuide.guideId == guideId
            && retainedGuide.contentHash == contentHash
            && retainedGuide.groupId == pointerGroupId
            && retainedGuide.payloadKind == pointerPayloadKind
            && retainedGuide.revision == revision)
            return;

        const auto guideFile = root.getChildFile ("guides").getChildFile (guideFileName);
        juce::MemoryBlock guideBytes;
        if (! readBoundedFile (guideFile, maxGuideBytes, guideBytes)
            || juce::SHA256 (guideBytes).toHexString() != artifactHash)
            return;
        const auto guideValue = parseJson (guideBytes);
        const auto* guide = guideValue.getDynamicObject();
        GuideModel parsed;
        if (guide == nullptr || objectString (*guide, "guide_id") != guideId
            || objectString (*guide, "content_hash") != contentHash
            || ! parseGuideModel (*guide, cacheKey, parsed)
            || parsed.groupId != "kirin_os" || parsed.groupId != pointerGroupId
            || parsed.payloadKind != pointerPayloadKind || parsed.revision != revision)
            return;
        retainedGuide = std::move (parsed);
        // Corrupt or momentarily missing transport files intentionally leave the
        // last valid guide untouched. Only a valid group clear removes it.
    }
}
