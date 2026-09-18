# Hypha サラウンド化 — 取込容量の分解と allocation lifecycle

Status: 調査のみ。実装なし・定数変更なし・予算変更なし。方式の選択は Daisuke の承認事項。

Date: 2026-09-18 / rev.2

Branch: `claude/eager-pascal-edacak` / 確認 commit `d701e55`

計画は `@docs/hypha_surround_implementation_plan_20260918.md`（B-925）、
棚卸しは `@docs/hypha_surround_p0_inventory_20260918.md`（B-928）。

## 0. 設問の立て方

**「メモリ予算をいくらにするか」を先に決めない。**
先に決めるのは、**サラウンド時にも現行の保証をすべて同時に維持する必要があるか**である。

問題は「サラウンドはメモリを食う」ではない。
**「262,144-frame 保証 × チャンネル数 × 最大 24 インスタンス」という現在の保証モデル**にある。

したがって次の 3 層は必ず分離する。

```
ABI capacity = 16
  ≠  対応レイアウト = 16ch
  ≠  24 インスタンスすべてに 16ch 相当の音声 buffer を常時保証
```

**16ch という ABI のスロット容量を、audio retention の設計思想へ持ち込まない。**

### 0.1 削ってよいものと、削ってはいけないもの

**音響測定の精度を削ってメモリを節約する案は採らない。**

§2 で見るとおり、4 領域のいずれも「測定の精度」を持っていない。
すべて **delivery（届ける保証）** である。削る候補になるのは精度ではなく、
**測定機会（drop の有無）** と **RT safety** と **製品保証の範囲**である。

## 1. メモリを 4 つに分類する

容量を議論する前に、各バイトが何のためにあるかで分ける。

| 分類 | 意味 |
|---|---|
| **必須 working set** | 現在処理するため絶対必要 |
| **RT headroom** | Audio Thread で allocation しないための余裕 |
| **burst absorption** | Measure Thread が一時的に遅れたときの吸収 |
| **product guarantee** | 最大 block、pre-roll、12 pair 等を保証するための予約 |

**ここまで分ければ、どこを削ると精度が落ちるのか、測定機会だけが減るのか、
RT safety を壊すのか、製品保証だけが変わるのかが分かる。**

## 2. 現行 4 領域を分類する [A]

根拠はコード自身のコメントである（`ingest_contract.rs:1-27`、`PluginProcessor.cpp:105-108`）。

| 領域 | コードが書いている目的 | 分類 |
|---|---|---|
| interleave scratch | 「processBlock が allocate しないように事前確保（RT-safe）」。`max(declared block, 262144)` | **ホスト宣言分 = 必須 working set + RT headroom。<br>262,144 への上乗せ = product guarantee** |
| Watch ring | 「One Watch lane only has to retain one complete host callback」 | **burst absorption（1 callback）。<br>その 1 callback が 262,144 であること = product guarantee** |
| Record burst ×3 | 「Maximum number of complete Record callbacks that may remain unconsumed」 | **burst absorption（3 callback）。<br>「3 つまで」の公表契約 = product guarantee** |
| raw pre-roll 1 秒 | 「bounded Keep notification/ACK handoff をカバーする。Watch metrics から Record データを導出しないため」 | **product guarantee**（用途が明示されている） |

module doc（`ingest_contract.rs:1-7`）が契約を明言している。

> every supported instance accepts three complete maximum-sized callbacks
> before the Measure Thread has to reclaim a slot.

**精度に関わる領域は 1 つも無い。** これが §0.1 の根拠である。

## 3. 現行 384 MiB の内訳 [A]

`ingest_contract.rs:68 max_generation_ingest_bytes()`。`MAX_CAPTURE_PAIRS = 12` → 24 インスタンス。
すべて最大条件（block 262,144 / rate 192 kHz）。

| 領域 | stereo | 5.1 | 7.1.4 | 割合 |
|---|---:|---:|---:|---:|
| Watch ring | 48.0 MiB | 144.0 | 288.0 | 14.9% |
| Record burst ×3 | 144.0 MiB | 432.0 | 864.0 | 44.6% |
| interleave scratch | 48.0 MiB | 144.0 | 288.0 | 14.9% |
| raw pre-roll | 83.2 MiB | 249.5 | 498.9 | 25.7% |
| **合計** | **323.2 MiB** | **969.5** | **1,938.9** | |

C=2 の 338,853,888 bytes は既存テスト `ingest_contract.rs:115` の期待値と一致する。

`max_generation_known_pipeline_bytes()` は 349,372,416（`:116`）。
**予算 384 MiB（402,653,184）の 86.8% を消費し、残りは約 50.8 MiB。**

