# Hypha サラウンド化 — 取込容量の分解と allocation lifecycle

Status: 調査のみ。実装なし・定数変更なし・予算変更なし。方式の選択は Daisuke の承認事項。

Date: 2026-09-18

Branch: `claude/eager-pascal-edacak` / 確認 commit `d701e55`

計画は `@docs/hypha_surround_implementation_plan_20260918.md`（B-925）、
棚卸しは `@docs/hypha_surround_p0_inventory_20260918.md`（B-928）。

## 0. 設問の立て方

**「メモリ予算をいくらにするか」を先に決めない。**
先に決めるのは、**サラウンド時にも現行の保証をすべて同時に維持する必要があるか**である。

取込容量は ABI 容量とは独立した P-0 の設計課題として扱う。
現行 384 MiB を単純に Nch へ線形拡張することは、まだ採用しない。
**16ch という ABI のスロット容量を、audio retention の設計思想へ持ち込まない。**

## 1. 現行 384 MiB が何を保証しているか [A]

`ingest_contract.rs:68 max_generation_ingest_bytes()` は 4 つの領域の和である。
`MAX_CAPTURE_PAIRS = 12` → 24 インスタンス。すべて最大条件（block 262,144 / rate 192 kHz）。

| 領域 | 保証している内容 | 式（1 インスタンス / C ch） |
|---|---|---|
| Watch ring | Watch 経路が最大 callback 1 つ分を落とさない | `262144 * C` |
| **Record burst ×3** | **Record が未消費 callback を 3 つ保持する** | `262144 * 3 * C` |
| interleave scratch | JUCE の planar→interleaved 変換が RT で再確保しない | `262144 * C` |
| raw pre-roll 1 秒 | 通知保持窓 1 秒 + 最大 callback | `(262144 + 192000) * C` |

寄与（式から算出。実 RSS 測定ではない）:

| 領域 | stereo | 5.1 | 7.1.4 | 割合 |
|---|---:|---:|---:|---:|
| Watch ring | 48.0 MiB | 144.0 | 288.0 | 14.9% |
| **Record burst ×3** | **144.0 MiB** | **432.0** | **864.0** | **44.6%** |
| interleave scratch | 48.0 MiB | 144.0 | 288.0 | 14.9% |
| raw pre-roll | 83.2 MiB | 249.5 | 498.9 | 25.7% |
| **合計** | **323.2 MiB** | **969.5** | **1,938.9** | |

C=2 の 338,853,888 bytes は既存テスト `ingest_contract.rs:115` の期待値と一致する。

`max_generation_known_pipeline_bytes()` は 349,372,416（`:116`）で、
差の約 10 MiB は spool と f32→f64 変換 workspace。**予算 384 MiB の 86.8% を消費済み。**

**最大の寄与は Record burst の 44.6% である。** これが最初に問う対象になる。

## 2. allocation lifecycle — 常駐と Record 時のみ [A]

**予算は「全 24 インスタンスが同時に Record 中」の最悪ケース契約であり、常時の実確保ではない。**

| 領域 | 確保箇所 | タイミング |
|---|---|---|
| Watch ring | `kirin_hypha_ffi/src/lib.rs:990-991` `rtrb::RingBuffer::new` | **engine 生成時。常駐** |
| interleave scratch | `PluginProcessor.cpp:109-110` `interleaveScratch.assign` | **`prepareToPlay`。常駐** |
| raw pre-roll | `measure_thread.rs:320-323` → `raw_pre_roll.rs:31` `VecDeque::with_capacity` | **measure thread 起動時。常駐** |
| **Record burst** | **`record_ingress.rs:466` `install_fresh_lane` 内の `rtrb::RingBuffer::new`** | **Record lane 設置時のみ** |

`RecordIngress::new`（`record_ingress.rs:56-66`）は **ring を確保しない。**
1 要素の bootstrap ring を作り、`capacity` と `allocated: false` を保持するだけである。
実確保は `install_fresh_lane`（`:465-474`）で `allocated = true` になる時。
解放は `:292` と `:318`。

したがって:

| | 常駐 | Record 時のみ | 合計 | 常駐 / 384 MiB |
|---|---:|---:|---:|---:|
| stereo | 179.2 MiB | 144.0 MiB | 323.2 MiB | 0.47× |
| 5.1 | 537.5 MiB | 432.0 MiB | 969.5 MiB | **1.40×** |
| 7.1.4 | 1,074.9 MiB | 864.0 MiB | 1,938.9 MiB | **2.80×** |

**常駐 55.4% / Record 時のみ 44.6%。**

**5.1 では常駐分だけで既に現行予算の 1.40 倍**になる。
「Record 時だけ増える」では済まない。

