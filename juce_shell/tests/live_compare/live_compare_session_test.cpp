#include "../../src/live_compare/LiveCompareAaxGroup.h"
#include "../../src/live_compare/LiveComparePin.h"
#include "../../src/live_compare/LiveCompareSession.h"
#include "../../src/live_compare/LiveCompareSharedRing.h"
#include "../../src/live_compare/LiveBlindSession.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <vector>

static thread_local bool inRt = false;
static unsigned rtAllocations = 0, rtDeletions = 0;
void* operator new (std::size_t bytes)
{
    if (inRt) ++rtAllocations;
    if (auto* p = std::malloc (bytes == 0 ? 1 : bytes)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t bytes) { return ::operator new (bytes); }
void operator delete (void* p) noexcept { if (inRt && p != nullptr) ++rtDeletions; std::free (p); }
void operator delete[] (void* p) noexcept { ::operator delete (p); }
void operator delete (void* p, std::size_t) noexcept { ::operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { ::operator delete (p); }

using namespace hypha::live_compare;

static void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "FAIL: " << message << '\n'; std::abort(); }
}

namespace
{
constexpr int frames = 512;
constexpr std::uint64_t key = 0xC0FFEEull;

float preValue (std::int64_t clock, int channel) { return 0.25f + 0.0001f * static_cast<float> (clock % 1000) + 0.1f * static_cast<float> (channel); }
constexpr float postValue = -0.5f;

// An adjacent pair (K = 0): PRE publishes a block; POST renders the same clock range.
struct Pair
{
    std::unique_ptr<Ring> ring = std::make_unique<Ring>();
    PreFeeder feeder;
    PostRenderer renderer;
    PostLevel level;
    float postTarget = 1.0f, ceiling = 1.0f;
    std::vector<float> pre[2], post[2];
    std::int64_t clock = 0, projectShift = 0;

    Pair()
    {
        ring->initialise (key, 48000);
        renderer.prepare (4096, 48000.0);
        level.configure (48000.0);
        for (auto* set : { pre, post })
            for (int c = 0; c < 2; ++c) set[c].assign (frames, 0.0f);
    }

    RenderReport step (bool demand, bool preSelected, float gain, bool afterGap = false, int channels = 2,
                       bool poison = false, bool blind = false, bool poisonPost = false)
    {
        ring->header.demand.store (demand ? 1u : 0u);
        BlockClock b;
        b.clock = clock;
        b.project = clock + projectShift;
        b.frames = frames;
        b.clockValid = b.projectValid = b.playing = true;
        b.afterGap = afterGap;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < frames; ++i)
            {
                pre[c][size_t (i)] = preValue (clock + i, c);
                post[c][size_t (i)] = postValue;
            }
        if (poison)
            pre[0][5] = std::nanf ("");
        const float* in[] = { pre[0].data(), pre[1].data() };
        inRt = true;
        feeder.feed (*ring, b, in, 2);
        float* io[] = { post[0].data(), post[1].data() };
        if (poisonPost) post[0][7] = std::nanf ("");
        const auto report = renderer.render (*ring, key, 48000, b, io, channels, preSelected, gain, level,
                                             postTarget, ceiling, blind);
        inRt = false;
        clock += frames;
        return report;
    }

    // Steps until the first proven block (the calibration needs its streak of equal candidates).
    RenderReport calibrate (bool preSelected, float gain)
    {
        for (int i = 0; i < 16; ++i)
        {
            const auto r = step (true, preSelected, gain);
            if (r.verdict == Verdict::accepted)
                return r;
            require (r.verdict == Verdict::calibrating && r.preWaiting == preSelected && ! r.preAudible,
                     "calibration keeps POST and reports PRE waiting");
            require (postTarget < 1.0f || postUntouched(), "an unproven block leaves POST bit-identical");
        }
        require (false, "the pair never calibrated");
        return {};
    }

    bool postUntouched() const
    {
        for (int c = 0; c < 2; ++c)
            for (float v : post[c]) if (v != postValue) return false;
        return true;
    }
};
}

