# Kirin Hypha current implementation audit

Status: verified implementation snapshot for redesign

Date: 2026-08-31

Inspection time: 16:21 JST

## 1. Scope

Meter再設計が古いHypha像を前提にしないよう、公開系と完了したATTACK系を分けて調査した。

この文書は、画面構造を維持するための仕様書ではない。

再設計で再利用する計測契約と、置き換え可能なpresentationを区別するための監査記録である。

調査はread-onlyで行い、ATTACK worktree、submodule、build成果物、VST3配置へ変更を加えていない。

## 2. Audited baselines

| Baseline | Path | Commit | State |
|---|---|---|---|
| Public and release line | `/Users/nishiodaisuke/Dev/kirin_hypha` | `734a72ac17cb113b3ea4ec2da58150a3f39e2ddb` | `[B-552] Bind PRE display to one Work and runtime` |
| Meter design | `/Users/nishiodaisuke/Dev/kirin_hypha_meter` | merge in progress at this update | isolated integration branch |
| ATTACK development | `/Users/nishiodaisuke/Dev/kirin_hypha_perceptual_continuous` | `d464f71c8426cb859a4076f3aa055fd60b21d553` | `[B-580] Make ATTACK UI contract Windows-safe` |

公開系とATTACK系のmerge baseは`cf2acd59c796258454c817f788a2dc42e8ead61f`である。

公開系にはrelease 1.1.46、1.1.47、PRE表示bindingの独自変更がある。

ATTACK系にはFREQ操作、host clock、DRUM ATTACK検出、event timeline、perceptual detail、scrub presentation、通常Analysis routeへの昇格の独自変更がある。

両者はfast-forward関係ではないため、Meter branchを一方へrebaseしても全実装を取り込んだことにはならない。

ATTACKのsource変更は`505438c`と`d464f71`へcommitされ、親repositoryには未コミットのsource fileがない。

JUCE submoduleはmodified表示であるが、`verify_juce_patch_state.sh`によりupstream `4f43011b`へ追跡済み7 patchを適用した状態と確認した。

Meter branchはATTACK worktreeのsubmoduleをcopyせず、ATTACK commitと追跡済みpatch fileだけを統合する。

## 3. Audio processing boundary

`juce_shell/src/PluginProcessor.cpp`の`processBlock()`はmonoまたはstereoの同一入出力layoutだけを許可する。

通常のsignal channelへ書き込まず、入力のない余剰output channelだけをclearする。

計測用sampleは`getReadPointer()`から事前確保済みinterleave scratchへcopyする。

Audio Thread上のwrite enableはatomic通知へ留まり、実際のI/O enableはmessage-thread Timerへ遅延する。

optional SpectrumとATTACKもvisibleかつenabledの場合だけ、lock-free runtimeへsampleを渡す。

この構造は再設計後も保持する実装契約である。

GUI router、metric配置、tab名はAudio Thread契約ではない。

## 4. Signal state

C ABIの実値は次のとおりである。

| State | Value | Presentation meaning |
|---|---:|---|
| Inactive | 0 | transport停止または測定対象外 |
| Active | 1 | 測定可能 |
| Bypassed | 2 | explicit bypass |

Watchは3秒のsilence gate内の短いmusical restで測定continuityを保つ。

heartbeat-awareな状態取得により、`processBlock()`が止まったhostでも古いActive表示を残さない。

非ActiveからActiveへ戻る境界は測定passの開始として扱われ、engine残留状態を持ち越さない契約がある。

AGENTS.md内のSignalState表は値の並びが異なるため、実装時はC ABI headerと`PluginProcessor.cpp`を正本にする。

## 5. Current metric payloads

`KirinMeasureResult`は次の値を持つ。

| Field group | Current semantics |
|---|---|
| `lufs_m` | Momentary、400 ms |
| `lufs_s` | Short-term、3 s |
| `true_peak` | recent TP、400 ms |
| `tp_session_max` | engine init以降のrunning MaxTP |
| `crest` | current Crest |
| `psr` | 3 s PSR |
| `sharpness` | Sharpness |
| `n_prime_total` | total specific loudness |
| `psb_low/mid/high` | three-band PSB |
| `n_prime[20]` | 20 Bark specific loudness |
| `psb_bark[20]` | 20-band PSB |
| `dropped_samples` | measurement ring overflow count |

`KirinWatchDisplay`は同じ`KirinMeasureResult`の`current`と`maximum`を持つ。

`KirinSessionSummary`は`lufs_i`、`lra`、`max_true_peak`を持つが、C headerではRecord finalize後の集計と定義されている。

したがって、常時メーター用IとLRAを現行Summaryからそのまま表示すると、RecordとMeter Sessionの意味が混ざる。

plugin dataはI、LRA、PLRを保持し、PLRを`MaxTP − I`として算出する。

PLRは現行のGUI向けC ABI payloadにはない。

per-channel sample peak、per-channel true peak、correlation、balance、clip event、長時間historyも現行payloadにはない。

## 6. Current Meters presentation

通常Editorは300×200である。

