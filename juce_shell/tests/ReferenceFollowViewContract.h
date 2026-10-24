#pragma once

#include "ReferenceComparisonLayoutContract.h"

namespace hypha::tests::reference_follow_view
{
namespace ref = reference_audition;
namespace layout = reference_comparison_layout;
using View = reference_ui::ComparisonView;

inline std::shared_ptr<ref::VisualTimeline> timeline (double duration = 60.0)
{
    auto source = std::make_shared<ref::RuntimeSource>();
    source->audio = { 48000, 2, std::llround (48000 * duration) };
    source->sourceFileSha256 = juce::String::repeatedString ("c", 64);
    auto overview = std::make_shared<ref::RuntimeDetailedMeasurement>();
    overview->waveform.emplace(); overview->waveform->framesPerBin = 4800;
    overview->waveform->samplePeakMillidbfs.resize (2);
    overview->waveform->rmsMillidbfs.resize (2);
    auto result = std::make_shared<ref::VisualTimeline>();
    result->binding.source = source; result->binding.overview = overview;
    result->binding.key = "follow-view-fixture";
    result->binding.aligned = result->binding.matched = true;
    result->binding.channels = 2; result->binding.hostRate = 48000;
    result->pass = result->revision = 1; result->hop = 4800;
    for (int index = 0; index < int (duration * 10); ++index)
    {
        const auto amplitude = 0.35 + 0.05 * std::sin (index * 0.2);
        for (size_t channel = 0; channel < 2; ++channel)
        {
            overview->waveform->samplePeakMillidbfs[channel].push_back (
                std::llround (20000 * std::log10 (amplitude)));
            overview->waveform->rmsMillidbfs[channel].push_back (
                std::llround (20000 * std::log10 (amplitude * 0.45)));
        }
        ref::VisualPairBin pair;
        // Completed observations end at the playback position, never in the future.
        if (index < 300)
        {
            pair.pass = 1; pair.a.frames = pair.b.frames = 4800;
            pair.a.short_lufs = -14.0 + std::sin (index * 0.07);
            pair.b.short_lufs = pair.a.short_lufs + 0.5;
            pair.a.crest_db = 7.0 + std::sin (index * 0.07);
            pair.b.crest_db = pair.a.crest_db + 0.5;
            for (size_t channel = 0; channel < 2; ++channel)
            {
                pair.a.peak[channel] = amplitude * 0.9; pair.b.peak[channel] = amplitude;
                pair.a.rms[channel] = amplitude * 0.4; pair.b.rms[channel] = amplitude * 0.45;
            }
        }
        result->bins.push_back (pair);
    }
    return result;
}

inline void requireRange (const View& view, double first, double last, const char* why)
{
    const auto actual = view.selectedRange();
    layout::expect (std::abs (actual.getStart() - first) < 1.0e-4
                        && std::abs (actual.getEnd() - last) < 1.0e-4, why);
}

inline void verifyRanges (const std::shared_ptr<const ref::VisualTimeline>& data)
{
    layout::expect (View::followHistorySeconds == 10.0 && View::followLookAheadSeconds == 2.0,
                    "FOLLOW exposes its ten-second history and two-second look-ahead");
    const auto context = presentation::forEditor (900, 600);
    View view; view.setSize (860, 300);
    view.update (data, 30, context, false);
    requireRange (view, 20, 32, "FOLLOW at 30 s shows 20..32 s, with playback two seconds from the right");
    view.update (data, 31, context, false);
    requireRange (view, 21, 33, "FOLLOW advances with playback and keeps twelve seconds visible");
    for (const double position : { 0.0, 1.0, 10.0 })
    {
        view.update (data, position, context, false);
        requireRange (view, 0, 12, "the song start shifts the full window inside the song");
    }
    view.update (data, 11, context, false);
    requireRange (view, 1, 13, "FOLLOW leaves the start clamp after ten seconds");
    for (const double position : { 58.0, 59.0, 60.0 })
    {
        view.update (data, position, context, false);
        requireRange (view, 48, 60, "the song end shifts the full window inside the song");
    }
    for (const double duration : { 5.0, 0.5 })
    {
        View shortView;
        shortView.update (timeline (duration), duration * 0.5, context, false);
        requireRange (shortView, 0, duration, "a song shorter than the window fits entirely");
    }
    view.update (data, 30, context, false);
    for (const double position : { -1.0, 61.0, std::numeric_limits<double>::quiet_NaN(),
                                  std::numeric_limits<double>::infinity() })
    {
        view.update (data, position, context, false);
        requireRange (view, 20, 32, "an unavailable or out-of-song position never moves the view");
    }
    view.update ({}, 40, context, false);
    requireRange (view, 20, 32, "an absent timeline never moves the view");
}

inline juce::MouseEvent mouse (View& view, float fraction, float anchor, bool dragged)
{
    const auto waveform = view.visualLayout().waveform;
    const auto point = [&] (float t)
    { return juce::Point<float> (waveform.getX() + waveform.getWidth() * t, waveform.getCentreY()); };
    return { juce::Desktop::getInstance().getMainMouseSource(), point (fraction),
             juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 0, 0, 0, 0, 0,
             &view, &view, juce::Time(), point (anchor), juce::Time(), 1, dragged };
}

inline void verifyNavigation (const std::shared_ptr<const ref::VisualTimeline>& data)
{
    const auto context = presentation::forEditor (900, 600);
    auto preferences = std::make_shared<ref::VisualPreferences>();
    View view; view.setSize (860, 300); view.update (data, 30, context, false, preferences);
    view.mouseDown (mouse (view, 0.5f, 0.5f, false));
    requireRange (view, 24, 36, "a manual click keeps its centred twelve-second window");
    view.mouseUp (mouse (view, 0.5f, 0.5f, false));
    view.update (data, 40, context, false, preferences);
    requireRange (view, 24, 36, "a click disables FOLLOW while playback moves");
    view.mouseDown (mouse (view, 0.3f, 0.3f, false));
    view.mouseDrag (mouse (view, 0.7f, 0.3f, true));
    view.mouseUp (mouse (view, 0.7f, 0.3f, true));
    requireRange (view, 18, 42, "a manual drag keeps the selected interval");
    layout::expect (view.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)), "right arrow is handled");
    requireRange (view, 19, 43, "an arrow pans the manual range by one second");
    view.keyPressed (juce::KeyPress (juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0));
    requireRange (view, 19, 42, "Shift and arrow resize the manual range by one second");
    view.update (data, 50, context, false, preferences);
    requireRange (view, 19, 42, "manual navigation remains fixed during playback");
    auto* crest = dynamic_cast<juce::Button*> (view.findChildWithID ("reference-crest"));
    layout::expect (crest && crest->isVisible(), "the existing metric choice remains available");
    crest->onClick();
    juce::XmlElement xml ("ReferenceChoices"); preferences->get().write (xml);
    auto restored = std::make_shared<ref::VisualPreferences>();
    restored->set (ref::VisualViewChoice::read (xml));
    const auto saved = restored->get();
    layout::expect (saved.valid() && !saved.follow && saved.crest
                        && saved.sourceHash == data->binding.source->sourceFileSha256,
                    "the existing source-qualified range, metric and FOLLOW preferences roundtrip");
    View reopened; reopened.setSize (860, 300); reopened.update (data, 50, context, false, restored);
    requireRange (reopened, 19, 42, "reopening preserves the saved manual interval");
    layout::expect (reopened.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)), "Home is handled");
    requireRange (reopened, 19, 42, "Home still resumes FOLLOW on the next update");
    reopened.update (data, 30, context, false, restored);
    requireRange (reopened, 20, 32, "Home resumes the playback-biased FOLLOW range");
    reopened.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    auto* follow = dynamic_cast<juce::Button*> (reopened.findChildWithID ("reference-follow"));
    layout::expect (follow && follow->isVisible(), "the existing FOLLOW button remains available");
    follow->onClick();
    requireRange (reopened, 21, 33, "the FOLLOW button still waits for the next update");
    reopened.update (data, 40, context, false, restored);
    requireRange (reopened, 30, 42, "the FOLLOW button resumes following playback");
    layout::expect (restored->get().follow && restored->get().crest,
                    "FOLLOW still saves its preference flag and preserves the chosen metric");
}

