#pragma once

#include "HyphaVuNeedleMotion.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace hypha::hybrid_vu
{
// Owned by the view and accessed only on the message thread.
class NeedleAnimator : private juce::AsyncUpdater, private juce::ComponentListener
{
public:
    // `refreshIn` runs at the start of every frame while showing, so the owner can pull the
    // audio thread's live VU values and retarget before the needles advance.
    NeedleAnimator (juce::Component& ownerIn, std::function<void()> repaintIn, std::function<void()> refreshIn = {})
        : owner (ownerIn), repaint (std::move (repaintIn)), refresh (std::move (refreshIn)) {}
    ~NeedleAnimator() override { setVisible (false); }

    bool isVisible() const noexcept { return static_cast<bool> (attachment); }

    void setVisible (bool visible)
    {
        if (visible == static_cast<bool> (attachment)) return;
        motion.setVisible (visible);
        if (! visible) { attachment.reset(); clearAncestors(); return; }
        refreshHierarchy();
        lastTime = juce::Time::getMillisecondCounterHiRes();
        attachment = std::make_unique<juce::VBlankAttachment> (&owner, [this] { onFrame(); });
    }

    void setTarget (int channel, double target)
    {
        const bool wasActive = motion.active();
        motion.setTarget (channel, target);
        if (! wasActive && motion.active()) lastTime = juce::Time::getMillisecondCounterHiRes();
    }

    void refreshHierarchy()
    {
        clearAncestors();
        for (auto* parent = owner.getParentComponent(); parent != nullptr;
             parent = parent->getParentComponent())
        {
            ancestors.emplace_back (parent);
            parent->addComponentListener (this);
        }
    }

    void snap (int channel, double target) noexcept { motion.snap (channel, target); }
    void setLive (bool live) noexcept { motion.setLive (live); }
    double position (int channel) const noexcept { return motion.position (channel); }
    void tickForTest (double dt) { dispatch (dt); }

private:
    void clearAncestors()
    {
        for (auto& parent : ancestors)
            if (auto* component = parent.getComponent()) component->removeComponentListener (this);
        ancestors.clear();
    }
    void componentVisibilityChanged (juce::Component&) override
    {
        if (! owner.isShowing()) setVisible (false);
    }
    void handleAsyncUpdate() override
    {
        if (! owner.isShowing()) setVisible (false);
    }
    void onFrame()
    {
        if (! owner.isShowing()) { triggerAsyncUpdate(); return; }
        if (refresh) refresh();
        if (! motion.active()) return;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto dt = (now - lastTime) / 1000.0;
        lastTime = now;
        dispatch (dt);
    }
    void dispatch (double dt)
    {
        if (motion.tick (dt)) repaint();
    }

    juce::Component& owner;
    std::function<void()> repaint, refresh;
    NeedleMotionPair motion;
    double lastTime = 0.0;
    std::unique_ptr<juce::VBlankAttachment> attachment;
    std::vector<juce::Component::SafePointer<juce::Component>> ancestors;
};
}
