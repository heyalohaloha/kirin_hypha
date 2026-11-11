#pragma once

#include <cmath>
#include <functional>
#include <limits>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaReferenceCueSummary.h"
#include "HyphaReferenceOverlayParts.h"
#include "HyphaUnavailableReason.h"

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

// Cue の段の右の CUE／SONG：枠で囲んだ 2 つの区切りで、選んでいる側を明るい地で埋める。LOOP では CUE の側に Cue の
// 時刻を添える（「CUE 0:30-1:00」）。FOLLOW A では曲全体を鳴らすので時刻を添えない（Cue を鳴らしているように読ませない。
// 時間軸の札の幅も残す）。SONG で比べられない Check（songSupported）では SONG を押せず、押すと理由を言う。
// 文字は C の画面の大きさで 12 px 以上、どの地とも 4.5:1 以上で、透明度で薄めない（試験が確かめる）。
class SongSwitch final : public juce::Component
{
public:
    static constexpr int height = 22;
    static juce::Font font (presentation::Context context)
    { return labelFont (context, typography::TextRole::unit, typography::Composition::information); }
    static juce::Colour frameFill() { return BG.brighter (0.05f); }
    static juce::Colour selectedFill() { return COL_NORMAL.interpolatedWith (BG, 0.80f); }
    static juce::Colour unavailableFill() { return BG; }  // 押せない側は一段沈める（文字は読める濃さのまま）
    static juce::Colour textColour (bool selected) { return selected ? COL_NORMAL : COL_TEXT_SECONDARY; }

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
        const auto part = [context] (const juce::String& words)
        { return juce::roundToInt (std::ceil (text_style::shownWidth (font (context), words))) + 20; };
        return part (cueWords) + part ("SONG") + 4;
    }
    // `songLit`：SONG で比べている。`songAvailable`：この Check で SONG を選べる（だめなら理由を持たせる）。
    void setChoice (bool songLit, bool songAvailable, const juce::String& cueWords, presentation::Context next)
    {
        context = cue.context = song.context = next;
        cue.setButtonText (cueWords);
        cue.setName (cueWords);
        cue.setToggleState (! songLit, juce::dontSendNotification);
        song.setToggleState (songLit, juce::dontSendNotification);
        unavailable::set (song, songAvailable, songUnsupportedText);
        song.setMouseCursor (songAvailable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        resized();
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (frameFill());
        g.fillRoundedRectangle (area, 4.0f);
        g.setColour (COL_MUTED);
        g.drawRoundedRectangle (area, 4.0f, 1.0f);
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (2);
        const auto cueWidth = juce::roundToInt (std::ceil (text_style::shownWidth (font (context), cue.getName()))) + 20;
        cue.setBounds (area.removeFromLeft (juce::jmin (cueWidth, area.getWidth() / 2 + area.getWidth() / 4)));
        song.setBounds (area);
    }

private:
    class Segment final : public juce::Button
    {
    public:
        explicit Segment (const juce::String& text) : juce::Button (text) {}
        presentation::Context context = presentation::defaultContext();
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const key_light::Scope light (*this);
            const auto area = getLocalBounds().toFloat().reduced (0.5f);
            const bool lit = getToggleState() && isEnabled();
            if (! isEnabled()) { g.setColour (unavailableFill()); g.fillRoundedRectangle (area, 3.0f); }
            else if (lit) { g.setColour (selectedFill()); g.fillRoundedRectangle (area, 3.0f); }
            if (isEnabled() && (lit || highlighted || down))
            {
                g.setColour (lit ? COL_TEXT_SECONDARY : COL_MUTED);
                g.drawRoundedRectangle (area, 3.0f, 1.0f);
            }
            g.setColour (textColour (lit));
            g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));  // = font()
            text_style::drawText (g, getName(), getLocalBounds(), juce::Justification::centred);
        }
    };
    Segment cue { "CUE" }, song { "SONG" };
    presentation::Context context = presentation::defaultContext();
};
}
