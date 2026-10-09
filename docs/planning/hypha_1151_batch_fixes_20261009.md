# Hypha 1.1.51 — G3観測に対する一括修正

G2後の実host観測に対する責務と再受入を固定する。G3、本人の日常操作・聴取・品位、両OS正式候補、公開受入は未完了。以前の候補の署名・CI、正常fixtureを修正版の合格へ読み替えない。私的fixture・操作履歴・接続情報は公開sourceへ含めない。

## 対象と再受入

|対象|修正の責務|必要な確認|
|---|---|---|
|A01 負荷|非表示editorのsnapshot取得とdemand、DRUM bounded bufferの再利用・LOCK中の不要summary取得、Reference変換内の512 exact phase上限の係数再利用。地形画像の転置はRetina描画を遅くしたため採用しない|同host/素材/区間/buffer/DPIのvisible/hidden/bypass。CPU平均/最大、paint分布、deadline/xrun、memory、操作応答を別々に測る|
|A02 復帰Δ|同runで実際に処理したframesから双方の測定窓を確認。LEVELもTIMEのcached qualificationを使う|無加工pairのscalar/current/history、seek/loop/bypass/reset/再起動/未知latency/片側不在。POST絶対値とgapを保持|
|A03 窓外LOCK|終端Singleの元proof・値・形・eventを1件に保持。LIVE supportとsource退役を区別|20秒超の連続再生、停止再開、帯域/候補/Facts/LIVE、source/clock/request変更、BUSY|
|A04 旧Cue|保存選択不在のsingletonを既存selectorで明示的に選び直す。同IDでも未成立sourceを再取得し、正常同IDは待機試聴を保持|旧Cue→現Cue、同ID revision、file変更/不在、reopen、検証失敗。Aの安全性と自動試聴なし|
|A05 案内|CはC、VはVで実在する既存操作を案内|C/V/B、未準備/Cue不在/source変更、英日、復旧先の存在|
|A06 LRA文字|state文字を自身のvalue boundsへfit/clip|全桁、待機/有効/停止/欠測、全size、両OS DPI、英日、PRE/POST|
|A07 Facts桁|同じdoubleへ往復する最短decimalの共通formatter|小数/整数/極小/大値/interval/非有限/欠測、単位、英日|
|A08 Facts入口|既存action fontと既存クリック枠を使用|全文とpointer、全size/DPI、ALL/BAND、英日、値と図の非干渉|
|A09 Keep結果|完了状態は最下部。保持した結果factはLEVEL、TIME/FREQ/SPACEは選択面を維持|開始/停止/保存成功/失敗、長文/連続操作、tab/VU基準/右button/LRの非干渉|
|A10 TRACE欠落|主時計/presentation/source/既知latencyが連続する補助時計cutをRecord音声継続として扱い、比較epochは切る|producer/Measure受理/expected slot/drain/spool、正式WAV/Keep/plugin_data/work.json、真の欠落/seek/force cutの拒否|
|A11 VU基準文字|既存legend枠で全文とpointerを保持|5基準、英日、mono/stereo、PRE/POST、全size/DPI、共有/保存/reopen/破損/保存失敗/scope競合|

## 時計・保存の境界

M/TP/LEVEL CRESTは400 ms、S/PSR/CORRは3秒を双方の同じrunで処理してからcurrent Δを作る。LEVEL/TIMEはoptional presentation latencyが報告されないhostでも同じ実frame窓を確認してΔを出す。この変更をFREQのexact join、live比較、Recordの既知latency条件へ適用しない。部分窓と完全窓を比較せず、arrival順・wall clock・再pollをproofにしない。TIMEの6値ABIは維持し、LEVEL CRESTは同時刻の内部publicationに付加する。旧publicationに追加証拠がない場合はfail closed。絶対POSTとSessionの累積統計をresetして辻褄を合わせない。累積PLRの等価prefixを証明できないΔは欠測を保持する。

終端Singleの元proofは値・形と共に1件だけ保持する。supportの移動で破壊せず、source/owner/pair authority/request/mapping epochの変更は退役させる。LIVE、別候補、帯域変更は既存所有関係に従う。BUSYを退役と扱わない。

Recordの補助時計cut例外はproducerで主時計と既知latencyの連続性を確認した場合だけ。serialized Record model、canonical Stop、HMAC/schema、sample範囲、比較epoch、実欠落の表現は維持する。seek/force cut/source変更/未知latencyへ適用しない。補間やcomplete偽装をしない。

