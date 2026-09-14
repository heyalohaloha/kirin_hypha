#include "ReferenceTonalViewContractTest.h"

#include "../src/HyphaReferenceComponent.h"
#include "../src/HyphaReferenceTonalView.h"

#include <array>

namespace hypha::tests
{
namespace
{
std::shared_ptr<const reference_audition::VisualTimeline> tonalTimeline()
{
    auto timeline = std::make_shared<reference_audition::VisualTimeline>();
    timeline->tonalAvailable = true;
    timeline->tonal.valid_bits = (std::uint64_t (1) << 60) - 1;
    auto reference = std::make_shared<reference_audition::ReferenceTonalCurve>();
    reference->validBits = timeline->tonal.valid_bits;
    for (int band = 0; band < 60; ++band)
    {
        timeline->tonal.values_db[band] = -30.0f - static_cast<float> (band) * 0.2f;
        reference->median[static_cast<size_t> (band)] =
            -31.0f - static_cast<float> (band) * 0.18f;
    }
    timeline->tonalReference = std::move (reference);
    return timeline;
}

juce::Image render (reference_ui::Component& component)
{
    juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    juce::Graphics graphics (image);
    component.paintEntireComponent (graphics, true);
    return image;
}

bool containsVisiblePixels (const juce::Image& image)
{
    int count = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            count += image.getPixelAt (x, y).getAlpha() != 0;
    return count > image.getWidth() * image.getHeight() / 4;
}

bool writeIfRequested (const juce::Image& image, const char* variable)
{
    const auto path = juce::SystemStats::getEnvironmentVariable (variable, {});
    if (path.isEmpty()) return true;
    juce::FileOutputStream stream { juce::File { path } };
    return stream.setPosition (0) && stream.truncate().wasOk()
        && juce::PNGImageFormat().writeImageToStream (image, stream);
}
}

bool verifyReferenceTonalViewContract()
{
    reference_ui::Component component;
    reference_ui::State state;
    state.separateComparisons = true;
    state.comparisonSlot = 2;
    state.title = "Balance";
    state.candidateName = "Reference C";
    state.cueLabel = "Full song";
    state.viewBindings = { "tonal_balance" };
    state.visualTimeline = tonalTimeline();
    reference_ui::TonalView* tonal = nullptr;
    juce::Image compact, large;
    for (const auto width : std::array { 300, 375, 450, 600, 900 })
    {
        component.setPresentationContext (presentation::forEditor (width, width * 2 / 3));
        component.setSize (width - 12, width == 900 ? 470 : width * 2 / 3 - 64);
        component.setState (state);
        tonal = dynamic_cast<reference_ui::TonalView*> (
            component.findChildWithID ("reference-tonal-view"));
        if (tonal == nullptr || ! tonal->isVisible()
            || ! component.getLocalBounds().contains (tonal->getBounds())) return false;
        if (width == 300) compact = render (component);
        if (width == 900) large = render (component);
    }
    if (! containsVisiblePixels (compact) || ! containsVisiblePixels (large)
        || tonal == nullptr || ! tonal->keyPressed (juce::KeyPress (juce::KeyPress::rightKey))
        || ! tonal->getDescription().contains ("selected")) return false;
    state.blindPhase = reference_ui::BlindPhase::active;
    component.setState (state);
    if (tonal->isVisible() || tonal->getTitle().isNotEmpty()
        || tonal->getDescription().isNotEmpty()) return false;
    return writeIfRequested (compact, "KIRIN_REFERENCE_UI_TONAL_COMPACT_OUTPUT")
        && writeIfRequested (large, "KIRIN_REFERENCE_UI_TONAL_OUTPUT");
}
}
