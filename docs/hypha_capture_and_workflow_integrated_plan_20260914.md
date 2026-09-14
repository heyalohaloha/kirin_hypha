# Capture Aと操作導線 — 統合修正計画

Date: 2026-09-14
Status: 提案の精査と実装計画。製品変更・新たな実機検証は未実施。
Source baseline: B-877 `9d7c3bb2`、`codex/reference-abc-delivery`。
Input: 利用者が提示したH01〜H08改善案。調査対象は旧main `9cb40e56`、関連OSは`27dee5b7d`。

目的は、正しく比較できる根拠を維持し、準備・聴取・復帰で利用者が迷う箇所を減らすことである。
本書は[Capture A構造修正計画](reference_capture_structural_repair_plan_20260914.md)へ操作導線の修正を接続する。
Captureのデータ構造・位置対応・保存予算は同計画を引き継ぐ。
H01〜H08は下記の修正を加えて計画へ含めるが、提案文の全項目を実装済み・承認済みとは扱わない。
今回はソースと既存のレビュー結果を確認した。ネイティブ画面、実出力、初見利用者の改善効果は検証していない。

## 1. 精査結果と、現行へ合わせる修正

| 対象 | 判定 | 計画に含める内容 |
| --- | --- | --- |
| H01 測定値の意味 | 推奨 | 既存の見出し・凡例を対象／絶対値または差分／範囲へ整理。説明段は増やさない |
| H02 単一PRE接続 | 条件付き推奨 | 相手を明示した1操作接続。候補取得のキャッシュ、上限、確定時再検証まで一体で設計 |
| H03 操作段 | 修正して推奨 | 通常POSTの600/900ではRESET枠をBlindへ置換する案を基本とする。KEEP追加は実寸比較で判断 |
| H04 Blind進行 | 修正して推奨 | 区間・各Sourceの完了・次の操作を固定位置へ。300％で使用し、比較用Contextの独立を維持 |
| H05 Close省略 | 推奨 | EndとReturn to Liveを残し、同じ試聴の音声復帰確認後に通常画面へ戻す |
| H06 失敗からの再開 | 既実装部分を維持して拡張 | B-876の再取得を再実装せず、開始条件の修復導線と正確な失敗理由を補う |
| H07 TIME直接選択 | 推奨 | 順送りを残した選択メニュー。既存履歴の表示範囲だけを変える |
| H08 Reference整理 | A/B/Cへ修正して推奨 | B＝Version、C＝Checkのプルダウンは主面に残し、まれな設定だけ補助面へ |

旧案との重要な差は次のとおり。

- B-876には、出力切替前の失敗からの`CAPTURE AGAIN`と資源解放後の`canRecapture`がある。旧案の4→1を新規の改善実績として数えない。
- Blind開始条件の要求は現在もboolで、複数の失敗が`ANALYSIS SLOT NOT AVAILABLE`へ集約される。H06の原因分類は必要である。
- B-876のBlind用Contextは通常メーターから初期値を受け取り、その比較内で変更できる。通常の表示スケールやATTACKへ波及させる旧動作へ戻さない。
- 通常A/B/Cと選択は全サイズ、両Blindの使用画面は900×600（300％）を維持する。600や小サイズは入口であり、小さいBlind画面で全操作する仕様へ戻さない。
- 「300％の画面数」と「2枠のAnalysis資源」を混同しない。今回、ウィンドウ数の新たな上限は追加しない。
- AAXのPRE/POST Blind入口は利用者指示で有効化済みである。有効化と実DAWのclock/PDC実証は別で、未確認の実機検証は引き続き必要である。
- ReferenceはA/Bだけに戻さない。INSPECTとの接続や、Kirin OSへ手動Connectする操作も追加しない。
- `Capture`は画像保存、Local Blindの4秒音声取得、ReferenceのCapture Aで意味が異なる。画像側は`SAVE IMAGE`等、4秒は`CAPTURE 4 S`、全体観測は`CAPTURE A`という区別を実寸で確認する。

## 2. 最初に直す基盤

操作短縮の前提として、B-875レビューの4件を構造から直す。
過去のCaptureへ現在の位置対応を流用する問題、復元直後の保存欠落、PARTIALの消失、成長後も冒頭0.1秒に固定される表示を、同じ回帰検証へ含める。
既存レビューは`target/b875-review/findings.txt`と[構造修正計画](reference_capture_structural_repair_plan_20260914.md)に記録されている。

