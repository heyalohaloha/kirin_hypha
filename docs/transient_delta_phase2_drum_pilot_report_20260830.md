# ATTACK DRUM development pilot報告

**日付**：2026-08-30
**実装ラベル**：B-553
**対象profile**：DRUM
**方式判定**：SuperFluxを継続
**候補freeze判定**：No-Go
**公開判定**：No-Go、ATTACKはOFF

## 1. 結論

B-553で、固定した290演奏へSuperFluxとMel32を実際に適用し、30秒coreの合計51.70分を採点した。
SuperFluxの最良pilotはPrecision 0.872、Recall 0.759、F1 0.811となり、三つの主要比率gateを初めて同時に通過した。

旧Mel32 controlはPrecision 0.778、F1 0.781、FP 1.315件/秒であり、SuperFluxより明確に悪かった。
ATTACK DRUMの主線をSuperFluxへ固定し、Mel32の追加調整を停止する。

ただし、kick-only Recallは0.665、timing P95は19.14 msで未達だった。
五foldのうちfold 0もPrecision、Recall、F1で未達である。
したがって方式の見通しは肯定するが、候補freezeと本体接続はまだ行わない。

## 2. ATTACKへ直結する測定契約

各WAVはsource sample 0をframe gridの起点として全sourceを解析した。
予測とMIDI labelの計数だけをmanifestの半開core区間へ限定した。

これにより、30秒coreの両端を物理的な音声端としてzero paddingする誤差を避けた。
MIDIはcoreへ入るraw note-onだけを選んだ後、30 msの非連鎖compound eventを再構成した。

matchingは±25 ms inclusiveの最大一対一対応である。
同じ予測または正解を複数回TPへ使わない。

このpilotはdevelopment上の方式選定である。
winner、fresh holdout、公開ATTACKを認可しない。

## 3. 比較結果

| candidate | Precision | Recall | F1 | FP/s | 判定 |
|---|---:|---:|---:|---:|---|
| Mel32 v2 control | 0.778 | 0.784 | 0.781 | 1.315 | 停止 |
| SuperFlux anchor、24 bpo、radius 1 | 0.888 | 0.710 | 0.789 | 0.525 | 改良対象 |
| SuperFlux pilot best、12 bpo、radius 0 | 0.872 | 0.759 | 0.811 | 0.654 | 継続 |

最良pilotのparameterは2,048 sample reference window、12 bands/octave、frequency maximum radius 0、reference -50 dBFSである。
peak pickerはdelta 0.00625、absolute floor 0、pre-max 3 hop、pre-average 24 hop、future lookahead 0、refractory 30 msである。

小さな比較ではwindow 1,024と2,048、12と24 bands/octave、radius 0と1、reference -50と-60 dBFS、pre-average 19と24 hopだけを見た。
reference -60 dBFSはPrecision 0.794、FP 1.210件/秒まで悪化したため不採用とした。
探索をML、adaptive threshold、session normalization、別ODFへ広げない。

## 4. 最良pilotのgate

| gate | target | actual | 判定 |
|---|---:|---:|---|
| Precision | 0.85以上 | 0.872 | pass |
| Recall | 0.75以上 | 0.759 | pass |
| F1 | 0.80以上 | 0.811 | pass |
| FP/s | 1.00以下 | 0.654 | pass |
| signed timing median absolute | 5.333 ms以下 | 1.318 ms | pass |
| kick-only Recall | 0.75以上 | 0.665 | fail |
| hat-only Recall | 0.50以上 | 0.663 | pass |
| timing P95 | 15.0 ms以下 | 19.141 ms | fail |

performance-ID macroはPrecision 0.871、Recall 0.821、F1 0.841、FP 0.716件/秒だった。
pooled値だけでfold差を隠さない。

## 5. fold別結果

| fold | Precision | Recall | F1 | kick-only | hat-only | timing P95 ms |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | 0.832 | 0.721 | 0.772 | 0.564 | 0.665 | 17.97 |
| 1 | 0.886 | 0.767 | 0.822 | 0.704 | 0.597 | 19.19 |
| 2 | 0.870 | 0.762 | 0.812 | 0.691 | 0.684 | 16.89 |
| 3 | 0.873 | 0.739 | 0.801 | 0.693 | 0.603 | 21.73 |
| 4 | 0.901 | 0.806 | 0.851 | 0.678 | 0.757 | 17.47 |

