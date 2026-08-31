# Hypha ATTACK perceptual visual contract

## Decision

ATTACKは波形から楽器を推測する画面にしない。
SuperFluxはトランジェントの位置を決める内部検出器に限定し、利用者へは同じイベントがPREからPOSTでどう感じられやすくなったかを、四つの観測量として見せる。

| 表示 | 実測量 | 見せ方 |
|---|---|---|
| CONTRAST | 30 ms attack RMS − 直前100 ms RMS | 六秒timelineの符号付きstem |
| SHAPE | context power超過energyの30 ms時間重心 | 選択eventのPRE/POST位置、timeline cap幅 |
| BRIGHTNESS | 既存100 ms Sharpness | 選択eventのPRE/POST barとΔ acum |
| PEAK | 30 ms Sample Peak、補助Crest | 選択eventのPRE/POST barとΔ dB |
| TEXTURE | sample-edge比、Crest、peak plateau幅 | 三条件の差が同時に現れる箇所だけ微細な繊維 |

主stemの上はPOSTでアタックが直前の音からより際立った事実、下はPOSTで際立ちが弱まった事実だけを表す。
色や文言で良い、悪い、改善、劣化を示さない。
BrightnessとPeakを主stemへ重ねず、選択した一イベントの詳細へ分離する。
TEXTUREは処理器を推定しない。POSTで高周波方向のsample-edge比が増え、Crestが下がり、−3 dB peak plateauが広がった三事実を別々に保持し、同時変化だけを`Saturation-like texture`として表示する。

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
- Sharpnessが届くまでBrightnessはpending。片側だけの値からΔを作らない。
- 単純な固定gainではContrast、Shape、Crestが不変で、Peakだけgain量に一致することをfixture化する。
- sample-edge比はattack内の一階差分power / signal power、peak plateauは最大frame周辺の−3 dB連続幅とし、固定gainで双方不変にする。

## Interaction

timelineはeventのない時刻を線で結ばない。
click、Left/Right、Home/Endで一eventを選び、詳細のPREとPOSTを同じscaleで並べる。
二段表示はPRE/POSTへ同じpaletteと絶対scaleを使い、identityでは同じ形・色にする。重ね表示は共通形を中立色、Brightness差を形の内側のcool tint、Peak/Contrast差を外側のgold aura、TEXTURE三条件の同時差を細い繊維で示す。
見た目から楽器名、原因、推奨操作を推測しない。
未対応event、欠測、floor-limited、Brightness pendingは輪郭または`---`で事実状態を保つ。
