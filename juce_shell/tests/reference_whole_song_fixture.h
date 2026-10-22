#pragma once
#include "ReferenceControllerTestAccess.h"
#include "ReferenceWholeSongAudioFixture.h"
namespace
{
    [[maybe_unused]] void observeWholeSongFixture (ref::ReferenceComparisonController& controller,
                                  const WholeSongFixture& fixture, int& position, int channels = 2)
    {
        constexpr int frames = wholeSongObservationFrames;
        if (position + frames > fixture.audio.getNumSamples()) position = 0;
        juce::AudioBuffer<float> input (channels, frames);
        for (int channel = 0; channel < channels; ++channel)
            input.copyFrom (channel, 0, fixture.audio, std::min (channel, fixture.audio.getNumChannels() - 1), position, frames);
        controller.observeTransport (position, true, true);
        controller.observeAInput (input, position, true, true, true);
        position += frames;
    }

    [[maybe_unused]] void exerciseWholeSongLibraryTrial (ref::ReferenceComparisonController& controller,
                                        const WholeSongFixture& fixture, const juce::File& root)
    {
        controller.observeTransport (0, true, true);
        hypha::tests::reference_control::await ([&] { return controller.snapshot().blindEligible; }, "Version preparation completes before Blind");
        require (controller.selectB (-22, -6), "normal Version B can lead directly into Blind");
        require (controller.startBlind (-22, -6), "whole-song Library Blind starts after acoustic confirmation");
        require (! controller.reserveLocalBlind(), "a VERSION BLIND started from a playing V keeps other Blinds out");
        require (!controller.startBlind (-22, -6) && !controller.approveBlindLowerAAndStart (-22, -6),
            "repeated start actions cannot interrupt or replace the active trial");
        require (!controller.answerBlind (1), "an unheard trial cannot be answered");
        juce::AudioBuffer<float> input (2, 1024);
        for (int stimulus = 1; stimulus <= 2; ++stimulus)
        {
            if (stimulus == 2) require (controller.selectBlindStimulus (stimulus), "whole-song hidden stimulus switch");
            for (int callback = 0; callback < 155; ++callback)
            {
                const auto position = callback * input.getNumSamples();
                for (int channel = 0; channel < 2; ++channel)
                    input.copyFrom (channel, 0, fixture.audio, channel, position, input.getNumSamples());
                controller.observeTransport (position, true, true);
                controller.observeAInput (input, position, true, true, true);
                require (controller.renderSelectedB (input, position, true, true, true), "whole-song audition callback");
                juce::Thread::sleep (5);
            }
            controller.observeTransport (0, false, false);
            input.clear();
            require (controller.renderSelectedB (input, 0, false, true, true), "host pause without a sample clock preserves the whole-song trial");
            // The pause must cross the callback timeout; this duration is test input, not a settle deadline.
            const auto pausedUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds (600);
            hypha::tests::reference_control::await ([&] {
                const auto state = controller.snapshot();
                return std::chrono::steady_clock::now() >= pausedUntil && ! state.transportPlaying;
            }, "the worker observes a pause beyond the callback timeout");
            require (controller.snapshot().blindPhase == ref::BlindPhase::active, "a paused trial survives the callback timeout");
            controller.observeTransport (0, true, true);
        }
        require (controller.answerBlind (1) && controller.revealBlind(), "audible whole-song preference completes");
        controller.observeTransport (0, false, false);
        controller.endBlind();
        hypha::tests::reference_control::await ([&] { return controller.snapshot().blindPhase == ref::BlindPhase::inactive; }, "END completes before inspecting the journal");
        require (controller.snapshot().blindPhase == ref::BlindPhase::inactive, "END during pause returns to the normal screen without another play click");
        int starts = 0, completions = 0;
        for (const auto& file : root.getChildFile ("library/events").findChildFiles (juce::File::findFiles, true, "*.json"))
        {
            const auto event = juce::JSON::parse (file);
            const auto type = event["event_type"].toString();
            if (type == "blind_compare_started") {
                ++starts;
                require (event["work_id"].isVoid() && event["payload"]["trial_start"]["sources"]["a"]["source_kind"] == "live_daw",
                         "whole-song journal never assigns B's Work to live A");
            }
            if (type == "blind_compare_completed") ++completions;
        }
        require (starts == 1 && completions == 1, "one immutable start/completion pair reaches the Library journal");
        const auto exportPath = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_WHOLE_SONG_EXPORT", {});
        if (exportPath.isNotEmpty())
            require (root.copyDirectoryTo (juce::File (exportPath)), "cross-language whole-song journal fixture export");

        controller.observeTransport (0, true, true);
        hypha::tests::reference_control::await ([&] { return controller.snapshot().blindEligible; }, "Version preparation completes before Blind");
        require (controller.startBlind (-22, -6), "an answer-free whole-song trial can start");
        for (int stimulus = 1; stimulus <= 2; ++stimulus)
        {
            if (stimulus == 2) require (controller.selectBlindStimulus (2), "switch to second anonymous source");
            for (int callback = 0; callback < 155; ++callback)
            {
                const auto position = callback * input.getNumSamples();
                for (int channel = 0; channel < 2; ++channel)
                    input.copyFrom (channel, 0, fixture.audio, channel, position, input.getNumSamples());
                controller.observeTransport (position, true, true);
                controller.observeAInput (input, position, true, true, true);
                require (controller.renderSelectedB (input, position, true, true, true),
                         "answer-free trial must render both sources");
                juce::Thread::sleep (5);
            }
        }
        require (controller.revealBlind() && controller.snapshot().answeredBlindStimulus == 0,
                 "one reveal discloses both sources without recording a preference");
        controller.observeTransport (0, false, false);
        controller.endBlind();
        hypha::tests::reference_control::await ([&] { return controller.snapshot().blindPhase == ref::BlindPhase::inactive; }, "END completes before inspecting the journal");
        starts = completions = 0;
        for (const auto& file : root.getChildFile ("library/events").findChildFiles (juce::File::findFiles, true, "*.json"))
        {
            const auto type = juce::JSON::parse (file)["event_type"].toString();
            if (type == "blind_compare_started") ++starts;
            if (type == "blind_compare_completed") ++completions;
        }
        require (starts == 2 && completions == 1,
                 "unanswered reveal records Start only and never fabricates a Completed answer");
    }
}
