#pragma once
#include "TestProcessorStorage.h"

namespace hypha::tests
{
inline void verifyFixtureProcessorStorage()
{
    juce::ScopedJuceInitialiser_GUI initialise;
    KirinHyphaProcessorBase pre (KirinHyphaProcessorBase::Role::Pre);
    KirinHyphaProcessorBase post (KirinHyphaProcessorBase::Role::Post);
    prepareFixtureProcessor (pre, 48000, 512);
    prepareFixtureProcessor (post, 48000, 512);
    const auto first = pre.instanceId(), second = post.instanceId();
    const auto check = [] (bool ok)
    { if (! ok) throw std::runtime_error ("fixture instance identity contract failed"); };
    check (! fixtureStorageReady (pre) && ! fixtureStorageReady (post));
    check (first.isNotEmpty() && first.length() <= 5 && second.length() <= 5 && first != second);
    pre.releaseResources();
    prepareFixtureProcessor (pre, 48000, 512);
    check (pre.instanceId() == first);
    // Explicit saved IDs, including the full legal boundary, must never be shortened.
    for (const auto& id : { juce::String ("11111111-1111-4111-8111-111111111111"),
                            juce::String::repeatedString ("x", 64) })
    {
        juce::XmlElement xml ("KirinHyphaState");
        xml.setAttribute ("instance_id", id);
        juce::MemoryBlock bytes;
        juce::AudioProcessor::copyXmlToBinary (xml, bytes);
        post.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
        post.releaseResources();
        prepareFixtureProcessor (post, 48000, 512);
        check (post.instanceId() == id);
    }
}
}
