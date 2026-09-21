#include "ReferenceRuntimeV2Blind.h"
#include "ReferenceContentAlignment.h"
#include "ReferenceProbeAudio.h"
#include "kirin_hypha_ffi.h"
#include <cmath>

namespace hypha::reference_audition
{
    bool RuntimeV2Blind::prepareWholeSong (const RuntimeACaptureAudio& a,
        const RuntimeSource& source, const RuntimeContentAlignment& alignment)
    {
        if (!enterPreparation()) return false;
        wholeSong = false;
        frozenA.clear(); frozenB.clear();
        rejectionCode.clear();
        resetSession();
        const auto reject = [this] (const char* code) {
            rejectionCode = code;
            int expected = preparing;
            lifecycle.compare_exchange_strong (expected, unavailable, std::memory_order_acq_rel);
            return false;
        };
        if (!alignment.established || a.sampleRateHz < 8000 || a.sampleRateHz > 768000
            || a.channels < 1 || a.channels > 2 || a.frameCount < a.sampleRateHz * 3
            || a.interleaved.size() != static_cast<size_t> (a.frameCount * a.channels)
            || alignment.alignedProbe.size() != a.interleaved.size()
            || !source.measurementSummary || !source.measurementSummary->maximumTruePeakDbtp
            || source.sourceKind != "work_version" || a.cuePcmSha256.length() != 64)
            return reject ("reference_blind_calibration_unavailable");
        KirinReferenceGainFacts facts {};
        if (!kirin_hypha_analyze_reference_gain (a.interleaved.data(), alignment.alignedProbe.data(),
                static_cast<size_t> (a.frameCount), static_cast<std::uint32_t> (a.sampleRateHz),
                static_cast<std::uint32_t> (a.channels), &facts))
            return reject ("reference_blind_gain_unavailable");
        aStartSample = a.startSample;
        frameCount = a.frameCount; // Observation scope; never the playable song length.
        channels = a.channels;
        sampleRateHz = static_cast<int> (a.sampleRateHz);
        bSampleRateHz = static_cast<int> (source.audio.sampleRateHz);
        bStartSample = alignment.sourceStartSample;
        bEndSample = bStartSample + static_cast<std::int64_t> (std::ceil (
            static_cast<long double> (frameCount) * bSampleRateHz / sampleRateHz));
        wholeSourceFrames = source.audio.totalSampleFrames;
        dawRevisionId = a.dawRevisionId;
        aCuePcmSha256 = a.cuePcmSha256;
        bCuePcmSha256 = referenceProbePcmHash (alignment.alignedProbe);
        alignmentCorrelation = alignment.minimumCorrelation;
        alignmentSpreadSamples = alignment.windowSpreadSamples;
        pairedBlockCount = facts.paired_block_count;
        pairedLoudnessDeltaDb = facts.paired_loudness_delta_median_millilu / 1000.0;
        aCueTruePeakDbtp = facts.a_cue_true_peak_millidbtp / 1000.0;
        // The comparison streams the whole B. Headroom must cover its measured
        // whole-source TP, including peaks outside the observed calibration.
        bCueTruePeakDbtp = *source.measurementSummary->maximumTruePeakDbtp;
        if (!std::isfinite (bCueTruePeakDbtp)) return reject ("reference_blind_gain_unavailable");
        const auto plan = planRuntimeV2BlindGain (pairedLoudnessDeltaDb, aCueTruePeakDbtp, bCueTruePeakDbtp);
        aGainDb = plan.aGainDb; bGainDb = plan.bGainDb;
        requiredAAttenuationDb = plan.requiredAAttenuationDb;
        preservedPeakCeilingDbtp = plan.preservedPeakCeilingDbtp;
        liveScratch.setSize (channels, 8192);
        wholeSong = true;
        int expected = preparing;
        return lifecycle.compare_exchange_strong (expected,
            plan.lowerAApprovalRequired ? approvalRequired : prepared, std::memory_order_acq_rel);
    }
}
