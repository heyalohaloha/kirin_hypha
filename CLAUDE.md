# Kirin Hypha — 計測プラグイン開発

## Notion操作 全面禁止
Claude Codeセッションは Notion へのいかなる書き込みも行わない。
- 📍現在地 SECTION:DEV 更新 → 禁止
- ログDB エントリ作成 → 禁止
- Daily Brief 更新 → 禁止
- その他あらゆる Notion ページへの write → 禁止

## 完了時の出力形式（チャット出力のみ）
- Commit hash / B番号 / 変更ファイル数 (+N / -N)
- Test: pass/fail/skip
- 未処理申し送り（番人裁定待ち等）

## プロジェクト概要
Kirin Hypha は Kirin OS の計測プラグイン。VST3。DAW内で音声を一切加工せず、計測のみ行う。
PRE/POST の2バイナリでマスタリングチェインの前後を計測し、差分（Δ）を表示する。
ライセンス: GPLv3（オープンソース公開）。Kirin OS本体（プロプライエタリ）とは完全分離。

**設計書正本:** `guardian_53_hypha_watch_minimum.md`（番人#53）
**上位設計:** `guardian_50_plugin_design_complete_v3.md`（番人#50。Kirin Hypha全体設計）

## 技術スタック
- Rust + nih-plug（VST3フレームワーク）
- ebur128 クレート（LUFS/TP）
- GUI: nih-plug対応フレームワーク（iced / egui / カスタム。選定は公式確認後に決定）
- ビルド: `cargo xtask bundle --release` → .vst3 生成
- 対象DAW: Studio One（Daisuke主環境）。他DAWは後日

## 絶対原則

### R-12 製造境界（不変）
Kirin Hypha は音声信号を生成・加工しない。計測・分析・制御信号の送信のみ。
Audio Thread では入力バッファを出力バッファにコピーするだけ。それ以外の処理を Audio Thread に入れない。

### R-13（Hub & Spoke）
`work.json`（schema: `work.schema.json`）が全システムの接続点。各モジュール間はこの接続点を通じて連携する。売り切り。サーバー・アカウント不要。

### 3層隔離
```
Audio Thread   — バッファコピーのみ。絶対に落ちない
Measure Thread — 計測。クラッシュ → Audio Threadが検出 → 自動再起動
IO Thread      — /tmp/ 書き込み。クラッシュ → Audio Threadが検出 → 自動再起動
```
Audio Thread から見て、Measure/IO は「落ちても構わない」存在。
Audio Thread が止まる = DAWの再生が止まる = 利用者の作業が全壊。この優先順位は絶対。

### 分離原則
各モジュール（計測エンジン / GUI / IO / Audio Thread）は独立して動作し、1つが落ちても他が連動して落ちない構造にする。

## 禁止事項（📍現在地から。全件適用）

1. backdrop-filterを全画面overlayにかけるな（GUI実装時）
2. 1画面だけ修正して他を放置するな — 全対象を一括処理
3. PRESENCE overlay値を勝手に変えるな — Daisuke実機調整済み
4. 既存コードを読まずに新規実装するな
5. 「表示されている」「解決した」と雰囲気で言うな — 確認した事実のみ
6. 「はい」「わかりました」だけで実装に入るな — 内容に触れてから
7. R-22: ADVISORは価値判断を出さない（Hypha GUI表示に適用）
8. R-26: 言うことがなければ沈黙する
8a. R-28（機能的沈黙）: 内部検証および互換 fallback 等で利用者操作と非紐づきの失敗は無言で skip。UIにエラーを出さない。ただし利用者が明示意図して操作した結果の失敗（沈黙すれば「問題なく進んだ」と勘違いするケース）は通知必須（G-115-100 / 2026-05-08）
9. 入力データを1件も見ずにコードを書くな — 実物確認必須。「こうなっているはず」禁止
10. 出力の妥当性を数字で確認しろ — サイズ・クラス分布・桁を期待値と比較
11. 外部ツールのコマンド/APIは公式ドキュメントで確認しろ — 記憶で書くな
12. 正常系だけテストするな — エラーパス・再起動後・ファイル不在時も確認
13. 世界観から逆算していない表現を画面に出すな — CE 2226世界設定 + ビジュアルバイブルv2
14. Phase 2送りはDaisukeの承認なしに決定するな

## 開発ルール

### 構造的対処原則（最重要）
パッチ処理（修正の積み重ね・段階的修正）禁止。タスクを受けたら:
1. 前の部屋のやりとりと設計意図を確認
2. 影響する全ファイルを最初に特定
3. 全問題を一括で洗い出し
4. 利用者目線で批評
5. 1回の出力で全変更を含んだ完全なファイルを出す

前の出力に依存する出力をしない。問題を能動的に発見・報告する。
「システムレベルの問題」「無関係」で片付けずコードで確認する。

