# 連続PRE/POST比較 契約改定案（G1Rの決定を正本へ落とす下書き）

作成日: 2026-09-28。
状態: 下書き。AGENTS、不変条件表、READMEの正本はまだ変更しない。
[実装計画](hypha_live_chain_compare_implementation_plan_20260927.md)（第13版）の方針どおり、正本は実装の承認時に改める。
本書は、そのときに入れる差分を先に固定し、承認の判断材料にする。

## 1. 入れる決定

| 決定 | 日付 | 根拠 |
| --- | --- | --- |
| 対応の鍵は連続時計（VST3連続時刻、AU render時刻）と、定常区間で較正した差K | 2026-09-28 | [G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第2節 |
| PREの周回（run）を照合に含める | 2026-09-28（第10版） | 同第6節、第7節 |
| 各側の呼出しの空白を時計の不連続として扱う | 2026-09-28（G1R） | 同第7節、第8節 |
| 静かな区間はinfinite tailの報告でplugin sleepを避ける（副作用の確認後） | 2026-09-28（G1R） | 同第8.4節 |
| 遅延変更の直後の2〜4 block（最大171 ms）の誤対応を許容し、説明書に記す。AUはM1 | 2026-09-28（G1R） | 同第8.3節 |
| 境界規則C（証明できない間はPOST、証明が戻り次第PREへ自動で戻る） | 2026-09-28（G1R） | 同第7節、第8節 |

## 2. AGENTS.md

### 2.1 R-12 製造境界

現行（抜粋）:

> 利用者の明示操作によるReferenceの比較試聴は、この禁止対象に含めない。登録済みの不変なReferenceを試聴用B経路で再生し、試聴コピーにだけ一時的なGain Matchを適用できる。Referenceファイル、A経路、正本のPRE/POST測定・Recordは変更せず、接続、読込、復元だけでBへ自動切替しない。offline render、Reference欠損、検証失敗時はA経路を維持する。

案: 上の段落の後に、次の段落を加える。

> 利用者の明示操作によるlive PRE/POST比較試聴も、この禁止対象に含めない。同じchainのPREが事前確保の共有memoryへ公開した直前の入力を、POSTは対応を証明できた範囲だけ試聴用B経路で出力し、試聴コピーにだけ承認済みのgainを適用できる。対応は連続時計、定常区間で較正した差K、PREの周回、各側の呼出しの空白で証明し、時計の一致や到着だけでは受け入れない。証明できない区間はPOSTを出力し、PREの選択と承認済みのgainは保持して、証明が戻った最初のblockからPREへ戻す。PREの入力、A経路、正本のPRE/POST測定・Recordは変更せず、接続、読込、復元だけでBへ自動切替しない。offline render、検証失敗、説明できない欠落、pair／format失効ではPOSTを維持して比較を中断し、再選択まで戻さない。遅延が変わる設定変更の直後の短い区間は、時計で検出できない誤対応があり得ることを利用者に示す。

現行（抜粋）:

> Audio Thread（processBlock）は通常計測では読み取り・コピー・通知だけを行う。比較試聴では、非RT側で検証・decode・準備した事前確保済みReference bufferの選択とRT-safeな出力だけを許可する。

案:

> Audio Thread（processBlock）は通常計測では読み取り・コピー・通知だけを行う。Reference比較試聴では、非RT側で検証・decode・準備した事前確保済みReference bufferの選択とRT-safeな出力だけを許可する。live比較では、PREは事前確保済みの共有memory ringへの書込みと周回の公開だけを、POSTは同じringの読取り、対応の判定、RT-safeな出力だけを許可する。

### 2.2 3層隔離

現行: `明示的な比較試聴は準備済みReference bufferのRT-safeな選択・出力だけを許可（alloc/lock/IO 禁止）`

案: `明示的な比較試聴は準備済みReference bufferの選択・出力と、live比較の事前確保ringの書込み・読取り・判定・出力だけを許可（alloc/lock/IO 禁止）`

### 2.3 現行製品面の正本

現行: `Reference比較試聴と承認済みのローカルBlindは通常A経路とは別の明示操作である。`

案: `Reference比較試聴、live PRE/POST比較、承認済みのローカルBlindは通常A経路とは別の明示操作である。`

## 3. 不変条件表（docs/hypha_invariants.md）

live比較の節（INV-LC）を新設する。
不変条件表の規則（各行に実在の試験名を紐づける）に従い、試験名は実装時に確定する。
下表の試験欄は、実装計画第13節の受入試験IDである。

| ID | 不変条件（案） | 紐づく試験（計画） |
| --- | --- | --- |
| INV-LC1 | PREとPOSTのsample対応は、連続時計と、定常区間でproject時刻の一致から較正した差K（同じ候補8回）で決める。project時刻だけ、counter差、wall-clock、相関で対応を決めない | LC-02、LC-04 |
| INV-LC2 | PREは、連続時計が直前のblockと連続しないとき、または自分の呼出しの空白（直前block長の2.5倍かつ20 ms超）の後に周回を改め、周回番号、先頭、書込み末尾を一貫して公開する。POSTは今の周回で書かれ、ring容量内にある範囲だけを受け入れ、書込み末尾だけでは受け入れない | LC-04、LC-05、LC-26 |
| INV-LC3 | POSTは自分の呼出しの空白でKを無効にし、同じ候補8回で較正し直すまでPREを出さない。AUでは候補の食い違いを1回見た時点でもKを無効にする（M1） | LC-20、LC-26 |
| INV-LC4 | 境界（seek、再生開始、loop、sleep、遅延変更）で証明できない区間はPOSTを出力し、PREの選択と承認済みgainを保持して、証明が戻った最初のblockからPREへ戻す。説明できない欠落、protocol／pair／format失効、上限超過では比較を中断し、再選択まで戻さない | LC-06、LC-12、LC-14、LC-26 |
| INV-LC5 | 遅延変更の直後は、時計で検出できた時点で直ちにPOSTへ倒す。検出できない最初の区間は許容し、hostごとの長さをprofileと説明書に記す（Studio Pro 8.1.2は最大171 ms）。相関による停止監視は使わない | LC-20 |
| INV-LC6 | （副作用の確認後に採用）PREとPOSTはinfinite tailを報告し、hostのplugin sleepで比較が途切れないようにする。音声とlatencyは変えない | LC-01、LC-21、LC-25 |

INV-LC6は、現行の`getTailLengthSeconds()`が0を返す実装（`juce_shell/src/PluginProcessor.cpp`）を変える。
JUCEはVST3でinfinityを`kInfiniteTail`として返す。infinite tailを報告する使い捨てのTail MonitorはVST3でpluginval（strictness 5）に、AUでauval（Tail Timeの検査を含む）に合格した。AUのhostが`kAudioUnitProperty_TailTime`を読むと、無限大のFloat64（`inf`）を受け取る（2026-09-28に確認）。各hostがbounceなどでこの値をどう扱うかは未確認である。

## 4. README.md

`## Local PRE/POST Blind Compare`の前に、live比較の節を加える。
READMEは英語なので、案も英語で書く。画面の日本語は翻訳catalogに入れる。

> ## Live PRE/POST compare
>
> Switch between PRE and POST of the same chain while the song keeps playing, at matched level. PRE is the input POST saw, sample for sample: Hypha only plays PRE where it can prove which PRE sample belongs to the POST sample being heard. Where it cannot, you hear POST, and PRE comes back by itself as soon as it can prove it again.
>
> - Right after a seek or a new start, you hear POST for about the latency of the chain between PRE and POST.
> - If the input stays silent for several seconds, the DAW may stop calling Hypha. PRE comes back shortly after the sound returns.
> - If you change a plug-in setting that changes its latency (look-ahead, oversampling, linear phase) while comparing, PRE can be misaligned for up to about 0.2 s right after the change.
> - Offline render and a broken or changed pair end the comparison. Select PRE again to continue.

sleepの一文は、INV-LC6を採用したら「DAWが止めても比較は途切れない」の趣旨へ差し替える。

## 5. 正本へ入れる前に確かめること

| 項目 | 内容 |
| --- | --- |
| infinite tailの副作用 | Studio Pro、Logic、Pro Toolsでのbounceの末尾（AUのhostは`TailTime`として`inf`を受け取る）、停止中のCPU、AAX。VST3のpluginvalとAUのauvalには合格済み |
| 他のhostの遅延変更 | Windowsと他のDAWで、時計で検出できない区間の長さ。報告が先に来る型と報告が来ない型 |
| M1 | plugin内での実装と、再配置の直後の追加棄却（記録からの再計算ではAUで30 block） |
| 実装の承認 | 本書の差分は実装の承認と同時に正本へ入れる。承認前に正本の文言だけを先に変えない |
