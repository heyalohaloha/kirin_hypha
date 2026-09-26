# Hypha CE2226 Visual System

Status: implemented development baseline

Date: 2026-09-01

Branch: `codex/hypha-meter`

Baseline: `a29f50c5cf38fe29fad2bedc7db690c492c2f471`

## 2026-09-11の承認済み方向と段階実装計画

Hyphaは通常時からCE 2226であり、Jungle発動は同じ生態系の生命感が加速する状態として扱う。
現在のVUの品位を通常版の全画面へ広げ、その通常版をJungleで深める方針が承認された。
具体的な仕上げ、全5サイズ、連動、軽量性、検証は[改訂実装計画](hypha_jungle_activation_and_visual_plan_20260911.md)を参照する。
通常版の共通surface、OSのJungle発動とMASKING送信をAND条件にしたpublisher、Hypha共有service、全5サイズ共通のJungle差分は実装済みである。
Jungle差分は追加bitmapやanimationを使わず、VUのガラス下層と共通外周へnativeの連続菌糸と低明度の内部光だけを加える。
初回の連動ポップアップや由来badgeは表示しない。
発動後は既存Displayメニューの`Jungle Mode`だけで独立してON/OFFできる。
以下の既存baselineを、未実装のJungle外観や連動まで達成済みだと読み替えない。
VUの計測、構図、操作と既存の意味色を守り、破線、点線、自律的な装飾animationは追加しない。

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

温かい黒（`#16110D`、わずかに焦げ茶へ寄せたgraphite）を画面とpanelの基材にする。

通常panelと未選択controlは、明るい四角枠で区切らない。
暗い基材の層差、上辺の連続した低明度反射、下辺の沈みで境界を成立させ、選択箇所だけ意味色の短い光を持たせる。
VUの計器フレームはこの簡略化の対象外とし、既存の筐体構図と意味を維持する。

### 3.1 Hypha共通配色（2026-09-26 Daisuke決定）

案2（黒とシャンパンゴールド）を中心に、案3（焦げ茶の地と水色）を少し混ぜた配色を全画面で使う。
実画面4面（VU、FREQ、DRUM、LIVE）の比較で決めた。色の値は`HyphaUiContract.h`と`HyphaAttackUiContract.h`が正本である。

| 役割 | 色 | 使う所 |
|---|---|---|
| 基材 | 温かい黒 `#16110D` | 画面、panel、計測面 |
| POSTの値 | シャンパンゴールド `#E0BD7E` | FREQのPOST、LIVEのLUFS-M、DRUMのPOST包絡、STRENGTH（`#D9A24E`） |
| 差分・動き・選択 | 水色 `#7FCFD8` | Δ、M/SのMID、true peak（VUのbar、LIVE）、PAIR、TRANSIENT、DRUMの選択（淡い氷色 `#B5E6EF`） |
| PRE | 温かい灰 `#968C80`（DRUMのPRE traceは明るい無彩色 `#D8D0C4`） | PRE曲線、PRE trace |
| 数値 | 象牙色 `#F0E4CC`、通常文字 `#E8E2D8` | 数値、見出し |
| 素材・保持・Guide | 金 `#C9A15A` | 名前、hold、Session、Guide |
| 小さな印と細い線だけ | 銅 `#D0835A`、薄紫（SIDE `#AD9FDC`、Sharpness `#B3A2E6`） | CREST、SIDE、Sharpness（LIVEの線、DRUMのlane）のlabel、棒、細い線 |

広い面と長い線に使う色は、基材、金、水色、無彩色（象牙色、温かい灰）だけとする。
銅と薄紫は区別のための色なので、label、棒、先端、細いデータ線にだけ使い、帯や面にしない（DRUMのlaneの0線は象牙色）。
同じ意味には全画面で同じ色を使う。画面ごとに意味を変えない。
根拠：核の色を2〜3色に絞る、低彩度ほど格が高く見える、青は有能さと精密さ、黒と金は精密さと高品質の印象を与える、計器は意味ごとに色を固定する、という調査結果による。


赤、黄、緑の信号色で品質を採点しない。

背景へ緑のwashを掛けない。

全面発光、全面texture、装飾目的の自律motionは使用しない。

新しい背景は右上に主形象を置き、下端には画面を支える低い有機鉱物構造だけを残す。

左上と中央は計測panel用の負空間とし、下端の構造は画面高の12%以内、中央60%はほぼ暗部に保つ。

