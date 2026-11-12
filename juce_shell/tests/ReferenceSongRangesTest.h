#pragma once

// C の SONG の比べる値：Kirin OS の ranges の「曲全体の範囲」（ReferenceCueMatch.h）。
//  - 範囲は開始の順なので、曲全体の範囲は先頭とは限らない（同じ 0 から始まる短い範囲の後でも見つける）。
//  - 曲全体の範囲の無い古い ranges では SONG の値は無い（Cue の値はそのまま読む）。
//  - Cue と同じ ranges を 1 度読んで、Cue の値と曲全体の値を返す。
#include "KirinLibraryFixture.h"
#include "../src/reference_audition/ReferenceCueMatch.h"
#include "../src/reference_audition/ReferenceRuntimeV2Repository.h"
#include "../src/reference_audition/ReferenceSourceRanges.h"

#include <cmath>

inline void testSongRanges (const juce::File& sandbox)
{
    namespace ref = hypha::reference_audition;
    const auto sameValue = [] (double value, double expected) { return std::abs (value - expected) < 1.0e-6; };
    ref::RuntimeSourceRanges ranges;
    ranges.totalSampleFrames = 96'000;
    ranges.ranges.resize (3);
    ranges.ranges[0].endSample = 30'000;  // 0 から始まる短い範囲（イントロ）が先
    ranges.ranges[1].endSample = 96'000;
    ranges.ranges[2].startSample = 9'600; ranges.ranges[2].endSample = 38'400;
    require (ranges.wholeSong() == &ranges.ranges[1], "the whole-song range is found wherever it sits");
    ranges.ranges.erase (ranges.ranges.begin() + 1);
    require (ranges.wholeSong() == nullptr, "older ranges without it have no whole-song range");

    const auto root = kirin_library_fixture::copy (sandbox, "song-ranges");
    require (root != juce::File(), "the Kirin OS library fixture must be copied");
    ref::RuntimeV2Repository repository (root);
    const auto loaded = repository.refreshLibrary();
    require (loaded.usable() && loaded.workspace->librarySets, "the fixture library must be read");
    ref::RuntimeSource source;
    source.sourceFileSha256 = juce::String::repeatedString ("e", 64);
    source.sourcePcmSha256 = juce::String::repeatedString ("f", 64);
    source.audio = { 48'000, 2, 96'000 };
    // B セットの曲は古い形の ranges（曲全体の範囲が無い）：Cue の値だけ。
    const auto& old = loaded.workspace->librarySets->songSets[0].songs[0];
    const auto oldLevels = ref::readRangeLevels (root, *loaded.workspace, old, old.cues[0], source);
    require (oldLevels.cue && sameValue (oldLevels.cue->integratedLoudness, -14.06) && ! oldLevels.song,
             "an older ranges file gives the Cue's values and no whole-song values");
    // C の Check の曲は曲全体の範囲を持つ（先頭の 0〜96,000）：Cue と別に曲全体の音量・帯域を読む。この曲の Cue は
    // 曲全体なので、Cue の値も同じ範囲から（今までの「Cue が 0〜全体の時は曲全体」と同じ値）。
    auto catalog = source;
    catalog.sourceFileSha256 = juce::String::repeatedString ("a", 64);
    catalog.sourcePcmSha256 = juce::String::repeatedString ("b", 64);
    int found = 0;
    for (const auto& preset : loaded.workspace->presets)
        for (const auto& check : preset.checks)
            for (const auto& candidate : check.candidates)
                for (const auto& cue : candidate.cues)
                {
                    const auto levels = ref::readRangeLevels (root, *loaded.workspace, candidate, cue, catalog);
                    if (! levels.song) continue;
                    require (levels.cue && levels.cue->part == ref::CuePart::whole
                                 && sameValue (levels.cue->integratedLoudness, levels.song->integratedLoudness),
                             "a Cue over the whole song reads the same whole-song range");
                    ++found;
                    // この写しの ranges は 12 帯域（64 帯域でないスペクトルは Cue と同じく使わない）。
                    require (sameValue (levels.song->integratedLoudness, -14.11) && sameValue (levels.song->maximumTruePeakDbtp, -1.0)
                                 && levels.song->part == ref::CuePart::whole && ! levels.song->spectrum,
                             "the whole-song range gives C's whole-song loudness and peak");
                    require (levels.cue.has_value() == ref::readCueLevel (root, *loaded.workspace, candidate, cue, catalog).has_value(),
                             "the Cue's values are read as before");
                }
    require (found > 0, "a C song with a whole-song range was read");
    auto withoutSets = *loaded.workspace;
    withoutSets.librarySets.reset();
    const auto none = ref::readRangeLevels (root, withoutSets, old, old.cues[0], source);
    require (! none.cue && ! none.song, "without sets.json there are no range values");
}
