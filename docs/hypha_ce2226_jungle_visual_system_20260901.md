# Hypha CE2226 Visual System

Status: implemented development baseline

Date: 2026-09-01

Branch: `codex/hypha-meter`

Baseline: `a29f50c5cf38fe29fad2bedc7db690c492c2f471`

## 1. 世界設定の接続

Kirin OSの通常面はCE2026の道具であり、Jungleは同じ世界がCE2226の生態系まで進んだ状態である。

HyphaはCE2226の菌糸先端がDAW内へ露出した計測器として扱う。

そのため、HyphaはJungleの植物や生物を小さく再現する画面ではない。

Hyphaの画面だけを見た段階では地下の観測設備として読め、Jungleを開いた段階で同じ素材と光の規則を持つ世界だと分かる関係を作る。

この関係を**地下観測所**と呼ぶ。

## 2. 視覚層

地下観測所は四つの視覚層で構成する。

- **構造層**：外周、panel境界、計測領域を支える静的な有機鉱物構造である。

- **観測層**：実測値が成立した場合だけ現れる線、密度、発光である。

- **時間層**：Sessionとhistoryを地層、沈殿、成長線として示す。

- **接続層**：PairとOS Guideの存在を細い根の接続として示す。

構造層は信号がなくても残る。

観測層は`Active`の実測値がない場合に表示しない。

時間層は実測historyを主表示とし、背景の地層は値を読み替えない低明度の補助表現に限定する。

接続層は接続事実がある場合だけ表示し、品質や推奨を表す色へ転用しない。

## 3. 共通色と素材

graphiteを画面とpanelの基材にする。

ivoryは数値、cyanは現在の測定線、amberはholdとSession、deep tealは構造の微光へ割り当てる。

赤、黄、緑の信号色で品質を採点しない。

背景へ緑のwashを掛けない。

全面発光、全面texture、装飾目的の自律motionは使用しない。

新しい背景は左上、右上、下端だけに有機鉱物構造を置き、中央を計測panel用の負空間として残す。

指定されたHypha素材`bg_mycelium.png`はTIMEとATTACKの下層へ固定表示し、時間が堆積する菌糸床として扱う。

生成した外周素材は観測所、`bg_mycelium.png`はHypha本体という役割に分ける。

数値、label、unit、axis、statusはJUCEで描画し、生成画像へ焼き込まない。

## 4. 画面別の配分

| Surface | 強度 | 主な視覚層 | 実測との接続 | Compactで残す事実（最大3） |
|---|---:|---|---|---|
| POST LEVEL | 2/5 | 構造、観測、接続 | LUFS-Mとbalanceが低明度の菌糸形状を決める | 選択M/S、TP、Crest |
| POST TIME HISTORY | 3/5 | 時間、観測 | ObservatoryはM、S、TP、PLR、correlationのexact history、CompactはM、S、TPだけを表示する | M、S、TP |
| POST TIME ATTACK | 5/5 | 観測、時間 | 選択eventのstrength、texture、brightness、transientをspecimenへ投影する | specimen、主値、選択位置 |
| POST TIME SHARP | 3/5 | 観測、時間 | exact Sharpness差分と六秒historyを膜状のfillへ投影する | 現在値、差分、history |
| POST TIME LIVE | 3/5 | 観測、時間 | POST単体のLUFS-M、TP、Sharpnessを固定scale上で追跡する | 三つの絶対値、history |
| POST FREQ | 3/5 | 構造、観測、接続 | SpectrumとGuide bandを別authorityとして重ねる | Spectrum、主値、差分 |
| POST SPACE | 4/5 | 観測 | 三秒MID/SIDE densityを抽象的な場へ投影する | field、balance、correlation |
| POST Delta | 2/5 | 観測 | PREを低明度の基準、POSTを現在の測定として描く | 選択ΔM/S、ΔTP、ΔCrest |
| PRE LEVEL | 2/5 | 構造、観測 | upstream sensorの実測だけを表示する | 選択M/S、TP、Crest |
| Capture | 4/5 | 全層 | immutable snapshotをObservation Plateへ固定する | Compactでは操作を出さない |
| WARMING、Inactive、Bypassed | 1/5 | 構造 | 成立していない値を`---`または事実状態で示す | role、domain、state、操作 |

