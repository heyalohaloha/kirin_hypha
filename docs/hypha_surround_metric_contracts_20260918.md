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
| Sharpness continuous（LR view） | **R** | **AGGREGATION-UNDEFINED**（`phase_d` の平均を継承 / §11.5） | 集約の定義が決まるまで一般化しない |
| Sharpness continuous（MID / SIDE view） | **R** | **STEREO-ONLY**（`as_chunks::<2>()` で対を前提） | Nch では明示的適用外 |
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

## 10. Reference の異種 layout 比較（承認事項 4）

### 10.1 現行の比較契約 [A]

```rust
crates/kirin_measure/src/reference_gain.rs:160-174
pub fn analyze_reference_gain(a: &[f32], b: &[f32], sample_rate: u32, channels: usize)
    ...
    || !matches!(channels, 1 | 2)
    || a.len() != b.len()
```

```rust
crates/kirin_measure/src/reference_gain.rs:80-95   analyze_track_event_gain
    ... 同じ形（post と pre で channels は 1 つ、長さ不一致はエラー）
```

```cpp
juce_shell/src/reference_audition/ReferenceACaptureRevisit.cpp:87
if (block.channels != data.channels || ...) // 不一致は拒否
```

**A と B は「同一チャンネル数・同一長・同一 sample rate」でなければ比較しない。**
`channels` はひとつの引数で、**両者が別の layout を持つことを表現できない。**

異種 layout 比較は「未定義」ではなく、**構造的に排除されている。**

### 10.2 しかし異種 layout は自然な運用である

AGENTS.md:59 / 77-83 のとおり、**Reference B は登録済みのファイル**である。
利用者が明示した比較試聴で、不変の Reference を B 経路で再生する。

したがって **入力 A が 7.1.4 で、登録した Reference B が stereo** という組合せは、
**B の layout が A と一致する保証がどこにも無い**以上、**想定すべき実用ケース**である。

**ここまではコードから確認できる事実である。**
**この組合せがどの程度の頻度で起きるかは運用上の評価であり、本書では判断しない** [C]。
設計判断の根拠として使うのは「起こり得る」ことまでで、「普通に起きる」ことではない。

### 10.3 「比較可能」の定義 — 推奨

| 案 | 内容 | 評価 |
|---|---|---|
| (i) 同一 layout のみ | 現行の延長。A と B の layout が一致しなければ比較しない | 最も安全。**だが stereo reference が 7.1.4 セッションで一切使えない** |
| (ii) 明示 view 経由 | A を明示した downmix で B の layout へ落として比較する | 実用的。**測定条件の記録が必須** |
| (iii) 共通 view | A と B の両方を mono / stereo などの共通 view へ落とす | B が失う情報も増える。(ii) より根拠が弱い |

**推奨: 既定を (i) とし、(ii) を「利用者が明示的に選択した比較 view」として追加する。**

根拠:

1. **R-22**。どの view で比較したかを利用者が選び、Hypha は**選ばれた view の事実だけを出す**。
   Hypha が勝手に downmix して「比較できました」とはしない。
2. **BS.1770-5 Annex 4**（findings §6.2）。レンダリングして測る場合は
   **使用した配置とレンダリング方法を報告する**。比較 view も同じ規律で記録する。
3. **R-12**。比較用 downmix は**測定のための内部計算**であり、入力 A の出力音声を変えない。
   既存 MONO と同じ扱いで、新しい原則を持ち込まない。
4. **既定を (i) にする理由**: 無言で downmix して比較すると、
   **利用者は「7.1.4 と stereo を比べた」ことに気づかない。** §3 の C と同じ失敗になる。

### 10.4 記録すべき項目

比較を行った場合、計画 §5.4 の分離に次を足す。

- A の layout / B の layout
- **比較に使った view**（どちらに合わせたか、恒等か downmix か）
- **downmix 係数の出典と版**（計画 §7.4。**未確認の係数を記憶で埋めない**）
- 比較が成立しなかった場合の理由（layout 不一致 / 長さ不一致 / rate 不一致）

