#include "HyphaJapaneseCatalog.h"

// The analysis pages: FREQ (Spectrum, PSB, Focus Trail), SHARP, LIVE, SPACE and DRUM. Their
// statuses, the notices a refused mode gives, and their hover help.
namespace hypha::i18n::catalog
{
namespace
{
const Entry entries[] = {
    // Shared analysis states.
    { "INACTIVE", u8"入力なし" },
    { "PREPARING ANALYSIS", u8"解析を準備中" },
    { "ANALYSIS DATA UNAVAILABLE", u8"解析データがありません" },
    { "ANALYSIS IN USE", u8"解析を使用中" },
    { "ANALYSIS SLOTS IN USE", u8"解析の枠を使用中" },
    { "PAIR PRE TO VIEW DIFFERENCE\nPOST SHARPNESS IS IN LIVE",
      u8"差分を見るにはPREとペアにしてください\nPOSTのシャープネスはLIVEにあります" },

    // FREQ help.
    { "M/S: show POST Mid and Side together. Stereo only.",
      u8"M/S：POSTのMidとSideを重ねて表示します。ステレオのみです。" },
    { "MID: analyze (L + R) / 2.", u8"MID：(L + R) / 2 を解析します。" },
    { "SIDE: analyze (L - R) / 2. Stereo only.", u8"SIDE：(L - R) / 2 を解析します。ステレオのみです。" },
    { "LR: analyze L and R separately, then average power.",
      u8"LR：LとRを別々に解析し、パワーを平均します。" },
    { "Select POST to show Mid and Side together.",
      u8"MidとSideを重ねて見るには、POSTを選んでください。" },
    { "M/S requires a stereo input.", u8"M/Sにはステレオの入力が必要です。" },
    { "POST Mid and Side at one frequency. Click to lock the readout.",
      u8"ひとつの周波数でのPOSTのMidとSideです。クリックで読み取りを固定します。" },
    { "Move to inspect frequency and Delta. Click to lock Focus Trail.",
      u8"動かすと周波数と差分を表示します。クリックでFocus Trailを固定します。" },
    { "POST Spectrum: current magnitude, six-second field, and rolling peak hold.",
      u8"POSTのスペクトラム：今のレベル、6秒間の履歴、ピークホールドです。" },
    { "~ means approximate frequency. Very low tones need a longer window to locate exactly.",
      u8"~ はおおよその周波数です。とても低い音の位置を正確に求めるには、長い窓が必要です。" },
    { "Delta: POST minus PRE on the left dB scale.", u8"Δ：POST − PREの差分です。左のdB目盛りで読みます。" },
    { "PRE: input spectrum on the right dBFS scale.",
      u8"PRE：入力のスペクトラムです。右のdBFS目盛りで読みます。" },
    { "POST: output spectrum on the right dBFS scale.",
      u8"POST：出力のスペクトラムです。右のdBFS目盛りで読みます。" },
    { "Clear the MARK reference.", u8"MARKの基準線を消します。" },
    { "MARK: freeze the current full-band Delta curve.",
      u8"MARK：今の全帯域の差分カーブを固定して残します。" },
    { "Release the frequency lock.", u8"周波数の固定を解除します。" },
    { "Focus Trail: six seconds of Delta at the locked frequency.",
      u8"Focus Trail：固定した周波数での6秒間の差分です。" },
    { "PSB: perceptual share by Bark band", u8"PSB：Bark帯域ごとの知覚上の割合" },
    { "Spectrum: frequency level and difference", u8"スペクトラム：周波数ごとのレベルと差分" },
    { "Return to Spectrum", u8"スペクトラムに戻ります" },
    { "Show perceptual spectral balance", u8"知覚上の帯域バランスを表示します" },
    { "LR specific-loudness share", u8"LRの特定ラウドネスの割合" },
    { "FOCUS TRAIL  /  CLICK A BAND", u8"FOCUS TRAIL  /  帯域をクリック" },
    { "Analysis size: 100%", u8"解析の大きさ：100%" },
    { "Analysis size: 125%", u8"解析の大きさ：125%" },
    { "Analysis size: 150%", u8"解析の大きさ：150%" },
    { "Analysis size: 200%", u8"解析の大きさ：200%" },
    { "Analysis size: 300% inspection view", u8"解析の大きさ：300%（Inspection View）" },
    { "Show POST - PRE analysis", u8"POST − PREの解析を表示します" },
    { "Return to meters", u8"メーターに戻ります" },

    // A mode the page cannot take says why for a moment.
    { "M/S -- POST STEREO", u8"M/S：要POSTステレオ" },
    { "SIDE -- STEREO", u8"SIDE：ステレオのみ" },
    { "SIDE -- MONO", u8"SIDE：モノ不可" },
    { "MODE --", u8"使用不可" },
    { "MARK --", u8"MARK不可" },

    // PSB.
    { "PRE REQUIRED FOR DELTA", u8"差分にはPREが必要です" },
    { "PSB UNAVAILABLE", u8"PSB利用不可" },
    { "PSB WARMING", u8"PSBを準備中" },

    // SHARP and LIVE help.
    { "Sharpness Delta is POST minus PRE. Unit: acum (DIN 45692).",
      u8"シャープネスの差分（POST − PRE）です。単位：acum（DIN 45692）。" },
    { "LIVE shows absolute POST facts on independent fixed scales.",
      u8"LIVEは、POSTの絶対値をそれぞれ固定の目盛りで表示します。" },
    { "LUFS-M: 400 ms momentary loudness. Absolute POST value.",
      u8"LUFS-M：400 msのモーメンタリーラウドネスです。POSTの絶対値です。" },
    { "True Peak: highest inter-sample peak in 400 ms. Unit: dBTP.",
      u8"トゥルーピーク：400 msの間のサンプル間ピークの最大値です。単位：dBTP。" },
    { "Sharpness: high-frequency weighting. Unit: acum (DIN 45692).",
      u8"シャープネス：高域の重み付けです。単位：acum（DIN 45692）。" },

    // SPACE.
    { "MONO INPUT", u8"モノラル入力" },
    { "WARMING %1/30", u8"準備中 %1/30" },

    // DRUM: why a hit has no value, and the page's own states.
    { "NEXT HIT", u8"次の打音" },
    { "QUIET AFTER", u8"後が無音" },
    { "PRE ONLY", u8"PREのみ" },
    { "POST ONLY", u8"POSTのみ" },
    { "NO PAIR", u8"ペアなし" },
    { "NO HIT", u8"打音なし" },
    { "UNAVAILABLE", u8"利用不可" },
    { "WARMING UP", u8"準備中" },
    { "Drum transient facts for TRACK/STEM; not a 2MIX onset detector.",
      u8"TRACK／STEMのドラムの打音ごとの計測値です。2MIXの立ち上がり検出ではありません。" },
};
}

Section analysisSection() noexcept
{
    return { "analysis", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
