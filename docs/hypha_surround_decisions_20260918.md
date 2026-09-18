# Hypha サラウンド化 — 確定判断の記録

Status: **Daisuke 承認済み（2026-09-18）。** 以後、本書を根拠にする。

Date: 2026-09-18

Branch: `claude/eager-pascal-edacak` / 承認時点 commit `45f07af`

P-0 の調査資料:

- 事実: `@docs/hypha_surround_channel_map_findings_20260918.md`（B-921 / B-922）
- 計画: `@docs/hypha_surround_implementation_plan_20260918.md`（B-925）
- 棚卸し: `@docs/hypha_surround_p0_inventory_20260918.md`（B-928 ほか）
- 容量: `@docs/hypha_surround_ingest_capacity_20260918.md`（B-929 ほか）
- 契約表: `@docs/hypha_surround_metric_contracts_20260918.md`（B-937 ほか）
- 集約候補: `@docs/hypha_surround_aggregation_candidates_20260918.md`（B-949 ほか）

## 1. 確定した判断

### D-1 対象レイアウト

**5.1 を先に通し、7.1.4 は後続。** 9.1.6 は対象外。

根拠: 5.1 は公式 EBU test set で検証できる唯一のサラウンド（計画 §1）。

### D-2 参照版

**版を一本化せず、測定に使った版を記録・表示する。**

根拠: Apple の Atmos 納品は BS.1770-4 参照、ITU 現行は -5 で一致しない（findings §6.1）。
Annex 1 / 2 に差分は無いので mono/stereo の数値は変わらない。

### D-3 ABI 容量

**固定最大 `KIRIN_MAX_CHANNELS = 16` + `channel_count`。**

ただし **「全部 16」ではない**（契約表 §4.1）:

- **Nch 一般化するものだけ**が `[16]` を必要とする。
- **stereo-only へ移すもの**は ABI を広げない。
- **Reference capture index の 64 byte 保存契約は独立**に扱う（棚卸し §3.1）。

**16 は容量であって、9.1.6 対応の宣言ではない。**

### D-4 未知レイアウト

**受理しない。推測で map を当てない。**

チャンネル数では layout を特定できない（findings §3.1）。
**pair 数でも特定できない**（集約候補 §4.2.3）。

### D-5 selector

**layout 上の役割で指す。index では指さない。**

根拠: `set_channel_mode` は生の `u8` を比較するので、
**index だと layout 変更で generation が上がらず、別チャンネルの履歴が無言で連結する**（契約表 §11.3.1）。

有効な値の集合は交渉済み layout の `channel_positions[]` + 適用できる導出 view。

**Spectrum / Sharpness / Attack の 3 指標に同じ selector が効く**（契約表 §11.6、集約候補 §2.2）。

### D-6 集約が未定義な 4 指標

| 指標 | 初期版 |
|---|---|
| **Attack** | **選択 view のみ。** 内部契約名は **Attack of selected view**。「作品全体の Attack」とは解釈しない |
| **Correlation** | **左右対 overview + 選択ペア詳細。** 現行式を変えない |
| **Balance** | **左右対ごと。** Correlation と同じ `PairRole` を参照する |
| **MONO** | **サラウンドでは適用外を明示。** |

**共通原則: 集約を発明しない。**

### D-7 Downmix Observation

**MONO の後継ではない。完全に独立した新規測定として設計する。**

- **初期 5.1 で新しい "surround MONO" を作らない。**
- 現行 MONO の分母 `Pm + Ps` は M/S 分解固有であり、一般の Nch → stereo downmix では作れない
  （集約候補 §5.1）。
- 設計するときは **source-band energy / downmix 後の band energy / 差分 を別々の事実として保持**してから、
  何を表示するか決める（§5.2）。**最初から 1 つの survival % を作らない。**

### D-8 Reference の異種 layout 比較

**既定は同一 layout のみ。異種 layout は利用者が明示的に選択した比較 view でのみ許可する。**

**無言で downmix して比較しない。**

### D-9 downmix 係数の出典

**「規格名」ではなく「どの renderer のどの downmix」で固定する。**

- 5.1 → stereo と 7.1 → 5.1 は **Dolby Atmos Renderer の Lo/Ro** として出典が付いた（契約表 §10.6）。
- **ITU-R BS.775-4 は選択肢を与えており、単一係数の出典にならない。**
- **−3 dB は式か数値で書く**（`10^(-3/20)` = 0.707946 と `1/√2` = 0.707107 は別物）。

### D-10 左右対の導出

**mirror = 同じ平面で方位角の符号反転。正中面（0° / 180°）と LFE には mirror が無い。**

**手で対の表を書かない。** 交渉済み layout に存在する role から機械的に導出する（集約候補 §4.2.1）。

### D-11 時間契約

**Nch 化で変えない**（契約表 §17.5）:

1. cadence と窓長（100 ms / 400 ms / 3 s）
2. floor（−100 LUFS / −100 dBTP / −120 dBFS）
3. Watch = 48 kHz / Record = native の二重 rate 構造

**足すのは reset 契機としての layout 変更だけ。**

### D-12 非互換レイアウト変更

**測定を保留し、旧 map へ新音声を入れない。**

- **保留は新規実装**（現行の「defer」は実体が「skip」である / 棚卸し §16.4）。
- **区切りは既存機構の拡張で足りる**（`generation` と view 境界 / §16.1、§16.2）。
- **再開権限は Stop 権限と分離する。** B-334 の判断（Stop 権限を移さない）は維持する。

### D-13 優先順位

**C（無言 clamp / drop）と G（誤った値）を R（構築拒否）より先に消す。**

> **「機能が少ない」ことより、「値が出ているのに意味が違う」ことを重大な失敗として扱う。**

### D-14 取込容量

**現行 384 MiB を単純に Nch へ線形拡張しない。**

検討順は **(a) 保持構造の変更 → (b) 予算の変更 → (c) 同時利用条件の変更**。
**利用条件の縮小は Daisuke の承認なしに行わない。**

384 MiB は **scope が未解決のまま文書化された legacy budget** として扱う（容量 §8）。

## 2. 承認に含まれないもの

**次は本承認の対象外であり、別途の判断を要する。**

| # | 項目 | 理由 |
|---|---|---|
| 1 | **初期 5.1 で Nch 化する指標の確定集合** | 契約表 §12 に候補はあるが、**確定していない** |
| 2 | **7.1.4 へ進む時点** | D-1 で順序は決めたが、**時点は決めていない** |
| 3 | 7.1.4 → 7.1 の downmix 係数 / Stereo Direct の定義 | **外部一次資料が要る** |
| 4 | Downmix Observation の測定量定義 | 係数と並行（D-7） |
| 5 | selector の UI 部品 / 永続化 | 設計判断（契約表 §11.3.2、§11.3.3） |
| 6 | 取込容量の (a)(b)(c) のどれを採るか | D-14 は**順序だけ**を決めた |

## 3. 実装が従う不変条件

以下は本承認により**実装の前提**となる。逸脱には再承認が要る。

1. **無言変換を残さない。** 出荷経路の 3 件（棚卸し §15.3）を拒否 / 適用外 / 明示変換のいずれかにする。
2. **未知 layout を受理しない**（D-4）。
3. **集約を発明しない**（D-6）。
4. **時間契約を変えない**（D-11）。
5. **入力 A の出力音声を変えない**（R-12）。比較用 downmix は測定のための内部計算である。
6. **値が出ているのに意味が違う状態を作らない**（D-13）。
