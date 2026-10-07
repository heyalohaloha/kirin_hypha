# G0 — DRUM／PSR snapshot契約

2026年10月7日。親計画は[DRUM・PSR改善計画](../hypha_drum_psr_usability_improvement_plan_20261007.md)。本書はG0の設計決定であり、製品に実装された契約ではない。G1以降を1.1.51へ含め、PR #83のmerge通知後のmainからCodexが実装する。Claudeが途中確認／CI／merge／releaseを担当する。公開前はG3と本人の日常操作・品位、友人G4は今後も公開後だけで開発工程・公開条件に含めない。検出器変更・新しい検出精度受入は別計画へ渡す。

## 1. 確認したsourceと今回の範囲

読取sourceはB-1300、commit bcb9ffd3b3913e9d5c4a526df38b4a7f430be99d。現行checkoutのAGENTS.mdとrelease入口を確認した。本書は製品source、ABI、wire、DAW stateを変更しない。数値・寿命・互換性の規則を具体化し、未確認の保証を設計決定と区別する。

| 現行の確認事実 | source根拠 |
| --- | --- |
| 旧Summaryは成立した打音を遡って最大8件集め、pendingをNへ数えない | crates/kirin_hypha_ffi/src/attack_ffi_band_summary.rs:159–177、322–343 |
| HEAD96点／TAIL64点、7秒の帯域PCM保持、event history上限240 | crates/kirin_measure/src/attack_band.rs:27–45、attack_runtime_state.rs:5–7 |
| band workerは4 msのservice予算、200 ms audio idle、30 ms部分publication | crates/kirin_measure/src/attack_band_worker.rs:28–32、157–240 |
| 4 msは各計測開始前の確認。単一計測を途中で止めるhard deadlineではない | attack_band_worker.rs:173–202、210–234、261–281 |
| idle時は保持音声のAudioEndまで測るかNotKept。ピーク探索130 msに必要な末尾も確認する | attack_band_worker.rs:95–121、attack_band.rs:422–467 |
| own cacheはonset、anchor cacheはonset＋requested end。entryは同onsetで置換し240上限 | attack_band_results.rs:106–113、139–150 |
| 現行anchor生成はMatchedに限定。POST eventがない場合に利用できる新しいproof経路は未実装 | attack_exchange_join.rs:131–164、166–191 |
| 旧C ABIの包絡には点ごとのmaskがなく、旧Summaryは四つの別中央値による到達／終了マークを持つ | include/kirin_hypha_attack_ffi.h:210–274 |
| 旧KirinAbiContract照合関数はoutのsizeを受けず、一struct分を直接書く | crates/kirin_hypha_ffi/src/abi_contract.rs:82–88 |
| TIME exchange schema7、PRE tail32、local join64。現行WirePointはepoch／incarnationを持たない | crates/kirin_measure/src/meter_delta_history.rs:36–42、102–135 |
| 現行editorはDRUM各batchを別poll／setterで更新する | juce_shell/src/PluginEditorAnalysis.cpp:232–255 |

以降の「必須」はG0で採用する新版の設計規則。上表の現行挙動へ、新版の保証を遡って付けない。

## 2. 責務とidentity

| 層 | 所有する判断 | 禁止する代替 |
| --- | --- | --- |
| Observation producer | 実測区間、点mask、閾値到達、打切り、source、対応proof、完成 | 音未取得をSilentへ変換、GUIでの追加計測 |
| Snapshot assembler | 固定cohort、lane五分類、区間中央値、参加集合、整合検査 | 成立打の補充、異なるrevisionの合成 |
| Presentation | target、主値kind、丸め、期限、数値周期、表示時計 | median再計算、raw端点変更、測定値の平滑化 |
| Interaction | event選択意図、cluster凍結、SingleRequest、LIVE復帰 | event時刻／cohort／測定窓の書換え |
| Render／Capture | 採用PresentationSnapshotの描画・保存 | 再poll、最新sourceへの無表示差替え |

identityを次の単位へ分ける。generationの一致・大小だけでは対応を認めない。

