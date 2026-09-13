#include <juce_audio_processors/juce_audio_processors.h>
#include "ValidationStorageSandbox.h"
#if JUCE_WINDOWS
 #include "WindowsCpuObservation.h"
#endif

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#if JUCE_WINDOWS
 #include <process.h>
#else
 #include <unistd.h>
#endif

namespace
{
constexpr double contractSampleRateHz = 48'000.0;
constexpr int maximumBlockSize = 1'024;
constexpr int passesPerConfiguration = 16;
constexpr std::array<int, 6> blockSizes { 1, 17, 64, 255, 512, maximumBlockSize };

[[noreturn]] void fail (const std::string& message)
{
    std::cerr << "Audio transparency contract failed: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

class ContractPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo position;
        position.setIsPlaying (true);
        position.setTimeInSamples (samplePosition);
        position.setTimeInSeconds (static_cast<double> (samplePosition) / contractSampleRateHz);
        return position;
    }

    void advance (int frames) noexcept
    {
        samplePosition += frames;
    }

private:
    int64_t samplePosition = 0;
};

uint32_t sampleBits (float value) noexcept
{
    uint32_t bits = 0;
    static_assert (sizeof (bits) == sizeof (value));
    std::memcpy (&bits, &value, sizeof (bits));
    return bits;
}

float contractSample (int channel, int frame, int blockIndex) noexcept
{
    const auto mixed = static_cast<uint32_t> (blockIndex + 1) * 0x9e3779b9u
                     ^ static_cast<uint32_t> (channel + 3) * 0x85ebca6bu
                     ^ static_cast<uint32_t> (frame + 11) * 0xc2b2ae35u;
    const auto signedValue = static_cast<int32_t> (mixed & 0xffffu) - 32'768;
    return static_cast<float> (signedValue) / 65'536.0f;
}

std::unique_ptr<juce::AudioPluginInstance> createInstance (
    juce::AudioPluginFormat& format,
    const juce::PluginDescription& description)
{
    juce::String error;
    auto instance = format.createInstanceFromDescription (
        description, contractSampleRateHz, maximumBlockSize, error);
    if (instance == nullptr)
        fail ("could not instantiate " + description.fileOrIdentifier.toStdString()
              + ": " + error.toStdString());
    return instance;
}

void verifyConfiguration (juce::AudioPluginFormat& format,
                          const juce::PluginDescription& description,
                          int channels,
                          bool offline)
{
    auto instance = createInstance (format, description);
    const auto channelSet = channels == 1 ? juce::AudioChannelSet::mono()
                                          : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (channelSet);
    layout.outputBuses.add (channelSet);
    if (! instance->setBusesLayout (layout))
        fail (description.name.toStdString() + " rejected "
              + std::to_string (channels) + "-channel matching I/O");
    if (instance->getTotalNumInputChannels() != channels
        || instance->getTotalNumOutputChannels() != channels)
        fail (description.name.toStdString() + " did not expose matching I/O");

    instance->setNonRealtime (offline);
    instance->setProcessingPrecision (juce::AudioProcessor::singlePrecision);
    instance->prepareToPlay (contractSampleRateHz, maximumBlockSize);
    if (instance->getLatencySamples() != 0)
        fail (description.name.toStdString() + " reported non-zero latency");

    if (auto* bypass = instance->getBypassParameter())
        bypass->setValueNotifyingHost (0.0f);

    ContractPlayHead playHead;
    instance->setPlayHead (&playHead);
    juce::MidiBuffer midi;
    int blockIndex = 0;
    int64_t verifiedSamples = 0;

    for (int pass = 0; pass < passesPerConfiguration; ++pass)
    {
        for (const auto frames : blockSizes)
        {
            juce::AudioBuffer<float> buffer (channels, frames);
            std::vector<uint32_t> expected;
            expected.reserve (static_cast<size_t> (channels * frames));
            for (int channel = 0; channel < channels; ++channel)
            {
                auto* samples = buffer.getWritePointer (channel);
                for (int frame = 0; frame < frames; ++frame)
                {
                    samples[frame] = contractSample (channel, frame, blockIndex);
                    expected.push_back (sampleBits (samples[frame]));
                }
            }

            midi.clear();
            instance->processBlock (buffer, midi);

            size_t index = 0;
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto* samples = buffer.getReadPointer (channel);
                for (int frame = 0; frame < frames; ++frame, ++index)
                {
                    const auto actual = sampleBits (samples[frame]);
                    if (actual != expected[index])
                    {
                        std::cerr << std::hex << std::setfill ('0')
                                  << "expected=0x" << std::setw (8) << expected[index]
                                  << " actual=0x" << std::setw (8) << actual << std::dec << '\n';
                        fail (description.name.toStdString() + " changed channel "
                              + std::to_string (channel) + " frame " + std::to_string (frame)
                              + " in block " + std::to_string (blockIndex));
                    }
                }
            }

            playHead.advance (frames);
            verifiedSamples += static_cast<int64_t> (channels) * frames;
            ++blockIndex;
        }
    }

    instance->setPlayHead (nullptr);
    instance->releaseResources();
    if (instance->getLatencySamples() != 0)
        fail (description.name.toStdString() + " changed latency after processing");

    std::cout << "PASS " << description.name << ' '
              << channels << "ch " << (offline ? "offline" : "realtime")
              << " samples=" << verifiedSamples << " latency=0 bit-identical\n";
}

