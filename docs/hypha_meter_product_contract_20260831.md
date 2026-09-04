# Kirin Hypha Meter product contract

Status: implemented development baseline; release conformance and licensed Kimera artifact pending

Date: 2026-08-31

Branch: `codex/hypha-meter`

Public baseline: `734a72ac17cb113b3ea4ec2da58150a3f39e2ddb`

ATTACK baseline: `d464f71c8426cb859a4076f3aa055fd60b21d553` (`[B-580] Make ATTACK UI contract Windows-safe`)

## 1. Product decision

POSTを2MIXの最終段に常設したとき、Hyphaだけで日常的なメータリングを完結できる状態を作る。

現在の`METERS / ANALYSIS`、`ATTACK / FREQ / SHARP / LIVE`、300×200固定Metersは完成仕様ではない。

既存の画面遷移を互換性のために温存せず、情報設計とvisual shellを根本から再構成する。

ただし、通常のA経路の音声非加工、計測式、exact endpoint、固定容量payload、解析資源制御は実装資産として保持する。

添付されたConcept C Hybrid Observatoryを、情報密度、階層、色、菌糸の抑制量を決める視覚基準にする。

Kirin OSのINSPECTとMASKINGから送るGuideは、常時表示されるPOSTを主送信先とし、親Shellのcontext layerへ統合する。

ATTACK統合後のworkspace testがgreenになったため、Daisukeの2026-08-31の判断に従ってMeter本体と共同で進める。

精度を装飾で演出するのではなく、単位、時間窓、軸、状態、測定時刻を美しく組み立てる。

この文書は実装済みの製品契約であり、B-590からB-605の実装と今後の公開判定を拘束する。

## 2. Isolation boundary

本作業は専用worktree `/Users/nishiodaisuke/Dev/kirin_hypha_meter` と専用ブランチ `codex/hypha-meter`だけで行う。

ATTACKセッションが使用するworktree、ブランチ、submodule、build成果物、VST3配置先には触れない。

ATTACKの完了commitはGitの三者マージでこのブランチへ統合し、ATTACK worktreeの作業中submoduleや生成物をcopyしない。

共用のStudio Oneプラグイン配置とリリース操作は、この設計統合では行わない。

## 3. Preserved engineering contracts

### 3.1 Audio boundary

R-12を維持する。

通常のPRE/POST計測では音声信号を生成、変更、減衰、遅延しない。

通常計測時のAudio Threadは入力の読み取り、事前確保済みバッファへのコピー、atomic通知だけを行う。

FFT、履歴集計、画像生成、ファイル保存、UI描画はAudio Threadで行わない。

メーター機能の追加後も、通常のA経路はレイテンシー0 samples、PREとPOSTの音声差分はbit identicalを
合格条件とする。

利用者が明示的に開始するReference比較試聴は、R-12で禁止する音声生成・加工には含めない。
登録済みの不変なReferenceを試聴専用B経路で再生し、試聴コピーにだけ一時的なGain Matchを適用できる。
Referenceファイル、通常のA経路、正本のPRE/POST測定・Recordは変更しない。

B経路は接続、Reference読込、project復元だけでは有効化しない。offline render、Reference欠損、
identity検証失敗時はA経路を維持する。Referenceのfile I/O、decode、検証、可変長準備は非RT側で行い、
Audio Threadでは事前確保済みbufferのRT-safeな選択・出力だけを許可する。allocation、lock、
blocking I/Oを持ち込まない。

Reference Blind Compareは、Bが`READY`で、Aの測定値、transport再生、project位置、Bの事前読込が
すべて成立した時だけ入口を表示する。割当はOS CSPRNGで生成して非公開runtime stateに保持し、開示前は音源名、source種別、
測定値、delta、gain、alignmentを表示またはaccessibility情報へ出さない。`1 / 2`の選択表示は、
Audio Threadが要求sourceを実際に出力したcallback receiptの後だけ更新する。明示Revealまたは終了まで
自動開示せず、Referenceまたはruntime条件の変更時は割当を開示せずAへ戻す。

### 3.1.1 Kirin OS access boundary

