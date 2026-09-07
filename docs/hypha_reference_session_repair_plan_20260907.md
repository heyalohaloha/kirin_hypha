# Hypha Referenceセッションの構造修正計画 — B-747レビュー後

作成日：2026-09-07。
基準：`65740249d72c682fde381037f9429d299783a561` / B-747。
状態：2026-09-08のB-748実装候補でSR1〜SR3を構造修正済み。対象native・静的契約・ASan・TSan・最終sourceゲートはpass。Windows実行と実DAW確認は未実施。
対象レビュー：B-734〜B-747の14コミット、85ファイル（+5,765 / −726）。

本書を、Referenceの開始・失効・保持・通常復帰と関連するRT検証ゲートの現行計画とする。
[旧構造修正計画](hypha_structural_repair_plan_20260907.md)の所有者分離・世代公開・Blind遷移について、実装の移行先と削除条件を具体化する。
旧計画のB-738までの試験記録は履歴として保持し、現行HEADの合格判定に流用しない。
ローカルPRE/POST Blind、SPACE、ATTACK、5.1、AAX、両OS・配布の範囲を、この修正計画だけで追加・削除・次期送りにはしない。

## 0. 実装時の構造裁定

計画で候補にした`ReferenceSessionControl`等の別エンジンと新しい重複native targetは追加しなかった。
既存の`RuntimeV2Blind`はすでに準備・lifecycle・RT出力・snapshotを500行以下のsourceへ分離し、本番sourceを直接linkする`KirinReferenceAuditionRuntimeTests`も全体ゲートに登録済みだった。
その境界を置き換え、同じ状態のwriterを二組に増やさない方を採用した。

`RuntimeV2Blind`を唯一のsession状態機械とし、非RT入口はsession ID・audition epoch・gate tokenを一括してArmedへ公開する。
RTだけが最初の実出力でArmedからActiveへ進め、workerのprepare / clearはArmed・Active・Held・ReturnPendingへ入れない。
失効・取消・通常復帰は同じ状態機械のCASとreader / callback pinを通し、退役時はsession ID付きtokenを返す。
重いdecodeは従来どおりworkerに残り、live session中に再prepareしないため、一般化したcommand queueを追加して通常復帰をdecode待ちにする構成は採用しなかった。

通常Bは同じgate adapterを使う既存経路を維持したが、予約中の再取得を拒否し、通常BとBlindのRT返却tokenを別々に保持した。
権限失効時の`SuspendAudition`はBlindならHeld、通常BならAへ戻して予約解放となる。
この裁定は単一のsession正本、重い処理の非RT隔離、RTでの待機禁止という目的を維持し、計画段階のfile名とqueue方式だけを実コードに合わせて収束させたものである。

## 1. 修正する問題と、前回計画で閉じられなかった理由

| ID | 確認した問題 | 構造上の原因 | この計画の完了条件 |
| --- | --- | --- | --- |
| SR1 / P1 | 開始中のReference更新で、−6 dBのA出力0.250594が、通常復帰操作なしに0.5へ戻った | 開始失敗の後処理が`blind.end()`を呼び、出力済みsessionにも通常復帰を要求する | 自動失効で通常復帰要求を作れない。出力済みなら保持へ移る |
| SR2 / P2 | 開始がtrueを返したのに、listening=false、出力callback数0となった | `blind.start()`のactive公開と`activeAuditionEpoch`の公開が別々。UIとRTが同じlifecycleを更新する | 一つの準備済みsessionからRTが開始を採用し、実出力receiptでだけ開始済みとする |
| SR3 / P2 | 現行HEADのRT安全性3件・試聴権限1件が失敗する | `processComparisonPaths`への改名に検証対象の登録が追従せず、新しい呼出し先の監査も分散している | 実際の呼出し境界に揃えた静的契約と、出力・所有権のnative試験が同じ候補で通る |