fold 0は全体より弱く、thresholdを全体平均へ合わせるだけでは解決しない。
kick-only不足は全foldに共通しているため、データの一foldだけに由来する現象でもない。

## 6. 次に厳密に見る箇所

### B-554限定追試

30–200 Hzの低域SuperFluxを独立にpeak判定し、既存all-band eventから30 msを超えて離れたeventだけを補う案を同じ290演奏へ一度適用した。

| candidate | Precision | Recall | F1 | FP/s | kick-only | 判定 |
|---|---:|---:|---:|---:|---:|---|
| SuperFlux pilot best | 0.872 | 0.759 | 0.811 | 0.654 | 0.665 | 継続 |
| 30–200 Hz独立kick assist | 0.582 | 0.831 | 0.684 | 3.510 | 0.832 | 棄却 |

kickは上がったが、誤検出が5倍を超えた。
停止条件に従ってparameter調整を追加せず、補助経路をコードから除いた。

kick-only Recallをdrummer別に集計すると、drummer 4が0.397、drummer 5が0.212、drummer 7が0.641であり、他の6人は0.79以上だった。
この三群だけでkick-only miss 450件中360件、80%を占めた。
問題は全素材に均一ではない。
次はこの三群のmissへ対象を限定し、独立検出器を増やさず既存共通trace内で成立する条件だけを見る。

### B-555 event診断

kick-only 1,342件をB-553候補のevent単位で再診断した。
miss 450件のうち、233件はMIDI時刻の±50 ms内にthresholdとlocal maximumを通るpeakがなかった。
182件は25–50 msにeligible peakがあったが、148件はMIDIより前であり、時刻差の中央値は−32.61 msだった。
refractory抑制は26件、一対一競合は9件だけで、合わせてもmissの7.8%である。

matched kickのMIDI velocity中央値は51、音声attack rise中央値は+14.99 dBだった。
missはvelocity 20、attack rise −1.76 dBだった。
低域補助が誤検出を増やした事実と合わせると、MIDI上の全kickを可聴attack正解としてRecall gateへ置く契約が過大である可能性が高い。
音響監査前にdetectorを追加調整しない。
event診断は二回の実行で589,421 bytes、SHA-256 `b0214bf3938d0087c7b07b9bbe8d31efd9bd48fe25907a96646ede700dd5ae8b`へbyte一致した。

### B-556 candidate-blind聴取パック

drummer 4、5、7について、matched、25–50 ms時刻差、±50 ms内peakなしを各5件ずつ、合計45件へ固定した。
各clipは500 ms、MIDI参照位置は200 ms、元音量を保持し、class、performance、candidate出力を聴取側へ含めない。
WAVは全件44.1 kHz mono PCM16、44,144 bytesである。
二回の生成はpackと別置きkeyの双方でbyte一致した。
pack definition SHA-256は`c2750847b09f340bb5ddac2d0efabf76fc649d2d2f126b63c774bb06debde5f3`、manifestは`d12cbafab016f78deb803447fb4a02c3535191c7c14fceccdb93d60ccaca6b13`である。
聴取完了までkeyを開かず、150–250 ms内の明瞭なattack有無、最寄り時刻、確信度だけを記録する。

### B-557 review HTML

参考ABX画面と同じく、再生系5項目、進捗、自動保存、次の未完了、途中TSV、完了TSVを一画面へ実装した。
HTMLにはopaque clip IDだけを含み、drummer、performance、matched/miss class、candidate値を含めない。
「ある」は150–250 msのattack位置入力を必須とし、別clipの再生開始時は既存再生を停止する。
生成HTMLのSHA-256は`5fbd5abce332b7791747d7f27e9264e57dc0b2a178b397540c932c1ecfeea849`である。

### B-558 kick判定の明確化

最初のclipで先行するスネアが判定対象に見える問題を修正し、質問を「150–250 ms内に低いkickが明瞭にあるか」へ限定した。
各clipに位置確認用波形、MIDI kickの200 ms線、150–250 ms帯、150–300 ms集中再生を追加した。スネア単独は`kickなし`とする。
45本の500 ms音源と45本の150 ms集中音源を二回生成し、pack、HTML、別置きkeyの全てがbyte一致した。
pack definition SHA-256は`0812ff567577d851da392bfdfab9eed057ed8945f1a9f3785a752c419423f5ab`、manifestは`d8a4a0947acb8af930650c63f44fd69058b7c414fc6b5c8da548b3a5d03a8e3a`、HTMLは`c77e9eb1fbeb6ec110cd11b5992f30b101dfa62fe8eb14aa6b0b1e5660d6fedb`である。

