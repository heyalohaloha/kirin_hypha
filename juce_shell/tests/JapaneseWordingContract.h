#pragma once

#include "../src/HyphaJapaneseCatalog.h"
#include "../src/HyphaLanguage.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
// User-facing distinctions: a sound source, a listening position, a measured interval,
// and a button to press. Check rendered translations, including substituted values.
inline void verifyJapaneseWording()
{
    const char* const cases[][2] {
        { "B plays a Reference song; C uses its Cue; V follows the song.",
          u8"Bは Reference の曲を鳴らし、CはCueを使い、Vは再生中の曲に追従します。" },
        { "A stays live until ready. Press A to cancel.",
          u8"準備が整うまで音はAのままです。Aを押すと取り消せます。" },
        { "V opened at 300%. Press V to listen.", u8"Vを300%で開きました。Vを押すと試聴します。" },
        { "A was not lowered. Press the role again.",
          u8"Aは下げていません。もう一度同じボタンを押してください。" },
        { "KIRIN OS CANNOT FIND THE FILE / RETRY IN KIRIN OS",
          u8"Kirin OSがファイルを見つけられません ／ Kirin OSで再試行" },
        { "SOURCE RECORD DOES NOT MATCH", u8"音源の登録情報が合いません" },
        { "SOURCE RECORD NOT READ", u8"音源の登録情報を読めません" },
        { "SOURCE FORMAT CHANGED", u8"音源の形式が変わりました" },
        { "THIS PASSAGE REPEATS IN V", u8"この箇所はVに何度も出てきます" },
        { "Choose a prepared listening position for this audition.",
          u8"この試聴で使う、準備済みのCue（聴く箇所）を選びます。" },
        { "Some Kirin OS sets were not read. Update Kirin OS and Hypha.",
          u8"Reference のセットの一部を読めませんでした。Kirin OSとHyphaを更新してください。" },
        { "Checks of this CHECK SET from Kirin OS. A tab changes C's Check and keeps the song.",
          u8"Kirin OSから届いたCHECK セットのCheckの一覧。タブで曲はそのままCのCheckを替えます。" },
        { "Balance: A's last 10 seconds, the chosen song and the Reference range, at B's level.",
          u8"Balance：Aの直近10秒、選んだ曲、Reference の範囲。Bは鳴らす音量で並べます。" },
        { "MEASURING", u8"計測中" },
        { "FORMAT HELD / STOP KEEP", u8"構成変更を保留 ／ Keepを停止" },
        { "SLOWER 2 ms", u8"2 ms遅い" },
        { "ATTACK 2 ms FASTER", u8"立ち上がりが2 ms速い" },
        { "Next hit", u8"次の打音" },
        { "Waiting for publication", u8"結果の反映待ち" },
        { "Within RES", u8"RES内" },
        { "Mean in dB; identical participants for PRE and POST. Set changes break lines and fill.",
          u8"dBで平均。PREとPOSTは同じ打音の組。組が変わる所で線と塗りを切ります。" },
        { "First 30 ms RMS minus body RMS.", u8"First 30 ms RMS minus body RMS." },
        { "POST arrival minus PRE arrival in this band.", u8"POST arrival minus PRE arrival in this band." },
        { "Work cannot preserve this observation; save the local PNG instead",
          u8"この計測値はWorkに保存できません。ローカルPNGを保存してください" },
        { "PRE timing is not confirmed", u8"PREとPOSTのタイミングを確かめられません" },
        { "The PRE loop occurrence is not verified", u8"PREがループの何周目か確かめられていません" },
        { "Checking loop timing; POST plays", u8"ループのタイミングを確認中（今はPOST）" },
        { "Sources revealed; keep comparing", u8"答えを表示しました。そのまま比較できます" },
        { "Reveal the sources without an answer", u8"回答せずに答えを表示します" },
        { "The previous capture is still releasing its resources.",
          u8"前の取り込みの後片付けが終わっていません。" },
        { "The comparison ownership changed.", u8"出力が別の試聴に移りました。" },
        { "Keep setup failed. Select PRE again.",
          u8"Keepの準備失敗。PREを選び直してください。" },
        { "Blind view opened. Press VERSION BLIND to start after checking the level.",
          u8"Blind画面です。音量を見てVERSION BLINDを押す" },
        { "Capture attached to Work", u8"画像をWorkに添付しました" },
        { "PRE is multi-mono: insert it as stereo", u8"PREがMulti-monoです。Stereoで挿してください" },
        { "Hypha v1.2.3 available; open PRE / POST for downloads",
          u8"Hypha v1.2.3があります。PRE／POSTメニューで入手できます" },
        { "HELD", u8"HOLD中" },
        { "Guide retained", u8"Guideを保持" },
    };
    auto valid = true;
    for (const auto& pair : cases)
    {
        const auto english = juce::String::fromUTF8 (pair[0]);
        const auto actual = i18n::translate (english, i18n::Language::japanese);
        if (actual != juce::String::fromUTF8 (pair[1])
            || i18n::translate (english, i18n::Language::english) != english)
        {
            std::cerr << "Japanese wording: " << english << " -> " << actual << '\n';
            valid = false;
        }
    }
    // All fixed toasts keep their full UTF-8 text in the engine's 128-byte buffer.
    const auto notices = i18n::catalog::noticeSection();
    for (std::size_t index = 0; index < notices.count; ++index)
    {
        auto text = juce::String::fromUTF8 (notices.entries[index].japanese);
        text = text.replace ("%1", "POST").replace ("%2", "POST").replace ("%3", "POST");
        if (text.getNumBytesAsUTF8() >= 128)
        {
            std::cerr << "Japanese toast exceeds its UTF-8 buffer: " << text << '\n';
            valid = false;
        }
    }
    // The accepted version components and comparison levels also fit at their boundaries.
    for (const auto* english : {
        "Hypha v2147483647.2147483647.2147483647 available; open PRE / POST for downloads",
        u8"TP limit: PRE \u221224.0 dB, need +24.0 dB",
        "C needs A 24.0 dB lower to match. Press LOWER A.",
        "A lowered 24.0 dB to match: A got louder after the offer.",
        "PRE / POST LISTEN is using POST. End it or press RETURN, then press the role again.",
        "C could not switch at this playhead. A remains live; retry when C is ready.",
        "Some Kirin OS sets were not read. Update Kirin OS and Hypha." })
    {
        const auto text = i18n::translate (english, i18n::Language::japanese);
        if (! i18n::hasTranslation (english) || text.getNumBytesAsUTF8() >= 128)
        {
            std::cerr << "Japanese notice boundary: " << text << '\n';
            valid = false;
        }
    }
    if (! valid) std::exit (EXIT_FAILURE);
}
}
