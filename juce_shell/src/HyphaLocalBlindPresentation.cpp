#include "HyphaLocalBlindComponent.h"
#include "HyphaLocalBlindFailureText.h"
#include "HyphaLocalBlindAdmissionText.h"
#include <cmath>

namespace hypha::local_blind_ui
{
namespace
{
using Phase = local_blind::ProductSessionPhase;
using Failure = local_blind::ProductSessionFailure;
using Answer = local_blind::TrialAnswer;

juce::String timeline (std::int64_t sample, std::uint32_t sampleRate)
{
    if (sampleRate == 0)
        return {};
    const auto signedMilliseconds = static_cast<std::int64_t> (
        std::llround (static_cast<double> (sample) * 1000.0
                      / static_cast<double> (sampleRate)));
    const auto milliseconds = std::abs (signedMilliseconds);
    const auto minutes = milliseconds / 60'000;
    const auto seconds = (milliseconds / 1'000) % 60;
    const auto millis = milliseconds % 1'000;
    return juce::String (sample < 0 ? "-" : "")
        + juce::String (minutes).paddedLeft ('0', 2) + ":"
        + juce::String (seconds).paddedLeft ('0', 2) + "."
        + juce::String (millis).paddedLeft ('0', 3);
}

juce::String rangeText (const local_blind::ProductSessionView& state)
{
    if (state.sampleRate == 0 || state.frames <= 0)
        return {};
    return timeline (state.start, state.sampleRate) + " - "
        + timeline (state.start + state.frames, state.sampleRate);
}


juce::String answerText (Answer answer)
{
    if (answer == Answer::one) return "ANSWER: PREFER 1";
    if (answer == Answer::two) return "ANSWER: PREFER 2";
    if (answer == Answer::noPreference) return "ANSWER: NO PREFERENCE";
    if (answer == Answer::cannotDistinguish) return "ANSWER: CANNOT TELL";
    return {};
}

bool trackStemPolicy (const local_blind::ProductSessionView& state) noexcept
{
    return state.gainPolicy == local_blind::GainMatchPolicy::exactTrackEventEnergyV1;
}

juce::String contextTag (bool trackStem)
{
    return trackStem ? "TRACK / STEM" : "2MIX";
}
}

void Component::refreshPresentation()
{
    const auto phase = current.phase;
    const bool hidden = phase == Phase::armed || phase == Phase::listening;
    const bool trackStem = canChooseContext()
        ? preflightContext == meter_context::MeterContext::trackStem
        : trackStemPolicy (current);
    const auto tag = contextTag (trackStem);
    titleLabel.setText (juce::String (hidden ? "BLIND COMPARE"
                                            : phase == Phase::revealed ? "BLIND RESULT"
                                                                      : "PRE / POST BLIND")
                           + (canChooseContext() ? juce::String() : " / " + tag),
                        juce::dontSendNotification);
    juce::String status;
    juce::String detail;
    juce::String result;
    if (phase == Phase::idle)
    {
        status = "READY TO CAPTURE";
        detail = admissionText (admission);
        if (admission != local_blind::CaptureAdmission::ready) status = "BEFORE CAPTURE";
    }
    else if (phase == Phase::capturing)
    {
        status = "CAPTURING EXACT 4 SECOND RANGE";
        detail = "Keep playback running without seeking, looping, bypassing, or changing PDC.";
    }
    else if (phase == Phase::preparing)
    {
        status = "PREPARING COMPARISON";
        detail = "The live signal remains unchanged while the fixed copies are checked.";
    }
    else if (phase == Phase::ready)
    {
        status = "EXACT RANGE READY";
        detail = "HYPHA: start Blind. DAW: play from before the captured range.";
        if (current.trial.lowerPostApprovalRequired)
        {
            const auto attenuation = std::abs (current.lowerPostGainDb);
            startButton.setButtonText ("LOWER POST " + juce::String (attenuation, 1)
                                       + " dB & START");
            startButton.setTitle ("Approve fixed POST attenuation and start Blind Compare");
            startButton.setDescription (startButton.getTitle());
            startButton.setTooltip (startButton.getTitle());
        }
        else
        {
            startButton.setButtonText ("START BLIND");
            startButton.setTitle (
                "Start the prepared comparison. PRE is matched to POST with fixed gain; DAW Solo and routing stay unchanged");
            startButton.setDescription (startButton.getTitle());
            startButton.setTooltip (startButton.getTitle());
        }
    }
    else if (phase == Phase::armed)
    {
        status = "WAITING FOR CAPTURED RANGE START";
        detail = "DAW: play from before the captured range to hear the full pass.";
    }
    else if (phase == Phase::listening)
    {
        status = current.trial.passComplete
            ? "PASS COMPLETE / SOURCE " + juce::String (current.trial.activeStimulus)
            : current.trial.pendingStimulus != 0
            ? "SWITCHING TO SOURCE " + juce::String (current.trial.pendingStimulus)
            : current.trial.activeStimulus != 0
                ? "LISTENING TO SOURCE " + juce::String (current.trial.activeStimulus)
                : "WAITING FOR AUDIBLE PLAYBACK";
        detail = current.trial.canAnswer
            ? "Both complete passes were heard. Choose your answer, then reveal."
            : current.trial.passComplete
                ? "HYPHA: select the other source. DAW: replay the captured range."
                : "Listen to one complete pass of Source 1 and Source 2.";
        result = answerText (current.trial.answer);
    }
    else if (phase == Phase::revealed)
    {
        status = current.trial.revealedOneSide == 1
            ? "SOURCE 1 = PRE   /   SOURCE 2 = POST"
            : "SOURCE 1 = POST   /   SOURCE 2 = PRE";
        detail = answerText (current.trial.answer) + ". Select END when finished.";
        result = answerText (current.trial.answer);
    }
    else if (phase == Phase::returnPending)
    {
        const bool requested = current.returnFacts.requested();
        status = requested ? "WAITING FOR LIVE OUTPUT" : "RETURN TO LIVE";
        detail = requested ? "DAW: resume audio processing. Closes when Live is confirmed."
            : current.returnFacts.attenuationApplied
                ? "Live level will rise by " + juce::String (std::abs (current.lowerPostGainDb), 1) + " dB."
                : "Return to the unchanged live signal.";
        if (! requested && (current.failure != Failure::none
            || current.trial.failure != local_blind::TrialFailure::none))
        {
            status = "COMPARISON STOPPED";
            detail = failureText (current, preflightContext);
            if (current.returnFacts.attenuationApplied)
                detail += " Live +" + juce::String (std::abs (current.lowerPostGainDb), 1) + " dB.";
        }
        // The exact range keeps the same position during recovery as during listening.
        result = rangeText (current);
    }
    else if (phase == Phase::returned)
    {
        status = "LIVE SIGNAL RESTORED";
        detail = "The comparison is closed. Live output is restored.";
    }
    else
    {
        status = current.failure == Failure::preparation
            && current.preparationFailure == local_blind::PreparationFailure::gainUnavailable
                ? "GAIN MATCH UNAVAILABLE" : "COMPARISON NOT PREPARED";
        detail = failureText (current, preflightContext);
    }
    // One fixed range position throughout preparation, listening, result, and return.
    if (current.frames > 0 && current.sampleRate > 0 )
        result = rangeText (current);
    if (canChooseContext() && preflightPair.isNotEmpty()) result = preflightPair;
    if (actionNotice.isNotEmpty()) result = actionNotice;
    statusLabel.setText (status, juce::dontSendNotification);
    detailLabel.setText (detail, juce::dontSendNotification);
    resultLabel.setText (result, juce::dontSendNotification);

    const bool selectable = (phase == Phase::armed || phase == Phase::listening)
        && current.trial.failure == local_blind::TrialFailure::none;
    for (auto* button : { &sourceOne, &sourceTwo })
    {
        button->setVisible (phase == Phase::armed || phase == Phase::listening
                            || phase == Phase::revealed);
        button->setEnabled (selectable);
    }
    sourceOne.setButtonText (current.trial.heardOneComplete ? "SOURCE 1 / DONE" : "SOURCE 1");
    sourceTwo.setButtonText (current.trial.heardTwoComplete ? "SOURCE 2 / DONE" : "SOURCE 2");
    sourceOne.setToggleState (current.trial.activeStimulus == 1, juce::dontSendNotification);
    sourceTwo.setToggleState (current.trial.activeStimulus == 2, juce::dontSendNotification);

    const bool answersVisible = phase == Phase::listening && current.trial.canAnswer;
    for (auto* button : { &answerOne, &answerTwo, &noPreference, &cannotDistinguish })
    {
        button->setVisible (answersVisible);
        button->setEnabled (answersVisible && current.trial.canAnswer);
    }
    answerOne.setToggleState (current.trial.answer == Answer::one, juce::dontSendNotification);
    answerTwo.setToggleState (current.trial.answer == Answer::two, juce::dontSendNotification);
    noPreference.setToggleState (current.trial.answer == Answer::noPreference,
                                 juce::dontSendNotification);
    cannotDistinguish.setToggleState (current.trial.answer == Answer::cannotDistinguish,
                                      juce::dontSendNotification);

    startButton.setVisible (phase == Phase::ready);
    const bool repairPair = admission == local_blind::CaptureAdmission::pairRequired;
    const bool repairKeep = admission == local_blind::CaptureAdmission::keepBusy;
    repairButton.setVisible (canChooseContext() && (repairPair || repairKeep));
    repairButton.setButtonText (repairPair ? "SELECT PRE" : "BACK TO KEEP");
    captureButton.setVisible (canChooseContext() && ! repairButton.isVisible());
    captureButton.setEnabled ((phase == Phase::idle || current.canRecapture)
        && admission == local_blind::CaptureAdmission::ready);
    captureButton.setButtonText (phase == Phase::failed ? "CAPTURE AGAIN" : "CAPTURE 4 S");
    contextChoice.setVisible (canChooseContext());
    contextChoice.setAccessible (canChooseContext());
    revealButton.setVisible (answersVisible);
    revealButton.setEnabled (current.trial.canAnswer && current.trial.answer != Answer::none);
    stopButton.setVisible (phase == Phase::capturing || phase == Phase::preparing
                           || phase == Phase::ready || phase == Phase::armed
                           || phase == Phase::listening || phase == Phase::revealed);
    stopButton.setButtonText (phase == Phase::capturing || phase == Phase::preparing
                                ? "CANCEL" : phase == Phase::revealed ? "END" : "STOP");
    returnButton.setVisible (phase == Phase::returnPending);
    returnButton.setEnabled (! current.returnFacts.requested());
    closeButton.setVisible (phase == Phase::idle || phase == Phase::returned
                            || phase == Phase::failed);
    closeButton.setButtonText (phase == Phase::idle ? "BACK" : "CLOSE");
}

}
