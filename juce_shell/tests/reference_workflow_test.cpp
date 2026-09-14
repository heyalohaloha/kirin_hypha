#include "reference_runtime_test_support.h"
#include "reference_runtime_test_entries.h"
#include "../src/reference_audition/ReferenceWorkflowRepository.h"

namespace
{
const juce::String reviewId = "10000000-0000-4000-8000-000000000001";
const juce::String reviewRevision = "10000000-0000-4000-8000-000000000002";
const juce::String momentId = "20000000-0000-4000-8000-000000000001";
const juce::String momentRevision = "20000000-0000-4000-8000-000000000002";
const juce::String itemId = "30000000-0000-4000-8000-000000000001";
const juce::String attemptId = "40000000-0000-4000-8000-000000000001";
const juce::String runtimeUuid = "50000000-0000-4000-8000-000000000001";

juce::var workflowCondition()
{
    auto source = new juce::DynamicObject();
    source->setProperty ("work_id", "60000000-0000-4000-8000-000000000001");
    source->setProperty ("recording_id", "60000000-0000-4000-8000-000000000002");
    source->setProperty ("version_id", "60000000-0000-4000-8000-000000000003");
    source->setProperty ("sha256_file", juce::String::repeatedString ("a", 64));
    source->setProperty ("sha256_pcm", juce::String::repeatedString ("b", 64));
    auto cue = new juce::DynamicObject();
    cue->setProperty ("cue_id", "cue-1");
    cue->setProperty ("label", "Chorus");
    cue->setProperty ("start_frame", static_cast<juce::int64> (48'000));
    cue->setProperty ("end_frame", static_cast<juce::int64> (96'000));
    cue->setProperty ("sample_rate", static_cast<juce::int64> (48'000));
    cue->setProperty ("loop_enabled", true);
    auto publication = new juce::DynamicObject();
    publication->setProperty ("namespace", "legacy");
    publication->setProperty ("revision", "revision-1");
    publication->setProperty ("sha256", juce::String::repeatedString ("c", 64));
    auto condition = new juce::DynamicObject();
    condition->setProperty ("comparison", "a_c");
    condition->setProperty ("preset_id", "preset-1");
    condition->setProperty ("check_id", "check-1");
    condition->setProperty ("candidate_id", "candidate-1");
    condition->setProperty ("cue", juce::var (cue));
    condition->setProperty ("source_kind", "work_version");
    condition->setProperty ("source_identity", juce::var (source));
    condition->setProperty ("publication_ref", juce::var (publication));
    return juce::var (condition);
}

juce::String fixtureArtifact (const juce::File& root, const juce::String& relative,
                              const juce::var& value)
{
    const auto temporary = root.getChildFile (relative + ".pending");
    require (writeJson (temporary, value), "workflow artifact fixture must be written");
    const auto hash = juce::SHA256 (temporary).toHexString();
    const auto target = root.getChildFile (relative + "." + hash + ".json");
    require (temporary.moveFileTo (target), "workflow artifact fixture must use its exact hash");
    return hash;
}

juce::var workflowHead (const juce::String& kind, const juce::String& id,
                        const juce::String& revision, const juce::String& relative,
                        const juce::String& hash, std::int64_t bytes)
{
    auto receipt = new juce::DynamicObject();
    receipt->setProperty ("relative_path", relative);
    receipt->setProperty ("sha256", hash);
    receipt->setProperty ("bytes", bytes);
    auto head = new juce::DynamicObject();
    head->setProperty ("format", "kirin_reference_workflow_head");
    head->setProperty ("version", "1.0");
    head->setProperty ("kind", kind);
    head->setProperty ("id", id);
    head->setProperty ("revision", revision);
    head->setProperty ("artifact", juce::var (receipt));
    head->setProperty ("updated_at", "2026-09-14T00:00:00.000Z");
    return juce::var (head);
}
}

void testReferenceWorkflow (const juce::File& sandbox)
{
    const auto transport = sandbox.getChildFile ("workflow-transport");
    const auto root = transport.getChildFile ("workflow-v1");
    auto moment = new juce::DynamicObject();
    moment->setProperty ("format", "kirin_reference_listening_moment");
    moment->setProperty ("version", "1.0");
    moment->setProperty ("listening_moment_id", momentId);
    moment->setProperty ("revision_id", momentRevision);
    moment->setProperty ("title", "Chorus detail");
    moment->setProperty ("purpose", "Listen for low-end recovery");
    moment->setProperty ("condition", workflowCondition());
    moment->setProperty ("created_at", "2026-09-14T00:00:00.000Z");
    moment->setProperty ("archived", false);
    const auto momentStem = "moments/" + momentId + "/" + momentRevision;
    const auto momentHash = fixtureArtifact (root, momentStem, juce::var (moment));

    auto row = new juce::DynamicObject();
    row->setProperty ("item_id", itemId);
    row->setProperty ("moment_id", momentId);
    row->setProperty ("moment_revision_id", momentRevision);
    row->setProperty ("moment_sha256", momentHash);
    juce::Array<juce::var> items; items.add (juce::var (row));
    auto review = new juce::DynamicObject();
    review->setProperty ("format", "kirin_reference_review");
    review->setProperty ("version", "1.0");
    review->setProperty ("review_id", reviewId);
    review->setProperty ("review_revision_id", reviewRevision);
    review->setProperty ("title", "Today's review");
    review->setProperty ("items", juce::var (items));
    review->setProperty ("created_at", "2026-09-14T00:00:00.000Z");
    review->setProperty ("archived", false);
    const auto reviewStem = "reviews/" + reviewId + "/" + reviewRevision;
    const auto reviewHash = fixtureArtifact (root, reviewStem, juce::var (review));
    const auto reviewRelative = reviewStem + "." + reviewHash + ".json";
    require (writeJson (root.getChildFile ("reviews").getChildFile (reviewId).getChildFile ("head.json"),
                        workflowHead ("reviews", reviewId, reviewRevision, reviewRelative,
                                      reviewHash, root.getChildFile (reviewRelative).getSize())),
             "workflow review head must be written");

    auto indexRow = new juce::DynamicObject();
    indexRow->setProperty ("kind", "reviews"); indexRow->setProperty ("id", reviewId);
    indexRow->setProperty ("revision", reviewRevision); indexRow->setProperty ("sha256", reviewHash);
    indexRow->setProperty ("title", "Today's review"); indexRow->setProperty ("search_text", "today");
    indexRow->setProperty ("archived", false); indexRow->setProperty ("updated_at", "2026-09-14T00:00:00.000Z");
    juce::Array<juce::var> rows; rows.add (juce::var (indexRow));
    auto index = new juce::DynamicObject();
    index->setProperty ("format", "kirin_reference_workflow_index");
    index->setProperty ("version", "1.0"); index->setProperty ("revision", static_cast<juce::int64> (1));
    index->setProperty ("entries", juce::var (rows));
    require (writeJson (root.getChildFile ("index.json"), juce::var (index)),
             "workflow index must be written");

    ref::ReferenceWorkflowRepository repository (transport);
    const auto catalog = repository.refresh();
    require (catalog && catalog->rejectionCode.isEmpty() && catalog->latestReview
             && catalog->latestReview->items.size() == 1
             && catalog->latestReview->items[0].condition.sourceIdentityKey.contains ("60000000"),
             "exact immutable review and listening point must load");

    ref::WorkflowEventRequest event;
    event.runtimeInstanceId = runtimeUuid; event.reviewId = reviewId; event.attemptId = attemptId;
    event.eventId = "70000000-0000-4000-8000-000000000001";
    event.operationId = "80000000-0000-4000-8000-000000000001";
    event.kind = "started"; event.conditionRevisionId = momentRevision; event.itemId = itemId;
    event.nextItemId = itemId;
    event.confirmation = "none"; event.artifactSha256 = reviewHash;
    const auto first = repository.appendReviewEvent (event);
    require (first.committed && first.sequence == 1 && ref::safeUuid (first.checkpointId),
             "first workflow event must commit with a durable checkpoint");
    event.eventId = "70000000-0000-4000-8000-000000000002";
    event.operationId = "80000000-0000-4000-8000-000000000002";
    event.kind = "confirmed"; event.confirmation = "confirmed";
    const auto second = repository.appendReviewEvent (event);
    require (second.committed && second.sequence == 2 && second.artifactSha256 != first.artifactSha256,
             "workflow journal must append a hash-chained confirmation");
    const auto headJson = juce::JSON::parse (root.getChildFile ("events").getChildFile (attemptId)
                                                  .getChildFile ("head.json"));
    require (headJson.getDynamicObject() != nullptr
             && headJson.getDynamicObject()->getProperty ("revision").toString().startsWith ("2-"),
             "workflow head must advance only after the second immutable event");

    const auto* receipt = headJson.getDynamicObject()->getProperty ("artifact").getDynamicObject();
    const auto latest = root.getChildFile (receipt->getProperty ("relative_path").toString());
    require (latest.replaceWithText ("tampered"), "workflow event tamper fixture must be written");
    event.eventId = "70000000-0000-4000-8000-000000000003";
    event.operationId = "80000000-0000-4000-8000-000000000003";
    require (! repository.appendReviewEvent (event).committed,
             "a damaged journal head dependency must stop the next append");
}
