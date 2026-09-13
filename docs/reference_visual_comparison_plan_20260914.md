# Reference — A/B visual comparison implementation plan

Date: 2026-09-14
Status: 計画策定。製品実装・配布は未着手。
Baseline: Hypha B-871 `3ccec450` / Kirin OS W-3080 `31fd330c`。

Daisukeが承認した方向は、曲全体の波形を比較の入口とし、現在のDAW音Aと計測済みVersion Bを、同じ場所・共通の尺度で見比べることである。
本計画の数値予算と操作の細部は実装時の検証対象であり、達成済みの製品仕様ではない。

## 1. 利用者に届ける結果

Referenceを開いてBのVersionを選ぶと、計測済みの全体像が現れる。
AはDAWで再生した場所から埋まり、現在位置がA/Bの共通時間軸を動く。
気になる区間を選ぶと、その区間の波形・強弱・ピークと密度の違いを確認できる。
A/B切替、表示項目の変更、画面サイズ変更で、確認していた場所を見失わない。

通常の操作は「Bを選ぶ → DAWを再生する → A/Bを切り替える」とする。
区間選択は任意の掘り下げ操作であり、通常試聴の開始条件にしない。
追加のConnect、A登録、解析開始、毎回のResetは設けない。
同じ曲のVersionを変更した場合も、検証が成立した後は同じ曲内の確認位置を引き継ぐ。
新しいBの読込・整列だけでBへ自動切替しない。

本計画の完成範囲は、A/B全体波形、区間選択、連動するLoudness/Crest比較、観測範囲管理、全サイズ、Blindとの分離、軽量性と実機検証までである。
これらを未検証のまま一部完成として配布しない。

## 2. 現物とコードで確認した現在地

| 対象 | 確認した事実 | 必要な対応 |
| --- | --- | --- |
| 全体波形 | `HyphaReferenceVisuals.cpp`はB/Cのsample peakを左右最大値でまとめ、上下対称の外形を描く。実際の正負PCM波形ではない | A/B共通軸とRMS内層を追加し、位相やsample一致を読み取れる波形と誤認させない |
| 既存Bデータ | OSの`reference_analysis.rs`はL/R別peak・RMS、LUFS-M/S、block crestを生成する | 既存の不変な計測物を再利用。追加解析を常時走らせない |
| Dynamics空欄 | OSはwindow不一致のPSRを出さないため、`psr`を全件nullにしている。HyphaはPSR系列を選ぶためNO DATAになる | 本計画ではCrestを明示して使用。CrestをPSRという名前で表示しない |
| Aの観測 | `ReferenceRuntimeACapture`は位置合わせ用の短いPCM観測であり、全曲の描画履歴ではない | 校正用captureと全体の表示要約を別責務にする |
| 時間 | 既存描画は配列の添字を横幅へ正規化する。OSのLUFSはbin終端で取得される | `framesPerBin`、`hopSamples`、最終bin長、window終端を使う共通時間モデルが必要 |
| 実機 | B-871診断版で自動Version一覧、B再生、A復帰を確認済み | 全体A/B表示と実DAWの厳密なPDC一致は未検証 |
| 残存不具合 | Windows CIで試聴所有権の解放時刻に関するassertionが2回失敗。AAE-6101の原因は未確定 | 新しい観測処理を増やす前に再現条件と既存負荷を記録し、解放処理の問題を修正する |

OSは波形を最大2,048 binへまとめ、hopを100 msの整数倍にしている。
Hyphaの既存読込上限4,096は受理上限であり、生成数や詳細解像度ではない。
全体要約を拡大するだけでは、元のbinより細かい事実は増えない。

## 3. 表示と操作

### 全体波形

- Aを上、Bを下とする2段表示を既定にする。cyanは現在のA観測、amberは保存済みBという既存の色の意味を継承する。
- 横軸は同じ曲内時刻、縦軸は共通の振幅倍率。片方だけの自動正規化は行わない。
- 外形はsample peak、内層はRMS。L/Rの統合方法は両者で同じにし、逆相stereoを加算して相殺しない。
- L/R統合はpeakが各channelの最大値、RMSがchannel energyの平均から求める値とする。channel別事実は内部で保持する。
- 音量一致が成立している表示ではBへ実際の固定試聴gainを適用する。Aは0 dB。成立しない場合にMATCHEDと表示しない。
- A/Bの同時刻を1本の再生位置線で示す。整列が未確定なら共通位置・差分を表示せず、B単独の全体像を維持する。
- Aの未観測部分は空白とする。無音の測定結果と区別し、未観測を0へ補間しない。
- 重ね合わせはモックで比較した代替案。初回実装の既定は2段とし、恒常的な設定項目を増やす根拠がなければ追加しない。

