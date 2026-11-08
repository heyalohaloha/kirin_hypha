#pragma once
#include "../src/PluginEditor.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"

namespace content_analysis_product
{
struct MidSideRefresh
{
    using Type = bool (KirinHyphaEditor::*) (bool, int, bool, bool, bool, bool, int);
    friend Type midSideRefresh (MidSideRefresh);
};
template <class Tag, typename Tag::Type Member> struct MidSideAccess
{ friend typename Tag::Type midSideRefresh (Tag) { return Member; } };
template struct MidSideAccess<MidSideRefresh, &KirinHyphaEditor::refreshAnalysisViews>;

inline hypha::SpectrumComponent* midSideComponent (juce::Component& parent)
{
    if (auto* view = dynamic_cast<hypha::SpectrumComponent*> (&parent)) return view;
    for (auto* child : parent.getChildren())
        if (auto* view = midSideComponent (*child)) return view;
    return nullptr;
}

// Match a host's editor lifetime: JUCE requires notification before deleting a registered editor.
struct AnalysisEditorRelease
{
    Processor& processor;
    void operator() (juce::AudioProcessorEditor* editor) const
    { processor.editorBeingDeleted (editor); delete editor; }
};
using AnalysisEditor = std::unique_ptr<juce::AudioProcessorEditor, AnalysisEditorRelease>;

inline void verifyMidSideContinuityProduct (Fixture& fixture)
{
    auto& processor = *fixture.post;
    for (const bool low : { false, true })
    {
        processor.setObservatoryDomainPreference (hypha::observatory::stateValue (hypha::observatory::Domain::frequency));
        processor.setObservatoryTargetPreference (0);
        processor.setSpectrumLowScope (low);
        require (processor.setSpectrumDisplaySelection (KIRIN_SPECTRUM_SELECTION_MID_SIDE, true), "select M/S");
        processor.setSpectrumAveragePreference (hypha::spectrum_average::Choice::seconds10);
        AnalysisEditor editor (processor.createEditorIfNeeded(), { processor });
        auto& shipping = *static_cast<KirinHyphaEditor*> (editor.get());
        editor->setSize (600, 400); editor->addToDesktop (0); editor->setVisible (true);
        auto* view = midSideComponent (*editor);
        require (view != nullptr, "shipping editor has M/S view");
        const auto refresh = [&]
        {
            require ((shipping.*midSideRefresh (MidSideRefresh {})) (true, KIRIN_SIGNAL_STATE_ACTIVE,
                false, false, false, false, processor.pairStatus()), "shipping analysis entry refreshes M/S");
        };
        KirinMidSideSpectrumView source {}; uint64_t continuity = 0;
        fixture.until ([&] { return processor.pollMidSideSpectrum (source, &continuity)
            && source.has_data && continuity != 0; }, "M/S publishes a continuous aperture");
        refresh();
        const auto firstEndpoint = source.presentation_end_samples;
        const auto firstContinuity = continuity;
        const auto displayed = view->displayedPreForTest (100);
        const auto updated = view->curveUpdateTimeForTest();
        const auto plot = hypha::spectrum_geometry::dataPlotBoundsFor (view->getLocalBounds().toFloat(), false, 96, true);
        const auto point = plot.getCentre();
        view->mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(), point, {}, 0, 0, 0, 0, 0,
            view, view, {}, point, {}, 0, false });
        require (view->hasFocusLock(), "FOCUS locks through the real plot control");
        // Keep analysing, but deliberately omit editor polls for several FFT publications.
        for (int block = 0; block < 24; ++block) fixture.block();
        require (processor.pollMidSideSpectrum (source, &continuity) && source.has_data
            && source.presentation_end_samples - firstEndpoint > 2400 && continuity == firstContinuity,
            "GUI skipped publications while the actual producer stayed continuous");
        refresh();
        require (view->hasFocusLock() && view->averageChoice() == hypha::spectrum_average::Choice::seconds10,
                 "the shipping poll preserves FOCUS and AVG after skipped acquisitions");
        hypha::spectrum_display::Bins raw;
        std::copy (std::begin (source.mid_dbfs), std::end (source.mid_dbfs), raw.begin());
        const auto target = hypha::spectrum_display::spatial (raw, source.min_hz, source.max_hz,
            view->displaySettingsForTest()[hypha::display_settings::smooth])[100];
        const auto expected = displayed + (target - displayed)
            * -std::expm1 (-(view->curveUpdateTimeForTest() - updated) / 10000.0);
        require (std::abs (view->displayedPreForTest (100) - expected) < 0.001f,
                 "a delayed poll does not replace the averaged curve with its new target");
        // Do not poll the intermediate warming state: the new aperture must itself carry the edge.
        fixture.preClock.content += 96000; fixture.postClock.content += 96000;
        fixture.until ([&] { return processor.pollMidSideSpectrum (source, &continuity)
            && source.has_data && continuity != firstContinuity; }, "new audio segment publishes a new identity");
        refresh();
        std::copy (std::begin (source.mid_dbfs), std::end (source.mid_dbfs), raw.begin());
        const auto resumed = hypha::spectrum_display::spatial (raw, source.min_hz, source.max_hz,
            view->displaySettingsForTest()[hypha::display_settings::smooth])[100];
        require (! view->hasFocusLock() && std::abs (view->displayedPreForTest (100) - resumed) < 0.001f,
                 "a real discontinuity starts from measured values even if the GUI missed the warming state");
        editor.reset();
        fixture.owner = processor.beginAnalysisUiSession();
    }
    std::cout << "M/S product: FULL/LOW skipped GUI polls, AVG/FOCUS and discontinuity recovery PASS\n";
}
inline void verifySpectrumContinuityProduct (Fixture& fixture)
{
    auto& processor = *fixture.post;
    fixture.until ([&] { return processor.setPairCandidate (fixture.pre->instanceId(), {}); },
                   "select PRE for the paired spectrum entry");
    for (const bool low : { false, true })
        for (const bool absolute : { false, true })
        {
            processor.setObservatoryDomainPreference (hypha::observatory::stateValue (hypha::observatory::Domain::frequency));
            processor.setObservatoryTargetPreference (absolute ? 0 : 1);
            processor.setSpectrumLowScope (low);
            require (processor.setSpectrumDisplaySelection (KIRIN_SPECTRUM_CHANNEL_LR, absolute), "select LR");
            processor.setSpectrumAveragePreference (hypha::spectrum_average::Choice::seconds10);
            AnalysisEditor editor (processor.createEditorIfNeeded(), { processor });
            auto& shipping = *static_cast<KirinHyphaEditor*> (editor.get());
            editor->setVisible (false); editor->setSize (600, 400); editor->addToDesktop (0); editor->setVisible (true);
            auto* view = midSideComponent (*editor);
            require (view != nullptr, "shipping editor has a spectrum");
            const auto refresh = [&] { require ((shipping.*midSideRefresh (MidSideRefresh {})) (true,
                KIRIN_SIGNAL_STATE_ACTIVE, false, false, false, false, processor.pairStatus()), "refresh actual spectrum entry"); };
            KirinSpectrumBatch source {}; uint64_t post = 0, delta = 0;
            const auto poll = [&] { return processor.pollSpectrumBatch (source, &post, &delta)
                && source.latest.post_has_data && (absolute || source.latest.has_data); };
            std::cerr << "Spectrum entry: showing=" << editor->isShowing() << " view=" << view->isShowing()
                      << " demand=" << static_cast<int> (processor.requestedAnalysisDemand().kind) << '\n';
            require (processor.requestedAnalysisDemand().kind == hypha::analysis::Kind::spectrum, "editor requests ordinary spectrum");
            fixture.until ([&] { return poll() && source.latest.has_data; }, "producer publishes an ordinary paired spectrum segment");
            refresh();
            require (view->isAbsoluteObservationForTest() == absolute, "requested target reaches the view");
            const size_t band = low ? 160 : 76;
            const auto endpoint = source.latest.presentation_end_samples;
            const auto identity = absolute ? post : delta;
            const auto displayed = absolute ? view->displayedPostForTest (band) : view->displayedDeltaForTest (band);
            const auto updated = view->curveUpdateTimeForTest();
            require (identity != 0, "real producer carries continuity");
            if (absolute) fixture.inputGain = 0.25f; else fixture.postGain = 0.5f;
            // Poll the producer, but omit the actual GUI entry for longer than its eight-frame ring.
            fixture.until ([&] { return poll() && source.latest.presentation_end_samples - endpoint > 16000; },
                           "GUI acquisition omits more than eight publications");
            require ((absolute ? post : delta) == identity, "only the GUI skipped frames");
            refresh();
            hypha::spectrum_display::Bins bins;
            const auto* raw = absolute ? source.latest.post_dbfs : source.latest.display_db;
            std::copy (raw, raw + bins.size(), bins.begin());
            const auto target = hypha::spectrum_display::spatial (bins, source.latest.min_hz, source.latest.max_hz,
                view->displaySettingsForTest()[hypha::display_settings::smooth])[band];
            require (std::abs (target - displayed) > 3, "actual audio made the reset bug observable");
            const auto expected = displayed + (target - displayed)
                * -std::expm1 (-(view->curveUpdateTimeForTest() - updated) / 10000.0);
            require (std::abs ((absolute ? view->displayedPostForTest (band) : view->displayedDeltaForTest (band)) - expected) < 0.002,
                     "delayed GUI acquisition preserves AVG through the FFI and editor");
            fixture.preClock.content += 96000; fixture.postClock.content += 96000;
            fixture.until ([&] { return poll() && (absolute ? post : delta) != identity; },
                           "real audio gap reaches the recovered batch without a warming GUI poll");
            refresh();
            std::copy ((absolute ? source.latest.post_dbfs : source.latest.display_db),
                       (absolute ? source.latest.post_dbfs : source.latest.display_db) + bins.size(), bins.begin());
            const auto resumed = hypha::spectrum_display::spatial (bins, source.latest.min_hz, source.latest.max_hz,
                view->displaySettingsForTest()[hypha::display_settings::smooth])[band];
            require (std::abs ((absolute ? view->displayedPostForTest (band) : view->displayedDeltaForTest (band)) - resumed) < 0.002,
                     "real missing audio cuts interpolation while retaining the selected average");
            require (view->averageChoice() == hypha::spectrum_average::Choice::seconds10, "AVG choice survives recovery");
            editor.reset(); fixture.owner = processor.beginAnalysisUiSession();
            fixture.inputGain = fixture.postGain = 1;
        }
    std::cout << "Spectrum product: FULL/LOW absolute/delta, long GUI stalls and real gaps PASS\n";
}

}