再現資料は[レビュー報告](/Users/nishiodaisuke/Downloads/hypha_review_B734_B747_20260907.md)と`/tmp/hypha_review_b734_b747/`にある。
SR1はControllerの実関数と出力値で再現した。
SR2は音源更新のない二つのthreadの交差で再現したもので、10,274回という捕捉回数をDAWでの発生確率とは扱わない。

前回計画にも「UIは要求だけ、workerが単一所有者」と記載されていた。
現行実装ではUI入口から`blind.start/end/answer/reveal`へ入り、workerが`prepare/clear`、RTが`invalidate`を行う経路が残った。
tokenやepochを追加しても、この複数所有者の構造は解消していない。
今回は新しい管理層を足すだけでなく、旧経路の削除と全呼出し元の移行を完了条件にする。

計画作成時には次の呼出し元も実コードで確認した。
これらは独立した再現済み指摘の件数には加えず、SR1の修正範囲へ含める。

- `PluginEditorReference.cpp::refreshReferenceAudition`はheartbeat・transport・位置の不成立から`endReferenceBlind()`を呼ぶ。
- `PluginProcessorGuideTransport.cpp`の開始・切替・Revealの権限失敗分岐にも`endBlind()`がある。
- `ReferenceRuntimeV2Commands.cpp::requestSelection`は選択変更後に`selectA()`へ入り、Blind中なら終了要求になる。
- `completeNormalReturn()`は再開始可能な状態を公開してからsessionをresetし、その後Controllerが現在のgateとepochを解放する。

## 2. 利用者から見た契約

開始ボタンの受理と、音が実際に出たことを区別する。
開始要求の受理後は短い開始待ち状態を表示し、1 / 2の実出力を確認してから聴取中とする。
待っている間に条件が変われば、要求を失効させて結果を通知し、更新後の音源で自動開始しない。

一度適用したAの減衰は、自動中断で解除しない。
利用者が通常復帰を要求した後、適格なcallbackが通常Aを出したことを確認して、そのsessionの予約を解放する。
音を一度も出していない要求は、未出力の取消確認後に撤回でき、新たな減衰を作らない。

offline・bypass・出力権限なしでは、入力Aを変更しない既存境界を守る。
この出力禁止と、論理上の保持・通常復帰完了を区別する。
再生再開だけで凍結PCMの比較を再開しない。

PREの名前は任意の表示ラベル、POSTの明示選択がペアの入口という契約を維持する。
この修正でhost固有IDや名前一致を必須に戻さない。
TrialをABX検定や音質改善の証明に読み替えない。

## 3. 正本を変更する主体を三つに分ける

| 主体 | 変更できる正本 | 禁止する操作 |
| --- | --- | --- |
| UI / processorの非RT入口 | 有界な要求受付、request ID、期待する準備世代 | 実出力状態の変更、gateの解放、live PCMの変更 |
| 単一の非RTセッション所有者 | 要求の採否、準備結果の採用、session公開、予約、receipt消費、退役、snapshot / History | Audio Threadの実出力実績を推測で作ること |
| Audio Thread | callback境界での経路選択、実際の減衰適用、聴取frames、返却確認 | decode、allocation / free、lock、I/O、待機、準備済み条件の書換え |

セッション所有者は`RuntimeV2Controller`の制御workerへ集約する。
通常BとBlindは同じ出力予約を使うため、通常Bの開始・解除もこの所有者を通す。
通常Bを別のwriterとして残し、Blindだけをqueue化する実装は不可とする。

既存worker内の`blind.prepare`、ページdecode、ファイル検証などの長い処理は、準備処理へ分離する。
制御workerが重い処理を完了するまで通常復帰要求を読めない構造にしない。
準備側は一つの実行中jobと一つの置換待ちjobに制限し、完了時に要求IDと準備世代を返す。
古い結果は非RT側で破棄する。準備workerはgate、live session、UIの結果を直接変更しない。

UIなど複数の非RT呼出し元は、非RT専用lockを持つ固定容量の受付へ要求を入れる。
RTへ渡すcommandのproducerは制御workerだけとする。
満杯時の開始・切替は明示的に拒否し、通常復帰と失効は専用の保留領域で保持して開始要求に埋もれさせない。
未処理要求の上書き、同じ操作の二重実行、失敗したenqueueの成功扱いを禁止する。

