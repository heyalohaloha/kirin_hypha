# Hypha サラウンド化 — 全指標の契約表（P-0 §6）

Status: 調査と分類。実装なし・仕様変更なし。Nch での扱いの確定は Daisuke の承認事項。

Date: 2026-09-18

Branch: `claude/eager-pascal-edacak` / 確認 commit `624a404`

計画 `@docs/hypha_surround_implementation_plan_20260918.md`（B-925）§6 の成果物。
事実は `@docs/hypha_surround_channel_map_findings_20260918.md`、
棚卸しは `@docs/hypha_surround_p0_inventory_20260918.md`、
容量と実測は `@docs/hypha_surround_ingest_capacity_20260918.md`。

## 0. 分類の軸

**「6ch を渡したとき実際に何が起きるか」**で分ける。推測ではなく、実 read と probe 実測による。

| 記号 | 状態 | 意味 |
|---|---|---|
| **R** | reject | **構築が拒否される。** 値が出ない |
| **C** | clamp | **無言で 2ch になる。** 構築は成功し、投入は drop される |
| **G** | garbled | 動くが **default channel map で一部チャンネルが寄与しない** |
| **N** | generic | 通る。ただし**集約の意味が Nch で未定義** |
| **S** | stereo-only | **正しく適用外**になる（stereo 専用と分かる形で無効） |

**R と S は違う。** S は設計どおりの適用外、R は単に構築できない。

## 1. 契約表

| 指標群 | 生成元 | 6ch での現状 | 分類 | 根拠 |
|---|---|---|---|---|
| LUFS-M / S / I、LRA、True Peak（session） | `MeasureEngine` → `EbuR128` | 構築は通り**値も出る**。ただし default map で **5/6 ch しか寄与しない** | **G** | `engine.rs:130`（ガード無し）/ findings §3 / capacity §12 |
| Sample Peak / TP / Clip / VU（ch 別） | `StereoMeter` | 構築拒否 | **R** | `stereo_meter.rs:105` |
| Correlation | `StereoMeter::stereo_window_facts` | 構築拒否。仮に通っても `channels != 2` で `None` | **R** (+S) | `:343` |
| Balance / BalanceState | 同上（同一関数） | 同上 | **R** (+S) | `:343` |
| Stereo field density | `StereoMeter` | 同上。`channels == 2` ゲート | **R** (+S) | `:168` |
| MONO（downmix survival） | `StereoMeter::update_mono_sum` | 同上 | **R** (+S) | `:230`（`channels == 2` ブロック内） |
| Spectrum / MID-SIDE | `SpectrumRuntime` | **無言で 2ch 化。6ch の push は 40/40 drop** | **C** | `spectrum_runtime.rs:91` / capacity §13 |
| Attack | `AttackRuntime` | 構築拒否 | **R** | `attack_runtime.rs:79` |
| Sharpness（continuous） | `SharpnessContinuousAnalyzer` | 構築拒否 | **R** | `perceptual.rs:139` |
| Zwicker / specific loudness | `phase_d` | 通る。**重みなし算術平均**で 1 値へ畳む | **N** | `phase_d/channels.rs:13, :71` |
| Absolute timeline（POST） | `absolute_timeline` | 構築拒否 | **R** | `absolute_timeline.rs:170` |
| SPACE decay | `space_decay` | 構築拒否 | **R** | `space_decay.rs:135` |
| Reference visual | `reference_visual` | 構築拒否 | **R** | `reference_visual.rs:58` |
| Reference tonal | `reference_tonal` | 構築拒否 | **R** | `reference_tonal.rs:194` |
| Reference capture index | `reference_capture_index` | 構築拒否。加えて **64 byte 保存契約** | **R** | `:32, :151` |
| Reference gain | `reference_gain`（`EbuR128` 直接） | 構築拒否 | **R** | `:89, :167` |
| Local blind capture | `local_blind_capture_protocol` | 構築拒否 | **R** | `:193` |

### 1.1 集計

**17 指標群のうち:**

- **R（構築拒否）: 14**
- **C（無言 2ch 化）: 1** — Spectrum / MID-SIDE
- **G（動くが一部 ch が寄与しない）: 1** — LUFS / LRA / TP
- **N（通るが集約が未定義）: 1** — Zwicker / Sharpness

**6ch で実際に数値が出るのは LUFS 系だけで、その数値は誤っている。**

**S（正しく適用外）に分類できるものは 1 つも無い。**
現状、stereo 専用の指標は「適用外」ではなく「構築拒否」で止まっている。

## 2. 分類ごとの意味

### 2.1 R — 14 件。サラウンドの最初の壁

メモリでも ABI でもない。**構築が通らない。**
容量の実測（capacity §11）が 6ch で `rejected` を並べたのはこれである。

R は 2 つに分かれる。**この区別が設計判断になる。**

| | 内容 | 例 |
|---|---|---|
| **R-a** | Nch へ一般化すべきもの | Sample Peak / TP / Clip / VU、Attack、Absolute timeline |
| **R-b** | stereo 専用として **S へ移すべき**もの | Correlation、Balance、field density、MONO |

**R-b を R のまま残すと、サラウンドで「機能が無い」ではなく「挿さらない」になる。**
適用外は、適用外と分かる形で示す必要がある。