| 名前 | 内容・寿命 |
| --- | --- |
| LocalSourceKey | opaque instance／owner／runtime incarnation、measurement epoch、generation、run、rate、layout、clock source／policy。Reset・seek等で新しい鍵 |
| EventKey | series identity＋公開eventの安定したcontent sample／識別token。pair kindは分類であり鍵の一部にしない |
| MeasurementKey | LocalSourceKey、ALL／band、band semantic identity、物理sample区間、requested終了条件 |
| ObservationRevision | 同じ測定対象に届くhead→complete／部分→延長等の事実改訂。単調増加し、source変更では継承しない |
| ProofKey | opaque selection／binding revision、両LocalSourceKey、mapping epoch、PRE anchor、ODF／band意味、対応区間 |
| CohortKey | series identity、cutoff C、時刻順EventKey配列、band／target。最新最大8件の対象を特定する |
| SnapshotRevision | producer事実を整合検査後に一括commitした版 |
| PresentationRevision | 使用したSnapshotRevision、提示時刻、mode／target／renderKind／clock。画像とmetadataの共通stamp |

PRE-only→Matchedの遅着はEventKeyを付け替えない。PRE anchorを持つ同一content eventとして更新する。POST-onlyに後からPREが対応した場合も既存公開鍵のaliasを保持し、選択を別打へ飛ばさない。aliasは現在series内、event上限内だけ保持する。曖昧な対応を統合して一件にしない。

## 3. 型付きpayload

### 3.1 共通型

G0の論理schemaを固定する。Cのpadding／offset／byte sizeはG1で同一schemaから確定し、C／Rust／JUCEに独立したlayout検査を置く。

| 型 | 必須field |
| --- | --- |
| SnapshotHeader | version、struct_size、kind、flags、snapshot_revision、source／authority token、cutoff C、rate、layout、signal／transport state |
| Endpoint | kind＝Finite／NegativeInfinity／PositiveInfinity、finite value、closed flag。Infinityは必ずopen |
| Interval | lower、upper、unit。逆転、open同一点、finite fieldのNaN／Infinityは禁止 |
| ScalarEvidence | class、optional Interval、reason、measurement／proof revision、requested／actual span、finish kind |
| LaneSummary | class_count[5]、N、whole_median_available、whole_interval、whole_numeric_informative、exact_count、exact_median、exact最新event時刻、reason内訳 |
| EnvelopePoint | sample位置または定義済みgrid位置、measured mask、finite dB値 |
| AveragePoint | participating_bits、valid_count、PRE／POST mean／min／max、connect_previous |

classはexact／bound／unknown／pending／not-applicableの五つ。exactは観測された閾値到達等による閉じた一点、boundは打切り等による区間、unknownは必須事実を確保できない状態、pendingは有効依頼の応答待ち、not-applicableはその指標が成立しないことを確認した状態。exactは物理的な真値の誤差0を保証する言葉ではない。分解能は別fieldとして必ず保持する。

unknown／pending／not-applicableに数値を隠さない。finite fieldを0／NaNへ置換してclassを推測させない。classと理由は別field。理由のenum番号、未知enumの拒否、表示短名はG1でC headerとlocale契約へ固定する。

finish kindはAcquiring／Full／AudioEnd／NotKept／Retired。NotKeptは必要音声の不在を確認した時だけ。RetiredにはSourceChanged、WorkerUnavailable、RequestDeadline等を持たせ、worker応答不能を「音声なし」と言い換えない。

### 3.2 DRUM snapshots

| kind | 必須payload |
| --- | --- |
| AllLive | native最新Single Snapshot、6秒内の全公開event locator、全信号history。BANDの8件上限を流用しない |
| BandSummaryV2 | CohortKey、N≤8、EventKey／pair kind、四LaneSummary、各打各laneのScalarEvidence、HEAD96／TAIL64の平均・参加集合 |
| SingleSnapshotV2 | ALL／band、固定SingleKey、選択意図revision、ObservationRevision、各側source／proof、四laneの型付き結果、同じ一打の形／点mask、finish kind |

SingleKeyはEventKey＋各側source identity＋ALL／band＋帯域意味＋requested span revision。late detailは同SingleKeyの新ObservationRevisionだけ受理する。値とshapeの版が異なる場合は同じ改訂としてcommitせず、その結果待ちを示す。

ALLの四指標はTRANSIENT、STRENGTH、CREST、SHARPNESSの現行定義。BANDの四指標はDELAY、ATT、REL、LEVEL。kindを見ずに同じ配列indexの単位を決めない。

## 4. BAND cohortと統計

### 4.1 対象と分類

