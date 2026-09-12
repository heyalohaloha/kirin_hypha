# Hypha 構造的修復計画

作成日: 2026-09-12
対象: B-812〜B-835 の変更群と、B-835厳格レビューで判明した未修復境界。
基準版: B-835 / `63daa09a9f6113fb868cd107b9072aaa7bdb7924`。旧設計基準はB-832 / `59aefdfb9d18ebfe4b65aed7083bd98e7101a889`。
状態: 2026-09-13に構造修復と実機前の統合ゲートを実施。G0〜G2の自動証跡はgreen、G3のexact AAX実機確認は未完了、G4は対象外。
製品修正と検出ゲートは同じ候補へ統合した。AAXのbuild・配置・公開は、この記録時点では実行していない。

## 0. Daisukeの作業が必要になる箇所（先に明示）

現在のツールにはPro Toolsを操作できるnative UI経路が公開されていない。
この制約は自動修復・native統合テスト・診断用成果物の準備を止めないが、実機PASSの取得を止める。
別の操作経路が実際に利用可能になった場合だけ担当を変更し、利用できると推測しない。

| 時点 | Daisukeにお願いする作業 | Codexが先に準備するもの |
|---|---|---|
| 実機診断の直前 | 通常の作業sessionを保存し、検証専用コピーを使う。既存Pro Toolsの終了と、診断版の配置・復旧が必要ならその実施範囲を確認する | 元sessionを上書きしない手順、既存bundleの識別・退避・復旧手順。無断配置はしない |
| B-835基準版と修復候補の実機試験 | 実際にPro Tools Developerを起動し、チェックシート順に操作する。画面録画、必要な画像、診断出力を渡す | PRE/POST、候補識別receipt、番号付き手順、期待結果、採取手順、テスト音源。手動コード編集やコマンド組立ては依頼しない |
| Reference連携試験 | 許可済みfixtureで不足する場合だけ、保存Workと利用可能なReference素材を選び、Kirin OSから接続する | 正常・未接続・素材欠損の試験条件。本番Workや素材を削除・変更しない |
| 外部環境に不足がある場合のみ | SDK利用許諾の確認、OS認証ダイアログ、必要な署名環境の解除等。認証情報をチャットへ貼らない | 不足条件を特定してまとめて伝える。許可済みDeveloper診断経路と、署名必須の通常ホスト経路を区別する |

実機操作は「B-835基準診断」と「修復候補の受け入れ」の2つに分ける。
同じ全体テストを何度もお願いせず、手順・素材・採取をまとめる。ただし別commitの合格証跡は流用しない。
実機に到達するまでは上記の操作をお願いせず、Codex側で実機前に検出できる不良を閉じる。
ホスト固有の原因調査が先に必要な場合だけ、目的を限定した基準診断を依頼する。

重要: AAXのローカルPRE/POST Blindは現在fail closedである。
今回の合格条件は無効理由の可視性と、勝手に試聴を開始しないこと。操作すれば有効になるとは案内しない。
有効化はexact-range clock/PDC実証という別の依存条件を持つため、その解除まで完了したとは扱わない。
ReferenceのA/B試聴・Reference Blindと、ローカルPRE/POST Blindを別々に検証する。

## 1. 完了させる対象

解析の空表示、非表示画面の解析枠占有、有効な履歴の消失、時間境界の横ずれを構造から直す。
直近レビューの指摘だけで完了扱いにせず、既存の実機監査 PT-01〜PT-31 を受け入れ項目に残す。
Pair、Reference、Blind、5サイズの文字・操作導線、停止後の遅い欠けも対象に含む。
さらに、実機前の検証で不具合を通過させた原因を調べ、検出・合格判定・報告の仕組みも修復する。
製品側の発生原因と検証側の見逃し原因を別々に閉じる。詳細と合格条件は第11章に定める。

「ソースで確認」「自動再現」「実機で再現」「実機で修正確認」を分けて記録する。
画面画像の生成、テスト成功、過去ビルドの実機成功を、現候補の実機成功に読み替えない。

## 2. 確認済みの構造的な問題

| ID | 優先度 | 確認内容 | 対処する境界 |
|---|---|---|---|
| R-01 | P1 | FREQを離れる経路の `setPsbVisible(false)` は停止ではなく、絶対PSBの開始になる | 表示要求と解析実行の境界 |
| R-02 | P2 | M/S/TPすべてのfinite判定を全曲線に流用し、有効なSやCORRまで消す | 時刻の連続性と各指標の有効性 |
| R-03 | P2 | 非表示SHARPがタイマー経由のPair変化で解析を再開できる | editorの生存・表示と解析要求の所有権 |
| R-04 | P2 | 同じ正規化時刻でも主グラフとCORRの実描画領域が違い、境界が横ずれする | 時刻モデルと共通の描画座標 |
| R-05 | P1 | engine再生成後の復元が `writesEnabled=false` で拒否され、editorの送信済み要求との一致により再送されない | engineの準備状態・世代・要求・適用確認 |
| R-06 | P1 | M+Sの接続先を別解析へ故障注入しても追加の契約テストがPASS。必須の実接続試験がないまま自動検証完了と記録 | 接続先の独立した期待値・実接続ハーネス・完了判定 |
| R-07 | P2 | 共通X範囲へ変更したCORR曲線が、左に残した `CORR +0.00` の文字を貫通する | 共通時間軸と文字領域を同時に解くlayout |

R-01/R-03はC++の呼出経路、R-02は実際のJUCE painterと測定エンジンの既存テスト、
R-04は描画領域の計算で確認済み。これらのPro Tools実機修正確認は未完了。
R-05はB-835ソースの呼出順序で確認し、ホストでの再生成再現は未確認。
R-06はリポジトリ外の隔離コピーで故障注入し、既存の限定テストが成功終了することを確認した。
R-07は実製品のView/painterによるCORR=0の600/900幅の描画で確認した。これをPro Tools実機再現とは呼ばない。
R-05/R-07はPT-01/02およびPT-12/15の再検証へ結び、検出不足は既存PT台帳の見逃し原因へ追記する。