**「比較できない」を無言で空値にしない。** 理由まで記録する。

### 10.5 この判断に依存する指標

| 指標 | 依存 |
|---|---|
| Reference gain | **直接依存。** `channels` 引数の形そのものを変える |
| Reference visual | 直接依存 |
| Reference tonal | 直接依存 |
| Reference capture index | **依存しない。** digest は PCM 同一性であって layout 同一性ではない（capacity §6）。ただし 64 byte 保存契約は別途 |

### 10.6 downmix 係数 — 「規格名」ではなく「どの renderer のどの downmix」で固定する [B]

**Hypha が係数を発明しない。** (ii) を採る場合、再現する対象を出典付きで固定する。

出典は Daisuke が一次資料で確認した [B]。
**本セッションの egress policy では再取得できないため、Claude Code 側では未検証である。**

#### 10.6.1 確定できるもの — Dolby Atmos Renderer の Lo/Ro

5.1 → stereo:

```
Lo = L + 10^(-3/20)·C + 10^(-3/20)·Ls
Ro = R + 10^(-3/20)·C + 10^(-3/20)·Rs
```

| 入力 | → Lo | → Ro |
|---|---:|---:|
| L | 1.0 | 0 |
| R | 0 | 1.0 |
| C | 0.70795 | 0.70795 |
| Ls | 0.70795 | 0 |
| Rs | 0 | 0.70795 |
| **LFE** | **0** | **0** |

**LFE を Lo/Ro へ足さない。** Dolby のこの式に LFE は無い。Stereo Direct も LFE を含まない。

7.1 → 5.1（Standard / Lo-Ro）:

```
Ls = Lss + Lrs
Rs = Rss + Rrs
```

**Atmos の stereo downmix は Stereo Direct を除き、Atmos → 7.1 → 5.1 → 2.0 の連鎖である。**
したがって 7.1.4 → stereo を実装するなら、**この連鎖として実装し、単一行列に畳まない。**
畳むと「どの段の係数か」が記録から消える。

#### 10.6.2 −3 dB には 2 つの解釈がある

| 表記 | 値 | dB |
|---|---:|---:|
| `10^(-3/20)` | **0.707945784** | **−3.0000 dB** |
| `1/√2`（半電力） | 0.707106781 | −3.0103 dB |

**差は 0.0103 dB。** Dolby の式は `10^(-3/20)`、すなわち**厳密な −3.00 dB** であり、
半電力の `1/√2` ではない。

**測定器としては、この差を取り違えてはならない。**
「C は −3 dB」という言い方は、どちらを指すか決まらない。**式か数値で書く。**

#### 10.6.3 ITU-R BS.775 を単一係数の出典にしない

- 現行版は **BS.775-4（2022）** であり、-3 ではない。
- BS.775-4 は Downward mixing の Annex を持ち、ITU は多チャンネル → 少チャンネル変換を正式に扱う。
- **しかし単一の係数を定めていない。** 旧版の Annex 8 でも surround について
  **0.7071 / 0.5000 / 0.0000 等の選択肢**が明記されている。

したがって:

> **「ITU 5.1 → Stereo = C −3 / Surround −3」と一般名称で固定してはならない。**
> ITU は選択肢を与えており、唯一の係数ではない。

**Dolby Atmos Renderer の Lo/Ro として固定するなら根拠がある。**
この区別は、Hypha が「何を再現したか」を記録できるかどうかを決める。

#### 10.6.4 記録する項目

§10.4 の「downmix 係数の出典と版」を、次の粒度にする。

- **renderer / 規格の名前**（例: Dolby Atmos Renderer）
- **その中のどの downmix か**（例: Standard Lo/Ro、Stereo Direct）
- **版**
- **連鎖のどの段か**（7.1.4 → 7.1 → 5.1 → 2.0 のどこを通ったか）

**「ITU 準拠の downmix」とだけ書かない。** それでは再現できない。

