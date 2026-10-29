#pragma once
#include "EditorProductChecks.h"
#include "EditorReachabilityCheck.h"

#include <array>
#include <functional>
#include <utility>

// 2026-10-18: "every control is reachable" is read with INV-S38 kept. 300x200 (100%) stays a view
// to read; controls that take room are chosen from 125% up and are not added there. What a size shows
// lies inside the editor, a press at its centre reaches it and Tab reaches it. The display scale here
// is the OS / host scale (Windows DPI, a Mac's scaled display: the host window holds the editor scaled),
// a separate axis from the editor's size presets. Cells: size x display scale x language x PRE/POST x
// page. The same cells, every MENU level and the information menu offer no PIN 4 S (1.1.52), also after
// a language switch and after an older saved state is read.
namespace hypha::tests::editor_product
{
struct OperationsMenuOf
{
    using Type = juce::PopupMenu (KirinHyphaEditor::*)();
    friend Type privateMember (OperationsMenuOf);
};
template struct PrivateAccess<OperationsMenuOf, &KirinHyphaEditor::operationsMenu>;
struct EntryLicense
{
    using Type = std::atomic<int> Processor::*;
    friend Type privateMember (EntryLicense);
};
template struct PrivateAccess<EntryLicense, &Processor::cachedLicenseCode>;
struct InformationMenuOf
{
    using Type = juce::PopupMenu (KirinHyphaEditor::*)();
    friend Type privateMember (InformationMenuOf);
};
template struct PrivateAccess<InformationMenuOf, &KirinHyphaEditor::informationMenu>;

// The five presets, the sizes just inside the limits and 750 x 500 between the last two presets.
inline constexpr std::array<std::pair<int, int>, 8> entrySizes {{
    { 300, 200 }, { 303, 202 }, { 375, 250 }, { 450, 300 }, { 600, 400 }, { 750, 500 }, { 897, 598 }, { 900, 600 } }};
inline constexpr std::array<float, 4> displayScales { 1.0f, 1.25f, 1.5f, 2.0f };
inline constexpr std::array<observatory::Domain, 5> entryPages { observatory::Domain::level, observatory::Domain::time,
    observatory::Domain::frequency, observatory::Domain::space, observatory::Domain::reference };

inline bool offersPin (const juce::String& text) { return text.contains ("PIN 4") || text.startsWith ("PIN"); }

inline void requireNoPinEntry (KirinHyphaEditor& editor, const char* where)
{
    std::function<bool (juce::Component&)> shown = [&] (juce::Component& component)
    {
        if (component.getComponentID() == "observatory-live-pin" || offersPin (component.getTitle()))
            return true;
        if (auto* client = dynamic_cast<juce::TooltipClient*> (&component); client && offersPin (client->getTooltip()))
            return true;
        if (auto* button = dynamic_cast<juce::Button*> (&component); button && offersPin (button->getButtonText()))
            return true;
        for (auto* child : component.getChildren())
            if (shown (*child)) return true;
        return false;
    };
    require (! shown (editor), where);
    for (const auto& menu : { (editor.*privateMember (OperationsMenuOf {}))(), (editor.*privateMember (InformationMenuOf {}))() })
        for (juce::PopupMenu::MenuItemIterator item (menu, true); item.next();)
            require (! offersPin (item.getItem().text), where);
}

// Every shown control at every size, display scale and page; returns the number of cells checked.
template <typename Pages = decltype (entryPages)>
inline int checkEveryCell (KirinHyphaEditor& editor, const juce::String& who, const Pages& pages = entryPages)
{
    auto* view = component<observatory::View> (editor);
    require (view != nullptr && view->onDomainChange != nullptr, "the domain buttons open their pages");
    juce::Component window;  // the host window holding the editor at the display scale
    window.setVisible (true);
    window.addAndMakeVisible (editor);
    int cells = 0;
    for (const auto& [width, height] : entrySizes)
    {
        editor.setSize (width, height);
        for (const auto page : pages)
        {
            view->onDomainChange (page);
            requireNoPinEntry (editor, "no PIN 4 S in the editor, MENU or the information menu");
            for (const auto scale : displayScales)
            {
                editor.setScaleFactor (scale);
                const auto scaled = editor.getBoundsInParent();
                window.setSize (scaled.getRight(), scaled.getBottom());
                const auto reached = editor_reachability::check (window, [&] (const juce::String& problem)
                {
                    std::cerr << width << "x" << height << " @" << scale << ' ' << who << ' ' << static_cast<int> (view->domain())
                              << ": " << problem << '\n';
                    require (false, "every shown control is inside the editor and reachable");
                });
                require (reached > 0, "the editor shows controls to reach");
                ++cells;
            }
            editor.setScaleFactor (1.0f);
        }
    }
    window.removeChildComponent (&editor);
    view->onDomainChange (observatory::Domain::level);
    return cells;
}

inline void verifyEveryControlReachable()
{
    int cells = 0;
    for (const auto role : { Processor::Role::Post, Processor::Role::Pre })
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        Processor processor (role);
        hypha::tests::prepareFixtureProcessor (processor, 48'000, 960);
        std::unique_ptr<KirinHyphaEditor> editor (dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
        require (editor != nullptr, "shipping editor opens");
        if (role == Processor::Role::Post)
        {
            auto& license = processor.*privateMember (EntryLicense {});
            const auto previous = license.exchange (0);
            const auto menu = (editor.get()->*privateMember (OperationsMenuOf {}))();
            bool enabled = false;
            for (juce::PopupMenu::MenuItemIterator item (menu, true); item.next();)
                if (item.getItem().itemID == 4) enabled = item.getItem().isEnabled;
            license.store (previous);
            require (enabled, "Keep remains available when no PRE is paired");
        }
        editor->setVisible (true);
        // Each language, then switched while the editor stays open (both directions).
        for (const auto language : { i18n::Language::english, i18n::Language::japanese, i18n::Language::english })
        {
            const i18n::ScopedLanguage shown (language);
            (editor.get()->*privateMember (SyncLanguage {})) (true);
            cells += checkEveryCell (*editor, juce::String (role == Processor::Role::Post ? "POST " : "PRE ")
                                                 + (language == i18n::Language::japanese ? "ja" : "en"));
        }
        processor.editorBeingDeleted (editor.get());
        editor.reset();
        processor.releaseResources();
    }
    // A state an older Hypha saved (REF page, the largest size, TRACK/STEM) opens with no PIN 4 S either.
    juce::XmlElement xml ("KirinHyphaState");
    xml.setAttribute ("display_state_version", 4);
    xml.setAttribute ("observatory_domain", 4);
    xml.setAttribute ("observatory_width", 900);
    xml.setAttribute ("observatory_height", 600);
    xml.setAttribute ("meter_context", 1);
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary (xml, bytes);
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        Processor restored (Processor::Role::Post);
        restored.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
        hypha::tests::prepareFixtureProcessor (restored, 48'000, 960);
        std::unique_ptr<KirinHyphaEditor> editor (dynamic_cast<KirinHyphaEditor*> (restored.createEditorIfNeeded()));
        require (editor != nullptr, "the editor opens on an older state");
        editor->setVisible (true);
        for (const auto& [width, height] : entrySizes)
        {
            editor->setSize (width, height);
            requireNoPinEntry (*editor, "no PIN 4 S after an older state is read");
        }
        restored.editorBeingDeleted (editor.get());
        editor.reset();
        restored.releaseResources();
    }
    // LISTEN's footer on POST: PRE / POST / MATCH / END while comparing, END and POST naming an approved
    // lowering, RETURNING after END, RETURN while the lowering is held after the session.
    int listenCells = 0;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        Processor processor (Processor::Role::Post);
        hypha::tests::prepareFixtureProcessor (processor, 48'000, 960);
        std::unique_ptr<KirinHyphaEditor> editor (dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
        require (editor != nullptr, "shipping editor opens");
        editor->setVisible (true);
        auto* view = component<observatory::View> (*editor);
        observatory::LiveCompareFooter comparing;
        comparing.entryEnabled = comparing.active = comparing.preSelected = comparing.matched = comparing.blindAvailable = true;
        comparing.preGainTenthsDb = -35;
        auto lowered = comparing;
        lowered.postHeldTenthsDb = -70;
        observatory::LiveCompareFooter returning;
        returning.finishing = true;
        returning.postHeldTenthsDb = -70;
        observatory::LiveCompareFooter held;
        held.entryEnabled = true;
        held.postHeldTenthsDb = -70;
        for (const auto& footer : { comparing, lowered, returning, held })
        {
            view->setLiveCompareFooter (footer);
            listenCells += checkEveryCell (*editor, juce::String ("POST LISTEN ") + (language == i18n::Language::japanese ? "ja" : "en"),
                                           std::array<observatory::Domain, 2> { observatory::Domain::level, observatory::Domain::reference });
        }
        view->setLiveCompareFooter ({});
        processor.editorBeingDeleted (editor.get());
        editor.reset();
        processor.releaseResources();
    }
    std::cout << "Editor reach: PASS (" << cells << " cells: 8 sizes x 4 display scales x 5 pages x en/ja/switched x PRE/POST;"
              << " LISTEN footer " << listenCells << " cells: 4 states x 8 sizes x 4 scales x 2 pages x en/ja;"
              << " no PIN 4 S in any MENU level, after a language switch or an older state)\n";
}
}
