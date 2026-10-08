# Kirin Hypha Meter product contract

Status: implemented development baseline; release conformance pending; Kimera is optional

2026-09-06 A0 差分: 既存 ATTACK の製品表示は DRUM とし、TRACK/STEM の入口に限定する。
2MIX では DRUM を表示せず、文脈の変更・復元時に DRUM の解析要求を終了する。
これは既存検出器の適用範囲の明示であり、2MIX 専用 ATTACK の完成ではない。
全サイズの PRE／POST に共通情報メニューと手動の更新情報・ダウンロード入口を設ける。
SPACE DECAY とローカル PRE/POST Blind は研究中で、以下の既存 FIELD／登録 Reference 契約を置き換えていない。
承認範囲と 2 枠の Blind 方針は [実装承認記録](hypha_implementation_approval_20260906.md) を参照する。

2026-09-29の差分: PRE/POSTの主Blindは[一回再生のLive Blind](planning/hypha_one_pass_live_blind_implementation_plan_20260929.md)へ変更する。
直接BLINDは固定MATCHを準備し、LISTENで有効なMATCHがあれば再利用する。AUTOは持ち込まない。
mono／stereoのPOSTで現在サイズの匿名面を開き、AAXはstereoまたは組の唯一のmono instanceに限る。対応時計、exact pair、排他のgateは維持する。
100%／125%はMENUを入口とし、試聴中は上段2行目を比較操作へ優先してENDとMENUを残す。小サイズでも終了時の上昇量を隠さない。
Source 1／2の実出力receipt後だけ回答可能。ENDは上昇量の明示→POSTへの退避→通常音量へのramp→実unity確認を一回の要求にまとめる。
close／hideだけなら減衰保持、回答／Revealだけなら試聴継続。終了後にMATCHへ戻さない。
固定4秒の厳密比較はLISTENのMENU → PIN 4 Sに残し、Exactの固定PCM／最小900×600契約を維持する。直接CAPTURE入口は主面とMENUから外す。
以下の登録Referenceの拡大規則は変更しない。本差分の実機format検証と公開適合は未完了である。

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

ATTACK統合後のworkspace testがgreenになったため、2026-08-31の設計判断に従ってMeter本体と共同で進める。

精度を装飾で演出するのではなく、単位、時間窓、軸、状態、測定時刻を美しく組み立てる。

この文書は実装済みの製品契約であり、B-590からB-605の実装と今後の公開判定を拘束する。

## 2. Isolation boundary

製品変更の検証は、他の作業から分離したcheckoutで行う。

ATTACKセッションが使用するworktree、ブランチ、submodule、build成果物、VST3配置先には触れない。

ATTACKの完了commitはGitの三者マージでこのブランチへ統合し、ATTACK worktreeの作業中submoduleや生成物をcopyしない。

共用のStudio Oneプラグイン配置とリリース操作は、この設計統合では行わない。

## 3. Preserved engineering contracts

### 3.1 Audio boundary

R-12を維持する。

通常のPRE/POST計測では音声信号を生成、変更、減衰、遅延しない。

通常計測時のAudio Threadは入力の読み取り、事前確保済みバッファへのコピー、atomic通知だけを行う。

FFT、履歴集計、画像生成、ファイル保存、UI描画はAudio Threadで行わない。背景の固定rasterは既存の容量・editor lifetimeの制限内で再利用する。背景のcache keyには描画が実際に使う値だけを含め、使う値・寸法・DPIの変更では再生成する。CPU削減のために観測値、提示周期、画素の既存許容差を変更しない。

メーター機能の追加後も、通常のA経路はレイテンシー0 samples、PREとPOSTの音声差分はbit identicalを
合格条件とする。

利用者が明示的に開始するReference比較試聴は、R-12で禁止する音声生成・加工には含めない。
登録済みの不変なReferenceを試聴専用B経路で再生し、試聴コピーにだけ一時的なGain Matchを適用できる。
Referenceファイル、通常のA経路、正本のPRE/POST測定・Recordは変更しない。

Reference比較試聴の役（A・B・C・V）、Gain Match（追従・固定・上限・Aを下げる承認）、選択と停止・シークからの
復帰、比べる表示の定義、VのAUTO、REFの状態の行と300%のページの規則は、`docs/hypha_invariants.md`の
INV-S46〜S53とREADMEのReferenceを正本とし、ここには書き写さない（2026-10-06：書き写した規則が古くなり、
Versionの役をBと呼んでいた）。この契約が決めるのは、上の音声の境界（A経路・Referenceファイル・正本の測定を
変えない）と、下の非RT・Audio Threadの境界である。

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

