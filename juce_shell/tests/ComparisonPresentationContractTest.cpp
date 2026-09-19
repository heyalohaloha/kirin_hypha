#include "ComparisonPresentationContractTest.h"

#include "../src/HyphaComparisonPresentation.h"
#include "../src/HyphaObservatoryView.h"

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
    post.setRecordDisplay ({}, false);
    KIRIN_COMPARISON_REQUIRE (! post.recordDisplayShowingForTest());
}
}
