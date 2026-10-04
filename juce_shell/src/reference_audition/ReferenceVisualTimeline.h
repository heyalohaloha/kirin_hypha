#pragma once
#include <cmath>
#include "ReferenceACaptureModel.h"
#include "ReferenceKirinSpectrum.h"
#include "ReferenceKirinFingerprint.h"
#include <limits>
#include "ReferenceRuntimeV2Measurement.h"
#include "ReferenceTonalRepository.h"
#include "kirin_hypha_reference_visual_ffi.h"
namespace hypha::reference_audition
{
struct VisualBinding
{
    std::shared_ptr<const RuntimeSource> source;
    std::shared_ptr<const RuntimeDetailedMeasurement> overview;
    juce::String key, presetId, presetRevisionId, checkId;
    std::int64_t hostRate = 0, hostAnchor = 0, sourceAnchor = 0, hostPosition = -1;
    int channels = 0;
    bool aligned = false, hidden = false, hostPositionValid = false;
    bool mapPosition (std::int64_t host, std::int64_t& output) const noexcept
    {
        constexpr auto limit = std::numeric_limits<std::int64_t>::max() / 4;
        for (const auto value : {host, hostAnchor, sourceAnchor}) if (value < -limit || value > limit) return false;
        output = host - hostAnchor + sourceAnchor; return true;
    }
    double gainDb = 0.0;
    bool matched = false;
    std::shared_ptr<const ACaptureReceipt> captureEvidence;
    // OS tonal artifacts aggregate source-rate samples, never host-rate playback positions.
    std::int64_t sourceCueStartSample = 0, sourceCueEndSample = 0;
    int matchWindowBlocks = 100; // H12: その役の A 側の窓（100 ms のブロック数。C は Cue と同じ長さ）
    double cuePlayheadSeconds = std::numeric_limits<double>::quiet_NaN(); // H12: 鳴っている Cue の位置（ループは折り返す）
};
struct VisualPairBin
{
    KirinReferenceVisualBin a {}, b {};
    std::uint64_t pass = 0;
};
struct VisualTimeline
{
    VisualBinding binding;
    std::shared_ptr<const ACaptureData> capture;
    std::vector<std::uint8_t> revisited;
    std::vector<VisualPairBin> bins;
    KirinReferenceTonalSnapshot tonal {};
    CaptureTonalSummary tonalCaptureRange;
    std::shared_ptr<const ReferenceTonalCurve> tonalReference;
    std::shared_ptr<const ReferenceTonalCurve> tonalGenre;
    std::shared_ptr<const KirinSpectrumWindow> aKirin; // H12: A の直近の窓（Kirin OS の Cue と同じ定義）
    // 2026-10-04：範囲の帯。A の直近 60 秒までの 100 ms の bin（古い順）。比べる側の hop にまとめ直して使う。
    std::shared_ptr<const std::vector<KirinReferenceVisualBin>> aTicks;
    int aTickChannels = 0;
    // V の画面の範囲の帯：位置合わせで対応した同じ区間の A と V の 100 ms の bin（直近 30 秒、同じフレーム）。
    std::shared_ptr<const std::vector<KirinReferenceVisualBin>> aPairTicks, vPairTicks;
    int pairTickChannels = 0;
    // H13: V の画面の Check のタブ。位置合わせで対応した同じ区間の A と V（直近 30 秒、同じ定義）。
    std::shared_ptr<const KirinSpectrumWindow> aPairKirin, vPairKirin;
    // H7: A の直近 30 秒の Kirin 指紋と、その最後の区切りの位置（曲の頭から 100 ms 単位）。V の自動特定に使う。
    std::shared_ptr<const KirinFingerprint> aFingerprint;
    std::int64_t aFingerprintEndTick = -1;
    std::int64_t hop = 0;
    std::uint64_t pass = 0, revision = 0;
    bool observing = false, pairedObserving = false, tonalAvailable = false;
    static std::int64_t outputSample (std::int64_t source, std::int64_t sourceRate, std::int64_t hostRate) noexcept
    {
        if (source < 0 || sourceRate < 8000 || sourceRate > 768000 || hostRate < 8000 || hostRate > 768000) return -1;
        const auto whole = source/sourceRate, remainder = source%sourceRate;
        const auto fraction = (remainder*hostRate+sourceRate-1)/sourceRate;
        if (whole > (std::numeric_limits<std::int64_t>::max()-fraction)/hostRate) return -1;
        return whole*hostRate+fraction;
    }
    std::int64_t boundary (size_t index) const noexcept
    {
        if (!binding.source || hop < 1 || index > size_t(std::numeric_limits<std::int64_t>::max()/hop)) return -1;
        return outputSample (std::int64_t(index)*hop, binding.source->audio.sampleRateHz, binding.hostRate);
    }
    double endpoint(size_t i) const noexcept
    { return capture ? double(capture->bins[i].offset+capture->bins[i].value.frames)/capture->rate
        : binding.source ? double(std::min(std::int64_t(i+1)*hop,binding.source->audio.totalSampleFrames))/binding.source->audio.sampleRateHz : 0; }
    double duration() const noexcept
    { return capture ? capture->duration() : binding.source ? double (binding.source->audio.totalSampleFrames) / binding.source->audio.sampleRateHz : 0.0; }
};
}
