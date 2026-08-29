#include "../src/HyphaUiContract.h"
#include "../src/HyphaDisplayContract.h"
#include "../src/HyphaClockSourceContract.h"
#include "../src/HyphaSignalStateContract.h"
#include "../src/HyphaSpectrumUiContract.h"
#include "../src/pre_display/PreDisplayClock.h"
#include "../src/pre_display/PreDisplayProjection.h"
#include "../../crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

namespace ui = hypha::ui_contract;
namespace display = hypha::display_contract;
namespace clockSource = hypha::clock_source_contract;
namespace signalState = hypha::signal_state_contract;

namespace
{
    constexpr bool hasArea (ui::Rect r)
    {
        return r.width > 0 && r.height > 0;
    }

    constexpr bool fitsEditor (ui::Rect r)
    {
        return r.x >= 0 && r.y >= 0
            && ui::right (r) <= ui::editorWidth
            && ui::bottom (r) <= ui::editorHeight;
    }

    constexpr bool fitsWithin (ui::Rect r, int width, int height)
    {
        return r.x >= 0 && r.y >= 0
            && ui::right (r) <= width
            && ui::bottom (r) <= height;
    }

    constexpr bool overlaps (ui::Rect a, ui::Rect b)
    {
        return a.x < ui::right (b) && b.x < ui::right (a)
            && a.y < ui::bottom (b) && b.y < ui::bottom (a);
    }

    constexpr ui::Rect scaled (ui::Rect r, int scale)
    {
        return { r.x * scale, r.y * scale, r.width * scale, r.height * scale };
    }

    double linearChannel (std::uint32_t channel)
    {
        const auto value = static_cast<double> (channel) / 255.0;
        return value <= 0.04045 ? value / 12.92
                                : std::pow ((value + 0.055) / 1.055, 2.4);
    }

    double luminance (std::uint32_t argb)
    {
        return 0.2126 * linearChannel ((argb >> 16) & 0xffu)
             + 0.7152 * linearChannel ((argb >> 8) & 0xffu)
             + 0.0722 * linearChannel (argb & 0xffu);
    }

    double contrastRatio (std::uint32_t a, std::uint32_t b)
    {
        const auto aLuminance = luminance (a);
        const auto bLuminance = luminance (b);
        const auto lighter = aLuminance > bLuminance ? aLuminance : bLuminance;
        const auto darker = aLuminance > bLuminance ? bLuminance : aLuminance;
        return (lighter + 0.05) / (darker + 0.05);
    }
}

