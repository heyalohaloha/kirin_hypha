# Hypha サラウンド化 — チャンネル対応の確定事実

Status: 事実収集のみ。実装計画ではない。レイアウト選定・準拠版の宣言は Daisuke の決定事項。

Date: 2026-09-18

Branch: `claude/eager-pascal-edacak`

この文書は、サラウンド対応で**どのレイアウトを選んでも変わらない事実**だけを置く。
すべてリポジトリ内の一次資料（JUCE 本体、vendor/ebur128、本体コード）を実 read して確認した。
外部一次資料（itu.int / tech.ebu.ch / 競合製品ページ）は本セッションの egress policy が
CONNECT を 403 で拒否するため取得できていない。未検証項目は §6 に分けた。

## 1. 既に確定している範囲（`docs/hypha_bs1770_5_r128_v5_audit_20260831.md` / B-605）

二重定義を避けるため要点のみ引く。矛盾時は B-605 の監査文書を優先する。

- 現行版は **ITU-R BS.1770-5**（2023-11-22 承認）。BS.1770-4 からの技術変更は
  Annex 4（object-based audio）の追加、BS.2051-3 に合わせた loudspeaker configuration I / J の追加、
  Annex 3 の configuration G の訂正である。
- Hypha が使う Annex 1（K-weighting / 400 ms / 75% overlap / gate）と Annex 2（True Peak）に
  この改訂の規範差分はない。
- **したがって -4 と -5 が実際に食い違うのは、まさにサラウンドが入る Annex 3 / Annex 4 である。**
  2MIX の間は版差が無害だったが、サラウンドへ出た瞬間に版の選択が測定内容を決める。
- `Cargo.lock` は `ebur128 0.1.10` を固定し、この crate の実装対象は **BS.1770-4** である。
- 製品表示は `ITU-R BS.1770-5 Annex 1/2, mono/stereo` に限定すると決めてある。

## 2. JUCE のチャンネル並びは初期化リスト順ではない

`AudioChannelSet` はチャンネルを `BigInteger` のビット集合として持ち、
`getTypeOfChannel` / `getChannelIndexForType` / `getChannelTypes` はいずれも
`findNextSetBit(0)` から**昇順**に走る
（`modules/juce_audio_basics/buffers/juce_AudioChannelSet.cpp`）。

つまり `AudioBuffer` 上のチャンネル index 順は **`ChannelType` の enum 値の昇順**であり、
`create7point1point4()` の初期化リストに書かれた順序ではない。

主要な enum 値（`juce_AudioChannelSet.h`）:

| 値 | 型 | 値 | 型 | 値 | 型 |
|---|---|---|---|---|---|
| 1 | left | 10 | leftSurroundSide (Lss) | 18 | topRearRight (TRR) |
| 2 | right | 11 | rightSurroundSide (Rss) | 20 | leftSurroundRear (Lsr) |
| 3 | centre | 13 | topFrontLeft (TFL) | 21 | rightSurroundRear (Rsr) |
| 4 | LFE | 15 | topFrontRight (TFR) | 22 | wideLeft |
| 5 | leftSurround (Ls) | 16 | topRearLeft (TRL) | 23 | wideRight |
| 6 | rightSurround (Rs) | | | 28 | topSideLeft (TSL) |
| 9 | centreSurround | | | 29 | topSideRight (TSR) |

この結果、**7.1.4 では天井 4 本（index 6–9）が後方サラウンド 2 本（index 10–11）より先に来る**。
初期化リストの見た目とは逆である。並びを目視で推測してはならない。

## 3. ebur128 の default channel map との突き合わせ

`vendor/ebur128/src/ebur128.rs:228 default_channel_map` は 4ch と 5ch だけを特別扱いし、
それ以外は先頭 6 個を `[Left, Right, Center, Unused, LeftSurround, RightSurround]` に固定して
**index 6 以降をすべて `Unused`** にする。

`Channel::Unused` のチャンネルは loudness の合計に一切寄与しない。
BS.1770 の +1.5 dB（energy ×1.41）は `LeftSurround` `RightSurround` `Mp060` `Mm060`
`Mp090` `Mm090` にだけ掛かる（`vendor/ebur128/src/filter.rs:352`）。

本体コードは `set_channel` / `set_channel_map` を**一度も呼んでいない**（grep で確認）。
よって常にこの default map が効く。

JUCE の index 順に default map を当てた結果:

| レイアウト | ch | JUCE index 順 | 落ちる本数（LFE 除く） | +1.5 dB が当たる実体 |
|---|---|---|---|---|
| stereo | 2 | L R | 0 | — |
| 5.0 | 5 | L R C Ls Rs | 0 | Ls Rs（正しい） |
| 5.1 | 6 | L R C LFE Ls Rs | 0 | Ls Rs（正しい） |
| 7.0 | 7 | L R C **Lss** Rss Lsr Rsr | **2**（Lss, Rsr） | **Rss, Lsr**（左右が食い違う） |
| 7.1 | 8 | L R C LFE Lss Rss Lsr Rsr | 2（Lsr, Rsr） | Lss Rss |
| 5.1.2 | 8 | L R C LFE Ls Rs TSL TSR | 2（TSL, TSR） | Ls Rs |
| 5.1.4 | 10 | L R C LFE Ls Rs TFL TFR TRL TRR | 4（天井 4 本） | Ls Rs |
| 7.1.2 | 10 | L R C LFE Lss Rss Lsr Rsr TSL TSR | 4 | Lss Rss |
| 7.1.4 | 12 | L R C LFE Lss Rss **TFL TFR TRL TRR** Lsr Rsr | **6** | Lss Rss |
| 7.1.6 | 14 | L R C LFE Lss Rss TFL TFR TRL TRR Lsr Rsr TSL TSR | 8 | Lss Rss |
| 9.1.6 | 16 | L R C LFE Lss Rss TFL TFR TRL TRR Lsr Rsr WL WR TSL TSR | **10** | Lss Rss |

