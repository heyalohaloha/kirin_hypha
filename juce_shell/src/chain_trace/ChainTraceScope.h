#pragma once
#include "../live_compare/LiveCompareProcessorState.h"

namespace hypha::chain_trace
{
// Lives across all comparison paths, including their early returns. Input has already been
// measured; output is copied only after every possible audition has finished.
class Scope
{
public:
    Scope (live_compare::ProcessorState& stateIn, const float* const* samplesIn,
           int channels, int frames, double rate, bool post, int wrapper,
           const HostProcessClock& host, bool bypassed, bool offline, int precisionBits = 32) noexcept
        : state (stateIn), samples (samplesIn), recorder (state.trace.get())
    {
        if (recorder == nullptr) return;
        if (auto* row = recorder->begin (samples, channels, frames))
        {
            row->host = host; row->role = post ? 1 : 0; row->wrapper = wrapper;
            row->rate = rate; row->bypassed = bypassed; row->offline = offline; row->precisionBits = precisionBits;
            row->preGain = state.gain.load (std::memory_order_relaxed);
            row->postGain = state.postLevel.value();
        }
    }
    ~Scope()
    {
        if (recorder == nullptr) return;
        if (auto* row = recorder->row())
        {
            row->reset = state.hostResetGeneration.load (std::memory_order_relaxed);
            row->active = state.sessionActive.load (std::memory_order_relaxed);
            row->selected = state.selection.command().pre();
            row->blind = state.blind.command().active();
            row->contentHeld = state.contentHold.held();
            // renderSeen carries the exact gains consumed by that renderer invocation.
            if (! row->renderSeen) row->postGain = state.postLevel.value();
        }
        recorder->finish (samples);
    }
private:
    live_compare::ProcessorState& state;
    const float* const* samples;
    Recorder* recorder;
};
}
