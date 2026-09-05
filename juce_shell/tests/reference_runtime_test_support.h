#pragma once

#include "../src/reference_audition/ReferenceAuditionLease.h"
#include "../src/reference_audition/ReferenceAuditionController.h"
#include "../src/reference_audition/ReferenceAuditionProtocol.h"
#include "../src/reference_audition/ReferenceAuditionRepository.h"
#include "../src/reference_audition/ReferenceRecoveryTransport.h"
#include "../src/reference_audition/ReferenceRuntimeEventTransport.h"
#include "../src/reference_audition/ReferenceRuntimeABinding.h"
#include "../src/reference_audition/ReferenceRuntimeACapture.h"
#include "../src/reference_audition/ReferenceRuntimeV2Alignment.h"
#include "../src/reference_audition/ReferenceRuntimeV2Blind.h"
#include "../src/reference_audition/ReferenceRuntimeV2Controller.h"
#include "../src/reference_audition/ReferenceRuntimeV2Measurement.h"
#include "../src/reference_audition/ReferenceRuntimeV2Repository.h"
#include "../src/reference_audition/ReferenceRuntimeV2Profile.h"
#include "../src/reference_audition/ReferenceRuntimeV2Presentation.h"
#include "../src/reference_audition/ReferenceRuntimeV2Source.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#include <juce_cryptography/juce_cryptography.h>
#include <juce_audio_formats/juce_audio_formats.h>

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <sys/stat.h>
#endif

namespace ref = hypha::reference_audition;

namespace
{
    const juce::String workId = "11111111-1111-4111-8111-111111111111";
    const juce::String preparationId = "33333333-3333-4333-8333-333333333333";
    const juce::String runtimeId = "runtime-post-a";
    const juce::String requestId = "44444444-4444-4444-8444-444444444444";

    [[maybe_unused]] void require (bool condition, const char* message)
    {
        if (! condition)
        {
            std::cerr << "Reference audition runtime test failed: " << message << '\n';
            std::exit (1);
        }
    }