読み方:

- **5.0 と 5.1 だけが偶然そのまま正しい。** LFE が index 3 に来るので `Unused` と一致する。
- **7.0 は LFE を持たないため index 3 が Lss になり、以降が 1 本ずつずれる。**
  Rss が LeftSurround として、Lsr が RightSurround として +1.5 dB を受け、Lss と Rsr は消える。
  左右非対称の誤りが無言で出る。
- 7.1 以上では落ちる本数が過半に達し、9.1.6 では 16 本中 10 本が loudness に寄与しない。
- いずれも**エラーにならない**。数字は出るが小さい側へ静かにずれる。

`EbuR128::set_channel_map`（`ebur128.rs:425`）は存在するので、レイアウトごとの
明示テーブルを渡せば解決する。推測で通してはならない、というのが AGENTS.md:175
「サラウンド対応を計測 core の引数だけから推定しない」の実体である。

## 4. 既存のサラウンド実測がどこまで担保しているか

`crates/kirin_measure/tests/ebu_v05_reference.rs:187` は EBU Loudness Test Set v05 の
`seq-3341-6-5channels-16bit.wav`（5ch）と `seq-3341-6-6channels-WAVEEX-16bit.wav`（6ch）を
LUFS-I `-23.0` で検証し、pass している。

これが成立するのは §3 の表のとおり **5ch / 6ch が default map で偶然正しいから**であり、
7ch 以上の担保にはならない。公式 test set にも 7ch 以上の素材はない。

`MeasureEngine::new`（`crates/kirin_measure/src/engine.rs:130`）にはチャンネル数の上限がなく、
`EbuR128` の `MAX_CHANNELS = 64` までそのまま通る。
2MIX 限定を実際に効かせているのは次の 2 か所だけである。

- `juce_shell/src/PluginProcessor.cpp:208 isBusesLayoutSupported` — `mono()` と `stereo()` のみ受理。
- `crates/kirin_measure/src/stereo_meter.rs:105 StereoMeter::new` — `1..=2` 以外を拒否。

## 5. 2 チャンネル前提が埋まっている箇所

- `channels == 2` の分岐: **17 か所** / 8 ファイル
  （`attack_detail.rs` `attack_runtime.rs` `attack_runtime_assembler.rs` `attack_runtime_worker.rs`
  `spectrum_mid_side.rs` `spectrum_runtime_assemblers.rs` `spectrum_runtime_worker.rs` `stereo_meter.rs`）。
- `KirinMeterSession` の `[2]` 固定長フィールド: **8 本**
  （`channel_clip_latched` `sample_peak_dbfs` `sample_peak_hold_dbfs` `channel_true_peak_dbtp`
  `channel_max_true_peak_dbtp` `clip_events` `channel_vu_dbfs` `channel_instant_true_peak_dbtp`）。
- 同様の `[2]`: `kirin_hypha_ffi.h:164 clip_event_count`、
  `kirin_hypha_reference_index_ffi.h:9 rms/peak`、`kirin_hypha_reference_visual_ffi.h:14 peak/rms`。
- Phase D（Zwicker）は既にチャンネル総称
  （`crates/kirin_measure/src/phase_d/channels.rs:30`）で、この制約の外にある。

## 6. 未検証（本セッションからは確認できない）

egress policy が次のホストへの CONNECT を 403 で拒否する。README の指示に従い再試行しない。

`www.itu.int` `tech.ebu.ch` `www.nugenaudio.com` `www.izotope.com` `www.apple.com`

したがって以下は**未確認**であり、Daisuke 側で一次資料から確認する必要がある。

- 競合（NUGEN VisLM、iZotope Insight 2）が実際に対応するレイアウトの範囲。
- Apple / Dolby Atmos の配信要件の数値と、そこが引用する BS.1770 の版。
- BS.1770-5 Annex 3 の configuration ごとの channel weighting の規定値。
  §3 に書いた ×1.41 の適用先は **ebur128 0.1.10 の実装がそうしている**という事実であり、
  規格本文との一致は確認していない。
- JUCE の各 `ChannelType` が ITU のどの角度（M+110 / U+045 等）に対応するかの割り当て。
  JUCE も vendor 化されている VST3 SDK も**名前しか持たず、角度を書いていない**。
  BS.2051 本文が要る。

## 7. 決定が要る事項（Daisuke）

事実から言えることだけ添える。工数は材料にしない。

1. **対象レイアウト。** 推奨は 5.1 / 7.1.4 の 2 本から。根拠: 5.1 は既に公式 test set で
   実測 pass 済みの唯一のサラウンド、7.1.4 は Atmos bed の実務上の最大公約数で、
   §3 の表で最も壊れ方が大きい（12 本中 6 本が消える）ため対処の価値が最も高い。
2. **準拠版の宣言。** 推奨は BS.1770-5 Annex 3 を明示。根拠: §1 のとおり -4 と -5 が
   食い違うのがまさに Annex 3 であり、vendor は -4 実装なので、版を決めないと
   `set_channel_map` に入れる weighting テーブルが確定しない。
3. **2 チャンネル固定の ABI をどう替えるか。** §5 の 8 本 + 3 本。
4. **2MIX 専用計測（correlation / balance / MONO / MID-SIDE）をサラウンドで何に置き換えるか。**

## 8. 次にやること

§7 が決まるまで実装計画は書けない。
§6 のうち BS.2051 の角度表だけは、どのレイアウトを選んでも必要になる。
