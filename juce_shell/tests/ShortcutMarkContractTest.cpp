#include "ShortcutKeyRig.h"
#include "EditorEntrySizes.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaShortcutText.h"
#include <string>

namespace hypha::tests
{
namespace
{
void require (bool condition, const std::string& message)
{
    if (! condition) throw std::runtime_error (message);
}

// Every row whole, inside the window, at this size and in this language; the list reaches its end.
void checkList (KeysPanel& panel, const std::string& cell)
{
    const auto* viewport = dynamic_cast<const juce::Viewport*> (panel.findChildWithID ("keys-list"));
    require (viewport != nullptr, "the list scrolls in a viewport");
    auto* list = viewport->getViewedComponent();
    require (list->getRight() <= viewport->getWidth(), "the rows are not wider than the panel: " + cell);
    require (list->getHeight() >= viewport->getMaximumVisibleHeight(), "rows fill the list: " + cell);
    text_style::ShownTextLog log;
    juce::Image canvas (juce::Image::ARGB, juce::jmax (1, list->getWidth()), juce::jmax (1, list->getHeight()), true);
    juce::Graphics g (canvas);
    list->paintEntireComponent (g, true);
    for (const auto& row : panel.rows())
    {
        const auto index = log.texts().indexOf (row.keys);
        require (index >= 0, "the keys are whole: " + row.keys.toStdString() + " " + cell);
        require (! log.areas()[(size_t) index].isEmpty() && log.areas()[(size_t) index].getRight() <= (float) list->getWidth(),
                 "the keys are inside: " + cell);
        require (log.texts().contains (row.where), "the scope is whole: " + cell);
    }
    require (panel.findChildWithID ("keys-close")->getRight() <= panel.getWidth(), "CLOSE is inside: " + cell);
}
}

void verifyShortcutMarks()
{
    i18n::holdLanguage (true);
    ShortcutKeyRig rig;
    auto& view = rig.view();
    auto& tip = rig.editor.get()->*shortcutMember (ShortcutTooltip {});
    const auto tipOf = [&] (const char* id)
    {
        auto* control = view.findChildWithID (id);
        require (control != nullptr, std::string ("control missing: ") + id);
        return tip.getTipFor (*control);
    };
    HoverHelpPreference::shared().overrideForTest (true);
    require (tipOf ("observatory-time").startsWith ("TIME Shift+2"), "a tab shows its key, whatever its size");
    require (tipOf ("observatory-domain-cycle").startsWith ("Shift+1 - 5"), "the small sizes' domain button names its keys");
    require (tipOf ("observatory-hybrid-vu").startsWith ("VU V"), "a footer control shows its key");
    require (shortcuts::withMark ("Help", "VU V") == "VU V\nHelp", "the key goes first, the help below it");
    require (shortcuts::withMark ("Help", {}) == "Help" && shortcuts::withMark ({}, "VU V") == "VU V", "either alone stands");
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    require (tipOf ("observatory-live-compare").startsWith ("LISTEN Q"), "LISTEN shows Q");
    require (tipOf ("observatory-live-pre").startsWith ("PRE 1"), "PRE shows 1");
    require (tipOf ("observatory-live-post").startsWith ("POST 2"), "POST shows 2");
    require (tipOf ("observatory-live-match").startsWith ("MATCH G"), "MATCH shows G");
    require (tipOf ("observatory-live-end").startsWith ("END T"), "END shows T");
    require (tipOf ("observatory-local-blind").startsWith ("BLIND E"), "BLIND shows E");
    require (tipOf ("observatory-candidate-compare").startsWith ("A/B W"), "A/B shows W");
    auto listenFooter = view.liveCompareFooter();
    listenFooter.active = true;
    auto candidateFooter = listenFooter;
    candidateFooter.candidate = true;
    view.setLiveCompareFooter (candidateFooter);
    require (tipOf ("observatory-live-pre").startsWith ("A 1"), "A/B's A shows 1");
    require (tipOf ("observatory-live-post").startsWith ("B 2"), "A/B's B shows 2");
    require (tipOf ("observatory-live-end").startsWith ("END T"), "A/B's END shows T");
    view.setLiveCompareFooter (listenFooter);
    require (tipOf ("observatory-live-pre").startsWith ("PRE 1"), "LISTEN gets its PRE back");
    auto& reference = rig.editor.get()->*shortcutMember (ShortcutReference {});
    const auto refTip = [&] (const char* id)
    {
        auto* control = reference.findChildWithID (id);
        require (control != nullptr, std::string ("reference control missing: ") + id);
        return tip.getTipFor (*control);
    };
    require (refTip ("reference-a").startsWith ("A 1"), "A shows 1");
    require (refTip ("reference-ref").startsWith ("B 2"), "B shows 2");
    require (refTip ("reference-c").startsWith ("C 3"), "C shows 3");
    require (refTip ("reference-b").startsWith ("V 4"), "V shows 4");
    require (refTip ("reference-cue").startsWith ("CUE Shift+X / C"), "the Cue shows its pair");
    require (refTip ("reference-preset").startsWith ("SET Shift+Q / W"), "the set shows its pair");
    auto& blind = rig.editor.get()->*shortcutMember (ShortcutLiveBlindView {});
    require (tip.getTipFor (*blind.findChildWithID ("live-blind-source-1")).startsWith ("SOURCE 1 1"), "SOURCE 1 shows 1");
    require (tip.getTipFor (*blind.findChildWithID ("live-blind-reveal")).startsWith ("REVEAL Shift+E"), "REVEAL shows its key");
    require (tip.getTipFor (*blind.findChildWithID ("live-blind-end")).startsWith ("END T"), "END shows T");
   #endif
    HoverHelpPreference::shared().overrideForTest (false);
    require (tipOf ("observatory-time").isEmpty(), "hover help off stays off");
    HoverHelpPreference::shared().overrideForTest (true);

    // At 300% the footer's line says it, at the right, and the sentence gives way.
    rig.editor->setSize (900, 600);
    auto* hybrid = view.findChildWithID ("observatory-time");
    const auto line = rig.editor->helpLineAt (rig.editor->getLocalPoint (hybrid, hybrid->getLocalBounds().getCentre()));
    require (line.mark == "TIME Shift+2", "the help line carries the key");
    HelpLineBar bar;
    bar.setBounds (0, 0, 260, 24);
    bar.show (juce::String::repeatedString ("A long help sentence ", 12), { 0, 0, 260, 24 }, { 0, 0, 260, 24 },
              presentation::forEditor (900, 600), "REVEAL Shift+E");
    {
        text_style::ShownTextLog log;
        juce::Image canvas (juce::Image::ARGB, 260, 24, true);
        juce::Graphics g (canvas);
        bar.paint (g);
        require (log.texts().contains ("REVEAL Shift+E"), "the mark is drawn whole, however long the sentence");
    }

    // KEYS at every size, in both languages.
    rig.editor->grabKeyboardFocus();
    rig.press ('F');
    require (rig.keys().isVisible() && ! rig.keys().rows().empty(), "the list is open");
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        for (const auto& [width, height] : editor_sizes::entrySizes)
        {
            rig.editor->setSize (width, height);
            checkList (rig.keys(), std::to_string (width) + "x" + std::to_string (height)
                                   + (language == i18n::Language::japanese ? " ja" : " en"));
        }
    }
    rig.press ('F');
    rig.editor->setSize (300, 200);

    i18n::holdLanguage (false);
    HoverHelpPreference::shared().overrideForTest (std::nullopt);
}
}
