#include "SessionCoverageContractTest.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaTextStyle.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>

namespace hypha::tests
{
namespace
{
void require (bool ok, const char* message)
{
    if (ok) return;
    std::cerr << "Session coverage contract failed: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

bool samePixels (const juce::Image& a, const juce::Image& b)
{
    if (a.getBounds() != b.getBounds()) return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y)) return false;
    return true;
}

void verifyBoundCell (const observatory::View& view)
{
    const auto body = view.bodyBounds();
    juce::Point<int> inside (-1, -1);
    const auto isMaximum = [&view] (juce::Point<int> at)
    { return view.metricHelpAt (at).contains ("Highest true peak (MAX TP)"); };
    for (int y = body.getY(); y < body.getBottom() && inside.x < 0; y += 4)
        for (int x = body.getX(); x < body.getRight(); x += 4)
            if (isMaximum ({ x, y })) { inside = { x, y }; break; }
    require (inside.x >= 0, "actual MAX TP cell has its pending-scope help hit region");
    int left = inside.x, right = inside.x, top = inside.y, bottom = inside.y;
    while (left > body.getX() && isMaximum ({ left - 1, inside.y })) --left;
    while (right + 1 < body.getRight() && isMaximum ({ right + 1, inside.y })) ++right;
    while (top > body.getY() && isMaximum ({ inside.x, top - 1 })) --top;
    while (bottom + 1 < body.getBottom() && isMaximum ({ inside.x, bottom + 1 })) ++bottom;
    juce::Rectangle<int> numberArea (left, top, right - left + 1, bottom - top + 1);
    const bool compact = view.experienceFamily() == observatory::ExperienceFamily::compactMeter;
    numberArea.removeFromTop (compact ? juce::jmax (14, numberArea.getHeight() / 4)
        : juce::jlimit (14, 18, numberArea.getHeight() / 4));
    if (compact && numberArea.getWidth() >= 180)
        numberArea.removeFromRight (juce::jmin (46, numberArea.getWidth() / 3));
    numberArea.reduce (compact ? 5 : 4, 0);
    const auto font = monoFont (presentation::forEditor (view.getWidth(), view.getHeight()),
        compact ? typography::TextRole::primaryValue : typography::TextRole::secondaryValue,
        typography::Composition::facts);
    require (tabularTextWidth (font, view.sessionMaximumBoundText()) <= numberArea.getWidth()
             && font.getHeight() <= numberArea.getHeight(),
             "pending MAX TP bound fits its actual numeric cell at the fixed font in JA and EN");
}

void verifyHeldCoverage (KirinObservatoryFrame frame)
{
    for (bool pending : { false, true })
    {
        frame.meter.active_frames = frame.meter.observed_frames = 2'880'000 + (pending ? 479u : 0u);
        frame.meter.max_true_peak = -1.2;
        KirinMeterSessionV2 coverage { 2, sizeof (coverage), frame.meter, 2'880'000,
            pending ? 479u : 0u, static_cast<uint8_t> (pending ? KIRIN_SESSION_SUMMARY_PENDING_TAIL
                : KIRIN_SESSION_SUMMARY_COMPLETE), {} };
        std::vector<KirinMeterHistoryEntry> history (600);
        for (std::size_t at = 0; at < history.size(); ++at)
        {
            auto& point = history[at];
            point.measurement_epoch = frame.meter.measurement_epoch;
            point.generation = frame.meter.generation;
            point.run_id = 1;
            point.first_observed_frames = point.last_observed_frames = (at + 1) * 4'800;
            point.observation_count = 1;
            point.resolution = KIRIN_METER_HISTORY_10_HZ;
            point.lufs_m.mean = -18.0;
            point.true_peak.max = -4.0;
        }
        observatory::View view (observatory::Role::post);
        view.setSize (900, 600);
        view.setObservatoryFrame (frame, true);
        view.setHistory (history);
        view.setSessionCoverage (coverage);
        juce::Image screen (juce::Image::ARGB, 900, 600, true);
        juce::Graphics graphics (screen);
        view.paintEntireComponent (graphics, true);
        const auto* live = view.findChildWithID ("history-live");
        require (live != nullptr && live->isVisible(), "actual LEVEL inspection controls are available");
        const juce::Point<float> point (200.0f, static_cast<float> (live->getBottom() + 60));
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(),
            point, juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0,
            &view, &view, now, point, now, 1, false);
        view.mouseDown (click);
        require (view.historyHeldForTest(), "real plot click freezes the adopted Session coverage");
        const auto capture = [&view]
        { return view.createCaptureImage (1200, 630, false, "2026-10-08 12:00:00", "G2 COVERAGE"); };
        const auto before = capture();
        const auto stamp = view.capturePresentationStamp();
        require (stamp.requiresTypedMetadata == pending, "held stamp reflects its adopted prefix scope");
        auto newer = frame;
        newer.meter.observed_frames += 4'800;
        newer.meter.active_frames += 4'800;
        newer.meter.max_true_peak = 0.4;
        auto complete = coverage;
        complete.session = newer.meter;
        complete.processed_frames = newer.meter.active_frames;
        complete.pending_frames = 0;
        complete.summary_status = KIRIN_SESSION_SUMMARY_COMPLETE;
        view.setObservatoryFrame (newer, true);
        view.setSessionCoverage (complete);
        require (std::abs (view.cumulativeMeterForDisplay().max_true_peak - 0.4) < 1.0e-12,
                 "canary publishes a distinct newer same-generation complete Session");
        const auto after = view.capturePresentationStamp();
        require (after.snapshotRevision == stamp.snapshotRevision
                 && after.presentationRevision == stamp.presentationRevision
                 && after.requiresTypedMetadata == pending && samePixels (before, capture()),
                 "held Capture retains exact payload and complete/pending scope after later completion");
        auto* resume = dynamic_cast<juce::Button*> (view.findChildWithID ("history-live"));
        require (resume != nullptr, "actual LIVE action is available");
        resume->onClick();
        require (! view.historyHeldForTest() && ! view.capturePresentationStamp().requiresTypedMetadata
                 && ! samePixels (before, capture()),
                 "resuming adopts the newer complete Session and proves the pixel oracle is sensitive");
    }
}
void verifyPlayingAndTimeCapture (KirinObservatoryFrame frame)
{
    using Page = analysis_navigation::Page;
    for (const auto role : { observatory::Role::pre, observatory::Role::post })
    {
        observatory::View view (role);
        view.setSize (900, 600);
        frame.signal_state = KIRIN_SIGNAL_STATE_ACTIVE;
        frame.meter.state = KIRIN_METER_SESSION_ACTIVE;
        view.setObservatoryFrame (frame, true);
        KirinMeterSessionV2 coverage { 2, sizeof (coverage), frame.meter, 2'880'000, 479,
                                      KIRIN_SESSION_SUMMARY_COMPLETE, {} };
        view.setSessionCoverage (coverage);
        require (! view.sessionSummaryPending() && view.sessionMaximumBoundText().isEmpty()
                 && ! view.capturePresentationStamp().requiresTypedMetadata,
                 "ordinary sub-10 ms in-flight audio permits LEVEL Capture during playback");
        juce::Image image (juce::Image::ARGB, 900, 600, true);
        juce::Graphics graphics (image);
        text_style::ShownTextLog log;
        view.paintEntireComponent (graphics, true);
        require (log.texts().contains ("PLR"), "playing PLR card is present");
        auto changed = coverage;
        changed.session.plr = 26.2;
        view.setSessionCoverage (changed);
        juce::Image canary (juce::Image::ARGB, 900, 600, true);
        juce::Graphics canaryGraphics (canary);
        view.paintEntireComponent (canaryGraphics, true);
        require (! samePixels (image, canary),
                 "playing PLR renders its processed-prefix number rather than a fixed missing marker");
        coverage.session.state = KIRIN_METER_SESSION_PAUSED;
        coverage.summary_status = KIRIN_SESSION_SUMMARY_PENDING_TAIL;
        view.setSessionCoverage (coverage);
        require (view.sessionSummaryPending() && view.capturePresentationStamp().requiresTypedMetadata,
                 "actual stopped unprocessed tail still requires typed LEVEL metadata");
        view.setDomain (observatory::Domain::time);
        for (const auto page : { Page::meters, Page::run, Page::attack, Page::perceptual, Page::absolute })
        {
            view.setAnalysisPage (page);
            const bool typed = page == Page::meters || page == Page::attack;
            require (view.capturePresentationStamp().requiresTypedMetadata == typed,
                     "TIME requires typed Capture only for HISTORY/PSR and DRUM, even with a pending Session");
        }
    }
}
}
void verifySessionCoverageContract()
{
    KirinObservatoryFrame frame {};
    frame.version = KIRIN_OBSERVATORY_FRAME_VERSION;
    frame.signal_state = KIRIN_SIGNAL_STATE_INACTIVE;
    frame.meter.generation = 7;
    frame.meter.measurement_epoch = 17;
    frame.meter.active_frames = frame.meter.observed_frames = 2'880'479;
    frame.meter.sample_rate = 48'000;
    frame.meter.channels = 2;
    frame.meter.state = KIRIN_METER_SESSION_PAUSED;
    frame.meter.max_true_peak = -1.2;
    frame.meter.max_lufs_m = -11.0;
    frame.meter.lufs_i = -14.3;
    frame.meter.plr = 13.1;
    KirinMeterSessionV2 coverage { 2, sizeof (coverage), frame.meter, 2'880'000, 479,
                                 KIRIN_SESSION_SUMMARY_PENDING_TAIL, {} };
    verifyHeldCoverage (frame);
    verifyPlayingAndTimeCapture (frame);
    observatory::View rounding (observatory::Role::post);
    rounding.setObservatoryFrame (frame, true);
    const std::pair<double, const char*> endpoints[] {
        { 2.04, u8"≥ 2.0" }, { 2.06, u8"≥ 2.0" },
        { -2.04, u8"≥ −2.1" }, { -2.06, u8"≥ −2.1" },
        { 0.04, u8"≥ 0.0" }, { 0.06, u8"≥ 0.0" },
        { -0.04, u8"≥ −0.1" }, { -0.06, u8"≥ −0.1" },
    };
    for (const auto& endpoint : endpoints)
    {
        auto value = coverage;
        value.session.max_true_peak = endpoint.first;
        rounding.setSessionCoverage (value);
        require (rounding.sessionMaximumBoundText() == juce::String::fromUTF8 (endpoint.second),
                 "outward lower-bound rounding never asserts a stronger peak than measured");
    }
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        for (const auto& preset : observatory::sizePresets)
        {
            observatory::View view (observatory::Role::post);
            view.setSize (preset.width, preset.height);
            view.setObservatoryFrame (frame, true);
            view.setSessionCoverage (coverage);
            require (view.sessionSummaryPending() && view.capturePresentationStamp().requiresTypedMetadata,
                     "479 pending frames keep the Session prefix explicit and typed for Capture");
            juce::Image image (juce::Image::ARGB, preset.width, preset.height, true);
            juce::Graphics graphics (image);
            text_style::ShownTextLog log;
            view.paintEntireComponent (graphics, true);
            require (view.sessionMaximumBoundText() == juce::String::fromUTF8 ("≥ −1.2")
                     && log.texts().contains ("MAX TP"),
                     "all sizes render Max TP with an explicit confirmed lower bound");
            verifyBoundCell (view);
            const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_G2_TIME_ARTIFACTS", {});
            if (output.isNotEmpty())
            {
                auto dir = juce::File (output);
                require (dir.createDirectory().wasOk(), "create Session PNG directory");
                auto stream = dir.getChildFile ("session-pending-" + juce::String (preset.width)
                    + (language == i18n::Language::japanese ? "-ja.png" : "-en.png")).createOutputStream();
                require (stream != nullptr && stream->setPosition (0) && stream->truncate().wasOk()
                         && juce::PNGImageFormat().writeImageToStream (image, *stream),
                         "save the adopted pending Session in five sizes and both languages");
            }
            auto complete = coverage;
            complete.summary_status = KIRIN_SESSION_SUMMARY_COMPLETE;
            complete.processed_frames = frame.meter.active_frames;
            complete.pending_frames = 0;
            view.setSessionCoverage (complete);
            require (! view.sessionSummaryPending() && view.sessionMaximumBoundText().isEmpty()
                     && ! view.capturePresentationStamp().requiresTypedMetadata,
                     "coherent completed LEVEL remains representable by Capture v1");
            auto newer = complete;
            newer.session.active_frames = newer.session.observed_frames
                = newer.processed_frames = frame.meter.active_frames + 1;
            newer.session.max_true_peak = -0.8;
            view.setSessionCoverage (newer);
            require (! view.sessionSummaryPending(), "coherent V2 may advance beyond the legacy 100 ms frame");
            log.clear();
            view.paintEntireComponent (graphics, true);
            require (std::abs (view.cumulativeMeterForDisplay().max_true_peak + 0.8) < 1.0e-12
                     && view.sessionMaximumBoundText().isEmpty(),
                     "Max TP adopts the completed 10 ms Session tail instead of stale frame peak");
            --complete.session.active_frames;
            complete.session.observed_frames = complete.processed_frames = complete.session.active_frames;
            complete.session.max_true_peak = -12.0;
            complete.session.lufs_i = -30.0;
            view.setSessionCoverage (complete);
            require (view.sessionSummaryPending() && view.capturePresentationStamp().requiresTypedMetadata,
                     "same-generation active-frame mismatch cannot certify complete Session");
            require (std::abs (view.cumulativeMeterForDisplay().max_true_peak - frame.meter.max_true_peak) < 1.0e-12
                     && std::abs (view.cumulativeMeterForDisplay().lufs_i - frame.meter.lufs_i) < 1.0e-12,
                     "BUSY-retained older coverage cannot replace the newer observed cumulative prefix");
            auto onlyTail = frame;
            onlyTail.meter.active_frames = onlyTail.meter.observed_frames = 479;
            onlyTail.meter.max_true_peak = onlyTail.meter.plr = std::numeric_limits<double>::quiet_NaN();
            onlyTail.meter.lufs_i = onlyTail.meter.lra = onlyTail.meter.max_lufs_m
                = std::numeric_limits<double>::quiet_NaN();
            view.setObservatoryFrame (onlyTail, true);
            auto tailCoverage = coverage;
            tailCoverage.session = onlyTail.meter;
            tailCoverage.processed_frames = 0;
            view.setSessionCoverage (tailCoverage);
            require (view.sessionSummaryPending(), "479 input frames do not imply any EBU-processed audio");
            log.clear();
            view.paintEntireComponent (graphics, true);
            require (view.sessionMaximumBoundText().isEmpty()
                     && ! std::isfinite (view.cumulativeMeterForDisplay().max_true_peak),
                     "an unprocessed-only Session cannot retain the previous finite peak");
        }
    }
}
}
