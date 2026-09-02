# ATTACK Phase 2-R formal development gate報告

**日付**：2026-08-30
**実装ラベル**：B-550
**対象profile**：DRUM
**DRUM研究判定**：fold成立性はGo
**候補freeze判定**：No-Go、正式採点未実施
**公開判定**：No-Go、ATTACKはOFF

> B-551でMIDI archive-member provenanceとcanonical重複監査がGoになった。
> B-552でaudio archive-member、PCM、対応MIDIのprovenanceもGoになった。
> 現在の入力証拠と残るNo-Go条件は`docs/transient_delta_phase2_audio_provenance_report_20260830.md`を正本とする。

## 1. 判定

B-550で、DRUM developmentを候補の成績から独立に選び、290 performance IDを58 IDずつ五foldへ割り当てる契約が成立した。
固定したduration、compound、kick-only、hat-only、positive-ID、単一ID寄与率の全fold gateは不足0である。

同時に、23列formal manifest、事前登録候補config、五fold集計、performance macro、kick/hat contributor macro、worst-fold marginの型と検証経路を接続した。
ただしformal CLIはsource commitに認可hashが固定される前に、authorization、dataset root、manifest、candidate config、result pathのいずれにも触れず停止する。
source contextを保つ採点、FormalAuthorization、blind acoustic audit、sealed candidate setも未完成である。

したがって今回のGoは、DRUMのdevelopment selectionとfold成立性に限る。
候補方式、threshold、definition hash、fresh holdout、runtime、Phase 3、公開ATTACKのGoではない。

| 対象 | 判定 | 根拠 |
|---|---|---|
| DRUM selection v2 | Go | N=290、23列manifest、二回実行byte一致 |
| DRUM fold balance | Go | 五foldの全hard gate合格、deficit 0 |
| DRUM formal candidate score | No-Go | filesystem入力前のsource-pin blockerとcontext blocker |
| DRUM candidate freeze | No-Go | FormalAuthorization、blind audit、candidate set、LODO/LOSO未完 |
| 2MIX | No-Go、未着手 | B-550ではdata、audio、annotation、候補を開いていない |
| 公開ATTACK | No-Go | route、request、worker、lease acquisition、UIはOFF |

## 2. 60演奏版をformal入力にしない理由

B-549の60 IDは研究基盤を検証するprovisional selectionであり、formal candidate developmentには使わない。
旧foldの最大対最小比はduration 1.93、compound 2.63、kick-only 5.91、hat-only 5.49だった。
再配置だけで四指標を同時に均衡させる最良共通比も3.2489であり、fold間の難度と証拠量を揃えられない。

また、全尺を使うと一つの長いperformanceがfoldのdurationとeventを支配する。
ID数だけを等しくしても、worst-fold gateが素材差ではなく単一performanceの成否を表す危険がある。

B-550は、上限付きexcerptとpool拡大を同時に固定してこの問題を解いた。
旧60 IDのmanifest、fold、receipt hashはB-549の診断履歴として保持し、B-550 artifactへ変換しない。

## 3. 固定excerpt契約

excerpt mapping versionは`attack-drum-hash-window-v1`である。
正本sample rateは44.1 kHz、最大長は1,323,000 sample、30秒とする。

sourceが30秒以下なら半開区間`[0, source_samples)`を全て使う。
sourceが30秒を超える場合は、selection rankとは別domainのSHA-256から441 sample、正確に10 ms単位のstartを決める。
位置数への写像はu128のmultiply-highを使い、剰余による位置biasを作らない。
endは`start + 1,323,000`であり、end sample上のnoteを含めない。

metadata durationは元のdecimal文字列を保存し、その整数部と小数部から44.1 kHz sampleへhalf-upで変換する。
`f64`へ変換してから文字列へ戻した値をsample境界の正本にしない。
manifestは元decimal、整数start、整数endを同時に持つ。

MIDI note-onはtempo map積分後に整数µsへhalf-upし、次の整数式でexcerptへ入れる。

`start_sample * 1,000,000 <= note_micros * 44,100 < end_sample * 1,000,000`

crop後のraw noteだけから30 ms non-chaining compoundを作り直す。
excerpt内のraw noteまたはcompound eventが0でも、有効なnegative intervalとして保持する。
source MIDI自体が空、破損、または許容duration外の場合だけpreflightで除外する。

290 IDのうち216 IDは短尺全区間、74 IDは長尺hash-windowになった。
長尺startの四分位bin件数は`22 / 25 / 12 / 15`、平均正規化位置は0.454338、中央値は0.419567、KS診断値は0.149340だった。
この固定realizationには軽い前半寄りがあるため、完全なdataset時間一様性を主張しない。
seedを結果確認後に引き直すことは禁止し、推論単位をperformance IDとする。

## 4. N=290の固定根拠

selectionはB-549のquota-first 60 IDをそのままprefixとして維持し、同じ固定rankのreserveを追加する。
探索対象は175から400 IDまでの5 ID刻みとし、B-550の固定targetを290 IDとした。