    juce::var makeClock()
    {
        auto value = new juce::DynamicObject();
        value->setProperty ("sample_rate_hz", static_cast<juce::int64> (48'000));
        value->setProperty ("channels", static_cast<juce::int64> (2));
        value->setProperty ("sample_count", static_cast<juce::int64> (96'000));
        value->setProperty ("duration_seconds", 2.0);
        return juce::var (value);
    }

    juce::var makeObservation()
    {
        auto value = new juce::DynamicObject();
        value->setProperty ("kind", "loudness_lufs_m");
        value->setProperty ("source_hop_ms", 100.0);
        value->setProperty ("source_point_count", static_cast<juce::int64> (4));
        value->setProperty ("downsample_stride", static_cast<juce::int64> (1));
        value->setProperty ("signature_hop_ms", 100.0);
        value->setProperty ("quantization_db", 0.01);
        juce::Array<juce::var> values;
        values.add (-14.25);
        values.add (juce::var());
        values.add (-13.75);
        values.add (-14.0);
        value->setProperty ("values", juce::var (values));
        return juce::var (value);
    }

    [[maybe_unused]] juce::var makeSourceReceipt (const juce::File& source, const juce::String& sourceHash)
    {
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_hypha_ab_source_receipt");
        root->setProperty ("version", "1.0");

        auto identity = new juce::DynamicObject();
        identity->setProperty ("kind", "work_version");
        identity->setProperty ("source_id", "version-a");
        identity->setProperty ("source_work_id", workId);
        identity->setProperty ("version_id", "version-a");
        identity->setProperty ("catalog_reference_id", juce::var());
        identity->setProperty ("title", "Version A");
        identity->setProperty ("file_name", source.getFileName());
        identity->setProperty ("file_path", source.getFullPathName());
        identity->setProperty ("sha256_file", sourceHash);
        identity->setProperty ("measurement_receipt_sha256", juce::var());
        auto revision = new juce::DynamicObject();
        revision->setProperty ("dev", "1");
        revision->setProperty ("ino", "2");
        revision->setProperty ("size", juce::String (source.getSize()));
        revision->setProperty ("mtime_ms", "1000");
        revision->setProperty ("ctime_ms", "1000");
        identity->setProperty ("revision", juce::var (revision));
        root->setProperty ("source", juce::var (identity));

        auto measurement = new juce::DynamicObject();
        measurement->setProperty ("standard", "ITU-R BS.1770");
        measurement->setProperty ("measured_at", "2026-09-03T00:00:00.000Z");
        measurement->setProperty ("lufs_i", -14.0);
        measurement->setProperty ("max_true_peak_dbtp", -3.0);
        measurement->setProperty ("duration_seconds", 2.0);
        measurement->setProperty ("sample_rate_hz", static_cast<juce::int64> (48'000));
        root->setProperty ("measurement", juce::var (measurement));

        auto material = new juce::DynamicObject();
        material->setProperty ("clock", makeClock());
        auto content = new juce::DynamicObject();
        content->setProperty ("sha256_pcm", juce::var());
        material->setProperty ("content", juce::var (content));
        material->setProperty ("observation_signature", makeObservation());
        material->setProperty ("runtime_correlation_required", true);
        root->setProperty ("alignment_material", juce::var (material));
        return juce::var (root);
    }

    [[maybe_unused]] juce::var makePreparation (const juce::File& source, const juce::String& sourceHash,
                               const juce::String& receiptHash, std::int64_t receiptBytes)
    {
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_hypha_ab_preparation");
        root->setProperty ("version", "1.2");
        root->setProperty ("preparation_id", preparationId);
        auto target = new juce::DynamicObject();
        target->setProperty ("work_id", workId);
        root->setProperty ("target", juce::var (target));
        auto sourceSummary = new juce::DynamicObject();
        sourceSummary->setProperty ("kind", "work_version");
        sourceSummary->setProperty ("source_id", "version-a");
        sourceSummary->setProperty ("source_work_id", workId);
        sourceSummary->setProperty ("version_id", "version-a");
        sourceSummary->setProperty ("catalog_reference_id", juce::var());
        sourceSummary->setProperty ("title", "Version A");
        sourceSummary->setProperty ("file_name", source.getFileName());
        sourceSummary->setProperty ("sha256_file", sourceHash);
        root->setProperty ("source", juce::var (sourceSummary));
        auto receipt = new juce::DynamicObject();
        receipt->setProperty ("sha256", receiptHash);
        receipt->setProperty ("bytes", receiptBytes);
        root->setProperty ("source_receipt", juce::var (receipt));
        auto level = new juce::DynamicObject();
        level->setProperty ("policy", "b_matches_a");
        level->setProperty ("true_peak_ceiling_dbtp", -1.0);
        level->setProperty ("max_safe_positive_gain_db", 2.0);
        level->setProperty ("positive_gain_allowed", true);
        root->setProperty ("level_match", juce::var (level));
        auto alignment = new juce::DynamicObject();
        alignment->setProperty ("requested_mode", "sample_lock");
        alignment->setProperty ("runtime_confirmation_required", true);
        alignment->setProperty ("reference_cue_seconds", juce::var());
        root->setProperty ("alignment", juce::var (alignment));
        auto state = new juce::DynamicObject();
        state->setProperty ("audible_source_on_open", "a");
        state->setProperty ("prepared_side", "b");
        state->setProperty ("audio_modified_by_os", false);
        root->setProperty ("state", juce::var (state));
        root->setProperty ("prepared_at_ms", static_cast<juce::int64> (1'788'390'000'000));
        return juce::var (root);
    }

    [[maybe_unused]] bool writeJson (const juce::File& file, const juce::var& value)
    {
        return file.getParentDirectory().createDirectory()
            && file.replaceWithText (juce::JSON::toString (value, true) + "\n");
    }

    [[maybe_unused]] juce::var makeRuntimeABinding (const ref::RuntimeIdentity& identity,
                                   const juce::String& recordingId,
                                   std::int64_t issuedAtMs,
                                   std::int64_t leaseExpiresAtMs)
    {
        auto object = new juce::DynamicObject();
        object->setProperty ("format", "kirin_hypha_reference_a_binding");
        object->setProperty ("version", "1.0");
        object->setProperty ("binding_id", "99999999-9999-4999-8999-999999999999");
        object->setProperty ("runtime_instance_id", identity.runtimeInstanceId);
        object->setProperty ("host_process_id",
                             static_cast<juce::int64> (identity.hostProcessId));
        object->setProperty ("work_id", identity.workId);
        object->setProperty ("recording_id", recordingId);
        object->setProperty ("issued_at_ms", issuedAtMs);
        object->setProperty ("lease_expires_at_ms", leaseExpiresAtMs);
        return juce::var (object);
    }

    [[maybe_unused]] bool hasExactKeys (const juce::DynamicObject& object,
                       std::initializer_list<const char*> names)
    {
        if (object.getProperties().size() != static_cast<int> (names.size()))
            return false;
        for (const auto* name : names)
            if (! object.hasProperty (name))
                return false;
        return true;
    }

    [[maybe_unused]] bool writeStereoWav (const juce::File& file)
    {
        auto stream = file.createOutputStream();
        if (stream == nullptr)
            return false;
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
            stream.release(), 48'000.0, 2, 24, {}, 0));
        if (writer == nullptr)
            return false;
        juce::AudioBuffer<float> audio (2, 96'000);
        for (int frame = 0; frame < audio.getNumSamples(); ++frame)
        {
            const auto sample = static_cast<float> (
                0.25 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * frame / 48'000.0));
            audio.setSample (0, frame, sample);
            audio.setSample (1, frame, sample * 0.75f);
        }
        return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

}
