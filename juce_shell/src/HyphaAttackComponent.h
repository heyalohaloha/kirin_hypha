#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaAttackLaneModel.h"
#include "HyphaAttackUiContract.h"
#include "HyphaPresentationContext.h"

namespace hypha
{
    // TRACK/STEM DRUM ATTACK view. HISTORY shows the six-second PRE trace and POST body; four
    // per-hit lanes share its time axis and show exact POST - PRE differences (POST values when
    // no pair exists). No quality judgement or instrument inference.
    class AttackComponent final : public juce::Component
    {
    public:
        AttackComponent();
        void setPresentationContext (presentation::Context next)
        {
            if (presentationContext == next) return;
            presentationContext = next;
            repaint();
        }
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
        KirinAttackEventBatch eventBatch {};
        KirinAttackWaveformBatch waveformBatch {};
        KirinAttackDetailBatch detailBatch {};
        KirinAttackWaveformBatch preWaveformBatch {};
        KirinAttackDetailBatch preDetailBatch {};
        KirinAttackPairEventBatch pairEventBatch {};
        KirinAttackStats runtimeStats {};
        attack_lanes::Model laneModel {};
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
        presentation::Context presentationContext = presentation::defaultContext();

        // Structure that only changes with size, device scale, context, VIEW, pairing and data
        // validity is rendered once into this image; every frame repaints only observations.
        struct ChromeKey
        {
            int width = 0;
            int height = 0;
            float scale = 0.0f;
            presentation::Context context = presentation::defaultContext();
            bool overlay = false;
            bool paired = false;
            bool dormant = false;
            bool operator== (const ChromeKey&) const noexcept;
        };
        static constexpr std::size_t chromeByteBudget = 8 * 1024 * 1024;
        juce::Image chromeImage;
        ChromeKey chromeKey;

        static juce::Rectangle<int> rectangleOf (attack_ui::Box box) noexcept
        {
            return { box.x, box.y, box.width, box.height };
        }
        const KirinAttackPairEvent* selectedPairEvent() const noexcept;
        const KirinAttackDetail* selectedPostDetail() const noexcept;
        const KirinAttackDetail* selectedPreDetail() const noexcept;
        attack_ui::Layout layout() const noexcept;
        juce::Rectangle<int> plotColumn (attack_ui::Box row) const noexcept;
        juce::Rectangle<int> historyPlotBounds() const noexcept;
        juce::Rectangle<int> axisPlotBounds() const noexcept;
        bool selectsAt (juce::Point<int>) const noexcept;
        int viewControlWidth() const;
        int statusControlWidth() const;
        void selectNearestEventAtX (int x) noexcept;
        void selectBoundaryEvent (bool selectLast) noexcept;
        void selectAdjacentEvent (bool moveRight) noexcept;
        void advancePresentation (double nowMs) noexcept;
        void paintChrome (juce::Graphics&, const attack_ui::Layout&, bool dormant);
        void drawChrome (juce::Graphics&, const attack_ui::Layout&, bool dormant);
        void drawHeaderChrome (juce::Graphics&, const attack_ui::Layout&);
        void drawHistoryChrome (juce::Graphics&, juce::Rectangle<int> plot);
        void paintHeaderState (juce::Graphics&, const attack_ui::Layout&);
        void paintHistory (juce::Graphics&, juce::Rectangle<int> plot);
        void paintAxis (juce::Graphics&, juce::Rectangle<int> axis);
        void paintSelection (juce::Graphics&, const attack_ui::Layout&);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AttackComponent)
    };
}