前回レビューの数値根拠:

- Mが有効な入力ではS変更による描画差が3,644px、Mが欠測の入力では0pxだった。
- 時間軸中点の主グラフ/CORR差は幅600で24.5px、幅900で33pxだった。
- 上記は限定したネイティブ検証の結果であり、ホスト上の全条件を保証しない。

既存監査表の説明も修正対象とする。
PT-01の「1 editorがSpectrumとPSBで2枠を取る」は、Rust側の単一lease構造と区別する。
確認済みなのは、停止要求のつもりで開始し、不要な1枠を保持し得ること。
PT-12の「全指標を共通finite判定で消す」は廃止する設計である。
PT-13の停止後の遅れは、現在の400ms設定だけで解決済みとせず、実測時系列で再判定する。

## 3. 変更しない境界

- 通常A経路のbit identical、0 samples latency。Audio Threadにalloc/lock/IOを追加しない。
- Meter Session、Record、Keep、TRACEの測定・保存契約。UI非表示でこれらを停止しない。
- TRACE公開に必要な完全な指標集合と、正しい同時刻PRE/POSTだけを使う差分契約。
- Reference試聴の独立した所有権とA/B制御。接続・復元だけでBへ切り替えない。
- AAXのローカルPRE/POST Blindは、exact-range clock/PDC実証までfail closedを維持する。
- Rustの既存解析枠数、単一lease管理、固定容量、既存worker。新しい管理系を二重に作らない。
- CE2226の通常/Jungle/VUの世界観。破線・点線なし。既存PRESENCE値は変えない。
- 既存の汚れたJUCEサブモジュール、テスト音源、未追跡資料は本作業の変更に混ぜない。

## 4. 解析要求の入口を一本化する

### 4.1 保存する選択と、今動かす解析を分ける

次の3つを別の状態として管理する。

1. 利用者の選択: domain/subview、LR/MID/SIDE/M+S、絶対/差分、測定context。
2. 実効解析要求: 実際に表示されるpaneと前提条件から決まる、なし/1種類の解析。
3. 適用状態: engineの世代、適用済み要求、準備中/枠待ち/稼働/取得失敗。

複数の独立したboolで「SpectrumもPSBも要求中」を表現できる構造をやめる。
例えば `AnalysisDemand` を判別可能な型にし、なし、通常Spectrum、同時M/S、
PSB絶対/差分、SHARP絶対/差分、LIVE、ATTACKの必要パラメーターを明示する。
不正な組合せは生成時に排除する。具体的な型名は実装時に既存命名へ合わせる。

PSBのfalseが絶対モードを意味する公開操作をなくす。
開始・変更は要求の設定、停止は要求の解除とし、停止にモード指定を持ち込まない。

### 4.2 入力を集める場所、判断する場所、適用する場所を分ける

選択・Pair状態・入力channel数・測定context・実効表示状態
→ 副作用のない要求判定
→ Processor内の唯一の適用窓口
→ 既存Rust Coordinator、という一方向にする。

- page切替、subview切替、Pair更新、VU表示、REF/Blind表示、再open、state復元を同じ窓口に通す。
- snapshot取得や描画処理は解析開始を直接行わない。必要な再判定は更新周期内の1箇所に集める。
- 同じ要求の再適用は何もしない。毎tickのworker再起動、履歴clear、session更新を禁止する。
- 枠待ちは適用失敗と混同しない。再取得は既存Coordinatorの再試行に委ねる。
- 適用に失敗した場合、要求と実状態を区別し、存在しない稼働状態をUIに出さない。
- engine再生成時だけ世代を更新し、現在有効な要求を1回再適用する。
- UIから見えない時のstate復元は選択のみ復元し、任意解析を勝手に開始しない。
- channel/context変更による正規化も同じ判定に集め、保存値と実解析の不一致を防ぐ。

### 4.3 表示の所有権を明示する

editorの生成世代を持つ軽量な所有権を用い、非表示・破棄でそのeditorの要求を解除する。
古いpopup、非同期callback、Pair更新が解除後に旧要求を復活させないようにする。
これはUI任意解析の所有権であり、Reference/Blindの試聴leaseとは別物である。

JUCEの自身の `isVisible()` だけではなく、親への所属、祖先の表示、peer、
VUなどによるpaneの置換を含む実効表示状態を使う。
`isShowing()`だけで全ホストのnative hideを検知できると仮定しない。
同梱JUCEのAAX wrapperはview作成/破棄を扱うため、そのイベントと実機の挙動を照合する。
ホストが通知しない場合に限り、既存の表示更新周期で補完する。高速タイマーは増やさない。
フォーカスがない、別窓に一部隠れた、というだけでは停止扱いにしない。

この解除はPOST表示が要求する任意解析に限定する。
PRE editorが閉じていても必要な、他POSTからの正当なPRE共有要求は維持する。

### 4.4 engine世代と適用確認をProcessorが一元管理する

B-835の `submittedAnalysisDemand == requestedAnalysisDemand` は、新engineでの適用成功を意味しない。
editorが再送要否を独自に決める構造を廃し、Processorの同一状態機械で保留・適用・再適用を決める。
最小の管理情報はUI owner世代、要求revision、engine世代、準備状態、適用済みtupleと適用結果とする。
これはC++側の適用確認であり、Rust Coordinatorの枠所有権を複製するものではない。

| イベント | 保持・無効化する状態 | 必須の挙動 |
|---|---|---|
| 初回生成／sample rate・channel変更で再生成 | 現ownerの有効な要求を保持。旧engineへの適用確認のみ無効化 | identity・role・Pair・format等の初期化後、最新要求を新engineへ一度だけ適用 |
| 準備中にpage変更・非表示・破棄 | 最新revision、または `none` に更新 | 古い保留要求を準備完了時に復活させない |
| 同じowner/revision/engineへ再通知 | 適用済みtupleを保持 | FFI開始・clear・worker再起動を追加しない |
| FFIが要求を拒否／handle生成失敗 | 成功として記録せず、待ち理由を保持 | 次の有効な準備・要求イベント、または既存周期での有界な再試行で回復 |
| Coordinatorが枠待ち | 受付済み要求と枠待ちという実状態を区別 | UIから連続再起動せず、既存Coordinatorの取得処理へ委ねる |
| 古いownerのcallback／復元通知 | 現owner/revisionと不一致 | 無視し、現ownerの要求も枠も解除しない |

