#pragma once
#include "AttackV2Fixtures.h"
#include "AttackUiImageHelpers.h"
#include "AttackPaintColourLog.h"
#include "../src/HyphaAttackV2Painter.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaTextStyle.h"
#include <iostream>

namespace hypha::attack_ui_test::v2_transport_readout_contract
{
using namespace attack_v2_test;
using namespace attack_v2;
#define TRANSPORT_Q(e) do { if (!(e)) { std::cerr << "DRUM transport readout line " << __LINE__ << ": " << #e << '\n'; return false; } } while (false)

inline juce::Image renderOnBackground (juce::Component& component, PaintColourLog* log = nullptr)
{
    // The component is transparent in its parent editor. Render that actual background too;
    // reading unpremultiplied RGB from low-alpha edge pixels cannot measure a text colour.
    juce::Image image (juce::Image::RGB, component.getWidth(), component.getHeight(), true);
    if (log)
    {
        PaintColourContext context (image, log);
        juce::Graphics graphics (context); graphics.fillAll (BG);
        component.paintEntireComponent (graphics, true);
    }
    else
    {
        juce::Graphics graphics (image); graphics.fillAll (BG);
        component.paintEntireComponent (graphics, true);
    }
    return image;
}
inline KirinAttackBandSummaryV2 emptySummary (KirinSnapshotHeader h, std::uint8_t band)
{
    KirinAttackBandSummaryV2 s {};
    s.header = h; s.header.band = band; s.header.kind = KIRIN_SNAPSHOT_BAND_SUMMARY;
    s.header.struct_size = sizeof (s);
    for (auto& lane : s.lanes) lane.render_kind = KIRIN_RENDER_NO_SCALAR;
    return s;
}
inline KirinSnapshotHeader navigationFor (const KirinAttackBandSummaryV2& summary)
{
    auto h = summary.header;
    h.kind = 4; h.band = 0; h.struct_size = sizeof (KirinAttackNavigationV2);
    return h;
}
inline void postOnly (KirinAttackBandSummaryV2& s)
{
    s.header.target = KIRIN_TARGET_POST;
    const auto level = s.header.band == 0 ? 1 : 3;
    s.lanes[level].whole_interval.unit = KIRIN_INTERVAL_DBFS;
    for (auto& row : s.evidence) row[level].interval.unit = KIRIN_INTERVAL_DBFS;
    if (s.header.band == 0) return;
    auto& delay = s.lanes[0]; delay = {}; delay.cohort_count = s.cohort_count;
    delay.class_count[KIRIN_SCALAR_NOT_APPLICABLE] = s.cohort_count;
    delay.reason_count[KIRIN_REASON_NO_PAIR] = s.cohort_count; delay.render_kind = KIRIN_RENDER_NO_SCALAR;
    for (auto& row : s.evidence)
    { row[0] = {}; row[0].class_code = KIRIN_SCALAR_NOT_APPLICABLE; row[0].reason = KIRIN_REASON_NO_PAIR; row[0].finish = KIRIN_FINISH_FULL; }
    for (auto& point : s.head) point.has_pre = 0;
    for (auto& point : s.tail) point.has_pre = 0;
}
inline KirinAttackBandSummaryV2 oneHit (KirinSnapshotHeader h, std::uint8_t band)
{
    auto s = band == 0 ? allSummary (9.2) : denseSummary();
    s.header = emptySummary (h, band).header; ++s.header.snapshot_revision;
    s.cohort_count = 1; s.events[0] = {h.source, h.cutoff_sample - 1000, 101};
    for (std::size_t i = 0; i < 4; ++i)
    {
        auto& lane = s.lanes[i]; lane.cohort_count = lane.exact_count = lane.class_count[0] = 1;
        lane.exact_median = 9.2; lane.exact_latest_event_sample = s.events[0].event_sample;
        lane.whole_interval = interval (9.2, 9.2);
        lane.whole_interval.unit = band != 0 ? (i == 3 ? KIRIN_INTERVAL_DECIBELS : KIRIN_INTERVAL_MILLISECONDS)
            : i == 3 ? KIRIN_INTERVAL_ACUM : KIRIN_INTERVAL_DECIBELS;
        if (h.target != KIRIN_TARGET_DELTA && (band != 0 ? i == 3 : i == 1))
            lane.whole_interval.unit = KIRIN_INTERVAL_DBFS;
        s.evidence[0][i].interval = lane.whole_interval;
    }
    for (auto& point : s.head) point = {};
    for (auto& point : s.tail) point = {};
    if (h.target == KIRIN_TARGET_POST) postOnly (s);
    return s;
}
inline bool shownIn (const text_style::ShownTextLog& log, const juce::String& text, juce::Rectangle<int> area)
{
    for (int i = 0; i < log.texts().size(); ++i)
        if (log.texts()[i] == text && log.areas()[static_cast<std::size_t> (i)].getSmallestIntegerContainer() == area) return true;
    return false;
}
inline bool preview (juce::Component& component, int width, std::uint8_t band,
                     i18n::Language language, const char* phase)
{
    const auto root = juce::SystemStats::getEnvironmentVariable ("KIRIN_DRUM_HOLD_PREVIEW_DIR", {});
    if (root.isEmpty() || width == 600) return true;
    const auto image = renderOnBackground (component);
    const auto name = "drum-" + juce::String (width) + (band == 0 ? "-all-" : "-band-")
        + (language == i18n::Language::english ? "en-" : "ja-") + phase + ".png";
    juce::FileOutputStream output (juce::File (root).getChildFile (name));
    juce::PNGImageFormat png;
    return output.openedOk() && output.setPosition (0) && output.truncate().wasOk()
        && png.writeImageToStream (image, output);
}

template <typename Rig>
bool verify()
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    for (const int width : {300, 600, 900})
    for (const auto band : std::array<std::uint8_t, 2> {0, 5})
    // Loop, forward seek, and a backwards cutoff arriving before its generation notification.
    for (const int movement : {0, 1, 2})
    {
        const i18n::ScopedLanguage scopedLanguage (language);
        Rig r; r.view->setBand (band);
        r.view->setPresentationContext (presentation::forEditor (width, width * 2 / 3));
        r.view->setSize (width, std::max (80, width * 2 / 3 - 125));
        const auto shape = geometry (r.view->getWidth(), r.view->getHeight(),
                                     presentation::forEditor (width, width * 2 / 3), band == 0);
        r.view->beginSnapshotV2();
        auto first = band == 0 ? allSummary (17.3) : denseSummary();
        auto h = navigationFor (first);
        TRANSPORT_Q (r.nav ({first.events, first.events + 8}, 0, h) && r.view->setSummarySnapshotV2 (first, 0));
        const auto before = r.view->presentationSnapshotV2();
        PaintColourLog normal;
        renderOnBackground (*r.view, &normal);
        h.cutoff_sample = movement == 1 ? 960000 : 48000;
        if (movement != 2) ++h.source.generation;
        // The automatic loss of pairing during reacquisition must not relabel old delta values.
        h.target = KIRIN_TARGET_POST;
        TRANSPORT_Q (r.nav ({}, 1, h));
        const auto& p = r.view->presentationSnapshotV2();
        TRANSPORT_Q (!p.summary && !p.single && p.live);
        const auto checkHeld = [&] {
            text_style::ShownTextLog log;
            PaintColourLog ink;
            renderOnBackground (*r.view, &ink);
            if (movement == 0 && !preview (*r.view, width, band, language, "previous")) return false;
            for (std::size_t i = 0; i < 4; ++i)
            {
                const auto area = shape.lanes[i].value;
                if (!shownIn (log, before.lanes[i].number.value, area)
                    || !normal.has (area, COL_NORMAL) || !ink.has (area, COL_TEXT_SECONDARY)
                    || ink.has (area, COL_NORMAL)) return false;
                if (!shownIn (log, before.lanes[i].number.unit, shape.lanes[i].unit)) return false;
            }
            return log.texts().joinIntoString ("|").contains (language == i18n::Language::english ? "8 hits" : u8"8打");
        };
        TRANSPORT_Q (checkHeld());
        auto empty = emptySummary (h, band);
        TRANSPORT_Q (validSummary (empty) && r.view->setSummarySnapshotV2 (empty, 2));
        r.view->presentationTickAt (502);
        TRANSPORT_Q (p.summary && p.summary->cohort_count == 0 && checkHeld());
        TRANSPORT_Q (!r.view->setSummarySnapshotV2 (first, 503)); // No old packet can enter this generation.
        h.target = KIRIN_TARGET_DELTA;
        auto fresh = oneHit (h, band);
        TRANSPORT_Q (validSummary (fresh) && r.nav ({fresh.events[0]}, 504, h));
        TRANSPORT_Q (r.view->setSummarySnapshotV2 (fresh, 504));
        r.view->presentationTickAt (1002);
        TRANSPORT_Q (p.summary && p.summary->cohort_count == 1 && p.header.source.generation == h.source.generation);
        text_style::ShownTextLog log;
        PaintColourLog ink;
        renderOnBackground (*r.view, &ink);
        if (movement == 0) TRANSPORT_Q (preview (*r.view, width, band, language, "new"));
        for (std::size_t i = 0; i < 4; ++i)
            TRANSPORT_Q (shownIn (log, p.lanes[i].number.value, shape.lanes[i].value)
                         && std::abs (p.lanes[i].number.raw.lower.value - 9.2) < 1e-12
                         && ink.has (shape.lanes[i].value, COL_NORMAL)
                         && !ink.has (shape.lanes[i].value, COL_TEXT_SECONDARY));
        TRANSPORT_Q (log.texts().joinIntoString ("|").contains (language == i18n::Language::english ? "1 hits" : u8"1打"));
    }
    return true;
}
template <typename Rig>
bool emptyAndReset()
{
    Rig empty; empty.view->beginSnapshotV2();
    TRANSPORT_Q (empty.nav ({}));
    auto h = header(); ++h.source.generation; h.cutoff_sample = 48000;
    TRANSPORT_Q (empty.nav ({}, 1, h));
    for (const auto& lane : empty.view->presentationSnapshotV2().lanes)
        TRANSPORT_Q (!lane.number.valid && !lane.heldNumber && lane.count == 0);
    { text_style::ShownTextLog log; renderOnBackground (*empty.view); TRANSPORT_Q (log.texts().contains (juce::String::fromUTF8 (u8"—"))); }
    for (const int reset : {0, 1, 2, 3})
    {
        Rig r; auto first = allSummary (17.3);
        auto current = navigationFor (first);
        TRANSPORT_Q (r.nav ({first.events, first.events + 8}, 0, current) && r.view->setSummarySnapshotV2 (first, 0));
        ++current.source.generation; current.cutoff_sample = 48000;
        TRANSPORT_Q (r.nav ({}, 1, current) && r.view->presentationSnapshotV2().previousReadout);
        if (reset == 0) r.view->setBand (5);
        if (reset == 1) r.view->retireV2();
        if (reset == 2) r.view->beginSnapshotV2();
        if (reset == 3) { ++current.authority_revision; TRANSPORT_Q (r.nav ({}, 2, current)); }
        text_style::ShownTextLog log; renderOnBackground (*r.view);
        TRANSPORT_Q (!log.texts().contains ("+17.3"));
    }
    return true;
}
template <typename Rig>
bool retiredAndOwner()
{
    for (const auto band : std::array<std::uint8_t, 2> {0, 5})
    for (const int ownerChange : {0, 1, 2, 3, 4, 5, 6})
    {
        Rig r; r.view->setBand (band);
        auto first = band == 0 ? allSummary (17.3) : denseSummary();
        auto h = navigationFor (first);
        TRANSPORT_Q (r.nav ({first.events, first.events + 8}, 0, h) && r.view->setSummarySnapshotV2 (first, 0));
        // The editor receives RETIRED before it can observe a new navigation generation.
        r.view->retireV2 (true); r.view->retireV2 (true);
        const auto& p = r.view->presentationSnapshotV2();
        TRANSPORT_Q (p.previousReadout && p.previousReadout->count == 8 && !p.summary && !p.single && !p.representative);
        TRANSPORT_Q (!r.view->setSummarySnapshotV2 (first, 1) && p.previousReadout && !p.summary);
        for (const auto& lane : p.lanes) TRANSPORT_Q (!lane.number.valid && lane.count == 0);
        auto invalid = h; invalid.version = 99;
        TRANSPORT_Q (!r.nav ({}, 1, invalid) && p.previousReadout);
        ++h.source.generation; h.cutoff_sample = 48000; h.target = KIRIN_TARGET_POST;
        if (ownerChange == 1) ++h.authority_revision;
        if (ownerChange == 2) ++h.source.incarnation[0];
        if (ownerChange == 3) h.source.sample_rate = 96000;
        if (ownerChange == 4) h.source.channels = 1;
        if (ownerChange == 5) ++h.source.odf_hash[0];
        if (ownerChange == 6) ++h.band_semantic_hash[0];
        TRANSPORT_Q (r.nav ({}, 2, h));
        if (ownerChange != 0) { TRANSPORT_Q (!p.previousReadout); continue; }
        TRANSPORT_Q (p.previousReadout && p.previousReadout->target == KIRIN_TARGET_DELTA);
        auto empty = emptySummary (h, band);
        TRANSPORT_Q (r.view->setSummarySnapshotV2 (empty, 3));
        // A completed but unavailable hit is not a new main value.
        auto unknown = oneHit (h, band);
        for (std::size_t i = 0; i < 4; ++i)
        {
            auto& lane = unknown.lanes[i]; lane = {}; lane.cohort_count = 1;
            lane.class_count[KIRIN_SCALAR_UNKNOWN] = 1; lane.render_kind = KIRIN_RENDER_NO_SCALAR;
            lane.reason_count[KIRIN_REASON_NOT_KEPT] = 1;
            auto& e = unknown.evidence[0][i]; e = {}; e.class_code = KIRIN_SCALAR_UNKNOWN;
            e.reason = KIRIN_REASON_NOT_KEPT; e.finish = KIRIN_FINISH_FULL;
        }
        TRANSPORT_Q (validSummary (unknown) && r.view->setSummarySnapshotV2 (unknown, 503));
        TRANSPORT_Q (p.summary && p.summary->cohort_count == 1 && p.previousReadout && p.previousReadout->count == 8);
        h.signal_state = 0; TRANSPORT_Q (r.nav ({}, 504, h) && p.previousReadout);
        h.signal_state = 1; h.target = KIRIN_TARGET_DELTA;
        TRANSPORT_Q (r.nav ({}, 505, h) && p.previousReadout);
        auto partial = oneHit (h, band);
        auto& unavailable = partial.lanes[3]; unavailable = {}; unavailable.cohort_count = 1;
        unavailable.class_count[KIRIN_SCALAR_UNKNOWN] = 1; unavailable.render_kind = KIRIN_RENDER_NO_SCALAR;
        unavailable.reason_count[KIRIN_REASON_NOT_KEPT] = 1;
        auto& e = partial.evidence[0][3]; e = {}; e.class_code = KIRIN_SCALAR_UNKNOWN;
        e.reason = KIRIN_REASON_NOT_KEPT; e.finish = KIRIN_FINISH_FULL;
        TRANSPORT_Q (r.view->setSummarySnapshotV2 (partial, 506) && p.previousReadout);
        auto fresh = oneHit (h, band);
        ++fresh.header.snapshot_revision;
        TRANSPORT_Q (r.nav ({fresh.events[0]}, 507, h) && r.view->setSummarySnapshotV2 (fresh, 507));
        r.view->presentationTickAt (1006);
        TRANSPORT_Q (!p.previousReadout && p.summary && p.summary->cohort_count == 1);
    }
    for (const auto band : std::array<std::uint8_t, 2> {0, 5})
    for (const bool newGeneration : {false, true})
    {
        Rig r; r.view->setBand (band);
        auto first = band == 0 ? allSummary (17.3) : denseSummary();
        auto h = navigationFor (first);
        TRANSPORT_Q (r.nav ({first.events, first.events + 8}, 0, h) && r.view->setSummarySnapshotV2 (first, 0));
        h.target = KIRIN_TARGET_POST;
        // Proof availability may switch first, while local navigation still names the old pass.
        TRANSPORT_Q (r.nav ({first.events, first.events + 8}, 1, h));
        const auto& p = r.view->presentationSnapshotV2();
        TRANSPORT_Q (p.previousReadout && p.previousReadout->target == KIRIN_TARGET_DELTA);
        auto fallback = first; postOnly (fallback);
        TRANSPORT_Q (r.view->setSummarySnapshotV2 (fallback, 2));
        TRANSPORT_Q (p.previousReadout); // The same old hits in POST are not a new hit's values.
        if (newGeneration) { ++h.source.generation; h.cutoff_sample = 48000; }
        else h.cutoff_sample += 4800;
        TRANSPORT_Q (r.nav ({}, 3, h) && p.previousReadout);
        auto fresh = oneHit (h, band);
        TRANSPORT_Q (r.nav ({fresh.events[0]}, 4, h) && r.view->setSummarySnapshotV2 (fresh, 4));
        r.view->presentationTickAt (502);
        TRANSPORT_Q (p.previousReadout && p.summary && p.summary->cohort_count == 1);
        h.target = KIRIN_TARGET_DELTA;
        TRANSPORT_Q (r.nav ({fresh.events[0]}, 503, h));
        auto paired = oneHit (h, band);
        TRANSPORT_Q (r.view->setSummarySnapshotV2 (paired, 504));
        TRANSPORT_Q (!p.previousReadout && p.header.target == KIRIN_TARGET_DELTA);
        // A deliberately chosen POST scope starts with its own values. Regaining qualified
        // delta for that same new hit need not wait for yet another hit.
        r.view->retireV2(); h.target = KIRIN_TARGET_POST;
        TRANSPORT_Q (r.nav ({fresh.events[0]}, 505, h) && r.view->setSummarySnapshotV2 (fresh, 505));
        TRANSPORT_Q (!p.previousReadout && p.header.target == KIRIN_TARGET_POST);
        h.target = KIRIN_TARGET_DELTA;
        TRANSPORT_Q (r.nav ({fresh.events[0]}, 506, h) && r.view->setSummarySnapshotV2 (paired, 507));
        TRANSPORT_Q (!p.previousReadout && p.header.target == KIRIN_TARGET_DELTA);
    }
    {
        Rig r; const auto first = allSummary (17.3);
        const auto h = navigationFor (first);
        TRANSPORT_Q (r.nav ({first.events, first.events + 8}, 0, h) && r.view->setSummarySnapshotV2 (first, 0));
        TRANSPORT_Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
        auto locked = single (r.last, r.view->singleRequestToken()); locked.finish = KIRIN_FINISH_FULL;
        std::copy (std::begin (h.band_semantic_hash), std::end (h.band_semantic_hash), std::begin (locked.header.band_semantic_hash));
        allSingleLanes (locked, {8.8, 7.7, 6.6, .4});
        TRANSPORT_Q (r.view->setSingleSnapshotV2 (locked, 1));
        r.view->retireV2 (true);
        const auto& p = r.view->presentationSnapshotV2();
        TRANSPORT_Q (p.previousReadout && p.previousReadout->count == 1 && !p.single && p.live
            && r.view->singleRequestToken() == 0 && std::abs (p.previousReadout->numbers[0].raw.lower.value - 8.8) < 1e-12);
    }
    return true;
}
#undef TRANSPORT_Q
}
