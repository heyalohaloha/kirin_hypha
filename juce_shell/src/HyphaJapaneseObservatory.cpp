#include "HyphaJapaneseCatalog.h"

// The Observatory shell: header and footer help, LEVEL and TIME help, status lines and the PRE /
// POST comparison states. Loudness terms follow ARIB TR-B32's katakana.
namespace hypha::i18n::catalog
{
namespace
{
const Entry entries[] = {
    // Header and footer help.
    { "Choose history time range", u8"履歴の時間範囲を選びます" },
    { "Switch Momentary / Short-term loudness",
      u8"モーメンタリーとショートタームのラウドネスを切り替えます" },
    { "Switch current / session maximum values", u8"現在値とセッション最大値を切り替えます" },
    { "Choose whether this instance observes a mix bus or a track / stem",
      u8"このインスタンスで観測する対象（ミックスバス、またはトラック／ステム）を選びます" },
    { "Switch WIDE / FOCUS loudness scale", u8"ラウドネスの目盛り（WIDE／FOCUS）を切り替えます" },
    { "Choose an exact editor size", u8"画面の大きさを選びます" },
    { "Keep, measurement, and display controls", u8"Keep、計測、表示の操作" },
    { "Stop the selected PRE / POST Keep", u8"選択中のPRE／POSTのKeepを止めます" },
    { "Open the received Kirin OS Guide details", u8"受け取ったKirin OS Guideの詳細を開きます" },
    { "Show or hide the Hybrid VU without changing measurement",
      u8"計測を変えずにHybrid VUを表示／非表示にします" },
    { "Set shared 0 VU. Audio, LUFS, TP and needle speed stay unchanged.",
      u8"PRE／POST共通の0 VU基準。音声、LUFS、TP、針の速さは変わりません。" },
    { "Chain unavailable; previous VU setting retained.", u8"チェーンを確認できません。VUの基準は維持します。" },
    { "VU calibration could not be saved. Previous setting retained.",
      u8"VU基準を保存できませんでした。変更前の設定を保持します。" },
    { "Clear True Peak and Clip holds", u8"トゥルーピークとClipの保持を消去" },
    { "Clear held channel True Peak and Clip indicators; keep current values and history",
      u8"チャンネルごとに保持したトゥルーピークとClipの表示を消します。現在値と履歴は残ります" },
    { "Switch between PRE and POST of this chain while the song plays. "
      "Keep this window open (pin it in Studio One / Studio Pro, turn off "
      "Target in Pro Tools); closing or replacing it returns to POST",
      u8"曲を再生したまま、このチェーンのPREとPOSTを切り替えます。"
      u8"この画面は開いたままにしてください（Studio One／Studio Proではピン留め、"
      u8"Pro ToolsではTargetをオフ）。閉じたり置き換えたりするとPOSTに戻ります" },
    { "MATCH stopped at the true-peak ceiling: PRE is still quieter than POST. Press to measure again",
      u8"MATCHはトゥルーピークの上限で止まりました。PREはまだPOSTより小さい音です。押すと測り直します" },
    { "Listen to PRE, the input of this chain. MATCH levels it to POST",
      u8"このチェーンの入力、PREを聴きます。MATCHでPOSTと音量を揃えます" },
    { "Listen to PRE, the input of this chain, at the level MATCH set",
      u8"MATCHで揃えた音量で、このチェーンの入力、PREを聴きます" },
    { "PRE waits while delay compensation is off in Pro Tools. Turn it on to hear PRE",
      u8"Pro Toolsの遅延補償がOFFの間はPREを待ちます。ONにするとPREが鳴ります" },
    { "PRE is held because the latency changed. Stop and restart playback",
      u8"遅延が変わったためPREを保留中です。再生を止めてから再開してください" },
    { "PRE is selected. POST plays until PRE is confirmed at this position",
      u8"PREを選択中です。この位置でPREを確かめられるまでPOSTが鳴ります" },
    { "Listen to POST, the output of this chain", u8"このチェーンの出力、POSTを聴きます" },
    { "Fix the last four seconds of PRE and POST and open them in PRE / POST Blind",
      u8"直前4秒のPREとPOSTを固定し、PRE／POST Blindで開きます" },
    { "Listen to POST, lowered by the attenuation you approved", u8"承認した減衰で下げたPOSTを聴きます" },
    { "Return POST to its normal level; it rises by the amount shown",
      u8"POSTを通常の音量に戻します。表示の分だけ上がります" },
    { "Match PRE to POST loudness over the latest four seconds",
      u8"直近4秒で、PREのラウドネスをPOSTに合わせます" },
    { "Press to MATCH again or to let PRE follow POST (AUTO)",
      u8"押すと、もう一度MATCHするか、PREをPOSTに追従させます（AUTO）" },
    { "AUTO: PRE follows POST loudness within 0.5 dB, up to 6 dB from your MATCH. Press to MATCH again or stop AUTO",
      u8"AUTO：PREがPOSTのラウドネスに0.5 dB以内で追従します（MATCHから最大6 dB）。押すと、もう一度MATCHするかAUTOを止めます" },
    { "Select LR, MID, or SIDE to view Delta",
      u8"差分を見るには、LR、MID、SIDEのいずれかを選んでください" },
    { "POST minus PRE; select POST to return to absolute values",
      u8"POST − PREの差分です。POSTを選ぶと絶対値に戻ります" },
    { "POST selects absolute values; delta selects POST minus PRE",
      u8"POSTで絶対値を、Δで差分（POST − PRE）を表示します" },
    { "Reference - About Kirin OS", u8"Reference ― Kirin OSについて" },
    { "Open Reference audition", u8"Referenceの試聴を開きます" },
    { "Open Kirin OS information and connection help", u8"Kirin OSの情報と接続のヘルプを開きます" },
    { "NOTE requires Kirin OS", u8"NOTEにはKirin OSが必要です" },
    { "NOTE requires an active Keep", u8"NOTEはKeep中に使えます" },
    { "Add a note at the current sample position", u8"今の位置にメモを付けます" },
    { "Finish Keep / Record before PRE / POST Blind",
      u8"PRE / POST Blindの前にKeep／Recordを終えてください" },
    { "Open PRE / POST Blind at 300%", u8"300%でPRE / POST Blindを開きます" },
    { "Open PRE / POST Blind", u8"PRE / POST Blindを開きます" },
    { "Pair and Keep menu", u8"ペアとKeepのメニュー" },

    // Footer and body status lines.
    { "FORMAT HELD / STOP KEEP", u8"形式を保持 / Keepを停止" },
    { "WAITING", u8"待機中" },
    { "BYPASSED", u8"バイパス中" },
    { "5.1 MEASURE", u8"5.1 計測のみ" },
    { "RECORD FINALIZING", u8"Recordを確定中" },
    { "RECORD RESULT", u8"Recordの結果" },
    { "WARMING %1 S", u8"準備中 %1 S" },
    { "One row per playback, from play to stop", u8"再生1回ごとに1行（再生から停止まで）" },
    { "Play audio to collect run facts in this history range",
      u8"再生すると、この履歴の範囲で再生ごとの値が集まります" },

    // The status LED.
    { "No active signal.", u8"入力がありません。" },
    { "Measurement is unavailable.", u8"計測できません。" },
    { "Keep is waiting for its pair.", u8"Keepはペアを待っています。" },
    { "Measurement is active.", u8"計測中です。" },
    { "Keep is recording.", u8"Keepで記録中です。" },
    { "A kept result is available.", u8"Keepの結果があります。" },

    // PRE / POST comparison states.
    { u8"MATCHED PAIR — MEASURING", u8"ペア一致 — 計測中" },
    { u8"PRE UPDATE DELAYED — HOLDING MATCHED Δ", u8"PRE更新待ち — 一致したΔを保持" },
    { u8"PRE UPDATE DELAYED — WAITING FOR MATCHED DATA", u8"PRE更新待ち — 一致データを待機中" },
    { u8"POST STOPPED — COMPARISON UPDATE PENDING", u8"POST停止中 — 比較更新待ち" },
    { u8"PRE IS OFF — ENABLE PRE TO COMPARE", u8"PREがオフ — PREを有効にして比較" },
    { u8"PRE INACTIVE — START PLAYBACK", u8"PRE入力なし — 再生してください" },
    { u8"CHANNEL LAYOUTS DIFFER — MATCH PRE / POST BUS", u8"チャンネル構成の不一致 — バスを揃えてください" },
    { u8"PRE LAYOUT UNKNOWN — UPDATE OR REOPEN PRE", u8"PREの構成が不明 — PREを開き直してください" },
    { u8"REFERENCE AUDITION — RETURN TO A TO COMPARE", u8"Reference試聴中 — 比較はAに戻してから" },
    { u8"VIEW CANNOT BE COMPARED — CHOOSE A SUPPORTED VIEW", u8"この表示は比較不可 — 別の表示を選んでください" },
    { u8"METRIC CANNOT BE COMPARED — CHOOSE A SUPPORTED METRIC", u8"この指標は比較不可 — 別の指標を選んでください" },
    { u8"NO MATCHING PRE — SELECT A PRE", u8"対応PREなし — PREを選んでください" },

    // LEVEL metric help. The more specific patterns come first: the first match wins.
    { "Momentary loudness (M): the loudness over 400 ms, moment by moment. Read it while playing to compare passages.",
      u8"モーメンタリーラウドネス（M）：400 msごとの音量。再生しながら場面ごとの大きさを比べるのに使います。" },
    { "Short-term loudness (S): the loudness over 3 seconds, phrase by phrase. Compare sections such as verse and chorus.",
      u8"ショートタームラウドネス（S）：3秒ごとの音量。Aメロとサビなど、場面ごとの大きさを比べます。" },
    { "Integrated loudness (I): the whole song since the last Meter Session reset. Compare it with a delivery target such as -14 LUFS.",
      u8"インテグレーテッドラウドネス（I）：リセットからの曲全体の音量。配信の基準（−14 LUFSなど）と比べます。" },
    { "Highest true peak (MAX TP) since the last Meter Session reset, inter-sample peaks included. Compare it with a ceiling such as -1 dBTP.",
      u8"最大トゥルーピーク（MAX TP）：リセットからの、サンプル間を含む最大値。−1 dBTPなどの上限と比べます。" },
    { "Loudness range (LRA): how far the loudness moves within the song since the reset. Compare PRE and POST to see how much compression narrowed it.",
      u8"ラウドネスレンジ（LRA）：曲の中での音量の幅。PREとPOSTで比べると、コンプで狭まった量が分かります。" },
    { "Average dynamics of the song (PLR): the highest true peak minus integrated loudness. Compare PRE and POST to see how much limiting reduced it.",
      u8"曲の平均的なダイナミクス（PLR）：最大TP − LUFS-I。PREとPOSTで比べるとリミッターで詰めた量が分かります。" },
    { "True peak (TP) in the current window, inter-sample peaks included. Watch it against a ceiling such as -1 dBTP.",
      u8"トゥルーピーク（TP）：今の計測窓の、サンプル間を含むピーク。−1 dBTPなどの上限と比べて見ます。" },
    { "Crest factor (CREST): peak minus RMS in this window, the dynamics of the moment. Compare PRE and POST to see how much transients were reduced.",
      u8"波高率（CREST）：ピーク − RMS（瞬間のダイナミクス）。PREとPOSTで比べるとアタックの減り方が分かります。" },
    { "Peak to short-term loudness (PSR) in the current window: the dynamics heard now. Compare PRE and POST to see how much they were reduced.",
      u8"ピーク − ショートターム（PSR）：今の窓の、聴こえるダイナミクス。PREとPOSTで比べると詰めた量が分かります。" },
    // TIME HISTORY's own helps (the legend and the PSR row share LEVEL's above).
    { "PSR over time: the 400 ms peak minus the 3 s loudness. It falls where the song is held down; compare verse and chorus.",
      u8"PSRの推移：400 msのピーク − 3秒の音量。曲を詰めた所で下がるので、Aメロとサビなどを比べます。" },
    { "PSR over time: below 0 where the chain reduced the dynamics at that moment of the song.",
      u8"PSRの推移：0より下は、曲のその時点でチェインがダイナミクスを減らした所です。" },
    { "PSR is PEAK - S of one 100 ms point: the 400 ms sample peak and the 3 s loudness. Read which one moved.",
      u8"PSRは同じ100 msのPEAK − S（400 msのサンプルピークと3秒の音量）。どちらが動いたかを読みます。" },
    { "Correlation (CORR) over 3 s: +1 alike, 0 unrelated, below 0 more SIDE than MID. A lilac tick marks each fall below 0; check it in mono.",
      u8"相関（CORR、3秒）：+1は左右が同じ、0は無関係、0未満はSIDEが多い。0未満は薄紫の印。モノラルで確かめます。" },
    { "Correlation (CORR) over 3 s: +1 alike, 0 unrelated, below 0 more SIDE than MID.",
      u8"相関（CORR、3秒）：+1は左右が同じ、0は無関係、0未満はSIDEが多い。" },
    { "POST. %1 Showing its maximum since the last Meter Session reset.",
      u8"POSTの%1最後にMeter Sessionをリセットしてからの最大値を表示しています。" },
    { "PRE. %1 Showing its maximum since the last Meter Session reset.",
      u8"PREの%1最後にMeter Sessionをリセットしてからの最大値を表示しています。" },
    { "POST minus PRE. %1", u8"POST − PREの%1" },
    { "POST. %1", u8"POSTの%1" },
    { "PRE. %1", u8"PREの%1" },
    { "Click to hold; measurement continues. HOST ~ is the window endpoint on the host "
      "project/render clock, not guaranteed project time or the exact peak. LIVE resumes "
      "scrolling.",
      u8"クリックすると表示を止めます。計測は続きます。HOST ~は計測窓の終わりをホストの"
      u8"プロジェクト／レンダーの時計で示したもので、プロジェクト上の時刻やピークの正確な位置"
      u8"ではありません。LIVEで表示を再開します。" },
    { "Hold the previous TP > -1 dBTP event; measurement continues",
      u8"−1 dBTPを超えた一つ前のTPで表示を止めます。計測は続きます" },
    { "Hold the next TP > -1 dBTP event; measurement continues",
      u8"−1 dBTPを超えた一つ後のTPで表示を止めます。計測は続きます" },
    { "Resume scrolling history; measurement and session maxima are unchanged",
      u8"履歴の表示を再開します。計測値とセッションの最大値は変わりません" },
    { "Copy TP window endpoint on the host clock (project or render clock, not guaranteed "
      "project time)",
      u8"TPの計測窓の終わりを、ホストの時計の値でコピーします（プロジェクトかレンダーの時計です。"
      u8"プロジェクト上の時刻は保証しません）" },

    // TIME pages.
    { "TIME detail", u8"TIMEの詳細" },
    { "Cycle History, Run facts, Drum Attack, Sharpness Delta, and POST live facts",
      u8"HISTORY、RUN、DRUM、SHARP、LIVEを順に切り替えます" },
    { "Cycle History, Run facts, Sharpness Delta, and POST live facts",
      u8"HISTORY、RUN、SHARP、LIVEを順に切り替えます" },
    { "Session history. Direct Observatory view.", u8"セッションの履歴を表示します。" },
    { "Absolute facts grouped by playback run within the selected history; not a transport "
      "control.",
      u8"選んだ履歴の範囲の絶対値を、再生1回ごとにまとめて表示します。再生の操作ではありません。" },
    { "Drum transient event facts. Not a 2MIX onset detector.",
      u8"ドラムの打音ごとの計測値です。2MIXの立ち上がり検出ではありません。" },
    { "Sharpness Delta history. Unit: acum.", u8"シャープネスの差分の履歴です。単位：acum。" },
    { "Absolute POST facts on fixed scales.", u8"POSTの絶対値を固定の目盛りで表示します。" },
    { "Switch TIME detail view", u8"TIMEの表示を切り替えます" },
    { "Measured facts grouped by playback run", u8"再生1回ごとにまとめた計測値" },
    { "%1. Click to switch view.", u8"%1。クリックで表示を切り替えます。" },
    { "Drum Attack", u8"ドラムのアタック" },
    { "Sharpness Delta", u8"シャープネスの差分" },
    { "POST live facts", u8"POSTのリアルタイム値" },
    { "PRE input measurements", u8"PREの入力の計測値" },
    { "Local measurements; PRE subtraction does not apply",
      u8"このインスタンスの計測値です。PREとの差分はありません" },
    { "SHARP: exact POST - PRE sharpness. A paired PRE is required",
      u8"SHARP：同じ時刻のPOSTとPREのシャープネスの差分です。ペアのPREが必要です" },
    { "LIVE: local POST absolute values, fixed scales, 100 ms observations",
      u8"LIVE：POSTの絶対値を、固定の目盛りで100 msごとに表示します" },
    { "RUN: absolute facts grouped by playback run within the selected history",
      u8"RUN：選んだ履歴の範囲の絶対値を、再生1回ごとにまとめて表示します" },
    { "ATTACK: automatic paired comparison or POST-only events",
      u8"ATTACK：ペアでの自動比較、またはPOSTだけの打音です" },

    // Information button.
    { "Hypha information", u8"Hyphaの情報" },
    { "Loaded version, update information, downloads, and hover help",
      u8"読み込んだ版、更新情報、ダウンロード、ホバーヘルプ" },
    { "Hypha information and downloads", u8"Hyphaの情報とダウンロード" },
};
}

Section observatorySection() noexcept
{
    return { "observatory", entries, sizeof (entries) / sizeof (entries[0]) };
}
}
