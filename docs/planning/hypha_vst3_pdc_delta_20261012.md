# VST3 の PDC と FREQ / SHARP / PSB の比較時計

## 座標の不一致と検証根拠

従来の `optional_analysis_clock::spectrum_presentation_start` は、project position に
output presentation latency を加えた座標を解析へ渡す。`PluginProcessorHostClock.cpp` は
JUCE wrapper が受けた数値をそのまま読む。VST3 wrapper の tracked patch は main bus の
`IAudioPresentationLatency` 通知を atomic に保存し、入力・出力を取り違えていない。
`spectrum_exchange_post_join.rs` と `spectrum_exchange_join.rs` は同じ種類の時計と
完全一致する窓の終点を要求するが、加算後の座標が同じ音を指すことまでは証明していなかった。

[Steinberg の IAudioPresentationLatency](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IAudioPresentationLatency.html)
は出力から提示までの遅れを通知する任意interfaceで、挿入が遅延を持つと、その前段の出力通知と
後段の入力通知が変わる。0 は遅延なしと不明の両方を意味する。
[ProcessContext](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ProcessContext.html)
の projectTimeSamples は project 時刻、continousTimeSamples は loop しない任意の時刻である。
これだけで「補償済みproject + output latency」が全host共通の音声対応座標になるとは言えない。
loop 境界でcallbackを分割することもhostの必須動作ではない。

同じ原音のsample `x` に着目すると、補償済みprojectが両側で `x`、出力通知がPREでは
`L + 4096`、POSTでは `L` の場合、旧座標はそれぞれ `x + L + 4096` と `x + L` になる。
同じ終点をjoinすると、4096 samples離れた音を比べてしまう。時間変化する合成PCMと
実FFI・解析worker・file exchangeを使う対照試験で、この条件による偽差を確認している。
単一の定常正弦波だけではこの不具合を検出できない。

Studio系の認定は、VST3連続時計が遅延後のcontent位置を指すことを前提とする。
遅延・loop fixtureと実hostの照合が根拠であり、適用範囲はStudio One 6〜7／Studio Pro 8。
実測版と版の適用方針はREADMEに区別して記す。
対照試験は二重計上という条件を確認するが、個々のhostでその条件が成立するかは、
同一callbackの両roleの通知値とPCMを照合するまで推定に留まる。
LISTENでPREを選択できることだけを音声一致の証明とはしない。

## 比較時計の設計

presentation latencyの差だけでは、独立した挿入遅延の正解値がなく、project側の
補償との二重計上を区別できない。LISTEN用 `TimingPreparation` の同じcallback・
同じPRE snapshotの証明を使う。RTで追加のPCM照合や別の遅延推定器は動かさない。

- 既存classifierが `certifiedContent` を返すVST3だけで、FREQとPerceptualを時計観測後へ
  遅延投入する。AAXのPerceptual用にあった原音scratchと一度だけのFFI transactionを共用する。
- PREは認定済みの連続content時計、POSTは `POST.continuous - K` を使う。
  このhostの認定は同じcontentの座標が共通という意味なので、受理する **Kは0**。
  物理的なchain遅延4096 samplesと、座標原点間のKは別の量である。相反する線形Kは拒否する。
- pairにはPREのowner/generation、現在のPOST callback、時計種別・認定、連続性、
  現在のloop周回と既存observerの証明を要求する。時計欠損、seek、pair失効、bypass、offline、
  他の試聴による出力占有、内容変化による既存holdではΔを出さず、新しい対応を待つ。
- 解析時計を `Vst3Content` として私的snapshotにも記録し、Presentation、ProjectTimeline、
  AaxEngineと混ぜない。proof revisionとstream generationの切替で古い表示leaseを失効する。
  FREQはpairを証明できない場合も、joinできない局所座標でPOST絶対曲線を保持できる。
- AAXの線形／上限付きK、AUのrender−K、REAPERの補償済みproject認定、未認定VST3は
  それぞれ別の時計規則に従う。
  LIVE・絶対PSB、ATTACK、Watch、Recordの時計は変更しない。
  音声出力・latency・公開Record schema・保存stateを変更せず、RTのalloc/lock/I/Oを増やさない。

## 再現試験

`content_analysis_ffi_tests.rs` は4096 samplesのPCM遅延、遅延通知なし、明示0、
遅延を含む通知、遅延0、対応なし、対応消失と復帰を実worker・file exchangeで確認する。
旧計算式を使う対照試験は偽差が実際に出ることも要求する。PSBはshareの差で検査し、
画面のpp表記へ変換する前の値を使う。投入は実時間以下にし、解析キューのoverflowを避ける。

`analysis_clock_content_test.cpp` は既存の `FixedValidationDelay` を使い、通知の有無・
遅延包含の有無・project補償の有無を組み合わせる。認定と矛盾するKは拒否し、受理した時計が
実遅延PCMのsampleを指すことを確認する。初回loop、折返しのclampも検査する。

`analysis_product_content_test.cpp` は実共通processor、mono/stereo、4種類の遅延通知、
FREQ/SHARP/PSB、linear/loop、時計欠損・復帰、通常Aのbit identityと0 latencyを検査する。
実PRE timing shared memoryが必須であり、作成不可の環境で代用品を使ってPASSにしない。

```sh
cmake --build target/hypha-release-native-tests --config Release --target \
  KirinContentAnalysisClockTests KirinContentAnalysisProductTests \
  KirinAaxAnalysisClockTests KirinAaxAnalysisProductTests
ctest --test-dir target/hypha-release-native-tests -C Release --output-on-failure \
  -R '^kirin_(content_analysis_|aax_analysis_|local_blind_pdc_validation_delay|live_compare_(timing|correspondence|loop))'
```

## 実hostでの受入と残る危険

1. 同一作業差分のPRE/POSTを検証用として使い、48 kHz stereoで、PRE → 遅延4096 → POSTを
   pairする。FREQ、SHARP、差分PSBを順に開き、準備後にΔがほぼ0で継続することを確認する。
   LISTEN開始を解析の前提にせず、原音のまま実施する。
2. 遅延を外した場合も確認する。loopを最初からON、再生途中でON/OFF、折返しを複数回、
   stop/play、seek、PREの削除・再挿入、保存・再openを試す。未対応区間で古いΔを出さず、
   再準備後に戻ること、pairなしでもPOST絶対FREQとLIVE・絶対PSBが更新することを見る。
3. Logic AUでも同じ検証を行う。render−Kの対応が成立し、遅延0/ありの結果が妥当で
   あることを確認する。AUで利用できる、音を変えず遅延を正しく報告する検証用挿入を使う。
4. 原因のhost側分類には、検証用Debug buildの両roleの既存診断から、project position、
   continuous time、input/output presentation latency、rate/blockを控える。別時刻の画面を
   同一callbackと見なさず、通知なしと明示0を分ける。通常ReleaseはPRE probeを常時更新しない。
5. LISTEN/BLINDの開始・PRE選択・終了も従来どおり確認する。この修正のfixture合格は、
   Studio Pro・Logic・REAPER・Pro Toolsの同一sourceに対する実host受入を代用しない。

認定外host/versionへこの修正を一般化していない。未通知のPDC設定変更や、実PCMと認定時計の
意味が食い違うhost状態を、時計証明だけで完全には検出できない。既存のcontent-jump監視は
比較開始前から毎callbackのPCM同一性を保証するものではない。実機の通知値と同じ音の対応の
再確認を受入条件とし、fixture合格を新しいhost認定や公開完了と扱わない。
