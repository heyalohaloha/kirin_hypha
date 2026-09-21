# Hypha サラウンド化 — チャンネル対応の確定事実

Status: 事実収集。実装計画ではない。対象レイアウトと参照版の確定は Daisuke の決定事項。

Date: 2026-09-18

Branch: `claude/eager-pascal-edacak`

二種類の事実を分けて置く。

- **A. リポジトリ内の一次資料**（JUCE 本体 / `vendor/ebur128` / 本体コード）を実 read したもの。§2–§5。
- **B. 外部の一次資料**を Daisuke 側が照合したもの。§6。本セッションの egress policy は
  `itu.int` `tech.ebu.ch` `nugenaudio.com` `izotope.com` `apple.com` への CONNECT を 403 で拒否するため、
  Claude Code 側での再取得はしていない。出典は §6 の各行に置く。

未確認は §7 に残す。推測で埋めない。

## 1. 既に確定している範囲（`docs/hypha_bs1770_5_r128_v5_audit_20260831.md` / B-605）

二重定義を避けるため要点のみ引く。矛盾時は B-605 の監査文書を優先する。

- 現行版は **ITU-R BS.1770-5**（2023-11-22 承認）。BS.1770-4 からの技術変更は
  Annex 4（object-based audio）の追加、BS.2051-3 に合わせた loudspeaker configuration I / J の追加、
  Annex 3 の configuration G の訂正である。
- Hypha が使う Annex 1（K-weighting / 400 ms / 75% overlap / gate）と Annex 2（True Peak）に
  この改訂の規範差分はない。
- **したがって -4 と -5 が実際に食い違うのは、まさにサラウンドが入る Annex 3 / Annex 4 である。**
  2MIX の間は版差が無害だったが、サラウンドへ出た瞬間に版の扱いが測定内容に効く。
- `Cargo.lock` は `ebur128 0.1.10` を固定し、`Cargo.toml:46` が `vendor/ebur128` へ差し替えている。

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
energy ×1.41 は `LeftSurround` `RightSurround` `Mp060` `Mm060` `Mp090` `Mm090` にだけ掛かる
（`vendor/ebur128/src/filter.rs:352`）。
これは **`ebur128 0.1.10` の実装がそうしている**という事実であり、規格本文との一致は §7 で未確認とする。

本体コードは `set_channel` / `set_channel_map` を**一度も呼んでいない**（grep で確認）。
よって常にこの default map が効く。

JUCE の index 順に default map を当てた結果:

| レイアウト | ch | JUCE index 順 | 落ちる本数（LFE 除く） | ×1.41 が当たる実体 |
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
  Rss が LeftSurround として、Lsr が RightSurround として ×1.41 を受け、Lss と Rsr は消える。
  左右非対称の誤りが無言で出る。
- 7.1 以上では落ちる本数が過半に達し、9.1.6 では 16 本中 10 本が loudness に寄与しない。
- いずれも**エラーにならない**。数字は出るが小さい側へ静かにずれる。

### 3.1 チャンネル数からレイアウトを特定してはならない

上表のとおり **8ch に 7.1 と 5.1.2 が、10ch に 5.1.4 と 7.1.2 が並ぶ**。
本数が同じでも位置が違うので、weighting も落ちる本数も変わる。
チャンネル数だけを見て map を選ぶ実装は、この 2 組で必ず誤る。
これが AGENTS.md:175「サラウンド対応を計測 core の引数だけから推定しない」の実体である。

### 3.2 `change_parameters` はチャンネル数が変わるとき map を捨てる

`vendor/ebur128/src/ebur128.rs:460-465` は、チャンネル数が変わった場合にだけ
`channel_map` を `default_channel_map(channels)` で上書きする。sample rate だけの変更では維持される。

本体は `change_parameters` を**一度も呼んでいない**（grep で確認）ため、現状この経路は動いていない。
採用する場合は、**呼んだ直後に必ず `set_channel_map` をやり直す**必要がある。

### 3.3 `EbuR128::set_channel_map` は存在する

`vendor/ebur128/src/ebur128.rs:425`。レイアウトごとの明示テーブルを渡せば §3 の欠落は解消する。
必要なのは JUCE の `ChannelType` → `ebur128::Channel` の対応表であり、その ITU 角度の根拠は §7 で未確認。

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

