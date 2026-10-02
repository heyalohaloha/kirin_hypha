#pragma once

#include "ReferenceRuntimeV2Model.h"

namespace hypha::reference_audition
{
// H1: Kirin OS が manifest の隣に書く library/sets.json（kirin_hypha_reference_library_sets 1.0）を読む。
// 中身は「Hypha に出す」順位を付けた B セット（曲は library preset の候補曲と同じ形）、CHECK セットの順位
// （同じ manifest の Preset を指す）、Cue の値のファイルの索引。
// 無いときは nullopt で rejection は空。別の manifest のもの（書き換えの途中）や形が壊れているときは
// nullopt と rejection。sets は追加の情報なので、読めなくても manifest の library はそのまま使える。
std::optional<RuntimeLibrarySets> readReferenceLibrarySets (const juce::File& root,
                                                            const RuntimeWorkspace& workspace,
                                                            juce::String& rejection);
}
