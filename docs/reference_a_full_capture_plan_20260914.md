# Reference — Capture A 実装計画

Date: 2026-09-14
Status: 実装・対象試験を実施。実機を含む完成判定は [検証記録](reference_a_full_capture_validation_20260914.md) を参照。
Baseline: Hypha B-873 `674e383a93aedcb3cbab211452e7a1f17fb2d931` / Kirin OS W-3080 `31fd330c`。

利用者の合意は、現在のDAW音Aを一度曲全体で把握し、波形・音量・強弱の比較に使えるようにすることである。
通常のA/B比較を続けながら、必要なときに全体を取得して保持する操作を加える。
本計画は既存の[全体比較計画](reference_visual_comparison_plan_20260914.md)を拡張する。
数値予算・操作名は設計案であり、実装済み・実証済みの仕様ではない。

## 1. 到達する使い方

基本動線は **CAPTURE A → 曲頭からDAWを再生 → 曲末でDAWを停止 → 全体を見比べる** とする。
停止後の集計・保存・表示切替は自動で行い、別のSaveや解析開始ボタンを増やさない。
取得後はA/Bの全体波形から気になる場所を選び、同じ範囲のLoudness/Crestを確認できる。

- Capture Aを使わなくても、今の短い観測によるA/B/C試聴を利用できる。
- 取得するAは、そのPOSTに入る置換前のDAW入力。TRACKならその経路、2MIXならそのバスであり、他の経路を合成・推測しない。
- B未選択、Kirin OS未接続、位置合わせ未成立でもA単体の取得を始められる。接続操作・Work登録・INSPECTを前提にしない。
- Aの試聴ボタンは現在のDAW音を選ぶ。取得時点の音への切替ボタンにはしない。
- 本計画のCaptureは計測情報の保持を中心とする。音声ファイルの保存・取得音の後日再生・OSへのVersion登録は追加用途であり、この合意から自動的に実装範囲へ含めない。

## 2. 現行実装との差

| 現物で確認した制約 | 今回の対応 |
| --- | --- |
| `observeAInput`はVersion選択済みのときだけ表示観測へ渡す | A取得の開始条件をB選択から分離する |
| `VisualObservation`はBの検証済みmap・overviewに依存する | Aはhost sample軸で独立取得し、Bへの投影を後段に置く |
| 表示を閉じると観測が止まる | 明示的なCapture中だけeditor非表示でも取得を続ける |
| Bの読込とA/B計測が同じ表示worker内にある | BのI/O停止がA取得を欠落させない責務分離を先に行う |
| 現在の要約はBのbin境界に合わせる | A自身の取得範囲と要約を持ち、異なるBでも再利用する |
| 現在のmeterはLUFS-S・TP/RMSで、取得範囲全体のLUFS-Iを持たない | 独立したCapture計測に連続範囲のIntegrated集計を追加する |
| 再起動で保存するのは表示範囲の設定のみ | 取得時点の要約を、現在のAと区別して復元する |

B-873の完全なruntime試験では8秒の80 bin中75 binが観測された。校正・準備中の空白は仕様どおりだが、Capture Aでは冒頭を取り逃さない設計が必要である。
既存の表示observerへ保存フラグを追加するだけでは完結しない。

## 3. 開始・終了と「全曲」の扱い

初回の開始操作は取得を待機状態にし、次の有効な再生callbackから入力を受ける。
再生中に押した場合はその位置から取得し、曲頭から取得したことにはしない。
開始時には必要な解析枠・事前確保を準備し、受付前の音を取得済みと扱わない。

通常終了はDAW停止、または利用者のFINISH操作とする。壁時計の停止通知時刻ではなく、最後に受理したcallbackの終端sampleで範囲を閉じる。
無音だけで終了しない。長い休符・静かなアウトロ・リバーブ尾を切り捨てない。
Bの全長は目安として利用できるが、Aの末尾を決める根拠にはしない。Version間の余白・尾・編集差を保持する。

