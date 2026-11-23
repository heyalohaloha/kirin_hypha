#pragma once
#include "../src/HyphaLanguage.h"
#include "../src/HyphaMetricReferenceZero.h"
#include "../src/HyphaReferenceRuntimeView.h"

// A metric face and the REFERENCE page read one set of facts: a source the page calls unusable is never
// PREPARING on the instrument, and one that is still being prepared never becomes UNAVAILABLE.
namespace hypha::tests::metric_reference_state_contract
{
namespace ra = reference_audition;
using metric_needle::Status;
using Check = void (*) (bool, const char*);

inline std::shared_ptr<ra::Snapshot> role (ra::RuntimeState state, const juce::String& code)
{
    auto value = std::make_shared<ra::Snapshot>();
    value->state = state; value->rejectionCode = code;
    value->candidateId = "candidate"; value->candidateName = "Check Song";
    return value;
}

inline ra::Snapshot comparison (std::shared_ptr<const ra::Snapshot> check)
{
    ra::Snapshot runtime;
    runtime.libraryReceived = true; runtime.comparisonSlot = 2;
    runtime.checkSelection = std::move (check);
    return runtime;
}

inline void expectZero (Check check, const ra::Snapshot& runtime, Status status, const char* reason)
{
    const auto scope = juce::String::repeatedString ("a", 64), owner = juce::String::repeatedString ("c", 32);
    for (std::size_t i = 0; i < 3; ++i)
    {
        auto zero = metric_needle::referenceZero (runtime, true, 48000.0, 2, i);
        check (zero.status == status && zero.reason == reason && zero.song == "Check Song", "POST publishes the state and its reason");
        check (std::isnan (zero.median), "no zero is invented");
        // The PRE of the chain reads the same state.
        zero.generation = 1;
        const auto text = metric_needle::SharedMetricZero::encoded (scope, owner, zero, 1700000000000LL);
        const auto read = metric_needle::SharedMetricZero::decoded (scope, zero.face, 48000, 2, text, 1700000000000LL);
        check (read.status == status && read.reason == reason && read.song == "Check Song", "PRE reads the same state and reason");
    }
}

inline void run (Check check)
{
    using State = ra::RuntimeState;
    using Step = reference_ui::SourceStep;
    using Pages = ra::AuditionPageState;
    const auto agrees = [check] (const ra::Snapshot& runtime, const ra::Snapshot& value) {
        const auto step = reference_ui::runtime_view::slotStep (value, true);
        const auto told = metric_needle::unmeasured (runtime, value);
        const bool position = step == Step::outsideCue || step == Step::outsideVersion || step == Step::pastSongEnd || step == Step::beforeSongStart;
        check ((told.status == Status::unavailable) == (reference_ui::kindOf (step) == reference_ui::StatusKind::unable && ! position),
               "the instrument and the REFERENCE page agree on whether the source can be used");
        check (told.status != Status::ready, "no zero without a measurement");
        check (told.status == Status::unavailable ? juce::String (metric_needle::reasonText (told.reason)) != "UNAVAILABLE" : told.reason == "preparing",
               "every state has public words");
        const i18n::ScopedLanguage japanese (i18n::Language::japanese);
        check (i18n::tr (metric_needle::reasonText (told.reason)) != metric_needle::reasonText (told.reason), "the words are told in Japanese");
    };
    for (const auto* code : { "", "reference_source_unavailable", "reference_cues_empty", "reference_selection_unavailable",
                              "reference_source_revalidating", "reference_source_read_failed", "reference_source_cache_full",
                              "reference_source_copy_failed" })
    { const auto value = role (State::waiting, code); agrees (comparison (value), *value); }
    for (const auto* code : { "source_changed", "reference_source_changed", "reference_source_audio_mismatch", "source_open_failed",
                              "source_decode_failed", "reference_source_open_failed", "reference_source_decode_failed",
                              "channel_layout_unsupported", "reference_source_receipt_rejected", "reference_source_contract_rejected",
                              "reference_preset_rejected", "reference_source_cache_full", "reference_source_copy_failed", "reference_other" })
    { const auto value = role (State::rejected, code); agrees (comparison (value), *value); }
    { const auto value = role (State::verifying, ""); agrees (comparison (value), *value); }
    for (const auto pages : { Pages::positionUnknown, Pages::mappingUnstable, Pages::waiting, Pages::ready, Pages::outsideSource, Pages::readFailed, Pages::sourceUnavailable })
        for (const auto* failure : { "", "reference_source_revalidating", "reference_source_changed", "reference_source_cache_full", "reference_source_open_failed" })
        {
            const auto value = role (State::ready, "");
            value->auditionPages = pages; value->sourceFailureReason = failure;
            agrees (comparison (value), *value);
        }
    // A song Kirin OS has not prepared: PREPARING only while Kirin OS says that it prepares it.
    auto runtime = comparison (role (State::waiting, "reference_source_unavailable"));
    expectZero (check, runtime, Status::unavailable, "source_unavailable");
    auto preparation = std::make_shared<ra::RuntimeLibraryPreparation>();
    preparation->songs["candidate"].state = "pending"; preparation->songs["candidate"].step = "measuring";
    runtime.libraryPreparation = preparation;
    expectZero (check, runtime, Status::measuring, "preparing");
    auto failed = std::make_shared<ra::RuntimeLibraryPreparation> (*preparation);
    failed->songs["candidate"].reason = "source_unavailable"; runtime.libraryPreparation = failed;
    expectZero (check, runtime, Status::unavailable, "source_unavailable");
    failed = std::make_shared<ra::RuntimeLibraryPreparation> (*preparation);
    failed->songs["candidate"].reason = "analysis_failed"; runtime.libraryPreparation = failed;
    expectZero (check, runtime, Status::unavailable, "analysis_failed");
    expectZero (check, comparison (role (State::verifying, "")), Status::measuring, "preparing");
    expectZero (check, comparison (role (State::rejected, "reference_source_changed")), Status::unavailable, "source_changed");
    // The B song follows the same rule, and an ordinary measurement is unchanged.
    ra::Snapshot song;
    song.libraryReceived = true; song.comparisonSlot = 3; song.selectedSongId = "set/candidate";
    song.referenceSelection = role (State::waiting, "reference_source_unavailable");
    expectZero (check, song, Status::unavailable, "source_unavailable");
}
}
