# ATTACK Evaluator v2 基盤実装報告

**日付**：2026-08-30
**実装ラベル**：B-548
**対象profile**：`DRUM`の既閲覧診断だけ
**判定**：Evaluator v2基盤完了、候補選定と公開Go判定は未実施
**公開状態**：ATTACK route、request、worker、lease acquisitionは未実装かつOFF

## 1. 今回の境界

B-548は、B-547で決めたPhase 2-Rを再現可能に実行する評価器の基盤である。
今回使った12演奏はB-546後にすでに閲覧済みであり、すべて`opened_development_diagnostic`として一つにまとめた。
元CSVの`validation`と`test`は出典情報として結果へ残すが、候補選定用とholdoutへ分けて解釈しない。
新しいholdoutを選択、読込、採点していない。
`2MIX`の音源または注釈も読込、採点していない。
CLIは`opened-diagnostic`と`DRUM`以外を、dataset pathを解決する前に拒否する。
候補は`diagnostic_only`の一構成を明示入力し、評価器内で探索、winner選定、threshold調整を行わない。

このため、今回の数値は旧Mel 32候補を新契約で診断した結果である。
DRUM候補の採用判定でも、fresh holdoutの代用でもない。

## 2. 固定した入出力契約

入力manifest、candidate config、dataset ID、dataset version、archive SHA表記、source commit、result pathをすべて明示必須にした。
dataset root内へcanonicalizeできない入力、欠損、重複行、重複入力path、不正split、不正CSVを採点前に拒否する。
WAVはmono PCM 16 bitまたは24 bitを解析できるが、B-548診断は44.1 kHzだけを許可する。
RIFF長、chunk長、format、channel、sample rate、block align、byte rate、無音、manifest duration差をpreflightする。
MIDIはformat 0または1、PPQ timebase、tempo map、running status、nonzero note-onを解析し、不正または範囲外のlabelを拒否する。
manifestと各MIDI/WAVは採点前後にSHA-256を再確認し、途中変更を拒否する。

結果JSONは既存fileを上書きせず、同一filesystem内の一時fileから排他的にpublishする。
結果にはmanifest SHA、全performance ID、全入力SHAとformat、candidateのraw/semantic SHA、analyzer layout hash、measurement definition hashを残す。
日時と絶対pathを除いた決定的coreにもSHA-256を付ける。
同じcommit、manifest、candidate、入力、評価定義による二回の実行で、このdigestが一致することを確認した。
結果は常に`publication_eligible: false`であり、診断gateが通っても公開許可へ変換しない。

## 3. Evaluator v2の測定契約

nonzero MIDI note-onをpitchとvelocity付きで保持する。
最初のnote-onから30.000 ms以内のcluster spanを一つのcompound eventとし、直前noteからの連鎖集約は行わない。
event時刻はcluster内note-onの算術平均とする。
kick pitchは36、hat pitchは22、26、42、44、46とする。
予測と正解はinclusive ±25 msで厳密に一対一対応させる。
対応数を最大化し、次に総絶対誤差を最小化し、さらに同値なら早いlabelと予測を優先する。
時刻P50とP95はnearest-rank、signed medianは偶数件で中央二値の平均とする。
分母または対応がない値を0で代用せず、`null`と`not_evaluable`で表す。
macro値は同一performance IDのkit renderを先に集約してから平均し、kit数の多い演奏へ過大な重みを与えない。

固定gateはPrecision 0.85以上、Recall 0.75以上、F1 0.80以上、FP/s 1.0以下とする。
timing absolute P95は15.0 ms以下、signed timing medianの絶対値は5.333 ms以下とする。
DRUMの原因別gateはkick-only recall 0.75以上、hat-only recall 0.50以上とする。
kick-containingとhat-containingは同時打撃による見かけの通過を避けるため診断値だけにする。
matching toleranceと同値になるtiming maxはgateにしない。

## 4. 境界・異常系の確認

