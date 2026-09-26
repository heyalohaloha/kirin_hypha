#include "HyphaAttackComponent.h"

#include <array>
#include <cmath>
#include <functional>
#include <initializer_list>

#include "HyphaAttackDepth.h"
#include "HyphaAttackLanePainter.h"
#include "HyphaAttackLoupePainter.h"
#include "HyphaAttackStage.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

// Cached DRUM structure: shell material, title, VIEW control, legend, every stage, label, scale
// tick and zero line. None of it depends on a measured value, so a live frame only blits it.
namespace hypha
{
namespace
{
const auto waveformColour = juce::Colour (attack_ui::waveformColour);
constexpr auto visualization = typography::Composition::visualization;
}

bool AttackComponent::ChromeKey::operator== (const ChromeKey& other) const noexcept
{
    return width == other.width && height == other.height
        && std::equal_to<float> {} (scale, other.scale) && context == other.context
        && overlay == other.overlay && paired == other.paired && dormant == other.dormant;
}

void AttackComponent::paintChrome (juce::Graphics& g, const attack_ui::Layout& shape, bool dormant)
{
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto pixelWidth = static_cast<int> (std::ceil (static_cast<float> (getWidth()) * scale));
    const auto pixelHeight = static_cast<int> (std::ceil (static_cast<float> (getHeight()) * scale));
    const bool cacheable = std::isfinite (scale) && scale > 0.0f && scale <= 4.0f
        && pixelWidth > 0 && pixelHeight > 0
        && static_cast<std::size_t> (pixelWidth) * static_cast<std::size_t> (pixelHeight) * 4
               <= chromeByteBudget;
    const ChromeKey key { getWidth(), getHeight(), scale, presentationContext, overlayMode,
                          pairedObservation(), dormant };
    // A size that differs from the previous paint is a corner drag or a Capture layout: building
    // an image for every step costs more than drawing once. The image of the last held size is
    // kept, so the editor size is served from it again after a Capture.
    const bool sizeHeld = paintedWidth == 0
                       || (paintedWidth == getWidth() && paintedHeight == getHeight());
    paintedWidth = getWidth();
    paintedHeight = getHeight();
    const bool cached = chromeImage.isValid() && key == chromeKey;
    if (! cached && (! cacheable || ! sizeHeld))
    {
        drawChrome (g, shape, dormant);
        return;
    }
    if (! cached)
    {
        juce::Image next (juce::Image::ARGB, pixelWidth, pixelHeight, true);
        {
            juce::Graphics pixels (next);
            pixels.addTransform (juce::AffineTransform::scale (scale));
            drawChrome (pixels, shape, dormant);
        }
        chromeImage = next;
        chromeKey = key;
    }
    juce::Graphics::ScopedSaveState saved (g);
    g.setOpacity (1.0f);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    // The image is already device resolution, so it keeps its physical size at fractional DPI.
    g.drawImage (chromeImage, { 0.0f, 0.0f, static_cast<float> (pixelWidth) / scale,
                                static_cast<float> (pixelHeight) / scale });
}

void AttackComponent::drawChrome (juce::Graphics& g, const attack_ui::Layout& shape, bool dormant)
{
    // ATTACK owns the Observatory body while selected. Keep the body opaque so the HISTORY
    // labels beneath this child cannot leak into its transparent header or capture composite.
    surface_material::paintPanel (g, getLocalBounds().toFloat(), 1.0f);
    if (shape.arrangement != attack_ui::Arrangement::glance)
        drawHeaderChrome (g, shape);
    if (dormant || shape.arrangement == attack_ui::Arrangement::header)
        return;
    const auto history = rectangleOf (attack_ui::historyPlot (shape));
    if (! history.isEmpty())
        drawHistoryChrome (g, history);
    if (shape.arrangement == attack_ui::Arrangement::lanes)
    {
        attack_lane_painter::paintHistoryLabel (
            g, rectangleOf (attack_ui::labelCell (shape, shape.history)), presentationContext);
        if (shape.loupe)
            attack_loupe::paintPanel (g, rectangleOf (attack_ui::loupeArea (shape)));
        for (std::size_t index = 0; index < attack_ui::laneCount; ++index)
            attack_lane_painter::paintLaneChrome (
                g, attack_lanes::lanes[index],
                rectangleOf (attack_ui::labelCell (shape, shape.lanes[index])),
                rectangleOf (attack_ui::lanePlot (shape, index)), pairedObservation(),
                presentationContext);
    }
    if (! shape.axis.empty())
    {
        auto axis = rectangleOf (attack_ui::axisPlot (shape));
        const auto labelWidth = attack_ui::axisLabelWidth (shape);
        g.setColour (waveformColour.withAlpha (0.28f));
        g.drawHorizontalLine (axis.getCentreY() - 2, static_cast<float> (axis.getX() + labelWidth),
                              static_cast<float> (axis.getRight() - labelWidth));
        attack_depth::engraveLip (g, static_cast<float> (axis.getCentreY() - 2),
                                  static_cast<float> (axis.getX() + labelWidth),
                                  static_cast<float> (axis.getRight() - labelWidth));
        g.setFont (monoFont (presentationContext, typography::TextRole::axis, visualization));
        g.setColour (COL_TEXT_TERTIARY);
        g.drawText ("-6 s", axis.removeFromLeft (labelWidth), juce::Justification::centredLeft);
    }
}

void AttackComponent::drawHeaderChrome (juce::Graphics& g, const attack_ui::Layout& shape)
{
    auto header = rectangleOf (shape.header);
    auto titleRow = header.removeFromTop (attack_ui::titleRowHeight (presentationContext));
    auto viewButton = titleRow.removeFromRight (viewControlWidth());
    const auto titleFont = attack_stage::trackedFont (
        presentationContext, typography::TextRole::sectionTitle, 0.08f);
    g.setFont (monoFont (presentationContext, typography::TextRole::sectionTitle, visualization)
                   .withExtraKerningFactor (0.08f));
    const auto& depth = attack_depth::look();
    if (depth.engrave > 0.0f)
    {
        // Etched into the instrument face: the impression sits one pixel below the lit letters.
        g.setColour (juce::Colours::black.withAlpha (0.75f * depth.engrave));
        g.drawText ("DRUM / ATTACK", titleRow.translated (0, 1), juce::Justification::centredLeft);
    }
    g.setColour (COL_NORMAL);
    g.drawText ("DRUM / ATTACK", titleRow, juce::Justification::centredLeft);
    // The selected TIME page carries one short cyan light under its name.
    const auto accentWidth = juce::jmin (44.0f, titleFont.getStringWidthFloat ("DRUM"));
    const auto accentY = static_cast<float> (titleRow.getBottom()) - 1.5f;
    if (depth.bloom > 0.0f)
    {
        g.setColour (waveformColour.withAlpha (juce::jmin (1.0f, 0.07f * depth.bloom)));
        g.fillRoundedRectangle ({ static_cast<float> (titleRow.getX()) - 2.0f, accentY - 3.5f,
                                  accentWidth + 4.0f, 7.0f }, 3.5f);
    }
    g.setColour (waveformColour.withAlpha (0.16f));
    g.fillRoundedRectangle ({ static_cast<float> (titleRow.getX()), accentY - 1.5f,
                              accentWidth, 3.0f }, 1.5f);
    g.setColour (waveformColour.withAlpha (0.85f));
    g.fillRect (juce::Rectangle<float> (static_cast<float> (titleRow.getX()), accentY - 0.5f,
                                        accentWidth, 1.0f));
    surface_material::paintControl (
        g, viewButton.reduced (1).toFloat(), false, false, overlayMode, waveformColour, 3.0f);
    g.setColour (COL_NORMAL);
    g.setFont (monoFont (presentationContext, typography::TextRole::action, visualization));
    g.drawText (overlayMode ? "VIEW  2 ROWS" : "VIEW  OVERLAY",
                viewButton, juce::Justification::centred);

    if (getWidth() >= 470)
        header.removeFromRight (statusControlWidth());
    const bool paired = pairedObservation();
    g.setColour (COL_TEXT_SECONDARY);
    if (shape.arrangement == attack_ui::Arrangement::lanes)
        attack_lane_painter::drawFitting (g, paired
            ? std::initializer_list<juce::String> {
                  "10 ms RMS / 6 S   PRE trace / POST body   bars POST - PRE",
                  "RMS / 6 S / bars POST-PRE", "bars POST-PRE" }
            : std::initializer_list<juce::String> {
                  "10 ms RMS / 6 S   POST body   bars POST values",
                  "RMS / 6 S / POST values", "POST values" },
            header, presentationContext, typography::TextRole::legend,
            juce::Justification::centredLeft);
    else if (shape.arrangement == attack_ui::Arrangement::line)
        attack_lane_painter::drawFitting (g, { paired ? "6 S / POST-PRE" : "6 S / POST" },
            header, presentationContext, typography::TextRole::legend,
            juce::Justification::centredLeft);
}

void AttackComponent::drawHistoryChrome (juce::Graphics& g, juce::Rectangle<int> plot)
{
    attack_stage::paint (g, plot.toFloat(), 4.0f, 0.30f, true);
    const auto& depth = attack_depth::look();
    for (int second = 1; second < attack_ui::presentationSeconds; ++second)
    {
        const auto x = plot.getX() + second * plot.getWidth() / attack_ui::presentationSeconds;
        g.setColour (waveformColour.withAlpha (second == 3 ? 0.10f : 0.035f));
        g.drawVerticalLine (x, static_cast<float> (plot.getY() + 4),
                            static_cast<float> (plot.getBottom() - 4));
    }
    if (depth.graticule > 0.0f)
    {
        // Instrument ticks on the upper and lower walls: every 0.25 s, longer at each second.
        constexpr int quarters = attack_ui::presentationSeconds * 4;
        g.setColour (COL_TEXT_TERTIARY.withAlpha (juce::jmin (1.0f, depth.graticule * 2.0f)));
        for (int quarter = 1; quarter < quarters; ++quarter)
        {
            const auto x = plot.getX() + quarter * plot.getWidth() / quarters;
            const auto length = quarter % 4 == 0 ? 5.0f : 2.5f;
            const auto top = static_cast<float> (plot.getY()) + 1.0f;
            const auto bottom = static_cast<float> (plot.getBottom()) - 1.0f;
            g.drawVerticalLine (x, top, top + length);
            g.drawVerticalLine (x, bottom - length, bottom);
        }
    }
    // Faint -24 and -48 dBFS levels on each envelope band, the envelope's own scale.
    std::array<juce::Rectangle<int>, 2> bands;
    const auto bandCount = envelopeBands (plot, bands);
    if (bandCount == 2)
    {
        g.setColour (waveformColour.withAlpha (0.075f));
        g.drawHorizontalLine (plot.getY() + plot.getHeight() / 2, static_cast<float> (plot.getX() + 3),
                              static_cast<float> (plot.getRight() - 3));
    }
    for (std::size_t index = 0; index < bandCount; ++index)
    {
        const auto band = bands[index].toFloat();
        const auto level = [&band] (float db) {
            const auto fraction = (db - attack_ui::absoluteFloorDb) / -attack_ui::absoluteFloorDb;
            return band.getBottom() - 0.5f - fraction * juce::jmax (0.0f, band.getHeight() - 1.5f); };
        g.setColour (COL_TEXT_TERTIARY.withAlpha (0.08f));
        for (const auto db : { -24.0f, -48.0f })
            g.drawHorizontalLine (juce::roundToInt (level (db)), band.getX() + 2.0f, band.getRight() - 2.0f);
        // The floor the light rises from, and a scale tick every 12 dB on both walls.
        g.setColour (waveformColour.withAlpha (0.14f));
        g.drawHorizontalLine (juce::roundToInt (band.getBottom() - 0.5f), band.getX() + 2.0f,
                              band.getRight() - 2.0f);
        attack_depth::engraveLip (g, band.getBottom() - 0.5f, band.getX() + 2.0f, band.getRight() - 2.0f);
        g.setColour (COL_TEXT_TERTIARY.withAlpha (juce::jmin (1.0f, depth.graticule * 2.0f)));
        for (int db = -12; db > static_cast<int> (attack_ui::absoluteFloorDb); db -= 12)
        {
            const auto y = juce::roundToInt (level (static_cast<float> (db)));
            g.drawHorizontalLine (y, band.getX() + 1.0f, band.getX() + 4.0f);
            g.drawHorizontalLine (y, band.getRight() - 4.0f, band.getRight() - 1.0f);
        }
    }
    // NOW: the current measurement edge in cyan, with a narrow glow.
    const auto nowX = static_cast<float> (plot.getRight()) - 1.5f;
    if (depth.bloom > 0.0f)
    {
        g.setColour (waveformColour.withAlpha (juce::jmin (1.0f, 0.045f * depth.bloom)));
        g.fillRect (juce::Rectangle<float> (nowX - 3.5f, static_cast<float> (plot.getY() + 2), 5.0f,
                                            static_cast<float> (plot.getHeight() - 4)));
    }
    g.setColour (waveformColour.withAlpha (0.12f));
    g.fillRect (juce::Rectangle<float> (nowX - 1.5f, static_cast<float> (plot.getY() + 2), 3.0f,
                                        static_cast<float> (plot.getHeight() - 4)));
    g.setColour (waveformColour.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<float> (nowX - 0.5f, static_cast<float> (plot.getY() + 2), 1.0f,
                                        static_cast<float> (plot.getHeight() - 4)));
}

// The only live header fact: pairing and whether time follows LIVE, is held, or is locked. Below
// 470 px the legend needs the row; the axis row states the same LIVE / HOLD / LOCK there.
void AttackComponent::paintHeaderState (juce::Graphics& g, const attack_ui::Layout& shape)
{
    if (getWidth() < 470)
        return;
    auto header = rectangleOf (shape.header);
    header.removeFromTop (attack_ui::titleRowHeight (presentationContext));
    const auto state = header.removeFromRight (statusControlWidth());
    const auto text = juce::String (pairedObservation() ? "PAIR / " : "POST / ") + timeMode();
    const auto font = monoFont (presentationContext, typography::TextRole::status, visualization);
    g.setColour (COL_TEXT_SECONDARY);
    g.setFont (monoFont (presentationContext, typography::TextRole::status, visualization));
    g.drawText (text, state, juce::Justification::centredRight);
    const auto dotColour = ! followLatest ? juce::Colour (attack_ui::selectionColour)
                         : liveSignalActive ? waveformColour : COL_FLORA;
    const auto dotX = static_cast<float> (state.getRight()) - font.getStringWidthFloat (text) - 8.0f;
    const auto dotY = static_cast<float> (state.getCentreY());
    if (dotX < static_cast<float> (state.getX()))
        return;
    g.setColour (dotColour.withAlpha (0.20f));
    g.fillEllipse (dotX - 4.5f, dotY - 4.5f, 9.0f, 9.0f);
    g.setColour (dotColour);
    g.fillEllipse (dotX - 2.2f, dotY - 2.2f, 4.4f, 4.4f);
}
}