ATTACK、SHARP、FREQは親Shellのbody上へ透明componentとして描画する。
ObservatoryのTIMEは`HISTORY / ATTACK / SHARP / LIVE`を常時見える二階層目のtabとして直接選択する。
Compact Meterでは同じ四項目を一つのcycle controlへ畳み、常設UIを増やさない。
FREQは時間detailへ埋めず一階層目に置き、`MARK`で現在の全帯域差分を固定し、`Focus Trail`で選択帯域の六秒差分を追跡する。

そのため、各解析面は同じ背景、外周、Header、Guide、Footer、Captureの文法を継承する。

## 5. 6画面の比較試作

比較試作は次の六画面を正本とする。

1. POST LEVEL 600×400
2. POST TIME ATTACK 600×400
3. POST FREQとOS Guide 600×400
4. POST SPACE 600×400
5. PRE LEVEL 600×400
6. Capture 1200×630

各画面についてCompact 300×200と375×250、Observatory 450×300と600×400の二系統を確認する。

Captureは追加で1080×1080と1080×1350のbounds契約を検証する。

ATTACKのbodyは既存の`attack_specimen_emission.png`とnative painterを使用し、親Shell側の背景を新しい地下観測所へ統一する。

## 6. responsive契約

画面寸法は四段階を保持するが、利用者に提供する表示思想は二系統だけとする。

### Compact meter: 300×200、375×250

CompactはDAW作業中に常設する即読メーターである。

主値一つと補助値二つまで、合計三つの数値的事実に限定する。

LEVELはM/SとCURRENT/MAXを切替式にし、選択LUFS、TP、Crestの三値だけを同時表示する。

CURRENTとMAXの六値を同時に縮小表示しない。

Compactは多数のトラックとステムへ刺しっぱなしにする常設面であり、optional analysis workerを要求しない。

domain tab、詳細axis、補助metric群、全面背景、domain固有の有機装飾は表示しない。

世界観はHeader接続領域の**Hypha Aperture**一箇所だけに固定する。

Hypha Apertureは小さな有機鉱物の開口部であり、信号状態とPair事実だけを静的な輪郭と微光で示す。

domainが変わっても位置と面積を変えず、小画面へ複数の世界観表現を持ち込まない。

300×200と375×250は同じgeometry規則を使用し、375専用の第三の表示思想を作らない。

### Observatory: 450×300、600×400

Observatoryは分析とCaptureのためのCE2226地下観測所である。

同時に開けるObservatoryは二枠に限定する。

一つ目の用途は、二つのトラックを同じ大画面geometryで見比べることである。

二つ目の用途は、2MIX側の大画面を残したまま、単体トラック側をもう一つの大画面で詰めることである。

二枠は一画面内のmetric数ではなく、optional analysisを所有できる同時インスタンス数を意味する。

四domain、主visual、背景構造、domain固有の世界層、詳細axisを表示する。

LEVELの主値はM、S、I、Crestとし、PREとの差分ではΔM、ΔS、ΔTP、ΔCrestを同じ四列geometryで比較する。

ΔMAXは時刻の異なる独立最大値同士を差し引く可能性があるため作らず、MAXは絶対値の事実としてだけ表示する。

450×300では同じ情報階層の余白と補助labelを縮め、600×400では補助metricとCaptureを含む完全表示にする。

背景素材はaspect ratioを維持して中央cropし、有機構造を引き伸ばさない。

CompactとObservatoryの境界で、測定値、Pair、信号状態、domain選択の意味を変えない。

### 旧版Hyphaから継承する製品核

旧版の価値は背景画像だけではない。

参照: https://kirinmastering.com/ja/hypha

Pair名、PRE/POSTの役割、選択したM/S loudness、TP、Crest、CURRENT/MAXを同じ面で即読でき、多数のトラック、ステム、busへ低負荷で挿しっぱなしにできることが製品核である。

B-617ではPair名、PRE/POST、二系統の画面密度、低負荷境界を親Shellへ固定した。

B-618では既存Watch値と既存M/S設定を再利用し、選択式M/S、TP、Crest、CURRENT/MAXを三値切替としてCompactへ再統合した。

B-619ではObservatory LEVELの主値へCrestを戻し、PREとの差分へΔCrestを追加した。

Audio Thread、Measure Thread、FFI ABIへ新しい処理や値は追加していない。

## 7. 状態と発光

POST Activeを背景明度の基準値にする。

PREは同じ素材をPOSTの72%へ抑える。

InactiveとBypassedはActiveの48%へ抑える。

Captureは静止画で外周を読み取れるよう、同じ背景を8%だけ持ち上げる。

LEVELの菌糸量はLUFS-Mを`-48..0 LUFS`から`0..1`へclampした値だけで変える。

