#include "HyphaObservatoryView.h"
#include <cmath>
namespace hypha::observatory
{
namespace
{
bool pendingSummary (const KirinMeterSession& meter, const KirinMeterSessionV2* coverage) noexcept
{
    if (meter.active_frames == 0) return false;
    return coverage == nullptr || coverage->session.generation != meter.generation
        || coverage->session.measurement_epoch != meter.measurement_epoch
        || coverage->session.active_frames < meter.active_frames
        || coverage->summary_status == KIRIN_SESSION_SUMMARY_PENDING_TAIL;
}
}

void View::setSessionCoverage (const KirinMeterSessionV2& value)
{
    if (value.version != 2 || value.struct_size != sizeof (value)
        || value.summary_status > KIRIN_SESSION_SUMMARY_PENDING_TAIL) return;
    sessionCoverage = value;
    haveSessionCoverage = true;
    repaint (bodyArea);
}
const KirinMeterSession& View::cumulativeMeterForDisplay() const noexcept
{
    return haveSessionCoverage && frameAvailable
        && sessionCoverage.session.generation == observatoryFrame.meter.generation
        && sessionCoverage.session.measurement_epoch == observatoryFrame.meter.measurement_epoch
        && sessionCoverage.session.active_frames >= observatoryFrame.meter.active_frames
        ? sessionCoverage.session : observatoryFrame.meter;
}
bool View::sessionSummaryPending() const noexcept
{
    return frameAvailable && pendingSummary (observatoryFrame.meter,
        haveSessionCoverage ? &sessionCoverage : nullptr);
}
juce::String View::sessionMaximumBoundText() const
{
    const auto value = cumulativeMeterForDisplay().max_true_peak;
    if (! sessionSummaryPending() || ! std::isfinite (value)) return {};
    auto lower = std::floor (value * 10.0) / 10.0;
    if (std::fpclassify (lower) == FP_ZERO) lower = 0.0;
    return juce::String::fromUTF8 (u8"≥ ")
        + juce::String (lower, 1).replace ("-", juce::String::fromUTF8 (u8"−"));
}
juce::String View::sessionSummaryScope() const
{
    if (! sessionSummaryPending()) return {};
    if (! haveSessionCoverage || sessionCoverage.session.generation != observatoryFrame.meter.generation
        || sessionCoverage.session.measurement_epoch != observatoryFrame.meter.measurement_epoch
        || sessionCoverage.session.active_frames < observatoryFrame.meter.active_frames)
        return "Latest Session input coverage is not confirmed; MAX TP is a confirmed lower bound and PLR is unavailable.";
    return "Session values cover processed audio. The final analysis chunk is pending; MAX TP is a confirmed lower bound and PLR is unavailable.";
}
capture::PresentationStamp View::capturePresentationStamp() const noexcept
{
    if (selectedDomain == Domain::time)
        return { timePresentation.available() ? timePresentation.packet().revision : 0,
                 timePresentation.revision(), true };
    if (selectedDomain == Domain::level && levelInspection.held()
        && levelInspection.packetFrameAvailable)
        return { levelInspection.packetFrame.meter.observed_frames,
                 levelInspection.packetFrame.meter.generation,
                 pendingSummary (levelInspection.packetFrame.meter,
                     levelInspection.haveSessionCoverage ? &levelInspection.sessionCoverage : nullptr) };
    return { frameAvailable ? observatoryFrame.meter.observed_frames : 0,
             frameAvailable ? observatoryFrame.meter.generation : 0,
             ! recordBodyActive() && sessionSummaryPending() };
}
}
