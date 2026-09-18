# Hypha サラウンド化 — 全指標の契約表（P-0 §6 / rev.2）

Status: 調査と分類。実装なし・仕様変更なし。Nch での扱いの確定は Daisuke の承認事項。

Date: 2026-09-18 / rev.2

Branch: `claude/eager-pascal-edacak` / 確認 commit `242ba64`

初版は分類軸が 1 本で、**「コードが今 6ch を拒否すること」と「その指標が本質的に stereo 専用であること」を
混同していた。** 二軸へ分離して全面的に書き直した。訂正は §1 に集約する。

## 0. 二軸で分類する

| 軸 | 問い |
|---|---|
| **Runtime behavior** | 現在の実装へ 6ch / Nch を渡したとき、**コードが実際に何をするか** |
| **Semantic applicability** | その**測定量自体**が Nch で意味を持つか |

**`R`（構築拒否）と `STEREO-ONLY` は同義ではない。**
Correlation は runtime 上は R だが、それは `StereoMeter` の親ガードによるものであり、
指標の意味として stereo 専用であることとは別の事実である。

### 0.1 Runtime behavior

| 記号 | 状態 | 意味 |
|---|---|---|
| **R** | REJECT | 構築または入力が**明示的に拒否**され、値が出ない |
| **C** | CLAMP / SILENT DROP | 構築は成功するが、**別チャンネル数へ丸められる**か、投入が**黙って失われる** |
| **G** | PRODUCES WRONG | 値は出るが、channel map 等のため**測定対象が欠落して誤る** |
| **P** | PRODUCES | Nch 入力を処理し、値を生成する |
| **U** | UNINVESTIGATED | runtime behavior を未確定 |

### 0.2 Semantic applicability

| 状態 | 意味 |
|---|---|
| **NCH-DEFINED** | 規格または測定定義として Nch の意味が定まっている |
| **CHANNEL-LOCAL** | 各チャンネル単独の事実として成立し、Nch 集約を必要としない |
| **STEREO-ONLY** | 2ch でのみ定義される現行指標。Nch では適用外 |
| **AGGREGATION-UNDEFINED** | チャンネル別処理は可能でも、**Nch 全体を一値・一イベント・一表示へ畳む意味が未定義** |
| **NEW-METRIC-CANDIDATE** | 現行指標を一般化せず、**別測定量**として設計する候補 |
| **COMPARABILITY-UNDEFINED** | 異種 layout 間の比較条件が未定義 |
| **UNINVESTIGATED** | 未調査 |

## 1. 初版からの訂正

| # | 初版 | 訂正 | 根拠 |
|---|---|---|---|
| 1 | LUFS / LRA / **TP** をまとめて **G** | **TP は G ではない。** `MeasureEngine` の TP は全チャンネルで正しい（§4 実測） | probe 実測 + `engine.rs:382` |
| 2 | 分類軸が 1 本 | **二軸へ分離。** R と STEREO-ONLY を混同しない | — |
| 3 | Balance は「Nch 一般化」推奨 | **AGGREGATION-UNDEFINED。** 候補に留める（§7） | — |
| 4 | Attack は「Nch 一般化」 | **二層に分ける。** detector は CHANNEL-LOCAL、event aggregation は UNDEFINED（§5.2） | — |
| 5 | Spectrum は「Nch 一般化」 | **二層に分ける。** analysis は CHANNEL-LOCAL、FIELD / presentation は UNDEFINED（§5.3） | — |
| 6 | 「ABI `[2]` を `[16]` に広げれば足りる」 | **不足。** 内部 state・history・session・FFI・表示・reset を同じ channel identity 契約で一般化する必要がある | — |
| 7 | `space_decay` は要調査 | **調査完了。** broadband（全 ch 合算 / ch 数）。ガードは入力検証のみ | `space_decay.rs:120-126` |
| 8 | `local_blind_capture_protocol` は要調査 | **調査完了。** metadata のみで **PCM を運ばない**。ガードは request 妥当性検査 | module doc + `:193` |

## 2. 契約表