170 IDまでのexcerpt合計は79,374,263 sampleであり、30分に必要な79,380,000 sampleへ5,737 sample不足する。
したがって170 ID以下の固定prefixはduration gateを通れない。

N=285 prefixのhat-only合計は3,354 event、最大単一IDは236 eventである。
この最大IDはselection rank 17で入り、175以降の全prefixに含まれる。
単一ID shareを25%以下にするには、そのIDが入るfoldへ最低944 hat eventが必要になる。
hat fold比を1.50以下にするには、残り四foldへ各630 event以上が必要になる。
必要総数は3,464 eventであり、N=285の3,354 eventでは数学的に不足する。

N=290では、同じ固定selectionと決定的fold探索で全hard gateを通る割当が得られた。
したがって5 ID刻みの固定prefixでは、290が不成立範囲の直後に成立を証明した最初のtargetである。

## 5. fold hard gate

fold hard gateを次の一組へ固定した。

| gate | 合格条件 |
|---|---:|
| performance ID | 各fold同数 |
| beat ID spread | 最大差1以下 |
| fill ID spread | 最大差1以下 |
| duration max/min | 1.25以下 |
| compound max/min | 1.25以下 |
| kick-only max/min | 1.50以下 |
| hat-only max/min | 1.50以下 |
| kick-only event | 各fold 150以上 |
| hat-only event | 各fold 300以上 |
| kick-positive performance ID | 各fold 8以上 |
| hat-positive performance ID | 各fold 8以上 |
| 単一ID share | duration、compound、kick、hatの各foldで25%以下 |

drummer、session、kit、primary style、tempo、density、split、既閲覧validation配置は、決定的探索のobjectiveと監査値にする。
これらは多数の小bucketを同時にhard制約化して選択を歪めないため、公開gateではない。

実測結果は次のとおりである。

| fold | ID | duration秒 | compound | kick-only | hat-only | kick-positive ID | hat-positive ID |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 58 | 646.735 | 3,902 | 280 | 633 | 37 | 30 |
| 1 | 58 | 593.091 | 3,313 | 250 | 630 | 45 | 35 |
| 2 | 58 | 623.679 | 3,627 | 223 | 944 | 45 | 35 |
| 3 | 58 | 605.717 | 3,568 | 300 | 634 | 32 | 26 |
| 4 | 58 | 632.849 | 3,801 | 289 | 630 | 40 | 41 |

最大対最小比はduration 1.090448、compound 1.177784、kick-only 1.345291、hat-only 1.498413である。
hat-onlyは上限1.50に近いため、比較は表示丸め値ではなく整数cross-productで行う。
最大単一ID shareはhat-onlyのfold 2で正確に0.25であり、全metric、全foldが上限以下だった。
beatは`22 / 23 / 23 / 22 / 22`、fillは`36 / 35 / 35 / 36 / 36`である。
既閲覧validation 6 IDも`1 / 2 / 1 / 1 / 1`へ分散した。

## 6. artifactと再現性

同じコードとsafe train/validation MIDI入力から二回実行し、全出力fileがbyte単位で一致した。
audio、test MIDI、fresh holdout、2MIX、candidate scoreはこのselector runで開いていない。
ただし、これはselectorが記録したrun-local assertionであり、非アクセスを暗号学的に証明するものではない。
formal認可では、到達不能なmountまたはACL、sandbox policy、実行binaryと入力のhashを別receiptで検証し、このbooleanを証拠として使わない。

| artifact | SHA-256 |
|---|---|
| 23列manifest | `80ebe2961ece9833f554f98430e6617aad2496603f0c105c611dfa710938ad8c` |
| 7列fold metadata | `51c0b30d535a2819dd17b0a49e410c378e2d436eb5f98be06caff5757be45675` |
| development receipt v2 | `57fcd65d2b9f265796ba2142f367a89bd932e1cefd73c76f09e96d743963153a` |

manifestはselection rank、selection key、fold、source metadata、MIDI SHA-256、excerpt sample境界、excerpt raw、compound、kick、hat、densityを一行に固定する。
receiptは元duration decimalと整数sampleのbinding hash、excerpt mapping、sampling audit、fold policy、fold audit、artifact hash、未解決blockerを記録する。

B-551で、固定MIDI archiveの同一bufferから全ZIP構造と選択290 memberを検証し、sourceとcropped note/eventのcanonical重複監査を完了した。
MIDI receipt SHA-256は`7c923cf224f8201d0496c304cb160b0cc8859340cdb0b74c7b490b3cd6223447`であり、二回実行でbyte単位に一致した。
B-552で、固定audio archiveの全体SHA-256とZIP64構造を検証し、選択290 WAVのsource、core、maximum-context PCMを固定した。
audio receipt SHA-256は`788faee0f60c6173aac48b9681d98314bcdbb949a0e41d9c78b6a185f3056233`であり、二回実行でbyte単位に一致した。
MIDIとaudio provenanceはformal inputの構成要素として成立したが、両receiptをsource commitへ結ぶFormalAuthorizationは未完成である。

