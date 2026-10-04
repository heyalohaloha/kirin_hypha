#include "HyphaReferenceVersionPage.h"

#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace hypha::reference_ui
{
namespace
{
constexpr int minimumPairFrames = 30;  // 位置合わせで対応した A・V が 3 秒に満たないあいだは比べない

juce::String signedDb (double value)
{
    return (value >= 0.0 ? "+" : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))) + juce::String (std::abs (value), 1);
}

float dbY (double db, juce::Rectangle<float> area)
{
    constexpr double minimumDb = -120.0, maximumDb = 6.0;  // HyphaReferenceVisuals と同じ縦軸
    return area.getBottom() - static_cast<float> ((juce::jlimit (minimumDb, maximumDb, db) - minimumDb) / (maximumDb - minimumDb))
        * area.getHeight();
}

float logX (double hz, juce::Rectangle<float> area, double maximumHz)
{
    constexpr double minimumHz = 20.0;
    const auto normalized = std::log (juce::jlimit (minimumHz, maximumHz, hz) / minimumHz) / std::log (maximumHz / minimumHz);
    return area.getX() + static_cast<float> (normalized) * area.getWidth();
}

juce::Path medianPath (const reference_audition::KirinSpectrumWindow& window, double shift, juce::Rectangle<float> area,
                       double maximumHz)
{
    juce::Path path;
    bool started = false;
    for (size_t index = 0; index < window.centersHz.size() && index < window.medianDb.size(); ++index)
    {
        if (window.centersHz[index] > maximumHz * 1.12) break;  // 描く幅の外（次の帯域の手前まで）
        const juce::Point<float> point { logX (window.centersHz[index], area, maximumHz), dbY (window.medianDb[index] + shift, area) };
        if (! started) path.startNewSubPath (point); else path.lineTo (point);
        started = true;
    }
    return path;
}

bool has (const std::vector<juce::String>& views, const char* name)
{
    return std::find (views.begin(), views.end(), juce::String (name)) != views.end();
}
}

bool sameSectionReady (const reference_audition::VisualTimeline* timeline) noexcept
{
    if (timeline == nullptr || ! timeline->aPairKirin || ! timeline->vPairKirin) return false;
    const auto& a = *timeline->aPairKirin;
    const auto& v = *timeline->vPairKirin;
    return a.frames >= minimumPairFrames && v.frames == a.frames && a.centersHz.size() == v.centersHz.size() && ! a.centersHz.empty();
}