ホスト時刻には欠損し得る値があり、JUCEのPositionInfoを曲全体の範囲保証として扱えない。これは[公式API](https://docs.juce.com/master/classjuce_1_1AudioPlayHead_1_1PositionInfo.html)と現行`HostProcessClock`を踏まえた設計判断である。
確実な曲範囲を持たない場合は、取得した開始・終了と時間を示す。単に停止しただけで「全曲取得済み」と断定しない。
既知の比較範囲がある場合は、その範囲の未取得部分を波形上で示す。範囲の確認・調整は任意の詳細操作とし、毎回の開始条件にはしない。

| 状態 | 画面上の最小表示・操作 | 内部の意味 |
| --- | --- | --- |
| 未取得 | CAPTURE A | 自動の通常観測を継続 |
| 待機 | PLAY / CANCEL | 次の有効な再生を待つ。受付済みかを明確にする |
| 取得中 | CAPTURING + 経過時間 / FINISH | 一つの連続passを取得。保存済みの前回分は保持 |
| 終了処理 | 短い進捗表示 | queue終端まで集計し、要約を保存する |
| 保持 | CAPTURED + 取得時間 / CAPTURE AGAIN | 取得範囲の連続性と保存を検証済み。曲全体の範囲保証とは別 |
| 不完全 | PARTIAL + 再取得操作 | 欠落や不連続がある。理由は操作に結びつく短文で示す |

停止位置より後の再生再開は自動的に追記しない。別passの計測を一曲分へ継ぎ合わせない。
seek・loop折返し・clock欠損・rate/channel/経路変更・queue欠落ではそのpassを不完全として閉じる。
失敗しても前回の正常な取得分と通常Aへの復帰を保つ。CANCELは今回の取得だけを破棄する。

## 4. 取得する事実

取得ID、algorithm/schema版、sample rate、channel構成、挿入instance、host clockの由来、開始・終了sample、連続性、実受理frame数、取得時刻を要約へ結びつける。
曲名・Work・Versionの見た目やB選択だけでAの内容identityを確定しない。

- 波形: L/R別sample peakとenergy/frame countを集計し、外側peak・内側RMSを描く。
- Loudness: 実入力の連続3秒windowから得たLUFS-Sを、そのwindow終端に置く。
- Crest: 同じbin範囲のtrue peakとRMSから算出する。PSRやLRAとは呼ばない。
- 取得範囲全体: 連続した全入力を処理したLUFS-Iと最大TP。取得範囲を必ず伴わせる。
- 照合: 既存の位置合わせと互換性を確認した時系列特徴と検証receiptを保持し、後の候補位置探索に利用する。

LUFS-Iは短時間LUFSの平均から作らない。採用中の[ebur128 0.1.10 API](https://docs.rs/ebur128/0.1.10/ebur128/struct.EbuR128.html)を使い、Capture専用の開始・終了・gate・履歴容量を検証する。
通常測定・Recordのmeterをリセットしたり、その数値をCaptureの集計と兼用したりしない。
同APIの履歴制限はIntegratedの対象にも影響するため、長曲の途中で古い範囲を捨てながら「全体LUFS」と表示する方式を採らない。
無音・短い取得・欠落・非有限入力では、成立しない指標だけを欠測にする。3秒未満でも成立する波形まで隠さない。

開始フィルタ状態、pre-roll、最終blockのframe数、TP末尾処理を独立した既知信号で固定する。
全曲の要約を拡大してもsample単位の形状は復元できない。表示解像度と照合精度を分ける。

## 5. 取得済みAと現在のA

表示の小さな選択をLIVE / CAPTUREDとし、A/Bの二段構成を保つ。第三の大きなグラフや常設説明文を増やさない。
CAPTUREDには取得時点・範囲を結びつけ、現在のDAW全体が同じ状態だとは表示しない。
画面内のA/B/Cは音の選択、LIVE / CAPTUREDは表示の選択として、mouse・keyboard・accessibilityでも混同させない。

- 取得完了後は全体像を表示し、以前の確認区間を範囲内へ維持する。
- DAWで後から調整したときは、再訪した場所を現在のAと比較する。差を検出した範囲を示し、保存したCapture自体は書き換えない。
- 未再訪の場所の編集は即時検出を保証しない。全体に「現在と一致」の印を付けない。
- 全体の更新はCAPTURE AGAINによる新しい取得を単位とする。新旧passの混合物を一曲分の正本にしない。
- B変更時はA Captureを消さず、新しいBの同曲性・map・対応範囲が検証できたところから比較する。

要約は上限付きの自己完結した形式でDAWのplugin stateへ保存する。復元は保持表示だけで、Capture開始・B再生・校正成立を復元しない。
editorを閉じてもprocessorが生存する限り明示Captureを続け、最小サイズや別domainにも停止へ戻れる小さな状態表示を残す。
plugin削除・DAW終了・途中のstate保存では、実際に確定できたsnapshotだけを保存する。途中状態を次回勝手に再開しない。
旧state、壊れた要約、未知schema、上限超過を受けても、通常A・B/Cの保存選択を読み出せる構造にする。

## 6. 音量合わせ・位置合わせ・Blindとの境界

[既定の音量方針](reference_whole_song_alignment_20260913.md)を維持する。
通常Aは0 dB、Bは対応するactive block差の中央値に基づく固定補正、現行のpeak ceilingと例外時の明示承認を保持する。
CaptureのLUFS-I差をそのまま新しい試聴gainへ置換しない。再生中に区間ごとにgainを追従させない。

全体の情報は、既存の位置・音量校正が曲内の離れた区間でも妥当かを確認する根拠として利用する。
特徴量・波形の一致だけでsample一致を宣言せず、確定には既存のPCM照合とhost clockの検証を必要とする。
不変なBの全体LUFSとA Captureの取得範囲が異なる場合、両者を同範囲のLUFS差として表示しない。
Bの異rate変換と表示gainは現行の試聴経路を正本とし、比較値に別の変換・補正を導入しない。

| 組合せ | 方針 |
| --- | --- |
| Captureと通常A/B/C | 置換前入力を取得し、通常試聴を許可。B/Cの出力やfadeをCaptureへ混ぜない |
| Capture中のB/C変更・B欠損 | A取得は継続。Bとの比較だけを無効化・再準備する |
| CaptureとVersion Blind | 同時開始を不可とし、完了または中止してからBlindへ進む |
| CaptureとPRE/POST Blind | 既存の排他へ同じ取得意図を登録する。Referenceの局所フラグだけで競合を防いだことにしない |
| 保持済みCaptureとBlind | 保持は可能。Blind中は画像・値・操作・tooltip・AXを秘匿する |
| Captureとoffline render/bypass | この計画のリアルタイム取得は中断。A透過を維持し、DAW処理を止めない |

これは音声を置換するPRE/POST Blindの4秒Captureとは別の取得である。通常RecordやINSPECTの入口を共用しない。

## 7. 負荷と実行構造

Audio Thread → 既存の有界入力queue → A計測/集約 → immutable Capture要約 → UI、の経路にする。
通常観測と明示CaptureはAの入力コピー・同一条件の計測を共有し、二重に全入力を測らない。
A計測workerにBファイルI/Oを載せず、B準備が遅いときは比較側を欠測にする。A保存にもBの整列成功を待たせない。

- Audio Threadは事前確保へのコピーとatomic通知だけ。allocation・lock・I/O・終了待ちは禁止。
- Captureも既存の2枠Analysis予算に含める。同じinstanceの表示・試聴とleaseを共有し、第三の隠れた枠を作らない。
- Captureは音声出力ownerとは別の取得意図を持つ。Aへ戻したためにCaptureの解析枠が解放される、という寿命の混同を防ぐ。
- 2枠使用中の新規Captureは開始受付できなかったことを短く伝え、音声と既存取得を継続する。途中から黙って開始しない。
- 非表示の通常観測は停止を維持。明示Capture中は描画だけを止め、Capture終了・取消時に追加計測とleaseを確実に解放する。
- Aの全曲PCMをRAMへ蓄積しない。boundedな計測履歴と要約で完了できる方式を選ぶ。

初期設計予算:

| 対象 | 予算・確認方法 |
| --- | --- |
| 取得時間 | 2時間を初期上限案とする。上限で安全に閉じ、全曲完了と誤表示しない。長尺fixtureで実装時に確定 |
| 入力queue | 既存2 MiB以内を共有。欠落時もAudio Threadを待たせない |
| 要約・特徴・集計 | activeと前回保持分を含む追加メモリ16 MiB以内を初期目標とし、実測。EBUフィルタ/3秒履歴は別計上 |
| 保存state | 全体波形・時刻・数値・validityの圧縮要約256 KiB以内を目標。encode/decodeはworkerで行い、state callbackへ処理を持ち込まない |
| 表示 | 最大2,048区間。必要なら同一passのpeak最大・energy総和・実frame数で段階集約。LUFS-Sは実window終端値を選び、平均しない |
| 更新 | summary最大10 Hz、位置線最大30 Hz。非表示時の描画0回 |
| 描画負荷 | 900×600追加描画p95 4 ms以内。B-873の1.000 ms cached / 2.523 ms rebuildを基準記録にする |

時間上限までIntegratedの全履歴を含められることを測定する。予算超過を隠すために数値・範囲の正確さを落とさない。
要約の再bin時は元sample範囲と末尾長を保持し、A/Bの比較窓が一致する場合だけ差を出す。
snapshotの大きな破棄・serializationも非RTへ隔離する。保存時に音声callbackがfile、UI、worker完了を待つ設計にしない。

## 8. UIの実装範囲

全5サイズでCAPTURE A・開始受付状態・FINISH/CANCELへ到達できるようにする。
300%への拡大をCapture開始の条件にしない。Blindの300%条件と大画面数に制限を付けない既存方針を維持する。
小画面は一つの操作と時間表示を優先し、範囲・全体指標の詳細を常設しない。
英語の短い状態名を使い、星・大きな説明・品質を採点する色を加えない。
見た目は[CE2226表示体系](hypha_ce2226_jungle_visual_system_20260901.md)の観測・保持・接続の意味を守り、Cや既存Blindを含め共通shellで検証する。

## 9. 変更責務と順序

| 順序 | 対象 | 完了条件 |
| --- | --- | --- |
| 1. 契約・fixture | 新規Capture状態/数値試験、既知音源、host clock fixture | 範囲・停止・不連続・whole/partial・編集後の意味が実行可能な試験になる |
| 2. A取得の独立 | `ReferenceComparisonController.*`、`ReferenceVisualObservation.*`、`PluginProcessorReference.cpp`、新規`ReferenceACaptureSession.*`等 | B準備前から取得し、BのI/O停止や画面closeでも明示取得が続く |
| 3. 計測・保存 | `crates/kirin_measure/src/reference_visual.rs`からの責務分離、新規Capture meter/FFI、`ReferenceComparisonSettings.h`と新規snapshot codec | 連続全範囲のI/TP、要約、失敗時保全、旧state互換、非RT保存が成立 |
| 4. 比較・校正境界 | `ReferenceVisualTimeline.h`、`ReferenceVisualBinding.cpp`、既存content alignment/ACaptureの接点 | B変更と異rate/異なる余白でもA保持。旧Captureを現在の校正証拠に誤用しない |
| 5. 共通UI | `HyphaReferenceComponent.*`、layout/controls、`HyphaReferenceComparisonView.*`、`PluginEditorReference.cpp` | 全サイズ、LIVE/CAPTURED、再取得、Blind秘匿、別domainからの停止が成立 |
| 6. 実機・負荷 | native/source/UI suite、Studio One/Pro Tools、Windows対象host | 下記の正確さ・復帰・負荷・操作の合格条件を満たす |

新規owned sourceは500行以下。既存500行超の変更責務は先に抽出し、無関係な大規模分割を行わない。
この計画ではOS側の画面・配信schema変更を必須にしない。既存のB計測物を使い、CaptureをWorkやRecordの正本へ書き込まない。
機能全体の内部実装順であり、検証や保存を別Phaseへ送る計画ではない。

## 10. 検証と完成判定

実装中は変更責務に絞った試験を使い、最終コードで全体suiteを一度にまとめる。修正後は影響範囲を再確認し、成功済み全体suiteを無条件に反復しない。
この計画作成だけではRust/C++の再ビルド・全体テストを実行しない。

- 正常数値: 一曲通しの参照計算とCaptureを比較。frame範囲誤差0、LUFS-I/S ±0.1 LU以内、peak/RMS/Crest ±0.05 dB以内を初期許容値とし、量子化誤差を別記する。
- 代表入力: gain差、EQ/dynamics差、逆相stereo、mono、前後余白、長い無音、1 sample末尾、3秒未満。Aの透過bit identity・0 samplesを維持する。
- 不完全取得: 開始途中、停止、seek、loop、rate変更、clock欠損、queue飽和、worker停止。欠落を0で補完せず、別passを合成しない。
- B独立: Bなし・OS終了・B変更/削除・source reader停止でもA Captureが保存できる。B再取得だけでAを消さない。
- 保持/更新: Capture後のDAW編集、未再訪範囲、再取得失敗、editor close/open、project保存/再open、別instanceへのstate複製、旧/壊れたstateで混同しない。
- 競合: 2枠使用中の追加開始、A/B/C連打、Capture中の両Blind開始、Blind中のCapture開始、全サイズへのresize、非表示中の停止を確認する。
- 位置合わせ: 取得範囲とBのmapを独立検証する。新しい実DAWの0/非0 PDC証跡を取り、波形が重なるだけでsample一致としない。
- 負荷: 44.1/48/96/192 kHzと64/128/512/1024 framesの境界確認、2時間入力fixtureでのmemory上限、代表実機条件で30分連続取得と切替を一度実測する。callback p95/p99、drop、RSS、AAEを記録する。
- 操作: 説明なしで取得・保持・LIVEへ戻る・再取得できるか、余分なclick/誤操作を確認する。全体像の保持を、現在Aの音声固定と誤解させないことを確認する。

Rust/FFI変更時はworkspace/clippyとignored parity/pairingの実測全件を最終ゲートへ含める。
macOS/Windowsで必要な証跡を同じ実装commitへ結ぶ。B-873までの実機未検証項目も、Reference全体の完成条件として継続する。
配布を行う場合は既存の3チャネル規約に従い、診断版の配置を公開版の完成と扱わない。

完成とは、全体取得・保存・表示・編集後の区別・既存試聴/Blind・負荷と復帰を一つの利用者動線で確認できた状態である。
提供・実機検証の状態は検証記録を正本とし、計画や内部試験だけで全環境の完成とは扱わない。