### 区間選択と下段

- 波形上のクリックでその付近を表示上の確認区間にし、ドラッグで範囲を調整する。キーボードでも移動・調整できる。
- 初期表示はDAW位置へ追従。区間を選ぶとその表示を保持し、短いFOLLOW操作で追従へ戻る。
- 選択区間は表示・比較の範囲である。クリックがDAWのseek、ループ、音声切替を暗黙に行うことはない。
- モックの12秒固定とrange sliderは操作説明用。製品では波形上の範囲操作を主とし、同じ役割のsliderを常設しない。
- 下段はLOUDNESS / CRESTの1面切替。全体波形と同じ確認区間を使い、独立した3枚のグラフを常設しない。
- グラフを指すと同時刻のA、B、B−Aを表示する。数値の基準時刻・単位・windowを短い凡例で確認できる。
- A/Bを押す音声操作と、何を表示するかの操作を分ける。音声切替時に波形、確認区間、軸倍率をリセットしない。
- Version変更で確認区間を引き継げるのは新Bへの同曲・位置の検証が成立した場合だけとする。範囲外や未成立を端へ黙って丸めない。

### 比較指標の意味

| 表示 | 利用者が確認すること | 数値契約 |
| --- | --- | --- |
| Peak + RMSの外形・内層 | 盛り上がり、休符、終わり方、ピークと持続的なエネルギーの分布 | 同一時間bin・channel集約・振幅倍率。sample peakはTPとして表示しない |
| LOUDNESS | 固定Gain Match後にも残る、同じ区間の強弱の違い | LUFS-Sを既定。同じ3秒windowの終端でA/Bを比較。必要な連続入力が無ければ欠測 |
| CREST | 同じ区間のTrue PeakとRMSの開き | 現行OSのblock crest定義を明示し、Aも同じbin・window・TP条件で計測。一定gainによるCrestの変化は0 |

LUFS系列の算術平均を「区間LUFS」と呼ばない。
モックの`region mean`は製品仕様へ採用せず、初回は同時刻の値と差に絞る。
区間Integrated LUFSは、両者の完全な同区間PCMを正しく計測した場合に限るため、本計画の表示へ追加しない。
Crestや波形から圧縮量、アタックの良し悪し、品質向上、推奨設定を断定しない。

## 4. Aの正確さと更新

Aは常に比較出力へ置換する前のDAW入力である。
BやCの再生音、Blindで一時減衰された出力をA履歴へ混ぜない。
専用の軽い表示要約を通常計測・Record・校正receiptと分離し、正常Aの計測正本を変更しない。

各要約にhostのsample位置、rate、channel、連続性世代、取得pass、観測範囲、有効性を持たせる。
sourceの時刻へ投影する時は、検証済みの位置mapとその世代を追加する。
mapが確定する前のhost座標を、確定済みの曲内座標と混ぜない。

- 再生した有効区間だけを蓄積する。先に聴いていない箇所や、queue欠落を推測で埋めない。
- 再訪した区間は最新の完全なbinで置換する。前passと途中passのwindowを継ぎ合わせない。
- seek、ループ、停止による不連続をまたいでLUFSやCrestのwindowを作らない。必要な履歴が再び揃うまで欠測にする。
- rate/channel/host配置の変更、検出済みの音声変更、map矛盾で該当世代を無効化する。共通の正確な比較と古い観測表示を区別する。
- 他の区間の編集をプラグインが即時検出できるとは扱わない。過去passの未再訪部分は「最後に観測した形」であり、現在の全曲を保証する線ではない。低明度の履歴表示と範囲情報で区別する。
- 初期状態・再openではAの全曲キャッシュを現在の音として復元しない。Bと表示上の選択は復元できるが、出力はAから始める。
- Referenceを閉じた間の空白を埋めるため、常時full-song録音を追加しない。再表示後に再び観測する。
- B試聴中も実入力が提供され、通常の観測条件を満たす場合は、置換前Aを観測できる。入力の無いhostやbypassでは進めない。