#### 10.6.5 まだ無いもの [C]

- **7.1.4 → 7.1** の段（天井チャンネルの扱い）。上記の一次資料確認には含まれていない。
- Stereo Direct の完全な定義。
- Hypha がどの版を再現対象にするか（**Daisuke の決定事項**）。

## 11. Spectrum の presentation model（Priority 1 の実体）

gate 4 で「ABI にチャンネルを表す手段が無い」と分かった以上、
**Spectrum の C の解消は clamp の除去ではなく、ここの設計である。**

### 11.1 現行モデル [A]

ABI は 2 つある。

```c
crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h:228-245  KirinSpectrumView
  uint8_t channel_mode;   /* KIRIN_SPECTRUM_CHANNEL_LR / MID / SIDE */
  uint8_t channels;       /* 1=mono / 2=stereo */
  float pre_dbfs[256]; float post_dbfs[256]; float display_db[256];
```

```c
crates/kirin_hypha_ffi/include/kirin_hypha_spectrum_mid_side_ffi.h  KirinMidSideSpectrumView
  float mid_dbfs[256]; float side_dbfs[256];   /* 対で 1 つの view */
```

**main view が運ぶのはスペクトル 1 本である。**
チャンネルの次元は配列ではなく **`channel_mode` という「表示する view の選択」**に畳まれている。

**そして MID / SIDE は、チャンネルではなく導出 view である。**
`KirinMidSideSpectrumView` が別構造体で対を運ぶのも同じ理由による。

> **現行モデルは「チャンネルの配列」ではなく「view を 1 つ選んで描く」である。**

### 11.2 推奨 — selector を拡張する

| 案 | 内容 | 評価 |
|---|---|---|
| **(a) selector 拡張** | `channel_mode` に**交渉済み layout のチャンネルを個別に足す**（L / R / C / LFE / Lss …）。表示は従来どおり 1 本 | **推奨** |
| (b) 配列化 | view を per-channel スペクトルの配列にする | ABI も表示も根本から変わる。12 本を 1 画面に置く設計が別途要る |
| (c) 集約 | N チャンネルを 1 本へ畳む | **AGGREGATION-UNDEFINED。** 根拠のない平均は R-22 に反する |

**(a) を推奨する根拠:**

1. **モデルを変えない。** 現行は既に「view を 1 つ選ぶ」であり、
   MID / SIDE がチャンネルでない時点で **selector は既にチャンネル以外を含んでいる。**
   L / R / C / Lss … を足すのは**同じ枠の拡張**である。
2. **集約を発明しない。** (c) の AGGREGATION-UNDEFINED を回避できる。
   R-22 の「価値判断も推測も出さない」に沿う。
3. **6 秒 FIELD の契約が保たれる。** FIELD は常に「選ばれた 1 view」を描くので、
   横軸 Low → High、縦軸 上 = 過去 / 下 = 現在、下端から発生して上へ移動、が変わらない。
   **根拠のない 12ch 平均を FIELD へ入れない**という §5.3 の要求をそのまま満たす。
4. **ABI のサイズが変わらない。** main view は 1 本のままである。

### 11.3 (a) を採る場合に決めること

- **selector はチャンネルを index ではなく layout 上の役割で指す。**
  findings §2 のとおり JUCE の index 順は直感と一致しない。
  計画 §5.1 の layout 記述と同じ語彙を使う。
- `channels` のコメント `/* 1=mono / 2=stereo */` を
  **「交渉済み layout のチャンネル数」**へ変える。selector が「どれを表示しているか」を持つ。
- **MID / SIDE は STEREO-ONLY のまま**（§6）。L/R 対を持つ layout でのみ選択肢に出す。
  Nch で勝手に「全体の M/S」を作らない。
- **PRE/POST 比較は両者が同じ view を選んでいることを条件にする。**
  Reference の §10 と同じ規律。**違う view の比較を無言で成立させない。**

### 11.3.1 selector の粒度 — 役割で指す（index ではない）[A]

