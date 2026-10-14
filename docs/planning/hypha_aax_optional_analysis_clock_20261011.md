# AAX の SHARP / LIVE / PSB と解析時計

2026-10-12追記: 認定済みStudio Pro VST3のFREQ / SHARP / 差分PSBは、
[PDC比較時計の修正](hypha_vst3_pdc_delta_20261012.md)で独立したcontent時計へ移した。
以下のAAX規則、AU、未認定VST3の規則は維持する。

## 原因

AAX の通常 Native callback は、既存の `0011-aax-engine-clock.patch` により
`AddClock()` の `AAX_CTimestamp` を JUCE の auxiliary source 3 として渡す。
`PluginProcessorHostClock.cpp` はこれを保持するが、output presentation latency の
source は VST3 / AU v2 のみを受理する。AAX の capture window は output latency が
未確定のため、従来の `optional_analysis_clock.rs` は LocalProject を選んでいた。
`spectrum_runtime_ingress.rs` は、この時計で FREQ と Absolute の局所観測を許可する。
LIVE、POST 単体の Sharpness、絶対 PSB は出力の遅れの通知を必要としない。
SHARP の Δ と PSB の Δ は Perceptual の整列条件を維持し、未整列座標を拒否する。

AAX の `GetSignalLatency()` は、そのプラグイン自身の algorithmic delay を返す。
Hypha 自身の 0 samples を、出力までの累積 presentation latency として使えない。
Hybrid の遅延も通常 Native の presentation latency の代用にならない。
公式資料で確認した今回の Native API 経路には、AU v2 の output presentation latency
と同じ通知は見つからない。

