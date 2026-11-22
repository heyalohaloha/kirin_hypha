#include "reference_audition/ReferenceMetricLeaseEpoch.h"
#include "TestPathBudget.h"
#include "TestTemporaryDirectory.h"
#include "AtomicFile.h"
#include "HyphaMetricSharedZero.h"
#include "HyphaMetricChainDemand.h"
#include <iostream>
#include <stdexcept>

using namespace hypha::metric_needle;
namespace
{
void require (bool condition, const char* message)
{ if (!condition) throw std::runtime_error (message); }
const auto scope = juce::String::repeatedString ("a", 64);
const auto otherScope = juce::String::repeatedString ("b", 64);
const auto owner = juce::String::repeatedString ("c", 32);
constexpr juce::int64 instant = 1700000000000LL;
SharedReferenceZero example (Face face = Face::psr)
{
    SharedReferenceZero value; value.face = face; value.status = Status::ready;
    value.rate = 48000; value.channels = 2; value.generation = 7;
    value.median = 9.123456789012345; value.band = {8.0, 10.0}; value.song = "Reference";
    return value;
}
void wire()
{
    for (const auto face : {Face::psr, Face::crest, Face::sharp})
    {
        auto value = example (face);
        const auto encoded = SharedMetricZero::encoded (scope, owner, value, instant);
        require (encoded.isNotEmpty() && encoded.getNumBytesAsUTF8() <= 256, "bounded complete wire");
        const auto decoded = SharedMetricZero::decoded (scope, face, 48000, 2, encoded, instant);
        require (decoded.status == Status::ready && decoded.face == face && decoded.generation == 7,
                 "face generation and status survive roundtrip");
        require (decoded.median == value.median && decoded.band.low == 8.0 && decoded.band.high == 10.0,
                 "native reference values preserve double precision");
        require (decoded.song == "Reference", "song label roundtrip");
        require (decoded.publisher == owner, "paint key uses the verified publisher, not the song title");
        const auto successor = juce::String::repeatedString ("d", 32);
        require (SharedMetricZero::encoded (scope, owner, decoded, instant) == encoded,
                 "decoded paint identity does not modify wire bytes");
        require (SharedMetricZero::decoded (scope, face, 48000, 2,
            SharedMetricZero::encoded (scope, successor, decoded, instant), instant).publisher == successor,
                 "new publisher is distinguishable even with equal song and generation");
        require (SharedMetricZero::decoded (scope, face, 48000, 2, encoded,
            instant + SharedMetricZero::leaseMs).publisher.isEmpty(), "expired publisher cannot survive in the paint key");
        for (const auto now : {instant - 1, instant + SharedMetricZero::leaseMs})
            require (SharedMetricZero::decoded (scope, face, 48000, 2, encoded, now).status == Status::unavailable,
                     "future and expired values rejected at exact lease boundary");
        require (SharedMetricZero::decoded (scope, face, 48000, 2, encoded, instant + 1499).status == Status::ready,
                 "last valid lease millisecond");
        require (SharedMetricZero::decoded (otherScope, face, 48000, 2, encoded, instant).status == Status::unavailable,
                 "other chain cannot read");
        require (SharedMetricZero::decoded (scope, face, 44100, 2, encoded, instant).status == Status::unavailable
            && SharedMetricZero::decoded (scope, face, 48000, 1, encoded, instant).status == Status::unavailable,
                 "definition format mismatch cannot compare");
        require (SharedMetricZero::decoded (scope, Face::vu, 48000, 2, encoded, instant).status == Status::unavailable,
                 "non-native face rejected");
        value.song = juce::String::repeatedString (juce::String::fromUTF8 ("長い曲名🎶"), 100);
        const auto longText = SharedMetricZero::encoded (scope, owner, value, instant);
        require (longText.isNotEmpty() && longText.getNumBytesAsUTF8() <= 256, "unicode name bounded");
        require (SharedMetricZero::decoded (scope, face, 48000, 2, longText, instant).status == Status::ready,
                 "truncated name preserves complete numeric record");
        value.band = {};
        require (SharedMetricZero::decoded (scope, face, 48000, 2,
            SharedMetricZero::encoded (scope, owner, value, instant), instant).status == Status::ready,
                 "complete median may have no all-set band");
        value.band = {1.0, missing};
        require (SharedMetricZero::encoded (scope, owner, value, instant).isEmpty(), "partial band rejected");
        value.band = {2.0, 1.0};
        require (SharedMetricZero::encoded (scope, owner, value, instant).isEmpty(), "reversed band rejected");
        value = example (face); value.median = std::numeric_limits<double>::infinity();
        require (SharedMetricZero::encoded (scope, owner, value, instant).isEmpty(), "nonfinite ready zero rejected");
        value.status = Status::measuring;
        const auto pending = SharedMetricZero::decoded (scope, face, 48000, 2,
            SharedMetricZero::encoded (scope, owner, value, instant), instant);
        require (pending.status == Status::measuring && std::isnan (pending.median)
            && std::isnan (pending.band.low), "pending clears numeric facts");
    }
    auto value = example();
    auto valid = SharedMetricZero::encoded (scope, owner, value, instant);
    const auto invalid = [&](juce::String text) {
        return SharedMetricZero::decoded (scope, Face::psr, 48000, 2, text, instant).status == Status::unavailable;
    };
    require (invalid (valid.replace ("KHMZ1", "KHMZ2")), "unknown definition version rejected");
    require (invalid (valid + "extra"), "trailing record rejected");
    require (invalid (valid.substring (0, 70)), "partial write rejected");
    require (invalid (valid.replace (" 7 ", " 18446744073709551616 ")), "generation overflow rejected");
    require (invalid (valid.replace (" 7 ", " -1 ")), "negative generation rejected");
    require (invalid (valid.replace (" 7 ", " 0 ")), "zero generation rejected");
    require (invalid (valid.replace ("48000", "48000x")), "numeric suffix rejected");
    require (invalid (valid.replace (" 8 10", " nan 10")), "NaN token rejected");
    value.rate = 48001;
    require (SharedMetricZero::encoded (scope, owner, value, instant).isEmpty(), "non-100ms integer rate rejected");
    value = example(); value.generation = 0;
    require (SharedMetricZero::encoded (scope, owner, value, instant).isEmpty(), "publisher missing generation rejected");
}
void reasons()
{
    auto value = example();
    const auto oldWire = SharedMetricZero::encoded (scope, owner, value, instant);
    require (juce::StringArray::fromTokens (juce::StringArray::fromLines (oldWire)[2], " ", {}).size() == 10,
             "empty reason preserves legacy ten-field wire");
    const auto legacy = SharedMetricZero::decoded (scope, Face::psr, 48000, 2, oldWire, instant);
    require (legacy.status == Status::ready && legacy.reason.empty(), "old record decodes without reason");
    value.status = Status::unavailable;
    value.song = juce::String::repeatedString (juce::String::fromUTF8 ("長い曲名🎶"), 100);
    for (const auto* code : {"select_reference_post", "preparing", "unsupported_format", "read_failed", "source_changed", "private_copy_required",
                             "capacity_exceeded", "audio_too_short", "cue_too_short", "no_valid_value", "analysis_failed"})
    {
        value.reason = code;
        const auto encoded = SharedMetricZero::encoded (scope, owner, value, instant);
        require (encoded.isNotEmpty() && encoded.getNumBytesAsUTF8() <= 256, "reason and unicode name fit wire bound");
        const auto decoded = SharedMetricZero::decoded (scope, Face::psr, 48000, 2, encoded, instant);
        require (decoded.reason == code && decoded.generation == value.generation && std::isnan (decoded.median),
                 "fixed reason survives without manufacturing numeric facts");
        require (SharedMetricZero::decoded (scope, Face::psr, 48000, 2, encoded, instant + 1500).reason.empty(),
                 "reason cannot outlive lease");
        require (SharedMetricZero::decoded (scope, Face::psr, 44100, 2, encoded, instant).reason.empty(),
                 "reason cannot cross native format boundary");
    }
    value.reason = "read_failed";
    const auto allowed = SharedMetricZero::encoded (scope, owner, value, instant);
    for (const auto* unsafe : {"unknown", "/absolute/song.wav", "C:\\private\\song.wav", "read_failed extra", "read_failed\npath"})
    {
        value.reason = unsafe;
        require (SharedMetricZero::encoded (scope, owner, value, instant).isEmpty(), "publisher rejects non-code reason");
        const auto rejected = SharedMetricZero::decoded (scope, Face::psr, 48000, 2,
            allowed.replace ("read_failed", unsafe), instant);
        require (rejected.reason.empty() && rejected.generation == 0, "reader rejects unknown or injected reason");
    }
    const auto oversize = allowed + juce::String::repeatedString ("x", 257);
    require (SharedMetricZero::decoded (scope, Face::psr, 48000, 2, oversize, instant).generation == 0,
             "reason extension retains 256-byte hard ceiling");
}
struct Fixture
{
    juce::File root = hypha::tests::temporaryParent()
        .getNonexistentChildFile ("hmz", "", false);
    ~Fixture() { hypha::tests::removeFixtureDirectory (root); }
    juce::File file (Face face) const
    { return root.getChildFile (scope + "-" + juce::String (static_cast<int> (face)) + ".txt"); }
};
void chainDemand()
{
    Fixture fixture;
    ChainDemand pre (fixture.root), anotherPre (fixture.root), post (fixture.root), otherPost (fixture.root);
    require (!post.requested (scope, 48000, 2, instant), "no PRE request initially");
    pre.request (scope, 48000, 2, true, instant);
    hypha::tests::recordPathsBeforeRemoval (fixture.root);
    require (post.requested (scope, 48000, 2, instant), "PRE-first demand crosses independent service instances");
    require (!post.requested (otherScope, 48000, 2, instant)
        && !post.requested (scope, 44100, 2, instant) && !post.requested (scope, 48000, 1, instant),
        "demand cannot cross chain or host format");
    require (!post.requested (scope, 48000, 2, instant - 1)
        && !post.requested (scope, 48000, 2, instant + 1500), "future or expired request cannot cause work");
    anotherPre.request (scope, 48000, 2, true, instant + 250);
    hypha::tests::recordPathsBeforeRemoval (fixture.root);
    pre.withdraw();
    require (post.requested (scope, 48000, 2, instant + 250), "one PRE close cannot withdraw another PRE's request");
    anotherPre.request (scope, 44100, 2, true, instant + 500);
    hypha::tests::recordPathsBeforeRemoval (fixture.root);
    require (!post.requested (scope, 48000, 2, instant + 500)
        && post.requested (scope, 44100, 2, instant + 500), "format change withdraws former demand");
    anotherPre.withdraw();
    require (!post.requested (scope, 44100, 2, instant + 500), "PRE close withdraws its last demand");
    auto processorOwner = post.ownership (scope);
    require (processorOwner != nullptr && !otherPost.ownership (scope), "one chain admits only one POST owner");
    auto detachedWorkerOwner = processorOwner;
    processorOwner.reset(); post.ownership ({});
    require (!otherPost.ownership (scope), "cancelled detached worker retains exclusive chain owner");
    detachedWorkerOwner.reset();
    auto successor = otherPost.ownership (scope);
    require (successor != nullptr, "ownership transfers only after old worker actually releases");
    auto separateChain = post.ownership (otherScope);
    require (separateChain != nullptr, "different chains own independent service slots");
   #if ! JUCE_WINDOWS
    // Windows symlink creation can require elevated/developer permissions; the
    // production handle rejects reparse files, and its platform test is separate.
    const auto target = fixture.root.getChildFile ("link-target");
    require (target.createDirectory().wasOk(), "symlink fixture target");
    const auto link = fixture.root.getChildFile ("link-parent");
    require (target.createSymbolicLink (link, false), "symlink fixture parent");
    ChainOwner redirected (link.getChildFile ("owner.lock"));
    require (!redirected.acquired(), "publisher refuses a redirected immediate parent");
   #endif
    require (post.serviceDue (instant) && !post.serviceDue (instant + 249) && post.serviceDue (instant + 250),
             "processor service transport remains bounded at four Hz");
}
void staleDemandPopulation()
{
    Fixture fixture;
    const auto directory = fixture.root.getChildFile (scope);
    require (directory.createDirectory().wasOk(), "stale peer fixture directory");
    const auto expired = juce::String ("KHMD1\n") + scope + "\n48000 2 " + juce::String (instant - 1500) + "\n";
    for (int i = 0; i < 130; ++i)
        require (hypha::storage::writeText (directory.getChildFile (juce::String (i) + ".req"), expired,
            false, false, "\n"), "write expired peer fixture");
    ChainDemand pre (fixture.root), post (fixture.root);
    pre.request (scope, 48000, 2, true, instant);
    hypha::tests::recordPathsBeforeRemoval (fixture.root);
    bool found = false;
    for (int i = 0; i < 3; ++i) found = post.requested (scope, 48000, 2, instant) || found;
    require (found, "rotating bounded reads discover live peer beyond 64 crash leftovers");
    require (post.requested (scope, 48000, 2, instant + 250), "cached peer validity is refreshed");
    require (!post.requested (scope, 48000, 2, instant + 1500), "cached peer expiration is enforced");
    require (!post.requested (otherScope, 48000, 2, instant), "cached peer never crosses scope");
    require (directory.findChildFiles (juce::File::findFiles, false, "*.req").size() == 131,
        "expired peers are never deleted by another instance");
}
void leasePublicationEpoch()
{
    hypha::reference_audition::metrics::LeaseEpoch epoch;
    require (epoch.current(), "unmanaged legacy epoch starts current");
    epoch.changed(); const auto oldLease = epoch.ticket();
    require (!epoch.current(), "new lease hides snapshots before worker applies it");
    epoch.acknowledge (oldLease); require (epoch.current(), "applied lease can publish");
    epoch.changed(); const auto newLease = epoch.ticket();
    require (!epoch.current(), "nonnull old to nonnull new lease immediately hides old result");
    epoch.acknowledge (oldLease);
    require (!epoch.current(), "late old worker acknowledgement cannot revalidate old result");
    epoch.changed(); const auto cancelledLease = epoch.ticket();
    epoch.acknowledge (newLease);
    require (!epoch.current(), "cancellation during lease application stays hidden");
    epoch.acknowledge (cancelledLease); require (epoch.current(), "cancel acknowledgement matches latest request");
    epoch.changed(); require (!epoch.current(), "reacquiring same pointer still needs new epoch after cancellation");
}
void scopeOwnerRelease()
{
    Fixture fixture;
    ChainDemand unselected (fixture.root), selected (fixture.root), successor (fixture.root);
    require (!unselected.selectionOwnership (scope, false, true), "unselected POST cannot monopolise peer demand");
    auto selectedLease = selected.selectionOwnership (scope, true, true);
    require (selectedLease != nullptr, "selected POST acquires behind an unselected POST");
    auto cancellingWorker = selectedLease;
    selectedLease.reset(); // Controller has revoked its lease; cancelled worker is still alive.
    require (!selected.selectionOwnership (scope, false, true), "deselection withdraws processor ownership");
    require (!successor.selectionOwnership (scope, true, true), "handoff waits for old work destruction");
    cancellingWorker.reset();
    auto next = successor.selectionOwnership (scope, true, true);
    require (next != nullptr, "selected successor acquires after cancellation completion");
    next.reset();
    require (!successor.selectionOwnership (scope, true, false), "no demand releases selected owner");
    require (selected.selectionOwnership (scope, true, true) != nullptr, "demand can resume on another selected POST");
    require (!unselected.selectionOwnership (otherScope, true, false), "idle POST never reserves another scope");
}
void transport()
{
    Fixture fixture;
    SharedMetricZero writer (fixture.root), reader (fixture.root);
    auto zero = example();
    require (writer.publish (scope, zero, instant), "atomic publish");
    require (!writer.publish (scope, zero, instant + 249), "write throttle bounded at four Hz");
    require (writer.publish (scope, zero, instant + 250), "lease heartbeat at refresh boundary");
    require (reader.read (scope, Face::psr, 48000, 2, true, instant + 250).status == Status::ready, "same chain reads");
    require (reader.read (scope, Face::psr, 48000, 2, false, instant + 251).status == Status::unavailable,
             "license/Reference gate overrides cached result immediately");
    require (reader.read (scope, Face::psr, 48000, 2, true, instant + 252).status == Status::ready, "gate reopen revalidates");
    require (reader.read (scope, Face::psr, 44100, 2, true, instant + 253).status == Status::unavailable,
             "format change overrides cache immediately");
    require (reader.read (scope, Face::psr, 48000, 2, true, instant + 254).status == Status::ready, "correct format restored");
    require (reader.read (scope, Face::psr, 48000, 2, true, instant + 1749).status == Status::ready, "lease before expiry");
    require (reader.read (scope, Face::psr, 48000, 2, true, instant + 1750).status == Status::unavailable,
             "cache cannot extend expired publisher lease");
    zero.generation = 8;
    require (writer.publish (scope, zero, instant + 2000), "new selection publication");
    require (reader.read (scope, Face::psr, 48000, 2, true, instant + 2000).generation == 8, "new generation observed");
    zero.generation = 7;
    require (writer.publish (scope, zero, instant + 2250), "simulate delayed old selection");
    require (reader.read (scope, Face::psr, 48000, 2, true, instant + 2250).status == Status::unavailable,
             "same owner generation rollback rejected");
    zero.generation = 9;
    require (writer.publish (scope, zero, instant + 2500), "recover newer selection");
    require (reader.read (scope, Face::psr, 48000, 2, true, instant + 2500).generation == 9, "newer generation recovers");
    require (hypha::storage::writeText (fixture.file (Face::psr), "damaged", false, false, "\n"), "damage fixture");
    require (reader.read (scope, Face::psr, 48000, 2, true, instant + 2750).status == Status::unavailable, "damage clears visible cache");
    zero.generation = 8;
    require (writer.publish (scope, zero, instant + 3000), "old generation after damage");
    require (reader.read (scope, Face::psr, 48000, 2, true, instant + 3000).status == Status::unavailable,
             "damage does not erase high water generation");
    SharedMetricZero replacement (fixture.root); zero.generation = 1;
    require (replacement.publish (scope, zero, instant + 3250), "replacement publisher lifetime");
    require (reader.read (scope, Face::psr, 48000, 2, true, instant + 3250).generation == 1, "new owner may restart generation");
    require (reader.read (scope, Face::crest, 48000, 2, true, instant + 3250).status == Status::unavailable,
             "other metric cannot inherit PSR zero");
    zero.face = Face::crest;
    require (replacement.publish (scope, zero, instant + 3250), "independent metric heartbeat");
    require (reader.read (scope, Face::crest, 48000, 2, true, instant + 3500).status == Status::ready, "second metric independent");
    require (reader.read (otherScope, Face::crest, 48000, 2, true, instant + 3501).status == Status::unavailable,
             "scope change immediately clears old chain cache");
}
}
int main()
{
    try { wire(); reasons(); transport(); chainDemand(); scopeOwnerRelease(); leasePublicationEpoch(); staleDemandPopulation(); std::cout << "Metric shared zero passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
