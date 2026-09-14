#include "ReferenceWorkflowRepository.h"
#include "ReferenceRuntimeEventTransport.h"
#include "ReferenceRuntimeRepositoryParsing.h"
#include "ReferenceWorkflowStorage.h"

#include <algorithm>
#include <set>

#include <juce_cryptography/juce_cryptography.h>

namespace hypha::reference_audition
{
using namespace runtime_repository_parsing;
namespace
{
constexpr std::int64_t indexLimit = 64 * 1024 * 1024;
constexpr std::int64_t headLimit = 16 * 1024;
constexpr std::int64_t artifactLimit = 64 * 1024;

bool text (const juce::var& value, juce::String& out, int bytes, bool empty = false)
{
    if (! value.isString()) return false;
    out = value.toString();
    return (empty || out.isNotEmpty()) && out.getNumBytesAsUTF8() <= static_cast<size_t> (bytes)
        && ! out.containsChar ('\0');
}

bool safeSegment (const juce::String& value)
{
    if (value.isEmpty() || value.length() > 160) return false;
    for (auto c : value)
        if (! juce::CharacterFunctions::isLetterOrDigit (c)
            && c != '.' && c != '_' && c != '-') return false;
    return true;
}

bool boolValue (const juce::var& value, bool& out)
{
    if (! value.isBool()) return false;
    out = static_cast<bool> (value); return true;
}

bool writeOutbox (const juce::File& root, const WorkflowEventRequest& request,
                  const juce::String& relative, const juce::String& hash,
                  std::int64_t bytes, const juce::String& createdAt)
{
    auto receipt = new juce::DynamicObject();
    receipt->setProperty ("relative_path", relative);
    receipt->setProperty ("sha256", hash);
    receipt->setProperty ("bytes", bytes);
    auto marker = new juce::DynamicObject();
    marker->setProperty ("format", "kirin_hypha_reference_workflow_outbox");
    marker->setProperty ("version", "1.0");
    marker->setProperty ("runtime_instance_id", request.runtimeInstanceId);
    marker->setProperty ("operation_id", request.operationId);
    marker->setProperty ("attempt_id", request.attemptId);
    marker->setProperty ("event_id", request.eventId);
    marker->setProperty ("event_artifact", juce::var (receipt));
    marker->setProperty ("created_at", createdAt);
    const auto body = RuntimeEventTransport::canonicalJson (juce::var (marker));
    return body.isNotEmpty() && writeWorkflowDurable (root,
        root.getChildFile ("outbox").getChildFile (request.runtimeInstanceId)
            .getChildFile (request.operationId + ".json"), body, false);
}

bool parseCue (const juce::var& value, WorkflowCondition& out)
{
    const auto* object = value.getDynamicObject();
    std::int64_t start = 0, end = 0, rate = 0;
    return object != nullptr && exactProperties (*object, {
               "cue_id", "label", "start_frame", "end_frame", "sample_rate", "loop_enabled" })
        && text (object->getProperty ("cue_id"), out.cueId, 160)
        && safeSegment (out.cueId)
        && text (object->getProperty ("label"), out.cueLabel, 160)
        && exactInteger (object->getProperty ("start_frame"), 0, 9'007'199'254'740'991LL, start)
        && exactInteger (object->getProperty ("end_frame"), 1, 9'007'199'254'740'991LL, end)
        && exactInteger (object->getProperty ("sample_rate"), 8'000, 768'000, rate)
        && boolValue (object->getProperty ("loop_enabled"), out.cueLoops)
        && end > start && ((out.cueStart = start), (out.cueEnd = end), (out.cueRate = rate), true);
}

bool parseSourceIdentity (const juce::var& value, WorkflowCondition& out)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr) return false;
    juce::String file, pcm;
    if (out.sourceKind == "work_version")
    {
        juce::String work, recording, version;
        if (! exactProperties (*object, { "work_id", "recording_id", "version_id", "sha256_file", "sha256_pcm" })
            || ! text (object->getProperty ("work_id"), work, 36) || ! uuidV4 (work)
            || ! text (object->getProperty ("recording_id"), recording, 36) || ! uuidV4 (recording)
            || ! text (object->getProperty ("version_id"), version, 36) || ! uuidV4 (version)
            || ! text (object->getProperty ("sha256_file"), file, 64) || ! sha256 (file)
            || ! text (object->getProperty ("sha256_pcm"), pcm, 64) || ! sha256 (pcm)) return false;
        out.sourceIdentityKey = work + ":" + recording + ":" + version + ":" + file + ":" + pcm;
        return true;
    }
    if (out.sourceKind == "catalog_track")
    {
        juce::String catalog;
        if (! exactProperties (*object, { "catalog_reference_id", "sha256_file", "sha256_pcm" })
            || ! text (object->getProperty ("catalog_reference_id"), catalog, 128)
            || ! text (object->getProperty ("sha256_file"), file, 64) || ! sha256 (file)
            || ! text (object->getProperty ("sha256_pcm"), pcm, 64) || ! sha256 (pcm)) return false;
        for (auto c : catalog)
            if (! ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) && c != '.' && c != '_'
                && c != '-' && c != ':') return false;
        out.sourceIdentityKey = catalog + ":" + file + ":" + pcm;
        return true;
    }
    return false;
}

bool parseCondition (const juce::var& value, WorkflowCondition& out)
{
    const auto* object = value.getDynamicObject();
    const auto* publication = object != nullptr
        ? object->getProperty ("publication_ref").getDynamicObject() : nullptr;
    juce::String nameSpace;
    return object != nullptr && exactProperties (*object, {
               "comparison", "preset_id", "check_id", "candidate_id", "cue",
               "source_kind", "source_identity", "publication_ref" })
        && text (object->getProperty ("comparison"), out.comparison, 8)
        && (out.comparison == "a_b" || out.comparison == "a_c")
        && text (object->getProperty ("preset_id"), out.presetId, 160) && safeSegment (out.presetId)
        && text (object->getProperty ("check_id"), out.checkId, 160) && safeSegment (out.checkId)
        && text (object->getProperty ("candidate_id"), out.candidateId, 160) && safeSegment (out.candidateId)
        && text (object->getProperty ("source_kind"), out.sourceKind, 32)
        && parseSourceIdentity (object->getProperty ("source_identity"), out)
        && parseCue (object->getProperty ("cue"), out)
        && publication != nullptr && exactProperties (*publication, { "namespace", "revision", "sha256" })
        && text (publication->getProperty ("namespace"), nameSpace, 16) && nameSpace == "legacy"
        && text (publication->getProperty ("revision"), out.presetRevisionId, 160)
        && safeSegment (out.presetRevisionId)
        && text (publication->getProperty ("sha256"), out.presetSha256, 64)
        && sha256 (out.presetSha256);
}

struct Head { juce::String kind, id, revision, sha, relativePath; std::int64_t bytes = 0; };

bool readHead (const juce::File& root, const juce::String& kind,
               const juce::String& id, Head& out)
{
    if (! safeSegment (kind) || ! safeSegment (id)) return false;
    juce::MemoryBlock bytes; juce::var json;
    const auto file = root.getChildFile (kind).getChildFile (id).getChildFile ("head.json");
    const auto* object = (readJson (file, headLimit, bytes, json) ? json.getDynamicObject() : nullptr);
    juce::String format, version, updated;
    if (object == nullptr || ! exactProperties (*object, {
            "format", "version", "kind", "id", "revision", "artifact", "updated_at" })
        || ! text (object->getProperty ("format"), format, 64)
        || format != "kirin_reference_workflow_head"
        || ! text (object->getProperty ("version"), version, 8) || version != "1.0"
        || ! text (object->getProperty ("kind"), out.kind, 16) || out.kind != kind
        || ! text (object->getProperty ("id"), out.id, 160) || out.id != id
        || ! text (object->getProperty ("revision"), out.revision, 160) || ! safeSegment (out.revision)
        || ! text (object->getProperty ("updated_at"), updated, 40)) return false;
    const auto* receipt = object->getProperty ("artifact").getDynamicObject();
    if (receipt == nullptr || ! exactProperties (*receipt, { "relative_path", "sha256", "bytes" })
        || ! text (receipt->getProperty ("relative_path"), out.relativePath, 512)
        || ! text (receipt->getProperty ("sha256"), out.sha, 64) || ! sha256 (out.sha)
        || ! exactInteger (receipt->getProperty ("bytes"), 1, artifactLimit, out.bytes)
        || out.relativePath != kind + "/" + id + "/" + out.revision + "." + out.sha + ".json") return false;
    return true;
}

bool readArtifact (const juce::File& root, const Head& head, juce::var& json)
{
    juce::MemoryBlock bytes;
    const auto file = root.getChildFile (head.relativePath);
    return file.isAChildOf (root) && readJson (file, artifactLimit, bytes, json)
        && static_cast<std::int64_t> (bytes.getSize()) == head.bytes
        && juce::SHA256 (bytes).toHexString() == head.sha;
}

bool parseMoment (const juce::var& json, const juce::String& expectedId,
                  const juce::String& expectedRevision, WorkflowItem& item)
{
    const auto* object = json.getDynamicObject();
    juce::String format, version, created; bool archived = false;
    return object != nullptr && exactProperties (*object, {
               "format", "version", "listening_moment_id", "revision_id", "title",
               "purpose", "condition", "created_at", "archived" })
        && text (object->getProperty ("format"), format, 64) && format == "kirin_reference_listening_moment"
        && text (object->getProperty ("version"), version, 8) && version == "1.0"
        && text (object->getProperty ("listening_moment_id"), item.momentId, 36)
        && item.momentId == expectedId && uuidV4 (item.momentId)
        && text (object->getProperty ("revision_id"), item.momentRevisionId, 36)
        && item.momentRevisionId == expectedRevision && uuidV4 (item.momentRevisionId)
        && text (object->getProperty ("title"), item.title, 240)
        && text (object->getProperty ("purpose"), item.purpose, 24 * 1024, true)
        && text (object->getProperty ("created_at"), created, 40)
        && boolValue (object->getProperty ("archived"), archived) && ! archived
        && parseCondition (object->getProperty ("condition"), item.condition);
}

std::shared_ptr<const WorkflowDefinition> readReview (
    const juce::File& root, const juce::String& id, const juce::String& expectedSha)
{
    Head head; juce::var json;
    if (! readHead (root, "reviews", id, head) || head.sha != expectedSha || ! readArtifact (root, head, json)) return {};
    const auto* object = json.getDynamicObject();
    juce::String format, version, reviewId, revision, title, created; bool archived = false;
    const auto* items = object != nullptr ? object->getProperty ("items").getArray() : nullptr;
    if (object == nullptr || ! exactProperties (*object, {
            "format", "version", "review_id", "review_revision_id", "title", "items", "created_at", "archived" })
        || ! text (object->getProperty ("format"), format, 64) || format != "kirin_reference_review"
        || ! text (object->getProperty ("version"), version, 8) || version != "1.0"
        || ! text (object->getProperty ("review_id"), reviewId, 36) || reviewId != id || ! uuidV4 (reviewId)
        || ! text (object->getProperty ("review_revision_id"), revision, 36) || revision != head.revision
        || ! text (object->getProperty ("title"), title, 240)
        || ! text (object->getProperty ("created_at"), created, 40)
        || ! boolValue (object->getProperty ("archived"), archived) || archived
        || items == nullptr || items->isEmpty() || items->size() > 64) return {};
    auto result = std::make_shared<WorkflowDefinition>();
    result->kind = WorkflowDefinition::Kind::review; result->id = id;
    result->revisionId = revision; result->sha256 = head.sha; result->title = title;
    std::set<std::string> itemIds;
    for (const auto& value : *items)
    {
        const auto* row = value.getDynamicObject(); WorkflowItem item; juce::String momentSha;
        if (row == nullptr || ! exactProperties (*row, { "item_id", "moment_id", "moment_revision_id", "moment_sha256" })
            || ! text (row->getProperty ("item_id"), item.itemId, 36) || ! uuidV4 (item.itemId)
            || ! itemIds.emplace (item.itemId.toStdString()).second
            || ! text (row->getProperty ("moment_id"), item.momentId, 36) || ! uuidV4 (item.momentId)
            || ! text (row->getProperty ("moment_revision_id"), item.momentRevisionId, 36) || ! uuidV4 (item.momentRevisionId)
            || ! text (row->getProperty ("moment_sha256"), momentSha, 64) || ! sha256 (momentSha)) return {};
        Head momentHead { "moments", item.momentId, item.momentRevisionId, momentSha,
            "moments/" + item.momentId + "/" + item.momentRevisionId + "." + momentSha + ".json", 0 };
        const auto file = root.getChildFile (momentHead.relativePath);
        momentHead.bytes = file.existsAsFile() ? file.getSize() : 0;
        juce::var moment;
        if (momentHead.bytes < 1 || momentHead.bytes > artifactLimit || ! readArtifact (root, momentHead, moment)
            || ! parseMoment (moment, item.momentId, item.momentRevisionId, item)) return {};
        result->items.push_back (std::move (item));
    }
    return result;
}

std::shared_ptr<const WorkflowDefinition> readBookmark (
    const juce::File& root, const juce::String& id, const juce::String& expectedSha)
{
    Head head; juce::var json;
    if (! readHead (root, "bookmarks", id, head) || head.sha != expectedSha || ! readArtifact (root, head, json)) return {};
    const auto* object = json.getDynamicObject();
    juce::String format, version, bookmarkId, revision, origin, title, note, listening, created;
    bool archived = false; WorkflowItem item;
    if (object == nullptr || ! exactProperties (*object, {
            "format", "version", "bookmark_id", "revision_id", "bookmark_origin", "title", "note",
            "condition", "listening_status", "created_at", "archived" })
        || ! text (object->getProperty ("format"), format, 64) || format != "kirin_reference_comparison_bookmark"
        || ! text (object->getProperty ("version"), version, 8) || version != "1.0"
        || ! text (object->getProperty ("bookmark_id"), bookmarkId, 36) || bookmarkId != id || ! uuidV4 (bookmarkId)
        || ! text (object->getProperty ("revision_id"), revision, 36) || revision != head.revision
        || ! text (object->getProperty ("bookmark_origin"), origin, 8) || (origin != "os" && origin != "hypha")
        || ! text (object->getProperty ("title"), title, 240)
        || ! text (object->getProperty ("note"), note, 48 * 1024, true)
        || ! text (object->getProperty ("listening_status"), listening, 32)
        || (listening != "conditions_only" && listening != "auditioned")
        || ! text (object->getProperty ("created_at"), created, 40)
        || ! boolValue (object->getProperty ("archived"), archived) || archived
        || ! parseCondition (object->getProperty ("condition"), item.condition)) return {};
    auto result = std::make_shared<WorkflowDefinition>();
    result->kind = WorkflowDefinition::Kind::bookmark; result->id = id;
    result->revisionId = revision; result->sha256 = head.sha; result->title = title;
    item.itemId = id; item.title = title; item.purpose = note; result->items.push_back (std::move (item));
    return result;
}
}