## 6. 外部一次資料（Daisuke 照合 / 2026-09-18）

| 項目 | 確認結果 | 仕様化するときの限定 |
|---|---|---|
| NUGEN VisLM | VisLM-C / VisLM-H とも **stereo / 5.1 / 7.1 / 7.1.2 / 7.1.4**。柔軟な構成を許すホストでは 1.0〜7.1.4。 | 対応形式の記載であり、全 DAW・全プラグイン形式での動作保証ではない。出典: [VisLM2 Manual](https://nugenaudio.com/files/manuals/VisLM2%20Manual.pdf) |
| iZotope Insight 2 | **stereo〜7.1.2 Atmos**。Intelligibility Meter あり。 | Intelligibility は **Relay 経由で受け取るダイアログトラックとミックス全体の比較**。7.1.4 対応や Atmos オブジェクト解析はこの資料からは確認できない。出典: [iZotope 製品ページ](https://www.izotope.com/en/products/insight.html) |
| Apple Music / Dolby Atmos | Dolby Atmos music deliverables は **Integrated loudness が −18 LKFS を超えないこと、True Peak が −1 dBTP を超えないこと、どちらも BS.1770-4 で測定**。 | **上限であって目標値ではない。** −20 LKFS は満たし、−16 LKFS は超過する。音楽の Atmos 納品要件であり、通常のステレオ配信や映像向けの別項目とは別。出典: [Apple Video and Audio Asset Guide](https://help.apple.com/itc/videoaudioassetguide/en.lproj/static.html) |
| EBU Tech 3344 | **v2.0 で AC-4 と MPEG-H を追加。** 現行は v2.1（v2.0 からの差は字体・図の配置などの編集上の修正）。 | 配信・再生経路の運用資料であり、メーターの中核アルゴリズム仕様ではない。参照は v2.1 とする。出典: [EBU 改訂履歴](https://tech.ebu.ch/publications/tech3344/changelog) |

### 6.1 参照版は一本化せず、用途で分ける

ITU の現行版は BS.1770-5 だが、**Apple の Atmos 納品ガイドは BS.1770-4 を明示的に参照する。**
確認日時点でこの 2 つは一致していない。

`vendor/ebur128` の BS.1770-4 / BS.2051-2 への言及は、`ebur128.rs:81` の `Channel` enum の
doc comment、すなわち**チャンネル位置の定義の参照先**である。crate 全体の doc は EBU R128 の
実装としか書いていない。この一文から「ライブラリ全体が検証済み」「Hypha が準拠済み」
「BS.1770-5 には使えない」のいずれも導けない。

したがって:

- Apple 向けプロファイルの参照規格は **BS.1770-4 で確定できる**。
- **Hypha 本体の準拠版は未確定。** 採用ライブラリの版・実装範囲・試験結果を確認するまで決まらない。
- 「最新だから BS.1770-5 準拠と表示する」進め方は取らない。

### 6.2 BS.1770-5 Annex 4 — オブジェクト音声は「レンダリングしてから測る」

BS.1770-5 Annex 4（本文 26 ページ）は、オブジェクト音声、またはチャンネル音声とオブジェクト音声の
混合について、**指定したスピーカー配置へレンダリングし、その出力を Annex 1 / 3 の方法で測る**
方式を示す。さらに、レンダリング条件によって測定値が変わり得るため、
**使用したスピーカー配置とレンダリングアルゴリズムを報告する**よう記載している。

ここから言えるのは次の 2 点で、両立する。

- Hypha がオブジェクトのメタデータを直接読まないこと。
- オブジェクトを含む作品の**レンダリング結果**を測定できること。

外部レンダラーの出力を受けるだけで BS.1770-5 準拠が自動的に成立するわけではない。

### 6.3 測定対象の言い方（設計文言の案 / 実装確認結果ではない）

> Hypha が通常の音声バスから取得する測定対象は、ホストから提供されるチャンネルベースの PCM 音声である。
> 専用の連携を実装しない限り、Atmos のオブジェクト ID や位置・移動などのメタデータは取得対象に含めない。
> 外部レンダラーの出力を測定する場合は、その出力配置とレンダリング条件を測定情報として区別する。

以前この点を「見えるのはレンダラーが出したベッド」「競合も全く同じ制約」と書いたが、どちらも誤り。

- 正しくは「**ベッドとオブジェクトを、指定した配置へレンダリングしたチャンネルベースの出力**」。
  7.1.4 のレンダリング出力と元の Atmos ベッドは同義ではない。
- 「競合も同じ制約」は広すぎる。**同じ PCM 入力だけを使うメーター**までしか言えない。
  Dolby Atmos Renderer はメタデータを入力に取り、Insight 2 は Relay 経由で別トラックの情報を受ける。

### 6.4 LFE は「LUFS から除外」と「観測しない」を別に扱う

BS.1770-4 のラウドネス計算は LFE を合算対象に含めない
（`vendor/ebur128` でも `Channel::Unused` の例に LFE が挙がる）。

設計上は、**ラウドネスへの寄与・チャンネル別ピーク・信号の有無を別々に持つ。**
規格上の除外を守りながら LFE 自体の観測は残せる。

### 6.5 Tech 3344 と Tech 3341 / 3342 の役割を混ぜない

Tech 3344 は配信・再生経路の運用資料である。AC-4 / MPEG-H を含むことは、
Hypha にそれらのデコーダーが要るという意味でも、この資料だけで測定器を実装できるという意味でもない。

測定実装が参照するのは BS.1770 に加えて、メーターの EBU Mode は **Tech 3341**、
Loudness Range は **Tech 3342** である。

## 7. 未確認

- **BS.2051 の角度表** — JUCE の各 `ChannelType` が ITU のどの角度（M+110 / U+045 等）に当たるか。
  JUCE にも vendor 化された VST3 SDK にも**名前しか無く、角度が書かれていない**。
  どのレイアウトを選んでも必要。
- **BS.1770-5 Annex 3 の configuration 別 channel weighting の規定値。**
  §3 の ×1.41 の適用先は実装の事実であり、規格本文との一致は未確認。
- **5.1 / 7.1.4 が Annex 3 のどの configuration に当たるか。**
  -4 → -5 の変更は configuration G の訂正と I / J の追加なので、
  **対象レイアウトが G / I / J に当たるかどうかで、-4 と -5 の数値差が出るかが決まる。**
- **Dolby のラウドネス専用リレンダーに関するサポート記事**は本文未取得。
  Apple 納品判定に結び付く測定経路は確定していない。
  数値条件が確認できたことを理由に、任意の 7.1.4 出力やバイノーラル出力の測定だけで
  「Atmos 納品適合」と判定してはならない。
- **現行 Hypha の依存版・チャンネル処理・ホストごとの受け渡し・適合試験結果。**
  §3 は API 上の条件であり、現行実装の不具合を指摘したものではない。

## 8. 決定が要る事項（Daisuke）

事実から言えることだけ添える。工数は材料にしない。

1. **対象レイアウト。** 推奨は 5.1 と 7.1.4 の 2 本から。根拠: 5.1 は公式 test set で実測 pass 済みの
   唯一のサラウンド。7.1.4 は VisLM が対応し Insight 2 が対応しない位置にあり（§6）、
   §3 の表で最も壊れ方が大きい（12 本中 6 本が消える）。
2. **参照版の扱い。** 推奨は版を一本化せず、**測定に使った版を記録・表示する**。
   根拠: §6.1 のとおり Apple 納品は BS.1770-4 参照、ITU 現行は -5 で一致していない。
   Annex 1 / 2 に差分は無いので mono/stereo の数値は変わらない。
3. **記録項目の分離。** 入力レイアウト / 実際のチャンネルマップ / レンダラー前後のどちらを測ったか /
   レンダリング条件 / 規格の参照版 / 測定エンジンの版 を、それぞれ別の項目として残す。
   不明な項目は推測で埋めず、不明のまま残す。
4. **2 チャンネル固定の ABI をどう替えるか。** §5 の 8 本 + 3 本。
5. **2MIX 専用計測（correlation / balance / MONO / MID-SIDE）をサラウンドで何に置き換えるか。**

## 9. 次にやること

§8 が決まるまで実装計画は書けない。
§7 の BS.2051 角度表は、どのレイアウトを選んでも必要になる。

サラウンド化の中心は「12 チャンネルを受け取れること」ではなく、
**どの位置の信号を、どの条件で測ったのかを誤認なく残せること**である。
