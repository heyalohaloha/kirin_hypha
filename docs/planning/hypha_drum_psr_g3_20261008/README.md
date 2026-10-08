# DRUM・PSR G3 — 実DAWと最終候補の記録

2026年10月8〜9日。利用者が委任した実DAW操作で、G2の画面、最大DPIのFREQ、既存の最下部通知を確認した。製品候補はB-1337、`3c4c999ed880ba4cb6b87da1fdbbab89703868b2`。CI確認・merge・正式releaseはClaude担当。公開前にはG3の技術・実DAWと利用者本人の日常操作・品位の受入が必要で、友人の確認は公開後G4だけである。

この記録は診断候補の実測であり、G3全体のPASSではない。性能の受入は未達である。再有効化後のTIME比較と途中の非ゼロΔは未解決で、正式署名候補の全host受入・本人確認も残る。結果とraw証跡のportableな参照は[検証receipt](validation-receipt.json)に分ける。

## 修正した範囲

B-1334はFREQ地形spanの完全被覆画素に使うblend weightを一回の計算へ移し、描画順・fractional coverageを保持した。修正前sourceを別namespaceでcompileした300条件の画素oracleは最大channel差0。900／DPI 2の地形とcanvas clearの120回CPUはsoftware 10.725→9.587 ms、native 14.643→13.595 msだった。FREQ全体やDAW全体の削減率へ読み替えない。画素不適合または実測CPU増加のあるcache案は採用しなかった。CompactのPOST見出しは描画paddingを含む必要幅だけを確保した。

B-1336は正常pairのPSR差分待ち、POSTの絶対FREQ欠損、VU通知overlayを修正した。選択済みPREのlocator・所有権・世代・対応時刻を照合し、PRE／POSTの別保存先を誤って不一致としない。hostが出力latencyを知らせない場合はPOST絶対観測だけを独立した時計で維持し、PRE差分の時計を推定しない。通知は既存の最下部へ収め、全文は既存の詳細とtooltipに残す。

B-1337はALL／BANDの主画面から繰り返す「最新の一打」、確定・不成立・不明・経過時間を取り除いた。一部の打音だけの集計は主画面「—」、部分値・内訳・計算済みの年齢は既存Factsへ残す。全体の値、意味のある区間、選択した一打の値とグラフは保持する。未設定のALLの窓、0 msのresolution、未計算の年齢を事実として出さない。使い方は既存Facts・READMEで説明し、測定定義、音声、針の速度、graphのgeometryを変えない。

左下のLIVE／HOLD／WAITING／CHAIN LOADはB-1292の調整どおり、右のボタンと同じ`action` fontである。全5サイズの高さは11／12／13／15／18 px、最大22 pxへは戻していない。2026-10-09に利用者が「長い通知だけ小さくする」を選んだため、同じ通知boundsへaction fontで収まらない長文だけ既存legend font（最大16 px、最小11 px）へ切り替える。短い通知、LIVE／HOLD／CHAIN LOAD、右ボタン、通知位置と図の面積は保持する。B-1337の実DAW結果はこの追加選択より前の候補である。

## ローカル試験

| 範囲 | 結果と証跡の境界 |
| --- | --- |
| 最終DRUMの選択・全体／部分・単打・欠損・区間・英日・全サイズ | 最新catalogをcompileしたATTACK／product-entry／typographyの3試験PASS。UI描画契約も親componentの可視fixtureを正した上でPASS |
| 長い通知だけのfont変更 | 変更後sourceを固定してeditor-surface／UI-render／product-entry／typographyの4試験PASS。通常通知と右ボタンのaction font、既存boundsを維持 |
| 正式DRUM描画速度 | B-1337で既存全条件を一回実行、18.438 s、再試行0、source・binary前後一致、PASS。legacy changing120／resize6／BAND4／V2 changing120と機能試験を完走 |
| 先行速度失敗 | legacy resize中央値16.2694 msのFAILは別logで保持。後続PASSで消去せず、閾値・回数・fixtureを変更していない |
| Rust workspace | 初回2,384 PASS／1 FAIL／41 ignored。唯一のFAILは抽出先に追従していないxtask source検査。修正後全xtask176件PASS。製品crate619 filesのSHAを照合して再利用し、初回実行をgreenとは呼ばない |
| Rust残gate | workspace docと全target Clippy PASS。必須ignoredは一覧を実測したparity20／pairing6、全26件PASS |
| 対象回帰・source契約 | 独立保存先のpair、FREQの不明latency／競合／退役／再取得・期限、公開文面・画面文面・500行規約・format・diffはPASS |

