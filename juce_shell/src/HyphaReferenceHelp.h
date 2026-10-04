#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

// 2026-10-04 Daisuke「Youlean は細かく下部に説明が入る。Hypha 側にも用途の説明を少し添えても良さそう」→「下の行に
// 出す」を選んだ。300% の B・C・V の画面で項目を指すと、下の状態の行が、その項目が何を測っていて何に使うかの一行に
// 替わり、離すと戻る。図は描く関数が描いた場所に説明を添え（note）、REF が描く間だけ集める（メッセージスレッドだけ）。
// ボタンなどの部品は今の説明（ツールチップ）をこの行に出し、吹き出しは出さない。良し悪しは書かない（R-22）。
namespace hypha::reference_ui::help
{
struct Region
{
    juce::Rectangle<int> area;
    juce::String text;  // 英語（描くときに画面の言語にする）
};

// 描いた場所に説明を添える。集めていないとき（Kirin OS のプレビューなど）は何もしない。
void note (juce::Rectangle<float> area, const juce::String& english);
void note (juce::Rectangle<int> area, const juce::String& english);

// REF が描く間だけ、添えられた説明を `regions` に集める（前のものは消す）。
class Collector final
{
public:
    explicit Collector (std::vector<Region>& regions);
    ~Collector();

private:
    std::vector<Region>* previous = nullptr;
    JUCE_DECLARE_NON_COPYABLE (Collector)
};

// `point` に後から添えられた（上に描いた）説明。無ければ空。
juce::String at (const std::vector<Region>& regions, juce::Point<int> point);

// 部品の説明を下の行に出すか（REF の 300% の B・C・V が自分に付ける）。吹き出し（HoverHelpTooltipWindow）は
// これが付いた部品の中では出さない。
inline const juce::Identifier shownInLineProperty { "hyphaHelpInLine" };

inline bool shownInLine (const juce::Component& component)
{
    for (auto* owner = &component; owner != nullptr; owner = owner->getParentComponent())
        if (static_cast<bool> (owner->getProperties().getWithDefault (shownInLineProperty, false)))
            return true;
    return false;
}
}
