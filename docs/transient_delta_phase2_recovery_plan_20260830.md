# ATTACK Phase 2-R 復旧方針

**日付**：2026-08-30
**実装ラベル**：B-557
**判定**：DRUM pilotの主要比率はGo、独立低域kick補助は棄却。kick missの候補blind HTML聴取待ち、timing、worst-fold、候補freeze、公開はNo-Go
**公開状態**：ATTACK route、request、worker、lease acquisitionは未実装かつOFF

**B-557進捗**：候補blind 45 clipへ自動保存HTMLを追加した。再生系、進捗、未完了移動、途中/完了TSVを一画面にし、可聴attack有無と時刻の聴取完了までdetectorを調整しない。

## 1. 決定

ATTACKは中止せず、Phase 2を評価器の修正からやり直す。
既存候補のthresholdを旧holdoutへ合わせる方法は採らない。
Precision、Recall、F1、FP/s、kick recall、hat recall、timing P95の既存合格値は下げない。
ATTACKは一つのrouteと画面を維持し、利用者が明示選択する`DRUM`と`2MIX`の二profileを持つ。
二つのplugin binary、二つのAnalysis lease、二つの同時workerには分けない。
各profileは独立したparameter、definition hash、public scale、development、fresh holdout、Go判定を持つ。
一度に動くprofileは一つだけとし、自動分類、自動切替、素材からのprofile推定を行わない。
ATTACKの初回表示はprofile未選択とし、利用者が選ぶまで解析requestを発行せず、選択は同じeditor lifetimeだけ保持する。
profile切替はrequestをretireし、historyとselectionをclearし、新しいdefinition epochでPREへ同じprofileを要求する。
一方だけがgateを通った場合は、そのprofileだけを公開可能とし、未達profileをselectorへ出さない。
Phase 3は、対象profileが修正済み評価器と新しい未閲覧holdoutの全合格値を満たすまで開始しない。

今回のGoは、Evaluator v2と次候補の研究を進める判断である。
利用者へATTACKを公開するGoではない。

## 2. B-546判定の扱い

`transient_delta_phase2_candidate_report_20260830.md`の測定値は、B-546実装を診断した履歴として保持する。
ただし、同報告を公開Goまたは恒久No-Goの根拠には再利用しない。

監査で次の評価契約上の問題を確認した。

| 問題 | 現行 | 影響 | Evaluator v2 |
|---|---|---|---|
| データ選択 | dataset rootの`selection.csv`が固定manifestより優先される | 同じbinaryでも対象が変わり、manifest hashも残らない | manifestを明示必須にし、SHA-256と全IDを結果へ記録する |
| 正解event | MIDI note-onを8 msで集約する | 最短20 msのrefractoryで分離不能な正解を要求する | 30 ms幅の広帯域compound eventへ固定する |
| instrument tag | kickが`35,36`、hatが`42,44,46` | E-GMD公式classと一致しない | kickを`36`、hatを`22,26,42,44,46`にする |
| matching | 誤差の小さいpairからgreedyに確定する | dense eventで最大TP数を保証しない | 最大cardinality、次に総絶対誤差最小で一対一対応する |
| timing max | matching幅とgateがともに30 ms | matched eventでは構造上必ずpassする | ±25 ms matchingとtiming P95、signed biasへ分離する |
| edge | 先頭zero paddingがなくwarmup frameを捨てる | 先頭付近のonsetを構造的に見落とす | 固定zero paddingとedge fixtureを追加する |
| candidate選定 | 42.370秒の6演奏で660構成を探索する | validationへ過適合しやすい | grouped cross-validationへ置き換える |
| winner選定 | 全gate marginではなくF1最大を先に選ぶ | pass可能な別構成を見落とし得る | worst-foldの全gate marginで選ぶ |

固定18演奏のraw MIDI note-onは3,798件である。
8 ms集約では3,219 event、30 ms集約では2,673 eventとなり、正解数は546件、約17.0%変わる。
8 ms集約後には30 ms未満で隣接するeventが598組あり、旧評価契約と検出器の時間分解能は無視できない規模で不整合だった。

同じ評価器はdataset rootのローカルmanifestを暗黙に読むため、現在の一時領域では12演奏、F1 0.8046を返し、B-546報告の18演奏、F1 0.6800を再現しない。
この再実行も全gateは通らないが、データ選択だけで結論が大きく変わることを示す。

