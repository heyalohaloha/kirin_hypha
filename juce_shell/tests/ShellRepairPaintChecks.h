#pragma once
#include "../src/HyphaAttackComponent.h"
#include "../src/HyphaAnalysisUiText.h"
#include "../src/HyphaSpectrumTerrain.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaReferenceStatusModel.h"

namespace hypha::tests::shell_repairs
{
struct RailTag {};
juce::Rectangle<int>& rail (observatory::View&, RailTag);
template <auto Member> struct RailAccess
{ friend juce::Rectangle<int>& rail (observatory::View& v, RailTag) { return v.*Member; } };
template struct RailAccess<&observatory::View::sessionArea>;

inline void savePreview (const juce::Image& image, const juce::String& stem)
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_SHELL_PREVIEW_DIR", {});
    if (path.isEmpty()) return;
    const juce::File directory (path);
    require (directory.createDirectory(), "create requested preview directory");
    auto output = directory.getChildFile (stem + ".png").createOutputStream();
    require (output && output->setPosition (0) && output->truncate().wasOk()
        && juce::PNGImageFormat().writeImageToStream (image, *output), "replace disposable UI preview");
}
inline void verifyDrumPaint (const char*);

inline void verifyReferenceStatusPaint (const char* lang)
{
    for (const int width : { 300, 375, 450, 900 })
    {
        reference_ui::Component panel;
        const auto context = presentation::forEditor (width, width * 2 / 3);
        panel.setPresentationContext (context);
        panel.setSize (width, width * 2 / 3);
        reference_ui::State state;
        state.separateComparisons = state.bSelected = state.transportPlaying = true;
        state.osAccess = os_access::State::ready;
        state.readiness = reference_ui::Readiness::ready;
        state.libraryReceived = state.auditionBuffered = true;
        state.referenceStep = reference_ui::SourceStep::ready;
        state.blindLargeScreen = width >= 900;
        state.comparisonSlot = state.audibleComparisonSlot = 3;
        state.tracking = reference_audition::TrackingState::stoppedCeiling;
        state.peakShortfallDb = 0.4;
        state.heldAttenuationDb = -0.8;
        panel.setStatusInFooter (true);
        panel.setState (state);
        auto& strip = panel.footerStatusStrip();
        strip.setBounds (0, 0, width, 30); strip.resized();
        const auto layout = panel.statusTextLayout (strip.getLocalBounds());
        const auto font = labelFont (context, typography::TextRole::readout, typography::Composition::information);
        require (text_style::shownWidth (font, layout.text) <= layout.textArea.getWidth(),
                 "localized Reference status keeps complete segments within its actual row");
        juce::Image image (juce::Image::ARGB, width, 30, true);
        { juce::Graphics g (image); strip.paintEntireComponent (g, true); }
        savePreview (image, juce::String (lang) + "-reference-status-" + juce::String (width));
    }
}

inline void verifyPaint()
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        const auto lang = language == i18n::Language::english ? "en" : "ja";
        for (const int width : { 300, 375, 450 })
        {
            live_compare::LiveBlindStatus unavailable;
            unavailable.stage = live_compare::BlindStage::preparing; unavailable.timingUnavailable = true;
            live_blind_ui::Component blind;
            blind.setSize (width, width * 2 / 3); blind.setState (unavailable, true, 0.5f);
            juce::Image image (juce::Image::ARGB, width, width * 2 / 3, true);
            { juce::Graphics g (image); blind.paintEntireComponent (g, true); }
            savePreview (image, juce::String (lang) + "-unavailable-" + juce::String (width));

            AttackComponent attack;
            attack.setPresentationContext (presentation::forEditor (width, width * 2 / 3));
            attack.setSize (width, width * 2 / 3);
            attack.setAnalysisUnavailable (analysis_ui::slotsInUse ("Fixture one, Fixture two"));
            text_style::ShownTextLog log;
            image = juce::Image (juce::Image::ARGB, width, width * 2 / 3, true);
            { juce::Graphics g (image); attack.paintEntireComponent (g, true); }
            require (! log.texts().isEmpty() && log.texts().joinIntoString (" ").contains ("Fixture"),
                     "a third DRUM view paints occupied owners before its first accepted snapshot");
            savePreview (image, juce::String (lang) + "-drum-in-use-" + juce::String (width));

            observatory::View footer (observatory::Role::post);
            footer.setSize (width, width * 2 / 3);
            KirinRecordDisplay record {}; record.phase = KIRIN_RECORD_DISPLAY_UNAVAILABLE;
            footer.setRecordDisplay (record, true);
            // Force a genuinely narrow rail, without truncating the status before the real painter.
            const bool railShown = ! rail (footer, {}).isEmpty();
            if (railShown) rail (footer, {}).setWidth (36);
            log.clear();
            image = juce::Image (juce::Image::ARGB, width, width * 2 / 3, true);
            { juce::Graphics g (image); footer.paintEntireComponent (g, true); }
            if (railShown)
                require (log.texts().joinIntoString (" ").contains (juce::String::fromUTF8 (u8"…")),
                         "a visible narrow footer paints an ellipsis instead of hiding its status");
            savePreview (image, juce::String (lang) + "-footer-" + juce::String (width));
        }
        for (const int width : { 600, 750, 900 })
        {
            juce::Image image (juce::Image::ARGB, width, width * 2 / 3, true);
            KirinSpectrumView spectrum {};
            text_style::ShownTextLog log;
            { juce::Graphics g (image); spectrum_terrain::paintInstrumentNotes (g, image.getBounds().toFloat(),
                spectrum, true, presentation::forEditor (width, width * 2 / 3), true); }
            const auto shape = log.texts().joinIntoString (" ");
            require (shape.contains ("SHAPE") && ! shape.startsWith (hypha::delta()), "SHAPE annotation describes the level offset");
            savePreview (image, juce::String (lang) + "-shape-notes-" + juce::String (width));
            log.clear();
            { juce::Graphics g (image); spectrum_terrain::paintInstrumentNotes (g, image.getBounds().toFloat(),
                spectrum, true, presentation::forEditor (width, width * 2 / 3), false); }
            require (! log.texts().joinIntoString (" ").contains ("SHAPE"), "RAW keeps its original delta formula");
        }
        verifyDrumPaint (lang);
        verifyReferenceStatusPaint (lang);
    }
}
}
#include "ShellRepairDrumPaint.h"