### コード品質
- ファイルは500行以下
- Rust: `cargo clippy` + `cargo test` を毎回実行
- エラーログは作業前に必ず読む
- 同じアプローチは最大2回。3回目は別手法
- テスト: 正常系 + エラーパス + 境界値
- vendor/* 配下の clippy 警告は upstream 修正待ちとして監査対象外。kirin_hypha 本体の警告ゼロが品質基準（G-115-100 / 2026-05-08）

### Daisukeに手動編集を依頼しない
全ての修正は完全なファイルとして出力する。
「この行を変えてください」「追加してください」は禁止。

### 質問には質問で答える。行動に先走らない
不明点があれば確認してから実装。推測で進めない。

## セッション開始手順

1. 📍現在地を確認（Notion ID: `33759d4f6faf8114bba4fdbcdb82a0fa`）
2. SECTION:DEV と SECTION:TASKS を読む
3. 番人の指示書がある場合はその md を読む
4. `[未検証]` 項目があれば公式ドキュメントで確認してから実装

## Notion連携

📍 現在地: `33759d4f6faf8114bba4fdbcdb82a0fa` — SECTION:DEV を更新
📖 ログ: `32f59d4f6faf8104a13af89272cb6d51` — `[開発]` タグで追記
📐 運用ルール: `32f59d4f6faf816b8850c36b49097bec`

セッション終了時:
1. SECTION:DEV を上書き更新（何をやったか + 次にやること）
2. 📖ログに追記
3. 💭日記必須 — Daisukeの発言を引用して記録

## Kirin Hypha 固有

### Watch計測項目（G-52-02）
| 項目 | 計測 | 表示 | 有効桁 |
|------|------|------|--------|
| LUFS-M | ✅ 常時 | ✅ | 小数1桁 |
| True Peak | ✅ 常時 | ✅ | 小数1桁 |
| Crest Factor | ✅ 常時 | ✅ | 小数1桁 |
| PSR | ✅ 常時 | ❌ Watch非表示 | 小数1桁 |

4項目常時計測。表示は3項目。PSRはRecord版で表示。

### /tmp/ 通信
```
PRE: /tmp/kirin/{project_hash}/{bus}/pre_{instance_id}.json
POST: 同ディレクトリのPREファイルを読む → Δ算出
```
100ms間隔。アトミック書き込み（tmp → rename）。

### PRE/POST別バイナリ
同一コードベースから role 定数（PRE/POST）でビルド時に分岐。
利用者がDAWで「Kirin Hypha PRE」「Kirin Hypha POST」を別々に選ぶ。

### GUI（最小版）
300×200px付近。暗い菌糸テクスチャ背景（静的PNG 1枚）。
3数値 + Δ値 + Watch LED（青・静的）。
菌糸脈動アニメーション・flora_color連動は後段。

### Lensエンジンからの流用
Lens側の既存Rustエンジン（symphonia + ebur128 + napi-rs）から計測コアを切り出す。
napi-rs依存を外し、純粋なRustライブラリとして抽出。

### [未検証] 項目（公式確認してから実装）
- U-1: nih-plugでPRE/POST別バイナリが作れるか
- U-2: processメソッドでバッファコピーだけすれば素通しになるか
- U-3: nih-plugからDAWプロジェクトパスが取得できるか
- U-4: GUIで300×200pxカスタム描画ができるか
- U-5: 別スレッドをnih-plugのVST3ランタイム内で安全に起動できるか
- U-6: ebur128クレートがVST3コンテキストで動くか
- U-7: /tmp/ への書き込みがmacOSサンドボックス内で許可されるか
- U-8: CE 2226フォントアセット（PNG）をGUIで描画できるか

### Studio One テスト前チェック
- チャンネル設定が **Stereo** であることを確認する（Mono だと -3dB/ch 適用され計測値がずれる）
- テスト信号は `test_signals/` 内の S-1〜S-5 を使用

### ビルド・テスト
```bash
# ビルド (PRE / POST 両方)
cargo run --package xtask -- bundle hypha_pre --release
cargo run --package xtask -- bundle hypha_post --release

# VST3を配置（macOS / B-022 段階 6 G-115-37）
# user-level (~/Library/Audio/Plug-Ins/VST3/) の古い .vst3 を除去 (sudo 不要)
# + system-level (/Library/Audio/Plug-Ins/VST3/) に最新を sudo cp で配置
# を一括実行する。途中で sudo パスワード入力プロンプトが出る。
cargo run --package xtask -- install --release

# Studio Oneで確認
# MIX Bus → PRE挿入 → POST挿入 → 再生 → 数値確認 → 音声素通り確認
```

**注意**: `sudo cp -r ... /Library/Audio/Plug-Ins/VST3/` の手動運用は禁止。
user-level に古いバイナリが残ると Studio One が古い方を優先読込みして
コード修正が反映されない事故が起こる (B-022 段階 5 真因 / commit 62f279f 参照)。
必ず `cargo run --package xtask -- install --release` を経由すること。

### 合格基準（Step 1）
- Audio Thread: テスト信号PRE/POST差分 = 0（ビット同一）
- レイテンシー: 0 samples
- LUFS-M: EBU R128テスト信号で ±0.1 LU以内
- Crest: ±0.2 dB以内
- CPU: processメソッド単体 0.1%未満
- クラッシュ耐性: Measure Thread panic → Audio Thread継続

## 世界観（GUI適用時）

Kirin Hypha は CE 2226 の菌糸の先端。DAWの中に200年後の世界がほんの少しだけ顔を出したもの。

- タイトル「PRE」「POST」は CE 2226 Font（実現可能であれば）
- 数値はシステムフォントまたは番人指定のフォント
- 背景は暗い菌糸テクスチャ
- Watch LED = 青の淡い発光（静的）
- Kirin OS本体（CE 2026の岩と苔）とは明確に異質

## GPLv3 分離

このリポジトリは GPLv3。Kirin OS本体（プロプライエタリ）とは:
- リポジトリが物理的に分離（~/Dev/kirin_hypha/ vs ~/Dev/kirin_sense_lens/）
- 通信はファイルベースのみ（/tmp/ → plugin_data/）
- コードの共有なし。計測アルゴリズムは同一ロジックだが独立実装
- ライセンス降格なし（G-50-47）

## 約束5原則（常に遵守）

1. 設定値の読み取りに徹する
2. 測定結果を的確に分析する
3. ユーザーの判断を最大限尊重する
4. プラグインベンダーとの良好な関係を保つ
5. ユーザーファーストの精神を貫く

## SignalState（SS-1〜SS-9 全完了）

仕様書: `advisor_signal_state_spec.md`

### 概要
Audio Thread が毎 process() で `AtomicU8` に書き込む信号状態。パイプライン全体がこの値に従う。

| 状態 | 値 | 意味 | Measure Thread | IO Thread | GUI |
|------|---|------|---------------|-----------|-----|
| Active | 0 | 信号あり・バイパスなし | 計測する | 全値JSON | 数値表示 |
| Bypassed | 1 | DAW バイパス中 | スキップ | 最小JSON | `---` |
| Inactive | 2 | transport停止 or 無音 | スキップ | 最小JSON | `---` |

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
30テスト全通過、clippy clean。

## 起草前 自己適用 Pass — methodology v5 (21 軸監査スイート)

メソドロジー v5 (2026-05-06 / 番人 #138 採否裁定中) に基づき、起草・実装着手前に以下を自己適用する。Pass 1〜13 は v1〜v2 継承で要点省略。Pass 14〜21 は v3〜v5 で順次新設された目的・整合・確証系の軸。

### Pass 14 — 起草前の目的言語化（v3 新設）

起草対象の目的・スコープ・期待成果を 1 段落で言語化してから着手する。動機側の整合確認。
番人 #132 / G-115-34 由来。

### Pass 15 — 既存仕様書との二重定義チェック（v3 新設）

起草前に既存仕様書（guardian_XX / room_guide / Daily Brief / CLAUDE.md）と重複する内容がないか確認する。出力側の整合確認。
番人 #132 / G-115-34 由来。

### Pass 16 — 入力データ実在確認 / 静的 read（v4 新設）

起草対象が依存する仕様書・コード・DB を実 read で確認する。「あるはず」「こうなっているはず」禁止。入力側の整合確認。
v4 / 2026-05-02 由来。

### Pass 17 — 動的状態確認（v5 新設 / 2026-05-06）

起草着手前に git log / cargo test / npm test / working tree / 実機状態を実取得する。
「これから実装する」が既 commit 済でないか確認（失態 #57 直接対応）。

### Pass 18 — チャット履歴一次ソース（v5 新設 / 2026-05-06）

Notion・タスク一覧・他部屋起草 md は二次資料。
recent_chats / conversation_search で chat history を一次ソースとして優先する。
番人 #137 karaoke 撤回由来。

### Pass 19 — 引用元三軸確認（v5 新設 / 2026-05-06）

他文書からコピー/引用する際、以下 3 軸を確認する:
- (a) 言質の出所 — 「人がそう言った」事実は当人発言を直接確認
- (b) 作成時点 vs 現状の差分 — 数字・状態は git log / cargo test 実測優先
- (c) 調査範囲の網羅性 — grep 結果はキーワード網羅性（別命名 / 大文字小文字 / 部分一致）を確認

失態 #62/#63/#64 由来。

### Pass 20 — 推奨完備（v5 新設 / 2026-05-06）

判断要請・指示文起草時、以下 4 点を満たす:
1. 推奨明示（推奨なし丸投げ禁止）
2. 工数・スケジュール非材料化（共通ルール v6 §11 行動規則 14 / 販売設計専管）
3. 一括提示禁止（1 件ずつ Daisuke 確認）
4. R-9 確証根拠を付ける（定量根拠なし断言禁止）

S101/S115/S119 / 失態 #50 由来。

### Pass 21 — 概念区別（v5 新設 / 2026-05-06）

類似用語・並存概念が存在するとき、起草前に区別マトリクスを作成し、
本起草の範囲を冒頭明記する。
失態 #37（Daisuke Hypha 一旦中止の直接原因）由来。

---

詳細: `docs/methodology/guardian_audit_robustness_methodology_v5_20260506.md`
