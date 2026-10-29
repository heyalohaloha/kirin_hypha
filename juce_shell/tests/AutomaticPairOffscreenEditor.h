#pragma once
#include "../src/PluginEditor.h"

#if JUCE_MAC
// A message-thread host peer without an OS window. It exercises the real editor and its timers.
class AutomaticPairOffscreenPeer final : public juce::ComponentPeer
{
public:
    AutomaticPairOffscreenPeer (juce::Component& owner, int flags) : ComponentPeer (owner, flags), bounds (owner.getBounds()) {}
    void* getNativeHandle() const override { return nullptr; }
    void setVisible (bool) override {}
    void setTitle (const juce::String&) override {}
    void setBounds (const juce::Rectangle<int>& next, bool) override { bounds = next; }
    juce::Rectangle<int> getBounds() const override { return bounds; }
    juce::Point<float> localToGlobal (juce::Point<float> point) override { return point; }
    juce::Point<float> globalToLocal (juce::Point<float> point) override { return point; }
    void setMinimised (bool) override {}
    bool isMinimised() const override { return false; }
    void setFullScreen (bool) override {}
    bool isFullScreen() const override { return false; }
    void setIcon (const juce::Image&) override {}
    bool contains (juce::Point<int> point, bool) const override { return bounds.withZeroOrigin().contains (point); }
    OptionalBorderSize getFrameSizeIfPresent() const override { return OptionalBorderSize (juce::BorderSize<int>()); }
    juce::BorderSize<int> getFrameSize() const override { return {}; }
    bool setAlwaysOnTop (bool) override { return true; }
    void toFront (bool) override {}
    void toBehind (juce::ComponentPeer*) override {}
    bool isFocused() const override { return true; }
    void grabFocus() override {}
    void repaint (const juce::Rectangle<int>&) override {}
    void performAnyPendingRepaintsNow() override {}
    void setAlpha (float) override {}
    juce::StringArray getAvailableRenderingEngines() override { return {}; }
private:
    void textInputRequired (juce::Point<int>, juce::TextInputTarget&) override {}
    juce::Rectangle<int> bounds;
};

class AutomaticPairOffscreenEditor final : public KirinHyphaEditor
{
public:
    explicit AutomaticPairOffscreenEditor (KirinHyphaProcessorBase& processor) : KirinHyphaEditor (processor) {}
private:
    juce::ComponentPeer* createNewPeer (int flags, void*) override
    { return new AutomaticPairOffscreenPeer (*this, flags); }
};

inline juce::AudioProcessorEditor* makeAutomaticPairEditor (KirinHyphaProcessorBase& processor)
{ return new AutomaticPairOffscreenEditor (processor); }
void initialiseBlindProductHostApplication();
#else
inline juce::AudioProcessorEditor* makeAutomaticPairEditor (KirinHyphaProcessorBase& processor)
{ return processor.createEditor(); }
#endif