int main()
{
    hypha::pre_display::ClockTap preDisplayClock;
    hypha::pre_display::ClockSnapshot preDisplaySnapshot;
    assert (! preDisplayClock.read (preDisplaySnapshot));
    preDisplayClock.publish (-512, 48'000.0, 512, true,
                             hypha::pre_display::ClockSource::projectTimeline);
    assert (preDisplayClock.read (preDisplaySnapshot));
    assert (preDisplaySnapshot.generation == 1);
    assert (preDisplaySnapshot.positionSamples == -512);
    assert (preDisplaySnapshot.sampleRate == 48'000.0);
    assert (preDisplaySnapshot.blockFrames == 512);
    assert (preDisplaySnapshot.playing);
    assert (preDisplaySnapshot.source == hypha::pre_display::ClockSource::projectTimeline);
    static_assert (hypha::pre_display::canProjectGuideTime (
        hypha::pre_display::ClockSource::projectTimeline));
    static_assert (! hypha::pre_display::canProjectGuideTime (
        hypha::pre_display::ClockSource::audioRenderTimeline));
    static_assert (! hypha::pre_display::canProjectGuideTime (
        hypha::pre_display::ClockSource::unknown));
    std::int64_t projectedNanoseconds = 0;
    assert (hypha::pre_display::projectSamplesToNanoseconds (48'000, 48'000.0,
                                                             projectedNanoseconds));
    assert (projectedNanoseconds == 1'000'000'000);
    assert (hypha::pre_display::projectSamplesToNanoseconds (-24'000, 48'000.0,
                                                             projectedNanoseconds));
    assert (projectedNanoseconds == -500'000'000);
    assert (! hypha::pre_display::projectSamplesToNanoseconds (1, 0.0, projectedNanoseconds));
    static_assert (hypha::pre_display::containsHalfOpen (10, 20, 10));
    static_assert (! hypha::pre_display::containsHalfOpen (10, 20, 20));
    std::int64_t sourceNanoseconds = 0;
    assert (hypha::pre_display::subtractNanoseconds (2'000, 500, sourceNanoseconds));
    assert (sourceNanoseconds == 1'500);
    assert (! hypha::pre_display::subtractNanoseconds (
        std::numeric_limits<std::int64_t>::min(), 1, sourceNanoseconds));
    assert (! hypha::pre_display::subtractNanoseconds (
        std::numeric_limits<std::int64_t>::max(), -1, sourceNanoseconds));
    static_assert (hypha::pre_display::saturatingAddNanoseconds (
        std::numeric_limits<std::int64_t>::max() - 1, 2)
        == std::numeric_limits<std::int64_t>::max());
    static_assert (ui::editorWidth == 300 && ui::editorHeight == 200);
    static_assert (ui::titleFontHeight == 20.0f);
    static_assert (ui::metricLabelFontHeight >= 12.0f);
    static_assert (ui::metricValueFontHeight >= 17.0f);
    static_assert (ui::metricUnitFontHeight >= 12.0f);
    static_assert (ui::nameFontHeight >= 16.0f);
    static_assert (ui::pairStatusFontHeight >= 13.0f);
    static_assert (ui::feedbackFontHeight >= 13.0f);
    static_assert (ui::preDisplayPresentationHz == 10);
    static_assert (ui::spectrumPresentationHz == 30);
    static_assert (ui::menuFontHeight >= 16.0f);
    static_assert (ui::pairMenuItemHeight >= 28);
    static_assert (ui::pairMenuMinimumWidth >= ui::editorWidth);
    static_assert (ui::pairMenuMaximumColumns == 1);
    static_assert (ui::background == 0xff0d0f1a);
    static_assert (ui::normal == 0xffe0e0e0);
    static_assert (ui::muted == 0xff606060);
    static_assert (ui::preDisplayContextDetail == 0xff898989);
    static_assert (ui::flora == 0xffd4a043);
    static_assert (ui::spectrumDelta == 0xff75d6e8);
    static_assert (ui::spectrumDeltaBright == 0xffcdeff5);
    static_assert (ui::spectrumPre == 0xff74808f);
    static_assert (ui::spectrumPost == 0xffa695d6);
    static_assert (ui::spectrumLegendFontHeight >= 8.5f);
    static_assert (ui::spectrumDeltaLegendLabelX + ui::spectrumDeltaLegendLabelWidth
                   < ui::spectrumPreLegendLabelX);
    static_assert (ui::spectrumPreLegendSampleWidth == 0);
    static_assert (ui::spectrumPostLegendSampleWidth == 0);
    static_assert (ui::spectrumPreStrokeWidth >= ui::spectrumPostStrokeWidth);
    static_assert (ui::spectrumPreCurveAlpha > ui::spectrumPostCurveAlpha);
    static_assert (ui::spectrumPostGlowStrokeWidth > ui::spectrumPostStrokeWidth);
    static_assert (ui::spectrumPostGlowAlpha < ui::spectrumPostCurveAlpha);
    static_assert (ui::spectrumDeltaLegendAlpha > ui::spectrumPreLegendAlpha);
    static_assert (ui::spectrumPreLegendAlpha > ui::spectrumPostLegendAlpha);
    static_assert (ui::spectrumSizePresets.size() == 3);
    static_assert (ui::spectrumSizePresets[0].width == 300
                   && ui::spectrumSizePresets[0].height == 200);
    static_assert (ui::spectrumSizePresets[1].width == 375
                   && ui::spectrumSizePresets[1].height == 250);
    static_assert (ui::spectrumSizePresets[2].width == 450
                   && ui::spectrumSizePresets[2].height == 300);
    static_assert (ui::spectrumPlotBounds().x == 10
                   && ui::spectrumPlotBounds().y == 67
                   && ui::spectrumPlotBounds().width == 280
                   && ui::spectrumPlotBounds().height == 79);
    static_assert (ui::spectrumPlotBounds (375, 250).height == 129);
    static_assert (ui::spectrumPlotBounds (450, 300).height == 179);
    static_assert (ui::spectrumVisualScale (ui::spectrumPlotBounds().width) == 1.0f);
    static_assert (ui::spectrumVisualScale (
                       ui::spectrumPlotBounds (375, 250).width) == 1.25f);
    static_assert (ui::spectrumVisualScale (
                       ui::spectrumPlotBounds (450, 300).width) == 1.5f);
    static_assert (fitsWithin (ui::spectrumPlotBounds (375, 250), 375, 250));
    static_assert (fitsWithin (ui::spectrumPostControlsBounds (375, 250), 375, 250));
    static_assert (fitsWithin (ui::editorLayout (true, 375, 250).feedback, 375, 250));
    static_assert (fitsWithin (ui::spectrumPlotBounds (450, 300), 450, 300));
    static_assert (fitsWithin (ui::spectrumPostControlsBounds (450, 300), 450, 300));
    static_assert (fitsWithin (ui::editorLayout (true, 450, 300).feedback, 450, 300));
    static_assert (! overlaps (ui::spectrumSizeToggleBounds(),
                               ui::editorLayout (true).pairStatus));
    static_assert (! overlaps (ui::spectrumPlotBounds (375, 250),
                               ui::spectrumPostControlsBounds (375, 250)));
    static_assert (! overlaps (ui::spectrumPlotBounds (450, 300),
                               ui::spectrumPostControlsBounds (450, 300)));
    static_assert (ui::spectrumHoverReadoutWidth >= 90);
    static_assert (ui::spectrumHoverReadoutHeight >= 14);
    static_assert (ui::spectrumHoverFrequencyX + ui::spectrumHoverFrequencyWidth
                   <= ui::spectrumHoverDeltaX);
    static_assert (ui::spectrumHoverDeltaX + ui::spectrumHoverDeltaWidth
                   <= ui::spectrumHoverReadoutWidth);
    static_assert (ui::spectrumHoverLineWidth <= 1.0f);
    static_assert (ui::spectrumTipAlpha.size() == 25);
    static_assert (ui::spectrumTipAlpha[0] == 0.0f);
    static_assert (ui::spectrumTipAlpha[3] < ui::spectrumTipAlpha[6]);
    static_assert (ui::spectrumTipAlpha[6] < ui::spectrumTipAlpha[9]);
    static_assert (ui::spectrumTipAlpha[9] < ui::spectrumTipAlpha[12]);
    static_assert (ui::spectrumTipAlpha[24] >= 0.43f);
    static_assert (ui::preDisplayPrimaryColour (ui::PreDisplayTone::context) == ui::normal);
    static_assert (ui::preDisplayPrimaryColour (ui::PreDisplayTone::emphasis) == ui::flora);
    static_assert (ui::preDisplayDetailColour (ui::PreDisplayTone::context)
                   == ui::preDisplayContextDetail);
    static_assert (ui::preDisplayDetailColour (ui::PreDisplayTone::emphasis) == ui::flora);
    assert (contrastRatio (ui::preDisplayPrimaryColour (ui::PreDisplayTone::context),
                           ui::background) >= 4.5);
    assert (contrastRatio (ui::preDisplayDetailColour (ui::PreDisplayTone::context),
                           ui::background) >= 4.5);
    assert (contrastRatio (ui::preDisplayPrimaryColour (ui::PreDisplayTone::emphasis),
                           ui::background) >= 4.5);
    static_assert (ui::ledBlue == 0xff4488cc);
    static_assert (ui::ledGreen == 0xff4cc07a);
    static_assert (ui::watchMetrics.size() == 6);
    static_assert (ui::recordMetrics.size() == 6);
    static_assert (KIRIN_SIGNAL_STATE_INACTIVE == 0u);
    static_assert (KIRIN_SIGNAL_STATE_ACTIVE == 1u);
    static_assert (KIRIN_SIGNAL_STATE_BYPASSED == 2u);
    static_assert (KIRIN_PAIR_STATUS_UNPAIRED == 0u);
    static_assert (KIRIN_PAIR_STATUS_WAITING == 1u);
    static_assert (KIRIN_PAIR_STATUS_PAIRED == 2u);
    static_assert (KIRIN_KEEP_PHASE_IDLE == 0u);
    static_assert (KIRIN_KEEP_PHASE_PREPARING == 1u);
    static_assert (KIRIN_KEEP_PHASE_ARMED == 2u);
    static_assert (KIRIN_DELTA_MODE_ACTIVE == 0u);
    static_assert (KIRIN_DELTA_MODE_STALE == 1u);
    static_assert (KIRIN_DELTA_MODE_NO_PRE == 2u);
    static_assert (KIRIN_DELTA_MODE_BYPASSED == 3u);
    static_assert (KIRIN_DELTA_MODE_PRE_INACTIVE == 4u);
    static_assert (KIRIN_SPECTRUM_BAND_COUNT == 256u);
    static_assert (KIRIN_SPECTRUM_DISPLAY_RANGE_DB == 24.0f);
    static_assert (KIRIN_PERCEPTUAL_BATCH_CAPACITY == 64u);
    static_assert (hypha::ui_contract::spectrumCurvePresentationHz == 12);
    static_assert (hypha::ui_contract::perceptualCurvePresentationHz == 5);
    static_assert (hypha::ui_contract::analysisNumericPresentationHz == 2);
    static_assert (KIRIN_SPECTRUM_HIDDEN == 0u);
    static_assert (KIRIN_SPECTRUM_NO_PAIR == 1u);
    static_assert (KIRIN_SPECTRUM_WARMING_UP == 2u);
    static_assert (KIRIN_SPECTRUM_ACTIVE == 3u);
    static_assert (KIRIN_SPECTRUM_UNAVAILABLE == 4u);
    static_assert (KIRIN_SPECTRUM_IN_USE == 5u);
    static_assert (KIRIN_RECORD_DISPLAY_WATCH == 0u);
    static_assert (KIRIN_RECORD_DISPLAY_LIVE == 1u);
    static_assert (KIRIN_RECORD_DISPLAY_FINALIZING == 2u);
    static_assert (KIRIN_RECORD_DISPLAY_RESULT_HOLD == 3u);
    static_assert (KIRIN_RECORD_DISPLAY_UNAVAILABLE == 4u);
    static_assert (! clockSource::audioUnitV2UsesRenderTimeline (true));
    static_assert (clockSource::audioUnitV2UsesRenderTimeline (false));
    static_assert (display::deltaIsActive (KIRIN_DELTA_MODE_ACTIVE));
    static_assert (display::deltaIsStale (KIRIN_DELTA_MODE_STALE));
    static_assert (! display::preUnavailableForDelta (KIRIN_DELTA_MODE_ACTIVE));
    static_assert (! display::preUnavailableForDelta (KIRIN_DELTA_MODE_STALE));
    static_assert (! display::preUnavailableForDelta (KIRIN_DELTA_MODE_NO_PRE));
    static_assert (display::preUnavailableForDelta (KIRIN_DELTA_MODE_BYPASSED));
    static_assert (display::preUnavailableForDelta (KIRIN_DELTA_MODE_PRE_INACTIVE));
    static_assert (display::recordPairContext (true, true, false, false));
    static_assert (display::recordPairContext (false, true, true, true));
    static_assert (! display::recordPairContext (false, true, true, false));
    static_assert (! display::recordPairContext (false, true, false, true));
    static_assert (display::recordMetricMode (false, false, KIRIN_DELTA_MODE_NO_PRE)
                   == display::MetricMode::absolute);
    static_assert (display::recordMetricMode (true, true, KIRIN_DELTA_MODE_ACTIVE)
                   == display::MetricMode::delta);
    static_assert (display::recordMetricMode (true, true, KIRIN_DELTA_MODE_PRE_INACTIVE)
                   == display::MetricMode::delta);
    static_assert (display::recordMetricMode (false, false, KIRIN_DELTA_MODE_PRE_INACTIVE)
                   == display::MetricMode::absolute);
    static_assert (display::watchMetricMode (true, false, KIRIN_DELTA_MODE_NO_PRE)
                   == display::MetricMode::delta);
    static_assert (display::watchMetricMode (true, true, KIRIN_DELTA_MODE_STALE)
                   == display::MetricMode::delta);
    static_assert (display::watchMetricMode (true, true, KIRIN_DELTA_MODE_PRE_INACTIVE)
                   == display::MetricMode::delta);
    static_assert (display::watchMetricMode (true, false, KIRIN_DELTA_MODE_PRE_INACTIVE)
                   == display::MetricMode::delta);
    static_assert (display::watchMetricMode (true, true, KIRIN_DELTA_MODE_BYPASSED)
                   == display::MetricMode::delta);
    static_assert (display::watchMetricMode (false, true, KIRIN_DELTA_MODE_ACTIVE)
                   == display::MetricMode::absolute);

    assert (std::strcmp (ui::labelFontFamily, ".SF NS") == 0);
    assert (std::strcmp (ui::monoFontFamily, ".SF NS Mono") == 0);
    assert (std::strcmp (ui::windowsLabelFontFamily, "Segoe UI") == 0);
    assert (std::strcmp (ui::windowsMonoFontFamily, "Consolas") == 0);
    assert (std::strcmp (ui::preTitle, "PRE") == 0);
    assert (std::strcmp (ui::postTitle, "POST") == 0);
    assert (std::strcmp (ui::maximumLabel, "MAX") == 0);
    assert (std::strcmp (ui::keepLabel, "Keep") == 0);
    assert (std::strcmp (ui::stopLabel, "Stop") == 0);
    assert (std::strcmp (ui::spectrumSizePresets[0].buttonText, "100%") == 0);
    assert (std::strcmp (ui::spectrumSizePresets[1].buttonText, "125%") == 0);
    assert (std::strcmp (ui::spectrumSizePresets[2].buttonText, "150%") == 0);
    assert (std::abs (ui::analysisTextScale (1.0f) - 1.25f) < 0.0001f);
    assert (std::abs (ui::analysisTextScale (1.25f) - 1.35f) < 0.0001f);
    assert (std::abs (ui::analysisTextScale (1.5f) - 1.62f) < 0.0001f);
    static_assert (ui::absoluteLufsBandTop < ui::absoluteLufsBandBottom
                   && ui::absoluteLufsBandBottom > ui::absolutePeakBandTop
                   && ui::absolutePeakBandBottom > ui::absoluteSharpnessBandTop
                   && ui::absoluteSharpnessBandBottom <= 1.0f);
    // A silent project start is still Inactive. Once Watch has heard audio, short musical rests
    // remain Active and feed zero samples through the meter; one complete LUFS-S window of silence
    // ends the grace. Transport/Record exclusion resets the gate immediately.
    signalState::WatchSilenceGate silenceGate;
    static_assert (signalState::WatchSilenceGate::eligible (false, false, true));
    static_assert (! signalState::WatchSilenceGate::eligible (true, false, true));
    static_assert (! signalState::WatchSilenceGate::eligible (false, true, true));
    static_assert (! signalState::WatchSilenceGate::eligible (false, false, false));
    static_assert (! signalState::WatchSilenceGate::sampleTimelineStartsNewPass (
        false, true, 4'800, 0, 4'800));
    static_assert (! signalState::WatchSilenceGate::sampleTimelineStartsNewPass (
        true, false, 4'800, 0, 4'800));
    static_assert (! signalState::WatchSilenceGate::sampleTimelineStartsNewPass (
        true, true, 4'800, 0, 4'800));
    static_assert (signalState::WatchSilenceGate::sampleTimelineStartsNewPass (
        true, true, 4'801, 0, 4'800));
    static_assert (signalState::availabilityStartsNewPass (
        KIRIN_SIGNAL_STATE_INACTIVE, KIRIN_SIGNAL_STATE_ACTIVE, false));
    static_assert (signalState::availabilityStartsNewPass (
        KIRIN_SIGNAL_STATE_BYPASSED, KIRIN_SIGNAL_STATE_ACTIVE, false));
    static_assert (! signalState::availabilityStartsNewPass (
        KIRIN_SIGNAL_STATE_ACTIVE, KIRIN_SIGNAL_STATE_ACTIVE, false));
    static_assert (! signalState::availabilityStartsNewPass (
        KIRIN_SIGNAL_STATE_INACTIVE, KIRIN_SIGNAL_STATE_ACTIVE, true));
    assert (! silenceGate.observeBlock (true, false, true, 4'800, 48'000.0));
    assert (silenceGate.observeBlock (true, false, false, 4'800, 48'000.0));
    for (int gap = 0; gap < 20; ++gap)
    {
        assert (silenceGate.observeBlock (true, false, true, 4'800, 48'000.0));
        assert (silenceGate.observeBlock (true, false, false, 4'800, 48'000.0));
    }
    assert (silenceGate.observeBlock (true, false, true, 143'999, 48'000.0));
    assert (! silenceGate.observeBlock (true, false, true, 1, 48'000.0));
    assert (! silenceGate.observeBlock (true, false, true, 4'800, 48'000.0));
    assert (silenceGate.observeBlock (true, false, false, 4'800, 48'000.0));
    assert (! silenceGate.observeBlock (true, true, true, 4'800, 48'000.0));
    assert (! silenceGate.observeBlock (true, false, true, 4'800, 48'000.0));
    assert (silenceGate.observeBlock (true, false, false, 4'800, 48'000.0));
    assert (! silenceGate.observeBlock (false, false, false, 4'800, 48'000.0));
    assert (! silenceGate.observeBlock (true, false, true, 4'800, 48'000.0));

    const auto pre = ui::editorLayout (false);
    const auto post = ui::editorLayout (true);
    assert (pre.metricTop == 43);
    assert (post.metricTop == 67);
    assert (pre.title.x == 10 && pre.title.width == ui::preTitleWidth);
    assert (post.title.x == 10 && post.title.width == 206);
    assert (ui::right (post.title) + ui::titlePairGap == post.pairStatus.x);
    assert (post.title.width > pre.title.width);
    assert (pre.name.x == 56 && pre.name.width == 160);
    assert (post.name.x == 10 && post.name.width == 248);
    assert (post.pairDropdown.width == 28 && post.pairDropdown.height == 24);
    assert (post.postControls.y == 149 && post.postControls.height == 28);
    const auto spectrumToggle = ui::spectrumToggleBounds();
    const auto spectrumPlot = ui::spectrumPlotBounds();
    assert (hasArea (spectrumToggle) && fitsEditor (spectrumToggle));
    assert (hasArea (spectrumPlot) && fitsEditor (spectrumPlot));
    assert (! overlaps (spectrumToggle, post.pairStatus));
    assert (! overlaps (spectrumPlot, post.postControls));
    assert (spectrumPlot.x == ui::margin && spectrumPlot.y == post.metricTop);
    assert (spectrumPlot.width == ui::editorWidth - 2 * ui::margin);
    assert (ui::spectrumDeltaLegendLabelX + ui::spectrumDeltaLegendLabelWidth
            < ui::spectrumPreLegendLabelX);
    assert (ui::spectrumPreLegendLabelX + ui::spectrumPreLegendLabelWidth
            < ui::spectrumPostLegendLabelX);
    assert (ui::spectrumPostLegendLabelX + ui::spectrumPostLegendLabelWidth
            <= spectrumPlot.width - ui::spectrumPlotLeftInset - ui::spectrumPlotRightInset);
    assert (std::strcmp (ui::spectrumTooltip (false), "Show POST - PRE analysis") == 0);
    assert (std::strcmp (ui::spectrumTooltip (true), "Return to meters") == 0);
    assert (std::strlen (ui::spectrumTooltip (false))
            <= static_cast<std::size_t> (ui::spectrumTooltipMaximumCharacters));
    assert (std::strlen (ui::spectrumTooltip (true))
            <= static_cast<std::size_t> (ui::spectrumTooltipMaximumCharacters));
    assert (post.feedback.y == 178 && post.feedback.height == 20);
    assert (pre.feedback.y == post.feedback.y);
    assert (pre.preDisplayPrimary.y == 126 && pre.preDisplayPrimary.height == 18);
    assert (pre.preDisplayDetail.y == 144 && pre.preDisplayDetail.height == 18);
    const auto detailWithoutState = ui::preDisplayDetailLayout (pre.preDisplayDetail, 0);
    assert (detailWithoutState.detail.x == pre.preDisplayDetail.x
            && detailWithoutState.detail.width == pre.preDisplayDetail.width
            && ! hasArea (detailWithoutState.state));
    const auto detailWithState = ui::preDisplayDetailLayout (pre.preDisplayDetail, 100);
    assert (detailWithState.detail.width >= ui::preDisplayDetailMinimumWidth);
    assert (detailWithState.state.width == 100);
    assert (! overlaps (detailWithState.detail, detailWithState.state));
    assert (ui::right (detailWithState.state) == ui::right (pre.preDisplayDetail));
    const auto oversizedState = ui::preDisplayDetailLayout (pre.preDisplayDetail, 10'000);
    assert (oversizedState.detail.width == ui::preDisplayDetailMinimumWidth);
    assert (! overlaps (oversizedState.detail, oversizedState.state));
    const auto undersizedLine = ui::preDisplayDetailLayout ({ 0, 0, 60, 18 }, 40);
    assert (undersizedLine.detail.width == 60 && ! hasArea (undersizedLine.state));
    assert (! hasArea (post.preDisplayPrimary) && ! hasArea (post.preDisplayDetail));
    assert (ui::bottom (post.feedback) == ui::editorHeight - 2);
    assert (ui::loudnessSelectorBounds (pre.metricTop).width == ui::loudnessSelectorWidth);
    assert (ui::loudnessSelectorBounds (post.metricTop).width == ui::loudnessSelectorWidth);
    assert (fitsEditor (ui::loudnessSelectorBounds (pre.metricTop)));
    assert (fitsEditor (ui::loudnessSelectorBounds (post.metricTop)));

    // The selector stays 40 px wide. Only the Δ prefix grows with the actual platform font;
    // each M/S hit target retains at least 12 px and all regions remain disjoint.
    const auto absoluteSelector = ui::loudnessSelectorLayout (false, 100);
    assert (absoluteSelector.deltaPrefixWidth == 0);
    assert (absoluteSelector.momentary.x == 1 && absoluteSelector.momentary.width == 19);
    assert (absoluteSelector.shortTerm.x == 20 && absoluteSelector.shortTerm.width == 19);
    const auto minimumDeltaSelector = ui::loudnessSelectorLayout (true, 8);
    assert (minimumDeltaSelector.deltaPrefixWidth == 8);
    assert (minimumDeltaSelector.momentary.width == 15);
    assert (minimumDeltaSelector.shortTerm.width == 15);
    const auto measuredDeltaSelector = ui::loudnessSelectorLayout (true, 10);
    assert (measuredDeltaSelector.deltaPrefixWidth == 10);
    assert (measuredDeltaSelector.momentary.width == 14);
    assert (measuredDeltaSelector.shortTerm.width == 14);
    const auto boundedDeltaSelector = ui::loudnessSelectorLayout (true, 10'000);
    assert (boundedDeltaSelector.deltaPrefixWidth
            == ui::loudnessDeltaMaximumPrefixWidth());
    assert (boundedDeltaSelector.momentary.width == ui::loudnessSegmentMinimumWidth);
    assert (boundedDeltaSelector.shortTerm.width == ui::loudnessSegmentMinimumWidth);
    for (const auto layout : { minimumDeltaSelector, measuredDeltaSelector,
                               boundedDeltaSelector })
    {
        assert (layout.deltaPrefixWidth > 0);
        assert (layout.momentary.x >= layout.deltaPrefixWidth);
        assert (! overlaps ({ 0, 0, layout.deltaPrefixWidth, ui::metricRowHeight },
                            layout.momentary));
        assert (! overlaps (layout.momentary, layout.shortTerm));
        assert (ui::right (layout.shortTerm) <= ui::loudnessSelectorWidth);
    }

    for (const auto rect : { pre.title, pre.led, pre.pairStatus, pre.name,
                             pre.preDisplayPrimary, pre.preDisplayDetail, pre.feedback,
                             post.title, post.led, post.pairStatus, post.name,
                             post.pairDropdown, post.postControls, post.feedback })
    {
        assert (hasArea (rect));
        assert (fitsEditor (rect));
    }

    assert (! overlaps (pre.title, pre.name));
    assert (! overlaps (pre.name, pre.pairStatus));
    assert (! overlaps (pre.pairStatus, pre.led));
    assert (! overlaps (ui::metricCellBounds (5, pre.metricTop), pre.preDisplayPrimary));
    assert (! overlaps (pre.preDisplayPrimary, pre.preDisplayDetail));
    assert (! overlaps (pre.preDisplayDetail, pre.feedback));
    assert (! overlaps (post.title, post.pairStatus));
    assert (! overlaps (post.pairStatus, post.led));
    assert (! overlaps (post.name, post.pairDropdown));
    assert (! overlaps (post.postControls, post.feedback));

    // AU and VST3 share logical bounds; a 2x host scale must be a pure transform with no
    // independently rounded/reflowed geometry. This catches a format-specific pixel layout from
    // being reintroduced outside the common 300x200 contract.
    for (const auto rect : { pre.title, pre.led, pre.pairStatus, pre.name,
                             pre.preDisplayPrimary, pre.preDisplayDetail, pre.feedback,
                             post.title, post.led, post.pairStatus, post.name,
                             post.pairDropdown, post.postControls, post.feedback })
    {
        const auto twice = scaled (rect, 2);
        assert (twice.x % 2 == 0 && twice.y % 2 == 0);
        assert (twice.width == rect.width * 2 && twice.height == rect.height * 2);
        assert (ui::right (twice) <= ui::editorWidth * 2);
        assert (ui::bottom (twice) <= ui::editorHeight * 2);
    }

    for (int index = 0; index < 6; ++index)
    {
        const auto preCell = ui::metricCellBounds (index, pre.metricTop);
        const auto postCell = ui::metricCellBounds (index, post.metricTop);
        assert (preCell.width == postCell.width);
        assert (preCell.height == postCell.height);
        assert (preCell.x == postCell.x);
        assert (ui::right (preCell) <= ui::editorWidth);
        assert (ui::right (postCell) <= ui::editorWidth);
        assert (ui::bottom (preCell) <= ui::editorHeight);
        assert (ui::bottom (postCell) <= ui::editorHeight);
        assert (! overlaps (preCell, pre.feedback));
        assert (! overlaps (postCell, post.postControls));
        assert (! overlaps (postCell, post.feedback));

        for (int other = index + 1; other < 6; ++other)
        {
            assert (! overlaps (preCell, ui::metricCellBounds (other, pre.metricTop)));
            assert (! overlaps (postCell, ui::metricCellBounds (other, post.metricTop)));
        }
    }

    using M = ui::Metric;
    assert (ui::watchMetrics[0].metric == M::lufs && ! ui::watchMetrics[0].maximum);
    assert (ui::watchMetrics[1].metric == M::lufs && ui::watchMetrics[1].maximum);
    assert (ui::watchMetrics[2].metric == M::truePeak && ! ui::watchMetrics[2].maximum);
    assert (ui::watchMetrics[3].metric == M::truePeak && ui::watchMetrics[3].maximum);
    assert (ui::watchMetrics[4].metric == M::crest && ! ui::watchMetrics[4].maximum);
    assert (ui::watchMetrics[5].metric == M::crest && ui::watchMetrics[5].maximum);

    assert (std::strcmp (ui::metricText (M::lufs).absoluteLabel, "") == 0);
    assert (std::strcmp (ui::metricText (M::lufs).absoluteUnit, "LUFS") == 0);
    assert (std::strcmp (ui::metricText (M::truePeak).absoluteUnit, "dBTP") == 0);
    assert (std::strcmp (ui::metricText (M::maxTruePeak).absoluteLabel, "Max TP") == 0);
    assert (std::strcmp (ui::metricText (M::integrated).absoluteLabel, "I") == 0);
    assert (std::strcmp (ui::metricText (M::sharpness).absoluteUnit, "acum") == 0);
    assert (ui::recordMetrics[0].metric == M::lufs);
    assert (ui::recordMetrics[1].metric == M::psr);
    assert (ui::recordMetrics[2].metric == M::maxTruePeak);
    assert (ui::recordMetrics[3].metric == M::integrated);
    assert (ui::recordMetrics[4].metric == M::crest);
    assert (ui::recordMetrics[5].metric == M::sharpness);
    assert (! ui::recordMetrics[2].deltaEligible);
    assert (! ui::recordMetrics[3].deltaEligible);
    return 0;
}
