#pragma once

#include <functional>

#include "HyphaTextButton.h"
#include "HyphaTextLookAndFeel.h"

// PRE/POST の LIVE BLIND と REF の VERSION BLIND が同じ画面で出す（2026-10-04 Daisuke「PRE POST Blind に形式や
// 見た目をあわせた方が認知負荷が下がる」）。窓全体に、上から題・指示・大きな SOURCE 1｜SOURCE 2・開示｜終了・説明。
// 開示すると題が BLIND RESULT になり、ボタンが「1: PRE」「2: POST」（VERSION は「1: A」「2: V」）に変わって、
// そのまま切り替えられる。中身（Screen）はそれぞれの Blind が作り、見た目と置き方はここだけが決める。
namespace hypha::blind_ui
{
struct Screen
{
    juce::String title, instruction, detail;
    bool guidanceShown = false;  // 止まった・待っている理由（cause）と直し方（recovery）の 2 行
    juce::String cause, recovery;
    bool sourcesShown = false, sourceOneEnabled = false, sourceTwoEnabled = false;
    int audible = 0;  // 1・2：鳴っているソース（そのボタンを点ける）
    juce::String sourceOne { "SOURCE 1" }, sourceTwo { "SOURCE 2" };
    bool revealShown = false, revealEnabled = false;
    juce::String revealTitle { "Reveal the sources without an answer" };
    bool approveShown = false;
    juce::String approveText { "LOWER POST" }, approveTitle, approveDescription;
    bool endEnabled = true;
    juce::String endTitle;

    bool operator== (const Screen&) const noexcept;
    bool operator!= (const Screen& other) const noexcept { return ! (*this == other); }
};

class ScreenComponent : public juce::Component
{
public:
    // 部品の ID は「<prefix>-screen」「<prefix>-source-1」…（LIVE BLIND は live-blind、VERSION BLIND は version-blind）。
    explicit ScreenComponent (const juce::String& idPrefix);
    ~ScreenComponent() override;
    std::function<void(int)> onSelect;
    std::function<void()> onReveal, onEnd, onApprove;
    void setScreen (const Screen&);
    const Screen& screen() const noexcept { return shown; }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void apply();
    TextLookAndFeel look;
    presentation::Context context = presentation::defaultContext();
    Screen shown;
    unsigned languageRevision = 0;
    juce::Label title, status, detail, cause, recovery;
    HyphaTextButton one { "SOURCE 1" }, two { "SOURCE 2" };
    HyphaTextButton reveal { "REVEAL SOURCES" }, end { "END" }, approve { "LOWER POST" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScreenComponent)
};
}