// INV-LC4: PRE sounds only in proven blocks, returns over the symmetric 5 ms fade, and is replaced by
// POST at the block start when the proof is lost, without using unproven PRE samples.
static void preSoundsOnlyWhenProven()
{
    Pair pair;
    const auto first = pair.calibrate (true, 1.0f);
    require (first.verdict == Verdict::accepted && first.preAudible && ! first.preWaiting, "the first proven block returns to PRE");
    const float fadeStart = pair.post[0][0];
    const float expectedStart = postValue * (1.0f - 1.0f / 240.0f) + preValue (pair.clock - frames, 0) * (1.0f / 240.0f);
    require (std::fabs (fadeStart - expectedStart) < 1.0e-6f, "the return starts the 5 ms fade from POST");
    require (pair.post[0][frames - 1] == preValue (pair.clock - 1, 0), "after 5 ms the output is exactly PRE");
    const auto full = pair.step (true, true, 1.0f);
    require (full.preAudible && pair.post[1][0] == preValue (pair.clock - frames, 1), "a proven block plays PRE sample for sample");

    const auto lost = pair.step (true, true, 1.0f, true);
    require (lost.verdict == Verdict::calibrating && lost.preWaiting && ! lost.preAudible, "a gap invalidates K and switches to POST");
    require (lost.reason == RecoveryReason::callbackGap, "lost correspondence retains the actual gap reason");
    require (pair.postUntouched(), "losing the proof switches at the block start without unproven PRE");
}

// The user's PRE to POST switch fades out over proven PRE samples; POST stays untouched afterwards.
static void userSwitchFadesBothWays()
{
    Pair pair;
    pair.calibrate (true, 1.0f);
    pair.step (true, true, 1.0f);
    const auto out = pair.step (true, false, 1.0f);
    require (out.verdict == Verdict::accepted && ! out.preAudible, "selecting POST fades PRE out within the block");
    require (pair.post[0][frames - 1] == postValue, "the fade ends exactly at POST");
    pair.step (true, false, 1.0f);
    require (pair.postUntouched(), "POST selected leaves POST bit-identical");
}

// The approved gain applies to the PRE copy only.
static void approvedGainAppliesToPre()
{
    Pair pair;
    pair.calibrate (true, 0.5f);
    pair.step (true, true, 0.5f);
    require (pair.post[0][7] == preValue (pair.clock - frames + 7, 0) * 0.5f, "PRE is played at the approved gain");
}

// More than two channels never render PRE; without demand PRE publishes nothing.
static void unsupportedLayoutsAndNoDemandKeepPost()
{
    Pair pair;
    pair.calibrate (true, 1.0f);
    pair.step (true, true, 1.0f, false, 3);
    require (pair.postUntouched(), "three channels keep POST");
    Pair idle;
    for (int i = 0; i < 12; ++i) idle.step (false, true, 1.0f);
    require (idle.postUntouched(), "no demand, no PRE input, POST untouched");
    require (idle.ring->header.published.load() == 0, "PRE publishes only while a session demands it");
}

// INV-LC14: an approved POST attenuation lowers POST, and only POST: PRE keeps its own gain.
static void approvedAttenuationLowersPostOnly()
{
    Pair pair;
    pair.postTarget = 0.5f;
    pair.calibrate (false, 1.0f);
    for (int i = 0; i < 4; ++i) pair.step (true, false, 1.0f);
    require (pair.post[0][0] == postValue * 0.5f && pair.post[1][frames - 1] == postValue * 0.5f,
             "POST sounds at the approved attenuation");
    pair.step (true, true, 1.0f);
    pair.step (true, true, 1.0f);
    require (pair.post[0][9] == preValue (pair.clock - frames + 9, 0), "PRE stays at its level");
}

