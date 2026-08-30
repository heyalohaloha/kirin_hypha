# Kirin Hypha POST Transient Delta 実装計画

**日付**：2026-08-30
**状態**：DRUM pilotでPrecision、Recall、F1通過。kick、timing、worst-foldは未達、公開ATTACKはOFF
**実装ラベル**：B-553
**画面名**：`ATTACK`（仮称）

Phase 2-Rの正本は`docs/transient_delta_phase2_recovery_plan_20260830.md`とする。
B-546候補報告は診断履歴として残すが、評価契約の監査結果により公開Goまたは恒久No-Goの根拠には再利用しない。
B-550からB-552の入力契約とB-553の実測は各報告書に記録し、kick、timing、worst-fold、fresh holdoutが全gateを通るまでworker以降へ進まない。

## 1. 結論

POST限定の一つのAnalysis画面として、六秒の時間軸を持つ`ATTACK`へ明示選択式の`DRUM`と`2MIX`を追加する。
主表示は、同じイベント時刻における符号付き `POST − PRE` Onset Flux のイベントステムとする。
補助値は、同じイベントに対する短時間 Crest、Sample Peak、Sharpness とする。
キック、スネア、ハイハットなどの楽器名は推定しない。
利用者へスコア、良否、目標値、修正提案を出さない。
画面へ複数の連続曲線を詰め込まず、イベントの位置と変化量を最初に読める構成にする。
既存の METERS、FREQ、SHARP、LIVE、Watch、Record、Keep、Pairing、ABS、Kirin OS 連携は変更しない。

## 2. 目的

この機能の目的は、処理操作後のアタック発生時刻と変化量を、DRUMではdrum/percussion bus、2MIXでは完成mixについて判断代行なしで確認することである。
二profileは別々のdevelopment、fresh holdout、definition hash、Go判定を持つ。
音声は生成、変更、減衰、遅延させず、R-12 の製造境界を維持する。

## 3. 用語

**Onset**：音の立ち上がりとして検出された観察イベントであり、楽器分類ではない。
**Onset Detection Function（ODF）**：各分析時刻で立ち上がりの強さを連続値として表す関数である。
**Onset Flux**：隣接する時間フレーム間で増加したスペクトル成分を集約した ODF である。
**イベントステム**：検出時刻を横位置、符号付き差分を縦方向と長さで示す一本の線である。
**共通イベント判定**：PRE と POST を別々に採択せず、一つの共通時間軸と判定系列からイベント時刻を決める処理である。
**観察系列位置**：DAW の再生位置が後方へ戻っても、六秒表示を連続して管理するための単調増加する内部位置である。
**観察欠損**：無音ではなく、worker、通信、transport、定義変更などにより実測フレームを確認できない状態である。

## 4. 変えない契約

- Audio Thread は既存の bounded lock-free copy と通知だけを行う。
- Audio Thread で FFT、Mel 変換、ODF、peak picking、割り当て、lock、I/O、待機を行わない。
- PRE と POST の時刻、定義、配置情報が一致しない差分は表示しない。
- raw 計測値を UI の表示範囲で clip しない。
- on-demand の Analysis とし、閉じている場合は専用解析を行わない。
- 一つの POST instance では FREQ、SHARP、LIVE、ATTACK の一つだけを解析する。
- DAW process 全体の Analysis 上限は既存と同じ二枠とする。
- FREQ、SHARP、LIVE、ATTACK 間の切り替えでは所有枠を維持し、METERS へ戻した場合または editor を閉じた場合だけ解放する。
- PRE が明示 OFF の場合は絶対値へ代替せず、`PRE OFF - ATTACK paused` と事実だけを表示する。
- 未ペアの場合は PRE worker を要求せず、ATTACK を開始しない。
- ATTACK が visible なら未ペア、PRE OFF、起動失敗でも同じ lease を保持し、METERS または editor close 以外で別 POST へ移さない。
- DRUMと2MIXを自動判定せず、一profileだけを動かし、切替時はrequest、history、selectionをclearしてPREへ同じprofileを要求する。
- 初回はprofile未選択でrequestを発行せず、明示選択を同じeditor lifetimeだけ保持する。
- 音声信号や差分音声を再生する機能は追加しない。

