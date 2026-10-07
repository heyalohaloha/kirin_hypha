# G0 — DRUM／PSR 実寸配置仕様

2026年10月7日。親計画は[DRUM・PSR改善計画](../hypha_drum_psr_usability_improvement_plan_20261007.md)、観測の意味は[snapshot契約](snapshot_contract.md)を正本とする。本書は設計用wireの寸法であり、製品の実装済み契約・検証合格ではない。製品source／ABIへの着手は1.1.51の三チャネル公開後。検出再評価は別計画、責任者＋友人1～2人の利用者評価は未実施。

## 1. 確認した実装・画面

読取sourceは `.claude/worktrees/loop`、B-1300 `bcb9ffd3b3913e9d5c4a526df38b4a7f430be99d`。同checkoutのAGENTS.md、release入口、`docs/hypha_ce2226_jungle_visual_system_20260901.md`、`docs/media/readme/drum.jpg`を確認した。以下は同checkoutのsourceを指す。

- shell寸法：`juce_shell/src/HyphaObservatoryContract.h`、`HyphaAttackUiContract.h`。
- 文字寸法：`HyphaTypographyContract.h`、`HyphaTextStyle.cpp`。requiredLineHeightはceilで確保する。
- 書体：`HyphaUiContract.h`、`HyphaTypography.cpp`。macOSはlabel SF／数値SF Mono、日本語はヒラギノ。WindowsはSegoe UI／Consolas、日本語はYu Gothic UI等。
- 素材・枠：`HyphaMainFrameGeometry.h`、`HyphaMainFrame.h`、`HyphaDepthMaterial.h`。観測窓が主役、四値の面は脇役。
- 数値：`HyphaAttackGlancePainter.cpp`、`HyphaAttackBandSummaryPainter.cpp`。現行の文字列幅に応じたfont選択をG0の固定font合格に読み替えない。

HTMLのui-monospace／Menlo等はnative数値書体と完全一致しない。DOMのfitと後工程のnative文字boundsを別の証拠にする。

## 2. 共通shell geometryを据え置く

外寸、上位domain、TIME navigation、外周、footerの位置を変えない。G0の新geometryは以下のanalysis body内部だけ。

| サイズ | editor W×H | margin | header H | TIME nav H | body x,y,W,H | footer |
| --- | --- | --- | --- | --- | --- | --- |
| 100% | 300×200 | 4 | 46 | 24 | 4,76,292,120 | headerへ折畳み |
| 125% | 375×250 | 6 | 50 | 26 | 6,86,363,158 | headerへ折畳み |
| 150% | 450×300 | 8 | 56 | 28 | 8,96,434,164 | 28 |
| 200% | 600×400 | 10 | 64 | 30 | 10,108,580,248 | 30 |
| 300% | 900×600 | 14 | 80 | 38 | 14,136,872,412 | 34 |

POST、Guide absentでの配置値。Guide presentや他roleを同一boundsと決めつけず、対象surfaceに実際のbody boundsを渡す。100%のbottom marginを3 pxへ変更する案は採用しない。

## 3. 主面の役割と文字寸法

一指標に主値一つ。順序はmetric＋unit、scope、value。scopeはWholeなら全N、ConfirmedSubsetなら確定n/N＋全体未成立の短理由、Singleなら最新／固定の一打。whole一点を「全打exact」と言い換えない。確定部分だけlane別ageを持ち、値と同じsnapshotから描く。NoScalarは数値位置を空けたまま理由を読めるようにする。

| 役割 | 100% | 125% | 150% | 200% | 300% |
| --- | --- | --- | --- | --- | --- |
| 主値 font／行高 | 14／16 | 15／17 | 18／21 | 22／25 | 29／33 |
| metric font／行高 | 11／14 | 11／14 | 11.5／14 | 13／16 | 16／20 |
| scope readout font／行高 | 11／13 | 11／13 | 12／14 | 14／16 | 17／20 |
| unit font／行高 | 11／14 | 11／14 | 11／14 | 12／15 | 14／17 |
| action font／行高 | 11／14 | 12／15 | 13／16 | 15／18 | 18／22 |