// The attenuation ramps down over 50 ms and back up over 500 ms of full range, never jumping, and a
// settled unity level leaves the audio bit-identical.
static void postLevelRampsDownFastAndUpSlowly()
{
    PostLevel level;
    level.configure (48000.0);
    int down = 0;
    while (level.next (0.5f) > 0.5f) ++down;
    int up = 0;
    while (level.next (1.0f) < 1.0f) ++up;
    std::printf ("post level: down to -6 dB in %d frames, back up in %d frames\n", down, up);
    require (down >= 1190 && down <= 1201, "halfway down takes 25 ms");
    require (up >= 11990 && up <= 12001, "halfway up takes 250 ms");
    float samples[] = { 0.3f, -0.2f, 0.1f };
    float* io[] = { samples };
    level.apply (io, 1, 3, 1.0f);
    require (samples[0] == 0.3f && samples[1] == -0.2f && samples[2] == 0.1f, "settled unity is bit-identical");
}

// INV-LC14: PRE raised above the ceiling fixed at MATCH never sounds; the block is POST from its
// start and the report ends the selection. A non-finite PRE sample trips the guard at any gain.
static void guardKeepsPreUnderTheCeiling()
{
    Pair raised;
    raised.ceiling = 0.5f;
    raised.calibrate (true, 1.0f);
    raised.step (true, true, 1.0f);
    const auto tripped = raised.step (true, true, 2.0f);
    require (tripped.guardTripped && ! tripped.preAudible && raised.postUntouched(),
             "a raised PRE over the ceiling is POST from the block start");
    require (tripped.reason == RecoveryReason::ceiling, "ceiling rejection retains its exact cause");
    Pair poisoned;
    poisoned.calibrate (true, 1.0f);
    const auto nan = poisoned.step (true, true, 1.0f, false, 2, true);
    require (nan.guardTripped && poisoned.postUntouched(), "a non-finite PRE sample never sounds");
    require (nan.reason == RecoveryReason::nonFinite, "invalid audio is not misreported as a level limit");
}

// A gain that changes while PRE sounds (a new MATCH, AUTO) moves linearly over 50 ms, never in a
// step, and the guard checks a rising ramp at its end.
static void preGainRampsOverFiftyMilliseconds()
{
    Pair pair;
    pair.calibrate (true, 1.0f);
    pair.step (true, true, 1.0f);
    std::vector<float> gains;
    for (int b = 0; b < 6; ++b)
    {
        const auto start = pair.clock;
        pair.step (true, true, 0.5f);
        for (int i = 0; i < frames; ++i)
            gains.push_back (pair.post[0][size_t (i)] / preValue (start + i, 0));
    }
    const float increment = 0.5f / 2400.0f;
    float largest = 0.0f;
    for (std::size_t i = 1; i < gains.size(); ++i)
        largest = std::max (largest, std::fabs (gains[i] - gains[i - 1]));
    std::printf ("pre gain ramp: first %.6f, frame 2398 %.6f, frame 2399 %.6f, largest step %.7f\n",
                 gains[0], gains[2398], gains[2399], largest);
    require (std::fabs (gains[0] - (1.0f - increment)) < 2.0e-5f, "the ramp starts in the first frame");
    require (largest <= increment * 1.01f + 2.0e-6f, "no step is larger than one ramp increment");
    require (gains[2398] > 0.5f + increment * 0.5f, "the ramp is still moving before 50 ms");
    for (std::size_t i = 2399; i < gains.size(); ++i)
        require (std::fabs (gains[i] - 0.5f) < 1.0e-6f, "after 50 ms PRE sits at the new gain");

    Pair raised;
    raised.ceiling = 0.5f;
    raised.calibrate (true, 1.0f);
    raised.step (true, true, 1.0f);
    const auto tripped = raised.step (true, true, 1.5f);
    require (tripped.guardTripped && raised.postUntouched(), "a rising ramp is checked at its end value");

    Pair silent;
    silent.calibrate (true, 1.0f);
    silent.step (true, true, 1.0f);
    silent.step (true, false, 1.0f);
    const auto back = silent.step (true, true, 0.25f);
    require (back.preAudible && std::fabs (silent.post[0][frames - 1] - preValue (silent.clock - 1, 0) * 0.25f) < 1.0e-6f,
             "a gain set while PRE is silent applies at once");
}

