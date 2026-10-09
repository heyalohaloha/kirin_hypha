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

M/TP/LEVEL CRESTは400 ms、S/PSR/CORRは3秒を双方の同じrunで処理し、既知latencyの証拠を持つ場合だけcurrent Δを作る。部分窓と完全窓を比較せず、arrival順・wall clock・再pollをproofにしない。TIMEの6値ABIは維持し、LEVEL CRESTは同時刻の内部publicationに付加する。旧publicationに追加証拠がない場合はfail closed。絶対POSTとSessionの累積統計をresetして辻褄を合わせない。累積PLRの等価prefixを証明できないΔは欠測を保持する。

終端Singleの元proofは値・形と共に1件だけ保持する。supportの移動で破壊せず、source/owner/pair authority/request/mapping epochの変更は退役させる。LIVE、別候補、帯域変更は既存所有関係に従う。BUSYを退役と扱わない。

Recordの補助時計cut例外はproducerで主時計と既知latencyの連続性を確認した場合だけ。serialized Record model、canonical Stop、HMAC/schema、sample範囲、比較epoch、実欠落の表現は維持する。seek/force cut/source変更/未知latencyへ適用しない。補間やcomplete偽装をしない。

## 表示・性能の境界

FREQは6秒の奥への流れ、全観測内容、12 Hz曲線/2 Hz数値を維持する。地形scratchは既存bounded storeを使い、blend/coverageの順を変えない。非表示editorの表示取得を止めても通常計測、Record、Session、IOは独立して進む。Audio Threadへ仕事を移さない。

Reference変換の係数cacheは1つの非RT変換jobの内部だけに保持し、同じ小数位置のbit精度を保つ。512種類を超えたら未登録係数は従来の式で計算する。rate/source間で共有せず、PCM、tap順、境界、gainを変えない。44.1/48/96 kHz source、5出力rate、mono/stereo、先頭/中間/末尾、上限後を独立した旧式oracleとbit比較する。

DRUM主面は値と図、全体値がなければ「—」。部分値・分類はFacts。PSRは補助情報で、比較失効を黙ってPOSTへ切り替えない。VUの300 ms応答・測定・音声を保持する。通知はfooter、長文だけ既存小font。Captureはv1の同じimmutable PNGを保つ。

## 検証・再ビルド・残件

対象Rust/native試験で正常・競合・欠損・再起動・境界を確認し、workspace test、clippy、実測inventoryの全ignored parity/pairing、release-source gateを通す。新しいlive製品試験を足す場合は正規inventoryとrelease-source regex/countを同時に更新する。retry条件は広げない。

採用commitとJUCE実bytesを固定し、正規入口でMac Universal 6本とWindows x64 4本を作る。診断AAX、通常Pro Tools用PACE署名、formal installer、exact CIを区別する。署名・公証・配置を行った場合はLS用PKGとApple verification dry-runまで準備する。公開は個別candidateの承認後だけ。

実hostの11現象の再確認、A01の事前固定した性能上限/host範囲、A10の実clockと全slot出力、AU PRE authority、Reference V停止時のOS publicationとHypha退役理由、VST3 offline bit identityは別の証拠が必要。process単体0.1%原則をDAW process CPUへ置き換えない。

ARM実DAW、Windows AAX/PDC、全Reference/Blind/Exact/Capture/Record往復、長時間/低buffer/複数instance、本人の聴取・操作・品位は担当・機器・fixture・再開条件を私的matrixへ記録する。CI確認・merge・releaseはClaude担当。別sessionへ無断送信しない。友人確認は公開後G4のまま。
