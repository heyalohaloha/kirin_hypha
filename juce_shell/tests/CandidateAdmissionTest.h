#pragma once
#include "CandidateProductHost.h"

namespace hypha::tests
{
template<class Require> void verifyCandidatePreAdmission (Require require)
{
    using namespace live_compare;
    using P = KirinHyphaProcessorBase;
    ValidationStorageSandbox isolated;
    writeCandidateFixtureIdentity (require, isolated.directory());
    ProductTestClock wall;
    for (const int scenario : { 0, 1, 2, 3 }) // PRE1 multi-mono, PRE2 multi-mono, mono, stereo
    {
        constexpr int frames = 512;
        const int channels = scenario == 3 ? 2 : 1;
        std::array<CandidateClock, 3> clocks;
        std::array<std::unique_ptr<P>, 3> processors;
        std::unique_ptr<P> extra;
        for (int i = 0; i < 3; ++i)
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AAX);
            processors[static_cast<std::size_t> (i)] = std::make_unique<P> (i == 2 ? P::Role::Post : P::Role::Pre);
        }
        if (scenario < 2)
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AAX);
            extra = std::make_unique<P> (P::Role::Pre);
        }
        // The host names every member before any processor is prepared.
        for (int i = 0; i < 3; ++i) processors[static_cast<std::size_t> (i)]->kirinHostInstanceGroup (100 + i, true);
        if (extra) extra->kirinHostInstanceGroup (100 + scenario, true);
        const auto prepare = [&] (P& processor, CandidateClock& clock)
        {
            auto layout = processor.getBusesLayout();
            layout.inputBuses.set (0, channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
            layout.outputBuses.set (0, layout.inputBuses[0]);
            require (processor.setBusesLayout (layout), "AAX admission fixture negotiates its layout");
            processor.setPlayHead (&clock);
            prepareFixtureProcessor (processor, 48000, frames);
            LiveTimingFixtureAccess::enableFixtureWrites (processor);
            LiveTimingFixtureAccess::configureAaxClock (processor, true);
        };
        for (int i = 0; i < 3; ++i) { clocks[static_cast<std::size_t> (i)].source = 3; prepare (*processors[static_cast<std::size_t> (i)], clocks[static_cast<std::size_t> (i)]); }
        if (extra) prepare (*extra, clocks[static_cast<std::size_t> (scenario)]);
        auto& pre1 = *processors[0]; auto& pre2 = *processors[1]; auto& post = *processors[2];
        juce::AudioBuffer<float> audio (channels, frames); juce::MidiBuffer midi;
        std::int64_t position = 0;
        const auto render = [&] (int count)
        {
            for (int n = 0; n < count; ++n, position += frames)
            {
                wall.advanceBlock (frames, 48000);
                for (auto& clock : clocks) clock.clock = position;
                for (auto& processor : processors) processor->serviceLiveCompare();
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < frames; ++i) audio.setSample (ch, i, candidateInput (position + i, ch));
                for (auto& processor : processors)
                {
                    processor->processBlock (audio, midi);
                    if (!post.candidateCompareActive())
                        for (int ch = 0; ch < channels; ++ch)
                            for (int i = 0; i < frames; ++i)
                                require (candidateIdentical (audio.getSample (ch, i), candidateInput (position + i, ch)),
                                         "rejected A/B leaves normal audio bit identical");
                }
            }
        };
        render (60);
        for (auto& processor : processors) { awaitAnalysisInput (*processor); awaitAnalysisExchange (*processor, true); }
        require (post.setPairCandidate (pre1.instanceId(), "Source"), "admission fixture establishes normal PRE1 pair");
        const auto result = post.startCandidateCompare (pre2.instanceId(), true);
        if (scenario >= 2)
        {
            require (result == StartResult::started, "single-group mono and stereo AAX admit A/B");
            post.finishLiveCompare(); render (700);
        }
        else
        {
            require (result == StartResult::preMultiMono, "either multi-mono PRE is refused");
            const auto& state = LiveTimingFixtureAccess::candidateStateForTest (post);
            const auto* mapping = LiveTimingFixtureAccess::publishedRing (pre2);
            require (!post.candidateCompareActive() && !post.candidateMeasurementIsolated() && state.scope == 0
                && state.token.load() == 0 && !post.liveCompareStatus().active && post.pairedPreInstanceId() == pre1.instanceId()
                && mapping && mapping->ring() && mapping->ring()->header.candidate.owner.load() == 0,
                "refusal leaves pair, output, exclusion and PRE2 request untouched");
            if (scenario == 0)
                for (const auto language : { i18n::Language::english, i18n::Language::japanese })
                {
                    const i18n::ScopedLanguage scoped (language);
                    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (post.createEditorIfNeeded());
                    editor->setSize (900, 600); editor->setVisible (true); candidateTick (*editor);
                    auto* button = dynamic_cast<juce::Button*> (candidateControl (*editor, "observatory-candidate-compare"));
                    require (button && button->onClick, "PRE1 refusal uses the actual A/B entry");
                    const auto candidates = post.enumeratePreCandidatesForMenu();
                    juce::String label;
                    for (const auto& entry : candidates)
                        if (entry.instanceId == pre2.instanceId()) label = pair_labels::menuLabel (candidates, entry);
                    require (label.isNotEmpty() && juce::MessageManager::callAsync ([button] { button->onClick(); }),
                             "open the PRE2 choice");
                    auto& modal = *juce::ModalComponentManager::getInstance();
                    require (afterQueuedMessages ([&]
                    {
                        require (modal.getNumModalComponents() == 1
                            && candidatePressMenuItem (*modal.getModalComponent (0), label), "choose PRE2 in the actual menu");
                    }), "deliver PRE2 selection");
                    require (afterQueuedMessages ([&]
                    {
                        require (modal.getNumModalComponents() == 1
                            && candidatePressMenuItem (*modal.getModalComponent (0), text_style::shownText ("START A/B")),
                            "confirm A/B in the actual menu");
                    }), "deliver placement confirmation");
                    require (afterQueuedMessages ([] {}), "deliver the refused A/B start");
                    const auto* view = candidateView (*editor);
                    require (view && text_style::shownText (view->feedback()) == text_style::shownText ("PRE is multi-mono: insert it as stereo")
                        && !post.candidateCompareActive(), "editor explains the PRE1 multi-mono refusal in both languages");
                    post.editorBeingDeleted (editor.get());
                }
            render (80);
            for (auto& processor : processors) { awaitAnalysisInput (*processor); awaitAnalysisExchange (*processor, true); }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (10);
            bool kept = post.keepPair();
            while (!kept && std::chrono::steady_clock::now() < deadline)
            { render (1); std::this_thread::sleep_for (std::chrono::milliseconds (100)); kept = post.keepPair(); }
            require (kept, "normal Keep succeeds after refusal");
            for (int i = 0; i < 48 && !post.isRecording(); ++i)
            { render (1); for (auto& processor : processors) awaitAnalysisExchange (*processor, true); }
            require (post.isRecording(), "normal Keep reaches Record after refusal");
            post.stopPair(); render (20);
        }
        for (auto& processor : processors) processor->releaseResources();
        if (extra) extra->releaseResources();
    }
}
}