補助時計cutが繰り返されてpair ringが周回しても、連続する主時計と同一latencyのRecord範囲を現在・直前の選択takeの2枠に保持する。Audio Threadの固定容量atomic証拠だけを延長し、pairの過去epochは復活させない。真のcut以後にprefixを延長・復活しない。異なるlatency間の対応は既存の隣接epoch検証へ渡す。

確定出力では、最初のepochとの一致だけでなく、producerが保持したRecord prefixのgeneration、連続epoch範囲、source、既知latency、raw/presentation対応と端点を確認した観測だけを採用する。元の比較epochとraw観測は書き換えない。実PRE/POST Keepの12秒・512 samples/callback・補助時計の反復cutを使い、両roleの120 slot、100 ms間隔、missing 0を実JSONで確認する。WAV未結合はexpected_wav_ready=falseで保持する。既存契約どおり、producerがsample数と全TRACEを証明したrender-clock takeはsample_count_ready/completeを独立して判定し、証拠がないtakeのfallbackを維持する。

## 表示・性能の境界

FREQは6秒の奥への流れ、全観測内容、12 Hz曲線/2 Hz数値を維持する。地形scratchは既存bounded storeを使い、blend/coverageの順を変えない。非表示editorの表示取得を止めても通常計測、Record、Session、IOは独立して進む。AUTOは非表示になった最初のtimerで停止し、承認済みPRE gainとPOST減衰を保持する。停止通知は再表示後にfooterへ出し、AUTOを自動再開しない。先に出たKeep／Capture等の操作通知は上書きせず、表示が終わってから停止通知を出す。Keep完了とCaptureのWork添付結果のeditor通知も非表示中は消費せず、再表示時まで遅れる。Audio Threadへ仕事を移さない。

Reference変換の係数cacheは1つの非RT変換jobの内部だけに保持し、同じ小数位置のbit精度を保つ。512種類を超えたら未登録係数は従来の式で計算する。rate/source間で共有せず、PCM、tap順、境界、gainを変えない。44.1/48/96 kHz source、5出力rate、mono/stereo、先頭/中間/末尾、上限後を独立した旧式oracleとbit比較する。

DRUM主面は値と図、全体値がなければ「—」。部分値・分類はFacts。PSRは補助情報で、比較失効を黙ってPOSTへ切り替えない。VUの300 ms応答・測定・音声を保持する。通知はfooter、長文だけ既存小font。Captureはv1の同じimmutable PNGを保つ。

## 検証・再ビルド・残件

対象Rust/native試験で正常・競合・欠損・再起動・境界を確認し、workspace test、clippy、実測inventoryの全ignored parity/pairing、release-source gateを通す。新しいlive製品試験を足す場合は正規inventoryとrelease-source regex/countを同時に更新する。retry条件は広げない。

Keep表示のshipping editor試験はFREQ/TIME各面/SPACE/LEVELの6面とFinalizing/ResultHold/Unavailableを確認する。既存material cacheの初回direct paintと再利用rasterの境界を温め、同じ描画の再現を先に確認してから許容差0で比較する。LEVEL以外ではfooter外に変化がなく、保持結果の数値を変えても本文とCapture v1を置き換えない。LEVELの結果数値は本文とCaptureへ反映される。旧来の全domainへ結果を重ねる期待値は受入根拠に使わない。

Windowsの全owned native targetは製品targetと同じUTF-8指定を継承する。CP932で日本語commentが行継続として解釈される試験targetのコンパイル失敗を、製品sourceの括弧変更や試験除外で隠さない。

shipping editorのchain footer preference試験はshared preferenceの保存先を、editor生成前に使い捨てfixtureへ固定する。WindowsのJUCE既知フォルダ取得はAPPDATAの環境変数置換では隔離されないため、利用者の保存値を既定値のoracleにせず、ON/OFF試験を実設定へ書かない。製品の保存場所・既定値・共有規則は変更しない。

採用commitとJUCE実bytesを固定し、正規入口でMac Universal 6本とWindows x64 4本を作る。診断AAX、通常Pro Tools用PACE署名、formal installer、exact CIを区別する。署名・公証・配置を行った場合はLS用PKGとApple verification dry-runまで準備する。公開は個別candidateの承認後だけ。

実hostの11現象の再確認、A01の事前固定した性能上限/host範囲、A10の実clockと全slot出力、AU PRE authority、Reference V停止時のOS publicationとHypha退役理由、VST3 offline bit identityは別の証拠が必要。process単体0.1%原則をDAW process CPUへ置き換えない。

