# Hypha TRACE Record inbox／runtime再構成／クラッシュ復旧 引き継ぎ

> 履歴資料：2026-09-13のGit整理で、従来ローカルにのみ存在した文書を保存した。
> 以下の進捗、未push、CI、配置状況は文書作成・更新時点の記録であり、現在の状態を示さない。
> 現行の契約はREADMEと`docs/hypha_invariants.md`、B-836の修復と残る実機検証は`docs/hypha_structural_repair_plan_20260912.md`を参照する。

**Handoff**

- To: 次の独立したTRACE耐障害セッション
- From: Codex 2026-08-06
- What: Record inboxの退役フェンス、runtime再構成、起動時クラッシュ復旧を、安定版から隔離した別タスクとして完成させる。
- Why: 完成すればTRACEのクラッシュ耐性と世代混在防止に有用だが、TRACE、通常測定、ペア、次回起動を直接壊し得る高リスク変更である。
- Next: 現在の`origin/main`から専用ブランチを作り、退避stashを参照資料として使い、復旧状態機械と異常系テストから設計する。
- Ref: 本書、`docs/hypha_invariants.md`、stash commit `0857949cc41c05538bfd19f9f47e60868db6ed3e`

## 1. 結論

この機能は、完成すれば有用である。

ただし、ペア瞬断を直すために必要な修正ではない。

目的は、DAWの状態復元、サンプルレート変更、プロジェクト変更、worker停止、プロセスクラッシュを跨いでも、TRACEの制御通知と計測世代を混在させないことである。

現在の退避実装には起動時クラッシュ復旧がなく、安定版へ採用できない。

安定版へ混ぜず、独立ブランチで完成と検証を行う。

合格条件をすべて満たすまで、インストール、リリース、安定版へのマージを行わない。

## 2. このタスクの対象

Record inboxは音声データではない。

POSTから、ペア相手である特定PREへ「TRACEを開始・終了する」という制御通知を渡す小さなファイルである。

通常の流れは次のとおりである。

```text
POSTがTRACEを開始する
  → ペア相手のPRE専用Record inboxへ通知する
  → PREとPOSTが同じsession／generationで計測する
  → TRACE終了通知とfinalizeを完了する
```

このタスクは次の三領域を扱う。

1. PRE runtime再構成中の新規TRACE受付制御
2. 古いPRE宛てRecord inboxへの誤配信防止
3. 処理途中でクラッシュした場合の次回起動時復旧

Audio Threadの音声処理、Watchの通常測定、ペア所有権そのものを改造することは目的ではない。

## 3. 解決対象となる実在の競合

次の順序で競合が起こり得る。

```text
PRE内部のruntime入れ替えを開始する
  → 古いPRE workerが終了する
  → 旧POSTが古いPRE宛てinboxへTRACE通知を送る
  → 通知を受け取るworkerが存在しない
  → 通知がPendingのまま残る
  → 次のTRACEが開始できない、または旧世代と新世代が混ざる
```

対象となる契機には、DAWの状態復元、サンプルレート変更、プロジェクト変更、plugin runtime再生成が含まれる。

## 4. 退避実装で試みていた構造

### 4.1 Record Admission Gate

PREの再構成中だけ、新しいTRACE受付を制限する制御面のゲートである。

退避実装には次の三状態がある。

| 状態 | 意味 |
|---|---|
| `Normal` | 通常の新規TRACEを受け付ける。 |
| `Blocked` | 新規TRACEを受け付けない。 |
| `CommittedTargetDrain` | 再構成直前にcommit済みの旧TRACEだけを完了させる。 |

`CommittedTargetDrain`の狙いは、開始済みTRACEを再構成要求だけで途中破棄しないことである。

一方で、三状態化は通常のTRACE経路にも遷移不備の危険を持ち込む。

`Blocked`が解除されなければ、以後のTRACEが開始不能になる。

このゲートは制御面だけで扱い、Audio Threadから読み書きしてはならない。

### 4.2 Record inboxの退役フェンス

古いPRE宛てinboxを一時的に書き込み不能にし、旧POSTが通知を置けないようにする仕組みである。

退避実装は、既存ファイルを固有名の`.retired.*`へ移し、元のinboxパスへディレクトリを作成する。

旧POSTが通常ファイルとしてatomic renameしようとすると失敗するため、古い宛先への通知投入を抑止できる。

通常終了時はディレクトリを除去し、必要な旧ファイルを元へ戻す。

