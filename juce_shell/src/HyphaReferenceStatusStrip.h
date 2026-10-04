#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// 2026-10-04：REF の状態の行（色の点・状態の文・鳴っている役の gain・承認と VERSION BLIND の
// ボタン）を 1 つの入れ物にする。300% の B・C・V の画面では、エディターが足元の段の左（LIVE／HOLD の場所）に
// 置き、図の高さを空ける。ほかのときは REF の中の今の場所に置く。描くのは REF（paintRow）で、ボタンは子に持つ。
// 入れ物そのものはクリックを取らない（Blind の 1・2・REVEAL など、下に重なるボタンへ通す）。
namespace hypha::reference_ui
{
class StatusStrip final : public juce::Component
{
public:
    std::function<void (juce::Graphics&, juce::Rectangle<int>, bool inFooter)> paintRow;
    std::function<void (juce::Rectangle<int>)> layoutRow;
    juce::Colour footerFill;

    StatusStrip() { setInterceptsMouseClicks (false, true); }
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
