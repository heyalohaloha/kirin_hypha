# Hypha サラウンド対応 — 現状レビュー依頼（B-953〜B-971 / 2026-09-19）

**目的**: 第三者レビューのための現状棚卸し。ここに書いたのは**実測・実読の結果**であり、
推測は「未確認 [C]」と明記する。レビューで最も見てほしい点は §7。

- **リポジトリ**: `heyalohaloha/kirin_hypha`
- **ブランチ**: `claude/eager-pascal-edacak`
- **範囲**: `5a6e4cc`(B-955) 〜 `c4a0ff6`(B-971) の 17 commit — 113 files, +4,900 / −834
- **Test**: `cargo test --workspace` 37 binaries 全て ok / failure 0。clippy 本体ゼロ
  （`nih_plug` upstream の 1 警告のみ）。`scripts/check_source_line_budget.sh` PASS。
- **実機検証**: **B-955 以降すべて未実施。** macOS / Windows ビルドも DAW 実測も通していない。

---

## 0. 前提 — 承認済みの判断（D-1〜D-14）

`docs/hypha_surround_decisions_20260918.md` が正本。実装が従う不変条件として特に効いているのは:

| # | 内容 |
|---|---|
| **D-3** | ABI 容量 16。**容量であって対応宣言ではない** |
| **D-4** | 未知 layout を受理しない。チャンネル数から配置を推測しない |
| **D-5** | selector は index ではなく**役割**で指す |
| **D-6** | 集約を発明しない |
| **D-11** | 時間契約を変えない。足すのは reset 契機としての layout 変更だけ |
| **D-12** | 非互換レイアウト変更では測定を保留し、旧 map へ新音声を入れない |
| **D-13** | **C（無言 clamp / drop）と G（誤った値）を R（構築拒否）より先に消す** |
| **D-14** | 取込容量 384 MiB を単純に Nch へ線形拡張しない。検討順は (a) 保持構造 → (b) 予算 → (c) 利用条件 |

**承認に含まれないもの**: 初期 Nch 化指標の確定集合 / 7.1.4 へ進む時点 / downmix 係数 /
Downmix Observation の測定量 / selector の UI 部品と永続化 / 取込容量の (a)(b)(c) の選択。

---

## 1. 何ができるようになったか（実測）

### 1.1 レイアウトは役割で認識される（B-955 / B-956）

`ChannelRole` 14 種（ITU 役割名）と `ChannelLayout`（mono / stereo / 5.0 / 5.1 / 7.1.4）。
`MeasureEngine::new(sample_rate, layout)` が `ebu.set_channel_map()` を必ず明示的に呼ぶ。

**vendor 実読の結果**（`docs/hypha_surround_decisions_20260918.md` §4）:

- `ebur128` 0.1.10 の `channel_sum *= 1.41` は `LeftSurround` / `RightSurround` /
  `Mp060` / `Mm060` / `Mp090` / `Mm090` の 6 型のみ（`vendor/ebur128/src/filter.rs:352-360`）。
  **7.1.4 の rear / 天井は 1.0 のまま。** BS.1770-4 §2 Table 3 と一致する。
- `ebur128::reset()` は channel_map を消さない（`ebur128.rs:555`）。
- 6ch の既定 map は正しい 5.1 map と**一致する**（`ebur128.rs:243-258`）ため、
  **5.1 では「map を適用したか」を検出できない。** 検出には 7.1.4 が要る。

### 1.2 ABI は 16 スロットを運び、どのビルドから来たかを言う（B-958）

`KIRIN_ABI_REVISION = 5`。実測サイズはすべて試験にリテラルで固定してある。

| 構造体 | 変更前 | 現在 |
|---|---:|---:|
| `KirinMeterSession` | 1,008 | **1,840** |
| `KirinObservatoryFrame` | 1,248 | **2,080** |
| `KirinMeterHistoryEntry` | 184 | **248** |

`static_assert` は古い staticlib を見られないため、**実行時 handshake**（`kirin_hypha_abi_contract`）で
殻と staticlib の一致を確かめる。

### 1.3 記録が「どの map で測ったか」を持つ（B-960 / B-966）

`PluginDataFile.measurement_layout`（`MeasurementLayout`: layout 名 / 役割並び / loudness map /
map 規則版）。schema_version は "1.3" のまま（additive optional）。
**checksum の対象に入っていることを実証済み**（B-966。`measurement_layout` だけを書き換えると
`verify_checksum` が false になる）。

