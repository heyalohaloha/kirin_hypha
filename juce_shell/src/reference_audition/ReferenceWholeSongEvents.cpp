#include "ReferenceWholeSongEvents.h"
#include <cmath>

namespace hypha::reference_audition
{
    namespace
    {
        void rename (juce::DynamicObject& object, const char* before, const char* after)
        {
            object.setProperty (after, object.getProperty (before));
            object.removeProperty (before);
        }
    }

    juce::var wholeSongTrialStart (juce::var value, const RuntimeCandidate& candidate,
                                   const RuntimeV2BlindSnapshot& facts)
    {
        auto& start = *value.getDynamicObject();
        start.setProperty ("format", "kirin_reference_whole_song_trial_start");
        start.removeProperty ("work_id");
        start.removeProperty ("recording_id");
        start.setProperty ("relationship", "acoustically_matched_recording");
        auto sources = start.getProperty ("sources");
        auto& a = *sources.getProperty ("a", {}).getDynamicObject();
        a.setProperty ("source_kind", "live_daw");
        rename (a, "daw_revision_id", "observation_id");
        rename (a, "cue_pcm_sha256", "observation_pcm_sha256");
        auto& b = *sources.getProperty ("b", {}).getDynamicObject();
        b.setProperty ("work_id", candidate.sourceWorkId);
        b.setProperty ("recording_id", candidate.sourceRecordingId);
        rename (b, "cue_pcm_sha256", "observation_pcm_sha256");

        auto calibration = start.getProperty ("cue");
        auto& observation = *calibration.getDynamicObject();
        observation.removeProperty ("cue_id");
        observation.removeProperty ("loop_enabled");
        observation.setProperty ("correlation_ppm", static_cast<juce::int64> (
            std::llround (facts.alignmentCorrelation * 1000000.0)));
        observation.setProperty ("spread_samples", facts.alignmentSpreadSamples);
        start.removeProperty ("cue");
        start.setProperty ("calibration", calibration);
        auto range = new juce::DynamicObject();
        range->setProperty ("sample_rate_hz", facts.bSampleRateHz);
        range->setProperty ("start_sample", static_cast<juce::int64> (0));
        range->setProperty ("end_sample", facts.wholeSourceFrames);
        start.setProperty ("playback_range", juce::var (range));

        auto conditions = start.getProperty ("conditions");
        auto& playback = *conditions.getProperty ("playback", {}).getDynamicObject();
        playback.setProperty ("engine", "kirin_hypha_reference_whole_song_v1");
        playback.setProperty ("switch_policy", "fixed_5ms_linear_crossfade");
        auto& alignment = *conditions.getProperty ("alignment", {}).getDynamicObject();
        alignment.setProperty ("algorithm", "kirin_measured_rms_multichannel_correlation_v1");
        auto& gain = *conditions.getProperty ("gain_match", {}).getDynamicObject();
        rename (gain, "a_cue_true_peak_millidbtp", "a_observation_true_peak_millidbtp");
        rename (gain, "b_cue_true_peak_millidbtp", "b_source_true_peak_millidbtp");
        return value;
    }

    juce::var wholeSongTrialCompleted (juce::var value)
    {
        value.getDynamicObject()->setProperty ("format", "kirin_reference_whole_song_trial_completed");
        // The start is an embedded immutable journal event, not a fictional Work file.
        value.getProperty ("start_artifact", {}).getDynamicObject()->removeProperty ("relative_path");
        return value;
    }
}