## 5. 外部調査から採用する考え方

### 5.1 EVERTONE COMPRESSOR V2

入力と処理後を同じ視覚面で観察し、操作直後の変化を確認できる構造を参考にする。
表示をクリックして止め、形状を落ち着いて観察できる操作も、Hypha の選択固定に通じる考え方として参考にする。
処理、推奨値、深度警告、音作りの操作は Hypha へ持ち込まない。

### 5.2 FVscope

音量だけでなく時間的な動きや勢いを可視化し、良否を判定しない立場を参考にする。
FV の独自百分率、ジャンル別 scale、評価帯は Hypha の計測定義と異なるため使用しない。

### 5.3 Onset detection の公開知見

単純な振幅差だけでは複雑な混合音の onset を安定して拾えないため、時間周波数表現を候補の中心に置く。
HFC は打楽器の検出に有効な場合がある一方、低い音程の onset を弱く扱い、シンバルへ偏る可能性がある。
Mel Spectral Flux は周波数方向の詳細を抑えつつ増加成分を観察でき、静的な gain 変化へ比較的強い候補である。
ODF の計算と peak picking は別段階にし、候補アルゴリズムの評価とイベント採択の評価を混同しない。

### 5.4 周波数別トランジェント処理製品

周波数別にトランジェントを観察する価値は確認できるが、最初の版へ帯域 heatmap や high-band 専用値を入れない。
短い窓は時間分解能を上げる一方で低域の扱いを難しくするため、単一素材だけで窓長を決めない。

## 6. 最小製品仕様

### 6.1 主表示

六秒の横時間軸へ、検出されたイベントだけをステムで描画する。
縦軸の中央を `0` とし、上側を `POST > PRE`、下側を `POST < PRE` とする。
ステム長は符号付き Onset Flux 差分を固定 scale へ写した表示値とする。
イベントがない時刻を線で結ばない。
PRE だけまたは POST だけで確認されたイベントは差分へ昇格せず、未対応イベントとして輪郭だけを表示する。

### 6.2 イベント詳細

一つのイベントへ次の四項目を保持する。

| 項目 | 役割 | 時間窓 | 表示 |
|---|---|---:|---|
| Onset Flux | 主指標 | 候補評価後に固定 | 符号付き Δ |
| Crest | 短時間の形状 | 30 ms 基準 | PRE、POST、Δ |
| Sample Peak | 瞬間的な高さ | Crest と同じ 30 ms | PRE、POST、Δ |
| Sharpness | 知覚的な明るさ | 既存定義と同じ 100 ms | PRE、POST、Δ |

`Crest 30 ms` は同じイベント窓の `sample peak dB − RMS dB` と定義する。
Sample Peak は True Peak ではなく、同じ 30 ms 窓内の sample peak とする。
`event_sample`はPhase 2-Rで固定したzero-padded ODF frameのcenterとし、CrestとSample Peakは`[event_sample, event_sample + 30 ms)`を使う。
Sharpness は連続した既存 100 ms endpoint のうち、event_sample との対応誤差が固定上限内にある値だけを関連付ける。
計測 floor 未満で Crest を定義できない場合は未定義のまま保持し、ゼロを発明しない。
Sharpness は既存の 100 ms 定義とチャンネル定義を再利用し、別の意味へ再定義しない。

### 6.3 段階的な表示確定

Onset marker は共通イベント判定が確定した時点で表示する。
Crest と Sample Peak は 30 ms の実測窓が揃った時点で同じイベントへ追記する。
Sharpness は 100 ms の実測窓が揃った時点で同じイベントへ追記する。
Sharpness の完了を待って onset marker を遅らせない。
48 kHz での目標は、annotated acoustic onset から painted marker までの end-to-end P95 表示遅延を 50 ms 以下、最悪値を 75 ms 以下とする。
Crest と Sample Peak の目標表示遅延は 60 ms から 90 ms とする。
Sharpness の目標表示遅延は約 150 ms 以下とする。
これらは UI の観察遅延であり、音声経路へレイテンシーを追加しない。

