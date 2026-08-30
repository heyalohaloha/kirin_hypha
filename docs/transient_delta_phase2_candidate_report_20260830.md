# ATTACK Phase 2 オフライン候補評価

**日付**：2026-08-30
**実装ラベル**：B-546
**判定**：No-Go
**公開状態**：ATTACK route、request、worker、lease acquisition は未実装かつ OFF

## 判定

Phase 2 の独立 holdout は、事前に固定した八つの合格値のうち五つを満たさなかった。
したがって、`docs/transient_delta_implementation_plan.md` の第19節に従い、Phase 3 の worker 実装へ進まない。
公開 navigation、公開単位、表示 full-scale も固定しない。

この判定は「ATTACKの構想が不要」という意味ではない。
現候補の検出器では、実演奏に対する同一イベントの観察精度を公開機能の根拠にできないという判定である。

## 評価境界

評価には E-GMD v1.0.0 の公式CSV、MIDI onset annotation、対応WAVを使った。
データは公式アーカイブから必要な範囲だけをローカル一時領域へ取得し、リポジトリへ追加していない。
評価対象のmetadataだけを `transient_egmd_selection_v1.csv` に固定し、音声rootに `selection.csv` がない場合も評価器が同じ18演奏を選ぶようにした。
E-GMDのライセンスはCC BY 4.0であり、帰属先は公式データセットページに示す。

候補選定用の validation は6演奏、42.370秒である。
最終 holdout は選定時に未閲覧だった12演奏、503.261秒であり、4人のドラマー、10種のグルーヴ、4種のキットを含む。
validation と holdout の performance ID は重複しない。

ローカルで確認したWAVは44.1 kHz、mono、PCM 16 bitであった。
評価器はこの保存形式以外を受け付けず、欠損、破損、空音声、空MIDIをエラーにする。

MIDI note-on は8 ms以内を一つの観察イベントへまとめた。
予測と注釈は30 ms以内で一対一対応させ、誤差の小さい組を先に確定した。
この注釈集約は現行評価器の固定条件であり、商品側の dense-event ambiguity 契約を代替しない。

## 固定した合格値

独立 holdout を開く前に、次の最低値を固定した。

| 指標 | 合格値 |
|---|---:|
| Precision | 0.85以上 |
| Recall | 0.75以上 |
| F1 | 0.80以上 |
| timing error P95 | 15.0 ms以下 |
| timing error max | 30.0 ms以下 |
| false positive | 1.0回/秒以下 |
| kick recall | 0.75以上 |
| hat recall | 0.50以上 |

## 候補選定

全候補は同じ窓長、hop、素材、threshold sweep、局所最大条件、refractory sweepで比較した。
窓長は48 kHz換算1,024 sample、hopは256 sampleであり、44.1 kHzでは941 sampleと235 sampleになる。
session最大値、running maximum、percentile正規化、auto scaleは使っていない。

| 候補 | threshold | radius | refractory | Precision | Recall | F1 | P95 | FP/s | Kick | Hat | Gate |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| Mel 32 | 1.44459105 | 2 | 20 ms | 0.8553 | 0.7821 | 0.8171 | 7.542 ms | 0.802 | 0.8039 | 0.5714 | pass |
| Mel 40 | 2.96714520 | 1 | 20 ms | 0.9529 | 0.7082 | 0.8125 | 7.741 ms | 0.212 | 0.6471 | 0.4286 | fail |
| Complex | 0.00162888 | 3 | 65 ms | 0.9015 | 0.7121 | 0.7957 | 23.479 ms | 0.472 | 0.8235 | 0.1786 | fail |
| Hybrid | 2.87830544 | 3 | 65 ms | 0.9490 | 0.7237 | 0.8212 | 7.542 ms | 0.236 | 0.7451 | 0.3571 | fail |

validation の全合格値を満たした候補はMel 32だけであった。
このため、Mel 32と上表の規則を変更せずに独立 holdoutを一度だけ評価した。

## 独立 holdout

| 指標 | 実測 | 合格値 | 結果 |
|---|---:|---:|---|
| Precision | 0.7334 | 0.85以上 | fail |
| Recall | 0.6338 | 0.75以上 | fail |
| F1 | 0.6800 | 0.80以上 | fail |
| timing error mean | 5.717 ms | 参考値 | pass |
| timing error P95 | 12.521 ms | 15.0 ms以下 | pass |
| timing error max | 29.390 ms | 30.0 ms以下 | pass |
| false positive | 1.353回/秒 | 1.0回/秒以下 | fail |
| kick recall | 0.6565 | 0.75以上 | fail |
| hat recall | 0.5786 | 0.50以上 | pass |

対応数はtrue positive 1,873、false positive 681、false negative 1,082であった。
時刻精度は合格したが、イベントの採択精度と再現率は合格していない。

## 失敗後の診断

一度開いたholdoutは以後の最終判定へ再利用できないため、18演奏を開発集合へ移して全候補を再評価した。
この診断ではMel 32、Mel 40、Complex、HybridのF1が順に0.6754、0.6750、0.5975、0.6825となり、合格候補はなかった。
したがって、新しいholdoutを取得して評価を繰り返していない。

現行の広帯域ODFと固定peak pickingだけでは、キット、演奏密度、奏者が変わったときのprecisionとrecallを同時に維持できなかった。
thresholdをholdoutへ合わせれば数値を動かせるが、その操作は独立評価を失効させるため行っていない。

## 未解決の測定契約

E-GMDのmix音源には「ハイハットに起因する二重検出」という原因ラベルがない。
そのため、現評価器のhat recallは測定できるが、計画が要求するhat false-positiveを直接測定できない。
isolated hatと同時kick-hatの再配布可能な合成fixtureを追加し、二重検出とdense ambiguityを別々に数える必要がある。

候補がholdoutを通っていないため、Onset Fluxの公開単位、表示full-scale、common decision trace、PRE-onlyとPOST-onlyの許容幅、gap期限、offline bounce容量も未確定のままである。
これらを推測で固定しない。

## 実装への影響

追加したRust moduleはPhase 2の候補式と決定的host-rate layoutだけを提供する。
既存runtime、FFI、JUCE、Audio Threadからは呼ばれず、ATTACKの公開経路を作らない。

再開する場合は、現行四候補のthreshold再調整から始めない。
注釈の観察イベント定義をproduct refractoryと整合させ、再配布可能な単発、hat、roll、同時打撃fixtureで原因別の誤検出を測定できる別方式を設計する。
その方式が開発集合の全合格値を満たした後に限り、新しい未閲覧holdoutを固定する。

## 参照資料

- [E-GMD公式データセットページ](https://magenta.withgoogle.com/datasets/e-gmd)
- [Essentia OnsetDetectionリファレンス](https://essentia.upf.edu/reference/std_OnsetDetection.html)
- [librosa 0.11 onset strengthリファレンス](https://librosa.org/doc/0.11.0/generated/librosa.onset.onset_strength.html)
