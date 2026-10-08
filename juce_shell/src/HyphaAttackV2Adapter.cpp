#include "HyphaAttackV2Model.h"
#include "HyphaAttackLaneModel.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace hypha::attack_v2
{
bool sameSource (const KirinSnapshotSourceKey& a, const KirinSnapshotSourceKey& b) noexcept
{
    return a.generation == b.generation && a.sample_rate == b.sample_rate && a.channels == b.channels
        && std::equal (std::begin (a.incarnation), std::end (a.incarnation), std::begin (b.incarnation))
        && std::equal (std::begin (a.odf_hash), std::end (a.odf_hash), std::begin (b.odf_hash));
}
bool sameEvent (const KirinSnapshotEventKey& a, const KirinSnapshotEventKey& b) noexcept
{
    return a.token == b.token && a.event_sample == b.event_sample && sameSource (a.source, b.source);
}
bool sameAuthority (const KirinSnapshotHeader& a, const KirinSnapshotHeader& b) noexcept
{
    return sameSource (a.source, b.source) && a.authority_revision == b.authority_revision
        && a.band == b.band && a.target == b.target
        && std::equal (std::begin (a.band_semantic_hash), std::end (a.band_semantic_hash),
                        std::begin (b.band_semantic_hash));
}
bool validHeader (const KirinSnapshotHeader& h) noexcept
{
    return h.version == 2 && h.kind <= 4 && h.target <= KIRIN_TARGET_PRE
        && h.band <= 8 && h.signal_state <= 2 && h.source.generation != 0
        && h.source.sample_rate > 0 && (h.source.channels == 1 || h.source.channels == 2)
        && std::any_of (std::begin (h.source.incarnation), std::end (h.source.incarnation), [](auto b) { return b != 0; })
        && std::any_of (std::begin (h.source.odf_hash), std::end (h.source.odf_hash), [](auto b) { return b != 0; });
}
namespace
{
bool validEvidence (const KirinSnapshotScalarEvidence& e)
{
    if (e.class_code > KIRIN_SCALAR_NOT_APPLICABLE || e.reason >= KIRIN_REASON_COUNT
        || e.finish > KIRIN_FINISH_RETIRED || e.has_interval > 1 || ! std::isfinite (e.resolution)
        || e.resolution < 0) return false;
    const bool numeric = e.class_code <= KIRIN_SCALAR_BOUND;
    if (numeric != (e.has_interval != 0)) return false;
    if (! numeric) return true;
    if (! validInterval (e.interval)) return false;
    return e.class_code != KIRIN_SCALAR_EXACT
        || (e.interval.lower.kind == KIRIN_ENDPOINT_FINITE && e.interval.upper.kind == KIRIN_ENDPOINT_FINITE
            && e.interval.lower.closed && e.interval.upper.closed && (e.interval.lower.value <= e.interval.upper.value && e.interval.lower.value >= e.interval.upper.value));
}
bool validAverage (const KirinAttackBandAveragePointV2& p, std::uint8_t allowed,
                   const KirinAttackBandAveragePointV2* previous)
{
    unsigned bits = p.participating_bits, count = 0;
    for (; bits != 0; bits >>= 1) count += bits & 1;
    if ((p.participating_bits & ~allowed) != 0 || count != p.valid_count || p.has_pre > 1
        || p.connect_previous > 1) return false;
    const bool connects = previous != nullptr && p.participating_bits != 0
        && previous->participating_bits == p.participating_bits;
    if ((p.connect_previous != 0) != connects) return false;
    const auto range = [] (double mean, double low, double high) {
        return std::isfinite (mean) && std::isfinite (low) && std::isfinite (high)
            && low <= mean && mean <= high; };
    return p.valid_count == 0 || (range (p.post_mean, p.post_min, p.post_max)
        && (p.has_pre == 0 || range (p.pre_mean, p.pre_min, p.pre_max)));
}
KirinSnapshotInterval point (double value, std::uint8_t unit)
{
    KirinSnapshotInterval result {};
    result.lower = { KIRIN_ENDPOINT_FINITE, 1, {}, value };
    result.upper = result.lower;
    result.unit = unit;
    return result;
}
int decimalsFor (bool band, std::size_t index) { return band ? (index == 2 ? 0 : 1) : (index == 3 ? 2 : 1); }
juce::String unitFor (bool band, std::size_t index, std::uint8_t target)
{
    return band ? (index < 3 ? "ms" : target == KIRIN_TARGET_DELTA ? "dB" : "dBFS")
                : index == 3 ? "acum" : index == 1 && target != KIRIN_TARGET_DELTA ? "dBFS" : "dB";
}
}
bool validSummary (const KirinAttackBandSummaryV2& s) noexcept
{
    if (! validHeader (s.header) || s.header.kind != KIRIN_SNAPSHOT_BAND_SUMMARY || s.header.band == 0
        || s.header.struct_size != sizeof (s) || s.cohort_count > 8) return false;
    const auto count = s.cohort_count;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (s.events[i].event_sample > s.header.cutoff_sample || s.pair_kind[i] > 4
            || (i > 0 && s.events[i].event_sample < s.events[i - 1].event_sample)) return false;
        for (const auto& e : s.evidence[i]) if (! validEvidence (e)) return false;
    }
    for (std::size_t at = 0; at < 4; ++at)
    {
        const auto& l = s.lanes[at];
        if (l.cohort_count != count || std::accumulate (std::begin (l.class_count), std::end (l.class_count), 0) != count
            || l.exact_count != l.class_count[KIRIN_SCALAR_EXACT] || l.render_kind > KIRIN_RENDER_NO_SCALAR
            || l.whole_median_available > 1 || l.whole_numeric_informative > 1 || l.whole_within_resolution > 1
            || ! std::isfinite (l.resolution) || l.resolution < 0
            || (l.exact_count > 0 && (! std::isfinite (l.exact_median)
                || l.exact_latest_event_sample > s.header.cutoff_sample))) return false;
        if (l.whole_median_available && ! validInterval (l.whole_interval)) return false;
        if (l.render_kind == KIRIN_RENDER_WHOLE_POINT && (l.whole_interval.lower.kind != KIRIN_ENDPOINT_FINITE
            || l.whole_interval.upper.kind != KIRIN_ENDPOINT_FINITE || ! l.whole_interval.lower.closed
            || ! l.whole_interval.upper.closed || (l.whole_interval.lower.value < l.whole_interval.upper.value || l.whole_interval.lower.value > l.whole_interval.upper.value))) return false;
        if (l.render_kind <= KIRIN_RENDER_WHOLE_INTERVAL
            && (! l.whole_median_available || ! l.whole_numeric_informative)) return false;
        if (l.render_kind == KIRIN_RENDER_CONFIRMED_SUBSET && l.exact_count == 0) return false;
        std::array<std::uint8_t, 5> actual {};
        for (std::size_t i = 0; i < count; ++i) ++actual[s.evidence[i][at].class_code];
        if (! std::equal (actual.begin(), actual.end(), std::begin (l.class_count))) return false;
    }
    const auto allowed = static_cast<std::uint8_t> ((1u << count) - 1);
    for (std::size_t i = 0; i < 96; ++i) if (! validAverage (s.head[i], allowed, i == 0 ? nullptr : &s.head[i - 1])) return false;
    for (std::size_t i = 0; i < 64; ++i) if (! validAverage (s.tail[i], allowed, i == 0 ? nullptr : &s.tail[i - 1])) return false;
    return true;
}
bool validSingle (const KirinAttackSingleSnapshotV2& s) noexcept
{
    if (! validHeader (s.header) || s.header.kind != KIRIN_SNAPSHOT_SINGLE || s.header.struct_size != sizeof (s)
        || s.request_token == 0 || s.finish > KIRIN_FINISH_RETIRED || s.reason >= KIRIN_REASON_COUNT
        || s.pair_kind > 4 || s.has_all_pre > 1 || s.has_all_post > 1) return false;
    for (const auto& lane : s.lanes) if (! validEvidence (lane)) return false;
    for (std::size_t i = 0; i < 160; ++i)
        if (s.pre_valid[i] > 1 || s.post_valid[i] > 1 || (s.pre_valid[i] && ! std::isfinite (s.pre[i]))
            || (s.post_valid[i] && ! std::isfinite (s.post[i]))) return false;
    const auto detail = [&] (const KirinAttackDetail& d) {
        return d.sample_rate == s.header.source.sample_rate && d.channels == s.header.source.channels
            && d.shape_count <= KIRIN_ATTACK_SHAPE_CAPACITY && d.shape_end_sample >= d.shape_start_sample; };
    return (! s.has_all_pre || detail (s.all_pre)) && (! s.has_all_post || detail (s.all_post));
}
Presentation summaryPresentation (std::shared_ptr<const KirinAttackBandSummaryV2> s)
{
    Presentation p;
    p.header = s->header;
    p.summary = std::move (s);
    for (std::size_t i = 0; i < 4; ++i)
    {
        const auto& raw = p.summary->lanes[i];
        auto& l = p.lanes[i];
        l.scope = static_cast<Scope> (raw.render_kind);
        l.count = raw.cohort_count; l.exactCount = raw.exact_count;
        std::copy (std::begin (raw.class_count), std::end (raw.class_count), l.counts.begin());
        std::copy (std::begin (raw.reason_count), std::end (raw.reason_count), l.reasons.begin());
        l.exactMedian = raw.exact_median; l.resolution = raw.resolution;
        l.unbounded = raw.whole_median_available && ! raw.whole_numeric_informative;
        l.withinResolution = raw.whole_within_resolution != 0;
        if (l.scope == Scope::confirmedSubset)
        {
            l.ageSeconds = static_cast<double> (p.header.cutoff_sample - raw.exact_latest_event_sample) / p.header.source.sample_rate;
            p.maximumSubsetAge = std::max (p.maximumSubsetAge, l.ageSeconds);
        }
        if (raw.render_kind != KIRIN_RENDER_NO_SCALAR)
            l.number = formatInterval (l.scope == Scope::confirmedSubset
                ? point (raw.exact_median, i == 3 ? KIRIN_INTERVAL_DECIBELS : KIRIN_INTERVAL_MILLISECONDS)
                : raw.whole_interval, decimalsFor (true, i), unitFor (true, i, p.header.target),
                p.header.target == KIRIN_TARGET_DELTA,
                l.scope == Scope::wholePoint || l.scope == Scope::confirmedSubset);
        else l.number.unit = unitFor (true, i, p.header.target);
    }
    return p;
}
Presentation singlePresentation (std::shared_ptr<const KirinAttackSingleSnapshotV2> s)
{
    Presentation p;
    p.header = s->header; p.single = std::move (s);
    attack_lanes::Hit hit;
    hit.post = attack_lanes::sideFor (p.single->has_all_post ? &p.single->all_post : nullptr);
    hit.pre = attack_lanes::sideFor (p.single->has_all_pre ? &p.single->all_pre : nullptr);
    if (p.header.target == KIRIN_TARGET_DELTA) attack_lanes::fillDelta (hit, p.single->pair_kind, p.single->has_all_pre, p.single->has_all_post);
    else attack_lanes::fillAbsolute (hit);
    for (std::size_t i = 0; i < 4; ++i)
    {
        auto& l = p.lanes[i];
        l.scope = Scope::single; l.count = 1; l.reason = p.single->reason;
        l.number.unit = unitFor (p.header.band != 0, i, p.header.target);
        if (p.header.band != 0)
        {
            const auto& e = p.single->lanes[i];
            l.counts[e.class_code] = 1; l.reason = e.reason; l.resolution = e.resolution;
            if (e.has_interval) l.number = formatInterval (e.interval, decimalsFor (true, i), l.number.unit,
                p.header.target == KIRIN_TARGET_DELTA, e.class_code == KIRIN_SCALAR_EXACT);
        }
        else if (p.single->finish != KIRIN_FINISH_RETIRED && std::isfinite (hit.cells[i].value))
        {
            l.counts[KIRIN_SCALAR_EXACT] = 1;
            l.number = formatInterval (point (hit.cells[i].value, KIRIN_INTERVAL_DECIBELS), decimalsFor (false, i),
                                      l.number.unit, p.header.target == KIRIN_TARGET_DELTA, true);
        }
        else
        {
            const bool pending = p.single->finish == KIRIN_FINISH_ACQUIRING;
            l.counts[pending ? KIRIN_SCALAR_PENDING : KIRIN_SCALAR_UNKNOWN] = 1;
            if (l.reason == 0) l.reason = pending ? KIRIN_REASON_WAITING_SERVICE
                : hit.cells[i].reason == attack_lanes::Reason::nextHit ? KIRIN_REASON_NEXT_HIT
                : hit.cells[i].reason == attack_lanes::Reason::quietBody ? KIRIN_REASON_SILENT : KIRIN_REASON_MAPPING;
        }
    }
    return p;
}
juce::String scopeText (const Lane& lane, bool live)
{
    if (lane.scope == Scope::single) return words (live ? "Latest hit" : "Locked hit", live ? u8"最新の一打" : u8"固定した一打");
    if (lane.scope == Scope::confirmedSubset) return words ("Exact ", u8"確定") + juce::String (lane.exactCount) + "/" + juce::String (lane.count);
    return words ("Whole ", u8"全") + juce::String (lane.count) + words ("", u8"打");
}
juce::String laneReason (const Lane& lane)
{
    if (lane.count == 0 && lane.reason != 0) return reasonText (lane.reason);
    if (lane.scope == Scope::single) return lane.number.valid ? juce::String {} : reasonText (lane.reason);
    if (lane.unbounded) return words ("Unbounded", u8"全体不定");
    if (lane.scope == Scope::wholePoint || lane.scope == Scope::wholeInterval)
        return lane.withinResolution ? words ("Within RES", u8"判別内") : juce::String {};
    for (const auto code : { KIRIN_SCALAR_NOT_APPLICABLE, KIRIN_SCALAR_UNKNOWN, KIRIN_SCALAR_PENDING })
        if (lane.counts[code] > 0) return words (code == KIRIN_SCALAR_NOT_APPLICABLE ? "N/A "
            : code == KIRIN_SCALAR_UNKNOWN ? "Unknown " : "Pending ",
            code == KIRIN_SCALAR_NOT_APPLICABLE ? u8"不成立" : code == KIRIN_SCALAR_UNKNOWN ? u8"不明" : u8"取得中") + juce::String (lane.counts[code]);
    return words ("No observations", u8"観測なし");
}
}
