# Hypha サラウンド化 P-0 — 影響範囲の棚卸し（第2巡 / 第1巡の訂正を含む）

Status: 調査のみ。実装なし・仕様変更なし・対応レイアウト変更なし。工数・日程は判断材料にしない（R-27）。

Date: 2026-09-18 / rev.2

Branch: `claude/eager-pascal-edacak` / 確認 commit `d701e55`

計画は `@docs/hypha_surround_implementation_plan_20260918.md`（B-925）。
事実は `@docs/hypha_surround_channel_map_findings_20260918.md`（B-921 / B-922）。

第1巡は厳密レビューで前提が複数崩れたため書き直した。訂正は §1 に集約する。
**P-0 完了とは宣言しない。** 残件は §10 の第2巡へ送る。

## 0. 出典の区分

- **[A]** 本セッションで実 read / 実測して確認。行番号を付す。
- **[B]** Daisuke 側のレビューで指摘され、本セッションが [A] として裏を取ったもの。
- **[C]** 未確認。推測で埋めない。

**実施していないこと**: Hypha のフルビルド、DAW 実測、実 RSS 測定、出荷経路の変異試験、
リポジトリ全通読。以下の「確認済み」は資料とコードの確認である。

## 1. 第1巡の訂正

| # | 優先度 | 第1巡の記述 | 訂正 |
|---|---|---|---|
| 1 | **最優先** | チャンネル数の制限は「拒否ガード」を全走査すれば掴める | **拒否しない無言変換がある**（§2）。走査設計の欠陥であり、件数の問題ではない |
| 2 | **最優先** | ABI の配列容量が主な容量問題 | **取込容量と RSS 予算も 2ch 前提**（§3）。5.1 で予算の 2.52 倍、7.1.4 で 5.05 倍 |
| 3 | 高 | `MeasureEngine` の生成は 4 か所 | 4 か所に加え、`reference_gain.rs` が **`EbuR128` を 2 か所で直接生成**（§4） |
| 4 | 高 | Reference の保存形式変更点は `rms[2]` / `peak[2]` | **`bands[8]` が 2ch × 4 帯域**（§5）。`[2]` 検索では原理的に拾えない次元積 |
| 5 | 高 | 構造体を広げれば digest 同一性にも影響する | **影響しない。** digest の入力は構造体ではない（§6） |
| 6 | 高 | Phase D は現状が既に規律違反 | **言い過ぎ。** 製品独自の集約として定義・文書化済み（§7）。未確認なのはサラウンドでの妥当性 |
| 7 | 高 | Record に metadata が無いのでゼロから設計 | **既存構造がある**（§8）。`capture_epoch` / `TraceClock` / `TraceDiagnostics`。無いのは layout 情報 |
| 8 | 高 | mono / stereo しか通らないので Record に実害なし | **安全を証明していない。** mono と stereo も既に別入力である |
| 9 | 訂正 | Attack のガードは 8 か所 | **9 か所。** 自分の表を足すと 9（§9）。総計 23 は一致 |
| 10 | 訂正 | interleave の 2 決め打ちは 2 か所 | **本番は 1 か所。** `reference_gain.rs:308` は `#[cfg(test)]` 内（§9） |

## 2. 最優先 — 明示 map より手前に、無言の 2ch 化がある [A]

```rust
crates/kirin_measure/src/measure_thread.rs:238-240
let n_channels = match n_channels {
    1 | 2 => n_channels,
    _ => N_CHANNELS,        // crates/kirin_measure/src/lib.rs:390  = 2
};
```

**拒否ではなく置換である。** この値で Watch 用・Record TRACE 用・Record summary 用の
`MeasureEngine`、resampler、Phase D を生成している。

意味するところ:

- 明示 channel map を正しく作っても、**その手前で入力が 2ch に変えられれば守れない。**
- `1..=2` / `matches!(_, 1 | 2)` を全走査しても、このパターンは**原理的に出てこない。**
  第1巡の走査設計は「拒否」しか見ていなかった。
