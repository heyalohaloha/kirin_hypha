#pragma once
#include "../src/HyphaLiveBlindMemory.h"
#include "ShortcutTestAccess.h"
#include "TestProcessorStorage.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY
namespace hypha::tests
{
// The real editor reads the plugin's own BLIND trial each time it opens. Closing the window keeps it, the saved
// state never holds it, and a restored state or END clears it without any window.
inline void verifyBlindTrialAcrossWindows()
{
    const auto require = [] (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); };
    using Processor = KirinHyphaProcessorBase;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (Processor::Role::Post);
    prepareFixtureProcessor (processor, 48000, 256);
    const auto memory = processor.liveBlindTrialMemory();
    require (memory != nullptr && processor.liveBlindTrialMemory() == memory, "one memory for the plugin's life");
    const auto fill = [&memory] {
        *memory = {};
        memory->activeRounds = 5; memory->generation = 7; memory->epoch = 3;
        memory->preference.begin (5, 7, 3);
        memory->preference.observePlayed (7, 3, 3);
        memory->preference.answer (1, true);
    };
    const auto opened = [&processor] (auto&& inspect) {
        std::unique_ptr<juce::AudioProcessorEditor> owner (processor.createEditorIfNeeded());
        auto* editor = dynamic_cast<KirinHyphaEditor*> (owner.get());
        if (editor == nullptr) throw std::runtime_error ("editor fixture unavailable");
        inspect (editor->*shortcutMember (ShortcutLiveBlindView {}));
        processor.editorBeingDeleted (owner.get());
    };
    juce::MemoryBlock empty, filled;
    processor.getStateInformation (empty);
    fill();
    processor.getStateInformation (filled);
    require (empty == filled, "the saved state never holds a trial");
    for (int again = 0; again < 2; ++again)
        opened ([&] (live_blind_ui::Component& view) {
            require (view.activePreferenceRounds() == 5 && view.trialSummary().total() == 1, "the reopened window reads the kept trial");
        });
    require (memory->holds (7, 3) && memory->preference.answered() == 1, "closing the window keeps the trial");
    processor.setStateInformation (empty.getData(), static_cast<int> (empty.getSize()));
    processor.serviceLiveCompare();
    require (memory->activeRounds == 0 && memory->preference.answered() == 0, "a restored state clears the trial");
    fill();
    processor.forgetBlindTrial();
    opened ([&] (live_blind_ui::Component& view) {
        require (view.activePreferenceRounds() == 0 && view.trialSummary().total() == 0, "a cleared trial never returns");
    });
    processor.releaseResources();
}
}
#endif
