#include "ClockTrace.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstring>

namespace
{
using hypha::clock_diagnostic::ClockTrace;
using hypha::clock_diagnostic::Row;
class Processor;
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override { repaint(); }
    Processor& owner;
    juce::TextButton start { "Start clock observation (one shot)" };
    juce::TextButton save { "Export immutable clock prefix" };
    juce::String result;
};
class Processor final : public juce::AudioProcessor
{
public:
    Processor() : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}
    void prepareToPlay (double value, int) override { rate = value; setLatencySamples (0); }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& b) const override
    { return b.getMainInputChannelSet() == b.getMainOutputChannelSet()
        && (b.getMainInputChannelSet() == juce::AudioChannelSet::mono()
            || b.getMainInputChannelSet() == juce::AudioChannelSet::stereo()); }
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        Row row;
        row.rate = rate;
        row.frames = static_cast<std::uint32_t> (buffer.getNumSamples());
        row.channels = static_cast<std::uint32_t> (buffer.getNumChannels());
        if (const auto* playhead = getPlayHead())
            if (const auto position = playhead->getPosition())
            {
                row.flags = (position->getIsPlaying() ? 1u : 0u) | (position->getIsLooping() ? 2u : 0u)
                          | (isNonRealtime() ? 128u : 0u);
                if (const auto v = position->getTimeInSamples()) { row.project = *v; row.flags |= 4u; }
                if (const auto v = position->getKirinAuxiliaryClockSamples()) { row.auxiliary = *v; row.flags |= 8u; }
                if (const auto v = position->getKirinInputPresentationLatencySamples()) { row.inputLatency = *v; row.flags |= 16u; }
                if (const auto v = position->getKirinOutputPresentationLatencySamples()) { row.outputLatency = *v; row.flags |= 32u; }
                row.auxiliarySource = static_cast<std::uint8_t> (position->getKirinAuxiliaryClockSource());
                row.presentationSource = static_cast<std::uint8_t> (position->getKirinPresentationLatencySource());
            }
        if (row.frames != 0 && row.channels != 0)
        {
            const auto bits = [] (float value) { std::uint32_t v; std::memcpy (&v, &value, sizeof (v)); return v; };
            row.firstLeft = bits (buffer.getSample (0, 0));
            row.lastLeft = bits (buffer.getSample (0, buffer.getNumSamples() - 1));
            if (row.channels > 1)
            {
                row.firstRight = bits (buffer.getSample (1, 0));
                row.lastRight = bits (buffer.getSample (1, buffer.getNumSamples() - 1));
            }
            row.flags |= 64u;
        }
        trace.append (row); // Read-only passthrough; no allocation, lock, file, or host call here.
    }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new Editor (*this); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    ClockTrace<> trace;
    const juce::String instance = juce::Uuid().toString();
private:
    double rate = 0;
};
Editor::Editor (Processor& processor) : AudioProcessorEditor (processor), owner (processor)
{
    addAndMakeVisible (start); addAndMakeVisible (save);
    start.onClick = [this] { owner.trace.start(); start.setEnabled (false); };
    save.onClick = [this]
    {
        const auto count = owner.trace.size();
        if (count == 0) { result = "No callbacks captured"; return; }
        // Keep explicit exports outside Documents, which can be cloud-redirected.
        auto directory = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("KirinValidation/HyphaClockDiagnostic");
        if (directory.createDirectory().failed()) { result = "Cannot create diagnostic directory"; return; }
        auto file = directory.getChildFile (owner.instance + "-" + juce::String (count) + ".csv");
        if (file.existsAsFile()) { result = "This exact prefix was already exported"; return; }
        auto stream = file.createOutputStream();
        if (! stream || ! stream->openedOk()) { result = "Cannot create trace file"; return; }
        *stream << "index,project,auxiliary,rate,frames,channels,flags,input_latency,output_latency,aux_source,presentation_source,first_left,last_left,first_right,last_right\n";
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& r = owner.trace[i];
            *stream << juce::String (i) << "," << juce::String (r.project) << "," << juce::String (r.auxiliary)
                << "," << juce::String (r.rate, 0) << "," << juce::String (r.frames) << "," << juce::String (r.channels)
                << "," << juce::String (r.flags) << "," << juce::String (r.inputLatency) << "," << juce::String (r.outputLatency)
                << "," << juce::String (r.auxiliarySource) << "," << juce::String (r.presentationSource)
                << "," << juce::String (r.firstLeft) << "," << juce::String (r.lastLeft)
                << "," << juce::String (r.firstRight) << "," << juce::String (r.lastRight) << "\n";
        }
        stream->flush();
        result = stream->getStatus().wasOk() ? "Exported to ~/KirinValidation/HyphaClockDiagnostic" : "Export failed";
    };
    setSize (500, 300); startTimerHz (4);
}
void Editor::resized()
{
    start.setBounds (20, 190, 460, 32); save.setBounds (20, 230, 460, 32);
}
void Editor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black); g.setColour (juce::Colours::white); g.setFont (16.0f);
    auto text = juce::String ("CLOCK DIAGNOSTIC ONLY / NOT HYPHA PRODUCT\n")
        + "Instance " + owner.instance.substring (0, 12) + "\n"
        + "Callbacks " + juce::String (owner.trace.size()) + " / " + juce::String (owner.trace.capacity())
        + "\nRaw clock evidence, not PDC qualification\n" + result;
    g.drawFittedText (text, 20, 15, 460, 160, juce::Justification::topLeft, 7);
}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
