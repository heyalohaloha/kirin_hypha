#pragma once

#include "HyphaLuNeedle.h"
#include "reference_audition/ReferenceAuditionController.h"

// What the LU face reads from Reference (POST). Values are Kirin OS's own: the audition gain
// (MATCH) is never added, and nothing is measured here.
namespace hypha::lu_needle
{
// The role whose song is the 0: the one that is heard (B or C), else the page in view when it is
// B or C, else B's song, else C's.
inline const reference_audition::Snapshot* zeroRole (const reference_audition::Snapshot& runtime) noexcept
{
    const auto* check = runtime.checkSelection.get();
    const auto* song = runtime.referenceSelection.get();
    const bool songChosen = song != nullptr && runtime.selectedSongId.isNotEmpty() && song->candidateId.isNotEmpty();
    const bool checkChosen = check != nullptr && check->candidateId.isNotEmpty();
    if (runtime.audibleComparisonSlot == 3 && songChosen) return song;
    if (runtime.audibleComparisonSlot == 2 && checkChosen) return check;
    if (runtime.comparisonSlot == 3 && songChosen) return song;
    if (runtime.comparisonSlot == 2 && checkChosen) return check;
    return songChosen ? song : checkChosen ? check : nullptr;
}

// Kirin OS's time series holds one value per hop, read at the end of the hop.
inline double samePlaceLufs (const reference_audition::RuntimeDetailedMeasurement& measurement,
                             const char* series, double positionSeconds) noexcept
{
    if (! measurement.loudness || measurement.loudness->hopSamples <= 0
        || measurement.audio.sampleRateHz <= 0 || ! (positionSeconds >= 0.0)) return nan;
    const auto found = measurement.loudness->series.find (series);
    if (found == measurement.loudness->series.end()) return nan;
    const auto sample = positionSeconds * static_cast<double> (measurement.audio.sampleRateHz);
    const auto hops = static_cast<std::int64_t> (std::floor (sample / static_cast<double> (measurement.loudness->hopSamples)));
    if (hops < 1 || static_cast<std::size_t> (hops) > found->second.size()) return nan;
    const auto& value = found->second[static_cast<std::size_t> (hops - 1)];
    return value ? static_cast<double> (*value) / 1000.0 : nan;
}

// readable: Kirin OS's Reference can be read here (the same condition that lets B and C list sets).
inline Facts referenceFacts (const reference_audition::Snapshot& runtime, bool readable, Window window)
{
    Facts facts;
    facts.readable = readable;
    if (! readable) return facts;

    // The chosen set's range: each song's Cue loudness, as it is played on this track.
    for (const auto& set : runtime.songSets)
        if (set.id == runtime.selectedSongSetId)
        {
            std::vector<double> played;
            for (const auto& song : set.facts)
                if (std::isfinite (song.lufsI)) played.push_back (song.lufsI + song.playbackLoudnessOffsetLu);
            facts.band = percentileBand (std::move (played));
        }

    const auto* role = zeroRole (runtime);
    if (role == nullptr) return facts;
    facts.song = role->candidateName;
    facts.role = role == runtime.checkSelection.get() ? "C" : "B";
    facts.playedOffsetLu = role->sourceLoudnessOffsetLu;
    // The Cue's Integrated loudness; the whole song's when the Cue has no value.
    if (role->cueLevelAvailable && std::isfinite (role->cueIntegratedLoudness))
        facts.zeroLufs = role->cueIntegratedLoudness;
    else if (role->measurementAvailable && std::isfinite (role->sourceIntegratedLoudness)
             && role->sourceIntegratedLoudness < 0.0)
        facts.zeroLufs = role->sourceIntegratedLoudness;
    facts.zero = std::isfinite (facts.zeroLufs) ? Zero::ready : Zero::noValue;
    if (facts.zero != Zero::ready) return facts;

    // The song at A's position: only while FOLLOW A places it there, never in LOOP or Blind, and
    // not for the Integrated window (the 0 is already the song's Integrated loudness).
    const bool blind = runtime.blindPhase != reference_audition::BlindPhase::inactive
        || role->blindPhase != reference_audition::BlindPhase::inactive;
    if (! runtime.followA || ! role->followA || blind || window == Window::integrated) return facts;
    if (! role->transportPositionValid) return facts;
    const auto value = role->detailedMeasurement && std::isfinite (role->followPositionSeconds)
        ? samePlaceLufs (*role->detailedMeasurement,
                         window == Window::shortTerm ? "lufs_s_millilu" : "lufs_m_millilu",
                         role->followPositionSeconds)
        : nan;
    if (std::isfinite (value))
    {
        facts.samePlace = SamePlace::shown;
        facts.samePlaceLufs = value + facts.playedOffsetLu;
    }
    else
        facts.samePlace = SamePlace::noValueHere;
    return facts;
}
}
