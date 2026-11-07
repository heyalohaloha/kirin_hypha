#pragma once
#include "../src/PluginProcessor.h"
#include "TestProcessorStorage.h"
#include "AaxStateHostFixture.h"
#include "EditorProductChecks.h"

namespace hypha::tests::editor_product
{
struct AlignMenuChoice { using Type = void (KirinHyphaEditor::*) (int); friend Type privateMember (AlignMenuChoice); };
template struct PrivateAccess<AlignMenuChoice, &KirinHyphaEditor::handleOperationsMenu>;
struct AlignMenu { using Type = juce::PopupMenu (KirinHyphaEditor::*)(); friend Type privateMember (AlignMenu); };
template struct PrivateAccess<AlignMenu, &KirinHyphaEditor::operationsMenu>;
}

// Processor serialization and actual AAX wrapper callbacks, including both host restore orders.
// Output selection is deliberately not part of these settings.
template <typename Require>
void verifyAutoAlignState (juce::AudioProcessor::WrapperType wrapper, Require require)
{
    using Processor = KirinHyphaProcessorBase;
    struct Host final : juce::AudioProcessorListener
    {
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
        {
            if (! details.nonParameterStateChanged) return;
            ++changes;
        }
        int changes = 0;
    } host;
    for (auto role : { Processor::Role::Pre, Processor::Role::Post })
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
        Processor original (role);
        juce::MemoryBlock defaults;
        original.getStateInformation (defaults);
        const auto defaultXml = juce::AudioProcessor::getXmlFromBinary (defaults.getData(), static_cast<int> (defaults.getSize()));
        require (defaultXml && !defaultXml->hasAttribute ("live_auto_align"), "default ON is omitted from state");
        auto layout = original.getBusesLayout();
        layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
        layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
        require (original.setBusesLayout (layout), "the host negotiates the stereo MENU workflow");
        hypha::tests::prepareFixtureProcessor (original, 48000, 1024);
        std::unique_ptr<hypha::tests::aax_state::Host> aax;
        if (wrapper == juce::AudioProcessor::wrapperType_AAX)
            aax = std::make_unique<hypha::tests::aax_state::Host> (original);
        std::int32_t initialChanges = 0;
        if (aax) aax->GetNumberOfChanges (&initialChanges);
        original.addListener (&host);
        const auto before = host.changes;
        std::unique_ptr<KirinHyphaEditor> editor;
        if (role == Processor::Role::Post)
        {
            using namespace hypha::tests::editor_product;
            editor.reset (dynamic_cast<KirinHyphaEditor*> (original.createEditorIfNeeded()));
            require (editor != nullptr, "OFF is chosen in the shipping editor");
            const auto menu = (editor.get()->*privateMember (AlignMenu {}))();
            bool offered = false;
            for (juce::PopupMenu::MenuItemIterator item (menu, true); item.next();)
                if (item.getItem().itemID == 44) offered = item.getItem().isEnabled && item.getItem().isTicked;
            require (offered, "MENU offers the checked default alignment setting");
            (editor.get()->*privateMember (AlignMenuChoice {})) (44);
            require (!original.liveCompareAutoAlign(), "MENU OFF reaches the actual processor preference");
            original.editorBeingDeleted (editor.get());
            editor.reset();
        }
        else original.setLiveCompareAutoAlign (false);
        original.setLiveCompareAutoAlign (false);
        require (host.changes == before + 1, "OFF marks non-parameter state dirty exactly once");
        original.removeListener (&host);
        juce::MemoryBlock saved;
        if (aax)
        {
            std::int32_t changes = 0;
            require (aax->GetNumberOfChanges (&changes) == 0 && changes == initialChanges + 2,
                     "one OFF callback increments the SDK base counter and the wrapper's dirty counter");
            saved = hypha::tests::aax_state::save (*aax, require);
        }
        else original.getStateInformation (saved);
        const auto offXml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
        require (offXml && offXml->hasAttribute ("live_auto_align") && !offXml->getBoolAttribute ("live_auto_align", true),
                 "the host's saved chunk contains explicit OFF");
        for (bool prepareFirst : { false, true })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
            Processor restored (role);
            if (prepareFirst) hypha::tests::prepareFixtureProcessor (restored, 48000, 1024);
            if (aax) hypha::tests::aax_state::restore (restored, saved, require);
            else restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
            if (!prepareFirst) hypha::tests::prepareFixtureProcessor (restored, 48000, 1024);
            require (!restored.liveCompareAutoAlign(), "OFF survives a fresh processor and either prepare order");
            juce::MemoryBlock again;
            restored.getStateInformation (again);
            const auto xml = juce::AudioProcessor::getXmlFromBinary (again.getData(), static_cast<int> (again.getSize()));
            require (xml && !xml->getBoolAttribute ("live_auto_align", true), "the reopened OFF state saves as OFF again");
            restored.setStateInformation (defaults.getData(), static_cast<int> (defaults.getSize()));
            require (restored.liveCompareAutoAlign(), "an older state without the setting restores ON");
            require (!restored.liveCompareStatus().active, "restoring settings cannot grant output authority");
            restored.releaseResources();
        }
        original.releaseResources();
    }
}
