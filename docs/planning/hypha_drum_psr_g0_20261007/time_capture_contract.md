# G0 — TIME packetとCapture受入契約

2026年10月7日。[親計画](../hypha_drum_psr_usability_improvement_plan_20261007.md)のT1～T6を具体化するG0設計・development証拠。製品source／ABIの実装や受入完了ではない。G1以降を1.1.51へ含め、PR #83のmerge通知後のmainからCodexが実装する。Claudeが途中確認／CI／merge／releaseを担当する。公開前はG3と本人の日常操作・品位、友人G4は今後も公開後だけで開発工程・公開条件に含めない。製品build、CI、実DAW／機器、実Workへの書込みは行っていない。

## 1. 読んだ正本と計測の限界

Hypha読取sourceはB-1300、`bcb9ffd3b3913e9d5c4a526df38b4a7f430be99d`。受信側Kirin OSは`9b2a055801939dbb5dd9d3b08befd17dbb76f2b7`。Hyphaの作業checkoutのAGENTS.mdとrelease入口を読み、既存release静的libを再利用した。静的libのexact source commit provenanceは未確定であり、B-1300の正式build証拠へ読み替えない。

| 再利用物 | 確認した値 |
| --- | --- |
| 静的lib | `target/release/libkirin_hypha_ffi.a`、SHA-256 `c2a4af783bae128d6cd11f2e3be61d035ffb66ee3b0a17323fac59ad97106eac` |
| ABI probe | revision 7＝header 7、MeterHistoryEntry 248 bytes、MeterSession 1840 bytes。epoch offset／MeasureResult sizeも一致 |
| 実行 | macOS x86_64、48 kHz stereo、480-frame block、PRE／POST同一997 Hz正弦波、明示選択single pair |
| 隔離 | `/tmp`のdriverのみcompile。使い捨て`KIRIN_HYPHA_TEST_STORAGE_ROOT`。Record／Keep／Listen／外部接続なし |
| 保存証拠 | [measurements.json](measurements.json)。最終2 runの全records、集計、probe source／binary hashと再現用sourceを含む |

現行PRE IOは100 ms sleep（`crates/kirin_measure/src/io_thread_pre.rs:51`）、POST IOも100 ms sleep（`io_thread_post.rs:165`）。POSTはscalar publicationとexact joinを順にserviceする（`io_thread_post_observation.rs:70–89`、`io_thread_post_analysis.rs:103–120`）。PREの最近32 exact pointはatomic fileへ公開する（`crates/kirin_measure/src/meter_history_publisher.rs:99–154`）。これらは周期設定であり、最大到着遅延の保証ではない。

### 1.1 既存native経路の短時間観測

各3.6秒、0.6秒warm-up除外、poll予定100 ms。file observerは2 ms sleep。同一driver loopで音声供給とfile観察を行った。下表はinput slotの最終blockを両handleへ**供給した時刻**から初めてそのendpointを観測するまでのms、表記はmedian／nearest-rank P95／max。測定workerのslot完了時刻からの遅れではない。

| 経路 | poll位相30 ms | poll位相75 ms |
| --- | --- | --- |
| PRE local native履歴 | 138.64／160.51／239.79（28点） | 85.47／191.59／204.49（29点） |
| POST local native履歴 | 138.44／140.55／160.51（28点） | 85.29／184.78／191.59（29点） |
| PRE file exact publication | 188.46／321.38／331.80（27点） | 183.39／303.07／306.09（28点） |
| Δ native履歴 | 339.94／539.09／539.35（27点） | 294.14／484.99／510.83（26点） |
| 各pollの最新Δ・供給起点age | 339.51／539.87／540.88（30 tick） | 289.16／483.77／486.12（30 tick） |
| local C−比較E | 200／400／500（30 tick） | 200／300／400（30 tick） |
| native poll実間隔 | 99.93／116.73／121.94（29区間） | 99.92／120.15／126.05（29区間） |

observer処理mean／maxは0.45／20.39 msと0.26／36.59 ms。warm-up後のinput slot供給lateness maxは21.90／17.90 ms（全run max103.63／34.84 ms）。PRE local poll falseは2/30／0/30、POST／Δは各0/30。供給transaction拒否0、終了pair statusは両runで2。実DAWのAudio Threadとは異なる供給・scheduler・観察負荷を含む。