inline void verify()
{
    const auto data = timeline();
    std::vector<std::pair<juce::Image, juce::Rectangle<float>>> rightHandTraces;
    const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_FOLLOW_OUTPUT", {});
    for (const auto& preset : observatory::sizePresets)
    {
        const auto context = presentation::forEditor (preset.width, preset.height);
        juce::Component root; root.getProperties().set (key_light::rootProperty, true);
        root.setSize (preset.width, preset.height);
        reference_ui::Component panel; root.addAndMakeVisible (panel);
        panel.setPresentationContext (context);
        panel.setBounds (6, 50, preset.width - 12, preset.width == 900 ? 470 : preset.height - 64);
        auto state = reference_review::playing (reference_review::library());
        state.comparisonSlot = 1; state.versionId = "v4"; state.versionReady = state.checkReady = true;
        state.versions = { { "v4", "Mix v4" } }; state.checks.clear(); state.cues.clear();
        state.visualTimeline = data; state.visualPositionSeconds = 30;
        panel.setState (state);
        auto* view = dynamic_cast<View*> (panel.findChildWithID ("reference-comparison-view"));
        layout::expect (view && view->isVisible(), "FOLLOW comparison is visible at every editor size");
        for (const bool crest : { false, true })
        {
            auto* mode = dynamic_cast<juce::Button*> (view->findChildWithID (crest ? "reference-crest" : "reference-loudness"));
            if (mode->isVisible()) mode->onClick();
            const auto image = layout::render (*view);
            layout::requireGeometry (*view, context);
            const auto& geometry = view->visualLayout();
            layout::requireTraces (image, geometry.waveform);
            if (geometry.detailed())
            {
                // Completed A/V curves must reach the right-hand part of the plot. A centred
                // window has no observations here; inspect real ink rather than computed bounds.
                const auto plot = geometry.plot;
                rightHandTraces.emplace_back (image, juce::Rectangle<float> (
                    plot.getX() + plot.getWidth() * 0.70f, plot.getY(), plot.getWidth() * 0.10f, plot.getHeight()));
            }
            if (output.isNotEmpty() && preset.width == 900 && !crest)
            {
                const juce::File directory (output);
                layout::expect (directory.createDirectory(), "snapshot directory is available");
                juce::FileOutputStream stream (directory.getChildFile ("900.png"));
                layout::expect (stream.setPosition (0) && stream.truncate().wasOk(), "snapshot file is writable");
                layout::expect (juce::PNGImageFormat().writeImageToStream (layout::render (panel), stream),
                                "the actual Reference panel snapshot is written");
            }
        }
    }
    verifyRanges (data);
    verifyNavigation (data);
    layout::expect (!rightHandTraces.empty(), "the size sweep includes detailed graphs");
    for (const auto& [image, region] : rightHandTraces) layout::requireTraces (image, region);
    std::cout << "Reference FOLLOW: history, look-ahead, clamps, navigation, preferences and all sizes passed\n";
}
}
