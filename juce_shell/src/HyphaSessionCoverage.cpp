#include "HyphaObservatoryView.h"
#include "HyphaObservationEquality.h"
#include <cmath>
namespace hypha::observatory
{
namespace
{
bool covers (const KirinMeterSessionV2& coverage, const KirinMeterSession& meter) noexcept
{
    const auto& session = coverage.session;
    return session.generation == meter.generation
        && session.measurement_epoch == meter.measurement_epoch
        && session.active_frames >= meter.active_frames;
}

auto displayedSummary (const KirinMeterSession& meter) noexcept
{
    return std::tie (meter.lufs_i, meter.lra, meter.max_true_peak, meter.plr, meter.song_psr);
}

bool pendingSummary (const KirinMeterSession& meter, const KirinMeterSessionV2* coverage) noexcept
{
    if (meter.active_frames == 0) return false;
    return coverage == nullptr || ! covers (*coverage, meter)
        || coverage->summary_status == KIRIN_SESSION_SUMMARY_PENDING_TAIL;
}
}

void View::invalidateSessionPresentation()
{
    sessionPresentation.invalidate();
    const bool changed = haveSessionCoverage;
    haveSessionCoverage = false;
    if (changed) repaint (bodyArea);
}

bool View::retainSessionPoll (const KirinObservatoryFrame* frame,
    const KirinMeterSessionV2* coverage, unsigned status, bool packetReady, double nowMs)
{
    // The last reading before a stop reaches the screen even when no later poll changes it.
    if (levelReadout.takeDue (nowMs)) repaint (bodyArea);
    const auto decision = sessionPresentation.decide ({ frame, coverage, status, packetReady }, nowMs);
    if (decision == session_presentation::Decision::retain) return true;
    if (decision == session_presentation::Decision::accept && coverage != nullptr)
        setSessionCoverage (*coverage);
    else
        invalidateSessionPresentation();
    return false;
}

void View::setSessionCoverage (const KirinMeterSessionV2& value)
{
    if (value.version != 2 || value.struct_size != sizeof (value)
        || value.summary_status > KIRIN_SESSION_SUMMARY_PENDING_TAIL) return;
    const bool wasCovered = haveSessionCoverage && frameAvailable
        && covers (sessionCoverage, observatoryFrame.meter);
    const bool isCovered = frameAvailable && covers (value, observatoryFrame.meter);
    const bool changed = ! haveSessionCoverage || wasCovered != isCovered
        || (isCovered && (sessionCoverage.summary_status != value.summary_status
            || ! observation_equality::fields (displayedSummary (sessionCoverage.session),
                                               displayedSummary (value.session))));
    // Keep the entire publication for later frame/inspection adoption, even when not repainting.
    sessionCoverage = value;
    haveSessionCoverage = true;
    if (changed) repaint (bodyArea);
}
const KirinMeterSession& View::cumulativeMeterForDisplay() const noexcept
{
    return haveSessionCoverage && frameAvailable
        && covers (sessionCoverage, observatoryFrame.meter)
        ? sessionCoverage.session : observatoryFrame.meter;
}
bool View::sessionSummaryPending() const noexcept
{
    return frameAvailable && pendingSummary (observatoryFrame.meter,
        haveSessionCoverage ? &sessionCoverage : nullptr);
}
juce::String View::sessionMaximumBoundText (const KirinMeterSession* displayed) const
{
    const auto value = (displayed != nullptr ? *displayed : cumulativeMeterForDisplay()).max_true_peak;
    if (! sessionSummaryPending() || ! std::isfinite (value)) return {};
    auto lower = std::floor (value * 10.0) / 10.0;
    if (std::fpclassify (lower) == FP_ZERO) lower = 0.0;
    return juce::String::fromUTF8 (u8"≥ ")
        + juce::String (lower, 1).replace ("-", juce::String::fromUTF8 (u8"−"));
}
juce::String View::sessionSummaryScope() const
{
    if (! sessionSummaryPending()) return {};
    if (! haveSessionCoverage || ! covers (sessionCoverage, observatoryFrame.meter))
        return "Latest Session input coverage is not confirmed; MAX TP is a confirmed lower bound and PLR is unavailable.";
    return "Session values cover processed audio. The final analysis chunk is pending; MAX TP is a confirmed lower bound and PLR is unavailable.";
}
capture::PresentationStamp View::capturePresentationStamp() const noexcept
{
    if (selectedDomain == Domain::time)
        return { timePresentation.available() ? timePresentation.packet().revision : 0,
                 timePresentation.revision(), analysisPage == analysis_navigation::Page::meters
                     || analysisPage == analysis_navigation::Page::attack };
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
