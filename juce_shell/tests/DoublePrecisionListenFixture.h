#pragma once
#include "DoublePrecisionParityTest.h"
#include <memory>

namespace hypha::tests::precision
{
// The synthetic host supplies two channels; its processor buses must agree before prepare.
inline std::unique_ptr<Processor> makeDoublePrecisionListenProcessor (
    Processor::Role role, juce::AudioPlayHead& host)
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    auto processor = std::make_unique<Processor> (role);
    auto layout = processor->getBusesLayout();
    layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
    layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
    check (processor->setBusesLayout (layout), "double LISTEN host stereo layout is admitted");
    processor->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    processor->setPlayHead (&host);
    return processor;
}

inline void verifyDoublePrecisionListenLayout()
{
    Host host;
    for (auto role : {Processor::Role::Pre, Processor::Role::Post})
    {
        const auto processor = makeDoublePrecisionListenProcessor (role, host);
        const auto inputs = processor->getTotalNumInputChannels();
        const auto outputs = processor->getTotalNumOutputChannels();
        if (inputs != 2 || outputs != 2)
            std::cerr << "double LISTEN layout: role=" << (role == Processor::Role::Pre ? "PRE" : "POST")
                      << " input=" << inputs << " output=" << outputs << " expected=2\n";
        check (inputs == 2 && outputs == 2, "double LISTEN host and processor both use stereo");
        check (processor->isUsingDoublePrecision(), "double LISTEN prepares the double overload");
    }
    // No prepare, writes, mapping or audio worker is needed to verify this host contract.
    std::cout << "double LISTEN layout: PRE/POST stereo and double precision PASS\n";
}
}