主値は既存visualization compositionのprimaryValueから固定する。値の桁、scope、欠測の変化でfontやanchorを変えない。unitはmetric行の定位置に置き、主値へ繰返さない。符号と小数を固定数値書体で描く。日本語名案を表示する場合は現行INV-S40の英語label契約を変更するG0決定と明示し、G2で正本と同期する。

上表の行高はnative契約の最低値。G0初回DOMでは125%主値の文字外接高18 pxに対して行予約17 px、200%scopeの外接高17 pxに対して16 pxで重なりが出た。fontを維持し、125%主値へ18 px、150%主値へ22 px相当の予約を与える。200%scopeは行高17 px×二行、先頭1 px、lane36 pxへ増やす。ブラウザー測定からnative sourceのfont値を書き換える判断はしない。

## 4. body配分

### 4.1 100% — 採用する新geometry

| 領域 | H | 内容 |
| --- | --- | --- |
| HISTORY | 34 | context操作行14＋実波形12＋枠／間隔8 |
| 第一段 | 43 | DELAY／ATT、またはALLの上二指標 |
| 第二段 | 43 | REL／LEVEL、またはALLの下二指標 |
| 合計 | 120 | 現行body内 |

列間4 pxならcard幅は144 px、左右padding各6 px、文字有効幅132 px。metric14＋scope13＋value16＝43 px。compact各roleのline-heightを予約高と同じ値で明示し、継承normalによるbaseline差を残さない。history34内のcontextにLIVE復帰／根拠入口を含める。action文字は最低14 px行高なので、13 pxの操作行＋13 px波形とはしない。

これは現行の横一列四値とHISTORY配分を変える新geometry。100%の「見出し・説明・時間軸行なし」の旧visual契約も、scope／短理由／contextを入れる限り変更になる。G0試作を旧契約準拠と呼ばない。BAND／VIEWの設定は125%以上を維持する。

### 4.2 125～300% — G0 wire候補

| サイズ | controls | history／平均図 | 主値領域 | 合計 | 主値構成 |
| --- | --- | --- | --- | --- | --- |
| 125% | 24 | 44 | 90 | 158 | 2×2、45 px／card |
| 150% | 24 | 40 | 100 | 164 | 2×2、50 px／card |
| 200% | 22 | 82 | 144 | 248 | 横長四lane、36 px／lane |
| 300% | 32 | 160 | 220 | 412 | 横長四lane、55 px／lane |

controlsは内部のBAND／VIEW入口へ予約する。VIEWは既存のOVERLAY／2 ROWSで、Summary／Singleの切替名へ流用しない。外側の試作toolbarの選択を、製品内selectorの実寸成立に使わない。ALLでは不要なBANDまとめcaptionを最新一打locatorへ置換し、値領域の座標を維持する。繰返しのDRUM見出しを削るため、これらcontrols高は現行の二行header高とは異なるG0 geometry。

125／150%のcard幅はそれぞれ179／180 px、215 px。左右6 pxを除くと167／168、203 px。150%の2×2は親計画の「四laneの値軸」を主面に常設する案と異なる。採用時は値軸を根拠面へ配置する契約変更を同時に決める。未決定のまま150%主面完成とはしない。

200%横長laneはlabel108 px、scope128 px、value218 pxを予約する。300%はlabel160 px、scope188 px、value300 px。label／unitとscope／理由は別々の二行、valueは一行で固定する。200%はnative minimumの34 pxからDOMの文字外接高に合わせて36 pxへ増やし、scopeは先頭1 px＋17 px×二行で予約する。300%は二行20×2、主値33を55 px内へ置く。値軸／分解能を残す場合は余った横幅に置き、主値・scopeと重ねない。実寸で成立しなければ根拠面を使い、契約変更を明示する。