## 4. 準備から最初の実出力までを一つのsessionへ束ねる

非RT側で作る準備済みsessionは、session ID、request ID、準備世代、出力予約token、format、Cue / mapping、固定gain、承認根拠、凍結PCMを一体で保持する。
sourceの文字列・hash・Trial commitment・History contextも同じsessionに結び付けるが、RTには検証済みの数値とPCM参照だけを渡す。
RT用の状態機械はJUCE・FFI・ファイル形式に依存しない小さなC++部品とし、共通shellがbufferとcallback事実を変換する。

処理順は次で固定する。

1. UIが表示していた準備世代に対し、開始要求を受け付ける。
2. 所有者が最新条件と承認を確認し、sessionに属するgateを取得する。
3. 取得中の失効を再確認し、全条件を設定し終えたsessionを公開する。
4. RTがcallback入口で公開をpinし、そのsessionの開始要求と現在の適格条件を照合する。
5. 全出力条件が成立した非空callbackだけが凍結PCMを出力し、session付きの開始receiptを発行する。
6. 所有者がreceiptから聴取中snapshotとHistoryを作る。

`active`を先に公開してから別のatomicへepochを書き込む方式を廃止する。
公開後の失効と最初のcallbackが交差した場合、未出力ならCancelledUnheard、出力済みならHeldへ確定する。
失効が先に観測されたcallbackで旧条件を新規採用しない。
callbackが既に採用した条件は、そのcallback内で混在させない。
最後の処理結果をUI側の事後チェックで上書きしない。

公開・pin・退役の原始操作には既存`RtPublicationSlot`の単一所有者・単一RT reader契約を再利用する。
slotの存在は出力許可を意味しない。公開条件とcommandの採用はReferenceの状態機械で判定する。
公開中のpayloadを書き換えず、新しい準備結果は別に保管する。
有効sessionは一つとし、準備結果・退役待ちを含む保持数とPCM bytesに上限を設ける。
readerが残って新規公開できない場合は所有者が後で再試行し、RTを待たせない。

## 5. 自動失効と通常復帰を別の操作にする

| 状態 | 入口 | RT出力 | 次に必要な事実 |
| --- | --- | --- | --- |
| Normal / Prepared | 未開始・準備完了 | 通常A | 利用者の明示開始 |
| StartPending / Armed | 要求受理・session公開 | 採用前は通常A | 最初の適格な実出力、または未出力取消確認 |
| Listening / Revealed | RTが開始を採用 | 選択した凍結PCMと固定gain | 切替receipt、失効、明示通常復帰 |
| CancelledUnheard | 未出力のまま失効・取消 | 通常A | 旧sessionを採用しないRT確認とreader返却 |
| Held | 出力済みsessionの自動失効 | 適格時は実績のある減衰だけをライブAへ適用 | 利用者の通常復帰要求 |
| ReturnPending | 明示通常復帰 | 次の適格callbackで通常A | 同じsession / return requestの通常出力receipt |
| NormalConfirmed / Retiring | 所有者が通常出力を確認 | 通常A | そのsessionのgate解放とPCM回収 |

通常復帰を要求できる入口は、利用者による通常A選択・Blind終了・通常復帰に限定する。
source / Preset / Cue / binding変更、準備失敗、lease失効、権限喪失、transport不成立、heartbeat停止、UI閉鎖、worker再準備は、明示通常復帰を発行しない。
画面更新はsnapshotの観測に徹し、heartbeat停止の監視はUIが閉じても続くprocessorの事実を制御workerへ渡す。

Heldのgainは承認値ではなく、実出力時にRTが記録した値を正本とする。
保持中の繰り返し失効・source再読込・sample rate変更で、その値とsession IDを上書きしない。
減衰なしの試行も、出力済み試行として通常復帰の確認と予約寿命を管理する。
未出力かどうかは所有者が途中のcounterだけを読んで決めず、取消後に旧sessionを採用しないRT確認とpin返却で確定する。

