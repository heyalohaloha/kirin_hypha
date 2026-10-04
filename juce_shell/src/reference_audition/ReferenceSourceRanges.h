#pragma once

#include "ReferenceCuePart.h"
#include "ReferenceRuntimeV2Model.h"

#include <array>

namespace hypha::reference_audition
{
// H1: Kirin OS の Cue の値のファイル（ranges/<sha256>.json、kirin_hypha_reference_ranges 1.3）。
// Kirin OS が解析のときに残す 100 ms ごとの値から計算した、Cue ごとの Integrated（BS.1770 のゲートつき）・
// 最大 True Peak・スペクトル・4 帯域 Balance と、曲の自動区間（サビ候補つき）と Kirin 指紋。
struct RuntimeRangeValues
{
    std::int64_t startSample = 0;
    std::int64_t endSample = 0;
    std::optional<std::int64_t> lufsIMilliLu;          // ゲートを通るブロックがなければ空
    std::optional<std::int64_t> maxTruePeakMilliDbtp;  // 無音だけなら空
    std::optional<std::array<std::int64_t, 4>> balanceMilliDbfs;
    std::int64_t spectrumFrameCount = 0;               // スペクトルが無ければ 0（下の 3 本も空）
    std::vector<std::int64_t> spectrumP10MilliDbfs;
    std::vector<std::int64_t> spectrumMedianMilliDbfs;
    std::vector<std::int64_t> spectrumP90MilliDbfs;
};

struct RuntimeSectionFamily
{
    bool chorusCandidate = false;
    int representative = 0;
    std::int64_t liftMilliLu = 0;
    std::int64_t similarityMilli = 0;
    std::vector<std::pair<std::int64_t, std::int64_t>> occurrences;  // start_sample, end_sample
};

struct RuntimeSourceRanges
{
    juce::String sha256File;
    juce::String sha256Pcm;
    std::int64_t sampleRateHz = 0;
    std::int64_t channels = 0;
    std::int64_t totalSampleFrames = 0;
    std::vector<double> spectrumBandCentersHz;  // スペクトルを持たない長い音源では空
    bool hasSections = false;
    juce::String sectionsStatus;                // found / not_found / unavailable
    juce::String sectionsReason;
    juce::String chorusReason;
    std::vector<RuntimeSectionFamily> sectionFamilies;
    std::int64_t fingerprintTicks = 0;          // 指紋が無ければ 0
    juce::MemoryBlock fingerprintChromaSigns;   // ticks × 2 bytes
    juce::MemoryBlock fingerprintLoudness;      // ticks bytes
    std::vector<RuntimeRangeValues> ranges;

    const RuntimeRangeValues* find (std::int64_t start, std::int64_t end) const
    {
        for (const auto& range : ranges)
            if (range.startSample == start && range.endSample == end) return &range;
        return nullptr;
    }
};

// Cue の範囲が曲のどの部分か。サビ候補は自動区間の回と同じ範囲かで見分ける（Cue の名前は利用者の言語に
// 訳されていることがあるので頼らない）。最も大きい 30 秒は Kirin OS が付ける名前でだけ見分ける。
inline CuePart cuePartOf (const RuntimeSourceRanges& ranges, std::int64_t start, std::int64_t end,
                          const juce::String& label)
{
    if (end <= start) return CuePart::unknown;
    if (start == 0 && end >= ranges.totalSampleFrames) return CuePart::whole;
    for (const auto& family : ranges.sectionFamilies)
        if (family.chorusCandidate)
            for (const auto& occurrence : family.occurrences)
                if (occurrence.first == start && occurrence.second == end) return CuePart::chorus;
    return label == "Loudest 30 s" ? CuePart::loudest : CuePart::cue;
}

// 受け取り（relative_path・sha256・bytes）どおりのファイルかを確かめてから読む。
bool readReferenceSourceRanges (const juce::File& root, const RuntimeContentReceipt& receipt,
                                RuntimeSourceRanges& result);
}