## 7. Onset 計測候補

### 7.1 基準配置

48 kHz基準windowは1,024または2,048 sample、hopは256 sampleとしてPhase 2-Rで一つに絞る。
44.1から192 kHzへ同じ時間長をhost-native sampleへ決定的に写し、layoutをpayload metadataへ含める。
layoutは純関数としてversion化し、FFT、Hann補償、実現filterbank bin、log、lag、peak ruleをdefinition hashへ含める。
PRE と POST の metadata が一致しない場合は join しない。
Audio Thread では resample しない。

### 7.2 候補式

B-546 Mel 32の旧ruleは診断専用とし、採用候補には30 ms共通peak ruleを適用する。
未達時の主候補はfixed-scale SuperFlux-style、kick-onlyまたはhat-only不足時だけ限定multibandとする。
Complex、Hybrid、HFC、ML、adaptive whiteningを今回の主線にしない。
session最大値、percentile正規化、auto scaleを使わず、固定reference、absolute floor、共通local meanと固定offsetを使う。
公開単位は0から100のscoreにせず、fresh holdout通過後に固定式の単位として確定する。

### 7.3 チャンネル定義

LRのOnset FluxはL/Rを独立STFTし、binごとの補償済みlinear powerを平均してからmagnitude、bank、logへ進む。
Crest、Sample Peak、Sharpness はそれぞれの既存意味を壊さない個別の channel aggregation 順を Phase 1 で固定する。
MID は現行と同じ `(L+R)/2` 波形を解析する。
SIDE は現行と同じ `(L−R)/2` 波形を解析する。
mono の SIDE は fail closed とし、値を発明しない。

## 8. 共通イベント判定

PRE と POST で独立した adaptive threshold と peak picking を行ってはならない。
PRE と POST は、それぞれ連続した exact ODF を同じ定義で算出する。
POST は exact stream を join した後、一つの共通判定系列からイベント時刻を採択する。
共通判定系列の第一候補は、同一 scale 上の `max(PRE ODF, POST ODF)` とする。
共通 threshold、局所最大条件、refractory interval は共通判定系列だけへ適用する。
共通候補ごとに PRE と POST の bounded local maximum を探し、存在 threshold、時間許容幅、one-to-one 対応、tie-break、refractory 内 merge を同じ固定規則で適用する。
両側が対応した場合は双方の実測 endpoint と値を保持し、対応後にだけ符号付き Δ を計算する。
この方式により、処理で弱くなったイベントと処理で新しく強調されたイベントを同じ時間軸で扱う。
PRE と POST の双方を同一イベントとして確認できない場合は、PRE-only または POST-only として残す。
密な roll や重なった kick と hat で対応が曖昧な場合は差分を表示しない。

## 9. 内容時刻の整合

同じ host endpoint でも、間のプラグインの lookahead、内部 latency、DAW の PDC により同じ音響イベントを指さない場合がある。
ATTACK は host endpoint の一致だけで内容時刻の一致を仮定しない。
差分を許可する正本は producer が保持した exact content sample、presentation latency、state epoch とし、既存 `INV-T6` を維持する。
ODF 相関は誤周期へ lock し得るため位置合わせへ使わず、内部診断とテストだけに使う。
exact content mapping が得られない場合は補間、推定、曲線 shift を行わず、差分を表示しない。
transport、sample rate、pair、channel mode、分析定義、latency metadata、worker または exchange generation が変わった場合は alignment を再 arm する。
再 arm 中は queue と pending detail を破棄し、新しい request ID と continuity epoch が両側で一致するまで delta を出さない。
lookahead を含む代表的なプラグインで exact mapping を保持できない場合は公開 No-Go とする。