### B-559 完了判定の修正

`kickあり`でも最寄り位置が数値でなければ未完了になる問題を修正した。
完了条件をkick有無と確信度の二項目へ限定し、最寄りkick位置は任意とした。回答保存キーはB-558と同一のため、再読込後も既存入力を引き継ぐ。
実ブラウザで`kickあり`、確信度5、位置欄`150–300 ms`の状態を作り、`1 / 45 完了`になることと再読込後の維持を確認した。
二回生成はbyte一致し、HTML SHA-256は`9de54162adc8c9e0acce6dcae769c70a7eadd7032b7fd6a00fa4ca6df543240e`である。

### B-560 kick聴取結果

45件の回答TSVは行数、連番、opaque ID、選択値、確信度、再生環境が全て整合した。
TSV SHA-256は`392df1d01a63a1832f7836c4401a866933153f4a5b43fb6672ccef27ce9458f9`、別置きkeyは`582d4003d02603063a75a349030e2d78f5cc900ee76681b93c9d74d0f6716598`である。個別回答と再生環境はrepositoryへ追加しない。

| detector診断層 | 件数 | kickあり | kickなし | 区別困難 |
|---|---:|---:|---:|---:|
| matched | 15 | 11 | 2 | 2 |
| 25–50 msにeligible peak | 15 | 10 | 5 | 0 |
| ±50 msにeligible peakなし | 15 | 8 | 7 | 0 |

kickあり29件のうち7件は回答上の位置が0–150 msまたは300 ms以後であり、MIDI参照位置の正解とは扱わない。
残る指定位置付近または位置未確定の22件は、detector matched 11件、未検出11件へ分かれた。
matched 11件と未検出11件の中央値は、MIDI velocityが48対43、peakが-28.49対-33.65 dBFS、attack riseが+16.26対-0.88 dB、最寄りselected event誤差が4.42対47.77 msだった。

この45件はdrummerと診断層を各5件へ揃えた原因調査用標本なので、11/22をkick Recallとして母集団へ外挿しない。
raw MIDI kick-only Recall 0.665には、非可聴label、時刻不一致、検出器の実missが混在するため、そのまま公開GoまたはNo-Goの判定値にしない。
次工程は指定位置付近の実miss 11件だけを対象に低域時系列とfull-band ODFを比較し、全体PrecisionとFP/sを悪化させない一つの修正が成立するか確認する。

timing P95はMIDI note-onと実際の可聴attack位置の差を含む。
固定audioを候補出力なしで注釈し、検出器の時刻誤差とMIDI proxyの誤差を分離する。
注釈結果を見る前にglobal offsetを調整しない。

これら二点以外のprovenanceや認可構造を追加してもATTACK精度は上がらないため、次工程の主作業にしない。

### B-561 確認済みkick missの帯域診断

B-558の固定500 ms clipへB-553と同じSuperFluxを適用し、各bandのhalf-wave fluxを30–200 Hz、200 Hz–2 kHz、2–17 kHzへ分けた。
閾値探索やcandidate変更は行っていない。

MIDI位置±25 msに低域eligible peakがあったのは、可聴target miss 11件中5件、kickなし14件中10件、target外kick 7件中5件だった。
全帯域peakの25–75 ms後だけへ限定しても、可聴target miss 5件に対してkickなし4件、target外kick 3件が残った。
低域peak中央値は可聴target miss 0.205、kickなし0.143だったが分布が大きく重なり、Precisionを保つ分離条件にはならない。

したがって低域補助経路は追加せず棄却を維持する。
残る直接課題は、既存eligible peakがMIDI位置から31–41 msずれる6件を含む11件の可聴attack時刻を確定し、検出時刻の誤りかMIDI proxyのずれかを分けることである。
診断artifactは45件でSHA-256 `b50c6fb62edd97bb7c9efbefef94f6b2c538d0c078033d3141063dd5c528a07a`、repository外へ保存した。

### B-562 可聴kickの開始時刻確認

B-560で指定位置付近のkickあり、かつB-553で未検出だった11件だけをopaque IDの時刻確認packへ固定した。
画面の問いは「低いkickが始まった瞬間を波形上でクリック」の一つに限定し、先行するスネアとハットを選ばないことを明示した。
クリック位置は青線で表示し、100–300 ms外を受け付けず、確信度と合わせて自動保存する。
候補status、演奏者、performance、miss classはHTMLへ含めていない。