- [Avid: AddClock](https://learn-cdn.avid.com/AAX_SDK_2p1p1/Documentation/Doxygen/output/html/a00065.html)
- [Avid: controller / GetSignalLatency](https://learn-cdn.avid.com/AAX_SDK_2p1p1/Documentation/Doxygen/output/html/a00066.html)
- [Avid: Hybrid processing](https://learn-cdn.avid.com/AAX_SDK_2p1p1/Documentation/Doxygen/output/html/a00251.html)

AU v2 は `kAudioUnitProperty_PresentationLatency` を host から output scope で受け取り、
秒から samples へ変換する既存 patch がある。Logic ではこの通知を使えるため、従来の
presentation 時計で解析できる。REAPER の VST3 で同等の通知が得られない場合も、
project timeline で LIVE と絶対 PSB は動く。SHARP の Δ と PSB の Δ は利用不可とする。
AddClock を presentation latency 0 と見なす変更はしない。

## 時計の証明と実装

時計の認定は Pro Tools の AAX 全体（実行ファイル名 `Pro Tools` / `ProTools` /
`Pro Tools Developer`、版 12.6 以降）に与える。実測したのは Pro Tools Developer 26.4.0.5 で、
他の版は版の下限で認定する（`measured` の印は 26.4.0.5 だけ）。下限の根拠は、公式の
遅延補償の上限（44.1/48 kHz で 16,383、88.2/96 kHz で 32,767、176.4/192 kHz で 65,534）と、
INV-LC8 が依る遅延補償 ON/OFF の通知（12.6 以降）の両方が成り立つ版であること。
sample rate の認定は拡大しない。SHARP の Δ の経路は認定の有無に依らず全 AAX host が使う。
認定は「loop 中の初回 K を遅延上限で認める」権限であり、Pro Tools 以外の AAX host は
LISTEN と同じ直線再生の線形 K だけで結合する。AddClock 不在、非連続、bypass、offline は
値を出さない。上限を超える遅延の chain を loop 中に測ると隣の周回と対になり得るが、
既存の内容の突き合わせ（content-jump hold）は差ではなく変化だけを見るので止められない。
これは Pro Tools が補償できない chain に限る既知の危険として残す。

- LIVE と絶対 PSB は AAX でも常に project timeline の経路を使う。engine 時計の空白や loop の
  設定で止まらず、pair の証明も要らない。
- PRE の SHARP 解析は連続 AddClock の局所時計を使う（認定は不要）。
- POST の SHARP / 差分 PSB は、LISTEN / BLIND が既に使う `TimingPreparation` の
  同一 callback・同一 PRE snapshot からの `TimingObserver` の証明を使う。
  PRE の owner / generation、時計種別、連続性、定常区間の K、callback の空白を維持する。
  Pro Tools（12.6 以降）は遅延上限内の K（初回 loop を含む）を、Pro Tools 以外の AAX host は
  `kind == linear` かつ両側が loop でなく `K >= 0` の K だけを受理する。POST の解析座標は `POST.engine - K`。
  同じ engine 数値が来たことだけでは差分を許可しない。
- compensation OFF、pair 失効、未確定の K、読み取り失敗は POST 差分を許可しない。
  LIVE は他側との対応を必要としない。
- Pro Tools は transport の位置を cycle ごとに一つ出し、insert の位置はその前段の遅延 K だけ
  補償して減らすので、範囲ありの loop では折り返しの後 K sample のあいだ POST の位置が loop の
  開始より前になる。比較の block ではこれを前の周回の末尾として読み替え（`PluginProcessorLiveCompareRealtime.cpp`、
  AAX だけ、capture / Record の位置は host のまま）、POST の折り返しは PRE の K sample 後に
  起きるものとして pair の証明・loop cycle・ring の anchor と一致させる。
- host の loop 再生の設定だけで範囲（選択）が無い block は直線再生として扱う。実際の折り返しは
  project の線形性で必ず失効し、再証明の 8 block は POST を出し差分を出さない。範囲のある loop を
  Pro Tools 以外の AAX host が再生している間は、LISTEN / BLIND / SHARP が
  「Loop Playback on: turn it off to compare」と案内して POST を出す。
- proof revision と runtime stream generation が変わると、履歴と公開済み表示を失効する。
  全 AAX host が Perceptual だけ専用経路を使い、証明を一時的に失った callback は
  `note_drop` と同じ履歴失効で `WARMING_UP = 2` と `has_data = 0` を返す。
  `OUTPUT_LATENCY_UNAVAILABLE = 6` は AU / VST3 の Perceptual が未整列座標を拒否した場合だけに残す。
  復帰しても以前の値を流用せず、解析を準備し直す。

通常の測定入力の責務を `PluginProcessorMeasurementIngress.cpp` へ抽出し、AAX だけは
時計観測の後に追加 FFI で任意解析を投入する。入力は callback 冒頭で採った既存 scratch
の原音であり、LISTEN / BLIND / Reference の出力を測らない。FFI は当該 callback の
受理済み frames / channels を一度だけ消費する。Watch / Record / ATTACK を二重に進めず、
Audio Thread で alloc / lock / I/O を増やさない。通常 A の bit identity と latency 0 を保つ。

解析座標には presentation と別の `AaxEngine` 種別を付ける。PRE の私的な SHARP snapshot
にも種別を付け、presentation と engine を混ぜて結合しない。私的な交換 magic は更新するが、
C の既存構造体、plugin state、work / Record の公開 schema は変えない。
v1.1.50 の保存状態の読み込み経路も変更しない。FREQ の既存交換は新種別を受け付けない。
AU / VST3 は既存 ingress から Absolute に局所座標も渡せる。Perceptual の整列条件と
AAX の認定時計は維持し、PRE / POST、macOS / Windows は共通実装を使う。
実 wrapper と Windows 実行は、共通 processor fixture の合格とは別の受入工程とする。

## 再現・回帰試験

新規の native product fixture は、presentation latency が無い AAX playhead、原点が
project と異なる AddClock、K = 1024 samples、mono / stereo を使う。修正前は LIVE と
絶対 PSB が準備中から進まないことを確認する。修正後は LIVE、SHARP、両 PSB、時計欠損と
復帰、認定外の差分の利用不可、原音の bit identity と latency 0 を確認する。
この fixture は実際の PRE timing shared-memory mapping を必要とする。

Rust 試験では、実 worker の解析、PRE/POST の共通 future epoch、同種時計だけの結合、
異種時計の両方向の拒否、revision による古い表示の失効、FFI の重複・null・frames/channels
不一致・revision 境界・PRE/POST 切替を検証する。native の時計試験では既存 observer を
使い、非ゼロ K、0 / 上限 / 範囲外 K、clock 欠損・復帰、seek、認定外を確認する。
既存 LISTEN / BLIND の native 試験と Rust / source 検証も併せて実行する。

## Pro Tools での受入手順

未署名 AAX を Developer host の検証用に別名で用意する。通常配布物や既存配置の置換は
別工程とする。PRE / POST とも同じ検証 build を読み込んだことを確認する。

1. Pro Tools Developer 26.4.0.5、48 kHz、stereo Aux、同じトラックに PRE → POST。
   Delay Compensation を ON にし、連続する既知の stereo 信号を再生する。
   PRE の選択と既存 LEVEL / TIME / FREQ / DRUM の動作を確認する。
2. LIVE を開き、LUFS-M / true peak 等の値と絶対 PSB が更新され、25 秒後も準備中に
   留まらないことを確認する。SHARP へ切り替え、値と差分 PSB が同様に更新されることを
   確認する。画面を閉じて開き直し、各 mode を繰り返す。
3. PRE と POST の間が無加工なら差分がほぼ 0。次に遅延を正しく報告する既知の delay
   を間へ挿し、音声は同一のまま、定常再生で SHARP / 差分 PSB が再びほぼ 0 となることを
   確認する。単純な clock 数値の一致による誤った結合がないことを見る。
4. stop / play、seek、通常の loop、PRE の削除・再挿入、pair 変更を行う。境界で古い値を
   出さず、証明と解析の準備後に新しい値へ戻ることを確認する。遅延上限を跨ぐ短い loop
   や説明できない時刻は無理に数値化しない。
5. Delay Compensation を OFF にして、SHARP / 差分 PSB が準備中となり、古い値が
   残らないことを確認する。ON へ戻し、新しい証明で復帰することを見る。LIVE の絶対観測と
   差分の条件を区別する。offline / bypass でも比較時刻を捏造しない。
6. LISTEN / BLIND を従来どおり試す。任意解析は原音から取り、試聴の切替で解析値が別の
   音源へ化けないことを確認する。通常計測は入出力 bit identical、0 samples のまま。
7. 保存して再 open し、自動で試聴へ切り替わらないこと、pair と解析 demand が従来の
   復元契約に従うことを確認する。mono、他の認定済み sample rate は別に受入する。

実機の合格には対象 host / version、source commit と未コミット差分の識別、PRE / POST
payload の hash、rate / block size / chain delay、上記の結果を紐付ける。実 host の受入前に
Pro Tools 対応完了や release gate 合格を宣言しない。
