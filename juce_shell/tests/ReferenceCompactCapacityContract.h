#pragma once

#include "ReferenceComparisonLayoutContract.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaReferenceTonalView.h"

#include <cstdlib>
#include <iostream>
#include <utility>

namespace hypha::tests::reference_compact_capacity
{
inline void expect (bool ok, const juce::String& message)
{
    if (ok) return;
    std::cerr << "Reference compact capacity: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

inline std::shared_ptr<const reference_audition::VisualTimeline> tonalTimeline()
{
    auto data = std::make_shared<reference_audition::VisualTimeline>();
    data->tonalAvailable = true;
    data->tonal.valid_bits = (std::uint64_t (1) << 60) - 1;
    auto reference = std::make_shared<reference_audition::ReferenceTonalCurve>();
    reference->validBits = data->tonal.valid_bits;
    for (int band = 0; band < 60; ++band)
    {
        data->tonal.values_db[band] = -30.0f - float (band) * 0.2f;
        reference->median[size_t (band)] = data->tonal.values_db[band] + 2.0f;
    }
    data->tonalReference = std::move (reference);
    return data;
}

inline std::shared_ptr<reference_audition::ACaptureAccess> capture (int variant)
{
    using namespace reference_audition;
    auto access = std::make_shared<ACaptureAccess>();
    if (variant == 1 || variant == 2)
    {
        expect (access->request (ACaptureAccess::start), "fixture starts a real capture operation");
        const auto operation = access->operationView().id;
        expect (access->advance (operation, variant == 1 ? CaptureOperationPhase::armed
                                                        : CaptureOperationPhase::capturing),
                "fixture publishes a busy operation through its ownership contract");
    }
    else if (variant == 3 || variant == 4)
    {
        auto document = std::make_shared<ACaptureData>();
        document->id = "compact-capacity-kept"; document->rate = 48000;
        document->channels = 2; document->frames = 48000 * 20;
        document->complete = true; document->restored = variant == 4;
        ACaptureState state; state.held = document;
        access->publish (state);
    }
    else if (variant == 5)
    {
        ACaptureState state; state.outcome.kind = CaptureOutcome::restoreFailed;
        state.message = "Saved capture unavailable";
        access->publish (state);
    }
    return access;
}

inline juce::Image render (juce::Component& component)
{
    juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    juce::Graphics g (image); g.fillAll (BG); component.paintEntireComponent (g, true);
    return image;
}

inline juce::Image comparisonGlass (reference_ui::ComparisonView& view)
{
    juce::Image image (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
    juce::Graphics g (image); g.fillAll (BG);
    const key_light::Scope light (view);
    surface_material::paintObservationWell (g, view.getLocalBounds().toFloat(), false);
    reference_ui::window_material::paintInterior (g, view.getLocalBounds().toFloat(), view.getLocalBounds().toFloat());
    return image;
}

inline juce::Image tonalGlass (reference_ui::TonalView& view, bool selected)
{
    juce::Image image (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
    juce::Graphics g (image); g.fillAll (BG);
    const key_light::Scope light (view);
    surface_material::paintPanel (g, view.getLocalBounds().toFloat(), 0.72f);
    reference_ui::window_material::paintInterior (g, view.getLocalBounds().toFloat(), view.getLocalBounds().toFloat());
    if (! selected)
        for (const auto& card : view.visualLayout().cards)
            surface_material::paintPanel (g, card, 0.72f, 3.0f);
    return image;
}

inline bool requireTextInk (const juce::Image& actual, const juce::Image& background,
                           juce::Rectangle<float> bounds, presentation::Context context,
                           typography::TextRole role, const juce::String& text,
                           int justification, juce::Colour ink, bool required = true)
{
    const auto line = std::ceil (typography::resolve (context, role,
        typography::Composition::visualization).lineHeight);
    expect (bounds.getHeight() >= line, "numerical output keeps its actual unchanged text line");
    juce::Image mask (juce::Image::ARGB, actual.getWidth(), actual.getHeight(), true);
    juce::Graphics g (mask); g.setColour (juce::Colours::white);
    const auto shown = text_style::shownText (text);
    const auto contracted = labelFont (context, role, typography::Composition::visualization);
    g.setFont (requiresJapaneseGlyphs (shown) ? nativeTextFontLike (contracted)
        : labelFont (context, role, typography::Composition::visualization));
    g.drawText (shown, bounds.toNearestInt(), justification, false);
    // Native font antialiasing responds to ink colour. A white-alpha mask is only a
    // footprint; compare the actual text to the unchanged product ink on its real glass.
    auto expected = background.createCopy();
    juce::Graphics reference (expected); reference.setColour (ink);
    reference.setFont (requiresJapaneseGlyphs (shown) ? nativeTextFontLike (contracted)
        : labelFont (context, role, typography::Composition::visualization));
    reference.drawText (shown, bounds.toNearestInt(), justification, false);
    int glyphs = 0, visible = 0;
    const auto region = bounds.getSmallestIntegerContainer().getIntersection (actual.getBounds());
    for (int y = region.getY(); y < region.getBottom(); ++y)
        for (int x = region.getX(); x < region.getRight(); ++x)
            if (mask.getPixelAt (x, y).getAlpha() >= 160)
            {
                ++glyphs;
                const auto pixel = actual.getPixelAt (x, y);
                const auto wanted = expected.getPixelAt (x, y);
                const auto glass = background.getPixelAt (x, y);
                const auto contrast = int (wanted.getRed()) + wanted.getGreen() + wanted.getBlue()
                    - glass.getRed() - glass.getGreen() - glass.getBlue();
                visible += contrast > 30 && std::abs (int (pixel.getRed()) - wanted.getRed()) <= 3
                    && std::abs (int (pixel.getGreen()) - wanted.getGreen()) <= 3
                    && std::abs (int (pixel.getBlue()) - wanted.getBlue()) <= 3;
            }
    const bool valid = glyphs > 5 && visible * 5 >= glyphs * 4;
    if (! valid && required)
    {
        std::cerr << "Reference ink diagnostic: language=" << int (i18n::current())
                  << " editor=" << context.logicalWidth << "x" << context.logicalHeight
                  << " role=" << int (role) << " bounds=" << bounds.toString()
                  << " expected=" << text << " shown=" << shown
                  << " glyphs=" << glyphs << " visible=" << visible << '\n';
        const auto path = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_REF_CAPACITY_REVIEW_DIR", {});
        if (path.isNotEmpty())
        {
            const juce::File directory (path);
            for (const auto& item : { std::make_pair ("failed-actual.png", actual),
                                      std::make_pair ("failed-mask.png", mask),
                                      std::make_pair ("failed-reference.png", expected),
                                      std::make_pair ("failed-background.png", background) })
            {
                juce::FileOutputStream stream (directory.getChildFile (item.first));
                if (stream.setPosition (0) && stream.truncate().wasOk())
                    juce::PNGImageFormat().writeImageToStream (item.second, stream);
            }
        }
        expect (false, "expected real numerical glyphs survive the glass, frame and curves");
    }
    return valid;
}

inline void click (reference_ui::TonalView& tonal, juce::Point<float> point)
{
    const auto now = juce::Time::getCurrentTime();
    juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), point, {},
        0, 0, 0, 0, 0, &tonal, &tonal, now, point, now, 1, false);
    tonal.mouseDown (event);
}

inline void writeIfRequested (juce::Component& root, const juce::String& name)
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_REF_CAPACITY_REVIEW_DIR", {});
    if (path.isEmpty()) return;
    const juce::File directory (path);
    expect (directory.createDirectory(), "capacity review directory");
    juce::FileOutputStream stream (directory.getChildFile (name + ".png"));
    expect (stream.setPosition (0) && stream.truncate().wasOk(), "fresh PNG stream");
    expect (juce::PNGImageFormat().writeImageToStream (render (root), stream), "capacity review PNG");
}

inline void requireTonal (reference_ui::TonalView& tonal, presentation::Context context)
{
    const auto& layout = tonal.visualLayout();
    const auto pane = tonal.getLocalBounds().toFloat();
    expect (pane.contains (layout.summary) && ! layout.summary.intersects (layout.graph),
            "tonal cards and curve have separate real bounds");
    if (! layout.twoRows) return; // 300/375 full-selector panes cannot contain two font lines.
    const auto image = render (tonal);
    const auto background = tonalGlass (tonal, false);
    const std::array<juce::String, 4> selectedText {{
        "67 Hz   A -32.0   C -30.0   C-A +2.0 dB",
        "670 Hz   A -36.0   C -34.0   C-A +2.0 dB",
        "4.2 kHz   A -39.2   C -37.2   C-A +2.0 dB",
        "13.4 kHz   A -41.2   C -39.2   C-A +2.0 dB" }};
    for (size_t group = 0; group < layout.cards.size(); ++group)
    {
        expect (layout.cards[group].contains (layout.labels[group])
                    && layout.cards[group].contains (layout.values[group])
                    && ! layout.labels[group].intersects (layout.values[group]),
                "band labels and differences fit their own cards");
        requireTextInk (image, background, layout.values[group], context, typography::TextRole::legend,
                        "C-A +2.0 dB", juce::Justification::centred, COL_NORMAL.withAlpha (0.9f));
        click (tonal, layout.cards[group].getCentre());
        expect (tonal.getDescription().contains ("selected"), "each real card still selects its band");
        requireTextInk (render (tonal), tonalGlass (tonal, true), layout.summary, context,
                        typography::TextRole::readout, selectedText[group], juce::Justification::centred,
                        COL_NORMAL.withAlpha (0.94f));
        expect (layout.summary.getHeight() >= std::ceil (typography::resolve (
            context, typography::TextRole::readout).lineHeight), "selected band keeps a full readout");
        expect (tonal.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)), "selected band returns to overview");
    }
}