## 10. 時間軸と状態遷移

計測の正本には exact host sample endpoint を保存する。
六秒表示には別途 monotonic observation sequence position を保存する。
DAW が loop や locate で後方へ移動した場合は新しい計測 run と alignment を開始する。
後方移動前の表示イベントと利用者が選択したイベントは、利用者が消すまで画面上に保持する。
異なる run のイベントを線で接続しない。

| 状態 | 計測 | 表示 | 選択 |
|---|---|---|---|
| 通常再生 | 継続 | 新しいイベントを追加 | 保持 |
| 検証済み無音 | 有効な no-event frame | 既存履歴を保持 | 保持 |
| transport stop | 停止 | 最後の検証済み表示を保持 | 保持 |
| 後方 loop または locate | 新 run と再 alignment | 既存イベントを保持し、新 run を追記 | 保持 |
| 短い観察欠損 | 値を生成しない | 小さな中立 gap metadata と直前の事実を保持 | 保持 |
| 長い観察欠損 | 新 run | 補間せず次の exact event から再開 | 保持 |
| worker または exchange panic、実 drop | 新 continuity epoch で再起動 | 直前の事実を保持し、新 run まで gap | 保持 |
| pair、sample rate、channel 定義変更 | history と alignment を再初期化 | 旧定義の表示を消去 | 消去 |
| DRUMと2MIXの切替 | request retire後に新definitionで開始 | 旧profileの表示を消去 | 消去 |
| ATTACK 以外へ明示切替 | 専用解析を終了 | ATTACK history を終了 | 消去 |
| session reopen | 新規開始 | history を復元しない | 復元しない |
| offline bounce | 対応倍率内だけ sample endpoint 基準で継続 | 対応外は `ATTACK paused` | 保持 |

無音と観察欠損を同じ状態として扱わない。
短い観察欠損では値やイベントを補間せず、欠損した事実だけを metadata として保持する。
古い endpoint の反復受信では hold 期限を延長しない。

## 11. UI 設計

### 11.1 画面構成

既存の POST Analysis navigation に `ATTACK` を追加する。
内部検証中は公開 navigation から隠せる独立 route とし、検出器と alignment の合格後に公開する。
plot上部にASCIIの`DRUM`と`2MIX` selectorを置き、初回は未選択とし、自動切替や推奨表示を行わない。
一方だけが合格したbuildでは未達profileを表示せず、profile切替はleaseを維持したままhistoryをclearする。
画面の主役は六秒のイベントステム plot とする。
常時表示する数値は最小限にし、詳細は hover または click lock で表示する。
クリックしたイベントは固定し、明示的な `x` 操作で解除する。
100% 表示は次の二行の compact readout を基準とする。

```text
ON +1.8   CR -2.6
PK -0.9   SH -0.13
```

150% と 200% では PRE、POST、Δ の内訳を展開する。
100% の compact 値は ON の確定単位、CR と PK の dB、SH の acum を tooltip と accessible text で明示する。
100% でもラベル、符号、単位、選択状態が読めることを pixel test と Windows 実機で確認する。

### 11.2 色と形

上側の `POST > PRE` は既存の ice cyan `#75D6E8` を使う。
下側の `POST < PRE` は既存の amethyst `#A695D6` を使う。
零線は淡い blue-gray とする。
選択イベントは色相を変えず、明度と輪郭 glow だけを上げる。
PRE-only は中立 gray の hollow circle、POST-only は中立 gray の hollow diamond として区別する。
赤、緑、純白、虹色を使わない。
色だけへ意味を依存させず、上側に `POST > PRE`、下側に `POST < PRE` を表示する。
上側 endpoint は丸くし、下側 endpoint は小さな diamond として形状でも区別する。
背景 `#0D0F1A` に対する通常、選択、disabled、gap の contrast を render test で固定する。
palette 値は既存 alias へ集約し、component 内へ個別の hardcode を散らさない。

### 11.3 Hover と文字

