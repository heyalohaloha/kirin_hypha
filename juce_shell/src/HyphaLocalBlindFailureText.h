#pragma once
#include "HyphaLocalBlindUiContract.h"
#include "HyphaMeterContext.h"
namespace hypha::local_blind_ui
{
using Failure = local_blind::ProductSessionFailure;
inline juce::String failureText (const local_blind::ProductSessionView& state,
                                meter_context::MeterContext selected)
{
    if (state.failure == Failure::pairChanged)
        return "The selected pair changed. Return to the live signal, then capture again.";
    if (state.failure == Failure::captureRequest)
        return "The exact range could not be scheduled. Keep playback running and try again.";
    if (state.failure == Failure::captureResult)
        return "The exact PRE and POST range was not completed.";
    if (state.failure == Failure::preparation)
    {
        if (state.preparationFailure == local_blind::PreparationFailure::gainUnavailable)
            return selected == meter_context::MeterContext::trackStem
                ? "Capture a section where both PRE and POST contain audible events."
                : "Capture a busier section. For short sounds, choose TRACK / STEM.";
        return "The comparison could not be prepared. Capture again.";
    }
    if (state.failure == Failure::publication)
        return "The prepared comparison could not be made available.";
    switch (state.trial.failure)
    {
        case local_blind::TrialFailure::format:
            return "The channel layout or sample rate changed.";
        case local_blind::TrialFailure::transport:
            return "Playback stopped, bypassed, or entered offline render.";
        case local_blind::TrialFailure::epochs:
            return "The comparison ownership changed.";
        case local_blind::TrialFailure::discontinuity:
            return "Playback jumped outside the exact captured sequence.";
        case local_blind::TrialFailure::range:
            return "Playback did not begin at the captured range start.";
        case local_blind::TrialFailure::clock:
            return "The host clock or delay compensation changed. Capture again.";
        case local_blind::TrialFailure::none:
            break;
    }
    return "The comparison stopped before it could be completed.";
}

}