この方式は旧バージョンPOSTとの互換競合に有効だが、プロセスクラッシュ時には通常のDropや終了処理を期待できない。

### 4.3 起動時クラッシュ復旧

ここが未完成であり、現在の採用を妨げる中心的な問題である。

フェンス設置後にPREまたはDAWが終了すると、元のinboxパスにディレクトリが残り、`.retired.*`も残り得る。

次回起動時は少なくとも次を区別する必要がある。

- 元へ戻すべき旧通知
- 破棄してよい終了済み通知
- 現在のTRACEが正当に所有する通知
- 別PRE、別session、別generationに属する通知
- 内容が壊れた通知、由来を証明できない通知

この分類を推測で行ってはならない。

所有者、対象PRE、session、generation、処理段階を永続情報から証明できる設計が必要である。

## 5. 現在の退避場所

未完成コードはGit stashへ保存されており、現在の安定版には適用されていない。

stash番号は増減するため、`stash@{0}`ではなく次のobject IDを参照する。

| 種別 | object ID |
|---|---|
| stash commit | `0857949cc41c05538bfd19f9f47e60868db6ed3e` |
| 作成時base | `ba3df52b8676e6b52de490d9fc01c295fd54f26d` |
| index parent | `63fb0ad10bb15c4139a62cac0753b9fc8d253a60` |
| untracked-files parent | `ebc48733cb3b6a9a0fa0e65b28820a847bcb4310` |

このstashは119ファイル、約`+29,606 / -5,712`を含む巨大な作業退避である。

すでに別途採用されたpair所有権強化と、未完成のruntime再構成、FFI、Record inbox、worker、GUI関連変更が混在している。

## 6. 絶対に行わないこと

`git stash apply`または`git stash pop`で、このstash全体を現在の`main`や安定版ブランチへ適用してはならない。

stash全体の適用は、すでに完成したpair修正を古い途中状態で上書きし、無関係な未完成変更を再導入する。

B-455／B-456で採用済みのpair所有権処理は、現在の`main`側を正として維持する。

必要な設計とコードだけを読み、現在の実装へ合わせて新しく組み直す。

安定版のPRE／POSTへ試験途中のバイナリをインストールしない。

起動時復旧が未完成のまま退役フェンスを有効化しない。

復旧不能な状態を「古いので削除」と推測して消さない。

Audio Threadへlock、allocation、I/O、待機、ファイル判定を追加しない。

## 7. 参照対象の候補ファイル

次のファイルはstashのuntracked-files parent `ebc48733cb3b6a9a0fa0e65b28820a847bcb4310`などに含まれる参照候補である。

### Record受付と退役フェンス

- `crates/kirin_measure/src/record_admission_gate.rs`
- `crates/kirin_measure/src/record_ingress_restart.rs`
- `crates/kirin_measure/src/record_signal/target_retirement.rs`
- `crates/kirin_measure/src/record_signal/restore_gate_tests.rs`

### PRE runtime再構成

- `crates/hypha_pre/src/runtime_coordinator.rs`
- `crates/hypha_pre/src/runtime_coordinator_loop.rs`
- `crates/hypha_pre/src/runtime_cutover.rs`
- `crates/hypha_pre/src/runtime_identity.rs`
- `crates/hypha_pre/src/runtime_transaction_tests.rs`
- `crates/hypha_pre/src/runtime_workers.rs`

### FFIとhost state復元

- `crates/kirin_hypha_ffi/src/runtime_reconfigure.rs`
- `crates/kirin_hypha_ffi/src/runtime_reconfigure_support.rs`
- `crates/kirin_hypha_ffi/src/runtime_reconfigure_tests.rs`
- `crates/kirin_hypha_ffi/src/host_state_restore.rs`
- `crates/kirin_hypha_ffi/src/write_enable_transaction_tests.rs`
- `crates/kirin_hypha_ffi/src/stop_poison_recovery_tests.rs`

### worker再起動

- `crates/kirin_measure/src/worker_activation.rs`
- `crates/kirin_measure/src/watchdog_restart.rs`

ファイル名は影響範囲の手掛かりであり、そのまま採用してよいという意味ではない。

stash作成後に現行コードが変わっているため、現在の`main`との差分を一ファイルずつ確認する。

## 8. 安全な参照方法

次セッションは現在の`origin/main`から専用ブランチまたは専用worktreeを作る。

例として、専用ブランチ名は`codex/hypha-trace-inbox-recovery`とする。