| 指標群 | Runtime | Semantic | 現時点の扱い |
|---|---|---|---|
| LUFS-M / S / I | **G** | NCH-DEFINED | 明示 channel map を入れて Nch 一般化 |
| LRA | **G** | NCH-DEFINED | loudness path と同じく明示 map 必須 |
| True Peak（`MeasureEngine` session / per-ch） | **P** | CHANNEL-LOCAL | **既に正しい**（§4）。map 導入で壊さないことを回帰で固定 |
| True Peak / instant TP（`StereoMeter` ABI 公開） | **R** | CHANNEL-LOCAL | Nch 一般化。**ABI だけでなく channel identity 契約全体** |
| Sample Peak / hold | **R** | CHANNEL-LOCAL | 同上 |
| Clip / clip latched | **R** | CHANNEL-LOCAL | 同上 |
| VU | **R** | **CHANNEL-LOCAL（集約なし）** | `vu_levels()` は `.take(self.channels)` の純チャンネル別。Nch 化は素直 |
| Correlation | **R** | **STEREO-ONLY**（pair-extension candidate） | 現行値は stereo 専用。左右ペア拡張は**別仕様** |
| Balance / BalanceState | **R** | **AGGREGATION-UNDEFINED** | 左右 group energy ratio は**候補に留める**（§7） |
| Stereo field density | **R** | **STEREO-ONLY** | Nch では明示的適用外 |
| MONO | **R** | **STEREO-ONLY** / NEW-METRIC-CANDIDATE | 現行 MONO は stereo 専用。downmix survival は**別測定量** |
| Spectrum analysis | **C** | CHANNEL-LOCAL | **silent clamp / drop を最優先で除去**。Nch 分析自体は成立 |
| Spectrum FIELD / presentation | **C** | **AGGREGATION-UNDEFINED**（ABI にチャンネルを表す手段が無い / §9.1） | 6 秒 FIELD へ**根拠なく Nch 平均を入れない** |
| MID-SIDE | **C**（Spectrum と同一 runtime） | **STEREO-ONLY** | Nch では明示的適用外 |
| Attack detector | **R** | CHANNEL-LOCAL | チャンネル別検出は Nch 化候補 |
| Attack event aggregation | **R** | **AGGREGATION-UNDEFINED** | 複数 ch の近接 attack の扱いが未定義（§5.2） |
| Sharpness continuous | **R** | **要確認** [C] | mono / stereo 定義を保持し、Nch は別途検証 |
| Zwicker / specific loudness（`phase_d`） | **P** | **AGGREGATION-UNDEFINED** | 現行算術平均を surround perceptual quantity と**宣言しない** |
| Absolute timeline | **R** | COMPOSITE / dependent | LUFS / TP / Sharpness の各契約確定後に判断 |
| SPACE decay | **R** | **broadband（全 ch 合算）** | 計算は既に総称。ガードは入力検証のみ |
| Reference visual | **R** | COMPARABILITY-UNDEFINED | A/B layout 比較条件を先に定義 |
| Reference tonal | **R** | COMPARABILITY-UNDEFINED | 同上 |
| Reference capture index | **R** | **STORAGE-CONTRACT DEPENDENT** | 64 byte 保存契約を独立に扱う |
| Reference gain | **R** | COMPARABILITY-UNDEFINED | stereo Reference と Nch input の比較条件を先に定義 |
| Local blind capture | **R** | metadata のみ（PCM 非依存） | request 妥当性の ch 範囲を広げるだけで足りる可能性 |

## 3. Runtime 別の危険度

**最優先で除去すべきは C と G である。R ではない。**

- **R は止まる。** 利用者に「使えない」と分かる。
- **C は成功したように見えて何もしない。** `SpectrumRuntime` は構築に成功し、
  `stats().channels` が 2 を返し、投入を 40/40 drop する。
- **G は値まで出る。** LUFS は 6ch で数値を返し、その数値が誤っている。

> **「機能が少ない」ことより、「値が出ているのに意味が違う」ことを重大な失敗として扱う。**

## 4. 実測 — True Peak は LUFS と分離する [A]

`channel_contract_probe truepeak <rate> <channels>`。
1 チャンネルだけに既知ピーク（0.5 = −6.02 dBFS の 997 Hz）を入れ、位置を順に移す。
`EbuR128` の mode は `MeasureEngine` と同一。

| ch 数 | 結果 |
|---|---|
| 2 | 全 2 チャンネルで **−6.02 dBTP を該当チャンネルのみに観測。ok** |
| 6 | 全 6 チャンネルで **ok** |
| 12 | 全 12 チャンネルで **ok** |

**12ch では index 3 と 6〜11 が default channel map で `Unused` になるが、
True Peak はそれらのチャンネルでも正しく観測される。**

session 集約も総称である。

```rust
crates/kirin_measure/src/engine.rs:382-385
let max_tp_lin = (0..self.n_channels as u32)
    .filter_map(|ch| self.ebu.true_peak(ch).ok())
    ...
```

**したがって TP を LUFS / LRA と同じ理由で G と断定してはいけなかった。**
channel weighting と channel map は loudness の合算に効くのであって、
チャンネル別ピーク検出には効かない。

