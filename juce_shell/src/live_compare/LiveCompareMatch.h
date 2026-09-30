#pragma once

#include "LiveCompareSession.h"

#include <cstdint>

namespace hypha::live_compare
{
enum class MatchFailure : std::uint8_t
{
    none,
    notProven,       // K is not valid for the latest block
    tooShort,        // less than minimumSeconds of contiguous, proven history
    overwritten,     // PRE or POST moved past the window while it was copied
    notEnoughSignal, // neither gain policy found enough paired active signal
    outOfRange,      // the final gain (including held POST) is beyond maximumMatchDb
    stale,           // remeasure on the current session; never reuse an old approval
    invalidPlan      // malformed plan; no gain is applied
};

// A MATCH never moves either side by more than this (the TRACK/STEM gate of Local Blind).
constexpr double maximumMatchDb = 24.0;

// What one MATCH measured over the aligned window. The ceiling is C = max(-1 dBTP, POST true
// peak, PRE true peak), the observed-TP basis of Local Blind (INV-S24).
struct MatchResult
{
    MatchFailure failure = MatchFailure::notProven;
    double measuredDb = 0.0;   // POST loudness minus PRE loudness
    double prePeakDbtp = 0.0, postPeakDbtp = 0.0;
    double ceilingDbtp = 0.0;
    std::uint64_t analysisUnits = 0;
    double seconds = 0.0;
    std::uint64_t generation = 0;
    bool generationBound = false; // processor-measured plans must stay on the same continuous session
    std::uint64_t proof = 0, preRun = 0;
    bool proofBound = false;
    bool ok() const noexcept { return failure == MatchFailure::none; }
};

// The gains a MATCH asks for, given the POST attenuation the user already approved (heldPostDb,
// never above 0). On the POST basis PRE takes the whole remaining difference. When that would
// raise PRE's true peak above C the user chooses, as in Local Blind: lower POST instead, PRE
// staying at its level, or raise PRE only up to C and hear the rest as TP LIMIT. Nothing is
// clamped without that choice, and a MATCH never raises POST.
struct MatchPlan
{
    MatchFailure failure = MatchFailure::none;
    double preGainDb = 0.0, postGainDb = 0.0; // POST basis, when no choice is needed
    bool needsApproval = false;
    double neededPreGainDb = 0.0;             // what PRE would need on the POST basis
    double lowerPostGainDb = 0.0;             // approved: PRE at 0 dB, POST at this
    double limitedPreGainDb = 0.0;            // declined: PRE at this, POST held
    double ceilingDbtp = 0.0;
    std::uint64_t generation = 0;
    bool generationBound = false;
    std::uint64_t proof = 0, preRun = 0;
    bool proofBound = false;
};

MatchPlan planMatch (const MatchResult&, double heldPostDb) noexcept;

// What the user chose for a plan that needs approval; `basis` for a plan that does not.
enum class MatchChoice : std::uint8_t { basis, lowerPost, limitPre };

MatchFailure validateMatchPlan (const MatchPlan&, MatchChoice) noexcept;
struct MatchApplication
{
    MatchFailure failure = MatchFailure::none;
    operator bool() const noexcept { return failure == MatchFailure::none; }
};

// INV-LC16, AUTO after an explicit MATCH. The plan's experimental values (6.2) until listening
// decides them: a step a second, 0.5 dB of tolerance, at most 6 dB from the MATCH.
constexpr double followIntervalSeconds = 1.0;
constexpr double followToleranceDb = 0.5;
constexpr double followReachDb = 6.0;

enum class FollowAction : std::uint8_t
{
    keep,        // within the tolerance, or nothing measured (silence keeps the gain)
    move,        // PRE moves to preGainDb
    stopCeiling, // PRE would pass the ceiling the MATCH approved; AUTO never raises it
    stopReach    // PRE would move more than followReachDb from the MATCH
};

struct FollowStep
{
    FollowAction action = FollowAction::keep;
    double preGainDb = 0.0;
};

// One AUTO step from a fresh measurement on the POST basis: PRE takes the difference that remains
// with POST at its approved attenuation (heldPostDb, 0 or below). approvedPreDb and ceilingDbtp are
// the last explicit MATCH's; POST never moves.
FollowStep followStep (const MatchResult&, double heldPostDb, double approvedPreDb, double ceilingDbtp,
                       double currentPreDb) noexcept;

// Non-RT (message thread). Aligns the latest window of POST's input history with PRE's ring through
// the proven K and measures it with the Local Blind gain policies (BS.1770 loudness, cue true peak).
MatchResult computeMatch (const Ring& ring, const PostRenderer& renderer, std::uint32_t sampleRate,
                          double maximumSeconds = 4.0, double minimumSeconds = 3.0);
}
