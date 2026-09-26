#include "../src/HyphaUiContract.h"
#include "../src/AbiContract.h"
#include "../src/HyphaClockSourceContract.h"
#include "../src/HyphaAttackUiContract.h"
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
namespace clockSource = hypha::clock_source_contract;
namespace signalState = hypha::signal_state_contract;
namespace attackUi = hypha::attack_ui;

namespace
{
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
    const auto matchingAbi = kirin::expectedAbiContract();
    assert (kirin::abiMatches (matchingAbi));
    auto oldStaticlib = matchingAbi;
    --oldStaticlib.observatory_frame_version;
    assert (! kirin::abiMatches (oldStaticlib));
    auto unknownRevision = matchingAbi;
    ++unknownRevision.revision;
    assert (! kirin::abiMatches (unknownRevision));
    auto insufficientFrame = matchingAbi;
    --insufficientFrame.observatory_frame_size;
    assert (! kirin::abiMatches (insufficientFrame));

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
    static_assert (ui::nameFontHeight >= 16.0f);
    static_assert (ui::pairStatusFontHeight >= 13.0f);
    static_assert (ui::feedbackFontHeight >= 13.0f);
    static_assert (ui::preDisplayPresentationHz == 10);
    static_assert (ui::spectrumPresentationHz == 30);
    static_assert (ui::menuFontHeight >= 16.0f);
    static_assert (ui::pairMenuItemHeight >= 28);
    static_assert (ui::pairMenuMinimumWidth >= ui::editorWidth);
    static_assert (ui::pairMenuMaximumColumns == 1);
    static_assert (ui::background == 0xff16110d);
    static_assert (ui::normal == 0xffe8e2d8);
    static_assert (ui::observatoryValue == 0xfff0e4cc);
    static_assert (ui::muted == 0xff6b6158);
    static_assert (ui::preDisplayContextDetail == 0xff898989);
    static_assert (ui::flora == 0xffc9a15a);
    static_assert (ui::spectrumDelta == 0xff7fcfd8);
    static_assert (ui::spectrumDeltaBright == 0xffd3eff3);
    static_assert (ui::spectrumPre == 0xff968c80);
    static_assert (ui::spectrumPost == 0xffe0bd7e);
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
    static_assert (ui::spectrumSizePresets.size() == 5);
    static_assert (ui::spectrumSizePresets[0].width == 300
                   && ui::spectrumSizePresets[0].height == 200);
    static_assert (ui::spectrumSizePresets[1].width == 375
                   && ui::spectrumSizePresets[1].height == 250);
    static_assert (ui::spectrumSizePresets[2].width == 450
                   && ui::spectrumSizePresets[2].height == 300);
    static_assert (ui::spectrumSizePresets[3].width == 600
                   && ui::spectrumSizePresets[3].height == 400);
    static_assert (ui::spectrumSizePresets[4].width == 900
                   && ui::spectrumSizePresets[4].height == 600);
    static_assert (ui::spectrumVisualScale (280) == 1.0f);
    static_assert (ui::spectrumVisualScale (355) == 1.25f);
    static_assert (ui::spectrumVisualScale (430) == 1.5f);
    static_assert (ui::spectrumVisualScale (580) == 2.0f);
    static_assert (ui::spectrumVisualScale (880) == 3.0f);
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
    static_assert (ui::ledBlue == 0xff7fcfd8); // PAIR shares the difference cyan
    static_assert (ui::ledGreen == 0xff4cc07a);
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
    static_assert (KIRIN_ATTACK_EVENT_BATCH_CAPACITY >= 200u);
    static_assert (attackUi::presentationSeconds == 6);
    static_assert (attackUi::presentationHz == 10);
    static_assert (attackUi::activationEnvironmentVariable[0] == 'K');
    static_assert (attackUi::activationValue[0] == '1');
    static_assert (attackUi::windowSamples (48'000) == 288'000);
    static_assert (attackUi::eventIsVisible (0, 288'000, 48'000));
    static_assert (attackUi::eventIsVisible (-288'000, 0, 48'000));
    static_assert (! attackUi::eventIsVisible (-288'001, 0, 48'000));
    static_assert (! attackUi::eventIsVisible (288'001, 288'000, 48'000));
    static_assert (attackUi::eventX (0, 288'000, 48'000, 281) == 0);
    static_assert (attackUi::eventX (144'000, 288'000, 48'000, 281) == 140);
    static_assert (attackUi::eventX (288'000, 288'000, 48'000, 281) == 280);
    static_assert (attackUi::eventX (0, 0, 0, 281) == -1);
    static_assert (! attackUi::validTimeline (
        std::numeric_limits<std::int64_t>::min(), 48'000));
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
    assert (std::strcmp (ui::kimeraFontFamily, "KMR Waldenburg Book") == 0);
    assert (std::strcmp (ui::fallbackLabelFontFamily, ".SF NS") == 0);
    assert (std::strcmp (ui::fallbackMonoFontFamily, ".SF NS Mono") == 0);
    assert (std::strcmp (ui::windowsFallbackLabelFontFamily, "Segoe UI") == 0);
    assert (std::strcmp (ui::windowsFallbackMonoFontFamily, "Consolas") == 0);
    assert (std::strcmp (ui::preTitle, "PRE") == 0);
    assert (std::strcmp (ui::postTitle, "POST") == 0);
    assert (std::strcmp (ui::spectrumSizePresets[0].buttonText, "100%") == 0);
    assert (std::strcmp (ui::spectrumSizePresets[1].buttonText, "125%") == 0);
    assert (std::strcmp (ui::spectrumSizePresets[2].buttonText, "150%") == 0);
    assert (std::strcmp (ui::spectrumSizePresets[3].buttonText, "200%") == 0);
    assert (std::strcmp (ui::spectrumSizePresets[4].buttonText, "300%") == 0);
    assert (std::abs (ui::analysisTextScale (1.0f) - 1.5f) < 0.0001f);
    assert (std::abs (ui::analysisTextScale (1.25f) - 1.5625f) < 0.0001f);
    assert (std::abs (ui::analysisTextScale (1.5f) - 1.625f) < 0.0001f);
    assert (std::abs (ui::analysisTextScale (2.0f) - 1.75f) < 0.0001f);
    assert (std::abs (ui::analysisTextScale (3.0f) - 2.0f) < 0.0001f);
    static_assert (ui::absoluteLufsBandTop < ui::absoluteLufsBandBottom
                   && ui::absoluteLufsBandBottom > ui::absolutePeakBandTop
                   && ui::absolutePeakBandBottom > ui::absoluteSharpnessBandTop
                   && ui::absoluteSharpnessBandBottom <= 1.0f);
    static_assert (ui::absoluteLufsBandBottom - ui::absoluteLufsBandTop >= 0.45f
                   && ui::absolutePeakBandBottom - ui::absolutePeakBandTop >= 0.45f
                   && ui::absoluteSharpnessBandBottom
                        - ui::absoluteSharpnessBandTop >= 0.45f);
    static_assert (ui::spectrumFocusTrailRangeDb == 12.0f
                   && ui::spectrumFocusTrailCompactHeight >= 24.0f
                   && ui::spectrumFocusTrailMediumHeight >= 38.0f
                   && ui::spectrumFocusTrailLargeHeight >= 54.0f);
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

    assert (ui::spectrumDeltaLegendLabelX + ui::spectrumDeltaLegendLabelWidth
            < ui::spectrumPreLegendLabelX);
    assert (ui::spectrumPreLegendLabelX + ui::spectrumPreLegendLabelWidth
            < ui::spectrumPostLegendLabelX);
    assert (ui::spectrumPostLegendLabelX + ui::spectrumPostLegendLabelWidth
            <= ui::editorWidth - 2 * ui::margin
                 - ui::spectrumPlotLeftInset - ui::spectrumPlotRightInset);
    assert (std::strcmp (ui::spectrumTooltip (false), "Show POST - PRE analysis") == 0);
    assert (std::strcmp (ui::spectrumTooltip (true), "Return to meters") == 0);
    assert (std::strlen (ui::spectrumTooltip (false))
            <= static_cast<std::size_t> (ui::spectrumTooltipMaximumCharacters));
    assert (std::strlen (ui::spectrumTooltip (true))
            <= static_cast<std::size_t> (ui::spectrumTooltipMaximumCharacters));
    const ui::Rect detailLine { 10, 144, 280, 18 };
    const auto detailWithState = ui::preDisplayDetailLayout (detailLine, 100);
    assert (detailWithState.detail.width >= ui::preDisplayDetailMinimumWidth);
    assert (detailWithState.state.width == 100);
    assert (ui::right (detailWithState.state) == ui::right (detailLine));
    const auto oversizedState = ui::preDisplayDetailLayout (detailLine, 10'000);
    assert (oversizedState.detail.width == ui::preDisplayDetailMinimumWidth);
    const auto undersizedLine = ui::preDisplayDetailLayout ({ 0, 0, 60, 18 }, 40);
    assert (undersizedLine.detail.width == 60 && undersizedLine.state.width == 0);
    return 0;
}
