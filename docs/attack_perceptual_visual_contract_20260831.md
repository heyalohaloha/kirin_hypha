# Hypha ATTACK perceptual visual contract

## Decision

ATTACKは波形から楽器を推測する画面にしない。
SuperFluxはトランジェントの位置を決める内部検出器に限定し、利用者へは同じイベントがPREからPOSTでどう感じられやすくなったかを、四つの観測量として見せる。

| 表示 | 実測量 | 見せ方 |
|---|---|---|
| STRENGTH | 30 ms attack RMS | 暖色の中心core。固定−48..0 dBFS、波形高と併記 |
| BRIGHTNESS | 既存100 ms Sharpness | ice blueの外側shell。固定0..3 acum |
| TRANSIENT | 30 ms attack RMS − 直前100 ms RMS | tealのenvelope aura。固定0..18 dB |
| TEXTURE | sample-edge比、Crest、peak plateau幅 | copperの連続組織。原因をSaturationとは断定しない |

色や文言で良い、悪い、改善、劣化を示さない。
六秒overviewへ四成分を同じ強さで重ねない。overviewは連続波形を背骨とし、イベントの体積と外光だけを即読できる形で残す。選択イベントは`−100 / +30 ms`のEVENT SHAPEへ拡大し、ここで四成分と四つの実測cardを同時に読む。PREまたはPOST単体でも同じ固定scaleを使い、focusの横軸を六秒overviewと混同させない。
色を硬い境界線では区切らず、連続波形を背骨、実測attack shapeを各イベントの個体輪郭として描く。個体内部のamber体積をStrength、その周囲に連続するcopper組織の密度をTexture、外側のice表皮をBrightness、輪郭外のteal発光をTransientとする。暖色の内部二層と寒色の外部二層を分け、実測が同時に強いほど灰褐色へ収束する表示を禁止する。点、点線、短いdash、四角、三角、菱形、等間隔stripeのような記号形は使わず、実測shapeへ追従する連続曲線と連続面だけで、生き物の断面のように一体で見せる。
四色を同じ彩度・面積で競わせない。量は色相の移動ではなく、固定色の明度、透明度、厚みで示す。色名が読めなくても、`CORE / FIELD / SHELL / AURA`の位置、形、密度、文字で四成分を区別できるようにする。
各色は固定物理閾値を越えた時だけ発光し、発光開始からfull値までをsmooth-stepで連続表示する。初期値はStrength `−42..−6 dBFS attack RMS`、Brightness `0.60..2.50 acum`、Transient `3..15 dB contrast`、Texture composite `0.10..0.65`。比較表示の最小差はそれぞれ`0.50 dB / 0.05 acum / 0.50 dB / 0.04`とし、曲内max、percentile、material依存normalizationを使わない。
TEXTUREは処理器を推定しない。sample-edge比、Crest、−3 dB peak plateauの固定観測からtexture-likeな状態だけを示し、`Saturation`という原因名へ置き換えない。

## Palette evidence

同じ値の増加が色によって不均一に見えないよう、paletteはScientific Colour Mapsの知覚均一性、順序性、色覚多様性の原則を採用する。Hypha固有色は同資料の色を直接転載せず、暗いpanel上でfull発光時の各componentが3:1以上になり、RGB chroma rangeが56未満へ落ちないgold/copper・cyan/teal familyとして固定する。
W3CのUse of ColorとNon-text Contrastに従い、色を唯一の識別手段にせず、四つの固定断面、Textureの連続組織、Transientのaura、明示labelを併用する。薄いgradient部分ではなく、輪郭とlabelを必要contrastの正本にする。

- Fabio Crameri, Scientific Colour Maps: https://www.fabiocrameri.ch/colourmaps/
- Crameri et al., 2020, *The misuse of colour in science communication*: https://www.nature.com/articles/s41467-020-19160-7
- W3C WCAG 2.2, Use of Color: https://www.w3.org/WAI/WCAG22/Understanding/use-of-color.html
- W3C WCAG 2.2, Non-text Contrast: https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast

## Why these four

人の音色認識ではattack timeとspectral centroidが主要な次元として繰り返し確認され、短い音ではattack temporal centroidがattack timeより知覚順序をよく説明した研究がある。
また、音のsalienceは絶対量だけでなく先行する背景とのloudnessやbrightnessの変化に依存する。
したがってHyphaでは「検出器の強さ」をそのまま明瞭度と呼ばず、直前との局所contrast、attack内のenergy位置、既存Sharpness、短時間Peak/Crestを別々に保持する。

