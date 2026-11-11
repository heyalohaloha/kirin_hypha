#pragma once

#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "HyphaReferenceOverlayParts.h"

// B（REF）の画面の左の曲の一覧。行は 番号・曲名・LUFS-I（Kirin OS の Cue の値）・MATCH の gain・状態
// （PLAYING／SWITCHING／READY／OS READY／PREPARING。準備中は Kirin OS の言う状態）。色で採点しない（状態は文字で出す）。
// 2026-10-04：見出しの下に A の行（直近の窓の音量、金）を置き、MATCH は鳴っていない曲にも押したときの gain
// （A の窓 − その曲の Cue の Integrated）を薄く出す（鳴っている曲だけだった）。
// 行を押すとその曲を選ぶ（B が鳴っていれば B のまま切り替わる）。
// 2026-11-11：Balance の ALL では番号が線を出し入れする箱になり、セットの曲の後に目標（DATA、音なし）を入れた順に
// 並べる。入りきらない行はホイールと右の帯で送る（選んだ曲は見える所へ送る）。
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
        double lowerAOfferDb = std::numeric_limits<double>::quiet_NaN();
        bool switching = false;  // 選んだ曲が鳴るのを待っている（前の曲が鳴っているあいだを含む）
        bool loaded = false;     // Hypha が確かめ終えている（押せばすぐ替わる）。無ければ Kirin OS の準備だけ
        bool target = false;     // ドロップした目標（測った値だけで音は無い）
        bool shown = false;      // ALL で線を出している
        int colour = 0;          // 出している線の色（HyphaReferenceOverlayParts.h）
    };

    SongList();
    std::function<void (const juce::String&)> onChoose;
    std::function<void (const juce::String&)> onToggle;  // ALL：番号を押した（線を出す・消す）
    std::function<void (const juce::String&)> onPoint;   // 指している行（離れたら空）
    void setRows (std::vector<Row>, presentation::Context);
    void setOverlay (bool all);  // ALL の時だけ番号を箱にする
    // A の直近の窓の音量（LUFS）と秒数。有限なら見出しの下に A の行を出す。
    void setLive (double loudness, int seconds);
    const std::vector<Row>& rows() const noexcept { return items; }
    int firstShownRow() const noexcept { return firstRow; }
    int shownRowCount() const noexcept { return layout().visible; }
    void scrollTo (int row);
    void paint (juce::Graphics&) override;
    void resized() override { placeToggles(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Layout
    {
        juce::Rectangle<int> content, header, live, rows, bar;  // bar：送りの帯（入りきる時は空）
        int visible = 0;
    };
    Layout layout() const noexcept;
    int rowHeight() const noexcept;
    int rowAt (juce::Point<int>) const noexcept;  // 描いている行の番号（無ければ -1）
    void placeToggles();
    bool liveShown() const noexcept { return std::isfinite (liveLoudness); }
    std::vector<Row> items;
    std::vector<std::unique_ptr<NumberToggle>> toggles;
    bool overlayAll = false;
    int firstRow = 0;
    double wheel = 0.0;
    juce::String shownSelection, pointed;
    double liveLoudness = std::numeric_limits<double>::quiet_NaN();
    int liveSeconds = 0;
    presentation::Context context = presentation::defaultContext();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongList)
};
}
