# Hypha サラウンド化 P-0 — 影響範囲の棚卸し（第1巡）

Status: 調査のみ。実装なし・仕様変更なし。計画は `@docs/hypha_surround_implementation_plan_20260918.md`（B-925）。

Date: 2026-09-18

Branch: `claude/eager-pascal-edacak` / commit `d701e55`

## 0. 網羅性の申告

**この文書が何を全走査し、何をサンプルしたかを先に書く。**
初版の「17 か所」が入口に過ぎなかった失敗を繰り返さないため、パターンごとに区別する。

| 対象 | 方法 | 網羅性 |
|---|---|---|
| チャンネル数の拒否ガード | `1..=2` / `matches!(_, 1 \| 2)` を全 grep | **そのパターンについては全走査。** 別表現のガードは未走査 |
| 固定長 2 要素配列 | `[2]`（C）/ `; 2]`（Rust）を全 grep | **そのパターンについては全走査。** `Vec` で 2 を仮定する箇所は未走査 |
| interleave / stride の 2 決め打ち | `chunks(2)` `chunks_exact(2)` `step_by(2)` `* 2` 系 | そのパターンについては全走査 |
| L/R 固定名 | `left` / `right` の全 grep 後、ファイル単位で分類 | **分類はサンプル。** 全 344 件の逐一確認はしていない |
| サブシステムのチャンネル配線 | 生成元を全 grep し、主要経路を実 read | **主要経路のみ。** 全経路の通読は未完 |
| 指標ごとの入力対象（計画 §6） | 主要なものを実 read | **未完。** §7 は第1巡の結果であり確定表ではない |
| Record / メタデータ | `plugin_data.rs` / `record_*.rs` を grep | そのパターンについては全走査 |

未走査・未完の項目は §9 に残す。**確認済み欄へ移していない。**

## 1. 計画の数字の訂正

| 計画の記述 | 実測 |
|---|---|
| 2MIX 限定を効かせているのは `StereoMeter::new` と `isBusesLayoutSupported` の **2 か所** | **Rust 側だけで 23 か所 / 15 ファイル。** + C++ 1 か所（§2） |
| `[2]` 固定長は `KirinMeterSession` の 8 本 + 他 3 本 | **Rust / C 合わせて 79 か所。** 3 種類に分かれる（§3） |
| `channels == 2` の 17 か所 | 分岐としては正しいが、**ガードとは別物**。ガードは §2、分岐は §2.2 |
| Phase D はチャンネル総称なのでこの制約の外 | **機構は総称だが、畳み方が未定義。** 重みなし算術平均（§5） |

## 2. チャンネル数を拒否するガード — 23 か所 / 15 ファイル

`crates/kirin_measure/src` および `crates/kirin_hypha_ffi/src`。`examples/` と `_tests.rs` は除く。

| ファイル | 行 | 形 |
|---|---|---|
| `absolute_timeline.rs` | 42, 170 | `(1..=2)` |
| `attack_exchange_codec.rs` | 133 | `matches!(channels, 1 \| 2)` |
| `attack_pair.rs` | 69 | `matches!(self.channels, 1 \| 2)` |
| `attack_perception.rs` | 114, 211 | `matches!` |
| `attack_runtime.rs` | 79, 313 | `matches!` |
| `attack_runtime_state.rs` | 25, 82, 124 | `matches!(self.channels, 1 \| 2)` |
| `local_blind_capture_protocol.rs` | 193 | `matches!` |
| `perceptual.rs` | 139 | `(1..=2)` |
| `reference_capture_index.rs` | 32, 151 | `(1..=2)` |
| `reference_gain.rs` | 89, 167 | `matches!` |
| `reference_gain_ffi.rs` | 40, 87 | `matches!(num_channels, 1 \| 2)` |
| `reference_tonal.rs` | 194 | `(1..=2)` |
| `reference_visual.rs` | 58 | `(1..=2)` |
| `space_decay.rs` | 135 | `matches!` |
| `stereo_meter.rs` | 105 | `(1..=2)` |

C++ 側は `juce_shell/src/PluginProcessor.cpp:208 isBusesLayoutSupported`（`mono()` / `stereo()` のみ受理）。