したがって、F1 0.6800を現行ODFの能力限界とは断定しない。

## 3. profile別ATTACK eventの固定契約

`DRUM`が数える対象はdrumまたはpercussion bus上のcompound attackである。
`2MIX`が数える対象は完成mix上で人が知覚するmusically relevant broadband attackである。
instrument tagは原因別診断にだけ使い、公開機能は楽器を分類しない。

`DRUM`のEvaluator v2は次の規則を固定する。

1. tempo mapを積分した各MIDI note-on時刻をhalf-upで整数µsへ丸め、velocityが0でない時刻、pitch、velocityを保持する。
2. 最初のnote-onを起点に、整数時刻差が30,000 µs以下になるnote-onを一つのcompound eventへまとめる。
3. 連鎖的なroll全体を一eventへ潰さないため、直前noteとの間隔ではなくクラスタ全体のspanを判定する。
4. event時刻はクラスタ内の整数note-on時刻の算術平均をhalf-upで整数µsへ丸める。
5. instrument tagはクラスタ内pitchの集合和とする。
6. 同時kickとhatは全体指標では一eventとし、kick-containing、hat-containing、kick+hatの各診断にも含める。
7. 30 msを超えて隣接するeventは別eventとして残し、30 ms超から50 ms以下のpairをdense診断へ別集計する。
8. velocityによる正解除外は行わず、velocity帯別recallとmeasurement floor近傍を別に報告する。

予測sample `p`、sample rate `r`、正解µs `l`は`abs(p*1,000,000-l*r)<=25,000*r`をi128で満たす場合だけ対応可能とし、浮動小数へ変換しない。
対応数を最大化し、対応数が同じ場合は総絶対時刻誤差を最小化し、さらに同値なら早い正解、早い予測の順で決定する。
一つの正解付近にある二つ目以降の予測はfalse positiveとする。
一つの予測で複数の正解を満たしたことにはしない。

fixtureは29 ms、30 ms、31 msを必ず含み、30 ms以下を一event、30 ms超を二eventとして境界を固定する。

E-GMDのMIDI compound eventは`DRUM`用の可聴attack proxyであり、人手のacoustic annotationそのものではない。
候補出力を見ない二人のannotatorがdevelopmentから固定seedで選んだ10分を独立に注釈し、MIDI proxyが各annotatorに対してPrecisionとRecall 0.95以上になることを候補評価前に確認する。
annotator間F1が0.90未満またはMIDI proxyが未達なら、E-GMDのMIDIだけを公開gateの正解には使わない。

## 4. Evaluator v2の再現性契約

評価コマンドは`--manifest <path>`を必須にし、dataset rootのファイルを暗黙に採用しない。
既定manifestを使う場合も、CLIでその選択を明示する。

各実行は機械可読なresult artifactへ次を記録する。

- Git commitとmeasurement definition hash。
- evaluator versionと候補parameter set。
- 23列manifest identifier、SHA-256、row数、split別件数、全performance ID。
- excerpt mapping version、元duration decimal、44.1 kHzの半開`[start_sample,end_sample)`、sourceとexcerptのhash。
- MIDIとWAVの相対path、SHA-256、sample rate、channel数、sample数、duration。
- sourceとexcerptそれぞれのraw note数、compound event数、kick-only、hat-only、instrument tag分布、velocity分布、density分布。
- prediction数、TP、FP、FN、duplicate、merged、track別指標、subgroup別指標。
- 実行日時と候補選定前後のmanifest状態。

入力preflightは欠損、重複ID、split漏洩、非finite、source MIDIの空または破損、annotationの音声範囲外、duration差、format不一致をfailにする。
選択excerpt内のnoteまたはeventが0でもnegative intervalとして保持し、無音coreもその事実だけでは除外しない。
E-GMD側に既知の利用不能trackがあるため、除外規則はscoreを見る前に固定し、除外理由をresult artifactへ残す。

tempo changeがtick 0にある場合はMIDI内の実tempoをdefault tempoより明示的に優先する。
同tick event、running status、複数track、leading event、trailing eventをunit testにする。

## 5. 評価データの分離

