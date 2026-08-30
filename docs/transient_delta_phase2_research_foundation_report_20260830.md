# ATTACK Phase 2-R 研究基盤報告

**日付**：2026-08-30
**実装ラベル**：B-549
**研究判定**：DRUM研究基盤はGo
**公開判定**：ATTACKはNo-Go、未準備
**対象範囲**：Offline候補研究、データ分離、fixed-scale SuperFlux、外部reference照合

## 1. 判定の境界

B-549で、DRUM候補を再現可能な手順で研究するためのコード基盤は成立した。
このGoはdevelopment selection、評価契約、fixed-scale ODF、外部reference、fail-closed gateを継続して検証できるという判断である。

一方、候補選定、blind acoustic audit、formal evaluator統合、fresh holdout、runtime、PRE/POST共通判定、公開UIは完了していない。
したがって「DRUM候補が採用可能になった」または「ATTACKを利用者へ公開できる」という判定ではない。

2MIXは画面と分離原則だけを確定しており、development audio、注釈、候補設定、definition hash、fresh holdoutの実体は未準備である。

| 対象 | 現在の判定 | 根拠 |
|---|---|---|
| DRUM研究基盤 | Go | deterministic selection、opened ledger、共通MIDI契約、fixed SuperFlux、評価前blockerが実装された |
| DRUM候補freeze | No-Go | fold不均衡、provenance、blind audit、formal evaluator統合、candidate scoreが未完 |
| 2MIX候補freeze | No-Go | 独立データとaudio-only annotationが未作成 |
| ATTACK runtime | No-Go | request、worker、transport、FFI、JUCE routeが未実装 |
| 公開ATTACK | No-Go | profile別fresh holdoutとmacOS、Windows実機gateを通していない |

## 2. 一画面二profileの境界

ATTACKはPOST限定の一画面とし、利用者が`DRUM`または`2MIX`を明示的に一つ選ぶ。
素材の自動分類、自動profile切替、同時実行は行わない。

二profileはanalyzerのコードを共有できるが、研究上の同一性は仮定しない。
DRUMの成立性を2MIXへ流用せず、2MIXの余裕でDRUMの未達を相殺しない。

| 契約 | DRUM | 2MIX |
|---|---|---|
| 対象 | drumまたはpercussion busのcompound attack | 完成mixのmusically relevant broadband attack |
| development | E-GMD trainとvalidation | Slakh2100-redux trainとvalidationから固定excerpt |
| 正解 | acoustic audit済みMIDI compound proxy | 二人がaudioだけを聴いた独立annotation |
| 候補設定 | DRUM専用parameter | 2MIX専用parameter |
| definition hash | DRUM専用 | 2MIX専用 |
| candidate gate | 一般gateにkick、hat診断を追加 | 一般gateのみ |
| fresh holdout | DRUM専用の未接触集合 | 2MIX専用の未接触集合 |
| 公開可否 | DRUMだけで決定 | 2MIXだけで決定 |

profile切替時は旧requestをretireし、historyとselectionを消去し、新しいdefinition epochを開始する設計を維持する。
一方だけが全gateを通った場合は、合格したprofileだけをselectorへ出せるが、未達profileを暗黙に代替しない。

## 3. B-549で実装した研究部品

### 3.1 fixed-scale SuperFlux core

製品候補用の**fixed-scale SuperFlux core**を、既存の`TransientOdfKind`へ追加せず独立moduleとして実装した。
44.1、48、88.2、96、176.4、192 kHz、基準window 1,024または2,048 sample、12または24 bands/octave、`r=0|1`、固定amplitude referenceを明示的なlayoutへ変換する。

channel topologyはconstructorでmonoまたはstereoへ固定し、`LR`、`MID`、`SIDE`をdefinition hashへ含める。
mono `SIDE`、frame途中のtopology変更、非finite、window長不一致、timestamp overflowはstateを進めずに失敗する。

FFT buffer、RustFFT scratch、band historyはconstructorで確保し、steady-stateの`analyze_window()`では再確保しない。
reset、odd window support、stationary noise、dual-mono、同相MID、SIDE cancellationを専用testで固定した。

48 kHz、基準window 1,024、24 bands/octave、`r=1`、reference -70 dBFS、mono LRの固定layout hashは次である。