Kirin OS連携は`OS未所有`、`OS所有・未接続`、`接続済み・準備不足`、`準備完了`の四状態を区別する。

OS未所有ではREF tab全体をdisabled表示にし、Keep／All Keepは消さずdisabled表示にする。
OS所有・未接続ではREF tabを開けるがBを無効にし、Kirin OSの`Open in Hypha`を案内する。
接続済み・準備不足では不足している前提に関係する操作だけを無効にし、準備完了時だけBとBlindを許可する。

REFはUIだけでなく、利用者操作の入口とAudio ThreadのB出力条件でもOS entitlementを再確認する。
Keep／Record開始は既存のRust側license gateを正本とし、UI状態だけで許可を推測しない。
Guide rail、TIME上のGuide時刻、FREQ上のGuide帯域、WorkへのCapture添付、Work名、CaptureへのGuide包含はOS所有時だけ利用できる。
LEVEL、TIME、FREQ、SPACE、通常のPRE/POST差分と解析、ローカル高解像度Capture、自由リサイズは制限しない。

### 3.2 Measurement boundary

現行のM、S、recent TP、Crest、PSR、Sharpness、I、LRA、MaxTPと、10 ms規格解析から公開するMax Mを別の意味へ読み替えない。

FREQのhost-rate aperture、Hann窓、FFT layout、256 band centre、exact PRE/POST joinを保持する。

SHARP、LIVE、ATTACKが持つsample endpointと欠測状態を、UI都合の補間で実測値へ変換しない。

追加snapshotは固定容量または境界付きとし、GUIがMeasure Threadの可変内部状態を直接読まない。

### 3.3 Resource boundary

追加解析は画面が必要とする間だけ動作させる。

process全体の解析枠は現行の2枠を初期上限として保持する。

画面遷移を変更しても、解析枠の取得、継続、解放は一つのcoordinatorに集約する。

通常のLEVELとloudness historyは追加FFTを起動せず、既存Watch snapshotから構成する。

### 3.4 Product boundary

R-22を維持する。

良い、悪い、適正、危険などの価値判断を表示しない。

赤、黄、緑の信号色で品質を採点しない。

ターゲット値への誘導、配信規格への合否、推奨処置は初期スコープに含めない。

R-28を維持する。

互換fallbackなど利用者操作と無関係な失敗は無言でskipする。

CaptureやResetなど明示操作の失敗は、成功と誤認されないよう事実だけを通知する。

リミッター、ゲイントリム、ノーマライズ、ラウドネスマッチなど音を変える機能は追加しない。

外部アカウント、サーバー、直接SNS投稿は追加しない。

## 4. Current implementation and redesign boundary

現行実装の詳細は`docs/hypha_meter_current_implementation_audit_20260831.md`を正本とする。

現行POST Watchは2列3行のgridで、選択中のMまたはS、recent TP、Crestと各最大値を表示する。

Record表示は選択中のMまたはS、PSR、MaxTP、I、Crest、Sharpnessを表示する。

LRAはSessionSummaryに存在するが現行6-cell UIには出ない。

PLRはplugin dataで算出されるが、現行UI snapshotにはない。

per-channel Peak/TP、correlation、balance、clip event、長時間historyはMeter Session coreとC ABIまで実装済みである。

Meter再設計branchのObservatoryはper-channel Peak/TP、balance、correlationをLEVEL/SPACEへ接続し、LEVEL上段M内へMeter SessionのMax Mを補助表示する。TIMEでは同一履歴点のM、S、TP、PLR、Correlation、集約min/max、run境界を同時表示する。

TIME ΔはPREの直近32点とPOSTの直近64点をpresentation source＋sample endpointでexact結合し、重複・欠測を線で補わず、pair/runtime変更時に全履歴を分離する。clip eventはLEVELの全sizeでL/R別session累積値を表示する。

現行AnalysisのFREQ、SHARP、LIVEと完了したATTACKは、計測器として再利用するが画面名と配置を固定しない。

ペア選択時にΔ gridへ強制遷移する現行表示も、互換性のための不変条件とはしない。

現行PRE DisplayはINSPECTとMASKINGの構造化Guideをexact PRE一台へ送信し、project clockへ投影する。