### 1.4 測定区間が識別できる（B-962）

`measurement_epoch`（静的 AtomicU64、1 から）。engine を別 layout / rate で作り直すたびに進む。
`generation` も `run_id` も session ごとに 1 から数え直すので、**区間をまたいだ継続かどうかを
言えるのはこの値だけ**である。TIME history の bucket 境界にも入れた。

### 1.5 Spectrum が役割で入力を選ぶ（B-963 / B-970 / B-971）

承認事項 3A の実装。`SpectrumView { Lr, Mid, Side, Channel(ChannelRole) }`。

**入力チャンネル数と解析チャンネル数を別の量として分離した**のが要点。

- `SpectrumView::analysis_channels(layout)` — 解析器が見る信号の本数
  （導出 view は 2 / mono では 1、単一チャンネル view は常に 1）
- `FrameSelection { input, left, right }` — ring から読む本数とフレーム内の位置

**実測（R-10）**: 5.1 で L に振幅 0.5、C に 0.125（比 4 倍 = 12.0412 dB）を流し、
役割 view ごとに最大 dBFS を読むと **L = −8.97 / C = −21.01、差 12.04 dB**。振幅比どおり。
出荷中の stereo でも L / R で同じ差が出る。dropped 0。

フレーム自身が `view: u8` を持ち、**違う観測対象どうしは引き算しない**
（`same_analysis_layout` に view 照合。交換 wire は予約バイトに載せて運ぶ。
`SPECTRUM_SCHEMA_VERSION` 3 → 4）。

---

## 2. 消した欠陥（D-13 の C / G）

| # | 類型 | 内容 | 直した commit |
|---|---|---|---|
| 1 | **C** | `SpectrumRuntime::new` の `num_channels.clamp(1, 2)` — 広い host buffer が 2ch として記録され、以後の block が全部拒否されて無言になる | B-963 |
| 2 | **C** | worker の de-interleave が 1 フレームにつき `num_channels == 2` のときだけ 2 本 pop。6ch の interleave が 1 本の流れとして読まれ ring が詰まる（実測: pushed 96 / dropped 32） | B-970 |
| 3 | **C** | `set_mid_side_enabled` が入力チャンネル数だけを見ていた。1 本の view のまま有効にすると組立器が `None` を返し続け、**有効に見えるのに何も出ない** | B-970 |
| 4 | **G** | TIME Δ が配置を照合していなかった。**mono PRE と stereo POST に同じ音を通すと `lufs_m delta = 3.0102999566398108`。連鎖は何もしていない**（mono を 1ch として測り +3.01 dB バイアスを入れない測定規約の差） | B-968 |
| 5 | **G** | `frame.channel_mode` は単一チャンネル view でも `Lr` のまま。view の名札として読むと C を測ったフレームが「LR」として比較される | B-971 |
| 6 | **自傷** | Timer → `prepareToPlay` が `interleaveScratch` を再確保し `hyphaHandle` を破棄する間、`processBlock` が両方を無ロックで読んでいた（B-961 で作り込んだ） | B-964 |
| 7 | **自傷** | `view()` が役割を答えながら frame は LR を運んでいた（B-963 で作り込んだ） | B-964 |
| 8 | **自傷** | clamp 除去が「きれいな拒否」を「無言の詰まり」に置き換えていた（実測 pushed=32 dropped=43 analyzed=0） | B-965 |

**欠陥 4 は surround 以前の問題で、出荷中の mono / stereo だけで起きる。**
`isBusesLayoutSupported` は mono と stereo を両方受理するので、
mono トラックに PRE / stereo バスに POST という通常の配置で発生する。

---

## 3. 取込容量の実測（B-967）

`docs/hypha_surround_ingest_capacity_20260918.md` §17 が全文。要点:

**§9.6 / §16 が「未列挙」としていた meter history / delta history を測った。**

`MeterHistory` は 3 tier とも `VecDeque::with_capacity` で **engine 生成時に満杯分を先に確保する**。
実 capacity は要求値そのままで丸め上げは無い。