Watchは2列3行のgridを使う。

| Cell | Current Watch display |
|---:|---|
| 0 | 選択中のMまたはS |
| 1 | 同じloudnessのmaximum |
| 2 | recent TP |
| 3 | TP maximum |
| 4 | Crest |
| 5 | Crest maximum |

Record表示は同じgridへ別の値を割り当てる。

| Cell | Current Record display |
|---:|---|
| 0 | 選択中のMまたはS |
| 1 | PSR |
| 2 | MaxTP |
| 3 | I |
| 4 | Crest |
| 5 | Sharpness |

LRAはSessionSummaryに存在するが、この6-cell Record表示には出ない。

PSRはWatchで計測されるがWatch gridには出ない。

現行POSTはpair identityがあるとWatchをΔ layoutへ切り替える。

短いPRE idle、stale、transport gapではΔ layoutを保ち、値をmuteまたはunavailableにする。

explicit PRE bypassだけがpaired POSTをabsolute layoutへ戻し、pair statusを`ABS`として示す。

POSTまたはΔを利用者が独立して選ぶglobal perspective controlはない。

## 7. Current Analysis presentation

公開系POST AnalysisはFREQ、SHARP、LIVEを持つ。

ATTACK完了点`d464f71`はDRUM ATTACKを通常Analysisへ昇格し、Analysisを開くとATTACKへ入り、`ATTACK → FREQ → SHARP → LIVE`を循環する。

ATTACK初回表示は200%を使い、環境変数は直接起動する検証shortcutとして残る。

現在の上位遷移は`METERS ↔ ANALYSIS`である。

この順序と名称は進行中のpresentationであり、計測契約ではない。

| View | Verified current behavior |
|---|---|
| FREQ | exact endpointで結合したsigned POST − PRE spectrum、PREとPOST reference、LR/MID/SIDE、probe、MARK、Focus Trail |
| SHARP | signed Sharpness Δのexact six-second timeline |
| LIVE | POST-only M、recent TP、Sharpnessを同一100 ms endpointで示すsix-second timeline |
| ATTACK | exact event timelineとscrub、pair時はPRE/POST、未接続時はPOST ABSOLUTE |

FREQは48 kHzで4096 sample apertureと8192 FFTを使う。

host sample rateに合わせてaperture時間を正規化し、host sampleをresampleしない。

256 band、30 Hz analysis source、12 Hz curve presentation、2 Hz numeric presentationを使う。

低域の観測cycle不足はapproximate frequencyとして表示し、存在しないFFT pointを作らない。

SHARPは10 Hz source、64 point payload、60 pointの六秒表示、5 Hz curve、2 Hz numeric presentationである。

LIVEも10 Hz source、64 point payload、六秒表示、5 Hz curve、2 Hz numeric presentationである。

LIVEの三値は一つの時刻を共有するが単位と値域を共有しない。

## 8. ATTACK completion snapshot

committed enumは既存値を保って末尾へATTACKを追加している。

| AnalysisViewMode | Value |
|---|---:|
| Spectrum | 0 |
| Perceptual | 1 |
| Absolute | 2 |
| Attack | 3 |

ATTACKの固定容量はraw ODF 64、event 240、waveform 600、detail 240、shape 96、pair event 240である。

ATTACK workerとpayloadはUI routeから分離されている。

Audio Threadはenableされた専用runtimeへだけsampleを渡し、event決定とdetail算出をworker側で行う。

ATTACKのvisual contractはinstrument classificationを禁止する。

完了baselineはSTRENGTH、BRIGHTNESS、TRANSIENT、TEXTUREを、価値判断なしの固定paletteと固定scaleで表示する。

専用PainterはLIVEとLOCK、drag scrub、二段とoverlayを実装する。

Windows向けUI contract testは大きなfixtureをheapへ移し、stack size差による失敗を避けている。

ATTACK、FREQ、SHARP、LIVEは一つのAnalysis leaseを共有し、METERSへ戻るときだけ解放する。

2MIXはATTACK DRUMとは別profileとされ、精度契約が確定するまでATTACK routeへ入れない。

Meter再設計はATTACK worker、event定義、固定容量、pair payloadを再利用できる。

一方、DRUMを最初に開く現在のrouteを保持する必要はない。

## 9. Analysis resource coordination

optional Analysisはprocess単位で安定した2枠を持つ。

三つ目のinstanceは解析を開始せず、取得済みowner名が検証できれば表示する。

FREQ、SHARP、LIVE、ATTACKは同一instance内で一つだけ有効になる。

macOSのcross-instance exchangeはatomic file、Windowsはpagefile-backed mappingを使う。

既存Watch、Record、plugin dataのfile contractはoptional Analysis exchangeと分離されている。

新しい`LEVEL / TIME / FREQ / SPACE` routerは、このcoordinatorを上位domainから操作する必要がある。

tabごとに独立したlease処理を追加すると、2枠のownershipが分裂するため採用しない。

## 10. Current sizing and visual language

Metersは300×200固定である。

Analysisだけが300×200、375×250、450×300、600×400の4 presetを持つ。