正式速度runの条件別最大中央値はlegacy changing15.6776 ms、resize14.0028 ms、BAND5.00528 ms、V2 changing10.8753 ms。V2の最大16.0783 ms／cold19.2227 ms、resize最大36.3753 msは既存閾値内だった。native fixtureを実DAWや利用者の主観評価へ繰り上げない。

## 最大DPIのFREQ

B-1337、900×600 logical／DPI 2、6 mode×cold1／warm60／changing60／identical2000の24行を一回実行した。source SHAは前後一致した。

| 強制FREQ絶対表示 | 中央値 | p99 |
| --- | --- | --- |
| warm paint | 26.852 ms | 33.636 ms |
| 値変更＋paint | 30.842 ms | 68.995 ms |

この描画costは残る。強制描画を出荷時の12 Hz曲線／2 Hz数値や実DAW CPUへ読み替えず、「絶対に重くならない」とは断定しない。実測CSVのSHAは`1ac2d21bb20709464bf723b5bbe38d5f14079389b5c568d381a357d052943cce`。

## 実DAWの結果

MacはREAPER 7.71／OSX64と独立した設定・custom VST path、WindowsはStudio Pro 8.1.2.113407と使い捨てprojectで確認した。候補moduleのSHAを両側で照合した。音源は合成WAV、stereo 48 kHz、録音待機なし、master出力はpluginの後で0。通常のproject、既存signed AAX、Merging経路を変更していない。

Windowsの2026年10月8日21:50〜22:03には他sessionの再生停止・最小化・Escape・clickと開発版Kirin OSの並行動作が報告された。重なる操作・負荷は最終受入から除外してrawを保持する。下表のCPUはその後の新しい測定である。

| 面 | 実読した結果 | 制限 |
| --- | --- | --- |
| TIME／PSR | Macの正常pairでPSR Δ+0.0、Windowsのfresh正常pairでM／S／TP／CORR Δ+0.0、PSR Δ+0.0と実plot | 途中の非ゼロΔや再有効化後の欠損を相殺しない |
| FREQ | 両hostのPOST絶対曲線と6秒地形が動いた。WindowsではPRE公開のrequest identity一致と世代・endpointの進行も実読 | PRE editorのFREQ受入、未知latencyでのPRE差分を認定しない |
| DRUM ALL | Windowsの日英・LIVE／LOCKでALL selectorとTRANSIENT／STRENGTH／CREST／SHARPNESSを確認、0差分の単打値と形を表示 | 一度ALLにBANDの行名・単位を当てた報告は訂正。画像上のselectorとmetric名で再分類した |
| DRUM BAND部分値 | 1 kHz・8打でDELAY／ATT／RELは主画面「—」、LEVELは全8打で+0.0 dB。Factsは前3項目の部分中央値0・exact7/8と理由を保持 | mainを部分中央値で埋めていない。検出精度の全素材受入ではない |
| VU | 両hostで通知と既存`0 VU = −18 dBFS`が別領域に収まった。Macでクリック後の5基準menuを確認 | 実DAWの既存基準は変更していない。針の5基準換算と保存・失敗・復元はnative fixtureで区別する |

各CPU条件を一回、約20秒測定した。100%は一つのCPU coreであり、全machine使用率やHyphaのAudio Thread単体ではない。値はDAWとPRE／POSTの処理を含む同じhost process全体である。

| 条件 | Mac REAPER | Windows Studio Pro |
| --- | --- | --- |
| 最大logical editor・FREQ表示 | 77.68% | 73.08% |
| editor非表示・両processor有効 | 42.93% | 29.76% |
| editor非表示・両host insert無効 | 17.17% | 23.98% |

