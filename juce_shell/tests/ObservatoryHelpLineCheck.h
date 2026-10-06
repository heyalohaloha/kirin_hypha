#pragma once

// 2026-10-04：300% 以上ではどの画面も同じ説明の行を使い、項目が何で、どう使うかまで言う。
// 300% 以上の LEVEL・TIME・FREQ・SPACE で項目を指すと、その説明を足元に一行で
// 出す（PluginEditorHelpLine.cpp・HyphaHelpLineBar.h）。図・値・タブの説明は足元の段の全幅に、足元のボタンの説明は左の
// 状態の所に、両言語で省略せずに収まる。200% 以下は吹き出しのまま、REF の B・C・V も同じ行（REF の説明の文の試験は
// ReferenceHoverHelpTest.h）。
#include "EditorProductChecks.h"
#include "ReferenceStatusPlacementCheck.h"
#include "../src/HyphaAnalysisUiText.h"
#include "../src/HyphaAttackBandPainter.h"
#include "../src/HyphaAttackBandSummaryPainter.h"
#include "../src/HyphaHelpLineText.h"
#include "../src/HyphaLevelMetricContract.h"

#include <set>

namespace hypha::tests::editor_product
{
template <class Tag, typename Tag::Type Member> struct HelpLineAccess
{
    friend typename Tag::Type helpLineMember (Tag) { return Member; }
};

struct HelpLinePage
{
    using Type = void (KirinHyphaEditor::*) (analysis_navigation::Page);
    friend Type helpLineMember (HelpLinePage);
};
template struct HelpLineAccess<HelpLinePage, &KirinHyphaEditor::setAnalysisPage>;

// The helps the line can show, each with whether it has the whole footer row or only the status.
struct HelpLineTexts
{
    std::set<juce::String> row, footer;
    void add (const juce::String& help, bool wholeRow)
    {
        if (help.isNotEmpty()) (wholeRow ? row : footer).insert (help_line::forLine (help, wholeRow));
    }
};

// Points at every 6 px of the editor the way a pointer does (the component under it sees the move
// first, so the help that follows the pointer is current) and gathers what the line would show.
// Returns how many points had a help.
inline int addPointedHelps (KirinHyphaEditor& editor, HelpLineTexts& texts)
{
    editor.createComponentSnapshot (editor.getLocalBounds()); // the value tiles note their help as they paint
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    int pointed = 0;
    for (int y = 0; y < editor.getHeight(); y += 6)
        for (int x = 0; x < editor.getWidth(); x += 6)
        {
            const juce::Point<int> point { x, y };
            if (auto* under = editor.getComponentAt (point); under != nullptr && under != &editor)
            {
                const auto local = under->getLocalPoint (&editor, point).toFloat();
                under->mouseMove (juce::MouseEvent (source, local, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, under, under,
                                                    now, local, now, 0, false));
            }
            const auto line = editor.helpLineAt (point);
            if (line.text.isEmpty()) continue;
            (line.wholeRow ? texts.row : texts.footer).insert (line.text);
            ++pointed;
        }
    return pointed;
}

// The footer controls that appear only with a paired PRE (PRE / POST Listen, Blind and the live
// comparison's own buttons): hidden in this test, they show the same help when they appear.
inline void addPairedFooterHelps (observatory::View& view, HelpLineTexts& texts)
{
    for (auto* child : view.getChildren())
        if (child->getComponentID().startsWith ("observatory-live") || child->getComponentID() == "observatory-local-blind")
            if (auto* client = dynamic_cast<juce::TooltipClient*> (child)) texts.add (client->getTooltip(), false);
}

// Help that pointing cannot reach in this test: it follows a paired PRE, a measured hit, a live
// comparison or a held value. Written out here so the line is checked for them all the same.
inline void addHelpsBeyondPointing (HelpLineTexts& texts)
{
    using namespace analysis_ui;
    for (std::uint8_t mode = 0; mode < 4; ++mode) texts.add (channelModeTooltip (mode), true);
    for (const auto absolute : { false, true })
        for (const auto stereo : { false, true }) texts.add (midSideModeTooltip (absolute, stereo), true);
    for (const auto& text : { midSideSpectrumPlotTooltip(), spectrumPlotTooltip(), absoluteSpectrumPlotTooltip(),
                              approximateFrequencyTooltip(), deltaLegendTooltip(), preLegendTooltip(), postLegendTooltip(),
                              markTooltip (true), markTooltip (false), focusTrailTooltip (true), focusTrailTooltip (false),
                              sharpnessDeltaTooltip(), liveOverviewTooltip(), liveMetricTooltip (0), liveMetricTooltip (1),
                              liveMetricTooltip (2) })
        texts.add (text, true);
    for (const auto* text : { "RAW: exact POST - PRE spectral difference",
                              "SHAPE: spectral difference after same-window energy normalization",
                              "Return to Spectrum", "Show perceptual spectral balance",
                              "PSB: perceptual share by Bark band", "Spectrum: frequency level and difference" })
        texts.add (text, true);
    for (std::uint8_t band = 0; band <= attack_band::bandCount; ++band)
        texts.add (attack_band_painter::chipTooltip (band), true);
    texts.add (attack_band_painter::predatesTooltip(), true);
    texts.add (attack_band_painter::paneTooltip (true), true);
    texts.add (attack_band_painter::paneTooltip (false), true);
    for (const auto lane : attack_lanes::bandLanes) texts.add (attack_band_painter::laneTooltip (lane), true);
    for (std::size_t lane = 0; lane < attack_ui::laneCount; ++lane)
        for (const auto delta : { false, true }) texts.add (attack_band_summary_painter::laneTooltip (lane, delta), true);
    texts.add (attack_band_summary_painter::cardTooltip(), true);
    using level_metrics::Metric;
    for (const auto metric : { Metric::momentary, Metric::shortTerm, Metric::integrated, Metric::maximumTruePeak,
                               Metric::loudnessRange, Metric::plr, Metric::truePeak, Metric::crest, Metric::psr })
        for (const auto* side : { "POST. ", "PRE. ", "POST minus PRE. " })
            texts.add (juce::String (side) + level_metrics::scopeHelp (metric), true);
    for (const auto* text : { "No active signal.", "Measurement is unavailable.", "Keep is waiting for its pair.",
                              "Measurement is active.", "Keep is recording.", "A kept result is available." })
        texts.add (text, true);
    for (const auto* text : {
             "PRE waits while delay compensation is off in Pro Tools. Turn it on to hear PRE",
             "PRE is held because the latency changed. Stop and restart playback",
             "PRE is selected. POST plays until PRE is confirmed at this position",
             "Listen to PRE, the input of this chain, at the level MATCH set",
             "Listen to PRE, the input of this chain. MATCH levels it to POST",
             "Listen to POST, lowered by the attenuation you approved", "Listen to POST, the output of this chain",
             "MATCH held; rematch to confirm levels",
             "AUTO: PRE follows POST loudness within 0.5 dB, up to 6 dB from your MATCH. Press to MATCH again or stop AUTO",
             "MATCH stopped at the true-peak ceiling: PRE is still quieter than POST. Press to measure again",
             "Press to MATCH again or to let PRE follow POST (AUTO)",
             "Match PRE to POST loudness over the latest four seconds", "NOTE requires Kirin OS",
             "Finish Keep / Record before PRE / POST Blind", "Match levels and compare while the song plays" })
        texts.add (text, false);
}

// The width the line has for its text: the whole footer row, and the status up to the first footer control.
inline std::pair<int, int> helpLineWidths (observatory::View& view)
{
    const auto footer = view.footerBounds();
    int status = footer.getRight();
    for (auto* child : view.getChildren())
        if (child->isVisible() && child != &view.feedbackDetailsAnchor() && dynamic_cast<juce::Button*> (child) != nullptr
            && footer.contains (child->getBounds().getCentre()))
            status = juce::jmin (status, child->getX() - 4);
    return { footer.getWidth() - 16, status - footer.getX() - 16 };
}

inline void verifyObservatoryHelpLine (const juce::File& previews)
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (Processor::Role::Post);
    processor.prepareToPlay (48'000, 960);
    std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditorIfNeeded());
    auto* editor = dynamic_cast<KirinHyphaEditor*> (base.get());
    auto* view = editor != nullptr ? component<observatory::View> (*editor) : nullptr;
    require (editor != nullptr && view != nullptr, "the shipping editor opens for the help line");
    editor->setVisible (true);
    editor->setSize (900, 600);