- 現在の公開経路は mono / stereo 限定（`PluginProcessor.cpp:208`）なので、
  **今の製品で誤測定が起きているという意味ではない。** 制限を外したときに効く。

**同じ置換が FFI の入口にもある**（第2巡で発見）:

```rust
crates/kirin_hypha_ffi/src/lib.rs:186-191
fn supported_channel_count(num_channels: u32) -> usize {
    match num_channels { 1 | 2 => num_channels as usize, _ => N_CHANNELS }
}
```

`kirin_hypha_create` → `KirinHyphaEngine::new`（`lib.rs:989`）が最初に通す関数である。
**measure thread より外側で、すべてに先立って 2 へ変えられる。**
`hypha_pre/src/lib.rs:187` も `record_ring_capacity_samples(N_CHANNELS)` を直接使う。

第2巡では、拒否ガードとは別に **「別の形を黙って作る処理」を独立した監査対象**にする。
候補パターン: `match` の `_ =>`、`if/else` の既定値、`.min(2)`、`.clamp(1, 2)`、
`unwrap_or(2)`、`.take(2)`、先頭 2 要素のスライス、暗黙の複製・間引き・downmix。
**これらが全部存在するという報告ではない。** 一致ごとに意味を読む。

## 3. 最優先 — 取込容量と RSS 予算が 2ch で閉じている [A]

`crates/kirin_measure/src/ingest_contract.rs`:

| 定数 | 値 | 行 |
|---|---|---|
| `MAX_AUDIO_BLOCK_FRAMES` | 262,144 | 12 |
| `RECORD_UNCONSUMED_BURST_BLOCKS` | 3 | 15 |
| **`MAX_AUDIO_CHANNELS`** | **2** | 18 |
| `MAX_SUPPORTED_SAMPLE_RATE` | 192,000 | 22 |
| `PRE_ROLL_NOTIFICATION_WINDOW_SECONDS` | 1 | 27 |
| `CAPTURE_GENERATION_RSS_BUDGET_BYTES` | 384 MiB | 98 |

`MAX_CAPTURE_PAIRS = 12`（`capture_contract.rs:9`）→ 24 インスタンス。

`max_generation_ingest_bytes()`（:68）は **`MAX_AUDIO_CHANNELS` を直接使う**。
チャンネル数だけを C に置き換えて再計算した（R-10。式は :30–:75 から取った）:

```
per_instance_samples(C) = 262144*C + 262144*3*C + 262144*C + (262144 + 192000)*C
                        = C * 1,764,864
bytes = 24 * per_instance_samples(C) * 4
```

| ch | 取込 bytes | MiB | 384 MiB 予算比 |
|---:|---:|---:|---:|
| 2 (stereo) | 338,853,888 | 323.2 | 0.84× |
| 6 (5.1) | 1,016,561,664 | 969.5 | **2.52×** |
| 12 (7.1.4) | 2,033,123,328 | 1,938.9 | **5.05×** |
| 16 (9.1.6) | 2,710,831,104 | 2,585.2 | **6.73×** |

C=2 の計算値 338,853,888 は、既存テスト `ingest_contract.rs:115` の期待値と**完全一致する**。
式の読み取りが正しいことの裏付けになる（ただしそのテストを実行したわけではない）。

さらに `max_generation_known_pipeline_bytes()` は現状 349,372,416 bytes
（`ingest_contract.rs:116`）で、**384 MiB 予算の 86.8%、残り 51 MiB しかない。**

**含意**: `MAX_AUDIO_CHANNELS` を 16 にすると、既存の予算テストが即座に落ちる。
2 のまま残すと予算式が実入力を表さなくなる。**どちらも成立しない。**