ARM実DAW、Windows AAX/PDC、全Reference/Blind/Exact/Capture/Record往復、長時間/低buffer/複数instance、本人の聴取・操作・品位は担当・機器・fixture・再開条件を私的matrixへ記録する。CI確認・merge・releaseはClaude担当。別sessionへ無断送信しない。友人確認は公開後G4のまま。

## B-1355レビューの修正と確認範囲

- LEVELのcached qualificationはbusyと実データ欠測を分ける。六つの取得mutex／authority競合では新frameを返さず、呼出し元の整合したHOLDING表示を保つ。停止時はIO側の保持publicationを待ち、MEASURINGで上書きしない。元pointの期限は延長しない。
- 再生中の通常の次の10 ms未満のchunkはcompleteとして処理済みprefixを表示する。processed／pendingの実framesは保持し、停止後の未処理尾や丸ごと未処理chunkはpendingのまま。PLR／MAX TPとCaptureを再生中・停止後の両方で確認する。
- Capture v1でtyped metadataを要求するTIME面はHISTORY/PSRとDRUMだけ。RUN／SHARP／LIVEはSessionのpending状態から添付拒否を引き継がない。LEVELの真の未処理尾は従来の拒否を維持し、同じ凍結PNGを保つ。
- Factsのfinishは意味のある英日文言を使い、enum値を表示しない。全体値のないlaneにWhole件数を付けず、分類と確定部分の内訳は保持する。
- READMEの機能説明と画像captionは利用者の操作・計測の言葉で記述する。release担当が扱うcurrent v1.1.50とAAX受入の記述は変更しない。

レビューの記録のみ（今回の修正外）：VU基準はローカル設定fileに残り、別PCで設定が無ければ通知なく既定−18になる。Session I／LRAのexact cacheは65,536 distinct nodesが上限で、約55分相当以後にはMeasure threadのcanonical全体再計算へ戻り得る。spectrum ingressの実装はatomicのみだが、xtaskのRT guard対象には含まれていない。これらを解消済みとは報告しない。

## B-1356レビューの停止表示とFacts

停止時のLEVEL／observatory frameはAudioの非ActiveとMeasureのPausedから公開し、IOの停止publicationを待たない。IO比較がActiveのまま／取得mutexがbusyでも絶対値・実framesは停止中として保持し、未確認Δには既存statusでLOCAL_INACTIVE（停止中・比較更新待ち）を表示する。再生中のbusyは出力不変、IOの正常HOLDINGは既存理由を保持する。Audio／Record／serialized出力は変更せず、instance directoryをfileで塞いだ失敗→復旧、IO不在・busy・両停止authority、正常再開を試験する。

FactsのAudio endedはfinish／reasonとも「入力終了」。同じ意味のfinishとreasonが同じ行に重なるときは1回だけ描く。異なる理由や内訳は保持する。

記録のみ（修正外）：Session V2 pollがbusyの後に新しいobservatory frameが届くと、coverageがそのframeへ追いつくまで未確認prefixとしてPLRが一度欠測になる場合がある。正常の10ms端数とcoverage取得競合の区別は別の未処理項目とし、今回解消済みとは報告しない。

## 足元通知の寿命（2026-10-10利用者指摘）

PRE選択／解除とstate復元で安全性のために保持するLISTEN退役原因を、動作中の比較状態へ投影していたため、LISTEN未使用でも案内が常駐した。現在の常設remedyはactive session・content hold・RT return待ち・保持減衰に限定する。実LISTENの終端遷移は一度の3秒通知とし、接続／復元のみでは通知しない。MENUと全文storyも同じ現在状態を使う。RT revocation、初回原因の保持、再選択guard、承認gain、A経路は変えない。

idle Keep完了はpersistent Record errorへの書込みを止め、既存pair releaseの一度だけ消費するslotへ渡す。出荷FFIではengineのbounded action noticeを共有し、IO worker再起動でslotを作り直さない。旧Rust呼出引数とC ABI形、Record終端・writer・serialized outputは維持する。PRE競合で実際に選択が解除された理由も同じbounded railで通知する。

再受入は、実PRE探索／選択／解除、LISTEN未使用、state restore、停止再開、hidden／visible、editor再open、実LISTEN中断／明示END／再選択、全size・domain・日英・VUと、一度のCapture／Keep結果通知の消去後の再出現を対象とする。現在のWAIT・content hold・RT return・実際／targetの保持減衰は過去理由全種と組み合わせて検証する。実paired Keepの5秒試験閾値auto-stopを通常workspace suiteで動かし、通知1回・persistent error無し・Watch／再openで復活無しを確認する。10分defaultは変更しない。fixture結果は実REAPER等のloaded module受入を代用しない。
