#pragma once
#include "AttackV2Fixtures.h"
#include "AttackUiLaneContract.h"
#include "../src/HyphaAttackV2Painter.h"
#include "../src/HyphaTextStyle.h"
#include <algorithm>
#include <iterator>
#include <iostream>

namespace hypha::attack_ui_test::v2_cadence_contract
{
using namespace attack_v2_test;
using namespace attack_v2;
#define CADENCE_Q(e) do { if (!(e)) { std::cerr << "DRUM cadence line " << __LINE__ << ": " << #e << '\n'; return false; } } while (false)
inline void noValues (KirinAttackBandSummaryV2& s)
{
    for (auto& row : s.evidence) for (auto& e : row)
    { e = {}; e.class_code = KIRIN_SCALAR_UNKNOWN; e.reason = KIRIN_REASON_NOT_KEPT; e.finish = KIRIN_FINISH_FULL; }
    for (auto& lane : s.lanes)
    {
        lane = {}; lane.cohort_count = s.cohort_count;
        lane.class_count[KIRIN_SCALAR_UNKNOWN] = s.cohort_count;
        lane.reason_count[KIRIN_REASON_NOT_KEPT] = s.cohort_count; lane.render_kind = KIRIN_RENDER_NO_SCALAR;
    }
    for (auto& p : s.head) p = {}; for (auto& p : s.tail) p = {};
}
inline void uninformativeValues (KirinAttackBandSummaryV2& s)
{
    const auto original = s;
    noValues (s);
    for (std::size_t i = 0; i < 4; ++i)
    {
        auto allReal = lowerBound (0);
        allReal.lower = {KIRIN_ENDPOINT_NEGATIVE_INFINITY, 0, {}, 0};
        allReal.unit = original.evidence[0][i].interval.unit;
        for (auto& row : s.evidence)
        {
            auto& e = row[i]; e.class_code = KIRIN_SCALAR_BOUND; e.has_interval = 1;
            e.interval = allReal; e.reason = KIRIN_REASON_LONG_TAIL;
        }
        auto& lane = s.lanes[i]; lane.class_count[KIRIN_SCALAR_UNKNOWN] = 0;
        lane.class_count[KIRIN_SCALAR_BOUND] = s.cohort_count;
        std::fill (std::begin (lane.reason_count), std::end (lane.reason_count), 0);
        lane.reason_count[KIRIN_REASON_LONG_TAIL] = s.cohort_count;
    }
}
template <typename Rig>
bool verify()
{
    for (const auto band : {0, 5})
    {
        Rig r; r.view->setBand (static_cast<std::uint8_t> (band));
        auto a = band == 0 ? allSummary (17.3) : denseSummary();
        CADENCE_Q (r.nav ({a.events, a.events + 8}) && r.view->setSummarySnapshotV2 (a, 0));
        const auto& p = r.view->presentationSnapshotV2();
        const auto first = p.summary;
        auto b = a; b.header.snapshot_revision = 901;
        b.lanes[0].whole_interval = interval (27.1, 27.1); b.lanes[0].exact_median = 27.1;
        for (auto& row : b.evidence) row[0].interval = b.lanes[0].whole_interval;
        b.head[0] = {}; b.head[1].connect_previous = 0;
        CADENCE_Q (r.view->setSummarySnapshotV2 (b, 125));
        r.view->presentationTickAt (499);
        CADENCE_Q (p.summary == first && p.header.snapshot_revision == 900);
        r.view->presentationTickAt (500);
        CADENCE_Q (p.header.snapshot_revision == 901 && p.summary->head[0].valid_count == 0);
        const auto heldText = p.lanes[0].number.value;
        auto missing = b; uninformativeValues (missing); missing.newer_unfinished_count = 1;
        missing.header.snapshot_revision = 902;
        CADENCE_Q (r.view->setSummarySnapshotV2 (missing, 501));
        r.view->presentationTickAt (1000);
        CADENCE_Q (p.summary->header.snapshot_revision == 902 && p.summary->head[0].valid_count == 0);
        CADENCE_Q (!p.lanes[0].number.valid && p.lanes[0].counts[KIRIN_SCALAR_BOUND] == 8);
        { text_style::ShownTextLog text; renderAttack (*r.view); CADENCE_Q (text.texts().contains (heldText)); }
        missing.newer_unfinished_count = 0; missing.header.snapshot_revision = 903;
        CADENCE_Q (r.view->setSummarySnapshotV2 (missing, 1001)); r.view->presentationTickAt (1500);
        { text_style::ShownTextLog text; renderAttack (*r.view); CADENCE_Q (!text.texts().contains (heldText)); }
        // Only genuinely newer pending hits permit holding. An empty cohort clears even then.
        b.header.snapshot_revision = 904; CADENCE_Q (r.view->setSummarySnapshotV2 (b, 2000));
        auto empty = a; empty.cohort_count = 0; noValues (empty);
        empty.newer_unfinished_count = 1; empty.header.snapshot_revision = 905;
        CADENCE_Q (r.view->setSummarySnapshotV2 (empty, 2500));
        { text_style::ShownTextLog text; renderAttack (*r.view); CADENCE_Q (!text.texts().contains (heldText)); }
        b.header.snapshot_revision = 906; CADENCE_Q (r.view->setSummarySnapshotV2 (b, 3000));
        const auto live = p.summary;
        CADENCE_Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
        auto locked = single (r.last, r.view->singleRequestToken());
        locked.finish = KIRIN_FINISH_FULL; allSingleLanes (locked, {8.0, 7.0, 6.0, .5});
        CADENCE_Q (r.view->setSingleSnapshotV2 (locked, 3001) && p.single && !p.live);
        CADENCE_Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
        CADENCE_Q (p.live && p.summary == live && !p.single);
        auto h = header(); h.signal_state = 0;
        CADENCE_Q (r.nav ({a.events, a.events + 8}, 3100, h));
        missing.header.snapshot_revision = 907;
        CADENCE_Q (r.view->setSummarySnapshotV2 (missing, 4000));
        CADENCE_Q (p.summary == live);
        h.signal_state = 1; h.target = KIRIN_TARGET_POST;
        CADENCE_Q (r.nav ({a.events, a.events + 8}, 4100, h));
        CADENCE_Q (!p.summary && !p.lanes[0].number.valid);
        b.header.target = KIRIN_TARGET_POST; b.header.snapshot_revision = 908;
        if (band != 0)
        {
            auto& lane = b.lanes[0]; lane = {}; lane.cohort_count = b.cohort_count;
            lane.class_count[KIRIN_SCALAR_NOT_APPLICABLE] = b.cohort_count;
            lane.reason_count[KIRIN_REASON_NO_PAIR] = b.cohort_count; lane.render_kind = KIRIN_RENDER_NO_SCALAR;
            for (auto& row : b.evidence) { row[0] = {}; row[0].class_code = KIRIN_SCALAR_NOT_APPLICABLE; row[0].reason = KIRIN_REASON_NO_PAIR; }
        }
        else
        {
            b.lanes[1].whole_interval.unit = KIRIN_INTERVAL_DBFS;
            for (auto& row : b.evidence) row[1].interval.unit = KIRIN_INTERVAL_DBFS;
        }
        CADENCE_Q (r.view->setSummarySnapshotV2 (b, 4101));
        r.view->setBand (static_cast<std::uint8_t> (band == 0 ? 5 : 0));
        CADENCE_Q (!p.summary && !p.lanes[0].number.valid);
        r.view->retireV2(); CADENCE_Q (!p.summary && !p.single && !p.lanes[0].number.valid);
    }
    return true;
}
template <typename Rig>
bool rhythmClock()
{
    for (const auto band : {0, 2, 5, 7})
    {
        Rig r; r.view->setBand (static_cast<std::uint8_t> (band));
        auto packet = allSummary (0.0); packet.header.band = static_cast<std::uint8_t> (band);
        const auto original = packet;
        if (band != 0) for (std::size_t i = 0; i < 4; ++i) {
            packet.lanes[i].whole_interval.unit = static_cast<std::uint8_t> (i == 3 ? KIRIN_INTERVAL_DECIBELS : KIRIN_INTERVAL_MILLISECONDS);
            for (auto& row : packet.evidence) row[i].interval.unit = packet.lanes[i].whole_interval.unit;
        }
        auto h = header(); double lastAdoption = -1, minimumInterval = 1e9;
        std::uint64_t previous = 0; unsigned adopted = 0, missingReturns = 0;
        for (int now = 0; now <= 4000; now += 10)
        {
            h.cutoff_sample = 480000 + now * 48;
            for (std::size_t i = 0; i < packet.cohort_count; ++i) packet.events[i].event_sample = original.events[i].event_sample + now * 48;
            for (auto& lane : packet.lanes) lane.exact_latest_event_sample = h.cutoff_sample;
            packet.header.cutoff_sample = h.cutoff_sample; packet.header.snapshot_revision = static_cast<std::uint64_t> (now + 1);
            CADENCE_Q (r.nav ({packet.events, packet.events + 8}, now, h));
            CADENCE_Q (r.view->setSummarySnapshotV2 (packet, now));
            const auto& p = r.view->presentationSnapshotV2();
            for (const auto& lane : p.lanes) if (! lane.number.valid) ++missingReturns;
            if (p.header.snapshot_revision != previous)
            {
                if (lastAdoption >= 0) { CADENCE_Q (now - lastAdoption >= 500); minimumInterval = std::min (minimumInterval, now - lastAdoption); }
                lastAdoption = now; previous = p.header.snapshot_revision; ++adopted;
            }
        }
        CADENCE_Q (adopted > 2 && missingReturns == 0);
        std::cout << "DRUM rhythm clock band=" << band << " minimum_adoption_ms=" << minimumInterval
            << " missing_returns=" << missingReturns << '\n';
    }
    return true;
}
template <typename Rig>
bool bindingRetirement()
{
    for (const auto band : {0, 5})
    {
        Rig r; r.view->setBand (static_cast<std::uint8_t> (band));
        auto packet = band == 0 ? allSummary (17.3) : denseSummary();
        packet.comparison_binding[0] = 1;
        const auto original = packet;
        CADENCE_Q (r.nav ({packet.events, packet.events + 8}) && r.view->setSummarySnapshotV2 (packet, 0));
        const auto text = r.view->presentationSnapshotV2().lanes[0].number.value;
        uninformativeValues (packet); packet.newer_unfinished_count = 1; packet.header.snapshot_revision = 901;
        CADENCE_Q (r.view->setSummarySnapshotV2 (packet, 500));
        { text_style::ShownTextLog log; renderAttack (*r.view); CADENCE_Q (log.texts().contains (text)); }
        packet.comparison_binding[0] = 2; packet.header.snapshot_revision = 902;
        for (auto& event : packet.events) event.source.incarnation[0] = 3;
        CADENCE_Q (r.view->setSummarySnapshotV2 (packet, 501));
        { text_style::ShownTextLog log; renderAttack (*r.view); CADENCE_Q (!log.texts().contains (text)); }
        auto stale = original; --stale.header.cutoff_sample; stale.header.snapshot_revision = 903;
        for (auto& event : stale.events) --event.event_sample;
        for (auto& lane : stale.lanes) --lane.exact_latest_event_sample;
        CADENCE_Q (validSummary (stale));
        CADENCE_Q (!r.view->setSummarySnapshotV2 (stale, 502));
        CADENCE_Q (r.view->presentationSnapshotV2().summary->comparison_binding[0] == 2);
        auto fresh = original; fresh.comparison_binding[0] = 2; fresh.header.snapshot_revision = 904;
        CADENCE_Q (r.view->setSummarySnapshotV2 (fresh, 1001));
        CADENCE_Q (r.view->presentationSnapshotV2().lanes[0].number.valid);
        noValues (fresh); fresh.newer_unfinished_count = 1; fresh.header.snapshot_revision = 905;
        CADENCE_Q (r.view->setSummarySnapshotV2 (fresh, 1501));
        for (const auto& lane : r.view->presentationSnapshotV2().lanes)
            CADENCE_Q (!lane.number.valid && !lane.heldNumber);
        { text_style::ShownTextLog log; renderAttack (*r.view); CADENCE_Q (!log.texts().contains (text)); }
    }
    return true;
}
template <typename Rig>
bool unfinishedHistory()
{
    for (const auto band : {0, 5})
    {
        Rig r; r.view->setBand (static_cast<std::uint8_t> (band));
        auto packet = band == 0 ? allSummary (17.3) : denseSummary();
        packet.newer_unfinished_count = 1;
        const auto pending = key (491520, 99);
        std::vector<KirinSnapshotEventKey> navigation (packet.events, packet.events + packet.cohort_count);
        navigation.push_back (pending);
        auto h = header(); h.cutoff_sample = 504000;
        CADENCE_Q (r.nav (navigation, 0, h) && r.view->setSummarySnapshotV2 (packet, 0));
        const auto& p = r.view->presentationSnapshotV2();
        CADENCE_Q (p.summary && p.summary->events[p.summary->cohort_count - 1].event_sample < pending.event_sample);
        const auto shape = geometry (580, 248, presentation::forEditor (600, 400), band == 0);
        const auto x = juce::roundToInt (eventX (pending.event_sample, p.viewport, h.source.sample_rate, shape.history));
        r.click ({x, shape.history.getCentreY()});
        CADENCE_Q (!p.live && sameEvent (r.last.event, pending) && r.view->singleRequestToken() != 0);
    }
    return true;
}
#undef CADENCE_Q
}