旧ABIに原slotのMeasure Thread完了monotonic時刻がないため、**完了起点400 ms TTLは未検証**。native初観測は10 Hz取得による発見上限で、正確なΔpublication時刻ではない。旧local／Δ pollは別APIであり、C−Eは新atomic packet成立の証拠ではない。C−E≥400 msが6/30、1/30 tickあったため、`C−E<400 ms`をこのendpointへ適用すると空欄になるtickがある。ただし、これだけでそれを本番の正常pendingか本当の期限超過かに分類しない。

400 msを空欄の出ない正常遅延保証としない。新slot完了timestamp・controlled scheduler・測定負荷を備えるG1検証で期限と「不要な空欄0」を両立させる根拠を得る。旧lib観測だけで期限を恣意的に延長しない。DRUMのlook-behind Lも別経路の計測が必要で、このTIME観測から150 msを校正しない。

## 2. 新TIME packetの設計契約

以下は未実装の新版。既存frame／履歴APIや旧wireへ保証を遡って付けない。

| 単位 | packetに保持する事実 |
| --- | --- |
| 共通header | version／size／revision、selection intent、共通local cutoff C、sample rate／layout、signal／transport状態、POST span token |
| authority／proof | owner／POST claim、opaque binding revision、exact PRE locator、両source epoch／incarnation／generation／run／clock／declared span。generation一致だけでは比較proofにしない |
| main component | 明示されたPOST／Δ（PRE shellはPRE）target、独立cutoff／proof、同cutoff M／S／TP／PLR／CORR frame、原slot完了時刻、current状態、gap付きmain history |
| PSR component | PRE／POST単体または選択ペアの自動Δtarget、E≤C、原raw PSR／最新None、独立proof／完了時刻／状態、gap付きPSR history |
| presentation寿命 | 各currentの残り期限・期限理由、currentとhistory HOLDの区別。元の完了時刻はpoll／IO service／同endpoint再joinで更新しない |

Measure Threadで最大64件の未集約100 ms raw tailに同slot frameとPSR Noneを保持する。currentをhistory mean／decimation／最後のfinite値から復元しない。新monotonic completion timestampは非RT側のslot完成時に一度だけ採番し、同endpoint再公開でも維持する。異なるprocessの絶対monotonic数値を比較せず、単体は自分の原local slot、POST比較は原POST slotと対応付けたage／残り期限をGUI受信時のmonotonic基準へ変換する。

取得順はauthority前読み→session `try_lock`でraw／frame／local historyをcopy→解放→exact exchange `try_lock`でbinding付き比較をcopy→解放→authority／POST span／signal後読み→全field整合検査→全出力commit。複数lockを同時に持たず、GUI pollでfile joinしない。通常C進行だけでrejectせず、source／authority変更を検出する。最終検査がlinearization point。race、null、短buffer、未知versionでは全出力不変で無言skip。

authorityと比較producer bindingだけが異なる時は、新authorityの比較waiting componentを成功返却する。main=POSTは同POST frame Cを継続し、main=Δはmainもwaiting。Cは軸の根拠であり、選択ΔをPOST値へfallbackしない。旧`build_observatory_frame()`のlegacy delta別取得を新TIMEへ混ぜない。

### 2.1 時刻、状態、履歴

global POST mainのC進行をPRE待ちへ巻き込まない。mainΔは独自cutoff／proof、PSRΔはEを持つ。共通軸はCで非減少、PSR履歴はEで止め、E～Cを未観測のまま残す。PLRの可視性とCORRの値／軸／helpはmain targetに従う。TIMEはnative100 msでpacketを一括applyし、PSRだけ4 Hzで旧valueを保持するcomposerは作らない。

正常publication待ちだけ、同じ完全binding／両span／runでActiveの直前比較を、原POST slot完了から400 ms未満かつC−Eも400 ms未満の設計期限内で保持し、毎tickの通知／色往復をしない。400 msは第1章の未検証設計値。None、後続確定invalid、対応不能、stop、bypass、clock／run失効ではcurrentを即退役し、`Δ ---`等の対象付き欠測にする。単体currentにも原完成起点の期限を適用する。GUIは連続poll falseでもmonotonic残り期限を減らす。

選択callback／既存lifecycle境界で影響componentを先に退役させる。pair／PRE失効は比較component、POST span／rate／layout／Resetは両component。退役直後のpoll falseで旧current／shapeを復活させない。同source過去履歴はHOLD、run／seekはsegment境界、別sourceは破棄する。退役packetへ新frameだけを足さず、Captureへ渡さない。

