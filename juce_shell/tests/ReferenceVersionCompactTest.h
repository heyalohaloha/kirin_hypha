#pragma once

#include "ReferenceGuideContractTest.h"
#include "ReferenceControlLookup.h"
#include "ReferenceComparisonLayoutContract.h"
#include "../src/HyphaReferenceRangeStrips.h"
#include <map>
#include <vector>

namespace hypha::tests
{
inline void verifyRangeLabelSpacing()
{
    using reference_guide_contract::require;
    struct Range { double low, high; };
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    for (const int width : { 600, 900 })
    for (const int paneWidth : { 200, 300, 450 })
    for (const bool checkView : { false, true })
    for (const auto limits : { Range { -90.0, -6.0 }, Range { -90.0, 6.0 },
                               Range { -18.0, -12.0 }, Range { -12.0, -11.0 } })
    {
        const i18n::ScopedLanguage scoped (language);
        auto context = presentation::forEditor (width, width * 2 / 3);
        context.referenceCheck = checkView;
        auto timeline = std::make_shared<reference_audition::VisualTimeline>();
        auto ticks = std::make_shared<std::vector<KirinReferenceVisualBin>>();
        for (int index = 0; index < 100; ++index)
        {
            KirinReferenceVisualBin bin {};
            bin.frames = 4800; bin.rms[0] = bin.rms[1] = 0.1;
            bin.peak[0] = bin.peak[1] = 0.2;
            bin.momentary_lufs = index < 25 ? limits.low : index < 75 ? (limits.low + limits.high) / 2 : limits.high;
            bin.short_lufs = -15.0;
            ticks->push_back (bin);
        }
        timeline->aPairTicks = ticks; timeline->vPairTicks = ticks; timeline->pairTickChannels = 2;
        juce::Image image (juce::Image::ARGB, paneWidth, 300, true);
        juce::Graphics graphics (image); text_style::ShownTextLog log;
        require (reference_ui::paintVersionRangeStrips (graphics, image.getBounds().toFloat(), timeline.get(),
                                                        "loudness", 0.0, context), "range strips render");
        std::map<int, std::vector<juce::Rectangle<float>>> rows;
        for (int index = 0; index < log.texts().size(); ++index)
        {
            const auto number = log.texts()[index].trimCharactersAtStart (juce::String::fromUTF8 (u8"−+-"));
            if (number.isNotEmpty() && number.containsOnly ("0123456789"))
            {
                const auto box = log.areas()[static_cast<size_t> (index)];
                rows[juce::roundToInt (box.getY())].push_back (box);
            }
        }
        require (! rows.empty(), "the strips draw numeric axis labels");
        for (auto& [y, boxes] : rows)
        {
            juce::ignoreUnused (y);
            std::sort (boxes.begin(), boxes.end(), [] (const auto& a, const auto& b) { return a.getX() < b.getX(); });
            for (size_t index = 1; index < boxes.size(); ++index)
                require (boxes[index - 1].getRight() <= boxes[index].getX(), "axis label boxes do not overlap");
        }
        const auto minimum = static_cast<int> (std::floor ((limits.low - 2.0) / 6.0) * 6.0);
        const auto maximum = std::max (minimum + 12, static_cast<int> (std::ceil ((limits.high + 2.0) / 6.0) * 6.0));
        const auto numberText = [] (int value)
        { return (value < 0 ? juce::String::fromUTF8 (u8"−") : juce::String()) + juce::String (std::abs (value)); };
        const int first = log.texts().indexOf (numberText (minimum));
        require (first >= 0, "the lower endpoint remains labelled");
        const auto axisY = log.areas()[static_cast<size_t> (first)].getY();
        const auto onAxis = [&] (const juce::String& text)
        {
            for (int index = 0; index < log.texts().size(); ++index)
                if (log.texts()[index] == text && log.areas()[static_cast<size_t> (index)].getY() == axisY) return true;
            return false;
        };
        require (onAxis (numberText (maximum)), "the upper endpoint remains labelled");
        if (minimum <= 0 && maximum >= 0) require (onAxis ("0"), "zero remains labelled");
    }
}

inline void verifyVersionCompact()
{
    using reference_guide_contract::require;
    verifyRangeLabelSpacing();
    require (! reference_ui::rangeStripBinding ("version_movement"),
             "V-only detail choices do not extend Kirin OS Check bindings");
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        std::vector<observatory::SizePreset> sizes (observatory::sizePresets.begin(), observatory::sizePresets.end());
        for (const int width : { 336, 339, 411, 414, 522, 525, 528, 747, 750, 753 })
            sizes.push_back ({ width, width * 2 / 3, observatory::densityForWidth (width), "" });
        for (const auto size : sizes)
        {
            observatory::View shell (observatory::Role::post);
            shell.setSize (size.width, size.height);
            shell.setDomain (observatory::Domain::reference);
            reference_ui::Component panel;
            panel.setPresentationContext (presentation::forEditor (size.width, size.height));
            panel.setSize (shell.analysisBodyBounds().getWidth(), shell.analysisBodyBounds().getHeight());
            auto state = reference_guide_contract::named ("ready");
            state.comparisonSlot = 1;
            state.title = "Unrelated C Song";
            state.blindLargeScreen = size.width >= 900;
            panel.setState (state);
            auto* version = dynamic_cast<juce::Button*> (panel.findChildWithID ("reference-b"));
            auto* chooser = panel.findChildWithID ("reference-version");
            auto* details = dynamic_cast<juce::Button*> (panel.findChildWithID ("reference-check-details"));
            auto* comparison = panel.findChildWithID ("reference-comparison-view");
            require (version && chooser && chooser->isVisible() && details && details->isVisible()
                         && ! details->getToggleState() && comparison && comparison->isVisible(),
                     "V exposes its Version, main comparison and closed details at every size");
            require (panel.getLocalBounds().contains (chooser->getBounds())
                         && panel.getLocalBounds().contains (details->getBounds())
                         && ! chooser->getBounds().intersects (details->getBounds()), "V controls fit without overlap");
            int played = 0, expanded = 0;
            panel.onSelectB = [&] { ++played; };
            panel.onOpenLarge = [&] (int) { ++expanded; };
            const auto checkText = [&]
            {
                const auto font = labelFont (panel.state().separateComparisons
                    ? presentation::forEditor (size.width, size.height) : presentation::defaultContext(),
                    typography::TextRole::action, typography::Composition::information);
                for (const auto* id : { "reference-check-details", "reference-blind" })
                    if (auto* button = dynamic_cast<juce::Button*> (findReferenceControl (panel, id)); button && button->isVisible())
                        require (text_style::shownWidth (font, button->getButtonText()) <= float (button->getWidth()),
                                 juce::String (id) + " text fits at " + juce::String (size.width));
                juce::Image image (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
                juce::Graphics graphics (image);
                text_style::ShownTextLog log;
                panel.paintEntireComponent (graphics, true);
                require (! log.texts().contains ("Unrelated C Song"), "V never shows another role's source title");
                for (const auto& text : log.texts())
                    require (! text.containsChar (0x2026) && ! text.endsWith ("..."),
                             "V text is not ellipsized at " + juce::String (size.width) + ": " + text);
                const auto status = panel.statusTextLayout (panel.footerStatusStrip().getLocalBounds());
                require (! status.cut, "V status is whole at " + juce::String (size.width) + ": " + status.line.text);
            };
            checkText();
            version->onClick();
            require (played == 1 && expanded == 0, "V plays directly at the chosen size");
            details->onClick();
            checkText();
            require (details->getToggleState() && played == 1 && expanded == 0,
                     "details opens without playing or resizing");
            auto* viewChoice = dynamic_cast<juce::ComboBox*> (panel.findChildWithID ("reference-check-choice"));
            require (viewChoice && viewChoice->isVisible() && viewChoice->getNumItems() == 9
                         && panel.getLocalBounds().contains (viewChoice->getBounds()), "all V views are reachable in details");
            for (int choice = 1; choice <= viewChoice->getNumItems(); ++choice)
            {
                viewChoice->setSelectedId (choice, juce::sendNotificationSync);
                const auto choiceFont = viewChoice->getLookAndFeel().getComboBoxFont (*viewChoice);
                require (text_style::shownWidth (choiceFont, viewChoice->getText()) <= float (viewChoice->getWidth() - 24),
                         "the selected V view name fits beside its arrow at " + juce::String (size.width));
                checkText();
                require (played == 1 && expanded == 0, "changing a V view does not change audition or size");
            }
            details->onClick();
            require (! details->getToggleState() && played == 1, "details closes without changing audition");
            require (panel.stepTab (-1).isEmpty() && details->getToggleState() && viewChoice->isVisible(),
                     "choosing a V view by keyboard opens details without changing the sound");
            require (played == 1 && expanded == 0, "keyboard view selection preserves audition and size");
        }
    }
    // Compact detail views report paired facts in both languages instead of an empty pane.
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        auto data = std::make_shared<reference_audition::VisualTimeline> (*reference_comparison_layout::timeline());
        auto spectrum = std::make_shared<reference_audition::KirinSpectrumWindow>();
        spectrum->frames = 200; spectrum->centersHz = { 100.0f };
        spectrum->medianDb = spectrum->p10Db = spectrum->p90Db = { -20.0f };
        data->aPairKirin = spectrum; data->vPairKirin = spectrum;
        auto a = std::make_shared<std::vector<KirinReferenceVisualBin>>();
        auto v = std::make_shared<std::vector<KirinReferenceVisualBin>>();
        for (const auto& bin : data->bins) { a->push_back (bin.a); v->push_back (bin.b); }
        data->aPairTicks = a; data->vPairTicks = v; data->pairTickChannels = 2;
        auto state = reference_guide_contract::named ("ready");
        state.comparisonSlot = 1; state.blindLargeScreen = false; state.visualTimeline = data;
        reference_ui::Component panel;
        panel.setPresentationContext (presentation::forEditor (300, 200));
        panel.setSize (288, 136); panel.setState (state);
        auto* details = dynamic_cast<juce::Button*> (panel.findChildWithID ("reference-check-details"));
        auto* choice = dynamic_cast<juce::ComboBox*> (panel.findChildWithID ("reference-check-choice"));
        require (details && choice, "compact V details controls exist");
        details->onClick();
        struct Detail { int index; const char* english; const char* japanese; };
        for (const auto& [index, english, japanese] : { Detail { 3, "CREST", "CREST" }, { 4, "WIDTH", "WIDTH" },
             { 6, "MOVEMENT", u8"音量の動き" }, { 7, "ATTACK", u8"アタック" },
             { 8, "ONSET", u8"立ち上がり" }, { 9, "CORRELATION", u8"相関" } })
        {
            const auto label = juce::String::fromUTF8 (language == i18n::Language::japanese ? japanese : english);
            choice->setSelectedId (index, juce::sendNotificationSync);
            juce::Image image (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
            juce::Graphics graphics (image); text_style::ShownTextLog log;
            panel.paintEntireComponent (graphics, true);
            require (log.texts().contains (label), juce::String ("compact V shows its paired ") + label + " comparison");
            require (log.texts().joinIntoString ("|").contains ("A ") && log.texts().joinIntoString ("|").contains ("V "),
                     "compact comparison identifies both observed sides");
        }
        for (const auto size : observatory::sizePresets)
        {
            panel.setPresentationContext (presentation::forEditor (size.width, size.height));
            panel.setSize (size.width - 12, size.width == 900 ? 470 : size.height - 64);
            state.blindLargeScreen = size.width >= 900;
            panel.setState (state);
            for (int selected = 1; selected <= choice->getNumItems(); ++selected)
            {
                choice->setSelectedId (selected, juce::sendNotificationSync);
                juce::Image output (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
                juce::Graphics graphics (output); text_style::ShownTextLog text;
                panel.paintEntireComponent (graphics, true);
                for (const auto& line : text.texts())
                    require (! line.containsChar (0x2026) && ! line.endsWith ("..."),
                             "paired V text fits at " + juce::String (size.width) + ": " + line);
            }
        }

    }

}
}
