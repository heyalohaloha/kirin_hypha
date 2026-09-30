# Live Blind 中断理由と復帰動線の構造改善計画

## 結論と対象

理由の保存と中断を同じ試行に結び、復帰案内と操作を同じ判定結果から表示する。
共通案内欄の配置も親editorで整合させる。既存の比較全体を巨大な状態機械へ作り替えず、
音声処理には固定サイズの状態と上限のあるatomic操作だけを使う。

2026-09-30の厳格レビューで確認した4件を、一つの変更単位として解消する実装計画である。
利用者の「進めて下さい」を受け、以下の構造を実装した。検証結果と残る実機確認は末尾に記録する。

- 中断理由とBLIND commandの世代がずれ、具体的な理由が失われる。
- 自動終了後に存在しないENDへ誘導する。
- content hold解除に必要なDAW停止・再生が中断画面から抜ける。
- 長文案内でREFの本文領域が縮んでも、外部パネルの配置が更新されない。

主対象はLIVE BLIND、記名LISTENのPRE WAIT、共通案内欄とREFの配置である。
Referenceや任意Exact 4 Sの回答モデルを今回変更済みとは扱わない。
LOOPでMATCHを保つ希望は維持するが、ループ認定・MATCH失効規則の変更まで今回の4件修正へ混ぜない。
この計画はLOOPをPhase 2へ移す裁定ではなく、追加設計と実装範囲の判断を未決のまま明記するものとする。

構造修正後の利用者指示を受け、LOOPの仕様案を[DAWループ再生と固定MATCH保持の設計案](hypha_daw_loop_match_design_20260930.md)へ分離した。
認定済み折り返しでのMATCH保持、短loopの測定窓、BLINDとの整合を扱う設計であり、LOOPの実装済みを意味しない。

## 維持する利用者契約

- LIVE BLINDはSource切替、1クリックの開示、終了。必要な音量減衰の承認は残す。
  回答を保存・集計・学習・音の調整へ使う設計が成立するまで、好みの回答選択は導入しない。
- 開示は両Sourceの安定したRT出力を確認してから行い、開示自体で音・gainを変えない。
- 正常A経路はbit identical、0 samples。計測・Record・元音源は変更しない。
- 中断や画面を閉じる操作で、承認済みPOST減衰を勝手に解除しない。
  END/RETURNの上昇量を表示し、完了は実出力が通常音量へ戻ったRT receiptで判定する。
- 中断したBLINDを同じ割当で自動再開しない。WAITの自動復帰と、試行の再開を混同しない。
- 「通常再生中に突然中断」の実事象は原因未特定。検出事実を表示し、他社pluginの故障や利用者操作を推測しない。

## 1 中断を理由付きの一つの遷移にする

独立したRecoveryReceiptの世代をcallback入口で読み、後から別のcommandを読む方式を廃止する。
記録先の世代は、必ずそのblockが採用したcommandから得る。

### BLIND

既存BlindSessionのterminal atomic wordへ、試行ID・終了種別・最初の理由をまとめる。
`invalidate(command, reason)`の一つの状態遷移で中断と理由を確定する。
理由保存とinvalidBit設定を別々に成功させる経路をなくす。

- 新試行はterminal初期化後にcommandを公開する。独立した理由用serialを増やさない。
- 古いcommandの中断・聴取receipt・開示要求は新試行へ作用しない。
- 開示との競合は固定回数のCASで扱い、無制限リトライ・spin待機はしない。
- ENDやwindow closeは明示終了として区別し、unknownの失敗へ変換しない。
  既に確定した失敗理由を終了処理が上書きしない。
- tear down後も最後の理由をUIから読めるようにし、新しい試行または完了した明示ENDで整理する。
- active前の準備失敗は、既存blindPreparationに結び付くmessage-thread所有の結果に保持する。
  まだ存在しないRT trialの失敗を捏造しない。

### 記名LISTEN

PRE/POSTの選択要求とその選択IDを一つの固定サイズcommandで公開する。
理由receiptはこのIDに結び付け、独立したreset用世代を持たせない。
古いblockが新しいPRE選択を無条件のbool書込みで解除する経路も、同じID検査へ統合する。
一時的な対応確認待ちはその時点の観測であり、終端の失敗理由とは分ける。