cutoff Cに対する現在6秒窓のALL公開系列から、時刻順の最後の最大8 EventKeyを先に固定する。窓は両端inclusiveとし、Cより未来のeventを含めない。Matchedは一件、PRE-only／POST-only／ambiguousも一件ずつ残す。band変更は同cutoffなら同じ鍵を使う。次snapshotでC／系列が進む時だけcohortを更新する。

各laneの五分類合計はN。必要な両側の件数を加算してNにしない。分類の優先順は、指標不成立が確定→必須条件の欠如が確定→有効依頼の結果待ち→有効な型付き区間。表示全体のtargetと各打の比較proof成立を分ける。

| 観測 | lane扱い |
| --- | --- |
| POST単体 | ATT／REL／LEVELを実測。DELAYはnot-applicable：NoPair |
| Silent | ATT／RELはnot-applicable。LEVELは実測上限≤−72 dBFS。床値をexactにしない |
| 両側SilentのΔLEVEL | not-applicable。片側Silentなら引算した区間bound |
| 到達が隠れるArrival Ringing＋Rises | DELAY／ATTはnot-applicable。REL／LEVELは自分の有効事実で判定 |
| 全体RingsOn | 自分のATT／RELはnot-applicable。LEVELは自分の実測結果 |
| NextHit、対応不能、clock不成立、PCM不在 | coreに有効boundがなければunknown。無効依頼をpendingとしない |

PRE-onlyに対してPOST無音を仮定しない。証明されたmappingと保持音声がある時だけPOSTを実測する。分類kindはPRE-onlyのままでも、音声比較proofが成立すれば有効なBAND差区間を作れる。

### 4.2 区間中央値

全N中央値の算出条件はN>0、not-applicable=unknown=pending=0。各exactを[v,v]、各boundを開閉付き区間として扱う。N/Aを全実数へ置き換えない。Δは一打ごとのPOST−PRE intervalを先に作り、その配列を集計する。

下限列は値順、同値closed→open。上限列は値順、同値open→closed。奇数は中央端点、偶数は中央二端点の算術平均。偶数の端点は選択した二端点が両方closedの時だけclosed。無限端点はopen。

差区間は[post.lower−pre.upper, post.upper−pre.lower]。各端点を作る二端点が両方closedの時だけclosed。閉じた同一点だけexact。両側medianの差、区間中点、上限capを実測値として使わない。

exact部分のmedianは同cohortのexactだけで別途算出する。whole medianが一点になっても全打exactとは表示しない。wholeの中にboundがあることはclass countへ残る。

両側RELが下限だけならΔは全実数になり得る。これをunknownへ改ざんしない。whole_median_availableとwhole_numeric_informativeを分け、結果区間が全実数の時だけ後者をfalseとする。全実数の入力を含んでも、他の打音により全N中央値が一点／有限制約に定まる場合は通常どおりwholeを使う。

### 4.3 主値kindと分解能

| renderKind | 条件 | 主値／scope |
| --- | --- | --- |
| WholePoint | 全Nmedianがclosed point | 全Nの値。全Nがexactとの断定はしない |
| WholeInterval | 全Nmedianが一点以外で情報を持つ区間 | 全Nの限界／両端区間 |
| ConfirmedSubset | whole算出不能または全実数、exact_count>0 | 確定n/Nのmedian＋全体を出せない理由 |
| NoScalar | 有効whole数値もexact部分もない | ---＋対象Nと内訳／理由 |
| Single | 一打LIVE／LOCK | 一打の値／限界／欠測。median、n/Nを付けない |

全実数の理由は「全体の範囲を限定できない」。∞をmsの主数値として描かない。ConfirmedSubsetに全体方向語を付けない。singleの境界も同じ型と丸めを用いる。

比較の判別範囲はDELAY=max(一周期/32,0.2 ms)、ATT／REL=一周期、LEVEL=0.2 dB。whole interval全体がその範囲に収まる時だけ全Nの「判別範囲内」を示す。0を跨ぐだけでSAMEとしない。POST単体ATTは一周期より短い時に従来の上限表示を維持し、raw測定値と表示分解能を分ける。

exactは既存最近接丸め、−0は正規化。boundはlowerを−∞方向、upperを＋∞方向へ丸める。closed／openを保持し、表示桁で同一点になってもexactへ昇格しない。raw Intervalと表示文字列を両方Captureへ残す。LEVEL絶対値1000以上は両端へ同じ共有指数を適用し、unit欄へ出す。外向丸めした10進文字列を再びf64へ戻してoverflowさせない。表示型・raw値・指数を別fieldで保持する。

