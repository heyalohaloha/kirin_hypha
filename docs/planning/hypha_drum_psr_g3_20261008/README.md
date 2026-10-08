# DRUM・PSR G3 — 最大DPIと実DAWの確認

2026年10月8日。利用者は、最大DPIのFREQ描画と実DAWでの負荷・操作・品位の残件を完了するよう指示し、DAW操作をCodexへ委任した。CIの確認、merge、正式releaseは従来どおりClaude担当とする。友人確認は公開後G4だけである。

## 候補の修正と検証

FREQの地形は、span内部の完全に覆われた画素へ同じblend weightを毎画素で計算していた。重みをspanごとに一回計算し、端のfractional coverageと描く順番を保持した。半透明の重なり、履歴、時刻、geometry、提示周期、解析・音声は変えていない。

比較oracleは修正前の製品sourceを別namespaceでcompileした。native／software、幅400／600／900、DPI 1／1.25／1.5／2／4、fractional／inherited origin、低値・高値・密集・欠損・細かい峰の300条件で、全画素の最大channel差は0だった。

| 地形描画900／DPI 2・120回 | 修正前CPU／回 | 修正後CPU／回 |
| --- | --- | --- |
| software | 10.725 ms | 9.587 ms |
| native | 14.643 ms | 13.595 ms |

これは地形painterとcanvas clearを含む単体計算で、DAW全体のCPUや強制FREQ全体の削減率ではない。ガラスの合成候補は画素検査を通ったが、同一画面の交互測定で一部のmodeにCPU増加が残ったため採用しなかった。外枠の4分割clip、cropped strip、全frame compositeの画素不適合は保存し、採用しない。

CompactのPOST見出しは、描画時のpaddingを含む必要幅を確保する。文字、役割、画面配置と色は保持する。

## 実DAWの手順と保存の境界

診断candidateは同じsourceを統合入口で未署名buildし、manifest、実binary SHA、sourceとJUCE patchを照合する。Macは独立したREAPER設定とcustom VST path、Windowsは独立sourceと使い捨てStudio Pro projectで確認する。既存のdirty checkoutをbuildに使わない。

Macは実読したbuilt-in入出力の48 kHz／512 samplesを専用設定へ指定し、fixture trackは録音待機させない。音源は合成WAVだけとし、masterの出力をplugin chainの後で0にする。通常のaudio device、Merging経路、既存projectを変更しない。Windowsの配置が必要な場合は旧bundleを全byte保全し、実moduleのSHAと候補を照合してから検証し、終了後に元の全SHAを復元する。

Watch／Recordのfixture保存先と、GUI preference・VU calibrationの保存先を同じものとして扱わない。実GUI preferenceは最初のbyte／SHAを保全し、明示操作で変更したfileだけを終了時に復元する。別操作によるhash driftがあれば上書きを止める。新fixtureのcalibrationも所有scopeを特定してから片付ける。

実UIはpublic AX／Core Graphics、Windowsのpublic UIAutomation／SendInputから操作し、その時のtree・画面で確認した対象だけを押す。host process CPU／wall／RSS、表示・非表示・bypass、実callback・解析到着、xrunの取得可否を記録する。強制30 Hz描画を出荷FREQの12 Hz曲線／2 Hz数値と混ぜない。

## 受入の記録

現在は修正候補と実DAWの操作・保全手順を準備した段階である。実DAW結果、exact候補CI、本人による品位承認は、実施した結果だけを追記する。native fixtureを実DAWや本人の主観評価のPASSへ繰り上げない。未署名VST3診断は、retail Pro ToolsのPACE署名済みAAXや全platformの正式release gateを代用しない。

raw source・command・log・機器情報・設定backupは非公開の検証領域へ保存し、公開するreceiptは件数・数値・basenameとSHAに絞る。