inline void verify()
{
    const auto pair = reference_comparison_layout::timeline();
    const auto tone = tonalTimeline();
    int layouts = 0;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        // REF is a POST-only shipping entry; PRE sanitizes this domain to LEVEL.
        for (const auto role : { observatory::Role::post })
            for (const auto& preset : observatory::sizePresets)
                for (int variant = 0; variant < 6; ++variant)
                {
                    const auto context = presentation::forEditor (preset.width, preset.height);
                    juce::Component root; root.getProperties().set (key_light::rootProperty, true);
                    root.setSize (preset.width, preset.height);
                    observatory::View shell (role); root.addAndMakeVisible (shell);
                    shell.setBounds (root.getLocalBounds()); shell.setDomain (observatory::Domain::reference);
                    shell.setExternalAnalysisBodyActive (true);
                    reference_ui::Component panel; root.addAndMakeVisible (panel);
                    panel.setPresentationContext (context); panel.setBounds (shell.analysisBodyBounds());
                    auto state = reference_review::playing (reference_review::library());
                    state.versionId = "v4"; state.versionReady = state.checkReady = true;
                    state.versionStep = state.checkStep = reference_ui::SourceStep::ready;
                    state.status = "READY / A REMAINS LIVE";
                    state.captureAccess = capture (variant);
                    state.title = "Balance"; state.checkLabel = "Balance";
                    state.viewBindings = { "balance" }; state.visualTimeline = tone;
                    panel.setState (state);
                    auto* tonal = dynamic_cast<reference_ui::TonalView*> (panel.findChildWithID ("reference-tonal-view"));
                    expect (tonal && tonal->isVisible(), "shipping library route displays C Balance");
                    requireTonal (*tonal, context);
                    if (preset.width >= 450)
                        expect (tonal->visualLayout().twoRows, "all supported standard/large pages keep four real numerical rows");
                    const auto name = juce::String (language == i18n::Language::english ? "en" : "ja")
                        + (role == observatory::Role::pre ? "-pre-" : "-post-")
                        + juce::String (preset.width) + "-" + juce::String (variant);
                    writeIfRequested (root, name + "-balance");
                    state.comparisonSlot = 1; state.visualTimeline = pair; state.visualPositionSeconds = 8;
                    panel.setState (state);
                    auto* comparison = dynamic_cast<reference_ui::ComparisonView*> (panel.findChildWithID ("reference-comparison-view"));
                    expect (comparison && comparison->isVisible(), "shipping route displays B comparison");
                    const auto& layout = comparison->visualLayout();
                    expect (! layout.readout.intersects (layout.waveform), "compact numerical row is clear of both waveforms");
                    if (preset.width >= 450 && ! layout.detailed())
                    {
                        const auto background = comparisonGlass (*comparison);
                        const auto actual = render (*comparison);
                        requireTextInk (actual, background, layout.readout, context,
                            typography::TextRole::captureMetadata, "B-A +0.5 LU / 3s",
                            juce::Justification::centredLeft, COL_TEXT_SECONDARY);
                        expect (! requireTextInk (background, background, layout.readout, context,
                            typography::TextRole::captureMetadata, "B-A +0.5 LU / 3s",
                            juce::Justification::centredLeft, COL_TEXT_SECONDARY, false),
                            "a glass fill alone cannot satisfy numerical ink coverage");
                        auto obscured = actual.createCopy();
                        juce::Graphics overlay (obscured); overlay.setColour (COL_NORMAL.withAlpha (0.65f));
                        overlay.fillRect (layout.readout);
                        expect (! requireTextInk (obscured, background, layout.readout, context,
                            typography::TextRole::captureMetadata, "B-A +0.5 LU / 3s",
                            juce::Justification::centredLeft, COL_TEXT_SECONDARY, false),
                            "a bright overlay covering the glyphs cannot satisfy ink coverage");
                    }
                    writeIfRequested (root, name + "-version");
                    const auto range = comparison->selectedRange();
                    state.visualTimeline.reset(); panel.setState (state);
                    expect (comparison->isVisible() && comparison->selectedRange() == range,
                            "missing data keeps its view range and empty state without fabricated measurements");
                    if (preset.width >= 450)
                        requireTextInk (render (*comparison), comparisonGlass (*comparison),
                            comparison->getLocalBounds().toFloat().reduced (6, 1), context,
                            typography::TextRole::captureMetadata, "Preparing B overview",
                            juce::Justification::centred, COL_TEXT_SECONDARY);
                    writeIfRequested (root, name + "-missing");
                    state.visualTimeline = pair; panel.setState (state);
                    expect (comparison->selectedRange() == range, "verified observations return to the same view");
                    state.blindPhase = reference_ui::BlindPhase::active; panel.setState (state);
                    expect (! comparison->isVisible() && ! tonal->isVisible()
                                && comparison->getTitle().isEmpty() && tonal->getDescription().isEmpty(),
                            "compact readouts and band identity remain concealed throughout Blind");
                    state.blindPhase = reference_ui::BlindPhase::available; panel.setState (state);
                    expect (comparison->isVisible() && comparison->selectedRange() == range,
                            "return from Blind keeps the selected interval");
                    ++layouts;
                }
    }
    expect (layouts == 60, "POST shipping entry, both languages and all sizes/capture phases were covered");
    std::cout << "Reference compact capacity: " << layouts << " shipping layouts passed\n";
}
}
