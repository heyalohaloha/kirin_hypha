#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// 2026-10-04：REF の状態の行（色の点・状態の文・鳴っている役の gain・承認と VERSION BLIND の
// ボタン）を 1 つの入れ物にする。300% の B・C・V の画面では、エディターが足元の段の左（LIVE／HOLD の場所）に
// 置き、図の高さを空ける。ほかのときは REF の中の今の場所に置く。描くのは REF（paintRow）で、ボタンは子に持つ。
// 入れ物そのものはクリックを取らない（Blind の 1・2・REVEAL など、下に重なるボタンへ通す）。
namespace hypha::reference_ui
{
class StatusStrip final : public juce::Component, public juce::TooltipClient
{
public:
    std::function<void (juce::Graphics&, juce::Rectangle<int>, bool inFooter)> paintRow;
    std::function<void (juce::Rectangle<int>)> layoutRow;
    // 切れている状態の文の全文（指すと読める。2026-10-06：300% 未満では読む手段が無かった）。300% は足元の説明の行が出す。
    std::function<juce::String()> wholeText;
    juce::Colour footerFill;

    // 行の上で指したことを受ける（全文の吹き出し）。足元では下に隠した LIVE／HOLD を押させない。
    StatusStrip() { setInterceptsMouseClicks (true, true); }
    juce::String getTooltip() override { return wholeText ? wholeText() : juce::String(); }
    bool inFooter() const noexcept { return footer; }
    void setInFooter (bool value) { if (footer != value) { footer = value; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        if (footer) g.fillAll (footerFill);  // 足元では下の LIVE／HOLD を隠す
        if (paintRow) paintRow (g, getLocalBounds(), footer);
    }
    void resized() override { if (layoutRow) layoutRow (getLocalBounds()); }

private:
    bool footer = false;
};
}