## 5. 包絡maskと平均

HEADは[onset−20 ms,onset＋40 ms)の96点、TAILは[onset,onset＋300 ms)の64点。gridのsample位置、実測actual終了位置、必要filter／RMS supportからproducerがmaskを作る。−120 dB等の床値からmaskを推測しない。実際に無音だった有効点の床値は参加できる。

POST単体の点参加集合は、その点を実測したcohort内POST。比較では同じ点でPRE実測かつPOST実測かつその点区間のproof成立した打音だけ。PRE／POSTのmean／min／maxに同じ集合を使い、四lane scalarの成立／欠測とは独立に決める。

participating_bitsはcohortの古い順bit0～N−1、valid_countはpopcount。各sideのdB算術平均、min／maxを返す。0件の数値はabsent。隣接点は同じ非0 bitsetでのみconnect_previous=trueとし、線／範囲fillとも断線する。同数でも別参加集合なら結ばない。

図captionはN、valid_countの幅、集合変更の説明を持つ。平均図にmedian時刻やΔ括弧を描かない。単打DELAYの実到達差括弧は保持できる。RELは各側終了マークとduration差の数値へ分ける。

## 6. PRE-only実測、proof、cache

### 6.1 証明条件

新版はPOSTイベント0でも、同runのODF frame／clock headerと必要POST PCMを公開・保持できる経路を要求する。現行のSilent時の保持／publication継続は下記fixtureで確認する未検証項目。新経路はevent-independent SourceEvidenceを使用する。post.newest eventの存在だけをidentityの入口にしない。

proofには完全な両source、selection／binding authority、ODF support mapping、mapping epoch、rate／layout、ODF hash、band semantic hash、PRE anchor、requested／actual区間、必要音声保持を含める。SilentでもODF／ringが進むことを実際に確認する。support不一致、PDC／clock前提不成立、対応の曖昧さは比較を閉じる。

authority前読み→local source／事実try-lock→解放→exchange／proof try-lock→解放→authority／source／signal後読み→全field検査→一括commit。複数lockを同時に持たず、GUI pollでfilesystem joinしない。通常cutoff進行だけでrejectせず、source／authority変更をrejectする。公開直前にも証明を再検証する。

### 6.2 二つのcache

DataKeyはLocalSourceKey、band semantic identity、実測POST sample区間、実際の終了理由。ProofKeyはmapping epoch、両source、PRE anchor、ODF／band identity、requested区間／終了理由。peer不一致で比較authorityだけを退役し、同じlocal sourceの絶対実測cacheを捨てない。

同じ物理区間／意味のownとanchorは実測を共有できる。pair kind／poll revisionをDataKeyにせず、遅着Matchedで重複計測しない。requested endが同じでもAudioEndから実際のcoverageが伸びた場合は新ObservationRevisionを生成する。古い部分cacheを永久hitにしない。terminal LOCKコピーは延長せず、LIVEだけが新改訂を採用する。

容量はown上限240、anchor上限240、選択Single一件。onset／window改訂はentry置換で、revision履歴を積まない。source／band変更でpending取消。ALL復帰で帯域PCMを解放。比較proofのalias／retired tokenも同じhistory bound内とし、未完了requestを無制限に積まない。二枠leaseを維持する。

### 6.3 意味descriptor

ODF hashとband semantic hashを別にする。band descriptor version1のcanonical fieldはfilter形式・係数生成／中心補償、RMS一周期rounding／settle、presence−72／rise3 dB、arrival peak−20 crossing、ATT10/90%、REL−20 dB、peak130 ms／tail300 ms、HEAD96／TAIL64 grid、span終了条件、区間端点、分解能規則、algorithm revision。

descriptorを固定順・固定型でserializeしhashを得る。rate、layout、band、sample区間は別identity。表示翻訳・桁・描画・意味不変の最適化ではhashを変えない。ODF一致だけでband比較を認めず、band一致だけでclock mappingを認めない。G1でcanonical bytesとgolden hashを固定し、current sourceに既存hashがあるとは主張しない。

## 7. Single finalizationとbounded response

transport Active／HOLD、選択LIVE／LOCK、取得Acquiring／terminalを独立状態にする。GUI stopはterminalの根拠ではない。同SingleKeyのlate Full／AudioEnd／NotKeptを受ける。

