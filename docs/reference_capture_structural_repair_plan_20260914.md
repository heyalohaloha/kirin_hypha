# Capture A — 構造修正と変更検知の計画

Date: 2026-09-14
Status: 計画。製品コードの修正・新たな実機検証は未実施。
Baseline: B-875 `d82f5261`。レビュー時のHEADはB-876 `841b96c1`で、対象Capture実装は同一。
Revision: B-878計画レビューの3件を反映。[根拠・判定・探索の実装契約](reference_evidence_and_discovery_contract_20260914.md)に音量基準、通知方式、G0検証を定義した。

利用者の依頼は、B-875の4件を構造から修正し、Capture後の調整によって現在の入力が変わった場合に分かりやすく知らせることである。
[既存計画](reference_a_full_capture_plan_20260914.md)の取得・比較・軽量性を維持し、本書を今回の修正範囲の正本とする。
実装の順序であり、一部を未完成のまま提供する段階分けではない。

H01〜H08の操作改善案は[Capture Aと操作導線の統合計画](hypha_capture_and_workflow_integrated_plan_20260914.md)で現行実装と照合した。
本書の構造・軽量性・変更検知を維持し、同計画のBlind復帰、失敗修復、A/B/C表示へ接続する。

## 1. 利用者に提供する動作

CAPTURE A → 再生 → 停止で取得範囲を保持する。
後からPOSTより前のEQ・コンプレッサー・フェーダー等を調整し、その影響がPOST入力へ届いた場合、記録時と同じDAW sample区間の入力差を自動で確認する。
差を確認したらA欄に小さく **A DIFFERS** を表示し、全体波形の該当範囲へ細い印を付ける。
表示の意味は「取得時のAと、今回観測した入力に差がある」であり、音質の評価や操作した機器の特定ではない。

- 追加の接続・照合開始ボタンは増やさない。取得済みAを開いて再生すれば確認が進む。
- 保存したAは保持する。更新は既存の **CAPTURE AGAIN** から利用者が開始する。
- A/B/Cは音、LIVE/CAPTUREDは表示の選択である。検知による音の切替・Gain追従・再取得は行わない。
- 検知済みの区間と未確認の区間を混同しない。「曲全体が現在と一致」「同じ曲の位置が一致」とは、この通知から表示しない。
- 再生がない間、POSTの後段、別の音声経路、音に影響しなかった操作は、この入力照合では検知できない。
- 操作されていないプラグインでも、ディザー・乱数・アナログモデリング等で出力が変わり得る。音の差から操作の有無や原因を断定しない。