```text
0733522696b3dbdab5a37da86d636f4b28b5ca08d992aba8d7d42886e9a6d364
```

このhashは上記一layoutの識別子であり、DRUMのwinner hashではない。
window、rate、channel topology、bank、reference levelが変われば別hashになる。
定義versionは`kirin-superflux-definition-v2`である。
hashは整数parameterと実現した整数bin tripletを対象とし、platformの`libm`実装に依存し得るHannとtriangleの`f32`係数を含めない。
実行時係数は別の検証receiptで有限性、補償値、triangle構造を確認する。
macOSではこの分離をtestしたが、Windows実機で同一hashと係数検証結果を測る作業は未実施である。

既存Mel、Complex、Hybrid系にもRustFFT scratchの事前確保を追加した。
これは旧候補の数式を変更せず、steady-state allocation経路を閉じる修正である。

### 3.2 外部reference control

候補rankingから分離した`paper_2013_online`と`cpjku_1_03_online`を追加した。
両receiptは型として`ranking_eligible=false`を返すため、外部式との一致を製品候補の勝者選定へ混ぜられない。

| control | definition SHA-256 | 固有差 |
|---|---|---|
| `paper_2013_online` | `20618c61143d82497bcd17795375d24f351238ee1681538d00fac440677f4e55` | 27.5 Hzから16 kHz、pre-max 30 ms、pre-avg 100 ms、deltaはdataset sweep |
| `cpjku_1_03_online` | `f15132c7b9127fb251fa6aaaa9627546f1aa3f9f9c9f630d1892d887268c8829` | 30 Hzから17 kHz、pre-max 10 ms、pre-avg 150 ms、delta 1.1 |

共通契約は44.1 kHz、window 2,048、200 fps、24 bands/octave、`mu=2`、`r=1`、symmetric Hann、band sum、NumPy ties-to-even roundingである。
CPJKU v1.03のbin列は143点、141 bandsとなり、little-endian `u32`列のSHA-256は`721fb9f9b5cb417ca10d361d4cc47cff4f1fdf1b4bdeeb0cea620100a072acc0`である。

論文PDFのSHA-256は`313b9767900fee8c8063ba032a34f2090624a402f4eef1d00713bea61a1f8fb1`である。
CPJKU sourceはcommit`0a85f898af69188324b649a4c640052a5443078e`へ固定し、source SHA-256は`c41059aa8f15b8a6ef70345b0a9696b3edf2d73bbe3c77a056d13ed7d615b75b`である。

論文は138 bandsと報告するが、CPJKUの数値生成を論文の周波数範囲へ適用すると139 bandsになる。
この差は`PaperReported138ButCpjkuNumericsRealize139`として残し、一致したとは扱わない。

CPJKU v1.03は`combine=0.03`をsecondsとして説明した後、実装内でさらに1,000で割る。
reference契約は入力30,000 µs、implementation divisor 1,000、effective 30 µsを字義どおり保持し、論文controlのeffective 30,000 µsと区別する。

公式audioとactivationの完全なODF trace goldenは得られていない。
そのためtrace receiptは`PendingNoOfficialAudioActivationGolden`であり、bin layoutの一致を完全なODF照合へ拡張していない。

### 3.3 共通MIDI契約

development selector、proxy audit、opened-diagnostic evaluatorは、共通の整数µs MIDI parserを使用する。
velocityが0より大きいnote-onを保持し、各note時刻をhalf-upで整数µsへ丸める。

compound eventはfirst tapを起点とする非連鎖の30,000 µs包含spanで作り、event時刻の平均もhalf-upで丸める。
kickはpitch 36、hatは22、26、42、44、46に固定し、不正header、tempo、running status、event境界はfail-closedとする。

予測sampleと正解µsの±25 ms判定はi128の整数式で行い、44.1 kHzを含むexact境界を浮動小数誤差で落とさない。
matcherはprediction×labelの全行列を保持せず、二本のrolling rowと許容edgeだけを保持するため、memoryは`O(label + admissible edge)`である。
20,000 predictionと5,000 labelの長尺fixtureで、旧方式なら3 GBを超える配置を全行列なしで完了した。
offline evaluatorは最後のsampleをsupportに含むframe centerまでzero paddingでflushし、奇数941 sample窓と偶数1,024 sample窓の末尾fixtureを固定した。