**決め手は epoch である。**

```rust
crates/kirin_measure/src/spectrum_runtime.rs:212-215
let previous = self.channel_mode.swap(mode as u8, Ordering::AcqRel);
if previous != mode as u8 {
    self.generation.fetch_add(1, Ordering::AcqRel);   // 履歴を捨てる
```

**比較しているのは生の `u8` である。**

selector が **index** だと、7.1.4 → 5.1.4 の layout 変更で
**index 7 は有効なまま、指すチャンネルだけが変わる。**
`previous != mode` が成立しないので **generation は上がらず、履歴が捨てられない。**
**別のチャンネルの履歴が無言で連結する。** §3 の C と同じ失敗である。

selector が **役割**（`Lss` 等）なら、layout 変更で
「役割が残る」か「役割が消える」かのどちらかになり、**どちらも検出できる。**

> **selector は layout 上の役割で指す。index では layout 変更を検出できない。**

**有効な値の集合は、交渉済み layout の `channel_positions[]` そのものである**（計画 §5.1）。
これに、適用できる場合の導出 view（LR / MID / SIDE）を足す。**別途の取捨選択は要らない。**

既定値は現在 `Lr = 0`（`spectrum.rs:105-106`、`PluginProcessor.h:429`）。
**L/R 対を持つ layout では LR を既定のまま維持し、持たない layout では先頭の位置を既定にする。**

### 11.3.2 UI の制約 — 3 項目固定のピクセル帯である [A]

```cpp
juce_shell/src/HyphaSpectrumUiContract.h:118-120
constexpr int spectrumChannelModeHeight = 13;
constexpr int spectrumChannelModeGap = 2;
constexpr std::array<int, 3> spectrumChannelModeWidths { 20, 26, 30 };
```

`channelModeBoundsFor`（`HyphaSpectrumGeometry.h:110-122`）は
この幅表を左から積んで hit 領域を作る。**帯の全幅は 76 px + gap×2 である。**

**12 チャンネルは入らない。** 同じ帯へ足す前提で設計しない。

**ただし値空間と UI 部品は別である。**

- **値空間**（`u8` の selector）は §11.3.1 のとおり拡張する。**epoch 機構がそのまま働く。**
- **UI 部品**は別途必要になる。現行の 3 項目帯は導出 view の数に合わせて作られている。

この UI には既に cycle 型の先例がある
（`PluginEditor.cpp:216` `spectrumSizeToggle` が 100/125/150/200% を cycle する）。

**どの部品にするかは P-0 では決めない。** 決めるのは
**「値空間は役割で拡張する」**ことと、**「3 項目帯には入らない」**ことである。

### 11.3.3 selector は永続化されていない [A]

`preferredSpectrumChannelMode`（`PluginProcessor.h:429`）の既定は `KIRIN_SPECTRUM_CHANNEL_LR` で、
`PluginProcessorState.cpp` が保存するのは `observatory_size` などであり、**channel mode は含まない。**

**再読み込みで LR に戻る。**

含意:

- **state 復元で index と役割の不一致が起きる心配は無い**（そもそも復元しない）。
- 一方、**チャンネル数が増えると「毎回選び直す」ことになる。**
  永続化するかどうかは別途の判断であり、**するなら役割で保存する**（index では layout 変更に耐えない）。

### 11.4 初期版に含めないもの

- **全チャンネル同時表示。** 表示設計が別途要る。FIELD に 12 本を置く根拠が無い。
- **チャンネル群（speaker group）単位の view。** 集約の定義が要る。
- **renderer 出力の view。** findings §6.2 の記録要件を満たす設計が先。

**これらは「やらない」ではなく「初期版では決めない」。**

### 11.5 Sharpness も同じ 3 view モデルである [A]

`SharpnessContinuousAnalyzer` は Spectrum とまったく同じ構造を持つ。

```rust
crates/kirin_measure/src/perceptual.rs:253-258   analyze_lr
    self.lr_stream.push_display_slot(&self.lr_samples)   // input_channels 本 → 算術平均
```