event hover readout と click lock は `Show hover help` の設定に影響されない。
`Show hover help` を OFF にした場合は説明 tooltip だけを閉じる。
全 tooltip と readout は 100%、125%、150%、200% の editor 内へ収める。
Windows の code page に依存しない ASCII の UI copy を使う。
未定義値は ASCII `--` とし、文字化けする記号を使わない。
利用者が明示した ATTACK 選択の失敗は事実状態を表示し、利用者操作に紐づかない一時的な内部失敗は R-28 に従って gap として扱う。
ATTACK plot は keyboard focus を受け、Left/Right と Home/End で event を移動し、Enter/Space で lock し、Escape または accessible Clear button で解除できるようにする。
focus は色以外の輪郭でも示し、accessible text は event 時刻、run、PRE、POST、Δ、単位、`PRE ONLY`、`POST ONLY` を伝える。
30 Hz の更新は読み上げず、focus または selection が変わった場合だけ VoiceOver と Narrator へ通知する。
pointer の hit radius は描画 endpoint より広く取り、100% と高 DPI で操作可能な最小値を contract に固定する。

## 12. MVP に入れない機能

- 自動 kick、snare、hat 分類。
- High-band energy の独立履歴。
- 帯域別 transient heatmap。
- Attack と sustain の自動分離表示。
- 0 から 100 の score。
- 良い、悪い、強すぎるなどの評価。
- ジャンル別 target または scale。
- session 最大値に追従する auto scale。
- 音声 delta listen。
- PRE 単独の ATTACK page。

これらは初版の実用性と測定境界を曖昧にするため、MVP の合格後に別提案として再評価する。

## 13. 実装隔離

ATTACK は既存 Analysis へ加える独立 mode とする。
上位の `PostAnalysisRoute` を新設し、旧 wire の `AnalysisViewMode` 値 0、1、2 と request decoder は変更しない。
ATTACK は既存の単一 lease coordinator に所有させ、専用 coordinator を並設しない。
mode 切替は、旧 analyzer 停止、queue drain、generation 更新、旧 request retire、新 analyzer 起動の順に行い、lease ID は維持する。
新しい transient request、ready、payload、cleanup ownership は既存 Spectrum、Perceptual、Absolute から完全に分離する。
macOS は専用 file namespace、Windows は現行 `Local\\KirinHyphaAnalysis-v1-*` と size/layout を共有しない専用 mapping name を使う。
新しい `KirinTransient*V1` は固定幅の `abi_version`、`struct_size`、reserved、count、capacity と status-only 規則を持つ。
Rust `repr(C)` size/offset test と C++ `static_assert` を clang と MSVC の両方で通す。
旧 PRE と新 POST、新 PRE と旧 POST の両作成順で fail closed と cleanup 非干渉を確認する。
既存 FFI struct の field 順、size、offset、公開 symbol を変更しない。
Rust 側は analyzer、event matcher、history、transport を小さな module に分ける。
JUCE 側は transient component、painter、UI contract、render test を既存ページから分離する。
既存 ingress の容量は、benchmark で不足を実測するまで変更しない。
既存 METERS、FREQ、SHARP、LIVE のデータ経路へ ATTACK の状態を混ぜない。
新しい不変条件は `INV-S14` として `docs/hypha_invariants.md` へ固定する。

## 14. 性能予算

ATTACK は一枠につき固定容量の ring buffer と固定容量の event history を使う。
steady state で heap allocation を行わない。
memory 上限を一枠ごとに数値化し、二枠の合計も記録する。
worker の一回の計算時間は hop budget の 25% 未満を目標とする。
二枠同時、192 kHz、長時間 loop、密なドラム素材で analysis ingress drop を 0 とする。
初期 transport 契約は presentation 30 Hz、request renew 500 ms、supervisor 約 0.8 s、verified hold と expiry 1.5 s とし、実測後に設計書へ固定する。
offline bounce は対応可能な最大速度倍率と ring capacity を benchmark で決め、それを超える場合は欠測を隠さず ATTACK を明示 pause する。
UI repaint が停止または遅延しても worker の exact history を失わない。
DAW 表示の 0% は検証根拠にせず、worker 時間、drop count、queue high-water mark、Audio Thread 時間を測定する。
表示遅延は全 rate と二枠条件で P50、P95、P99、max を記録する。
Audio Thread の処理時間と lock-free copy 回数が既存 baseline から増えた場合は No-Go とする。