ただし、formal developmentの21列manifestと5-foldをcandidate evaluatorへ接続する統合は未完である。

## 4. opened ledgerと仮選択

**opened-set ledger**は、候補評価器が実際に採点または診断した集合だけを記録する。
現在のledger SHA-256は次である。

```text
e9935efba336a40b44ba46bcaa234117927c05e36bb5ca8f80f257b8ba58b3ca
```

ledgerはB-546の18件とB-548の12件を統合し、重複6件を除いた24 unique performance IDを持つ。
validation 6件はdevelopmentへ必須で取り込み、test 18件は`diagnostic_only`として新しいdevelopmentとfresh holdoutから除外する。

現契約のselectorを独立outputへ二回実行し、全artifactがbyte単位で一致した。
固定seed`ATTACK-V2-20260830`とquota-first ruleにより60 performance IDを選んだ。
総unique durationは2,619.070秒、beat 25件、fill 35件、primary style 13、kit 28、drummer 9、kick-only event 1,923、hat-only event 3,089だった。

この60件はMIDIデータ量quotaを満たすが、candidate freeze用manifestではない。
fold、archive member、audio、blind auditが未認証なのでprovisional artifactとしてのみ扱う。

manifest SHA-256は`cb70d786b5d3359dfe62bfa6e7b02424d1f1407c3019493c1d3a98718a3918bd`、fold SHA-256は`83f3331fb2c9d560cb6649f2f913caa3af3fc4b05d974a3c0f0ff4a692c78bc2`、receipt SHA-256は`25fbd43cab393480e8f920450d6be378c9394ff6ccea58335b9f1c1c739f70af`である。
これらはprovisional artifactの再現記録であり、採用候補の根拠には使わない。

## 5. fold不均衡

provisional runは各foldへ12 performance IDずつ割り当てたが、durationとevent分布が均衡していない。

| fold | ID | duration秒 | compound | kick-only | hat-only |
|---:|---:|---:|---:|---:|---:|
| 0 | 12 | 808.648 | 5,966 | 857 | 619 |
| 1 | 12 | 516.009 | 3,596 | 538 | 242 |
| 2 | 12 | 439.331 | 3,123 | 207 | 360 |
| 3 | 12 | 419.188 | 3,167 | 145 | 1,329 |
| 4 | 12 | 435.893 | 2,270 | 176 | 539 |

fold 0には611.652秒の一演奏が入り、この一件だけで全選択時間の23.4%を占める。
最大foldと最小foldのduration比は約1.93であり、kick-onlyはfold 0の857件に対してfold 3が145件、hat-onlyはfold 3の1,329件に対してfold 1が242件である。

現fold scorerはdrummer、session、kit、beat/fill、tempo、density、ID数をdurationより先に辞書式で比較し、event数をcostへ含めない。
したがって「各foldが12 ID」であることは、worst-fold gateを比較できる分布を保証しない。

`fold_balance_not_qualified`はcandidate evaluationのhard blockerになった。
611.652秒の一件はfold平均時間より長いため、再配置だけでは解消できない。
決定的な上限付きexcerpt契約またはselection pool拡大を先に固定し、duration、compound、kick-only、hat-onlyの数値gateを満たす同一foldを再現できるまでprovisional runを採点へ渡さない。

## 6. MIDIとaudioのprovenance

公式metadata SHA-256は`80677e8fb00e973f33cb91ddaaf7f0cffe55359f9a76c1833ce56c84d1d92c64`である。
検証済みMIDI ZIP SHA-256は`5e70a6f4d760385a5e5ec986a2f02179d96f61181a920e592876b577a75844d3`である。

selectorは選択したMIDI fileを個別にhash化するが、現在のreceiptでは`midi_root_archive_provenance_verified=false`である。
ZIPのidentityが正しいことと、展開rootの全memberがそのZIPだけに由来することは別の証明だからである。

E-GMD audio archiveの期待SHA-256は`7d9a264fb4c9eabd9fec09d5f8e333192f529b1a1b845d170279a977ac436053`である。
B-549 selectorはaudioを開いておらず、audio archive検証、選択audioのhash、重複検査、exact excerpt契約は未完である。