これは ABI のスロット容量とは別の契約である。計画 §5.5 の「容量 16」は
**ABI の話であって、取込容量・世代予算の承認ではない。**
予算を上げる / 保持方式を変える / 受理条件を制限する、のどれを採るかは Daisuke の判断であり、
384 MiB・12 ペア・3 callback 保証を本セッションで勝手に変えない。

## 4. 測定器の生成経路は 4 + 2 [A]

| 用途 | 生成元 | 行 |
|---|---|---|
| `MeterSession` | `MeasureEngine::new` | `meter_session.rs:104` |
| Watch（内部 48 kHz） | `MeasureEngine::new` | `measure_thread.rs:245` |
| Record TRACE（native rate） | `MeasureEngine::new` | `measure_thread.rs:252` |
| Record summary（native rate） | `MeasureEngine::new` | `measure_thread.rs:262` |
| Reference gain（True Peak） | **`EbuR128::new` を直接** | `reference_gain.rs:52` |
| Reference gain（Mode::M） | **`EbuR128::new` を直接** | `reference_gain.rs:185` |

**`MeasureEngine` だけを直しても Reference の 2 経路は守れない。**
第2巡では `EbuR128::new`、ラッパー、再生成、reset、worker 再起動、
ファイル解析・Reference 解析まで台帳に載せる。

これは 6 つの**生成箇所**の確認であり、プロセス内の同時生存数の確定ではない [C]。

## 5. Reference には `[2]` では拾えない次元積がある [A]

`reference_capture_index.rs`:

```rust
:90   self.bands[c][k] += value * value;          // 内部は [[f64; 4]; 2]
:121  result.bands[c * 4 + k] = ...               // 保存は bands[8] へ平坦化
:166  let (a, b) = (a.bands[c * 4 + k], b.bands[c * 4 + k]);   // 比較も同じ index
```

**`bands[8]` は 2ch × 4 帯域である。** `rms` / `peak` だけを 16 要素にしても
多チャンネルの保存形式にはならない。3ch 目の帯域は index 8 から始まり、
8 要素配列の範囲外になる。現在は §2 のガードが到達を防いでいる。

第2巡では、長さではなく**次元の意味**で追跡する:
`[8]` `[32]` 等の固定長、channel × band、channel × history、pair × bin、係数行列、ビットマスク。

## 6. digest の入力は構造体ではない [A]

```rust
reference_capture_index.rs:39-41, :77, :105
hash.update(b"Hypha Capture Unit 1\0");
hash.update(rate.to_le_bytes());
hash.update((channels as u32).to_le_bytes());
...  hash.update(&bytes[..part.len() * 4]);   // 正規化した PCM
hash.update(self.frames.to_le_bytes());
```

domain 文字列 + rate + ch 数 + 正規化 PCM + frame 数である。
**構造体のメモリ全体をハッシュしていない。** よって `rms`/`peak`/`bands` を広げても
digest は自動的には変わらない。第1巡の記述は誤り。

逆に、**同じ rate・ch 数・frame 数・PCM でも異なるスピーカー配置があり得る**ので、
現在の digest 一致だけでレイアウト同一とは判断できない。
PCM 同一性 / 測定条件同一性 / 比較可能性 / 保存 schema を分ける。

## 7. Phase D は定義済み。未確認なのはサラウンドでの妥当性 [A]

```
phase_d/channels.rs:3-5
Each input channel is measured independently. Combining audio samples before
the nonlinear loudness pipeline would make anti-correlated stereo cancel to
silence even though both channels contain measurable audio.

phase_d/channels.rs:13
Kirin's channel aggregation is the arithmetic mean of independently measured ...
```

**算術平均は事故ではなく、意図された製品独自の集約としてコメントに明記されている。**
mono と dual-mono の尺度を揃え、極性反転で打ち消さないことが目的である。

また adapter は 2 系統ある。第1巡は片方しか読んでいなかった。

- `PhaseDChannelStream`（:17）— Record 経路。`average_results`（:134）で畳む。
- `PhaseDSharpnessChannelStream`（:24）— 表示 / Perceptual Delta 経路。

