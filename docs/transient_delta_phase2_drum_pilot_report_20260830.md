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

timing P95はMIDI note-onと実際の可聴attack位置の差を含む。
固定audioを候補出力なしで注釈し、検出器の時刻誤差とMIDI proxyの誤差を分離する。
注釈結果を見る前にglobal offsetを調整しない。

これら二点以外のprovenanceや認可構造を追加してもATTACK精度は上がらないため、次工程の主作業にしない。

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
