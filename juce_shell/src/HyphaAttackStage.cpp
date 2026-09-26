#include "HyphaAttackStage.h"

#include <BinaryData.h>

#include "HyphaAttackDepth.h"
#include "HyphaDepthMaterial.h"
#include "HyphaAttackUiContract.h"
#include "HyphaTheme.h"

namespace hypha::attack_stage
{
namespace
{
const juce::Image& myceliumBed()
{
    static const juce::Image image = juce::ImageFileFormat::loadFrom (
        BinaryData::bg_mycelium_png, static_cast<size_t> (BinaryData::bg_mycelium_pngSize));
    return image;
}
}

void paint (juce::Graphics& g, juce::Rectangle<float> area, float corner, float bed, bool vignette)
{
    if (area.getWidth() < 2.0f || area.getHeight() < 2.0f)
        return;
    const auto outer = area.reduced (0.5f);
    const auto radius = juce::jlimit (1.0f, juce::jmin (outer.getWidth(), outer.getHeight()) * 0.5f,
                                      corner);
    // The well is cut into the face: its upper lip casts a line of shadow onto the face.
    depth_material::paintCastShadowAbove (g, outer, radius, attack_depth::look().wellShadow * 0.9f);
    juce::ColourGradient depth (BG.darker (0.55f), outer.getCentreX(), outer.getY(),
                                BG.darker (0.82f), outer.getCentreX(), outer.getBottom(), false);
    g.setGradientFill (depth);
    g.fillRoundedRectangle (outer, radius);
    const auto& image = myceliumBed();
    if (bed > 0.0f && image.isValid())
    {
        juce::Graphics::ScopedSaveState saved (g);
        juce::Path clip;
        clip.addRoundedRectangle (outer, radius);
        g.reduceClipRegion (clip);
        g.setOpacity (juce::jlimit (0.0f, 1.0f, bed));
        g.drawImage (image, outer, juce::RectanglePlacement (juce::RectanglePlacement::centred
                                                             | juce::RectanglePlacement::fillDestination));
    }
    // Deep-teal structural light, a continuous low top reflection and a sunken lower edge.
    g.setColour (juce::Colour (attack_ui::waveformColour).withAlpha (0.08f));
    g.drawRoundedRectangle (outer, radius, 0.7f);
    g.setColour (COL_NORMAL.withAlpha (0.05f));
    g.drawLine (outer.getX() + radius, outer.getY() + 0.6f,
                outer.getRight() - radius, outer.getY() + 0.6f, 0.6f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawLine (outer.getX() + radius, outer.getBottom() - 0.5f,
                outer.getRight() - radius, outer.getBottom() - 0.5f, 0.8f);
    attack_depth::paintWell (g, outer, radius, vignette);
}

// Measuring only: setFont builds the same font inline, as the typography source contract requires.
juce::Font trackedFont (const presentation::Context& context, typography::TextRole role,
                        float tracking)
{
    return monoFont (context, role, typography::Composition::visualization)
        .withExtraKerningFactor (tracking);
}
}