Macの3測定はREAPERが示すaudio／media xrunが各開始・終了とも0だった。Windowsは全20 sampleでown hostのforeground、再生緑表示、再生位置の進行を確認したが、hard RT／dropoutのassertionは取得していない。MacはWindowsと同じ連続foreground guardを持たない。描画を閉じても計測costは残り、20秒の結果から全素材・全hostの無負荷を保証しない。Macの有効／無効差は非表示25.76、FREQ追加34.75、合計60.51 percentage pointsである。この負荷を「音切れなし」の理由で合格にはしない。既存の強制FREQ絶対fixtureを3秒・1 ms間隔で一回profileし、高解像度画像の合成、地形、曲線の塗りを頻出stackとして確認した。profile中の時間は受入性能値へ流用せず、非表示時のworker別costの根拠にも使わない。

Macはown hostを通常終了し、preference SHA・実読したbuilt-in device設定と全observed deviceが開始前と一致、機器routing writeは0。Windowsも通常終了し、2本のplugin treeと4 scopeのpreferenceを全SHA照合して復元、DAW／own task残存0。backupは保持した。

## 解消を認定していない実動条件

Windowsで両host insertを20秒無効→有効、PRE TIME→POST HISTORY Δ、Stop／Play後5.5秒の時点に、実Play緑・Pair青でも`PRE observation unavailable`と全値「—」が出た。正常close／fresh再open後は0差分へ戻ったが、再openを復帰gateのPASSにしない。

追加の一回だけのRound 4でも、fresh正常pairの全0差分→両insert無効20秒→有効→Stop／Playで同じ欠損を再現した。PREの公開は120 ms以内に更新され、32点・各点間隔4800 samples、spanのepoch／incarnation／generation／tokenは同じだった。endpointの4800剰余は初回4560、再有効化4608、Stop／Play後384で、observedの剰余は0だった。これはPREのcadence phase変化の事実であり、取得できていないPOST TIME packet／spanを補って原因を確定する証拠ではない。実機は通常終了し、plugin treeとpreferenceのSHAを再照合して復元した。

同じhostの繰り返し再生・言語変更後には、HISTORY ΔでM+2.6／TP+2.6／PSR+2.5、別frameでM−3.5／TP−5.3／PSR−5.6を観測した。無加工pairの期待0と不一致で、PSRだけの問題とは呼ばない。Round 4ではこれらの非ゼロframeは再現しなかった。該当時刻のTIME packet／両spanを保存していないため、時計・generation・解析欠落のどれが原因かは確定していない。健全なFREQ時のPRE publicationを失効時の証拠へ流用しない。

これらの切り分け・影響gateと、最大FREQの実負荷の評価はG3の残件である。正常結果・native機能／速度PASSで埋めず、原因を確かめる前に測定定義や値の補間を変更しない。

## ビルドと公開の境界

統合入口の未署名all-format buildはB-1337でMac PRE／POST×AU／VST3／AAX Universalの6本、Windows PRE／POST×VST3／AAX x64の4本を生成・検証した。Macは712.511 s、Windowsは471.368 s、各一回のbuildである。Macのverify-onlyもPASS。両OSの実module確認はVST3であり、AAXは配置・retail Pro Tools受入を行っていない。

後続の利用者が選んだ長い通知だけのfont変更と文書・receipt更新を、B-1337の診断manifestや実DAW結果、旧CIのexact commitへ読み替えない。font変更後のnative試験とall-format buildは各候補のsourceを照合して別に扱う。旧CI run `37759344861`の4必須jobと補助run `37759344860`はB-1333での成功で、最終pushの受入は別にClaudeが判定する。

未署名診断buildは通常Pro Tools用PACE署名済みAAX、署名・公証、installer lifecycle、exact候補CI、A4の全host、本人の日常操作・品位の承認を代用しない。正式公開はMac LS PKG・HP配布・Windows署名EXEの3チャネルが同版で揃って完了する。

raw command・log・機器情報・設定backupはignored領域へ保存した。公開receiptは件数・数値・basenameとSHAに絞り、個人PCのpathやoperational identityを含めない。

2026-10-09の利用者方針: FREQの6秒履歴が奥へ流れる表現を保持する。計測プラグインとしての軽さを優先し、測定値・履歴長・表示cadenceを削らず、重複計算と再描画を対象に性能改善を進める。負荷が残る状態を完了とは呼ばない。
