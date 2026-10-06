#include "../../src/live_compare/LiveCompareGainApproval.h"
#include "../../src/live_compare/LiveCompareAuthority.h"
#include "../../src/live_compare/LiveBlindPreparation.h"
#include "../../src/live_compare/LiveCompareIdle.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
using namespace hypha::live_compare;
static void require (bool ok, const char* why)
{ if (! ok) { std::fprintf (stderr, "gain snapshot: %s\n", why); std::abort(); } }
namespace
{
void (*interleave)() = nullptr;
int injection = 0, remainingLoads = 1;
unsigned loadMask = 0;
std::array<unsigned, 8> loads {};
void arm (int field, void (*hook)(), int occurrence = 1) noexcept
{ injection = field; interleave = hook; remainingLoads = occurrence; loadMask = 0; loads.fill (0); }
void loadBoundary (int field) noexcept
{
    loadMask |= 1u << field; ++loads[static_cast<std::size_t> (field)];
    if (field == injection && interleave != nullptr && --remainingLoads == 0)
    { const auto hook = interleave; interleave = nullptr; hook(); }
}
template <typename Value>
struct Hooked
{
    std::atomic<Value> value;
    int field = 0;
    Hooked (Value v, int id) : value (v), field (id) {}
    Value load (std::memory_order order = std::memory_order_seq_cst) const noexcept
    {
        const auto result = value.load (order);
        loadBoundary (field);
        return result;
    }
    void store (Value v, std::memory_order order = std::memory_order_seq_cst) noexcept { value.store (v, order); }
};
struct HookedApproval
{
    GainApproval value;
    void bind (const GainIdentity& stamp) noexcept { value.bind (stamp); }
    GainIdentity read() const noexcept
    { const auto result = value.read(); loadBoundary (6); return result; }
};
struct State
{
    Authority authority;
    std::atomic<bool> sessionActive { true };
    Hooked<std::uint64_t> gainRevision { 0, 7 };
    Hooked<float> gain { 1, 1 }, postTarget { 1, 2 }, ceilingLinear { 1, 3 };
    Hooked<bool> matchLimited { false, 4 }, matchRetained { false, 5 };
    HookedApproval gainApproval;
    std::atomic<bool> matched { true };
    std::atomic<std::uint64_t> gainReceipt { 0 }, matchGeneration { 1 }, sessionGeneration { 1 };
    std::atomic<std::uint64_t> blindGainRevision { 0 };
    std::atomic<std::uint32_t> matchRun { 1 }, playbackRun { 1 };
};
State* active = nullptr;
constexpr GainIdentity first { 10, 11, 12, 0, 48000 }, second { 20, 21, 22, 1ull << 32, 96000 };
void publish (State& state, float pre, float post, float ceiling, bool limited, const GainIdentity& stamp)
{
    GainUpdate update (state.gainRevision.value);
    require (static_cast<bool> (update), "one bounded writer obtains an even revision");
    state.gain.store (pre); state.postTarget.store (post); state.ceilingLinear.store (ceiling);
    state.matchLimited.store (limited); state.matchRetained.store (true); state.gainApproval.bind (stamp);
}
void changeApproval() { publish (*active, 1.2f, 0.25f, 0.8f, true, second); }
void publicationInterleavingsAreRejected()
{
    State state; active = &state;
    publish (state, 0.7f, 0.5f, 1.0f, false, first);
    // Exact old read order: POST was sampled BEFORE the revision fence. This causal control
    // admits a mixed tuple even though the new MATCH was fully committed before that fence.
    arm (2, &changeApproval);
    const float oldPost = state.postTarget.load();
    const auto revision = state.gainRevision.load();
    const float newPre = state.gain.load();
    const bool oldCoherent = (revision & 1u) == 0 && revision == state.gainRevision.load();
    require (oldCoherent && oldPost == 0.5f && newPre == 1.2f,
             "control reproduces the old coherent-old-POST/new-PRE race");
    for (int field = 1; field <= 6; ++field)
    {
        publish (state, 0.7f, 0.5f, 1.0f, false, first);
        arm (field, &changeApproval);
        const auto mixed = readGainSnapshot (state);
        require (! mixed.coherent, "writer at EVERY payload boundary rejects the entire tuple, including POST");
    }
    const auto coherent = readGainSnapshot (state);
    require (coherent.coherent && coherent.pre == 1.2f && coherent.post == 0.25f
        && coherent.ceiling == 0.8f && coherent.limited && coherent.retained
        && coherent.identity.same (second), "one revision owns gain/POST/ceiling/choice/approval identity");
    {
        GainUpdate publishing (state.gainRevision.value);
        require (static_cast<bool> (publishing), "MATCH writer opens its publication fence");
        require (! readGainSnapshot (state).coherent, "odd revision authorises no output receipt");
        GainUpdate ending (state.gainRevision.value);
        require (! ending, "RT END cannot turn an in-progress MATCH revision even");
    }
    require (readGainSnapshot (state).coherent, "completed writer releases the same publication fence");
    publish (state, 0.5f, 0.4f, 0.8f, false, first);
    const auto received = readGainSnapshot (state);
    state.gainReceipt.store (received.revision);
    state.blindGainRevision.store (received.revision);
    require (currentGainReceipt (state, received), "current actual RT receipt permits the one approved tuple");
    require (currentBlindReceipt (state), "Blind's own approval revision also has its actual RT receipt");
    publish (state, 0.7f, 0.2f, 0.7f, false, first);
    require (! currentGainReceipt (state, received), "writer after completed status snapshot revokes its current-revision claim");
    const auto unreceived = readGainSnapshot (state);
    require (! currentGainReceipt (state, unreceived), "new revision with an old actual gain receipt cannot start Blind");
    state.gainReceipt.store (unreceived.revision);
    require (currentGainReceipt (state, unreceived), "new tuple gains permission only through its own audio receipt");
    require (! currentBlindReceipt (state), "a new approval and receipt cannot revive the old Blind assignment");
    state.sessionGeneration.store (2);
    require (! currentGainReceipt (state, unreceived), "seek/restore proof generations revoke old receipt");
    state.sessionGeneration.store (1); state.playbackRun.store (2);
    require (! currentGainReceipt (state, unreceived), "stop/restart cannot reuse an earlier playback-run receipt");
    state.playbackRun.store (1); state.matchRetained.store (false);
    require (! currentGainReceipt (state, unreceived), "independent END/restore revocation remains fail-closed");
    state.matchRetained.store (true);
    require (currentGainReceipt (state, unreceived), "causal receipt is current before authority revocation");
    state.authority.revoke();
    require (! currentGainReceipt (state, unreceived), "pair revoke after snapshot immediately rejects the current receipt");
    require (state.authority.arm (state.authority.ticket()), "new explicit permission can be armed");
    require (! currentGainReceipt (state, unreceived), "new permission cannot re-authorise an old approved identity");
}
float nextIdlePost = 0.5f;
void changeIdleApproval() { publish (*active, 0.25f, nextIdlePost, 0.7f, true, second); }
void changeIdleIdentity() { publish (*active, 0.7f, 1.0f, 0.8f, false, second); }
std::unique_ptr<GainUpdate> heldIdleWriter;
void openIdleWriter()
{
    heldIdleWriter = std::make_unique<GainUpdate> (active->gainRevision.value);
    require (static_cast<bool> (*heldIdleWriter), "idle causal writer holds an odd revision");
}
void idleSubsetIsBoundedAndNeverApprovesComparison()
{
    State state; active = &state;
    publish (state, 0.7f, 1.0f, 0.8f, false, first);
    arm (0, nullptr);
    require (unchangedUnityPostOnly (state, false, false, false, 1.0f),
             "normal unity idle uses only the coherent POST subset");
    constexpr unsigned subsetMask = (1u << 2) | (1u << 7);
    require (loadMask == subsetMask && loads[7] == 2 && loads[2] == 1,
             "idle load mask excludes PRE ceiling flags and approval identity");
    for (const int gate : { 0, 1, 2, 3 })
    {
        arm (0, nullptr);
        require (! unchangedUnityPostOnly (state, gate == 0, gate == 1, gate == 2,
                                           gate == 3 ? 0.5f : 1.0f) && loadMask == 0,
                 "lease END Blind or unsettled actual short-circuits every gain load");
    }
    for (const float target : { 0.0f, 0.5f, std::nextafter (1.0f, 0.0f), std::nextafter (1.0f, 2.0f),
                               std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
    {
        publish (state, 0.7f, target, 0.8f, false, first); arm (0, nullptr);
        require (! coherentUnityPostTarget (state) && loadMask == subsetMask,
                 "held nonunity and nonfinite target never use normal idle routing");
    }
    publish (state, 0.7f, 1.0f, 0.8f, false, first);
    {
        GainUpdate writer (state.gainRevision.value); arm (0, nullptr);
        require (writer && ! coherentUnityPostTarget (state) && loadMask == (1u << 7) && loads[7] == 1,
                 "odd opening revision rejects idle before reading a default target");
    }
    for (const int field : { 7, 2 })
        for (const float oldPost : { 1.0f, 0.5f })
            for (const float newPost : { 1.0f, 0.5f })
            {
                publish (state, 0.7f, oldPost, 0.8f, false, first);
                nextIdlePost = newPost; arm (field, &changeIdleApproval);
                require (! coherentUnityPostTarget (state),
                         "writer after first revision OR target rejects unity transitions and tuple changes");
                const auto full = readGainSnapshot (state);
                require (full.coherent && full.pre == 0.25f && full.post == newPost
                    && full.ceiling == 0.7f && full.limited && full.identity.same (second),
                         "rejected idle subset is followed by a fresh full tuple with no partial-field reuse");
            }
    for (const int field : { 7, 2 })
    {
        publish (state, 0.7f, 1.0f, 0.8f, false, first); arm (field, &changeIdleIdentity);
        require (! coherentUnityPostTarget (state), "identity-only publication still invalidates the narrow revision");
        const auto full = readGainSnapshot (state);
        require (full.coherent && full.pre == 0.7f && full.post == 1.0f && full.ceiling == 0.8f
            && ! full.limited && full.identity.same (second) && coherentUnityPostTarget (state),
                 "same gain tuple with new identity permits idle only, never approval reuse");
    }
    publish (state, 0.7f, 1.0f, 0.8f, false, first); arm (7, &openIdleWriter);
    require (! coherentUnityPostTarget (state) && heldIdleWriter,
             "writer still odd at final revision check rejects idle");
    heldIdleWriter.reset();
    publish (state, 0.7f, 1.0f, 0.8f, false, first);
    nextIdlePost = 0.5f; arm (7, &changeIdleApproval, 2);
    require (coherentUnityPostTarget (state) && state.postTarget.load() == 0.5f,
             "publication after final sampled revision may start only on the next unchanged-A callback");
    publish (state, 0.7f, 1.0f, 0.8f, false, first);
    const auto gains = readGainSnapshot (state); state.gainReceipt.store (gains.revision);
    require (currentGainReceipt (state, gains), "authority control has a real coherent receipt before revoke");
    state.authority.revoke(); arm (0, nullptr);
    require (unchangedUnityPostOnly (state, false, false, false, 1.0f)
        && state.gainReceipt.load() == gains.revision && ! currentGainReceipt (state, gains) && ! currentBlindReceipt (state),
             "unity after authority revoke touches no output and creates no gain or Blind authority");
    publish (state, 0.7f, 0.5f, 0.8f, false, first); arm (0, nullptr);
    require (! unchangedUnityPostOnly (state, false, false, false, 1.0f) && state.postTarget.load() == 0.5f,
             "revoked retained attenuation stays on the full path and is never raised by idle");
}
void approvalCannotMoveBetweenConditions()
{
    auto ring = std::make_unique<Ring>(); ring->initialise (first.pair, first.rate, 0, first.ownerA, first.ownerB);
    const auto identity = gainIdentity (*ring, first.authority);
    require (identity.same (first), "approval binds actual pair/rate/128-bit ring lifetime and restore authority");
    for (int field = 0; field < 5; ++field)
    {
        auto changed = first;
        if (field == 0) ++changed.pair;
        if (field == 1) ++changed.rate;
        if (field == 2) ++changed.ownerA;
        if (field == 3) ++changed.ownerB;
        if (field == 4) changed.authority += 1ull << 32;
        require (! identity.same (changed), "old MATCH is not approval for a changed condition");
    }
}
void preparationPublicationRequiresTheNewAdmissionWithoutClobberingReusedMatch()
{
    State state;
    publish (state, 0.5f, 0.4f, 0.8f, false, first);
    const auto approved = readGainSnapshot (state);
    state.gainReceipt.store (approved.revision);
    std::atomic<std::uint64_t> request { 7 }, receipt { 7 };
    BlindPreparation preparation;
    // Message setup runs BEFORE publication, never after an RT re-certification.
    state.matched.store (false); state.sessionGeneration.fetch_add (1);
    const auto required = request.load() + 1;
    preparation.begin (required);
    const auto opening = preparation.command();
    require (opening.requiredAdmission == 8 && request.load() == 7 && receipt.load() == 7,
             "causal interleaving exposes the new command before its request is published");
    preparation.observe (opening, true, RecoveryReason::none, receipt.load());
    require ((preparation.command().word & 2u) == 0,
             "accepted old renderer plus old admission receipt cannot establish the new preparation");
    preparation.observe (opening, false, RecoveryReason::clockMissing, receipt.load());
    require (preparation.failure() == RecoveryReason::none && ! currentGainReceipt (state, approved),
             "old unacknowledged timing cannot fail or MATCH-authorise the new epoch");
    request.fetch_add (1);
    receipt.store (request.load()); // RT renews history, proves fresh timing, then acknowledges it
    state.matchGeneration.store (state.sessionGeneration.load()); state.matched.store (true);
    preparation.observe (opening, true, RecoveryReason::none, receipt.load());
    require ((preparation.command().word & 2u) != 0 && currentGainReceipt (state, approved),
             "the fresh RT proof re-certifies the same gain immediately and later message setup cannot erase it");
    preparation.end(); preparation.begin (9);
    preparation.observe (opening, true, RecoveryReason::none, receipt.load());
    require ((preparation.command().word & 2u) == 0,
             "even a formerly valid request receipt cannot prove the next explicit preparation epoch");
}
}
int main()
{
    publicationInterleavingsAreRejected(); approvalCannotMoveBetweenConditions();
    idleSubsetIsBoundedAndNeverApprovesComparison();
    preparationPublicationRequiresTheNewAdmissionWithoutClobberingReusedMatch();
    std::puts ("Gain snapshot: PASS (causal old-read control, publication/END fences, all tuple fields, identity negatives)");
}