```bash
git fetch origin
git switch -c codex/hypha-trace-inbox-recovery origin/main
```

既存のdirty worktreeがある場合は、その変更を移動、破棄、上書きせず、新しいworktreeを使う。

stash全体の一覧は次のようにobject IDで確認する。

```bash
git diff --name-status ba3df52b8676e6b52de490d9fc01c295fd54f26d 0857949cc41c05538bfd19f9f47e60868db6ed3e
```

untracked側の個別ファイルは次のように読む。

```bash
git show ebc48733cb3b6a9a0fa0e65b28820a847bcb4310:crates/kirin_measure/src/record_admission_gate.rs
git show ebc48733cb3b6a9a0fa0e65b28820a847bcb4310:crates/kirin_measure/src/record_signal/target_retirement.rs
```

対象がstash commit側にある場合は、同じ要領で`0857949cc41c05538bfd19f9f47e60868db6ed3e:<path>`を読む。

ファイルを取り込む前に、現在の`main`にある同名または関連コードを先に読む。

## 9. 次セッションで最初に確定する設計

実装を再開する前に、復旧状態機械と永続情報の形式を決める。

少なくとも次の情報を、クラッシュ後にも検証可能な形で保持する必要がある。

- 対象PREの安定したidentity
- ペア相手POSTのidentity
- TRACE session ID
- generation
- inboxの元パスと退役先
- フェンス設置、runtime commit、cleanupの処理段階
- 通知がPending、開始済み、終了済みのどれか
- 記録形式のversion

sidecar manifestまたは同等のtransaction recordを使う場合は、manifest自体の途中書き込みと破損も扱う。

書き込みは同一filesystem内のtemp fileからatomic renameし、ディレクトリ同期が必要かをmacOS上で検証する。

復旧走査は対象PREの正確なnamespaceへ限定し、各instanceが`/tmp/kirin`全体を無差別に変更してはならない。

## 10. 復旧判断の最低要件

次の表を出発点として、テスト可能な決定表へ確定する。

| 起動時の形 | 必要な判断 |
|---|---|
| 通常ファイル、`.retired.*`なし | 通常状態として扱う。 |
| ディレクトリフェンス、対応する退役ファイルあり | transaction recordとsession／generationを検証して復元、継続、破棄を決める。 |
| ディレクトリフェンス、退役ファイルなし | 処理段階を証明できる場合だけ安全な状態へ収束させる。 |
| 元パス不在、退役ファイルあり | 所有権と処理段階を証明できる場合だけ復元する。 |
| 同一対象に複数の退役ファイルあり | 一意な正当所有者を証明できなければfail closedとする。 |
| malformed、future version、別identity | 誤ったTRACEを開始せず隔離し、通常測定とAudio Threadへ波及させない。 |

「fail closed」は誤ったTRACEを開始しないという意味であり、Watch、音声素通し、ペア所有権まで止めるという意味ではない。

利用者操作と紐づかない内部の互換fallback失敗はR-28に従って無言でskipできる。

利用者が明示的に開始したTRACEを実行できない場合は、成功したように見せず、既存の通知方針に従って失敗を明示する。

## 11. 全クラッシュ地点を列挙する

最低でも次の各地点で強制終了し、次回起動後の収束を検証する。

1. 退役対象の検査前
2. 元ファイルを`.retired.*`へ移す前
3. 移動後、ディレクトリフェンス作成前
4. ディレクトリフェンス作成後、旧runtime停止前
5. 旧runtime停止後、新runtime起動前
6. 新runtime起動後、commit前
7. commit後、フェンス解除前
8. 退役ファイル復元または破棄の途中
9. フェンス解除後、Admission Gateを`Normal`へ戻す前
10. TRACE finalizeの途中

各処理は再実行可能であり、二度走っても状態を壊さない必要がある。

rollback失敗時にも、Audio Thread、Watch、確立済みpairを巻き込んではならない。

## 12. 守るべき不変条件

### TRACE

- TRACE中の再構成要求でも、commit済みの現在TRACEを最後まで完了できる。
- 新旧sessionまたはgenerationを一つのTRACEへ混在させない。
- 古いPOSTからの通知で、新runtimeのTRACEを誤開始しない。
- Admission Gateが意図せず`Blocked`のまま残らない。

### Watchと通常測定

- TRACE制御面の復旧失敗でWatch計測を停止させない。
- Measure／IO workerの切替失敗時は、検証済みの旧runtimeへ完全に戻す。
- 旧runtimeへ戻せない場合もAudio Threadを止めない。