訂正後の記述:

> Phase D のチャンネル集約は、製品独自の算術平均として実装・文書化されている。
> ただし、これを LFE を含む多チャンネル配置全体の知覚量として扱う根拠、適用対象、
> 校正条件、表示・保存上の説明は未確認である [C]。
> 既存 mono / stereo の定義は維持し、Nch への適用契約を別途確定する。

「各 ch の Sharpness を平均する」と「平均した specific loudness から Sharpness を求める」が
同じとは仮定しない。

## 8. Record には既存構造がある。無いのは layout 情報 [A]

| 既存構造 | 行 |
|---|---|
| `TraceDiagnostics` | `plugin_data.rs:216` |
| `TraceClock` | `plugin_data.rs:229` |
| `TraceClockObservation` | `plugin_data.rs:253` |
| `TraceClockObservation.capture_epoch: Option<u64>` | `plugin_data.rs:260` |

`MeterSession` にも `generation` がある。

`plugin_data.rs` に `channels` の出現が 0 であることは事実だが、
そこから「Record の metadata も epoch も診断も無い」は導けない。第1巡の記述は誤り。

**必要なのは、既存の capture epoch / clock / diagnostics と、
新しい layout・map・測定方式の識別を、どう関係付けるかの設計である。**
既存識別子をすべて新しい `measurement_epoch` へ置換するのではない。

「mono / stereo だけなので実害なし」も撤回する。mono と stereo は既に別入力であり、
比較や履歴の再現で識別不要とは証明されていない。

## 9. 件数の訂正 [A]

- **Attack のガードは 9 か所**（`attack_exchange_codec` 1、`attack_pair` 1、
  `attack_perception` 2、`attack_runtime` 2、`attack_runtime_state` 3）。
  第1巡の本文は 8 と書いたが、同じ文書の表を足すと 9 になる。**自分の表と本文が矛盾していた。**
- Reference のガードは 8 か所。総計 23 か所 / 15 ファイルは変わらない。
- **本番の interleave 2 決め打ちは `mono_sum.rs:141` の 1 か所のみ。**
  `reference_gain.rs:308` は `#[cfg(test)] mod tests`（:234 開始）の中にある。
  `examples/` と `_tests.rs` の除外だけでは、通常ファイル内の `#[cfg(test)]` を除けない。

**分類は削除ではなく、production / test / example / 非出荷経路**として行う。
テスト側のステレオ前提は回帰 fixture として重要である。

## 10. 第2巡の調査（P-0 内の作業順）

**全項目が調査・設計の確定であり、サラウンド受理を有効にする実装ではない。**

### 調査1 — 無言変換と出荷到達性

§2 のフォールバック、clamp、切詰め、既定定数、clone / 再生成を対象に加える。
Rust / C++ / FFI / worker / ファイル解析 / 状態復元。
入口 → producer → queue → consumer まで、ch 数と layout の由来を読む。

**完了条件**: 発見した全ての無言変換について、どの呼出し経路で成立するかと、
拒否 / 適用外 / 明示変換のどれにするかが決まる。**未読 caller を「安全」に分類しない。**
`N_CHANNELS` を 16 に変えて済ませない。

### 調査2 — 生成器と所有者の台帳

§4 の 6 経路に加え、`StereoMeter`、Phase D の 2 系統、resampler、Reference、Attack の
生成・再生成・reset・worker 再起動を追う。

必須列: 用途 / 生成箇所 / 所有者 / native・内部 rate / 入力 view / map 設定 /
窓・履歴 / reset 条件 / 失敗時の状態 / 保存・表示先 / テストが通る入口。

**完了条件**: 共通コンストラクターを置いても迂回経路が残らないことを説明できる。
生成箇所数と実体数は別集計にする。

### 調査3 — 取込容量とリアルタイム契約

