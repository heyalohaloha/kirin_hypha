#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "kirin_hypha_reference_visual_ffi.h"
#include <atomic>
#include <memory>
#include <vector>
#include <limits>
#include "ReferenceACaptureStore.h"
#include "ReferenceCaptureEvidence.h"
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
// A UI may outlive its processor. Commands only target this mailbox; no dangling callbacks.
class ACaptureAccess
{
public:
    enum Command { none, start, finish, cancel };
    bool request(Command command)
    {
        const juce::ScopedLock lock(mutex);
        if(!alive || pending!=none || (command==start && active)) return false;
        if(command==start) commandGeneration=store.beginAttempt();
        pending=command; wake.signal(); return true;
    }
    Command takeCommand(std::uint64_t& generation)
    { const juce::ScopedLock lock(mutex); generation=commandGeneration; return Command(pending.exchange(none)); }
    std::uint64_t beginRestore(const juce::String& payload,bool shown)
    { const juce::ScopedLock lock(mutex); const auto token=store.beginRestore(payload); capturedView=shown; return token; }
    void presentIfCurrent(std::uint64_t token,bool shown)
    { const juce::ScopedLock lock(mutex); if(store.isCurrent(token)) capturedView=shown; }
    void publish(ACaptureState value,std::uint64_t token)
    { const juce::ScopedLock lock(mutex); if(store.isCurrent(token)) state=std::move(value); }
    ACaptureStore store;
    ACaptureState snapshot() const { const juce::ScopedLock lock(mutex); return state; }
    void publish(ACaptureState value) { const juce::ScopedLock lock(mutex); state=std::move(value); }
    juce::WaitableEvent wake; // UI/control notification only; never signalled from the audio callback.
    std::atomic<std::uint64_t> framesProcessed{0},currentTimingEpoch{0};
    std::atomic<int> pending {none};
    std::atomic<bool> capturedView {false}, active {false}, alive {true}, analysisAvailable {false};
private:
    mutable juce::CriticalSection mutex;
    ACaptureState state;
    std::uint64_t commandGeneration=0;
};
std::uint64_t captureHash(std::uint64_t, const float*, size_t) noexcept;
juce::String encodeACapture(const ACaptureData&);
std::shared_ptr<const ACaptureData> decodeACapture(const juce::String&);
void mergeACaptureBins(std::vector<ACaptureBin>&, int channels);
}