単体テスト28件が通過した。
cargo fmtとHypha本体のclippyは通過し、workspace全体のtestも通過した。
clippyが出力したwarningは監査対象外の`vendor`配下に限られ、今回の実装にはなかった。
30 ms compound境界、非連鎖集約、±25 ms matching境界、最大cardinality、等価解の決定順を確認した。
先頭zero padding、local meanのedge padding、30 ms refractory境界を確認した。
kick-only、hat-only、mixed compound、duplicate prediction、merged labelの数え方を確認した。
PCM 16/24 bit、RIFF破損、manifest欠損、path逸脱、重複、tempo競合、running statusを確認した。
fresh holdoutと2MIXを存在しないpathと同時に渡しても、path errorより先にpurpose/profile errorとなることを確認した。
既存result pathを指定した場合も入力へ進まず、上書きを拒否することを確認した。

**B-549追補**：B-548実行時はMIDI秒と予測秒をf64で比較し、末尾supportのflushも行っていなかった。
B-549で正解を整数µs、予測を整数sampleのままi128で±25 ms判定し、EOF supportまでflushする契約へ改訂したため、以下のB-548数値とdigestはhistorical diagnosticとしてのみ保持する。

## 5. 既閲覧12演奏の診断結果

manifest SHA-256は`151e876109722459b7d836525e5cf6e0d2e7fe1c41bc87b23c4ca6faadd6c8c3`である。
対象は12演奏、81.2497秒、raw note-on 523件、30 ms compound event 395件である。
single-note eventは275件、multi-note compoundは120件、30 ms超50 ms以下の隣接pairは12件だった。
旧B-546 Mel 32構成は489 eventを予測し、TP 374、FP 115、FN 21だった。

| 指標 | 実測 | gate | 診断 |
|---|---:|---:|---|
| Precision | 0.7648 | 0.85以上 | fail |
| Recall | 0.9468 | 0.75以上 | pass |
| F1 | 0.8462 | 0.80以上 | pass |
| false positive | 1.4154回/秒 | 1.0回/秒以下 | fail |
| timing absolute P95 | 11.535 ms | 15.0 ms以下 | pass |
| signed timing median absolute | 2.604 ms | 5.333 ms以下 | pass |
| kick-only recall | 0.8462（22/26） | 0.75以上 | pass |
| hat-only recall | 0.7813（25/32） | 0.50以上 | pass |

診断gateはPrecisionとFP/sの二項で未達だった。
F1だけを見ると0.80を超えるが、誤検出密度が公開条件を満たさないため合格とは扱わない。
B-546との差には、対象演奏、30 ms compound化、公式pitch mapping、matching方式の変更が混在する。
そのため、B-546の8 ms labelと±30 ms greedy matchingによる数値に対する検出器の性能向上とは解釈しない。

同じ入力による二回の診断でdeterministic result SHA-256は一致した。
B-548 commit後に再実行した最終digestは`ca78c7e573fa1e0703aa75b36420d5a941ba6722bac9e0c647a74acd29ce16d7`だった。
measurement definition SHA-256は`4f09784cb737a8f3f280358b85e20d2943cd7419e11d6fc74e68acd3ee7e33a9`だった。

## 6. 判定と次工程

Evaluator v2の基盤は次工程へ進める状態である。
旧Mel 32 absolute thresholdは、v2共通peak ruleを持つ採用候補へ昇格させない。
次のB-549では、既閲覧DRUM development manifestとperformance/drummer/kit単位のgrouped foldを決定的に固定する。
その上でfixed-scale SuperFlux-styleと共通causal local-mean peak pickerを実装し、事前登録した小さな段階的gridだけを比較する。
fresh DRUM holdoutは候補、parameter、definition hash、runtime成立性を固定した後に一度だけ開く。
2MIXはDRUMの数値を流用せず、一般mix向けの可聴attack注釈と独立development/holdoutを準備してから別判定する。
いずれか一profileだけが全gateを通る場合は、そのprofileだけを公開対象にできる。
現時点では両profileとも公開ATTACKはOFFを維持し、Phase 3 workerへ進まない。