B-546の18演奏とB-548の12演奏には6演奏の重複があり、既閲覧集合は24 unique performance IDである。
内訳はvalidation 6件、test 18件とし、24件すべてを今後のfresh holdoutから除外する。
validation 6件はdevelopmentへ使用できるが、test 18件は`opened-diagnostic`だけに隔離し、新規developmentへ入れない。
このvalidation 6件だけでは次の最低構成を満たさないため、正式なcandidate freezeには使用できない。
2026-08-30のtest MIDI誤抽出事故によりoriginal E-GMD testを未開封とは主張せず、guardian裁定または新しい未接触holdoutをfresh評価の必須条件とする。

新規developmentはE-GMDのtrainとvalidationから290 unique performance IDを固定し、各foldへ58 IDずつ割り当てる。
短尺は全区間、長尺は候補出力に依存しない最大30秒のhash-windowを使い、採点するexcerpt合計を30分以上にする。
44.1 kHzで最大1,323,000 sample、startは441 sample単位、end exclusiveとする。
selection rankとexcerpt位置は別domainとversionを持ち、score確認後にseedを引き直さない。
元metadata durationはdecimal文字列から整数sampleへhalf-upし、`f64`を境界の正本にしない。

候補選定はperformance IDで分離した5-fold grouped cross-validationを使い、次をhard gateとする。

- performance IDを各fold同数にする。
- beatとfillのfold間ID数差を各1以下にする。
- durationとcompoundのmax/minを各1.25以下にする。
- kick-onlyとhat-onlyのmax/minを各1.50以下にする。
- 各foldへkick-only 150 event以上、hat-only 300 event以上を含める。
- 各foldへkick-positiveとhat-positive performance IDを各8以上含める。
- duration、compound、kick-only、hat-onlyの単一ID shareを各foldで25%以下にする。

drummer、session、kit、style、tempo、density、split、既閲覧validation配置は決定的な探索objectiveと監査値にし、追加hard gateにはしない。
固定prefixは170 ID以下が30分未達、175から285 IDがhat必要総数未達であり、290 IDで全fold gateを初めて証明した。
別途leave-one-drummer/session-outの診断を出し、特定奏者だけで成立する候補を採用しない。

guardianが隔離事故後のE-GMD test再利用を明示許可した場合に限り、未評価performance IDから次の最低構成でfresh manifestを先に固定する。
許可されない場合は、同じ最低構成を満たす新しい未接触holdoutへ置き換え、E-GMD testをfresh判定に使わない。

- 30以上のunique performance ID。
- unique sequence duration 15分以上。
- testに存在する全drummer、beat、fillを含む。
- 8以上のstyle、8以上のkitを含む。
- developmentとperformance ID、MIDI、WAVの重複を許さない。
- kick-onlyとhat-onlyのcompound eventを各100件以上含む。

fresh holdoutのmanifestとSHA-256をcommitした後にだけ、対象音声を取得または評価器で開いて一度だけ評価する。
holdout scoreを見た後のthreshold、event contract、除外規則、候補変更を禁止する。

E-GMD v1.0.0の公式archive hashをdataset identityとし、選択した配布fileを変換せず保存する。
canonical ingestは44.1 kHz、mono、integer PCM 16または24 bitをexact f32 scaleでdecodeし、それ以外をfailにする。
AudioIngestは選択renderのmanifest decimal由来sample数と実audio sample_countを別々に型付きで記録、hashし、`core_end <= actual_sample_count`を必須にする。
B-552の固定290件は両sample数のexact一致を要求し、最初と最後のraw annotationは実音声範囲の前後2 ms以内とする。診断用`f64` duration比較をprovenanceの代用にしない。

metadata snapshot、selection algorithm version、固定seed `ATTACK-V2-20260830`をcommitし、version、seed、split、performance IDを長さprefix付きでdomain separationしたSHA-256順に候補とreserveを決める。
excerpt位置はselection rankと異なるdomainで同じidentityをhashし、位置数へu128 multiply-highで写像する。
kit renderは順位を独立に持たず、同じperformance IDのfoldを継承する。
quota充足、fold割当、既知不良trackのreserve置換は同じ決定的scriptだけで行い、人手でeasy trackを選ばない。
manifest、fold、reserve順をcommitしてから対象audioを開く。