POSTのREF入口はOS権限を確認できない状態でも開け、Referenceの説明、公式製品ページ、所有者向け接続案内を表示する。
公式製品ページは英語と日本語の明示選択で開き、購入処理や外部通信を自動開始しない。
画面の言語（英語／日本語）はこの選択と独立しており、どちらの画面からも両方の公式ページを選べる（INV-S40）。
所有者向け案内ではローカルlicenseの明示再確認を提供し、確認できないことを「未購入」と断定しない。
Keep／All Keepは消さずdisabled表示にする。
ReferenceはKirin OSが保存済みプリセットを自動配信し、POSTが受信する独立した経路である。
INSPECT、Guide、Work接続、PRE/POSTペア選択をReference接続の前提にしない。
OS所有・未受信でも通常画面への操作（選択欄とA・B・C・Vのボタン）を覆わない。
聴ける役が無い間の案内はINV-S41を正本とする。
受信したプリセットと確認項目は音源の準備状態にかかわらず選択できる。Hypha内蔵のFactory代用品は表示しない。
A・B・C・Vとプリセットの選択は全5サイズで使用でき、VERSION BLINDは300%で使用する。
配信、読込、再初期化だけでBへ切り替えず、通常Aへの明示復帰と既存の解析2枠・比較試聴排他を維持する。
接続済み・準備不足では不足している前提に関係する操作だけを止め、準備完了時だけB・C・VとBlindを許可する。
聴けない役を押したときの動き（待たせる・理由を言う・300%で開く）はINV-S46・S48を正本とする（R-28）。

REFの案内画面は試聴の許可ではない。
登録ReferenceのB／BlindはUIだけでなく、利用者操作の入口とAudio ThreadのB出力条件でもOS entitlementを再確認する。
進行中の比較や減衰保持からの復帰操作を案内画面で覆わない。
ローカルPRE/POST BlindはHypha単体機能として承認済みだが、このReference案内の実装では音声経路へ接続しない。
Keep／Record開始は既存のRust側license gateを正本とし、UI状態だけで許可を推測しない。
FooterのGuide context、TIME上のGuide時刻、FREQ上のGuide帯域、WorkへのCapture添付、Work名、CaptureへのGuide包含はOS所有時だけ利用できる。
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

Header左にroleを先頭にした`POST HYPHA`または`PRE HYPHA`を置く。PREは青、POSTはflora amberのrole文字で区別し、製品名の`HYPHA`は共通色を保つ。

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
| FREQ | Spectrum | 現行FREQのPRE、POST、Δ、LR、MID、SIDE、M/S同時表示、probe、MARK、Focus Trail | SPECTRUM |
| SPACE | correlation、L/R balance、goniometer density、MONO（帯域別モノ加算残存） | なし | FIELD |

MONO は 2026-09-18 追加（B-908〜B-915）。100 ms 観測ごとに 1/3 oct 32 帯域の
`10*log10(Pm/(Pm+Ps))` を測り、既存表示を損なわずに入る editor サイズでだけ SPACE へ足す
（現時点では 900x600 のみ）。定義と表示契約は INV-S31 / INV-S32、計画は
`hypha_space_mono_sum_plan_20260918.md` を正本とする。

`LIVE`は独立ページとして残さない。

LIVEのM、TP、SharpnessはLEVELの現在値とTIMEの履歴へ吸収する。

`SHARP`は時間変化を主表示とするためTIMEのsubviewに置く。exact pair成立時はPOST−PRE、
未成立時はPOST単体のSharpnessを同じ画面に表示し、見出しと固定scaleを切り替える。

`ATTACK`もeventの六秒scrubを主表示とするためTIMEのsubviewに置く。

FREQは既存Spectrumの意味と操作を保ったまま、上位領域へ移す。

### 7.3 Observation targets

| Target | Meaning | PRE unavailable |
|---|---|---|
| POST | POSTの絶対値 | 常に使用可能 |
| Δ | POST − PRE | `NO PAIR`を表示し、値は`---` |

ペア選択は入力データの接続状態だけを変える。

POSTのpair欄は読取専用の候補選択とし、自由入力を持たない。PRE名は任意の表示ラベルであり、無名PREも候補に含める。選択とDAW state復元の権威はexact PRE locatorとし、名前一致による自動選択・再接続を行わない。同名PREは選択欄で短いinstance IDを併記する。

POSTとΔの切替は利用者の観測視点だけを変える。

ペア接続によって画面を強制的にΔへ切り替えない。

意味が固定できた領域だけにΔを提供する。

LEVEL、TIME、FREQはPOSTとΔを持つ。