通常復帰要求は停止中でも保持する。
0 frames、無効layout、offline、bypass、位置不明、停止中など、既存の適格条件を満たさないcallbackから通常復帰完了を捏造しない。
出力権限のない間も新たな試聴は許可せず、通常復帰要求そのものは受付可能にする。
試聴許可と通常Aの出力確認を別の述語にし、通常Aを非破壊で通した事実の確認に試聴ライセンスの再取得を要求しない。
明示復帰要求があり、realtime・非bypass・再生中・有効位置・現在の有効layout・非空bufferが揃うcallbackは、試聴権限がなくても通常Aを確認できる。
保持状態をアクセス画面で隠さず、この確認または実際のhost teardownまでの待機を表示へ反映する。
ReturnPendingへ入った後の自動失効は、通常復帰要求をHeldへ戻したり削除したりしない。
同一要求の再送は同じ結果へ収束させ、古い開始・失効commandで新しい要求を上書きしない。

UIを閉じたこと、`releaseResources()`、transport停止をhost teardownの証明に使わない。
processor破棄などcallback停止が確定した経路だけで、無callbackの終了を別の終了理由として記録して回収する。
workerを停止・再起動する場合も、processor側のsession保管を先に破棄しない。

## 6. 予約、返却確認、表示・Historyを同じsessionへ固定する

gateはsession IDとtokenを持つ非RT所有の予約として管理する。
通常復帰receiptと退役対象の両方が同じsessionに一致した場合だけ解放する。
通常経路で「現在のgate」を探して解放する`releaseActiveOutputGate()`を使わない。
旧sessionのreset、token解放、PCM退役が完了してから、再開始可能というsnapshotを公開する。
通常BからBlindへの既存の明示切替も同じ所有者が引き継ぎ、二重出力・二重予約・旧tokenによる新予約解放を起こさない。

既存C ABIの`kirin_hypha_set_reference_audition_active`は、単一所有者からのadapterとしてまず維持する。
`stateLock`を保持したままgate callbackへ入らず、`handleLock`を保持したままworker終了を待たない。
Recordとの排他、二重開始拒否、handle破棄との交差をprocessor経由の試験で確認する。
FFIの意味変更が必要になった場合は、その変更とignored parity / pairingの検証を同じ候補へ含める。

開始・切替・未出力取消・通常復帰のreceiptはsession ID、command sequence、callback sequenceを照合する。
聴取framesは実出力だけを累積し、通常AやHeld中のライブAを1 / 2の聴取実績へ加えない。
重要な返却確認は、表示更新が遅くても失われないsession内の確認領域に保持する。
RTが容量不足で待機したり、イベントを上書きして通常復帰確認を失ったりする実装を禁止する。
複数の値を読むsnapshotは整合した世代として取得し、非atomic fieldをseqlock風に読み書きしてdata raceを作らない。

UIの戻り値は`rejected`と`accepted(requestId)`を区別し、acceptedを再生開始済みとは表示しない。
workerが後で拒否した明示操作は、そのrequest IDに対応する結果を一度通知する。
準備の再試行など利用者の要求に紐づかない内部失敗は静かに処理する。
開始待ち・通常復帰待ちをsnapshotへ追加し、1 / 2、回答、Revealはそれぞれ必要なreceiptが揃ってから有効にする。
表示とHistoryのgain・source・commitmentは、現在のworkspaceから取り直さず採用されたsessionから作る。
凍結sourceをReveal前に漏らさず、開始未成立を完了Trialとして保存しない。

## 7. 影響範囲と移行先

下表は計画時点で確認した変更対象である。
追加の呼出し元が実装中に見つかった場合も同じ移行表へ加え、暗黙の互換経路として残さない。
新規owned sourceはすべて500行以下とする。

