# サラウンド対応 — Gate 計画（2026-09-19 / B-975）

レビュー提案の Gate 順序を受け、**各 gate の目的・合格条件・「この gate では検出できないもの」**を
分けて書く。検出できないものを書かない gate 表は、それ自体が誤った安心を作る。

- **前提**: D-1〜D-14 は変更しない。工数・日程は判断材料にしない（R-27）。
- **現在地**: `eb1ac6d`（B-974）。Gate A 発効中。

---

## 0. 順序

```
Gate A  — Freeze
Gate B  — Baseline 実機検証          （いま何が本当に動いているかを知る）
Gate C  — 構造整理
Gate C2 — 構造変更後の実機回帰       （Gate B の結果を壊していないことを示す）
Gate D  — R-28 rejection transport
Gate E  — role-view ABI 公開
Gate E2 — ABI 公開後の実機回帰       （probe からしか届かなかった経路が製品経路に繋がる）
Gate F  — 5.1 bus 開放
Gate F2 — 5.1 製品経路の実機検証
```

**実機を複数回やることは重複ではない。** 1 回目は「現在何が動いているかを知る試験」、
2 回目以降は「変更がそれを壊していないことを示す試験」である。
Gate B を飛ばすと、Gate C 後に問題が出たとき **B-955〜974 由来か Gate C 由来かの切り分けができない。**
この切り分け可能性が、順序を決める第一の理由である。

---

## 1. 各 gate の目的と合格条件

### Gate A — Freeze

**目的**: 検証対象を 1 点に固定する。

- surround 機能を追加しない。
- **G / C 類型の欠陥修正は freeze の対象外**（D-13 は実装が従う不変条件であり、
  「値が出ているのに意味が違う」状態を放置する理由に freeze を使わない）。
- 修正した場合は Gate B の対象コミットを更新し、**何を変えたかを Gate B の観察項目に足す。**

**合格**: 対象コミットが 1 つに定まっていること。

### Gate B — Baseline 実機検証

**目的**: `現在地` が実製品経路で成立しているかを知る。**ここで見つかる問題は Gate C の設計材料になる。**

| # | 項目 |
|---|---|
| 1 | macOS build → plugin validation |
| 2 | Windows build → plugin validation |
| 3 | DAW で mono / stereo の既存回帰（Watch / Record / reset / reopen） |
| 4 | plugin state save → close → reopen |
| 5 | ABI handshake 正常系 |
| 6 | ABI revision mismatch の negative test |
| 7 | 長時間 history の production-equivalent RSS（§3 の 328 MiB 算定の実地確認） |
| 8 | **mono PRE ↔ stereo POST**（§2 の未修正 G / 下記） |
| 9 | Record / restore 後の `measurement_epoch` の振る舞い観察（棚卸し §9.3 の未記述箇所） |

**合格**: 1〜7 が通り、8・9 の観察結果が記録されていること。

**この gate で検出できないもの**:

- **surround 経路**（`isBusesLayoutSupported` が mono / stereo しか受理しない）
- **役割 view**（ABI 入口が無い）
- **B-974 型の race**。view を 3 回読んでいた欠陥は、狭い窓で名札だけが食い違う。
  DAW で操作しても再現しない。**実機は race を検出する手段ではない。**
  この類型はコード読解・変異試験・stress でしか捕まらない。

### Gate C — 構造整理

**目的**: 今回見つかった類型の欠陥を**構造的に起こしにくく**する。

| # | 項目 | 由来 |
|---|---|---|
| 1 | `input_channel_count` / `analysis_channel_count` の共通語彙 | B-970 |
| 2 | **同じ状態を複数回読まない**規律（位置・本数・名札を 1 回の読み出しから derive） | **B-974** |
| 3 | `MeasurementIdentity` / `ComparisonIdentity` の共通化 | B-968 / B-971 |
| 4 | `measurement_epoch` の ownership 記述（Gate B §9 の観察後） | 棚卸し §9.3 |
| 5 | history contract（current ABI のコピーにしない） | B-967 |
| 6 | `AttackRuntime` / `SharpnessContinuousAnalyzer` / `PerceptualAssembler` / `AbsoluteAssembler` / Reference 系の同観点監査 | B-970 |

**合格**: 1〜6 の各項目について、変異試験で「戻したら落ちる」ことを示せること。

**この gate で検出できないもの**: 実機挙動。Gate C は構造の話であって、
host lifecycle / state / ABI が壊れていないことは示さない。→ Gate C2。

### Gate C2 — 構造変更後の実機回帰

**目的**: **Gate B で正常だったものを Gate C が壊していないことを示す。**

**合格**: Gate B の 1〜7 を再実行して同じ結果。差が出たら Gate C 由来として切り分ける。

**この gate で検出できないもの**: Gate B と同じ（surround / 役割 view / race）。
**Gate B が見ていない範囲は、Gate C2 も見ていない。**

### Gate D — R-28 rejection transport

**目的**: 「利用者が明示操作した結果、測定が成立しない」を無言で終わらせない。

対象（machine-readable reason を FFI へ運び、UI で明示する）:

- layout mismatch（TIME Δ / live Δ / Spectrum Δ）
- unsupported metric / unsupported view / unsupported layout
- measurement held during layout transition
- **旧版の記録を読んだときの「checksum 不一致」**（棚卸し §9.2 の残る限界。
  拒否理由が「改竄または破損」になっており、**R ではあるが表示される理由が誤り**）

**合格**: 上記それぞれについて、理由が FFI を越えて UI に届くこと。
`bool` だけで終わる経路が無いこと。

### Gate E — role-view ABI 公開

**目的**: 内部で成立している役割 view を製品経路へ繋ぐ。

- role-view setter
- `KirinSpectrumFrame` への `view`（内部側は B-971 で済み）
- ABI negative tests（未知 view コード / 旧 revision / 容量外）

**合格**: mono / stereo で役割 view（`Channel(Left)` / `Channel(Right)`）が
UI から選べ、表示される名札が測っている対象と一致すること。

### Gate E2 — ABI 公開後の実機回帰

**目的**: **probe からしか到達できなかった経路が、初めて製品経路に繋がる。**

**合格**: Gate B の 1〜7 に加え、stereo 上の役割 view が DAW で成立すること。

**この gate で検出できないもの**: surround。bus はまだ閉じている。

### Gate F — 5.1 bus 開放

`isBusesLayoutSupported` で 5.1 を受理する。**Gate D が閉じていることが前提。**

### Gate F2 — 5.1 製品経路の実機検証

**目的の性質が他と違う。** Gate C2 / E2 は回帰試験だが、
**Gate F2 は surround 経路にとって最初の実機接触である。**

Gate B が mono / stereo しか見られない以上、surround 製品経路には**比較すべき baseline が無い。**
したがって Gate F2 は「壊していないことの証明」ではなく「初めて動かす試験」であり、
**リスクの性質が Gate C2 / E2 と異なる。**

- 5.1 の Watch / Record / reset / reopen
- 5.1 の state save → reopen
- 5.1 ↔ stereo の bus 変更（D-12 の保留動作）
- 5.1 での history RSS（§3 の算定は stereo 形 entry での値。Nch 化していなければ変わらない）
- EBU 公式 test material による数値適合（D-1 の根拠）

**含意**: Gate F2 の前に、5.1 を probe 経路でどこまで確かめられるかを尽くしておく。
実機で初めて見る項目を減らすほど、この gate の切り分けが楽になる。

---

## 2. Gate B の前に決めるべきこと — live Δ の未修正 G

**B-968 は不完全だった。** TIME Δ history（二次経路）に配置照合を入れたが、
**live Δ（利用者が実際に読む一次の数値）には入っていない。**

実読による事実:

- `pre.json`（`io_thread_pre.rs:1599` / `serialize_pre_json_with_daw_session_id_and_owner`）が運ぶのは
  `v` / `role` / `instance_id` / `name` / `daw_session_id` / `watch_owner_id` /
  `host_process_id` / `signal_state` / `t` / `lufs_m` / `lufs_s` / `true_peak` / `crest` / `psr`。
  **layout もチャンネル数も sample rate も無い。**
- `compute_delta_with_state`（`io_thread_post_delta.rs:148`）は
  `project_dir` / `post: &MeasureResult` / `pair_pre_name` だけを受け取る。
  `MeasureResult` は layout を持たない。**照合しようが無い。**

したがって **mono PRE ＋ stereo POST の live Δ LUFS は +3.01 LU ずれる。**
B-968 で実測した `lufs_m delta = 3.0102999566398108` と同じ量が、一次の数値に出る。
`isBusesLayoutSupported` は mono と stereo を両方受理するので、**出荷中の通常配置で起きる。**

### 推奨

**Gate B の前に閉じる。** 根拠は 2 つ。

1. D-13 は G を R より先に消すと決めている。B-968 で同じ判断を一度下している。
2. **Gate B は baseline validation である。** 既知の G を含んだまま baseline を取ると、
   「mono PRE + stereo POST で 3 LU ずれる」が正常値として記録される。

### 判断を仰ぐ点 — `pre.json` の `v` を上げるかどうか

B-968 では交換 schema を 2 → 3 に上げ、版が混ざれば Δ が出ない（G → R）ようにした。
**pre.json では同じ判断を採らないことを推奨する。**

| | `v` を上げる | additive optional（推奨） |
|---|---|---|
| 版混在時 | 旧 POST が新 PRE を**一切読めない** → ペアリング自体が成立しない | 従来どおり Δ は出る。layout が不明なので照合できない（現状維持） |
| 閉じる範囲 | 完全 | 両側が新版のときだけ |
| 失敗の重さ | **ペアリング喪失** | 狭い版混在窓で G が残る |

`meter_history.json` は失われても Δ history が出ないだけだが、
**`pre.json` は PRE/POST ペアリングの一次経路**である。ここを版で落とすと機能全体が止まる。
閉じる範囲は狭くなるが、失敗の重さが釣り合わない。

**この差は B-968 の前例からの意図的な逸脱なので、実装前に確認したい。**