FREQのM/SはPOST targetでだけ成立するstereo絶対観測であり、同じapertureのMIDとSIDEを同時表示する。

M/S中はΔだけをdisabledにし、Δ中はM/Sだけをdisabledにする。

LR、MID、SIDEへ戻ればΔを再び選択でき、targetやpairの状態を自動変更しない。

SPACEはcorrelation差分の定義と知覚上の意味を固定するまでPOSTだけを持つ。
SPACEの主表示は、同じ100 ms観測境界からMeasure Threadが生成するrolling 3秒のMID/SIDE densityとする。
MIDは`(L+R)/2`、SIDEは`(L-R)/2`とし、25×25の固定fieldへ各観測最大1024点を蓄積する。
表示用に符号を保つ平方根compandingを行い、3秒窓内の最大cellを255としてdensityを正規化する。
この正規化はstereo形状の事実だけを表し、絶対音量はLEVELが所有する。
30観測未満は実際の観測数を`WARMING n/30`として表示し、mono、無音、未成立を数値で装わない。
correlationとL/R balanceも同じrolling 3秒窓を参照し、SPACEの発光やcell色は品質判定へ使わない。

ATTACKはTRACK／STEMのDRUMに限定する。pair時のHISTORYはexact PRE/POSTを重ね、
TRANSIENT／STRENGTH／CREST／SHARPNESSの四laneは、POSTをPREのonsetで同じ窓のまま測り直した打音ごとの`POST−PRE`を描く（B-1016）。
PRE未接続時はlaneをPOST absoluteへ切り替え、PREと差分を生成しない。
正本は`hypha_drum_lanes_20260924.md`とする。

DRUMのBANDで1オクターブ帯域（ISO 63 Hz〜8 kHz）を選ぶと、4laneはその帯域の
DELAY／ATT／REL／LEVELとなる。snapshotのtargetをPOST／Δとして明記し、対応済みPOSTは
PREのonset・区間で測る。Δに必要なband意味、PRE実測、対応proofが不足する場合はunknownと
そのtyped理由（`Update PRE`、`No mapping`等）を示し、POST値で埋めない。接続なしのPOSTでは
DELAYはN/A（`No PRE`）。Navigation・Single・Summaryはsource証拠付きの同じproducer event keyを
共有し、UIはonsetから鍵を作らない。band切替だけで全帯域navigationの鍵を失効させない。

DRUMのNavigationは現在の比較利用可否を明示する。Activeな選択pair viewと現在authority・両source・
band意味のproofが揃えば要求Δを返し、揃わなければtarget=POSTを返す。GUIは返されたtargetで
Summary／Singleを取得する。これは要求済みtarget=Δの測定snapshotへPOST値を代用する規則ではない。
そのΔに必要な事実が欠ければ、Δのtargetとtyped分類・理由を維持する。同source・同ledgerの
利用可否変更だけではLOCKのproducer keyを捨てない。

BAND LIVEはsnapshot cutoff時点の6秒内の直近最大8検出打音を先に固定し、
exact／interval／N/A／unknown／pendingを五分類する。帯域が立ち上がらない打音、測定中の打音も
cohortに数える。欠測を除いて古い打音を補充しない。四laneは全対象中央値、全対象中央値の区間、
確定部分中央値・件数・age、数値不成立を区別する。確定部分を全体値として表示しない。
端点のopen／closed、±∞、外向丸めを保ち、単位と測定分解能を値から区別する。
帯域無音・前音残響はN/A、次打で切れた尾はunknown、長尾・AudioEndの測定済み端はintervalとして
理由を残す。未取得のunknownと、受理済みでまだ音声／worker／publicationを待つpendingを混同しない。
値・scope・件数・理由・平均HEAD／TAIL・実測mask・参加集合・接続可否は同じtyped stampで採用し、
UIで中央値を再計算しない。参加集合が変わる点、未観測区間をつながない。

ALL LIVEは最新の検出打音のSingleを使い、BANDの8打中央値を流用しない。LOCKは一打の値・理由・
包絡を保持し、6秒窓外でも同じeventを読む。BANDのpointerは最後に採用したSummary cutoff Cの
最大8件cohortを使い、LOCK中もその表示集合を保持する。普通の←→・HOMEは現在集合を使う。
BANDは現在Navigation cutoffの6秒内の直近最大8件を欠測込みで、ALLは現在表示窓の全Navigation
系列（容量240件）を巡回する。clusterとactive dragは開始時の鍵集合を使い、publicationで
差し替えない。対象窓が空ならLOCKを保持する。同じ単独点の再選択、LIVE/HOLDボタン、ENDで
LIVEへ戻る。空所は近い打音を選び、ESCは
候補操作／Factsを閉じてもLOCKを維持する。同source・同ledgerのPOST↔Δ利用可否だけでは選択を
失わず、source／proof binding失効は退役する。Factsは同じ採用presentationの根拠を凍結して示す。