共通化はwordの符号化・世代検査などの小さな処理に限る。Authority、Completion、MATCHの承認とgain、
ringの寿命は既存の責務を保つ。汎用イベントバス、新しいRTキュー、履歴DBは作らない。
不整合な状態はPREの出力許可へ昇格させず、既存のPOST退避と減衰保持を維持する。

## 2 復帰案内と操作の判定を共通にする

message/UI側に副作用のない小さなRecoveryPresentation判定を置く。
入力は既存のstatusとadmission結果、出力は理由ID・現在の阻害条件・次の操作ID・音量復帰注意とする。
文字列化は英日catalog側で行い、RTには文言を持ち込まない。

過去の中断理由と現在の解除条件は別々に扱う。例えば「callback gapで中断」した後に遅延補償が無効なら、
理由は残し、現在必要な補償の有効化を案内する。複数の阻害条件は固定サイズのflagsで保持し、
優先順位に従う次の手順を表示する。解除されるたびに次へ進み、解除済みの操作は要求しない。

| 現在の状態 | 案内と操作 |
| --- | --- |
| 記名PREの一時的な確認待ち | POST出力と自動復帰を表示。終了・再MATCHを一律に要求しない |
| 時間差変化によるhold | DAWの停止→再生が必要と表示。ENDだけで解除されるとは案内しない |
| 遅延補償が無効 | 有効化を案内し、実際の解除後に対応を再確認する |
| 記名比較が終了済みで通常音量 | 原因への対処後にLISTEN。表示されないENDは要求しない |
| 終了済みだがPOST減衰を保持 | 実在するRETURNと上昇量を示す。無断で通常音量に戻さない |
| BLINDが中断 | 原因と現在必要な対処を表示。END後、新しいBLINDへ。無効な試行は再利用しない |
| 明示ENDの音声処理待ち | 終了待ちを表示。停止中なら再生が必要と示し、未完了を完了表示しない |

content holdはEND後も残り得るため、active=falseや中断理由クリアを理由に案内から消さない。
BLIND中断画面でも停止・再生の必要性を最初から示し、再開してから初めて知らせる導線をなくす。

footer、BLIND、MENU、tooltip、accessibilityはこの判定を共有する。
小さい画面でLISTENがMENU内なら、案内もMENU経由の実際の導線に合わせる。
操作時はprocessorの既存admissionを再確認し、表示後のpair/restore/END競合で古い操作を実行しない。
確認ダイアログや新しい常設ボタンを追加することを前提にしない。

## 3 案内欄と本文の配置を親editorで揃える

Observatoryは本文・案内欄のgeometryを算出し、親editorの一つの同期処理が
Reference、AccessPanel、他の外部analysis panel、案内欄へ適用する。
REFだけ旧boundsを渡し直す経路と、更新のたびにパネルを無条件で最前面へ出す経路を整理する。
REF下段を案内欄から避ける既存方針は維持し、他domainの表示方針を一括変更しない。

- 長文の出現・消滅、文言長、言語、サイズ、domain、capture、Blindの切替を配置の入力とする。
- geometry/visibilityが同じならsetBounds・重ね順変更を行わない。
- View::resizedから親resizedを呼び返す再帰構造にしない。
  既存UI更新周期と即時操作の最後に、小さな同期処理を呼ぶ。
- 理由/操作ID・表示数値・言語が同じなら文言を再構築しない。音量復帰の数値更新は省略しない。
  文字幅再計測と再レイアウトも、文言・フォント・geometryなどの入力変更時だけ行う。
- 新しいTimer、worker、フレームごとの全体再配置は追加しない。

## 4 負荷と安全性の上限

音声側の追加処理はblock単位の固定サイズ状態操作に限定する。
Audio Threadでのalloc/free、lock、blocking I/Oは禁止する。
音声サンプルの追加走査、PCMコピー、解析窓、buffer、レイテンシー、ログ書込みを増やさない。
通常時に理由を毎block CASする必要はなく、中断遷移時だけ更新する。
新しいatomic wordは対象platformでlock-freeをcompile時に確認し、CASの試行回数には上限を設ける。