必要pre-rollがあるBANDでonset＋150 msの音声保持がピーク130 msとRMS末尾条件を満たせば、有効ATT／LEVELとREL下限を採用できる。＋100 msでは同条件を満たさず、満たせないlaneは欠測。時刻だけから値の成立を断定しない。ALLのhead30、body、sharp100も同eventの完成だけ受ける。

G0の応答設計値は次のとおり。現行sourceの性能保証ではなく、G1の独立worker／fake clock fixtureで受入する値。

| 境界 | 設計上限・処理 |
| --- | --- |
| 選択意図受理 | UIへP95≤100 ms。計測結果と分ける |
| 正常待ち | Acquiringをnative30Hzで提示。音声future待ち／service待ち／publication待ちを理由別に記録 |
| 一依頼の最終応答 | 受理monotonic時刻から1,000 ms以内にFull／AudioEnd／NotKept／Retiredのいずれか |
| 期限超過／worker死活不明 | 非RT controlが同requestをRetiredへcommit。RequestDeadline／WorkerUnavailableとして数値なし |
| 退役後のlate reply | stale request tokenを拒否。別打へ置換せず、自動再依頼・期限延長なし |

1,000 msは300 msのfuture tail、idle200 ms、部分publication30 msを上回る有限案だが、4 ms serviceのhard WCETから導いた値ではない。G0の旧ABI probeはTIMEの限定観測で、DRUMの通常case／密集二枠の根拠にはしない。新経路の証拠はG1／G2で取り、G3 freeze前に応答値を固定する。timeoutは有効なREL boundやSilentの根拠にしない。結果がNotKeptか応答不能かを区別する。

source失効、seek、Reset、意味変更、editor離脱はrequest／LOCKを解除する。帯域変更は同eventへの新requestで旧帯域値を消す。窓外terminal Singleをimmutableコピーで保持し、「固定した過去の一打」、時刻、source、帯域を示す。再openで値／形／LOCKを復元せず、選択意図だけを新LIVE観測へ適用する。

## 8. TIME／PSR packet

TIMEにはDRUM Summaryを流用せず、同packet内でmainとPSRを独立componentにする。

| field | 内容 |
| --- | --- |
| Local axis | 最新local cutoff C、POST source span、共通range[max(0,C−duration),C] |
| MainComponent | global POST／Δ意図、target、独自cutoff、M／S／TP／PLR／CORR raw current、proof、current state、history |
| PsrComponent | PRE単体／POST単体／paired自動Δ、cutoff E≤C、raw PSR／None、独自proof、current state、history、残期限 |
| Segment | first／last endpoint、source／run、total／valid count、min／max／mean、gap／境界 |

現在値はMeasure Threadの未集約100 ms事実とNoneから取得する。raw local tailは最大64件。PRE wire tailは既存32件を維持する。history mean、decimation、last finiteからcurrentを作らない。payloadに各raw点が属するepoch／incarnation／declared spanを明示し、publisherが全点を確認する。

paired PSRの主値はΔ、単体はPRE／POST。global main POSTはlocal Cで更新し、PSR PRE待ちへ巻き込まない。global mainΔは独自proof／cutoffで判定する。PLR／CORRはmain targetに従う。

400 msは原slot完了timestamp未公開のためG0で検証できていない設計値。旧ABI観測のC−Eは最大500 msで、通常の不要な空欄0を保証しない。TIME・Capture契約の限界を適用する。

PSRの通常publication待ちだけ、POST slot完了から400 ms未満かつC−E<400 ms、完全binding／両source span／run一致、Active、後続確定invalidなしの直前比較を保持できる。同endpoint再join／poll成功で延長しない。monotonic残期限を減らし、poll失敗中も期限を切る。単体currentにも完了起点の鮮度とsignal失効を適用する。

None、対応不能、stop、bypass、clock／run変更は影響currentを即退役。pair／PRE失効は比較componentだけ。POST span／rate／layout／Resetは両component。新authorityと旧producer bindingの不一致は新authorityのwaitingを返し、同POST main Cを継続する。選択ΔをPOST値へfallbackしない。

取得はauthority前読み→session try-lock→解放→binding付きexchange try-lock→解放→POST span token含むauthority／signal後読み→整合検査→一括commit。検査をlinearization pointとし、以後の変更はGUI lifecycle境界で退役。競合時は出力不変、無言skip。legacy build_observatory_frameの別delta取得を新authorityに使わない。