### 3.1 1,938.9 MiB を「7.1.4 対応時の必要 RAM」として扱わない

> **1,938.9 MiB は 7.1.4 の最終 worst-case memory ceiling ではない。**
> これは現行 4 保持領域のみを 12ch へ線形拡張した contract-model 上の中間値であり、
> **Phase D、MeasureEngine、resampler、worker workspace、history、UI / FFI 等の
> Nch 増分を含まない。**

Phase D は 1 チャンネルにつき `PhaseDStream` を 1 本作り、さらにチャンネル別 buffer を持つ
（`phase_d/channels.rs:30-37`）。**Nch 化すると DSP state 自体も増える。**
音声 retention だけの話ではない。

## 4. allocation lifecycle [A]

**「常駐」という語は使わない。** `Vec::with_capacity` や ring の確保は**論理的な確保容量**であり、
OS が観測する RSS / physical residency と同じではない。
「24 インスタンス分がプロセス起動から常に物理 RAM に resident」という意味にしない。

正しくは **engine lifecycle 上の事前確保 / Record 時遅延確保** である。

| 領域 | 確保箇所 | 確保される期間 |
|---|---|---|
| Watch ring | `kirin_hypha_ffi/src/lib.rs:990-991` `rtrb::RingBuffer::new` | **engine 生成〜破棄** |
| interleave scratch | `PluginProcessor.cpp:109-110` `interleaveScratch.assign` | **`prepareToPlay` 〜次の prepare** |
| raw pre-roll | `measure_thread.rs:320-323` → `raw_pre_roll.rs:31` `VecDeque::with_capacity` | **measure thread 起動〜終了** |
| **Record burst** | **`record_ingress.rs:466` `install_fresh_lane`** | **Record lane 設置〜解放（`:292` / `:318`）** |

`RecordIngress::new`（`:56-66`）は ring を確保しない。1 要素の bootstrap ring と
`allocated: false` を持つだけである。

| | engine lifecycle 事前確保 | Record 時遅延確保 | 合計 |
|---|---:|---:|---:|
| stereo | 179.2 MiB | 144.0 MiB | 323.2 MiB |
| 5.1 | 537.5 MiB | 432.0 MiB | 969.5 MiB |
| 7.1.4 | 1,074.9 MiB | 864.0 MiB | 1,938.9 MiB |

**Record burst を仮に完全にゼロにしても、7.1.4 の事前確保分 1,074.9 MiB は
現行 384 MiB には戻らない。**

したがって調査の優先順位は「Record burst 削減 → その他」ではない。
**「事前確保が本当にすべて最大容量を必要とするのか」と「Record burst 3 の根拠」を並列で調べる。**

## 5. interleave scratch — 必要分と保険が分離できる可能性 [A]

```
PluginProcessor.cpp:109  const int maxFrames = jmax (jmax (0, samplesPerBlock), kOversizeHeadroomFrames);
PluginProcessor.cpp:32   constexpr int kOversizeHeadroomFrames = 262144;
```

**`prepareToPlay` はホストの `samplesPerBlock` を既に知っている。**
それでも 262,144 を下限に取る。したがって
**「実際に必要な容量」と「異常に大きい callback への保険」が明確に分離できる可能性がある。**

stereo・24 インスタンスでの内訳（式からの算出）:

| ホスト宣言 block | 必須 working set | 確保 | 保険が占める割合 |
|---:|---:|---:|---:|
| 128 | 0.023 MiB | 48.0 MiB | 99.95% |
| 512 | 0.094 MiB | 48.0 MiB | 99.80% |
| 1024 | 0.188 MiB | 48.0 MiB | 99.61% |
| 4096 | 0.750 MiB | 48.0 MiB | 98.44% |

**この領域は、ほぼ全部が product guarantee である。** 調査価値が高い。

超過時の経路も既にある。

```
PluginProcessor.cpp:428-429
if (numCh > 0 && needed > scratchCapacitySamples)
    kirin_hypha_note_oversized_drop (hyphaHandle, (uint64_t) needed);
```

`PluginProcessor.cpp:31` のコメントどおり、**oversized drop を記録したうえで音声は通過する。**
計測機会は落ちるが無言ではなく、R-12 の A 通過は維持される。

## 6. 検討の順序（決定ではない）

**利用条件の縮小は Daisuke の承認なしに行わない。**

### (a) 現行保証を維持したまま保持構造を変更する