既存の短いcaptureは同曲判定・固定gain校正のために維持する。
新しい全体表示が埋まるまで、既存のA/B試聴を待たせない。

## 5. Bの再利用、時間・計測形式

OSのsource hash、PCM hash、rate、channel、全長、計測物receiptを検証して既存の全体要約を読む。
同じVersionを再選択しただけで、全曲decode・hash・FFTを繰り返さない。

実装最初の契約確認で、次をfixtureとともに確定する。

1. 波形binは`[start, end)`、末尾は実frame数で集約する。peakは最大、RMSはenergyとframe数からまとめる。dB平均で再binしない。
2. LUFS-Sは値を取得したwindow終端へ置く。現行producerがbin末尾で測った値を先頭時刻へ置いていないか、短い既知信号で確認する。
3. CrestのTP window、RMS window、channel統合、silence条件をproducer/receiverで一致させる。長曲でhopが変わっても異なるwindowの値を直接減算しない。
4. 全体概観の解像度と実測数値の解像度を区別する。見た目の補間値を計測値としてtooltipへ出さない。
5. rate変換時はsource sampleとhost sampleを有理比で対応させ、既存の整列結果とresampler遅延を使う。丸め誤差をbinごとに累積させない。
6. 異なるrateの元B要約を、そのまま変換後の試聴コピーの厳密なpeak/crest値と呼ばない。必要な区間は既存の準備済みBページをworkerで再計測し、共通条件が成立した範囲だけ厳密な差を表示する。

既存measurement 2.0を無断で別の意味に変更しない。
追加のwindow/algorithm情報が必要かは上記fixtureで決め、必要なら旧receiptと旧readerを維持した新しい計測profile/形式を定義する。
実装開始時の成果物に採用形式と旧版互換の判断を残し、曖昧な時刻のままUIへ進めない。
Kirin OSとHyphaはコードを共有せず、公開データ契約と独立実装の数値一致試験で揃える。

## 6. Blind・C・サイズ・復帰

- Version Blind中はsource名だけでなく、A/Bの波形形状、色、数値、選択履歴、tooltip、accessibility名も隠す。音から推測できるグラフを中立色に変えるだけでは不十分。
- Blind中は既存の1/2、回答、Reveal、ENDへ集約する。終了後に元の表示範囲へ戻す。
- PRE/POST BlindとReferenceの排他を保持する。PRE/POST captureをReference全体波形のAデータへ流用しない。
- Cは独立したCheck/プリセットとして維持する。別曲CにA/B用の同曲時間mapを適用しない。
- INSPECTを表示・登録・接続・解析の前提にしない。
- 未提供値を大きな空panelで占有させず、その表示項目の位置で短く欠測を示す。明示操作が失敗した場合は、Aを維持したことと必要な次の操作だけを伝える。

| サイズ | 表示方針 |
| --- | --- |
| 300×200 / 375×250 | 通常A/B/C、B/C選択を維持。全体波形は最小の所在表示、数値は同時刻の差を優先し、詳細graphを縮小して詰め込まない |
| 450×300 / 600×400 | 2段の全体波形と、選択した比較項目を面積に応じて配置。hover依存にせず範囲操作と現在状態を読めるようにする |
| 900×600 | 全体波形、確認区間の詳細比較、操作の余白を確保。新しい指標を増やして面積を埋めない |

通常A/B/Cは全サイズ、Blindだけ300%という合意を維持する。
300%のウィンドウ数自体へ新しい2枚制限は付けない。
新しい観測workerは既存の2枠Analysis予算と連動させ、既存所有者なら同じ枠を使う。追加の隠れた解析枠を作らない。
取得できない時も既存要約の表示とAへの復帰を維持する。

## 7. 軽量化と分離

