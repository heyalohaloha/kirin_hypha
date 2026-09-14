#pragma once
#include "../src/HyphaReferenceComponent.h"
#include <cstdlib>
#include <iostream>
namespace hypha::tests
{
inline void verifyReferenceVisualComparison()
{
    const auto check = [] (bool valid, const char* message) { if (!valid) { std::cerr << "Visual UI: " << message << '\n'; std::exit (1); } };
    auto source = std::make_shared<reference_audition::RuntimeSource>();
    source->audio = { 48000, 2, 48000*200 }; source->sourceFileSha256 = juce::String::repeatedString ("a",64);
    source->sourceKind = "work_version"; source->sourceIdentityKey = "same-work:recording:version-a";
    auto measure = std::make_shared<reference_audition::RuntimeDetailedMeasurement>();
    measure->waveform.emplace(); measure->waveform->framesPerBin = 4800;
    measure->waveform->samplePeakMillidbfs.resize (2); measure->waveform->rmsMillidbfs.resize (2);
    auto timeline = std::make_shared<reference_audition::VisualTimeline>();
    timeline->binding.source = source; timeline->binding.overview = measure; timeline->binding.key = "same-song-verified";
    timeline->binding.aligned = timeline->binding.matched = true; timeline->binding.channels = 2; timeline->binding.hostRate = 48000;
    timeline->pass = timeline->revision = 1; timeline->hop = 4800; timeline->bins.resize (2000);
    for (size_t i=0; i<2000; ++i)
    {
        const double value = 0.4 + 0.28 * std::sin (i * 0.037) * std::sin (i * 0.017);
        for (int c=0;c<2;++c) { measure->waveform->samplePeakMillidbfs[size_t(c)].push_back (std::llround (20000*std::log10(value))); measure->waveform->rmsMillidbfs[size_t(c)].push_back (std::llround (20000*std::log10(value*0.45))); }
        if (i > 700 && i < 1100)
        {
            auto& bin = timeline->bins[i]; bin.pass = 1; bin.a.frames = bin.b.frames = 4800;
            for (int c=0;c<2;++c) { bin.a.peak[c] = value*0.97; bin.b.peak[c] = value; bin.a.rms[c] = value*0.44; bin.b.rms[c] = value*0.45; }
            bin.a.short_lufs = -14.0 + std::sin (i*0.03); bin.b.short_lufs = bin.a.short_lufs+0.5;
            bin.a.crest_db = 7.0; bin.b.crest_db = 6.8;
        }
    }
    reference_ui::State state;
    state.visualPreferences = std::make_shared<reference_audition::VisualPreferences>();
    state.visualTimeline = timeline; state.visualPositionSeconds = 98.0;
    state.separateComparisons = state.libraryReceived = state.osOnline = state.aAvailable = state.versionReady = state.checkReady = true;
    state.comparisonSlot = 1; state.title = "Mix v4"; state.status = "PLAY TO AUDITION";
    state.osAccess = os_access::State::ready; state.readiness = reference_ui::Readiness::ready;
    state.blindPhase = reference_ui::BlindPhase::available;
    state.versions = {{ "v4", "Mix v4" }}; state.versionId = "v4";
    state.presets = {{ "basic", "Quick Reference" }}; state.presetId = "basic";
    state.checks = {{ "dynamics", "Dynamics" }}; state.checkId = "dynamics";
    reference_ui::Component component;
    int audioActions = 0; component.onSelectA = component.onSelectB = component.onSelectC = [&] { ++audioActions; };
    for (int width : {300,375,450,600,900})
    {
        component.setPresentationContext (presentation::forEditor (width,width*2/3));
        component.setSize (width-12,width==900 ? 470 : width*2/3-64); component.setState (state);
        check (!component.findChildWithID ("reference-preset")->isVisible(), "C Preset row stays in C while B owns the comparison view");
        auto* view = dynamic_cast<reference_ui::ComparisonView*> (component.findChildWithID ("reference-comparison-view"));
        check (view && view->isVisible() && component.getLocalBounds().contains (view->getBounds()), "comparison fits every size");
        for (const auto* id : {"reference-version","reference-check","reference-a","reference-b","reference-c"})
            check (!view->getBounds().intersects (component.findChildWithID (id)->getBounds()), "graphs never cover selectors or A/B/C");
        view->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)); const auto range = view->selectedRange();
        state.bSelected = true; state.audibleComparisonSlot = 1; component.setState (state);
        check (view->selectedRange() == range && audioActions == 0, "view navigation never switches audio and A/B preserves selection");
        juce::Image image (juce::Image::ARGB,component.getWidth(),component.getHeight(),true);
        juce::Graphics g (image); component.paintEntireComponent (g,true);
        const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_VISUAL_OUTPUT",{});
        if (output.isNotEmpty()) { const auto root=juce::File(output); check(root.createDirectory(),"render directory"); juce::FileOutputStream stream(root.getChildFile(juce::String(width)+".png")); check(juce::PNGImageFormat().writeImageToStream(image,stream),"UI render"); }
        state.blindPhase = reference_ui::BlindPhase::active; component.setState (state);
        check (!view->isVisible() && view->getTitle().isEmpty(), "Blind hides waveform, controls and accessibility identity");
        state.blindPhase = reference_ui::BlindPhase::available; component.setState (state);
        state.bSelected = false; state.audibleComparisonSlot = 0;
    }
    const auto saved = state.visualPreferences->get();
    check (saved.valid(), "source-qualified view choice is saved");
    juce::XmlElement xml ("ReferenceChoices"); saved.write (xml);
    const auto restored = reference_audition::VisualViewChoice::read (xml);
    check (restored.sourceHash == saved.sourceHash && std::abs (restored.start-saved.start)<1e-9 && restored.follow == saved.follow, "view choice roundtrips independently of observations");
    reference_ui::Component reopened; reopened.setPresentationContext (presentation::forEditor (900,600));
    reopened.setSize (888,470); reopened.setState (state);
    auto* restoredView = dynamic_cast<reference_ui::ComparisonView*> (reopened.findChildWithID ("reference-comparison-view"));
    check (restoredView && std::abs (restoredView->selectedRange().getStart()-saved.start)<1e-9, "editor reopen restores verified source range");
    std::vector<double> durations;
    juce::Image bench (juce::Image::ARGB,888,470,true); juce::Graphics bg (bench);
    for (int i=0;i<120;++i) { const auto at=juce::Time::getMillisecondCounterHiRes(); reopened.paintEntireComponent(bg,true); if(i>=5) durations.push_back(juce::Time::getMillisecondCounterHiRes()-at); }
    std::sort (durations.begin(),durations.end());
    std::cout << "Reference full panel 900x600 paint p95=" << durations[size_t(durations.size()*0.95)] << " ms p99=" << durations[size_t(durations.size()*0.99)] << " ms\n";
    juce::Image detailImage(juce::Image::ARGB,restoredView->getWidth(),restoredView->getHeight(),true);
    juce::Graphics detailGraphics(detailImage);
    for (bool rebuild : {false,true})
    {
        durations.clear();
        for(int i=0;i<60;++i)
        {
            if(rebuild) ++timeline->revision;
            const auto at=juce::Time::getMillisecondCounterHiRes(); restoredView->paintEntireComponent(detailGraphics,true);
            if(i>=5) durations.push_back(juce::Time::getMillisecondCounterHiRes()-at);
        }
        std::sort(durations.begin(),durations.end());
        std::cout << "Reference comparison " << (rebuild ? "cache rebuild" : "cached paint")
            << " p95=" << durations[size_t(durations.size()*0.95)] << " ms p99=" << durations[size_t(durations.size()*0.99)] << " ms\n";
    }
    std::cout << "Reference visual UI: five sizes, navigation, audition separation and Blind privacy passed\n";
    const auto oldRange = restoredView->selectedRange();
    auto anotherSource = std::make_shared<reference_audition::RuntimeSource>(*source);
    anotherSource->sourceFileSha256 = juce::String::repeatedString("b",64);
    anotherSource->sourceIdentityKey = "same-work:recording:version-b";
    auto anotherTimeline = std::make_shared<reference_audition::VisualTimeline>(*timeline);
    anotherTimeline->binding.source = anotherSource; anotherTimeline->binding.key = "new-version-map";
    anotherTimeline->binding.sourceAnchor += 96000;
    state.visualTimeline.reset(); reopened.setState(state); // Loading may temporarily remove the published source.
    state.visualTimeline = anotherTimeline; reopened.setState(state);
    check(std::abs(restoredView->selectedRange().getStart()-oldRange.getStart()-2.0)<1e-9,
        "same-song Version change retains the selected DAW interval through its verified offset");
}
}