**含意**: ガードは Reference 経路（`reference_*` 5 ファイル 8 か所）と Attack 経路
（`attack_*` 5 ファイル 8 か所）に集中している。ラウドネス経路は `stereo_meter.rs` の 1 か所だけである。
**サラウンド化の作業量の重心は、ラウドネスではなく Reference と Attack にある。**

`ResamplerTo48k::new`（`resampler.rs:43`）にはチャンネル数のガードが無く、既に総称である。

### 2.1 `MeasureEngine` にはガードが無い

`engine.rs:130` はチャンネル数を検証せず、`EbuR128` の `MAX_CHANNELS = 64` までそのまま通る。
`EbuR128::new` の成否だけが実質の上限になっている。

### 2.2 `channels == 2` の分岐 — 17 か所 / 8 ファイル

ガードとは別に、2ch のときだけ別処理へ入る分岐が
`attack_detail.rs` `attack_runtime.rs` `attack_runtime_assembler.rs` `attack_runtime_worker.rs`
`spectrum_mid_side.rs` `spectrum_runtime_assemblers.rs` `spectrum_runtime_worker.rs` `stereo_meter.rs`
に 17 か所ある。これは「2ch なら右チャンネルがある」という前提の分岐であり、
**N>2 では `right` が何を指すか定義されていない。**

## 3. 固定長 2 要素配列 — 79 か所、3 分類

| 分類 | 内容 | 拡張方針 |
|---|---|---|
| **(a) 入力チャンネル** | `stereo_meter.rs` の内部状態と出力（sample peak / hold / TP / instant TP / max TP / VU / clip events / clip latched / rectified / true_peak_window / session_clip_*）、`meter_session_abi.rs` の 8 本、`meter_history.rs` `meter_history_decimation.rs` `meter_delta_history.rs` `lib.rs:3161` の `clip_event_count` | **Nch 化の対象** |
| **(b) Reference 経路** | `reference_capture_index.rs` の `rms` `peak` `filter[[f64;3];2]` `bands[[f64;4];2]` `energy` `peak`、`reference_visual.rs` の `peak` `rms` `energy` | **チャンネルではあるが別契約**（§3.1） |
| **(c) チャンネルではない** | `analysis_blind_admission_probe.rs:41` と `reference_capture_admission.rs:6` の `[PathBuf; 2]` | **対象外** |

### 3.1 Reference capture index は保存形式である

```
crates/kirin_measure/src/reference_capture_index.rs:14
const _: () = assert!(std::mem::size_of::<CaptureUnit>() == 64);

crates/kirin_hypha_ffi/include/kirin_hypha_reference_index_ffi.h:17
static_assert(sizeof(KirinReferenceCaptureUnit)==64,"Capture index storage contract");
```

`rms[2]` / `peak[2]` を `[16]` にすると **64 → 176 バイト**になり、
保存形式と `kirin_reference_index_compare` の意味が変わる。
`digest[32]` によるハッシュ同一性の判定にも影響する。**(a) と同じ扱いにしてはいけない。**

## 4. interleave / stride の 2 決め打ち — 2 か所のみ

- `mono_sum.rs:141` — `interleaved.chunks_exact(2)`。MONO は定義上 2ch 専用なので想定内。
- `reference_gain.rs:308` — `tone.iter().step_by(2)`。

**インターリーブ処理そのものは、ほぼ `channels` 変数で書かれている。**
機械的な stride 置換はほとんど不要。危険は stride ではなく §2.2 の「右チャンネル」分岐にある。

## 5. Phase D（Zwicker / Sharpness）— 機構は総称、畳み方が未定義

`phase_d/channels.rs:30` はチャンネル数で `Vec<PhaseDStream>` を作り、2 の決め打ちは無い。

しかし `push_display_slot`（`phase_d/channels.rs:60-72`）は次のように畳む。

```rust
combined.sharpness += observation.sharpness;
*target += value / n_channels as f64;      // specific loudness
...
combined.sharpness /= n_channels as f64;
```

**重みなしの算術平均である。**
7.1.4 では LFE を含む 12 チャンネルの単純平均になり、これは知覚値として定義されていない。
計画 §6 の「チャンネル別値の単純和を『サラウンドの知覚値』と宣言しない」に**現状が既に抵触する形**である。

`perceptual.rs:163-164` は `lr_stream`（`input_channels` 本）と `mono_stream`（1 本）の二本立てで、
N>2 では `lr_` という名前自体が意味を失う。