### Pair

- 現在のB-455／B-456 pair所有権強化を維持する。
- runtime再構成中も、pair claimのcommit／rollbackを不整合にしない。
- PAIR表示と所有権が一瞬も外れないことを確認する。
- TRACE復旧のために相手PREを再探索し、別PREへ勝手に付け替えない。

### Audio Thread

- 入出力はビット同一である。
- レイテンシーは0 samplesである。
- lock、allocation、I/O、blocking、待機を追加しない。
- Measure／IO／復旧処理のpanicや停止をAudio Threadへ伝播させない。

## 13. 必須の検証行列

少なくとも次の組合せを自動テストとStudio One実機テストへ落とす。

| 軸 | ケース |
|---|---|
| PRE／POST世代 | 新PRE＋新POST、新PRE＋旧POST、可能なら旧PRE＋新POST |
| 動作 | Watch、TRACE開始、TRACE中、TRACE終了、finalize |
| 再構成契機 | DAW state restore、sample rate変更、project変更、worker restart |
| filesystem状態 | 通常ファイル、ファイル不在、ディレクトリフェンス、単一`.retired.*`、複数`.retired.*`、malformed |
| 終了地点 | 第11節の全クラッシュ地点 |
| rollback | 旧runtime復帰成功、復帰失敗、新runtime起動失敗、cleanup失敗 |
| pair | 正常pair、相手消失、旧claim残存、再起動後の同一pair |

テストは正常系だけでなく、ファイル不在、権限不整合、破損、再起動後、重複実行を含める。

## 14. 自動テストの必須ゲート

変更範囲に応じて、少なくとも次を通す。

```bash
cargo fmt --all -- --check
cargo test --workspace
cargo clippy --workspace --all-targets -- -D warnings
```

`kirin_hypha_ffi`を変更した場合は、通常のworkspace testだけでは不十分である。

次のignoreスイートを直列実行する。

```bash
cargo test -p kirin_hypha_ffi --test parity -- --ignored --test-threads=1
cargo test -p kirin_hypha_ffi --test pairing_candidates -- --ignored --test-threads=1
```

テスト数は`-- --ignored --list | grep -c ': test'`で実測し、parity 20件、pairing_candidates 5件の合計25件を確認する。

配布経路へ進む前に、既存のrelease source contract、pluginval、auvalも現在のリポジトリ手順に従って通す。

## 15. Studio One実機の合格条件

- TRACE開始、継続、終了、finalizeが成立する。
- TRACE中のtransport stop、再開、DAW state restoreを跨げる。
- Watch測定が停止しない。
- PRE／POSTのPAIR表示と所有権が一瞬も外れない。
- Studio One再起動後に残留フェンスから自動復旧する。
- 新旧PRE／POST混在時に誤ったTRACEを開始しない。
- 音声がビット同一で素通りする。
- レイテンシーが0 samplesである。
- worker異常や復旧失敗でDAWが停止またはクラッシュしない。

## 16. 次セッションの作業順序

1. 本書、ルート`AGENTS.md`、`docs/hypha_invariants.md`、現行pair実装を読む。
2. 現在の`origin/main`から独立ブランチまたはworktreeを作る。
3. stashの候補ファイルと現行コードの影響範囲を先に一覧化する。
4. 復旧状態機械、永続transaction record、決定表を設計する。
5. 第11節のクラッシュ地点を再現するテストを先に作る。
6. Admission Gate、退役フェンス、runtime transactionを現在のコードへ最小単位で再実装する。
7. TRACE、Watch、pair、Audio Threadを個別に回帰検証する。
8. 全ゲート通過後にDaisukeへ結果と残存リスクを報告する。
9. Daisukeの承認前に安定版へマージ、インストール、リリースしない。

## 17. 完了の定義

「クラッシュしなかった」だけでは完了ではない。

全クラッシュ地点から次回起動時に一意で安全な状態へ収束し、TRACE、Watch、pair、Audio Threadの不変条件を同時に満たしたときに完了とする。

復旧不能または所有権不明のデータがあっても、誤ったTRACEを開始せず、音声素通し、Watch、確立済みpairへ波及させないことを確認する。

この作業はペア瞬断修正とは別のTRACE耐障害タスクとして扱う。

## 18. 現在の保全状態

- 未完成コードはstashに残っている。
- stashは適用していない。
- 安定版コードは変更していない。
- ビルド、インストール、リリースは行っていない。
- Notionへの書き込みは行っていない。