- Audio Threadは事前確保queueへの必要なコピーと通知だけを行う。FFT、測定、allocation、lock、file I/O、破棄待ちを追加しない。
- 可能な限り既存の入力コピーをworker側で共有する。UI描画のためだけの全曲PCM bufferを作らない。
- queueが満杯なら観測を落とし、その範囲を欠測にする。音声callbackを待たせない。
- 要約、整列、Bページ供給、所有権解放、journal書込みの依存を監査する。観測やdisk書込みが遅くてもAへの復帰・解放を妨げない。
- 静的な全体波形pathはVersion、要約世代、サイズ、表示gainが変わった時だけ再構築する。描画ごとの文字幅探索・全bin計算を避ける。
- 要約更新は最大10 Hz、位置線の描画は最大30 Hzを初期上限案とし、非表示時は停止する。
- 新規の要約・描画cache・snapshotの追加メモリは合計2 MiB/receiverを初期予算とする。既存PCM/page/capture予算とは別に増分を実測する。
- 追加の入力queueが必要なら最大2 MiBを事前確保の別予算とし、queue共有の可否を先に確認する。長曲で無制限に増えない構成にする。
- 900×600の追加描画は基準実機でp95 4 ms以内を初期目標とする。平均だけでなくp95/p99、callback時間、dropout、peak RSSを記録する。

数値は実装前の設計予算である。達成できない場合はデータの正確さを下げず、再描画頻度、cache、worker境界、表示密度を見直す。
通常Aの既存0 samples・bit identity・CPU基準を緩和しない。

## 8. 変更する責務とファイル

| 所有 | 対象 | 変更内容 |
| --- | --- | --- |
| Hypha入力 | `PluginProcessorReference.cpp`、`ReferenceComparisonController.*`、`HostProcessClock.h`の利用境界 | 置換前A、sample clock、世代、有効性を観測へ渡す。clockを勝手にPDC権威へ昇格させない |
| Hypha観測 | 新規`ReferenceVisualObservation.*` / `ReferenceVisualTimeline.*`（名称案） | 有界queueのconsumer、bin集約、coverage/pass管理、連続window、immutable snapshot |
| Hypha計測読込 | `ReferenceRuntimeV2Measurement.*`、source/cache/measurement契約 | 明示的な時刻、Crest/RMS、異rate・旧形式の互換判定 |
| Hypha表示状態 | `PluginEditorReference.cpp`、`HyphaReferenceComponent.*`、選択・controls | auditionとviewの分離、FOLLOW/区間、欠測、Blind秘匿、既存C維持 |
| Hypha描画 | `HyphaReferenceVisuals.cpp`、`HyphaReferenceLayout.cpp`、新規比較描画module | 共通軸の2段波形、RMS内層、連動するLoudness/Crest、5サイズ |
| Kirin OS計測 | `native/src/reference_analysis.rs`、`referenceWorkspaceSourcePreparation.mjs` | windowの意味と数値fixtureを固定。必要な場合のみ計測profileを追加 |
| Kirin OS契約・配信 | `referenceWorkspaceRuntimeMeasurement.mjs`、artifact compiler、Library/source preparation/cache | 旧形式互換、不変receipt、計測profile更新、再利用と無効化 |
| 検証 | native Reference/UI試験、OS計測・reader/publisher試験、実機runbook | 同一の数値fixture・失敗条件・操作試験を両repoで独立実装 |

`PluginEditorReference.cpp`は基準時点で500行である。
新責務は先に500行以下のmoduleへ抽出し、既存巨大ファイルに表示・観測・校正をまとめて追記しない。
既存long-file baselineを下げた時はratchetを同じコミットで更新する。

## 9. 実装順序と各完了条件

1. **基準とデータ契約を固定する。** 実物の波形/RMS/Crest、時刻anchor、rate変換、末尾binをfixture化する。Windows解放失敗を再現・切り分けし、既存host負荷を測る。出力は比較の数値契約、互換形式の決定、負荷baseline。
2. **観測と解放の境界を整える。** 既存の所有権問題を修正し、Aの要約/coverageをUIから独立して実装する。通常Aのbit identity、0 latency、worker停止・queue欠落時の継続を先に通す。
3. **共通時間と比較値を完成させる。** A/Bのbin、LUFS終端、Crest、gain、rate変換、欠測を一致させる。UIより前に既知信号・同曲変種の数値試験を通す。
4. **共通UIを一括で作る。** 2段波形、RMS内層、FOLLOW/区間、Loudness/Crest、A/B切替時の位置維持を全5サイズへ実装する。通常C、Version Blind、PRE/POST Blind、復帰まで一緒に検証する。
5. **実機で反復し、完成判定する。** macOS/Windowsの対象host、PDC、連続再生、再open、欠損、負荷、利用者動線を確認する。見つかった問題を反映して同じ変更範囲の検証を閉じる。