200%平均図82 pxはcontext18＋pane64 px。枠paddingはpane64の内部へ含め、さらに8 pxを外引きしない新geometry。現行bandPaneMinimumHeight64はcaption／axisを含むglass全体の最低高であり、実波形や各PRE／POST rowを64 px必要とする規則ではない。`HyphaAttackBandPanes.cpp::partsOf()`はpaneの上下6 px、caption16 px、axis14 px、plot上下2 pxを除いた26 pxを、2 ROWSでは4 px gapを挟んだ各11 pxへ分割する。nativeの分割条件はplot高24 px以上。G0のcanvas／SVGでもcaption・時間軸・凡例込みの分割を確認し、狭さで選択した2 ROWSをOVERLAYへ無表示fallbackしない。125／150%は小stripを維持し、差分図は根拠面で選択中のVIEWを反映する。300%は形、参加数、PRE／POST凡例を同snapshotで示す。

## 5. context、根拠面、最新一打locator

contextは帯域、対象N、集計時間幅、条件付き主値がある時だけ確定部分の最大age、LIVE、根拠入口を持つ。固定slotを使い、文字の出現で波形・主値を押し下げない。最大ageは「確定部分の古さ（最大）」という意味が読めるlocale短名とし、集計時間幅と混同する無名の秒数を並べない。

global captionへ「8打」等の対象件数を常設する条件で、laneの短名は「確定7/8」とする。「全実数で全体の範囲を限定できない」は主面短理由を「全体不定」＋lane age、根拠面を原語の説明とする。これは文字幅を確認するG0 locale候補であり、初見の理解が実証された短名ではない。数字だけのn/Nや意味不明な記号へ置換せず、global件数が消える状態は短名の前提を満たさない。

100%で全項目が横一行に収まることは高さの算術から証明できない。JA／ENの最長文字列をDOMへ入れ、全ての文字boundsが重ならないことを確認する。単にageや全体未成立理由を消して通さない。root根拠面では指標別class内訳、全N区間、exact部分、理由、age、分解能、source、窓応答を説明する。主面で必須のscope・限界・短理由を根拠面専用にしない。

ALLのC時点最新一打Bがlook-behindのVへ未到達でも、locatorはBの時刻・状態・選択入口を示す。四値とshapeは同SingleKeyのB、履歴markerだけ実時刻に画面へ入ってから同鍵で強調する。Aのmarkerを選択強調したままBの値を出さない。右の空白や偽のmarker時刻を導入しない。100%波形は全帯域の六秒履歴で、選択帯域包絡と表示しない。

根拠を開閉しても元snapshot／選択を維持し、LIVE一操作で復帰する。LOCKの時刻・帯域・過去の一打を明示する。cluster候補数と前後入口の出現でもcontextの高さを変えず、captionを覆う場合は対象情報の読める代替配置を示す。

## 6. formatterと文字幅のgate

既存桁を保持する初期案はDELAY／ATT 0.1 ms、REL整数ms、LEVEL 0.1 dB。両端区間内のcomma後spaceは0、unitは固定label行。boundの方向付き丸め・open／closedはsnapshot契約を使い、狭い幅に合わせてexactへ変換しない。

独立の最悪fixtureに `[−999.9,+999.9]`、全pending、片側／両側無音、確定1/8＋長尾、ageありscope、旧PRE、窓外LOCK、ALL未到達locator、三候補＋長いlocaleを含める。132 px内で14 px数値の15文字は0.6 em見積りで126 pxだが、これは実fontのfit証明ではない。符号／括弧／open endpointも測る。