| tier | 解像度 | 実 capacity | bytes |
|---|---|---:|---:|
| exact | 10 Hz / 10 分 | 6,001 | 1,968,328 |
| one_second | 1 Hz / 2 時間 | 7,201 | 2,361,928 |
| ten_seconds | 0.1 Hz / 24 時間 | 8,641 | 2,834,248 |
| **合計** | | **21,843** | **7,164,504（6.83 MiB）** |

`MeterHistoryEntry` は **328 B**（B-962 の epoch で 320 B から +8 B）。1 バイトが 21,843 倍で効く。
**engine 1 台は MeterHistory を 2 本持つ**（`meter_session.rs:117` / `meter_delta_history.rs:99`）。

### RSS 実測（`memory_contract_probe census`、24 インスタンス）

| 条件 | 確保直後 | 全 tier を埋めたあと |
|---|---:|---:|
| 48 kHz / 2ch | **+0.29 MiB** | **+164.21 MiB** |
| 192 kHz / 2ch | +0.64 | +164.34 |
| 48 kHz / 6ch | +0.28 | +163.86 |

1. **確保だけでは resident にならない**（論理確保 164.0 MiB に対し RSS +0.29 MiB）
2. **埋め切ると論理確保とほぼ一致する**（差 0.2% 未満）。ring は 1 ページも余らせない
3. **sample rate にもチャンネル数にも依存しない** — これは history が Nch 化していないことの
   裏返しであって、Nch でも安いという意味ではない

### 製品としての量

2 本 × 24 インスタンスで **328.0 MiB。384 MiB 予算の 85.4%** が §3 の 4 領域モデルに入っていない。
**Record burst と違い、Watch を開いているだけで作業時間に比例して埋まる。**

| 経過 | 埋まる tier | 24 × 2 本 |
|---|---|---:|
| 10 分 | exact | 90.1 MiB |
| 2 時間 | + one_second | 198.2 MiB |
| 24 時間 | + ten_seconds | 328.0 MiB |

（終点のみ実測。中間は算術。）`AttackRuntime`（192k で 134 MiB）を超える最大の未計上項目だった。

### Nch 化したときの増分（**仮定を明示した算術。設計の提案ではない**）

| 仮定 | entry | 24 × 2 本 |
|---|---:|---:|
| 現状（stereo 形のまま） | 328 B | 328.0 MiB |
| `clip_event_count` のみ 12ch 化 | 368 B | 368.0 MiB |
| 5 range すべてを 12ch 分持つ | 3,008 B | **3,007.7 MiB（予算の 7.8 倍）** |

D-14 の (a)(b)(c) を判断するとき、history は Record burst と同じ桁の材料になる。

---

## 4. 利用者にまだ届かないもの（重要）

**出荷中の挙動は B-968 以外ほとんど変わっていない。** 理由は 2 つの門が閉じたままだから:

1. **`isBusesLayoutSupported`（`PluginProcessor.cpp:120-130`）は mono / stereo しか受理しない。**
   surround が engine に到達するのは FFI を直接叩く経路だけ。
2. **役割 view を選ぶ ABI 入口が無い。** `kirin_hypha_set_spectrum_channel_mode` は
   0..2 の導出 view しか受け付けない。

したがって §1.5 の 5.1 解析も、§2 の欠陥 1/2/3/5 も、**現時点では利用者に届かない**。
INV（利用者に見える契約）には足していない。

**ABI を開くときに同じ commit で要るもの**:

- 役割 view の setter と `KirinSpectrumFrame` への `view` フィールド（内部側は B-971 で済み）
- surround bus を受理したときの拒否の見せ方（§5 の 1 / 2）

---

## 5. 未処理申し送り

1. **拒否が UI に出ない（R-28）。** 次の 2 つは値を捏造しないので R であって G ではないが、
   利用者は明示的に操作しているので「沈黙すれば問題なく進んだと勘違いする」側に当たる。
   FFI に理由を伝える経路が無い。
   - B-968 の配置不一致による TIME Δ の結合拒否
   - surround での `MeterSession` 構築拒否（`kirin_hypha_poll_meter_session` が false を返す）
2. **history を Nch でどう持つか未確定**（D-6）。§3 の算術が材料。
3. **`AttackRuntime` / `SharpnessContinuousAnalyzer` は 2ch を超えると構築を拒否する**（R）。
   同じ selector へ載せるのは未着手。
