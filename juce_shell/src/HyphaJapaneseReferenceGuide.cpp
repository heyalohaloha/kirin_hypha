#include "HyphaJapaneseCatalog.h"

// The REFERENCE page's guide (HyphaReferenceGuide.cpp): the next step while V and C cannot be heard,
// what A, V and C are, where each stands, and the reason a V or C not ready yet gives on hover and
// after a click. Headings ask politely; the rows are short.
namespace hypha::i18n::catalog
{
namespace
{
const Entry entries[] = {
    // The next step.
    { "Open Kirin OS", u8"Kirin OSを開いてください" },
    { "Receiving from Kirin OS", u8"Kirin OSから受け取っています" },
    { "Play the song in your DAW", u8"DAWで曲を再生してください" },
    { "Register a Version of this song in Kirin OS",
      u8"Kirin OSでこの曲のVersionを登録してください" },
    { "Choose a Version for V", u8"VのVersionを選んでください" },
    { "No Check is enabled in Kirin OS", u8"Kirin OSで有効なCheckがありません" },
    { "Choose a source for %1 in Kirin OS", u8"Kirin OSで%1の音源を選んでください" },
    { "Aligning V with A", u8"VをAに位置合わせしています" },
    { "Play another passage to align V", u8"Vを合わせるため、別の箇所を再生してください" },
    { "Preparing %1", u8"%1を準備しています" },
    { "Kirin OS's B sets were not read", u8"Kirin OSのBセットを読めませんでした" },
    { "The source of %1 changed", u8"%1の音源が変わりました" },
    { "The format of %1's source changed", u8"%1の音源の形式が変わりました" },
    { "%1's source could not be opened", u8"%1の音源を開けませんでした" },
    { "%1's source is unavailable", u8"%1の音源を使えません" },
    { "The saved choice for %1 is gone", u8"%1の保存した選択がありません" },

    // Why.
    { "Versions and References registered in Kirin OS arrive here automatically.",
      u8"Kirin OSに登録したVersionとReferenceが、ここに自動で届きます。" },
    { "V follows the song; C uses its Cue.",
      u8"Vは同曲、Cは設定済みCueを比較します。" },
    { "V plays another Version of the song you are playing, registered in Kirin OS.",
      u8"Vでは、Kirin OSに登録した、再生中の曲の別Versionを鳴らします。" },
    { "Choose it in V / VERSION above. V plays another Version of the song you are playing.",
      u8"上の「V / VERSION」で選びます。Vでは、再生中の曲の別Versionを鳴らします。" },
    { "C plays the source a Check compares your mix with.",
      u8"Cでは、Checkでミックスと比べる音源を鳴らします。" },
    { "Keep playing. V must be a Version of the song you are playing.",
      u8"再生を続けてください。Vには、再生中の曲のVersionを選びます。" },
    { "This passage repeats in V. Play a part that occurs only once.",
      u8"この部分はVの中で繰り返し出てきます。一度だけ出てくる部分を再生してください。" },
    { "This takes a moment.", u8"少しお待ちください。" },
    { "B plays the songs of the B sets you rank for Hypha in Kirin OS.",
      u8"Bは、Kirin OSでHyphaに出したBセットの曲を鳴らします。" },
    { "Prepare it again in Kirin OS. A stays live.", u8"Kirin OSで準備し直してください。Aを保ちます。" },
    { "Verify it in Kirin OS. A stays live.", u8"Kirin OSで確認してください。Aを保ちます。" },
    { "Check the source file in Kirin OS. A stays live.", u8"Kirin OSで音源のファイルを確認してください。Aを保ちます。" },
    { "Open Kirin OS to make the source available. A stays live.", u8"Kirin OSを開いて音源を使えるようにしてください。Aを保ちます。" },
    { "Choose the source again. A stays live.", u8"音源を選び直してください。Aを保ちます。" },
    { "Update Kirin OS and Hypha to the same release.", u8"Kirin OSとHyphaを同じリリースに更新してください。" },
    // 断られた理由ごとの段階（2026-10-18）。知らない理由はコードで言う。
    { "STEREO SOURCE ON A MONO TRACK", u8"モノラルのトラックにステレオの音源" },
    { "USE A STEREO TRACK", u8"ステレオのトラックを使う" },
    { "Stereo source on a mono track", u8"モノラルのトラックにステレオの音源があります" },
    { "%1's source is stereo and this track is mono", u8"%1の音源はステレオで、このトラックはモノラルです" },
    { "A stereo source plays on a stereo track. A mono source plays on either. A stays live.",
      u8"ステレオの音源はステレオのトラックで鳴らせます。モノラルの音源はどちらでも鳴らせます。Aを保ちます。" },
    { "SOURCE RECORD DOES NOT MATCH", u8"音源の記録がファイルと合わない" },
    { "Source record does not match", u8"音源の記録がファイルと合いません" },
    { "%1's source record does not match its file", u8"%1の音源の記録がファイルと合いません" },
    { "SOURCE RECORD NOT READ", u8"音源の記録を読めない" },
    { "Source record not read", u8"音源の記録を読めません" },
    { "%1's source record was not read", u8"%1の音源の記録を読めませんでした" },
    { "Update Kirin OS and Hypha to the same release. A stays live.",
      u8"Kirin OSとHyphaを同じリリースに更新してください。Aを保ちます。" },
    { "SOURCE NOT USABLE IN HYPHA", u8"Hyphaでは使えない音源" },
    { "SEE THE REASON CODE", u8"理由のコードを確認" },
    { "Not usable in Hypha", u8"Hyphaでは使えません" },
    { "Not usable in Hypha (%1)", u8"Hyphaでは使えません (%1)" },
    { "%1's source is not usable in Hypha", u8"%1の音源はHyphaでは使えません" },
    { "Hypha has no message for this reason. A stays live.", u8"この理由の説明はHyphaにありません。Aを保ちます。" },
    { "Reason code: %1", u8"理由のコード：%1" },
    { "REASON CODE %1", u8"理由のコード %1" },
    { "NO REASON CODE", u8"理由のコードなし" },
    { "Publication not read", u8"公開データを読めません" },
    { "%1's Reference publication was not read", u8"%1のReferenceの公開データを読めませんでした" },
    { "Publish it again in Kirin OS. A stays live.", u8"Kirin OSで再公開してください。Aを保ちます。" },

    // A, V and C, and where each stands.
    { "Your mix, live from the DAW", u8"今のミックス（DAWの音）" },
    { "A Version of this song, from Kirin OS", u8"この曲のVersion（Kirin OSから）" },
    { "The source of a Check, from Kirin OS", u8"Checkの比較音源（Kirin OSから）" },
    { "Playing", u8"再生中" },
    { "Stopped", u8"停止中" },
    { "Ready", u8"準備完了" },
    { "Register a Version in Kirin OS", u8"Kirin OSでVersionを登録" },
    { "Choose a Version", u8"Versionを選ぶ" },
    { "Enable a Check in Kirin OS", u8"Kirin OSでCheckを有効にする" },
    { "Choose a source in Kirin OS", u8"Kirin OSで音源を選ぶ" },
    { "Ready when the DAW plays", u8"再生すると使えます" },
    { "Aligning with A. Keep playing", u8"Aに位置合わせ中。再生を続ける" },
    { "Play another passage", u8"別の箇所を再生" },
    { "Preparing", u8"準備中" },
    { "Check the source in Kirin OS", u8"Kirin OSで音源を確認" },
    { "Prepare the source again in Kirin OS", u8"Kirin OSで音源を準備し直す" },
    { "Update Kirin OS and Hypha", u8"Kirin OSとHyphaを更新" },
    { "Verify the source in Kirin OS", u8"Kirin OSで音源を確認し直す" },
    { "Choose again", u8"選び直す" },
    { "Press A, B, C or V to switch audio and open its page. "
      "The audition level is shown while listening.",
      u8"A・B・C・Vで音を切り替え、その役の画面を開きます。試聴中の音量は下に表示します。" },

    // A B, C or V not ready yet, on hover and after a click.
    { "B: %1", u8"B：%1" },
    { "V: %1", u8"V：%1" },
    { "C: %1", u8"C：%1" },

    // LISTEN while B, C or V in REF holds the output for more than 3 s (HyphaReferenceAuditionRecoveryText.h).
    { "B plays from REF: press A there to listen", u8"REFのBを再生中。REFでAを押すと戻ります" },
    { "C plays from REF: press A there to listen", u8"REFのCを再生中。REFでAを押すと戻ります" },
    { "V plays from REF: press A there to listen", u8"REFのVを再生中。REFでAを押すと戻ります" },
    { "B plays now. Press A in REF to hear PRE.", u8"今はB。REFでAを押すとPREに戻ります。" },
    { "B plays now. Press A in REF to hear POST.", u8"今はB。REFでAを押すとPOSTに戻ります。" },
    { "C plays now. Press A in REF to hear PRE.", u8"今はC。REFでAを押すとPREに戻ります。" },
    { "C plays now. Press A in REF to hear POST.", u8"今はC。REFでAを押すとPOSTに戻ります。" },
    { "V plays now. Press A in REF to hear PRE.", u8"今はV。REFでAを押すとPREに戻ります。" },
    { "V plays now. Press A in REF to hear POST.", u8"今はV。REFでAを押すとPOSTに戻ります。" },
};
}

Section referenceGuideSection() noexcept
{
    return { "reference guide", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
