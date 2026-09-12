#include "HyphaAttackComponent.h"

#include <cmath>
#include <limits>

#include "HyphaAttackOrganismPainter.h"
#include "HyphaAttackPainter.h"
#include "HyphaAttackUiContract.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

namespace hypha
{
namespace
{
const auto strengthColour = juce::Colour (attack_ui::strengthColour);
const auto textureColour = juce::Colour (attack_ui::textureColour);
const auto sharpnessColour = juce::Colour (attack_ui::sharpnessColour);
const auto transientColour = juce::Colour (attack_ui::transientColour);
const auto selectionColour = juce::Colour (attack_ui::selectionColour);

juce::String signedValue (float value, int decimals = 1)
{
    return (value >= 0.0f ? "+" : "") + juce::String (value, decimals);
}

void drawTransientBar (juce::Graphics& g, juce::Rectangle<int> area,
                       const juce::String& label, const juce::String& value,
                       float contrast, juce::Colour colour,
                       presentation::Context presentation)
{
    area = area.reduced (4, 0);
    const auto style = typography::resolve (
        presentation, typography::TextRole::readout,
        typography::Composition::visualization);
    const auto font = monoFont (presentation, typography::TextRole::readout,
                                typography::Composition::visualization);
    const auto labelWidth = juce::jmin (
        text_style::requiredWidth (font, label, style), area.getWidth());
    g.setFont (monoFont (presentation, typography::TextRole::readout,
                         typography::Composition::visualization));
    g.setColour (COL_TEXT_SECONDARY);
    g.drawText (label, area.removeFromLeft (labelWidth), juce::Justification::centredLeft);
    const auto valueWidth = juce::jmin (
        text_style::requiredWidth (font, value, style), area.getWidth());
    auto valueArea = area.removeFromRight (valueWidth);
    auto rail = area.reduced (3, juce::jmax (3, area.getHeight() / 3));
    g.setColour (colour.withAlpha (0.14f));
    g.fillRoundedRectangle (rail.toFloat(), 2.0f);
    if (std::isfinite (contrast))
    {
        const auto fraction = juce::jlimit (0.0f, 1.0f, contrast / 18.0f);
        rail.setWidth (static_cast<int> (std::lround (rail.getWidth() * fraction)));
        g.setColour (colour.withAlpha (0.86f));
        g.fillRoundedRectangle (rail.toFloat(), 2.0f);
    }
    g.setColour (colour);
    g.drawText (value, valueArea, juce::Justification::centredRight);
}
}

void AttackComponent::paintTransientComparison (juce::Graphics& g,
                                                 juce::Rectangle<int> area)
{
    if (area.isEmpty()) return;
    const auto* pre = selectedPreDetail();
    const auto* post = selectedPostDetail();
    area = area.reduced (7, 2);
    const auto titleText = juce::String (attack_ui::transientTitle (presentationContext));
    const auto titleStyle = typography::resolve (
        presentationContext, typography::TextRole::sectionTitle,
        typography::Composition::visualization);
    const auto titleFont = monoFont (
        presentationContext, typography::TextRole::sectionTitle,
        typography::Composition::visualization);
    auto title = area.removeFromLeft (attack_ui::transientTitleWidth (
        area.getWidth(), text_style::requiredWidth (titleFont, titleText, titleStyle)));
    g.setColour (COL_NORMAL);
    g.setFont (monoFont (presentationContext, typography::TextRole::sectionTitle,
                         typography::Composition::visualization));
    g.drawText (titleText, title, juce::Justification::centredLeft);
    const auto sectionWidth = juce::jmax (1, area.getWidth() / 3);
    auto preArea = area.removeFromLeft (sectionWidth);
    auto postArea = area.removeFromLeft (sectionWidth);
    auto deltaArea = area;
    const auto missing = std::numeric_limits<float>::quiet_NaN();
    const auto preValue = pre != nullptr ? pre->contrast_db : missing;
    const auto postValue = post != nullptr ? post->contrast_db : missing;
    const auto delta = pre != nullptr && post != nullptr ? postValue - preValue : missing;
    drawTransientBar (g, preArea, "PRE", std::isfinite (preValue)
        ? juce::String (preValue, 1) : "--", preValue, COL_NORMAL,
        presentationContext);
    drawTransientBar (g, postArea, "POST", std::isfinite (postValue)
        ? juce::String (postValue, 1) : "--", postValue, transientColour,
        presentationContext);
    drawTransientBar (g, deltaArea, getWidth() >= 700 ? "DELTA" : "D",
        std::isfinite (delta) ? signedValue (delta) : "--",
        std::abs (delta), selectionColour, presentationContext);
}

void AttackComponent::paintSelectedEvent (juce::Graphics& g, juce::Rectangle<int> area)
{
    using attack_painter::drawEventFocus;
    using attack_painter::drawMetricFact;
    if (area.isEmpty()) return;
    const auto* post = selectedPostDetail();
    if (post == nullptr)
    {
        g.setColour (COL_NORMAL);
        g.setFont (monoFont (presentationContext, typography::TextRole::status,
                             typography::Composition::visualization));
        g.drawText (selectedPreDetail() != nullptr ? "PRE event / matching POST unavailable"
            : followLatest ? "Waiting for POST event" : "Locked event is outside retained detail",
            area, juce::Justification::centred);
        return;
    }

    if (area.getHeight() < 30)
    {
        const auto texture = attack_organism::textureAmount (*post);
        const auto width = juce::jmax (1, area.getWidth() / 3);
        g.setFont (monoFont (presentationContext, typography::TextRole::readout,
                             typography::Composition::visualization));
        g.setColour (strengthColour);
        g.drawText (std::isfinite (post->attack_rms_dbfs)
                        ? juce::String (post->attack_rms_dbfs, 1) + " dBFS" : "---",
                    area.removeFromLeft (width),
                    juce::Justification::centred);
        g.setColour (textureColour);
        g.drawText (attack_organism::textureAvailable (*post)
                        ? juce::String (texture, 2) : "---",
                    area.removeFromLeft (width),
                    juce::Justification::centred);
        g.setColour (sharpnessColour);
        g.drawText (post->sharpness_available != 0 && std::isfinite (post->sharpness_acum)
                        ? juce::String (post->sharpness_acum, 2) + " acum" : "---",
                    area, juce::Justification::centred);
        return;
    }

    area = area.reduced (1);
    surface_material::paintPanel (g, area.toFloat(), 0.22f);
    g.setColour (selectionColour.withAlpha (0.16f));
    g.drawRoundedRectangle (area.toFloat(), 4.0f, 0.75f);
    auto content = area.reduced (7, 3);
    const bool canShowHeader = content.getHeight() >= 72;
    if (canShowHeader)
    {
        const auto focusHeaderHeight = text_style::requiredLineHeight (typography::resolve (
            presentationContext, typography::TextRole::legend,
            typography::Composition::visualization));
        auto focusHeader = content.removeFromTop (focusHeaderHeight);
        g.setColour (COL_TEXT_SECONDARY);
        g.setFont (monoFont (presentationContext, typography::TextRole::legend,
                             typography::Composition::visualization));
        g.drawText ((followLatest ? "LATEST / " : "LOCKED / ")
                       + juce::String ("POST SPECIMEN"),
                    focusHeader, juce::Justification::centred);
    }

    const auto strengthValue = std::isfinite (post->attack_rms_dbfs)
        ? juce::String (post->attack_rms_dbfs, 1) + " dBFS" : "---";
    const auto texture = attack_organism::textureAmount (*post);
    const auto textureValue = attack_organism::textureAvailable (*post)
        ? juce::String (texture, 2) : "---";
    const auto sharpnessValue = post->sharpness_available != 0
        && std::isfinite (post->sharpness_acum)
        ? juce::String (post->sharpness_acum, 2) + " acum" : "---";

    const auto labelHeight = text_style::requiredLineHeight (typography::resolve (
        presentationContext, typography::TextRole::metricLabel,
        typography::Composition::visualization));
    const auto valueHeight = text_style::requiredLineHeight (typography::resolve (
        presentationContext, typography::TextRole::primaryValue,
        typography::Composition::visualization));
    const auto contextHeight = canShowHeader
        ? text_style::requiredLineHeight (typography::resolve (
              presentationContext, typography::TextRole::body,
              typography::Composition::visualization))
        : 0;
    const auto metricHeight = juce::jmin (
        content.getHeight(), labelHeight + valueHeight + contextHeight);
    auto metricRow = content.removeFromBottom (metricHeight);
    if (content.getHeight() >= 34 && content.getWidth() >= 150)
        drawEventFocus (g, nullptr, post, content.reduced (4, 1), {}, &glyphCache);

    const auto width = juce::jmax (1, metricRow.getWidth() / 3);
    auto strength = metricRow.removeFromLeft (width).reduced (3, 0);
    auto textureArea = metricRow.removeFromLeft (width).reduced (3, 0);
    auto sharpness = metricRow.reduced (3, 0);
    drawMetricFact (g, strength, "STRENGTH", strengthValue,
                    canShowHeader ? "30 ms ATTACK RMS" : "", strengthColour,
                    presentationContext);
    drawMetricFact (g, textureArea, "TEXTURE", textureValue,
                    canShowHeader ? "EDGE / CREST / PLATEAU" : "", textureColour,
                    presentationContext);
    drawMetricFact (g, sharpness, "SHARPNESS", sharpnessValue,
                    canShowHeader ? "100 ms ACUM" : "", sharpnessColour,
                    presentationContext);
}
}