historyはNone、slot抜け、未対応endpoint、clock／runでsegmentを切る。0を分母へ入れない。出力各component≤1200、compact PSRはcapacity0。segmentを跨いでdecimateしない。rangeを跨ぐbucketは保持exact点から再集約できる場合だけ使用し、不能なら欠線。座標clampで将来meanを残さない。

## 9. PresentationとCapture

BAND LIVEまとめだけ4Hzを初期案とする。値、kind、scope、件数、理由、平均図を同revisionで交換する。ALL latestとSingle取得中はnative30Hz、TIME／PSRはnative100 ms。失効を4Hz待ちにしない。窓応答とsnapshot→提示遅延を別に測る。

主面scopeは全N／確定n/N／最新一打／固定一打を値と一組にする。ConfirmedSubsetのageは、そのlaneのC−最新exact event時刻。共有captionは表示中ConfirmedSubsetのage最大値。cohortの最新打ageで古いREL exactを新しく見せない。

DRUM facts C、到着時刻、viewport Vを分ける。Vは同runの固定anchor＋rateで進み、正常publicationごとにreanchorしない。有限look-behind Lの値はmotion probeで固定する。V未到達の最新ALL一打は同SingleKeyのlocatorへ時刻／状態を出し、shape／四値と同じ鍵を保持する。実markerが入る時に同じ鍵で強調する。Vをcohort cutoffや測定時刻へ使わない。

Captureは採用PresentationSnapshotへ期限／退役を反映後freezeする。以下のmetadata保存はv2採用時の候補契約で、採否はG0を閉じる時に利用者へ提案する。v2採用なら画像とmetadataを同PresentationRevisionから作る。raw interval、表示文字列、renderKind、scope、event／cohort鍵、source／proof opaque token、cutoff C／E、V／clock、finish／reasonを保存する。旧target／Work observation_targetはmainの意味を維持し、PSR等のcomponent metadataをversion付きで追加する。

現consumerはv1追加fieldを拒否し、Workへtype/path/notesだけ保存することを使い捨てfixtureで確認した。詳細は[TIME・Capture契約](time_capture_contract.md)。新版metadataのv2 round-tripは未検証。v2には別repoのconsumer／Work変更も必要。採用時だけround-tripを公開前に受入し、失敗は明示通知する。v1を維持する案では、表現・保持不能なsnapshotの添付を明示失敗として通知し、metadataを捨てたattached成功へ縮退しない。既存v1添付は意味を保持できる検証済み範囲だけ維持する。ローカルPNGは利用可能。private pathやowner管理情報をCaptureへ出さない。

## 10. 新API／wireと旧互換

旧struct、旧poll、KirinAbiContractのlayout／size／意味は保持する。旧Summaryのcount／left_outを新N／五分類へ読み替えない。旧reservedやNaNを新版状態の隠し口にしない。

新版の論理APIは以下。名称はG0案、C宣言・symbol登録・layoutはG1で固定する。

| API案 | 入出力契約 |
| --- | --- |
| query_snapshot_contract_v2 | size付きcaller bufferへfeature version、size／align／offset、enum revision、各capacityを返す |
| poll_attack_summary_v2 | size／version付きrequestからBandSummaryV2を一括取得 |
| request_attack_single_v2 | SingleKey／request token／requested spanを受理、有限deadlineを設定 |
| poll_attack_single_v2 | request tokenを指定し同SingleSnapshotV2を取得。未知tokenは旧値を返さない |
| cancel_attack_single_v2 | 指定token退役。新requestを巻き込まない |
| poll_time_presentation_v2 | main意図、range／capacityを受け、同authorityのTIME packetを一括取得 |

null、短buffer、未知version、invalid count／enum、raceでは出力全byte不変。返り値はSuccess／Busy／InvalidRequest／Unsupported／Retiredを区別する。Successは数値成立を意味せず、waiting／欠測も整合したsnapshotなら返す。既知requestのterminal RetiredはSuccess＋finish kind Retiredのpayloadで返す。未知／取消済みtokenはAPI status Retiredで出力不変とし、終端payloadと混同しない。sizeを確認する前にoutを書かない。

起動時は旧ABI照合に加えて新版capability／layoutを照合する。新shellが必須symbolを持たない旧staticlibと混在すればlink／起動段階で拒否し、legacy pollで新版画面を続行しない。旧関数で拡大KirinAbiContractを書かせてからversion拒否する方法は禁止。

