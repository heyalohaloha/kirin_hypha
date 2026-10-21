#pragma once
#include "AttackV2Fixtures.h"
#include "AttackUiLaneContract.h"
#include "../src/HyphaAttackV2Painter.h"
#include "../src/HyphaTextStyle.h"
#include <iostream>

namespace hypha::attack_ui_test::v2_loupe_contract
{
using namespace attack_v2_test;
using namespace attack_v2;
#define LOUPE_Q(e) do { if (!(e)) { std::cerr << "DRUM loupe line " << __LINE__ << ": " << #e << '\n'; return false; } } while (false)
template <typename Rig>
bool verify()
{
    Rig r; const auto context = presentation::forEditor (900, 600);
    r.view->setPresentationContext (context); r.view->setSize (872, 412);
    auto packet = allSummary();
    LOUPE_Q (r.nav ({packet.events, packet.events + packet.cohort_count}));
    const auto shape = geometry (872, 412, context, true);
    LOUPE_Q (!shape.loupe.isEmpty());
    const auto empty = renderAttack (*r.view);
    packet.has_representative_post = 1;
    packet.representative_post = laneDetail (packet.events[7].event_sample);
    LOUPE_Q (r.view->setSummarySnapshotV2 (packet, 0));
    const auto& p = r.view->presentationSnapshotV2();
    const auto first = p.representative;
    LOUPE_Q (first && sameEvent (first->key, packet.events[7]) && !p.single);
    const auto measured = renderAttack (*r.view);
    LOUPE_Q (differences (empty, measured, shape.loupe) > 0);
    auto next = packet; ++next.header.snapshot_revision;
    next.events[7].token += 100;
    for (auto& value : next.representative_post.shape) value *= .1f;
    LOUPE_Q (r.view->setSummarySnapshotV2 (next, 125));
    r.view->presentationTickAt (499);
    LOUPE_Q (p.representative == first && differences (measured, renderAttack (*r.view), shape.loupe) == 0);
    r.view->presentationTickAt (500);
    LOUPE_Q (p.representative != first && sameEvent (p.representative->key, next.events[7]));
    const auto changed = renderAttack (*r.view);
    LOUPE_Q (differences (measured, changed, shape.loupe) > 0);
    const auto second = p.representative;
    // A fresh stamp about the same completed event must reuse its immutable shape and image.
    ++next.header.snapshot_revision;
    for (auto& value : next.representative_post.shape) value *= .1f;
    LOUPE_Q (r.view->setSummarySnapshotV2 (next, 1000));
    LOUPE_Q (p.representative == second && differences (changed, renderAttack (*r.view), shape.loupe) == 0);
    LOUPE_Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    auto locked = single (r.last, r.view->singleRequestToken());
    locked.has_all_post = 1; locked.all_post = laneDetail (r.last.event.event_sample);
    locked.finish = KIRIN_FINISH_FULL;
    LOUPE_Q (r.view->setSingleSnapshotV2 (locked, 1001) && p.single && !p.representative);
    LOUPE_Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::endKey)) && p.representative == second);
    r.view->setBand (5); LOUPE_Q (!p.representative);
    LOUPE_Q (r.nav ({packet.events, packet.events + packet.cohort_count}, 1500));
    r.view->setBand (0);
    LOUPE_Q (r.nav ({packet.events, packet.events + packet.cohort_count}, 1501));
    ++packet.header.snapshot_revision;
    LOUPE_Q (r.view->setSummarySnapshotV2 (packet, 1501) && p.representative);
    r.view->retireV2(); LOUPE_Q (!p.representative && !p.summary);
    return true;
}
template <typename Rig>
bool confirmedSubsetRetentionAndUnits()
{
    for (const auto target : {KIRIN_TARGET_POST, KIRIN_TARGET_DELTA})
    {
        Rig r; auto packet = allSummary (17.3); packet.header.target = target;
        packet.lanes[1].whole_interval.unit = target == KIRIN_TARGET_POST ? KIRIN_INTERVAL_DBFS : KIRIN_INTERVAL_DECIBELS;
        for (auto& row : packet.evidence) row[1].interval.unit = packet.lanes[1].whole_interval.unit;
        auto h = header(); h.target = target;
        LOUPE_Q (r.nav ({packet.events, packet.events + packet.cohort_count}, 0, h));
        LOUPE_Q (r.view->setSummarySnapshotV2 (packet, 0));
        const auto original = r.view->presentationSnapshotV2();
        // A supported legacy subset stamp: four exact hits and four unknowns.
        for (std::size_t i = 0; i < 4; ++i)
        {
            auto& lane = packet.lanes[i];
            std::fill (std::begin (lane.class_count), std::end (lane.class_count), 0);
            std::fill (std::begin (lane.reason_count), std::end (lane.reason_count), 0);
            lane.class_count[KIRIN_SCALAR_EXACT] = lane.exact_count = 4;
            lane.class_count[KIRIN_SCALAR_UNKNOWN] = lane.reason_count[KIRIN_REASON_CLOCK] = 4;
            lane.reason_count[KIRIN_REASON_NONE] = 4;
            lane.render_kind = KIRIN_RENDER_CONFIRMED_SUBSET;
            lane.whole_median_available = lane.whole_numeric_informative = 0;
            lane.exact_median = 1e12; lane.exact_latest_event_sample = packet.events[3].event_sample;
            for (std::size_t hit = 0; hit < 8; ++hit)
            {
                auto& e = packet.evidence[hit][i];
                if (hit < 4)
                {
                    const auto unit = e.interval.unit;
                    e.interval = interval (1e12, 1e12); e.interval.unit = unit;
                }
                else { e = {}; e.class_code = KIRIN_SCALAR_UNKNOWN; e.reason = KIRIN_REASON_CLOCK; e.finish = KIRIN_FINISH_FULL; }
            }
        }
        LOUPE_Q (validSummary (packet));
        packet.newer_unfinished_count = 1; ++packet.header.snapshot_revision;
        LOUPE_Q (r.view->setSummarySnapshotV2 (packet, 500));
        const auto& p = r.view->presentationSnapshotV2();
        for (std::size_t i = 0; i < 4; ++i)
            LOUPE_Q (p.lanes[i].heldNumber && p.lanes[i].heldNumber->value == original.lanes[i].number.value);
        { text_style::ShownTextLog log; renderAttack (*r.view);
          LOUPE_Q (log.texts().contains (original.lanes[0].number.value) && !log.texts().contains ("ms")); }
        packet.newer_unfinished_count = 0; ++packet.header.snapshot_revision;
        LOUPE_Q (r.view->setSummarySnapshotV2 (packet, 1000));
        { text_style::ShownTextLog log; renderAttack (*r.view);
          LOUPE_Q (!log.texts().contains (original.lanes[0].number.value) && log.texts().contains ("acum")
              && log.texts().contains (target == KIRIN_TARGET_POST ? "dBFS" : "dB") && !log.texts().contains ("ms")); }
    }
    return true;
}
#undef LOUPE_Q
}
