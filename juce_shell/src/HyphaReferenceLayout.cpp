#include "HyphaReferenceComponent.h"

namespace hypha::reference_ui
{
void Component::resized()
{
    const int comparisonWidth = comparisonButtonWidth();
    connectionStatus.setBounds (getWidth() - comparisonWidth * (current.separateComparisons ? 4 : 2) - 42, 6, 24, 18);
    auto area = panelArea();
    auto header = area.removeFromTop (panelHeaderHeight());
    const auto place = [this,&header] (juce::Component& button, int width)
    {
        button.setBounds (header.removeFromRight (width).reduced (0, shortPanel() ? 1 : 4));
        header.removeFromRight (3);
    };
    const bool blindSession = isBlindSession (current.blindPhase);
    // A B C V（左から）。右から V・C・B・A の順に置く（H10）。始めた VERSION BLIND の操作は、エディターが窓全体に出す
    // PRE/POST Blind と同じ画面にある（HyphaVersionBlindScreen.h）。
    place (bButton, comparisonWidth);
    if (current.separateComparisons) { place (cButton, comparisonWidth); place (refButton, comparisonWidth); }
    place (aButton, comparisonWidth);
    if (! versionPage() && blindButton.getParentComponent() != &statusStrip) statusStrip.addChildComponent (blindButton);
    // 2026-10-04：300% の B・C・V では選択欄を A・B・C・V のボタンと同じ段の左に置く（曲名の見出しは選択欄と同じなので出さない）。
    auto selectors = header.withRight (juce::jmin (header.getRight(), connectionStatus.getX() - 10));
    if (checkPage() || versionPage()) layoutCheckPage (area, selectors);  // H12・H13: C・V の画面（HyphaReferenceCheckPage.cpp）
    else if (rolePage() && current.comparisonSlot == 3)
    {
        // B の画面：B SET と曲。V・C の選択欄と Preset・Cue は B では出さない。
        songSetBox.setBounds (selectors.removeFromLeft ((selectors.getWidth() - 8) / 2).removeFromBottom (25));
        selectors.removeFromLeft (8);
        songBox.setBounds (selectors.removeFromBottom (25));
        versionBox.setBounds (songSetBox.getBounds()); checkBox.setBounds (songBox.getBounds());
    }
    else if (current.separateComparisons && ! blindSession)
    {
        area.removeFromTop (panelGap());
        auto row = area.removeFromTop (detailedLayout() ? 40 : panelPickerHeight());
        auto b = row.removeFromLeft ((row.getWidth() - 5) / 2);
        row.removeFromLeft (5);
        if (detailedLayout())
        {
            versionBox.setBounds (b.removeFromBottom (25));
            checkBox.setBounds (row.removeFromBottom (25));
        }
        else
        {
            b.removeFromLeft (14); row.removeFromLeft (14);
            versionBox.setBounds (b); checkBox.setBounds (row);
        }
        songSetBox.setBounds (versionBox.getBounds()); // B の画面では同じ場所。100% は曲名だけを行いっぱいに（H10）
        songBox.setBounds (songSetBox.isVisible() ? checkBox.getBounds() : versionBox.getBounds().getUnion (checkBox.getBounds()));
        auto top = area.removeFromTop (selectionVisible (presetBox) || selectionVisible (cueBox)
            ? (detailedLayout() ? 38 : panelPickerHeight()) : 0);
        const auto width = selectionVisible (cueBox) ? (top.getWidth() - 5) * 3 / 5 : top.getWidth();
        presetBox.setBounds (top.removeFromLeft (width).removeFromBottom (detailedLayout() ? 22 : panelPickerHeight()));
        top.removeFromLeft (5);
        cueBox.setBounds (top.removeFromBottom (detailedLayout() ? 22 : panelPickerHeight()));
    }
    else if (detailedLayout() && ! blindSession)
    {
        area.removeFromTop (panelGap());
        auto selectors = area.removeFromTop (46);
        const int gap = 5;
        const int columnWidth = (selectors.getWidth() - gap * 3) / 4;
        presetBox.setBounds (selectors.removeFromLeft (columnWidth).removeFromBottom (27));
        selectors.removeFromLeft (gap);
        checkBox.setBounds (selectors.removeFromLeft (columnWidth).removeFromBottom (27));
        selectors.removeFromLeft (gap);
        candidateBox.setBounds (selectors.removeFromLeft (columnWidth).removeFromBottom (27));
        selectors.removeFromLeft (gap);
        cueBox.setBounds (selectors.removeFromBottom (27));
    }
    else if (! blindSession && (selectionVisible (presetBox) || selectionVisible (checkBox) || selectionVisible (candidateBox)))
    {
        if (selectionVisible (presetBox)) { auto row = area.removeFromTop (panelPickerHeight()); presetBox.setBounds (row); }
        area.removeFromTop (panelGap());
        auto selector = area.removeFromTop (panelPickerHeight());
        constexpr int gap = 5;
        if (selectionVisible (checkBox) && selectionVisible (candidateBox))
        {
            auto checkSelector = selector.removeFromLeft ((selector.getWidth() - gap) * 5 / 12);
            selector.removeFromLeft (gap);
            checkSelector.removeFromLeft (42);
            selector.removeFromLeft (14);
            checkBox.setBounds (checkSelector);
            candidateBox.setBounds (selector);
        }
        else if (selectionVisible (checkBox))
        {
            selector.removeFromLeft (42);
            checkBox.setBounds (selector);
        }
        else
        {
            selector.removeFromLeft (68);
            candidateBox.setBounds (selector);
        }
    }
    area.removeFromTop (panelGap());
    const bool rowInPanel = ! statusInFooter();
    auto footer = rowInPanel ? area.removeFromBottom (statusRowHeight()) : juce::Rectangle<int> {};
    if (checkPage()) area.removeFromBottom (checkFooterHeight());
    comparisonView.setBounds (area);
    tonalView.setBounds (area);
    songList.setBounds (area.withWidth (juce::roundToInt (static_cast<float> (area.getWidth()) * 0.52f))); // H11
    // 状態の行（ボタンは StatusStrip の子）。足元の段に出すときはエディターが置く（REF の中では隠す）。
    if (statusStrip.getParentComponent() == this)
    {
        statusStrip.setInFooter (false);
        statusStrip.setVisible (rowInPanel && ! statusRowConcealed());
        if (rowInPanel) statusStrip.setBounds (footer);
    }
    statusStrip.resized();
    layoutSelectionReadouts();
}

}