左右方向はbalanceを`-12..+12 dB`から`-1..+1`へclampした値だけで変える。

これらは品質評価ではなく、同じ測定値の第二表現である。

## 8. PairとOS Guide

青いpaired stateだけがHeaderの細いroot connectionを表示する。

waitingと未接続では接続済みの形を表示しない。

OS Guideが存在する場合だけGuide railと下端の細いrootを表示する。

Guide railは現在のdomainを変更しない。

Guideの文字とbandはKirin OS由来の事実であり、Live POSTとLive Deltaの測定値へ混ぜない。

## 9. Capture

Captureは画面の拡大画像ではなく、同じsnapshotを別寸法へ再描画するObservation Plateである。

外周の二重hairlineと四隅の短いtickをCaptureだけに追加する。

OS Guideは既定で含めない。

利用者が明示した場合だけGuideを含める。

保存失敗は利用者操作の結果なので通知する。

## 10. 生成素材と権利境界

`observatory_understory.png`はbuilt-in image generationで新規生成した。

出力は1536×1024、RGB、alphaなし、1,474,029 bytesである。

参照画像はHypha内のConcept CとATTACK emissionだけである。

指定された添付JPEGと既存`bg_mycelium.png`は300×200で一致し、pixel比較の平均PSNRは47.12 dBだった。

JPEG圧縮による差をproduction assetへ増やさないため、既存のPNG正本を使用する。

Kirin SenseのJungle素材は今回の実装へコピーしていない。

既存`assets_source/fonts`のCE2226 letter画像には出典markが含まれるため、production binaryへ埋め込まず、今回の生成参照にも使用していない。

最初の二出力はalphaがなく、透明checkerboardを焼き込んでいたため不採用とした。

採用素材の生成promptは次のとおりである。

```text
Use case: stylized-concept
Asset type: opaque 3:2 background plate for a responsive desktop audio-meter UI
Primary request: create an original CE2226 underground observatory background for Kirin Hypha, a quiet mycelial sensing instrument from 200 years in the future
Input images: Image 1 is the approved Hypha Observatory UI composition reference; Image 2 is the approved ATTACK emission material and palette reference
Scene/backdrop: uniform near-black graphite background matching RGB #0D0F1A, fully opaque
Subject: sparse organic-mineral mycelial architecture confined to the extreme upper-left and upper-right corners, plus a thin root substrate only along the lowest 12 percent; the middle 70 percent must remain clean near-black negative space for native UI panels
Style/medium: refined dark translucent biomineral filaments, microscopic root tissue, mineral inclusions, precision-instrument finish; original artwork
Composition/framing: 3:2 landscape, full bleed; detail occupies less than 18 percent of the canvas; structure remains legible at 600x400 and 300x200
Lighting/mood: very low luminance; muted amber cores, deep teal, tiny ice-cyan nodes; quiet, factual, restrained
Color palette: #0D0F1A graphite, muted bronze/amber, deep teal, sparse ice-cyan
Materials/textures: translucent mycelial veins and dark mineral shell
Constraints: fully opaque image; no checkerboard; no white or light background; no UI panels; no values; no text; no letters; no icons; no logos; no watermark; no bright bloom; no full-screen texture
Avoid: generic vines, steampunk ornament, Celtic filigree, floral wallpaper, fantasy magic, lush leaves, mushrooms, animals, eyes, landscape depth, green wash, decorative symmetry
```

## 11. 検証項目

native render testはPREとPOST、四domain、四size、POSTとDelta、Guide有無、Active、Inactive、Bypassed、LRA Warming、三つのCapture寸法を一括で描画する。

親Shellだけで合格とせず、LEVEL、HISTORY、ATTACK、SHARP、FREQ、SPACEを実際の外部body componentと合成して検証する。

ATTACKはCompact、Observatory、1200×630 Captureの三経路でbodyが欠落しないことをpixel差分で確認する。

300×200と375×250はCompact、450×300と600×400はObservatoryに属することをcompile-timeとruntimeの両方で固定する。

TIMEとSPACEの600×400描画は12 ms未満を維持する。

背景PNGはUIプロセス内で一度だけdecodeし、通常ViewとCapture Viewで共有する。

指定Hypha素材は四つの画面密度に対応するrasterを初回だけ生成し、通常描画では再scaleしない。

描画処理はMessage Threadだけで動作する。

Audio Thread、Measure Thread、FFI、計測式、payloadは変更しない。

Windowsは同じnative render contractをCIで実行し、文字欠け、bounds、stack、asset埋め込みを確認する。