| 対象 | 主なファイル（repo相対） | 変更内容 |
| --- | --- | --- |
| 要求・所有者 | `juce_shell/src/reference_audition/ReferenceRuntimeV2Controller.{h,cpp}`、`ReferenceRuntimeV2Commands.cpp`、`ReferenceRuntimeV2Lifecycle.cpp` | 要求受付と所有者loopへ集約。重い準備と制御を分離 |
| 準備・世代 | 同ディレクトリの`ReferenceRuntimeV2Workspace.cpp`、`ReferenceRuntimeV2Blind.cpp`、`ReferenceRuntimeV2NormalSelection.cpp` | live状態を変えるprepareを廃止し、世代付き不変結果へ変換。通常Bの予約も移管 |
| 出力・遷移 | 同ディレクトリの`ReferenceRuntimeV2Selection.cpp`、`ReferenceRuntimeV2Realtime.cpp`、`ReferenceRuntimeV2Blind.{h,cpp}`、`ReferenceRuntimeV2BlindLifecycle.cpp`、`ReferenceRuntimeV2BlindRealtime.cpp`、`ReferenceRuntimeV2BlindState.cpp` | 新状態機械へ移す。旧lifecycle mutatorと独立epoch公開を削除 |
| snapshot・記録 | 同ディレクトリの`ReferenceAuditionController.h`、`ReferenceRuntimeV2Events.cpp` | 共通Snapshotの受理・実出力区分、session付き結果を追加。既存v1 consumerの互換も確認 |
| processor | `juce_shell/src/PluginProcessor.h`、`PluginProcessorGuideTransport.cpp`、`PluginProcessorAudition.cpp`、`PluginProcessorValidation.cpp`、必要箇所の`PluginProcessor.cpp` | 型付き要求、gate adapter、callback facts、teardownを共通PRE/POST shellへ接続 |
| UI | `juce_shell/src/PluginEditorReference.cpp`、必要箇所の`PluginEditor.h`、`HyphaReferenceComponent.{h,cpp}`、`HyphaReferenceAccessPanel.h` | timerの自動終了を削除。開始待ち・保持・通常復帰待ちと明示操作の結果を全サイズへ反映 |
| 既存publication | `juce_shell/src/local_blind/RtPublicationSlot.h`、`LocalBlindSlot.h`、`ExactRangeCaptureSlot.h`、`LocalBlindCaptureLane.h` | lifetime契約を参照・再検証。共通primitiveを変更した場合は既存capture / trialも全対象に含める |
| native試験 | `juce_shell/tests/reference_runtime_v2_workspace_test.cpp`、`reference_runtime_v2_transaction_test_support.h`、`reference_runtime_blind_test_support.h`、`ReferenceAuditionComponentContractTest.cpp`、`local_blind_capture_test.cpp`、`local_blind_trial_test.cpp` | Controller・processor・UIまでの回帰とpublication寿命の確認 |
| 登録・source契約 | `juce_shell/CMakeLists.txt`、`juce_shell/cmake/`の専用module、`xtask/src/rt_safety.rs`、`xtask/src/os_access.rs`、`xtask/src/shell_parity/runtime_tests.rs`、`scripts/test_release_source.sh`、`.github/workflows/ci.yml` | 新native試験と実RT呼出し先をmacOS / Windows、全体ゲートへ登録 |
| 正本・行数 | 本書、統合計画、旧構造修正計画、必要な`docs/hypha_invariants.md`、`scripts/source_line_budget.tsv` | 未完了状態・検証証拠の更新、行数ratchet |

追加する責務は`ReferenceSessionTypes.h`、`ReferenceSessionControl.{h,cpp}`、`ReferenceSessionRealtime.{h,cpp}`、`ReferenceSessionPreparation.{h,cpp}`を基本単位とする。
要求・receiptの受渡しは小さな`ReferenceSessionExchange.h`に分ける。
新native試験は`juce_shell/tests/reference_session_contract_test.cpp`を入口とし、専用fixtureへ分割する。
`juce_shell/cmake/ReferenceSessionTests.cmake`で`KirinReferenceSessionContractTests` / `kirin_reference_session_contract`を登録する。
静的なRT検査対象の共通定義は`xtask/src/rt_contract_surface.rs`へ置き、`xtask/src/main.rs`のmodule登録も変更対象に含める。
これらは既存Referenceの責務を移す先であり、旧実装を併存させるための別エンジンにはしない。
汎用的なhost管理や解析資源管理の新frameworkを作らない。

