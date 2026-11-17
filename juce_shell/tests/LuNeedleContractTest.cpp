#include "LuNeedleContractTest.h"
#include "TestTemporaryDirectory.h"
#include "TestPathBudget.h"

#include "../src/HyphaLanguage.h"
#include "../src/HyphaLuReferenceFacts.h"
#include "../src/HyphaLuSharedZero.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTextStyle.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
namespace
{
using lu_needle::Facts;
using lu_needle::SamePlace;
using lu_needle::Window;
using lu_needle::Zero;
using reference_audition::Snapshot;

void require (bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "LU needle contract failed at line " << line << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}
#define KIRIN_LU_REQUIRE(expression) require ((expression), #expression, __LINE__)

bool near (double a, double b, double tolerance = 1.0e-6) { return std::abs (a - b) <= tolerance; }

void verifyScale()
{
    KIRIN_LU_REQUIRE (near (lu_needle::normalized (-18.0), 0.0));
    KIRIN_LU_REQUIRE (near (lu_needle::normalized (9.0), 1.0));
    KIRIN_LU_REQUIRE (near (lu_needle::normalized (0.0), 18.0 / 27.0));
    KIRIN_LU_REQUIRE (near (lu_needle::normalized (-30.0), 0.0) && near (lu_needle::normalized (20.0), 1.0));
    KIRIN_LU_REQUIRE (near (lu_needle::normalized (lu_needle::nan), 0.0)); // no value rests on the left stop
    KIRIN_LU_REQUIRE (near (lu_needle::relativeLu (-12.0, -9.8), -2.2));
    KIRIN_LU_REQUIRE (std::isnan (lu_needle::relativeLu (-12.0, lu_needle::nan)));
    KIRIN_LU_REQUIRE (std::isnan (lu_needle::relativeLu (lu_needle::nan, -9.8)));
    KIRIN_LU_REQUIRE (lu_needle::signedLu (1.23) == "+1.2" && lu_needle::signedLu (-0.04) == "0.0");
    KIRIN_LU_REQUIRE (lu_needle::signedLu (-3.0) == "-3.0" && lu_needle::signedLu (lu_needle::nan) == "---");

    // The set's range: nothing for no song or one song, p10 to p90 (nearest rank) from two up.
    KIRIN_LU_REQUIRE (! lu_needle::percentileBand ({}).valid());
    KIRIN_LU_REQUIRE (! lu_needle::percentileBand ({ -9.0 }).valid());
    KIRIN_LU_REQUIRE (! lu_needle::percentileBand ({ -9.0, lu_needle::nan }).valid());
    const auto two = lu_needle::percentileBand ({ -8.0, -12.0 });
    KIRIN_LU_REQUIRE (two.valid() && near (two.lowLufs, -12.0) && near (two.highLufs, -8.0));
    std::vector<double> twenty;
    for (int index = 1; index <= 20; ++index) twenty.push_back (-static_cast<double> (index));
    const auto many = lu_needle::percentileBand (twenty);
    KIRIN_LU_REQUIRE (near (many.lowLufs, -19.0) && near (many.highLufs, -3.0));

    // The saved choice: face and window in one value; unknown values read as the default.
    for (const auto window : { Window::momentary, Window::shortTerm })
        for (const bool face : { false, true })
        {
            const auto packed = lu_needle::packChoice (face, window);
            KIRIN_LU_REQUIRE (lu_needle::packedLuFace (packed) == face && lu_needle::packedWindow (packed) == window);
        }
    KIRIN_LU_REQUIRE (lu_needle::packChoice (false, Window::momentary) == 0);
    KIRIN_LU_REQUIRE (lu_needle::packedWindow (5) == Window::momentary);
    KIRIN_LU_REQUIRE (lu_needle::nextWindow (Window::shortTerm) == Window::momentary);
    KIRIN_LU_REQUIRE (lu_needle::packedWindow (7) == Window::momentary);

    // The needle points at the value: a 60 ms ease, within 5 % after 180 ms, settled within half a second.
    lu_needle::EasedNeedle needle;
    needle.snap (0.0f);
    KIRIN_LU_REQUIRE (needle.step (1.0f, 0.0) && near (needle.shown, 0.0));
    for (int frame = 0; frame < 6; ++frame) needle.step (1.0f, 0.030);
    KIRIN_LU_REQUIRE (needle.shown > 0.95f && needle.shown < 1.0f);
    bool moving = true;
    for (int frame = 0; frame < 11 && moving; ++frame) moving = needle.step (1.0f, 0.030);
    KIRIN_LU_REQUIRE (! moving && near (needle.shown, 1.0));

    // Reading: the PRE needle exists only with a confirmed POST - PRE, and never for Integrated.
    Facts facts;
    facts.readable = true; facts.zero = Zero::ready; facts.zeroLufs = -10.0;
    auto reading = lu_needle::reading (facts, Window::momentary, -12.0, 1.5);
    KIRIN_LU_REQUIRE (near (reading.own, -2.0) && near (reading.pre, -3.5) && near (reading.postMinusPre, 1.5));
    reading = lu_needle::reading (facts, Window::momentary, -12.0, lu_needle::nan);
    KIRIN_LU_REQUIRE (near (reading.own, -2.0) && std::isnan (reading.pre) && std::isnan (reading.postMinusPre));
    reading = lu_needle::reading (facts, Window::integrated, -12.0, 1.5);
    KIRIN_LU_REQUIRE (std::isnan (reading.pre));
    facts.zeroLufs = -14.0; // another song: the same loudness reads differently
    KIRIN_LU_REQUIRE (near (lu_needle::reading (facts, Window::momentary, -12.0, lu_needle::nan).own, 2.0));
    facts.playedOffsetLu = 3.0; // a mono song plays 3 LU above its value
    KIRIN_LU_REQUIRE (near (lu_needle::reading (facts, Window::momentary, -12.0, lu_needle::nan).own, -1.0));
    facts.zero = Zero::noSong;
    KIRIN_LU_REQUIRE (std::isnan (lu_needle::reading (facts, Window::momentary, -12.0, 1.5).own));
}

std::shared_ptr<const reference_audition::RuntimeDetailedMeasurement> timeline()
{
    auto measurement = std::make_shared<reference_audition::RuntimeDetailedMeasurement>();
    measurement->audio = { 48000, 2, 48000 * 10 };
    reference_audition::RuntimeMeasurementTimeline loudness;
    loudness.hopSamples = 4800; // 100 ms
    auto& momentary = loudness.series["lufs_m_millilu"];
    auto& shortTerm = loudness.series["lufs_s_millilu"];
    for (int hop = 0; hop < 100; ++hop)
    {
        // Measured only from 2 s to 6 s, as when Kirin OS has values for a Cue alone.
        const bool measured = hop >= 20 && hop < 60;
        momentary.push_back (measured ? std::optional<std::int64_t> (-9000 - hop * 10) : std::nullopt);
        shortTerm.push_back (measured ? std::optional<std::int64_t> (-11000 - hop * 10) : std::nullopt);
    }
    measurement->loudness = loudness;
    return measurement;
}

Snapshot role (const char* name, double cueLufs, double wholeLufs)
{
    Snapshot snapshot;
    snapshot.candidateId = "candidate";
    snapshot.candidateName = name;
    snapshot.cueLevelAvailable = std::isfinite (cueLufs);
    snapshot.cueIntegratedLoudness = cueLufs;
    snapshot.measurementAvailable = std::isfinite (wholeLufs);
    snapshot.sourceIntegratedLoudness = std::isfinite (wholeLufs) ? wholeLufs : 0.0;
    snapshot.appliedGainDb = 6.0; // the audition gain never moves the 0
    return snapshot;
}

Snapshot runtimeWith (const Snapshot* song, const Snapshot* check, int viewed)
{
    Snapshot runtime;
    runtime.libraryReceived = true;
    runtime.comparisonSlot = viewed;
    if (song != nullptr) { runtime.referenceSelection = std::make_shared<const Snapshot> (*song); runtime.selectedSongId = "set/song"; }
    if (check != nullptr) runtime.checkSelection = std::make_shared<const Snapshot> (*check);
    return runtime;
}

void verifyReferenceFacts()
{
    const auto cueSong = role ("North Road", -9.8, -11.5);
    const auto wholeOnly = role ("South Gate", lu_needle::nan, -12.4);
    const auto unmeasured = role ("East Field", lu_needle::nan, lu_needle::nan);

    // Reference cannot be read: nothing is offered, whatever is chosen.
    KIRIN_LU_REQUIRE (! lu_needle::referenceFacts (runtimeWith (&cueSong, nullptr, 3), false, Window::momentary).readable);
    // Nothing chosen in B or C.
    auto facts = lu_needle::referenceFacts (runtimeWith (nullptr, nullptr, 3), true, Window::momentary);
    KIRIN_LU_REQUIRE (facts.readable && facts.zero == Zero::noSong && std::isnan (facts.playedZero()));
    // The Cue's value; the whole song's when the Cue has none; a reason when Kirin OS has neither.
    facts = lu_needle::referenceFacts (runtimeWith (&cueSong, nullptr, 3), true, Window::momentary);
    KIRIN_LU_REQUIRE (facts.zero == Zero::ready && near (facts.zeroLufs, -9.8) && facts.song == "North Road");
    facts = lu_needle::referenceFacts (runtimeWith (&wholeOnly, nullptr, 3), true, Window::momentary);
    KIRIN_LU_REQUIRE (facts.zero == Zero::ready && near (facts.zeroLufs, -12.4));
    facts = lu_needle::referenceFacts (runtimeWith (&unmeasured, nullptr, 3), true, Window::momentary);
    KIRIN_LU_REQUIRE (facts.zero == Zero::noValue && facts.song == "East Field");
    // B and C both hold a song: the page in view decides, and a role that is heard decides first.
    auto both = runtimeWith (&cueSong, &wholeOnly, 2);
    KIRIN_LU_REQUIRE (lu_needle::referenceFacts (both, true, Window::momentary).song == "South Gate");
    both.comparisonSlot = 3;
    KIRIN_LU_REQUIRE (lu_needle::referenceFacts (both, true, Window::momentary).song == "North Road");
    both.audibleComparisonSlot = 2;
    KIRIN_LU_REQUIRE (lu_needle::referenceFacts (both, true, Window::momentary).song == "South Gate");
    both.audibleComparisonSlot = 0; both.comparisonSlot = 1; // the V page: B's song
    KIRIN_LU_REQUIRE (lu_needle::referenceFacts (both, true, Window::momentary).song == "North Road");

    // The set's band: Kirin OS's songs of the chosen set with a value, as played (mono +3.01).
    auto withSet = runtimeWith (&cueSong, nullptr, 3);
    reference_audition::RuntimeSongSetOption chosen { "set", "Set", 1, {}, {} }, other { "other", "Other", 2, {}, {} };
    const auto song = [] (double lufs, double offset)
    { reference_audition::RuntimeSongFacts value; value.lufsI = lufs; value.playbackLoudnessOffsetLu = offset; return value; };
    other.facts = { song (-30.0, 0.0), song (-40.0, 0.0) };
    withSet.songSets = { other, chosen };
    withSet.selectedSongSetId = "set";
    KIRIN_LU_REQUIRE (! lu_needle::referenceFacts (withSet, true, Window::momentary).band.valid());
    withSet.songSets[1].facts = { song (-9.8, 0.0), song (lu_needle::nan, 0.0) };
    KIRIN_LU_REQUIRE (! lu_needle::referenceFacts (withSet, true, Window::momentary).band.valid());
    withSet.songSets[1].facts = { song (-9.8, 0.0), song (-13.0, 3.01), song (-7.5, 0.0) };
    const auto band = lu_needle::referenceFacts (withSet, true, Window::momentary).band;
    KIRIN_LU_REQUIRE (band.valid() && near (band.lowLufs, -9.99) && near (band.highLufs, -7.5));

    // The song at A's position: FOLLOW A only, the position already holds the offset.
    auto following = cueSong;
    following.followA = true;
    following.transportPositionValid = true;
    following.detailedMeasurement = timeline();
    following.followPositionSeconds = 3.05; // 30 whole hops: the value at the end of hop 29
    auto follow = runtimeWith (&following, nullptr, 3);
    follow.followA = true;
    facts = lu_needle::referenceFacts (follow, true, Window::momentary);
    KIRIN_LU_REQUIRE (facts.samePlace == SamePlace::shown && near (facts.samePlaceLufs, -9.29));
    facts = lu_needle::referenceFacts (follow, true, Window::shortTerm);
    KIRIN_LU_REQUIRE (facts.samePlace == SamePlace::shown && near (facts.samePlaceLufs, -11.29));
    KIRIN_LU_REQUIRE (lu_needle::referenceFacts (follow, true, Window::integrated).samePlace == SamePlace::off);
    following.followPositionSeconds = 3.05 + 1.5; // the offset moved the song 1.5 s later
    follow = runtimeWith (&following, nullptr, 3); follow.followA = true;
    KIRIN_LU_REQUIRE (near (lu_needle::referenceFacts (follow, true, Window::momentary).samePlaceLufs, -9.44));
    following.sourceLoudnessOffsetLu = 3.01; // a mono song on a stereo track
    follow = runtimeWith (&following, nullptr, 3); follow.followA = true;
    facts = lu_needle::referenceFacts (follow, true, Window::momentary);
    KIRIN_LU_REQUIRE (near (facts.samePlaceLufs, -9.44 + 3.01) && near (facts.playedZero(), -9.8 + 3.01));
    following.sourceLoudnessOffsetLu = 0.0;
    for (const auto outside : { 1.0, 7.0, lu_needle::nan }) // before and after the measured part, outside the song
    {
        following.followPositionSeconds = outside;
        follow = runtimeWith (&following, nullptr, 3); follow.followA = true;
        facts = lu_needle::referenceFacts (follow, true, Window::momentary);
        KIRIN_LU_REQUIRE (facts.samePlace == SamePlace::noValueHere && std::isnan (facts.samePlaceLufs));
    }
    following.followPositionSeconds = 3.05;
    follow = runtimeWith (&following, nullptr, 3); // LOOP
    KIRIN_LU_REQUIRE (lu_needle::referenceFacts (follow, true, Window::momentary).samePlace == SamePlace::off);
    follow.followA = true;
    follow.blindPhase = reference_audition::BlindPhase::active; // Blind never shows which is which
    KIRIN_LU_REQUIRE (lu_needle::referenceFacts (follow, true, Window::momentary).samePlace == SamePlace::off);
}

void verifySharedZero()
{
    const auto directory = temporaryParent().getNonexistentChildFile ("lu-zero", {}, false);
    const juce::String scope = juce::String::repeatedString ("0123456789abcdef", 4);
    const juce::String otherScope = juce::String::repeatedString ("fedcba9876543210", 4);
    Facts facts;
    facts.readable = true; facts.zero = Zero::ready; facts.song = juce::String::fromUTF8 (u8"North Road 道");
    facts.zeroLufs = -9.8; facts.playedOffsetLu = 3.01; facts.band = { -12.5, -7.5 };
    facts.samePlace = SamePlace::shown; facts.samePlaceLufs = -8.0; // POST's own, never shared
    {
        lu_needle::SharedZero post (directory), pre (directory);
        KIRIN_LU_REQUIRE (! pre.read (scope, 1000).readable); // nothing shared yet: PRE keeps the VU
        post.publish (scope, facts);
        const auto read = pre.read (scope, 2000);
        KIRIN_LU_REQUIRE (read.readable && read.zero == Zero::ready && read.song == facts.song);
        KIRIN_LU_REQUIRE (near (read.zeroLufs, -9.8) && near (read.playedOffsetLu, 3.01) && read.band == facts.band);
        KIRIN_LU_REQUIRE (read.samePlace == SamePlace::off);
        KIRIN_LU_REQUIRE (! pre.read (otherScope, 3000).readable); // another chain reads nothing
        auto next = facts;
        next.zero = Zero::noSong; next.song.clear(); next.zeroLufs = lu_needle::nan; next.band = {};
        post.publish (scope, next);
        const auto later = pre.read (scope, 4000);
        KIRIN_LU_REQUIRE (later.readable && later.zero == Zero::noSong && later.song.isEmpty() && ! later.band.valid());
        post.publish (scope, Facts {});
        KIRIN_LU_REQUIRE (pre.read (scope, 4100).readable); // the file is read at most four times a second
        KIRIN_LU_REQUIRE (! pre.read (scope, 4300).readable);
    }
    facts.song = juce::String::repeatedString (juce::String::fromUTF8 (u8"長い名前 "), 60);
    const auto encoded = lu_needle::SharedZero::encoded (scope, facts);
    KIRIN_LU_REQUIRE (encoded.getNumBytesAsUTF8() <= 256);
    KIRIN_LU_REQUIRE (lu_needle::SharedZero::decoded (scope, encoded).zero == Zero::ready);
    KIRIN_LU_REQUIRE (! lu_needle::SharedZero::decoded (scope, "KIRIN_HYPHA_LU_ZERO_V1\n" + scope + "\n1 2 x\nname\n").readable);
    KIRIN_LU_REQUIRE (! lu_needle::SharedZero::decoded (scope, encoded.replace ("V1", "V9")).readable);
    KIRIN_LU_REQUIRE (removeFixtureDirectory (directory));
}
}

void verifyLuNeedleFace(); // LuNeedleFaceContractTest.cpp

void verifyLuNeedleContract()
{
    verifyScale();
    verifyReferenceFacts();
    verifySharedZero();
    verifyLuNeedleFace();
    std::cout << "LU needle: PASS\n";
}
}
