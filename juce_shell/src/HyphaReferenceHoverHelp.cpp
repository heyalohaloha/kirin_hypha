#include "HyphaReferenceComponent.h"

#include "HyphaReferenceHelpText.h"

// 300% の B・C・V で指している項目の説明（HyphaReferenceHelp.h）。部品は今の説明（ツールチップ）、説明の無い部品
// （タブ・曲の一覧・WHOLE）はここで決めた一行、図は描いたときに添えた一行。足元に出すのはエディターの説明の行
// （PluginEditorHelpLine.cpp。2026-10-04 Daisuke「足元の段の全幅を使う」）。
namespace hypha::reference_ui
{
// 300% だけ（200% 以下の足元の行は説明の一行に足りないので、今までどおり吹き出し）。
bool Component::helpInLine() const noexcept
{
    return rolePage() && presentationContext.density == observatory::Density::inspection;
}

juce::String Component::helpAt (juce::Point<int> local)
{
    if (! helpInLine() || ! getLocalBounds().contains (local)) return {};
    for (auto* under = getComponentAt (local); under != nullptr && under != this; under = under->getParentComponent())
    {
        if (auto* client = dynamic_cast<juce::SettableTooltipClient*> (under);
            client != nullptr && client->getTooltip().isNotEmpty())
            return client->getTooltip();
        if (under == &checkTabs) return versionPage() ? help_text::versionTabs : help_text::checkTabs;
        if (under == &songList) return help_text::songs;
        if (under == &comparisonView) return comparisonView.helpAt (comparisonView.getLocalPoint (this, local));
    }
    return help::at (helpRegions, local);
}
}