履歴はNone、未対応endpoint、slot抜け、clock／run境界を跨がず集約する。entryはfirst／last endpoint、total／valid count、min／max／mean、接続可否を保持し、平均分母はvalid count。欠測0置換はしない。共通rangeは`[max(0,C−durationFrames),C]`、各componentデータは自身cutoff以下。両端bucketは保持exact pointから正確なprefixを再集約できる時だけ採用し、不能なら欠線。座標clampでfuture meanを残さない。

固定multi-rate retentionと最近10 Hz exact historyを再利用し、64 raw tailを全履歴へ拡張しない。出力はmain／PSR各1200件以下、予算超過は古いsegmentから落とす。compact PSRはcapacity=0で追加poll／cloneなし。新ABI入口はversion／caller size／count／revisionを持ち、旧structと`KirinAbiContract`layoutを拡大しない。起動時に新shell／旧lib混在を拒否する。

## 3. G0参照sourceのCaptureをfixtureで確認した結果

G0で参照したB-1300のHypha `juce_shell/src/CaptureWorkAttachment.cpp:177–207`はversion1.0・19 fieldのrequestとPNGを`hypha_capture/v1`へ発行する。request IDは`juce::Uuid().toString()`（compact32hex）。`CaptureWorkAttachmentTest.cpp:44–47`もその形式を期待する。一方、Kirin OS `src/port/hypha_capture/contract.cjs:9,54`と`store.cjs:19,289–290`はdashed UUIDだけを受理・走査する。

| 使い捨てconsumer fixture | 独立期待値 | 実結果 |
| --- | --- | --- |
| C1 compact UUIDのv1 request | UUID検証で拒否 | `ok=false`、`request_id`。compact filenameはpoll対象0、requestが残る |
| C2 正規dashed UUID、他は同一v1 request | 受理 | `ok=true`。fake WorkへPNG byte identityを維持してattached |
| C3 C2へcomponentsを一項目追加 | exact fieldsで拒否 | `ok=false`、`fields` |
| C4 C2の保存Work readback | 現保存項目だけが残る | `type/path/notes`のみ。domain／observation_target／component metadataは永続化されない |

純validatorと既存store factoryのdependency injectionを使用した。fake Work／index／connection／write authority／plugin rootは全て`/tmp`。実Work／real production_assetsに触れていない。PNGは既存45-byte envelope fixtureであり、product画面のdecode／見栄え検証ではない。probe source hashと結果は[measurements.json](measurements.json)へ保存した。

receiverは19 field完全一致（`contract.cjs:32–50`）、receiptも10 field完全一致（`:79–96`）。現在の60秒leaseは添付要求の寿命でありraw測定TTLではない。Work assetは`store.cjs:193–198`の`{type:'other',path,notes}`。既存assetの再受理もPNG hashだけ確認する（`:172–179`）。`schemas/work.schema.json:533–557`にcomponent metadataの型付きfieldはない。unknown fieldの保存・継承を推定しない。

したがって、**現sender request ID、v1へのcomponent追加、componentの永続化の三境界は不適合**。新packetと画像のtarget roundtripを現経路で確認済みとは言わない。UUID修正はPR #83のB-1306に含まれる。merge後のmainを再照合し、過去C1を現在候補の不適合と決めつけない。UUID修正だけでmixed-target metadata保持まで受入済みにはしない。採用方式に応じてv2 round-tripまたはv1明示失敗をG1以降で確認する。

## 4. G0終了時に提案するCapture方式

Capture二案を提案し、2026年10月7日に利用者が「v1を維持し、失敗を明示する」を採用した。1.1.51は保持不能なWork添付の明示失敗とローカルPNG保持を実装・検証する。v2とKirin OS側の別repo変更は今回対象外。以下には比較した二案と未採用v2の設計を残す。

| 案 | 必要な変更と公開前受入 |
| --- | --- |
| v2を1.1.51に含める | Hypha senderに加え別repoのconsumer／Work schema／read-update-backup-restoreを整合し、PNG／metadataの両hash・stampとround-tripを受入する |
| v1を維持し、失敗を明示する | 表現・保持不能なsnapshotのWork添付は明示失敗とし、ローカルPNGを残す。metadataを捨てたattached成功は禁止。既存v1添付は意味を保持できる検証済み範囲だけ維持し、失敗・通知・timeout／再試行を本人確認まで含めて受入する |

以下4.1～4.3は未採用v2の設計資料。今回の実装・公開条件には含めない。v1の失敗・通知・ローカルPNG保持をG2で受入する。

### 4.1 v2候補のfreezeとrequest

