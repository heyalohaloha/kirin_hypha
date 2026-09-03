#include "PluginEditor.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY

#include <cmath>

namespace
{
hypha::reference_ui::Readiness referenceReadiness (
    hypha::reference_audition::RuntimeState state) noexcept
{
    using Input = hypha::reference_audition::RuntimeState;
    using Output = hypha::reference_ui::Readiness;
    switch (state)
    {
        case Input::disconnected: return Output::disconnected;
        case Input::waiting:      return Output::waiting;
        case Input::verifying:    return Output::verifying;
        case Input::ready:        return Output::ready;
        case Input::rejected:     return Output::rejected;
    }
    return Output::disconnected;
}

juce::String sourceLabel (const juce::String& kind)
{
    if (kind == "work_version") return "WORK VERSION";
    if (kind == "catalog") return "CATALOG";
    return "KIRIN OS";
}

juce::String rejectedStatus (const juce::String& code)
{
    if (code == "source_changed") return "SOURCE CHANGED / PREPARE AGAIN IN KIRIN OS";
    if (code == "source_open_failed" || code == "source_decode_failed")
        return "SOURCE COULD NOT BE OPENED";
    return "PREPARE AGAIN IN KIRIN OS";
}
}

void KirinHyphaEditor::configureReferenceAudition()
{
    referenceView.onSelectA = [this] { processorRef.selectReferenceA(); };
    referenceView.onSelectB = [this]
    {
        const auto& state = referenceView.state();
        if (! processorRef.selectReferenceB (state.aIntegratedLoudness,
                                              state.aMaximumTruePeakDbtp))
            showToast ("Reference B is not ready");
    };
    scaleRoot.addChildComponent (referenceView);
}

void KirinHyphaEditor::layoutReferenceAudition (juce::Rectangle<int> body)
{
    referenceView.setBounds (body);
    referenceView.setVisible (observatoryDomain == hypha::observatory::Domain::reference);
    referenceView.toFront (false);
}

void KirinHyphaEditor::refreshReferenceAudition (const KirinObservatoryFrame& frame,
                                                  bool frameAvailable)
{
    const auto runtime = processorRef.referenceAuditionSnapshot();
    hypha::reference_ui::State state;
    state.readiness = referenceReadiness (runtime.state);
    state.title = runtime.title;
    state.sourceLabel = sourceLabel (runtime.sourceKind);
    state.alignmentLabel = runtime.alignmentMode
            == hypha::reference_audition::AlignmentMode::sampleLock
        ? "PROJECT TIMELINE" : "REFERENCE CUE";
    state.bSelected = runtime.bSelected;
    state.gainLimited = runtime.gainLimited;

    const bool liveA = frameAvailable
        && frame.meter.state != KIRIN_METER_SESSION_EMPTY
        && std::isfinite (frame.meter.lufs_i)
        && std::isfinite (frame.meter.max_true_peak);
    state.aAvailable = runtime.bSelected || liveA;
    state.aIntegratedLoudness = runtime.bSelected
        ? runtime.aIntegratedLoudness
        : liveA ? frame.meter.lufs_i : hypha::reference_ui::unavailableValue();
    state.aMaximumTruePeakDbtp = runtime.bSelected
        ? runtime.aMaximumTruePeakDbtp
        : liveA ? frame.meter.max_true_peak : hypha::reference_ui::unavailableValue();

    if (runtime.bSelected)
    {
        state.adjustedBIntegratedLoudness = runtime.adjustedBIntegratedLoudness;
        state.adjustedBMaximumTruePeakDbtp = runtime.adjustedBMaximumTruePeakDbtp;
        state.loudnessDeltaBMinusA = runtime.loudnessDeltaBMinusA;
        state.truePeakDeltaBMinusA = runtime.truePeakDeltaBMinusA;
        state.appliedGainDb = runtime.appliedGainDb;
    }

    using Runtime = hypha::reference_audition::RuntimeState;
    if (runtime.bSelected)
        state.status = "B AUDITION / PRE DELTA PAUSED";
    else if (runtime.state == Runtime::ready)
        state.status = liveA ? "READY / B FOLLOWS A" : "PLAY A TO MEASURE";
    else if (runtime.state == Runtime::verifying)
        state.status = "VERIFYING SOURCE";
    else if (runtime.state == Runtime::rejected)
        state.status = rejectedStatus (runtime.rejectionCode);
    else if (runtime.state == Runtime::waiting)
        state.status = "OPEN IN HYPHA FROM KIRIN OS";
    else
        state.status = "CONNECT TO A KIRIN OS WORK";
    referenceView.setState (std::move (state));
}

#endif