帯域処理とPCM保持はATTACK workerが担当し、帯域を替えると直近7秒を停止中でも再解析する。
AudioEnd後の連続入力で実測区間が延びた場合は新revisionを公開するが、終端済みSingleは凍結する。
ALLへ戻すと帯域用PCMを解放する。旧DRUMの`--`、NOW、available-onlyまとめ、旧PREでのPOST代用は
現行V2の表示契約に使わない。測定定義は`hypha_drum_band_view_plan_20260928.md`（§11〜§14）、
現行presentationはINV-S44と`planning/hypha_drum_psr_usability_improvement_plan_20261007.md`を正本とする。

DRUM取得は30 Hz、BAND LIVEまとめの採用は250 msごと、ALL最新一打／LOCK取得中はnative周期を使う。
source失効と選択操作はまとめ周期を待たない。表示時計のviewportとfact cutoffを分離し、source sample clockとGUI monotonic clockをanchorする。
暫定look-behind 150 ms、鮮度250 msはdevelopment値で、実DAWの正常jitter・提示遅延はG3で校正する。
factsへ追いつく、stop／bypass／非実時間入力、鮮度切れではHOLDにし、毎publicationのeaseや高速catch-upで移動を作らない。

### 7.4 OS Guide layer

Kirin OSのINSPECTとMASKINGは、POSTの第五domainではなく全domainへ作用できるGuide layerとする。

Guide layerの取得・接続承認・表示snapshotはKirin OS entitlementで制限する。
OS未所有でも各domain自体は使用でき、Guide由来のcontext、時刻、帯域だけを表示しない。

Guideの実装計画は`docs/hypha_post_os_guide_integration_plan_20260831.md`を正本とする。

Kirin OSは保存済みWorkから利用者が確認したPOST一台へ直接送信する。

PREをrelayに使わず、POSTはPREとpairされていなくてもGuideを表示できる。

Guide不在時はFooterのGuide contextを表示せず、測定面の寸法を変えない。

Guide受信時も現在のdomainを自動変更しない。

LEVELはFooterのGuide contextだけを表示する。

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

`RESET`だけが現在のSession統計、generation、履歴をまとめて明示的に破棄する。

Hybrid VUの`CLEAR`はSession破棄ではない。左右の保持TPとVU専用clip表示ラッチだけを解除し、現在TP、VU平均、I、LRA、MaxTP、PLR、LEVEL／CaptureのL/R Session累積clip event、TIME履歴、Record／Keepを維持する。解除時点でもclipが続いている場合は次の100 ms観測で表示だけ再点灯し、同じ連続runをSessionへ二重算入しない。

Record、Keep、Kirin OS接続の状態はMeter Sessionに影響しない。

プロジェクトreloadまたはplugin runtime再生成後は、新しいMeter Sessionを開始する。

I/LRAのgating履歴を完全保存せず累積値だけ復元すると、reload前後で同じ測定事実にならないため、Meter SessionはDAW stateへ保存しない。

Session I／LRA／MaxTPはEBUで処理済みの全10 ms blockを含み、100 ms公開境界より後の処理済み尾も反映する。
Max Mとcurrent／TIME pointの更新は従来の100 ms境界を保つ。公開済みpointは後から変更しない。
独立したsized `MeterSessionV2`でSession統計とprocessed／pending framesを同じlockから取得する。
framesは各channelあたりのsample数で、Active入力数はprocessed＋pendingとなる。Stop／bypassでpendingを消さず、再開は同Session、RESETは全て破棄する。
10 ms未満の未処理尾があるときは、MaxTPを確認済み下限`≥`、PLRを`---`とし、I／LRAは処理済みprefixと明示する。
raw sample peakでtrue peakを代用せず、無音を追加して尾の測定完了を作らない。canonical Record／finalizeとAudio Threadは変更しない。
LEVELのSession I／LRA／MaxTPはこのV2の最新処理済みscopeを使う。TIMEのPLRは同一packetの完全100 ms pointが持つ処理済みprefixの累積値であり、後続のpending入力を含む最新Session全体値とは呼ばない。Active中は元pointの期限内だけ残り、Stopではcurrentを退役する。Sessionを別pollしてTIME pointのPLRを組み替えない。
V2取得のBUSYは出力不変で、同じsourceの直前の整合した表示を保持する。旧Session ABIとimmutable publicationの意味は維持する。

