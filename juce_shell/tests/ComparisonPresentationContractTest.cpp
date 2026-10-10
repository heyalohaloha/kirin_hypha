#include "ComparisonPresentationContractTest.h"

#include "../src/HyphaComparisonPresentation.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaTextStyle.h"

#include <cstdlib>
#include <iostream>

namespace hypha::tests::comparison_presentation_contract
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Comparison presentation contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

std::uint64_t imageSignature (const juce::Image& image)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
        {
            hash ^= image.getPixelAt (x, y).getARGB();
            hash *= 1099511628211ull;
        }
    return hash;
}
}

#define KIRIN_COMPARISON_REQUIRE(expression) require ((expression), #expression, __LINE__)

void verify()
{
    KIRIN_COMPARISON_REQUIRE (
        comparison_presentation::statusText (
            KIRIN_COMPARISON_STATE_REJECTED,
            KIRIN_COMPARISON_REASON_LAYOUT_MISMATCH).contains ("MATCH PRE / POST BUS"));
    KIRIN_COMPARISON_REQUIRE (
        comparison_presentation::statusText (
            KIRIN_COMPARISON_STATE_PREPARING,
            KIRIN_COMPARISON_REASON_STALE).contains ("WAITING"));
    KIRIN_COMPARISON_REQUIRE (
        comparison_presentation::statusText (
            KIRIN_COMPARISON_STATE_HOLDING,
            KIRIN_COMPARISON_REASON_STALE).contains ("HOLDING"));
    KIRIN_COMPARISON_REQUIRE (
        comparison_presentation::notifiesExplicitAction (
            KIRIN_COMPARISON_STATE_REJECTED,
            KIRIN_COMPARISON_REASON_LAYOUT_UNKNOWN));
    KIRIN_COMPARISON_REQUIRE (
        ! comparison_presentation::notifiesExplicitAction (
            KIRIN_COMPARISON_STATE_HOLDING,
            KIRIN_COMPARISON_REASON_STALE));
    KIRIN_COMPARISON_REQUIRE (
        comparison_presentation::statusText (
            KIRIN_COMPARISON_STATE_REJECTED,
            KIRIN_COMPARISON_REASON_LOCAL_INACTIVE).contains ("COMPARISON UPDATE PENDING"));
    KIRIN_COMPARISON_REQUIRE (
        ! comparison_presentation::notifiesExplicitAction (
            KIRIN_COMPARISON_STATE_REJECTED,
            KIRIN_COMPARISON_REASON_LOCAL_INACTIVE));

    observatory::View post (observatory::Role::post);
    post.setSize (300, 200);
    const auto watchCapture = post.createCaptureImage (600, 400);
    KirinRecordDisplay record {};
    record.phase = KIRIN_RECORD_DISPLAY_RESULT_HOLD;
    record.has_measure = 1u;
    record.has_session = 1u;
    record.has_delta = 1u;
    record.pair_matches_current = 1u;
    record.generation = 42u;
    record.measure.lufs_m = -17.2;
    record.measure.lufs_s = -16.8;
    record.measure.psr = 9.4;
    record.measure.crest = 11.1;
    record.measure.sharpness = 1.3;
    record.session.max_true_peak = -0.8;
    record.session.lufs_i = -16.1;
    record.delta.mode = KIRIN_DELTA_MODE_ACTIVE;
    record.delta.lufs = 0.7;
    record.delta.lufs_s = 0.5;
    record.delta.psr = -0.4;
    record.delta.crest = 0.2;
    record.delta.sharpness = 0.1;
    post.setTarget (observatory::ObservationTarget::delta);
    post.setRecordDisplay (record, true);
    KIRIN_COMPARISON_REQUIRE (post.recordDisplayShowingForTest());
    const auto recordCapture = post.createCaptureImage (600, 400);
    KIRIN_COMPARISON_REQUIRE (imageSignature (recordCapture) != imageSignature (watchCapture));
    for (const auto phase : { KIRIN_RECORD_DISPLAY_FINALIZING,
                             KIRIN_RECORD_DISPLAY_RESULT_HOLD,
                             KIRIN_RECORD_DISPLAY_UNAVAILABLE })
    {
        record.phase = static_cast<std::uint8_t> (phase);
        post.setRecordDisplay (record, true);
        const auto notice = post.footerStatusForTest();
        KIRIN_COMPARISON_REQUIRE (notice.isNotEmpty() && notice != "WAITING");
        for (const auto domain : { observatory::Domain::time, observatory::Domain::frequency,
                                   observatory::Domain::space })
        {
            post.setDomain (domain);
            KIRIN_COMPARISON_REQUIRE (! post.recordDisplayShowingForTest());
            KIRIN_COMPARISON_REQUIRE (post.footerStatusForTest() == notice);
        }
        post.setDomain (observatory::Domain::level);
        KIRIN_COMPARISON_REQUIRE (post.recordDisplayShowingForTest());
    }
    post.setRecordDisplay ({}, false);
    KIRIN_COMPARISON_REQUIRE (! post.recordDisplayShowingForTest());
    KirinObservatoryFrame stopped {};
    stopped.version = KIRIN_OBSERVATORY_FRAME_VERSION;
    stopped.signal_state = KIRIN_SIGNAL_STATE_INACTIVE;
    stopped.meter.state = KIRIN_METER_SESSION_ACTIVE; // The Audio stop can precede Measure publication.
    stopped.meter.active_frames = stopped.meter.observed_frames = 192000;
    stopped.meter.sample_rate = 48000;
    stopped.meter.lufs_m = -18.2;
    stopped.meter.lufs_i = -18.4;
    stopped.meter.max_true_peak = -9.1;
    stopped.comparison_state = KIRIN_COMPARISON_STATE_REJECTED;
    stopped.comparison_reason = KIRIN_COMPARISON_REASON_LOCAL_INACTIVE;
    post.setTarget (observatory::ObservationTarget::absolute);
    post.setObservatoryFrame (stopped, true);
    KIRIN_COMPARISON_REQUIRE (post.footerStatusForTest() == "HOLD");
    stopped.signal_state = KIRIN_SIGNAL_STATE_BYPASSED;
    post.setObservatoryFrame (stopped, true);
    KIRIN_COMPARISON_REQUIRE (post.footerStatusForTest() == "BYPASSED");
    // A retained exact pair is stopped, never a request to select that PRE again.
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        for (const auto reason : { KIRIN_COMPARISON_REASON_LOCAL_INACTIVE,
                                   KIRIN_COMPARISON_REASON_PRE_INACTIVE })
        {
            stopped.signal_state = KIRIN_SIGNAL_STATE_INACTIVE;
            stopped.comparison_reason = static_cast<uint8_t> (reason);
            post.setTarget (observatory::ObservationTarget::delta);
            post.setObservatoryFrame (stopped, true);
            for (const auto domain : { observatory::Domain::level, observatory::Domain::time })
            {
                post.setDomain (domain);
                text_style::ShownTextLog log;
                const auto image = post.createCaptureImage (600, 400);
                KIRIN_COMPARISON_REQUIRE (image.isValid());
                const auto text = log.texts().joinIntoString ("\n");
                KIRIN_COMPARISON_REQUIRE (! text.contains ("NO MATCHING PRE"));
                KIRIN_COMPARISON_REQUIRE (! text.contains (juce::String::fromUTF8 ("対応PREなし")));
                if (domain == observatory::Domain::level)
                    KIRIN_COMPARISON_REQUIRE (log.texts().contains (text_style::shownText (
                        comparison_presentation::statusText (stopped.comparison_state,
                                                              stopped.comparison_reason))));
            }
        }
        stopped.comparison_reason = KIRIN_COMPARISON_REASON_NO_PAIR;
        post.setDomain (observatory::Domain::level);
        post.setObservatoryFrame (stopped, true);
        text_style::ShownTextLog unbound;
        post.createCaptureImage (600, 400);
        KIRIN_COMPARISON_REQUIRE (unbound.texts().contains (text_style::shownText (
            comparison_presentation::statusText (stopped.comparison_state, stopped.comparison_reason))));
    }
}
}