## 15. 検証計画

### 15.1 実装前 baseline

METERS、FREQ、SHARP、LIVE の golden 値、render image、slot transition、CPU、drop count を保存する。
macOS と Windows の Studio One で、同じ session と test signal を使う。
実装後は同じ baseline を再実行し、ATTACK 以外に差がないことを確認する。

### 15.2 合成信号

- 単発 impulse。
- 低い kick。
- noise burst の snare。
- 高域中心の hat。
- 同時に鳴る kick と hat。
- ghost note と高密度 roll。
- bass pluck と sustained tone。
- tremolo。
- silence と measurement floor 直上の信号。

### 15.3 間に置く処理

- identity pass-through。
- 固定 scalar gain。
- static EQ。
- compressor の attack 変更。
- limiter と lookahead。
- transient shaper。
- fixed delay。
- saturation。

固定 gain の厳密な期待値は Crest Δ が約 0、Sample Peak Δ が gain 量と一致することである。
Onset Flux と Sharpness は floor と非線形性の影響を受けるため、gain/floor sweep から別の数値 tolerance を固定する。
identity の全指標は tolerance 内で Δ 0 とする。

### 15.4 レートとチャンネル

44.1、48、88.2、96、176.4、192 kHz を検証する。
mono、stereo、dual-mono、逆相 stereo、MID、SIDE を検証する。
同じ時間長の onset が sample rate により違う判断へ変わらないことを確認する。

### 15.5 実演奏データ

B-550でDRUMは290 ID、58 ID×5 fold、23列manifest、44.1 kHz整数sample境界、最大30秒の候補非依存hash-windowを固定し、negative excerptも保持する。
B-551で公式MIDI archiveの選択290 member、sourceとexcerpt event、canonical重複0件を同一archive bufferから検証した。
B-552で公式audio archiveの同一full SHA読み取りから選択WAVと対応MIDIを固定し、source、core、maximum-context PCMの重複と無響を検証した。
Evaluator v2の30 ms compound event、±25 ms最大一対一matchingを正本とし、物理source sample 0起点の実contextを解析してcoreだけを採点する。
2MIXのSlakh2100-redux、blind annotation、候補、holdoutは将来の独立契約であり、B-552まで開いていない。
E-GMD、Slakh、その他配布条件を確認していないaudioをrepositoryへcommitしない。
CIには再配布可能な生成fixtureだけを入れる。
`transient_delta_phase2_recovery_plan_20260830.md`のgrouped development、性能成立性、二つのfresh holdoutで公開可否を決める。

### 15.6 自動テスト

- ODF、Crest、Peak、Sharpness の式と floor の unit test。
- identity、gain、delay の property test と random test。
- 共通 event decision と曖昧 matching の test。
- exact content mapping の欠落、不一致、変更と、ODF 相関だけでは delta を許可しない test。
- transport 後退、loop、stop、silence、短い gap、長い gap の state test。
- PRE worker、POST worker、exchange の個別 panic、restart、実 overflow と復帰の test。
- payload corruption、schema mismatch、non-finite、oversize、formal source-pin、candidate-plan欠落、context-guard未実装の negative test。
- 二枠 ownership と全 page transition の exhaustive test。
- 100%、125%、150%、200% の render、contrast、tooltip containment test。
- Windows の ASCII copy、文字化け、stem 消失、DPI scale の render test。
- keyboard、VoiceOver、Narrator、focus、hit radius、PRE-only、POST-only の interaction test。
- macOS file failure と Windows mapping open、write、contention、stale generation の test。
- offline bounce の sample endpoint test。

