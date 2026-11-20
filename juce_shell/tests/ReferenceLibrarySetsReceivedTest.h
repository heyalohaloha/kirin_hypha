#pragma once
#include "../src/reference_audition/ReferenceLibrarySets.h"

namespace
{
// Independent contract assertions, not Kirin OS source: OS 1.6/1.7 accept v1.0,
// and confirm a set only when all three identities match the current snapshot.
bool receivedSetMatches (const juce::var& json, std::int64_t manifest,
                         std::int64_t revision, const juce::String& hash)
{
    return json["format"] == "kirin_hypha_reference_library_received" && json["version"] == "1.0"
        && (json["manifest_revision"].isInt64() || json["manifest_revision"].isInt()) && static_cast<juce::int64> (json["manifest_revision"]) == manifest
        && (json["sets_revision"].isInt64() || json["sets_revision"].isInt())
        && static_cast<juce::int64> (json["sets_revision"]) == revision
        && revision >= 1 && revision <= 9007199254740991LL
        && json["sets_sha256"].isString() && json["sets_sha256"].toString() == hash;
}

void testLibrarySetsReceived (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("receipt-sets");
    auto id = receiptIdentity(); id.hyphaVersion = "1.1.58";
    ref::RuntimeWorkspace workspace; workspace.library = true; workspace.manifest.revision = 7;
    const auto file = receiptFile (root, id);
    const auto now = juce::Time::currentTimeMillis();
    ref::LibraryReceivedWriter writer (root);
    const auto write = [&] {
        writer.observe (id, workspace, now); writer.finish(); writer.service (now); writer.finish();
        return juce::JSON::parse (file);
    };
    auto json = write();
    require (!json.hasProperty ("sets_revision") && !json.hasProperty ("sets_sha256"),
             "missing sets preserves the legacy v1.0 receipt shape");
    ref::RuntimeLibrarySets sets; sets.manifestRevision = 7; sets.revision = 3;
    const auto acceptedBytes = juce::String (R"({"format":"kirin_hypha_reference_library_sets","version":"1.0","revision":3,"manifest_revision":7,"song_sets":[],"check_sets":[],"source_ranges":[]})");
    const auto setsFile = root.getChildFile ("library/sets.json");
    require (hypha::storage::writeText (setsFile, acceptedBytes, false, false, "\n"), "write accepted sets fixture");
    juce::String rejection; std::vector<ref::RuntimeSkippedItem> skipped;
    const auto loaded = ref::readReferenceLibrarySets (root, workspace, rejection, skipped);
    require (loaded && loaded->manifestRevision == 7 && loaded->revision == 3,
             "the parser retains the actual accepted manifest and sets revision");
    sets = *loaded;
    require (sets.hash == juce::SHA256 (setsFile).toHexString(), "receipt hash comes from exact accepted bytes");
    auto nextManifest = workspace.manifest; nextManifest.revision = 8;
    const auto carried = ref::carriedLibrarySets (sets, nextManifest);
    require (carried.manifestRevision == 7 && carried.hash == sets.hash, "carry retains the original manifest identity");
    workspace.librarySets = sets;
    json = write();
    require (receivedSetMatches (json, 7, 3, sets.hash), "accepted sets bind the actual writer receipt");
    require (!receivedSetMatches (json, 8, 3, sets.hash) && !receivedSetMatches (json, 7, 4, sets.hash)
        && !receivedSetMatches (json, 7, 3, juce::String::repeatedString ("a", 64)),
        "the consumer cannot confirm a different manifest, revision or raw-byte hash");
    workspace.librarySets->revision = 4;
    json = write();
    require (receivedSetMatches (json, 7, 4, sets.hash), "sets-only revision updates rewrite receipt");
    const auto nextHash = juce::String::repeatedString ("b", 64);
    workspace.librarySets->hash = nextHash;
    json = write();
    require (receivedSetMatches (json, 7, 4, nextHash), "same revision but changed bytes rewrite receipt");
    workspace.manifest.revision = 8; // carried sets keep their accepted origin (7)
    json = write();
    require (!json.hasProperty ("sets_revision") && !json.hasProperty ("sets_sha256"),
             "carried sets are not acknowledged as belonging to the new manifest");
    workspace.librarySets->manifestRevision = 8;
    json = write();
    require (receivedSetMatches (json, 8, 4, nextHash), "sets catching up rewrite at the same manifest");
    for (const auto bad : { juce::String {}, juce::String::repeatedString ("B", 64), juce::String::repeatedString ("b", 63) })
    {
        workspace.librarySets->hash = bad; json = write();
        require (!json.hasProperty ("sets_revision") && !json.hasProperty ("sets_sha256"), "invalid hashes omit both fields");
    }
    workspace.librarySets->hash = nextHash;
    for (const auto bad : { std::int64_t (0), std::int64_t (-1), std::int64_t (9007199254740992LL) })
    {
        workspace.librarySets->revision = bad; json = write();
        require (!json.hasProperty ("sets_revision") && !json.hasProperty ("sets_sha256"), "unsafe revisions omit both fields");
    }
    workspace.librarySets->revision = 4; write();
    workspace.librarySets.reset(); json = write();
    require (!json.hasProperty ("sets_revision") && !json.hasProperty ("sets_sha256"), "removal clears stale set acknowledgement");

    // A pending old write must not win over the newest set accepted by the worker.
    const auto queuedRoot = sandbox.getChildFile ("receipt-sets-queue");
    ReceiptStoreBarrier barrier;
    ref::LibraryReceivedWriter queued (queuedRoot, [&] (const auto& f, const auto& text) { return barrier.store (f, text); });
    workspace.manifest.revision = 7; workspace.librarySets = sets;
    queued.observe (id, workspace, now); barrier.awaitEntry();
    workspace.librarySets->revision = 5; workspace.librarySets->hash = nextHash;
    queued.observe (id, workspace, now + 1);
    barrier.release(); queued.finish(); queued.service (now + 2); queued.finish();
    require (receivedSetMatches (juce::JSON::parse (receiptFile (queuedRoot, id)), 7, 5, nextHash),
             "queued sets update supersedes an older asynchronous receipt");
    require (barrier.maximumActive == 1, "set receipt stores stay serial");
}
}