補助exact summary cacheはI／LRA共有で65,536 distinct energy nodesまでとする。量子化・間引きを行わず、上限到達後は補助木を解放し従来のcanonical exact集計へ戻る。
通常queryは木の深さに比例する。相対gateの丸めで参加集合が変わり得る場合と上限後は、同じ履歴のcanonical結果を再利用する。
履歴更新後の最初のfallbackはIで走査、LRAで走査・sortを要するため、上限後のquery costは履歴長に依存する。
上限は補助cacheだけに適用し、canonical gating履歴、Recordの保存量、allocatorやOS RSSの上限ではない。

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
| L/R VU | VU / dBFS | full-wave average、sine校正、直近300 ms、0 VU基準は−12／−14／−16／−18／−20 dBFS（既定−18） | 針 |
| L/R INSTANT TP | dBTP | 最新のexact 100 ms観測 | 1 dB segment |
| L/R HOLD TP | dBTP | Meter Session開始または直近CLEAR以降 | marker |
| BAL | dB | 3 sのL/R energy差 | 0.1 dB |
| CORR | unitless | 3 s energy-normalized correlation | 0.01 |

L/R Sample Peakのhold markerはMeter Session開始後のチャンネル別最大値とし、時間で自動解除しない。

L/R Sample Peakのhold markerは`RESET`だけが解除する。Hybrid VUのL/R HOLD TPとVU専用clip表示ラッチは`CLEAR`でも解除できるが、Sample Peak holdとMeter Session正本のL/R累積clip eventは変更しない。

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

M、S、TP、PSR、CORRを10 Hzで10分、1 Hzで2時間、0.1 Hzで24時間保持する。

TIME HISTORYは主面にM、S、TPを置き、その下の一段にPSR（400 msのsample peak − S）の推移を描く。PSRは曲の中で動くので、Sessionの間ほとんど動かないPLRとCORRは同じ段の数字だけにする。CORRは3 s相関が0を下回った連続区間ごとに、主面の床へ最小点の印を一つ置く（2026-10-06）。PLRはSession全体の値であり、各履歴点には持たない。

ペア時のPSRはmain POST／Δとは独立したtarget・cutoff・proofで自動Δを表示する。PLR／CORRはmainに従う。source・proofが同じmainボタンのPOST／Δ切替ではmainだけを空にして新targetを待ち、PSRのscope・値・履歴と元の完了期限を保持する。PSRを再取得したり期限を延ばしたりせず、source・range・domain変更の退役条件は維持する。

2026-10-08の利用者決定により、PSRは補助情報として主面のM／S／TPを超える大きさ・太さで強調しない。全sizeでlegendと同じ固定font、通常weight、secondary色を使い、`Δ −3.3 dB`／`POST 10.1 dB`のtargetと数値を維持する。計算式は主面に置かず、意味は説明入口で読める。

RUNは選択中のTIME resolutionだけを`generation + run_id`で集約し、別の履歴や永続化を作らない。表示範囲内の経過時間、M min/max、Max TP、L/R clip数を出す。見出しの下に「再生1回ごとに1行（再生から停止まで）」の一文を置き、幅のある行（520 px以上）では列の見出し（RUN、LENGTH、M RANGE LUFS、TP MAX dBTP、CLIPS）を添える。行は読める高さまでとし、少ないrunを画面全体へ引き伸ばさない。clip数の欄は表示する書体の文字幅から決め、300%でも切らない。DAW sample endpointが全点で成立する時は`RUNS IN VIEW`、clock不明のホストでは捏造した区切りを足さず`SESSION RUN`として1本を表示する。resolution混在、不完全なsample endpoint、非単調sample位置は表示しない。PRE/POST間でrun_idを同一識別子として扱わず、RUNのΔは初期契約に含めない。

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

TIMEのSHARPまたはATTACKも、該当subviewを開いたときだけ解析枠を取得する。SHARPはpair未成立時に
既存のPOST absolute timelineを使い、PRE exchange requestを生成しない。

POST FREQは既存Spectrum解析の同じ実測frameから、現在Spectrum、6秒固定長の時間周波数field、rolling peak holdを生成する。

6秒の時間周波数fieldは、100%、125%、150%では縦軸を時間とする平面の濃淡、200%と300%では同じframeを奥へ並べた遠近の地形として描く（INV-S30、INV-S34）。PRE/POST対応のΔ表示は全サイズで平面のままとする。

POST FREQのM/Sは同じstereo入力窓をMID、SIDEの順で解析し、一つの専用frameとして公開する。

M/SはPOSTローカルであり、PRE要求、PRE/POST差分、6秒field、peak hold、MARK、Focus Trailを生成しない。

