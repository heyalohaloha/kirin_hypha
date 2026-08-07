#include "../src/HyphaUiContract.h"
#include "../src/HyphaDisplayContract.h"
#include "../src/HyphaClockSourceContract.h"
#include "../src/HyphaSignalStateContract.h"
#include "../../crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h"

#include <cassert>
#include <cstring>

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

    constexpr bool overlaps (ui::Rect a, ui::Rect b)
    {
        return a.x < ui::right (b) && b.x < ui::right (a)
            && a.y < ui::bottom (b) && b.y < ui::bottom (a);
    }

    constexpr ui::Rect scaled (ui::Rect r, int scale)
    {
        return { r.x * scale, r.y * scale, r.width * scale, r.height * scale };
    }
}

int main()
{
    static_assert (ui::editorWidth == 300 && ui::editorHeight == 200);
    static_assert (ui::titleFontHeight == 20.0f);
    static_assert (ui::metricLabelFontHeight >= 12.0f);
    static_assert (ui::metricValueFontHeight >= 17.0f);
    static_assert (ui::metricUnitFontHeight >= 12.0f);
    static_assert (ui::nameFontHeight >= 16.0f);
    static_assert (ui::pairStatusFontHeight >= 13.0f);
    static_assert (ui::feedbackFontHeight >= 13.0f);
    static_assert (ui::menuFontHeight >= 16.0f);
    static_assert (ui::pairMenuItemHeight >= 28);
    static_assert (ui::pairMenuMinimumWidth >= ui::editorWidth);
    static_assert (ui::pairMenuMaximumColumns == 1);
    static_assert (ui::background == 0xff0d0f1a);
    static_assert (ui::normal == 0xffe0e0e0);
    static_assert (ui::muted == 0xff606060);
    static_assert (ui::flora == 0xffd4a043);
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
    assert (std::strcmp (ui::preTitle, "PRE") == 0);
    assert (std::strcmp (ui::postTitle, "POST") == 0);
    assert (std::strcmp (ui::maximumLabel, "MAX") == 0);
    assert (std::strcmp (ui::keepLabel, "Keep") == 0);
    assert (std::strcmp (ui::stopLabel, "Stop") == 0);

    // A silent project start is still Inactive. Once Watch has heard audio, short musical rests
    // remain Active and feed zero samples through the meter; one complete LUFS-S window of silence
    // ends the grace. Transport/Record exclusion resets the gate immediately.
    signalState::WatchSilenceGate silenceGate;
    static_assert (signalState::WatchSilenceGate::eligible (false, false, true));
    static_assert (! signalState::WatchSilenceGate::eligible (true, false, true));
    static_assert (! signalState::WatchSilenceGate::eligible (false, true, true));
    static_assert (! signalState::WatchSilenceGate::eligible (false, false, false));
    assert (! signalState::WatchSilenceGate::callbackGapStartsNewPass (
        0.250, 4'800, 48'000.0));
    assert (signalState::WatchSilenceGate::callbackGapStartsNewPass (
        0.251, 4'800, 48'000.0));
    assert (! signalState::WatchSilenceGate::callbackGapStartsNewPass (
        2.0, 48'000, 48'000.0));
    assert (signalState::WatchSilenceGate::callbackGapStartsNewPass (
        2.001, 48'000, 48'000.0));
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
    assert (pre.name.x == 56 && pre.name.width == 160);
    assert (post.name.x == 10 && post.name.width == 248);
    assert (post.pairDropdown.width == 28 && post.pairDropdown.height == 24);
    assert (post.postControls.y == 149 && post.postControls.height == 28);
    assert (post.feedback.y == 178 && post.feedback.height == 20);
    assert (pre.feedback.y == post.feedback.y);
    assert (ui::bottom (post.feedback) == ui::editorHeight - 2);
    assert (ui::loudnessSelectorBounds (pre.metricTop).width == ui::loudnessSelectorWidth);
    assert (ui::loudnessSelectorBounds (post.metricTop).width == ui::loudnessSelectorWidth);
    assert (fitsEditor (ui::loudnessSelectorBounds (pre.metricTop)));
    assert (fitsEditor (ui::loudnessSelectorBounds (post.metricTop)));

    for (const auto rect : { pre.title, pre.led, pre.pairStatus, pre.name, pre.feedback,
                             post.title, post.led, post.pairStatus, post.name,
                             post.pairDropdown, post.postControls, post.feedback })
    {
        assert (hasArea (rect));
        assert (fitsEditor (rect));
    }

    assert (! overlaps (pre.title, pre.name));
    assert (! overlaps (pre.name, pre.pairStatus));
    assert (! overlaps (pre.pairStatus, pre.led));
    assert (! overlaps (post.title, post.pairStatus));
    assert (! overlaps (post.pairStatus, post.led));
    assert (! overlaps (post.name, post.pairDropdown));
    assert (! overlaps (post.postControls, post.feedback));

    // AU and VST3 share logical bounds; a 2x host scale must be a pure transform with no
    // independently rounded/reflowed geometry. This catches a format-specific pixel layout from
    // being reintroduced outside the common 300x200 contract.
    for (const auto rect : { pre.title, pre.led, pre.pairStatus, pre.name, pre.feedback,
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