採用済みPresentationSnapshotへ期限とcomponent退役を適用してからfreezeし、PNGとmetadataを同じPresentationRevisionから作る。Capture時の再poll／source再選択／統計再計算は0。legacy `target`／Work `observation_target`はmain targetを指す意味を維持し、metadataのcomponent targetでmain POST＋PSRΔを表す。過去履歴HOLDと現在scalar失効を別fieldへ保存する。

新request／receiptのversionを2.0、transport rootを`hypha_capture/v2`としてv1と区別する。v1の19 fieldへ追加して同じversionで送らない。request IDは生成時にcanonical lowercase dashed UUIDへ統一し、file／PNG／metadata／receiptで同じ値を使う。読んだJUCE実装はconstructorでRFC v4 bitsを設定し、`toDashedString()`を持つ（`juce_Uuid.cpp:26–35,92–99`）。文字置換だけで任意IDを有効UUIDへ変換しない。

新版consumerのsupported-versionとmetadata保存能力は、現在のWork bindingに結び付くread-only capabilityとして確認する。capabilityは添付のwrite authorityを与えない。旧consumer／未知version／timeout／metadata不適合では明示添付失敗を通知し、ローカルPNGを使えるようにする。内部fallbackの無言skip規則を明示操作へ適用しない。新版requestをv1へ縮退してmetadataを捨てない。

### 4.2 bounded metadata

requestは既存16 KiB上限、PNGは16 MiB上限を維持する。immutable UTF-8 JSON sidecarを`<request_id>.capture.json`とし、metadata上限256 KiBを新設する設計。requestにmetadata file名／byte length／SHA-256／PresentationRevision／schema versionを追加する。canonical byte列のdigestを送り、receiverが再serializeしたJSONのhashへ置換しない。

| metadata項目 | 保存する意味 |
| --- | --- |
| snapshot／presentation | schema、SnapshotRevision／PresentationRevision、domain、display kind、locale／size、表示時のsignal／transport状態 |
| component[main,psr] | target、cutoff、opaque source／binding proof、raw current presence／None／waiting／expired、表示文字とunit、原slot age・残り期限・期限理由、history HOLD |
| axis／history | 共通Cとrange、各component cutoff、gap／segmentのendpoint区間とvalid／total count。旧bucketから推測したgapを作らない |
| DRUM | event／cohort鍵、四lane型付き端点と表示文字、renderKind／scope、finish状態、C／V／clock／L。TIME metadataへDRUM値を混ぜない |

u64 sample endpoint／revisionはdecimal stringでJSONへ保存し、JS Numberの精度上限を超えて丸めない。current absentに0を埋めない。source／bindingはopaque identityを使用し、private file path、owner管理情報、Work管理URLをsidecarへ入れない。componentは2以下、history区間は出力合計2400以下、DRUM cohortは8以下、event locatorは既存固定上限。区間はcompactな型付き配列とし、元frame／raw PCM／全履歴を複製しない。最長identity・最大countのencoder fixtureで256 KiB内を確認し、超過はcritical fieldを捨てず添付失敗にする。

期限は**freeze時点の表示事実**として保存し、Workで数時間後に開いた画像をlive値へ再判定しない。元slot ageと残り期限を固定する。PNGの表示丸めをraw区間端点へ書き戻さない。Work／binding／runtimeの既存private authority fieldはrequestで照合し、sidecarのopaque測定proofと混同しない。

### 4.3 Work保存とreceipt proof

receiverはrequest／PNG／metadataのbounds・version・hash・same stamp・component整合を先に確認し、既存POST Work bindingとwrite authorityをcommit直前にも照合する。PNGとsidecarを同じWorkの`captures/hypha`へimmutable保存し、その参照を同じindexed Work transactionへ追加する。Work commit前の失敗は今回新規作成した両ファイルだけをrollbackし、既存添付を消さない。Work commit後のreadback／receipt失敗ではcommit済み添付を破壊せず、attachedを返さず同ID再試行で検証を再開する。

`production_assets`は既存`type:'other'`を維持し、新しいtyped optional `hypha_capture` fieldへmetadata version／relative path／SHA-256／PresentationRevisionを保存する。Work schemaとread／update／backup consumerのroundtripを明示試験する。generic unknown fieldの保持任せにしない。再試行時は同request IDでPNGとmetadata**双方**のhash・persisted stampを照合し、PNG一致だけでidempotent成功にしない。

