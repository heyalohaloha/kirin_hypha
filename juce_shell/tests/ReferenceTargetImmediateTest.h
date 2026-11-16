#pragma once
#include "../src/reference_audition/ReferenceComparisonController.h"
#include "ReferenceControllerTestAccess.h"

inline void testTargetImmediateDisplay (const juce::File& sandbox)
{
    namespace ref = hypha::reference_audition;
    namespace control = hypha::tests::reference_control;
    constexpr int rate = 48000, tick = rate / 10;
    const auto file = sandbox.getChildFile ("target-immediate.wav");
    juce::AudioBuffer<float> pcm (2, rate * 3);
    for (int channel = 0; channel < 2; ++channel)
        for (int n = 0; n < pcm.getNumSamples(); ++n)
            pcm.setSample (channel, n, 0.1f * std::sin (float (n) * 0.13f));
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (file.createOutputStream().release(), rate, 2, 24, {}, 0));
    require (writer && writer->writeFromAudioSampleBuffer (pcm, 0, pcm.getNumSamples()), "target fixture writes");
    writer.reset();
    const auto seed = ref::measureTargetFile (file, [] { return false; }).target;
    require (seed.valid(), "target fixture measures");
    for (const int count : { 0, 1, 15 })
        for (const bool make : { false, true })
        {
            ref::ReferenceComparisonController controller (sandbox.getChildFile ("immediate-" + juce::String (count) + (make ? "-make" : "-import")));
            controller.configure ({ "immediate", {}, 42, true }, rate, 2);
            controller.setPresented (true);
            for (int i = 0; i < count; ++i)
            {
                auto prior = seed; prior.id = "target-" + juce::String::toHexString (i + 1);
                require (controller.targetService().add (prior), "existing target added");
            }
            control::await ([&] { const auto state = controller.snapshot(); return state.visualTimeline && state.visualTimeline->observing; });
            juce::AudioBuffer<float> block (2, tick);
            for (int t = 0; t < 30; ++t)
            {
                for (int c = 0; c < 2; ++c) block.copyFrom (c, 0, pcm, c, t * tick, tick);
                controller.observeTransport (t * tick, true, true);
                controller.observeAInput (block, t * tick, true, true, true);
                control::await ([&] { const auto state = controller.snapshot();
                    return state.visualTimeline && state.visualTimeline->aKirin && state.visualTimeline->aKirin->frames == t + 1; });
            }
            const auto before = controller.snapshot();
            if (make) require (controller.makeTarget(), "MAKE succeeds");
            else require (controller.targetService().importFiles ({ file.getFullPathName() })
                          && controller.targetService().waitUntilIdle (30000), "import settles");
            const auto after = controller.snapshot();
            const auto facts = controller.targetService().snapshot();
            require (facts.collection.items.size() == static_cast<size_t> (count + 1), "new target is present");
            const auto& id = facts.collection.items.back().id;
            const auto overlay = after.visualPreferences->overlay();
            require (overlay.all && overlay.find (id) != nullptr, "new target immediately appears in ALL");
            for (const auto& item : facts.collection.items)
                require (overlay.find (item.id) != nullptr, "first ALL also shows the existing targets");
            require (after.audibleComparisonSlot == before.audibleComparisonSlot && after.bSelected == before.bSelected,
                     "target completion does not change the audible role");
        }
}