表示は全5サイズで共通の0〜−96 dBFS軸を使い、MIDをcyan実線、SIDEをviolet実線として色で区別する。
どちらの曲線にも破線や点線を用いない。

このfieldのためにAudio Thread処理、FFT worker、解析slotを追加しない。

履歴はUI側の固定容量180 frame（30 Hz、6秒）に限定し、GUIを閉じている間の永続保持や長時間Spectrogramを約束しない。

PRE不在時もPOST absolute factsは表示できるが、Δ、MARK、Focus Trailを捏造または流用しない。

## 11. Responsive screens

5サイズとPRE、POSTを同じ変更単位として設計、実装、確認する。

| Size | POST required content | PRE required content |
|---|---|---|
| 300×200 | 選択domainの主値、role、pair、POST/Δ、Session state。操作を置かない | S、I（TRACK/STEMではCrest）、MAX TP、name、pair state |
| 375×250 | Compact内容、補助値、domain switch | Compact内容、I/O state、接続context |
| 450×300 | 世界背景を抑えた主visual、軸、session facts | Standard内容、測定stateの詳細 |
| 600×400 | Concept Cのfull cockpit、M/S/I、TP/MaxTP/LRA/PLR/Crest、History凡例のMax M、60秒History、左右TP、POST/Δ、Capture | POSTと共通のshell、広い数値面、接続context |
| 900×600 | 全domain共通Inspection View、拡張History、詳細axis、既存解析の高解像度表示 | POSTと共通のInspection shell、拡張History、詳細axis |
| 450%以上 | 900×600のInspection Viewを画面の画素に揃う段階で拡大（配置は900×600と同一） | POSTと同じ |

小さい画面で情報を単純に縮小しない。

優先度の低い補助値を折り畳み、数字の最小可読サイズを守る。

現行Analysisと一般Editorは5 presetを共有し、Metersだけ固定という分断をなくす。

600×400は二つのトラック比較、および2MIXと単体トラックの二面比較を成立させる主力Observatoryとして維持する。

共通HeaderのMeter Contextは即時toggleにせず、`2MIX`をmix／master busと連続active区間、
`TRACK / STEM`をindividual／group busと短い・疎なeventとして説明する選択menuを開く。
PRE／POST Live Blindは現在の再生から固定MATCHを準備し、Capture操作を要求しない。
復元で失効した試行はSource／回答を閉じ、保持減衰と明示済みENDだけを残す。減衰保持中の新規比較は
主面とMENUで同じ開始条件を使い、上昇量付きRETURNを先に完了する。同一sessionのMATCH→BLINDは継続できる。
MATCHの最終gainが±24 dBを超える場合は理由とENDを示し、準備中表示のまま待たせない。
任意のExact 4 Sでは明示PINで同じ4秒を固定し、準備画面のtagは取得時のpolicyから表示する。
channel数、名前、routing、levelからcontextを推測または自動変更せず、通常画面の行も増やさない。

900×600（300%）は600×400を置換せず、LEVEL、TIME、FREQ、SPACEとTIME配下の解析を同じ操作体系のまま高解像度で読むInspection Viewとする。LEVELは履歴面積、channel strip、数値階層を拡張するが、未合意の新指標は追加しない。将来Session Atlasを載せる場合は別途表示内容を確定する。