現行Hyphaには他プラグインのパラメータ操作を受け取る接続がない。
JUCEの[AudioProcessorListener](https://docs.juce.com/master/classjuce_1_1AudioProcessorListener.html)は登録対象processorの通知、VST3の[IComponentHandler](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IComponentHandler.html)は当該controllerからhostへの編集通知である。
これらをDAW内の他プラグイン全体を監視するAPIと解釈しない。全format共通の根拠は、Hypha自身が受け取った入力とhost情報に置く。

## 2. レビューで再現した問題と責務の変更

| 問題 | 再現した事実 | 構造上の対処 |
| --- | --- | --- |
| 過去のAに現在の位置対応を適用 | live側の位置対応を1秒変更しても、同じCaptureがalignedのまま30区間を誤比較。元はRMS差0、変更後最大0.0772399 | Capture固有の位置対応とLiveの位置対応を別にする |
| 復元直後の保存が空 | 4,504 bytesの復元入力に対し、直後の保存値0 bytes。worker完了後に復帰 | 永続化の正本をworkerの進捗snapshotから分離する |
| 不完全取得の状態が消える | complete=falseをheldとして復元し、PARTIALが消える | 取得の完全性を保存された事実として扱い、表示状態から推測しない |
| 終了しても冒頭だけを詳細表示 | 0.1秒時点から4秒完了まで表示範囲が0..0.1秒のまま | 表示範囲の意図をfit/manual/followとして独立させる |

再現コード・結果は開発worktreeの`target/b875-review/`に保持済み。実装時には本番の回帰試験へ移す。

## 3. データと所有者を分ける

| 責務 | 持つ情報 | 変更できる契機 |
| --- | --- | --- |
| CaptureDocument | 不変な取得ID、入力format、取得sample範囲、完全性・終了理由、測定値、照合用の要約 | 新しい取得を確定、または保存済みDocumentを復元するとき |
| CaptureStore | 確定したDocument、保存用payload、復元要求の世代、直前の正常なDocument | 一つの世代を確定したとき |
| CaptureAttempt | 今回の待機・取得・終了処理、queue、計測器、解析枠 | 明示開始から終了・取消まで |
| CaptureBinding | Capture内sampleと不変なB sourceの対応、証拠の範囲、source hash、format、校正の世代 | そのCaptureへ結びつく照合が成立したとき |
| CaptureGainReceipt | CaptureとBに固有の校正区間、policy、固定表示Gain、headroomとfallbackの根拠 | 当該Captureの入力に結びつく校正が成立したとき |
| LiveEvidence | 現在の再生pass、DAW時間軸の根拠、実際に確認した区間、raw差と通知対象の差 | 新しい入力を観測したとき |
| CaptureViewState | 全体表示・手動範囲・追従の意図、LIVE/CAPTURED | 利用者の表示操作、または取得確定イベント |

取得中のdraft、以前の保存済みA、復元待ち、再生中の一致判定を一つの`phase`で表さない。
失敗した再取得の案内と、保持している前回Captureの完全性も別の事実として表示する。
UIは上記から導いたview modelを読む。workerの状態を直接書き換えて復元や表示切替を成立させない。

## 4. Capture固有の位置対応

`verifiedWork`だけでは位置一致の根拠にしない。
CaptureBindingには、Capture IDと範囲、対象Versionの不変hash、sample rate/channel、対応anchor、照合根拠の範囲とalgorithm revisionを結びつける。
LiveBindingのhost/source anchorを参照して過去のCaptureをその都度ずらす経路を廃止する。

1. Capture中に得られた校正は、その校正が読んだ実入力範囲とCaptureの受理範囲が対応する場合だけCaptureへ結びつける。
2. Capture確定後にDAWのクリップを移動しても、保持済みAと既に検証済みの不変なBの位置関係は動かさない。現在の再生位置は別の対応として再確認する。
3. B未選択で取得したAや復元したAは、後の通常再生で取得音に一致する完全な4秒と既存のPCM校正を結び、Capture → Live観測 → Bの対応を検証する。Aが変わってこの橋渡しが成立しない場合は、現在A/Bを利用可能にしながら、根拠のない過去A/Bは共通位置と差分を保留する。
4. B変更時は既存の計測済み特徴とsource cacheを再利用し、必要な小区間だけを照合する。同じWorkという理由だけで時間軸を継承しない。
5. 対応は証拠が支える範囲を持つ。切貼り・途中の位置ずれ・曖昧な反復を単一offsetで覆わない。全体へ適用する場合も既存のwhole-song成立条件とCapture側の根拠を満たすことを要求する。

現在のAが変わっても、過去のAと不変なBの有効な比較まで破棄しない。
成立しない対応だけを保留し、Aの保持表示を残す。内部の再検証失敗でエラーを連発しない。
Capture保存時にはBindingの証拠も保存するが、復元した証拠はsource・format・schemaを検証してから使い、現在のDAWとの一致やB試聴開始は復元しない。
旧schemaに証拠がない場合もAの波形・値は残し、通常再生から再確認する。

位置情報は[JUCE PositionInfo](https://docs.juce.com/master/classjuce_1_1AudioPlayHead_1_1PositionInfo.html)で任意値として扱われる。欠損を0で補わず、既存のclock/PDC検証を維持する。
波形の見た目や要約値の近さをsample一致の証明へ格上げしない。

Captureの位置だけでなく表示Gainも、そのCaptureの根拠へ固定する。
CaptureGainReceiptの成立条件、原音表示のfallback、現在試聴との表示区分は[実装契約1章](reference_evidence_and_discovery_contract_20260914.md)に従う。
現在の再校正・B選択・復帰によって過去の波形やLUFS差を動かさない。Capture由来の全体LUFSを新しい試聴Gainへ使わない。

## 5. 変更検知を軽く、誤解なく提供する

通知では、まず同じDAW時間軸のsample区間を比較できるか確認し、その後に入力差を判定する。
同じ音であることを先に要求しない。EQ変更やクリップ差替えによって音が変わっても、同じDAW区間での差は判定できる。
内部状態は時間軸未確認／単位digest一致／raw差のみ／通知対象の差を区別する。
同曲の位置校正は別処理であり、時間軸やraw差の確認をその証拠へ流用しない。

表示用波形の間引きと照合用の時間単位を分離する。
現行は長いCaptureほどfingerprintの単位も大きくなるため、短い再生では比較が完了しない。
新しい通知索引は一定の時間単位を持ち、DAW sample区間を直接参照する。全曲の候補探索・PCM保存・再decodeをしない。

1秒単位・1単位64 bytes、最大2時間で7,200単位とする。形式はSHA-256と軽量なband/RMS/peak特徴へ固定し、最初のG0検証で精度と処理量を確認する。
索引、表示要約、bitset、位置・Gain Receipt、metadataを含む圧縮前の保存予算は[実装契約5章](reference_evidence_and_discovery_contract_20260914.md)を正本とする。最終的なencoded Capture領域の上限は1 MiB。
これは旧256 KiB上限の無条件緩和ではなく、照合索引の追加に伴う新schema専用の上限である。旧schemaの読み取り上限は維持する。
G0不合格時は方式と本計画を修正してからschema・UIへ組み込む。予算を黙って広げない。

- rawな不一致、音量・帯域・ダイナミクスの変化、位置の不確かさを内部で分ける。微小差を無視して「完全一致」とは扱わない。
- 利用者への通常表示は、対応範囲で確認した差を短く集約する。ディザーだけで操作変更を断定する表示や、毎callbackの通知を出さない。
- 通知閾値と解除条件は[実装契約2章](reference_evidence_and_discovery_contract_20260914.md)に従う。実入力のG0で検証し、UIの閾値を位置・音量校正の成立基準へ流用しない。
- 検知済み範囲は再生passと観測時点を持つ。設定を戻した場合は、その区間を再確認して表示を更新する。未再訪の区間まで一致へ戻さない。
- 提案する目標は、位置対応が既知の連続再生で、差を含む完全な照合単位を受理してから次のUI更新で表示すること。1秒単位では境界待ちを含め概ね1〜2秒を目安に実測し、未確認の条件で保証しない。

A欄の **A DIFFERS** と波形上の細い印を共通UIとする。tooltip/AXでは「Live audio differs from this capture in the marked range.」と説明する。
色だけに頼らず、点滅・モーダル・星・長い常設説明を追加しない。全5サイズで同じ意味を保つ。
既存の **CAPTURE AGAIN** を隣接させる。Captureの完全性は **PARTIAL** として別に保持し、差の表示で覆い隠さない。

既定の変更照合はReferenceを表示している間に行う。editorを閉じている間の常時監視を新たに増やさない。
閉じている間も明示Captureは従来どおり継続し、再表示時の再生から変更照合を再開する。
保持中の通知は最後に確認した事実として扱い、再表示では小さなLAST CHECK表示で鮮度を区別する。監視していなかった時間を確認済みとしない。

## 6. 保存・復元と表示範囲

CaptureStoreは、復元要求を受けた時点で上限付きpayloadと世代を保持する。
workerのdecode前でも保存要求にはその復元要求を返し、空の旧snapshotで上書きしない。
検証済みDocumentへの置換は世代一致を条件に一度だけ確定する。古い復元workerが新しい復元・取得を上書きすることを禁止する。
通常の音声callbackにdecode、保存の待機、ファイルI/Oを載せない。

schemaには完全性と終了理由を含める。旧Captureの`complete=false`は理由不明のPARTIALとして復元し、正常終了扱いしない。
壊れたCaptureや未知schemaでも通常A、B/C選択、他の保存設定を巻き込まない。
取得取消・失敗では前回の正常なDocumentを残す。host dirty通知は確定した保存payloadが読める状態の後に行う。

表示範囲には`fitCapture / manual / followLive`の意図を持たせる。
初期はfitCaptureで、取得範囲の成長に合わせて全体へ更新する。確定時も最終範囲を表示する。
利用者が範囲を指定したらmanualへ移り、進捗更新や完了で勝手に戻さない。サイズ変更とデータrevision変更を別に扱う。

## 7. 実装順序と対象

| 順序 | 完結させる責務 | 主な対象 |
| --- | --- | --- |
| G0 | 取得音に固定したGain、Bなし入力差、探索上限、保存サイズと処理量を小さく実証 | [実装契約4章](reference_evidence_and_discovery_contract_20260914.md)。新方式のschema/UI統合の前提 |
| 1 | CaptureDocument/StoreとAttemptを分離。schema、復元世代、完全性、確定通知を接続 | `ReferenceACaptureModel/Session/Codec`、`ReferenceComparisonSettings/Controller`、`PluginProcessorState`、`HyphaCaptureStateNotification` |
| 2 | CaptureBindingとCaptureGainReceiptを導入。投影cacheをCapture/source/位置/Gain各世代に結ぶ | `ReferenceACaptureRevisit/Projection`、`ReferenceComparisonCapture`、`ReferenceVisualBinding/Timeline`、既存content alignment |
| 3 | 一定単位の通知索引とLiveEvidence。DAW区間差と同曲位置校正を分離 | `ReferenceACaptureRevisit`、Capture worker、必要な測定/FFI境界 |
| 4 | typed view modelから範囲・変更表示・PARTIALを統一して描画 | `HyphaReferenceComparisonView/CapturedView/CaptureControls`、`PluginEditorReferenceCapture`、全5サイズ共通shell |
| 5 | 再現試験、境界試験、実機の一巡を実施して完成を判定 | 既存Capture native/UI/processor suites、Studio One/Pro Tools、Windows対象経路 |

各owned sourceは500行以下。既存巨大ファイルの変更責務は先に抽出し、UIへ判定ロジックを重複させない。
Kirin OSの配信、INSPECT、通常Aの0 dB、固定B Gain、PRE/POSTの測定・Recordは既存契約を維持する。
両Blindの排他と2枠Analysis上限へCapture/変更照合を一貫して接続し、第三の解析枠を作らない。
Audio Threadは既存の上限付きコピーとatomic通知のみ。照合・hash・集計・保存はworkerで処理し、UI更新は最大10 Hzとする。
queue合計2 MiBと既存の追加RAM目標16 MiBを基準に、active/held/restoreの同時保持、最大rateで実測する。

## 8. 合格条件

- レビュー4件を回帰試験にし、1秒移動後の誤比較、即時restore/saveの欠落、PARTIAL消失、0.1秒固定をすべて解消する。
- 復元A→復元Bの連続要求、worker遅延、保存の同時要求、取得中restore、旧schema、破損、上限超過、editorなし、instance複製を検証する。
- 同一音の移動、Gain/EQ/dynamics変更、編集取り消し、途中だけの切貼り、反復の曖昧さ、無音、ディザー、ランダム変調を使い、位置不明と音の差を混同しない。
- 一度の一致区間から未確認の全体を保証せず、未再生区間・監視休止・再openを含めて表示の根拠を確認する。
- Bなし取得→後からB、B切替・欠損、0/非0 PDC、rate/channel変更、両Blind中の秘匿・通知抑止・復帰を確認する。
- Bなし取得→全体EQ変更→B選択でも、入力差の通知と現在A/Bは成立し、根拠のない過去A/Bだけを保留することを確認する。
- 現在のGainを±6 dB変えても有効な過去の位置・Gain・A値は不変。原音fallback、MATCHED非表示、Receipt欠損・復元も回帰検証する。
- 5サイズ、キーボード、tooltip、AXでA/B/Cと表示選択を混同せず、fit/manual/follow、差、PARTIAL、再取得の動線を確認する。
- 最大保存サイズ、RSS、callback p95/p99、queue欠落、検知までの時間を記録する。30分実機再生とプラグイン操作を同じ検証で行い、過去のAAE原因を推測で解決済みにしない。

実装中は対象試験、最終状態で全体suiteを一度に集約する。成功済み全体suiteの無条件反復はしない。
Rust/FFI変更時のworkspace/clippy、ignored parity/pairingの全件ゲートもこの一括検証に含める。
nativeの再現解消と実DAW/PDC/Windowsの結果を分けて記録する。必要な検証が残る間は完成扱いにしない。
配布・配置を行う場合は別途既存の3チャネル規約を満たす。この計画作成ではbuild/install/releaseを行わない。
