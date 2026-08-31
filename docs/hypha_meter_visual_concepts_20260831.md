# Kirin Hypha Meter visual concepts

Status: Concept C selected as visual baseline

Date: 2026-08-31

## 1. Purpose

常時開いて読み続けられる精密計器と、CE 2226の菌糸という異質さを同じ画面で成立させる。

3案は機能差ではなく、同じ測定snapshotを異なるvisual grammarで比較した。

2026-08-31にDaisukeが添付画像のConcept Cを「この方向性は悪くない」と評価し、続けて現行動線を仮として根本再設計を許可した。

この判断により、Concept Cを今後のvisual baselineとし、`LEVEL / TIME / FREQ / SPACE`も上位情報設計の候補として扱う。

生成画像内の文字は設計仕様ではない。

数値、単位、情報階層の正本はこの文書とproduct contractとする。

## 2. Verified references

現行PRE/POST画像は `docs/media/kirin-hypha-pre-post.jpg` を参照する。

現行菌糸背景は `crates/hypha_gui/assets/bg_mycelium.png` を参照する。

ATTACK baseline内の現行Analysis画面は`docs/media/kirin-hypha-freq.jpg`、`docs/media/kirin-hypha-sharp.jpg`、`docs/media/kirin-hypha-live.jpg`を参照する。