CaptureDocument/Store/Attempt/Binding/LiveEvidence/ViewStateの責務を分離する。
取得時のAと現在の入力に差が確認された場合は、A欄の小さな`A DIFFERS`と確認区間の印で伝える。
未再生区間を確認済みにせず、どのプラグインを操作したかも断定しない。
波形の間引きやUI通知の閾値を、sample位置・Gain Matchの成立判定へ流用しない。
保存済みA、現在のA出力、比較画面の表示対象を分け、変更検知による自動再取得・音量追従・音源切替を行わない。

## 3. Blindを一つの利用手順として整える（H03〜H06）

### 入口と操作段

600/900の通常POSTは、まず既存RESET枠をPRE/POST Blindへ置き換える構成を作る。
600では「300％で開く」意味を押す前に伝え、その1操作で拡大と開始前画面への移動を行う。
小さい3サイズでは同じ意味のメニュー入口を残す。
メニューと直接入口は共通の可用性判定から作り、現在表示している面で直接入口が出る場合だけ重複を除く。
REF/VU面でもメニューまで消して入口や復帰先を失わない。PREや非対応形式へ試聴入口を増やさない。

KEEPの直接追加は、上記構成と拡張構成を600/900の実寸で比較する。
既存フォントと押下領域を保ち、NOTE出現時もKeep/StopとMenuを動かさず、終了処理・保存失敗も収まる場合に限り採用候補とする。
詰め込むための文字縮小を行わず、入らない場合はKeep開始をメニューに残す。
この場合、Keepの3→2は計画上も達成扱いにしない。All Keep/All Stopは別操作を維持する。
RESETは既存のReset Meter Sessionメニューへ集約し、VU CLEARはVUに残す。

### 表示と次の操作

| 段階 | 主面に残す内容 | 次の明示操作 |
| --- | --- | --- |
| 開始前 | 相手、2MIXまたはTRACK / STEM、4秒取得、現在必要な条件 | DAWで再生しCapture。未接続なら同じ画面からPREを選択 |
| 取得中 | 進行とCancel。禁止条件の長文一覧は常設しない | 再生を続ける／Cancel |
| 準備完了 | 固定Context、取得区間、必要なGain承認 | Start。必要時は表示量のPOST減衰を明示承認 |
| 区間待ち・試聴 | 同じ位置の開始〜終了、Source 1/2の完了、要求中と実出力の区別 | HyphaでSource選択、DAWで区間より前から再生 |
| 両側完了 | 同じ回答領域に四つの選択肢 | 回答、別操作でReveal |
| Reveal後 | 本人の回答と割当 | End、その後Return to Live |
| 復帰要求後 | 復帰確認待ち／実際に確認できた不足条件 | 同じ試聴の音声側確認後、追加Closeなしで通常画面へ |

既存の`heardOneComplete`、`heardTwoComplete`、active/pendingStimulusを使い、表示用の再生タイマーは作らない。
回答前の短い案内と回答ボタンは同じ領域を使い、進行によって停止ボタンやkeyboard focusの意味を入れ替えない。
Sourceの割当を色、Gain、波形、アクセシビリティ説明から漏らさない。
Hyphaの固定UIは英語を維持し、日本語の提案例をそのまま常設コピーにしない。ユーザーの固有名は勝手に翻訳しない。

### 復帰の所有者と確認

現行Trialはcommandに対応するreturnReceiptを検証しているが、ProductSessionViewには試聴世代と実際の減衰適用状態が十分に公開されていない。
UIだけに成功フラグを追加せず、非RTの状態投影で試聴世代、復帰要求の識別、確認済み状態、実適用Gainの事実を渡す。
既存のcommand/receiptと`lowerApplied`を再利用し、音声callbackへ新しい探索・文字列生成を追加しない。
退役後も当該世代の終端結果を次の比較と区別して保持する。

Editorは「このEditorで、この試聴へReturn to Liveを要求した」という一時的な意図だけを持つ。
同じ試聴の対応する復帰確認で一度だけ閉じ、元の表示面とサイズへ戻す。
この意図はprojectへ保存せず、Editor破棄で消す。再表示だけで復帰要求を再送しない。
callbackなし、古い確認、別世代、二重クリックでは閉じない。正常に閉じる処理からCancelを再送しない。
復帰時の増加量は実適用状態から表示し、準備しただけの減衰を適用済みと扱わない。
callback不在を必ずbypassと決めつけず、ホストから確認できた事実だけを案内する。