band wireは現行v4がactual span_endを持つためmask導出だけでは増版しない。新版はevent-independent source／band semantic identity／proofに必要なfieldを追加する別versionを設ける。TIME schemaもepoch／incarnation／declared span／raw current対応版へ増版。旧versionを新比較authorityへ使わず、local POST絶対観測は維持して理由を返す。旧wireを改変して旧readerへ新版を読ませない。

## 11. G1影響file一覧

以下はB-1300で存在を確認したfile。変更予定であり、今回編集しない。巨大fileは変更責務を先に500行以下moduleへ抽出し、既存行数を増やさない。

| 責務 | 対象 |
| --- | --- |
| DRUM測定／寿命 | crates/kirin_measure/src/attack_band.rs、attack_band_measure.rs、attack_band_worker.rs、attack_band_results.rs |
| event-independent identity／proof | crates/kirin_measure/src/attack_runtime.rs、attack_runtime_state.rs、attack_runtime_worker.rs、attack_runtime_assembler.rs、attack_exchange_join.rs、attack_exchange_codec.rs、attack_exchange_codec_band.rs、transient_layout.rs |
| DRUM ABI | crates/kirin_hypha_ffi/src/attack_ffi.rs、attack_ffi_band.rs、attack_ffi_band_map.rs、attack_ffi_band_summary.rs、lib.rs、include/kirin_hypha_ffi.h、include/kirin_hypha_attack_ffi.h |
| 新境界／照合 | crates/kirin_hypha_ffi/src/abi_contract.rs、abi_contract_tests.rs、include/kirin_hypha_abi_contract.h、juce_shell/src/AbiContract.h、HyphaObservationEquality.h |
| TIME事実／wire／proof | crates/kirin_measure/src/meter_session.rs、meter_history.rs、meter_history_decimation.rs、meter_delta_history.rs、meter_pair_observation.rs、meter_history_publisher.rs、meter_content_wire.rs、io_thread_post_observation.rs、io_thread_post_observation_pair.rs、io_thread_post_observation_runtime.rs |
| TIME ABI／selection authority | crates/kirin_hypha_ffi/src/meter_observation_ffi.rs、meter_session_abi.rs、meter_history_abi.rs、pair_binding.rs、include/kirin_hypha_meter_session_ffi.h、include/kirin_hypha_meter_history_ffi.h |
| processor／editor取得 | juce_shell/src/PluginProcessorAnalysis.cpp、PluginProcessor.h、PluginEditorAnalysis.cpp、PluginEditorObservatory.cpp、PluginEditor.h |
| DRUM提示／選択 | juce_shell/src/HyphaAttackComponent.h／.cpp、HyphaAttackInteraction.cpp、HyphaAttackBandView.cpp、HyphaAttackBandModel.h、HyphaAttackBandSummary.h、HyphaAttackBandSummaryWords.cpp、HyphaAttackBandSummaryPainter.h／.cpp、HyphaAttackBandPanes.cpp、HyphaAttackBandPainter.h／.cpp、HyphaAttackEnvelopeGeometry.h、HyphaAttackSnapshotEquality.h、HyphaAttackLaneModel.h、HyphaAttackLaneText.cpp、HyphaAttackLanePainter.h／.cpp、HyphaAttackGlancePainter.cpp、HyphaAttackLoupePainter.h／.cpp、HyphaAttackChrome.cpp |
| TIME提示／target／axis | juce_shell/src/HyphaObservatoryView.h、HyphaObservatoryFrame.cpp、HyphaObservatoryPresentation.h、HyphaObservatoryViewState.cpp、HyphaObservatoryViewLayout.cpp、HyphaObservatoryViewFooter.cpp、HyphaTimeHistoryLayout.h／.cpp、HyphaTimeHistoryPainter.h／.cpp |
| Capture | juce_shell/src/HyphaCaptureContract.h、PluginEditorCapture.cpp、HyphaObservatoryCapture.cpp、CaptureWorkAttachment.h／.cpp |
| 根拠／既存試験 | attack_ffi_band_summary_tests.rs、attack_ffi_band_identity_tests.rs、attack_band_worker_tests.rs、attack_band_outcome_tests.rs、attack_exchange_join_tests.rs、attack_exchange_codec_identity_tests.rs、meter_delta_history_tests.rs、meter_history_tests.rs、juce_shell/tests/AttackUiContractTest.cpp、TimeHistoryContractTest.cpp、ObservatoryViewContractTest.cpp |