struct Vst3State
{
    std::unique_ptr<juce::XmlElement> wrapper;
    std::unique_ptr<juce::XmlElement> plugin;
};

Vst3State readVst3State (juce::AudioProcessor& instance)
{
    juce::MemoryBlock state;
    instance.getStateInformation (state);
    auto wrapper = juce::AudioProcessor::getXmlFromBinary (
        state.getData(), static_cast<int> (state.getSize()));
    if (wrapper == nullptr || ! wrapper->hasTagName ("VST3PluginState"))
        fail (instance.getName().toStdString() + " did not publish VST3 host state");
    auto* component = wrapper->getChildByName ("IComponent");
    juce::MemoryBlock pluginState;
    if (component == nullptr || ! pluginState.fromBase64Encoding (component->getAllSubText()))
        fail (instance.getName().toStdString() + " did not publish VST3 component state");
    auto plugin = juce::AudioProcessor::getXmlFromBinary (
        pluginState.getData(), static_cast<int> (pluginState.getSize()));
    if (plugin == nullptr || ! plugin->hasTagName ("KirinHyphaState"))
        fail (instance.getName().toStdString() + " did not publish KirinHyphaState");
    return { std::move (wrapper), std::move (plugin) };
}

juce::MemoryBlock writeVst3State (Vst3State state)
{
    juce::MemoryBlock pluginState;
    juce::AudioProcessor::copyXmlToBinary (*state.plugin, pluginState);
    for (const auto* sectionName : { "IComponent", "IEditController" })
    {
        if (auto* section = state.wrapper->getChildByName (sectionName))
        {
            section->deleteAllChildElements();
            section->addTextElement (pluginState.toBase64Encoding());
        }
    }
    juce::MemoryBlock wrapperState;
    juce::AudioProcessor::copyXmlToBinary (*state.wrapper, wrapperState);
    return wrapperState;
}