## 6. Record にチャンネル情報が無い

`crates/kirin_measure/src/plugin_data.rs` は `sample_rate` を複数の構造体に持つが、
**`channels` の出現回数は 0 である。** `record_*.rs` にも無い。

つまり **Record は「何チャンネルで測ったか」を記録していない。**
現在は mono / stereo しか通らないので実害は無いが、
計画 §5.4 が要求するメタデータ（レイアウト / map / epoch / 参照版）は**どれも存在しない。**

P-2 / P-3 は既存フィールドの拡張ではなく、**新規の記録項目の追加**になる。

## 7. 指標ごとの入力対象（第1巡 / 確定表ではない）

| 指標 | 生成元 | チャンネルの扱い | N>2 での未定義点 |
|---|---|---|---|
| LUFS-M/S/I, LRA, TP | `MeasureEngine`（`meter_session.rs:104`、`measure_thread.rs:245/252/262` の 3 実体） | ebur128 へ全チャンネル。**ガード無し** | 既定 channel map による無言欠落（findings §3） |
| Sample Peak / TP / Clip / VU | `StereoMeter`（`meter_session.rs:105`） | `[2]` 固定、`1..=2` ガード | 配列と身元の両方 |
| Correlation / Balance / MONO | `stereo_meter.rs:343` ほか | `channels != 2` で無効化 | 置き換え定義が未確定（計画 §7） |
| Spectrum / MID-SIDE | `spectrum_runtime_*`、`spectrum_mid_side.rs:14-15` | `channels == 2` 前提の `right` | `right` の定義、Side の意味 |
| Attack | `attack_runtime.rs:79` ほか 8 か所 | `matches!(_, 1\|2)` | 全経路 |
| Sharpness / Zwicker | `phase_d`、`perceptual.rs` | 機構は総称、**重みなし平均** | 畳み方（§5） |
| Absolute timeline | `absolute_timeline.rs:42/170` | `(1..=2)` | — |
| Reference visual / tonal / index / gain | `reference_*` 5 ファイル | `(1..=2)` / `matches!` 8 か所 | 保存形式（§3.1）、stereo Reference との比較条件 |
| Local blind capture | `local_blind_capture_protocol.rs:193` | `matches!` | — |
| Space decay | `space_decay.rs:135` | `matches!` | — |

**この表は第1巡である。** 各指標の窓・正規化・無効条件までは読み切っていない。

## 8. 第1巡で見えた作業の重心

1. **Reference 経路（8 ガード + 保存形式契約）** が最大の塊。ラウドネスより重い。
2. **Attack 経路（8 ガード + `right` 分岐）** が次。
3. **ラウドネス経路は `stereo_meter.rs` の 1 ガードと ABI の 8 配列**で、意外に小さい。
4. **Record / メタデータはゼロから作る。** 既存の拡張ではない。
5. interleave / stride の機械的置換はほぼ不要。

計画の Phase 順（P-1 → P-2 → P-3）は変えなくてよいが、
**P-1 の「レイアウト記述を渡す先」に Reference 5 経路と Attack 5 経路が入る**ことを前提にする。

## 9. P-0 の残り

- 指標ごとの窓・正規化・無効条件・レイアウト変更時のリセット（§7 を確定表にする）。
- L/R 344 件の逐一分類（現状はファイル単位のサンプル分類）。
- `Vec` 上でチャンネル 2 を仮定する箇所の走査（`[2]` パターンでは拾えない）。
- state 復元経路と PRE/POST 比較条件の通読。
- epoch / Record 遷移表の作成。
- ABI 方針の確定値（サイズは実測する。概算を確定値にしない）。

## 10. 承認が要る事項への影響

計画 §11.2 の 3 件について、第1巡で得た材料。

1. **容量 16 ＋ 対応配置は別管理** — §3 の (a) は素直に Nch 化できる。
   ただし (b) の Reference は別契約なので、**「全部 16」ではなく分類ごとの方針**が要る。
2. **非互換変更中は測定を保留** — §6 のとおり Record にチャンネル情報が無いので、
   epoch は既存フィールドの追加ではなく新規設計になる。
3. **初期公開で適用外を明示するか** — §8 のとおり Reference と Attack が重心なので、
   ラウドネス + ピークだけ先に出す形は技術的には成立する。**どうするかは Daisuke の判断。**
