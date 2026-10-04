#pragma once

#include <cmath>
#include <functional>
#include <limits>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"

// B（REF）の画面の左の曲の一覧。行は 番号・曲名・LUFS-I（Kirin OS の Cue の値）・MATCH の gain・状態
// （PLAYING／READY／PREPARING。準備中は Kirin OS の言う状態）。色で採点しない（状態は文字で出す）。
// 2026-10-04：見出しの下に A の行（直近の窓の音量、金）を置き、MATCH は鳴っていない曲にも押したときの gain
// （A の窓 − その曲の Cue の Integrated）を薄く出す（鳴っている曲だけだった）。
// 行を押すとその曲を選ぶ（B が鳴っていれば B のまま切り替わる）。
namespace hypha::reference_ui
{
class SongList final : public juce::Component
{
public:
    struct Row
    {
        juce::String id, title;
        double lufsI = std::numeric_limits<double>::quiet_NaN();
        double gainDb = std::numeric_limits<double>::quiet_NaN();
        bool selected = false, playing = false, preparing = false;
        juce::String preparation;  // 準備中の曲の Kirin OS の状態（2 AHEAD・CHECKING・NOT FOUND など、無ければ空）
    };

    SongList();
    std::function<void (const juce::String&)> onChoose;
    void setRows (std::vector<Row>, presentation::Context);
    // A の直近の窓の音量（LUFS）と秒数。有限なら見出しの下に A の行を出す。
    void setLive (double loudness, int seconds);
    const std::vector<Row>& rows() const noexcept { return items; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    int rowHeight() const noexcept;
    bool liveShown() const noexcept { return std::isfinite (liveLoudness); }
    std::vector<Row> items;
    double liveLoudness = std::numeric_limits<double>::quiet_NaN();
    int liveSeconds = 0;
    presentation::Context context = presentation::defaultContext();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongList)
};
}