### 失敗を修復できる同じ画面

開始可否と拒否理由はprocessor側の型付き結果へ集約し、メニュー・直接入口・開始前画面が同じ事実を使う。
形式非対応、所有中、pair不成立、clock/format不明、枠取得失敗、取得要求失敗、準備失敗、解放待ちを区別する。
下位APIが原因を区別できない場合は「取得できなかった」と表示し、枠不足を推定しない。
継続的な診断リストを出さず、現在進めない理由一つと、その修復先を近くへ置く。

PRE選択後は開始前画面へ戻すが、自動Captureは行わない。
Keepの終了・保存失敗・Reference Blind・通常復帰待ちを無断で解消しない。
別Blindが進行中なら既存の秘匿・復帰画面を維持し、新規の開始前画面で覆わない。
B-876の再取得条件を維持し、取得クリック時にpair、Context、資源解放を再検証する。
新しい取得世代に聴取完了、回答、Gain承認、遅延結果を持ち越さない。

## 4. 接続・観測・Reference（H01、H02、H07、H08）

H01は`Δ POST−PRE`など、現行見出しの意味を明確にする置換を基本とする。
現在窓、選んだ履歴、Meter Sessionの区切りを区別し、停止・bypassは確認可能な場合だけ分ける。
現行のInactiveには複数原因が含まれるため、値だけから停止原因を細分化しない。
同じ入力値を使い、履歴生成、Reset範囲、測定窓、SPACEの絶対観測を変えない。

H02の候補は現状、クリック時にFFI経由で列挙している。常設ボタン化は表示変更だけでは済まない。
新しい候補snapshotはhost/session境界、対象ID、claims、完全取得かどうかを持ち、描画はそれだけを読む。
既存の非RTサービスで候補更新をまとめ、接続済み・非表示中には常時探索を追加しない。
現在の列挙を背景threadから呼ぶだけで済ませず、engineの寿命と`handleLock`の保持範囲を監査する。
ファイル探索のためにUIの状態取得やengine破棄を長く待たせず、公開済みsnapshotの読み取りを短く保つ。
未接続の画面表示・接続状態変更で更新し、表示中の再探索が必要なら同じhost/sessionで最大1 Hzへ集約する予算案とする。
候補数に応じた探索量も実測し、インスタンスごとに専用監視threadやファイルwatcherを増設しない。
現在のC++候補／claims列挙は各32件で打ち切られるため、上限到達・取得失敗・古い境界は単一候補の証拠に使わない。
その場合は既存候補メニューへ戻す。表示の更新後も、押した瞬間には表示した同じIDの存在・排他・再生条件を再検証する。
名前欄と矢印の役割変更は未接続時に限定し、長い名前や識別不能な小サイズも既存選択を維持する。

H07は時間範囲ボタン本体の順送りと、隣の一覧入口を分ける。
600/900ではkeyboardでも選べる一覧を追加し、小サイズは従来どおりとする。
データが存在しない時間は空白を保つ。メニューを開くために履歴を再集計しない。

H08の主面はA/B/Cの実出力、BのVersion選択、CのCheck選択、現在位置、表示がLIVEかCAPTUREDかを優先する。
Capture Aと`A DIFFERS`／PARTIALはAの観測情報として配置し、CやBの準備状態へ混ぜない。
B/Cの名前と対応を安定させ、Preset・Cueは必要な比較で直接操作を維持する。
単一候補は読み取り可能な現在値へ置換できるが、Source/Cue変更に追加操作が必要な折り畳みは採用しない。
まれな設定だけ補助面へまとめる。星、点滅、長い常設説明、確認用の新しい接続ボタンは増やさない。
準備・失敗・再試行は該当するBまたはCに結び、保持Aや他方の選択を消さない。
Kirin OSの既存配信と安定IDを使い、接続・準備完了・復元だけでB/Cを再生しない。
両Blindの音声状態機械、Gain方針、復帰契約は独立したまま、案内の順序をそろえる。

## 5. 負荷を増やさないための実装条件

