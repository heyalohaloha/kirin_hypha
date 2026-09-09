#include "HyphaAttackComponent.h"

#include <cmath>
#include <limits>

#include "HyphaAttackOrganismPainter.h"
#include "HyphaAttackPainter.h"
#include "HyphaAttackUiContract.h"
#include "HyphaTheme.h"

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
                       float contrast, juce::Colour colour)
{
    area = area.reduced (4, 0);
    const bool compact = area.getWidth() < 120;
    const auto labelWidth = juce::jmin (compact ? 28 : 46, area.getWidth() / 3);
    g.setFont (monoFont (11.0f));
    g.setColour (COL_MUTED);
    g.drawText (label, area.removeFromLeft (labelWidth), juce::Justification::centredLeft);
    const auto valueWidth = juce::jmin (compact ? 42 : 68, area.getWidth() / 3);
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
    auto title = area.removeFromLeft (juce::jmin (88, area.getWidth() / 5));
    g.setColour (COL_NORMAL);
    g.setFont (monoFont (11.0f));
    g.drawText (getWidth() < 500 ? "TRANSIENT dB" : "TRANSIENT",
                title, juce::Justification::centredLeft);
    const auto sectionWidth = juce::jmax (1, area.getWidth() / 3);
    auto preArea = area.removeFromLeft (sectionWidth);
    auto postArea = area.removeFromLeft (sectionWidth);
    auto deltaArea = area;
    const auto missing = std::numeric_limits<float>::quiet_NaN();
    const auto preValue = pre != nullptr ? pre->contrast_db : missing;
    const auto postValue = post != nullptr ? post->contrast_db : missing;
    const auto delta = pre != nullptr && post != nullptr ? postValue - preValue : missing;
    const bool showUnits = getWidth() >= 500;
    const auto unit = showUnits ? " dB" : "";
    drawTransientBar (g, preArea, "PRE", std::isfinite (preValue)
        ? juce::String (preValue, 1) + unit : "--", preValue, COL_NORMAL);
    drawTransientBar (g, postArea, "POST", std::isfinite (postValue)
        ? juce::String (postValue, 1) + unit : "--", postValue, transientColour);
    drawTransientBar (g, deltaArea, getWidth() >= 700 ? "DELTA" : "D",
        std::isfinite (delta) ? signedValue (delta) + unit : "--",
        std::abs (delta), selectionColour);
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
        g.setFont (monoFont (12.0f));
        g.drawText (selectedPreDetail() != nullptr ? "PRE event / matching POST unavailable"
            : followLatest ? "Waiting for POST event" : "Locked event is outside retained detail",
            area, juce::Justification::centred);
        return;
    }

    if (area.getHeight() < 30)
    {
        const auto texture = attack_organism::textureAmount (*post);
        const auto width = juce::jmax (1, area.getWidth() / 3);
        g.setFont (monoFont (11.0f));
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
    g.setColour (selectionColour.withAlpha (0.16f));
    g.drawRoundedRectangle (area.toFloat(), 4.0f, 0.75f);
    auto content = area.reduced (7, 3);
    const bool canShowHeader = content.getHeight() >= 72;
    if (canShowHeader)
    {
        auto focusHeader = content.removeFromTop (18);
        g.setColour (COL_MUTED);
        g.setFont (monoFont (11.0f));
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

    const auto metricHeight = content.getHeight() >= 88 ? 48 : content.getHeight();
    auto metricRow = content.removeFromBottom (metricHeight);
    if (content.getHeight() >= 34 && content.getWidth() >= 150)
        drawEventFocus (g, nullptr, post, content.reduced (4, 1), {}, &glyphCache);

    const auto width = juce::jmax (1, metricRow.getWidth() / 3);
    auto strength = metricRow.removeFromLeft (width).reduced (3, 0);
    auto textureArea = metricRow.removeFromLeft (width).reduced (3, 0);
    auto sharpness = metricRow.reduced (3, 0);
    drawMetricFact (g, strength, "STRENGTH", strengthValue,
                    canShowHeader ? "30 ms ATTACK RMS" : "", strengthColour, false);
    drawMetricFact (g, textureArea, "TEXTURE", textureValue,
                    canShowHeader ? "EDGE / CREST / PLATEAU" : "", textureColour, false);
    drawMetricFact (g, sharpness, "SHARPNESS", sharpnessValue,
                    canShowHeader ? "100 ms ACUM" : "", sharpnessColour, true);
}
}