新規module案はattack_snapshot_v2、attack_interval、attack_band_semantics、time_snapshot_v2、JUCEの同名snapshot／presentation adapter。既存fileへ全責務を追記せず、登録先lib.rs／CMakeと対象testへ接続する。locale／help-line／Capture consumerの実際のentry pointはG1開始時に再列挙し、外部consumerを存在確認なしに編集対象へ指定しない。

## 12. 独立fixtureと未検証

| 入力・操作 | 必須期待値 |
| --- | --- |
| REL exact20、20、N/A | N3、exact2、N/A1、whole算出不能、ConfirmedSubset20・2/3 |
| [0,10,≥10] | whole[10,10]、exact部分5・2/3。全3exactとは表示しない |
| Δ1打−10、7打≥＋200 | whole≥＋200、全8。subset−10・1/8は詳細。SHORTER0 |
| Δexact0、exact0、bound全実数 | whole[0,0]、available=true／informative=true、bound1を保持 |
| Δexact−10、bound全実数7 | whole全実数、available=true／informative=false、ConfirmedSubset−10・1/8＋全体の範囲を限定できない |
| ID1～8：値1～7／8pending→ID2～9：値2～8／9pending→9exact=9 | whole未成立→未成立→5.5。subset4・7/8→5・7/8→whole5.5・全8 |
| [0,1)、[2,3] | 偶数whole[1,2)。両入力順で同じ |
| >10、≥20 | whole>15。openをclosedへしない |
| lower／upper同値のopen／closed全順列 | lower closed→open／upper open→closedの同じ中央値 |
| ≥146.6、≤−52.06、[−10.14,−9.96] | 整数≥146、0.1桁≤−52.0、[−10.2,−9.9]。raw端点不変 |
| A：PRE−20／POST−18、B：PRE−40のみ | common maskの点はAだけ、meanPRE−20／POST−18、count1。0点は線なし |
| 隣接参加集合01→10、各count1 | 同数でも断線。範囲fillもなし |
| PRE-only、POST event0、有効ODF support mapping・同clock／意味・保持音声あり→Matched遅着 | 有効窓だけ実測、kindとproof独立、同DataKey再利用。bad proofでΔ0 |
| PRE-only、POST event0、clock一致とPCMだけで有効mappingなし | unknown＋対応不能。POST Silentの捏造0、clock一致だけでΔ0 |
| 同requested end、AudioEnd150→音声再開でFull300 | LIVE新ObservationRevision。固定済みSingleは150の事実を維持 |
| 一打取得中stop、worker reply遅着 | GUI stop terminal0。同keyの正当なFull／AudioEndを期限内に受理 |
| worker replyなし、1,000 ms経過、さらにlate reply | Retired、永続pending0、偽Silent／REL bound0、stale reply採用0 |
| A−30を0 ms、B−10を125 ms、250 ms前にB選択 | ALL locator／marker／shape／四値はB。同A値の4Hz残留0 |
| ODF同一／band意味不一致、旧peer | 比較不成立、同local POST dataは利用可、旧Δ復活0 |
| TIME Reset／pair callback→poll busy→Capture | 退役済みcomponent復活0。v2採用時はPNGとtyped metadata同stamp、v1維持時は保持不能添付の明示失敗とローカルPNG維持 |
| ABI短buffer／未知version／途中source変更 | 全出力byte不変、混在snapshot0 |

今回の文書化を上記fixtureのPASSとは呼ばない。新API／wireのbytes、semantic golden hash、event alias、event0時のODF／PCM保持、dead worker退役実装、1,000 ms応答、consumer round-tripは未検証。G0の既存probeは現行挙動の観測だけで、新版保証の代用にしない。

G0では実寸wire、finite L／正常jitter／4Hz／worker応答の候補と校正手順、consumer保存能力、Capture二案を整理する。製品変更を要する証拠はG1／G2で取り、G3 freeze前に値を固定する。公開前の人による確認は本人の日常操作・品位だけ。友人の初見等は今後も常に公開後G4だけに行い、開発工程・公開条件には含めない。初見は未見の参加者だけが確認する。

通常A経路、RTのalloc／lock／blocking I/O禁止、0 samples、Record／plugin_dataの正本、二枠lease、role／host／layout gateを維持する。本書保存ではbuild／CI／実機／リリースを開始していない。
