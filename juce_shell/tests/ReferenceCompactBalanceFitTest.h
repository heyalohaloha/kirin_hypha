#pragma once
#include "ReferenceOverlayTest.h"
#include "../src/HyphaReferenceBlauertZones.h"
#include "../src/HyphaFooterNoticeFont.h"
#include "../src/HyphaRecordBindingNotice.h"
#include <set>

namespace hypha::tests::reference_overlay
{
// Produce an actually wrapped, local-only notification in either language and at either width.
inline juce::String twoLineNotice (presentation::Context context, int width)
{
    juce::String text = "Capture saved";
    const int baseline = footer_notice_font::wrappedHeight (context, text, width);
    for (int index = 0; index < 20 && footer_notice_font::wrappedHeight (context, text, width) <= baseline * 3 / 2; ++index)
        text += " / Capture saved";
    return text;
}

inline void requireTwoLineNotice (const observatory::View& shell, presentation::Context context)
{
    const auto width = shell.statusStripBounds().getWidth() - 12;
    const auto font = footer_notice_font::fontForText (context, shell.feedback(), width);
    const auto height = footer_notice_font::wrappedHeight (context, shell.feedback(), width);
    require (height > font.getHeight() * 1.5f && height <= font.getHeight() * 2.6f
                 && shell.statusStripBounds().getHeight() >= height,
             "the long notice really reserves exactly two readable lines");
}

inline void verifyWrappedBalanceNotice()
{
    const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_COMPACT_BALANCE_IMAGES", {});
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    for (const int width : { 375, 450 })
    for (const int count : { 0, 1, 16 })
    for (const bool all : { false, true })
    {
        const i18n::ScopedLanguage scoped (language);
        observatory::View shell (observatory::Role::post);
        shell.setSize (width, width * 2 / 3); shell.setDomain (observatory::Domain::reference);
        shell.setExternalAnalysisBodyActive (true);
        const auto context = presentation::forEditor (width, width * 2 / 3);
        shell.setFeedback (twoLineNotice (context, shell.analysisBodyBounds().getWidth() - 12));
        requireTwoLineNotice (shell, context);
        reference_ui::Component panel;
        panel.setPresentationContext (context); shell.addAndMakeVisible (panel);
        auto state = page (1, count); state.overlay.all = all;
        if (count > 0) state.targets.collection.selected = state.targets.collection.items.front().id;
        if (all)
        {
            int colour = 0;
            for (const auto& song : state.songs) state.overlay.lines.push_back ({ song.id, colour++ });
            for (const auto& target : state.targets.collection.items) state.overlay.lines.push_back ({ target.id, colour++ });
        }
        panel.setState (state);
        panel.setStatusInFooter (! shell.statusStripFolded() && ! shell.statusStripBounds().isEmpty());
        panel.setBounds (shell.analysisBodyBounds());
        juce::Image image (juce::Image::ARGB, width, width * 2 / 3, true);
        juce::Graphics g (image); text_style::ShownTextLog log; shell.paintEntireComponent (g, true);
        const auto caption = log.texts().indexOf (text_style::shownText (all ? "BALANCE / ALL" : "BALANCE / ONE"));
        const auto tag = juce::String (width) + "-wrapped-" + juce::String (count) + (all ? "-all" : "-one")
                       + (language == i18n::Language::japanese ? "-ja" : "-en");
        require (caption >= 0, "two-line notice keeps the B chart: " + tag);
        const auto title = log.areas()[static_cast<size_t> (caption)];
        const auto bottom = panel.footerStatusStrip().isVisible() ? panel.footerStatusStrip().getY() : panel.panelArea().getBottom();
        require (bottom - title.getBottom() >= 16.0f, "two-line notice keeps the B plot height: " + tag);
        if (output.isNotEmpty())
        {
            if (shell.statusStripFolded())
            { g.setColour (COL_NORMAL); footer_notice_font::draw (g, context, shell.feedback(), shell.statusStripBounds().reduced (4, 0)); }
            const auto file = juce::File (output).getChildFile ("b-" + tag + ".png"); file.deleteFile();
            auto stream = file.createOutputStream();
            require (stream && juce::PNGImageFormat().writeImageToStream (image, *stream), "write wrapped B image");
        }
    }
}

inline void verifyStandaloneBalanceA()
{
    const auto context = presentation::forEditor (300, 200);
    const juce::Rectangle<float> chart (10.0f, 10.0f, 275.0f, 72.0f);
    auto waiting = page (1, 0);
    waiting.songFacts.front().medianDb.clear(); // the selected song has no measured B yet
    auto a = std::make_shared<ref::KirinSpectrumWindow> (*waiting.aKirin);
    a->centersHz = { 100.0, 1500.0, 10000.0 }; a->medianDb = { -41, -41, -41 };
    a->frames = 30;
    waiting.aKirin = a;
    waiting.chartPointer = chart.getCentre();
    const auto read = [&] (const auto& state)
    {
        juce::Image image (juce::Image::ARGB, 300, 100, true);
        juce::Graphics g (image);
        const text_style::ShownTextLog log;
        reference_ui::paintCompactBalanceChart (g, chart, state, context);
        return log.texts().contains (text_style::shownText ("A -41.0 dB"));
    };
    require (read (waiting), "compact BALANCE reads A on its own bands while B is unmeasured");
    a->frames = 29;
    require (! read (waiting), "compact BALANCE keeps the existing three-second A minimum");
    a->frames = 30;
    waiting.songFacts.front() = page (1, 0).songFacts.front(); // a different grid is now being drawn for B
    require (! read (waiting), "compact BALANCE does not overlay incompatible A onto a measured B grid");

    waiting.songFacts.front().centersHz = a->centersHz;
    waiting.songFacts.front().medianDb = { -50, -50, -50 };
    require (read (waiting), "compact BALANCE accepts a fully compatible measured B grid");
    waiting.songFacts.front().centersHz[1] = 1700.0;
    require (! read (waiting), "compact BALANCE rejects a B whose interior band differs despite matching count and last band");
    waiting.overlay.all = true;
    waiting.overlay.lines.push_back ({ waiting.songId, 0 });
    require (! read (waiting), "ALL lines rejects a displayed B with a mismatched interior band");

    auto targetState = page (0, 1);
    targetState.aKirin = a;
    targetState.chartPointer = chart.getCentre();
    auto& target = targetState.targets.collection.items.front();
    targetState.targets.collection.selected = target.id;
    target.spectrum.centersHz = a->centersHz;
    target.spectrum.medianDb = { -50, -50, -50 };
    require (read (targetState), "compact BALANCE accepts a fully compatible target grid");
    target.spectrum.centersHz[1] = 1700.0;
    require (! read (targetState), "compact BALANCE rejects a target whose interior band differs despite matching count and last band");
}

inline void verifyCompactBalanceRangeReading()
{
    auto context = presentation::forEditor (300, 200);
    context.referenceCompact = true;
    auto state = page (2, 0);
    state.aWindowLoudness = std::numeric_limits<double>::quiet_NaN(); // equal B loudness => zero drawing gain
    auto a = std::make_shared<ref::KirinSpectrumWindow> (*state.aKirin);
    a->frames = 30;
    std::fill (a->medianDb.begin(), a->medianDb.end(), -40.0f);
    state.aKirin = a;
    for (size_t index = 0; index < state.songFacts.size(); ++index)
    {
        auto& fact = state.songFacts[index];
        fact.lufsI = -20.0; fact.loudnessOffsetLu = 0.0;
        std::fill (fact.medianDb.begin(), fact.medianDb.end(), index == 0 ? -50.0f : -30.0f);
    }
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    for (const float height : { 14.0f, 52.0f })
    {
        const i18n::ScopedLanguage scoped (language);
        const juce::Rectangle<float> chart (10.0f, 30.0f, 220.0f, height);
        const auto allowed = chart.withTop (chart.getY() - 14.0f).expanded (0.5f);
        juce::Image image (juce::Image::ARGB, 300, 120, true);
        juce::Graphics g (image);
        text_style::ShownTextLog log;
        state.chartPointer = chart.getCentre();
        reference_ui::paintCompactBalanceChart (g, chart, state, context);

        const int aIndex = log.texts().indexOf (text_style::shownText ("A -40.0 dB"));
        const int bIndex = log.texts().indexOf (text_style::shownText ("B -50.0 dB"));
        int rangeIndex = -1;
        for (int index = 0; index < log.texts().size(); ++index)
            if (log.texts()[index].contains ("-48.0") && log.texts()[index].contains ("-32.0")) rangeIndex = index;
        require (aIndex >= 0 && bIndex >= 0 && rangeIndex >= 0,
                 "100% BALANCE hover reads both compared curves and the true p10/p90 endpoints: " + log.texts().joinIntoString (" | "));
        for (const auto index : { aIndex, bIndex, rangeIndex })
            require (allowed.contains (log.areas()[static_cast<size_t> (index)]), "hover values stay in the compact chart or its own caption");
        require (log.areas()[static_cast<size_t> (rangeIndex)].getY() > log.areas()[static_cast<size_t> (aIndex)].getY(),
                 "range reading wraps to a lower row instead of being dropped");
        log.clear();
        state.chartPointer = { -1.0f, -1.0f };
        reference_ui::paintCompactBalanceChart (g, chart, state, context);
        require (! log.texts().joinIntoString (" | ").contains ("-48.0"), "compact BALANCE range values disappear when pointer leaves");
    }
}

inline void verifyCompactBalanceBlauertReading()
{
    auto context = presentation::forEditor (300, 200);
    context.referenceCompact = true;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    for (const int count : { 1, 2 })
    for (const float height : { 40.0f, 52.0f })
    {
        const i18n::ScopedLanguage scoped (language);
        auto state = page (count, 0);
        state.aWindowLoudness = std::numeric_limits<double>::quiet_NaN();
        auto a = std::make_shared<ref::KirinSpectrumWindow> (*state.aKirin);
        std::fill (a->medianDb.begin(), a->medianDb.end(), -40.0f);
        state.aKirin = a;
        for (int index = 0; index < count; ++index)
        {
            auto& fact = state.songFacts[static_cast<size_t> (index)];
            fact.lufsI = -20.0; fact.loudnessOffsetLu = 0.0;
            std::fill (fact.medianDb.begin(), fact.medianDb.end(), index == 0 ? -50.0f : -30.0f);
        }
        const juce::Rectangle<float> chart (10.0f, 30.0f, 252.0f, height);
        const auto expected = text_style::shownText (reference_ui::blauertReadout ('B', { 0.0, 0.0 }));
        juce::Image image (juce::Image::ARGB, 300, 120, true);
        juce::Graphics g (image);
        text_style::ShownTextLog log;
        reference_ui::paintCompactBalanceChart (g, chart, state, context);
        require (log.texts().contains (expected), "100% BALANCE shows Blauert before hover");
        log.clear();
        state.chartPointer = chart.getCentre();
        reference_ui::paintCompactBalanceChart (g, chart, state, context);
        const auto blauert = log.texts().indexOf (expected);
        const auto aIndex = log.texts().indexOf (text_style::shownText ("A -40.0 dB"));
        const auto bIndex = log.texts().indexOf (text_style::shownText ("B -50.0 dB"));
        int range = -1;
        for (int index = 0; index < log.texts().size(); ++index)
            if (log.texts()[index].contains ("-48.0") && log.texts()[index].contains ("-32.0")) range = index;
        require (blauert >= 0 && aIndex >= 0 && bIndex >= 0 && ((count == 2) == (range >= 0)),
                 "100% hover keeps Blauert and both readings, with a range for two songs only: " + log.texts().joinIntoString (" | "));
        const auto& areas = log.areas();
        for (const auto index : { aIndex, bIndex, range })
            if (index >= 0)
                require (areas[static_cast<size_t> (index)].getY() >= areas[static_cast<size_t> (blauert)].getBottom()
                             && chart.expanded (0.5f).contains (areas[static_cast<size_t> (index)]),
                         "hover readings are visible on their own row below Blauert");
    }
}

inline void verifyPointedCurveMustJoinTheShape()
{
    using Drawing = ref::BalanceOverlay::Drawing;
    auto state = page (2, 0);
    state.aWindowLoudness = std::numeric_limits<double>::quiet_NaN();
    auto a = std::make_shared<ref::KirinSpectrumWindow> (*state.aKirin);
    a->centersHz = { 100.0, 1500.0, 10000.0 }; a->medianDb = { -41, -41, -41 }; a->frames = 30;
    state.aKirin = a;
    state.overlay.all = true;
    for (size_t index = 0; index < state.songFacts.size(); ++index)
    {
        auto& fact = state.songFacts[index];
        fact.centersHz = a->centersHz; fact.medianDb.assign (3, index == 0 ? -45.0f : -35.0f);
        fact.lufsI = -20.0; fact.loudnessOffsetLu = 0.0;
        state.overlay.lines.push_back ({ state.songs[index].id, static_cast<int> (index) });
    }
    const juce::Rectangle<float> chart (10.0f, 10.0f, 275.0f, 72.0f);
    state.chartPointer = chart.getCentre();
    state.overlayPointed = state.songs.back().id;
    const auto draw = [&]
    {
        juce::Image image (juce::Image::ARGB, 300, 100, true);
        juce::Graphics g (image);
        const text_style::ShownTextLog log;
        reference_ui::paintCompactBalanceChart (g, chart, state, presentation::forEditor (300, 200));
        return log.texts();
    };
    for (const auto mode : { Drawing::shape, Drawing::allShape })
    {
        state.overlay.drawing = mode;
        state.songFacts.back().centersHz[1] = 1500.0;
        require (draw().contains (text_style::shownText ("2 -35.0 dB")), "a compatible hovered song joins the shape");
        state.songFacts.back().centersHz[1] = 1700.0;
        const auto mismatched = draw();
        require (! mismatched.contains (text_style::shownText ("2 -35.0 dB"))
                     && mismatched.contains (text_style::shownText ("A -41.0 dB"))
                     && mismatched.contains (text_style::shownText ("MEAN -45.0 dB")),
                 "one different interior band excludes the hovered song but preserves A and the valid mean");
    }
}

inline void verifyThreeBalanceRowsStayInsidePlot()
{
    const i18n::ScopedLanguage scoped (i18n::Language::english);
    auto context = presentation::forEditor (300, 200); context.referenceCompact = true;
    auto state = page (2, 0);
    state.aWindowLoudness = std::numeric_limits<double>::quiet_NaN();
    auto a = std::make_shared<ref::KirinSpectrumWindow> (*state.aKirin);
    std::fill (a->medianDb.begin(), a->medianDb.end(), -40.0f); state.aKirin = a;
    for (size_t index = 0; index < state.songFacts.size(); ++index)
    {
        auto& fact = state.songFacts[index];
        fact.lufsI = -20.0; fact.loudnessOffsetLu = 0.0;
        std::fill (fact.medianDb.begin(), fact.medianDb.end(), index == 0 ? -50.0f : -30.0f);
    }
    const auto blauertWords = text_style::shownText (reference_ui::blauertReadout ('B', { 0.0, 0.0 }));
    bool checked = false;
    for (int width = 160; width <= 225; ++width)
    {
        const juce::Rectangle<float> chart (10.0f, 30.0f, static_cast<float> (width), 40.0f);
        state.chartPointer = { chart.getX() + chart.getWidth() * static_cast<float> (std::log (62.5) / std::log (1000.0)),
                               chart.getCentreY() }; // 1250 Hz, no note added
        juce::Image image (juce::Image::ARGB, 300, 120, true);
        juce::Graphics g (image);
        const text_style::ShownTextLog log;
        reference_ui::paintCompactBalanceChart (g, chart, state, context);
        const auto& area = log.areas();
        std::vector<int> reads;
        for (int index = 0; index < log.texts().size(); ++index)
        {
            const auto& text = log.texts()[index];
            if (text.endsWith (" Hz") || text == text_style::shownText ("A -40.0 dB")
                || text == text_style::shownText ("B -50.0 dB") || (text.contains ("-48.0") && text.contains ("-32.0")))
                reads.push_back (index);
        }
        if (reads.size() != 4) continue;
        std::set<float> rows;
        for (const auto index : reads) rows.insert (area[static_cast<size_t> (index)].getY());
        if (rows.size() != 3) continue;
        const auto blauert = log.texts().indexOf (blauertWords); if (blauert < 0) continue;
        require (area[static_cast<size_t> (blauert)].getBottom() <= chart.getY(),
                 "a three-row pointer reading relocates Blauert to the chart caption");
        for (const auto index : reads)
            require (chart.expanded (0.5f).contains (area[static_cast<size_t> (index)]),
                     "all three readout rows fit, including the final range endpoints");
        checked = true; break;
    }
    require (checked, "a narrow 100% chart actually exercised three rendered readout rows");
}
// Exercise the real shell: the notice changes the space available to Reference.
inline void verifyCompactBalanceFit()
{
    verifyStandaloneBalanceA();
    verifyCompactBalanceRangeReading();
    verifyCompactBalanceBlauertReading();
    verifyPointedCurveMustJoinTheShape();
    verifyThreeBalanceRowsStayInsidePlot();
    juce::StringArray failures;
    const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_COMPACT_BALANCE_IMAGES", {});
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        for (const int width : { 300, 375, 450 })
            for (const bool longNotice : { false, true })
                for (const int count : { 0, 1, 16 })
                    for (const bool all : { false, true })
                    {
                        const i18n::ScopedLanguage scoped (language);
                        observatory::View shell (observatory::Role::post);
                        shell.setSize (width, width * 2 / 3);
                        shell.setDomain (observatory::Domain::reference);
                        shell.setExternalAnalysisBodyActive (true);
                        const juce::String notice = longNotice
                            ? "Measurements saved; waiting for Kirin OS audio (auto link)" : "Capture saved";
                        shell.setCompactReferenceNotices (width == 300);
                        shell.setFeedback (notice);
                        if (width == 300 && longNotice)
                            require (shell.feedback().isEmpty(), "the persistent recording notice is hidden only in compact Reference");
                        reference_ui::Component panel;
                        panel.setPresentationContext (presentation::forEditor (width, width * 2 / 3));
                        shell.addAndMakeVisible (panel);
                        auto state = page (1, count);
                        if (count == 0) { state.referenceStep = reference_ui::SourceStep::playDaw; state.transportPlaying = false; }
                        if (count > 0) state.targets.collection.selected = state.targets.collection.items.front().id;
                        state.overlay.all = all;
                        if (all)
                        {
                            int colour = 0;
                            for (const auto& song : state.songs) state.overlay.lines.push_back ({ song.id, colour++ });
                            for (const auto& target : state.targets.collection.items)
                                state.overlay.lines.push_back ({ target.id, colour++ });
                        }
                        panel.setState (state);
                        panel.setStatusInFooter (! shell.statusStripFolded() && ! shell.statusStripBounds().isEmpty());
                        panel.setBounds (shell.analysisBodyBounds());
                        const auto tag = juce::String (width) + (longNotice ? "-two" : "-one")
                            + "-" + juce::String (count) + (all ? "-all" : "-one-selection")
                            + (language == i18n::Language::japanese ? "-ja" : "-en");
                        for (const bool opened : { false, true })
                        {
                            if (opened)
                            {
                                auto* bar = panel.findChildWithID ("reference-targets");
                                auto* details = bar == nullptr ? nullptr : dynamic_cast<juce::Button*> (bar->findChildWithID ("reference-target-details"));
                                require (details != nullptr, "target details control exists");
                                details->onClick();
                            }
                            text_style::ShownTextLog log;
                            juce::Image image (juce::Image::ARGB, width, width * 2 / 3, true);
                            juce::Graphics graphics (image);
                            shell.paintEntireComponent (graphics, true);
                            // In the editor this strip belongs to the editor, outside the shell child.
                            if (shell.statusStripFolded() && shell.feedback().isNotEmpty())
                            {
                                graphics.setColour (COL_NORMAL);
                                footer_notice_font::draw (graphics, presentation::forEditor (width, width * 2 / 3),
                                                          shell.feedback(), shell.statusStripBounds().reduced (4, 0));
                            }
                            if (output.isNotEmpty() && width == 300)
                            {
                                const auto file = juce::File (output).getChildFile ("b-" + tag + (opened ? "-open.png" : "-closed.png"));
                                file.deleteFile();
                                auto stream = file.createOutputStream();
                                require (stream && juce::PNGImageFormat().writeImageToStream (image, *stream), "write compact B image");
                            }
                            if (width == 300 && ! opened)
                            {
                                auto* connection = panel.findChildWithID ("reference-connection");
                                require (connection && ! connection->isVisible(), "compact B hides the extra connection badge");
                                auto* follow = panel.findChildWithID ("reference-follow-a");
                                auto* loop = panel.findChildWithID ("reference-loop");
                                require (follow && ! follow->isVisible() && loop && ! loop->isVisible(), "compact B keeps playback controls in details");
                                require (! log.texts().joinIntoString ("|").contains ("TARGETS"),
                                         "compact B hides the target hint and count until details");
                                require (panel.footerStatusStrip().isVisible(), "compact B keeps its status");
                                auto* mode = panel.findChildWithID ("reference-overlay");
                                auto* bar = panel.findChildWithID ("reference-targets");
                                auto* details = bar ? bar->findChildWithID ("reference-target-details") : nullptr;
                                require (mode && mode->isVisible() && details && details->isVisible()
                                         && ! mode->getBounds().intersects (panel.getLocalArea (details, details->getLocalBounds())),
                                         "compact B shows ONE / ALL separately from details: " + tag
                                         + " mode " + (mode ? mode->getBounds().toString() : "missing")
                                         + " detail " + (details ? panel.getLocalArea (details, details->getLocalBounds()).toString() : "missing"));
                            }
                            const auto closeText = text_style::shownText (width == 300 ? "CLOSE DETAILS FOR CHART" : "CLOSE DETAILS TO SEE BALANCE");
                            const auto caption = text_style::shownText (all ? "BALANCE / ALL" : "BALANCE / ONE");
                            const int captionIndex = log.texts().indexOf (caption);
                            if (! opened)
                            {
                                if (captionIndex < 0 || log.texts().contains (closeText)) failures.add (tag + " missing closed chart");
                                else
                                {
                                    const auto title = log.areas()[static_cast<size_t> (captionIndex)];
                                    const auto status = panel.footerStatusStrip().getBounds();
                                    const auto bottom = panel.footerStatusStrip().isVisible() ? status.getY() : panel.panelArea().getBottom();
                                    if (title.isEmpty() || bottom - title.getBottom() < 16.0f)
                                        failures.add (tag + " chart plot below minimum");
                                }
                            }
                            else if (log.texts().contains (closeText) && captionIndex >= 0)
                                failures.add (tag + " misleading close message with chart");
                            if (width == 300 && opened)
                            {
                                const auto hint = text_style::shownText ("DROP AUDIO / TARGETS " + juce::String (count) + "/16");
                                require (log.texts().contains (hint), "compact details shows the drop hint and target count: " + tag + " / " + log.texts().joinIntoString (" | "));
                                auto* loop = panel.findChildWithID ("reference-loop");
                                auto* follow = panel.findChildWithID ("reference-follow-a");
                                auto* bar = panel.findChildWithID ("reference-targets");
                                auto* details = bar->findChildWithID ("reference-target-details");
                                require (loop && follow && loop->isVisible() && follow->isVisible()
                                    && ! loop->getBounds().intersects (follow->getBounds())
                                    && ! follow->getBounds().intersects (panel.getLocalArea (details, details->getLocalBounds()))
                                    && ! follow->getBounds().intersects (panel.footerStatusStrip().getBounds())
                                    && ! loop->getBounds().intersects (panel.footerStatusStrip().getBounds()),
                                    "compact B details keep playback and target controls separate: " + tag
                                    + " loop " + (loop ? loop->getBounds().toString() : "missing")
                                    + " follow " + (follow ? follow->getBounds().toString() : "missing")
                                    + " status " + panel.footerStatusStrip().getBounds().toString());
                            }
                        }
                    }
    {
        observatory::View shell (observatory::Role::post);
        shell.setSize (300, 200); shell.setDomain (observatory::Domain::reference);
        shell.setCompactReferenceNotices (true);
        shell.setFeedback ("Measurements saved; waiting for Kirin OS audio (auto link)");
        reference_ui::Component panel;
        panel.setPresentationContext (presentation::forEditor (300, 200));
        shell.addAndMakeVisible (panel);
        auto state = page (1, 1); state.targets.issue = "LOW DISK SPACE / REFERENCE AVAILABLE";
        panel.setState (state); panel.setBounds (shell.analysisBodyBounds());
        require (panel.footerStatusStrip().isVisible(), "a target failure stays visible when space is tight");
        const auto drawn = a_direction::drawnTexts (panel);
        require (! drawn.contains (text_style::shownText ("CLOSE DETAILS TO SEE BALANCE")),
                 "a closed target panel never asks to close details");
    }
    {
        observatory::View shell (observatory::Role::post);
        shell.setSize (300, 200); shell.setDomain (observatory::Domain::reference);
        shell.setCompactReferenceNotices (true);
        reference_ui::Component panel;
        panel.setVisible (true); panel.setPresentationContext (presentation::forEditor (300, 200));
        panel.setBounds (shell.analysisBodyBounds()); panel.setState (page (1, 1));
        int saved = 0;
        panel.onOverlayChange = [&saved] (const auto&) { ++saved; };
        auto* mode = panel.findChildWithID ("reference-overlay");
        auto* all = dynamic_cast<juce::Button*> (mode->findChildWithID ("reference-overlay-all"));
        require (all && all->isVisible(), "compact ALL is reachable");
        all->onClick();
        require (panel.state().overlay.all && saved == 1, "compact ALL changes and saves the actual selection");
        auto playing = page (1, 1);
        playing.bSelected = playing.audibleHeard = true; playing.audibleComparisonSlot = 3;
        playing.followA = true; playing.tracking = reference_audition::TrackingState::fixed;
        panel.setState (playing);
        require (panel.statusTextLayout (panel.footerStatusStrip().getLocalBounds()).line.text == "B PLAYING",
                 "ordinary playback has a short compact status");
        auto* bar = panel.findChildWithID ("reference-targets");
        auto* details = dynamic_cast<juce::Button*> (bar->findChildWithID ("reference-target-details"));
        require (details != nullptr, "compact playback details exists"); details->onClick();
        require (panel.statusTextLayout (panel.footerStatusStrip().getLocalBounds()).line.text
                     == reference_ui::referenceStatusLine (playing, false).text,
                 "details restores the full playback description");
        editor_reachability::checkAtDisplayScales (panel, [] (float scale, const juce::String& problem)
        { require (false, "compact B details is reachable at " + juce::String (scale) + ": " + problem); });
        details->onClick();
        shell.setFeedback (record_ui::wavWaiting);
        require (shell.feedback().isEmpty(), "compact Reference hides the continuing recording notice");
        shell.setCompactReferenceNotices (false);
        require (shell.feedback() == record_ui::wavWaiting, "leaving compact Reference restores the recording notice");
        shell.setCompactReferenceNotices (true);
        shell.setFeedback ("A long direct action notice should remain visible on one line when space is tight");
        require (shell.feedback().isNotEmpty() && shell.statusStripBounds().getHeight() < 25,
                 "direct action feedback remains visible in one line");
        shell.setFeedback ({});
        require (shell.feedback().isEmpty(), "compact Reference keeps routine states such as WAITING quiet");
        shell.setMeasurementFormatHeld (true); shell.setFeedback ({});
        require (shell.feedback() == "FORMAT HELD / STOP KEEP", "a state the user must act on stays visible at 100%");
        shell.setMeasurementFormatHeld (false); shell.setFeedback ({});
        require (shell.feedback().isEmpty(), "the held state leaves with its cause");
    }
    require (failures.isEmpty(), "compact Balance fits: " + failures.joinIntoString ("; "));
    verifyWrappedBalanceNotice();
}
}