```rust
crates/kirin_measure/src/perceptual.rs:272-283   analyze_mono
    let polarity = if channel_mode == SpectrumChannelMode::Mid { 1.0 } else { -1.0 };
    let (frames, remainder) = input.as_chunks::<2>();
    ... (frame[0] + polarity * frame[1]) * 0.5
```

| view | 実体 | Nch での状態 |
|---|---|---|
| **LR** | `lr_stream`（`input_channels` 本）→ `phase_d` の**重みなし算術平均** | **AGGREGATION-UNDEFINED。** ガードを外せば「動く」が意味が未定義 |
| **MID** | `(L + R) * 0.5` を 1 本の stream へ | **STEREO-ONLY。** `as_chunks::<2>()` が対を前提 |
| **SIDE** | `(L − R) * 0.5` を 1 本の stream へ | 同上 |

**`as_chunks::<2>()` は 6ch の interleaved バッファに対して
チャンネル境界を跨いで対を切る。** `perceptual.rs:139` のガードが到達を防いでいる。

### 11.6 Spectrum と Sharpness は同じ設計判断で閉じる

両者とも **「LR / MID / SIDE の 3 view から 1 つ選ぶ」**モデルであり、
**MID / SIDE はチャンネルではなく導出 view**、**LR は per-channel の畳み込み**である。

したがって §11.2 の推奨 (a)（selector 拡張）は、**Spectrum と Sharpness の両方に同じ形で効く。**

- selector に layout のチャンネルを足す → **どちらも「選ばれた 1 チャンネル」を測れる**。
  Sharpness の LR view が抱える AGGREGATION-UNDEFINED を**回避できる**
  （平均を定義しなくても、チャンネル別の値は定義済みだから）。
- MID / SIDE は両方で STEREO-ONLY のまま。L/R 対を持つ layout でのみ出す。

**これは偶然ではない。** 同じ 3 view モデルを共有しているので、
**片方だけを直すと UI の一貫性が壊れる。**

## 12. 初期公開の考え方

**初期公開を「全指標 16ch 化」と定義しない。**

最小の安全な状態は次である。

- Nch-defined / channel-local な測定は**正しく動く**
- stereo-only は**明示的に適用外**
- aggregation-undefined は**推測で値を出さない**
- unknown layout は**拒否**
- **silent clamp / silent drop が存在しない**
- **channel map が不明な状態で loudness 値を出さない**

## 13. 承認事項（P-0 が事実を確定した後、順に）

1. どの指標を初期 5.1 で Nch 化するか
2. どの stereo-only 指標を明示的適用外とするか
3. Correlation pair extension / Balance / MONO 置換 / Attack aggregation /
   Spectrum aggregation を初期版に含めるか（**§11。Spectrum は推奨 (a) selector 拡張**）
4. Reference で異種 layout 比較を許すか（**§10。推奨は「既定 (i) + 明示選択で (ii)」**）
5. 7.1.4 へ進む時点

工数・日程は判断材料にしない。

## 14. 評価 — 問題は 4 層に分かれる

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

## 15. probe の構成

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

## 16. 未確認 [C]

- selector の UI 部品（§11.3.2。値空間は確定、部品は未定）。
- selector を永続化するか（§11.3.3）。
- **7.1.4 → 7.1 の段の係数**（§10.6.5）。5.1 → stereo と 7.1 → 5.1 は出典が付いた。
- 異種 layout 比較がどの程度の頻度で起きるか（**運用上の評価。コードからは判定できない**）。

## 17. 窓・cadence・有効条件・reset [A]

計画 §6 の残り。**Nch 化で「変えてはいけない時間契約」を先に固定する。**

### 17.1 観測 cadence

