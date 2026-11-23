#pragma once
#include "HyphaMetricNeedle.h"
#include "reference_audition/ReferenceAuditionController.h"

// A Reference role whose baseline has no measurement yet: still being prepared, or known not to be usable.
// It reads the same facts as the REFERENCE page's stages (HyphaReferenceRuntimeView.h), so the instrument
// never says PREPARING while that page says the source cannot be used.
namespace hypha::metric_needle
{
struct Unmeasured
{
    Status status = Status::measuring;
    std::string reason = "preparing";
};

// Why a rejected or failed source cannot be measured, in the fixed public words (reasonText); empty while
// Reference retries it by itself.
inline std::string unusableSourceReason (const juce::String& code)
{
    if (code == "reference_source_cache_full" || code == "reference_source_copy_failed" || code == "reference_source_revalidating")
        return {};
    if (code == "source_changed" || code == "reference_source_changed" || code == "reference_source_audio_mismatch")
        return "source_changed";
    if (code == "channel_layout_unsupported") return "unsupported_format";
    if (code == "source_open_failed" || code == "source_decode_failed" || code == "reference_source_open_failed"
        || code == "reference_source_decode_failed" || code == "reference_source_read_failed")
        return "read_failed";
    return "source_unavailable";
}

inline Unmeasured unmeasured (const reference_audition::Snapshot& runtime, const reference_audition::Snapshot& role)
{
    using Runtime = reference_audition::RuntimeState;
    using Pages = reference_audition::AuditionPageState;
    const auto known = [] (std::string reason) {
        return reason.empty() ? Unmeasured {} : Unmeasured { Status::unavailable, std::move (reason) };
    };
    const auto& code = role.rejectionCode;
    switch (role.state)
    {
        case Runtime::rejected: return known (unusableSourceReason (code));
        case Runtime::ready:
            if (role.auditionPages == Pages::readFailed) return known ("read_failed");
            if (role.auditionPages == Pages::sourceUnavailable && role.sourceFailureReason.isNotEmpty())
                return known (unusableSourceReason (role.sourceFailureReason));
            break;
        case Runtime::waiting:
            if (code == "reference_selection_unavailable" || code == "reference_version_unselected"
                || code == "reference_checks_empty" || code == "reference_candidates_empty")
                return known ("select_reference_post");
            if (code == "reference_cues_empty") return known ("source_unavailable");
            if (code == "reference_source_unavailable")
            {
                // Kirin OS prepares the song itself: that is a wait, until it reports that it cannot.
                const auto song = runtime.libraryPreparation != nullptr ? runtime.libraryPreparation->find (role.candidateId)
                                                                        : reference_audition::RuntimeSongPreparation {};
                if (song.reason == "analysis_failed") return known ("analysis_failed");
                if (song.reason.isEmpty() && song.state == "pending") break;
                return known ("source_unavailable");
            }
            break;
        case Runtime::verifying: case Runtime::disconnected: break;
    }
    return {};
}
}