`writesEnabled`を早めにtrueにするだけの修正は採用しない。
Audio Threadに公開する既存の初期化完了境界を維持し、解析適用可能な準備段階を非RT側で明示する。
`prepareToPlay`、遅延enable、state復元、UI要求、engine破棄を同じlifecycleへ接続する。
公開前後の順序をテストで固定し、未初期化handleをAudio Threadへ見せない。
snapshot読取りは開始副作用を持たず、再試行も高速timerや毎paintへの処理追加で実現しない。
計測・Recordの世代やresetをUI都合で変更しない。

## 5. 履歴を「時間」「指標」「配置」に分ける

### 5.1 共通にするのは時刻と再生区間

履歴のgeneration、run ID、採用済み観測のendpoint、clock種別から、
全時間レーンが共有する時間投影を1つ作る。
DAW位置のジャンプ、停止/再開、resetによる区切りはこの投影から全レーンに渡す。
時刻不明時は既存のaudio/session時間へのfallbackを明示し、DAW時刻を捏造しない。

M/S/TP/CORRの有効性は、それぞれの値と観測窓で判定する。
Mが欠けても有効なSは残す。CORRやTPをM/Sの欠測に巻き込まない。
同じrun内の指標固有の欠測は、その指標だけを途切れさせる。
有効値の削除や欠測の0埋め、停止区間の直線補間によって見た目を揃えない。
差分は検証済み同時刻の組を必要とし、表示修正を理由にその条件を緩めない。

平均値だけでなくmin/max、端点、hover、Captureにも同じ意味の区切りを適用する。
Record/TRACEの完全性判定はこのUI投影へ移さず、既存の厳格な契約を維持する。

### 5.2 全時間レーンのX座標を共有する

時間の正規化関数だけでなく、共通の左端・右端・幅をlayoutから一度だけ渡す。
各レーンはY軸と色だけを個別に持つ。ラベルの長さでデータ領域の開始位置を変えない。
同じ時刻の論理X座標は完全一致、ラスタライズ結果の差は1デバイスpixel以内とする。

150%などでCORRがPLRの横に半幅配置される条件も対象にする。
CORRを時間曲線として表示する場合は全幅の薄いレーンとし、主履歴と左右を揃える。
PLRはSession全体の累積事実として残し、時間曲線には戻さない。
補助情報の配置は既存領域内で調整し、設定行を増やさず、主測定領域を不用意に縮めない。
この配置案は5サイズの実寸検証を通して採用する。

R-07対策では、layout結果として共通timeline Xと各レーンのreadout/axis/data矩形を同時に返す。
CORR数値は既存CORRレーン内の上部の文字帯へ移し、その下を全幅のdata領域にする案を基準とする。
独立した設定行や白い囲みは増やさず、主グラフの高さをこの修正だけのために削らない。
小さい密度では既存の表示項目契約に従い、CORRを表示しない面へ無理に追加しない。
曲線と文字の矩形は重ならず、stroke/glowを含めた描画はdata矩形に収める。
文字の背後だけ曲線を消す、CORRだけX原点を戻す、文字を薄くする回避は採用しない。
絶対CORR=-1/0/+1、ゼロ交差、差分CORR=-2/0/+2、欠測、長い数値を含めて実描画を確認する。
時刻一致と、全レーンの最終描画座標・文字・軸の非交差を別々の合格条件にする。

### 5.3 停止後約1秒で欠ける現象を、時間列で検証する

診断buildで、callback最終到達、transport変化、最終採用endpoint、run境界、
worker公開、UI採用時点を同一テスト内で記録する。
Audio Threadでログを出さず、既存の非RT診断経路を利用する。

「新しい観測がない」「取得に失敗した」「指標が欠測」「新runへ移った」を分ける。
停止後に完了した正当な最終観測は残し、空のpollやliveness期限切れだけで過去を消さない。
区間を跨ぐ観測は既存契約どおり不採用とし、停止後の見かけの遅れとは分けて記録する。
再開後は実際の新run境界だけを切り、新しい有効観測から描く。
heartbeatや無音閾値を再び一律に短縮することを先に決めない。

## 6. 状態表示と操作導線も、実状態を正本にする

Pair/Reference/Blindを1つの「使える・使えない」にまとめない。
Pair接続選択、PREの在否・占有、同時刻データの準備、ライセンス、保存Work接続、
Reference素材準備、ホスト別Blind対応は、独立した事実として扱う。

既存のaccess/modelを再利用し、header・menu・REF paneが同じ事実から表示と操作可否を導く。
修復済みの箇所を一括で書き換えるのではなく、判定の重複と不一致がある境界だけを統合する。
再生中Pair変更禁止は接続故障と区別する。Reference待機と所有権未確認も区別する。
AAX Blindは未対応理由を示し、試聴可能と誤解させない。
利用者の明示操作が失敗した時は結果を伝え、背景の互換fallback失敗は従来どおり静かに扱う。

測定値・準備状態・説明文・単位を独立した表示要素にする。
LRAのWARMINGを測定値や音質評価と混同させず、準備中の単位を誤表示しない。
10Hzは主操作から外した状態を維持する。SIDEの符号はM/S座標の極性であり、良否判定にしない。
長い文字列を固定枠へ押し込まず、実フォントの幅と優先順位で割り当てる。
正常、待機、枠競合、欠測、操作失敗をすべて実寸で確認する。

## 7. 影響範囲と分割方針

以下は事前の対象一覧。実装前に各入口の参照を再検索し、漏れなく接続する。
巨大ファイルの無関係な責務まで分割しない。新規owned sourceは500行以下。
巨大ファイルを変更する場合は対象責務の抽出を先行コミットにし、baselineをratchetする。