opened-diagnostic evaluatorはarchive SHA引数をこの値へ固定したが、root member byteをarchive member mapへ認証していない。
実際に読んだMIDIとWAVのSHAはresultへ残す一方、preflightは`diagnostic_only_unverified_archive_members`と明示し、formal gateへ転用しない。

したがってformal candidate evaluationは、MIDI archive member provenance、audio archive identity、audio duplicate preflight、excerpt contract、fold balance、blind acoustic auditの全条件が揃うまで拒否する。
現B-549 gateはcallerがbooleanを全てtrueにして通す経路を持たず、検証済みtyped receipt chainが実装されるまで無条件にcandidate evaluationを拒否する。

## 7. blind acoustic audit

DRUM MIDI proxyを候補出力から盲検化して調べるaudit toolを実装した。
設計値は10分、二annotator、±25 msの最大cardinality matching、annotator間F1 0.90以上、各annotatorに対するMIDI proxyのPrecisionとRecall 0.95以上である。

このtoolはcandidate output、audio、test、fresh holdout、2MIXを読まない契約を持つ。
しかし、formal development auditはofficial audio identity、exact excerpt bytes、quota-stratified audit selectionが固定されるまでfail-closedであり、現在実行できるのはsynthetic fixtureだけである。
synthetic resultはsource identityと`formal_gate_eligible=false`を持ち、statusも`synthetic_fixture_*`としてformal passと区別する。

人手annotationは未取得であり、blind auditのpassを示すresultは存在しない。
MIDI compound eventを可聴attackの正解として公開gateへ使えるとは、まだ判断できない。

## 8. evaluatorとruntimeの未統合

現在のcandidate evaluatorはopened-diagnostic用の12列manifestと既存`Mel32`、`Mel40`、`Complex`、`Hybrid`だけを受け付ける。
formal developmentの21列manifest、5-fold、worst-fold margin、fixed SuperFlux、named reference controlは接続されていない。

すでにopenedとして固定した12演奏では、B-549契約の評価を独立resultへ二回実行し、395 label、489 prediction、TP 374、FP 115、FN 21、diagnostic gate failで一致した。
deterministic result SHA-256は`509121547b3d7233b27c6852f58bcb540a7ef92932fe0581278a0986e5ed07f3`、measurement definition SHA-256は`5a274bc84f5fac082f1fd66e5a53a1c168aeeaec7bc091d9bc21b5fbd8112c52`である。
archive memberを認証していないためstatusは`diagnostic_complete_unverified_archive_members`、`publication_eligible=false`であり、この数値をcandidate選定へ使わない。

このため、旧Mel 32 v2とfixed SuperFluxのformal score、全fold gate、winner、runtime tie-breakは未算出である。
named reference controlも候補scoreへ入れていない。

fixed SuperFluxはoffline library coreであり、ATTACK runtimeそのものではない。
Audio Thread ingress、独立worker、PRE/POST exact content join、common peak、history、request、transport、FFI payload、macOS namespace、Windows mapping、JUCE routeと画面は未実装である。
公開するOnset Fluxの単位とfull-scaleも、候補freeze前なので未確定である。

fresh DRUM holdout、2MIX development、2MIX fresh holdoutは一度もcandidate evaluatorで採点していない。
性能gateである48 kHz P95 50 ms以下、max 75 ms以下、二枠192 kHz drop 0、Audio Thread baseline不変も未計測である。

## 9. E-GMD MIDI隔離事故

2026-08-30 15:14:37から15:15:57 JSTに、trainまたはvalidationの代表MIDI path一覧を作るRuby処理がArray比較errorになった。
その結果、空のstdinが`bsdtar`へ渡り、検証済みMIDI ZIPの全entry抽出が始まった。

抽出先は`/tmp/kirin-attack-development-representatives.ShlDUS`で、14,506 filesが展開された。
公式metadataとの照合ではtest 5,289 rows中の2,021 unique pathがあり、内訳は`eval_session` 430件とsession-based 1,591件だった。
そのほかtrain 11,366件、validation 1,118件、LICENSEなど1件が含まれた。

この処理はarchive extractionだけで停止した。
candidate evaluator、MIDI parser、audio読込、音楽内容の表示または解析は実行していない。
後続確認は`find`によるpathと件数の確認、およびofficial metadata filenameが存在するかの照合だけだった。