§3 の定数、frame / sample / byte 単位、ring 容量、全 frame 投入の原子性、scratch、spool、
pre-roll、worker workspace、12 ペアの世代予算。混在レイアウトの世代も扱う。

**完了条件**: 5.1 / 7.1.4 と既存 384 MiB 契約の整合案がある。
予算式だけ 2ch に据え置かない。予算超過を無言 drop・古い値の再表示・ゼロ埋めで成功扱いしない。
実 RSS と RT 試験は後続 gate に残し、式による見積りと区別する。

### 調査4 — 全指標の意味

第1巡の指標表を、実際の出力 field 単位へ展開する。

必須列: 入力 view / source layout / 使用 ch / LFE / 単位 / 窓長・hop / フィルター /
channel 集約 / 時間集約 / 無音 / 未計測 / 適用外 / エラー / reset / 遅延結果 / 保存先 / UI ラベル。

**完了条件**: Nch で有効 / 明示 view 限定 / 適用外 / 別仕様の承認が必要、が区別されている。
`channels == 2` の 17 分岐は 1 件ずつ読む。
**正しく適用外にしている分岐まで不具合扱いせず、先頭 2ch を全体と偽る分岐を見逃さない。**

### 調査5 — 保存・hash・互換性

§5 の `bands[8]`、全 serializer / deserializer、比較、version、record 長、digest、
旧データ fixture。Record の layout 情報は writer だけでなく frame 生成・finalize・読取り・
Kirin OS 側の利用条件まで接続する。未取得の consumer は未確認に残す。

**完了条件**: Reference 保存形式を ABI 配列と同じ扱いにしない。次元積が閉じている。
旧 Record の欠けた layout を推測で補わない。**保存値が無い状態と、測定済みの 0 を区別する。**

### 調査6 — epoch / Record 遷移と公開方針

§8 の既存 capture epoch、`MeterSession` generation、時計、状態復元、保留中の callback、
queue 内データ、worker 遅延結果、PRE/POST 片側変更の関係を整理する。

**完了条件**: 新しい入力を古い map へ流さない。queue や worker 結果の区間も追跡する。
GUI ラベルの変更だけで完了にしない。Stop 権限と同一形式の Record 継続を維持する。

## 11. 第2巡で完成させる状態遷移表（設計要求。現状の実装表ではない）

| 操作・条件 | 測定 / 履歴 | Record / 保存 | 音声と後続処理 |
|---|---|---|---|
| 同一 layout・同一 rate で block 長のみ変更 | 不要な reset を避ける | 既存 Record 継続契約を守る | 容量充足を確認。A 通過を維持 |
| mono ⇄ stereo | 入力条件の境界を明示。旧履歴と混ぜない | 旧条件と新条件を識別 | 保留中に旧 engine へ新音声を入れない |
| 同数・別 layout | count 一致だけで再利用しない | layout 条件の境界を保存 | 未対応なら明示的に拒否 / 保留 |
| sample rate 変更 | native / 48 kHz 経路・resampler・窓・clock を整合 | capture・測定・時計の関係を明示 | 新 rate を旧 rate として処理しない |
| Record 中に非互換変更 | I / LRA / peak / history の混在を防ぐ | 保留・欠落・再開・最終化の権限を定義 | Stop を捏造しない。A 通過は維持 |
| state restore がホスト交渉と不一致 | 保存設定で入力 layout を上書きしない | 実入力と自己申告設定を分ける | ホストから確定した条件を使う |
| PRE または POST だけ変更 | 比較条件を再検証 | 旧比較結果を現在の差分として保存しない | 不一致の比較は適用外 |
| 旧区間の解析 job が遅れて完了 | 結果の source / epoch を検証 | 現在区間へ誤って合流させない | cancel / 破棄 / 旧区間保存の規則を決める |
| ring 不足・部分 frame・取込失敗 | frame 境界と欠落を保持 | **無音ではなく欠落として残す** | 音声 thread を待たせる対処にしない |
| worker 再起動 | layout / map / 状態の復元方針を確認 | fresh な値と以前の値を区別 | **既定 2ch へ戻らない** |