PRE送信は当時の表示余地に基づくため、再設計ではtransportの安全契約を保持して主送信先をPOSTへ移す。

POST版の受信、表示、移行Gateが成立するまで、現行PREのtransportと表示を維持する。

## 5. Standards gate

現行一次資料との意味差分監査は`docs/hypha_bs1770_5_r128_v5_audit_20260831.md`を正本とする。

1. [ITU-R BS.1770-5](https://www.itu.int/rec/R-REC-BS.1770)
2. [EBU R 128 v5.0](https://tech.ebu.ch/publications/r128)
3. [EBU Loudness resources and test set](https://tech.ebu.ch/loudness/)
4. [ITU-R BS.1771-1](https://www.itu.int/rec/R-REC-BS.1771/en)

Mは400 ms、Sは3 s、Iは利用者がResetするまでのMeter Sessionとして扱う。

LRAは測定開始直後に安定しないため、値が成立していない期間は`WARMING`と経過時間を表示する。

BS.1770-5はobject-based audio用Annex 4と高度音響方式の構成を更新したが、Hyphaのmono、stereoが使うAnnex 1とAnnex 2の測定式は変更していない。

依存crateは`ebur128 0.1.10`で固定する。公式test set全70素材は完走済みだが、mono/stereoの製品範囲を越えた適合を暗示しないため、UIとCaptureは版番号を付けない`ITU-R BS.1770`を表示する。

HyphaはMaximum Mを製品契約に含める。Maximum SとEBU +9/+18 scaleは含めないため、製品全体を`EBU Mode`とは表示せず、EBU R 128 logoも使用しない。

規格版、計測式、単位、丸め、更新周期を一つの測定仕様に固定し、GUIとexportが同じsnapshotを読む構造にする。

## 6. Competitive baseline and late-mover strategy

2026-08-31時点の公開一次資料から、通常メーターの基準線を次のように置く。

| Product | Publicly documented strengths | Hypha response |
|---|---|---|
| [Youlean Loudness Meter](https://youlean.co/youlean-loudness-meter/) | I、LRA、PLR、DR、loudness graph、distribution、A/B、PNG/PDF/SVG export | 主要loudness値と履歴を標準装備し、CaptureをSNS比率から逆算する |
| [NUGEN VisLM](https://nugenaudio.com/vislm/) | 最大24時間のtimecode history、True Peak log、navigable flag | 固定容量の多段履歴と事実eventを持ち、品質alertは持たない |
| [iZotope Insight](https://www.izotope.com/products/insight) | loudness、level、sound field、spectrogramのmodular表示 | LEVEL、TIME、FREQ、SPACEを一つの観測体系へ統合する |
| [Process Audio Decibel](https://process.audio/en/products/decibel) | customizable meter、spectrum、spectrogram、phase scope、stereo cloud | 任意配置より一貫した情報階層を優先し、精密さとHypha固有性を両立する |

I、LRA、PLR、per-channel peak、history、spectrum、correlationは差別化機能ではなく、常用メーターの基準線として扱う。

後発優位は、別々に存在していた観測を同一時刻、同一snapshot、同一visual grammarへ統合することから作る。

Hypha固有の差は、PREとPOSTのexactな差分を複数領域へ横断させられることである。

単体メーターとしてのPOSTと処理差を観測するΔを、同じ画面構造で往復できるようにする。

競合のtarget、alert、recommendation、normalizationは追随しない。

観測と判断を分けることを、Hyphaの製品境界として明確にする。

## 7. New information architecture

画面は「観測領域」と「観測対象」の二軸で構成する。

### 7.1 Global shell

Header左にroleを含む`HYPHA POST`または`HYPHA PRE`を置く。

Header中央に`LEVEL / TIME / FREQ / SPACE`を置く。

Header右にpair名と信号状態を置く。

POST画面ではFooterに`POST / Δ`、Meter Session、Reset、Captureを置く。

PRE画面も同じ外形、背景、typography、status grammarへ更新し、一画面だけ旧世界に残さない。

PREはpair側の測定sensorであり、POSTと同じ機能数を無理に持たせない。

### 7.2 Observation domains

| Domain | Default surface | Existing capability absorbed | Optional subview |
|---|---|---|---|
| LEVEL | M、Max M、S、I、recent TP、MaxTP、LRA、PLR、Crest、L/R meter | 現行Watch、Record、LIVEの現在値 | session facts |
| TIME | M、S、TPの履歴、playback run単位の事実集計 | LIVE timeline、SHARP timeline、ATTACK event timeline | HISTORY、RUN、SHARP、ATTACK、LIVE |
| FREQ | Spectrum | 現行FREQのPRE、POST、Δ、LR、MID、SIDE、probe、MARK、Focus Trail | SPECTRUM |
| SPACE | correlation、L/R balance、goniometer density | なし | FIELD |

`LIVE`は独立ページとして残さない。

LIVEのM、TP、SharpnessはLEVELの現在値とTIMEの履歴へ吸収する。

`SHARP`は時間変化を主表示とするためTIMEのsubviewに置く。

`ATTACK`もeventの六秒scrubを主表示とするためTIMEのsubviewに置く。

FREQは既存Spectrumの意味と操作を保ったまま、上位領域へ移す。

### 7.3 Observation targets

| Target | Meaning | PRE unavailable |
|---|---|---|
| POST | POSTの絶対値 | 常に使用可能 |
| Δ | POST − PRE | `NO PAIR`を表示し、値は`---` |

ペア選択は入力データの接続状態だけを変える。

POSTとΔの切替は利用者の観測視点だけを変える。

ペア接続によって画面を強制的にΔへ切り替えない。

意味が固定できた領域だけにΔを提供する。

LEVEL、TIME、FREQはPOSTとΔを持つ。

SPACEはcorrelation差分の定義と知覚上の意味を固定するまでPOSTだけを持つ。
SPACEの主表示は、同じ100 ms観測境界からMeasure Threadが生成するrolling 3秒のMID/SIDE densityとする。
MIDは`(L+R)/2`、SIDEは`(L-R)/2`とし、25×25の固定fieldへ各観測最大1024点を蓄積する。
表示用に符号を保つ平方根compandingを行い、3秒窓内の最大cellを255としてdensityを正規化する。
この正規化はstereo形状の事実だけを表し、絶対音量はLEVELが所有する。
30観測未満は実際の観測数を`WARMING n/30`として表示し、mono、無音、未成立を数値で装わない。
correlationとL/R balanceも同じrolling 3秒窓を参照し、SPACEの発光やcell色は品質判定へ使わない。

ATTACKは現在の契約どおり、pair時はexact PRE/POST、未接続時はPOST absoluteを表示する。

### 7.4 OS Guide layer

Kirin OSのINSPECTとMASKINGは、POSTの第五domainではなく全domainへ作用できるGuide layerとする。

Guide layerの取得・接続承認・表示snapshotはKirin OS entitlementで制限する。
OS未所有でも各domain自体は使用でき、Guide由来のrail、時刻、帯域だけを表示しない。

Guideの実装計画は`docs/hypha_post_os_guide_integration_plan_20260831.md`を正本とする。

Kirin OSは保存済みWorkから利用者が確認したPOST一台へ直接送信する。

PREをrelayに使わず、POSTはPREとpairされていなくてもGuideを表示できる。

Guide不在時は画面上の占有面積を0にする。

Guide受信時も現在のdomainを自動変更しない。

LEVELはGuide railだけを表示する。

TIMEはINSPECTの時刻または区間と、MASKINGの選択範囲および実測collision intervalを表示する。

FREQは存在する場合だけINSPECT bandを表示し、MASKINGのfrequency focusとmeasured bandを別の形で表示する。

SPACEは対応するGuide事実がないため投影しない。

`OS GUIDE`、`LIVE POST`、`LIVE Δ`は別のauthorityとしてlabelとsnapshotを分離する。

2MIX POSTはMASKINGの二つのsourceを分離できないため、現在のMASKING再測定を称しない。

## 8. Meter Session

通常メーターはプラグインを開いた直後から操作なしで読める。

Max M、I、LRA、MaxTP、PLR、clip countは独立した`Meter Session`に蓄積する。

最初のActive音声でSessionを開始する。

transport停止、無音、DAW bypass中はSession時間と集計を進めない。

再びActiveになったときは同じSessionを再開する。

UIを閉じてもプラグインinstanceが生存する限りSessionを保持する。

`RESET`だけが現在のSession統計を明示的に破棄する。

Record、Keep、Kirin OS接続の状態はMeter Sessionに影響しない。

プロジェクトreloadまたはplugin runtime再生成後は、新しいMeter Sessionを開始する。

I/LRAのgating履歴を完全保存せず累積値だけ復元すると、reload前後で同じ測定事実にならないため、Meter SessionはDAW stateへ保存しない。

## 9. Metric semantics

| Label | Unit | Window or scope | Display precision |
|---|---|---|---|
| M | LUFS | 400 ms | 0.1 LU |
| MAX M | LUFS | Meter Session内の10 ms cadence Maximum Momentary | 0.1 LU |
| S | LUFS | 3 s | 0.1 LU |
| I | LUFS | Meter Session | 0.1 LU |
| TP | dBTP | recent 400 ms | 0.1 dB |
| MAX TP | dBTP | Meter Session | 0.1 dB |
| LRA | LU | Meter Session | 0.1 LU |
| PLR | dB | MAX TP − I | 0.1 dB |
| L/R SP | dBFS | current block and hold | 0.1 dB |
| L/R TP | dBTP | recent 400 ms | 0.1 dB |
| BAL | dB | 3 sのL/R energy差 | 0.1 dB |
| CORR | unitless | 3 s energy-normalized correlation | 0.01 |

L/R Sample Peakのhold markerはMeter Session開始後のチャンネル別最大値とし、時間で自動解除しない。

`RESET`だけがholdを解除する。

`BAL`は`10 log10(E_L / E_R)`の符号付き値とし、正値をL、負値をRとしてラベルにも明示する。

`CORR`は`sum(LR) / sqrt(sum(L²) sum(R²))`の固定式とする。

無音または分母0では`CORR`を`---`にする。

BAL、CORR、per-channel peak、clip countはMeter Sessionへ実装済みである。

境界値、無音、mono、逆相、片ch、同相信号のgolden testを実装し、workspace testで検証する。

clip thresholdはチャンネル別に`abs(sample) >= 1.0`（0 dBFS）とする。

1 clip eventは、同一チャンネルでthreshold以上が連続する最大runとする。

runが100 ms観測境界をまたいでも1 eventのまま保持し、threshold未満のsampleを1点以上挟んだ次のrunを新しいeventとして数える。

L/R同時clipは各チャンネルの独立eventとして数え、総数へ暗黙に畳み込まない。

## 10. History and optional analysis

表示履歴はMeasure Thread側で固定容量の多段ring bufferへ集計する。

M、S、TP、PLR、CORRを10 Hzで10分、1 Hzで2時間、0.1 Hzで24時間保持する。

RUNは選択中のTIME resolutionだけを`generation + run_id`で集約し、別の履歴や永続化を作らない。表示範囲内の経過時間、M min/max、Max TP、L/R clip数を出す。DAW sample endpointが全点で成立する時は`RUNS IN VIEW`、clock不明のホストでは捏造した区切りを足さず`SESSION RUN`として1本を表示する。resolution混在、不完全なsample endpoint、非単調sample位置は表示しない。PRE/POST間でrun_idを同一識別子として扱わず、RUNのΔは初期契約に含めない。

10 Hz層は既存Watch snapshotのexact sample endpointを保持する。

低rate層はbucketのmin、max、mean、first endpoint、last endpointを保持し、exact値と同じ線として描かない。

各100 ms観測はDAW presentation sample endpointとtransport run IDを保持する。

一つの観測がtransport jumpをまたいだ場合、その観測に虚偽のendpointを付けずhistoryだけをskipし、次の完全な100 msから新runとして再開する。

hostがsample座標を供給しない場合はDAW endpointを`Unavailable`にしたまま、Meter Session相対endpointによる履歴を保持する。

UIは30 s、2 min、10 min、2 h、24 hを切り替える。

表示中のresolutionを時間軸に明示する。

UI再描画周期と履歴sample周期を分離する。

UIを閉じても履歴計測を継続し、再表示時に直前の文脈を復元する。

FREQは画面を開いたときだけ既存Spectrum解析を取得する。

TIMEのSHARPまたはATTACKも、該当subviewを開いたときだけ解析枠を取得する。

POST FREQは既存Spectrum解析の同じ実測frameから、現在Spectrum、6秒固定長の時間周波数field、rolling peak holdを生成する。

このfieldのためにAudio Thread処理、FFT worker、解析slotを追加しない。

履歴はUI側の固定容量180 frame（30 Hz、6秒）に限定し、GUIを閉じている間の永続保持や長時間Spectrogramを約束しない。

PRE不在時もPOST absolute factsは表示できるが、Δ、MARK、Focus Trailを捏造または流用しない。

## 11. Responsive screens

5サイズとPRE、POSTを同じ変更単位として設計、実装、確認する。

| Size | POST required content | PRE required content |
|---|---|---|
| 300×200 | 選択domainの主値、role、pair、POST/Δ、Session state | MまたはS、TP、Crest、name、pair state |
| 375×250 | Compact内容、補助値、domain switch | Compact内容、I/O state、接続context |
| 450×300 | 世界背景を抑えた主visual、軸、session facts | Standard内容、測定stateの詳細 |
| 600×400 | Concept Cのfull cockpit、M/S/I、TP/MaxTP/LRA/PLR/Crest、History凡例のMax M、60秒History、左右TP、POST/Δ、Capture | POSTと共通のshell、広い数値面、接続context |
| 900×600 | 全domain共通Inspection View、拡張History、詳細axis、既存解析の高解像度表示 | POSTと共通のInspection shell、拡張History、詳細axis |

小さい画面で情報を単純に縮小しない。

優先度の低い補助値を折り畳み、数字の最小可読サイズを守る。

現行Analysisと一般Editorは5 presetを共有し、Metersだけ固定という分断をなくす。

600×400は二つのトラック比較、および2MIXと単体トラックの二面比較を成立させる主力Observatoryとして維持する。

900×600（300%）は600×400を置換せず、LEVEL、TIME、FREQ、SPACEとTIME配下の解析を同じ操作体系のまま高解像度で読むInspection Viewとする。LEVELは履歴面積、channel strip、数値階層を拡張するが、未合意の新指標は追加しない。将来Session Atlasを載せる場合は別途表示内容を確定する。

LEVELの60秒Historyは固定時間軸とし、M主線、run別2秒最大TP event、L/R別sample clip event、`60 S MAX TP`と相対時刻を表示する。Sを含む詳細なM/S/TP推移はTIMEへ集約し、LEVELは現在地を読むcontext面として重複させない。TP専用railは作らず、Mが全面を使う同じ横軸の下部へ、右側`+6〜-24 dBTP`軸と下から立ち上がるstemを重ねる。中央の`MAX TP`は全Session、Historyは直近60秒という範囲差を文言で固定する。Max MもSession事実としてHistory上部凡例へ置き、現在のM数値内へ混在させない。

600×400以上のLEVELは、上段3と中段5の合計高を従来割当の約60%へ圧縮し、残りをHistoryへ渡す。FooterもCAPTUREボタン単体ではなく操作段全体を40 pxから24 pxへ縮め、測定履歴を画面の主面積にする。

## 12. Visual system

Concept C Hybrid Observatoryをvisual baselineとする。

主数値とunitはsolidなgraphite panelで保護する。

菌糸は外周、構造境界、history下層、status周辺へ限定する。

[Kirin OS 1.0](https://kirinmastering.com/kirin-os-1-0)下部のJungle世界から、巨大な有機構造、湿度を感じる奥行き、暖色の生活光、疎なcyan signalを取り入れる。

細い蔓を画面へ貼り付けただけの装飾にはしない。

ivoryの数字、cool cyanの実測線、低彩度amberのholdと居住光を基本とする。

600×400以上のLEVEL主数値はneutral whiteではなく暖色instrument ivoryとし、labelを上、unitを下へ分離する。主数値面のHyphaは専用corner素材を外縁から伸ばし、数値を横切る人工的な楕円線は置かない。

数値はtabular figuresを使い、小数点、符号、単位の位置を揃える。

製品UIの書体は有料版Kimera KMR Waldenburg Bookで確定する。

既存のKimera App Licenseは購入済みであり、Kirin Hyphaへの適用可否をDaisukeが確認中である。

追加licenseが必要な場合もKimeraの採用は変更せず、必要なlicenseを取得する。

Font Software本体はGPLソースへ含めず、Kirin Hyphaを対象にしたApp Licenseの確認後、リポジトリ外の正規OTFからrelease buildへ埋め込む。

ライセンス確認前の開発buildは同じ文字役割と固定digit cellを保ったnative fallbackで検証し、公開buildではKimera埋め込みを必須gateにする。

グラフには時間軸、値軸、現在位置を表示する。

発光色は品質判定に使わない。

更新停止、WARMING、NO PAIR、Inactive、Bypassed、analysis slot unavailableを視覚的に区別する。

動きを減らすOS設定では、脈動と遷移を止めても全情報を読めるようにする。

全画面overlayに`backdrop-filter`を使わない。

既存のPRESENCE overlay値は変更しない。

## 13. Capture contract

Captureは利用者の明示操作で現在の測定snapshotを画像に保存する。

ローカル保存はKirin OS entitlementに依存しない。Workへの添付、Work表示名、OS Guide包含だけをOS連携機能として制限する。

出力presetは1200×630、1080×1080、1080×1350とする。

画像には製品名、POSTまたはΔ、主要値と単位、ABS/Δ、Session elapsed、測定標準、capture時刻、Hypha versionを含める。

PRE名、POST名、プロジェクト名、OS Guideは項目ごとの明示opt-inとし、既定で含めない。

PRE名は利用者が設定したpair表示名、POST名はホストが明示提供したtrack表示名、プロジェクト名は利用者が接続を承認したKirin OS Work表示名だけを候補にする。

表示名を取得できない項目は選択不可とし、ファイルpath、UUID、work ID、内部instance IDで代替しない。

画像は表示中のUIを拡大せず、shellとATTACK、SHARPNESS、FREQ、LIVEを含む表示中の観測面を一回の同期read boundaryで固定し、同じimmutable snapshotから専用layoutで描く。

LEVELのObservation Plateは主値、M内のMax M補助値、その他の補助値、channel stripに加え、Capture操作時に固定した直近60秒のM History、TP event、L/R clip eventを含める。

60秒Historyは600×400以上のLEVELとLEVEL保存構図だけに置き、TIMEのrange切替や全機能は重複させない。SpectrumはLEVELへ重複搭載せずFREQを正規入口にする。

保存とPNG encodeは非Audio Threadで行う。

直接SNSへ送信せず、利用者が選んだローカル保存先だけへ書く。

保存失敗時は対象pathを秘匿した短い事実通知を表示する。

## 14. State migration

既存のinstance identity、project identity、pair名、exact pair locator、M/S選択を失わない。

新しいdomain、subview、size、POST/Δ選択は末尾追加の表示stateとして扱う。

旧state、legacy nih-plug state、不正な新規値は、POSTのLEVEL、100%、POST perspectiveへ安全にfallbackする。

表示stateの復元は計測、pairing、plugin dataのschemaを変更しない。

## 15. Verification gates

### Gate A: measurement semantics

現行規格との差分表は`docs/hypha_bs1770_5_r128_v5_audit_20260831.md`として完了した。

既存test signalによるM、Max M、S、I、TP、LRAの数値比較はpassした。

公式test set v05の全70素材は、固定`ebur128 0.1.10`とHyphaの`MeasureEngine`でpassした。
内部解析はTech 3341の20 ms alignmentを保持する10 ms、既存GUI、TRACE、IO公開は100 msである。
Tech 3341のM、S、I、Max M、Max S、TP、Tech 3342のLRAと4 reference/alignment素材を公式許容差で検証する。
5.0/5.1素材はdecodeと参照値を確認するが、製品入力範囲はmono/stereoのままである。

PLR、BAL、CORR、clip eventの正常系、境界値、無音、mono、逆相を検証する。

Max Mは10 ms規格解析値を100 msのMeter Session snapshot境界へ固定し、pause、UI close、再開で保持され、明示RESETだけで消えることを検証する。

### Gate B: real-time safety

Audio Threadのallocation、lock、I/Oが0件であることをコードと計測で確認する。

通常のA経路でPREとPOSTのbit identical、0 samples latency、process CPU基準を再確認する。

Reference比較試聴を実装する場合は、明示操作前、project復元、offline render、Reference欠損、identity検証失敗で
A経路が維持されること、B経路が正本のPRE/POST測定・Recordへ混入しないこと、Audio Threadへallocation、
lock、blocking I/Oを追加しないことを検証する。

Measure Thread panic、UI close/reopen、history欠落、sample rate変更を検証する。

### Gate C: visual system

PREとPOSTの5サイズを同じfixtureでrenderする。

文字切れ、unit誤記、小数桁、WARMING、NO PAIR、Inactive、Bypassedを画像差分で確認する。

30分以上の表示でmotion、固定高輝度、背景による可読性低下を実機確認する。

### Gate D: navigation and resource control

全domainとsubviewの取得、継続、解放を2枠制限下で確認する。

旧state復元、PREなし、PRE stale、explicit bypass、Editor再表示を確認する。

ATTACKのevent、FREQのexact join、SHARPとLIVE由来のendpointが再配置後も変わらないことを確認する。

### Gate E: OS Guide

exact POST binding、receipt、artifact完全性、project clock、retention、End、legacy PRE fallbackを確認する。

Guide受信がdomain、pair、Meter Session、Analysis selectionを変更しないことを確認する。

INSPECT instant、MASKING interval、optional band、unlocated frequencyを全5サイズで確認する。

### Gate F: capture

3 presetのpixel寸法、文字、snapshot一致、capture時刻、製品versionを自動確認する。

Guideは既定で含めず、明示選択時だけ含める。

ファイルパス、UUID、内部instance IDが画像へ入らないことを自動確認する。

保存先なし、権限なし、disk full、連続操作を確認する。

## 16. Implementation order

1. 公開系`734a72a`とATTACK完了点`d464f71`をMeter branchで統合し、sourceとtracked JUCE patch stackを検証する。
2. 規格差分と全metricの測定仕様を固定し、golden testを追加する。
3. Guideの有無を含むPREとPOST、5サイズのwireframeと共通visual tokenを完成させる。
4. UI非依存の`MeterSnapshot`、`MeterSession`、固定容量history、`GuidePresentationSnapshot`を追加する。
5. PRE専用Guide protocolをrole-neutralなexact POST bindingへ拡張する。
6. 新しいglobal shellと`LEVEL / TIME / FREQ / SPACE` router、Guide railを追加する。
7. 現行FREQ、SHARP、LIVE由来の表示、ATTACKを新しい領域へ移し、旧routerを除去する。
8. TIMEとFREQへOS Guideを投影し、OS GUIDEとLIVE測定のauthorityを分離する。
9. per-channel peak、BAL、CORR、SPACE visualを追加する。
10. Captureをimmutable snapshotから実装する。
11. conformance、RT safety、全状態、全サイズ、旧state migrationをまとめて検証する。

各段階は旧UIへ継ぎ足すpatchではなく、その段階で責務を満たす完全な層として実装する。

## 17. Fixed product decisions
visual方向はConcept Cで確定した。
情報設計は既存動線を固定せず、`LEVEL / TIME / FREQ / SPACE`を上位構造として進める。
Meter Sessionはplugin instanceの同一runtime中だけ保持し、DAW project reloadでは空のSessionから開始する。
SPACEはPOST専用の実測MID/SIDE densityとして初回公開対象に含め、意味未定義のΔ表示は作らない。
製品書体はKimera KMR Waldenburg Bookで確定し、OTF埋め込みだけをHypha対象App License確認後の公開gateとする。