Footer（100%と125%では上段2行目）へ置く`VU`ボタンは通常時もHybrid VUを全sizeで前面表示し、同じボタンで選択domainを変更せず元の画面へ戻す。手動選択は読み込まれたplugin instanceのeditorを閉じて再表示しても保持するが、DAW project stateへは保存しない。
DAW hostがRecordを通知している間は、選択domainやPOST/Δを変更せず、一時的なHybrid VU面を全sizeで前面表示する。
停止後はRecord前の画面へ復帰する。
情報メニューの`Show Hybrid VU while recording`は既定ONとし、DAWのplugin stateへ保存する。OFFではRecord中も選択中のdomainを維持する。ONでもHybrid VUの役割表示から情報メニューを開き、`Show selected view for this recording`を選ぶと、そのRecord区間だけ自動表示を解除できる。次のRecord開始時には再びHybrid VUを表示する。
Hybrid VUは左右300 ms平均応答の針、左右100 ms True Peak rail、Session開始または直近`CLEAR`以降の左右最大TP marker、Session累積clip eventから独立した解除可能なclip indicator、M/S・TP・Crestの三値を同時表示し、音種別の目標帯や品質判定を表示しない。`CLEAR`は同じ面の既存button styleで置き、新しい画面を作らない。
Hybrid VU下部の`0 VU = −18 dBFS`を全sizeでクリックし、−12／−14／−16／−18／−20 dBFSから選ぶ。左右は共通の一値とし、exactなPRE／POST pairは同じ基準を使う。選択中の値をcheck markで示す。新しいノブや音量操作は置かない。校正はdBFS値から針位置へ換算する表示設定で、300 msの測定窓・針の時間応答、音声、LUFS、Peak／TP、Session、Record／Keep、plugin_dataとwork.jsonは変更しない。
校正の永続正本は、このcomputerのuser設定領域にあるexact project＋PRE instanceごとの小さい設定fileとする。識別子はlength-delimited keyのSHA-256でpath化し、別chainへ流用しない。PREは自身の解決済みidentity、POSTは選択したPREのexact locator、unpaired POSTは自身のidentityを使う。初期値は−18。identityの取得が一時的に競合する場合は採用済み基準を保持し選択を無効にする。新しいscopeの取得成功後にそのchainの設定を採用する。合法64-byte identityは専用additive getterで完全に取得し、旧DTOの切詰めを共有に使わない。message threadだけが250 ms間隔で設定を読み、明示選択だけが完全なtemporary siblingをatomic置換する。保存失敗は旧値を保持して理由を通知する。不在／破損／未知値は既定−18。pair変更と再openはそのchainの設定を読み直し、DAW state restoreは共有fileへ書かず古い値で他側を巻き戻さない。校正fileはDAW chunkへ含めないので、このcomputerのchain別表示設定として扱う。
host callbackが350 ms以上停止した場合はRecord通知を失効させ、古いREC表示を保持しない。

Recordの中間I／LRAは同じ処理済みprefixのexact-energy cacheを使い、入力blockごとの全履歴走査を避ける。補助cacheはengineごとに65,536 distinct nodesを上限とし、上限／丸め曖昧gateではcanonical exactへ戻る。Max TPと値の有無・表示期限は変えない。Stopは従来のtight-drainとcanonical finalizeを使い、保存Recordの数値・schema・範囲を変えない。

RecordのJSON保存はIO-owned writerで実行する。checksumを空にした正規JSONのbytesへ従来と同じHMACを計算し、最後のchecksum欄へ格納する。二重serializeを一回へまとめても、従来のserializerによる最終bytes、field順序、schema、HMAC、atomic write／renameの意味は維持する。未知のserializer形はcanonical経路へ戻り、encode失敗・unwindでは元のchecksumを復元する。

LEVELの60秒Historyは固定時間軸とし、M主線、TP > -1 dBTPの連続区間ごとの最大TP event、L/R別sample clip eventを表示する。閾値超過がある場合だけ`60 S MAX TP`と相対時刻を表示し、固定2秒区間の最大値とは呼ばない。Sを含む詳細なM/S/TP推移はTIMEへ集約し、LEVELは現在地を読むcontext面として重複させない。TP専用railは作らず、Mが全面を使う同じ横軸の下部へ、右側`-1〜+3 dBTP`軸と下から立ち上がるstemを重ねる。stemはすべて-1 dBTPを超えるので、軸はstemが立つ範囲だけを持ち、0 dBTPに基準線を引く。+3を超える値は上端で止め、印を付ける（2026-10-06）。TPのstemと軸はVUのTP railと同じ水色とし、Mの金と見分ける。中央の`MAX TP`は全Session、Historyは直近60秒という範囲差を文言で固定する。Max MもSession事実としてHistory上部凡例へ置き、現在のM数値内へ混在させない。

PRE／POSTのexact chain action level observation（B-1046以降）はRust／FFIで計測・照合するが、LEVEL HistoryのCHAIN ACTION帯とCompact／Standardのchain summaryはDebug buildの診断表示に限り、製品buildでは表示しない（2026-09-27）。現在の認定hostはWindowsのStudio Pro 8.1.2.113407（VST3）だけであり、表示は認定hostが広がった時点で改めて判断する。

600×400以上のLEVELは、上段3と中段5の合計高を従来割当の約60%へ圧縮し、残りをHistoryへ渡す。FooterもCAPTUREボタン単体ではなく操作段全体を40 pxから24 pxへ縮め、測定履歴を画面の主面積にする。

## 12. Visual system

