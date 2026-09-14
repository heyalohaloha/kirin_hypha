#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "kirin_hypha_reference_visual_ffi.h"
#include <atomic>
#include <memory>
#include <vector>
#include <limits>
#include "ReferenceACaptureStore.h"
#include "ReferenceCaptureEvidence.h"
#include "ReferenceCaptureOperation.h"
namespace hypha::reference_audition
{
enum class ACapturePhase { idle, armed, capturing, finalizing, held, partial };
struct ACaptureBin { std::uint64_t offset = 0; KirinReferenceVisualBin value {}; double truePeak = 0; std::uint64_t fingerprint = 0; };
struct ACaptureData
{
    juce::String id, receiver, verifiedWork;
    std::int64_t created = 0, hostStart = 0;
    std::uint64_t frames = 0, hop = 0, revision = 0;
    int rate = 0, channels = 0, clockSource = 0;
    bool complete = false, restored = false;
    double integrated = std::numeric_limits<double>::quiet_NaN(), maximumTruePeak = 0;
    std::vector<ACaptureBin> bins;
    std::vector<KirinReferenceCaptureUnit> units;
    std::vector<CaptureBindingReceipt> bindings;
    // Never serialized: a reopened processor must establish its own time-axis evidence.
    std::uint64_t runtimeToken=0,inputConfiguration=0,timingEpoch=0;
    CaptureClockSignature clockSignature;
    int terminationReason=0;
    double duration() const { return rate > 0 ? double(frames)/rate : 0; }
};
struct ACaptureState
{
    ACapturePhase phase = ACapturePhase::idle;
    CaptureOperationView operation;
    CaptureAttemptOutcome outcome;
    std::shared_ptr<const ACaptureData> shown, held;
    juce::String message, encoded;
    std::vector<std::uint8_t> revisited; // 0 unknown, 1 matching input fingerprint, 2 changed input
    juce::String revisitedWork;
    std::vector<std::uint8_t> unitStatus; // 0 unknown, 1 exact, 2 material difference, 3 raw difference
    bool timingVerified=false,observationFresh=false;
    std::uint64_t observationPass=0,confirmedTimingEpoch=0;
    std::vector<std::uint64_t> unitPass;
    std::vector<std::int64_t> unitCheckedAt;
    std::int64_t checkedAt=0;
};
std::uint64_t captureHash(std::uint64_t, const float*, size_t) noexcept;
juce::String encodeACapture(const ACaptureData&);
std::shared_ptr<const ACaptureData> decodeACapture(const juce::String&);
void mergeACaptureBins(std::vector<ACaptureBin>&, int channels);
}

#include "ReferenceACaptureAccess.h"