`PluginProcessor.cpp`は現在1,144行なので、製品変更が必要な責務を先行コミットで500行以下のmoduleへ抽出し、baselineを下げる。
`HyphaReferenceComponent.cpp`は495行、workspace testは496行のため、表示投影と新しい交差試験をそれぞれ別fileへ置く。
CMakeの追加登録も専用moduleへ分け、既存巨大fileを増やさない。
行数のために無関係な計測・描画責務を同時分割しない。

## 8. 検証の主役を、固定した実行順序と実出力にする

レビューの一時probeを、実コードをlinkする専用のnative回帰試験へ移す。
private memberの直接初期化に依存する最終試験にはせず、準備・公開・callback・receiptという実際の境界からfixtureを入れる。
テスト専用の制御点で交差順序を固定し、製品buildには停止hookを含めない。
大量反復で偶然失敗しなかったことをSR2の合格根拠にしない。

| 試験 | 固定する交差・条件 | 合格値・結果 |
| --- | --- | --- |
| SR1 | 開始採用 → 減衰Aを実出力 → source失効 → 次callback | 入力0.5・−6 dBならHeld出力`0.5 × 10^(-6/20)`、誤差1e-6以内。明示復帰前の通常出力receipt・gate解放は0 |
| SR2 | 公開直前 / 直後、最初のpin前 / 後にcallbackと失効を差し込む | 有効条件の自己失効0。失効前に出力した場合はHeld、未出力なら取消確認。受付だけで聴取中にしない |
| 明示復帰 | Held → Return要求 → 空・停止callback → 適格callback | 不適格callbackで完了0。適格callbackで通常Aがbit identical、同sessionの解放1回 |
| 復帰と再失効 | 権限喪失 → 明示復帰、ReturnPending中の再失効・同要求再送 | 新規試聴0。通常Aの実出力確認で解放可能。復帰要求の消失0・二重解放0 |
| 世代・予約 | 旧prepare完了、旧開始/復帰receiptを新session開始前後へ配送 | 別sessionの採用・gain上書き・gate解放0。旧session退役前の再開始公開0 |
| 自動入口 | timer更新、UI閉鎖、source/Cue変更、権限喪失、heartbeat停止、再prepare、worker再起動 | 通常復帰request発行0。権限なし/offline/bypassのAはbit identical。再開時の比較自動開始0 |
| 受付・遅延 | queue満杯、連打、準備job遅延、UI破棄、receipt消費遅延 | 有界メモリ。失敗の成功扱い0。復帰要求消失0。RT待機0 |
| Trial一連 | 開始 → 複数callback → 1 / 2 → 回答 → Reveal → 失効 → 保持 → 通常復帰 | 両側の実聴取条件とcommitmentが一致。Held/通常Aの誤加算0 |
| 既存通常B | original / Gain Match、B→Blind、source交換、snapshotと描画の交差 | 表示と実gain一致、1callbackの二重出力0、観測だけによるB解除0 |
| 所有権・停止 | publish/pin/retire/collectとprepare・destructorの交差 | RTのalloc/free/lock/I/O 0、使用中PCMの解放0、teardown deadlock 0 |
| 正本・排他 | PRE/POST・mono/stereo・Reference/local dormant・Record開始拒否と通常復帰 | 計測前後の入力bit identity、0 samples latency、正本Record不変。失敗による別予約解除0 |

SR3は単なる旧名の一括置換で完了としない。
xtaskにRT入口と検査対象source/functionをまとめる小さな共通定義を置き、`rt_safety`と`os_access`で共有する。
関数がない場合は明示failにし、空の検査でpassさせない。
`processComparisonPaths`からcapture lane、publication、local trial、Referenceの新RT部品まで、監査対象の呼出し先を列挙する。
禁止処理を含むfixtureと関数欠損fixtureで、検査自体が失敗できることも確認する。
この文字列ベースの契約をRT安全性の完全証明とは呼ばず、native側のallocation監視・退役thread検証と組み合わせる。