2026-09-24の常時表示契約のうち、Compactの値の構成は2026-09-26のINV-S38（S、I／TRACK・STEMではCrest、MAX TP）に置き換えた。LEVEL履歴と描画更新については次を維持する。
LEVELの履歴固定・前後TP選択・LIVE復帰は表示だけの操作であり、計測・Record・Session最大値を変更しない。
強い局所発光はTP > 0 dBTPに限定し、TP > -1 dBTPのevent線の高さは実測値を維持する。
host clockの種別が現行history ABIで失われるため、時刻はHOST ~／ELAPSEDと明記し、project timeやexact peak positionを保証しない。
Record表示の同値更新ではbody再描画を要求しない。未表示Record値の変化も描画理由にしない。
共通背景はサイズ・pixel scale・状態明度をkeyにした不透明な合成済みplateを再利用し、
同じ明度での全面alpha合成を毎tick繰り返さない。状態変更時の明度、crop、PRE/POSTの差は維持する。
この表示最適化は計測周期、Audio Thread、履歴・Recordの正本を変更しない。

Concept C Hybrid Observatoryをvisual baselineとする。

主数値とunitはsolidなgraphite panelで保護する。

菌糸は外周、構造境界、history下層、status周辺へ限定する。

[Kirin OS 1.0](https://kirinmastering.com/kirin-os-1-0)下部のJungle世界から、巨大な有機構造、湿度を感じる奥行き、暖色の生活光、疎なcyan signalを取り入れる。

通常時からCE 2226の完成した計器として成立させ、Jungleでは同じ筐体の生命感だけを加速する。
Kirin OSのJungle発動とMASKING GuideのHyphaへの送信成功が両方成立した場合だけ初回発動し、順序は問わない。
連動ポップアップや由来badgeは表示せず、発動後だけ既存Displayメニューの`Jungle Mode`で独立してON／OFFする。
選択は利用者dataとしてPRE／POST、DAW再起動、Project Folder、製品versionを跨いで保持する。

細い蔓を画面へ貼り付けただけの装飾にはしない。

ivoryの数字、cool cyanの実測線、低彩度amberのholdと居住光を基本とする。

600×400以上のLEVEL主数値はneutral whiteではなく暖色instrument ivoryとし、labelを上、unitを下へ分離する。主数値面のHyphaは専用corner素材を外縁から伸ばし、数値を横切る人工的な楕円線は置かない。

数値はtabular figuresを使い、小数点、符号、単位の位置を揃える。

製品UIはKimera KMR Waldenburg Bookを任意の付加価値として利用できる。

既存のKimera App LicenseのKirin Hyphaへの適用可否は、Kimeraを搭載するbuildを作る時だけ確認する。

追加licenseが必要な場合は、確認が完了するまでnative fallbackで配布できる。

Font Software本体はGPLソースへ含めず、Kirin Hyphaを対象にしたApp Licenseの確認後、リポジトリ外の正規OTFからrelease buildへ埋め込む。

Kimeraを使わないbuildも同じ文字役割と固定digit cellを保ったnative fallbackで正式配布できる。Kimeraを指定したbuildだけ、対象App Licenseの確認と外部OTFの存在をgateにする。

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

DRUM／TIMEのtyped snapshot、scope、cutoff、viewport／clock、未処理尾を持つ表示は、その同一presentationを固定してPNGへ描く。
Work添付はv1を維持し、必要なtyped metadataをv1で保持できない場合はunsupportedを明示して添付しない。
同じ凍結PNGのローカル保存を案内し、metadataを捨てたattached成功を返さない。timeout／再試行も明示操作の結果として通知する。

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

公式test set v05の全70素材は、固定`ebur128 0.1.10`とHyphaの`MeasureEngine`でpassした（2026-10-06に再実行）。
サラウンドを閉じている間、5.0／5.1の2素材はreferenceだけで確かめ、Hyphaの`MeasureEngine`には通さない。
内部解析はTech 3341の20 ms alignmentを保持する10 ms、既存GUI、TRACE、IO公開は100 msである。
Tech 3341のM、S、I、Max M、Max S、TP、Tech 3342のLRAと4 reference/alignment素材を公式許容差で検証する。
mono／stereoに加え、役割順が`L, R, C, LFE, Ls, Rs`と一致するexact 5.1を
**計測専用**で受理するmodeを計測coreに持つ。2026-10-06から、サラウンドの受入が済むまで
plugin wrapperは5.1を受け付けない（INV-S33）。開いたときに5.1で提供する製品面はLEVEL／TIME、6役割別Peak／True Peak／Clip、
共通のLoudness／LRA／PLRである。Record／Keep、Reference、PRE／POST Blind、Hybrid VU、
FREQ、SPACEはmono／stereo専用のままにし、5.1では入口を表示しない。5.0、役割不明の6ch、
7.1.4は受理しない。公式5.0/5.1素材のdecode確認と、製品で開いたexact 5.1の数値適合を
混同しない。

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
Kimera KMR Waldenburg Bookは任意で追加できる。OTFを埋め込む場合だけHypha対象App License確認をgateとし、未搭載を公開blockerにしない。