`2MIX`にはCC BY 4.0のSlakh2100-reduxを使い、E-GMD scoreを2MIX判定へ流用しない。
B-552まで2MIXのdata、audio、annotation、候補、holdoutを開いておらず、以下は将来の独立契約である。
重複MIDIをsplit間から除いた公式redux splitから、train/validationに30秒excerptを20本、testに30秒excerptを20本、同じ固定seedで選ぶ。
各集合は10分以上とし、piano、bass、guitar、drumsに加えて利用可能なstrings、brass、reedの比率をmanifestへ固定する。
二人のannotatorがaudioだけを聴き、候補出力とMIDIを見ずにmusically relevant broadband attackを独立に記録する。
annotator間F1は±25 ms strict matchingで0.90以上を要求し、候補は二つのannotation setそれぞれに対して全一般gateを通す。
test annotationのSHA-256をcandidate freeze前にcommitし、内容はfresh holdout実行まで開発者と評価器から隔離する。

## 6. 合格指標

二profileへ共通する主要な数値gateは維持する。

| 指標 | 公開合格値 |
|---|---:|
| Precision | 0.85以上 |
| Recall | 0.75以上 |
| F1 | 0.80以上 |
| timing absolute error P95 | 15.0 ms以下 |
| false positive | 1.0回/秒以下 |
| signed timing medianの絶対値 | 48 kHzの一hop、`256 / 48000`秒以下 |
| kick-only compound event recall | 0.75以上 |
| hat-only compound event recall | 0.50以上 |

旧timing max 30 msは、旧matching幅と同値で独立したgateになっていなかったため、Evaluator v2では診断値として残す。

全体指標はmicro値とperformance単位macro値を同時に出す。
P/R/F1 macroは正解eventがあるperformance IDだけを対象とし、正解あり予測0を0として残す。
negative-only performanceはP/R/F1 macroから除外するが、予測0を含む全performance IDをFP/s macroへ入れる。
kick/hat macroは該当class正解がある全performance IDを対象とし、TP 0をrecall 0にする。
timingはfold単位のmicro gateとし、match 0を合格にしない。
`DRUM`のcandidate freezeにはE-GMD、`2MIX`には一般mixのpooled out-of-fold値と全foldが該当する上表のgateを満たすことを要求する。
kick-onlyとhat-onlyのgateは`DRUM`だけへ適用し、その他のgateは各profileへ独立に適用する。
一profileの余裕で他profileの未達を相殺しない。
選定はF1単独最大ではなく、各gateまでの正規化marginのworst-fold値を最大にする。
全候補を事前に封印したCandidateSetReceiptの完走を確認し、callerが渡すbooleanや無名marginだけでwinnerを選ばない。
同値ならFP/s、timing P95、worker時間の順に小さい候補を選ぶ。

次の値は原因診断として必ず報告する。

- performance、drummer、session、kit、beat/fill、tempo、density別のPrecision、Recall、F1。
- hat-only、kick-only、kick-containing、hat-containing、kick+hat、単打、複合eventのRecall。
- 30 ms超から50 ms以下のdense pairを両方検出した割合。
- duplicate FP/eventとmerged FN/event。
- timing absolute P50、P95、maxとsigned mean、median。
- velocity帯別Recallとmeasurement floor近傍の未検出数。

E-GMD mixだけではfalse positiveがhatに起因したかを特定できない。
hat false-positive gateは再配布可能なisolated-hat fixtureで測り、一打あたり余分なmarkを0.05以下とする。
同fixtureのhat single-stroke recallは0.95以上とする。

実演奏のPRE/POST共通系列もPhase 2で評価する。
`DRUM`はE-GMD、`2MIX`は一般mixのPREを原音とし、POSTへidentity、-24/-12/+6 dB gain、100 Hz low shelf ±12 dB、8 kHz high shelf ±12 dB、fast compressor、slow compressor、5 ms lookahead limiterを決定的に適用する。
fast compressorはthreshold -24 dBFS、ratio 10:1、attack 0.1 ms、release 50 ms、makeup 0 dBとする。
slow compressorはthreshold -18 dBFS、ratio 4:1、attack 30 ms、release 100 ms、makeup 0 dBとする。
limiterはceiling -1 dBFS、lookahead 5 msとし、全transformの係数式、channel処理、latency metadata、version hashをaudio評価前にcommitする。
low shelfとhigh shelfはAudio EQ Cookbookのbilinear transform式を使い、Q、slope、端数処理も同じtransform hashへ含める。
各pairはexact content join後の`max(PRE, POST)`を同じ正解へ照合し、各transformが該当する全gateを個別に通ることを要求する。
identity pairはcommon event ID一致とdelta 0を別fixtureで要求する。