**注意**: ABI が公開している per-channel TP は `StereoMeter` 側であり、そちらは R である。
**同じ「True Peak」でも経路が 2 つあり、状態が違う。**

## 5. channel-local と Nch aggregate を分離する

### 5.1 Sample Peak / TP / Clip は「意味が簡単」だが「実装が ABI だけ」ではない

測定量の一般化は小さい。しかし必要なのは
**`StereoMeter` 内部 state / history / session / FFI・ABI / 表示 / reset を
同じ channel identity 契約で Nch 化すること**である。

**意味が簡単 ≠ 実装が ABI だけで済む。**

### 5.2 Attack は二層

- **Detector**: channel-local。Nch へ一般化可能。
- **Event aggregation**: **未定義。**

例:

```
L  : 1.000 s
C  : 1.004 s
Ls : 1.020 s
```

これを 3 events とするか、1 spatial event とするか、最初だけを採るか、
channel identity を残すかは**別仕様**である。

「検出が channel-local だから Nch 一般化」で閉じない。

### 5.3 Spectrum は二層

- **Analysis**: channel-local spectrum は成立する。
- **Presentation / FIELD**: **Nch 集約が未定義。**

現行 6 秒 FIELD の時間契約は維持する（横軸 Low → High、縦軸 上 = 過去 / 下 = 現在、
現在の観測が下端から発生して上へ移動）。

**Nch 化を理由に、この FIELD へ根拠のない 12ch 平均を投入しない。**
channel selector / speaker group / aggregate / renderer output などの候補は **P-0 では決定しない。**

## 6. stereo 専用を「挿さらない」から「適用外」へ

現行は `StereoMeter` の親ガードが 6ch を拒否するため、
Correlation / field density / MONO へ到達できない。

| 指標 | Runtime | Semantic | 扱い |
|---|---|---|---|
| Correlation | R | STEREO-ONLY | 左右ペア correlation を足す場合、**現行 stereo correlation の単純 Nch 化ではなく pair-extension** |
| Stereo field density | R | STEREO-ONLY | 2ch 散布図の意味をそのまま拡張しない。**適用外を明示** |
| MONO | R | STEREO-ONLY | **現行 MONO の名前だけを残して意味を差し替えない。** downmix survival は NEW-METRIC-CANDIDATE |

## 7. Balance はまだ NCH-DEFINED としない

「左右グループのエネルギー比、C / LFE 除外」は合理的な候補だが、**現時点では提案である。**

- 7.1.4 では複数の左右対を**同一重みで加算するか、speaker position を考慮するか**で結果が変わる。
- Center を除外すると、**Center 主体の mix でも L/R balance が 0 dB になり得る。**
  それは「左右成分の balance」としては成立しても「全体の spatial balance」ではない。

したがって **Balance = AGGREGATION-UNDEFINED。** 候補定義は別文書で比較する。

## 8. 優先順位

### Priority 1 — silent / wrong result を消す

1. **Spectrum の C** — 無言 2ch clamp と入力 drop
2. **LUFS / LRA の G** — default channel map

**R より先に扱う。** R は止まるが、C / G は正常動作と誤認される。

### Priority 2 — channel-local fact

Sample Peak / True Peak（`StereoMeter` 側）/ Clip / channel-local Attack detector /
channel-local Spectrum analysis。

### Priority 3 — semantic design が必要

Correlation pair extension / Balance / MONO 置換 / Attack event aggregation /
Spectrum FIELD aggregation / Phase D aggregate / SPACE。

### Priority 4 — Reference

**単なる Nch 化ではない。** 入力 A = 7.1.4、Reference B = stereo のような
異種 layout 比較を許すなら、**「比較可能」とは何かを先に定義する。**

## 9. P-0 §6 を閉じる前の必須 gate

| # | gate | 状態 |
|---|---|---|
| 1 | TP を LUFS から分離した 6ch / 12ch single-channel probe | **完了**（§4） |
| 2 | `space_decay` の実装読破 | **完了**。broadband（全 ch 合算 / ch 数）。`space_decay.rs:120-126` |
| 3 | `local_blind_capture_protocol` の PCM / metadata 依存性 | **完了**。metadata のみ。PCM を運ばない |
| 4 | Spectrum の C が発生する層の特定 | **完了**（§9.1）。**ABI と表示にチャンネルの概念が無い** |
| 5 | `phase_d` 以外に「runtime generic / aggregate 未定義」がないか全走査 | **完了**（§9.2）。**該当は `phase_d` のみ** |

### 9.1 Spectrum の C はどの層か