4. **24 インスタンスが history ring を 2 本とも同時に埋めるかは未確認** [C]。
   `DeltaHistoryState` 側は POST 結合が無ければ押されない。片側だけなら 24 時間で 164.0 MiB。
5. **B-955 以降すべて実機未検証。** ABI を開く作業に入る前にここを通すのが順序として正しい、
   というのが実装側の見解。

---

## 6. 試験規律

- **§9.1**: 期待値を製品から作らない。すべてリテラルか、選んだ入力からの算術。
  これで実際に 2 件の誤りを捕まえた（`KIRIN_STEREO_FIELD_BINS` を 1024 と推測 → 実際 625 /
  offset の誤り）。
- **§9.2**: 変異 → 当該試験だけ落ちる → 復元。本セッションで約 30 件。
  **1 件、試験の弱さがこの手順で露見した**: 詰まり試験が ring 容量に収まる量しか流しておらず
  「1 フレームにつき 1 本しか pop しない」変異を捕まえられなかった。
  ring 容量の 4 倍を流すよう直してから取り直した。

---

## 7. レビューで特に見てほしい点

### 7.1 B-968 の直し方は妥当か

違う配置で測った PRE / POST を**結合しない**（差を補正しない）ことにした。
mono / stereo の 3.01 dB は既知の定数だが、補正は集約の発明であり D-6 に反する、という判断。
交換 schema は 2 → 3 に上げたので、版が混ざれば Δ が出ない（G → R への置換）。

**問いたいこと**: R-28 の観点で、拒否を UI に出す経路が無いまま先に拒否を入れたのは正しい順序か。
それとも UI 経路とセットにすべきだったか。

### 7.2 「入力チャンネル数 ≠ 解析チャンネル数」の分離は十分か

B-970 で 2 つの量を分けたが、`num_channels` という名前は `SpectrumRuntime` に残っている。
`AttackRuntime` / `SharpnessContinuousAnalyzer` / `PerceptualAssembler` / `AbsoluteAssembler` は
まだ「チャンネル数」を 1 つの量として扱う。**同じ混同が別の場所に残っていないか。**

### 7.3 §3 の容量測定の読み方

- 「確保 0.29 MiB / 埋め切り 164.21 MiB」から、**製品 RSS をどう見積もるべきか。**
  24 インスタンスが同時に 24 時間 Watch を開く前提は現実的か。
- §3 の Nch 仮定表は「算術であって提案ではない」としたが、**D-14 の材料として十分か。**
  足りないなら何を測るべきか。

### 7.4 5.1 の map 検証が 5.1 ではできない件

§1.1 のとおり、ebur128 の 6ch 既定 map は正しい 5.1 map と一致するため、
**5.1 のテストでは「map を適用したか」を検証できない。** 7.1.4 を使って検証している。
D-1 は「5.1 を先に通し、7.1.4 は後続」（根拠: 5.1 は公式 EBU test set で検証できる唯一の
サラウンド）と決めている。**出荷順としての D-1 と、検証手段としての 7.1.4 必須は両立するのか。**
5.1 を先に出す場合、map 適用の検証は 7.1.4 経由のままでよいか。

### 7.5 実機未検証のまま積み上げた量

B-955 から B-971 まで 17 commit、113 files。**一度も実機で動かしていない。**
この状態でさらに ABI を開くべきか、先に実機を通すべきか。
実装側は「先に実機」を推奨しているが、根拠は「未検証の量が多い」という一般論であり、
具体的な失敗の予測ではない。

---

## 8. 参照

| 文書 | 内容 |
|---|---|
| `docs/hypha_surround_decisions_20260918.md` | D-1〜D-14、承認範囲、P-1 で判明した vendor 事実 |
| `docs/hypha_surround_ingest_capacity_20260918.md` | 取込容量。§17 が history の実測 |
| `docs/hypha_surround_metric_contracts_20260918.md` | 4 指標の契約表 |
| `docs/hypha_surround_aggregation_candidates_20260918.md` | AGGREGATION-UNDEFINED 指標の候補 |
| `docs/hypha_surround_p0_inventory_20260918.md` | P-0 棚卸し |
| `docs/hypha_surround_implementation_plan_20260918.md` | P-1〜P-4 の計画 |
| `docs/hypha_invariants.md` | INV 一覧（INV-S17 を B-968 で更新） |