void verifyVst3HostStatePersistence (juce::AudioPluginFormat& format,
                                     const juce::PluginDescription& description)
{
    auto source = createInstance (format, description);
    auto fresh = readVst3State (*source);
    if (fresh.plugin->getIntAttribute ("meter_context", -1) != 1)
        fail (description.name.toStdString() + " fresh instance did not default to 2MIX");
    fresh.plugin->setAttribute ("display_state_version", 5);
    fresh.plugin->setAttribute ("meter_context", 0);
    fresh.plugin->setAttribute ("scale_mode", 0);
    fresh.plugin->setAttribute ("observatory_size", 3);
    fresh.plugin->setAttribute ("observatory_width", 654);
    fresh.plugin->setAttribute ("observatory_height", 436);
    const auto trackState = writeVst3State (std::move (fresh));
    source->setStateInformation (trackState.getData(), static_cast<int> (trackState.getSize()));
    const auto applied = readVst3State (*source);
    if (applied.plugin->getIntAttribute ("meter_context", -1) != 0
        || applied.plugin->getIntAttribute ("observatory_width", -1) != 654
        || applied.plugin->getIntAttribute ("observatory_height", -1) != 436)
        fail (description.name.toStdString() + " rejected host-provided TRACK/STEM editor state");

    auto* appliedEditor = source->createEditorIfNeeded();
    if (appliedEditor == nullptr)
        fail (description.name.toStdString() + " did not create its shipped VST3 editor");
    if (appliedEditor->getWidth() != 654 || appliedEditor->getHeight() != 436)
    {
        const auto afterEditor = readVst3State (*source);
        fail (description.name.toStdString() + " serialized size was not applied to the VST3 editor: "
              + std::to_string (appliedEditor->getWidth()) + "x"
              + std::to_string (appliedEditor->getHeight()) + " component-state="
              + std::to_string (afterEditor.plugin->getIntAttribute ("observatory_width", -1))
              + "x"
              + std::to_string (afterEditor.plugin->getIntAttribute ("observatory_height", -1)));
    }
    const auto afterAppliedEditor = readVst3State (*source);
    if (afterAppliedEditor.plugin->getIntAttribute ("meter_context", -1) != 0)
        fail (description.name.toStdString()
              + " editor creation replaced restored TRACK/STEM with 2MIX");
    delete appliedEditor;

    juce::MemoryBlock saved;
    source->getStateInformation (saved);
    if (saved.isEmpty())
        fail (description.name.toStdString() + " did not save TRACK/STEM and the free editor size");
    auto savedState = readVst3State (*source);
    if (savedState.plugin->getIntAttribute ("meter_context", -1) != 0
        || savedState.plugin->getIntAttribute ("observatory_width", -1) != 654
        || savedState.plugin->getIntAttribute ("observatory_height", -1) != 436)
        fail (description.name.toStdString() + " did not save TRACK/STEM and the free editor size");

    auto restored = createInstance (format, description);
    restored->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    const auto restoredState = readVst3State (*restored);
    if (restoredState.plugin->getIntAttribute ("meter_context", -1) != 0
        || restoredState.plugin->getIntAttribute ("observatory_width", -1) != 654
        || restoredState.plugin->getIntAttribute ("observatory_height", -1) != 436)
        fail (description.name.toStdString() + " changed TRACK/STEM or editor size after host reload");

    auto* restoredEditor = restored->createEditorIfNeeded();
    if (restoredEditor == nullptr)
        fail (description.name.toStdString() + " did not recreate its shipped VST3 editor");
    if (restoredEditor->getWidth() != 654 || restoredEditor->getHeight() != 436)
        fail (description.name.toStdString() + " did not restore the VST3 editor to 654x436: "
              + std::to_string (restoredEditor->getWidth()) + "x"
              + std::to_string (restoredEditor->getHeight()));
    const auto afterRestoredEditor = readVst3State (*restored);
    if (afterRestoredEditor.plugin->getIntAttribute ("meter_context", -1) != 0)
        fail (description.name.toStdString()
              + " reloaded editor replaced restored TRACK/STEM with 2MIX");
    delete restoredEditor;

    std::cout << "PASS " << description.name
              << " VST3 host-state fresh=2MIX restored=TRACK/STEM size=654x436\n";
}

void verifyBundle (juce::AudioPluginFormat& format,
                   const juce::String& path,
                   const char* formatName)
{
    const juce::File bundle (path);
    if (! bundle.exists())
        fail (std::string (formatName) + " bundle does not exist: " + path.toStdString());

    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile (descriptions, bundle.getFullPathName());
    if (descriptions.size() != 1)
        fail ("expected one component in " + path.toStdString() + ", found "
              + std::to_string (descriptions.size()));

    verifyConfiguration (format, *descriptions[0], 2, false);
    verifyConfiguration (format, *descriptions[0], 2, true);
    verifyConfiguration (format, *descriptions[0], 1, false);
    if (std::string (formatName) == "VST3")
        verifyVst3HostStatePersistence (format, *descriptions[0]);
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc != 3 && argc != 5)
        fail ("usage: KirinAudioTransparencyContractTests <PRE.vst3> <POST.vst3> "
              "[<PRE.component> <POST.component>|--cpu-observation PAIRS]");

    ValidationStorageSandbox sandbox;
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    if (argc == 5)
    {
       #if JUCE_WINDOWS
        if (std::string (argv[3]) == "--cpu-observation")
        {
            try
            {
                const std::string count (argv[4]);
                size_t consumed = 0;
                const int pairs = std::stoi (count, &consumed);
                if (consumed != count.size()) fail ("invalid diagnostic pair count");
                hypha::validation::runCpuObservation (
                    juce::String::fromUTF8 (argv[1]), juce::String::fromUTF8 (argv[2]), pairs);
            }
            catch (const std::exception& error) { fail (error.what()); }
            return EXIT_SUCCESS;
        }
       #endif
    }

    juce::VST3PluginFormat vst3;
    verifyBundle (vst3, juce::String::fromUTF8 (argv[1]), "VST3");
    verifyBundle (vst3, juce::String::fromUTF8 (argv[2]), "VST3");

    if (argc == 5)
    {
       #if JUCE_MAC
        juce::AudioUnitPluginFormat audioUnit;
        verifyBundle (audioUnit, juce::String::fromUTF8 (argv[3]), "AU");
        verifyBundle (audioUnit, juce::String::fromUTF8 (argv[4]), "AU");
       #else
        fail ("AU bundle arguments are supported on macOS only");
       #endif
    }

    std::cout << "PASS PRE/POST audio transparency contract\n";
    return EXIT_SUCCESS;
}
