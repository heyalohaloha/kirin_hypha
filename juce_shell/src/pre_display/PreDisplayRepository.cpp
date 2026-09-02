#include "PreDisplayRepository.h"

#include <set>
#include <utility>

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

        bool validExactClearAuthority (const juce::File& file,
                                       const RuntimeIdentity& identity)
        {
            const auto value = parseBoundedJson (file, maxPointerBytes);
            const auto* clear = value.getDynamicObject();
            const bool post = identity.role == GuideTargetRole::post;
            return clear != nullptr
                && (post
                    ? exactProperties (*clear, { "format", "version", "scope", "group_id",
                                                 "target_role", "binding_id", "work_id",
                                                 "retired_at" })
                    : exactProperties (*clear, { "format", "version", "scope", "group_id",
                                                 "binding_id", "work_id", "retired_at" }))
                && objectString (*clear, "format") == "kirin_pre_display_retired"
                && objectString (*clear, "version") == (post ? "3.0" : "2.0")
                && objectString (*clear, "scope") == "binding"
                && objectString (*clear, "group_id") == "kirin_os"
                && (! post || objectString (*clear, "target_role") == "post")
                && objectString (*clear, "binding_id") == identity.bindingId
                && (clear->getProperty ("work_id").isVoid()
                    || objectString (*clear, "work_id") == identity.workId)
                && canonicalIsoInstant (objectString (*clear, "retired_at"));
        }
    }

    ConnectionRequest GuideRepository::pendingConnection (
        std::int64_t nowMs, const RuntimeIdentity& identity) const
    {
        const auto value = parseBoundedJson (
            root.getChildFile ("connection").getChildFile ("request.json"), maxPointerBytes);
        const auto* request = value.getDynamicObject();
        ConnectionRequest result;
        const bool post = identity.role == GuideTargetRole::post;
        if (request == nullptr
            || (post
                ? ! exactProperties (*request, { "format", "version", "target_role",
                                                  "binding_id", "work_id", "work_title",
                                                  "observed_at_ms", "expires_at_ms" })
                : ! exactProperties (*request, { "format", "version", "binding_id", "work_id",
                                                  "work_title", "observed_at_ms", "expires_at_ms" }))
            || objectString (*request, "format") != "kirin_pre_display_connection_request"
            || objectString (*request, "version") != (post ? "2.0" : "1.0")
            || (post && objectString (*request, "target_role") != "post")
            || ! objectInteger (*request, "observed_at_ms", 0, maxSafeJsonInteger, result.observedAtMs)
            || ! objectInteger (*request, "expires_at_ms", 0, maxSafeJsonInteger, result.expiresAtMs))
            return {};
        result.targetRole = identity.role;
        result.bindingId = objectString (*request, "binding_id");
        result.workId = objectString (*request, "work_id");
        result.workTitle = objectString (*request, "work_title");
        if (! safeId (result.bindingId) || ! safeId (result.workId)
            || ! request->getProperty ("work_title").isString()
            || result.workTitle.length() > 96 || result.workTitle != result.workTitle.trim()
            || result.expiresAtMs - result.observedAtMs > 5 * 60 * 1000
            || ! result.validAt (nowMs))
            return {};
        return result;
    }

    GuideReceipt GuideRepository::refresh (GuideModel& retainedGuide,
                                            const RuntimeIdentity& identity) const
    {
        if (! safeId (identity.runtimeInstanceId) || ! safeId (identity.workId)
            || ! safeId (identity.bindingId))
        {
            retainedGuide = {};
            return {};
        }
        const bool post = identity.role == GuideTargetRole::post;
        const juce::String expectedVersion = post ? "3.0" : "2.0";
        // A Hypha instance may be explicitly reconnected from one Work to another while its
        // process remains alive. Never project the previous Work's retained guide under the
        // new binding, even when the new pointer has not been published yet.
        if (retainedGuide.valid()
            && (retainedGuide.targetRole != identity.role
                || retainedGuide.runtimeInstanceId != identity.runtimeInstanceId
                || retainedGuide.workId != identity.workId
                || retainedGuide.bindingId != identity.bindingId))
            retainedGuide = {};
        if (validExactClearAuthority (
                root.getChildFile ("retired_exact").getChildFile (identity.bindingId + ".clear.json"),
                identity))
        {
            retainedGuide = {};
            GuideReceipt receipt;
            receipt.state = GuideRefreshState::cleared;
            receipt.targetRole = identity.role;
            return receipt;
        }

        const auto pointerValue = parseBoundedJson (
            root.getChildFile ("active_exact").getChildFile (identity.bindingId + ".json"),
            maxPointerBytes);
        const auto* pointer = pointerValue.getDynamicObject();
        if (pointer == nullptr
            || (post
                ? ! exactProperties (*pointer, { "format", "version", "group_id", "target_role",
                                                  "work_id", "binding_id", "runtime_instance_id",
                                                  "guide_id", "revision", "content_hash",
                                                  "artifact_sha256", "guide_file", "payload_kind",
                                                  "activated_at" })
                : ! exactProperties (*pointer, { "format", "version", "group_id", "work_id",
                                                  "binding_id", "runtime_instance_id", "guide_id",
                                                  "revision", "content_hash", "artifact_sha256",
                                                  "guide_file", "payload_kind", "activated_at" }))
            || objectString (*pointer, "format") != "kirin_pre_display_active"
            || objectString (*pointer, "version") != expectedVersion
            || (post && objectString (*pointer, "target_role") != "post"))
            return {};

        const auto guideFileName = objectString (*pointer, "guide_file");
        const auto artifactHash = objectString (*pointer, "artifact_sha256");
        const auto guideId = objectString (*pointer, "guide_id");
        const auto contentHash = objectString (*pointer, "content_hash");
        const auto pointerGroupId = objectString (*pointer, "group_id");
        const auto pointerPayloadKind = objectString (*pointer, "payload_kind");
        const auto pointerWorkId = objectString (*pointer, "work_id");
        const auto pointerBindingId = objectString (*pointer, "binding_id");
        const auto pointerRuntimeId = objectString (*pointer, "runtime_instance_id");
        const auto activatedAt = objectString (*pointer, "activated_at");
        const auto cacheKey = guideFileName + ":" + artifactHash;
        std::int64_t revision = 0;
        if (! safeGuideFileName (guideFileName) || ! safeHash (artifactHash)
            || ! safeId (guideId) || ! safeHash (contentHash)
            || ! canonicalIsoInstant (activatedAt)
            || pointerGroupId != "kirin_os"
            || pointerWorkId != identity.workId
            || pointerBindingId != identity.bindingId
            || pointerRuntimeId != identity.runtimeInstanceId
            || (pointerPayloadKind != "masking" && pointerPayloadKind != "inspect")
            || ! objectInteger (*pointer, "revision", 1, maxSafeJsonInteger, revision))
            return {};

        GuideReceipt receipt;
        receipt.state = GuideRefreshState::rejected;
        receipt.targetRole = identity.role;
        receipt.groupId = pointerGroupId;
        receipt.workId = pointerWorkId;
        receipt.bindingId = pointerBindingId;
        receipt.runtimeInstanceId = pointerRuntimeId;
        receipt.guideId = guideId;
        receipt.contentHash = contentHash;
        receipt.payloadKind = pointerPayloadKind;
        receipt.revision = revision;

        if (retainedGuide.valid() && retainedGuide.cacheKey == cacheKey
            && retainedGuide.guideId == guideId && retainedGuide.contentHash == contentHash
            && retainedGuide.targetRole == identity.role
            && retainedGuide.workId == pointerWorkId && retainedGuide.bindingId == pointerBindingId
            && retainedGuide.runtimeInstanceId == pointerRuntimeId
            && retainedGuide.payloadKind == pointerPayloadKind && retainedGuide.revision == revision)
        {
            receipt.state = GuideRefreshState::accepted;
            return receipt;
        }

        const auto guideFile = root.getChildFile ("guides").getChildFile (guideFileName);
        juce::MemoryBlock guideBytes;
        if (! readBoundedFile (guideFile, maxGuideBytes, guideBytes)
            || juce::SHA256 (guideBytes).toHexString() != artifactHash)
            return receipt;
        const auto guideValue = parseJson (guideBytes);
        const auto* guide = guideValue.getDynamicObject();
        GuideModel parsed;
        if (guide == nullptr || objectString (*guide, "guide_id") != guideId
            || objectString (*guide, "content_hash") != contentHash
            || ! parseArtifactVerifiedGuideModel (*guide, cacheKey, parsed)
            || parsed.protocolVersion != expectedVersion || parsed.targetRole != identity.role
            || parsed.groupId != pointerGroupId
            || parsed.workId != identity.workId || parsed.bindingId != identity.bindingId
            || parsed.runtimeInstanceId != identity.runtimeInstanceId
            || parsed.payloadKind != pointerPayloadKind || parsed.revision != revision)
            return receipt;
        retainedGuide = std::move (parsed);
        receipt.state = GuideRefreshState::accepted;
        return receipt;
    }

    GuideReceipt GuideRepository::refresh (GuideModel& retainedGuide) const
    {
        if (validClearAuthority (root.getChildFile ("retired")
                                    .getChildFile ("kirin_os.clear.json")))
        {
            retainedGuide = {};
            GuideReceipt receipt;
            receipt.state = GuideRefreshState::cleared;
            return receipt;
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
            return {};

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
            return {};

        GuideReceipt receipt;
        receipt.state = GuideRefreshState::rejected;
        receipt.groupId = pointerGroupId;
        receipt.guideId = guideId;
        receipt.contentHash = contentHash;
        receipt.payloadKind = pointerPayloadKind;
        receipt.revision = revision;

        if (retainedGuide.valid() && retainedGuide.cacheKey == cacheKey
            && retainedGuide.guideId == guideId
            && retainedGuide.contentHash == contentHash
            && retainedGuide.groupId == pointerGroupId
            && retainedGuide.payloadKind == pointerPayloadKind
            && retainedGuide.revision == revision)
        {
            receipt.state = GuideRefreshState::accepted;
            return receipt;
        }

        const auto guideFile = root.getChildFile ("guides").getChildFile (guideFileName);
        juce::MemoryBlock guideBytes;
        if (! readBoundedFile (guideFile, maxGuideBytes, guideBytes)
            || juce::SHA256 (guideBytes).toHexString() != artifactHash)
            return receipt;
        const auto guideValue = parseJson (guideBytes);
        const auto* guide = guideValue.getDynamicObject();
        GuideModel parsed;
        if (guide == nullptr || objectString (*guide, "guide_id") != guideId
            || objectString (*guide, "content_hash") != contentHash
            || ! parseArtifactVerifiedGuideModel (*guide, cacheKey, parsed)
            || parsed.groupId != "kirin_os" || parsed.groupId != pointerGroupId
            || parsed.payloadKind != pointerPayloadKind || parsed.revision != revision)
            return receipt;
        retainedGuide = std::move (parsed);
        receipt.state = GuideRefreshState::accepted;
        // Corrupt or momentarily missing transport files intentionally leave the
        // last valid guide untouched. Only a valid group clear removes it.
        return receipt;
    }
}