// A Pin fixes the latest window as one project range: POST's input and the PRE mapped to it. Too
// little history, or a seek inside the window, fixes nothing.
static void pinFixesOneProjectRange()
{
    Pair pair;
    pair.calibrate (false, 1.0f);
    for (int i = 0; i < 12; ++i) pair.step (true, false, 1.0f);
    const auto stereo = pinLatest (*pair.ring, pair.renderer, 4096, 2);
    require (stereo.ok() && stereo.projectStart == pair.clock - 4096 && stereo.post.size() == 8192,
             "the latest window is one project range");
    require (stereo.post[0] == postValue && stereo.pre[0] == preValue (stereo.projectStart, 0)
                 && stereo.pre[8191] == preValue (pair.clock - 1, 1),
             "POST and PRE hold the same range");
    const auto mono = pinLatest (*pair.ring, pair.renderer, 4096, 1);
    require (mono.ok() && mono.pre.size() == 4096 && mono.pre[1] == preValue (mono.projectStart + 1, 0),
             "a mono POST pins one channel");
    require (pinLatest (*pair.ring, pair.renderer, 1 << 20, 2).failure == PinFailure::tooShort,
             "more than the history is too short");
    pair.projectShift = 96000;
    pair.step (true, false, 1.0f);
    require (pinLatest (*pair.ring, pair.renderer, 4096, 2).failure == PinFailure::notProven,
             "a seek inside the window fixes nothing");
    for (int i = 0; i < 17; ++i) pair.step (true, false, 1.0f);
    const auto after = pinLatest (*pair.ring, pair.renderer, 4096, 2);
    require (after.ok() && after.projectStart == pair.clock - 4096 + 96000, "after the seek, a new range");
}

// PRE creates the ring for its identity; POST opens it for the same key and rate only.
static void sharedRingPairsOnlyTheSameIdentityAndRate()
{
    const auto name = sharedRingName (pairKeyForPreInstance ("pre-instance-id"));
    require (name.size() <= 31 && name.rfind ("/kh-lc3-", 0) == 0, "versioned name fits the POSIX limit");
    require (pairKeyForPreInstance ("a") != pairKeyForPreInstance ("b"), "different PRE identities give different keys");
    const auto pairKey = pairKeyForPreInstance ("live-compare-session-test");
    require (sharedRingAvailable(), "macOS and Windows map the ring");
    SharedRingMapping pre, post, wrongRate;
    require (pre.create (pairKey, 48000), "PRE creates its ring");
    require (post.open (pairKey, 48000), "POST opens the ring of its PRE");
    require (! wrongRate.open (pairKey, 44100), "a ring for another rate is refused");
    post.ring()->header.demand.store (1);
    require (pre.ring()->header.demand.load() == 1, "both roles see the same memory");
    post.close();
    require (pre.ring()->header.demand.load() == 0, "closing POST clears its demand");
    require (post.open (pairKey, 48000) && post.ring()->header.ownerClosed.load() == 0,
             "an open PRE ring is live");
    pre.close();
    require (post.ring()->header.ownerClosed.load() == 1, "POST learns that PRE closed its ring");
    SharedRingMapping late;
    require (! late.open (pairKey, 48000), "a ring its PRE closed is never opened again");

    // PRE prepares again while POST still holds the closed ring (on Windows the section keeps its
    // name): PRE gets a new ring, the old one stays closed for that POST, and a new POST opens the
    // new one.
    SharedRingMapping again, newer;
    require (again.create (pairKey, 48000), "PRE creates a new ring while POST holds the old one");
    post.ring()->header.demand.store (0);
    again.ring()->header.demand.store (1);
    require (post.ring()->header.ownerClosed.load() == 1 && post.ring()->header.demand.load() == 0,
             "the old ring stays closed and apart from the new one");
    require (newer.open (pairKey, 48000) && newer.ring()->header.demand.load() == 1
                 && newer.ring()->header.ownerClosed.load() == 0,
             "a new POST opens the new ring");
    post.close();
    newer.close();
    again.close();
    require (! late.open (pairKey, 48000), "nothing is left open after every owner closed");

    // INV-LC9: a PRE that is one channel of a multi-mono set stamps its ring, and POST reads it.
    SharedRingMapping channel, reader;
    require (channel.create (pairKey, 48000, ringSourceMultiMono), "a multi-mono PRE creates its ring");
    require (reader.open (pairKey, 48000) && reader.ring()->header.source.load() == ringSourceMultiMono,
             "POST sees that PRE is one channel of a multi-mono set");
    reader.close();
    channel.close();
    require (again.create (pairKey, 48000) && again.ring()->header.source.load() == 0,
             "a stereo or mono-track PRE stamps none");
    again.close();
}

