#include "HyphaAttackComponent.h"
#include "HyphaAttackPainter.h"
#include "HyphaAttackUiContract.h"
#include "HyphaTheme.h"

namespace hypha
{
namespace
{
const auto strengthColour = juce::Colour (attack_ui::strengthColour);
const auto textureColour = juce::Colour (attack_ui::textureColour);
const auto brightnessColour = juce::Colour (attack_ui::brightnessColour);
const auto transientColour = juce::Colour (attack_ui::transientColour);
const auto selectionColour = juce::Colour (attack_ui::selectionColour);
juce::String signedValue (float value, int decimals = 1)
{ return (value >= 0.0f ? "+" : "") + juce::String (value, decimals); }
}
void AttackComponent::paintSelectedEvent (juce::Graphics& g, juce::Rectangle<int> metrics)
{
    using attack_painter::drawEventFocus;
    using attack_painter::drawMetricFact;
    const auto textScale = attack_ui::textScale (getWidth(), getHeight());
    const auto* preDetail = selectedPreDetail();
    const auto* postDetail = selectedPostDetail();
    if (metrics.isEmpty())
        return;
    if (postDetail == nullptr)
    {
        g.setColour (COL_NORMAL); g.setFont (monoFont (12.0f));
        g.drawText (preDetail != nullptr ? "PRE-only event / no matching POST"
            : followLatest ? "Waiting for event detail" : "Locked event is outside retained detail",
            metrics, juce::Justification::centred);
        return;
    }

    metrics = metrics.reduced (1);
    g.setColour (selectionColour.withAlpha (0.16f));
    g.drawRoundedRectangle (metrics.toFloat(), 4.0f, 0.75f);

    auto content = metrics.reduced (7, 3);
    auto focusHeader = content.getHeight() >= 60 ? content.removeFromTop (18) : juce::Rectangle<int> {};
    g.setColour (COL_MUTED);
    g.setFont (monoFont (6.5f * textScale));
    const auto beforeMs = postDetail->sample_rate > 0
        ? (postDetail->event_sample - postDetail->shape_start_sample) * 1'000
            / static_cast<std::int64_t> (postDetail->sample_rate) : 0;
    const auto afterMs = postDetail->sample_rate > 0
        ? (postDetail->shape_end_sample - postDetail->event_sample) * 1'000
            / static_cast<std::int64_t> (postDetail->sample_rate) : 0;
    const auto target = juce::String (preDetail != nullptr ? "POST - PRE" : "POST ABSOLUTE");
    g.drawText (getWidth() < 430 ? target
                : (followLatest ? "LATEST / -" : "LOCKED / -") + juce::String (beforeMs)
                    + "..+" + juce::String (afterMs) + " ms / " + target,
                focusHeader, juce::Justification::centred);

    const auto pairedDetail = preDetail != nullptr;
    const auto strengthValue = pairedDetail
        ? signedValue (postDetail->attack_rms_dbfs - preDetail->attack_rms_dbfs) + " dB"
        : juce::String (postDetail->attack_rms_dbfs, 1) + " dBFS";
    const auto strengthContext = pairedDetail
        ? "PRE " + juce::String (preDetail->attack_rms_dbfs, 1)
            + "  POST " + juce::String (postDetail->attack_rms_dbfs, 1)
        : "30 ms ATTACK RMS";
    const auto edge = pairedDetail
        ? postDetail->sample_edge_ratio_db - preDetail->sample_edge_ratio_db
        : postDetail->sample_edge_ratio_db;
    const auto crest = pairedDetail ? postDetail->crest_db - preDetail->crest_db
                                    : postDetail->crest_db;
    const auto plateau = pairedDetail
        ? postDetail->peak_plateau_ms - preDetail->peak_plateau_ms
        : postDetail->peak_plateau_ms;
    const auto textureValue = (pairedDetail ? signedValue (edge) : juce::String (edge, 1))
                            + " dB";
    const auto textureContext = "CREST " + (pairedDetail ? signedValue (crest)
                                                        : juce::String (crest, 1))
                              + "  PLAT " + (pairedDetail ? signedValue (plateau, 2)
                                                          : juce::String (plateau, 2));
    const bool brightnessAvailable = postDetail->sharpness_available != 0
        && (! pairedDetail || preDetail->sharpness_available != 0);
    const auto brightnessValue = brightnessAvailable
        ? (pairedDetail ? signedValue (postDetail->sharpness_acum - preDetail->sharpness_acum, 2)
                        : juce::String (postDetail->sharpness_acum, 2)) + " acum"
        : "---";
    const auto brightnessContext = pairedDetail ? "SHARPNESS DIFFERENCE" : "100 ms SHARPNESS";
    const auto transientValue = pairedDetail
        ? signedValue (postDetail->contrast_db - preDetail->contrast_db) + " dB"
        : juce::String (postDetail->contrast_db, 1) + " dB";
    const auto transientContext = pairedDetail ? "CONTRAST DIFFERENCE" : "LOCAL CONTRAST";

    if (content.getWidth() >= 390 && content.getHeight() >= 65)
    {
        const auto sideWidth = juce::jmin (textScale > 1.4f ? 178 : 112,
                                           content.getWidth() / 4);
        auto left = content.removeFromLeft (sideWidth);
        auto right = content.removeFromRight (sideWidth);
        auto specimen = content.reduced (4, 1);
        const auto phase = rate > 0 ? juce::jlimit (0.0f, 1.0f,
            static_cast<float> (latest - postDetail->event_sample) / (0.42f * rate)) : 1.0f;
        drawEventFocus (g, preDetail, postDetail, specimen, phase);
        auto leftTop = left.removeFromTop (left.getHeight() / 2).reduced (1);
        auto leftBottom = left.reduced (1);
        auto rightTop = right.removeFromTop (right.getHeight() / 2).reduced (1);
        auto rightBottom = right.reduced (1);
        drawMetricFact (g, leftTop, "STRENGTH", strengthValue, strengthContext,
                  strengthColour, false);
        drawMetricFact (g, leftBottom, "BRIGHTNESS", brightnessValue, brightnessContext,
                  brightnessColour, false);
        drawMetricFact (g, rightTop, "TEXTURE", textureValue, textureContext,
                  textureColour, true);
        drawMetricFact (g, rightBottom, "TRANSIENT", transientValue, transientContext,
                  transientColour, true);
    }
    else
    {
        const auto width = content.getWidth() / 4;
        auto strength = content.removeFromLeft (width);
        auto brightness = content.removeFromLeft (width);
        auto texture = content.removeFromLeft (width);
        drawMetricFact (g, strength, "STRENGTH", strengthValue, {}, strengthColour, false);
        drawMetricFact (g, brightness, "BRIGHT", brightnessValue, {}, brightnessColour, false);
        drawMetricFact (g, texture, "TEXTURE", textureValue, {}, textureColour, true);
        drawMetricFact (g, content, "TRANSIENT", transientValue, {}, transientColour, true);
    }
}
}