| 責務 | 主な既存ファイル（リポジトリ相対） | 分割・確認範囲 |
|---|---|---|
| 解析要求・再生成 | `juce_shell/src/PluginProcessorAnalysis.cpp`, `PluginProcessor.h`, `PluginProcessor.cpp`, `PluginProcessorDisplayState.cpp`, `PluginProcessorValidation.cpp` | 型付き要求、唯一の適用窓口、engine世代、診断 |
| editorの入口 | `juce_shell/src/PluginEditorAnalysis.cpp`, `PluginEditor.cpp`, `PluginEditorObservatory.cpp`, `PluginEditorLifecycle.cpp`, `PluginEditorMeter.cpp`, `PluginEditor.h` | 選択/Pair/表示/破棄/復元の統合 |
| 置換pane・接続表示 | `juce_shell/src/PluginEditorLocalBlind.cpp`, `PluginEditorReference.cpp`, `PluginEditorMenu.cpp`, `PluginProcessorPairing.cpp`, `HyphaReferenceAccessPanel.h`, `HyphaOsAccess.h` | 解析所有権との分離、同じ実状態からの表示 |
| 履歴の意味・座標 | `juce_shell/src/HyphaTimeHistoryPainter.cpp`, `.h`, `HyphaTimeAxisContract.h`, `HyphaObservatoryHistoryInteraction.cpp`, `HyphaObservatoryViewLayout.cpp` | 時間投影と指標可用性を描画から抽出 |
| 同じ履歴を使う出力 | `juce_shell/src/PluginEditorCapture.cpp`, `HyphaCaptureHistoryPainter.cpp`, `HyphaCaptureHistoryAnalysis.cpp` | Captureとhoverの時刻整合、必要な共通部だけ利用 |
| 既存Rust正本 | `crates/kirin_measure/src/spectrum_exchange_control.rs`, `meter_history.rs`, `meter_history_decimation.rs`, `meter_delta_history.rs`; `crates/kirin_hypha_ffi/src/audition_admission_ffi.rs` | まず変更せず確認。UI所有権と試聴所有権を混ぜない |
| 検証 | `juce_shell/tests/TimeHistoryContractTest.cpp`, `TimeHistoryPaintProfile.h`; `crates/kirin_hypha_ffi/tests/juce_lifecycle_wiring.rs`; 関連access/render契約、build登録 | ソース文字列検査に加え実状態の試験 |
| 見逃し防止・実行判定 | `juce_shell/tests/UiFeatureContracts.h`, `ui_render_contract_test.cpp`, `ObservatoryCompositeContractTest.cpp`, `TypographyContractTest.cpp`; `crates/kirin_hypha_ffi/tests/support/juce_presentation_contract.rs`; `juce_shell/CMakeLists.txt`, `scripts/test_release_source.sh`, `.github/workflows/ci.yml` | 実入力、実接続、合格条件、実行inventory、部分実行の明示 |
| 正本・監査 | `docs/hypha_pro_tools_full_issue_audit_20260912.md`, `hypha_meter_product_contract_20260831.md`, `hypha_invariants.md`, `scripts/source_line_budget.tsv` | 誤った説明の訂正、証跡と実装境界の一致 |

Rust/FFIの変更は、既存APIで一貫した適用を保証できないことを確認した場合に限る。
今回の追加対象は `HyphaAnalysisDemand.h`、`AnalysisDemandContractTest.h`、
実Editor/Processorと本物のFFIをリンクするnative統合target、履歴layoutの抽出先、
既存release-source gateの結果集約である。新規モジュール名は実装時に既存配置へ合わせる。
AAXは `scripts/build_aax_universal.sh` と既存receipt/検証スクリプトを再利用する。
複数APIを順に呼ぶ場合の途中状態も検証し、必要なら既存Coordinator内の遷移として直す。
別のlease管理、プロセス全体の強制解放、全worker再起動で回避しない。

## 8. 実装と検証の順序

### A0. 検証が通過させた経路の監査

第11章に従い、各不具合の混入時点と当時の入力・期待値・実行記録・完了判定を照合する。
実機前に落とすべき項目と実機固有の残余を分け、独立した合格条件を先に固定する。
修復後の実装に合わせてテストの期待値だけを書き換える進め方を避ける。

### A. 再現と境界の固定

R-01〜07を既知不良で失敗するテストにする。PT-01〜31の既存証跡と未確認を整理する。
新しい検証が既知の不良を実際に落とすことも確認してから、製品修復へ進む。
依存する責務を挙動不変で抽出し、機能変更前にレビュー可能にする。

### B. 解析所有権の統合

単一の要求モデル、適用窓口、全editor入口を一体で移す。
旧bool制御への迂回経路が残らないことを確認し、engine再生成・破棄まで含めて完成させる。

### C. 履歴と状態表示の統合

共通時間投影、指標固有の有効性、共通plot領域をまとめて導入する。
Pair/Reference/Blindや欠測表示の重複判定も点検し、確認された不一致を同じ候補内で解消する。

### D. 統合候補の受け入れ

各段階はコミット単位のレビュー境界であり、途中版を実機へ順次配布する計画ではない。
全対象を揃えた1候補に対し、統合テストを実行し、第11章の実機前ゲートを満たしてから実機検証を行う。
新しい実機問題は同じ台帳へ登録し、原因・対処・再検証を経ずに完了へ移さない。

工程の完了は次の成果物で判定する。手順を書いたことを実行済みとは扱わない。

| Gate | 成果物・通過条件 | 未達時の扱い |
|---|---|---|
| G0 証跡と再現 | B-835の固定識別、PT全件の現状、R-05〜07を含む失敗再現、見逃し分類 | 原因未確定を明示して診断を続ける |
| G1 構造修復 | lifecycleとlayoutの単一実装、全入口の移行、実接続・故障注入・描画検査の成功 | 実機前に直せる不良として差し戻す |
| G2 実機前受け入れ | 必須inventory、同一候補の統合結果、5サイズのデータ入り画像と実寸レビュー、性能予算 | 自動検証完了と呼ばず、不足suiteを特定 |
| G3 ホスト受け入れ | 修復候補exact AAX、PT全件、0 samples、A経路、停止1秒超を含む証跡 | ツール未公開・Daisuke操作待ちも未完了として残す |
| G4 公開適格性（別途依頼時） | 同一release commit/versionの3チャネルと既存署名・notary・Windows検証 | G3や診断buildの成功を公開readyへ読み替えない |

