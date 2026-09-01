# Hypha POST OS Guide integration plan

Status: active joint plan with Hypha Meter (resumed 2026-08-31)

Date: 2026-08-31

Branch: `codex/hypha-meter`

ATTACK統合後のworkspace testがgreenになったため、Daisukeの判断に従ってMeter本体と共同で進める。

POST版の受信、表示、移行Gateが成立するまで、現行PREのprotocol、receiver、routing、UIを維持する。

Implementation status:

- B-583: Kirin OSの現行MASKING v1.1 fixtureとHypha consumerをbyte同期した。
- protocol v3 guide target: `exact_hypha_binding + target_role=post`のproducer/consumer fixtureを追加した。
- v3 transport identity: presence、capability、connection、active pointer、clear authority、acknowledgementをPOST roleとexact runtime identityで照合する契約を追加した。
- POST receiver worker: PREと同じ低優先度workerをPOST targetにも組み込み、Audio Threadはlock-free clock publishだけに限定した。POST VST3 Release buildで組込みを検証済み。
- B-586〜B-589: Hypha POST receiver、接続確認、Guide rail、FREQ投影を実装した。
- W-2836: Kirin OSのINSPECT／MASKING既定送信入口をPOSTへ変更し、旧PRE bindingを明示再接続までfail-closedにした。

## 1. Decision

Kirin OSのINSPECTとMASKINGから送るGuideの主表示先を、Hypha PREからHypha POSTへ変更する。

PREが主表示先だった理由は、当時のPRE画面に表示余地があったためである。

POSTを2MIXの最終段へ常設し、Hypha Observatoryの親Shellとして使う新構成では、利用者が継続して見るPOSTへGuideを直接送る方が製品動線と一致する。

Kirin OSからPOSTへ直接接続し、PREを中継器にはしない。

PREからPOSTへのrelayは、Guide表示をpairing状態へ依存させ、接続先と受信確認を二段に分裂させるため採用しない。

## 2. Current verified mechanism

現行のPRE Display配送は、単純な文字列通知ではない。

Kirin OSは保存済みWorkと利用者が確認したHypha PRE一台をlocalかつexactにbindingする。

INSPECTは選択した一件の検出について、時刻、区間またはinstant、表示名、source、channel、任意の周波数帯を送る。

MASKINGは選択した時間範囲、frequency focus、source pair、実測collision interval、測定できた周波数帯とfrequency basisを送る。

HyphaはDAW project clockをsource zeroへ投影し、`RECEIVED`、`NEXT`、`CUE`、`ACTIVE`、`HELD`、`END`、`PAUSED`を区別する。

Guideは明示的なEndまたは置換まで保持され、再生停止、presence lease失効、acknowledgement lease失効だけでは消えない。

transportはpresence、capability、connection request、active pointer、artifact、acknowledgementを分離する。

Guide artifactはSHA-256でbyte完全性を検証し、不一致時は直前の有効Guideを保持する。

このtransport、保持規則、clock projection、receipt、完全性検証をPOST移行後も再利用する。

## 3. Product role

Guideは第五の観測domainにしない。

Guideは`LEVEL / TIME / FREQ / SPACE`へ必要な事実だけを投影するcontext layerとする。

Guideが存在しないときは画面上の占有面積を0にする。

Guide受信によって表示中domainを自動変更しない。

利用者がGuide railを選んだときだけ、対応するTIMEまたはFREQへ移動する。

POSTはPREとpairされていなくてもGuideを表示できる。

pair済みの場合だけ、Guideと同じ時刻または帯域に現在のPRE、POST、Δ観測を併置できる。

## 4. Authority separation

Kirin OSとHyphaが測った事実を一つの測定結果へ混ぜない。

| Authority | Label | Facts |
|---|---|---|
| Kirin OS | `OS GUIDE` | 保存されたINSPECT detection、MASKING selection、time、band、source、channel、pair label |
| Hypha POST | `LIVE POST` | 現在のPOST信号からHyphaが測った値 |
| Hypha pair | `LIVE Δ` | 現在のexact PREとPOSTからHyphaが算出した差 |

OS GUIDEの時刻と帯域は、LIVE POSTまたはLIVE Δを絞り込む観測contextとして使う。

OS GUIDEを現在の再測定値、原因推定、改善結果へ昇格しない。

MASKING Guideは二つのsourceについて保存された測定事実である。

2MIX POSTはsourceを分離できないため、現在のステレオ出力からMASKINGを再測定したとは表示しない。

INSPECTのtype labelをHypha ATTACK、SHARP、SPACEの測定結果として扱わない。

## 5. Target and connection contract

protocol v3でtargetをPRE専用からrole-neutralなexact Hypha bindingへ変更する。

| Field | Contract |
|---|---|
| `selection_mode` | `exact_hypha_binding` |
| `target_role` | 初期公開では`post` |
| `work_id` | producer Workと一致 |
| `binding_id` | 利用者が確認した接続identity |
| `runtime_instance_id` | 一台の受信instance |

