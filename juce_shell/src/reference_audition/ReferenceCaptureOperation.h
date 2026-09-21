#pragma once
#include <cstdint>
#include <optional>
#include <juce_core/juce_core.h>
namespace hypha::reference_audition
{
enum class CaptureOperationPhase { idle, starting, armed, capturing, finalizing, restoring, closed };
enum class CaptureBlindOwner { none, version, local };
enum class CaptureRequestResult { accepted, inProgress, stale, unavailable };
struct CaptureOperationView
{
    CaptureOperationPhase phase=CaptureOperationPhase::idle;
    std::uint64_t id=0;
    bool cancellation=false,committed=false;
    bool busy() const noexcept { return phase!=CaptureOperationPhase::idle && phase!=CaptureOperationPhase::closed; }
    bool canStart() const noexcept { return phase==CaptureOperationPhase::idle; }
};
enum class CaptureOutcome { none, captured, cancelled, unavailable, interrupted, limit, saveFailed, restoreFailed };
struct CaptureAttemptOutcome
{
    CaptureOutcome kind=CaptureOutcome::none;
    std::uint64_t operation=0;
    juce::String retainedCapture;
};
}