v2 receiptはauthority identityに加えartifact SHA-256／metadata SHA-256／PresentationRevisionを含み、Work commit・両ファイルreadback確認後にattachedを返す。Hyphaもfreezeした両digest／stampと一致するreceiptだけを受理する。これはbyte保存と対応を確認するproofであり、測定の真実性や実機受入をreceiverが新たに認定するものではない。

## 5. G1以降の変更範囲と採用方式別fixture

| 層 | 対象source／正本 |
| --- | --- |
| TIME producer／exchange | `crates/kirin_measure/src/engine_readout.rs`、`meter_history.rs`、`meter_delta_history.rs`関連、`meter_history_publisher.rs`、`io_thread_post_observation.rs`／`io_thread_post_analysis.rs`、source authority／session lifecycle |
| C ABI | `crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h`の新独立header／入口、対応Rust poll、ABI照合・fixture。旧`abi_contract.rs` layout不変 |
| JUCE TIME | `PluginProcessor*`の新coherent取得、`PluginEditorAnalysis.cpp`／lifecycle、`HyphaTimeHistory*` layout／painter／component、target help |
| JUCE Capture | `HyphaCaptureContract.h`、`HyphaObservatoryCapture.cpp`、`PluginEditorCapture.cpp`／`PluginEditorLifecycle.cpp`、`PluginProcessorGuideTransport.cpp`、`CaptureWorkAttachment.h/.cpp`とnative tests |
| OS consumer（別repo、v2選択時だけ変更、今は無編集） | `src/port/hypha_capture/contract.cjs`／`store.cjs`／`__tests__/store.test.cjs`、`schemas/work.schema.json`、既存Work read／update／backup経路、Capture契約doc |
| 同期doc | 親計画T1～T6、`hypha_meter_product_contract_20260831.md`、`hypha_invariants.md`、`hypha_observation_loop_work_attachment_20260901.md`、OS Capture契約 |

巨大fileは変更責務を先にowned moduleへ分離し、既存line budgetを増やさない。追加解析はMeasure／worker、IOは非RT。Audio Threadへalloc／lock／blocking IOやA経路変更を入れない。

| 独立fixture | 期待する結果 |
| --- | --- |
| T-C/E | main=POST Cが100 ms進みPREは2 tick遅れる：main frame／PLR／CORRはC、PSRはE、共通C軸は後退せず、将来値なし |
| T-expiry | 原完了deadline直前／同値／超過、再join、poll連続false：strict `<`境界通りcurrentだけ失効、pollで寿命延長0 |
| T-None | 最新raw None、後続invalid、slot gap：直前finiteを復元せず、該当segment断線。normal待ちと別期待値 |
| T-race | authority前後にpair／POST span変更、Reset直後poll false：出力不変または新waitingの完全packet、旧比較復活0 |
| T-prefix | endpoint既知の小配列、partial bucketと保持exact不足：手計算prefixのみ採用、不能なら欠線、future mean漏れ0 |
| T-ABI | 旧caller／旧lib、新caller short buffer、未知version、null：canary／全出力不変、起動時混在拒否 |
| C-v1 | 本書C1～C4：compact UUID拒否・dashed受理・unknown field拒否・旧保存metadata欠落を独立期待値として保持 |
| C-v1-failure（v1維持時） | 表現・保持不能なsnapshotは誤attached0、明示失敗とローカルPNG維持。対応範囲の通常添付、timeout、再試行、理由表示を受入 |
| C-v2（v2採用時） | main POST＋PSRΔのPNG／metadataをfreeze：fake Work readback後に両hash・target／C／E／HOLD／gap／期限・stamp一致 |
| C-retry（v2採用時） | 同ID同両hashは一添付、metadataだけ異なる同IDは拒否。receipt hash改変／旧receipt／unsupported consumerは明示失敗 |
| C-rollback（v2採用時） | PNG後／metadata後／Work transaction後に注入失敗、authority変更、lease超過：半添付・既存添付削除0、誤attached receipt0 |
| C-roundtrip（v2採用時） | Work通常read／update／backup／restoreを通してtyped参照とsidecar bytesを保持、最大metadata／不正u64／path traversalを拒否 |

上表のT新版とC-v2以降は未実施。G0で実施したのは既存ABIの限定development観測と現consumerの使い捨てC1～C4だけ。新TIME snapshot、400 ms完成起点TTL、採用Capture方式の受入、通常A経路回帰、本人の日常操作／品位とmotion受入は公開前のG1～G3／本人確認へ残る。友人の初見等は今後も公開後G4だけ。G0設計資料が揃うことと製品gate PASSを分ける。
