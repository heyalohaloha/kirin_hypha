#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"

namespace hypha
{
    // POST-only internal validator. It shows confirmed ATTACK positions, not a waveform,
    // confidence score, kick label, or public product judgement.
    class AttackInternalComponent final : public juce::Component
    {
    public:
        AttackInternalComponent() = default;
        void setSnapshot (const KirinAttackEventBatch& events,
                          const KirinAttackWaveformBatch& waveform,
                          const KirinAttackDetailBatch& details,
                          std::int64_t latestSample,
                          std::uint32_t sampleRate,
                          std::uint64_t generation,
                          const KirinAttackStats& stats);
        void clearSnapshot();
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        KirinAttackEventBatch eventBatch {};
        KirinAttackWaveformBatch waveformBatch {};
        KirinAttackDetailBatch detailBatch {};
        KirinAttackStats runtimeStats {};
        std::int64_t latest = -1;
        std::uint32_t rate = 0;
        std::uint64_t currentGeneration = 0;
        std::int64_t selectedEventSample = -1;

        const KirinAttackDetail* selectedDetail() const noexcept;
        juce::Rectangle<int> timelineBounds() const noexcept;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AttackInternalComponent)
    };
}