// INV-LC9: the host's AAX instance group tells a mono track (the group's only instance) from a
// multi-mono set (instances sharing a group). A host that names no group proves nothing.
static void aaxGroupsTellAMonoTrackFromAMultiMonoSet()
{
    AaxGroupMembership unnamed;
    require (! unnamed.alone(), "without a group a mono instance is not shown to be a mono track");
    unnamed.assign (7, false);
    require (! unnamed.alone(), "an undefined group proves nothing");
    AaxGroupMembership track;
    track.assign (11, true);
    require (track.alone(), "the only instance of its group is a mono track");
    {
        AaxGroupMembership left, right;
        left.assign (22, true);
        right.assign (22, true);
        require (! left.alone() && ! right.alone(), "the channels of a multi-mono set share their group");
        require (track.alone(), "another group changes nothing");
    }
    AaxGroupMembership moved;
    moved.assign (22, true);
    require (moved.alone(), "a group counts only the instances that still live");
    moved.assign (11, true);
    require (! moved.alone() && ! track.alone(), "joining a group leaves the old one");
}

int main()
{
    BlindSession trial;
    NamedSelection selection;
    for (int i = 0; i < 1000; ++i)
    {
        trial.start ((i & 1) != 0); selection.select (true);
        const auto command = trial.command(); const auto named = selection.command();
        inRt = true;
        trial.observe (command, true);
        trial.invalidate (command, RecoveryReason::callbackGap);
        selection.fail (named, RecoveryReason::ceiling);
        inRt = false;
    }
    for (const bool choosePre : { false, true })
    {
        Pair pair;
        pair.calibrate (false, 1.0f);
        auto report = pair.step (true, choosePre, 1.0f, false, 2, false, true);
        require (report.stableSource && report.gainSettled, "proven Blind source earns an output receipt");
        report = pair.step (true, choosePre, 1.0f, false, 2, true, true);
        require (report.guardTripped && ! report.stableSource && pair.postUntouched(),
                 "invalid PRE ends Blind even when anonymous POST is selected");
        report = pair.step (true, choosePre, 1.0f, false, 2, false, true, true);
        require (report.guardTripped && ! report.stableSource, "invalid POST never counts as heard");
        report = pair.step (true, choosePre, 1.0f, true, 2, false, true);
        require (! report.stableSource && pair.postUntouched(), "gap fallback never counts as anonymous output");
    }
    preSoundsOnlyWhenProven();
    userSwitchFadesBothWays();
    approvedGainAppliesToPre();
    unsupportedLayoutsAndNoDemandKeepPost();
    sharedRingPairsOnlyTheSameIdentityAndRate();
    aaxGroupsTellAMonoTrackFromAMultiMonoSet();
    approvedAttenuationLowersPostOnly();
    postLevelRampsDownFastAndUpSlowly();
    guardKeepsPreUnderTheCeiling();
    preGainRampsOverFiftyMilliseconds();
    pinFixesOneProjectRange();
    require (rtAllocations == 0 && rtDeletions == 0, "PRE feed / POST render never new/delete in named or Blind mode");
    std::printf ("live compare session: all checks passed; RT new=%u delete=%u\n", rtAllocations, rtDeletions);
    return 0;
}