## 7. 検出器の主線

B-546のMel 32、Mel 40、Complex、Hybridは比較履歴として残す。
旧thresholdの再調整とComplexまたはHybridの拡張を主線にしない。

Evaluator v2完成後、B-546 Mel 32と20 ms ruleを変更せず再採点し、評価器補正量だけを診断する。
この旧ruleを公開候補にはしない。
`DRUM`ではMel 32をEvaluator v2の30 ms共通peak ruleと事前登録gridで別に選定し、未達ならfixed-scale SuperFlux-styleへ進む。
`2MIX`ではvibratoと持続音のFP抑制を主目的として、fixed-scale SuperFlux-styleを第一候補に固定する。
二profileは同じanalyzer実装を共有できるが、window、bank、threshold、public scale、definition hashを共有する義務はない。
profile間のOnset Flux数値を直接比較できるとは表示しない。

SuperFlux-style候補の連続値を次で定義する。

1. periodic Hann後にLとRを別FFTし、補償済みone-sided powerを`P_k=(P_L,k+P_R,k)/2`としてから後段へ渡す。
2. MIDは`(L+R)/2`、SIDEは`(L-R)/2`をFFTし、mono SIDEはinvalidとする。
3. DCとNyquistをbankから除外し、`g_fs = sum(w) / sqrt(2 * sum(w^2))`と`M_k[n] = sqrt(P_k[n]) / g_fs`でcoherent-gain基準の固定scaleへ変換する。
4. A4 440 Hzをanchorに`440 * 2^(q/bands_per_octave)`でcenter候補を作り、30 Hz未満と17 kHz超のcornerを各一つ含める。
5. HzからFFT binはnearest integer、tieはaway from zeroとし、重複binを低い周波数側から一つにまとめる。
6. 30 Hzから17 kHz内の各centerと直前直後のunique binを三点にして、Hz軸で0、1、0となる非等面積triangular bankを作る。
7. unique binが三点未満のbankはfailにし、全integer triplet、生成version、Nyquist clampをdefinition hashへ含める。
8. band magnitudeを`A_b[n] = sum_k F[b,k] * M_k[n]`とする。
9. `a0 = 10^(reference_dBFS/20)`として`L_b[n] = log10(1 + A_b[n]/a0)`を求める。
10. 過去frameの隣接band最大を`R_b[n-mu] = max L_j[n-mu]`、ただし`j`は`b-r`から`b+r`とする。
11. `D_b[n] = max(0, L_b[n] - R_b[n-mu])`とする。
12. 実現band数差を除くため、公開候補値を`S[n] = mean_b D_b[n]`とする。

このfixed scaleはcoherent sineの規約であり、zero padding、bin位相、広帯域noiseに対する完全なwindowまたはsample-rate不変性を主張しない。
44.1、48、88.2、96、176.4、192 kHzのtone、impulse、noise、gain sweepで分布差を測り、thresholdを最終window definitionへ結び付ける。

PREとPOSTは独立にpeak pickしない。
exact content join後にだけ`C[n] = max(S_PRE[n], S_POST[n])`を作り、共通eventを選ぶ。

frame centerは物理sourceのsample 0を起点に`0, hop, 2*hop, ...`とし、window supportを`[center-floor(N/2), center+ceil(N/2))`、物理source外だけをzero paddingとする。
30秒coreを独立audioとして両端zero paddingせず、実source contextを解析してpredictionとlabelの採点だけをcore半開区間へ限定する。
offline末尾は物理sourceの最後のsampleを含むcenterまでflushし、event sampleはselected frame center、plateauは最早frame、global offsetとonset backtrackは0へ固定する。
B-552でもこのcontext guardが未実装であり、formal loadとscoreを`not_ready_context_guard_unimplemented`で停止する。

共通peakは次の四条件をすべて満たすframeとする。