B-835基準診断はG0の証拠取得であり、既知不良があるままG2/G3を通す例外ではない。

## 9. 合格条件

### 9.1 自動検証

- 解析状態の全有効遷移と、非表示・破棄・Pair変化・engine再生成を組み合わせる。
- Demand単体試験は `none` からだけでなく全有効状態間を列挙し、期待するFFI種類・channel・順序・停止を検査する。
- 期待する種類の表を製品の `apply()` と同じswitchから生成しない。API失敗を各段階で注入し、成功扱い・古い要求の復活を防ぐ。
- 実Processor/FFIを使う複数instance試験で、2枠稼働時の3台目待機、1台解放後の取得を確認する。
- 実Editorの操作と既存timer処理を通し、実FFIの要求状態・Coordinator所有者・世代・測定出力・表示snapshotまで追跡する。
- 48→96 kHz、stereo→mono→stereo、同format再prepare、準備中のpage変更・hide・破棄、旧owner callbackを含む。
- 解析出力はFFTの既知ピーク、M/S分離信号等で種類を識別し、「何か1回enableされた」「画像が変わった」だけで合格にしない。
- 同format再prepareは不要な再生成なし、新engineでは同じ要求でも適用1回、再表示時には最新要求だけが戻ることを測る。
- 非表示SHARPのPair変化で枠が復活しない。FREQ退出で絶対PSBが始まらない。
- 同じ要求を繰り返してもworker数、解析世代、clear回数が増えない。
- M欠測/S有効、TPのみ有効、CORRのみ有効、全欠測、mono、逆相を含む履歴fixtureを使う。
- エンジンが実際に生成する400msと3s窓の違いを入力にし、正しいSが描かれることを検証する。
- 停止/再開、seek、run/reset境界、DAW clockなし、長期履歴の解像度境界を検証する。
- 共通run境界のX一致と、指標固有の欠測が他指標を消さないことを別々に検証する。
- CORRのゼロ線・曲線・数値・軸の最終描画領域が非交差であることを検査する。座標helperだけの試験で代用しない。
- 全5サイズ、PRE/POST、通常/Jungle、実フォントとfallbackで文字欠け・重なり・操作欠落を確認する。
- 解析、Reference等は専用のデータ入りcomponentで検証し、空のshell画像を合格根拠にしない。
- 既存CPU・描画時間・固定容量の予算を維持する。予算緩和で通さない。

時間投影の再計算は新snapshot/axis/layout変更時に限り、毎paintの全履歴コピーを増やさない。
非表示時は任意解析負荷が解放されることを測り、常時計測の負荷とは分ける。

### 9.2 全体テストを何度も繰り返さない

実装中は変更境界の失敗テストと関連suiteだけを実行する。
統合候補でworkspace test、clippy、JUCE/native UI、source契約・行数予算を1セット実行する。
その後に変更した場合は影響suiteを再実行し、未変更部分の証跡との対応を明記する。
RT/ABI/共通基盤まで変更した場合は、広い再検証を省略しない。

`kirin_hypha_ffi`変更時はignored parity / pairing_candidatesの両suiteも必須。
件数はその時点で列挙して確認し、workspace greenだけで代用しない。
実装中は解析要求、TIME履歴、JUCE配線のfocused testとPRE/POST Debug buildだけを使う。
候補確定後に、この節の統合ゲートを1回実行する。

### 9.3 実機検証

#### 基準診断と修復確認を分ける

引き継ぐ未処理事項:

> B-835 exact sourceからAAXを作り、Pro Tools DeveloperでPT-01〜PT-31、5サイズ、
> Pair・Reference・Blind、停止1秒超→再生、0 samples、A経路連続性を確認する。
> 現在の操作環境ではPro Toolsのネイティブ操作が公開されていないため、実機PASSとはしていない。

この要求は削らず、B-835の**基準診断**として全項目の観測結果を残す。
基準版は既知不良があるため、FAILも診断結果として保存し、合格するまで基準版を改変しない。
修復後は新しいexact commitのAAXを別artifactとして作り、同じチェックシートで受け入れる。
新しい修正を混ぜた成果物をB-835と呼ばず、B-835の結果を新候補のPASSへ流用しない。

#### Codex側の事前準備

1. B-835と修復候補を、それぞれ隔離したソース・build出力で固定する。現worktreeのJUCEや音源の変更を混入させない。
2. 各commitのsubmodule revisionと追跡済みJUCE patch適用内容を固定し、FFI/native binaryを同じ候補から作る。
3. SDK利用条件とDeveloper診断経路を確認し、既存AAXスクリプトの明示的diagnosticモードを使う。通常版NFRをDeveloper executableと同一視しない。
4. PRE/POST hash、commit/B番号、version、architecture、build種別、font、署名状態、配置先をreceiptへ記録する。
5. 別candidateのプラグインが同時に探索されないことを確認する。配置が必要なら第0章の確認後に退避・配置・復旧を行う。
6. 音源のhash/format/期待値を検査し、既存の変更済みS-1を無検査で使用しない。停止・無音・部分欠測のfixtureを揃える。
7. チェックシートにPT ID、前提、操作、期待結果、観測値、証跡path、判定、candidateを用意する。既存PT台帳を正本にする。

#### 実機チェックシートの必須範囲

Pro Tools Developerで、読み込んだPRE/POSTのexact commit、version、architecture、
署名/診断buildの条件を記録してから検証する。既存の作業sessionは変更せず、専用コピーを使う。
起動中の通常Pro Toolsが使う署名済みbundleを無断で置換しない。