LEVELは全面背景の上へ別の不透明画像を重ねない。

指定されたHypha素材`bg_mycelium.png`はTIMEとATTACKの下層へ固定表示し、時間が堆積する菌糸床として扱う。

生成した外周素材は観測所、`bg_mycelium.png`はHypha本体という役割に分ける。

数値、label、unit、axis、statusはJUCEで描画し、生成画像へ焼き込まない。

### 3.2 立体感（2.5D）（2026-09-26 Daisuke決定）

立体感は縁で作り、面の中央を光らせない。中央を明るくして膨らみを見せる表現は使わない。
光源は左上の一つだけとし、全画面の面が同じ向きの光を受ける。

- 観測窓（FREQ、LIVE、SHARPなどのplot）と計測card、section panelは凹んだガラスとする。上と左の壁に内側の影、下と右の縁に弱い反射、切り口の上端に細い象牙色の線、左上から中央の手前で消える静かな映り込みを置く。観測窓だけ右端と床をわずかに沈める。
- 操作部は浮き出た板とする。上の縁に細い光、下側に沈み、板の下に柔らかい接地影を置く。
- 凹んだ面の上端のすぐ外に、面の縁が落とす1 pxの影を置く。
- これらは素材であり、計測値に従わないので描画cacheに一度だけ描いてよい。正本は`HyphaDepthMaterial.h`で、DRUMの溝も同じ関数を使う。
- 軽さは見た目と同じく品質の一部とする（2026-09-26 Daisuke指示）。静的な素材（panel、観測窓、操作部の板、DRUMの本体の光）はエディタが開いている間、端末解像度の画像として一度だけ描き、以後の再描画では貼るだけにする（`HyphaMaterialCache.h`）。画像はエディタを閉じると解放し、合計24 MBを上限とする。直接描画との差は3階調以内とし、契約テストで確かめる。
- 毎フレーム変わる層（FREQの地形など）は、半透明の面を重ね塗りせず各画素をほぼ一度だけ書く。描画予算は、エディタと同じくcacheを持った状態で測る。

FREQの200%と300%のPOST絶対表示は、六秒の履歴を奥へ並べた地形として描く（INV-S34）。平面曲線が現在の値、地形はその六秒前までの経過である。
PRE/POST対応のΔ表示は平面のままとする（2026-09-26 Daisuke決定）。
同じ2サイズでは、plotの四隅の目印、描いている量の定義、表示中の解析条件を計器の注記として小さく左下と左上へ置く。

## 4. 画面別の配分

| Surface | 強度 | 主な視覚層 | 実測との接続 | Compactで残す事実（最大3） |
|---|---:|---|---|---|
| POST LEVEL | 2/5 | 構造、観測、接続 | LUFS-Mとbalanceが低明度の菌糸形状を決める | 選択M/S、TP、Crest |
| POST TIME HISTORY | 3/5 | 時間、観測 | ObservatoryはM、S、TP、PLR、correlationのexact history、CompactはM、S、TPだけを表示する | M、S、TP |
| POST TIME ATTACK | 5/5 | 観測、時間 | 六秒HISTORYの下で打音ごとのTRANSIENT、STRENGTH、CREST、SHARPNESSのlaneを同じ時間軸へ並べ、選択打音を静的な菌糸線で貫く | HISTORY、選択打音の四値、選択位置 |
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

POST targetのFREQでは`LR / MID / SIDE`の右に`M/S`を置き、同じapertureのMIDをcyan実線、SIDEをviolet実線で重ねる。
曲線の識別に破線や点線を使わず、色、連続した輪郭、抑えた発光量で精密さを保つ。
既存VUの暗部、細い光、端正な数値を質感の基準とし、VU自体の配色や挙動は変更しない。
全絶対Spectrumのlive inkは上昇を次の既存描画tickで反映し、下降だけを20 dB／500 msで残す。符号付きΔは正負を偏らせない150 msの対称追従とし、数値、MARK、Focus Trail、履歴、正本snapshotへ時間平滑を混ぜない。

M/Sは二つの絶対測定を比較する観測面であり、PRE/POST差分、評価色、fill、glow、MARK、Focus Trailを混在させない。

M/S中のΔとΔ中のM/Sは位置を残して低明度のdisabled表示とし、LR、MID、SIDEへ戻る経路を常に見せる。