1. inclusiveな`[n-pre_max, n+post_max]`で最早のlocal maximumである。
2. inclusiveな`[n-pre_avg, n+post_avg]`のmeanへ固定`delta`を加えた値以上である。
3. 固定absolute floor以上である。
4. event centerが直前eventから`round(0.030*sample_rate)` sampleを超えている。

refractoryは秒へ変換する前の整数sampleで判定する。
refractory内の候補はcommon valueが大きい方を残し、同値なら早いevent centerを残す。

local meanはevent選択専用であり、PRE値、POST値、公開deltaを再スケールしない。
PREとPOSTへ別々のmoving thresholdを適用しない。
session maximum、running maximum、percentile normalization、adaptive whitening、material-dependent gainは使わない。

frame grid、padding、flush、timestamp、plateau tie-break、inclusive幅をE-GMD score前にfixtureで固定し、definition hashへ含める。
候補別、track別、session別のtiming補正を禁止する。
`post_max`または`post_avg`が0より大きい候補はworker表示だけのbounded lookaheadとし、音声を遅延させずmarker latency gateへ全待ち時間を含める。
FFT scratchはprepare時に`get_inplace_scratch_len()`分を確保し、steady stateでは`process_with_scratch()`だけを使う。

## 8. 事前登録する探索範囲

探索はfront endとpeak pickerを分け、全要素を同時に最適化しない。

### Stage 1: front end

peak幅を`pre_max=6 hop`、`post_max=0`、`pre_avg=19 hop`、`post_avg=0`、`refractory=30 ms`へ固定する。

- 48 kHz基準window：1,024または2,048 sample。
- hop：256 sample相当の5.333 msへ固定。
- bands/octave：12または24。
- frequency maximum radius `r`：0または1。
- spectral lag `mu`：Hann height ratio 0.5の式でwindowと結び、1,024 sampleでは1 hop、2,048 sampleでは2 hop。
- `reference_dBFS`：-80、-70、-60、-50 dBFS amplitude。
- mean-band log10単位の`delta`：0.00625、0.008、0.0125、0.025、0.05、0.10、0.20。
- mean-band log10単位のabsolute floor：0、0.025、0.05、0.10、0.20、0.40。

各front endは上の同一threshold gridからworst-foldの全gate marginが最大になる組を使って比較する。
controlの`r=0` log-filtered fluxは同じ単位とgridを使う。
旧Mel 32は単位が異なるためB-546の固定ruleで別に再採点し、Stage 1のthresholdを共有しない。
採用可能なMel 32には`delta`を0.25、0.5、0.75、1.0、1.5、2.0 dB/frame、absolute floorを0、0.5、1.0、1.5、2.0、3.0 dB/frameとする専用gridを使う。

外部式との照合には`paper_2013_online`と`cpjku_1_03_online`の二つのnamed controlを使う。
前者は論文の27.5 Hzから16 kHz、pre-max 30 ms、pre-avg 100 msを、後者はCPJKU v1.03既定の30 Hzから17 kHz、pre-max 10 ms、pre-avg 150 msを固定する。
両controlは44.1 kHz、window 2,048 sample、200 fps、24 bands/octave、`mu=2`、`r=1`、symmetric Hann、band sum、NumPy roundingを含む全parameterを別hashへ記録し、candidate rankingへ参加させない。
CPJKU controlはsourceの字義どおり`combine=0.03`をさらに1,000で割るeffective 30 µsを再現し、意図された30 msのpaper controlと同一視しない。

### Stage 2: peak picker

Stage 1のfront endを一つに固定した後、次だけを比較する。

- `pre_max`：3または6 hop。
- `post_max`と`post_avg`：0、1、2 hopを同じ値で使う。
- `pre_avg`：12、19、24 hop。
- mean-band log10単位の`delta`：Stage 1と同じ固定grid。
- mean-band log10単位のabsolute floor：Stage 1と同じ固定grid。

実測値がgridの全域から外れる場合だけ、fresh holdoutを作る前に物理単位を根拠としてgridを一度改訂できる。
per-track quantileやholdout scoreを根拠にgridを改訂しない。

### Stage 3: 限定multiband

`DRUM`でStage 2後にkick-onlyまたはhat-onlyのworst-foldだけが未達の場合に限り、固定multibandを一度比較する。
`2MIX`へmultibandを適用しない。