これらは1つの完成範囲の内部作業順であり、未完成項目の別Phase送りを意味しない。
計画承認だけで新しい公開リリースや自動配布を行わない。

## 10. 検証と合格条件

### 数値・同一性

- 既知音源の同一A/B、定数gain、head padding、EQ、非線形dynamicsを、曲の冒頭・中間・末尾と長い無音前後で確認する。
- 同一rate/整数offsetのfixtureはsource位置誤差0 samples。rate変換は理論上の対応位置とresampler遅延から誤差・不確かさを別記し、0でない結果を完全一致と報告しない。
- 同一PCM/同一条件のA/B要約は量子化誤差以内。peak/RMS/Crestは基準計算との差0.05 dB以内、LUFS-Sは0.1 LU以内を初期許容値とする。
- 定数gainの波形・LUFSはそのgain分だけ変わり、Crestは0.05 dB以内で不変。fixed gainを区間ごとに追従させない。
- 非整数rate比、逆相stereo、mono、1 sample末尾、3秒未満、欠損bin、非連続再生、異なるhop、再binを含める。
- DSP変更で波形自体が異なることと、位置誤差を混同しない。曖昧な曲位置やtempo/edit差を無理に整列させない。

### 境界・復帰

- worker停止、queue満杯、遅いdisk/journal、OS終了、source移動/変更/削除で通常Aを継続する。
- 観測がなくてもAへ戻せる。B/C間の遷移、連打、終了tail、所有権解放を同時に検証する。
- 100回のA/B/C切替、seek/loop/pause、host bypass/offline、画面close/open、rate変更で古いsnapshotやmapを採用しない。
- PRE/POST Blindとの競合、2枠使用中の追加instance、frame0、各Blindの停止中ENDを含む。
- Blindは画像、tooltip、AX/accessibility、再open後の残像にsource情報が漏れない。
- 再起動でA/B/C選択肢と表示上の範囲は復元できるが、Aの観測済み判定や試聴開始は復元しない。

### 実機と使いやすさ

- Studio OneとPro Toolsを含むmacOS実機、Windows対象hostで確認する。利用可能なformatと未確認formatをreceiptへ分けて残す。
- 2MIXに常設 + TRACK観測、TRACK2個比較、非表示/小画面を複数追加、というDaisukeの使い方を含める。
- 実機のPDCを既知の遅延0および非0条件で検証し、DAW出力のcaptureとcallback/source証跡を同一commitへ結ぶ。画面の位置線だけでは合格にしない。
- 基準実機で44.1/48/96/192 kHz、対応する64/128/512/1024 framesを対象とし、未対応条件は実測表へ明記する。
- 最も負荷が高かった対応条件と通常使用条件で、それぞれ30分の再生・切替を行い、AAE/dropout/CPU spike/メモリ増加を確認する。重いprofiler同時実行を唯一の基準にしない。
- 事前説明なしに「Bを選ぶ、今の場所を知る、違いを見る、Aへ戻る」を完了できるか実機で確認する。迷った操作、追加クリック数、初回準備時間、再選択時間を記録してUIへ反映する。

Rust変更時はworkspace test/clippy、FFI変更時はignored parity/pairingの実測全件も通す。
C++のnative/runtime/UI契約、OS変更時のtargeted + full baseline、source-line budgetを通す。
新しいsigned DAW/Windows実機証跡は実装commitへ結び、過去の診断版や別commitの合格を流用しない。

## 11. 完成の報告

実装完了は、上の機能範囲と検証が揃い、通常経路・Blind・Cに退行がなく、既存のWindows所有権問題とCPU問題の扱いが証拠付きで説明できる状態とする。
公表時の「正確」「軽い」は、対応host/rate・誤差・負荷の実測範囲を越えない。
公開リリースを別途行う際は、同一版のLS macOS pkg、HP macOS zip/Release/英日リンク、署名済みWindows installerの3チャネルを揃える。

参照: [位置合わせと固定gain](reference_whole_song_alignment_20260913.md)、[既存の整合性修正](reference_review_corrections_20260913.md)、[表示体系](hypha_ce2226_jungle_visual_system_20260901.md)。
比較案の根拠として参照した他製品の公式資料: [Metric AB](https://adptraudio.com/product/metric-ab/)、[REFERENCE](https://www.masteringthemix.com/pages/reference-manual)。他製品に存在しない機能であるとは断定しない。
