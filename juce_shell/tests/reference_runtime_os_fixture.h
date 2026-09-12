#pragma once

// Optional producer/consumer check. Kirin OS exports a temporary fixture with
// its real worker and publisher; this branch reads it without running the
// unrelated runtime suite or changing a user's installed plugin.
namespace
{
    bool testRuntimeOsFixtureIfRequested()
    {
        const auto directory = juce::SystemStats::getEnvironmentVariable (
            "KIRIN_REFERENCE_OS_FIXTURE_ROOT", {});
        if (directory.isEmpty()) return false;
        const auto root = juce::File (directory).getChildFile ("transport");
        ref::RuntimeV2Repository repository (root);
        const auto loaded = repository.refresh (workId);
        if (! loaded.usable()) std::cerr << loaded.rejectionCode << '\n';
        require (loaded.usable(), "OS-produced Manifest and factory Work snapshot must load");
        require (loaded.workspace->presets.size() == 1, "producer fixture must contain one Preset");
        ref::RuntimeV2SourceRepository sources (root);
        ref::RuntimeV2MeasurementRepository measurements (root);
        ref::RuntimeV2AlignmentRepository alignments (root);
        ref::RuntimeV2ProfileRepository profiles (root);
        std::size_t checked = 0;
        for (const auto& check : loaded.workspace->presets[0].checks)
        {
            require (! check.candidates.empty(), "all factory Checks must have a source");
            const auto source = sources.load (check.candidates[0]);
            require (source.accepted(), "OS-produced source receipt must validate");
            require (sources.verifySourceFile (*source.source).isEmpty(),
                     "OS-produced source revision and full file hash must validate");
            const auto measurement = measurements.load (*source.source);
            require (measurement.accepted(), "OS-produced detailed facts must validate");
            const auto& value = *measurement.measurement;
            require (value.waveform && value.waveform->samplePeakMillidbfs[0].size() == 100
                     && value.spectrum && value.spectrum->bandCentersHz.size() == 64,
                     "real 10-second WAV must keep 100 time bins and 64 frequency bands");
            require (value.loudness && ! value.loudness->series.at ("lufs_m_millilu")[0],
                     "unmeasured opening loudness must stay absent in the consumer");
            const auto alignment = alignments.load (*source.source);
            require (alignment.accepted() && alignment.alignment->grid.pointCount == 100,
                     "OS-produced alignment facts must keep source coverage");
            require (! check.profileBindings.empty(), "producer fixture must include a Profile");
            for (const auto& binding : check.profileBindings)
            {
                const auto profile = profiles.load (binding.profileArtifact);
                require (profile.accepted() && profile.profile->sourceCount == 3,
                         "OS-produced Profile projection must validate");
            }
            auto wrongSource = *source.source;
            wrongSource.sourcePcmSha256 = juce::String::repeatedString ("f", 64);
            require (! measurements.load (wrongSource).accepted()
                     && ! alignments.load (wrongSource).accepted(),
                     "source identity drift must reject otherwise valid producer facts");
            ++checked;
        }
        require (checked > 0, "producer fixture must exercise enabled Checks");
        std::cout << "OS -> Hypha Reference fixture: " << checked << " Checks accepted\n";
        return true;
    }
}