最初に調べる価値が高い。現行は
**「最大 block × channels × instances」をかなり保守的に事前確保する**設計で、
サラウンド化で初めてその設計コストが表面化した。

**調査対象（解決策ではない）**:

1. **262,144 frames という現在の callback 保証を、サラウンドでも維持すべき製品保証なのか検証する。**
   これは最適化ではなく**保証の検証**である。§6.1 参照。
2. **interleave scratch の「必要分」と「保険」を分離できるか**（§5）。
   ホスト宣言 block は既知であり、保険部分は 98〜99.9% を占める。
3. **Record burst 3 blocks の根拠を確認する。** 何を保証するための 3 なのか。
   チャンネル数に比例させる必要があるのか。
4. **事前確保 3 領域を遅延確保へ移せるか。** Record ring は既に遅延確保なので、
   同じ仕組みが他へ適用できるかを見る。
5. **24 インスタンスが同時に最大条件を満たす前提の妥当性。**
   `install_fresh_lane` は per-instance なので、実際の同時確保数を追う。
6. **384 MiB という数字そのものが、現在どのように導出されたか。**
   昔の安全マージンなのか、12 pair 実測から決めた hard ceiling なのか、
   特定環境の実 RSS から逆算したのか。**由来によって 384 MiB を維持する意味が変わる。**
7. **`remaining reserve`（約 50.8 MiB）の実体。**
   `max_generation_known_pipeline_bytes()` のコメントは
   「DSP engine internals and control state use the remaining reserve」としている。
   §3.1 のとおり、その DSP state 自体が Nch で増える。

### 6.1 262,144 の縮小は「解決策」ではなく「保証変更の候補」である

bytes 一定になるようチャンネル数で割る案は、メモリは一定になるが、
**対応可能な callback 長がレイアウトによって変わるという新しい製品契約を作る。**

| | frames | @48 kHz | @192 kHz |
|---|---:|---:|---:|
| stereo | 262,144 | 5.46 秒 | 1.37 秒 |
| 5.1 | 87,381 | 1.82 秒 | 0.46 秒 |
| 7.1.4 | 43,690 | 0.91 秒 | 0.23 秒 |

技術的には成立しても、
**「Hypha はチャンネル数が増えるほど許容 block が短くなる」という仕様の導入**になる。
最適化ではなく保証変更なので、**P-0 では候補の一つに留める。**

### (b) RSS 予算を変更する

最も単純だが、**現在の保持構造をそのまま N 倍する設計を固定する危険**がある。
5.1 のために 384 MiB → 約 1 GiB としても、7.1.4 で再び突き当たる。

**5.1 だけを見て決めない。** 2ch→6ch で約 3 倍、2ch→12ch で約 6 倍。
ここで「予算を増やせば解決」とすると、P-6 で同じ設計問題にもう一度ぶつかる。

### (c) 製品上の同時利用条件を変更する

`MAX_CAPTURE_PAIRS` を減らす等。**まだ早い。**
これをすると **Hypha の機能仕様がチャンネル構成によって変わる。**
(a) を尽くしてから、明示的な承認事項として扱う。

## 7. 計画へ反映済みの文言（B-925 §5.5.1）

> 取込容量は ABI 容量とは独立した P-0 の設計課題とする。
> 現行の 384 MiB 契約を単純に Nch へ線形拡張することは、まだ採用しない。
> まず現行容量を構成する各保持領域と allocation lifecycle を実測・分類し、
> 5.1 および 7.1.4 で現行の同時 pair 数・burst・pre-roll 保証を維持した場合の上限を算出する。
> その結果を基に、(a) 現行保証を維持したまま保持構造を変更、(b) RSS 予算を変更、
> (c) 明示的に同時利用条件を変更、の順で検討する。
> **利用条件の縮小は Daisuke の承認なしに行わない。**

## 8. 未確認 [C]

- **実 RSS 測定。** 本書はすべて式からの算出である。論理的確保容量 ≠ physical residency。
- **384 MiB の由来**（§6 (a) 6）。
- **`remaining reserve` 約 50.8 MiB の実体**（§6 (a) 7）。
- **Record burst = 3 の根拠となった元の事象。**
- 同時に Record 中となるインスタンス数の実際の分布。
- spool と f32→f64 workspace の内訳（約 10 MiB の詳細）。
- `record_alignment_silence` / `chunk_f64` / `resampled_buf`（`measure_thread.rs:314-328`）など、
  measure thread 内の他の `with_capacity` の合計。**§3 の 4 領域に含まれていない。**
- **Phase D / MeasureEngine / resampler / worker workspace / history / UI・FFI の Nch 増分**（§3.1）。