- Layout A：30–200 Hz、200 Hz–2 kHz、2–17 kHz。
- Layout B：30–250 Hz、250 Hz–4 kHz、4–17 kHz。

帯域境界は下端inclusive、上端exclusiveとし、最上帯域だけ17 kHzを含める。
各帯域をmeanし、Stage 2 winnerの同一`delta`とabsolute floorを三帯域へ共有して追加探索を禁止する。
exact join後の共通系列上で候補をunionし、最初の候補からspan 30 ms以下を一clusterとして最大prominence、同値なら最早時刻をcommon eventにする。
帯域ごとにPREとPOSTを独立判定しない。
Stage 3が選ばれても公開値はall-bandの`S_PRE`、`S_POST`、その差分とし、帯域unionはevent選択専用とする。
全主要gateを満たし、all-band候補よりworst-fold PrecisionとFP/sを悪化させない場合だけ採用する。

## 9. 再配布可能fixture

Evaluator v2と候補は、E-GMD評価前に次を通す。

- silence、DC、定常sine、定常noiseで余分なevent 0。
- isolated kick、snare、hat、広帯域impulseのvelocityとgain sweep。
- kick+hat同時打撃と29、30、31、35、40、50、65 msのclose pair。
- ghost noteとmeasurement floorの上下。
- 増大するroll、減衰するroll、cymbal tail、vibrato、tremolo。
- 先頭0 ms付近、末尾、短いbuffer、sample rate切替。
- note/event 0のnegative excerpt、source先頭以外から始まるcore、core境界直前と直後のevent。
- source-origin frame grid不変、core外prediction非採点、core内prediction採点。
- PRE/POST identity、固定gain、fixed delay、lookahead compressor、PRE-only、POST-only。
- 無音、欠損、loop、transport後退、worker panic、restart。
- 44.1、48、88.2、96、176.4、192 kHzとmono、stereo、dual-mono、MID、SIDE。

30 ms以下のclose pairは一event、30 ms超の明瞭な合成impulse pairは二eventを要求する。
identityでは同一common event IDと全delta 0を要求する。
fixed delayとlookaheadでexact content mappingが一致しない場合はdeltaを出さない。

## 10. fresh holdout前の性能成立性

fresh holdout前に最終候補とparameterからalgorithmic latencyの下限と上限をsample単位で算出する。
window support completion、post lookahead、host block到着、worker queue、publish、30 Hz paint phaseをすべて含め、理論上gateを超えるparameterをfreeze対象から外す。

offline analyzerのrelease benchmarkはiMac20,1の10-core Intel Core i9、stereo、各sample rate、host block 32、64、128、256、512、1,024 sampleで行う。
一度に一profileだけを実行し、profile切替後に旧profileのworker、queue、scratchが残らないことを測る。
5秒warm-up後に10分測定し、FFT、bank、ODF、exact join、peak、payload生成を含むhop単位P50、P95、P99、maxを記録する。
一workerのP99をhop時間の25%、48 kHzでは1.333 ms以下とし、maxも必ず記録する。

192 kHz二枠simulatorを10分連続で動かし、drop 0、backlogが一frame以内、queue high-waterが事前固定capacityの25%以下であることを要求する。
marker latencyは`decision availability - event_sample`、`worker publish - decision availability`、`first visible paint - worker publish`へ分解する。
三成分の合計をend-to-end値とし、48 kHzでP95 50 ms以下、max 75 ms以下をfresh holdout前に実測する。

## 11. 実行順序

1. B-550でDRUMのN=290 selection、23列manifest、fold balance、formal candidate config、formal metric基盤を固定した。
2. B-551でverified MIDI archive member、sourceとcropped note/event hash、cross-IDとcross-split重複検査を固定した。
3. B-552でofficial audio archiveの同一full SHA読み取りからsource、core、maximum-context PCM、重複、無響、MIDI終端を固定した。
4. B-553でsource sample 0起点のdevelopment pilotを実行し、SuperFluxの主要比率通過とkick、timing、worst-fold不足を実測した。
5. 30–200 Hzの独立低域SuperFluxはPrecisionとFP/sを悪化させたため棄却し、追加調整しない。
6. kick不足が集中するdrummer 4、5、7をevent診断し、候補状態を隠した45 clipを固定した。聴取で可聴attack有無と時刻だけを注釈する。
7. 通過候補だけをLODO、LOSO、paired transform、runtimeで確認し、一方式、一parameter set、一definition hashを残す。
8. candidate freeze前に必要なartifactをsource commitへ固定し、caller指定hashをtrust rootにしない。
9. 対象profileのfresh holdout manifest、sealed annotation hash、transform hashをcommitする。
10. 各profileのfresh holdoutと全paired transformをprofileごとに一度だけ評価する。
11. 対象profileの全gateを通った場合だけPhase 3へ進み、worker、pairing、identity、性能、macOS、Windowsを同一commitで通した場合だけ公開Goとする。