| 層 | 状態 |
|---|---|
| constructor clamp | **ここが起点。** `spectrum_runtime.rs:91` `num_channels.clamp(1, 2)` |
| runtime state | clamp 後の値を保持。`stats().channels` が 2 を返す |
| push admission | `num_channels != self.num_channels` で **全 drop**（`:245`） |
| worker | `block.channels != self.num_channels` で discard（`spectrum_runtime_worker.rs:42`） |
| **FFI** | `channels: uint8_t` はあるが、**`channel_mode` は 3 値のみ**（`kirin_hypha_ffi.h:68-70` `KIRIN_SPECTRUM_CHANNEL_LR / MID / SIDE`） |
| **presentation** | 同じ 3 値。**per-channel spectrum を表す手段が無い** |

**clamp を外しても足りない。** ABI と表示の channel model が
「LR / Mid / Side」という**stereo 由来の 3 値**であり、
**6ch のスペクトルを表現する場所が存在しない。**

したがって Spectrum の C は 1 行の修正ではなく、**presentation model の設計**である。

### 9.2 gate 5 — runtime generic かつ集約未定義は `phase_d` のみ

`channels` を受け取るコンストラクタ 21 件を全走査し、ガードの有無と集約を確認した。

| 分類 | 対象 |
|---|---|
| **集約が未定義** | **`phase_d/channels.rs:30, :86` のみ**（重みなし算術平均） |
| 集約は定義済み | `absolute_level.rs:30`（TP は `0..channels` の max、LUFS-M は規格定義） |
| 変換であって指標ではない | `resampler.rs:43`、`raw_pre_roll.rs:27` |
| ガード付き経路からしか到達しない | `attack_detail` `attack_runtime_assembler` `spectrum_runtime_assemblers`（×3）`reference_tonal:76` `meter_session` |

**`absolute_level.rs:30` は 4 つ目の `EbuR128` 生成経路であり、チャンネル数ガードを持たない。**
本番の呼出し元は `absolute_timeline.rs:179` の 1 つで、そちらは `:170` でガードされる。
**独立した危険ではないが、明示 map の適用対象に必ず含める。**

## 10. 初期公開の考え方

**初期公開を「全指標 16ch 化」と定義しない。**

最小の安全な状態は次である。

- Nch-defined / channel-local な測定は**正しく動く**
- stereo-only は**明示的に適用外**
- aggregation-undefined は**推測で値を出さない**
- unknown layout は**拒否**
- **silent clamp / silent drop が存在しない**
- **channel map が不明な状態で loudness 値を出さない**

## 11. 承認事項（P-0 が事実を確定した後、順に）

1. どの指標を初期 5.1 で Nch 化するか
2. どの stereo-only 指標を明示的適用外とするか
3. Correlation pair extension / Balance / MONO 置換 / Attack aggregation /
   Spectrum aggregation を初期版に含めるか
4. Reference で異種 layout 比較を許すか
5. 7.1.4 へ進む時点

工数・日程は判断材料にしない。

## 12. 評価 — 問題は 4 層に分かれる

サラウンド化は「チャンネル数を 16 へ増やす作業」ではない。

| 層 | 内容 |
|---|---|
| **1. Transport / runtime failure** | reject、clamp、drop |
| **2. Measurement correctness** | channel map、channel-local fact |
| **3. Semantic aggregation** | Nch を一値・一イベント・一 FIELD へどう畳むか |
| **4. Presentation / applicability** | stereo-only をどう明示するか |

**P-1 へ進む前に、この 4 層を混同しない契約を固定する。**

最も危険なのは「構築できない R」ではない。
**構築できて正常に見える C と、値まで出る G である。**

## 13. probe の構成

役割で 2 つに分けてある。**バイト数を測るものと、挙動を測るものは別物である。**

| example | 測るもの | mode |
|---|---|---|
| `memory_contract_probe` | バイト数（RSS） | `single` `scaling` `touched` `census` `routing` `calibrate` |
| `channel_contract_probe` | **挙動**（受理するか、どのチャンネルで観測されるか） | `accept` `truepeak` |

```bash
cargo run -p kirin_measure --example channel_contract_probe --release -- truepeak 48000 12
cargo run -p kirin_measure --example channel_contract_probe --release -- accept 48000 6
```

`memory_contract_probe` は **1 ケース 1 プロセス**で実行する（§10 / capacity 文書）。

## 14. 未確認 [C]

- Sharpness continuous の Nch 適用可否。
- Spectrum の presentation model を Nch でどう設計するか（**C の解消はここに依存する**）。
- 各指標の窓長・hop・正規化・無音条件・reset 条件。**本表は Nch 挙動に絞っている。**
- Reference B が stereo、入力 A が Nch のときの比較条件。