## 12. 後続実装へ渡す試験要求（要求の定義。実行した試験ではない）

production と同じ入口を通す。期待値を production の table だけから生成しない。

| 試験 | 検出する失敗 |
|---|---|
| 6ch / 12ch の識別信号を worker 入口まで通す | **2ch フォールバック**、先頭 2ch 切詰め、ch 入替、frame 数の誤認 |
| 同じ buffer の frame / sample / 時刻を各境界で照合 | interleave の時間軸解釈、native / 48 kHz 経路の混同 |
| 全生成経路の map 適用を個別に外す変異 | 共通エンジンだけ守り Reference・再起動経路を見逃すテスト |
| Reference の 3 本目以降の 4 帯域へ異なる値を入れる | **`bands[8]` の取り残し**、保存・読取り・比較の誤位置 |
| 同一 PCM・同一 ch 数・異なる layout | PCM digest 一致を測定条件一致と誤認する比較 |
| 旧 Reference / Record を新版で読む | schema の無言誤読、未知 layout の捏造、範囲外読取り |
| Phase D で同一 dual-mono、逆相、無音 ch 追加、LFE のみ | 既存集約の回帰、分母の暗黙変更、未定義な全体知覚量の表示 |
| レイアウト変更後に旧 worker 結果を配送 | 新ラベル・新 epoch への古い結果の混入 |
| 2ch / 5.1 / 7.1.4 混在の世代容量を計算・実測 | **予算計算だけ 2ch**、実 alloc と台帳の不一致 |
| NaN / 未測定 / 適用外 / 測定済み 0 を UI・保存で往復 | 無効値が有効なゼロや古い値として表示される |
| A 入力と A 出力を各遷移で比較 | 観測追加によるサンプル・ch 順・レイテンシの意図しない変更 |

### 12.1 ビルド確認の階層を混ぜない

B-926 で、Linux では `PluginProcessor.cpp` を個別 object としてコンパイルでき、
target 全体は `AppearanceContract.cpp:169` の既存 GCC 曖昧性で先に停止することを確認した。

後続の証拠は、**個別 object / ライブラリ / 全 target / プラグイン load / DAW 実測**に分ける。
個別 object の成功を出荷可能としない。
一方、全 target の無関係な失敗を理由に、個別変更の検証可能性まで否定しない。

## 13. P-0 の終了条件

B-925 §11 の条件に加えて、次が要る。

1. **無言変換と生成経路** — §2 のフォールバック、§4 の 6 経路、再起動経路まで処置方針がある。
2. **意味と保存** — 全指標の入力 view・無効条件が定まり、
   Reference の次元積・schema・hash、Record の既存 epoch との関係が閉じている。
3. **容量と状態** — 実入力と取込容量の式、既存 384 MiB / 12 ペア / 3 callback 契約との整合案、
   非互換変更・queue・worker 遅延を含む遷移表がある。
4. **承認と検証** — 容量 16 と対応配置の別管理、Record 保留・再開、初回公開範囲、
   実行時契約を変更する場合の内容が承認され、後続 gate へ試験が対応付いている。

**ABI 容量 16 は提案として保持するが、Reference 保存形式を全部 16 へ広げる承認でも、
24 インスタンスで 16ch を受理する承認でもない。**

## 14. 第1巡からの教訓

第1巡の弱点は、**見つかった構文の数からシステムの安全性と作業の重心を推定したこと**である。

今回の 2ch フォールバック、`bands[8]`、既存 epoch、取込容量は、
いずれも `channels == 2` や `[2]` だけでは捉えられない。

第2巡の中心は検索結果を増やすことではなく、
**入力の身元・時間・容量・所有者・保存先を一つの経路として結び付けること**である。
数値が正しくても、別の ch 数・別の区間・別の比較条件へ結び付けば観測として誤る。