新しい小さなsession / publicationのnative試験に絞ってASanとTSanを各1回実行する。
全プロジェクトをsanitizer付きで繰り返しビルドしない。
sanitizerが環境理由で動かない場合は理由を分けて記録し、未確認を競合解消済みとはしない。

## 9. 実装順と省エネの検証工程

| 工程 | 成果物 | 次へ進む条件 |
| --- | --- | --- |
| RS0：境界の抽出 | 変更する巨大fileの責務抽出、全呼出し元の分類表、SR1/SR2再現fixtureと対象suite一覧 | 機能変更を混ぜない抽出。既知の失敗と新規の失敗を区別できる |
| RS1：一括置換 | 所有者・準備・RT session・gate・UI・Historyを全経路で移行し、旧mutatorを削除。SR3の登録と新native試験も同じ変更集合へ含める | 開始から退役までの全遷移が通る。互換目的の旧writerが残っていない |
| RS2：最終候補の検証 | 対象native、sanitizer、同候補の全体ゲートとOS別結果、実装証拠表 | 下記の完了判定。失敗・外部未検証は残件として記録 |

指摘ごとの小修正を出荷しながら重ねる工程にはしない。
RS0の責務抽出後、RS1は相互依存する変更をすべて含めた整合した実装として出す。
新sessionだけ作ってUIや通常Bを旧経路に残した段階を「構造修正完了」としない。

実装中は変更範囲の短いnative試験と対象xtaskに限定する。
最終候補を固定した後、`scripts/test_release_source.sh`による全体ゲートは1回だけ実行する。
同scriptのRust通常試験・xtask・clippy・ignored FFIを別途重複実行しない。
追加するnative試験をscriptに登録し、全体実行前に既存suiteとの対応表を作る。
FFI変更時のignored parity / pairing_candidatesは一覧件数を実測して全件実行し、script内の実行を充当する。

全体実行が途中で失敗した場合は、失敗箇所・影響箇所・未実行分だけを修正後に実行する。
修正前の結果は無関係なsuiteにだけ引き継ぎ、実行commit・差分・再確認範囲を記録する。
「全体1回」の方針を理由に、変更後の影響試験を省かない。

同じ候補からmacOS / Windowsで新native試験とPRE/POST共通shellのbuildを確認する。
実DAWでの開始・中断・停止再開・通常復帰と音量保持は、部品試験と分けて結果を残す。
Windowsを未確認のまま全OS合格としない。Windows操作前には共通Runbookを読む。
製品配置や署名へ進む場合は既存release Runbookを適用し、LS用PKGを含む3チャネルを揃える。
本計画の作成は配置・公開を行ったことを意味しない。

## 10. 完了の判定と、その後の接続

完了表には「計画した責務 → 実装ファイルと所有thread → 廃止した旧入口 → 実行した試験 → commit」を記録する。
旧lifecycle・独立したBlindのactive epoch・通常経路の無指定gate解放が残る場合は未完了とする。
UIの観測から通常復帰が発行される経路、auto invalidationから通常復帰へ入る経路も残存0を確認する。
SR1〜SR3、全遷移、通常B、正本A、Record排他、対象sanitizer、最終ゲートを区別してpass / fail / skipを記録する。
既存4件のテスト失敗が解消しただけでは完了にしない。

Referenceのsession境界を閉じた後、既存統合計画のローカルPRE/POST Blindへ戻る。
接続順は、非RT開始所有者 → request/poll/arm/armed → PRE PCM回収とpair barrier → 同一区間・PDCの実証 → 試聴開始排他とUI → 実ホスト検証とする。
共有するのは公開・寿命・出力確認・予約の境界であり、Referenceのfile authorityとPRE/POSTのexact pair / clock authorityを同一視しない。
capture slotがあることを、PCM transportやPDC成立の証明にはしない。

