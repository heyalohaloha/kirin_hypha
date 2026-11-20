#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "HyphaSpectrumFrameCadence.h"
#include <functional>
#include <memory>
#include <vector>

namespace hypha::spectrum_display
{
// The 30 Hz source poll stays in the editor. Only presentation follows the display clock.
// Watch ancestor visibility too: a hidden host editor must release its VBlank attachment.
class FrameDriver final : private juce::ComponentListener, private juce::AsyncUpdater
{
public:
    FrameDriver (juce::Component& component, std::function<void(double)> callback)
        : owner (component), tick (std::move (callback))
    { owner.addComponentListener (this); hierarchy(); }
    ~FrameDriver() override
    { cancelPendingUpdate(); attachment.reset(); clearParents(); owner.removeComponentListener (this); }
    void setEnabled (bool value) { enabled = value; synchronize(); }
    bool active() const noexcept { return attachment != nullptr; }
private:
    void clearParents()
    {
        for (auto& parent : parents)
            if (auto* component = parent.getComponent()) component->removeComponentListener (this);
        parents.clear();
    }
    void hierarchy()
    {
        clearParents();
        for (auto* parent = owner.getParentComponent(); parent; parent = parent->getParentComponent())
        { parents.emplace_back (parent); parent->addComponentListener (this); }
        synchronize();
    }
    void synchronize()
    {
        if (! enabled || ! owner.isShowing()) { attachment.reset(); cadence.reset(); return; }
        if (! attachment)
            attachment = std::make_unique<juce::VBlankAttachment> (&owner, [this]
            {
                if (! enabled || ! owner.isShowing()) { triggerAsyncUpdate(); return; }
                const auto now = juce::Time::getMillisecondCounterHiRes();
                if (cadence.due (now)) tick (now);
            });
    }
    void componentVisibilityChanged (juce::Component&) override { synchronize(); }
    void componentParentHierarchyChanged (juce::Component&) override { hierarchy(); }
    void handleAsyncUpdate() override { synchronize(); }
    juce::Component& owner;
    std::function<void(double)> tick;
    bool enabled = false;
    FrameCadence cadence;
    std::unique_ptr<juce::VBlankAttachment> attachment;
    std::vector<juce::Component::SafePointer<juce::Component>> parents;
};
}
