// Planning-only geometry and glyph-width audit. No plug-in or DSP implementation.
// Build instructions and scope limitations are recorded in
// docs/hypha_spectrum_mid_side_overlay_plan_20260911.md.
#include <juce_gui_basics/juce_gui_basics.h>
#include "HyphaTheme.h"
#include "HyphaObservatoryContract.h"
#include "HyphaSpectrumGeometry.h"
#include <cmath>
#include <iomanip>
#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    using namespace hypha;
    std::cout << std::fixed << std::setprecision(2);
    int checked = 0, failures = 0;
    float minGap = 1e9f, minReadoutSpare = 1e9f, minHeight = 1e9f;
    for (int width = 300; width <= 900; ++width)
    {
        const int height = (width * 2 + 1) / 3;
        auto preset = observatory::presetForWidth(width);
        preset.width = width; preset.height = height;
        const auto context = presentation::forEditor(width, height);
        const auto nav = monoFont(context, typography::TextRole::navigation,
                                  typography::Composition::visualization);
        const auto readout = monoFont(context, typography::TextRole::readout,
                                      typography::Composition::visualization);
        const auto unit = monoFont(context, typography::TextRole::unit,
                                   typography::Composition::visualization);
        for (const auto guide : { observatory::GuidePresence::absent,
                                   observatory::GuidePresence::present })
        {
            const auto shell = observatory::shellLayout(observatory::Role::post, preset, guide);
            const juce::Rectangle<float> bounds(0, 0, shell.body.width, shell.body.height);
            const auto outer = spectrum_geometry::plotBoundsFor(bounds);
            const auto plot = spectrum_geometry::dataPlotBoundsFor(bounds, false);
            const float scale = spectrum_geometry::visualScaleFor(bounds);
            const float gap = std::max(2.0f, 2.0f * scale);
            float modeWidth = 0;
            const char* labels[] = {"LR", "MID", "SIDE", "M/S"};
            const float bases[] = {20, 26, 30, 32};
            for (int i=0; i<4; ++i)
                modeWidth += std::max(bases[i] * scale,
                    std::ceil(nav.getStringWidthFloat(labels[i]) + nav.getHeight() * 0.5f));
            modeWidth += 3 * gap;
            const float switchWidth = std::max(64 * scale,
                std::ceil(nav.getStringWidthFloat("SPECTRUM") + nav.getHeight() * 0.5f));
            const float markWidth = 42 * scale;
            const float spare = outer.getWidth() - modeWidth - switchWidth - markWidth - gap;
            const bool compact = width < 413;
            const auto read = [&](const char* label) {
                return std::ceil(tabularTextWidth(readout, label) + readout.getHeight() * 0.5f);
            };
            const float frequencyWidth = std::max({read("~35 Hz"), read("999 Hz"),
                                                   read("9.99 kHz"), read("22.0 kHz")});
            const float cells = frequencyWidth
                + read(compact ? "M -144.0" : "MID -144.0")
                + read(compact ? "S -144.0" : "SIDE -144.0")
                + std::ceil(unit.getStringWidthFloat("dBFS") + unit.getHeight()*0.5f)
                + 16*scale + 4*gap;
            const float readoutSpare = outer.getWidth() - cells;
            const float rowH = std::ceil(1.2f * nav.getHeight()) + 2;
            const float readH = std::ceil(1.12f * readout.getHeight());
            const float headerH = plot.getY() - outer.getY();
            const float usedH = rowH + 2 + readH + 1;
            const bool ok = spare >= gap && readoutSpare >= 0 && usedH <= headerH + 0.001f;
            ++checked; if (!ok) ++failures;
            minGap = std::min(minGap, spare);
            minReadoutSpare = std::min(minReadoutSpare, readoutSpare);
            minHeight = std::min(minHeight, plot.getHeight());
            if (guide == observatory::GuidePresence::present &&
                (width==300 || width==375 || width==450 || width==600 || width==900))
                std::cout << width << "x" << height << " font=" << nav.getTypefaceName()
                    << " nav=" << nav.getHeight() << " read=" << readout.getHeight()
                    << " plot=" << plot.getWidth() << "x" << plot.getHeight()
                    << " control_spare=" << spare << " readout_spare=" << readoutSpare
                    << " header_used=" << usedH << "/" << headerH << "\n";
            if (!ok && failures < 10)
                std::cout << "FAIL width=" << width << " guide=" << (guide==observatory::GuidePresence::present)
                    << " control=" << spare << " readout=" << readoutSpare
                    << " header=" << usedH << "/" << headerH << "\n";
        }
    }
    std::cout << "checked=" << checked << " failures=" << failures
        << " min_control_spare=" << minGap << " min_readout_spare=" << minReadoutSpare
        << " min_plot_height=" << minHeight << "\n";
    for (const char* family : {"KMR Waldenburg Book", ".SF NS Mono", "Consolas"})
        std::cout << "font_available " << family << "="
            << juce::Font::findAllTypefaceNames().contains(family, true) << "\n";
    return failures != 0;
}
