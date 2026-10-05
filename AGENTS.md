# Kirin Hypha — Contributor contract

## 最初に確認するビルド・公開入口

ビルド・署名・配布・HP反映を行う前に、`docs/hypha_release_entry.md`を読む。
入口は`node scripts/build_hypha.mjs`一つ。まず採用commitと作業checkoutを確認し、以下のhelpから目的を選ぶ。

|目的|入口|手順書|
|---|---|---|
|GUI / DSP用の検証プラグインを簡単に作る|`node scripts/build_hypha.mjs --without-aax`|`docs/hypha_build_entry.md`|
|全formatの未署名診断build|`node scripts/build_hypha.mjs --help`|`docs/hypha_build_entry.md`|
|正規build・署名・公証からHPアップまで|`node scripts/build_hypha.mjs --release --help`|`docs/hypha_release_entry.md`|

MacはPRE/POST × AAX/AU/VST3 Universal、WindowsはPRE/POST × AAX/VST3 x64。AUはApple専用。
`--without-aax`はSDK不要の明示的な検証専用。全formatや通常Pro Tools用署名・公開gateを代用しない。
helpはSDK・認証・USB不要。未署名buildにもiLok不要。既存の成果物・CI・private stateは照合して再利用し、
この案内のためにbuild / CI / 署名を起動しない。実機受入、LS担当者工程、公開承認、3チャネル条件は維持する。
途中再開のprivate profile・証跡は作業checkoutのignored `release_state/`で確認し、管理URLや秘密値を転載しない。
入口が無い古いworktreeには必要差分の統合が必要。別checkoutのscriptを絶対pathで実行して代用しない。
詳細手順・安全契約は上記の文書を正本とする。

## 公開リリース3チャネル（全件必須）
Hyphaの公開リリースは、以下の3チャネルを同じバージョンで揃えて初めて完了とする。

1. Lemon Squeezy: signed / notarized済みmacOS Universal `.pkg`
2. HP無料配布: signed / notarized済みmacOS Universal `.zip` + GitHub Release + 英日HPリンク
3. Windows: 同一コミットのgreenな`windows-latest` CI artifactから作る、PRE/POST同梱・
   Authenticode署名済みInno Setup `.exe`（payload / installer / uninstallerの全署名と
   install → 同版再install → 旧公開版からのupgrade → uninstall検証がgreenであること）。
   手動VST3 `.zip`は診断・復旧用fallbackのみ

Windows版を暗黙に外すこと、macOSだけを先に公開してリリース完了とすることは禁止。
同一コミットの署名済みWindows installer artifactが無い、CI / pluginval / installer実動検証が未完了、
または必要な外部検証が未完了の場合は、
Windowsを省略せず公開リリース全体のblockerとして報告する。
詳細は`docs/ls_release/kirin_hypha_ls_runbook.md`の「Distribution channels (ALL updated every release)」に従う。

## LSアップ用パッケージ準備（リリース作業では必須）
Hyphaを release build / notarize / install したセッションでは、作業完了前に必ず Kirin OS 式の
Lemon Squeezy 用 `.pkg` まで準備する。

```bash
node scripts/ls_release/build_kirin_hypha_pkg.mjs
node scripts/ls_release/kirin_hypha_ls_dry_run.mjs \
  --state release_state/kirin_hypha_X.Y.Z_ls.state.json \
  --with-apple-verification
```

`release_state/` はローカル専用かつ gitignore 対象。
`docs/ls_release/kirin_hypha_ls_state.example.json` から作成し、商品ID・管理URL・担当者メモをコミットしない。
公開する検証情報は `dist/` に生成される `.pkg.json` / `.sha256` を GitHub Release に添付する。

成果物:
- `dist/LS_UPLOAD/Kirin-Hypha-X.Y.Z-macOS-Universal.pkg`
- `dist/LS_UPLOAD/Kirin-Hypha-X.Y.Z-macOS-Universal.pkg.sha256`
- `dist/LS_UPLOAD/Kirin-Hypha-X.Y.Z-macOS-Universal.pkg.json`

`Developer ID Installer` 証明書が無く signed `.pkg` を作れない場合は、LSアップ用を ready と言わない。
`blocker: Developer ID Installer certificate missing` と明記する。`UNSIGNED-DO-NOT-UPLOAD.pkg`
は payload smoke test 用のみで、LSには絶対にアップロードしない。