void paintVersionSameSection (juce::Graphics& g, juce::Rectangle<int> area, const reference_audition::VisualTimeline* timeline,
                              const juce::String& checkLabel, double gainDb, const std::vector<juce::String>& views,
                              presentation::Context context)
{
    // 表示の分からない Check（古い Kirin OS）は今までどおり全帯域のスペクトル。
    const bool spectral = views.empty() || has (views, "spectrum_full") || has (views, "spectrum_low") || has (views, "balance");
    const bool lowOnly = has (views, "spectrum_low") && ! has (views, "spectrum_full");
    const double maximumHz = lowOnly ? 250.0 : 20'000.0;
    const bool ready = sameSectionReady (timeline);
    const auto shift = std::isfinite (gainDb) ? gainDb : 0.0;
    auto bands = area.removeFromBottom (40);
    area.removeFromBottom (6);
    surface_material::paintPanel (g, area.toFloat(), 0.72f);
    auto header = area.removeFromTop (28).reduced (9, 1);
    g.setColour (COL_NORMAL.withAlpha (0.92f));
    g.setFont (labelFont (context, typography::TextRole::metricLabel, typography::Composition::visualization));
    text_style::drawEllipsized (g, checkLabel, header.removeFromLeft (header.getWidth() * 2 / 5), juce::Justification::centredLeft);
    g.setFont (labelFont (context, typography::TextRole::legend, typography::Composition::visualization));
    g.setColour (COL_TEXT_TERTIARY);
    const auto legend = ready ? "SAME SECTION " + juce::String (juce::roundToInt (timeline->aPairKirin->frames / 10.0)) + " S"
                                    + (lowOnly ? juce::String (" / 20-250 HZ") : juce::String {})
                                    + (std::isfinite (gainDb) ? juce::String {} : juce::String (" / LEVEL NOT MATCHED"))
                              : juce::String ("PLAY A WITH V ALIGNED");
    text_style::drawEllipsized (g, legend, header, juce::Justification::centredRight);
    if (ready && spectral)  // 凡例の色（A は金の太い線、V は水色の細い線。図と同じ）
    {
        auto keys = header.withTrimmedRight (juce::roundToInt (std::ceil (text_style::shownWidth (g.getCurrentFont(), legend))) + 14);
        for (const auto& [name, colour, thickness] : { std::tuple { "V", COL_SPECTRUM_DELTA, 1.4f }, std::tuple { "A", COL_FLORA_BR, 2.8f } })
        {
            auto key = keys.removeFromRight (44);
            g.setColour (COL_TEXT_TERTIARY);
            text_style::drawEllipsized (g, name, key.removeFromRight (14), juce::Justification::centredLeft);
            g.setColour (colour);
            g.fillRect (key.removeFromRight (22).toFloat().withSizeKeepingCentre (18.0f, thickness));
        }
    }
    auto chart = area.toFloat().reduced (10.0f, 8.0f);
    if (ready && spectral)
    {
        // 2026-10-04（Daisuke「A と V が両方表示された方が便利」）：A を太く下に、V を細く上に。同じ値でも両方見える。
        g.setColour (COL_FLORA_BR.withAlpha (0.9f));
        g.strokePath (medianPath (*timeline->aPairKirin, 0.0, chart, maximumHz), juce::PathStrokeType (2.8f));
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.95f));
        g.strokePath (medianPath (*timeline->vPairKirin, shift, chart, maximumHz), juce::PathStrokeType (1.4f));
    }
    else
    {
        g.setColour (COL_TEXT_SECONDARY.withAlpha (0.92f));
        g.setFont (labelFont (context, typography::TextRole::status, typography::Composition::visualization));
        text_style::drawEllipsized (g, ! spectral ? "V compares spectrum and balance. This Check is shown on C."
                                                  : "V is measured over the same section as A while they are aligned",
                                    chart.toNearestInt(), juce::Justification::centred);
    }
    // 4 帯域の V−A（数字だけ、良し悪しの色は付けない）。
    static constexpr const char* names[] { "LOW 20-250", "LOW-MID 250-2k", "MID 2k-8k", "HIGH 8k-20k" };
    constexpr int gap = 6;
    const auto width = (bands.getWidth() - gap * 3) / 4;
    for (size_t band = 0; band < 4; ++band)
    {
        auto cell = bands.removeFromLeft (width);
        bands.removeFromLeft (gap);
        surface_material::paintPanel (g, cell.toFloat(), 0.6f);
        auto inner = cell.reduced (8, 3);
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
        text_style::drawEllipsized (g, names[band], inner.removeFromTop (inner.getHeight() / 2), juce::Justification::centredLeft);
        const auto a = ready ? timeline->aPairKirin->balanceDb[band] : std::numeric_limits<double>::quiet_NaN();
        const auto v = ready ? timeline->vPairKirin->balanceDb[band] : std::numeric_limits<double>::quiet_NaN();
        const bool shown = std::isfinite (a) && std::isfinite (v) && a > -200.0 && std::isfinite (gainDb);
        g.setColour (shown ? COL_OBSERVATORY_VALUE : COL_MUTED);
        g.setFont (monoFont (context, typography::TextRole::readout, typography::Composition::information));
        text_style::drawEllipsized (g, shown ? signedDb (v + shift - a) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                                    inner.removeFromLeft (inner.getWidth() * 3 / 5), juce::Justification::centredLeft);
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
        text_style::drawEllipsized (g, "dB V-A", inner, juce::Justification::centredRight);
    }
}
}
