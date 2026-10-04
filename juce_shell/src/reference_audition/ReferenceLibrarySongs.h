#pragma once

#include "ReferenceRuntimeV2Model.h"

namespace hypha::reference_audition
{
// B（REF）の曲。Kirin OS が「Hypha に出す」にした B セット（sets.json、最大 3）の曲を、V の Version と
// 同じく 1 Check・1 曲の合成の Preset にして、既存の選択・準備・再生・MATCH の仕組みでそのまま鳴らす。
// C の Preset・Check の一覧と V の一覧には出さない。試聴の記録は書かない（Kirin OS の履歴の形が無いため）。
juce::String referenceSongEntryId (const juce::String& songSetId, const juce::String& candidateId);

// 今の workspace.librarySets から、曲の Preset を作り直す（前の曲の Preset は除く）。
// あわせて、曲の既定の Cue の Kirin OS の値（ranges）を読んで librarySets->songFacts に置く。
void applyLibrarySongEntries (const juce::File& root, RuntimeWorkspace&);

// B の曲の選択の ID（Preset／Check／候補）。曲の Preset の ID と Check の ID は同じ。
inline juce::String referenceSongSelectionId (const juce::String& entryId, const juce::String& candidateId)
{
    return entryId + "/" + entryId + "/" + candidateId;
}
}