UI側は既存の更新周期を維持し、判定結果の比較で不要な文字列生成・再配置を抑える。
原因履歴の蓄積、常時トレース、ネットワーク通信、回答収集、重い汎用状態管理は導入しない。

性能は修正前の未commit候補を基準として、同一fixture・同一条件で前後比較する。
無試聴、記名PRE/POST、BLIND、WAIT、失敗・復帰を分け、64/128/256/512 samplesを含む低bufferで測る。
通常処理時間の中央値とp99、失敗blockの時間、RT alloc/free回数、UIレイアウト適用回数を記録する。
benchmarkの時刻計測はtest harness側に置き、製品RTに常設しない。
既存性能ゲートは維持し、反復で測定ノイズを区別して有意な後退が残る場合は完了扱いにしない。
実機未測定の段階で「負荷ゼロ」「軽量化済み」とは報告しない。

## 5 影響ファイルと実装順

責務ごとの主対象は次のとおり。実装前に全呼出し元を再検索し、旧APIと重複した状態を残さない。

- 試行と理由: `LiveBlindSession.h`、`LiveCompareRecovery.h`、`LiveCompareProcessorState.h`、
  `LiveCompareProcessorApi.h`、`PluginProcessorLiveCompareRealtime.cpp`、
  `PluginProcessorLiveCompare.cpp`、`PluginProcessorLiveBlind.cpp`。小さな選択command moduleは必要時に分離する。
- 復帰表示: `HyphaLiveCompareRecoveryText.h`、`HyphaLiveBlindComponent.{h,cpp}`、
  `PluginEditorLiveCompare.cpp`、`PluginEditorLiveBlind.cpp`、`HyphaObservatoryLiveCompare.cpp`、
  `HyphaJapaneseBlind.cpp`、`HyphaJapaneseNotices.cpp`。純粋な表示判定は独立moduleへ置く。
- 配置: `HyphaObservatoryViewLayout.cpp`、`HyphaObservatoryViewState.cpp`、`HyphaObservatoryView.h`、
  `PluginEditor.cpp`、`PluginEditor.h`、`PluginEditorReference.cpp`、`PluginEditorObservatory.cpp`。
  共通の本文配置処理は既存editorの小さな`layoutBodyAndFeedback`へ集約する。
  対象ファイルは500行以下のため、新しいmoduleへの分割は不要だった。
- 検証と契約: live_compare unit、live_blind_product、live_compare_lifecycle/offset product、
  LiveBlindUi/LiveCompareFooter/Referenceの表示試験、`LocalBlind.cmake`、RT source契約、README、invariants。

1. レビューの4件を決定的な回帰fixtureにする。負荷の基準を採取し、変更前に失敗することを確認する。
2. 理由付き終端と選択commandの責務を実装する。公開順序、古いcallback、END/開示競合を単体で確かめる。
3. 共通の復帰判定を接続し、案内された操作を実行して復帰まで到達するproduct試験を通す。
4. geometryの単一適用経路を接続し、長文出現・消滅とREF表示を検証する。
5. 音声安全・負荷・全対象表示をまとめて再検証し、厳格レビューをやり直す。

途中工程だけを製品完成として渡さない。新規owned sourceは500行以下、既存500行超は増加禁止とする。
500行以下の既存対象も上限内に保つ。
変更対象が500行超なら規約どおり該当責務を先行分離し、baselineも同時に下げる。
今回は多くの対象が既に500行以下のため、無関係な大規模分割を前提にしない。

## 6 合格条件

- begin、PRE再選択、開示、END、restoreをRTの読取り・記録の間へ挟む決定的な順序テストで、
  新試行の破壊・具体的理由の欠落・理由の上書きが起きない。stress試験だけに依存しない。
- 古い停止理由を保持している間に別の復帰条件が変わっても、案内と操作が現在の状態に一致する。
- inactive/unity、保持減衰、content hold、pair不在、format変更、PRE再起動、再openを試験する。
  必要な操作が直接ボタンかMENUに存在し、実行後に説明どおりの状態へ進む。
- 非finite、ceiling、callback gap、時計不明、bypass、offline、別出力ownerでも安全境界を維持する。
- 正常Aのbit同一性・0 samples、保持減衰、ENDの実unity完了、匿名性、1クリック開示が退行しない。
- 英日・全サイズ・全domainで長文の出現/消滅、REFの本文/AccessPanel、capture/Blindとの重なりと
  accessibilityを検証する。幅の計算だけでなく、実editorの位置・重ね順と画像を確認する。