展開directoryは`~/.Trash/kirin-attack-development-representatives.ShlDUS`へ移動した。
Trashを空にするまでは回収可能であり、本作業では中身を開かず、移動せず、削除せず、Trashを空にしない。
秘密情報は含まれていない。

この事故で候補内容を見たとは認定しないが、strict unopened contractは破られた。
したがって、original E-GMD fresh holdoutを「未開封」とは主張できない。

24 IDのopened-set ledgerは、評価器が実際に採点または診断した集合の記録である。
事故で抽出だけされた2,021 test pathを同じledgerへ混ぜず、独立したimmutable incident recordとblockerとして残す。
incident record SHA-256は`27c10a5c6de848029deb18c394181be76ed373d67269773f519708cf53494257`で、selector receiptへ隔離違反をhard blockerとして結び付けた。
guardian adjudicationまたは新holdoutの決定は未完である。

fresh DRUM holdoutへ進むには、guardianが隔離状態を裁定するか、新しい未接触holdoutを用意する必要がある。

## 10. Goを維持できる範囲

隔離事故はfresh holdoutの独立性を損ねるが、trainとvalidationを使うDRUM development研究まで無効にはしない。
開発集合はすでにopenedであることを前提に扱い、candidate freeze前のfold修正、provenance検証、blind audit、evaluator統合を継続できる。

fixed SuperFlux coreとreference controlもdataset scoreを使わず検証できる。
したがってDRUM研究基盤はGoを維持するが、fresh holdout選定と公開判断は停止する。

## 11. 次工程

1. guardian adjudicationまたは新しい未接触holdoutの方針を決める。
2. 上限付きexcerptまたはpool拡大を固定し、duration、compound、kick-only、hat-onlyのfold balance gateを満たす5-foldを再生成する。
3. MIDI rootをverified ZIP member一覧へ結び付け、audio archive、選択audio hash、重複、exact excerptを検証する。
4. quota-stratifiedな10分のblind acoustic audit planを固定し、二人のannotationでproxy gateを判定する。
5. formal 21列manifestと共通MIDI parserをcandidate evaluatorへ接続し、旧Mel 32 v2とfixed SuperFluxを全foldで採点する。
6. worst-fold全gate margin、FP/s、timing P95、runtimeの順でDRUMの一方式、一parameter set、一definition hashをfreezeする。
7. Slakh2100-reduxから2MIX developmentとfresh holdoutを独立に固定し、audio-only annotationを作る。
8. profileごとのcandidate freeze後にruntime、exact PRE/POST join、common peak、性能、macOSとWindows実機を検証する。
9. 新しい未接触holdoutをprofileごとに一度だけ開き、全gate通過後に限って公開routeを有効化する。

次工程6まではDRUM研究であり、公開ATTACKの実装開始条件ではない。
Phase 3以降へ進むには、対象profileのcandidate freezeとfresh holdout入口の独立性を先に確定する。

## 12. 一次資料

- [Böck and Widmer, Maximum Filter Vibrato Suppression for Onset Detection](https://phenicx.upf.edu/system/files/publications/Boeck_DAFx-13.pdf)
- [CPJKU SuperFlux v1.03 pinned source](https://raw.githubusercontent.com/CPJKU/SuperFlux/0a85f898af69188324b649a4c640052a5443078e/SuperFlux.py)
- [E-GMD公式データセット](https://magenta.withgoogle.com/datasets/e-gmd)
- [E-GMD論文](https://arxiv.org/pdf/2004.00188)
- [Magenta Groove MIDI mapping](https://magenta.tensorflow.org/datasets/groove)
- [Slakh2100公式サイト](https://www.slakh.com/)
- [Slakh2100-redux配布記録](https://zenodo.org/records/4599666)
- [W3C Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/)

## 13. 結論

DRUMは研究基盤Goとする。
formal candidate scoreとwinnerはまだ存在せず、original E-GMD fresh holdoutの未開封性も主張できない。

ATTACKは公開No-Go、未準備である。
このNo-Goは恒久中止ではなく、profile別のデータ、設定、hash、gate、runtime、fresh holdoutが揃うまで公開経路を閉じる判定である。
