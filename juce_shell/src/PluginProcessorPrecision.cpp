#include "PluginProcessor.h"
#include <cstring>

bool KirinHyphaProcessorBase::supportsDoublePrecisionProcessing() const { return true; }

void KirinHyphaProcessorBase::preparePrecisionScratch (int frames, int channels)
{
    precisionScratchFrames = precisionScratchChannels = 0;
    precisionPointers.fill (nullptr);
    precisionView.setDataToReferTo (precisionPointers.data(), 0, 0);
    if (! isUsingDoublePrecision() || channels < 1 || channels > 2)
    {
        std::vector<float>().swap (precisionScratch);
        return;
    }
    precisionScratch.assign (static_cast<size_t> (frames) * static_cast<size_t> (channels), 0.0f);
    for (int ch = 0; ch < channels; ++ch)
        precisionPointers[static_cast<size_t> (ch)] = precisionScratch.data() + static_cast<size_t> (ch) * static_cast<size_t> (frames);
    precisionScratchFrames = frames;
    precisionScratchChannels = channels;
}

void KirinHyphaProcessorBase::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int channels = getTotalNumInputChannels(), frames = buffer.getNumSamples();
    for (int ch = channels; ch < getTotalNumOutputChannels(); ++ch) buffer.clear (ch, 0, frames);
    if (hyphaHandle == nullptr) return;
    if (channels < 1 || channels > 2 || channels != precisionScratchChannels
        || buffer.getNumChannels() < channels || frames > precisionScratchFrames)
    {
        if (channels > 0)
        {
            kirin_hypha_note_oversized_drop (hyphaHandle, static_cast<uint64_t> (frames) * static_cast<uint64_t> (channels));
            kirin_hypha_push_samples (hyphaHandle, nullptr, 0, static_cast<uint32_t> (channels));
        }
        return;
    }
    for (int ch = 0; ch < channels; ++ch)
        for (int frame = 0; frame < frames; ++frame)
            precisionPointers[static_cast<size_t> (ch)][frame] = static_cast<float> (buffer.getSample (ch, frame));
    // Mono/stereo uses JUCE's embedded channel-pointer array, with no heap allocation.
    precisionView.setDataToReferTo (precisionPointers.data(), channels, frames);
    processBlock (precisionView, midi);
    for (int ch = 0; ch < channels; ++ch)
        for (int frame = 0; frame < frames; ++frame)
        {
            const auto original = static_cast<float> (buffer.getSample (ch, frame));
            const auto rendered = precisionView.getSample (ch, frame);
            if (std::memcmp (&original, &rendered, sizeof (float)) != 0)
                buffer.setSample (ch, frame, static_cast<double> (rendered));
        }
}