### 2.2 C — 1 件。最も危険

`SpectrumRuntime` は **構築に成功し、`stats().channels` が 2 を返し、投入を全部 drop する。**
呼び出し側には成功したように見える。

**R より危険である。** R は止まるが、C は黙って何もしない。

### 2.3 G — 1 件。数値が出るが誤っている

LUFS 系だけが 6ch で動き、**default channel map によって一部チャンネルが寄与しない**。
findings §3 のとおり 7ch 以上では過半が落ち、capacity §12 ではその未書込領域が
メモリ側からも観測された。

**唯一「動いている」ものが、唯一「誤った値を出している」ものである。**

### 2.4 N — 1 件。動くが意味が未定義

`phase_d` はチャンネル総称で、**重みなし算術平均**で 1 値に畳む
（`channels.rs:13` に製品独自の集約として明記）。
mono と dual-mono の尺度を揃えるための設計であり、事故ではない。

ただし **LFE を含む 12ch の単純平均を「サラウンドの知覚量」と呼べる根拠は未確認** [C]。

## 3. 各指標の Nch での扱い（推奨 / 決定は Daisuke）

工数は材料にしない。根拠のみ付す。

| 指標群 | 推奨 | 根拠 |
|---|---|---|
| LUFS / LRA / TP | **Nch 一般化**（明示 map 必須） | 規格が多チャンネルを定義している唯一の指標。map を入れないと誤った値が出続ける |
| Sample Peak / TP / Clip / VU | **Nch 一般化** | チャンネル別の事実であり、集約の設計判断が要らない。ABI の `[2]` を広げれば足りる |
| Correlation | **明示ペア限定**（S ではなく限定して有効） | 全ペアは 12ch で 66 組。左右対 overview + 選択ペア詳細（計画 §7.1） |
| Balance | **Nch 一般化**（左右グループのエネルギー比） | 定義が素直に拡張できる。C と LFE を分母から除く |
| Stereo field density | **S（適用外を明示）** | 2ch の散布図であり、Nch での意味が定義されていない |
| MONO | **別仕様が必要** | downmix survival へ拡張する案があるが、測定量が未確定（計画 §7.3） |
| Spectrum | **Nch 一般化** | チャンネル別スペクトルは定義が要らない。**まず C を解消する** |
| MID-SIDE | **S（適用外を明示）** | M/S は 2ch の定義。Nch の等価物は別仕様 |
| Attack | **Nch 一般化** | 検出はチャンネル別に成立する |
| Sharpness / Zwicker | **現状維持 + 適用条件の明示** | 集約の妥当性が未確認。mono / stereo の定義は変えない |
| Absolute timeline | **Nch 一般化** | 中身は LUFS-M + TP + Sharpness の合成。それぞれの結論に従う |
| SPACE decay | **要調査** | 中身を読んでいない [C] |
| Reference visual / tonal / index / gain | **stereo Reference との比較条件を先に定義** | 入力 A が 7.1.4、参照 B が stereo という組合せが成立しうる（計画 §4） |
| Local blind capture | **要調査** | metadata 経路であり PCM を運ばない。影響範囲が未確認 [C] |

## 4. 承認事項への接続

計画 §11.2 の 3 件に、本表が与える材料。

### 4.1 容量 16 と対応配置の別管理

**表の R-a（Nch 一般化するもの）だけが ABI の `[16]` を必要とする。**
R-b（S へ移すもの）は ABI を広げる必要がない。
**「全部 16」ではなく、指標ごとに決まる。**

### 4.2 非互換レイアウト変更中の保留

**C（`SpectrumRuntime`）が最も危険である。**
レイアウトが変わっても構築は成功し、`stats().channels` は古い値を返し、投入は drop される。
**保留の設計は、この「成功したように見えて何もしない」状態を検出できる必要がある。**

### 4.3 初期公開で適用外を認めるか

**現状、適用外（S）に分類できる指標は 1 つも無い。**
14 件が構築拒否で止まっている。

したがって初期公開の選択は「どれを適用外にするか」ではなく、
**「どれを R から S へ移し、どれを R から Nch 一般化へ移すか」**である。

最小構成の候補（推奨ではなく、表から導ける最小の集合）:

- **Nch 一般化**: LUFS / LRA / TP（明示 map）、Sample Peak / TP / Clip / VU
- **S へ移す**: Correlation、Balance、field density、MONO、MID-SIDE
- **C を解消**: Spectrum（少なくとも clamp を拒否か一般化へ）
- それ以外は現状維持で適用外を明示

これなら「ラウドネスとピークは正しく測れ、他は適用外と分かる」状態になる。
**採否は Daisuke が決める。**

## 5. 未確認 [C]

- `space_decay` と `local_blind_capture_protocol` の中身。
- 各指標の窓長・hop・正規化・無音条件・reset 条件。**本表は「6ch で何が起きるか」に絞っている。**
- Zwicker の算術平均が多チャンネル配置の知覚量として妥当かどうか。
- Reference B が stereo、入力 A が Nch のときの比較条件。
- `phase_d` 以外に N（総称だが集約未定義）に当たるものがないか。