そのため、各解析面は同じ背景、外周、Header、Guide、Footer、Captureの文法を継承する。

## 5. 6画面の比較試作

比較試作は次の六画面を正本とする。

1. POST LEVEL 600×400
2. POST TIME ATTACK 600×400
3. POST FREQとOS Guide 600×400
4. POST SPACE 600×400
5. PRE LEVEL 600×400
6. Capture 1200×630

各画面についてCompact 300×200と375×250、Observatory 450×300と600×400、Inspection 900×600の五つの基準寸法を確認する。表示思想はCompactとObservatoryの二系統であり、InspectionはObservatoryの高解像度表示である。

Captureは追加で1080×1080と1080×1350のbounds契約を検証する。

ATTACKは専用の画像素材を持たない。B-1015で水中生命体の中央標本（`attack_specimen_body_v3.png`）を撤去し、HISTORYと四laneをnative painterで描く。
計測面は殻より一段深い黒とし、共通のHypha素材`bg_mycelium.png`を時間の床として低明度で敷く。
CE 2226の表現は、選択打音を貫く静的な菌糸線（exact x ±1 px、打音sampleで決まるdrift、HISTORY上のbulb、各laneの値の先端の点、下端の先端）と、観測値だけが持つ光に集約する。時間で揺らさない。
2026-09-26から計測面は2.5Dの奥行き（凹んだガラスの溝、ガラス管の光、古いほど薄くなる光、片側のHISTORY）を持つ。
数値はivory、lane色はlabelと棒と短いaccentに限る。詳細は`hypha_drum_lanes_20260924.md`の外観節を正本とする。

## 6. responsive契約

画面寸法は300×200、375×250、450×300、600×400、900×600の五つを基準とするが、利用者に提供する表示思想はCompactとObservatoryの二系統だけとする。

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

2026-09-26から、Compactの二寸法では下段を上段2行目へ畳み、本体を下端の余白まで広げる（INV-S37）。
2行目は右からPOST／Δ、サイズ、VU・STOP・MENU、OS Guideの順に場所を保ち、domainの巡回が残りの幅を使う。
WAITING、BYPASSEDなど短い状態は表示中だけ巡回の行の右半分に出す。
フィードバックと実行中のCaptureは表示中だけ本体下端の1行の帯に全幅で出し、解析ページより手前に置く。

DAW Record中はCompactの常設面をHybrid VUへ一時置換する。通常時も`VU`ボタン（Compactでは上段2行目、Observatoryでは下段）から同じ面を開き、同じボタンで選択domainを変更せず元の画面へ戻る。
Recordによる面は停止時に元の画面へ戻る。手動選択はRecord開始／停止と独立する。
既定ONの表示設定をOFFにした場合、またはHybrid VUの情報メニューから選択中のviewへ戻した場合は、自動置換を行わない。後者はそのRecord区間だけ有効とする。
左右の針は0 VU = -18 dBFSの300 ms平均応答、上段cyan railは左右100 ms True Peak、amber markerはSession開始または直近`CLEAR`以降の左右最大TPとし、異なる時間尺度を一つの針へ混ぜない。
`CLEAR`はcalibration stripの既存button styleでTP markerとclip latchだけを解除し、現在値、Session正本、履歴、Record／Keepを変更しない。通常表示への復帰も同じ`VU`ボタンで行い、新しい画面階層を追加しない。
下段は選択M/S、TP、Crestの三値を維持し、音種別の推奨帯や品質色を追加しない。
Record面の低明度菌糸はmeter face下端と外周だけに置き、目盛り、針、数値の負空間を侵食しない。

### Standard: 450×300 / Full cockpit: 600×400

450×300は詳細な計測面、600×400はCE2226地下観測所のfull cockpitである。

同時に開けるObservatoryは二枠に限定する。

一つ目の用途は、二つのトラックを同じ大画面geometryで見比べることである。

二つ目の用途は、2MIX側の大画面を残したまま、単体トラック側をもう一つの大画面で詰めることである。

二枠は一画面内のmetric数ではなく、optional analysisを所有できる同時インスタンス数を意味する。

両方で四domain、主visual、詳細axisを表示する。

全面背景、domain固有の世界層、LEVELの60秒Historyは600×400以上に表示する。

450×300のLEVEL主値はM、S、I、Crestとする。

600×400はConcept Cに合わせてM、S、Iを三つの主面とし、TP、MAX TP、LRA、PLR、Crestを五つの補助面へ置く。

