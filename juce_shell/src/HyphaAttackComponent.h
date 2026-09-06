#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaAttackOverviewGlyphPainter.h"

namespace hypha
{
    // POST-only DRUM ATTACK view. It presents factual absolute PRE/POST shapes and deltas;
    // no confidence score, instrument inference, or quality judgement is introduced.
    class AttackComponent final : public juce::Component
    {
    public:
        AttackComponent();
        bool setSnapshot (const KirinAttackEventBatch& events,
                          const KirinAttackWaveformBatch& waveform,
                          const KirinAttackDetailBatch& details,
                          const KirinAttackWaveformBatch& preWaveform,
                          const KirinAttackDetailBatch& preDetails,
                          const KirinAttackPairEventBatch& pairEvents,
                          std::int64_t latestSample,
                          std::uint32_t sampleRate,
                          std::uint64_t generation,
                          const KirinAttackStats& stats);
        void clearSnapshot();
        void setOverlayMode (bool shouldOverlay);
        void presentationTick (bool signalActive);
        void presentationTickAt (double nowMs);
        bool pairedObservation() const noexcept { return pairEventBatch.status == KIRIN_SPECTRUM_ACTIVE; }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        attack_overview_glyph::Cache glyphCache;
        KirinAttackEventBatch eventBatch {};
        KirinAttackWaveformBatch waveformBatch {};
        KirinAttackDetailBatch detailBatch {};
        KirinAttackWaveformBatch preWaveformBatch {};
        KirinAttackDetailBatch preDetailBatch {};
        KirinAttackPairEventBatch pairEventBatch {};
        KirinAttackStats runtimeStats {};
        std::int64_t latest = -1;
        std::int64_t presentationStartLatest = -1;
        std::int64_t presentationTargetLatest = -1;
        double presentationStartMs = 0.0;
        std::uint32_t rate = 0;
        std::uint64_t currentGeneration = 0;
        std::int64_t selectedEventSample = -1;
        bool overlayMode = true;
        bool followLatest = true;
        bool liveSignalActive = true;

        const KirinAttackPairEvent* selectedPairEvent() const noexcept;
        const KirinAttackDetail* selectedPostDetail() const noexcept;
        const KirinAttackDetail* selectedPreDetail() const noexcept;
        juce::Rectangle<int> timelineBounds() const noexcept;
        juce::Rectangle<int> scrubBounds() const noexcept;
        void selectNearestEventAtX (int x) noexcept;
        void selectBoundaryEvent (bool selectLast) noexcept;
        void selectAdjacentEvent (bool moveRight) noexcept;
        void advancePresentation (double nowMs) noexcept;
        void paintSelectedEvent (juce::Graphics&, juce::Rectangle<int>);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AttackComponent)
    };
}