現行paletteはnear-black背景、ivory系normal、amber flora、cyan spectrum delta、slate PRE、lavender POSTを使う。

macOSは`.SF NS`と`.SF NS Mono`、WindowsはSegoe UIとConsolasを使う。

ATTACK baselineに含まれる現行FREQ、SHARP、LIVEの実画面は次を参照する。

- `docs/media/kirin-hypha-freq.jpg`
- `docs/media/kirin-hypha-sharp.jpg`
- `docs/media/kirin-hypha-live.jpg`

既存の暗い菌糸背景と色は再利用候補である。

しかし、固定Metersと可変Analysisを分けるvisual shellは根本再設計の対象である。

## 11. Persisted state and compatibility

現行JUCE stateはinstance ID、project UUID、DAW session UUID、name、pair name、exact pair locator、M/S表示選択を保存する。

旧nih-plug VST3 JSON stateを一度だけdecodeするmigrationも存在する。

Analysis pageとsizeは現行計測identityの正本ではない。

新routerを追加するときは既存identityとpair fieldsを変更せず、display stateを末尾追加する。

不正値と旧stateは一つの既定domainへfallbackさせる。

## 12. Current PRE Display transport

現行Hyphaは、Kirin OSのINSPECTとMASKINGから構造化Guideを受け取るPRE専用subsystemを持つ。

Kirin OSは保存済みWorkと利用者が確認したPRE一台をlocalにbindingする。

Guide transportはpresence、capability、connection、active pointer、artifact、acknowledgementを分離する。

artifactは最大1 MiB、itemは最大2048件であり、active pointerのSHA-256とartifact bytesを照合する。

破損artifactはrejectし、直前の完全Guideを表示し続ける。

GuideはEndまたはreplaceまで保持され、再生停止とlease失効だけでは消えない。

INSPECT payloadは選択した一件のeventを持つ。

eventは表示名、source、channel、start、end、optional bandを持ち、durationのないpointは1 nsのwire sentinelで区別する。

MASKING payloadはsource pair、review selection、frequency focus、実測collision interval、frequency state、optional measured band、frequency basisを持つ。

Kirin OSの現行MASKING互換fixtureはprotocol v1.1であり、review selectionと各intervalの`selection_ref`を含む。

2026-08-31の再監査ではHypha側fixtureとparserがv1.0のまま止まっていたため、Kirin OS側1,907 bytes、SHA-256 `f8176aecd61cab9c553861629af2164af6f9a6fce3e860f1f50202bf7b6e8f73`へbyte同期し、v1.1のsemantic validationを復旧した。

Hyphaはaudio threadが公開したproject clockをworker threadで読み、Guide timeを`RECEIVED`、`NEXT`、`CUE`、`ACTIVE`、`HELD`、`END`、`PAUSED`へ投影する。

現在のGUIへ渡す`DisplaySnapshot`は、projection結果をprimary、detail、stateTextの二行相当へ圧縮する。

検証済みGuideModelには時刻と帯域が型付きで残るが、GUIはその構造をTIMEまたはFREQへ描画していない。

現行不変条件はPRE targetだけを許可し、POST、Watch、Record、Keep、PairingからGuide transportを分離する。

このPRE限定は当時の画面余地に基づくproduct routeであり、transportの安全性そのものには必要ない。

## 13. Redesign integration boundary

次の実装はそのまま保持する。

- R-12 read-only passthrough
- mono and stereo layout restriction
- heartbeat-aware signal state
- Watch silence gate and engine reset boundary
- M、S、TP、Crest、PSR、Sharpness、I、LRA、MaxTPの定義
- exact presentation endpoint and missing-data semantics
- Spectrum layout and exact PRE/POST join
- ATTACK event definition and fixed-capacity payloads after the ATTACK branch is committed
- process-wide two-slot Analysis coordinator
- plugin data and pairing schemas

次のpresentationは根本から置き換えられる。

- `METERS / ANALYSIS`二分法
- `ATTACK / FREQ / SHARP / LIVE`の循環順
- Analysisを開いたときにATTACKを最初に出すroute
- Metersだけ300×200に固定するsize model
- pair接続によるΔ layoutへの強制切替
- WatchとRecordで同じ6-cell gridへ別metricを詰める構成
- LIVEを独立pageとして置く構成
- Guideの受信先をPREだけに限定する構成
- 構造化Guideを二行テキストだけへ圧縮する構成

## 14. Safe next step

ATTACK完了点を公開系へ統合したMeter branchで、PREとPOSTの4サイズwireframeと新しいrouter contractを作る。

統合baseline上で計測payloadを`MeterSnapshot`へ整理し、既存Analysis workerを`LEVEL / TIME / FREQ / SPACE`から呼び出す。

Kirin OSからPOSTへGuideを送る計画は、`MeterSnapshot`と分離した`GuidePresentationSnapshot`として同じ親Shellへ接続する。

POST版の移行Gateが成立するまで現行PRE Guideを維持し、ATTACKの計測資産を旧navigationへ固定せずにMeter本体とGuideを進める。