11 clipを二回生成してbyte一致を確認した。
HTML SHA-256は`c46d62497e2a6519bed7281b8cae0e5d5d1c39ccaaddd40d4e4868c099b49c4a`、manifest SHA-256は`e4f951624b73021abdef5dfdc94e62b418ca4ad0038bf785f0dcf89dda5a3537`である。
実ブラウザで波形クリックによる180.3 ms記録、青線、確信度入力後の`1 / 11 完了`、再読込後の復元を確認した。

B-562は波形の形から可聴attack時刻を判断させる設計だった。
しかし、波形上の変化と低いkickが知覚上始まる位置は一致するとは限らないため、Daisukeの確認後にB-562を棄却した。
B-562のHTMLと回答は以後のATTACK評価に使用しない。

### B-563 聴覚ガイドによるkick開始時刻確認

B-563は波形表示を削除し、元clipへ短い高音のガイド音を加えた音源を耳で合わせる方式へ置き換えた。
ガイドは6 kHz、4 ms、左channelだけに加え、元clipは左右へ同じ信号を保った。
回答者は100–300 msを10 ms刻みで動かし、低いkickが始まった瞬間とガイドが重なる位置を決定する。

11 clipに対して21位置ずつ、計231個のガイド付き音源を生成した。
ガイド付き音源は44.1 kHz stereo PCM16、500 msである。
二回の生成はpack全体でbyte一致した。
HTML SHA-256は`2136f10f32eaf44aff1788d08992e77d6dff6e7290e0d07409d0c696299ab4c0`、manifest SHA-256は`e4f951624b73021abdef5dfdc94e62b418ca4ad0038bf785f0dcf89dda5a3537`、guide manifest SHA-256は`7660bf2d76579cf4d3f9b21c8d336415e71829a5dfa296e19cfabd6003c12483`である。

実ブラウザでガイド位置を180 msへ変更し、明示的な決定と確信度5の入力後に`1 / 11 完了`となることを確認した。
再読込後もガイド位置、決定位置、確信度が復元された。
この回答を用いて可聴attack時刻を固定し、その時刻の直前約100 msと直後20–30 msの音量差と帯域別音色差を比較する。

B-563はガイド音と時刻調整の意味が回答者へ伝わらず、Daisukeの確認後に棄却した。
B-563のHTMLと回答もATTACK評価に使用しない。

### B-564 集中区間のkick有無確認

B-564は問いを「150–300 msの区間で低いkickが鳴っているか」の一つへ限定した。
各trialは150–300 msの集中音源と「キックあり」「キックなし」「わからない」の三択だけを表示する。
時刻、確信度、波形、ガイド音、再生環境入力は含めない。

11 clipを二回生成し、pack全体のbyte一致を確認した。
HTML SHA-256は`f2b63be26a170cf4c6934d588c49723380b49ee23a9974311624d22aadc8b90d`、manifest SHA-256は`c7f2a708fc9fd951448e299a4238b8b82d75c7ffc700b9350cd6f264cba7e4cf`である。
実ブラウザで「キックあり」の選択後に`1 / 11 完了`となり、再読込後も回答が復元された。

## 7. 再現性

最良pilotは二回実行でbyte単位に一致した。
resultは各865,922 bytesで、SHA-256は`8134e78c49d27d93ad7c0469688eeccdd34ebd6ef4660e2575ccdbe0f2a0b617`である。
result内の決定的評価digestは`5d9dd753d9b345cd17f11dac18473c9749bc1fe2e2306f40098a89c96fc12a0f`である。

実resultはローカル`/private/tmp`に置き、音声、MIDI、個別track値をrepositoryへ追加しない。
candidate configと評価コードだけをcommit対象とする。

## 8. 現在の判断

DRUM ATTACKは完成可能性がある。
主要比率がdevelopmentで同時通過したため、方式選定の中心課題は解消した。

一方、kick-onlyとtiming、worst-foldが未達なので、完成したとは扱わない。
kick miss条件の特定と音響時刻監査を通すまでATTACK route、worker、UIはOFFを維持する。

2MIXは別profileとして未着手であり、この結果を転用しない。

## 9. 参照

- `docs/transient_delta_phase2_recovery_plan_20260830.md`
- `docs/transient_delta_phase2_audio_provenance_report_20260830.md`
- `docs/transient_delta_phase2_formal_development_gate_report_20260830.md`
- [SuperFlux](https://phenicx.upf.edu/system/files/publications/Boeck_DAFx-13.pdf)