Kirin OSの既定操作名は`Send to Hypha POST`とする。

POST側はpresenceとcapabilityにroleとGuide表示能力を公開する。

Kirin OSはcapabilityを確認したexact POSTだけへactive pointerを発行する。

複数のPOSTへbroadcastしない。

別Work、別binding、別runtime、PRE roleはv3 POST Guideを受け取らない。

接続先POSTが不在または非対応の場合、送信操作を成功扱いにしない。

## 6. Parent Shell integration

各POST instanceのHypha Observatory Shellが三つの独立snapshotを受け取る。

```text
Hypha Observatory Shell
├── MeterSnapshot
├── AnalysisSnapshot
└── GuidePresentationSnapshot
```

親ShellはGuideの状態、表示寿命、domainへの投影を管理する。

LEVEL、TIME、FREQ、SPACEの子Viewはtransport file、Guide artifact、acknowledgementを直接読まない。

Guide receiverは既存どおりworker threadで動かし、Audio Threadへfile I/O、JSON parse、allocation、lockを持ち込まない。

`GuidePresentationSnapshot`は次の事実をimmutableに渡す。

- Guide identity、revision、payload kind
- projection statusとproject clock state
- focused、active、cue、held、nextのitem identity
- source、channel、source pair label
- temporal kind、start、end
- optional bandとfrequency basis
- overlapping item count
- fact availabilityとtruncation state

Shell用snapshotは現在の主factと次のfactを固定上限で持つ。

TIMEとFREQ用のwindow batchは表示範囲内のfactを最大64件まで返し、残りがある場合は件数を明示する。

2048件のGuide document全体をpaint threadへ毎tick copyしない。

## 7. Domain projection

| Domain | Guide presentation |
|---|---|
| LEVEL | Header下のGuide railだけを表示し、meter値とscaleを変更しない |
| TIME | INSPECTのinstantまたはinterval、MASKINGのreview selectionとmeasured collision intervalを時間軸へ表示する |
| FREQ | INSPECTにbandがある場合のbracket、MASKINGのfrequency focusとmeasured bandを別の形で表示する |
| SPACE | 対応するGuide事実がないため投影しない |

MASKINGの`frequency_state = unlocated`にはbandを描かない。

frequency focusは利用者が選んだ範囲、measured bandはKirin OSが測定した範囲として線種を分ける。

INSPECT instantの1 ns wire sentinelを実測durationとして描かず、point markerへ戻す。

`HELD`は過去のINSPECT contextであり、現在activeの測定区間として発光させない。

Guideのmarker、range、bandは品質色を使わない。

## 8. Interaction

Guide受信時は、現在のdomainを保ったままHeader下へ`INSPECT`または`MASKING` railを出す。

railは現在状態、時刻、主label、bandの有無を一行で示す。

railを選ぶと、時間事実だけならTIMEを開く。

bandを持つGuideではTIMEとFREQの明示的な行先を提示する。

Guide受信だけでtransport再生、loop、seek、pair変更、Reset、Captureを実行しない。

Kirin OS側のEndはGuideだけを消し、Meter Session、Analysis、pairingへ影響を与えない。

CaptureへGuideを含める場合は明示toggleを要求し、Work名、source path、instance identityを既定で出力しない。

## 9. Responsive behavior

PREとPOSTの5サイズを同じ変更単位で検証する。

| Size | POST Guide behavior |
|---|---|
| 300×200 | 一行railと状態point。選択時だけ主factをmetric area外へ展開する |
| 375×250 | railに時刻とlabelを表示し、bandの有無を示す |
| 450×300 | railと選択domainのmarkerまたはbandを同時表示する |
| 600×400 | rail、TIMEまたはFREQ projection、LIVE POSTまたはLIVE Δを同時表示する |
| 900×600 | 600×400と同じauthorityを保ち、全projectionをInspection解像度で表示する |

旧PRE fallbackを残す期間は、PREも共通visual tokenへ更新し、従来の二行Guideを旧画面だけに残さない。

## 10. Migration

v1.0、v1.1、v2.0のPRE Guide artifactは既存PRE readerで受信できる状態を保つ。

v3対応Kirin OSは、利用者が接続したPOSTのcapabilityを確認できた場合にPOSTを主送信先とする。

旧Hyphaしか見つからない場合は、利用者が明示的に選んだときだけPRE互換送信を使う。

POST送信に失敗した後、無言でPREへ送り先を変更しない。

PRE互換送信を使った場合は、Kirin OS側に`Sent to legacy Hypha PRE`と事実を表示する。

POST普及後もv1.0、v1.1、v2.0 parserは既存projectのGuideを壊さないために保持する。

PRE専用UIを削除する時期は、POST版のmacOSとWindowsが同一versionで公開され、Kirin OS側の接続migrationが完了した後に決める。

