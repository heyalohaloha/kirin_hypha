# FREQ: optional latency未報告hostのPRE／POST比較

2026-10-12追記: 認定済みStudio Pro VST3には、[PDC比較時計の修正](hypha_vst3_pdc_delta_20261012.md)で
別種のcontent時計とLISTENの対応証明を使う。それ以外のhostには以下の規則を維持する。

2026-10-10の表示契約。PREとPOSTが同じ時計の種類を使うときだけ、同runの完成したnative窓を比較する。presentation同士、project timeline同士を許可し、種類の混在とrender-onlyを比較しない。latencyを0と推定したり、欠測を補完したりしない。

## 修正境界

- ingressの実clock definition、stream generationとsnapshot identityは維持する。種類やlatencyが変わると旧frameを退役する。
- runtimeの履歴とclock kindを同じidentityで取得する。mutex競合は公開物の消去理由にしない。
- PREはproject timelineの完成履歴も公開する。private snapshot KHSPEC05のheaderにPresentation=1／ProjectTimeline=2を記録する。長さ・上限は維持し、旧形式と未知tagを拒否する。
- POSTは自分と同じkindのPREだけをexact endpointで結合する。混在したら古いΔの表示leaseも即時に失効させ、絶対POSTを保持する。
- 同run抽出、seek／gap／失効、外部requestの保全、6秒履歴、通常A経路、Recordと既存C ABIの形は維持する。Perceptualの整列条件とDRUM時計は維持する。Absoluteはproject timeline／local renderも受理し、LIVE、POST単体のSharpnessと絶対PSBを表示する。状態6はPerceptualの未整列座標だけに属する。

## 回帰

実workerとPRE／POST exchangeを使って、両側projectのPRE snapshotとActive Δ、両方向の種類混在、同endpointでの再取得、片側だけの完成窓、種類tagの往復・不正tag／旧形式、公開中／clone中のclock cutoverを確認する。既存の数値・窓・期限のoracleは保持する。

同じ変更に含めるReference試験では、pause中のEND後、固定0.3秒sleepを最大5秒のmonotonic deadline待ちへ置換する。inactiveへの復帰とStart／Completed journalの期待値は維持する。

実DAWでの再確認・PR／CI・mergeは担当者の後工程。fixture成功を実host受入や公開readyとしない。FFI試験の本物の保存先への書込み対策は別変更とする。