## プロジェクト概要
Kirin Hypha は Kirin OS と連携する計測プラグイン。出荷shellはJUCE共通実装で、AAXは既定OFFの追加formatとする。通常の計測経路ではDAW入力を加工せず、
利用者が明示した比較試聴では登録済みReferenceを非破壊再生できる。
PRE/POST の2バイナリでマスタリングチェインの前後を計測し、差分（Δ）を表示する。
ライセンス: GPLv3（オープンソース公開）。Kirin OS本体（プロプライエタリ）とは完全分離。


## 技術スタック
- 計測core: Rust、JUCEとの境界: `kirin_hypha_ffi` C ABI
- ebur128 クレート（LUFS/TP）
- 出荷processor / GUI: JUCE共通shell（macOS AU / VST3、Windows VST3、追加AAX）
- nih-plug: 旧VST3 identityとstate互換性を検証するlegacy経路。出荷GUIの選定待ちではない
- 全formatの統合入口: 本書冒頭の「最初に確認するビルド・HPアップ入口」。詳細は`docs/hypha_build_entry.md` / `docs/hypha_release_entry.md`を正本とする
- AAX不要のmacOS通常build: `scripts/build_juce_universal.sh`。署名は`docs/aax_build_signing_entry.md`、Windows配布は対応手順書を正本とする
- 対象DAWと受入状況: `README.md`と各hostの検証記録を参照し、formatの生成成功をhost対応完了としない

## 絶対原則

### R-12 製造境界（不変）
通常のPRE/POST計測経路では、HyphaはDAW入力を生成・加工・減衰・遅延させない。A経路は0 samples latency、
入出力bit identicalを維持し、元音源と正本のPRE/POST測定・Recordを書き換えない。

利用者の明示操作によるReferenceの比較試聴は、この禁止対象に含めない。登録済みの不変なReferenceを
試聴用B経路で再生し、試聴コピーにだけ一時的なGain Matchを適用できる。Referenceファイル、A経路、
正本のPRE/POST測定・Recordは変更せず、接続、読込、復元だけでBへ自動切替しない。offline render、
Reference欠損、検証失敗時はA経路を維持する。

利用者の明示操作によるlive PRE/POST比較試聴も、この禁止対象に含めない。同じchainのPREが公開した
直前の入力を、POSTは時計の規則で対応を確かめた範囲だけ試聴用B経路で出力し、試聴コピーにだけ
承認済みのgainを適用できる。PREからPOSTへの転送経路はplatformごとに事前確保し、方式は実測で決める。
対応は連続時計、定常区間で較正した差K、PREの周回、各側の呼出しの空白で確かめ、時計の一致や到着
だけでは受け入れない。確かめられない区間はPOSTを出力し、PREの選択と承認済みのgainは保持して、
確かめられた最初のblockからPREへ戻す。PREの入力、A経路、正本のPRE/POST測定・Recordは変更せず、
接続、読込、復元だけでBへ自動切替しない。offline render、検証失敗、説明できない欠落、pair／format
失効ではPOSTを維持して比較を中断し、再選択まで戻さない。この対応は、PREとPOSTの間のpluginが遅延を
正しく報告し、DAWが遅延を補償することを前提にする。前提が崩れた疑いは利用者に警告し、遅延が変わる
設定変更の直後の短い区間に時計で検出できない誤対応があり得ることも示す。
（技術契約: `docs/planning/hypha_live_chain_compare_contract_draft_20260928.md`）
PREの増幅が比較ceilingを超えるMATCHでは、利用者の明示承認の後だけ、PREを原音量に保ってPOSTの出力を
差だけ固定減衰できる（Local Blindと同じ方式）。減衰は試聴の終了後も、利用者が明示して通常の音量へ
戻すまで保持し、急に上げない。offline render、hostが知らせるbypass、他の試聴が出力を取るblockには
適用せず、正本のPRE/POST測定・Recordは減衰の前で取る。（2026-09-28技術契約）
明示MATCHの後、利用者が明示して選んだ追従（AUTO）だけは、そのMATCHのgainから±6 dB以内、そのMATCHで
承認したceiling以下で、PREの試聴コピーのgainを50 msのrampで動かせる。POSTとceilingは動かさず、範囲を
出るときは追従を止めて通知する。追従はBlindへ持ち込まない。（2026-09-28技術契約）

Audio Thread（processBlock）は通常計測では読み取り・コピー・通知だけを行う。Reference比較試聴では、
非RT側で検証・decode・準備した事前確保済みReference bufferの選択とRT-safeな出力だけを許可する。
live比較では、PREは事前確保済みの転送領域への書込みと周回の公開だけを、POSTは同じ領域の読取り、
対応の判定、承認済みPOST減衰の適用を含むRT-safeな出力だけを許可する。
いずれもAudio Threadでのアロケーション、ロック、ブロッキングI/Oは禁止する。