公開 navigation へ追加する前に、`FREQ↔SHARP`、`FREQ↔LIVE`、`FREQ↔ATTACK`、`SHARP↔LIVE`、`SHARP↔ATTACK`、`LIVE↔ATTACK`、各 page と `METERS`、editor の close と reopen を確認する。
Rust 変更後は `cargo test --workspace` と `cargo clippy` を通す。
`kirin_hypha_ffi` を変更した場合は ignored parity 20 件と pairing candidates 5 件も直列で通す。

## 16. 実装段階

### Phase 0: 既存機能の正本化

既存四画面の baseline、性能、render、slot transition を保存する。
ATTACK の追加で変化してはならない file と symbol を列挙する。

### Phase 1: 設計契約

`docs/transient_delta_design.md` と `INV-S14` を追加する。
式、単位、floor、event support、exact content mapping、channel aggregation、route、state transition、OS 別 namespace、FFI payload version を固定する。

### Phase 2: Offline 候補評価

B-552までに整数時刻の30 ms compound event、厳密±25 ms最大一対一matching、N=290 fold gate、23列formal manifest、MIDIとaudio provenance、SuperFlux候補config、macroとworst-fold集計を実装した。
formal authorizationのsource pin、blind audit、context guard、sealed candidate setが未成立なのでscoreとwinnerは存在しない。2MIXは設計だけでdata未着手である。
窓、hop、bank、lag、floor、共通peak、固定scale、性能成立性、gap期限、offline倍率をfresh holdout前に決定する。
この Phase が終わるまで公開 UI の scale と Onset 単位を固定しない。

### Phase 3: 独立 worker analyzer

Audio Thread を変更せず、既存 ingress のコピーを読み、既存単一 coordinator に所有される専用 analyzer を実装する。
連続 exact ODF、30 ms detail、100 ms Sharpness、fixed history を生成する。

### Phase 4: PRE/POST alignment と共通判定

exact content mapping、common event decision、PRE-only、POST-only、fail-closed を実装する。
dense rhythm と lookahead を通過するまで UI へ public delta を出さない。

### Phase 5: Versioned transport と FFI

専用 payload、batch history、gap metadata、diagnostic counter を追加する。
専用 request、ready、cleanup と OS 別 namespace を追加し、既存 ABI、旧 binary、両起動順の fail-closed を検証する。

### Phase 6: 最小 UI

内部 route へ六秒 stem plot、二色、hover、click lock、段階的 readout を実装する。
この段階では新しい表示を追加せず、MVP 四指標だけを使う。

### Phase 7: Robustness と性能

全 state、二枠、sample rate、Windows shared mapping、macOS transport、offline bounce、worker restart を検証する。
二枠 192 kHz の性能 gate と既存画面の回帰 gate を通す。

### Phase 8: 実機評価と公開判断

macOS と Windows の Studio One で kick、snare、hat、mix bus、lookahead plugin を検証する。
100% の可読性、表示遅延、stem の欠損、選択保持を動画で確認する。
No-Go 条件が一つでも残る場合は公開 navigation へ追加しない。

### Phase 9: 公開資料

合格後に README、画像、短い実使用動画、release note を同じ仕様へ更新する。
処理機能や楽器分類と誤解されないよう、POST 限定の観察機能であることを明記する。

## 17. Go と No-Go

### Go 条件

- 対象profileのfresh holdoutでPhase 2-Rに固定したonset、timing、false-matchを満たし、DRUMはkick/hat gateも満たす。
- identity と固定 gain の期待値を満たす。
- lookahead と fixed delay で内容時刻の対応を誤らない。
- silence と観察欠損を区別できる。
- 48 kHz の end-to-end 表示遅延が P95 50 ms 以下、max 75 ms 以下になる。
- 二枠 192 kHz で drop 0 を維持する。
- METERS、FREQ、SHARP、LIVE の baseline に回帰がない。
- Windows で stem、文字、tooltip、選択表示が欠けない。
- 100% 表示で基本情報を読める。

