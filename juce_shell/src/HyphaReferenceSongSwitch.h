#pragma once

#include <cmath>
#include <functional>
#include <limits>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaReferenceOverlayParts.h"

// C の画面の CUE／SONG（2026-11-12）。比べる値の切り替えで、鳴らし方（LOOP／FOLLOW A）と MATCH の gain は変えない。
//  - CUE：今までどおり C の Cue と A の窓（直近）。
//  - SONG：C の曲全体（Kirin OS の ranges の曲全体の範囲）と、A を DAW の頭から溜めた値（A FROM START、
//    reference_audition::ASongMeter）。図と 4 帯域は同じ音量（両方の曲全体の LUFS-I）にそろえて比べる。
//  - C の曲全体の値が無い（古い Kirin OS）時は CUE の値のまま描き、状態の行が理由を言う。
namespace hypha::reference_ui
{
struct SongComparison
{
    bool chosen = false;    // SONG を選んでいる（保存する選択）
    bool shown = false;     // SONG の値で比べている（C の画面・目標でない・C の曲全体の値がある）
    bool cMissing = false;  // SONG を選んだが C の曲全体の値が無い
    bool aBeyond = false;   // A が DAW の頭から 30 分より後を鳴らした（その分は入っていない）
    int aSlots = 0;         // A FROM START の長さ（100 ms の区切りの数）
    double aLoudness = std::numeric_limits<double>::quiet_NaN();  // A FROM START の LUFS-I
    double cLoudness = std::numeric_limits<double>::quiet_NaN();  // C の曲全体の LUFS-I（Kirin OS の値）
};

// Cue の段の右の CUE／SONG。LOOP では CUE の側に Cue の時刻を添える（「CUE 0:30-1:00」）。FOLLOW A では曲全体を
// 鳴らすので時刻を添えない（Cue を鳴らしているように読ませない。時間軸の札の幅も残す）。
class SongSwitch final : public juce::Component
{
public:
    static constexpr int height = 22;
    SongSwitch()
    {
        setComponentID ("reference-compare");
        setTitle ("CUE / SONG");
        cue.setComponentID ("reference-compare-cue");
        song.setComponentID ("reference-compare-song");
        for (auto* half : { &cue, &song })
        {
            half->setWantsKeyboardFocus (true);
            half->setMouseCursor (juce::MouseCursor::PointingHandCursor);
            half->setTooltip (help_text::songCompare);
            addAndMakeVisible (*half);
        }
        cue.onClick = [this] { if (onChoose) onChoose (false); };
        song.onClick = [this] { if (onChoose) onChoose (true); };
    }
    std::function<void (bool)> onChoose;  // true：SONG
    static juce::String cueText (double startSeconds, double endSeconds, bool followA)
    {
        if (followA || ! std::isfinite (startSeconds) || ! std::isfinite (endSeconds) || endSeconds <= startSeconds) return "CUE";
        const auto clock = [] (double seconds)
        {
            const auto whole = juce::roundToInt (seconds);
            return juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2);
        };
        return "CUE " + clock (startSeconds) + "-" + clock (endSeconds);
    }
    // 両方の言葉が切れずに入る幅（それぞれ言葉の幅に余白）。
    static int widthFor (const juce::String& cueWords, presentation::Context context)
    {
        const auto font = labelFont (context, typography::TextRole::unit, typography::Composition::information);
        const auto part = [&font] (const juce::String& words) { return juce::roundToInt (std::ceil (text_style::shownWidth (font, words))) + 20; };
        return part (cueWords) + part ("SONG") + 2;
    }
    void setChoice (bool songChosen, const juce::String& cueWords, presentation::Context next)
    {
        context = next;
        cue.context = song.context = next;
        cue.setButtonText (cueWords);
        cue.setName (cueWords);
        cue.setToggleState (! songChosen, juce::dontSendNotification);
        song.setToggleState (songChosen, juce::dontSendNotification);
        resized();
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const key_light::Scope light (*this);
        surface_material::paintPanel (g, getLocalBounds().toFloat(), 0.82f, 4.0f);
    }
    void resized() override
    {
        const auto font = labelFont (context, typography::TextRole::unit, typography::Composition::information);
        auto area = getLocalBounds().reduced (1);
        const auto cueWidth = juce::roundToInt (std::ceil (text_style::shownWidth (font, cue.getName()))) + 20;
        cue.setBounds (area.removeFromLeft (juce::jmin (cueWidth, area.getWidth() / 2 + area.getWidth() / 4)));
        song.setBounds (area);
    }

private:
    SwitchHalf cue { "CUE" }, song { "SONG" };
    presentation::Context context = presentation::defaultContext();
};
}