    using observatory::Domain;
    using Page = analysis_navigation::Page;
    struct Shown { Domain domain; Page page; };
    const Shown pages[] { { Domain::level, Page::meters }, { Domain::time, Page::meters }, { Domain::time, Page::run },
                          { Domain::time, Page::attack }, { Domain::time, Page::perceptual }, { Domain::time, Page::absolute },
                          { Domain::frequency, Page::spectrum }, { Domain::space, Page::meters } };
    HelpLineTexts texts;
    int rowWidth = 900, statusWidth = 900;
    for (const auto& shown : pages)
    {
        view->onDomainChange (shown.domain);
        if (shown.page != Page::meters)
            (editor->*helpLineMember (HelpLinePage {})) (shown.page);
        const auto [row, status] = helpLineWidths (*view);
        rowWidth = juce::jmin (rowWidth, row);
        statusWidth = juce::jmin (statusWidth, status);
        require (addPointedHelps (*editor, texts) > 0, "every page has help in the line at 300%");
        addPairedFooterHelps (*view, texts);
    }
    addHelpsBeyondPointing (texts);
    const auto context = presentation::forEditor (900, 600);
    juce::String tooWide;
    for (const auto wholeRow : { true, false })
        for (const auto& text : wholeRow ? texts.row : texts.footer)
        {
            const auto width = wholeRow ? rowWidth : statusWidth;
            juce::String widths;
            for (const auto language : { i18n::Language::english, i18n::Language::japanese })
            {
                const i18n::ScopedLanguage inLanguage (language);
                const auto font = labelFont (context, typography::TextRole::unit, typography::Composition::information);
                if (const auto shown = text_style::shownWidth (font, text); shown > (float) width)
                    widths << (language == i18n::Language::japanese ? " JA " : " EN ") << juce::String (shown, 0)
                           << (language == i18n::Language::japanese ? " " + text_style::shownText (text) : juce::String());
            }
            if (widths.isNotEmpty())
                tooWide << "\n  " << (wholeRow ? "ROW " : "FOOTER ") << width << " [" << text << "]" << widths;
        }
    std::cout << "Help line: " << texts.row.size() << " helps in the footer row (" << rowWidth << " px), "
              << texts.footer.size() << " in the status (" << statusWidth << " px) at 300%" << std::endl;
    if (tooWide.isNotEmpty()) std::cerr << "Help line too wide:" << tooWide << std::endl;
    require (tooWide.isEmpty(), "every help fits its place in the footer at 300% in English and Japanese");

