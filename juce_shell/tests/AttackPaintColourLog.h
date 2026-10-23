#pragma once
#include <juce_graphics/juce_graphics.h>
#include <algorithm>
#include <vector>

namespace hypha::attack_ui_test
{
// Observe the colour actually passed to the rasterizer for text. Native glyph antialiasing
// changes with text colour, so neither exact RGB pixels nor a shared glyph-mask assumption
// can identify a palette token portably. All drawing still reaches the real image context.
struct PaintColourLog
{
    struct Ink { juce::Rectangle<int> clip; juce::Colour colour; };
    std::vector<Ink> glyphs;
    bool has (juce::Rectangle<int> clip, juce::Colour colour) const
    {
        return std::any_of (glyphs.begin(), glyphs.end(), [&] (const auto& ink) {
            return ink.clip == clip && ink.colour == colour;
        });
    }
};
class PaintColourContext final : public juce::LowLevelGraphicsContext
{
public:
    PaintColourContext (const juce::Image& image, PaintColourLog* output)
        : native (image.createLowLevelContext()), log (output) {}
    bool isVectorDevice() const override { return native->isVectorDevice(); }
    void setOrigin (juce::Point<int> p) override { native->setOrigin (p); }
    void addTransform (const juce::AffineTransform& t) override { native->addTransform (t); }
    float getPhysicalPixelScaleFactor() override { return native->getPhysicalPixelScaleFactor(); }
    bool clipToRectangle (const juce::Rectangle<int>& r) override { return native->clipToRectangle (r); }
    bool clipToRectangleList (const juce::RectangleList<int>& r) override { return native->clipToRectangleList (r); }
    void excludeClipRectangle (const juce::Rectangle<int>& r) override { native->excludeClipRectangle (r); }
    void clipToPath (const juce::Path& p, const juce::AffineTransform& t) override { native->clipToPath (p, t); }
    void clipToImageAlpha (const juce::Image& i, const juce::AffineTransform& t) override { native->clipToImageAlpha (i, t); }
    bool clipRegionIntersects (const juce::Rectangle<int>& r) override { return native->clipRegionIntersects (r); }
    juce::Rectangle<int> getClipBounds() const override { return native->getClipBounds(); }
    bool isClipEmpty() const override { return native->isClipEmpty(); }
    void saveState() override { saved.push_back (fill); native->saveState(); }
    void restoreState() override { native->restoreState(); fill = saved.back(); saved.pop_back(); }
    void beginTransparencyLayer (float a) override { native->beginTransparencyLayer (a); }
    void endTransparencyLayer() override { native->endTransparencyLayer(); }
    void setFill (const juce::FillType& f) override { fill = f; native->setFill (f); }
    void setOpacity (float a) override { fill.setOpacity (a); native->setOpacity (a); }
    void setInterpolationQuality (juce::Graphics::ResamplingQuality q) override { native->setInterpolationQuality (q); }
    void fillRect (const juce::Rectangle<int>& r, bool replace) override { native->fillRect (r, replace); }
    void fillRect (const juce::Rectangle<float>& r) override { native->fillRect (r); }
    void fillRectList (const juce::RectangleList<float>& r) override { native->fillRectList (r); }
    void fillPath (const juce::Path& p, const juce::AffineTransform& t) override { native->fillPath (p, t); }
    void drawImage (const juce::Image& i, const juce::AffineTransform& t) override { native->drawImage (i, t); }
    void drawLine (const juce::Line<float>& l) override { native->drawLine (l); }
    void setFont (const juce::Font& f) override { native->setFont (f); }
    const juce::Font& getFont() override { return native->getFont(); }
    void drawGlyph (int glyph, const juce::AffineTransform& t) override { record(); native->drawGlyph (glyph, t); }
    bool drawTextLayout (const juce::AttributedString& s, const juce::Rectangle<float>& r) override
    { record(); return native->drawTextLayout (s, r); }
private:
    std::unique_ptr<juce::LowLevelGraphicsContext> native;
    PaintColourLog* log;
    juce::FillType fill;
    std::vector<juce::FillType> saved;
    void record()
    { if (log && fill.isColour()) log->glyphs.push_back ({native->getClipBounds(), fill.colour}); }
};
}
