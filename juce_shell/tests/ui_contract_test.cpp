#include "../src/HyphaUiContract.h"
#include "../src/HyphaDisplayContract.h"
#include "../../crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h"

#include <cassert>
#include <cstring>

namespace ui = hypha::ui_contract;
namespace display = hypha::display_contract;

int main()
{
    static_assert (ui::editorWidth == 300 && ui::editorHeight == 200);
    static_assert (ui::titleFontHeight == 20.0f);
    static_assert (ui::metricLabelFontHeight == 11.0f);
    static_assert (ui::metricValueFontHeight == 14.0f);
    static_assert (ui::metricUnitFontHeight == 10.0f);
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
    static_assert (KIRIN_DELTA_MODE_ACTIVE == 0u);
    static_assert (KIRIN_DELTA_MODE_STALE == 1u);
    static_assert (KIRIN_DELTA_MODE_NO_PRE == 2u);
    static_assert (KIRIN_DELTA_MODE_BYPASSED == 3u);
    static_assert (KIRIN_DELTA_MODE_PRE_INACTIVE == 4u);
    static_assert (display::deltaIsActive (KIRIN_DELTA_MODE_ACTIVE));
    static_assert (display::deltaIsStale (KIRIN_DELTA_MODE_STALE));
    static_assert (! display::preUnavailableForDelta (KIRIN_DELTA_MODE_ACTIVE));
    static_assert (! display::preUnavailableForDelta (KIRIN_DELTA_MODE_STALE));
    static_assert (! display::preUnavailableForDelta (KIRIN_DELTA_MODE_NO_PRE));
    static_assert (display::preUnavailableForDelta (KIRIN_DELTA_MODE_BYPASSED));
    static_assert (display::preUnavailableForDelta (KIRIN_DELTA_MODE_PRE_INACTIVE));
    static_assert (display::recordMetricMode (false, KIRIN_DELTA_MODE_NO_PRE)
                   == display::MetricMode::delta);
    static_assert (display::recordMetricMode (true, KIRIN_DELTA_MODE_ACTIVE)
                   == display::MetricMode::delta);
    static_assert (display::recordMetricMode (true, KIRIN_DELTA_MODE_PRE_INACTIVE)
                   == display::MetricMode::absolute);
    static_assert (display::recordMetricMode (false, KIRIN_DELTA_MODE_PRE_INACTIVE)
                   == display::MetricMode::absolute);
    static_assert (display::watchMetricMode (true, false, KIRIN_DELTA_MODE_NO_PRE)
                   == display::MetricMode::delta);
    static_assert (display::watchMetricMode (true, true, KIRIN_DELTA_MODE_STALE)
                   == display::MetricMode::delta);
    static_assert (display::watchMetricMode (true, true, KIRIN_DELTA_MODE_PRE_INACTIVE)
                   == display::MetricMode::absolute);
    static_assert (display::watchMetricMode (true, false, KIRIN_DELTA_MODE_PRE_INACTIVE)
                   == display::MetricMode::absolute);
    static_assert (display::watchMetricMode (true, true, KIRIN_DELTA_MODE_BYPASSED)
                   == display::MetricMode::absolute);
    static_assert (display::watchMetricMode (false, true, KIRIN_DELTA_MODE_ACTIVE)
                   == display::MetricMode::absolute);

    assert (std::strcmp (ui::labelFontFamily, ".SF NS") == 0);
    assert (std::strcmp (ui::monoFontFamily, ".SF NS Mono") == 0);
    assert (std::strcmp (ui::preTitle, "PRE") == 0);
    assert (std::strcmp (ui::postTitle, "POST") == 0);
    assert (std::strcmp (ui::maximumLabel, "MAX") == 0);
    assert (std::strcmp (ui::keepLabel, "Keep") == 0);
    assert (std::strcmp (ui::stopLabel, "Stop") == 0);

    const auto pre = ui::editorLayout (false);
    const auto post = ui::editorLayout (true);
    assert (pre.metricTop == 45);
    assert (post.metricTop == 69);
    assert (pre.name.x == 74 && pre.name.width == 140);
    assert (post.name.x == 10 && post.name.width == 254);
    assert (post.postControls.y == 143 && post.postControls.height == 26);
    assert (ui::bottom (post.recordError) == ui::editorHeight);

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
    }

    using M = ui::Metric;
    assert (ui::watchMetrics[0].metric == M::lufs && ! ui::watchMetrics[0].maximum);
    assert (ui::watchMetrics[1].metric == M::lufs && ui::watchMetrics[1].maximum);
    assert (ui::watchMetrics[2].metric == M::truePeak && ! ui::watchMetrics[2].maximum);
    assert (ui::watchMetrics[3].metric == M::truePeak && ui::watchMetrics[3].maximum);
    assert (ui::watchMetrics[4].metric == M::crest && ! ui::watchMetrics[4].maximum);
    assert (ui::watchMetrics[5].metric == M::crest && ui::watchMetrics[5].maximum);

    assert (std::strcmp (ui::metricText (M::lufs).absoluteLabel, "LUFS-M") == 0);
    assert (std::strcmp (ui::metricText (M::truePeak).absoluteUnit, "dBTP") == 0);
    assert (std::strcmp (ui::metricText (M::sharpness).absoluteUnit, "acum") == 0);
    assert (ui::recordMetrics[0].metric == M::lufs);
    assert (ui::recordMetrics[1].metric == M::psr);
    assert (ui::recordMetrics[2].metric == M::truePeak);
    assert (ui::recordMetrics[3].metric == M::loudness);
    assert (ui::recordMetrics[4].metric == M::crest);
    assert (ui::recordMetrics[5].metric == M::sharpness);

    return 0;
}
