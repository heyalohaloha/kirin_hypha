#include <juce_audio_processors/juce_audio_processors.h>
#include "WindowsCpuObservation.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <process.h>
#include <string>
#include <vector>

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

class ValidationStorageSandbox
{
public:
    ValidationStorageSandbox()
    {
        root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getNonexistentChildFile ("kirin-hypha-audio-transparency-"
                                             + juce::String (::_getpid()), {}, false);
        const auto result = root.createDirectory();
        if (result.failed())
            fail ("could not create isolated storage: " + result.getErrorMessage().toStdString());
        // Windows Watch snapshots use TEMP, identity uses APPDATA, Record uses
        // LOCALAPPDATA. Isolating only the latter leaves discovery in the user's tree.
        for (const auto* name : { "LOCALAPPDATA", "APPDATA", "TEMP", "TMP" })
        {
            SavedVariable saved { name, {}, false };
            char* current = nullptr;
            size_t length = 0;
            if (::_dupenv_s (&current, &length, name) == 0 && current != nullptr)
            {
                saved.value = current;
                saved.present = true;
                std::free (current);
            }
            variables.push_back (std::move (saved));
            const auto destination = root.getChildFile (
                juce::String (name) == "TMP" ? "TEMP" : name);
            if (destination.createDirectory().failed()
                || ::_putenv_s (name, destination.getFullPathName().toRawUTF8()) != 0)
                fail ("could not redirect validation storage");
        }
    }

    ~ValidationStorageSandbox()
    {
        for (const auto& saved : variables)
            ::_putenv_s (saved.name.c_str(), saved.present ? saved.value.c_str() : "");
        root.deleteRecursively();
    }

private:
    struct SavedVariable { std::string name, value; bool present; };
    juce::File root;
    std::vector<SavedVariable> variables;
};

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
    const auto mixed = static_cast<uint32_t> ((blockIndex + 1) * 0x9e3779b9u)
                     ^ static_cast<uint32_t> ((channel + 3) * 0x85ebca6bu)
                     ^ static_cast<uint32_t> ((frame + 11) * 0xc2b2ae35u);
    const auto signedValue = static_cast<int32_t> (mixed & 0xffffu) - 32'768;
    return static_cast<float> (signedValue) / 65'536.0f;
}

std::unique_ptr<juce::AudioPluginInstance> createInstance (
    juce::VST3PluginFormat& format,
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

void verifyConfiguration (juce::VST3PluginFormat& format,
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

void verifyBundle (const juce::String& path)
{
    const juce::File bundle (path);
    if (! bundle.exists())
        fail ("VST3 bundle does not exist: " + path.toStdString());

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile (descriptions, bundle.getFullPathName());
    if (descriptions.size() != 1)
        fail ("expected one component in " + path.toStdString() + ", found "
              + std::to_string (descriptions.size()));

    verifyConfiguration (format, *descriptions[0], 2, false);
    verifyConfiguration (format, *descriptions[0], 2, true);
    verifyConfiguration (format, *descriptions[0], 1, false);
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc != 3 && argc != 5)
        fail ("usage: KirinAudioTransparencyContractTests <PRE.vst3> <POST.vst3> [--cpu-observation PAIRS]");

    ValidationStorageSandbox sandbox;
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    if (argc == 5)
    {
        if (std::string (argv[3]) != "--cpu-observation")
            fail ("unknown diagnostic option");
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
    verifyBundle (juce::String::fromUTF8 (argv[1]));
    verifyBundle (juce::String::fromUTF8 (argv[2]));
    std::cout << "PASS PRE/POST VST3 audio transparency contract\n";
    return EXIT_SUCCESS;
}
