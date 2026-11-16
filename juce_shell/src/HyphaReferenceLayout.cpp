#include "HyphaReferenceComponent.h"

#include "HyphaMainFrame.h"
#include "HyphaReferenceBalance.h"
#include "HyphaReferenceCompactBalance.h"
#include "HyphaReferenceVisuals.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

namespace hypha::reference_ui
{
bool Component::keepPanelStatus (juce::Rectangle<int> area) const
{
    if (statusInFooter()) return false;
    if (presentationContext.logicalWidth > 300 && presentationContext.logicalWidth < 450
        && current.separateComparisons && current.comparisonSlot == 3 && ! guideShown
        && area.getHeight() - statusRowHeight() < compactBalanceMinimumHeight && ! statusRowHasControls()
        && (referenceStatusLine (current, false).kind == StatusKind::ready
            || (current.referenceStep == SourceStep::playDaw && current.preparationOverdue.isEmpty()
                && current.publicationReturn == 0 && ! current.kirinOsRequest
                && current.pendingAudition.stage == reference_audition::PendingAuditionView::Stage::none))
        && ! current.targets.measuring && current.targets.issue.isEmpty() && current.publicationIssue.isEmpty()
        && current.peakShortfallDb <= 0.05 && std::abs (current.rangeShortfallDb) <= 0.5
        && current.heldAttenuationDb >= -0.05 && current.status != "LOW DISK SPACE / REFERENCE AVAILABLE"
        && current.status != "TEMPORARY STORAGE UNAVAILABLE / REFERENCE AVAILABLE") return false;
    return true;
}

bool Component::showCheckPurpose() const noexcept
{
    if (! presentationContext.referenceCheck || presentationContext.referenceCompact) return false;
    if (guideShown && ! detailedLayout()) return false;
    if (detailedLayout() && ! rolePage() && actionButton.isVisible() && statusRowHeight() > 24) return false;
    return detailedLayout() || current.viewBindings.empty() || ! current.viewBindings.front().startsWith ("spectrum_");
}

void Component::resized()
{
    if (targetBar.isVisible())
        targetBar.setBounds (getLocalBounds().reduced (6, shortPanel() ? 3 : 6).removeFromBottom (targetBar.rowHeight()));
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
    // A B C V（左から）。右から V・C・B・A の順に置く。始めた VERSION BLIND の操作は、エディターが窓全体に出す
    // PRE/POST Blind と同じ画面にある（HyphaVersionBlindScreen.h）。
    place (bButton, comparisonWidth);
    if (current.separateComparisons) { place (cButton, comparisonWidth); place (refButton, comparisonWidth); }
    place (aButton, comparisonWidth);
    if (presentationContext.referenceCompact && presentationContext.referenceCheck && ! blindSession)
    {
        matchButton.setBounds (header.removeFromLeft (52).reduced (0, 1)); header.removeFromLeft (4);
        detailsButton.setBounds (header.removeFromLeft (60).reduced (0, 1));
    }
    if (! versionPage() && blindButton.getParentComponent() != &statusStrip) statusStrip.addChildComponent (blindButton);
    // 2026-10-04：300% の B・C・V では選択欄を A・B・C・V のボタンと同じ段の左に置く（曲名の見出しは選択欄と同じなので出さない）。
    auto selectors = header.withRight (juce::jmin (header.getRight(), connectionStatus.getX() - 10));
    if (guideShown && targetBar.isVisible() && ! current.libraryReceived) {} // Leave the initial guide clear.
    else if (checkPage() || versionPage()) layoutCheckPage (area, selectors);  // C・V の画面（HyphaReferenceCheckPage.cpp）
    else if (rolePage() && current.comparisonSlot == 3)
    {
        // B の画面：B SET と曲。V・C の選択欄と Preset・Cue は B では出さない。
        songSetBox.setBounds (selectors.removeFromLeft ((selectors.getWidth() - 8) / 2).removeFromBottom (25));
        selectors.removeFromLeft (8);
        songBox.setBounds (selectors.removeFromBottom (25));
        versionBox.setBounds (songSetBox.getBounds()); checkBox.setBounds (songBox.getBounds());
        if (playbackButtons[1].isVisible())  // 流し方とずらしの段（曲の一覧と BALANCE の上）
        { area.removeFromTop (panelGap()); layoutPlayback (area.removeFromTop (26)); }
    }
    else if (presentationContext.referenceCheck && ! detailedLayout() && ! blindSession)
    {
        area.removeFromTop (panelGap());
        auto row = area.removeFromTop (panelPickerHeight());
        if (presentationContext.referenceCompact)
        {
            const auto width = (row.getWidth() - 4) * 40 / 100;
            presetBox.setBounds (row.removeFromLeft (width)); row.removeFromLeft (4);
            checkBox.setBounds (row);
        }
        else
        {
            const auto width = (row.getWidth() - 8) * 30 / 100;
            presetBox.setBounds (row.removeFromLeft (width)); row.removeFromLeft (4);
            cueBox.setBounds (row.removeFromRight ((row.getWidth() - 4) * 35 / 100)); row.removeFromRight (4);
            checkBox.setBounds (row);
        }
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
            if (! (presentationContext.referenceCompact && current.comparisonSlot == 3))
            { b.removeFromLeft (14); row.removeFromLeft (14); }
            versionBox.setBounds (b); checkBox.setBounds (row);
            if (presentationContext.referenceCheck) checkBox.setBounds (b.getUnion (row));
        }
        songSetBox.setBounds (versionBox.getBounds()); // B の画面では同じ場所。100% は曲名だけを行いっぱいに
        songBox.setBounds (songSetBox.isVisible() ? checkBox.getBounds() : versionBox.getBounds().getUnion (checkBox.getBounds()));
        if (current.comparisonSlot == 3 && playbackButtons[1].isVisible() && ! presentationContext.referenceCompact)  // 300% 未満の B：曲の欄の右に FOLLOW A の切り替え
        {
            auto picker = songBox.getBounds();
            const auto font = labelFont (presentationContext, typography::TextRole::action, typography::Composition::information);
            const auto width = juce::roundToInt (std::ceil (text_style::shownWidth (font, "FOLLOW A"))) + 14;
            songBox.setBounds (picker.withTrimmedRight (width + 4));
            playbackButtons[1].setBounds (picker.removeFromRight (width));
        }
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
    if (presentationContext.referenceCompact && current.comparisonSlot == 3 && playbackButtons[1].isVisible())
    {
        const auto font = labelFont (presentationContext, typography::TextRole::action, typography::Composition::information);
        const auto width = juce::roundToInt (std::ceil (text_style::shownWidth (font, "LOOP")))
            + juce::roundToInt (std::ceil (text_style::shownWidth (font, "FOLLOW A"))) + 35;
        auto row = area.withTrimmedTop (panelGap()).removeFromTop (panelPickerHeight()).removeFromRight (width);
        layoutPlayback (row);
    }
    if (presentationContext.referenceCheck && ! detailedLayout() && ! blindSession
        && (! presentationContext.referenceCompact || detailsOpen))
    {
        auto row = area.removeFromTop (panelPickerHeight());
        if (! presentationContext.referenceCompact && matchButton.isVisible())
        { matchButton.setBounds (row.removeFromRight (50)); row.removeFromRight (3); }
        if (songSwitch.isVisible())
        {
            const auto width = SongSwitch::widthFor ("CUE", presentationContext);
            songSwitch.setBounds (row.removeFromRight (width)); row.removeFromRight (3);
        }
        if (presentationContext.referenceCompact && cueBox.isVisible())
        { cueBox.setBounds (row.removeFromRight (70)); row.removeFromRight (3); }
        layoutPlayback (row);
    }
    if (! blindSession && showCheckPurpose()) area.removeFromTop (16);
    area.removeFromTop (panelGap());
    const bool rowInPanel = keepPanelStatus (area);
    auto footer = rowInPanel ? area.removeFromBottom (statusRowHeight()) : juce::Rectangle<int> {};
    if (checkPage()) area.removeFromBottom (checkFooterHeight());
    // The page's main window, in the frame the page draws around it (paint). B の一覧と BALANCE は枠の外の広さのまま。
    observationArea = area.reduced (main_frame::insetFor (presentationContext), main_frame::insetFor (presentationContext));
    comparisonView.setBounds (observationArea);
    songList.setBounds (area.withWidth (juce::roundToInt (static_cast<float> (area.getWidth()) * 0.52f)));
    balanceBounds = area.withTrimmedTop (panelGap()).withTrimmedLeft (songList.getWidth() + 6);
    const auto switchWidth = OverlaySwitch::widthFor (current.overlay.all, presentationContext);  // 描き方は ALL の時だけ
    if (presentationContext.referenceCompact && current.comparisonSlot == 3)
    {
        overlaySwitch.setBounds (targetBar.getBounds().removeFromTop (24).removeFromLeft (OverlaySwitch::width));
        if (overlaySwitch.isVisible()) overlaySwitch.toFront (false);
    }
    else
        overlaySwitch.setBounds (juce::Rectangle<int> (switchWidth, OverlaySwitch::height)
                                     .withPosition (balanceBounds.getRight() - 9 - switchWidth, balanceBounds.getY() + 3));
    layoutAWindowControls();
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

void Component::configureAWindowControls()
{
    for (size_t index = 0; index < aWindowControls.size(); ++index)
    {
        auto& control = aWindowControls[index];
        control.setComponentID ("reference-a-window-" + juce::String (static_cast<int> (index)));
        control.onChoose = [this] (AWindowControl::Target opened, AWindowControl::Choice choice, int seconds) { if (onSelectAWindow) onSelectAWindow (opened, choice, seconds); };
        control.onReset = [this] (AWindowControl::Target opened) { if (onResetAWindow) onResetAWindow (opened); };
        addChildComponent (control);
    }
}

// A の窓を選ぶボタンは、A の窓の線を描く図の凡例の上にだけ出す：B の Balance（300%）と、C の Cue の図（200% と 300%）。
void Component::layoutAWindowControls()
{
    std::array<juce::Rectangle<int>, 3> areas {};
    if (detailedLayout() && ! guideShown && ! isBlindSession (current.blindPhase) && ! comparisonView.isVisible())
    {
        if (songList.isVisible()) areas[0] = referenceBalanceAWindowArea (balanceBounds.toFloat(), current, presentationContext);
        else if (! (current.separateComparisons && current.comparisonSlot == 3) && ! (current.separateComparisons && current.comparisonSlot == 2 && listeningCheck()))
            areas = referenceAWindowAreas (observationArea.toFloat(), current, presentationContext);
    }
    for (size_t index = 0; index < aWindowControls.size(); ++index)
    {
        auto& control = aWindowControls[index];
        control.setChoice (current.aWindows.of (current.comparisonSlot), current.aWindows.secondsOf (current.comparisonSlot));
        control.setFromStart (! songList.isVisible() && current.song.shown);  // C の SONG の図は A FROM START
        control.setTarget (current.aWindowTarget);
        control.setBounds (areas[index]);
        control.setVisible (! areas[index].isEmpty());
    }
}

juce::Rectangle<int> Component::observationWindowBounds() const noexcept
{
    // REF の枠は、実際に見える主役の窓を一つだけ囲む：V の比較の窓か、C の設定された図の群。B の一覧と BALANCE、
    // 耳で聴き比べる Check の案内は囲まない（paint と同じ条件）。
    if (guideShown || isBlindSession (current.blindPhase)) return {};
    if (comparisonView.isVisible()) return observationArea;
    if (detailedLayout() && ! songList.isVisible() && ! (current.separateComparisons && current.comparisonSlot == 3)
        && ! (current.separateComparisons && current.comparisonSlot == 2 && listeningCheck())
        && configuredReferenceViewsFit (observationArea.toFloat(), current))
        return observationArea;
    return {};
}

}
