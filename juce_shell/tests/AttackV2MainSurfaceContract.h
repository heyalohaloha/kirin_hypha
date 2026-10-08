#pragma once
#include "AttackV2Fixtures.h"
#include "AttackUiLaneContract.h"
#include "../src/HyphaAttackV2Painter.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaMaterialCache.h"
#include "../src/HyphaTextStyle.h"
#include <iostream>
#include <limits>

// Native production paint and Facts checks are separate from producer/selection contracts.
namespace hypha::attack_ui_test::v2_main_surface_contract
{
using namespace attack_v2_test;
using namespace attack_v2;
#define MAIN_Q(e) do { if (! (e)) { std::cerr << "DRUM main surface line " << __LINE__ << ": " << #e << '\n'; return false; } } while (false)
template <typename Rig>
bool quietValuesAndFacts (const presentation::Context& context, int width, int height)
{
    Rig r; r.view->setPresentationContext (context); r.view->setSize (width, height); r.view->setBand (5);
    auto partial = summary();
    for (std::size_t metric = 0; metric < 4; ++metric)
    {
        auto& lane = partial.lanes[metric];
        std::fill (std::begin (lane.class_count), std::end (lane.class_count), 0);
        std::fill (std::begin (lane.reason_count), std::end (lane.reason_count), 0);
        lane.class_count[KIRIN_SCALAR_EXACT] = lane.exact_count = 4;
        lane.class_count[KIRIN_SCALAR_UNKNOWN] = lane.reason_count[KIRIN_REASON_CLOCK] = 4;
        lane.render_kind = KIRIN_RENDER_CONFIRMED_SUBSET;
        lane.whole_median_available = lane.whole_numeric_informative = 0;
        lane.exact_median = 10.8 + static_cast<double> (metric);
        lane.exact_latest_event_sample = partial.events[3].event_sample;
        for (std::size_t hit = 0; hit < 8; ++hit)
        {
            auto& evidence = partial.evidence[hit][metric];
            evidence.class_code = hit < 4 ? KIRIN_SCALAR_EXACT : KIRIN_SCALAR_UNKNOWN;
            evidence.has_interval = hit < 4; evidence.reason = hit < 4 ? 0 : KIRIN_REASON_CLOCK;
            evidence.interval = interval (lane.exact_median, lane.exact_median);
            evidence.interval.unit = metric == 3 ? KIRIN_INTERVAL_DECIBELS : KIRIN_INTERVAL_MILLISECONDS;
        }
    }
    MAIN_Q (r.nav ({ partial.events, partial.events + 8 }));
    MAIN_Q (r.view->setSummarySnapshotV2 (partial, 0));
    const auto before = r.view->presentationSnapshotV2();
    {
        text_style::ShownTextLog main; MAIN_Q (renderAttack (*r.view).isValid());
        int dashes = 0; for (const auto& text : main.texts()) if (text == juce::String::fromUTF8 (u8"—")) ++dashes;
        MAIN_Q (dashes == 4 && main.texts().contains ("ms") && main.texts().contains ("dB"));
        for (const auto& lane : before.lanes)
            MAIN_Q (! main.texts().contains (lane.number.value) && ! main.texts().contains (scopeText (lane, true)));
    }
    MAIN_Q (r.view->presentationSnapshotV2().summary == before.summary
        && r.view->presentationSnapshotV2().revision == before.revision);
    {
        text_style::ShownTextLog facts;
        MAIN_Q (r.view->keyPressed (juce::KeyPress ('i', {}, 'i'))); MAIN_Q (renderAttack (*r.view).isValid());
        const auto text = facts.texts().joinIntoString ("\n").removeCharacters (" \n");
        const std::array<const char*, 4> labels { "DELAY", "ATT", "REL", "LEVEL" };
        for (std::size_t metric = 0; metric < 4; ++metric)
        {
            const auto& lane = before.lanes[metric];
            const auto expected = juce::String (labels[metric]) + " / " + scopeText (lane, true)
                + " / " + lane.number.value + " " + lane.number.unit;
            MAIN_Q (text.contains (expected.removeCharacters (" \n")));
        }
        MAIN_Q (text.contains ((words ("Exact / bound / unknown / pending / N/A: ",
            u8"確定 / 限界 / 不明 / 取得中 / 不成立: ") + "4/0/4/0/0").removeCharacters (" \n")));
        MAIN_Q (text.contains (words ("Exact median ", u8"確定部分中央値 ").removeCharacters (" \n"))
            && text.contains (words (" / age ", u8" / 古さ ").removeCharacters (" \n"))
            && text.contains ((reasonText (KIRIN_REASON_CLOCK, true) + " 4").removeCharacters (" \n")));
        MAIN_Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    }
    // A selected single is still a measured value, independent from the aggregate's quiet dash.
    MAIN_Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    auto selected = single (r.last, r.view->singleRequestToken()); selected.finish = KIRIN_FINISH_FULL;
    for (std::size_t metric = 0; metric < 4; ++metric)
    {
        auto& lane = selected.lanes[metric]; lane.class_code = KIRIN_SCALAR_EXACT;
        lane.has_interval = 1; lane.reason = 0; lane.finish = KIRIN_FINISH_FULL;
        lane.interval = interval (30.8 + static_cast<double> (metric), 30.8 + static_cast<double> (metric));
        lane.interval.unit = metric == 3 ? KIRIN_INTERVAL_DECIBELS : KIRIN_INTERVAL_MILLISECONDS;
    }
    MAIN_Q (r.view->setSingleSnapshotV2 (selected, 1));
    {
        text_style::ShownTextLog main; MAIN_Q (renderAttack (*r.view).isValid());
        MAIN_Q (! main.texts().contains (juce::String::fromUTF8 (u8"—")));
        for (const auto& lane : r.view->presentationSnapshotV2().lanes)
            MAIN_Q (lane.scope == Scope::single && main.texts().contains (lane.number.value)
                && ! main.texts().contains (scopeText (lane, false)));
    }
    return true;
}
template <typename Rig>
bool allSinglesQuiet (const presentation::Context& context, int width, int height)
{
    Rig r; r.view->setPresentationContext (context); r.view->setSize (width, height);
    auto h = header(); h.cutoff_sample = 520000;
    MAIN_Q (r.nav ({key (400000, 1), key (470000, 2)}, 0, h));
    {
        text_style::ShownTextLog waiting; MAIN_Q (renderAttack (*r.view).isValid());
        int count = 0; for (const auto& text : waiting.texts()) if (text == juce::String::fromUTF8 (u8"—")) ++count;
        MAIN_Q (count == 4);
    }
    for (const bool live : { true, false })
    {
        if (! live) MAIN_Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
        auto one = single (r.last, r.view->singleRequestToken()); one.header.cutoff_sample = h.cutoff_sample;
        one.has_all_pre = one.has_all_post = 1;
        one.all_post = laneDetail (r.last.event.event_sample);
        one.all_pre = laneDetail (r.last.event.event_sample, 5);
        one.all_pre.transient_db = 7; one.all_pre.body_rms_dbfs = -24;
        one.all_pre.crest_db = 7; one.all_pre.sharpness_acum = 1.2f;
        one.finish = KIRIN_FINISH_FULL; MAIN_Q (r.view->setSingleSnapshotV2 (one, live ? 0 : 1));
        text_style::ShownTextLog main; MAIN_Q (renderAttack (*r.view).isValid());
        const auto& adopted = r.view->presentationSnapshotV2();
        MAIN_Q (adopted.live == live && adopted.single && adopted.single->event.event_sample == r.last.event.event_sample);
        for (const auto& lane : adopted.lanes)
            MAIN_Q (lane.scope == Scope::single && lane.number.valid && main.texts().contains (lane.number.value)
                && ! main.texts().contains (scopeText (lane, live))
                && (laneReason (lane).isEmpty() || ! main.texts().contains (laneReason (lane))));
        const auto text = main.texts().joinIntoString ("\n");
        MAIN_Q (! text.contains (words ("Max age ", u8"確定最大古さ"))
            && ! text.contains (words (" old", u8" 古さ")));
        {
            text_style::ShownTextLog facts;
            MAIN_Q (r.view->keyPressed (juce::KeyPress ('i', {}, 'i')));
            MAIN_Q (renderAttack (*r.view).isValid());
            const auto evidence = facts.texts().joinIntoString ("\n");
            MAIN_Q (! evidence.contains (words ("Requested ", u8"要求窓 "))
                && ! evidence.contains (words ("Measured ", u8"実測窓 "))
                && ! evidence.contains (words ("Resolution ", u8"判別範囲 ")));
            MAIN_Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
        }
    }
    return true;
}
template <typename Rig>
bool wholeValuesVisible (const presentation::Context& context, int width, int height)
{
    Rig r; r.view->setPresentationContext (context); r.view->setSize (width, height); r.view->setBand (5);
    auto whole = denseSummary(); MAIN_Q (r.nav ({ whole.events, whole.events + 8 }));
    MAIN_Q (r.view->setSummarySnapshotV2 (whole, 0));
    text_style::ShownTextLog main; MAIN_Q (renderAttack (*r.view).isValid());
    MAIN_Q (! main.texts().contains (juce::String::fromUTF8 (u8"—")));
    for (const auto& lane : r.view->presentationSnapshotV2().lanes)
        MAIN_Q (lane.scope == Scope::wholePoint && lane.number.valid && main.texts().contains (lane.number.value));
    {
        text_style::ShownTextLog facts;
        MAIN_Q (r.view->keyPressed (juce::KeyPress ('i', {}, 'i')));
        MAIN_Q (renderAttack (*r.view).isValid());
        MAIN_Q (! facts.texts().joinIntoString ("\n").contains (words (" / age ", u8" / 古さ ")));
    }
    return true;
}
template <typename Rig>
bool verify()
{
    material_cache::Lifetime lifetime;
    const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_ATTACK_V2_ARTIFACT_DIR", {});
    const std::array<int, 5> widths { 292, 363, 434, 580, 872 }, heights { 120, 158, 164, 248, 412 }, editors { 300, 375, 450, 600, 900 };
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage locale (language);
        for (std::size_t size = 0; size < 5; ++size)
        {
            std::cout << "DRUM V2 native layout editor=" << editors[size] << " language="
                << (language == i18n::Language::english ? "en" : "ja") << '\n';
            Rig r; const auto c = presentation::forEditor (editors[size], editors[size] * 2 / 3);
            r.view->setPresentationContext (c); r.view->setSize (widths[size], heights[size]); r.view->setBand (5);
            if (output.isNotEmpty())
            {
                Rig all; all.view->setPresentationContext (c); all.view->setSize (widths[size], heights[size]);
                auto h = header(); h.cutoff_sample = 520000; MAIN_Q (all.nav ({key (400000, 1), key (470000, 2)}, 0, h));
                auto one = single (all.last, all.view->singleRequestToken()); one.header.cutoff_sample = h.cutoff_sample;
                one.has_all_pre = one.has_all_post = 1; one.all_post = laneDetail (470000); one.all_pre = laneDetail (470000, 5);
                one.all_pre.transient_db = 7; one.all_pre.body_rms_dbfs = -24; one.all_pre.crest_db = 7; one.all_pre.sharpness_acum = 1.2f;
                one.finish = KIRIN_FINISH_FULL; MAIN_Q (all.view->setSingleSnapshotV2 (one, 0));
                const auto file = juce::File (output).getChildFile ("drum-v2-all-" + juce::String (editors[size]) + "-"
                    + (language == i18n::Language::english ? "en" : "ja") + ".png");
                MAIN_Q (file.getParentDirectory().createDirectory().wasOk()); juce::FileOutputStream stream (file); juce::PNGImageFormat png;
                MAIN_Q (stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk() && png.writeImageToStream (renderAttack (*all.view), stream));
            }
            for (const auto all : { false, true })
            {
                const auto roles = geometry (widths[size], heights[size], c, all);
                const std::array<const char*, 4> labels = all
                    ? std::array<const char*, 4> { "TRANSIENT", "STRENGTH", "CREST", "SHARPNESS" }
                    : std::array<const char*, 4> { "DELAY", "ATT", "REL", "LEVEL" };
                const std::array<const char*, 4> units = all
                    ? std::array<const char*, 4> { "dB", "dBFS", "dB", "acum" }
                    : std::array<const char*, 4> { "ms", "ms", "ms", "dBFS" };
                for (std::size_t i = 0; i < labels.size(); ++i)
                {
                    MAIN_Q (text_style::shownWidth (monoFont (c, typography::TextRole::metricLabel, typography::Composition::visualization), labels[i]) <= roles.lanes[i].metric.getWidth());
                    MAIN_Q (text_style::shownWidth (monoFont (c, typography::TextRole::unit, typography::Composition::visualization), units[i]) <= roles.lanes[i].unit.getWidth());
                    MAIN_Q (monoFont (c, typography::TextRole::metricLabel, typography::Composition::visualization).getHeight() <= roles.lanes[i].metric.getHeight());
                    MAIN_Q (monoFont (c, typography::TextRole::unit, typography::Composition::visualization).getHeight() <= roles.lanes[i].unit.getHeight());
                    MAIN_Q (monoFont (c, typography::TextRole::readout, typography::Composition::visualization).getHeight() <= roles.lanes[i].scope.getHeight());
                    MAIN_Q (monoFont (c, typography::TextRole::primaryValue, typography::Composition::visualization).getHeight() <= roles.lanes[i].value.getHeight());
                }
                if (! all && size >= 3)
                {
                    const auto font = monoFont (c, typography::TextRole::axis, typography::Composition::visualization);
                    MAIN_Q (text_style::shownWidth (font, juce::String::fromUTF8 (u8"HEAD −20…+40 ms"))
                        + text_style::shownWidth (font, words ("Mean(dB)", u8"平均(dB)") + juce::String::fromUTF8 (u8" 0–8/8")) + 4 <= roles.head.getWidth() - 8);
                }
            }
            auto s = std::make_unique<KirinAttackBandSummaryV2> (summary()); MAIN_Q (r.nav ({ s->events, s->events + 8 }));
            MAIN_Q (r.view->setSummarySnapshotV2 (*s, 0));
            const auto shape = geometry (widths[size], heights[size], c);
            for (const auto& lane : shape.lanes)
            {
                MAIN_Q (r.view->getLocalBounds().contains (lane.metric) && r.view->getLocalBounds().contains (lane.value));
                MAIN_Q (! lane.metric.intersects (lane.scope) && ! lane.scope.intersects (lane.value));
                MAIN_Q (text_style::shownWidth (monoFont (c, typography::TextRole::primaryValue, typography::Composition::visualization),
                    juce::String::fromUTF8 (u8"[−999.9,+999.9]")) <= lane.value.getWidth());
            }
            text_style::ShownTextLog log; const auto image = renderAttack (*r.view);
            MAIN_Q (! log.texts().joinIntoString ("\n").containsChar (0x00e2));
            if (size >= 3)
            {
                MAIN_Q (log.texts().contains (juce::String::fromUTF8 (u8"HEAD −20…+40 ms")));
                MAIN_Q (log.texts().contains (juce::String::fromUTF8 (u8"TAIL 0…+300 ms")));
            }
            MAIN_Q (log.texts().contains (juce::String::fromUTF8 (u8"≥+200")));
            const auto& adopted = r.view->presentationSnapshotV2();
            MAIN_Q (log.texts().contains (juce::String::fromUTF8 (u8"—")));
            MAIN_Q (! log.texts().contains (adopted.lanes[0].number.value));
            for (const auto& lane : adopted.lanes)
            {
                MAIN_Q (! log.texts().contains (scopeText (lane, true)));
                MAIN_Q (laneReason (lane).isEmpty() || ! log.texts().contains (laneReason (lane)));
            }
            const auto mainText = log.texts().joinIntoString ("\n");
            MAIN_Q (! mainText.contains (words ("Max age ", u8"確定最大古さ"))
                && ! mainText.contains (words (" old", u8" 古さ")));
            MAIN_Q (quietValuesAndFacts<Rig> (c, widths[size], heights[size]));
            MAIN_Q (allSinglesQuiet<Rig> (c, widths[size], heights[size]));
            MAIN_Q (wholeValuesVisible<Rig> (c, widths[size], heights[size]));
            if (output.isNotEmpty())
            {
                const auto file = juce::File (output).getChildFile ("drum-v2-" + juce::String (editors[size]) + "-"
                    + (language == i18n::Language::english ? "en" : "ja") + ".png");
                MAIN_Q (file.getParentDirectory().createDirectory().wasOk()); juce::FileOutputStream stream (file); juce::PNGImageFormat png;
                MAIN_Q (stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk() && png.writeImageToStream (image, stream));
            }
            // Actual native fonts prove the full endpoint range and maximum exponent at every size.
            for (const auto magnitude : { 999.9, 3202.6, std::numeric_limits<double>::max() })
            {
                auto broad = *s; broad.header.snapshot_revision += static_cast<std::uint64_t> (magnitude > 1e300 ? 4 : magnitude > 1000 ? 3 : 2);
                auto& level = broad.lanes[3]; std::fill (std::begin (level.class_count), std::end (level.class_count), 0);
                level.class_count[1] = 8; level.exact_count = 0; level.render_kind = KIRIN_RENDER_WHOLE_INTERVAL;
                level.whole_interval = interval (-magnitude, magnitude); level.whole_interval.unit = KIRIN_INTERVAL_DECIBELS;
                for (auto& row : broad.evidence) { row[3].class_code = KIRIN_SCALAR_BOUND; row[3].interval = level.whole_interval; }
                MAIN_Q (r.view->setSummarySnapshotV2 (broad, magnitude > 1e300 ? 1750 : magnitude > 1000 ? 1500 : 1250)); // force immediate publication without elapsed-value interpolation
                const auto& lane = r.view->presentationSnapshotV2().lanes[3];
                MAIN_Q (text_style::shownWidth (monoFont (c, typography::TextRole::primaryValue, typography::Composition::visualization), lane.number.value) <= shape.lanes[3].value.getWidth());
                MAIN_Q (text_style::shownWidth (monoFont (c, typography::TextRole::unit, typography::Composition::visualization), lane.number.unit) <= shape.lanes[3].unit.getWidth());
                { text_style::ShownTextLog whole; MAIN_Q (renderAttack (*r.view).isValid() && whole.texts().contains (lane.number.value)); }
                if (output.isNotEmpty())
                {
                    const auto condition = magnitude > 1e300 ? "exp308" : magnitude > 1000 ? "exp3" : "finite999";
                    const auto file = juce::File (output).getChildFile ("drum-v2-" + juce::String (editors[size]) + "-"
                        + (language == i18n::Language::english ? "en-" : "ja-") + condition + ".png");
                    juce::FileOutputStream stream (file); juce::PNGImageFormat png; MAIN_Q (stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk() && png.writeImageToStream (renderAttack (*r.view), stream));
                }
            }
            // Unavailable producer values stay unchanged; the main surface uses one quiet dash.
            for (auto& lane : s->lanes) { std::fill (std::begin (lane.class_count), std::end (lane.class_count), 0); lane.exact_count = 0; lane.class_count[3] = 8; lane.render_kind = KIRIN_RENDER_NO_SCALAR; lane.whole_median_available = lane.whole_numeric_informative = 0; }
            for (auto& row : s->evidence) for (auto& e : row) { e.class_code = KIRIN_SCALAR_PENDING; e.has_interval = 0; }
            s->header.snapshot_revision = 901; MAIN_Q (r.view->setSummarySnapshotV2 (*s, 2000));
            MAIN_Q (r.view->presentationSnapshotV2().lanes[0].number.value == "---");
            {
                text_style::ShownTextLog missing; MAIN_Q (renderAttack (*r.view).isValid());
                int count = 0; for (const auto& text : missing.texts()) if (text == juce::String::fromUTF8 (u8"—")) ++count;
                MAIN_Q (count == 4);
            }
        }
    }
    return true;
}
#undef MAIN_Q
}