| 項目 | 上限・方針 | 確認方法 |
| --- | --- | --- |
| H01/H03〜H08の表示整理 | 新しいDSP、FFT、PCM保存、周期的ファイル読込を追加しない | callbackの変更差分と呼出経路を監査 |
| H02の候補探索 | 非RT、表示条件付き、要求集約。上記1 Hzは予算案であって実測結果ではない | 1/複数POST、候補多数、非表示で検索回数・I/O待ちを計測 |
| UI | 既存の更新経路を使用。新しい状態表示は最大10 Hz、状態不変なら文言・配置・波形pathを再生成しない | idle/再生/リサイズのUI CPUと割当を比較 |
| Capture変更照合 | 既存queueからworkerへ渡し、全曲の再decodeをしない。Reference表示中に限り追加照合 | 検知遅延、queue欠落、worker CPUを測定 |
| 資源 | 2枠Analysisと両Blind排他を維持。空の開始前画面では枠を取らない | 競合・取消・復帰・解放待ちを検証 |
| Capture保存とRAM | B-877の新schema上限1 MiB、queue合計2 MiB、追加RAM目標16 MiBを引き継ぐ | 最大2時間、restoreと保持Aと取得の同時存在を実測 |
| 通常A | 0 samples、bit identical。RTのalloc/lock/blocking I/Oを禁止 | nativeの厳密比較と対象DAWで確認 |

UI更新の制限をメーター全体の描画フレームレート変更へ広げない。
実機負荷は現在と同じsession・buffer・sample rate・インスタンス数で比較する。
1個の300％表示だけでなく、2MIX＋TRACK、TRACK二つ、閉じたEditorも含める。
callback p95/p99、最大処理時間、RSS、queue欠落、ホストのoverload発生を記録する。
「UI変更なので負荷ゼロ」とは扱わず、測定ノイズを超える継続的な増加があれば原因を除去する。

## 6. 実装順序・影響範囲・検証

これは一つの完成範囲を作る順序であり、項目を無断で次期版へ送る区分ではない。

| 順序 | 変更する責務 | 主なファイル群と対応試験 |
| --- | --- | --- |
| 1 | 既存巨大ファイルから変更責務を抽出し、Captureの4不具合を解消 | B-877のModel/Store/Session/Codec/Controller/Projection/表示範囲と4件の回帰試験 |
| 2 | Capture照合と表示、保存Aと現在入力の区別を完結 | CaptureRevisit/Binding、ReferenceCaptureControls、ComparisonView、schema・変更検知試験 |
| 3 | H03〜H06の入口・拒否理由・聴取表示・復帰を一体で接続 | PluginEditorLocalBlind/Lifecycle/Menu、LocalBlindComponent、ProcessorPairing、ProductSession/Trial、UI/Product/Playback/Transition試験 |
| 4 | H01/H02/H07/H08を共通shellへ反映 | ObservatoryView/Layout/Footer/Presentation/Contract、Pair候補snapshotと確定処理、ReferenceComponent/Editor/表示adapter、候補競合・Reference選択・5サイズ試験 |
| 5 | 対象試験を通し、最終状態で全体検証と実機を一巡 | 通常A不変、Record、復元、排他、負荷、操作数、初見観察を別々に記録 |

新しいowned sourceは500行以内。既存巨大ファイルの行数を増やさず、変更する責務だけを分離する。
表示成功をUI内で再実装せず、製品ごとの型付き状態と、共通の表示／操作可用性関数を使う。
共有するのは表示規則であり、ReferenceとLocal Blindを巨大な共通状態機械へ統合しない。
現行`hypha_invariants.md`にはINV-S25がFREQ M/SとLocal Blind UIで重複している。契約改訂では見出しも特定し、番号だけで別契約を上書きしない。

必須境界は利用者提案の8群を維持し、次を一体で検証する。

- 候補の同名・無名・消失・同時取得・列挙上限、pair変更、再生開始の競合。
- Captureの停止・seek・loop・PDC/rate/channel変更、解放待ち、連打、遅延結果、restore/save競合。
- 片側未完走、両側完走、聴き直し、回答変更、NO PREFERENCE/CANNOT TELL、割当秘匿。
- POST減衰の未適用／適用、End後の保持、復帰要求、callback不在、古い確認、Editor破棄・再表示。
- Keepの記録・終了・保存失敗、Reference通常試聴／Blind、Capture A、他POSTのAnalysis使用との重なり。
- 全5サイズの入口と通常画面、300％のBlind、サイズ復帰、長い名前、mouse/keyboard/AX、連打とfocus。
- Capture変更検知の誤通知、未確認区間、位置移動、設定を戻した再生、ディザー、無音、最大保存サイズ。

