#pragma once
#include "../src/HyphaCaptureHistoryPainter.h"
#include "../src/HyphaCaptureHistoryGeometry.h"
#include "../src/HyphaCaptureHistoryTruePeak.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaSpectrumTerrain.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSurfaceMaterial.h"
#include "../src/HyphaTextStyle.h"
#include "LraStateBoundsContract.h"
#include "SpectrumPerformanceFixture.h"
#include <stdexcept>

namespace hypha::tests::visual_readability
{
inline void require (bool pass, const char* message)
{
    if (! pass) throw std::runtime_error (message);
}
inline void save (const juce::Image& image, const juce::String& name)
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_VISUAL_READABILITY_DIR", {});
    if (path.isEmpty()) return;
    const juce::File directory (path);
    require (directory.createDirectory().wasOk(), "create visual evidence directory");
    auto output = directory.getChildFile (name + ".png").createOutputStream();
    require (output && output->setPosition (0) && output->truncate().wasOk()
             && juce::PNGImageFormat().writeImageToStream (image, *output), "write visual evidence");
}
inline void history (int width, const juce::String& suffix)
{
    const auto context = presentation::forEditor (width, width * 2 / 3);
    for (const auto role : { typography::TextRole::historyAxis, typography::TextRole::historyLegend })
        require (typography::resolve (context, role, typography::Composition::visualization).fontHeight >= 12,
                 "history labels never below twelve pixels");
    // Exercise the painter even where the compact product intentionally omits history.
    for (const int height : { 92, 130, 180 })
    {
        juce::Image image (juce::Image::ARGB, width, height, true);
        juce::Graphics g (image);
        std::vector<KirinMeterHistoryEntry> entries (60);
        for (size_t i = 0; i < entries.size(); ++i)
        {
            auto& entry = entries[i];
            entry.generation = entry.run_id = 1;
            entry.last_observed_frames = (i + 1) * 48'000;
            entry.lufs_m = { -21.5, -21.5, -21.5 };
            entry.true_peak = { -10, -10, -10 };
        }
        text_style::ShownTextLog log;
        capture_history::paint (g, image.getBounds(), entries, false, 48'000, context,
                                 std::nullopt, "MAX M -21.5 LUFS");
        const auto layout = capture_history::layoutFor (image.getBounds());
        require (layout.sharedPlot.getHeight() > 0, "history preserves a positive data field");
        const auto& texts = log.texts();
        const auto& boxes = log.areas();
        bool foundHeader = false;
        for (int i = 0; i < texts.size(); ++i)
        {
            const bool header = texts[i].startsWith ("MAX M ");
            foundHeader |= header;
            if (header)
                require (boxes[(size_t) i].getBottom() + 7 <= layout.sharedPlot.getY(),
                         "header clears the top tick's text box");
            // The numeric endpoint boxes must clear every header and time label.
            if (texts[i] != "0" && texts[i] != "-36" && texts[i] != "+3" && texts[i] != "-1") continue;
            for (int j = 0; j < texts.size(); ++j)
                if (texts[j].startsWith ("MAX M ") || texts[j] == "-60" || texts[j] == "-30" || texts[j] == "NOW")
                    require (! boxes[(size_t) i].intersects (boxes[(size_t) j]),
                             "history endpoints clear header and horizontal axis");
        }
        require (foundHeader, "history detail header was drawn");
        save (image, "history-" + suffix + "-" + juce::String (height));
    }
}
inline void frequency (int width, const juce::String& suffix)
{
    const auto context = presentation::forEditor (width, width * 2 / 3);
    SpectrumComponent product;
    product.setPresentationContext (context); product.setAbsoluteObservation (true); product.setSignalActive (true);
    const auto bounds = ui_contract::spectrumPlotBounds (width, width * 2 / 3);
    product.setSize (bounds.width, bounds.height);
    auto signal = spectrumPerformanceFixture();
    for (auto& band : signal.post_dbfs) band = -96;
    signal.post_dbfs[150] = -24; signal.post_dbfs[151] = -24;
    product.setSnapshot (signal); product.presentationTick();
    require (product.displayedPostForTest (150) > -30, "product FREQ fixture accepted a real peak");
    save (product.createComponentSnapshot (product.getLocalBounds()), "frequency-product-" + suffix);
    const juce::Rectangle<float> plot (12, 12, float (width - 24), 180);
    KirinSpectrumView view {};
    view.sample_rate = 48'000; view.aperture_samples = 4'096; view.fft_size = 8'192;
    const auto render = [&] (bool crossing, bool notes, juce::Rectangle<int>& label) {
        juce::Image image (juce::Image::ARGB, width, 210, true);
        juce::Graphics g (image);
        surface_material::paintObservationWell (g, plot);
        const auto backing = notes ? spectrum_terrain::paintInstrumentNotes (
            g, plot, view, false, context, false, true) : juce::Rectangle<int>{};
        if (crossing)
        {
            juce::Graphics::ScopedSaveState clip (g);
            if (! backing.isEmpty()) g.excludeClipRegion (backing);
            // Deterministic high-contrast ridge crosses the entire bottom information row.
            g.setColour (COL_SPECTRUM_POST);
            g.drawLine (plot.getX(), plot.getBottom() - 15, plot.getRight(), plot.getBottom() - 15, 4);
        }
        text_style::ShownTextLog log;
        if (notes) spectrum_terrain::paintInstrumentNotes (g, plot, view, false, context);
        for (int i = 0; i < log.texts().size(); ++i)
            if (log.texts()[i].startsWith ("APERTURE ")) label = log.areas()[(size_t)i].toNearestInt();
        return image;
    };
    juce::Rectangle<int> label;
    const auto quiet = render (false, true, label);
    const auto crossing = render (true, true, label);
    if (plot.getWidth() < spectrum_terrain::minimumPlotWidth)
    { require (label.isEmpty(), "narrow plot omits instrument notes"); return; }
    require (! label.isEmpty(), "wide plot must draw instrument notes");
    juce::Rectangle<int> unused;
    const auto uncovered = render (true, false, unused);
    const auto bareQuiet = render (false, false, unused);
    int changedWithoutBacking = 0;
    for (int y = label.getY(); y < label.getBottom(); ++y)
        for (int x = label.getX(); x < label.getRight(); ++x)
        {
            require (quiet.getPixelAt (x,y) == crossing.getPixelAt (x,y),
                     "spectrum ridge cannot alter any information-line pixel");
            changedWithoutBacking += bareQuiet.getPixelAt (x,y) != uncovered.getPixelAt (x,y);
        }
    require (changedWithoutBacking > 20, "crossing fixture actually touches information line");
    save (crossing, "frequency-" + suffix);
}
inline KirinObservatoryFrame warmingFrame()
{
    KirinObservatoryFrame frame {};
    frame.version = KIRIN_OBSERVATORY_FRAME_VERSION; frame.signal_state = KIRIN_SIGNAL_STATE_ACTIVE;
    frame.lra_state = KIRIN_LRA_WARMING;
    frame.meter.generation = 7; frame.meter.sample_rate = 48'000;
    frame.meter.active_frames = frame.meter.observed_frames = 48'000 * 4;
    frame.meter.state = KIRIN_METER_SESSION_ACTIVE; frame.meter.channels = 2;
    frame.meter.lufs_m = frame.meter.lufs_s = frame.meter.lufs_i = -21.5;
    frame.meter.true_peak = frame.meter.max_true_peak = -20;
    frame.meter.max_lufs_m = -21.5;
    return frame;
}
inline void verify()
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const int width : { 300, 450, 600, 900 })
        {
            const auto suffix = juce::String (language == i18n::Language::english ? "en-" : "ja-")
                              + juce::String (width);
            history (width, suffix); frequency (width, suffix);
            observatory::View view (observatory::Role::post);
            view.setSize (width, width * 2 / 3);
            auto frame = warmingFrame(); frame.lra_elapsed_seconds = 4;
            view.setObservatoryFrame (frame, true);
            std::vector<KirinMeterHistoryEntry> measured (40);
            for (size_t i = 0; i < measured.size(); ++i)
            {
                auto& entry = measured[i]; entry.generation = 7; entry.run_id = 1;
                entry.last_observed_frames = (i + 1) * 4'800;
                entry.lufs_m = { -21.5, -21.5, -21.5 };
                const double peak = i == 38 ? 0.3 : -20;
                entry.true_peak = { peak, peak, peak };
            }
            view.setHistory (measured);
            save (view.createComponentSnapshot (view.getLocalBounds()), "level-" + suffix);
        }
    }
    lra_state_bounds::verify (warmingFrame());
    std::cout << "Visual readability: history geometry, spectrum backing, localized LRA PASS\n";
}
}
