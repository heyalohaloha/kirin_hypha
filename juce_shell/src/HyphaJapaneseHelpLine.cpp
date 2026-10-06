#include "HyphaJapaneseCatalog.h"

// The help line at 300% and above: the shorter versions of the hover helps (HyphaHelpLineText.h).
// Each fits its place in the footer at 300% in Japanese too (ObservatoryHelpLineCheck.h).
namespace hypha::i18n::catalog
{
namespace
{
const Entry entries[] = {
    // The whole footer row: the few helps too long for it.
    { "Click to hold; measurement continues. HOST ~ is the window end on the host clock, not project time.",
      u8"クリックで止めます（計測は続きます）。HOST ~はホストの時計での計測窓の終わりで、プロジェクト上の時刻ではありません。" },
    { "Copy the TP window end on the host clock (project or render clock)",
      u8"TPの計測窓の終わりを、ホストの時計（プロジェクトかレンダー）でコピーします" },
    { "DELAY: POST arrival minus PRE arrival in this band, ms; arrival is where the envelope rises through peak - 20 dB.",
      u8"DELAY：この帯域でのPOSTの到達 − PREの到達（ms）。到達は包絡がピーク − 20 dBを上向きに越えた時刻です。" },
    { "ATT: the rise from 10 % to 90 % of the band peak, ms; one shorter than a period reads as an upper bound.",
      u8"ATT：帯域のピークの10 %から90 %までの立ち上がり（ms）。1周期より短いものは上限で示します。" },
    { "REL: the fall from the band peak to peak - 20 dB, ms. >288 ms: still ringing where the tail ends.",
      u8"REL：帯域のピークからピーク − 20 dBまでの時間（ms）。>288 msは測った尾の終わりでもまだ鳴っています。" },
    { "LEVEL: the band's peak envelope level; POST - PRE in dB when paired, dBFS otherwise.",
      u8"LEVEL：帯域包絡のピークのレベル。ペアのときはPOST − PRE（dB）、それ以外はdBFSです。" },
    { "DELAY: POST minus PRE arrival of each recent hit as dots; the bar is the median.",
      u8"DELAY：直近の各打音の到達の差（POST − PRE）を点、中央値を棒で示します。" },
    { "ATT: POST minus PRE rise of each recent hit as dots; the bar is the median. The shade is one period.",
      u8"ATT：直近の各打音の立ち上がりの差（POST − PRE）を点、中央値を棒で示します。影は1周期です。" },
    { "Summary of the recent hits in this band: each lane's median and how many agree. Click a dot for that hit.",
      u8"この帯域の直近の打音のまとめ：各段の中央値と、同じ向きの打音の数。点をクリックするとその打音を表示します。" },
    { "This PRE predates bands: update PRE to compare the band. POST's own values are shown meanwhile.",
      u8"このPREは帯域に対応する前の版です。PREを更新すると帯域を比べられます。それまではPOSTの値を表示します。" },
    { "Octave band %1: %2. Each hit is measured in this band on PRE and POST once its ring-out is over.",
      u8"%1のオクターブ帯域（%2）。PREとPOSTの各打音を、余韻が終わってからこの帯域で測ります。" },
    // The footer's own controls: the status at the left of the footer.
    { "Finish Keep / Record before Blind", u8"Blindの前にKeep／Recordを終えてください" },
    { "Open the last 4 s of PRE and POST in Blind", u8"直前4秒のPREとPOSTをBlindで開きます" },
    { "Switch PRE and POST while the song plays", u8"曲を再生したままPREとPOSTを切り替えます" },
    { "Return POST to its normal level", u8"POSTを通常の音量に戻します" },
    { "Start a separate Version Blind trial.", u8"別のVersion Blindを始めます" },
    { "AUTO: PRE follows POST loudness. Press to rematch", u8"AUTO：PREがPOSTに追従中（押すと再MATCH）" },
    { "MATCH stopped at the true-peak ceiling", u8"MATCHはTPの上限で止まりました" },
    { "Press to MATCH again or follow (AUTO)", u8"押すと再MATCH、またはAUTOで追従" },
    { "Match PRE to POST over the latest 4 s", u8"直近4秒でPREの音量をPOSTに合わせます" },
    { "Latency changed: stop and restart playback", u8"遅延が変化：再生を止めて再開してください" },
    { "POST plays until PRE is confirmed here", u8"PREを確かめるまでPOSTが鳴ります" },
    { "Turn on delay compensation to hear PRE", u8"遅延補償をONにするとPREが鳴ります" },
    { "Listen to PRE at the MATCH level", u8"MATCHの音量でPREを聴きます" },
    { "Listen to PRE, the input of this chain", u8"このチェーンの入力、PREを聴きます" },
};
}

Section helpLineSection() noexcept
{
    return { "helpLine", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