| 経路 | cadence | 根拠 |
|---|---|---|
| `StereoMeter` の observation | **100 ms** | `OBSERVATIONS_PER_*` が 100 ms 単位（`stereo_meter.rs:13-15`） |
| Sharpness の presentation | **10 Hz = 100 ms** | `perceptual.rs:18-19`。aperture 4,800 frames @48 kHz |
| `MeasureEngine` の解析 | **10 ms**、公開は 100 ms | B-605 監査（解析 cadence と公開 cadence の分離） |
| Watch engine の rate | **常に 48 kHz** | `measure_thread.rs:245` `ENGINE_SR` |
| Record engine の rate | **入力 native** | `:252` `:262` |

**Nch 化で cadence を変えない。** 変えると PRE/POST 比較と履歴の同一性が崩れる。

### 17.2 窓長

| 指標 | 窓 | 根拠 |
|---|---|---|
| LUFS-M | 400 ms | ebur128 `Mode::M` |
| LUFS-S | 3 s | ebur128 `Mode::S`（`window = 3000`） |
| LUFS-I / LRA | session（gated） | `Mode::I` / `Mode::LRA` |
| True Peak（recent） | **400 ms、frames 基準**（壁時計ではない） | `engine.rs:49-52 tp_recent_window_frames` |
| True Peak（`StereoMeter` instant） | **400 ms** = 4 × 100 ms | `stereo_meter.rs:14` |
| VU | **300 ms** = 3 × 100 ms。全波平均 × π/2 で正弦の peak へ校正 | `:15` / `:364-375` |
| Correlation / Balance | **3 s 固定** = 30 × 100 ms | `:13` / `:343` |
| Spectrum | FFT window **4,096** | `spectrum.rs:16` |
| Attack context / detail | **100 ms / 30 ms** | `attack_perception.rs:9-10` |
| MONO | 1/3 oct **32 band**、3 周期未満は近似表示 | `mono_sum.rs:24, :34` |
| SPACE decay | bin **10 ms**、early split **80 ms**、early end **250 ms**、最大区間 **6 s** | `space_decay.rs:10-13` |

### 17.3 有効条件（floor）

| 値 | floor | 意味 |
|---|---|---|
| LUFS | **−100 LUFS** | `engine.rs:18` / `absolute_level.rs:12`。下回れば無信号扱い（`---`） |
| True Peak | **−100 dBTP** | `engine.rs:19` / `absolute_level.rs:13` |
| Reference gain の active | **−100 LUFS** | `reference_gain.rs:13` |
| MONO の測定可能下限 | **−120 dBFS** | `mono_sum.rs:28`。下回れば **undefined（NaN）** |
| MONO の比の floor | **−60 dB** | `mono_sum.rs:31`。**「消えた」であって「測っていない」ではない** |
| Attack level floor | **−120 dBFS** | `attack_perception.rs:11` |

**Nch 化で floor を変えない。** 特に MONO の「undefined と floor の区別」（INV-S31）は、
**サラウンドの downmix 指標でも同じ規律を引き継ぐ**（計画 §7.3）。

### 17.4 reset の契機

```rust
crates/kirin_measure/src/measure_thread.rs:426-431
engine.reset();
record_trace_engine.reset();
record_summary_engine.reset();
phase_d.reset();
    rs.reset();
```

Watch → Record 遷移で **engine 群と phase_d を一括 reset** する（`:208` のコメント）。

`StereoMeter::reset`（`:247`）は sample peak / hold / max TP / clip latched /
session clip events を初期化する。

**Nch 化で新たに必要になる reset は、レイアウト変更である**（計画 §5.3 の epoch）。
現行の reset 契機は rate と Record 遷移しか見ていない。

### 17.5 この節が Nch 化へ課す制約

1. **cadence と窓長は不変。** Nch でチャンネルが増えても 100 ms / 400 ms / 3 s を変えない。
2. **floor は不変。** チャンネル数で有効条件を変えない。
3. **reset にレイアウト変更を足す。** 現行は rate と Record 遷移のみ。
4. **Watch が常に 48 kHz、Record が native** という二重 rate 構造を維持する。
   §14.2 のとおり、Watch のコストが sample rate に依存しないのはこの構造による。