1. 複数PRE/POST、名前重複、未接続、別POSTが占有、再生中/停止中のPair操作。
2. FREQ LR/MID/SIDE/M+S/PSB、SHARP、LIVE、ATTACK、非解析pane間の遷移。
3. pane切替、VU表示、window非表示/再表示、破棄/再open、session再読込、channel/rate変更。
4. 0.1/0.4/1/1.2/3秒の停止を含む停止・再開とseek。実機では停止直前から再開後まで連続記録し、実測停止時間も残す。特に1秒超の停止中に遅れて欠けるかを確認する。
5. Referenceの未接続/接続/素材欠損/試聴終了、Reference Blindの開始・終了・中断、Aへ戻ること。ローカルPRE/POST BlindはAAXの明示的な無効理由とfail closedを確認する。
6. 5サイズの全domainとmenu、normal/Jungle、長い名前、枠待ち/欠測状態の可読性。
7. 通常Aの音声透過性と0 samplesを表示履歴とは別に検証し、安定動作と負荷を確認する。
8. 同じ候補のAU/VST3のライフサイクルをsmoke確認する。WindowsはRunbookを読んでから検証する。

通常Aの確認は、ホストの0 samples表示、実測の遅延、透過性、再生中の連続性を別項目にする。
UI切替・resize・hide/reopen・解析枠競合中もAが途切れないことを録音またはbounceの数値で確認する。
明示的B試聴中をAのbit-identical試験へ混ぜず、B終了・失敗・offline時のA復帰を別に確認する。
整数bit depthへ書き出した場合は既知の量子化誤差を報告し、残差ゼロと偽らない。
修復候補で異常が出たらPT台帳へ戻し、修正後は影響試験を再実行する。証跡の依存を説明できなければ統合ゲートも取り直す。
Intelホストの成功はarm64やWindowsの実機成功を意味しない。未確認platformは未確認と残す。

自動native UIテストは、このPro Tools実機確認の代用にしない。
実機操作手段が利用できない場合は未完了として残し、実機修正済みと報告しない。
クラッシュ報告は当該buildのログ・再現条件で調べ、未帰属のまま解決扱いにしない。

## 10. 全問題台帳を閉じる条件

| グループ | 既存ID | 完了に必要な証跡 |
|---|---|---|
| 解析の可用性・所有権 | PT-01, 02, 18, 23, 25, 31 | 実解析データ、複数instanceと非表示遷移、空/競合状態 |
| Pair / REF / Blind | PT-03, 05, 06, 28 | 実際の操作可否、前提条件、失敗/未対応の説明 |
| 測定の意味・時間 | PT-07〜13, 21 | 窓の違い、停止/再開の時間列、PLR/SIDEの意味と表示 |
| レイアウト・操作品質 | PT-04, 14〜17, 19, 20, 22, 24 | 5サイズ、実フォント、tooltip/popup、画面切替 |
| 安全・性能・試験の信頼性 | PT-26, 27, 29, 30 | A経路・latency、クラッシュ帰属、負荷、再現可能なfixture |

既存項目を理由なく次期版へ送らない。検証できない項目は未完了とblockerを明記する。
正本の監査表、実装、テストの期待値が同じ契約を示し、見逃し防止の検証が有効に働いて初めて修復完了とする。

本計画は公開リリースを実行する許可ではない。
後続でrelease build/notarize/installを行う場合はLS pkg準備と既存Runbookに従う。
公開完了には同一version/commitのLS macOS、HP macOS、署名済みWindows installerの
3チャネルすべての検証が必要であり、Windows未完了を暗黙に除外しない。

## 11. 実機前にNGを通した原因の調査と再発防止

### 11.1 調査の目的と証拠の境界

「実機でしか分からなかった」で一括処理しない。
実装の欠陥とは別に、なぜ入力が不足したか、なぜ検査が不良を見抜かなかったか、
なぜその結果で次工程へ進んだか、という検証と判断の経路を調べる。
実機前に検出可能だった問題は実機前の必須ゲートへ移す。
実機で判明した条件は、ホスト固有部分を除いて再現可能な自動試験へ戻す。

今回確認したのはB-832時点のテストコード・build登録・CI条件と一部のgit履歴である。
これだけで「当時CIが実行されなかった」「全件成功と誤報した」とは断定しない。
ユーザーの画像に表示されたversionだけでも、読込バイナリのexact commitは特定できない。
実機前の変更群、実機発見後のB-832修復、新たなレビュー発見を時系列で分ける。

### 11.2 コードから確認できた検出の穴

| ID | 確認した事実 | その検証では保証できないこと | 必須の補強 |
|---|---|---|---|
| Q-01 | `ui_render_contract_test.cpp`は `releasesSlot` の判定を検査。`juce_presentation_contract.rs`の該当検査は `visibilityChanged` 等のソース文字列の存在確認 | 呼出先の意味、実際のlease解放、非表示後の再取得。falseで絶対PSBを開始する呼出しも判定関数の成功と両立する | 実editor→Processor→FFIの遷移後に、稼働種類・枠所有者・世代を観測する |
| Q-02 | `TimeHistoryContractTest.cpp`のfixtureはM/S/TP/CORRすべてfinite。画像全体の差分量も使用 | M欠測/S有効を消しても、この入力では発見できない。別の指標や凡例だけの変化でも画像差は出る | 指標別の欠測組合せと、対象曲線の描画領域に限定した検証 |
| Q-03 | 同テストの時刻検査は `normalizedX == 0.1`。5サイズ確認には画像全域のalpha確認や補助情報の画像差を使用 | 正規化後に異なるplot矩形へ写像した際の横ずれ、文字同士の衝突 | 主グラフ/CORRの実X座標、文字の実配置、run境界を比較する |
| Q-04 | `ObservatoryCompositeContractTest.cpp`は部品を生成し、解析済みsnapshotを直接注入。`KirinUiRenderContractTests`のbuild対象に実PluginEditor/PluginProcessorは含まれない | 解析の起動・枠競合・タイマー・状態復元からsnapshot取得までの接続。部品にデータを渡せば描けることと、利用時に届くことは別 | 部品テストは残し、実接続を通す小さな統合ハーネスを追加 |
| Q-05 | B-832でPSB文字領域の明るいpixel検査が追加された。直前版の該当検査は切替状態や文字幅が中心 | 文字列が存在し幅が足りても、実際に見える色で描かれるとは限らない | 実文字の存在・clip・コントラストを確認。数pixelの明るさだけで可読性を認定しない |
| Q-06 | `engine_tests.rs`にはB-605以来、M欠測時にもSが有効な実測テストがある。一方、履歴描画fixtureへその条件が引き継がれていない | 測定coreの契約とUIの合格条件の不整合。層ごとのgreenが製品の正しさを保証しない | 測定出力→履歴→FFI→描画の境界試験と、指標の意味を根拠にした期待値 |
| Q-07 | `UiFeatureContracts.h`の `--product-entry-only` はTIME履歴等を実行する前に正常終了。CIの主要jobはPR/manual/指定commit messageに限定 | 部分実行や条件によるskipを含む結果から、全機能を検証済みとは言えない | 実行対象・非対象・結果・候補識別を明示する集約判定。過去に誤認があったかは記録を照合 |