ReferenceWorkflowRepository::ReferenceWorkflowRepository (juce::File runtimeRoot)
    : root (std::move (runtimeRoot).getChildFile ("workflow-v1")) {}

WorkflowEventCommit ReferenceWorkflowRepository::appendReviewEvent (
    const WorkflowEventRequest& request) const
{
    WorkflowEventCommit result;
    result.attemptId = request.attemptId;
    result.eventId = request.eventId;
    result.operationId = request.operationId;
    if (! uuidV4 (request.runtimeInstanceId) || ! uuidV4 (request.reviewId)
        || ! uuidV4 (request.attemptId) || ! uuidV4 (request.eventId)
        || ! uuidV4 (request.operationId) || ! uuidV4 (request.conditionRevisionId)
        || ! uuidV4 (request.itemId)
        || (request.nextItemId.isNotEmpty() && ! uuidV4 (request.nextItemId))
        || ! sha256 (request.artifactSha256)
        || (request.kind != "started" && request.kind != "confirmed"
            && request.kind != "deferred" && request.kind != "moved"
            && request.kind != "paused" && request.kind != "completed")
        || (request.confirmation != "none" && request.confirmation != "confirmed"
            && request.confirmation != "deferred")) return result;

    Head previous;
    std::int64_t sequence = 1;
    juce::String previousSha;
    const auto headFile = root.getChildFile ("events").getChildFile (request.attemptId)
                              .getChildFile ("head.json");
    if (headFile.exists())
    {
        juce::var previousEvent;
        if (! readHead (root, "events", request.attemptId, previous)
            || ! readArtifact (root, previous, previousEvent)) return result;
        const auto dash = previous.revision.indexOfChar ('-');
        const auto parsed = dash > 0 ? previous.revision.substring (0, dash).getLargeIntValue() : 0;
        const auto* object = previousEvent.getDynamicObject();
        std::int64_t storedSequence = 0;
        if (parsed < 1 || object == nullptr || ! exactProperties (*object, {
                "format", "version", "namespace", "runtime_instance_id", "review_id",
                "attempt_id", "event_id", "operation_id", "sequence", "previous_event_sha256",
                "kind", "condition_revision_id", "item_id", "next_item_id", "confirmation", "artifact_sha256",
                "created_at" })
            || object->getProperty ("format") != "kirin_hypha_reference_workflow_event"
            || object->getProperty ("version") != "1.0"
            || object->getProperty ("namespace") != "workflow-v1"
            || object->getProperty ("attempt_id") != request.attemptId
            || ! exactInteger (object->getProperty ("sequence"), 1,
                               9'007'199'254'740'991LL, storedSequence)
            || parsed != storedSequence) return result;
        if (object->getProperty ("operation_id") == request.operationId)
        {
            if (object->getProperty ("event_id") != request.eventId
                || object->getProperty ("runtime_instance_id") != request.runtimeInstanceId
                || object->getProperty ("review_id") != request.reviewId
                || object->getProperty ("kind") != request.kind
                || object->getProperty ("condition_revision_id") != request.conditionRevisionId
                || object->getProperty ("item_id") != request.itemId
                || object->getProperty ("next_item_id") != request.nextItemId
                || object->getProperty ("confirmation") != request.confirmation
                || object->getProperty ("artifact_sha256") != request.artifactSha256) return result;
            if (! writeOutbox (root, request, previous.relativePath, previous.sha, previous.bytes,
                               object->getProperty ("created_at").toString())) return result;
            result.sequence = storedSequence; result.artifactSha256 = previous.sha;
            result.checkpointId = workflowCheckpointFromHash (previous.sha);
            result.committed = result.checkpointId.isNotEmpty(); return result;
        }
        sequence = storedSequence + 1;
        previousSha = previous.sha;
    }

    auto event = new juce::DynamicObject();
    event->setProperty ("format", "kirin_hypha_reference_workflow_event");
    event->setProperty ("version", "1.0");
    event->setProperty ("namespace", "workflow-v1");
    event->setProperty ("runtime_instance_id", request.runtimeInstanceId);
    event->setProperty ("review_id", request.reviewId);
    event->setProperty ("attempt_id", request.attemptId);
    event->setProperty ("event_id", request.eventId);
    event->setProperty ("operation_id", request.operationId);
    event->setProperty ("sequence", sequence);
    event->setProperty ("previous_event_sha256", previousSha);
    event->setProperty ("kind", request.kind);
    event->setProperty ("condition_revision_id", request.conditionRevisionId);
    event->setProperty ("item_id", request.itemId);
    event->setProperty ("next_item_id", request.nextItemId);
    event->setProperty ("confirmation", request.confirmation);
    event->setProperty ("artifact_sha256", request.artifactSha256);
    const auto createdAt = juce::Time::getCurrentTime().toISO8601 (true);
    event->setProperty ("created_at", createdAt);
    const auto body = RuntimeEventTransport::canonicalJson (juce::var (event));
    const auto bodyBytes = juce::MemoryBlock (body.toRawUTF8(), body.getNumBytesAsUTF8());
    const auto hash = body.isNotEmpty() ? juce::SHA256 (bodyBytes).toHexString() : juce::String {};
    const auto revision = juce::String (sequence) + "-" + request.eventId;
    const auto relative = "events/" + request.attemptId + "/" + revision + "." + hash + ".json";
    if (! sha256 (hash) || ! writeWorkflowDurable (root, root.getChildFile (relative), body, false)) return result;

    auto receipt = new juce::DynamicObject();
    receipt->setProperty ("relative_path", relative);
    receipt->setProperty ("sha256", hash);
    receipt->setProperty ("bytes", static_cast<juce::int64> (bodyBytes.getSize()));
    auto head = new juce::DynamicObject();
    head->setProperty ("format", "kirin_reference_workflow_head");
    head->setProperty ("version", "1.0");
    head->setProperty ("kind", "events");
    head->setProperty ("id", request.attemptId);
    head->setProperty ("revision", revision);
    head->setProperty ("artifact", juce::var (receipt));
    head->setProperty ("updated_at", juce::Time::getCurrentTime().toISO8601 (true));
    const auto headBody = RuntimeEventTransport::canonicalJson (juce::var (head));
    if (! writeWorkflowDurable (root, headFile, headBody, true)) return result;
    Head verified;
    juce::var verifiedEvent;
    if (! readHead (root, "events", request.attemptId, verified)
        || verified.sha != hash || ! readArtifact (root, verified, verifiedEvent)) return result;
    if (! writeOutbox (root, request, relative, hash,
                       static_cast<std::int64_t> (bodyBytes.getSize()), createdAt)) return result;
    result.sequence = sequence;
    result.artifactSha256 = hash;
    result.checkpointId = workflowCheckpointFromHash (hash);
    result.committed = result.checkpointId.isNotEmpty();
    return result;
}

std::shared_ptr<const WorkflowCatalog> ReferenceWorkflowRepository::refresh (
    std::shared_ptr<const WorkflowCatalog> previous) const
{
    const auto index = root.getChildFile ("index.json");
    if (! index.exists()) return std::make_shared<WorkflowCatalog>();
    const auto modified = index.getLastModificationTime().toMilliseconds();
    const auto size = index.getSize();
    if (previous != nullptr && previous->indexModifiedMs == modified
        && previous->indexBytes == size) return previous;
    juce::MemoryBlock bytes; juce::var json;
    const auto* object = readJson (index, indexLimit, bytes, json) ? json.getDynamicObject() : nullptr;
    std::int64_t revision = 0; juce::String format, version;
    const auto* entries = object != nullptr ? object->getProperty ("entries").getArray() : nullptr;
    if (object == nullptr || ! exactProperties (*object, { "format", "version", "revision", "entries" })
        || ! text (object->getProperty ("format"), format, 64) || format != "kirin_reference_workflow_index"
        || ! text (object->getProperty ("version"), version, 8) || version != "1.0"
        || ! exactInteger (object->getProperty ("revision"), 0, 9'007'199'254'740'991LL, revision)
        || entries == nullptr || entries->size() > 20'000)
    { auto failed = std::make_shared<WorkflowCatalog>(); failed->rejectionCode = "workflow_index_rejected"; return failed; }
    auto result = std::make_shared<WorkflowCatalog>();
    result->indexModifiedMs = modified; result->indexBytes = size;
    std::set<std::string> identities;
    for (const auto& value : *entries)
    {
        const auto* row = value.getDynamicObject(); juce::String kind, id, rev, sha, title, search, updated; bool archived = false;
        if (row == nullptr || ! exactProperties (*row, { "kind", "id", "revision", "sha256", "title", "search_text", "archived", "updated_at" })
            || ! text (row->getProperty ("kind"), kind, 16) || ! safeSegment (kind)
            || ! text (row->getProperty ("id"), id, 160) || ! safeSegment (id)
            || ! identities.emplace ((kind + ":" + id).toStdString()).second
            || ! text (row->getProperty ("revision"), rev, 160) || ! safeSegment (rev)
            || ! text (row->getProperty ("sha256"), sha, 64) || ! sha256 (sha)
            || ! text (row->getProperty ("title"), title, 240, true)
            || ! text (row->getProperty ("search_text"), search, 80 * 1024, true)
            || ! boolValue (row->getProperty ("archived"), archived)
            || ! text (row->getProperty ("updated_at"), updated, 40))
        { result->rejectionCode = "workflow_index_rejected"; return result; }
        if (archived) continue;
        if (kind == "reviews" && result->latestReview == nullptr)
            result->latestReview = readReview (root, id, sha);
        else if (kind == "bookmarks" && result->latestBookmark == nullptr)
            result->latestBookmark = readBookmark (root, id, sha);
    }
    if ((result->latestReview == nullptr && std::any_of (entries->begin(), entries->end(), [] (const auto& v) {
            const auto* o = v.getDynamicObject(); return o && o->getProperty ("kind") == "reviews" && ! static_cast<bool> (o->getProperty ("archived")); }))
        || (result->latestBookmark == nullptr && std::any_of (entries->begin(), entries->end(), [] (const auto& v) {
            const auto* o = v.getDynamicObject(); return o && o->getProperty ("kind") == "bookmarks" && ! static_cast<bool> (o->getProperty ("archived")); })))
        result->rejectionCode = "workflow_artifact_rejected";
    juce::ignoreUnused (revision);
    return result;
}
}