## 11. B-748実装候補の証拠

| 計画した責務 | 実装と所有境界 | 廃止・防止した旧入口 | 確認結果 |
| --- | --- | --- | --- |
| 開始sessionの一括公開 | `ReferenceSessionTypes.h`と`ReferenceRuntimeV2BlindLifecycle.cpp`。非RTがID・epoch・gate tokenを設定し、releaseでArmedを公開 | Activeを先に公開して別atomicへepochを書く順序を廃止 | 固定交差試験でStartingから最初の実出力後だけActive。混合identity 0 |
| 自動失効と通常復帰 | `ReferenceRuntimeV2Selection.cpp`、`ReferenceRuntimeV2BlindLifecycle.cpp`、`ReferenceRuntimeV2Realtime.cpp` | source・権限・transport・heartbeat失効から`endBlind` / `selectA`を呼ぶ経路を廃止 | 出力済み−6 dBをsource交換後も保持。明示復帰callback前の解放0、同じtokenを1回だけ解放 |
| RT lifetime | `ReferenceRuntimeV2BlindRealtime.cpp`。callback pin後に非atomic PCM条件を読み、clear / retireはreader返却を待つ | pin前のframe / channel読取を廃止 | native、ASan、TSan pass。RT静的禁止語契約pass |
| 表示・権限 | `PluginEditorReference.cpp`、`HyphaReferenceComponent.*`、`HyphaReferenceAccessPanel.h` | UI timerによる自動終了を廃止。権限喪失で保持表示を隠す経路を廃止 | Startingでは比較・回答を隠して終了操作だけ表示。Heldはアクセス画面より優先 |
| History | `ReferenceRuntimeV2Events.cpp` | 要求受理だけでBlind startを書く経路を廃止 | 同一sessionの`firstCallbackSequence != 0`だけを開始receiptとして消費 |
| 検証面 | `xtask/src/rt_contract_surface.rs`、`rt_safety.rs`、`os_access.rs`、Windows CI | `renderComparisonOutputs`という旧関数名への監査を廃止 | `processComparisonPaths`と実calleeを監査。対象12件 + 権限1件pass。Windowsは同じnative targetをCIへ登録 |

2026-09-08の対象確認は、Debug PRE/POST AU・VST3 build、Reference runtime / component native、source line budget、対象xtask、Reference runtimeのASan / TSanがpassした。
ASanはmacOSで非対応のLeakSanitizerだけを無効にし、AddressSanitizer本体は`halt_on_error=1`で完走した。
最終sourceゲートは一度だけ開始し、先頭の`cargo fmt --check`で整形差分を検出した。
整形後は先頭から重複実行せず、未実行部分を継続した。
通常Rust、FFI、Release性能probe、native 4 target、xtask 138件はpassし、固定件数が旧5件だった`pairing_candidates`で停止した。
登録済みの未命名exact pair Blind handshakeを含む実測6件へ、`scripts/test_release_source.sh`、`AGENTS.md`、`hypha_invariants.md`、Windows readiness契約を同期した。
影響するWindows readiness 1件、ignored parity 20 / 20件、pairing_candidates 6 / 6件、release-owned Clippy警告ゼロまでpassした。
最終自己レビューでは、保持中の返却案内がKirin OS接続表示に上書きされる順序と、旧新sessionのatomic identity読取が混成し得る窓を追加で修正した。
session sequenceをidentityのpublication markerとして最後に公開し、128回の入替と並行読取をnative / TSanで確認した。
稼働中fixtureのJSON直接上書きも製品契約と同じatomic置換へ直し、遅いsanitizer実行中はA bindingを更新するようにした。
最終差分でReference runtime / component native、ASan、TSan、POST VST3増分build、権限表示契約、行数、fmtを再確認し、すべてpassした。
Windows CIの実行結果とStudio Oneでの開始・自動中断・停止再開・明示通常復帰は未確認なので、両OS・実DAWの完了判定には含めない。
配置、署名、notarize、公開は行っていない。
