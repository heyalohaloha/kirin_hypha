#include "HyphaAbsoluteComponent.h"

#include "HyphaAbsolutePainter.h"

#include <cmath>

namespace hypha
{
namespace
{
    bool validFrame (const KirinAbsoluteView& frame) noexcept
    {
        return frame.status == KIRIN_SPECTRUM_ACTIVE
            && frame.has_data != 0u
            && (frame.channels == 1u || frame.channels == 2u)
            && frame.sample_rate >= 8'000u
            && frame.sample_rate % 10u == 0u
            && frame.aperture_samples == frame.sample_rate / 10u
            && frame.generation != 0u
            && frame.state_epoch_samples
                % static_cast<int64_t> (frame.aperture_samples) == 0
            && frame.presentation_end_samples > frame.state_epoch_samples
            && (std::isnan (frame.lufs_m)
                || (std::isfinite (frame.lufs_m) && frame.lufs_m <= 0.0))
            && (std::isnan (frame.true_peak)
                || (std::isfinite (frame.true_peak) && frame.true_peak <= 24.0))
            && std::isfinite (frame.sharpness) && frame.sharpness >= 0.0;
    }

    bool validBatch (const KirinAbsoluteBatch& value) noexcept
    {
        if (value.count > KIRIN_ABSOLUTE_BATCH_CAPACITY)
            return false;
        if (value.count == 0u)
            return value.latest.has_data == 0u;
        if (! validFrame (value.latest))
            return false;
        const auto historySamples = static_cast<int64_t> (value.latest.sample_rate) * 6;
        for (uint32_t index = 0u; index < value.count; ++index)
        {
            const auto& frame = value.frames[index];
            if (! validFrame (frame)
                || frame.sample_rate != value.latest.sample_rate
                || frame.aperture_samples != value.latest.aperture_samples
                || frame.channels != value.latest.channels)
                return false;
            if (index > 0u
                && frame.presentation_end_samples
                    <= value.frames[index - 1u].presentation_end_samples)
                return false;
        }
        const auto& last = value.frames[value.count - 1u];
        return last.presentation_end_samples == value.latest.presentation_end_samples
            && last.state_epoch_samples == value.latest.state_epoch_samples
            && last.generation == value.latest.generation
            && value.latest.presentation_end_samples
                   - value.frames[0u].presentation_end_samples <= historySamples;
    }
}

AbsoluteComponent::AbsoluteComponent()
{
    setInterceptsMouseClicks (false, false);
    setAccessible (false);
}

void AbsoluteComponent::setAnalysisOwnerNames (const juce::String& names)
{
    if (analysisOwnerNames == names)
        return;
    analysisOwnerNames = names;
    repaint();
}

void AbsoluteComponent::setBatch (const KirinAbsoluteBatch& next)
{
    setBatchAt (next, juce::Time::getMillisecondCounterHiRes());
}

void AbsoluteComponent::setBatchAt (const KirinAbsoluteBatch& next, double nowMs)
{
    if (! validBatch (next))
        return;
    // A short worker re-arm publishes WARMING_UP before the next exact 100 ms fact. Retain the
    // last verified field during that internal transition; switching page calls clearSnapshot(),
    // while IN_USE / UNAVAILABLE still replace the field with their explicit user-facing state.
    if (next.count == 0u
        && next.latest.status == KIRIN_SPECTRUM_WARMING_UP
        && haveBatch && batch.count > 0u)
        return;
    const bool firstPresentation = ! havePendingBatch;
    const bool definitionChanged = havePendingBatch
        && pendingBatch.count > 0u && next.count > 0u
        && (pendingBatch.latest.sample_rate != next.latest.sample_rate
            || pendingBatch.latest.aperture_samples != next.latest.aperture_samples
            || pendingBatch.latest.channels != next.latest.channels
            || pendingBatch.latest.state_epoch_samples != next.latest.state_epoch_samples
            || pendingBatch.latest.generation != next.latest.generation);
    const bool hadNumericSnapshot = haveNumericSnapshot;
    pendingBatch = next;
    havePendingBatch = true;
    bool needsRepaint = firstPresentation || definitionChanged;
    if (firstPresentation || definitionChanged
        || nowMs - lastCurvePresentationMs >= 200.0)
    {
        batch = pendingBatch;
        haveBatch = true;
        lastCurvePresentationMs = nowMs;
        ++curvePresentationCount;
        needsRepaint = true;
    }
    if (next.count == 0u)
    {
        numericSnapshot = {};
        haveNumericSnapshot = false;
        lastNumericPresentationMs = nowMs;
        if (hadNumericSnapshot)
            ++numericPresentationCount;
        needsRepaint = needsRepaint || hadNumericSnapshot;
    }
    else if (definitionChanged || ! haveNumericSnapshot
             || nowMs - lastNumericPresentationMs >= 500.0)
    {
        numericSnapshot = next.latest;
        haveNumericSnapshot = true;
        lastNumericPresentationMs = nowMs;
        ++numericPresentationCount;
        needsRepaint = true;
    }
    if (needsRepaint)
        repaint();
}

void AbsoluteComponent::clearSnapshot()
{
    batch = {};
    pendingBatch = {};
    numericSnapshot = {};
    haveBatch = false;
    havePendingBatch = false;
    haveNumericSnapshot = false;
    lastCurvePresentationMs = 0.0;
    lastNumericPresentationMs = 0.0;
    curvePresentationCount = 0u;
    numericPresentationCount = 0u;
    repaint();
}

void AbsoluteComponent::paint (juce::Graphics& g)
{
    absolute_painter::paint (g, getLocalBounds().toFloat(), {
        batch, numericSnapshot, analysisOwnerNames, haveBatch, haveNumericSnapshot
    });
}
}