## 3. 262,144 frames は要件ではなく事前確保の方針 [A]

```
PluginProcessor.cpp:32   constexpr int kOversizeHeadroomFrames = 262144;
PluginProcessor.cpp:109  const int maxFrames = jmax (jmax (0, samplesPerBlock), kOversizeHeadroomFrames);
```

**ホストが 512 frames を宣言しても 262,144 frames 分が確保される。** 下限である。

これを超える block が来たときの経路も既にある。

```
PluginProcessor.cpp:428-429
if (numCh > 0 && needed > scratchCapacitySamples)
    kirin_hypha_note_oversized_drop (hyphaHandle, (uint64_t) needed);
```

`PluginProcessor.cpp:31` のコメントどおり、**oversized drop を記録したうえで音声は通過する。**
計測を落とすが、無言ではない。R-12 の A 通過は維持される。

つまり 262,144 が買っている保証は **「262,144 frame の callback まで計測を落とさない」**であり、
正しさの要件ではない。192 kHz で 1.37 秒、48 kHz で 5.46 秒に相当する。

**この保証は明示的に取引できる。失敗モードは既に定義され、観測可能である。**

## 4. 検討の順序（決定ではない）

Daisuke の指示に従い、次の順で検討する。**利用条件の縮小は承認なしに行わない。**

### (a) 現行保証を維持したまま保持構造を変更する

最初に調べる価値が高い。現行は
**「最大 block × channels × instances」をかなり保守的に事前確保する**設計で、
サラウンド化で初めてその設計コストが表面化した。

調査候補（**実装提案ではなく、調べる対象**）:

1. **`kOversizeHeadroomFrames` の下限をチャンネル数で調整する。**
   bytes を一定に保てば 5.1 で 1/3、7.1.4 で 1/6 の frame 数になる。
   落ちるのは「非常に大きな callback での計測」であり、既存の oversized drop 経路が扱う。
   192 kHz で 1.37 秒 → 5.1 なら 0.46 秒。**この取引が許容できるかは Daisuke の判断。**
2. **Record burst 3 blocks（44.6%）の根拠を確認する。**
   何を保証するための 3 なのか、チャンネル数に比例させる必要があるのか。
3. **常駐 3 領域を Record 時確保へ移せるか。**
   Record ring は既に遅延確保なので、同じ仕組みが他へ適用できるかを見る。
4. **24 インスタンスが同時に最大条件を満たす前提の妥当性。**
   `install_fresh_lane` は per-instance なので、実際の同時確保数を追う。

### (b) RSS 予算を変更する

最も単純だが、**現在の保持構造をそのまま N 倍する設計を固定する危険**がある。
5.1 のために 384 MiB → 約 1 GiB としても、7.1.4 で 2 GiB 弱に再び突き当たる。

**5.1 だけを見て決めない。** 2ch→6ch で約 3 倍、2ch→12ch で約 6 倍。
ここで「予算を増やせば解決」とすると、P-6 で同じ設計問題にもう一度ぶつかる。

### (c) 製品上の同時利用条件を変更する

`MAX_CAPTURE_PAIRS` を減らす等。**まだ早い。**
これをすると **Hypha の機能仕様がチャンネル構成によって変わる。**
(a) を尽くしてから、明示的な承認事項として扱う。

## 5. 計画へ反映する文言

B-925 §5.5 に次を加える。

> 取込容量は ABI 容量とは独立した P-0 の設計課題とする。
> 現行の 384 MiB 契約を単純に Nch へ線形拡張することは、まだ採用しない。
> まず現行容量を構成する各保持領域と allocation lifecycle を実測・分類し、
> 5.1 および 7.1.4 で現行の同時 pair 数・burst・pre-roll 保証を維持した場合の上限を算出する。
> その結果を基に、(a) 現行保証を維持したまま保持構造を変更、(b) RSS 予算を変更、
> (c) 明示的に同時利用条件を変更、の順で検討する。
> **利用条件の縮小は Daisuke の承認なしに行わない。**

## 6. 未確認 [C]

- **実 RSS 測定。** 本書はすべて式からの算出である。
- 同時に Record 中となるインスタンス数の実際の分布。
- spool と f32→f64 workspace の内訳（約 10 MiB の詳細）。
- `record_alignment_silence` `chunk_f64` `resampled_buf`（`measure_thread.rs:314-328`）など、
  measure thread 内の他の `with_capacity` の合計。**§1 の 4 領域に含まれていない。**
- Record burst 3 blocks の根拠となった元の事象。
- DSP engine 内部と control state の実サイズ。予算の「残り reserve」が何を賄っているか。