- Caclin et al., 2005, *Acoustic correlates of timbre space dimensions*: https://pubmed.ncbi.nlm.nih.gov/16119366/
- Caclin et al., 2007, *Interaction between acoustic features in timbre perception*: https://pubmed.ncbi.nlm.nih.gov/17261274/
- Kazazis et al., 2021, *Effect of Temporal Envelope on the Timbre of Exponentially Decaying Sounds*: https://pubmed.ncbi.nlm.nih.gov/34852574/
- Huang & Elhilali, 2020, *Auditory salience using natural soundscapes*: https://pmc.ncbi.nlm.nih.gov/articles/PMC6909985/
- Angeloni et al., 2021, *Neural auditory contrast enhancement in humans*: https://pmc.ncbi.nlm.nih.gov/articles/PMC8307757/

## Fixed measurement contract

- Worker only。Audio Threadでは算出しない。
- 六秒scrubのabsolute waveformは10 ms binごとのRMS/Peakをsource 0 gridで保持し、ODFを波形として代用しない。
- 選択eventのshapeは`[event−100 ms,event+30 ms)`を96個の固定binへ写し、各binのstereo mean-linear-power peakを使う。
- sample rateはhost-native、monoまたはstereo。stereoはchannelごとのlinear power平均でdownmixしない。
- contextは`[event−100 ms,event)`、attackは`[event,event+30 ms)`。content先頭不足だけzero padする。
- レベルfloorは固定−120 dBFS。floorへ触れたContrastはpayloadで明示し、通常値と区別する。
- Shapeは各frameの`max(frame_power−context_power,0)`をweightとする時間重心。正の超過energyが無ければ未定義。
- Sharpnessはsource-grid上で30 ms attack全体を最初に含む100 ms窓を使う。窓が届くまで
  Brightnessはpendingとし、片側だけの値からΔを作らない。
- 単純な固定gainではContrast、Shape、Crestが不変で、Peakだけgain量に一致することをfixture化する。
- sample-edge比はattack内の一階差分power / signal power、peak plateauは最大frame周辺の−3 dB連続幅とし、固定gainで双方不変にする。

## Interaction

初期状態は最新eventをLIVE追従し、clickまたはdrag、Left/Right、Homeで一eventをLOCKする。
`NOW`またはEndでLIVE追従へ戻し、LIVEとLOCKは常に画面内へ明示する。これは視覚選択だけであり、Hyphaから音声を生成、加工、seekしない。
初期表示は主scrubをPRE/POST二段で大きく使う。明示ボタンで、同じ全領域を使う一段の重ね表示へ切り替える。小さな別comparison plotは置かない。
二段はPRE/POSTで同じpaletteと絶対scaleを使い、identityでは同じ形・色にする。
重ね表示はPREを細い連続trace、POSTを連続bodyにする。離散点や点線でPREを表さない。色は絶対値を二重表示せず、Strength差を金、Brightness差をアイスブルー、Transient差をteal、Texture差をcopperの連続膜で示す。
EVENT SHAPEは150%と200%で常設し、四つのcardを右側の二行へ置く。125%は四card、100%はoverviewへ段階的に縮退し、読めない装飾を押し込まない。
10 Hzの正本更新間は同じサンプル時刻を参照したまま100 msのsmooth-stepで横軸だけを補間する。transport世代変更、後方seek、無効状態では補間せず即時にsnapし、信号が止まっている間に自律的な呼吸や発光を作らない。
見た目から楽器名、原因、推奨操作を推測しない。
未対応event、欠測、floor-limited、Brightness pendingは輪郭または`---`で事実状態を保つ。

## ATTACK product-trial route

POSTの通常Analysis導線は最初にDRUM ATTACKを開き、同じ枠内でATTACK、FREQ、SHARP、LIVEを切り替える。
初回表示は細部を見落とさない200%とし、既存のsize操作で100%、125%、150%、200%を選べる。
環境変数はATTACKを直接開く実機検証shortcutとしてだけ残し、通常利用の必須条件にしない。
2MIXは別profileとして精度契約が確定するまでこの導線へ出さない。
ATTACKは既存のexact-pair optional-analysis leaseを再利用し、別の常時通信を増やさない。
POSTがATTACKを表示している間だけ10 HzでPREの六秒waveform、ODF、event detailを専用の固定上限payloadへ発行し、同じcontent sampleとdefinitionの系列だけを結合する。
PRE未接続ではPOST実測だけを`POST ABSOLUTE`として表示し、PRE波形を複製しない。
二段表示はPRE/POSTで同じ固定paletteと絶対scaleを使い、overview、EVENT SHAPE、四cardを同じ選択eventへ同期する。
重ね表示はPREを連続trace、POSTを連続bodyにし、選択EVENT SHAPEで四成分の差だけを各固定色で示す。
同一音声では形と差分色が一致し、色だけでPRE/POSTを識別させない。