既存の `test_release_source.sh` にはnative suite、CTest inventory、ignored suiteの確認がある。
検証基盤が存在しなかったわけではないため、この正規経路を拡張し、別の「全部検証」スクリプトを乱立させない。
上記は検出力の不足を示す確認済みの例であり、全項目の過去の通過原因が確定したという意味ではない。

B-835レビューによる追加の確認:

- Q-08: `AnalysisDemandContractTest.h` は `enabledKind` を記録するが検査せず、adapterはすべて成功を返す。
  M+Sからabsoluteへの誤配線を入れてもPASSした。FFI種類の期待値、失敗経路、実接続の検査が必要である。
- Q-09: B-835にはengine世代に結び付いた適用確認がなく、復元処理の受付前呼出しをテストが通していた。
  `none` からの要求試験やowner単体試験だけでは、実Processorの初期化順序を検証できない。
- Q-10: 共通X関数の成功ではCORRと文字の非交差は証明できない。CORR=0の実描画で重なった。
  座標helperの試験に加え、実layoutとpainterの最終出力を検査する。

したがって「15状態の単体試験がPASS」「native suiteが成功」から「構造修復の自動検証完了」への判定を撤回する。
既存成功ログは限定証跡として残し、必要な試験がなかったことと、既存試験が失敗したことを混同しない。

### 11.3 全不具合に「発生原因」と「見逃し原因」を記録する

既存PT台帳へ、次の項目を追加する。別台帳への二重管理は行わない。

- 混入したcommit/初めて観測したbuild、修復commit、関連する実機画像またはログ。
- 製品側の発生原因と、その根拠。未特定は未特定のまま残す。
- 当時あったテスト、その入力、期待値、実行された範囲と結果。
- 見逃し分類: 入力不足／期待値誤り／層間接続なし／表示検査不足／未実行・filter・skip／候補不一致／結果の解釈・報告／実機固有。
- 原因を裏付ける証拠、証拠がない部分、追加した検出試験、NG版で失敗する証跡。
- 実機前に防げる範囲と、依然として実機で確認する範囲。

git履歴、保存されたテスト出力・画像、build設定、実行記録、配置receiptを対応付ける。
記録が残っていなければ推測で補完せず、「当時の実行は追跡不能」と分類し証跡不足を是正する。
修復のたびに入力・期待値・閾値も変わっていた場合は、独立した製品契約から妥当性を再審査する。
固定文字列の更新と測定意味の変更を区別し、実装・文書・テストの三者一致だけを正しさの根拠にしない。

### 11.4 実機前の検証を3層にする

1. 意味の検証: 測定窓・欠測・run境界・操作前提を、製品契約と実入力から検証する。
2. 接続の検証: 実editor/Processor/FFIを使い、操作→実行状態→測定出力→表示まで確認する。
3. 見た目と操作の検証: 出荷側のlayoutとpainterで5サイズを描画し、文字とヒット領域を確認する。

接続ハーネスでは生成、表示、page移動、timer更新、Pair変化、非表示、再表示、破棄、
engine再生成を順に流す。必要な解析枠は実Coordinatorへ接続し、結果をmockだけで代用しない。
時刻・Pair通知・表示イベントの順序は決定的な入力にし、sleepによる偶然の成功を避ける。
共有ファイルと設定は隔離したテスト領域に置き、利用者のsessionやPairを変更しない。
外部の音声入力・clock・通知順序だけをfixture化し、製品のlifecycle・FFI・Coordinatorをmockへ置換しない。
診断の観測口は非RT側に限定し、Audio ThreadからログやファイルI/Oを行わない。
既存のキャッシュ・増分buildを共用し、巨大Processorを試験用にもう一つ実装しない。

見た目は、文字列の存在、最終描画時のfont/colour/clip、実際の割当幅、隣接要素との交差、
選択状態、実際に押せる領域、曲線の終点と時刻座標を対象にする。
画像のalpha充足、明るいpixel数、画像全体の差分は補助検査として残すが、可読性の合格条件には単独で使わない。
製品部品から得る実配置と実描画を用い、テスト内に製品と別のlayout実装を複製しない。
自動検査に加え、データ入り/欠測/長い名称を含む実寸コンタクトシートを目視レビューする。
用語の分かりやすさ、高級感、PLRの情報価値はpixel検査では判定できないため、製品レビュー項目として残す。

### 11.5 テスト自体の検出力を確認する

既知不良版で新テストが失敗し、修復版で成功することを同じ入力で示す。
履歴を消す、解析を解放しない、CORRのX原点をずらす、文字を背景色で描く、非表示後に再取得する、
という今回の代表的な不良を一時的に戻して、対応する検査が落ちることも確認する。
これは限定した故障注入であり、全repositoryを対象とした重いmutation testingは行わない。
既知不良の注入は隔離した検証環境内だけで行い、製品commitや配置候補へ混入させない。