±999.9 dBはsourceの最大保証ではない。`attack_band_measure.rs`はf64 filter／power／RMS、`attack_band.rs`のcentre補償にgain capはなく、validityはlevel_dbfs有限を要求するだけ。入力f32の最大だけからfilter後の最大を断定しない。17文字の `[−3202.6,+3202.6]` 等を例外fixtureへ置く。LEVELの絶対値が1000以上なら共通指数を固定unit行へ出し、例を主値 `[−3.21,+3.21]`、unit `×10³ dB` とするG0 formatter案を採用する。区間端点は変換後も外向きに丸め、元値／指数／文字列をmetadataへ保存する。指数あり／なしで行高やanchorを変えない。詳細は元値を示す。cap、ellipsis、符号やunit省略で閉じない。広い値を実音源で必ず生じる値とも断定しない。指数の最大桁を含む全有限型のfitは独立fixtureで確認する。

## 7. PSRと動的な品位

PSRは100%非表示、125%以上。現行125%はcompactのためPSR laneがなく、125%での追加はG0の契約変更。主値はpairedなら `PSR Δ −3.3 dB`、単体なら `PSR POST 10.1 dB`。Δ待ちは `Δ ---` と理由を残し、POST数値へ無表示差替えしない。式はhelpへ移し、主面の数値／履歴へ使う。TIME主面に全体target button、M／S／TP、PLR／CORRを含めてfitを確認する。PSRは自動paired Δ／単体POSTで、全体targetとの意味を混ぜない。helpはPSR主値行の右の固定入口とし、数値と重ねない。

`HyphaTimeHistoryLayout.cpp::makeGeometry()`でcompactを解除した時の参考PSR lane高は125／150で37 px、200で55 px、300で80 px。これは125%の現行表示済み高ではない。readout roleの行高13／14／16／20と最低24 px plotを別に取る。主値をvisualization primaryへ大きくするなら125／150で41／45 px以上が必要で、main historyとの再配分を明示する。37 pxに15／18 px主値と24 px plotを押し込まない。

G0最終wire候補はnative lane高の単純流用をせず、PSR主値行と理由行をgraphの外へ分ける。各graph高は125%37、150%39、200%64、300%128 px。小さい37 px graphは実trace24＋時刻axis13 pxで、主値行17～18 pxをgraph37へ含めない。全体履歴とPSR履歴へ同じgeometryを与え、slotごとのcutoff／失効を別に示す。DOM再試験結果を受け取るまではfit候補とする。

数値・件数・理由・平均図は同revisionで同時更新する。BANDまとめ4Hzは候補、ALL／Singleは30Hz、TIME／PSRは100 ms。viewportの移動は数値提示周期と分離する。font、unit、符号、scope、理由、countのanchorは更新を跨いで固定する。測定値の補間、根拠のないsmooth値、フラッシュを高級感の代替にしない。

色は象牙色の数値、通常文字、POST gold、Δ／選択cyan、PRE無彩色を既存正本から採る。銅／紫はlabel・細い線に留める。静的素材と縁の光は値で動かさず、四値の面を観測窓と同じ明るい枠で囲わない。高級感は既存native素材の最終画面と動的受入で判断する。低忠実度HTMLの静止fitだけで合格にしない。

## 8. 現時点の完了境界

sourceと保存済み実画面の読取、shell・文字行高の算出、100%の縦配分、初回DOMの文字外接高による配置補正まで反映した。5サイズ×JA／ENのDOM再試験結果、必要selector／値軸／平均図の領域、広い有限値formatter、cluster時caption、PSR全TIME配分はwireで個別に確認する項目。原語の短名・初見理解、製品native描画、実host、友人受入、motion／性能gateは未実施。G0の未成立を製品不具合の修正済み・受入PASSとして報告しない。

独立reviewで初回検査のscope↔value等208交差が見つかった。全role対の交差検査、compact line-heightの明示、125%主値18 px行（font15維持）、両側無音fixtureを追加し、最終280条件を再試験して主要文字のbounds交差0。native glyph／SVG／実hit testingの合格にはしない。