### No-Go 条件

- 対象profileのfresh holdoutまたはpaired transform gateが一つでも未達になる。
- dense event を別の onset と誤対応する。
- lookahead で false delta を表示する。
- 観察欠損を無音または event 0 として表示する。
- 48 kHz の end-to-end 表示遅延が P95 50 ms または max 75 ms を超える。
- 二枠使用時に ingress drop が発生する。
- 既存 Analysis または METERS に回帰が出る。
- Windows で stem が消える、途切れる、文字化けする。
- Audio Thread の作業量が増える。

## 18. Rollback

ATTACK は別 mode、別 module、別 payload、別 UI component として追加する。
No-Go の場合は公開 navigation と ATTACK request を無効化し、新規 module を外すだけで既存四画面へ戻せる構造にする。
既存 enum 値、FFI layout、transport schema を上書きしないため、rollback 後も session と旧 binary の互換性を維持する。
公開前は default-OFF の単一 build capability を Rust request validator、C ABI、JUCE route の全層へ伝播する。
gate OFF では route、request、worker、lease acquisition を全て不成立にし、既存 symbol、layout、test の golden 一致を専用 CI で確認する。
ATTACK の未公開状態を DAW state へ保存せず、runtime OFF を設ける場合は METERS 復帰、request retire、history clear、lease 解放を一操作で行う。

## 19. 実装前に数値で確定する項目

次の項目は推測で固定せず、Phase 2 の比較結果と fixture を設計書へ残す。

- profileごとに採用するODFをMel 32 v2またはfixed-scale SuperFlux-styleから選び、限定multibandはDRUMだけに許可する。
- filterbankと実現band数。
- 窓、hop、refractory interval。
- fixed threshold と absolute floor。
- Onset Flux の単位と表示 full-scale。
- common decision trace の式。
- exact content mapping の metadata 経路と許容誤差。
- PRE-only と POST-only の時間許容幅。
- short gap、long gap、publish、hold、expiry の期限。
- offline bounce の最大対応倍率と容量。
- holdout の precision、recall、timing error、false-match 合格値。
- public navigation の並び順。

これらを確定できない場合は実装を次段階へ進めない。

## 20. 予定成果物

- `docs/transient_delta_design.md`。
- `docs/hypha_invariants.md` の `INV-S14`。
- Offline candidate report と再現 fixture。
- Rust の transient analyzer、aligner、history、transport module。
- Versioned FFI contract と negative tests。
- JUCE の ATTACK component、painter、UI contract、render tests。
- macOS と Windows の Studio One 検証記録。
- Windows 正式サポート用の CI、pluginval、実機、同一 commit artifact チェックリスト。
- 性能測定結果と Go または No-Go 判定。
- 合格後の README、画像、短い動画、release note。

## 21. 参考資料

- [EVERTONE PLUGINS](https://evertone.jp/plugins.html)
- [EVERTONE COMPRESSOR V2](https://evertone.jp/compressorv2.html)
- [FVscope](https://evertone.jp/fvscope.html)
- [Essentia Streaming OnsetDetection reference](https://essentia.upf.edu/reference/streaming_OnsetDetection.html)
- [Essentia onset detection tutorial](https://essentia.upf.edu/tutorial_rhythm_onsetdetection.html)
- [librosa onset strength reference](https://librosa.org/doc/0.11.0/generated/librosa.onset.onset_strength.html)
- [Bello et al., A Tutorial on Onset Detection in Music Signals](https://hans.fugal.net/comps/papers/bello_2005.pdf)
- [oeksound spiff manual](https://oeksound.com/manuals/spiff/)
- [Expanded Groove MIDI Dataset](https://magenta.withgoogle.com/oaf-drums)

## 22. 一文の指針

Hypha の ATTACK は、音を分類する画面ではなく、同じトランジェントが処理の前後でどう変わったかを、欠損も誤差も隠さず操作直後に見せる画面とする。
