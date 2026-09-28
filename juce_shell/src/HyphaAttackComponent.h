#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaAttackBandContract.h"
#include "HyphaAttackLaneModel.h"
#include "HyphaAttackUiContract.h"
#include "HyphaPresentationContext.h"

namespace hypha
{
    // TRACK/STEM DRUM ATTACK view. HISTORY shows the six-second PRE trace and POST body; four
    // per-hit lanes share its time axis and show exact POST - PRE differences (POST values when
    // no pair exists). With a band chosen (B-1097) the lanes carry that octave band's DELAY, ATT,
    // REL and LEVEL, and at 200% and 300% HISTORY shows the selected hit's HEAD and TAIL in it.
    // No quality judgement or instrument inference.
    class AttackComponent final : public juce::Component,
                                  public juce::SettableTooltipClient
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
        // The chosen band's hits, polled after the snapshot; false when nothing changed.
        bool setBandSnapshot (const KirinAttackBandBatch&);
        void clearSnapshot();
        void setOverlayMode (bool shouldOverlay);
        // 0 = ALL. Editor-lifetime state like VIEW; the editor forwards it to the engine.
        void setBand (std::uint8_t band);
        std::uint8_t band() const noexcept { return chosenBand; }
        std::function<void (std::uint8_t)> onBandChange;
        // Paired, a band chosen, and PRE has not sent it: an older PRE, or not yet.
        bool preBandPending() const noexcept;
        void presentationTick (bool signalActive);
        void presentationTickAt (double nowMs);
        bool pairedObservation() const noexcept { return pairEventBatch.status == KIRIN_SPECTRUM_ACTIVE; }
        // The cached structure image is rebuilt on the next paint. The editor calls this when a
        // host hides it without destroying it; leaving the page or hiding this view does it too.
        void releaseCachedChrome() noexcept { chromeImage = {}; }
        std::size_t cachedChromeBytes() const noexcept
        {
            return chromeImage.isValid() ? static_cast<std::size_t> (chromeImage.getWidth())
                                               * static_cast<std::size_t> (chromeImage.getHeight()) * 4
                                         : 0;
        }
        void paint (juce::Graphics&) override;
        void visibilityChanged() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        KirinAttackEventBatch eventBatch {};
        KirinAttackWaveformBatch waveformBatch {};
        KirinAttackDetailBatch detailBatch {};
        KirinAttackWaveformBatch preWaveformBatch {};
        KirinAttackDetailBatch preDetailBatch {};
        KirinAttackPairEventBatch pairEventBatch {};
        KirinAttackBandBatch bandBatch {};
        KirinAttackStats runtimeStats {};
        attack_lanes::Model laneModel {};
        attack_lanes::Model bandModel {};
        std::int64_t latest = -1;
        std::int64_t presentationStartLatest = -1;
        std::int64_t presentationTargetLatest = -1;
        double presentationStartMs = 0.0;
        std::uint32_t rate = 0;
        std::uint64_t currentGeneration = 0;
        std::int64_t selectedEventSample = -1;
        std::uint8_t chosenBand = 0;
        bool overlayMode = true;
        bool followLatest = true;
        bool liveSignalActive = true;
        presentation::Context presentationContext = presentation::defaultContext();

        // Structure that only changes with size, device scale, context, VIEW, pairing, the band
        // and data validity is rendered once into this image; every frame repaints only
        // observations. While the size changes between paints (a corner drag, a Capture layout)
        // the structure is drawn directly and the image is kept for the size it was built at.
        struct ChromeKey
        {
            int width = 0;
            int height = 0;
            float scale = 0.0f;
            presentation::Context context = presentation::defaultContext();
            bool overlay = false;
            bool paired = false;
            bool dormant = false;
            std::uint8_t band = 0;
            bool bandDelta = false;
            bool prePending = false;
            bool operator== (const ChromeKey&) const noexcept;
        };
        static constexpr std::size_t chromeByteBudget = 8 * 1024 * 1024;
        juce::Image chromeImage;
        ChromeKey chromeKey;
        int paintedWidth = 0;
        int paintedHeight = 0;

        static juce::Rectangle<int> rectangleOf (attack_ui::Box box) noexcept
        {
            return { box.x, box.y, box.width, box.height };
        }
        const KirinAttackPairEvent* selectedPairEvent() const noexcept;
        const KirinAttackDetail* selectedPostDetail() const noexcept;
        const KirinAttackDetail* selectedPreDetail() const noexcept;
        const attack_lanes::Hit* visibleSelection() const noexcept;
        // The band lanes' hit and the engine's hit behind it, for the selected visible hit.
        const attack_lanes::Hit* bandSelection (const attack_lanes::Hit* selected) const noexcept;
        const KirinAttackBandHit* selectedBandHit (const attack_lanes::Hit* selected) const noexcept;
        void rebuildBandModel() noexcept;
        attack_ui::Layout layout() const noexcept;
        // 100% is view-only and shows HISTORY in one row whatever VIEW was chosen at 125% and up.
        bool viewOnly() const noexcept { return presentationContext.density == observatory::Density::compact; }
        bool twoRows() const noexcept { return pairedObservation() && ! overlayMode && ! viewOnly(); }
        // The HISTORY row shows the band's HEAD / TAIL panes instead of the six seconds.
        bool bandPanes (const attack_ui::Layout& shape) const noexcept
        {
            return chosenBand != 0 && attack_band::panesShown (shape);
        }
        bool selectsAt (const attack_ui::Layout&, juce::Point<int>) const noexcept;
        juce::String tooltipAt (const attack_ui::Layout&, juce::Point<int>) const;
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
        // The envelope bands inside a HISTORY plot: one, or PRE above POST in two rows. The
        // chrome's scale lines and the painted envelopes share them.
        std::size_t envelopeBands (juce::Rectangle<int> plot,
                                   std::array<juce::Rectangle<int>, 2>& bands) const noexcept;
        void paintHeaderState (juce::Graphics&, const attack_ui::Layout&);
        void paintHistory (juce::Graphics&, juce::Rectangle<int> plot);
        void paintAxis (juce::Graphics&, const attack_ui::Layout&);
        void paintSelection (juce::Graphics&, const attack_ui::Layout&, const attack_lanes::Hit*);
        juce::String timeMode() const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AttackComponent)
    };
}