主面は全面背景の暗部とnativeの面差で一つに括り、別の不透明なHypha素材を重ねない。

中央の数値面は暗く静かに保つ。

LEVEL下段は60秒Historyを既定とし、Spectrumは重複搭載せずFREQを正規入口にする。

LEVEL Historyの横軸は常に固定60秒とし、測定開始直後の短い履歴を横幅いっぱいへ引き伸ばさない。

Mを主線、Sを低彩度の副線とし、TPは連続線を重ねず、runごとの2秒区間で保持した最大`true_peak.max`をevent stemとして示す。TPは別railへ分離せず、同じ60秒全面の下部へ右側`+6〜-24 dBTP`軸とともに重ねる。M/S Historyは全面を使い、TP stemだけが下から立ち上がる。

可視区間の正確な最大値と相対時刻を`60 S MAX TP`として表示し、中央の`MAX TP`がResetまでの全Session最大であることと区別する。Max Mは現在Mの面から外し、同じHistory凡例にSession factとして置く。

ポインタ位置では同一100 ms観測点のM、S、TP、相対時刻へ切り替え、Captureは同じ固定軸とevent位置を正本snapshotから描く。

Δ HistoryはM/S差分に限定し、意味の異なる符号付きΔTPを絶対TP eventへ混在させない。

各100 ms History点はL/R別の新規sample clip run数を保持し、shared plot下端にchannel別pipを置く。hoverでは同じ観測点の相対時刻とL/R件数を表示し、右stripのSession累積値とは範囲を混同しない。

900×600は四domain共通のInspection Viewとし、LEVELではHistory、channel strip、数値階層へ追加面積を与える。TIME、FREQ、SPACEとTIME配下解析も既存の測定事実と操作を変えず高解像度化し、未合意の新指標は載せない。

Hybrid VUは300×200から900×600まで同じ3:2構図を保つ。
300×200と375×250では補助目盛りを間引き、450×300以上ではVUとTrue Peakの全補助目盛りを出す。
筐体、ガラス、菌糸は最大表示より高解像度の埋込chassis素材を縮小使用し、針、目盛り、True Peak rail、文字、線幅、余白はnative painterが実boundsから再計算する。
素材へ測定値や目盛りを焼き込まず、全ての動的事実はnative layerだけが所有する。

PREとの差分ではΔM、ΔS、ΔTP、ΔCrestを同じ四列geometryで比較する。

ΔMAXは時刻の異なる独立最大値同士を差し引く可能性があるため作らず、MAXは絶対値の事実としてだけ表示する。

450×300では世界背景を出さず、同じ測定事実を明るさと余白を抑えたStandard面で示す。

600×400のPOST FooterだけにPOSTとΔの独立ボタン、左右TP数値と0〜−48 dBTP目盛り、CAPTURE入口を置く。

右端L/R meterは1 dBごとの48 blockを使い、300%でも粗い24段表示に見せない。左上の静的titleは役割を先頭にした`PRE HYPHA`／`POST HYPHA`とし、PREは青、POSTはflora amberで即座に区別する。domain tabと同じ操作 affordanceは持たせない。runtimeのPAIR name fieldがあるサイズでは背景側のPAIR文字を重ねて描かない。

450×300以下ではPOST/Δを一つの切替へ畳み、CAPTURE入口を出さない。

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

OS Guideが存在する場合だけFooter（Compactでは上段2行目）に短いGuide contextを表示する。

Guide contextは現在のdomainを変更せず、測定面の高さも変えない。

Guideの文字とbandはKirin OS由来の事実であり、Live POSTとLive Deltaの測定値へ混ぜない。

## 9. Capture

Captureは画面の拡大画像ではなく、同じsnapshotを別寸法へ再描画するObservation Plateである。

外周の二重hairlineと四隅の短いtickをCaptureだけに追加する。

OS Guideは既定で含めない。

利用者が明示した場合だけGuideを含める。

PRE名、POST名、プロジェクト名も個別opt-inとする。

PRE名はpair表示名、POST名はhost track表示名、プロジェクト名は承認済みKirin OS Work表示名だけを使い、利用できない表示名をpath、UUID、work ID、instance IDで補完しない。

CAPTURE操作時にshellと表示中の外部解析面を同じmessage-thread read boundaryで画像へ固定し、保存先選択中のtimer更新を出力へ混ぜない。