### R-13（Hub & Spoke）
`work.json`（schema: `work.schema.json`）が全システムの接続点。各モジュール間はこの接続点を通じて連携する。売り切り。サーバー・アカウント不要。

### 3層隔離
```
Audio Thread   — 通常計測は読み取り・コピー・通知のみ。明示的な比較試聴は準備済みReference bufferの
                 選択・出力と、live比較の事前確保した転送領域の書込み・読取り・判定・出力だけを許可
                 （alloc/lock/IO 禁止）。絶対に落ちない
Measure Thread — 計測。クラッシュ → Audio Threadが検出 → 自動再起動
IO Thread      — /tmp/ 書き込み。クラッシュ → Audio Threadが検出 → 自動再起動
```
Audio Thread から見て、Measure/IO は「落ちても構わない」存在。
Audio Thread が止まる = DAWの再生が止まる = 利用者の作業が全壊。この優先順位は絶対。

### 分離原則
各モジュール（計測エンジン / GUI / IO / Audio Thread）は独立して動作し、1つが落ちても他が連動して落ちない構造にする。

## Contribution requirements

- 既存コード、実データ、設計契約と影響範囲を確認してから変更する。
- 通常A経路のbit identity、0 sample latency、RT安全性、Record整合性を維持する。
- 全対象role/platform/formatを確認し、一画面・一formatだけを直して他を放置しない。
- 表示と解決を検証した事実で報告し、正常系・エラーパス・境界値・復元を確認する。
- 外部API/format仕様を新たに使う場合は公式資料で確認する。
- ADVISORは価値判断を出さない。内部検証や互換fallbackの非操作失敗は無言でskipするが、
  利用者の明示操作が失敗した場合は通知する。
- GUIのCE 2226契約と実画面を確認する。調整済みPRESENCE値を無根拠に変更せず、
  全画面overlayへbackdrop-filterをかけない。
- ソース、テスト、ライセンス、公開検証方法を保持する。秘密値・個人PC情報・運用stateをcommitしない。
- PR検証は署名credential不要の環境で行う。署名・公証・正式配布はレビュー済みexact sourceと
  独立した権限境界で行い、CI成功だけで公開承認を推定しない。

## 開発ルール

### コード品質

#### 500行長期収束規約

- 新規のowned sourceは500行以下とする。未追跡ファイルも`check_source_line_budget.sh`の対象に含める。
- 既存の500行超ファイルは`source_line_budget.tsv`に記録した長期的な負債であり、全件分割をUI・製品機能への着手条件にしない。
- 分離作業は、原則として既存巨大ファイルへ製品変更を加えるときだけ行う。変更対象の責務を先行コミットで500行以下のモジュールへ抽出し、無関係な責務を同時に一括分割しない。
- 既存巨大ファイルの行数は増やさない。減少した場合は同じコミットでbaselineを現在値へ下げ、過去の大きさまで戻せないratchetにする。500行以下へ到達したらbaselineから削除する。
- RT安全性、Record整合性、または変更対象のテスト可能性を妨げている境界だけを優先分離する。それ以外の行数負債は製品価値に直結する作業を止める理由にしない。
- 全owned sourceを500行以下へ収束させることは長期目標であり、現在のUI着手ゲートではない。

