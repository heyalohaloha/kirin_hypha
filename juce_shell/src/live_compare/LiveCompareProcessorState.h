#pragma once

#include "../local_blind/RtPublicationSlot.h"
#include "LiveCompareClock.h"
#include "LiveCompareMatch.h"
#include "LiveCompareOffset.h"
#include "LiveComparePinResult.h"
#include "LiveCompareSession.h"
#include "LiveCompareSharedRing.h"

#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
enum class StartResult : std::uint8_t
{
    started,
    notPost,           // only POST starts a live session
    notReady,          // writes are not enabled yet, or the previous mapping is still in use
    noPair,            // POST is not paired with a PRE
    unsupportedLayout, // mono/stereo only; AAX stereo only until INV-LC9 (no platform ring either)
    preUnavailable     // PRE's ring is missing, stale, for another rate, or the platform has none
};

struct Status
{
    bool active = false;
    bool preSelected = false;
    bool preAudible = false;
    bool preWaiting = false;  // PRE selected, POST sounding because correspondence is not proven
    bool interrupted = false; // offline render or bypass ended the session; select PRE again
    Verdict verdict = Verdict::noClock;
    float gain = 1.0f;
    float postTarget = 1.0f;  // approved POST attenuation, held after the session until RETURN
    bool contentHeld = false; // INV-LC10: POST until playback stops and restarts
    bool compensationOff = false; // INV-LC8: the host's delay compensation is off
};

// Everything one processor owns for the live compare. PRE uses the ring and the feeder; POST uses
// the ring and the renderer. The message thread publishes and retires the mapping; the Audio
// Thread reads it only through the slot and publishes its observations through the atomics.
struct ProcessorState
{
    local_blind::RtPublicationSlot<SharedRingMapping> ring;
    PreFeeder feeder;
    PostRenderer renderer;
    ContinuousClock clock;
    GapDetector gaps;
    std::atomic<bool> sessionActive { false };
    std::atomic<bool> preSelected { false };
    std::atomic<bool> preAudible { false };
    std::atomic<bool> preWaiting { false };
    std::atomic<bool> preWaitSeen { false }; // any waiting block since the editor last looked
    std::atomic<bool> interrupted { false };
    std::atomic<float> gain { 1.0f };
    std::atomic<std::uint8_t> verdict { 0 };
    PostLevel postLevel;                      // Audio Thread; the message thread configures it
    std::atomic<float> postTarget { 1.0f };   // approved POST attenuation (linear, at most 1)
    std::atomic<float> ceilingLinear { 1.0f }; // PRE guard fixed at MATCH, 10^(C/20)
    std::atomic<bool> guardTripped { false };
    std::atomic<bool> contentHold { false };      // INV-LC10: the content offset jumped
    std::atomic<std::uint32_t> playbackRun { 0 }; // counts stop-to-play transitions
    bool wasPlaying = false;                      // Audio Thread only
    std::atomic<bool> compensationOff { false };  // INV-LC8: the host says delay compensation is off
    bool compensationWasOff = false;              // Audio Thread only
};
}
