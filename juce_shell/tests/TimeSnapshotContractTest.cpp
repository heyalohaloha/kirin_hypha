#include "TimeSnapshotContractTest.h"
#include "TimeComparisonLifetimeContractTest.h"
#include "SessionCoverageContractTest.h"
#include "TimeSnapshotInputContractTest.h"
#include "../src/HyphaTimeSnapshotPainter.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaTextStyle.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace hypha::tests
{
namespace
{
using time_snapshot::Metric;
using time_snapshot::Presentation;

void require (bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "TIME V2 presentation failed: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

KirinTimeSnapshotV2 packet()
{
    KirinTimeSnapshotV2 result {};
    result.version = KIRIN_TIME_SNAPSHOT_VERSION;
    result.struct_size = sizeof (result);
    result.revision = 7;
    result.local_cutoff = 480'000;
    result.range_start = 0;
    result.post_span = { 41, 42, 3, 43, 48'000, 2, {} };
    result.binding_revision = 11;
    result.signal_state = KIRIN_SIGNAL_STATE_ACTIVE;
    result.selection_intent = 1;
    for (auto* component : { &result.main, &result.psr })
    {
        component->current.span = result.post_span;
        component->current.cutoff = result.local_cutoff;
        component->current.run = 5;
        component->current.endpoint = 900'000;
        component->current.completion_age_ms = 300.0;
        component->current.remaining_ms = 100.0;
        component->current.state = KIRIN_TIME_CURRENT_LIVE;
        component->current.clock = 1;
        component->current.finite_mask = 63;
        const double values[] { -17.0, -15.0, -4.0, 10.1, 12.0, 0.75 };
        std::copy (std::begin (values), std::end (values), component->current.values);
        component->history_count = 2;
    }
    result.main.current.target = KIRIN_TIME_POST;
    result.psr.current.target = KIRIN_TIME_DELTA;
    result.psr.current.cutoff = 470'400;
    result.psr.current.values[3] = -3.3;
    result.psr.current.values[4] = 99.0; // PSR's source must never supply the main PLR.
    result.psr.binding_revision = result.binding_revision;
    result.psr.pre_span = { 81, 82, 8, 83, 48'000, 2, {} };
    result.psr.pre_run = 9;
    result.psr.locator_identity = 12;
    result.psr.owner_identity = 13;
    result.psr.claim_identity = 14;
    return result;
}

KirinObservatoryFrame applyDisplayFacts (observatory::View& view, const KirinTimeSnapshotV2& data)
{
    KirinObservatoryFrame frame {};
    frame.version = KIRIN_OBSERVATORY_FRAME_VERSION;
    frame.signal_state = data.signal_state;
    frame.meter.generation = data.post_span.generation;
    frame.meter.measurement_epoch = data.post_span.epoch;
    frame.meter.active_frames = frame.meter.observed_frames = data.local_cutoff;
    frame.meter.sample_rate = data.post_span.sample_rate;
    frame.meter.channels = data.post_span.channels;
    frame.meter.state = KIRIN_METER_SESSION_ACTIVE;
    frame.meter.lufs_m = data.main.current.values[0];
    frame.meter.lufs_s = data.main.current.values[1];
    frame.meter.true_peak = frame.meter.max_true_peak = data.main.current.values[2];
    frame.meter.plr = data.main.current.values[4];
    view.setObservatoryFrame (frame, true);
    return frame;
}

std::vector<KirinTimeHistoryEntryV2> history (std::uint64_t end, double psr = 10.1)
{
    std::vector<KirinTimeHistoryEntryV2> result (2);
    for (unsigned index = 0; index < 2u; ++index)
    {
        auto& entry = result[index];
        entry.epoch = 41;
        entry.generation = 3;
        entry.run = 5;
        entry.segment = 6;
        entry.first_observed = entry.last_observed = end - (1u - index) * 4'800;
        entry.first_endpoint = entry.last_endpoint = static_cast<std::int64_t> (entry.last_observed);
        entry.total_count = 1;
        entry.clock = 1;
        entry.connects_previous = index != 0u;
        const double values[] { -17.0, -15.0, -4.0, 0.75, psr };
        for (unsigned metric = 0; metric < 5u; ++metric)
        {
            entry.ranges[metric] = { values[metric], values[metric], values[metric] };
            entry.valid_count[metric] = 1;
        }
    }
    return result;
}

std::vector<KirinTimeHistoryEntryV2> paintHistory (std::uint64_t end, bool comparison)
{
    auto point = history (end).back();
    std::vector<KirinTimeHistoryEntryV2> result (80, point);
    for (size_t index = 0; index < result.size(); ++index)
    {
        auto& entry = result[index];
        entry.first_observed = entry.last_observed = end - (79u - index) * 4'800;
        entry.first_endpoint = entry.last_endpoint = static_cast<int64_t> (entry.last_observed);
        entry.run = entry.segment = index < 40 ? 5 : 6;
        entry.connects_previous = index > 0 && index != 40;
        const double wave = std::sin (static_cast<double> (index) * 0.19);
        const double values[] { -17.0 + wave * 2, -15.0 + wave, -4.0 + wave * 0.5,
                                0.75, comparison ? -3.3 + wave * 0.8 : 10.1 + wave };
        for (unsigned metric = 0; metric < 5; ++metric)
            entry.ranges[metric] = { values[metric], values[metric], values[metric] };
        if (index == 37) entry.valid_count[4] = 0; // One measured PSR gap remains disconnected.
    }
    return result;
}

bool hasText (const text_style::ShownTextLog& log, const juce::String& text)
{
    return log.texts().contains (text);
}

void verifyRawCurrentAndLifetime()
{
    auto data = packet();
    Presentation accepted;
    require (accepted.apply (data, history (480'000), history (470'400, 23.0),
                             1'000.0, 1'080.0, true), "accept coherent independent components");
    require (accepted.main().value (Metric::plr) == 12.0
             && accepted.psr().value (Metric::plr) == 99.0,
             "keep main and PSR scalar sources independent");
    require (time_snapshot::currentText (accepted.psr(), Metric::psr, true)
                 == juce::String::fromUTF8 ("Δ −3.3"), "format the raw comparison, not history mean 23");
    auto solo = accepted.psr();
    solo.facts.current.values[3] = 10.1;
    for (const auto target : { KIRIN_TIME_PRE, KIRIN_TIME_POST })
    {
        solo.facts.current.target = static_cast<uint8_t> (target);
        require (time_snapshot::currentText (solo, Metric::psr, true)
            == (target == KIRIN_TIME_PRE ? "PRE 10.1" : "POST 10.1"),
            "absolute PRE and POST scopes remain literal");
    }
    solo.facts.current.target = KIRIN_TIME_DELTA;
    solo.facts.current.values[3] = -0.04;
    require (time_snapshot::currentText (solo, Metric::psr, true)
        == juce::String::fromUTF8 ("Δ +0.0"), "rounded delta zero has no negative-zero sign");
    solo.facts.current.target = KIRIN_TIME_POST;
    require (time_snapshot::currentText (solo, Metric::psr, true) == "POST 0.0",
             "rounded absolute zero has no negative-zero sign");
    require (accepted.psr().deadlineMs == 1'100.0
             && accepted.psr().facts.current.remaining_ms == 20.0,
             "anchor remaining time at poll start, excluding 80 ms acquisition overhead");
    auto lateProof = data;
    ++lateProof.psr.claim_identity;
    require (accepted.apply (lateProof, history (480'000), history (470'400), 1'020, 1'088, true)
             && accepted.psr().deadlineMs == 1'100.0,
             "new PRE evidence cannot extend the same local completion deadline");
    require (! accepted.advance (1'099.0) && accepted.psr().currentAvailable (Metric::psr),
             "strictly before the original deadline remains live even without another poll");
    data.psr.current.remaining_ms = 90.0;
    require (accepted.apply (data, history (480'000), history (470'400), 1'050.0, 1'099.0, true),
             "accept a repeated same-cutoff publication");
    require (accepted.psr().deadlineMs == 1'100.0 && accepted.advance (1'100.0)
             && ! accepted.psr().currentAvailable (Metric::psr)
             && ! accepted.psr().history.empty(),
             "re-poll cannot extend the lifetime; expiry retires current but preserves history");

    data = packet();
    data.psr.current.finite_mask &= ~(1u << 3);
    data.psr.current.values[3] = std::numeric_limits<double>::quiet_NaN();
    require (accepted.apply (data, history (480'000), history (470'400, 23.0), 2'000, 2'000, true)
             && ! accepted.psr().currentAvailable (Metric::psr)
             && time_snapshot::currentText (accepted.psr(), Metric::psr, true)
                 == juce::String::fromUTF8 ("Δ ---"), "latest None never recovers a prior finite PSR");
}

void verifyAxisGapsAndRetirement()
{
    auto data = packet();
    Presentation accepted;
    require (accepted.apply (data, history (480'000), history (470'400), 1'000, 1'000, true),
             "axis fixture accepts");
    require (std::abs (accepted.normalizedX (470'400) - 0.98) < 1.0e-12,
             "PSR E ends before local C on the same fixed axis");
    data.local_cutoff = data.main.current.cutoff = 489'600;
    ++data.revision;
    require (accepted.apply (data, history (489'600), history (470'400), 1'010, 1'010, true)
             && std::abs (accepted.normalizedX (470'400) - 49.0 / 51.0) < 1.0e-12,
             "POST C advances while the independently delayed PSR E stays factual");
    data.revision = 2; // A content hash has no chronological order.
    data.local_cutoff = data.main.current.cutoff = 494'400;
    require (accepted.apply (data, history (494'400), history (470'400), 1'020, 1'020, true),
             "a decreasing content hash still accepts progressing local cutoff");
    data.revision = 1;
    data.psr.current.values[3] = -2.2;
    data.psr.current.remaining_ms = 100.0;
    require (accepted.apply (data, history (494'400), history (470'400), 1'030, 1'030, true)
             && std::abs (accepted.psr().value (Metric::psr) + 2.2) < 1.0e-12
             && std::abs (accepted.psr().deadlineMs - 1'100) < 1.0e-12,
             "new evidence at the same cutoff accepts changed hash without extending TTL");
    const auto revision = accepted.revision();
    auto older = data;
    older.local_cutoff = 480'000;
    require (! accepted.apply (older, history (480'000), history (470'400), 1'020, 1'020, true)
             && accepted.revision() == revision, "same-source axis never moves backward");
    auto points = history (470'400);
    require (time_snapshot::connects (points[0], points[1], 4), "factual continuous segment connects");
    points[1].connects_previous = 0;
    require (! time_snapshot::connects (points[0], points[1], 4), "a declared gap breaks the PSR trace");
    points[1].connects_previous = 1;
    ++points[1].segment;
    require (! time_snapshot::connects (points[0], points[1], 4), "different segments never bridge");
    --points[1].segment;
    ++points[1].run;
    require (! time_snapshot::connects (points[0], points[1], 4), "seek/run changes never bridge");
    --points[1].run;
    points[1].valid_count[4] = 0;
    require (! time_snapshot::connects (points[0], points[1], 4), "missing PSR cannot become zero");
    accepted.retire (false);
    accepted.advance (1'050);
    require (accepted.main().currentAvailable (Metric::shortTerm)
             && ! accepted.main().history.empty() && ! accepted.psr().currentAvailable (Metric::psr)
             && accepted.psr().history.empty(), "pair retirement keeps local POST across a busy poll");
    accepted.retire (true);
    accepted.advance (1'051);
    require (! accepted.main().currentAvailable (Metric::shortTerm) && accepted.main().history.empty(),
             "local source retirement clears both components without another acquisition");

    data = packet();
    data.psr = {};
    Presentation compact;
    require (compact.apply (data, history (480'000), {}, 1'000, 1'000, false)
             && ! compact.psrVisible() && compact.psr().history.capacity() == 0u,
             "100 percent omits every PSR history allocation and clone");
    data.main.history_count = 3;
    require (! compact.apply (data, history (480'000), {}, 1'000, 1'000, false),
             "an inconsistent count cannot partially replace the accepted packet");
}

void verifyViewComparisonRetirement()
{
    observatory::View view (observatory::Role::post);
    view.setSize (375, 250);
    view.setDomain (observatory::Domain::time);
    const auto data = packet();
    auto frame = applyDisplayFacts (view, data);
    frame.comparison_identity = 17;
    frame.comparison_generation = 3;
    view.setObservatoryFrame (frame, true);
    require (view.setTimeSnapshot (data, history (480'000), history (470'400), 1'000, 1'080, true),
             "adopt actual POST main and independent delta PSR before a comparison transient");
    const auto revision = view.acceptedTimePresentation().revision();
    ++frame.comparison_generation;
    view.setObservatoryFrame (frame, true);
    view.advanceTimePresentation (1'099);
    const auto& retained = view.acceptedTimePresentation();
    require (retained.revision() == revision && retained.psr().history.size() == 2u
             && retained.psr().currentAvailable (Metric::psr)
             && retained.psr().deadlineMs == 1'100.0 && retained.main().history.size() == 2u,
             "same comparison identity retains PSR history and original deadline through generation transient");
    ++frame.comparison_identity;
    view.setObservatoryFrame (frame, true);
    require (retained.psr().history.empty() && ! retained.psr().currentAvailable (Metric::psr)
             && retained.main().history.size() == 2u
             && retained.main().currentAvailable (Metric::shortTerm),
             "changed comparison identity retires only delta PSR and keeps absolute POST main");
}

void verifyFailureReasons()
{
    struct Case { uint8_t reason; const char* english; const char* japanese; };
    const Case cases[] {
        { KIRIN_TIME_REASON_WAITING, "Waiting for corresponding PRE observation", u8"同時刻のPRE観測を待機中" },
        { KIRIN_TIME_REASON_STOPPED, "PRE observation stopped; history held", u8"PRE観測が停止・履歴を保持" },
        { KIRIN_TIME_REASON_INCOMPATIBLE, "PRE and POST observations are incompatible", u8"PREとPOSTの観測は比較できません" },
        { KIRIN_TIME_REASON_MISSING, "PRE observation unavailable", u8"PRE観測を取得できません" },
    };
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        for (const auto& value : cases)
        {
            auto data = packet();
            data.psr.current.state = KIRIN_TIME_CURRENT_WAITING;
            data.psr.current.finite_mask = 0;
            data.psr.reason = value.reason;
            observatory::View view (observatory::Role::post);
            view.setSize (450, 300);
            view.setDomain (observatory::Domain::time);
            applyDisplayFacts (view, data);
            require (view.setTimeSnapshot (data, history (480'000), history (470'400), 1'000, 1'000, true),
                     "adopt comparison reason without replacing main POST");
            require (time_snapshot::currentReason (view.acceptedTimePresentation().psr()) == value.english,
                     "distinct PRE waiting, stopped, incompatible and missing facts");
            juce::Image image (juce::Image::ARGB, 450, 300, true);
            juce::Graphics graphics (image);
            text_style::ShownTextLog log;
            view.paintEntireComponent (graphics, true);
            const auto expected = juce::String::fromUTF8 (
                language == i18n::Language::english ? value.english : value.japanese);
            require (hasText (log, expected), "actual reason remains distinct in JA and EN output");
        }
    }
}

void verifyNativePaintAndCapture()
{
    const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_G2_TIME_ARTIFACTS", {});
    const auto previousLanguage = i18n::current();
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        for (const auto& preset : observatory::sizePresets)
        {
            observatory::View view (observatory::Role::post);
            view.setSize (preset.width, preset.height);
            view.setDomain (observatory::Domain::time);
            auto data = packet();
            const bool showPsr = preset.density != observatory::Density::compact;
            data.main.current.run = data.psr.current.run = 6;
            data.main.history_count = data.psr.history_count = 80;
            if (! showPsr) data.psr = {};
            applyDisplayFacts (view, data);
            require (view.setTimeSnapshot (data, paintHistory (480'000, false),
                       showPsr ? paintHistory (470'400, true) : std::vector<KirinTimeHistoryEntryV2> {},
                       1'000, 1'000, showPsr), "apply packet to the actual native View");
            juce::Image image (juce::Image::ARGB, preset.width, preset.height, true);
            text_style::ShownTextLog log;
            juce::Graphics graphics (image);
            view.paintEntireComponent (graphics, true);
            require (hasText (log, "PLR 12.0 dB") || ! showPsr,
                     "PLR follows main POST, never independent PSR's PLR 99");
            require (hasText (log, juce::String::fromUTF8 ("PSR Δ −3.3 dB")) == showPsr,
                     "PSR is visible from 125 percent and absent at 100 percent");
            const auto context = presentation::forEditor (preset.width, preset.height);
            const auto actual = view.timeHistoryBounds();
            require (! actual.isEmpty(), "native View reports its actual painted TIME bounds");
            const auto layout = time_snapshot::geometry (actual, context, showPsr);
            require (actual.contains (layout.mainReadout) && actual.contains (layout.mainAxis)
                && ! layout.mainReadout.intersects (layout.mainFacts),
                "main readout, facts and axis fit the actual View area without overlap");
            if (showPsr)
            {
                std::cout << "TIME actual " << preset.width << "x" << preset.height
                    << ": area=" << actual.toString() << " main=" << layout.mainPlot.getHeight()
                    << " psr=" << layout.psrPlot.getHeight() << " px\n";
                require (layout.mainPlot.getHeight() >= 24 && layout.psrPlot.getHeight() >= 24
                    && actual.toFloat().contains (layout.mainPlot) && actual.toFloat().contains (layout.psrPlot),
                         "both actual 125+ percent trace plots keep at least 24 logical pixels");
                require (actual.contains (layout.psrReadout) && actual.contains (layout.psrStatus)
                    && ! layout.psrReadout.intersects (layout.psrStatus)
                    && ! layout.psrReadout.toFloat().intersects (layout.psrPlot)
                    && ! layout.psrStatus.toFloat().intersects (layout.psrPlot)
                    && ! layout.mainAxis.toFloat().intersects (layout.psrPlot),
                    "PSR number, status, trace and common axis have separate real areas");
                const auto font = monoFont (context, time_snapshot::psrReadoutRole,
                                            typography::Composition::visualization);
                const auto mainFont = monoFont (context, typography::TextRole::legend,
                                                typography::Composition::visualization);
                require (font.getHeight() <= mainFont.getHeight() && ! font.isBold()
                    && layout.psrReadout.getHeight() <= layout.mainReadout.getHeight(),
                    "PSR remains supplementary at M/S/TP legend height and normal weight");
                require (text_style::shownWidth (font, juce::String::fromUTF8 ("PSR Δ −3.3 dB"))
                    <= layout.psrReadout.getWidth(), "PSR number fits at its fixed font in the actual row");
            }
            const auto help = view.metricHelpAt (layout.psrHelp.getCentre());
            if (! layout.mainFacts.isEmpty())
            {
                const auto scope = text_style::shownText (view.metricHelpAt (layout.mainFacts.getCentre()));
                require (language == i18n::Language::english
                    ? scope.contains ("processed Session prefix") && scope.contains ("completed 100 ms point")
                        && scope.contains ("LEVEL: latest Session") && scope.contains ("PSR is independent")
                    : scope.contains (juce::String::fromUTF8 ("完了した100 ms"))
                        && scope.contains (juce::String::fromUTF8 ("処理済みSession範囲"))
                        && scope.contains (juce::String::fromUTF8 ("LEVELは最新。PSRは独立")),
                    "localized concise help preserves completed-point prefix and independent Session/PSR scopes");
            }
            if (showPsr) require (language == i18n::Language::english
                ? help.contains ("400 ms sample peak minus 3 s loudness") && help.contains ("independently compares")
                : help.contains (juce::String::fromUTF8 ("400 msサンプルピーク"))
                    && help.contains (juce::String::fromUTF8 ("全体表示とは独立")),
                "formula and independent scope live in localized help");
            require (! hasText (log, "PSR = 400 ms sample peak minus 3 s loudness"),
                     "formula cannot consume the primary value row");
            if (output.isNotEmpty())
            {
                auto dir = juce::File (output);
                require (dir.createDirectory().wasOk(), "create isolated native image output");
                const auto name = "time-" + juce::String (preset.width) + "-"
                    + (language == i18n::Language::japanese ? "ja" : "en") + ".png";
                auto stream = dir.getChildFile (name).createOutputStream();
                require (stream != nullptr && stream->setPosition (0) && stream->truncate().wasOk()
                         && juce::PNGImageFormat().writeImageToStream (image, *stream),
                         "save five sizes in both languages from the native painter");
            }
            view.advanceTimePresentation (1'100);
            const auto stamp = view.capturePresentationStamp();
            const auto frozen = view.createCaptureImage (1'200, 630, false, "frozen", "test");
            require (frozen.isValid() && stamp.requiresTypedMetadata,
                     "freeze the expired accepted presentation and flag v1 meaning loss");
            require (! view.acceptedTimePresentation().psr().currentAvailable (Metric::psr),
                     "Capture cannot revive current after expiry");
        }
    }
    require (i18n::current() == previousLanguage, "restore test language");
}
}

void applyTimeSnapshotFixture (observatory::View& view,
                                const std::vector<KirinMeterHistoryEntry>& legacy, bool delta)
{
    auto data = packet();
    data.local_cutoff = legacy.empty() ? 0 : legacy.back().last_observed_frames;
    if (! legacy.empty()) data.post_span.generation = legacy.back().generation;
    data.main.current.span = data.psr.current.span = data.post_span;
    data.main.current.cutoff = data.psr.current.cutoff = data.local_cutoff;
    data.main.current.target = data.psr.current.target = delta ? KIRIN_TIME_DELTA : KIRIN_TIME_POST;
    std::vector<KirinTimeHistoryEntryV2> entries;
    for (const auto& source : legacy)
    {
        KirinTimeHistoryEntryV2 entry {};
        entry.epoch = 41;
        entry.generation = source.generation;
        entry.run = entry.segment = source.run_id;
        entry.first_observed = source.first_observed_frames;
        entry.last_observed = source.last_observed_frames;
        entry.first_endpoint = source.first_timeline_endpoint_samples;
        entry.last_endpoint = source.last_timeline_endpoint_samples;
        entry.clock = 1;
        entry.total_count = source.observation_count;
        entry.connects_previous = ! entries.empty() && entries.back().run == entry.run;
        const KirinMeterHistoryRange ranges[] { source.lufs_m, source.lufs_s, source.true_peak,
                                        source.correlation, source.psr };
        for (unsigned i = 0; i < 5; ++i)
        {
            entry.ranges[i] = { ranges[i].min, ranges[i].max, ranges[i].mean };
            entry.valid_count[i] = std::isfinite (ranges[i].mean) ? entry.total_count : 0;
        }
        entries.push_back (entry);
    }
    const bool showPsr = presentation::forEditor (view.getWidth(), view.getHeight()).density != observatory::Density::compact;
    data.main.history_count = static_cast<uint32_t> (entries.size());
    if (showPsr) data.psr.history_count = data.main.history_count;
    else data.psr = {};
    applyDisplayFacts (view, data);
    require (view.setTimeSnapshot (data, entries,
        showPsr ? entries : std::vector<KirinTimeHistoryEntryV2> {}, 1'000, 1'000, showPsr),
        "legacy rendering input becomes an explicit V2 fixture");
}

void verifyTimeSnapshotContract()
{
    verifySessionCoverageContract();
    verifyFailureReasons();
    verifyRawCurrentAndLifetime();
    verifyTimeComparisonLifetimeContract (packet(), history (480'000), history (470'400));
    verifyAxisGapsAndRetirement();
    verifyViewComparisonRetirement();
    verifyTimeSnapshotMainTargetContract (packet(), history (480'000), history (470'400));
    verifyTimeSnapshotInputContract (packet(), history (480'000), history (470'400));
    verifyNativePaintAndCapture();
    std::cout << "TIME V2 packet, original deadline, independent PSR, gaps and frozen Capture: PASS\n";
}
}