- Rust: `cargo clippy` + `cargo test` を毎回実行
- **kirin_hypha_ffi の検証ゲート**: `cargo test --workspace` の green だけでは Record/pairing を検証しない（Record finalize・PRE-POST ペアリング・plugin_data 実出力のテストは realtime で遅いため全て `#[ignore]`）。kirin_hypha_ffi を変更したら `cargo test -p kirin_hypha_ffi --test parity -- --ignored --test-threads=1`（parity.rs 20 件）と `cargo test -p kirin_hypha_ffi --test pairing_candidates -- --ignored --test-threads=1`（pairing_candidates.rs 6 件）の #[ignore] スイート（計 26 件）の pass も検証ゲートに含める。件数は `-- --ignored --list | grep -c ': test'` で実測のこと（loose grep は散文中の `#[ignore]` を誤カウントする）。これらは CI（ci.yml）では PR / workflow_dispatch / `[ci full]` 時のみ走る（通常 push は test job ごとスキップ）。
- エラーログは作業前に必ず読む
- 同じアプローチは最大2回。3回目は別手法
- テスト: 正常系 + エラーパス + 境界値
- vendor/* 配下の clippy 警告は upstream 修正待ちとして監査対象外。kirin_hypha 本体の警告ゼロが品質基準

## Kirin Hypha 固有

### 現行製品面の正本

固定した項目数、画面寸法、テスト件数をこのファイルへ複製しない。
現行の利用者向け機能は`README.md`、不変条件は`docs/hypha_invariants.md`、表示契約は
`docs/hypha_meter_product_contract_20260831.md`と各実装計画を正本とする。

- 上位domainはLEVEL / TIME / FREQ / SPACE。TIMEにはHISTORY / ATTACK / SHARP / LIVEがある。
- PREは絶対観測、POSTは検証済みの同時刻PREがある場合だけ差分を表示する。PRE不在時もPOSTの
  絶対観測を捏造せず維持する。
- mono / stereoを基本範囲とし、機能ごとのrole / platform / host gateを維持する。
  exact 5.1（L, R, C, LFE, Ls, Rs）はLEVEL / TIMEの計測専用で、
  Record / Keep、Reference、local Blind、live比較、Hybrid VU、FREQ、SPACEは許可しない（INV-S33）。
  他layoutは拒否する。AAX等の実host受入は別に検証し、計測coreの引数やlayout宣言だけから完了を推定しない。
- macOSのPRE表示共有はatomic file、Windowsはpagefile-backed共有メモリを使う。platformごとの
  transport正本を確認し、`/tmp/`だけを全platform共通仕様として扱わない。
- Reference比較試聴、live PRE/POST比較、承認済みのローカルBlind（その前の記名A/Bを含む）は通常A経路とは別の明示操作である。
  Preference Listening TrialをABX識別検定や音質改善の証明と呼ばない。

### PRE/POST別バイナリ
同一コードベースから role 定数（PRE/POST）でビルド時に分岐。
利用者がDAWで「Kirin Hypha PRE」「Kirin Hypha POST」を別々に選ぶ。

### GUI

JUCE共通shellが出荷面であり、PRE / POSTとAU / VST3 / AAXは同じeditor実装を使う。
300×200から900×600まで3:2固定比でリサイズし、LEVEL / TIME / FREQ / SPACEとReferenceを表示する。
外観変更は`docs/hypha_ce2226_jungle_visual_system_20260901.md`と実画面を両方確認する。

### Measurement core

Rust計測coreとJUCE共通shellは本リポジトリの公開sourceで検証する。
proprietary製品のsourceを取り込まず、公開されたファイル形式・通信契約で連携する。

### AAX境界

AAX作業は最初に`docs/aax_build_signing_entry.md`を読む。モード選択、PACE顧客入力の参照先、
Mac KeychainとWindows private signing factoryの違い、完了条件を共通入口にまとめている。
機器固有の資料所在は、存在すればgitignore対象の`release_state/aax_signing_local_handoff.md`を参照する。
存在未確認の設定fileを前提に質問せず、顧客番号・認証情報・管理画面を公開repoやログへ転記しない。

AAXは既定OFFで、SDKとPACEツールはリポジトリ外に保つ。macOS Universal build/PACE署名と
Windows x64 build/PACE+Authenticode署名の単体経路は実証済み。B-786のIntel版Pro Tools実機では
Native load、再open、stereo/multi-mono、pairing、0 sample表示、Offline Bounceを確認済みだが、
後続commitへ証跡を流用しない。現在の配布候補にはexact commit、clean source、Native-only
stampまたはWindows signed provenanceを要求する。Kimeraフォントは任意の付加価値であり、
未搭載をbuild・署名・公証・配布のblockerにしない。指定時だけ外部OTFとlicense確認を要求する。
Windows installerはprovenance sidecarと
PRE/POST hashを同じrelease commitへ結び、別commitの署名済みAAXを受理しない。
AAXのローカルPRE/POST Blindの入口は2026-09-13の利用者指示により有効。
exact capture、clock/PDC連続性、開始排他の検証は維持する。実AAX hostのPDC実証は未完了であり、
入口の有効化を実機検証・公開リリースの完了根拠にしない。
macOSの正本は`docs/aax_macos_universal_build_20260910.md`、Windowsは
`docs/aax_windows_build_20260910.md`、Phase A境界は`docs/aax_phase_a_readiness_20260907.md`とする。

### 解決済みの初期調査

旧U-1〜U-8は初期prototypeの調査項目であり、現行実装の未検証一覧ではない。
新しい変更で外部APIやformat仕様を使う場合だけ、その変更に必要な公式資料を改めて確認する。

### Studio One テスト前チェック
- チャンネル設定が **Stereo** であることを確認する（Mono だと -3dB/ch 適用され計測値がずれる）
- テスト信号は `test_signals/` 内の S-1〜S-5 を使用

### ビルド・テスト

Contributor手順は[CONTRIBUTING.md](CONTRIBUTING.md)、未署名buildは
[docs/hypha_build_entry.md](docs/hypha_build_entry.md)を参照する。
署名・配置・公開は[docs/hypha_release_entry.md](docs/hypha_release_entry.md)の対象工程を確認する。
通常PRの検証にcredential、iLok、licensed AAX SDKは不要。
通常のsource検証は`bash scripts/test_lightweight_contract.sh`、完全なrelease-source検証は
`bash scripts/test_release_source.sh`。後者は実DAW受入・正式署名・公開を代用しない。

### 合格基準（Step 1）
- Audio Thread: 通常のA経路でテスト信号PRE/POST差分 = 0（ビット同一）
- レイテンシー: 0 samples
- LUFS-M: EBU R128テスト信号で ±0.1 LU以内
- Crest: ±0.2 dB以内
- CPU: processメソッド単体 0.1%未満
- クラッシュ耐性: Measure Thread panic → Audio Thread継続

## 世界観（GUI適用時）

Kirin Hypha は CE 2226 の菌糸の先端。DAWの中に200年後の世界がほんの少しだけ顔を出したもの。

- タイトル「PRE」「POST」は CE 2226 Font（実現可能であれば）
- 数値はシステムフォントまたはデザイン仕様で指定したフォント
- 背景は暗い菌糸テクスチャ
- Watch LED = 青の淡い発光（静的）
- Kirin OS本体（CE 2026の岩と苔）とは明確に異質

## GPLv3 分離

このリポジトリは GPLv3。Kirin OS本体（プロプライエタリ）とは:
- リポジトリを物理的に分離する
- 通信はファイルベースのみ（/tmp/ → plugin_data/）
- 初期計測coreにはLensからの移植と公開MoSQITo参照の記録がある。リポジトリ分離だけで独立実装を証明しない
- componentごとの出典・変更・license条件を保持する。公開方針は`PROVENANCE.md`と`THIRD_PARTY_NOTICES.md`を参照する
- ライセンス降格なし（G-50-47）

## 約束5原則（常に遵守）

1. 設定値の読み取りに徹する
2. 測定結果を的確に分析する
3. ユーザーの判断を最大限尊重する
4. プラグインベンダーとの良好な関係を保つ
5. ユーザーファーストの精神を貫く

## SignalState


### 概要
以下はC ABIとJUCE表示が使う公開値。Audio Thread内部の`SignalState` enumは別表現を持つため、
境界では`set_signal_state` / `signal_state_to_abi`の明示写像を必ず通す。

| 状態 | 値 | 意味 | Measure Thread | IO Thread | GUI |
|------|---|------|---------------|-----------|-----|
| Inactive | 0 | transport停止 or 無音 | スキップ | 最小JSON | `---` |
| Active | 1 | 信号あり・バイパスなし | 計測する | 全値JSON | 数値表示 |
| Bypassed | 2 | DAW バイパス中 | スキップ | 最小JSON | `---` |

### Heartbeat 方式（Studio One 対応）
Studio One はバイパス時に `process()` 自体を停止する（BoolParam bypass は同期されない）。
`AtomicU32` heartbeat カウンタで対応:
- Audio Thread: 毎 `process()` で `heartbeat.fetch_add(1)`
- Measure Thread: 200ms（2回連続）heartbeat 無変化 → `signal_state` を Inactive に上書き
- process() 再開時: Audio Thread が即座に heartbeat 更新 + 正しい state を書き戻す

BoolParam bypass は残存（対応 DAW では即時 Bypassed 検出に使える）。

### SS-8: エンジンリセット
非Active → Active 遷移時に `engine.reset()` で ebur128 FIR 遅延ライン / tp_window / window_400ms をクリア。前セッションの残留データによる汚染を防ぐ。

### テスト

固定件数を完了根拠にしない。
通常suite、対象native試験、source契約、clippyに加え、`kirin_hypha_ffi`変更時は上記の
ignored parity / pairing_candidatesの一覧件数を実測して全件実行する。
