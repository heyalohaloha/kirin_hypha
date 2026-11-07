#pragma once
#include "HyphaSpaceLiveTap.h"
#include "live_compare/CallbackWallClock.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace hypha::space_live
{
// Message-thread owner. The timer never raises the 10 Hz public-meter/editor polling cadence.
// Each point keeps its continuous coordinate; the small raster is an antialiased display surface,
// not a measurement histogram. Bilinear subpixel ink avoids a second, visible quantization grid.
class Display : private juce::Timer, private juce::ComponentListener
{
public:
    static constexpr int refreshHz = 60;
    static constexpr int refreshIntervalMs = 1000 / refreshHz;
    Display (juce::Component& component, std::function<void()> repaint)
        : owner (component), invalidate (std::move (repaint)) { owner.addComponentListener (this); }
    ~Display() override
    {
        stopTimer(); if (source) source->setEnabled (false);
        clearAncestors(); owner.removeComponentListener (this);
    }
    void setSource (LiveTap* next)
    {
        if (source == next) return;
        if (source) source->setEnabled (false);
        source = next; clear(); refreshVisibility();
    }
    bool attached() const noexcept { return source != nullptr; }
    void setPresented (bool next)
    { presented = next; refreshVisibility(); }
    void refreshVisibility()
    {
        const bool next = source != nullptr && presented && owner.isVisible() && owner.isEnabled()
                       && (owner.getPeer() == nullptr || (owner.isShowing() && nativeShowing (*owner.getPeer())));
        bool ancestorsVisible = true;
        for (auto* p = owner.getParentComponent(); p != nullptr; p = p->getParentComponent())
            ancestorsVisible = ancestorsVisible && p->isVisible();
        const bool showing = next && ancestorsVisible;
        if (source) source->setEnabled (showing);
        if (showing == isTimerRunning()) return;
        clear();
        if (showing) startTimer (refreshIntervalMs); else stopTimer();
        invalidate();
    }
    // Deterministic frame entry shared by the real timer and native tests (no short sleeps).
    bool refresh (std::uint64_t nanos)
    {
        if (! source || ! source->enabled()) { clear(); return false; }
        const auto result = source->read (pending, nanos);
        // Contention cannot extend the lifetime of the last accepted audio callback.
        if (result == Read::empty || (result == Read::busy && snapshot.expired (nanos)))
        {
            const bool changed = ! snapshot.samples.empty(); clear(); return changed;
        }
        if (result == Read::busy || pending.revision == snapshot.revision) return false;
        std::swap (pending, snapshot); raster = {}; return true;
    }
    const Snapshot& frame() const noexcept { return snapshot; }
    bool running() const noexcept { return isTimerRunning(); }

    void paint (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        if (snapshot.samples.empty()) return;
        const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const int pixels = juce::jlimit (32, 1024, juce::roundToInt (plot.getWidth() * scale));
        if (! raster.isValid() || raster.getWidth() != pixels) buildRaster (pixels);
        g.drawImage (raster, plot);
    }

private:
    void clear() { snapshot.samples.clear(); snapshot.revision = 0; raster = {}; }
    void timerCallback() override
    {
        refreshVisibility();
        if (isTimerRunning() && refresh (live_compare::callbackWallNanos())) invalidate();
    }
    void clearAncestors()
    {
        for (auto& ancestor : ancestors)
            if (auto* c = ancestor.getComponent()) c->removeComponentListener (this);
        ancestors.clear();
    }
    void componentVisibilityChanged (juce::Component&) override { refreshVisibility(); }
    void componentEnablementChanged (juce::Component&) override { refreshVisibility(); }
    void componentParentHierarchyChanged (juce::Component&) override
    {
        clearAncestors();
        for (auto* p = owner.getParentComponent(); p != nullptr; p = p->getParentComponent())
        { ancestors.emplace_back (p); p->addComponentListener (this); }
        refreshVisibility();
    }
    static bool nativeShowing (const juce::ComponentPeer&);
    void buildRaster (int pixels);
    juce::Component& owner;
    std::function<void()> invalidate;
    LiveTap* source = nullptr;
    bool presented = false;
    Snapshot snapshot, pending;
    juce::Image raster;
    std::vector<float> ink;
    std::vector<juce::Component::SafePointer<juce::Component>> ancestors;
};
}
