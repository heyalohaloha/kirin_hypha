#pragma once

#include "../src/reference_audition/ReferenceCrestBasis.h"

namespace
{
void testCrestBasisFixtures (const juce::File& root)
{
    const auto fixtures = juce::File (KIRIN_REFERENCE_FIXTURE_DIR).getChildFile ("kirin_os_crest_basis");
    const auto read = [&] (const char* name) { return juce::JSON::parse (fixtures.getChildFile (name)); };
    const auto views = [] (juce::var& value) { return value.getDynamicObject()->getProperty ("views").getDynamicObject(); };
    ref::RuntimeSource source;
    source.sourceFileSha256 = juce::String::repeatedString ("a", 64);
    source.sourcePcmSha256 = juce::String::repeatedString ("b", 64);
    source.audio = { 48'000, 2, 192'000 };
    ref::RuntimeV2MeasurementRepository measurements (root);
    ref::RuntimeV2ProfileRepository profiles (root);
    const auto measurement = [&] (const juce::var& value)
    {
        source.measurementArtifact = stageRuntimeV2Artifact (root, "measurements", value);
        return measurements.load (source);
    };
    const auto profile = [&] (const juce::var& value)
    { return profiles.load (stageRuntimeV2Artifact (root, "profiles", value)); };
    auto m = read ("measurement_sample_peak_over_rms.json");
    auto loaded = measurement (m);
    require (loaded.accepted() && loaded.measurement->dynamics->samplePeakCrest
             && juce::String (ref::crestLabel (loaded.measurement.get())) == "CREST", "new measurement crest basis");
    auto* dynamics = views (m)->getProperty ("dynamics").getDynamicObject();
    dynamics->removeProperty ("crest_basis");
    loaded = measurement (m);
    require (loaded.accepted() && juce::String (ref::crestLabel (loaded.measurement.get())) == "CREST (OLD)", "absent basis marks old measurement");
    for (const auto& basis : { juce::var ("unknown"), juce::var (42) })
    {
        dynamics->setProperty ("crest_basis", basis);
        loaded = measurement (m);
        require (loaded.accepted() && juce::String (ref::crestLabel (loaded.measurement.get())) == "CREST (OLD)", "unknown basis marks old measurement");
    }
    dynamics->setProperty ("other", 1);
    require (! measurement (m).accepted(), "unknown dynamics field rejected");
    m = read ("measurement_sample_peak_over_rms.json");
    views (m)->getProperty ("waveform").getDynamicObject()->setProperty ("crest_basis", "sample_peak_over_rms");
    require (! measurement (m).accepted(), "measurement other view rejects crest basis");
    m = read ("measurement_sample_peak_over_rms.json");
    views (m)->setProperty ("dynamics", juce::var());
    loaded = measurement (m);
    require (loaded.accepted() && juce::String (ref::crestLabel (loaded.measurement.get())) == "CREST", "null dynamics has no marker");
    require (juce::String (ref::crestLabel (nullptr)) == "CREST", "missing measurement has no marker");

    auto p = read ("profile_sample_peak_over_rms.json");
    auto loadedProfile = profile (p);
    require (loadedProfile.accepted() && loadedProfile.profile->views.at ("dynamics").samplePeakCrest, "new profile crest basis");
    p = read ("profile_mixed_formulas.json");
    loadedProfile = profile (p);
    require (loadedProfile.accepted() && ! loadedProfile.profile->views.at ("dynamics").samplePeakCrest, "mixed profile uses old basis");
    auto* profileDynamics = views (p)->getProperty ("dynamics").getDynamicObject();
    for (const auto& basis : { juce::var ("unknown"), juce::var (42) })
    {
        profileDynamics->setProperty ("crest_basis", basis);
        loadedProfile = profile (p);
        require (loadedProfile.accepted() && ! loadedProfile.profile->views.at ("dynamics").samplePeakCrest, "unknown profile basis is old");
    }
    profileDynamics->setProperty ("other", 1);
    require (! profile (p).accepted(), "profile dynamics rejects other keys");
    p = read ("profile_sample_peak_over_rms.json");
    views (p)->getProperty ("spectrum").getDynamicObject()->setProperty ("crest_basis", "sample_peak_over_rms");
    require (! profile (p).accepted(), "profile other view rejects crest basis");
    p = read ("profile_sample_peak_over_rms.json");
    views (p)->setProperty ("dynamics", juce::var());
    loadedProfile = profile (p);
    require (loadedProfile.accepted() && loadedProfile.profile->views.count ("dynamics") == 0, "null profile dynamics stays absent");
}
}
