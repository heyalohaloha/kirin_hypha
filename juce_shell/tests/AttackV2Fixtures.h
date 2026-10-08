#pragma once
#include "../src/HyphaAttackComponent.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace hypha::attack_v2_test
{
inline KirinSnapshotHeader header (std::uint8_t kind = 4, std::uint8_t band = 0)
{
    KirinSnapshotHeader h {};
    h.version = 2; h.kind = kind; h.band = band; h.target = KIRIN_TARGET_DELTA; h.struct_size = sizeof (KirinAttackNavigationV2);
    h.signal_state = 1; h.snapshot_revision = 900; h.authority_revision = 31; h.cutoff_sample = 480000;
    h.source.generation = 7; h.source.sample_rate = 48000; h.source.channels = 2;
    h.source.incarnation[0] = 9; h.source.odf_hash[0] = 11; h.band_semantic_hash[0] = band == 0 ? 0 : 13;
    return h;
}
inline KirinSnapshotEventKey key (std::int64_t sample, std::uint64_t token)
{ return { header().source, sample, token }; }
inline KirinSnapshotInterval interval (double low, double high, bool lowClosed = true, bool highClosed = true)
{
    KirinSnapshotInterval i {};
    i.lower = { KIRIN_ENDPOINT_FINITE, static_cast<std::uint8_t> (lowClosed), {}, low };
    i.upper = { KIRIN_ENDPOINT_FINITE, static_cast<std::uint8_t> (highClosed), {}, high };
    return i;
}
inline KirinSnapshotInterval lowerBound (double value, bool closed = true)
{
    auto i = interval (value, value, closed);
    i.upper = { KIRIN_ENDPOINT_POSITIVE_INFINITY, 0, {}, 0 };
    return i;
}
inline KirinAttackBandSummaryV2 summary()
{
    KirinAttackBandSummaryV2 s {};
    s.header = header (KIRIN_SNAPSHOT_BAND_SUMMARY, 5); s.header.struct_size = sizeof (s); s.cohort_count = 8;
    for (std::size_t i = 0; i < 8; ++i)
    {
        s.events[i] = key (i == 0 ? 336000 : 480000 - static_cast<std::int64_t> (7 - i) * 4800, i + 1);
        for (std::size_t lane = 0; lane < 4; ++lane)
        {
            auto& e = s.evidence[i][lane];
            e.class_code = KIRIN_SCALAR_EXACT; e.has_interval = 1; e.finish = KIRIN_FINISH_FULL;
            e.interval = interval (static_cast<double> (i), static_cast<double> (i));
            e.interval.unit = lane == 3 ? KIRIN_INTERVAL_DECIBELS : KIRIN_INTERVAL_MILLISECONDS;
        }
    }
    for (std::size_t lane = 0; lane < 4; ++lane)
    {
        auto& l = s.lanes[lane]; l.cohort_count = 8; l.class_count[0] = l.exact_count = 8;
        l.whole_median_available = l.whole_numeric_informative = 1;
        l.whole_interval = interval (3.5, 3.5); l.whole_interval.unit = lane == 3 ? 1 : 0;
        l.exact_median = 3.5; l.exact_latest_event_sample = 480000;
        l.resolution = lane == 3 ? .2 : lane == 0 ? .2 : 2;
    }
    // D4: whole-N informative bound wins over the exact subset's +3 ms.
    auto& rel = s.lanes[2]; rel.class_count[0] = rel.exact_count = 3; rel.class_count[1] = 5;
    rel.whole_interval = lowerBound (200); rel.render_kind = KIRIN_RENDER_WHOLE_INTERVAL; rel.exact_median = 3;
    for (std::size_t i = 3; i < 8; ++i) { s.evidence[i][2].class_code = KIRIN_SCALAR_BOUND; s.evidence[i][2].interval = lowerBound (200); }
    // S1: whole real line cannot become a scalar; exact n/N keeps its independent age.
    auto& delay = s.lanes[0]; delay.class_count[0] = delay.exact_count = 1; delay.class_count[2] = 7;
    delay.render_kind = KIRIN_RENDER_CONFIRMED_SUBSET; delay.whole_numeric_informative = 0;
    delay.whole_interval.lower = { KIRIN_ENDPOINT_NEGATIVE_INFINITY, 0, {}, 0 };
    delay.whole_interval.upper = { KIRIN_ENDPOINT_POSITIVE_INFINITY, 0, {}, 0 };
    delay.exact_latest_event_sample = 336000; delay.exact_median = .8;
    s.evidence[0][0].interval = interval (.8, .8);
    for (std::size_t i = 1; i < 8; ++i) { s.evidence[i][0].class_code = KIRIN_SCALAR_UNKNOWN; s.evidence[i][0].has_interval = 0; s.evidence[i][0].reason = KIRIN_REASON_CLOCK; }
    delay.reason_count[KIRIN_REASON_CLOCK] = 7;
    // Two different non-empty sets: no connection at point 48, same set resumes at49.
    for (std::size_t i = 0; i < 96; ++i)
    {
        auto& p = s.head[i]; p.participating_bits = i < 48 ? 1 : 2; p.valid_count = 1;
        p.connect_previous = i != 0 && i != 48; p.has_pre = 1;
        p.pre_mean = p.pre_min = p.pre_max = -70; p.post_mean = p.post_min = p.post_max = i < 48 ? -20 : -100;
    }
    return s;
}
inline KirinAttackSingleSnapshotV2 single (const KirinAttackSingleV2Request& request, std::uint64_t token)
{
    KirinAttackSingleSnapshotV2 s {};
    s.header = header (KIRIN_SNAPSHOT_SINGLE, request.band); s.header.struct_size = sizeof (s);
    s.event = request.event; s.request_token = token; s.measurement_revision = 1;
    s.finish = KIRIN_FINISH_ACQUIRING;
    for (auto& e : s.lanes) { e.class_code = request.band == 0 ? KIRIN_SCALAR_NOT_APPLICABLE : KIRIN_SCALAR_PENDING; e.reason = KIRIN_REASON_WAITING_SERVICE; }
    return s;
}
inline KirinAttackBandSummaryV2 denseSummary()
{
    auto s = summary();
    for (auto& lane : s.lanes)
    {
        lane.class_count[0] = lane.exact_count = 8;
        std::fill (std::begin (lane.class_count) + 1, std::end (lane.class_count), 0);
        std::fill (std::begin (lane.reason_count), std::end (lane.reason_count), 0);
        lane.render_kind = KIRIN_RENDER_WHOLE_POINT; lane.whole_median_available = lane.whole_numeric_informative = 1;
        lane.whole_interval = interval (3.5, 3.5); lane.exact_median = 3.5; lane.exact_latest_event_sample = 480000;
    }
    s.lanes[3].whole_interval.unit = KIRIN_INTERVAL_DECIBELS;
    for (std::size_t i = 0; i < 8; ++i) for (std::size_t at = 0; at < 4; ++at)
    {
        auto& e = s.evidence[i][at]; e.class_code = KIRIN_SCALAR_EXACT; e.reason = 0; e.has_interval = 1;
        e.interval = interval (static_cast<double> (i), static_cast<double> (i)); e.interval.unit = at == 3 ? KIRIN_INTERVAL_DECIBELS : KIRIN_INTERVAL_MILLISECONDS;
    }
    const auto populate = [] (auto& points) {
        for (std::size_t i = 0; i < std::size (points); ++i)
        {
            auto& p = points[i]; p.participating_bits = 255; p.valid_count = 8; p.has_pre = 1; p.connect_previous = i != 0;
            p.pre_mean = -48 + 10 * std::sin (static_cast<double> (i) * .2);
            p.post_mean = p.pre_mean + 3.5;
            p.pre_min = p.pre_mean - 18; p.pre_max = p.pre_mean + 18;
            p.post_min = p.post_mean - 18; p.post_max = p.post_mean + 18;
        }
    };
    populate (s.head); populate (s.tail); return s;
}
inline juce::MouseEvent mouse (juce::Component& c, juce::Point<float> p)
{
    const auto t = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), p, {}, 0, 0, 0, 0, 0, &c, &c, t, p, t, 0, false };
}
}