## 7. formal evaluatorへ接続した範囲

formal evaluatorは23列manifestと7列fold metadataを型付きで解析する。
selection key、元decimalからのsource sample、hash-window境界、正確にN=290、五fold同数、split、style、kit、drummer quotaを再計算する。

候補configはDRUM専用のformal schemaを持つ。
Mel 32 v2とfixed SuperFluxだけを許可し、thresholdとpeak幅は事前登録した整数grid外を拒否する。
SuperFluxは44.1 kHz mono LRとし、window 1,024はspectral lag 1、2,048はlag 2へ固定する。

評価result型はpooled micro、五fold、performance macro、kick-only contributor macro、hat-only contributor macro、worst-fold normalized marginを分離する。
P/R/F1 macroは正解eventがあるperformanceだけを対象にし、正解あり予測0は0として残す。
negative-only performanceはP/R/F1 macroから外すが、予測0を含む全performanceをFP/s macroへ入れる。
kick/hat macroは該当class正解があるperformanceを全て数え、TP 0をrecall 0とする。
timingはfoldのmicro gateとし、match 0のfoldを合格にしない。

候補winnerをcallerが作ったbooleanや無名marginから選ぶ旧scaffoldは削除した。
将来のwinnerは、事前に封印した全候補を完走したCandidateSetReceiptと、evaluatorが生成したtyped summaryだけから決める。

## 8. formal採点を停止する理由

現在のformal CLIは`formal_authorization_not_pinned_in_source_commit`で停止する。
停止はresult pathの存在確認を含むfilesystem accessより前に起きる。
同じcallerがauthorizationと期待hashを同時に渡しても、事前登録の時系列証明にはならないためである。

さらに、次のblockerを独立に保持する。

- B-551 MIDI receiptとB-552 audio receiptをsource commitへ結ぶFormalAuthorization semantic verifierが未実装。
- blind auditとcandidate planのverified receiptが未実装。
- source sample 0を起点に実contextを解析し、coreだけを採点するcontext guardが未実装。
- candidate planから必要な左右contextを導出し、B-552 maximum-context証拠へ結ぶ検証が未実装。
- blind acoustic auditがsynthetic fixture以外で未実施。
- sealed candidate setの完走receiptが未実装。
- leave-one-drummer-outとleave-one-session-outの実行resultが未取得。
- fold reportのID集合、fold間disjoint、全290 ID coverage、pooled値のfold完全和、全reportのcandidate identityを結ぶsemantic verifierが未実装。
- candidate score、winner、runtime tie-breakが未取得。

30秒coreだけを独立audioとして解析すると、内部境界を物理source端と誤認してzero paddingする。
formal実装はframe gridをsource sample 0へ固定し、必要な実source contextをdecodeして、predictionとlabelの採点だけを`[core_start, core_end)`へ限定する。
この経路ができるまで、formal loadとscoreは`not_ready_context_guard_unimplemented`で停止する。

## 9. 隔離と2MIX

B-550はofficial metadataとtrain/validation MIDIだけをDRUM development用に読んだ。
audio、test MIDI、fresh holdout、2MIX data、Slakh、annotation、candidate outputは開いていない。
B-552では公式audio archive全体を不透明な圧縮bytesとしてhashし、semantic decodeは固定train 246件とvalidation 44件のWAVと対応MIDIだけに限定した。

2026-08-30のE-GMD test MIDI誤抽出事故は、独立immutable incident recordとしてrepoへhash固定済みである。
original E-GMD testを未開封とは主張しない。
fresh holdoutはguardianが再利用を明示許可するか、新しい未接触holdoutへ置き換えるまで開始しない。

2MIXは同じATTACK画面の別profileという設計だけを共有する。
DRUMのmanifest、threshold、definition hash、gate合格を2MIXへ転用しない。

## 10. 次工程

1. B-551でverified MIDI archive member、sourceとcropped note/event hash、重複検査receiptを完了した。
2. B-552でofficial audio archive、source PCM、core、maximum-context PCM、対応MIDIのreceiptを完了した。
3. 二人のannotatorへ同じ固定audio excerptを渡し、候補出力なしでblind acoustic auditを完了する。
4. selection、fold、MIDI、audio、audit、candidate planをsource commitの固定hashへ結ぶ。
5. source-origin context guardを実装し、formal CLIの入力前blockerを解除する。
6. sealed candidate setを全foldで一度だけ採点し、LODO、LOSO、runtimeを含むwinner receiptを作る。
7. guardian裁定または新holdoutを確定し、その後にだけfresh評価へ進む。

工程6が完了するまでDRUM candidate freezeはNo-Goである。
工程7とPhase 3以降のruntime、macOS、Windows gateが完了するまでATTACKは公開No-Go、OFFを維持する。
