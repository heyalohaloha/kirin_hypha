#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "kirin_hypha_reference_visual_ffi.h"
#include <atomic>
#include <memory>
#include <vector>
#include <limits>
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
    double duration() const { return rate > 0 ? double(frames)/rate : 0; }
};
struct ACaptureState
{
    ACapturePhase phase = ACapturePhase::idle;
    std::shared_ptr<const ACaptureData> shown, held;
    juce::String message, encoded;
    std::vector<std::uint8_t> revisited; // 0 unknown, 1 matching input fingerprint, 2 changed input
    juce::String revisitedWork;
};
// A UI may outlive its processor. Commands only target this mailbox; no dangling callbacks.
class ACaptureAccess
{
public:
    enum Command { none, start, finish, cancel };
    bool request(Command command) { int expected=none; if(!alive || !pending.compare_exchange_strong(expected,int(command))) return false; wake.signal(); return true; }
    ACaptureState snapshot() const { const juce::ScopedLock lock(mutex); return state; }
    void publish(ACaptureState value) { const juce::ScopedLock lock(mutex); state=std::move(value); }
    juce::WaitableEvent wake; // UI/control notification only; never signalled from the audio callback.
    std::atomic<std::uint64_t> framesProcessed{0};
    std::atomic<int> pending {none};
    std::atomic<bool> capturedView {false}, active {false}, alive {true}, analysisAvailable {false};
private:
    mutable juce::CriticalSection mutex;
    ACaptureState state;
};
struct ACaptureReceipt { juce::String work; std::int64_t hostPosition=0; int rate=0; bool verified=false; };
std::uint64_t captureHash(std::uint64_t, const float*, size_t) noexcept;
juce::String encodeACapture(const ACaptureData&);
std::shared_ptr<const ACaptureData> decodeACapture(const juce::String&);
void mergeACaptureBins(std::vector<ACaptureBin>&, int channels);
}