[Kirin OS 1.0](https://kirinmastering.com/kirin-os-1-0)下部のJungle世界と、同pageで使われている`habitat-signal-grove-1920.webp`、`mycelium-city-1920.webp`、`release-habitat-1440.webp`をvisual world referenceとして確認した。

確認できた特徴は、巨大な菌類建築、暗い湿潤空間、静水の反射、暖色の居住光、疎なcyan signalである。

細い蔓を平面へ貼っただけの装飾は、この世界の奥行きと建築性を再現しない。

現行POST画像から、`PAIR DRUM`、`ΔS -2.5 LU`、`ΔTP -2.0 dB`、`ΔCREST +0.6 dB`を採用する。

repository内のplugin data test fixtureから、`I -14.3 LUFS`、`LRA 6.4 LU`、`PLR 13.1 dB`を採用する。

同fixtureの式`PLR = MAX TP - I`から、`MAX TP -1.2 dBTP`を使う。

mockup固有のM、S、recent TP、左右peak、CORRは視覚比較用の固定値であり、実音源の測定結果を称しない。

## 3. Shared snapshot

| Field | Value |
|---|---|
| Role | POST |
| Perspective | POST |
| Pair | DRUM connected |
| Meter Session | 04:32 |
| M | -13.8 LUFS |
| S | -14.2 LUFS |
| I | -14.3 LUFS |
| TP | -3.6 dBTP |
| MAX TP | -1.2 dBTP |
| LRA | 6.4 LU |
| PLR | 13.1 dB |
| L TP | -4.2 dBTP |
| R TP | -4.8 dBTP |
| CORR | +0.82 |
| ΔS | -2.5 LU |
| ΔTP | -2.0 dB |
| ΔCREST | +0.6 dB |

## 4. Shared anatomy

Header左に`HYPHA POST`を置く。

Header中央に`LEVEL  TIME  FREQ  SPACE`を置く。

この4領域は生成画像内の仮labelではなく、根本再設計する上位情報構造の候補とする。

Header右に`PAIR DRUM`と青い静的status pointを置く。

Kirin OSからINSPECTまたはMASKING Guideが届いた場合だけ、Header直下に一行のGuide railを追加する。

Guideがない状態ではrail用の空白を残さない。

主領域ではM、S、Iを最も大きく表示する。

TP、MAX TP、LRA、PLRは主数値より一段小さく表示する。

左右の縦meterにはLとRを明記し、peak holdを細い印で示す。

下部に60秒historyと時間軸を置く。

Footerに`POST`、`Δ`、`SESSION 04:32`、`RESET`、`CAPTURE`を置く。

ATTACK、SHARPなどの詳細viewは上位Headerへ並べず、該当domain内のsubviewに置く。

Guide railは現在のdomainを自動変更せず、選択された場合だけTIMEまたはFREQへ移動する。

選択中のPOSTは明度差と細い輪郭で示し、品質色は使わない。

## 5. Concept A: Precision Instrument

測定値と座標系を主役にする。

背景はほぼ黒に近いgraphiteとし、菌糸は低contrastの構造線として残す。

主数値は暖かいivory、補助情報は青みのあるgray、active stateは低彩度cyanを使う。

1 pxのhairline、明確なbaseline、広めの余白、tabular figuresで研究計器の緊張感を作る。

Historyは細い実線とsample pointで構成し、glowを最小限にする。

長時間の可読性と測定器としての信頼感を最優先する。

弱点候補は、Hypha固有の異質さとSNS上の一目で分かる特徴が弱くなることである。

## 6. Concept B: Living Mycelium

菌糸の生体的な存在感を主役にする。

背景の菌糸networkを測定領域の境界やhistoryの流れと接続する。

M、S、TPの実測値だけが発光量を変える前提で、cyanとamberの局所glowを使う。

数値の背後には暗いsolid plateを置き、textureと文字を直接競合させない。

Goniometerやhistoryを菌糸の成長形状として読める構図にする。

Jungle世界の調査後は、単純なnetwork密度ではなく、菌類建築の量塊、空間の奥行き、内部に人の活動を感じるamber lightを比較軸に加える。

SNS上でHyphaと識別できる固有性を最優先する。

弱点候補は、視覚密度とglowが長時間利用時の疲労や数値の読みづらさにつながることである。

## 7. Concept C: Hybrid Observatory

Precision Instrumentの情報階層を骨格にし、Living Myceliumを測定データの第二表現として限定利用する。

主数値とunitはsolidなgraphite panelで保護する。

菌糸は外周、領域区切り、historyの下層、status pointの周辺だけに現れる。

外周の菌糸は細いornamentとして描かず、暗い奥行きから生えた構造物の断面として扱う。

ivoryの数字、cool cyanの測定線、低彩度amberのhold markerを使う。

一つの焦点だけに局所glowを許し、全面発光を避ける。

静止画でも精密さとHyphaらしさが同時に残ることを狙う。

弱点候補は、抑制が弱いとAとBの要素が競合し、逆に抑制しすぎると中庸になることである。

## 8. Selection rubric

同じsnapshot、同じ画角、同じ表示項目で3案を比較する。

| Criterion | Weight | Question |
|---|---:|---|
| Measurement legibility | 25 | 数値、unit、state、time windowを一読できるか |
| Always-on comfort | 20 | 30分以上開いても眩しさとmotionが邪魔にならないか |
| Hypha identity | 20 | 小さなSNS画像でもHyphaと識別できるか |
| Responsive integrity | 15 | 4サイズへ縮退しても情報階層が壊れないか |
| Capture presence | 10 | 1200×630と1080×1350で構図が成立するか |
| Data-linked motion | 5 | motionを止めても意味が失われず、動く場合は実測に結び付くか |
| Implementation risk | 5 | RT境界、CPU、描画負荷、asset依存を管理できるか |

70点未満の案は採用しない。

最高点の案をそのまま採用するとは限らない。

弱点が明確な場合は、最高点案を骨格にして他案から一要素だけ移植する。

## 9. Review procedure

1. 3枚を同じ表示寸法で並べ、5秒で読み取れた値を記録する。
2. 100%表示でunit、小数点、選択状態、history軸を確認する。
3. 25%表示で製品識別性と主数値の残り方を確認する。
4. grayscaleで情報が色だけに依存していないか確認する。
5. 30分表示する実装prototyping前に、静止画の輝度面積を比較する。
6. 選定後に4サイズのwireframeを作り、1画面だけの最適化を防ぐ。

## 10. Image generation prompts

各promptは現行PRE/POST画像と菌糸背景をreference imageとして使う。

共通して、single standalone desktop audio meter plugin、landscape 3:2、front orthographic view、no DAW chrome、no hands、no desk、no perspective distortionを指定する。

Concept Aはeditorial UI mockup、near-black graphite、subtle mycelium linework、strict grid、large tabular numerals、minimal glow、precise axesを指定する。

Concept Bはdark biological observatory、data-linked mycelium network、localized cyan and amber bioluminescence、solid dark number plates、controlled high contrastを指定する。

Concept Cはprecision instrument skeleton、restrained living mycelium perimeter、solid number panels、one focal glow、quiet cinematic depth、legible measurement hierarchyを指定する。

生成時にはShared snapshotのlabelと値を明示し、余分なmeter、knob、waveform、marketing copyを追加しないよう指定する。

## 11. Generated outputs

| Concept | File | Pixel size |
|---|---|---:|
| A Precision Instrument | `docs/media/hypha_meter_concepts/concept-a-precision-instrument.png` | 1536×1024 |
| B Living Mycelium | `docs/media/hypha_meter_concepts/concept-b-living-mycelium.png` | 1536×1024 |
| C Hybrid Observatory | `docs/media/hypha_meter_concepts/concept-c-hybrid-observatory.png` | 1536×1024 |

生成tool、reference、完全なpromptは `docs/media/hypha_meter_concepts/README.md` に保存する。

3枚ともM、S、I、TP、MAX TP、LRA、PLR、L TP、R TP、CORRの値と単位を原寸目視で確認した。

3枚ともPOST、Δ、Session、Reset、Captureを持つ。

3枚とも同じ1536×1024であり、比較時のpixel数に差はない。

## 12. First-pass review

この採点は静止画だけに対する一次評価である。

4サイズ、30分表示、実測連動motion、実機の色再現は未検証である。

| Criterion | Weight | A | B | C |
|---|---:|---:|---:|---:|
| Measurement legibility | 25 | 25 | 19 | 24 |
| Always-on comfort | 20 | 20 | 11 | 18 |
| Hypha identity | 20 | 11 | 20 | 18 |
| Responsive integrity | 15 | 14 | 8 | 14 |
| Capture presence | 10 | 7 | 10 | 9 |
| Data-linked motion | 5 | 5 | 3 | 5 |
| Implementation risk | 5 | 5 | 2 | 4 |
| Total | 100 | 87 | 73 | 92 |

Aは数値、単位、軸、左右meterの読み取りが最も速い。

Aは背景の菌糸が薄く、SNS縮小時に一般的なdark meterへ近づく。

Bは小さなthumbnailでもHyphaと識別しやすい。

Bは発光junctionと菌糸がpanel間を横断し、常時表示と300×200への縮退で負荷が高い。

Cは主数値と補助値をsolid panelで守りながら、外周とhistoryにHypha固有の形を残す。

Cの左右meter scaleは省略が多く、実装時はAの連続した目盛り設計を移植する必要がある。

## 13. Selected direction

Concept C Hybrid Observatoryをvisual baselineとして採用する。

Aからtabular figures、hairline、history軸、左右meter目盛り、余白規則を移植する。

Bの高密度networkは採用しない。

代わりにJungle世界から、暗い菌類建築の量塊、湿度を感じる層、局所的なamber habitat light、疎なcyan signalを移植する。

発光はlive statusとhistoryのNOWだけに制限する。

主数値と軸の周囲は静かなsolid panelを保ち、世界観を数値の背面へ敷き詰めない。

この方向を`Hypha Observatory`と呼び、PREとPOSTの4サイズwireframeの基準にする。

生成画像に描かれた`LEVEL / TIME / FREQ / SPACE`は情報設計の起点として採用するが、各domain内のmetric配置とsubviewは実装contractで確定する。

現行の`METERS / ANALYSIS`と`ATTACK / FREQ / SHARP / LIVE`はvisual baselineを拘束しない。

INSPECTとMASKINGは第五tabにせず、`OS GUIDE`として全domain共通のcontext layerへ置く。

TIMEとFREQではGuideのmarkerまたはbandと`LIVE POST`、`LIVE Δ`を併置できるが、authorityはlabelとline styleで分離する。

次の設計出力は、同一fixtureによるPREとPOSTの300×200、375×250、450×300、600×400である。
