#pragma once

#include "ReferenceRuntimeV2Model.h"

namespace hypha::reference_audition
{
// Kirin OS が manifest の隣に書く library/sets.json（kirin_hypha_reference_library_sets 1.0）を読む。
// 中身は「Hypha に出す」順位を付けた B セット（曲は library preset の候補曲と同じ形）、CHECK セットの順位
// （同じ manifest の Preset を指す）、Cue の値のファイルの索引。
// 無いときは nullopt で rejection は空。別の manifest のもの（書き換えの途中）やファイルの形が壊れている
// ときは nullopt と rejection。読めない B セット・曲・CHECK セット・Cue の値の索引は 1 つずつ飛ばし、
// 残りを返して rejection に理由を入れる（1 つの壊れた項目で全部を失わない）。
// sets は追加の情報なので、読めなくても manifest の library はそのまま使える。
std::optional<RuntimeLibrarySets> readReferenceLibrarySets (const juce::File& root,
                                                            const RuntimeWorkspace& workspace,
                                                            juce::String& rejection);
// 新しい manifest に sets.json がまだ追いついていない・読めないあいだ、前の sets を保つ。CHECK セットの順位は
// 今の manifest にある Preset（同じ revision）だけを残す。
RuntimeLibrarySets carriedLibrarySets (const RuntimeLibrarySets& previous, const RuntimeManifest& manifest);
}