    // The line replaces the bubble only where it is shown: 300% and above, not on REF's A or below 300%.
    // A pointer move over the editor marks it so; HoverHelpTooltipWindow then shows no bubble inside.
    const auto lineReplacesBubble = [&]
    {
        const auto now = juce::Time::getCurrentTime();
        editor->mouseMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 1.0f, 0.0f,
                                             0.0f, 0.0f, 0.0f, editor, editor, now, {}, now, 0, false));
        return reference_ui::help::shownInLine (*view);
    };
    view->onDomainChange (Domain::level);
    require (lineReplacesBubble(), "at 300% the line replaces the bubble");
    editor->createComponentSnapshot (editor->getLocalBounds());
    const auto plr = editor->helpLineAt ({ 532, 225 });
    require (plr.wholeRow && plr.text.contains ("Average dynamics of the song (PLR)") && plr.text.contains ("Compare PRE and POST"),
             "pointing at PLR says what it is and how it is used, across the footer row");
    const auto size = editor->helpLineAt (editor->getLocalArea (&view->sizeMenuAnchor(),
                                                                view->sizeMenuAnchor().getLocalBounds()).getCentre());
    require (! size.wholeRow && size.text == "Choose an exact editor size", "a footer control speaks in the status only");
    if (previews != juce::File())
        for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        {
            const i18n::ScopedLanguage inLanguage (language);
            auto* bar = dynamic_cast<HelpLineBar*> (find (*editor, "help-line"));
            require (bar != nullptr, "the help line bar exists");
            bar->show (plr.text, view->footerBounds(), view->footerBounds(), presentation::forEditor (900, 600));
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
            auto output = previews.getChildFile (juce::String ("help-line-")
                + (language == i18n::Language::japanese ? "ja" : "en") + "-900.png").createOutputStream();
            require (output != nullptr && output->setPosition (0) && output->truncate().wasOk()
                         && juce::PNGImageFormat().writeImageToStream (image, *output), "help line preview PNG");
            bar->show ({}, {}, {}, presentation::forEditor (900, 600));
        }
    editor->setSize (600, 400);
    require (editor->helpLineAt ({ 100, 150 }).text.isEmpty() && ! lineReplacesBubble(), "200% keeps the bubble, not the line");
    editor->setSize (1350, 900);
    require (editor->helpLineAt ({ 205, 225 }).text.contains ("Momentary loudness") && lineReplacesBubble(),
             "a magnified 300% shows the line too");
    editor->setSize (900, 600);
    view->onDomainChange (Domain::reference);
    require (editor->helpLineAt ({ 450, 300 }).text.isEmpty() && ! lineReplacesBubble(),
             "REF's A page and its access panel keep their bubbles");
    if (auto* panel = component<reference_ui::Component> (*editor))
        withReferenceShown (*editor, *panel, [&] { verifyReferenceHelpLine (*editor, *panel); });
    processor.editorBeingDeleted (editor);
    base.reset();
    processor.releaseResources();
}
}
