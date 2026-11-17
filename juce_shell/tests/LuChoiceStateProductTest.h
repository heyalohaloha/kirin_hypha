#pragma once
#include "../src/PluginProcessor.h"
#include "TestProcessorStorage.h"

// The VU's LU face and its window are saved with the project and come back on reopening, in PRE
// and POST under every wrapper. Reference need not be readable for the choice to be kept.
template <typename Require>
void verifyLuChoiceState (juce::AudioProcessor::WrapperType wrapper, Require require)
{
    using Processor = KirinHyphaProcessorBase;
    using hypha::lu_needle::Window;
    struct Host final : juce::AudioProcessorListener
    {
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
        { if (details.nonParameterStateChanged) ++changes; }
        int changes = 0;
    } host;
    const auto xmlOf = [] (Processor& processor)
    {
        juce::MemoryBlock block;
        processor.getStateInformation (block);
        return juce::AudioProcessor::getXmlFromBinary (block.getData(), static_cast<int> (block.getSize()));
    };
    for (auto role : { Processor::Role::Pre, Processor::Role::Post })
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
        Processor original (role);
        juce::MemoryBlock defaults;
        original.getStateInformation (defaults);
        const auto defaultXml = xmlOf (original);
        require (defaultXml && ! defaultXml->hasAttribute ("vu_lu_choice"), "the VU face is the default and is not written");
        require (original.luChoicePreference() == 0, "a new plug-in shows the VU face");
        original.addListener (&host);
        const auto before = host.changes;
        const auto chosen = hypha::lu_needle::packChoice (true, Window::shortTerm);
        original.setLuChoicePreference (chosen);
        original.setLuChoicePreference (chosen);
        require (host.changes == before + 1, "choosing the LU face marks the project changed once");
        original.removeListener (&host);
        juce::MemoryBlock saved;
        original.getStateInformation (saved);
        const auto savedXml = xmlOf (original);
        require (savedXml && savedXml->getIntAttribute ("vu_lu_choice", -1) == chosen, "the saved project holds the face and window");
        for (bool prepareFirst : { false, true })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
            Processor restored (role);
            if (prepareFirst) hypha::tests::prepareFixtureProcessor (restored, 48000, 1024);
            restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
            if (! prepareFirst) hypha::tests::prepareFixtureProcessor (restored, 48000, 1024);
            const auto packed = restored.luChoicePreference();
            require (hypha::lu_needle::packedLuFace (packed) && hypha::lu_needle::packedWindow (packed) == Window::shortTerm,
                     "reopening returns the LU face and its window");
            const auto again = xmlOf (restored);
            require (again && again->getIntAttribute ("vu_lu_choice", -1) == chosen, "the reopened choice is saved again");
            for (int oldChoice : { 4, 5 })
            {
            auto legacy = xmlOf (original);
            require (legacy != nullptr, "legacy fixture serializes to XML");
            legacy->setAttribute ("vu_lu_choice", oldChoice); // old LU / I selection
            juce::MemoryBlock legacyBytes;
            juce::AudioProcessor::copyXmlToBinary (*legacy, legacyBytes);
            restored.setStateInformation (legacyBytes.getData(), static_cast<int> (legacyBytes.getSize()));
            require (restored.luChoicePreference() == oldChoice - 4, "legacy I opens on M and keeps its face");
            const auto restoredLegacy = xmlOf (restored);
            require (restoredLegacy != nullptr, "restored legacy choice serializes to XML");
            require (restoredLegacy && restoredLegacy->getIntAttribute ("vu_lu_choice", 0) == oldChoice - 4, "legacy selection saves as M");
            }
            restored.setStateInformation (defaults.getData(), static_cast<int> (defaults.getSize()));
            require (restored.luChoicePreference() == 0, "a project saved before the LU face opens on the VU");
            restored.releaseResources();
        }
    }
}