B-551のMIDI receiptとB-552のaudio receiptは各単独componentとしてGoだが、formal CLIは全receiptを結ぶsource pinが無いため、authorization file、dataset root、manifest、candidate config、result pathへ触れる前に`formal_authorization_not_pinned_in_source_commit`で停止する。

## 12. 停止条件

次のどれかに該当した時点で対象profileの探索と公開をOFFのまま停止する。

- `2MIX`のStage 2が一gateでも未達になる。
- `DRUM`のStage 2未達がkick-onlyまたはhat-onlyだけに限定できず、Stage 3の開始条件を満たさない。
- `DRUM`の限定multibandがall-bandよりworst-fold PrecisionまたはFP/sを悪化させる。
- 対象profileのfresh holdoutまたはpaired transformが一項目でも未達になる。
- 48 kHzの一worker処理が一hop時間の25%、約1.333 msを超える。
- 48 kHzのmarker latencyがP95 50 msまたはmax 75 msを超える。
- 二枠192 kHzでingress dropが一件でも発生する。
- Audio Threadの作業量または既存四画面のbaselineが変わる。

候補またはholdout固有の失敗は対象profileだけを停止し、共有runtime、Audio Thread、既存画面baselineの失敗はATTACK全体を停止する。

fresh holdout失敗後に同じprofileと候補のthresholdを調整しない。
失敗したprofileのholdoutはdevelopmentへ移し、別の構造変更がない限り同profileで新しいholdoutを開かない。
他profileの合格または不合格はこの判定を変更しない。

今回の探索ではML、adaptive whitening、decaying running maximum、session max、percentile normalizationへ広げない。
2MIXのSuperFlux-style、またはDRUMのSuperFlux-styleと限定multibandでdevelopmentを通らない場合は、別方式を新しい設計判断として提案し、Phase 2-Rを自動延長しない。

## 13. Goの見通し

Goの可能性はある。
現No-Goには評価器が作った見かけのFNと不安定なデータ選択が混ざっており、旧F1 0.6800だけで構想を棄却する根拠にはならない。
さらに、SuperFluxは一般mixのonline評価でPrecision 0.855、Recall 0.787、F1 0.820を報告し、Hyphaの主要比率gateを外部結果上は超えている。
同論文のthresholdは同じ評価dataset上で選ばれているため、この数値を独立holdoutの代用にはしない。

ただし、その外部結果はHyphaのE-GMD split、FP/s、kick、hat、PRE/POST exact pairingを保証しない。
したがって、見通しは肯定するが、公開Goはfresh holdoutとruntime gateの実測だけで決める。
B-553までにDRUMの主要比率は通過したが、kick、timing、worst-fold、candidate freeze、fresh holdout、runtime、公開のGoではない。

## 14. 参照資料

- [Böckらによるonline onset評価、30 ms集約、±25 ms strict評価](https://www.cp.jku.at/research/papers/Boeck_etal_ISMIR_2012.pdf)
- [BöckとWidmerによるSuperFlux](https://phenicx.upf.edu/system/files/publications/Boeck_DAFx-13.pdf)
- [CPJKU SuperFlux参照実装](https://github.com/CPJKU/SuperFlux)
- [mir_eval設計論文](https://colinraffel.com/publications/ismir2014mir_eval.pdf)
- [Magenta Groove MIDI mapping](https://magenta.tensorflow.org/datasets/groove)
- [E-GMD公式データセットページ](https://magenta.withgoogle.com/datasets/e-gmd)
- [E-GMD論文](https://arxiv.org/pdf/2004.00188)
- [Slakh2100公式サイト、aligned MIDI、redux split、CC BY 4.0](http://www.slakh.com/)
- [W3C Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/)