LEVELのObservation Plateでは、通常画面を縦横へ引き伸ばさず、右のchannel strip、上段の測定値、下段の60秒Historyを一つの標本構図に組み直す。

60秒Historyでは指定済みHypha標本と時間strataをHistory領域内だけ、測定線の背後へ低明度で残し、Mを青緑、Sを低彩度の補助線として描く。

保存失敗は利用者操作の結果なので通知する。

## 10. 生成素材と権利境界

`observatory_understory.png`はbuilt-in image generationで新規生成した。

2026-09-15に下部の主張を抑えたv8へ差し替えた。

現在の出力は1536×1024、RGB、alphaなし、1,351,618 bytesである。

参照画像はHypha内のConcept CとATTACK emissionだけである。

指定された添付JPEGと既存`bg_mycelium.png`は300×200で一致し、pixel比較の平均PSNRは47.12 dBだった。

JPEG圧縮による差をproduction assetへ増やさないため、既存のPNG正本を使用する。

Kirin SenseのJungle素材は今回の実装へコピーしていない。

既存`assets_source/fonts`のCE2226 letter画像には出典markが含まれるため、production binaryへ埋め込まず、今回の生成参照にも使用していない。

最初の二出力はalphaがなく、透明checkerboardを焼き込んでいたため不採用とした。

初回素材の生成promptは次のとおりである。

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

v8への差し替えに使った編集promptは次のとおりである。

```text
Keep the approved upper-right spiral organism exactly as it is. Reduce the lower foreground substantially so it supports the interface instead of competing with it. In the lower-left, reduce the organic form to roughly 45 percent of its current height and 55 percent of its current area, keep it within the lowest 12 percent of the canvas, and reduce the amber light by about 60 percent. In the lower-right, remove the mushroom-like mound and leave only a very low dark substrate with one tiny subdued cyan gland near the edge. Keep the bottom center almost empty and near-black. Preserve irregular biological material and avoid repeated mesh, scales, regular cells, decorative bands, or wallpaper-like patterns. Do not change the graphite background, upper-right composition, palette, aspect ratio, or overall rendering style. No UI, text, letters, icons, logos, watermark, borders, or panels.
```

## 11. 検証項目

native render testはPREとPOST、四domain、五size、POSTとDelta、Guide有無、Active、Inactive、Bypassed、LRA Warming、三つのCapture寸法を一括で描画する。

親Shellだけで合格とせず、LEVEL、HISTORY、ATTACK、SHARP、FREQ、SPACEを実際の外部body componentと合成して検証する。

ATTACKはCompact、Observatory、1200×630 Captureの三経路でbodyが欠落しないことをpixel差分で確認する。

300×200と375×250はCompact、450×300はStandard、600×400はfull cockpit、900×600はInspection Viewとしてcompile-timeとruntimeの両方で固定する。文字はこの五つを基準点として中間寸法を連続補間し、役割と構成は`HyphaTypographyContract.h`、画面対応は`HyphaSurfacePresentation.h`を正本とする。

文字は一律拡大しない。LEVELのM/S/Iなど即読する主値を`primaryValue`、TP、MAX TP、LRA、PLR、Crestなど比較を補助する値を`secondaryValue`として、全基準寸法で主値を大きく保つ。ATTACKの四laneは同じ観測階層として同じ高さと列にそろえ、lane名を`metricLabel`、選択打音の値を`secondaryValue`、値を出さない理由を`readout`とする。

利用可能なlabel、unit、axis、legend、説明文には背景に対して4.5:1以上の可読色を使う。従来のmuted色は欠測値、無効な操作、非文字の補助線へ限定し、存在する情報を単に薄く見せる用途には使わない。PRESENCE overlayの既存値は変更しない。

TIMEとSPACEの600×400描画は12 ms未満を維持し、900×600も独立の性能上限で検証する。

背景PNGはUIプロセス内で一度だけdecodeし、通常ViewとCapture Viewで共有する。

指定Hypha素材は四つの画面密度に対応するrasterを初回だけ生成し、通常描画では再scaleしない。

描画処理はMessage Threadだけで動作する。

900×600追加はAudio Threadと計測式を変更しない。L/R clip event timestampはMeasure Thread既存100 ms観測から履歴schemaへ渡し、Audio Threadへ処理を追加しない。

Windowsは同じnative render contractをCIで実行し、文字欠け、bounds、stack、asset埋め込みを確認する。
