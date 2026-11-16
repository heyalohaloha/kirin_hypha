#pragma once
#include "ReferenceAWindowTest.h"
#include "ReferenceOverlayTest.h"
#include "ReferenceComparisonLayoutContract.h"
#include "../src/HyphaFooterNoticeFont.h"
#include "../src/HyphaRecordBindingNotice.h"

namespace hypha::tests::reference_compact_check
{
using reference_guide_contract::require;

inline void verifyVersion()
{
    const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_COMPACT_CHECK_IMAGES", {});
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    for (const bool longNotice : { false, true })
    {
        const i18n::ScopedLanguage scoped (language);
        observatory::View shell (observatory::Role::post);
        shell.setSize (300, 200); shell.setDomain (observatory::Domain::reference);
        shell.setExternalAnalysisBodyActive (true); shell.setCompactReferenceNotices (true);
        shell.setFeedback (longNotice ? record_ui::wavWaiting : "Capture saved");
        reference_ui::Component panel;
        panel.setPresentationContext (presentation::forEditor (300, 200));
        shell.addAndMakeVisible (panel);
        auto state = reference_guide_contract::named ("ready");
        state.comparisonSlot = 1;
        auto data = std::make_shared<reference_audition::VisualTimeline> (*reference_comparison_layout::timeline());
        auto spectrum = kirinWindow (0.0f, { -20.0, -14.0, -18.0, -30.0 }, 200);
        data->aPairKirin = spectrum; data->vPairKirin = spectrum;
        data->pairTickChannels = 2;
        state.visualTimeline = data;
        panel.setState (state); panel.setBounds (shell.analysisBodyBounds());
        const auto tag = juce::String (longNotice ? "two" : "one")
                       + (language == i18n::Language::japanese ? "-ja" : "-en");
        juce::Image image (juce::Image::ARGB, 300, 200, true);
        juce::Graphics g (image); text_style::ShownTextLog log; shell.paintEntireComponent (g, true);
        if (shell.statusStripFolded() && shell.feedback().isNotEmpty())
        {
            g.setColour (COL_NORMAL);
            footer_notice_font::draw (g, presentation::forEditor (300, 200), shell.feedback(),
                                      shell.statusStripBounds().reduced (4, 0));
        }
        if (output.isNotEmpty())
        {
            const auto file = juce::File (output).getChildFile ("v-300-" + tag + ".png");
            file.deleteFile(); auto stream = file.createOutputStream();
            require (stream && juce::PNGImageFormat().writeImageToStream (image, *stream), "write compact V image");
        }
        auto* version = panel.findChildWithID ("reference-version");
        auto* details = panel.findChildWithID ("reference-check-details");
        auto* chart = panel.findChildWithID ("reference-comparison-view");
        require (version && version->isVisible() && version->getHeight() >= 12
                 && details && details->isVisible() && details->getHeight() >= 12
                 && chart && chart->isVisible() && chart->getHeight() >= 12
                 && panel.footerStatusStrip().isVisible(), "compact V keeps Version, TONE, DETAILS and status");
        require (! version->getBounds().intersects (details->getBounds())
                 && ! chart->getBounds().intersects (panel.footerStatusStrip().getBounds())
                 && ! chart->getBounds().intersects (version->getBounds()), "compact V controls do not overlap");
        for (const auto* id : { "reference-check-choice", "reference-preset", "reference-cue", "reference-loop", "reference-follow-a", "reference-blind", "reference-connection", "reference-selection-value-0", "reference-selection-value-4" })
            if (const auto* extra = panel.findChildWithID (id)) require (! extra->isVisible(), juce::String ("compact V hides ") + id);
        auto* button = dynamic_cast<juce::Button*> (details);
        require (button != nullptr, "V details can be opened");
        int played = 0;
        panel.onSelectB = [&played] { ++played; };
        button->onClick();
        const auto* choice = panel.findChildWithID ("reference-check-choice");
        const auto* blind = panel.findChildWithID ("reference-blind");
        require (choice && choice->isVisible() && blind && blind->isVisible() && played == 0,
                 "V details exposes its other comparisons and VERSION BLIND without playing");
        button->onClick();
    }
}

inline void verifyWrappedCheckNotice()
{
    const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_COMPACT_CHECK_IMAGES", {});
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    for (const int width : { 375, 450 })
    {
        const i18n::ScopedLanguage scoped (language);
        observatory::View shell (observatory::Role::post);
        shell.setSize (width, width * 2 / 3); shell.setDomain (observatory::Domain::reference);
        shell.setExternalAnalysisBodyActive (true);
        const auto context = presentation::forEditor (width, width * 2 / 3);
        shell.setFeedback (reference_overlay::twoLineNotice (context, shell.analysisBodyBounds().getWidth() - 12));
        reference_overlay::requireTwoLineNotice (shell, context);
        reference_ui::Component panel;
        panel.setPresentationContext (context); shell.addAndMakeVisible (panel);
        auto state = a_direction::checkState(); state.viewBindings.clear();
        panel.setState (state); panel.setBounds (shell.analysisBodyBounds());
        juce::Image image (juce::Image::ARGB, width, width * 2 / 3, true);
        juce::Graphics g (image); text_style::ShownTextLog log; shell.paintEntireComponent (g, true);
        const auto tag = juce::String (width) + "-wrapped" + (language == i18n::Language::japanese ? "-ja" : "-en");
        for (const auto* label : { "LUFS-I", "MAX TP" })
        {
            int index = -1;
            for (int item = 0; item < log.texts().size(); ++item) if (log.texts()[item].contains (label)) index = item;
            require (index >= 0 && log.areas()[static_cast<size_t> (index)].getHeight() >= 12.0f,
                     "two-line notice keeps the C values tall enough: " + tag + " " + label);
            require (! log.areas()[static_cast<size_t> (index)].intersects (panel.footerStatusStrip().getBounds().toFloat()),
                     "two-line notice keeps C values clear of its status: " + tag);
        }
        if (output.isNotEmpty())
        {
            if (shell.statusStripFolded())
            { g.setColour (COL_NORMAL); footer_notice_font::draw (g, context, shell.feedback(), shell.statusStripBounds().reduced (4, 0)); }
            const auto file = juce::File (output).getChildFile ("c-" + tag + ".png"); file.deleteFile();
            auto stream = file.createOutputStream();
            require (stream && juce::PNGImageFormat().writeImageToStream (image, *stream), "write wrapped C image");
        }
    }
}

inline void verify()
{
    juce::StringArray failures;
    for (const bool checkPage : { false, true })
    {
        auto context = presentation::forEditor (300, 200);
        context.referenceCompact = true; context.referenceCheck = checkPage;
        for (const auto role : { typography::TextRole::action, typography::TextRole::unit,
                                 typography::TextRole::selector, typography::TextRole::readout,
                                 typography::TextRole::metricLabel, typography::TextRole::status })
            require (typography::resolve (context, role, typography::Composition::information).fontHeight >= 12.0f,
                     "compact Reference text is at least 12 px");
    }
    const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_COMPACT_CHECK_IMAGES", {});
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    for (const int width : { 300, 375, 450 })
    for (const bool longNotice : { false, true })
    {
        const i18n::ScopedLanguage scoped (language);
        observatory::View shell (observatory::Role::post);
        shell.setSize (width, width * 2 / 3);
        shell.setDomain (observatory::Domain::reference);
        shell.setExternalAnalysisBodyActive (true);
        shell.setCompactReferenceNotices (width == 300);
        shell.setFeedback (longNotice ? record_ui::wavWaiting : "Capture saved");
        reference_ui::Component panel;
        panel.setPresentationContext (presentation::forEditor (width, width * 2 / 3));
        shell.addAndMakeVisible (panel);
        auto state = a_direction::checkState();
        state.presets = { { "set-a", "Mastering" }, { "set-b", "Other" } };
        state.presetId = "set-a";
        state.checks = { { "tone/song", "Tone / Song" }, { "dynamics/song", "Dynamics / Song" } };
        state.checkId = "tone/song";
        state.viewBindings.clear();
        panel.setState (state);
        panel.setBounds (shell.analysisBodyBounds());
        const auto tag = juce::String (width) + (longNotice ? "-two" : "-one")
                       + (language == i18n::Language::japanese ? "-ja" : "-en");
        text_style::ShownTextLog log;
        juce::Image image (juce::Image::ARGB, width, width * 2 / 3, true);
        juce::Graphics graphics (image);
        shell.paintEntireComponent (graphics, true);
        if (shell.statusStripFolded() && shell.feedback().isNotEmpty())
        {
            graphics.setColour (COL_NORMAL);
            footer_notice_font::draw (graphics, presentation::forEditor (width, width * 2 / 3),
                                      shell.feedback(), shell.statusStripBounds().reduced (4, 0));
        }
        if (output.isNotEmpty())
        {
            const auto file = juce::File (output).getChildFile ("c-" + tag + ".png");
            file.deleteFile();
            auto stream = file.createOutputStream();
            require (stream && juce::PNGImageFormat().writeImageToStream (image, *stream), "write compact C image");
        }
        int loud = -1, peak = -1;
        for (int index = 0; index < log.texts().size(); ++index)
        {
            if (log.texts()[index].contains ("LUFS-I")) loud = index;
            if (log.texts()[index].contains ("MAX TP")) peak = index;
        }
        if (loud < 0 || peak < 0) failures.add (tag + " missing metric labels: " + log.texts().joinIntoString (" | "));
        else
        {
            const auto a = log.areas()[static_cast<size_t> (loud)];
            const auto b = log.areas()[static_cast<size_t> (peak)];
            const auto status = panel.footerStatusStrip().getBounds().toFloat();
            if (a.getHeight() < 12.0f || b.getHeight() < 12.0f || a.intersects (b)
                || a.intersects (status) || b.intersects (status)
                || ! panel.panelArea().toFloat().contains (a) || ! panel.panelArea().toFloat().contains (b))
                failures.add (tag + " metric height/overlap: " + a.toString() + " " + b.toString());
            if (width == 300)
                for (const auto* id : { "reference-preset", "reference-check", "reference-match", "reference-check-details",
                                        "reference-a", "reference-ref", "reference-c", "reference-b" })
                    if (const auto* c = panel.findChildWithID (id); c && c->isVisible()
                        && (a.intersects (c->getBounds().toFloat()) || b.intersects (c->getBounds().toFloat())))
                        failures.add (tag + " metric overlaps " + id);
        }
        if (width == 300)
        {
            for (const auto* id : { "reference-preset", "reference-check", "reference-match", "reference-check-details" })
                if (const auto* c = panel.findChildWithID (id); c == nullptr || ! c->isVisible() || c->getHeight() < 12)
                    failures.add (tag + " missing control " + id);
            for (const auto* id : { "reference-loop", "reference-follow-a", "reference-compare", "reference-cue",
                                    "reference-selection-value-4", "reference-connection" })
                if (const auto* c = panel.findChildWithID (id); c != nullptr && c->isVisible())
                    failures.add (tag + " extra control " + id);
            std::vector<juce::Rectangle<int>> primary;
            for (const auto* id : { "reference-preset", "reference-check", "reference-match", "reference-check-details",
                                    "reference-a", "reference-ref", "reference-c", "reference-b" })
                if (const auto* c = panel.findChildWithID (id); c && c->isVisible()) primary.push_back (c->getBounds());
            for (size_t i = 0; i < primary.size(); ++i)
                for (size_t j = i + 1; j < primary.size(); ++j)
                    if (primary[i].intersects (primary[j])) failures.add (tag + " primary controls overlap");
            if (log.texts().contains (text_style::shownText ("C: A vs Kirin OS song and cue")))
                failures.add (tag + " extra purpose line");
            if (! panel.footerStatusStrip().isVisible() || panel.footerStatusStrip().getHeight() < 12)
                failures.add (tag + " status missing");
            auto* details = dynamic_cast<juce::Button*> (panel.findChildWithID ("reference-check-details"));
            if (details && details->isVisible())
            {
                int played = 0, enlarged = 0;
                panel.onSelectC = [&played] { ++played; };
                panel.onOpenLarge = [&enlarged] (int) { ++enlarged; };
                details->onClick();
                if (played != 0 || enlarged != 0) failures.add (tag + " details changed audition or size");
                std::vector<juce::Rectangle<int>> controls;
                for (const auto* id : { "reference-loop", "reference-follow-a", "reference-compare", "reference-cue" })
                {
                    auto* control = panel.findChildWithID (id);
                    if (! control || ! control->isVisible() || control->getHeight() < 12)
                        failures.add (tag + " details missing " + id);
                    else controls.push_back (control->getBounds());
                }
                for (size_t i = 0; i < controls.size(); ++i)
                {
                    if (! panel.panelArea().contains (controls[i]) || controls[i].intersects (panel.footerStatusStrip().getBounds()))
                        failures.add (tag + " details overlaps status or frame");
                    for (size_t j = i + 1; j < controls.size(); ++j)
                        if (controls[i].intersects (controls[j])) failures.add (tag + " details controls overlap");
                }
                if (language == i18n::Language::english && ! longNotice)
                    editor_reachability::checkAtDisplayScales (panel, [] (float scale, const juce::String& problem)
                    { require (false, "compact C details is reachable at " + juce::String (scale) + ": " + problem); });
                juce::Image opened (juce::Image::ARGB, width, width * 2 / 3, true);
                juce::Graphics g (opened); text_style::ShownTextLog openedLog;
                shell.paintEntireComponent (g, true);
                for (const auto* name : { "LUFS-I", "MAX TP" })
                {
                    int found = -1;
                    for (int index = 0; index < openedLog.texts().size(); ++index)
                        if (openedLog.texts()[index].contains (name)) found = index;
                    if (found < 0 || openedLog.areas()[static_cast<size_t> (found)].getHeight() < 12.0f)
                        failures.add (tag + " details hides or compresses " + name);
                    else
                    {
                        const auto metric = openedLog.areas()[static_cast<size_t> (found)].getSmallestIntegerContainer();
                        for (const auto& control : controls)
                            if (metric.intersects (control)) failures.add (tag + " details overlaps " + name);
                    }
                }
                if (shell.statusStripFolded() && shell.feedback().isNotEmpty())
                {
                    g.setColour (COL_NORMAL);
                    footer_notice_font::draw (g, presentation::forEditor (width, width * 2 / 3),
                                              shell.feedback(), shell.statusStripBounds().reduced (4, 0));
                }
                if (output.isNotEmpty())
                {
                    const auto file = juce::File (output).getChildFile ("c-" + tag + "-open.png");
                    file.deleteFile(); auto stream = file.createOutputStream();
                    require (stream && juce::PNGImageFormat().writeImageToStream (opened, *stream), "write C details image");
                }
                details->onClick();
                panel.requestRole (2);
                if (played != 1 || enlarged != 0) failures.add (tag + " C press should play without resizing");
                auto approval = state;
                approval.bSelected = approval.audibleHeard = true; approval.audibleComparisonSlot = 2;
                approval.tracking = reference_audition::TrackingState::fixed;
                approval.appliedGainDb = 3.3; approval.peakShortfallDb = 5.8;
                approval.action = { reference_ui::ActionKind::lowerAAndPlay, { 2, -9.1, "fixture", 1, 1 } };
                approval.actionText = "LOWER A 9.1 DB";
                panel.setState (approval);
                auto* action = dynamic_cast<juce::Button*> (panel.footerStatusStrip().findChildWithID ("reference-action"));
                if (! action || ! action->isVisible() || panel.footerStatusStrip().getHeight() != 36)
                    failures.add (tag + " approval lacks its one status line and button");
                else
                {
                    auto context = presentation::forEditor (300, 200); context.referenceCheck = true; context.referenceCompact = true;
                    const auto font = labelFont (context, typography::TextRole::action, typography::Composition::information);
                    if (text_style::shownWidth (font, action->getButtonText()) > action->getWidth())
                        failures.add (tag + " approval button text is cut");
                    const auto statusLayout = panel.statusTextLayout (panel.footerStatusStrip().getLocalBounds());
                    if (statusLayout.line.text != "C LIMIT +3.3 DB / 5.8 DB BELOW A" || panel.statusLineWhole().isEmpty())
                        failures.add (tag + " approval status or full explanation is missing");
                    juce::Image approvalImage (juce::Image::ARGB, width, width * 2 / 3, true);
                    juce::Graphics g (approvalImage); text_style::ShownTextLog approvalLog;
                    shell.paintEntireComponent (g, true);
                    for (const auto* name : { "LUFS-I", "MAX TP" })
                    {
                        int found = -1;
                        for (int index = 0; index < approvalLog.texts().size(); ++index)
                            if (approvalLog.texts()[index].contains (name)) found = index;
                        if (found < 0 || approvalLog.areas()[static_cast<size_t> (found)].getHeight() < 12.0f
                            || approvalLog.areas()[static_cast<size_t> (found)].getSmallestIntegerContainer()
                                   .intersects (panel.getLocalArea (action, action->getLocalBounds())))
                            failures.add (tag + " approval compresses or overlaps " + name);
                    }
                    if (shell.statusStripFolded() && shell.feedback().isNotEmpty())
                    {
                        g.setColour (COL_NORMAL);
                        footer_notice_font::draw (g, context, shell.feedback(), shell.statusStripBounds().reduced (4, 0));
                    }
                    if (output.isNotEmpty())
                    {
                        const auto file = juce::File (output).getChildFile ("c-" + tag + "-approval.png");
                        file.deleteFile(); auto stream = file.createOutputStream();
                        require (stream && juce::PNGImageFormat().writeImageToStream (approvalImage, *stream), "write approval image");
                    }
                }
            }
        }
    }
    require (failures.isEmpty(), "compact C: " + failures.joinIntoString ("; "));
    verifyVersion();
    verifyWrappedCheckNotice();
}
}