開発中は変更責務の対象試験を行い、全体suiteは最終状態で一度にまとめる。
Rust変更時のworkspace/clippyと、FFI変更時のignored parity/pairing全件もその一括検証に含める。
成功済み全体suiteは新変更・失敗・未解決の懸念がない限り繰り返さない。
実寸render、ネイティブ試験、実DAWの音声とPDC、Windowsは別の証跡とする。
Windows操作前は既存Runbookを読み、macOS AU/VST3/AAXとWindows対象形式の検証を省略して完了とはしない。

## 7. 操作数と完了判定

| 課題 | B-877を基準にした目標 | 注意 |
| --- | --- | --- |
| 通常POSTの直接入口からLocal Blindを終える | 10→8 | 接続・Context設定済み、各1巡、DAW操作別。600では入口の同意で300％へ開く |
| 小サイズのメニューから同じ比較 | 10→9 | 試聴自体は300％。開始サイズへの復帰も確認 |
| 単一PREを接続 | 2→1 | 完全で識別可能な候補snapshotがある場合 |
| 選択ペアKeep開始〜Stop | 3の維持を基本、直接KEEP採用時のみ2 | 配置検証前に削減を約束しない |
| TIME 30秒→24時間／隣の範囲 | 4→2／1の維持 | 大画面の直接メニュー利用時 |
| 適格な準備前失敗の再取得 | B-876で既に1。1を維持 | 開始拒否や出力後中断を同じ経路として数えない |
| A/B/C試聴・B/C選択・Cue変更 | 現行より増やさない | Capture表示操作と音声切替を別集計 |

上記は設計目標であり、ユーザーテストで得られた改善率ではない。
初回設定と反復課題、Hypha操作とDAW操作、機械の待ち時間と探索時間を分けて計測する。
戻り先・音量・相手・範囲の誤認が増えた場合は、クリック数が減っても不合格とする。
初見参加者の人数と条件が未定のため、理解しやすさの実証完了はまだ判定できない。
既存の三つの実利用課題は維持し、OS・Jungleを再設計したり、Hypha側の修正をそれらの改修待ちにしたりしない。
提案内の外部資料3点とOS側実画面は今回再検証していない。存在や操作結果を推定して引用しない。

実装に進む場合の出発点は順序1である。公開資料、配布物、Notion、Kirin OS schemaはこの計画作成では変更しない。

## 8. 確認したソース

すべて上記worktreeのB-877。コードを読んだ事実をテストpassや出荷済みバイナリの証明へ置き換えない。

- [Blindの入口・取得・復帰](../juce_shell/src/PluginEditorLocalBlind.cpp)、[開始要求のbool](../juce_shell/src/PluginProcessorPairing.cpp)、[表示と再取得](../juce_shell/src/HyphaLocalBlindComponent.cpp)。
- [ProductSessionView](../juce_shell/src/local_blind/LocalBlindProductSession.h)、[退役・資源解放](../juce_shell/src/local_blind/LocalBlindProductSession.cpp)、[聴取・復帰receipt](../juce_shell/src/local_blind/LocalBlindTrial.cpp)。
- [操作段](../juce_shell/src/HyphaObservatoryViewFooter.cpp)、[レイアウト](../juce_shell/src/HyphaObservatoryViewLayout.cpp)、[時間範囲](../juce_shell/src/HyphaObservatoryView.cpp)、[メニューとPRE選択](../juce_shell/src/PluginEditorMenu.cpp)。
- [候補のC++列挙](../juce_shell/src/PluginProcessor.cpp)、[候補FFI](../crates/kirin_hypha_ffi/src/pair_candidates_ffi.rs)、[session境界](../crates/kirin_measure/src/pairing_scope.rs)。
- [ReferenceのA/B/Cと選択](../juce_shell/src/HyphaReferenceComponent.cpp)、[Reference状態投影](../juce_shell/src/PluginEditorReference.cpp)、[Capture A操作](../juce_shell/src/HyphaReferenceCaptureControls.h)。
- [不変条件](hypha_invariants.md)、[外観契約](hypha_ce2226_jungle_visual_system_20260901.md)、[Capture検証記録](reference_a_full_capture_validation_20260914.md)、[構造修正計画](reference_capture_structural_repair_plan_20260914.md)。