- 同じ復帰状態を繰り返し通知しても、復帰UI由来の再配置が増え続けない。RT alloc/freeは0。
- `cargo test --workspace`、owned crateのclippy、対象native suite、source/text/line-budget契約を通す。
  FFIを変更した場合はignored parity/pairingの一覧件数を実測し、全件実行する。
- macOS/Windowsの対象compileとfixtureを確認する。未実施platformや実DAW低buffer、AAX実host PDCは
  別の未検証項目として残し、既存証跡を新commitの成功へ流用しない。

## 実装と検証の境界

理由付き終端、記名選択command、現在条件に基づく復帰案内、本文と案内欄の同期を実装した。
理由用の独立世代を廃止し、RTの中断CASは既存commandへ結び付けた。
新しいPCM処理、解析、buffer、thread、timer、RT loggingは追加していない。
BLIND表示は同じ状態・音量・言語なら更新を省略する。親editorは異なるboundsだけを適用し、
REFを毎tick最前面へ出さない。匿名性、保持減衰、実出力のEND完了は既存責務を維持する。

検証用Debug buildと使い捨てfixtureを実施した。実DAW/Windows検証機の操作、install、署名、公証、公開は行っていない。
配置やreleaseを別途依頼された場合は、AAX入口文書、LSパッケージ、全配布チャネルの既存gateに従う。
参照: `target/validation/live-blind-strict-review-20260930.md`、INV-LC18〜21。

### 軽量性の実測と残る範囲

修正前後のrendererとBLIND command/receiptを、同じ最適化条件の独立benchmarkで比較した。
64/128/256/512 frames、POST/PRE/BLIND/対応不成立の各経路を各12,000 block、前後10組測定した。
ビルドと試験終了後に実行し、各runの中央値・p99をさらに10組の中央値でまとめた。
これはprocessor全体、MATCH worker、通常の無試聴経路、実DAWのCPU測定ではない。

| BLIND block | 中央値 前→後（µs） | p99 前→後（µs） |
| --- | --- | --- |
| 64 | 0.6060 → 0.6100 | 0.6650 → 0.6725 |
| 128 | 1.1565 → 1.1705 | 1.3140 → 1.2945 |
| 256 | 2.2675 → 2.2740 | 2.8820 → 2.6990 |
| 512 | 4.3985 → 4.4265 | 8.2055 → 8.5340 |

中央値の増加は約0.3〜1.2%。RT allocation guard内の比較処理と理由付きcommand遷移はnew/deleteとも0件。
英日×5サイズ×5domainの50ケースでREF本文/AccessPanelを実editorで確認し、同一配置100回の追加bounds変更は0件。
実DAW低buffer、Windows compile/fixture、実AAX hostは未検証。LOOPのMATCH保持も今回の実装には含まない。
「通常再生中に突然中断」した実事象の原因は未特定であり、今回の理由表示を使って次の発生事実を確認する。

### 検証結果（2026-09-30、未commit候補）

- macOS x86_64 DebugのPRE/POST共通shellと対象試験をbuild。対象native suiteは28/28 pass。
  最終ログにJUCE assertionなし。実AAX hostではなく、wrapper境界はfixtureで確認した。
- `cargo test --workspace`: 2,166 pass / 0 fail / 41 ignored。最終C++変更後のxtask再実行も157/157 pass。
  FFI本体は変更していないため、追加のignored parity/pairing suiteは今回実行していない。
- `cargo clippy --workspace --all-targets`: pass。owned codeのclippy警告なし。vendor由来の既存警告は残る。
- source line budgetと`git diff --check`: pass。英日・各画面寸法の実画像も確認した。
- 実editorの再open後にも具体的な最初の理由が残り、実在するLISTEN/ENDで復帰できることを確認。
  content holdは実際の停止callbackで解除され、ENDのみでは消えないことも確認した。

試験ログと未送信のセッション記録は`target/validation/live-blind-structural-implementation-20260930.md`参照。
release build、install、署名、公証、公開はskip。基準は`e02b4251` / B-1101であり、新しいB番号は採番していない。
