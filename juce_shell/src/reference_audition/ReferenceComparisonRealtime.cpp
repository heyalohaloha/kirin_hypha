#include "ReferenceComparisonController.h"

namespace hypha::reference_audition
{
void ReferenceComparisonController::observeTransport (std::int64_t position, bool valid, bool playing) noexcept
{
    if (rtPlaying && !playing) pendingSafetyEpoch.fetch_add (1, std::memory_order_release);
    rtPlaying = playing;
    version.observeTransport (position, valid, playing);
    check.observeTransport (position, valid, playing);
    reference.observeTransport (position, valid, playing);
}
void ReferenceComparisonController::observeAInput (const juce::AudioBuffer<float>& buffer,
    std::int64_t position, bool valid, bool playing, bool allowed, int clock, std::optional<bool> captureAllowed, CaptureClockSignature signature) noexcept
{
    if (!rtInputObserved || rtInputAllowed != allowed)
    {
        pendingInputSafety.store (allowed ? 1 : 0, std::memory_order_release);
        if (!allowed) pendingSafetyEpoch.fetch_add (1, std::memory_order_release);
    }
    rtInputAllowed = allowed;
    rtInputObserved = true;
    capture.observe(buffer,position,valid,playing,captureAllowed.value_or(allowed),clock,signature,allowed);
    version.observeAInput (buffer, position, valid, playing, allowed && versionChosen.load (std::memory_order_acquire), false);
    check.observeAInput (buffer, position, valid, playing, allowed, false);
    reference.observeAInput (buffer, position, valid, playing, allowed, false);
}
// Each role with an output path (the selected one, or one still fading back to A) renders from A.
// Their deviations from A are summed, so a switch between roles crossfades and never sums two full sources.
bool ReferenceComparisonController::renderSelectedB (juce::AudioBuffer<float>& buffer,
    std::int64_t position, bool valid, bool allowed, bool returnAllowed) noexcept
{
    const int target = normalOutputSlot.load (std::memory_order_acquire);
    RuntimeV2Controller* paths[3] {};
    juce::AudioBuffer<float>* scratch[3] {};
    int slots[3] {}, count = 0;
    const auto add = [&] (RuntimeV2Controller& role, juce::AudioBuffer<float>& space, int slot) noexcept
    { if (role.hasOutputPath()) { paths[count] = &role; scratch[count] = &space; slots[count++] = slot; } };
    add (version, bScratch, 1); add (check, cScratch, 2); add (reference, rScratch, 3);
    bool rendered = false;
    if (count == 1)
        rendered = paths[0]->renderSelectedB (buffer, position, valid, allowed, returnAllowed, target == slots[0]);
    else if (count > 1)
    {
        const int frames = buffer.getNumSamples(), channels = buffer.getNumChannels();
        if (frames < 1 || frames > 8192 || channels < 1 || channels > 2)
        {
            for (int index = 0; index < count; ++index) paths[index]->renderSelectedB (buffer, position, valid, false, false);
            return false;
        }
        bool audible[3] {};
        for (int index = 0; index < count; ++index)
        {
            for (int c = 0; c < channels; ++c) scratch[index]->copyFrom (c, 0, buffer, c, 0, frames);
            juce::AudioBuffer<float> view (scratch[index]->getArrayOfWritePointers(), channels, frames);
            audible[index] = paths[index]->renderSelectedB (view, position, valid, allowed, returnAllowed, target == slots[index]);
            rendered = rendered || audible[index];
        }
        int first = -1;
        for (int index = count - 1; index >= 0; --index) if (audible[index]) first = index;
        // One audible role is copied exactly; more start from the first and add the others' deviations from A.
        if (rendered) for (int channel = 0; channel < channels; ++channel)
            for (int frame = 0; frame < frames; ++frame)
            {
                const auto a = buffer.getSample (channel, frame);
                auto output = scratch[first]->getSample (channel, frame);
                for (int index = first + 1; index < count; ++index)
                    if (audible[index]) output += scratch[index]->getSample (channel, frame) - a;
                buffer.setSample (channel, frame, output);
            }
    }
    // Every journal observes A only after the actual output decision. C->B is not an A return.
    if (! rendered && rtInputAllowed && rtPlaying && valid && buffer.getNumSamples() > 0)
    {
        version.confirmAOutput();
        check.confirmAOutput();
        reference.confirmAOutput();
    }
    return rendered;
}
}