追加の必須注入はM+Sの誤配線、準備完了前の復元、engine世代の適用確認無効化漏れ、
CORR数値をdata領域へ戻す、API拒否を適用成功と記録する、の5条件とする。
各注入に検出すべきtest IDと失敗理由を対応付け、元へ戻した候補で同じ入力が成功する証跡を残す。

失敗理由は対象の不具合であることを確認する。コンパイル失敗、fixture不在、別のassert失敗は検出証拠に数えない。
閾値緩和、golden画像の無審査更新、同じrunの都合の良い再試行だけでgreenへ戻さない。
描画の決定性・実性能・意味の正しさを別々の検査にし、テスト不安定を理由に製品検査を外さない。

### 11.6 実行されなかった検証を合格にしない

必須テスト一覧と実行結果を既存のgateに結び付け、選択0件、実行されない分岐、
不足target、古いbinary、異なる候補、途中中断は実機前ゲートの合格に数えない。
focused実行は有用な局所証跡として残すが、その結果を「UI全体PASS」と表示しない。
既存のCTest inventory確認を維持し、1つのnative実行ファイル内で走ったsubsuiteも識別可能にする。

記録にはcommit、source差分の有無、JUCE revision/patch、build config/architecture、
FFIとnative binaryの識別、対象suite、pass/fail/skip/未実行、出力の保存先を含める。
集約結果は「ソース検証」「native統合/表示」「ホスト実機」「配布適格性」を分ける。
集約はG0〜G4ごとの必須test IDとreceiptを照合し、未実行・0件選択・途中終了・候補不一致を成功から除外する。
各PT項目の「修正実装」「自動検出」「native修正確認」「実機修正確認」を別欄にし、一つの完了印にまとめない。
この一覧と結果集約は既存release-source gateへ組み込み、ドキュメント上の自己申告だけで次工程へ進めない。
通常pushで重いCIがskipされる運用は維持できるが、必須検証の欠落を完成や配置準備完了へ読み替えない。
CI設定だけで過去の実行を推定せず、候補ごとの実行記録を根拠にする。

### 11.7 実機前の完了条件とコスト管理

- PT全件に実機前検出可能性と見逃し分類を記録する。原因未確定・記録不足を隠さない。
- 実機前に再現できる既知の不良が残っている候補を、通常の受け入れ実機検証へ送らない。
- R-01〜07と代表的な文字不可視を、新しい自動検証が実際に検出する。
- 実入力の部分欠測、複数instance、非表示中イベント、実座標/実文字を検証する。
- 必須検証のfail/skip/未実行を含む候補に、総合PASSを出さない。
- 実機では残ったホスト固有条件を重点確認し、必要な最終回帰も行う。実機そのものは省略しない。

ホスト固有の原因を掴むために実機が先に必要な場合は、目的を限定した診断として別記する。
診断の実行を受け入れ合格とは扱わず、取得した条件を自動試験へ持ち帰る。
追加するのは検出力のある小さい入力・状態遷移・境界試験であり、同じ全体suiteの反復回数ではない。
共通ハーネスと既存の増分buildを再利用し、統合ゲートをまとめて実行する第9.2節の方針を維持する。

## 12. 実施結果（2026-09-13）

### 12.1 構造修復

- Processorが要求、engine世代、準備状態、適用済み要求、拒否後の有界retryを一元管理する。
  editor側の送信済み判定を廃止し、engine再生成後も同じ要求を新世代へ再適用する。
- 実出荷C ABIの関数ポインタを型へ固定したadapterを追加した。M+Sをabsolute等へ誤配線すると、
  独立した期待route、出荷adapter型、実Rust engineのC ABI試験のいずれかで失敗する。
- TIME履歴のcontent、主plot、共通timeline X、PLR、CORRのreadout/data/axisを1つのlayoutへ集約した。
  CORR数値帯と曲線領域は非交差で、主履歴とCORRは同じ左右端を使う。
- 5サイズの通常/差分を実データで描画し、300/375では既存のcompact表示、450/600/900では
  PLR/CORR補助レーンを確認した。監査で見つけた差分時刻ラベルの固定幅clipも可変割当へ直し、
  実フォント必要幅を5サイズ契約に加えた。

### 12.2 見逃し対策

- 既知の5 mutation、M+S誤配線、準備完了前の適用、engine世代無効化漏れ、CORR文字/曲線重なり、
  API拒否の成功記録をsource上で個別に注入し、各detectorが対象理由で失敗することを確認する。
- Demandの全active遷移、API拒否、engine destroy/create/ready/retryをnative契約へ追加した。
  実C ABI側ではPOST stereoの全解析routeと、PRE/mono/nullの拒否を実engineで確認する。
- `kirin_analysis_demand_contract`をCTest inventoryへ独立登録した。focused実行をUI全体PASSと
  混同できない名前と入口にし、既存release-source gateから必須実行する。
- CORRはhelper座標だけでなくproduction painterのpixel非交差を検査する。差分時刻ラベルも
  文字列存在ではなく、実fontの必要幅と製品側割当幅を比較する。

### 12.3 実行結果と残る境界

- PRE/POST Release VST3 compile: pass。
- Release native CTest inventory: 15件、15/15 pass。
- `kirin_measure`: main 1,454 pass / 9 intentional ignore。FFI main 88/88 pass。
- realtime ignored gate: parity 20/20、pairing candidates 6/6 pass。xtask 140/140 pass。
- Release性能probe、source/line-budget、format、public-history、clippy `-D warnings`: pass。
- 統合wrapperは1回だけ実行し、`release source contract: PASS`で完了した。その後の見た目監査で
  発見した差分時刻ラベル修正は、TIME focused Release契約と5サイズ画像だけを再実行してpassした。
- 自動試験は実Processorの初期化順、出荷adapter、実Rust C ABIを相互に固定するが、Pro Toolsの
  native window lifecycleそのものではない。G3はB-835基準版と修復候補のexact AAXを分離し、
  第9.3節の全項目をDaisukeがPro Tools Developerで確認するまで未完了とする。
- Reference BlindとローカルPRE/POST Blindは別項目である。後者はexact-range clock/PDC実証まで
  fail closedを維持し、有効化済みとは扱わない。