## 11. Implementation phases

### Phase 0: Baseline freeze

現在のPRE Display fixture、protocol test、projection test、retention test、acknowledgement testを移行前baselineとして固定する。

PRE-only invariantのうち、target roleだけを変更する項目と保持する安全項目を分類する。

### Phase 1: Protocol v3

role-neutral exact binding、POST capability、POST acknowledgementをschemaとcross-boundary fixtureへ追加する。

Kirin OSとHyphaの両repositoryで同じfixture bytes、hash、semantic validationを確認する。

### Phase 2: POST receiver

PRE専用compile boundaryからtransport controllerを切り出し、POSTでも同じworkerを起動できるようにする。

receiver起動と終了がWatch、Record、Keep、Pairing、Analysisへ依存しないことを検証する。

### Phase 3: Structured presentation snapshot

既存の二行`DisplaySnapshot`を互換表示として残し、親Shell向け`GuidePresentationSnapshot`とbounded window batchを追加する。

文字列から時刻や帯域を再parseせず、検証済みGuideModelから型付き事実を投影する。

### Phase 4: Global Guide rail

POSTの全domainと5サイズへGuide railを追加する。

Guide不在、受信待ち、project clock待ち、active、held、end、paused、rejectedを同じfixtureでrenderする。

### Phase 5: TIME and FREQ projection

TIMEへinstant、interval、review selection、collision intervalを追加する。

FREQへfocus bandとmeasured bandを追加し、band unavailableを描画しない。

### Phase 6: Paired observation

Guideの時刻または帯域と、現在のLIVE POST、LIVE Δを同一画面へ併置する。

OS GUIDEとHypha実測はlabel、line style、snapshot authorityを分離する。

### Phase 7: Kirin OS default route

Kirin OSの接続、送信、受信状態、End、DisconnectをPOST中心の文言と動線へ変更する。

旧PRE artifactのparser／store互換だけを保持し、現在のUIにはPRE fallbackを出さない。

旧PRE bindingを検出した場合は`role_mismatch`としてfail-closedにし、利用者がPOSTへの再接続を明示する。

### Phase 8: Cross-platform release gate

macOSとWindowsの同一commitでPOST receipt、clock projection、UI、legacy PRE artifact互換を検証する。

公開3チャネルを同一versionで揃えるまでmigration完了としない。

## 12. Verification gates

### Transport

- exact POST一台だけがGuideを受信する
- 別Work、別binding、別runtime、PRE roleが受信しない
- POST不在、capability不在、ack不在、rejected receiptを区別する
- artifact破損時に旧Guideを保持する
- End、replace、disconnectのscopeがGuideだけに限定される
- future lease、stale lease、再起動後、file不在を確認する

### Projection

- project zero、sample rate変更、transport停止、seek、loopで時刻を検証する
- half-open interval、instant sentinel、overlap、active優先、focus優先、heldを確認する
- MASKING focusとmeasured bandを混同しない
- unlocated frequencyからbandを生成しない

### Product behavior

- Guide受信がdomain、pair、Meter Session、Analysis selectionを勝手に変更しない
- unpaired POSTでもGuideが表示される
- paired POSTだけがLIVE Δを併置する
- 2MIXからMASKING再測定を称するcopyを表示しない
- Guide不在時の占有面積が0である

### Real-time and stability

- Audio Threadのallocation、lock、I/Oが0件である
- receiver panicとmalformed GuideでAudio Threadが継続する
- Guide worker停止時もMetersとAnalysisが継続する
- PREとPOSTの音声がbit identicalでレイテンシー0 samplesを保つ

### Visual

- PREとPOSTの5サイズを一括renderする
- INSPECT、MASKING、NEXT、CUE、ACTIVE、HELD、END、PAUSEDを確認する
- grayscaleとreduced motionで意味が保持される
- GuideとLIVEのauthorityが色だけに依存しない

## 13. Non-goals

HyphaからKirin OSのINSPECTまたはMASKINGを実行しない。

POSTのステレオ信号からsource分離またはMASKING再計算を行わない。

Guideを処置、推奨、合否、品質scoreへ変換しない。

Guide受信を条件に音声、DAW transport、plugin parameterを変更しない。

Guide artifactへsource audioを含めない。

直接SNS投稿とserver同期を追加しない。

## 14. Integration order with ATTACK

ATTACK完了点`d464f71`はMeter branchへ統合する。

POST Guideを再開する場合は、統合baseline上でGuide receiverをATTACK workerとは別のoptional subsystemとして接続する。

ATTACK、FREQ、SHARPのanalysis leaseとGuide receiverの生存期間を共有しない。

Guide railを閉じてもGuideをEndせず、Kirin OSのEndまたはreplaceまでretention契約を保つ。

この分離により、ATTACKの精度と資源制御へ影響を与えず、POSTをKirin OSの主観測窓へ拡張できる。
