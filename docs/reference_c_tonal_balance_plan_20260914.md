# Reference統合実装計画 v4（Tonal Balance、聴取再利用、確認手順、PRE/POST Blind）

作成日: 2026-09-14。
改訂: 第4版の第2統合改訂。Tonal Balanceと聴取再利用の統合契約を維持し、Local PRE/POST Blind第2版の状態、診断、実機受入を組み込む。
状態: 実装計画。方式の実証は未完了であり、製品コードの実装、配布、DAWへの配置は今回の作業に含めない。
対象: MIX／MasteringのTonal Balance、聴きどころの再利用 → 今日の確認 → 比較しおり、ローカルPRE/POST Blind。

本書を統合計画の入口とし、既存リンクを保つためファイル名は変更しない。
第3〜10節はTonal Balanceの詳細、第12節は聴取手順との統合契約、第13節はLocal PRE/POST Blindとの統合契約を扱う。
[聴取再利用と確認手順の詳細](reference_listening_workflow_plan_20260914.md)は本書の構成文書であり、別の独立計画として競合する既定値を持たせない。
[PRE/POST Blindの詳細](hypha_pre_post_blind_usability_plan_20260914.md)も本書の構成文書とし、BL0〜BL4、BL-V01〜BL-V12、BL-U1〜BL-U3を統合計画の完成条件に含める。
共通の選択、保存、資源、履歴、工程の境界は本書を正本とし、聴取手順固有の操作とR試験は詳細文書を正本とする。
Local Blind固有の固定4秒Capture、診断、回答、Reveal、Return、GainMatchとの実機比較はLocal Blind詳細文書を正本とする。
三つの機能群は相互の着手条件にせず、共有sourceと実時間出力の共存検証を省略しない。

2026-09-14追加指示により、Reference通常A/B/CとReference Blindへ[比較機能の共通安全契約](hypha_comparison_safety_contract_20260914.md)を適用する。
制作設定、Undo、書き出し、中断後の再生に関する四条件は同契約を正本とし、Local PRE/POST Blindとも同じ最終sourceで検証する。

## 1. 利用者に届ける結果

MIXとMasteringのどちらでも、CのTonal Balanceを選ぶと、現在の音Aと選んだ比較曲Cの周波数バランスを同じ尺度で確認できる。
Kirin OSにあるジャンル分布も、同じ測定定義のまま表示基準として利用できるようにする。
低域から高域までの関係を見て、必要な帯域を詳しく確かめ、固定Loudness Matchで聴き直す流れを一つのCheck内に収める。

音声の選択は既存のA/B/Cを維持する。
AはDAW入力、Bは登録Version、CはCheckに割り当てた比較曲である。
ジャンル分布を表示してもCの音源を入れ替えず、BのVersion一覧や同曲比較にも干渉しない。

本計画ではCheck名の追加だけを完成条件にしない。
比較可能な測定、既存ジャンルデータの配信、設定保存、Cの描画、Capture Aとの整合、実機検証までを一つの完成範囲にする。
利用者側の完成条件には、同じ曲やCueの再選択、試聴開始の再クリック、範囲確認のための再Captureを要求しないことを含める。
第10節のU1〜U3で操作数と条件の理解を確認し、技術試験のpassだけで使いやすさを確認済みとしない。

聴取手順では、登録済みの曲とCueを別Presetへ再利用し、その回に選んだCheckを確認して、比較条件と本人のメモを次回へ残す。
確認状態を品質の合否や聴取の証明に変換せず、Tonalの分布も採点に使わない。
この3機能も統合計画の完成範囲に含め、Tonalだけの完了を統合計画全体の完了と呼ばない。

Local PRE/POST Blindでは、制作音を変更せずに固定した4秒のPRE/POSTコピーを比較し、準備済みの取り直し、短音の不成立理由、Pauseとofflineを含む中断、通常出力への復帰を一つの手順として扱う。
比較用Contextだけを同じprocessorとexact pairで再利用し、PCM、固定Gain、承認、Source割当、回答、再生許可は引き継がない。
Local BlindはKirin OSやWork接続を必要条件にせず、Tonalと聴取手順の保存schemaも変更しない。
BL4まで完了していない状態で、Reference側の完了を統合計画全体の完了と呼ばない。

## 2. 実装基点と確認済みの制約

| 対象 | 今回確認した基点 | 扱い |
| --- | --- | --- |
| Hypha計画の置き場 | /Users/nishiodaisuke/Dev/kirin_hypha、mainの9cb40e56 | 本書、聴取手順、Local Blind、共通安全契約の詳細文書を整合させる。製品実装の基点にはしない |
| Hypha Reference | /Users/nishiodaisuke/Dev/kirin_hypha_reference_abc、codex/reference-abc-delivery、B-887 53937c0da5916c7b771329e98fff79671fb5b22c | B-887のCapture操作直列化、ReferenceAnalysisOwner、表示投影を実装基点にする。DAW実機、Windows、両Blindを含む共存確認は未完了 |
| Kirin OS Reference | /Users/nishiodaisuke/Dev/kirin_os_reference_delivery、codex/reference-whole-song、W-3080 31fd330c4b297637bebe321d2d60d6bd59ff643f | Library 1.1、独立したBのVersions公開、CのLibrary配信を維持して変更する |
| 旧案のOS参照 | /Users/nishiodaisuke/Dev/kirin_sense_lens、27dee5b7d | 別worktreeの確認記録としてのみ残す。今回の配信設計と回帰試験の基点から外す |

実装開始時には両Reference worktreeのHEADと変更一覧を再取得し、組み合わせたexact commit、対象差分、試験結果を同じ実装記録へ残す。
HEADの移動だけで安全性を推定せず、観測、保存、Library、Versionsに関係する差分を読み直す。
この更新にmainや別worktreeのcheckout、merge、resetは含めない。
work.jsonのschema変更も本計画の前提にしない。

| 確認した現状 | 設計への反映 |
| --- | --- |
| Mastering先頭のTonal balanceは絶対Spectrum全域と低域を表示する | 項目を維持し、相対パワーの専用表示を接続する |
| MIX先頭のBalanceにはmusical priorityの目的がある | 楽器間バランスを残し、Tonal Balanceを追加する |
| ジャンルはkirin_multires_relative_power.v1、60帯域、P10/P50/P90。runtime bundleは38,594 bytes、6ジャンル | 検証済みbundleとresolverを使い、別corpusや分類器を作らない |
| 既存Reference Spectrumは64点の絶対振幅dBFS | 補間だけで60帯域の相対パワーに転用しない |
| VisualObservationはalignment、参照波形、参照音源decodeの成功に依存する | A単独の観測を先行分離し、Cの成立条件には同曲alignmentを持ち込まない |
| 既存Libraryは共通manifest内のPresetを一つでも読めないと更新全体を拒否する | 新bindingを旧配信へ混ぜず、旧新版を別namespaceで公開する |
| Captureは正常終了した新しい取得をheldへ置き換え、60帯域の履歴は持たない | 区間の確認ではCapture操作を呼ばず、新たに保持する帯域時系列から再集計する |
| 通常試聴の入口はVisualObservationの枠を即時解放してから同期的に試聴枠を取得する。既存の借用経路はCaptureを対象とする | A観測のownerから通常試聴への借用を明示し、退役待ちを同期的な開始失敗や再クリックへ転嫁しない |
| B-887はCapture、通常Reference試聴、両Blindの開始予約を同じPOSTで排他化し、Reference側の解析需要を一つのownerへ集約した | Local Blindは既存のBlind admissionを維持し、ReferenceAnalysisOwnerのshared grantへ混ぜない。物理2枠の合算と開始排他を別々に検証する |
| Local Blindは固定4秒Capture、固定Gain、完全聴取、回答、Reveal、明示Returnを持つ | 操作短縮でこれらを自動化せず、比較用Contextだけを非永続で再利用する |

先の「MIXのBalanceを単純にTonal Balanceへ置換する」と「Genre Contextを削除する」は採用しない。
既存ジャンルに対する「上、範囲内、下」は測定上の関係として使い、合否やEQ修正指示へ変換しない。

## 3. Check Presetの構成

| Preset | 冒頭の順番 | 役割 |
| --- | --- | --- |
| MIX | Tonal Balance → Musical Balance → Kick and bass → Vocal balance → Midrange → High end | 全帯域の分布、楽器間の優先関係、局所の確認へ進む |
| Mastering | Tonal Balance → Loudness → True Peak → Dynamics → Stereo and phase | 全帯域の分布を確認してから音量とダイナミクスを見る |

MIXの後続項目とMasteringのLow-end consistency、Section difference、Album contextは維持する。
MIXには既存catalogの`tonal_balance`を追加し、同じ意味のcatalog項目を新設しない。
新しいMIXのBalanceは表示名と説明を「楽器間バランス」と明確にし、既定は`audition_only`にする。
楽器の抽出やボーカル量の推定値は作らない。
この変更は新しいFactory revisionにだけ適用し、保存済みの利用者Presetを改変しない。

Tonal Balanceの計測定義と操作は両Presetで共通にする。
MIXだけ緩い判定、Masteringだけ厳しい判定といった閾値差を設けない。
工程の違いは説明文と利用者が登録する候補曲、Cueで表す。
新しい主表示bindingは`tonal_balance`とし、既存`spectrum_full`／`spectrum_low`の意味を変えない。
Tonal Balanceの中で全域と低域の詳細を扱い、既定では全域グラフと同じ内容の低域グラフを常設で重ねない。

## 4. Cの表示と操作

主表示は60帯域のAとCの曲線とする。
四広帯域の要約から詳細へ移れるようにし、確認中の帯域はPresetや音声選択と独立した表示設定として扱う。
帯域は既存Kirin OSと揃え、Low 20–250 Hz、Low-mid 250 Hz–2 kHz、High-mid 2–8 kHz、High 8–20 kHzとする。
実際のgroup集計は既存のband membershipと定義に従い、任意に区切り直さない。

通常はAと選択曲Cを表示する。
Kirin OSのCheck詳細から既存のジャンル分布を指定した場合は、その分布帯と名称を補助表示する。
ジャンル未設定でもA/C比較を使える。
Genre Contextという聴取項目と、Tonal Balanceの表示に使うジャンル指定は別の設定である。

- 凡例は下記の常設情報と詳細情報に分ける。AのLIVE／CAPTURED、Cの曲とCue、集計条件の違い、選択した分布帯の種類は、詳細を開かなくても識別できるようにする。
- CAPTUREDの波形範囲を変えたら保存済み帯域時系列を再集計する。範囲解除は全域要約へ戻し、Capture開始、保持データの置換、音声commandを発行しない。
- CのP10/P90は一曲内の時間変動、ジャンルのP10/P90は複数曲の分布と区別する。二種類の帯を常時重ねず、通常は中心線と選択した一種類の分布帯を表示する。
- 帯域を指すかキーボードで選ぶと、周波数、AとCの相対値、C−Aを表示する。LIVE窓対Cue集計など集計条件が異なる場合は双方の条件を併記し、同条件の差分やEQ補正量と呼ばない。PRE/POSTのΔとも区別する。
- ジャンルに対する関係は「範囲より上／範囲内／範囲より下」とし、上下の意味が混在するgroupを一つの良否へ丸めない。
- グラフの縦方向は相対パワーの共通尺度とする。曲線ごとの自動縦移動や見た目を揃える補正を入れない。
- 表示基準や帯域の変更は音声commandを出さず、選択中の曲と固定gainを維持する。
- Cの音声選択、Cue変更、終了と復帰は既存の状態機械を通す。通常試聴の枠借用と待機要求の扱いは第6節で拡張し、同期的な枠取得失敗をそのまま利用者の再操作にしない。
- Blind中は曲線、数値、分布、選択履歴、tooltip、accessibilityの音源情報を隠す。

300×200と375×250では既存A/B/C操作を優先し、四帯域の概要と選択帯域の値を同じ面積で切り替えて読む。
450×300、600×400、900×600では全域曲線と要約を配置し、常設の新しい下位タブを増やさない。
共通JUCE rendererを使うKirin OSのPreviewも同時に更新する。
CE2226の共通素材と意味色を使い、品質を赤黄緑で表さない。

### 常設情報と詳細情報

| 表示する場所 | 情報と操作 |
| --- | --- |
| 常設 | A/B/Cの音声操作、実際に鳴っている音、AのLIVE／CAPTUREDと全域／選択範囲、Cの曲とCue、LIVE一窓／Cue集計等の条件差、選択中の帯の種類とジャンル名、待機や欠測の事実と出口 |
| 帯域を1回選ぶと表示 | 周波数、AとCの値、C−A、帯域ごとの欠測。小サイズでは四帯域の概要と同じ領域を使い、概要への復帰も1操作とする |
| 条件詳細を1回開くと表示 | 要求範囲と採用窓の実範囲、plane別の窓終端と窓数、整数sample境界、rate、channel、method。共通の凡例から開き、閉じても曲、Cue、音声選択、表示範囲を維持する |

要求範囲に完全な窓が収まらない場合や、LIVEの更新が止まった場合は、その事実を常設側にも短く示す。
詳細へ移すのは検証のための内訳であり、現在値か保持値か、何と何を比べているか、どの操作を待っているかを隠すためではない。
長い曲名やCue名は省略表示から元の項目で全文を確認できるようにし、キーボードでも同じ詳細へ到達できるようにする。
Blindの非表示規則は、これらの詳細とaccessibilityにも適用する。

### ジャンル設定から同じ比較へ戻る動線

HyphaのTonal表示から1操作で、Kirin OSの該当Check詳細を開く接続動線を設ける。
新しい編集画面をHyphaへ複製せず、ジャンルの選択と明示保存はOSの正本で行う。
接続要求にはreceiver、namespace、Preset revision、Check IDと要求IDを含め、戻る操作は1操作とする。
戻り先を他POSTや現在開いている別Checkから推定しない。

保存後は、その明示編集に対応する新版revisionと表示設定だけを元のreceiverへ反映する。
Cの曲、Cue、固定gain、Captureの表示範囲を保持し、比較中のHistory contextとpublication_refは開始時のままにする。
編集要求との対応がない配信や再接続から、選択revisionの変更や音声commandを発行しない。
取消時は元の設定へ戻し、対象の削除、競合、receiverの終了時は別の対象へ適用せず理由を示す。
OS不在時はA/C比較を維持し、ジャンル設定が未完了であることとOSを開く出口を示す。
この往復は予定する新しい契約であり、既存の接続機能だけで成立済みとは扱わない。
今日の確認やしおりから開く場合は、第12節の固定した比較条件と表示設定の分離も適用する。

## 5. 測定と比較範囲の契約

### 相対パワーと解析窓

定義は既存の`10log10(band_power / same_aperture_20Hz_20kHz_power)`に揃える。
20 Hz–20 kHzの60等log帯域、channel power mean、Hann窓、三解析plane、silence gate、floor、欠測規則を一致させる。
要求窓長2.0／0.5／0.2秒、hop 0.5／0.2／0.1秒と既存のFFT丸め規則を使い、実FFT長とhopを整数sampleで保存する。
相対グラフに聴取用Loudness Matchのgainを加算しない。
既存64点Spectrum、20 Bark PSB、Crest、変更検知用4帯域索引は代替入力にしない。

解析窓の配置契約を、新しいscope contractである`retained_origin_grid.v1`として固定する。
観測対象の先頭sampleを起点Oとし、各planeの窓は`[O + kH, O + kH + N)`、kは0以上の整数、Hはhop、Nは実FFT長とする。
表示範囲`[s,e)`には、その中へ完全に収まる窓だけを含める。
Cueや表示範囲を変更しても起点を取り直さず、端の窓のpadding、部分窓の換算、別passの連結は行わない。

| 観測対象 | 起点と範囲 |
| --- | --- |
| C | 不変な音源PCMのsample 0が起点。Cueはこの窓列から対象窓を選ぶ条件であり、再解析の起点ではない |
| A CAPTURED | 実際に取得した最初のsampleをCapture内sample 0とする。host上の開始位置は別fieldへ記録する。部分選択でも同じ起点を保つ |
| A LIVE | 連続観測epochの最初のsampleが起点。seek、rate変更、queue欠落等で新epochにし、以前の窓を混ぜない |

receiptには観測対象ID、scope contract、起点の種類とsample位置、epoch、sample rate、channel、各planeのNとH、要求範囲、最初と最後の採用窓、窓数、帯域ごとの有効数を保存する。
GUIの秒表示や丸め値をsample境界の正本にしない。
窓がないplaneは欠測とし、他planeの成立を理由に値を補間しない。

### 同じ数値を要求できる条件

OS/Hyphaの0.05 dB一致は、同一PCM、rate、channel、起点、要求範囲、窓列、集計方式が揃ったfixtureに要求する。
任意位置から始めたAと、元曲の同じ秒範囲だけを選んだCを、起点確認なしに同条件として扱わない。
途中開始のCapture fixtureでは、そのCaptureと同じPCM列をOSにも渡し、両方の起点を0に揃えて独立計算する。
元曲artifactの切出しは別ケースとして残し、両者の窓列が異なることを検証する。

48 kHzの低域は実FFT長65,536 samples、hop 24,000 samplesになる。
元曲の0.03〜3.03秒を選ぶと窓開始は0.5、1.0、1.5秒の3個だが、0.03秒から新規取得したPCMでは0.03、0.53、1.03、1.53秒の4個になる。
この反例をT3の固定fixtureにし、起点の違いを隠して誤差だけ比較する試験を禁止する。
別曲A/Cの比較に同曲alignmentは要求せず、起点や曲内容が違うときは測定条件の違いとして表示する。

### LIVEと集計値

| 入力 | 表示する値 |
| --- | --- |
| A LIVE | 各planeの最新の完全な一窓。窓終端を保持し、未来sampleを使う中央窓を現在値と呼ばない |
| A CAPTURED | Capture取得全域、または明示選択した範囲内の窓から集計したP10/P50/P90 |
| C | Cue内の窓から集計したP10/P50/P90。Cue未設定時は既存の全曲Cue |
| ジャンル | 検証済み全曲集計による曲間分布。LIVEの一窓や短いCueと同じ時間条件とは表示しない |

時間方向の集計は、有効なf32帯域値を線形powerへ変換し、昇順に並べ、位置`q × (n − 1)`で補間してからdBへ戻す既存方式に揃える。
全域と部分範囲に同じ集計を使い、percentile同士の平均や固定容量の近似分布へ置き換えない。
ジャンルの曲間集計方式は別に保存し、時間方向の集計と同じ「中央値」という名前だけで共用しない。
無音、短すぎる入力、非finite値、低sample rateの上帯域、連続性欠落は既存の欠測規則に従う。

### 保持中Captureの部分範囲

新しいCaptureでは、全域要約に加え、実測した60帯域の窓ごとの値とvalidityを不変なローカルartifactとして保持する。
全曲PCMは保存せず、過去のAを現在のDAW入力やB/Cの測定から再生成しない。
範囲変更はartifactの読取りと再集計だけで完結し、`ACaptureSession::begin`や`ACaptureStore::commit`を呼ばない。
Capture ID、全域要約、既存の波形、変更検知索引、Bとのreceipt、保持中データのserialized hashは範囲変更前後で不変とする。
表示範囲の設定はCapture本体と分離する。

範囲内の窓が足りなければその値を欠測とし、全域percentileを部分範囲の値として表示しない。
範囲解除は保存済みの全域要約へ戻る。
解析jobにはCapture IDと範囲generationを結び、範囲連打、Capture切替、復元後に古いjobが現在の表示を上書きしない。
旧Capture、時系列の欠損、破損時は既存Captureを保持し、区間値だけを利用不可にする。
旧Captureに全域Tonal summaryも存在しない場合は、その値も欠測にし、従来のCapture表示だけを維持する。
明示的な区間操作には理由と「全域へ戻る」「保存データを読み込む」の出口を出し、取り直しを通常の確認手順にしない。

## 6. 観測と配信の責務

### A単独の観測

Hyphaに、参照曲の状態から独立したA観測の所有者を設ける。
既存VisualObservationから置換前入力の取得、連続性判定、admissionの所有を分離し、Bのdecodeと同曲mappingは比較側へ残す。
C音源、参照波形、alignment、ジャンル、OS接続の有無をA観測の開始条件にしない。
A側の開始条件はTonal表示またはCaptureによる観測要求、有効な入力設定、観測可能な実時間入力、既存Analysis枠の取得とする。
Cのdecodeやartifact検証が失敗しても、有効なA入力の計測を止めない。
OSライセンス、役割、offline、bypass、Blindの既存境界は維持する。

一つのreceiverでLIVEからCaptureへ進む場合は、一つのA観測所有者の枠を引き継ぐ。
Captureの窓起点は取得開始で新設するが、LIVEの窓をCaptureへ流用しない。
同じ入力を使う既存観測と新Tonal観測はworker側で分配し、audio callbackでの新しい二重コピーを前提にしない。
Capture終了、取消、restore、画面close、破棄では第8節のrunning→draining→releasedに従う。
保持済み値の閲覧だけではLIVE計測を常駐させず、終了処理中の枠と現在の音声出力を区別する。
Blindへ入ると表示用jobと公開snapshotを失効させ、既存の観測禁止条件を共有する。
Blind終了や画面再open時は現在の要求を再評価し、前epochの停止確認と枠の取得後に新epochを開始する。

### LIVE観測と通常B/C試聴の枠共有

同じreceiverではA観測のownerをAnalysis枠の寿命管理者とし、LIVE、Capture、通常B/C試聴を、その枠を使う別々のconsumerとして登録する。
通常試聴だけを行う場合も枠の寿命管理を通すが、Tonal計測の要求がなければそのworkerは起動しない。
Analysis枠の借用と、既存のproject／process単位の試聴排他権は別の契約とし、借用によって試聴の禁止条件を迂回しない。
Blindは通常B/C試聴の借用対象へ含めず、既存の専用許可と観測禁止条件を維持する。

| 操作と所有者の状態 | 枠の扱いと表示 |
| --- | --- |
| LIVE観測中に同じreceiverでB/Cを要求 | running ownerの枠を借用する。新しい枠の取得やLIVEの停止を必要条件にせず、既存の試聴許可と準備済み音源を検証して開始する |
| Capture中にB/Cを要求 | 同じownerから借用し、Captureの起点と取得を継続する。試聴のためにCaptureを終了、再開始しない |
| B/CからAへ戻る | 音声は既存の復帰commandとreceiptで戻す。試聴consumerの停止確認後に借用を外し、LIVE／Captureが必要ならownerの枠を保持する |
| B/C試聴中にLIVE表示やCaptureを要求 | 同じownerに観測consumerを登録する。Captureは実際の取得開始に起点を作り、試聴済みPCMやLIVE窓から生成しない |
| 旧ownerがdraining中にB/Cを要求 | 旧ownerへ新consumerを登録せず、有界の待機要求として保持する。停止ack後に現在の条件を再検証し、枠と試聴許可を得たときだけ開始する |

借用tokenはreceiver、owner generation、consumerの種別に結び付け、別receiverや退役済みownerへ使い回さない。
同じrunning ownerでの借用は所有権や観測epochの移譲ではなく、第8節の停止ackを省く理由にしない。
最後の観測consumerが止まっても、試聴consumerが生存中なら枠を返さず、逆方向も同様とする。
共有する全consumerの処理とRAMを第8節の合算予算へ含め、独立した3件目の解析処理を借用名目で追加しない。

試聴の待機要求はreceiverごとに最大1件とし、利用者が押したB/C、sourceとCueのreceipt、固定gain、selection generation、transport epoch、lifecycle tokenへ結び付ける。
待機中は現在の音を継続し、「試聴開始待ち」の事実と取消操作を同じ場所へ出す。
枠の解放通知後は同じ要求を非RTで再評価し、有効な要求の完了に再クリックやLIVE画面を閉じる操作を要求しない。
同じreceiver内では、この明示要求を任意のLIVE再開要求より先に評価するが、他receiverの有効な枠を奪わない。

Aへの復帰、待機の取消、曲／Cue／Check／Presetの変更、transportの停止や不連続、rate変更、bypass、offline、Blind開始、Editorのclose、restore、processorの終了で待機要求を失効させる。
新たなB/C操作は以前の要求を置き換え、旧要求を後から再生しない。
停止ack後も音源、固定gain、試聴許可、要求の同一性を再検証し、条件が変わっていれば開始せず理由を示す。
この待機要求はDAW状態へ保存せず、再openや再接続によって復活させない。
Historyの試聴開始は既存の音声receiptで確定し、待機に入っただけでは「聴いた」記録を作らない。

### Local Blindとの排他と解析所有権

Local PRE/POST Blindは通常B/C試聴の借用対象ではなく、B-887で接続されたReference controllerの予約と既存のBlind admissionを通す。
Reference Capture、通常Reference試聴、Reference Blind、Local Blindのどれかが開始予約を所有している間は、別の開始経路が独自の可否判定から割り込まない。
Local Blindの開始によって失効した通常B/Cの待機要求は後から再生せず、Reference側の音声選択とHistoryへ試聴済み事実を作らない。

Local Blindの4秒Captureと固定Gain解析は既存のBlind admissionを使い、ReferenceAnalysisOwnerの用途別grantとして登録しない。
Tonalの窓、Capture Aの帯域時系列、通常B/Cのdecode結果も入力へ転用しない。
両者は物理2枠の上限だけを共有し、Local BlindをReference側の借用と呼んで処理量や枠を隠さない。
Local Blindが`preparedDiscardPending`または通常復帰待ちにある間は、RT退役ackと音声receiptを満たすまで用途を解放済みと扱わない。
一方、音声の通常復帰はTonal保存、workflow保存、OS配信、診断表示を待たずに要求できる。
開始予約、解析枠、試聴排他、音声復帰は別々の事実として追跡し、一つのboolから全状態を推定しない。

Local Blindの一時Contextは同じprocessorとexact PRE/POST pairにだけ属し、DAW保存snapshot、Work、ReferenceChoices、workflowへ入れない。
Editor close/openでは条件が有効なContextだけを再表示できるが、processor再生成、project restore、動作中のstate restore、pairまたは通常Contextの変更で失効させる。
この保持からCapture、Gain承認、Source、回答、再生許可を復元しない。
詳細な状態遷移と診断は第13節から参照するLocal Blind詳細文書を正本とする。

### Cとジャンルの準備

Kirin OSはW-3080のSpectral Balance計測とresolverを使い、音源起点を維持したC/Cue用Tonal artifactを準備する。
既存artifactと同じidentity、起点、方式なら再利用し、Cue選択のためだけに窓を置き直さない。
音源のfile/PCM identity、Cueの整数sample範囲、第5節の窓receipt、band定義、集計方式をartifactへ結び付ける。
音源配信とoptionalな観測配信は別に進め、新Tonalの準備を理由に音源やBのVersions公開を待たせない。
Tonalだけの追加公開は音源receiptを書き換えず、第7節の観測indexで通知する。

Hyphaは同じ数値契約を独立実装し、OSのRustやJSコードをGPL repositoryへコピーしない。
ジャンルはOSが公開した検証済みruntime artifactを読む。
corpus、曲名、個別profile、非公開の元データをHyphaへ複製しない。
ジャンル指定はOSのCheck詳細へ明示保存し、現在のWorkや他POSTから推定しない。
既存canonical keyと検証を使い、runtimeのsupport = 5を収録曲数として表示しない。

## 7. 旧新版の保存と配信

### OS内部のPreset保存

旧OSが読む保存一覧へ新版Presetを登録しない。
配信用Libraryとは別に、OS自身が編集、設定、再起動に使うcanonical storageを旧新版で分離する。
以下の新pathとversionは実装予定の契約であり、現行製品に存在するものではない。

| 保存対象 | 旧形式のpathと契約 | 新形式のpathと契約 |
| --- | --- | --- |
| Preset本体 | reference/presets/<preset_id>/<revision_id>.v1.json、Preset 1.0 | reference/tonal-v1/presets/<preset_id>/<revision_id>.v2.json、Preset 2.0 |
| 一覧と設定 | reference/templates/index.v1.jsonとreference/settings.v1.jsonを従来schemaのまま維持 | reference/tonal-v1/state/head.jsonが、不変なstate snapshotのhashとrevisionを指す |
| 新state snapshot | 旧readerの対象外 | reference/tonal-v1/state/snapshots/<sha256>.json。一覧、新版default、旧revisionとの対応、旧側の確認済みrevision/hashを一組にする |
| 編集途中データ | reference/editor_drafts/<preset_id>.v1.jsonを維持 | reference/tonal-v1/editor_drafts/<preset_id>.v2.json。参照元namespaceとrevisionを含む |

旧形式のPresetを旧schemaのまま編集する場合だけ、既存の保存処理を使う。
Tonal追加やジャンル設定によって新版へ移る編集は、新revisionを新領域へ保存し、元の旧Preset、旧default、旧draftを書き換えない。
新版でのdefault変更は新版stateにだけ保存する。
保存処理、import/export、draft復元、IPCの入口で形式を判定し、UI側だけの振分けに依存しない。
新版fileを旧indexへ登録したり、拡張子だけv1にして旧writerへ渡したりしない。
新版編集を保存するときは旧版が元のrevisionを使い続けることを短く伝える。

初回移行は旧保存物の検証済みsnapshotを読み、旧Preset IDとrevision/hashを新stateの対応表へ記録する。
新しいTonal既定値を保存済み利用者Presetへ自動適用しない。
新Presetとstate snapshotを先に検証保存し、既存の単一writer権限と期待revision/hashを再確認してから新headだけをatomicに更新する。
中断した移行は公開済みheadから再開し、旧領域を修復名目で削除、置換しない。
旧新版を同時起動した場合も、既存のwriter権限を得ていないプロセスは保存、移行、配信を行わない。

旧OSへ戻れば旧領域だけで起動、編集、配信できる状態を保つ。
再び新版OSを開いたときは、旧indexと設定のrevision/hashを対応表の前回値と照合する。
新版側に派生編集がない旧Presetの変更は、検証後に旧形式のまま一覧へ反映できる。
旧側と新版派生の両方が変わっていれば双方を保持し、次の明示編集時に差を示して選択させる。
旧側の削除や旧default変更から、新版派生の削除や新版defaultの変更を推定しない。
古い対応表を根拠に削除済み旧Presetを復活させない。

### Libraryの並行配信

接続するreaderの一つに合わせて共通manifestを切り替えず、Reference transport root内に旧新版の固定namespaceを持つ。

| 配信 | transport rootからの相対path | 契約 |
| --- | --- | --- |
| 旧版 | library/manifest.json | W-3080のLibrary 1.1とPreset projection 1.0。旧canonical storageの現在の有効一覧とVersionsを公開する |
| 新版 | library/tonal-v1/manifest.json | Library 2.0とPreset projection 2.0。旧保存物の検証済み参照と新版stateから構成し、Tonal viewとジャンル設定を表現する |
| 新版観測 | library/tonal-v1/observations/manifest.json | 観測index 1.0。source identity、Check、Cue、methodに対応する不変artifactのreceiptを更新する |
| 新版の稼働証明 | library/tonal-v1/presence.json | 既存presenceのproducer sessionと、新版headのrevision/hash、入力stateのreceiptを結び付ける。旧presenceのschemaは変えない |

各namespaceは独立した単調増加revisionと本文hashを持つ。
不変artifactを先に書き、検証後にそのnamespaceのheadだけをatomicに公開する。
過去manifestは旧版ならlibrary/manifests/<revision>.json、新版ならlibrary/tonal-v1/manifests/<revision>.jsonへ不変保存する。
同revision別本文、rollback、path逸脱、symlink、過大入力を拒否する。
途中終了で片側だけ更新されても、それぞれが完全な公開物として読めることを要求する。
観測indexの遅延や破損は観測だけへ閉じ、Library全体や成立済みの試聴を失効させない。

旧経路には新bindingやfieldを混ぜず、未知fieldやCheckを削って旧形式を偽造しない。
新版専用Presetは旧一覧へ公開せず、旧defaultは旧形式の有効Presetを指し続ける。
互換用の対応表よりも現在の旧canonical storageを優先し、旧OSで行った編集や削除を配信時にも保持する。
変更不能な旧Hyphaが新機能用の通知を表示できるとは仮定せず、新版専用変更の配信範囲はOS側の明示操作時に伝える。

新版Hyphaは、稼働中OSのproducer session、presenceが指すheadのhash、入力stateのreceiptが一致する新版経路を優先する。
新OSの再起動時に、新presenceだけで前sessionの残存headを現在の配信と認定しない。
旧OSへ戻った場合は旧presenceとの不一致を検出し、旧経路を既存1.0／1.1の条件で読む。
OS停止中は最後に検証した公開物をofflineとして保持する。
再接続、fallback、optional観測の到着だけで音声commandを発行しない。
選択とrevision追跡はreceiverとnamespaceに分け、新版だけにある選択を旧Presetへ勝手に置換しない。

### Presetと表示設定の同一性

Factoryは明示的なstable ID表へ整理し、既存CheckのIDを保持したまま新規Tonal Checkへ新IDを割り当てる。
MIXとMasteringには新しいFactory revisionを発行し、同revisionの本文とhashを差し替えない。
旧Factory、利用者Preset、Work snapshot、Historyへ新しい既定値を自動適用しない。
Preview requestとPresetのimport/exportも新viewを表現するversionとclosed schemaを持つ。
Checkのジャンル設定を音源Candidateやaudio comparison modeに混在させない。
個々のPOSTの選択と表示設定はreceiver単位で保存し、他POSTのB、C、Work bindingへ伝播させない。

### Historyの配信識別

この項のevent契約は通常Library比較を対象とし、聴取手順の一時比較は第12節の別journalで扱う。
新版のLibrary履歴eventはversion 2.0とし、`publication_ref = { namespace, revision, sha256 }`を持つ。
namespaceは許可したlegacyとtonal-v1だけとし、pathはこの値から決定する。
番号だけでmanifestを探したり、現在のheadで過去eventを検証したりしない。
新版eventはlibrary/tonal-v1/events/<runtime_instance_id>/<event_id>.jsonへ不変保存し、旧event 1.0／1.1のlibrary/events配下へ混ぜない。
16 KiBのevent上限を維持し、新しいreceipt分も含めた最大eventを検証する。

新版Hyphaが旧経路で比較を開始した場合は、従来event形式と旧保存先を使う。
新版経路で開始した比較は、その後に旧OSへ戻っても新版eventと元のpublication_refで終了する。
開始時のcontextにnamespace、manifestのrevision/hash、PresetまたはVersionのreceipt、source、Cue、比較条件を固定し、途中の再配信で差し替えない。
start/completionとnote改訂の対応ではpublication_ref全体を照合する。
Historyの内部keyと重複判定にもnamespaceを含め、旧revision 10と新版revision 10、同じruntime/event IDを別経路のものとして区別する。

新版OSは旧新版のjournalを別々に検証してから一覧へ統合する。
新版eventは該当namespaceの過去manifestをhashまで検証し、その中のPresetまたはVersion receiptから表示を解決する。
旧eventにnamespaceや当時のmanifest hashがない場合は、旧保存先からlegacyと確定し、従来の検証を維持する。
読取り時に計算した旧manifest hashを、event作成時に保存された証拠とは呼ばない。
hash不一致、未知namespace、archive欠損、片側event欠損は当該eventだけを保留し、他経路を検索して合いそうな履歴へ結び直さない。
旧OSは新版履歴を表示できないが、新版journalを壊さず、新版へ戻したときに読めるようにする。

### Captureの埋込み情報と帯域時系列

DAW状態には既存Captureと、version付きの別XML要素に入れたTonal保存状態を持たせる。
既存ReferenceChoicesのversion = 1とACaptureのdata属性は従来形式を維持する。
Tonal追加要素はCapture IDと取得内容のhashへ結び付け、後付けのB receiptや表示設定はこのhashの対象から外す。
既存blobを拡張せず、旧readerは新要素を無視して従来のCaptureを読める構造にする。
旧版で再保存するとTonal追加情報が落ちる場合があるため、旧版経由のround tripは保証せず、元の保存データから復元可能にする。

帯域時系列はHypha所有のcapture-tonal-v1/artifacts/<sha256>.binへ保存する。
artifactはf32帯域値、validity、窓列、method、Capture ID、取得範囲を持ち、全曲PCMは含めない。
これは削除可能なcacheではなく、保存済みCaptureの区間確認を支えるデータであり、OSのLibrary writerは変更しない。
初回作成時のreceiver情報と復元先のruntime所有権を区別し、別POSTへの復元だけでCapture IDや取得内容を変更しない。

### Capture終了時の確定とDAW保存

base Captureの保持とTonalの確定を別の状態で公開する。
下表の状態名は内部契約であり、そのままUIの常設ラベルにはしない。

| Tonal状態 | 保存する追加情報 | 区間表示と復元 |
| --- | --- | --- |
| none | Tonalなし | 旧Capture等。従来のCapture表示を維持する |
| pending | Capture ID、取得内容hash、method、期待する取得終端、recovery key。完成artifactのreceiptは持たない | Tonalは保存処理中。base Captureは保存でき、区間を確定値として表示しない |
| ready | 全域要約と、検証済みartifactのreceipt | 同じ取得内容に結び付く区間を表示、復元できる |
| unavailable | 同じCaptureの識別と失敗理由 | base Captureを保持し、Tonalの保存未完了または失敗を明示する |

終了時の順序は次のとおりとする。

1. base Captureの確定時に、同じCapture IDと取得内容hashを持つpendingを一組の保存snapshotとして公開する。既存Captureのdirty通知を出す。
2. Tonal workerは受理済み終端まで処理し、writerが全窓とvalidityを永続化する。完全性、取得終端、hashを検証したartifactと、その取得に結び付くsealed記録を保存する。
3. sealed artifactから全域要約を集計し、readyのcommit記録を不変保存する。recovery headをその記録へatomicに進める。
4. 現在のCapture ID、取得内容hash、state generationが一致する場合だけreadyの保存snapshotへ切り替え、Tonal専用のdirty通知を追加する。Editorが閉じていても通知する。

各公開snapshotはbase blobとTonal状態を一組の不変オブジェクトとして持ち、異なるCaptureの要素を別々に読み合わせない。
聴取手順との共存時は、この一組を第12節の共通保存snapshotに収め、workflowのcheckpoint更新で巻き戻さない。
getStateInformationは公開済みsnapshotだけを取得し、queue drain、hash、encode、ファイル読取りをその場で開始しない。
setStateInformation直後の再保存は、検証待ちの受信済みbaseとTonal追加要素を有界のまま保持し、非同期decode前に落とさない。
壊れた追加要素はTonalだけ拒否し、正常なbase Captureを消さない。
dirty通知はprocessorの有効なlifecycle tokenに結び付け、破棄済みprocessorや古いgenerationへ遅延callbackを送らない。
DAWへのdirty通知は自動保存の完了を意味しない。

### pendingからの復元と終了境界

recovery keyはCapture ID、取得内容hash、methodから決定し、frame数だけが同じ別Captureへ一致させない。
capture-tonal-v1/recovery/<recovery_key>/head.jsonから、hash付きのsealedまたはready記録を解決する。
sealedは期待終端までの帯域時系列が永続化済みであること、readyは全域要約まで検証済みであることを示す。
書込み途中の長さだけでsealedと判定せず、完成receiptを作る前に永続化と再検証を済ませる。
復元jobも第8節の解析枠を取得し、同じrecovery keyの同時復元はwriter claimで重複実行を防ぐ。

| pendingの保存後に終了した位置 | 再open時の処理 |
| --- | --- |
| ready記録あり、DAWはpendingのまま保存されている | keyとhashを検証し、音声再取得なしでreadyへ復元してdirty通知 |
| sealed記録あり、全域要約の確定前 | 保存済み帯域値だけから集計を完了し、readyへ復元 |
| sealed前にプロセス終了、末尾未永続化、記録破損、別PCでrecovery欠損 | unavailableを公開してbase Captureを保持。未保存の窓を推定せず、Tonal未完了を表示 |
| 新Capture、別restore、取消が先行した後に旧jobが完了 | 旧Captureの不変保存物は保持できるが、現在のsnapshotとdirty通知を上書きしない |

sealed前の強制終了から完全なTonal復元は保証しない。
その境界を「保存済み」と表示せず、再取得なしの復元保証はsealed以後とする。
未完了ファイル、孤立したartifact、ready記録だけを見つけた場合も、保存されたCaptureのkeyと取得内容の照合なしに採用しない。
成功、失敗、取消の各終端でもbase Captureの既存置換規則を変えず、Tonalの都合で過去heldを消さない。

### Capture保存データの持出し

同じPCの保存復元ではreceiptとrecovery keyから自動解決し、利用者にpathを再入力させない。
別PCへの持出しは、Capture状態と参照artifact、必要なcommit記録をまとめる書出し／読込みで扱う。
pendingの書出しをreadyの完全bundleとして完了させず、sealedから確定できる場合は非RTで完了後に書き出す。
sealedがなければTonalを含む完全な書出しは未完了と伝える。
読込みはhash、Capture ID、取得内容、長さ、方式、path containmentを検証してからローカルへ保存する。
DAWプロジェクトだけの移動では完全な区間復元を保証せず、欠損時も埋込み済み全域要約とbase Captureは保持する。
確定保存物をcache掃除や現在のPOST数だけで自動削除しない。
削除は対象Captureを示す明示操作とし、古いDAW保存が参照している可能性を隠さない。

## 8. 実時間処理と合算資源予算

Audio Threadは置換前入力の事前確保済みbufferへのコピーと通知に限る。
FFT、集計、hash、JSON、メモリ確保、lock、ファイルI/Oはworker側で行う。
Aへの音声復帰は既存の音声commandとreceiptで進め、計測、decode、配信、ディスクI/Oの終了を待たせない。
解析枠は音声復帰と別の所有権とし、対応する実処理が止まるまで返さない。

### 解析枠の退役

| 所有者の状態 | 新しい仕事 | 枠と結果の扱い |
| --- | --- | --- |
| running | 有界の要求だけを受理 | 既存Analysis枠を所有して処理する |
| draining | 新規受付を停止し、取消または確定すべき終端処理だけを進める | 枠は保持する。新しい表示への結果公開は失効させ、実行中jobの停止を待つ |
| released | 同じ所有者では開始しない | 全consumerの停止確認後に枠を一度だけ返す。次の要求は新しい所有者として取得する |

generationの更新だけでは停止確認としない。
実行中のFFT、集計、I/O、worker間の受渡しが終了し、旧jobが再開して処理や公開を行わないことをacknowledgementで確認する。
停止待ちは専用ownerが引き受け、audio callbackとUI操作からjoinしない。
取消済みの待ちjobは除去し、短いchunk境界で取消を確認する。
タイムアウト、画面close、processorの再作成を理由に、生存jobの枠を空きとして扱わない。
詰まったI/Oで停止未確認ならdrainingを維持し、他の有効枠と通常Aを継続する。

Editorやprocessorを破棄する場合も、退役ownerはjobと枠の寿命を保持し、破棄済みのUIやprocessorへ参照を残さない。
wrapperのunload境界で実行中コードやcallbackを残さない終了手順をG0で実証する。
枠の移譲は停止ack後にだけ行い、旧epochと新epochを一つの枠で同時に走らせない。
第6節の借用では所有者を変えず、試聴consumerの出力終了receiptと非RT処理の停止も解放条件へ含める。
CaptureのTonal確定処理もdraining中の仕事に計上し、停止後の「後片付け」という名で枠外へ逃がさない。
一方、定常動作で同じreceiverのLIVEとCaptureを引き継ぐ際は、停止と窓起点の切替を確認した上で同じ枠を保持できる。

保持artifactの再集計は、計測中なら同じownerの有界jobとして扱い、停止中なら既存枠を取得する。
同じownerの共有は処理量を無料とする意味ではなく、既存Captureを待たせない実行順と合算CPU上限を満たす場合に限る。
runningとdrainingの両方を数え、既存の2枠に対して3枠目を作らない。
枠不足時は、現在の有効表示をその対象範囲のまま保持し、新しい範囲の値と偽らず要求の待機を表示する。
枠はwrapper内のstaticだけで数えず、既存の共有leaseで混在wrapperからも判定する。
同じreceiverでの試聴開始待ちは解析jobの待ち行列と分け、新TonalのFFT、集計、保存I/Oを枠外で始める理由にしない。
既存の試聴音源のdecodeと準備は従来の許可境界を維持し、新Tonalの待機を開始条件へ持ち込まない。

RT入力queueとworkerへの受渡しbufferは合計2 MiB以内に収める。
consumer別のgenerationと有界mailboxを使い、遅いTonalやディスク処理が既存Captureを待たせない。
Tonal側の受渡し欠落は当該窓の欠測または当該取得のTonal保存失敗として扱う。
音声継続とbase Captureに対する失敗条件は、Tonal表示の都合で厳しくしない。

### DAW保存データ

64 KiBのTonal追加上限は撤回する。
以下は圧縮を前提にしない配分であり、完成したplugin stateの実encoder出力を最終判定に使う。

| 内訳 | 上限bytes |
| --- | ---: |
| 既存Captureの宣言上限 | 988,172 |
| Tonal状態、全域要約、receiptまたはpending情報、表示設定のencode後増分 | 16,384 |
| 聴取手順のmode、戻り先ref、checkpoint等のencode後増分 | 4,096 |
| その他の既存plugin stateと外側XML等のための追加予約 | 32,768 |
| 統合時の合計 | 1,041,420 |
| 1 MiBまでの未配分余裕 | 7,156 |

16 KiBはraw structの大きさではなく、base64、文字列escape、field名、paddingを含む保存後の増分上限とする。
既存metadataと追加予約を区別して実測し、実際の非Capture状態が予約を超えるならG0で形式を改訂する。
既存Captureの最大取得時間、波形、索引、receipt、他の設定を削って予算を満たさない。
最大Capture、最大Tonal、最大聴取手順、最大既存設定を同時に入れたDAW状態で1 MiB未満と、欠落なしの読戻しを要求する。
聴取手順の4 KiBには戻り先と保存参照のescape等も含め、32 KiBの既存state用予約を両機能で二重計上しない。
合計は算術上の配分であり、実encoderで達成した容量とは扱わない。

### 帯域時系列とworker

1 Captureの確定したTonal保存物は、時系列artifactとrecovery記録を合算して16 MiB以内にする。
headerとindexは合計64 KiB以内、sealed/ready記録とrecovery headは合計32 KiB以内とする。
既存のplane別band数18／12／30とhopを使い、2時間の窓数上限を14,401／36,001／72,001とする。
validityは各planeの全窓×帯域を連続bit列として保持し、plane全体でbyteへ切り上げる。窓ごとのbyte paddingは含めない。
f32帯域値11,405,040 bytesとvalidity 356,409 bytesの合計は11,761,449 bytesになる。
headerとindexに64 KiB、recovery記録に32 KiBを加えても、16 MiBまで4,917,463 bytesの余裕がある。
同一filesystem内のrenameでstagingを確定し、同じpayloadの確定コピーを二重に保持しない。
書出しbundleの作成中に必要な別領域も空き容量検証の対象にする。
rateごとの整数hopと実窓長で再計算し、この値を全rateの実証結果とは扱わない。
容量不足、書込み拒否、途中終了はTonal artifactへ閉じ、確定した既存データを自動削除して空きを作らない。

追加Tonal RAMは同じreceiverの所有者あたり32 MiB以内とし、FFT、作成中、保持、restore待ち、pending受信state、recovery検証、公開snapshot、I/O buffer、encode一時領域を合算する。
退役ownerの残存RAMを次のownerの予算から隠さず、running/drainingを含む全体peakを測る。
既存Captureの16 MiB予算とは別に計測し、両者と共有queueを重複なく含む全体peak RSSも報告する。
共存時は聴取手順の追加8 MiB／POSTも測定対象に含める。
Local Blindの非永続Contextと診断は1 POST当たり4 KiB以内とし、DAW保存予算には加算しないが、同時に生存するprocessor、prepared trial、退役待ちを合算peak RSSへ含める。
Local Blindのための追加PCM、常駐thread、新しい解析枠は0とし、既存4秒PCMと既存Blind admissionを使う。
各所有者の個別上限だけで合算passとせず、読み込み、保存、再集計と退役ownerが重なる実際のpeakを記録する。
時系列全体を作成中と保持中の両方でRAMへ複製せず、有界I/Oと帯域単位の作業bufferを使う。
全域と区間のpercentileは保存値を帯域単位で読んで正確に集計し、近似方式で0.05 dB基準を緩めない。
I/Oを伴う集計は1所有者あたり実行1件、待ち1件までとし、範囲連打は最後の要求へ置き換える。
snapshot公開は10 Hz以下、静的pathは値または寸法が変わった場合だけ再構築する。

G0では追加worker時間を100 ms入力あたりp95 1 ms以下（48 kHz stereo）、他の対応rateでも実時間の10%未満とする初期基準を実測する。
callbackの追加alloc、lock、I/Oは0、音声dropout追加0を要求する。
予算不成立の場合は同じ計画内で方式を改訂し、実機検証前に上限を未定のまま残さない。

## 9. 影響するファイルと責務

OSとHyphaのパスは第2節のReference worktreeを基準とする。
新規モジュール名は実装候補だが、分離する責務と検証範囲は以下で固定する。

| 所有 | 対象 | 変更内容 |
| --- | --- | --- |
| OS Check | referenceWorkspaceFactoryCatalog.mjs、referenceWorkspaceCheckViewBindings.mjs、referenceWorkspaceCheck.mjs、Preset/schema群 | 共通Tonal view、MIX追加、stable ID、新revision、形式別の検証 |
| OS 保存 | referenceWorkspaceTemplateRepository.mjs、referenceWorkspacePresetRegistry.mjs、referenceWorkspaceGlobalSettings.mjs、referenceWorkspaceEditorDraft.mjs、src/port/reference/ipcRouter.cjs、新referenceTonalStateRepository.mjs | canonical storageとdraftの旧新版分離、atomic state head、非破壊移行、旧OSでの編集との照合 |
| OS 設定UI | ReferenceCheckEditor.jsx、ReferenceWorkspaceScreen.jsx、referenceWorkspaceSupport.js、locales/{ja,en}.json、src/port/reference/ipcRouter.cjs、関連request／response schema | 明示ジャンル、目的、旧版への配信状況、移行表示、receiverとCheckを指定した編集往復、取消と競合 |
| OS 数値 | native/src/spectral_balance.rs、spectralBalanceContract.mjs、readSpectralBalanceModel.mjs、genreSpectralBalanceReference{,Contract}.mjs | 起点保持、整数範囲、同じ窓列の集計、fixture。既存MEASUREの意味は変更しない |
| OS 配信 | referenceLibraryDelivery.mjs、referenceLibraryVersions.mjs、referenceWorkspaceSourcePreparation.mjs、referenceWorkspaceRuntimeMeasurement.mjs、src/port/reference/libraryService.mjs | 旧新版head、入力state receipt、producer session、独立観測index、BのVersions |
| OS History | referenceLibraryHistory.mjs、referenceWorkspaceRuntimeEvent.mjs、referenceWorkspaceHistoryDisplaySnapshot.mjs、関連schema | 新版event 2.0、publication_ref、namespace別archive解決、旧新版journalの統合 |
| OS Preview | ReferenceHyphaPreview.jsx、referenceHyphaPreviewRenderer.cjs、Preview schema/test | 新binding、scope、欠測、常設／詳細の情報区分、共通rendererへの受渡し |
| Hypha A観測 | PluginProcessorReference.cpp、ReferenceVisualObservation.*、ReferenceVisualBinding.cpp、ReferenceACaptureSession.*、新ReferenceAObservationOwner.*、crates/kirin_measure/src/analysis_lease.rsとreference_capture_admission.rs | A観測の先行分離、LIVE/Capture移譲、draining owner、停止ack、既存leaseの解放境界 |
| Hypha 試聴許可と遷移 | ReferenceComparisonController.*、ReferenceRuntimeV2Controller.*、ReferenceRuntimeV2NormalSelection.cpp、crates/kirin_hypha_ffi/src/audition_admission_ffi.rs、既存ABI/header、新ReferenceAObservationOwner.* | LIVE／Captureと通常B/C試聴の枠借用、既存排他権との分離、単一の待機要求、取消と再検証、A復帰後の解放条件 |
| Hypha Local Blind状態 | PluginProcessorLocalBlind.*、PluginProcessorPairing.cpp、local_blind/LocalBlindProductSession.*、LocalBlindPreparation.*、LocalBlindCaptureService.*、新しい非永続Context owner | B-887の開始排他と既存Blind admissionの維持、同じprocessorとexact pairに限定したContext、preparedDiscardPending、RT退役ack、Pauseと復帰 |
| Hypha Local Blind診断 | crates/kirin_measure/src/reference_gain.rs、crates/kirin_hypha_ffi/src/reference_gain_ffi.rs、kirin_hypha_reference_ffi.h、HyphaLocalBlindFailureText.h | 既存Gain算式を変えない診断付きABI、全Rust errorのreason code写像、世代付き結果、原因別の修復出口 |
| Hypha 共通出力とhost通知 | PluginProcessorAudition.cpp、PluginProcessorState.cpp、HyphaCaptureStateNotification.h、通常Referenceと両Blindのrestore／return経路 | 共通安全契約CS1〜CS8、offlineとbypassの最初のbuffer、一時試聴と保存snapshotの通知分離、旧要求の自動再生拒否 |
| Hypha History | ReferenceRuntimeEventTransport.*、event contextの生成箇所、関連schema/test | 開始時publication_refの固定、新旧event保存先、遅延completionと再接続の同一性 |
| Hypha 共通保存と一時選択 | PluginProcessorState.cpp、ReferenceComparisonController.*、ReferenceComparisonSettings.h、新ReferenceWorkflowState.*と保存snapshot組立て責務 | 第12節の通常選択と戻り先の分離、base Capture＋Tonalとworkflowの領域別更新、restore時の全体失効、相互巻戻し防止 |
| Hypha 計測とABI | crates/kirin_measure/src/reference_tonal*.rs、crates/kirin_hypha_ffi/src/reference_tonal_ffi.rs、既存ABI/header | 独立FFT、窓receipt、有界snapshot、正確な範囲集計 |
| Hypha Library | ReferenceLibraryRepository.*、ReferenceRuntimeV2Repository.*、ReferenceRuntimeRepositoryParsing.*、ReferenceRuntimeV2Measurement.* | namespace別reader、新版Preset、producer検証、観測だけの失効、旧版fallback |
| Hypha Capture | ReferenceComparisonSettings.h、ReferenceACapture{Model,Codec,Store,Session,Restore}.*、PluginProcessorState.cpp、HyphaCaptureStateNotification.h、新ReferenceCaptureTonalArtifact.*、ReferenceCaptureTonalCommit.*、ReferenceCaptureTonalRange.*、ReferenceCaptureTransfer.* | 一組の保存snapshot、pending/sealed/ready、recovery head、追加dirty通知、generation、非破壊区間と持出し |
| Hypha 表示 | PluginEditorReference.cpp、HyphaReferenceComponent.*、HyphaReferenceLayout.cpp、HyphaReferenceVisuals.cpp、新HyphaReferenceTonalView.*、編集往復のrequest／response境界 | A/Cの凡例、常設／詳細の区分、試聴開始待ちと取消、範囲、分布、欠損時の出口、OSの該当Checkへの往復、Captureデータの書出し／読込み、全サイズとBlind |
| Hypha Local Blind表示 | PluginEditorLocalBlind.cpp、HyphaLocalBlindComponent.*、HyphaLocalBlindPresentation.*、HyphaLocalBlindAdmissionText.h、HyphaLocalBlindFailureText.h | 準備済みの取り直し、固定コピーと現在音の区別、出力状態、診断、秘匿、通常復帰、全サイズ |
| 検証 | ReferenceCaptureStorageBudgetTest.h、両repoのReference/native/schema/UI試験、Local Blind native／UI試験、実装記録 | 第10節T1〜T10とU1〜U3、BL-V01〜BL-V12、BL-U1〜BL-U3、CS1〜CS8、最大状態、障害注入、同じ最終sourceの実機と初見試行 |

既存500行超ファイルへ製品変更を入れる場合は、変更する責務だけを先行して500行以下へ抽出する。
既存巨大ファイルの行数を増やさず、縮小したbaselineは同じコミットで下げる。
無関係な責務の一括分割を着手条件にしない。

## 10. 実装順序と受入試験

以下のG0〜G4はTonal工程である。
聴取手順はWG0〜WG4とし、共通部分の証跡と統合完了の判定は第12節で束ねる。
Local PRE/POST BlindはBL0〜BL4とし、詳細な順序と判定は構成文書、三機能群の共存と統合完了は第13節で束ねる。

| 工程 | 作業 | 終了条件 |
| --- | --- | --- |
| G0 基点と境界の実証 | 両repoの基点固定、数値と保存とworkerの試作、枠借用とwrapper終了、保存とHistoryの契約モデル、小サイズ画面とU1〜U3の動線試作 | 下記G0-N／E／R／L／M／Uをすべて満たす。実encoder、実worker、停止ackとunloadは実行可能な試作の証跡を必須とし、契約モデルのpassで代替しない |
| G1 観測分離と互換契約 | A観測、通常試聴との枠共有、待機要求と取消、停止ack、canonical storage、Library、stable ID、producer識別、Historyを実装 | Tonal表示なしの既存A/B/Cと、表示ありで2枠使用中のLIVE→B/C→Aが回帰pass。T1/T2/T7/T9/T10が実装に対してpass |
| G2 数値とCapture保存 | OS観測index、三plane計測、pending/sealed/ready、recovery、区間集計、持出し、dirty通知を実装 | 実OS出力の読取り、T3/T4/T5/T8、既存Captureの最大条件がpass |
| G3 共通UI | A/C/genre、常設／詳細の区分、保存処理中と失敗の区別、試聴開始待ちと取消、範囲変更、OS設定との往復、全サイズ、Preview、Blind | U1〜U3を実データと初見試行で確認。操作上限を満たし、区間確認でCaptureを置換せず、旧新版の配信範囲、保存状態、持出しを確認できる |
| G4 全体回帰と実機 | 対象試験後に全体suite、同じ最終版でStudio One／Pro ToolsとWindows | T1〜T10、U1〜U3の確定動線の回帰、RT境界、保存復元、両platform実機がpass。未検証を完成扱いにしない |

全工程を本機能の実装完成範囲に含める。
G0不合格時は失敗fixtureを残して本計画を改訂し、精度を下げる、範囲確認を取り直しへ戻す、別Phaseへ送ることで合格扱いにしない。

### G0で必要な証跡の区分

G0の試作は方式を検証するためのものであり、製品への統合、利用者のDAWへの配置、公開を意味しない。
実行環境、両repoのexact commit、試作差分のhash、入力fixture、コマンド、期待値と実測値を記録する。

| ID | 必須の証跡と終了条件 | モデルで代替できない部分／後続工程で残る部分 |
| --- | --- | --- |
| G0-N | OSとHyphaの独立計算を実行し、T3の同一起点fixtureで0.05 dB以内、窓列と欠測bitmapの一致、3窓対4窓の条件差を確認 | 計算式の検算だけでは不可。実製品の観測入口からのT3全件はG2で確認 |
| G0-E | 新しいTonal要素を含む実encoderの試作へ最大Capture、最大聴取手順要素、最大既存設定を同時投入し、実plugin stateが1 MiB未満、Tonal増分16 KiB以内、聴取手順増分4 KiB以内、欠落なしの読戻しを確認。最大時系列も実ファイルへ保存し、recovery記録込みで16 MiB以内を確認 | structの見積りやモデルの文字列長では不可。DAW保存通知とT5／T8の全終了境界はG2とG4で確認 |
| G0-R | 実FFT、正確なpercentile、保存I/Oを含むworkerを実行し、第8節のrate別CPU基準、32 MiBの追加RAMと合算peak、2 MiBのqueue上限を確認。2枠使用、範囲連打、draining残存を含める | sleep等で代用したworker負荷では不可。試作でもcallbackの追加alloc／lock／I/Oは0。DAWでの追加dropout 0と30分連続動作はG4で再検証 |
| G0-L | 実leaseと停止ackを使い、2枠使用中のLIVE／Capture→B/C→A、借用中のclose、取消、processor再作成を実行する。対象wrapperをmacOSとWindowsの隔離した検証ホストでload／unloadし、終了後の実行中コードとcallbackが残らないことを確認 | 状態モデル、generation更新、threadを持たない模擬unloadでは不可。FFTとI/Oの停止遅延を含め、停止未確認を枠の空きとしない。停止できないI/Oを残してunloadする方式は不合格。Studio One／Pro Toolsでの最終統合はG4で確認 |
| G0-M | T7／T8／T10の保存、復元、履歴識別を契約モデルで検査し、実writerへ入れる障害注入点と期待結果を確定する | モデルpassとして記録できるが、実filesystemの耐障害性やDAW通知の実証とは呼ばない。T7／T10はG1、T8はG2の実装で確認 |
| G0-U | 実データを入れた300×200と375×250の画面試作と、U1〜U3の操作可能な動線試作を確認する。常設／詳細の区分、クリック経路、課題ごとの総操作上限を固定する | 静止画だけでは操作数の証跡にしない。設計者の試行を初見利用と呼ばず、実装UIと初見試行はG3で確認 |

G0の終了記録では「実行試作」「契約モデル」「画面と動線の試作」を分け、各行をpass／fail／未実施で示す。
G0-N／E／R／Lをモデルpassで埋めたり、G0-Uの操作上限を未定のままG1へ進めたりしない。
試作から方式や保存形式を変更した場合は該当するG0項目を再実証し、旧試作のpassを新方式へ流用しない。

### レビュー指摘に対応する必須試験

| ID | 対応する指摘 | 入力と操作 | 合格条件 |
| --- | --- | --- | --- |
| T1 | A観測の独立化と通常試聴との引継ぎ | B未選択、別曲C、alignmentなし、参照波形なし、C decode失敗、OS停止、C未選択、LIVE→Capture→保持→再open。2枠使用中の同じreceiverでLIVE→B/C→A、Capture中のB/C往復、試聴中の観測開始 | 有効なAは単独観測可能。C失敗はCだけ欠測。借用による枠の二重取得なし。連続入力の通常往復でLIVE epochとCapture起点を変更せず、試聴consumer生存中の枠返却なし。既存Bの位置合わせ結果は不変 |
| T2 | 旧新版の配信未定義 | 凍結したB-881 readerと新版readerを別プロセスで同時使用。初回起動、両起動順、未知binding、片側公開中断、新版専用Preset、旧OSへ戻す、再接続 | 旧readerは旧一覧とVersionsを読め、新readerは新Tonalを読む。各headは完全。旧defaultが有効。stale新版を現在配信と誤認しない。受信だけで音声切替なし |
| T3 | 窓起点の欠落 | 同一PCMと同一起点、0.03〜3.03秒の反例、開始1 sample差、window/hop境界、任意途中開始、seek/loop、全域／部分範囲 | 同条件では値0.05 dB以内かつ窓列、窓数、欠測bitmapが一致。3窓対4窓は条件差として検出し、値の一致を偽装しない |
| T4 | 区間再Captureによる置換 | 全曲Capture→サビ選択→別範囲→全域→保存再open。停止中とOS不在で同じ操作。範囲連打、別PCへの書出し／読込み、artifact欠損 | 再生も再Captureも不要。Capture ID、全域要約、波形、索引、receipt、保持データhashが不変。正常な移送後は区間値も一致。欠損時は区間値だけ利用不可 |
| T5 | 保存予算の衝突 | 2時間、最大binsと索引、最大receipt、最大Tonal、最大聴取手順要素、最大既存設定を同時保存。全rate/channel、encode境界、ディスク不足、破損と途中終了 | 実plugin stateが1 MiB未満、Tonal増分16 KiB以内、聴取手順増分4 KiB以内、artifact 16 MiB以内。既存項目の欠落0。読戻し一致。Tonal失敗が音声と既存Captureへ波及しない |
| T6 | OS基点の不一致 | 両exact commitを記録し、W-3080のFactory、genre、Library、Versions、History、初回Workless利用とOS再起動を通す | 新旧namespaceの両方で独立B VersionsとCの意味を保持。別worktreeのpassを対象基点の証跡へ転用しない |
| T7 | 再レビュー1：OS内部保存の旧新版混在 | 旧形式2件を持つ保存領域から新版Preset、default、draftを保存。旧OSへ戻って起動、編集、削除し、再び新版へ。各書込み境界で終了、移行再試行、同時起動 | 新版形式の保存、移行、default、draft操作で旧保存物のhash不変。旧形式の明示編集は既存writerで保存できる。新旧の並行編集を喪失せず、削除を復活させない。新版headは一覧と設定が同じsnapshot |
| T8 | 再レビュー2：Tonal確定前のDAW保存 | base確定前、pending公開後、sealed前後、ready記録後、dirty通知前後に保存→即終了→再open。restore直後再保存、Editorなし、旧job完了前の新Capture、破損、別PC | 各境界で第7節の復元表どおり。sealed以後は再取得なしで復元。sealed前は未完了を明示しbaseを保持。snapshot混在0、追加dirty通知の欠落0、破棄済みcallback 0 |
| T9 | 生存jobより早い枠返却と試聴待機の取消漏れ | 2枠使用中に一方のFFT/I/Oを停止点で保持し、同じreceiverでB/Cを1回要求。待機中にA、取消、曲／Cue／Check／Preset変更、transport停止／不連続、rate変更、bypass、offline、Blind、close、restore、processor再作成を個別に行う。3番目の要求、遅延ack、混在wrapper、unloadも確認 | running ownerでは借用でき、draining ownerには新規登録しない。停止ackまで枠を保持。条件不変なら枠と試聴許可の取得後に再クリックなしで1回だけ開始。取消済み要求の開始0、待機だけの試聴履歴0。A復帰は即時に要求可能。running+drainingは2枠以内、二重解放0、RT/UIのjoin待ち0 |
| T10 | 再レビュー4：Historyのrevision衝突 | 旧新版で同じrevision番号とruntime/event ID、別manifest hashを用意。start後に新版公開、旧OSへ復帰、遅延completion、note改訂、archive欠損、hash改ざん | namespace別に正しいarchiveを解決。比較中publication_refは不変。誤ったstart/completion結合0。新版eventは旧journalへ混入せず、異常eventだけ保留 |

ここで旧新版の混在試験に使う旧Hyphaは、今回の変更直前にLibrary 1.1を読めるB-881を基準とする。
それより古い版に既存1.1機能があるとは仮定せず、従来対応していたschemaの読取り試験は別に維持する。

### 三つの実利用課題

U1〜U3は試験課題であり、実施済みの利用観察ではない。
初見の協力者は未定で、現時点の完了率、クリック数、理解度の実測値はない。
G0では設計者が試作を操作して経路と上限を確定し、G3では製品の操作説明を受けていない協力者による初見試行を別に記録する。

| ID | 開始条件と課題 | 操作と理解の合格条件 |
| --- | --- | --- |
| U1 全体から帯域を確認して聴く | Libraryに登録済みの比較曲を使い、Tonal CheckでCとCueを選ぶ。全体の分布から指定帯域を確認し、Cを聴いてAへ戻る | 帯域の詳細を開く操作と概要へ戻る操作は各1回以内。詳細確認のための曲／Cue／Presetの再選択0。準備済みのCへの試聴要求とAへの復帰は各1操作、枠待ちによる再クリック0。現在鳴っている音と、LIVE一窓対Cue集計という条件差を回答できる |
| U2 Captureからサビを確認する | 保存済みの全域Captureを開き、サビ範囲、別範囲、全域の順に確認する。DAW停止中とOS不在でも行う | 再生開始と再Captureは0。全域へ戻る操作は1回以内。範囲変更前後で第5節の保持データが不変。どの範囲を表示中か、保存済み値か集計待ちかを回答できる。欠測時は理由と全域へ戻る出口を見つけられる |
| U3 ジャンルを設定して比較へ戻る | HyphaのTonal CheckからOSの同じCheckを開き、ジャンルを選んで明示保存し、元のreceiverへ戻る。取消とOS不在も確認する | Checkを開く接続動線と戻る動線は各1操作。ジャンル選択と保存は別に数え、Checkの検索と曲／Cue／Presetの再選択は0。C、固定gain、Captureの表示範囲は不変。ジャンル設定だけによる音声commandは0。曲内変動の帯とジャンル分布の帯を区別できる |

操作数はメニューを開くクリック、選択、確定、戻る操作をそれぞれ数え、アプリ切替、スクロール、文字入力、範囲指定drag、キーボード操作も別欄へ残す。
既存の安全上必要な確認を消して操作数を達成せず、確認が必要な試行は理由と操作数を分けて記録する。
CやCueの初回選択、Captureの事前作成、ジャンルの選択と保存を計測から隠さず、準備操作と課題内操作を分ける。
G0-Uで初回選択を含む課題ごとの最短経路と総操作上限を固定し、上表の部分動作の上限も満たすことをG3の判定条件にする。

記録欄は「課題ID／試作または実装版／参加者の匿名IDと既利用課題／OSとwrapperと画面サイズ／開始状態／操作の列と件数／アプリ切替／待ち時間／誤操作と引返し／説明の介入／条件理解の回答／完了または未完了」とする。
初見試行では課題の目的だけを伝え、操作箇所や用語の答えを先に教えない。
終了後は「何が鳴っているか」「どの範囲と分布を見ているか」「何が変わり、何が変わっていないか」を画面から確認してもらう。
同じ参加者の再試行や別課題で得た学習を初見の結果へ混ぜず、設計者の成功だけで合格にしない。
説明介入、不要な再選択、条件の誤認があればその試行を合格にせず、同種の入口を修正して再検証する。
協力者未確定や試行未実施はUの未検証として残し、G3完了や製品全体の完成とは扱わない。

### 数値と異常系

- 一定gain ±6／±12 dBは、gate/floor非通過条件で相対値の差0.01 dB以内。通過する例では欠測遷移を検証する。
- sine、白色／pink noise、低域／高域shelf、帯域境界、逆相stereo、mono、無音、極小値、非finite入力を含める。
- 44.1／48／88.2／96／176.4／192 kHzと異rateのA/C、64／128／512／1024 frame分割を確認する。既存Captureが受理する8／384／768 kHz等も容量境界と欠測の試験から外さない。
- 利用許諾済み実音源で疎な編成、低域中心、明るい曲、長い無音を含め、hashと観測範囲を記録する。Referenceへの近さを音質評価の正解にしない。
- source移動／置換／削除、古いjob完了、bundle差替え、未知version、path逸脱、symlink、非finite artifact、超過長、部分ファイル、OS停止を検証する。
- Capture書出し／読込みは正常往復、欠損、改ざん、別Capture ID、展開先逸脱、過大bundle、他POSTでの復元を含める。
- 旧readerで新DAW状態のACaptureが読めること、旧版再保存でTonal追加情報が失われても元の保存データから復元できること、不一致な追加情報でbase Captureが失われないことを確認する。

### 全体回帰と実機

A/B/Cの100回切替、Cue連打、seek/loop、bypass、offline、画面close/open、rate変更、2枠占有中の追加要求を確認する。
Tonal表示中の2枠使用から同じreceiverのB/Cを聴く往復と、停止ack前に取り消した要求が後から再生されないことも最終版で確認する。
RT allocation/lock/I/O追加0、通常Aのbit identity、報告latency 0 samples、queue飽和とworker停止でも音声継続を要求する。
最終版でRust workspace testとowned warningなしのclippyを実行する。
FFI変更時はignored parity／pairingの一覧件数を実測して全件実行する。
OS側は対象試験が安定してからnpm testの全体baselineを実行する。

全5サイズ、共通Preview、Studio OneとPro Toolsの対象wrapper、macOSとWindowsを同じ最終ソースで検証する。
U1〜U3の確定動線を全5サイズで回帰し、G0の小サイズ試作を実装UIの証跡へ転用しない。
30分連続動作ではcallback p95/p99、追加dropout、worker時間、running/draining数、停止ackまでの時間、peak RSS、描画時間、ディスク増分を記録する。
Hyphaの固定UIは既存の英語を維持し、OS側の英日表示とPreviewを検証する。
基点との差と第8節の上限に対して判定し、数値を記録しただけでpassとしない。

## 11. 既存作業との依存と改訂の検証

Tonal第3版の改訂時のHypha ReferenceはB-882で、B-881からのBlindと表示の変更が統合されていた。
その時点でCapture保存、History transport、AnalysisLeaseの基盤に差分がないことを確認したが、後続commitやその他のB-882変更の完成証拠へは流用しない。
最初の統合改訂は本書と聴取手順の詳細文書だけを変更し、他worktreeの製品コードを変更しなかった。
第4版の開始時もHypha ReferenceのHEADはB-882、OS ReferenceはW-3080だった。
Hypha Referenceには別作業の未コミット差分があるため、HEADの記録をworktree全体が凍結済みである証拠にしない。
通常試聴の同期的な枠取得とCapture限定の借用経路を実読し、第6節の共有契約、第9節の影響ファイル、第10節のT1／T9へ対応付けた。
初見協力者の未確定という利用者の回答を保持し、U1〜U3は新しい試験計画として追加した。
G0の独立fixtureは先行できるが、観測所有権の抽出とCapture統合は対象責務の統合済み状態に対して行う。
過去の不具合を現在の停止条件と決め付けず、exact commitと対象差分で再現を確認する。

統合改訂時はHypha ReferenceがB-884、OS ReferenceがW-3080だった。
B-882〜B-884の変更一覧と、B-883〜B-884のgain解析とCapture codecの製品差分を読み、以前の「保存基盤に差分なし」を現在へ延長しない。
HyphaのJUCE submodule、HyphaObservatoryView.h、ui_render_contract_test.cppに別作業の差分があり、本改訂では変更しない。
直前の聴取手順レビュー4点を詳細文書へ反映し、第12節に選択・保存・配信・工程の統合条件を追加した。
聴取手順レビュー時のW-3080対象8 test fileの47件passは既存実装の記録であり、WG0、R1〜R12や共存試験の実証ではない。
統合改訂でも同じW-3080の8 test fileを再実行し、47件pass、0 fail、0 skipだった。
文書の表、節番号、試験ID、共通容量、参照先の実在と、Tonal第5節・第6節を維持したことを確認した。
これらは文書と既存実装の確認であり、新しい保存snapshot、遷移、writer、実寸画面を動かした結果ではない。
統合改訂でも計画置き場のmainでRust workspace testとclippyを起動したが、全体の完了前に中断した（終了コード130）。
既存3 suiteの110件passは得られたが、workspace全体とclippyは未完了であり、B-884や新機能の検証証拠にはしない。

第2統合改訂時はHypha ReferenceがB-887、OS ReferenceがW-3080で、両worktreeは確認時にcleanだった。
B-886〜B-887の39ファイル、1,239行追加、228行削除の差分とB-887実装記録を読み、Capture操作の直列化、ReferenceAnalysisOwner、入力配信、表示投影が実装済みであることを計画へ反映した。
B-887の対象native試験、全体baseline、実寸検証は既存実装の証拠として扱うが、同じcommitで未実施のDAW実機、Windows、両Blindとの共存をBL工程やCS試験のpassへ転用しない。
Local Blindの試験IDはTonalのU1〜U3および聴取手順のV1〜V3と衝突しないよう、BL-V01〜BL-V12とBL-U1〜BL-U3へ固定した。
今回の変更は本書、Local Blind詳細文書、共通安全契約の整合に限定し、製品コード、他worktree、既存WAVを変更しない。
第2統合改訂の文書検証では、構成4文書のローカル参照57件、表55件、Local Blind試験ID 15件、統合見出し3件を検査し、参照切れ、列ずれ、ID定義の重複がないことを確認した。

初稿で確認したFactoryとgenreの14件passは、旧案のOS worktreeに対する入力確認の記録であり、今回の基点の回帰証拠として転用しない。
第2版の改訂時にはW-3080でFactory、genre、Library delivery、Library Versionsの4 test fileが32件pass、0 fail、0 skipだった。
第3版の改訂時にはW-3080のTemplateRepository、PresetRegistry、GlobalSettings、EditorDraft、RuntimeEvent、LibraryDelivery、LibraryVersionsの7 test fileが44件pass、0 fail、0 skipだった。
第3版では別に、メモリ上の契約モデルを33 assertionで確認した。T7の保存分離とatomic headが6件、T8の復元とgenerationが12件、T9の停止ackと2枠制限が8件、T10の履歴識別が7件で、すべてpassだった。
T7のうち旧readerについては、実際のW-3080 readerとencoderへメモリ上の保存領域を渡して確認した。
モデルは状態遷移の矛盾を探す補助であり、実filesystemの耐障害性、DAW保存通知、worker停止、wrapper unloadの実証ではない。
既存対象試験とモデルのpassは、新機能T1〜T10の実装や全体baselineのpassを意味しない。
解析窓の3個対4個、統合後のDAW状態配分1,041,420 bytes、時系列payloadの11,761,449 bytesは検算対象とする。
統合前のDAW状態配分1,037,324 bytesに聴取手順の4,096 bytesを加えたもので、既存予約は増やしていない。
これらは数値契約の検算であり、新encoder、区間artifact、互換配信、実機の完成を示さない。
第4版で追加した借用、試聴開始待ち、編集往復、U1〜U3とG0-N／E／R／L／M／Uは未実施であり、過去のモデルや既存suiteのpassを完成証拠へ転用しない。
第4版の文書チェックでは、版番号、試験ID、表の列数、参照先の実在、第5節と第7節の維持、既存WAVのhash不変を含む27項目を確認した。
これは文書の機械的な整合確認であり、借用方式や操作動線の実証ではない。
計画の置き場であるHypha mainで`cargo test --workspace --locked --offline`と`cargo clippy --workspace --locked --offline -- -D warnings`も起動したが、完了待ちを中断した（終了コード130）。
中断時点でGUIの31件passは得られたが、workspace testとclippyは未完了であり、Reference worktreeの回帰証拠にも使わない。
既存LastTestsFailedログを確認したが、古いbuild結果を今回のソースでの再現結果とは扱わない。
NotionのSECTION:DEV／TASKSは利用可能なread toolがなく未読であり、書込みも行わない。

計画ファイルはHypha mainに置き、開始時から変更されていたtest_signals/S-1_1kHz_sine_m6dBFS_10s.wavを保持する。
今回、新しいcommitやB番号は発行しない。
実装後の配布ではLS用macOS pkg、HP用macOS zip、同一commitの署名済みWindows installerの3チャネルを既存runbookに従って揃える。
計画改訂の完了と、G0以降の実装検証や公開完了を区別する。

## 12. 聴取再利用と確認手順の統合契約

### 機能ごとの正本と共通境界

聴取手順の詳細は[構成文書](reference_listening_workflow_plan_20260914.md)で定義し、以下の3機能を本計画へ組み込む。
同文書のWG0〜WG4、R1〜R12、V1〜V3は任意の参考資料ではなく、統合計画の受入条件である。

| 機能 | 保存の正本 | 既存機能を変えない境界 |
| --- | --- | --- |
| 聴きどころの再利用 | OSの不変な音源・Cue・聴取目的revision。適用先は利用者Presetの新revision | 原本更新で適用済みCueを変えず、4 Cue上限、既存Note、旧形式を保持する |
| 今日の確認 | OSの不変な定義と、Hyphaのattempt別条件snapshot・実行journal | Presetの順序と通常B選択を変えず、本人の確認状態と試聴receiptを区別する |
| 比較しおり | OS原本と、Hyphaが先に永続化する条件・メモ・outbox | 過去Aの音声再現を約束せず、現在Aへのgainと位置対応を再検証する |

### 比較状態、戻り先、Tonal表示

通常比較、今日の確認、しおり比較はreceiver内で排他的な選択状態とする。
Tonal表示はこれらと直交する表示要求であり、第四の音声選択状態を作らない。
通常のB/C選択を戻り先の正本として保持し、一時比較のIDを通常選択へ保存しない。
詳細文書第5節の遷移表を全入口に適用し、今日の確認から同じCheckの別Cueを持つしおりを開いても、項目の条件改訂として扱わない。
その確認回を一時中断して項目、条件revision、確認状態を保持し、しおりを閉じると再検証して同じ項目へ戻れるようにする。
二つ目のしおりは同じ戻り先を保って置換し、戻り先の無制限な入れ子を作らない。

音声のA復帰は保存I/Oを待たずに要求する。
次の一時選択を確定するのは、その遷移のローカル保存と必要なA復帰receiptが揃った後とし、待機中も音声のA復帰や取消を妨げない。
明示した開始、再開、項目移動、しおり適用は準備までで、B/Cの再生を開始しない。
これらの要求は第6節の旧試聴開始待ちを失効させ、後から旧sourceが鳴らないようにする。
Blind所有中は開始要求を実行待ちとして保持せず、利用不可の理由を示す。
Blind終了だけで保留された開始やしおり適用を実行せず、新たな明示操作を必要とする。

保持中CaptureのID、取得内容、波形、帯域時系列は状態移動で変更しない。
新しいC用の観測が遅れた場合はCだけ準備中または欠測とし、前のCの曲線を新しいCとして表示しない。
帯域、分布の表示切替、Captureの範囲変更は音声選択や本人の確認状態を変えない。
今日の確認やしおりから第4節のジャンル編集へ進む場合、元Presetへの明示保存と、その場の表示設定を分ける。
保存結果は要求IDに対応したreceiverの表示上書きとして保持し、凍結したPreset snapshot、項目の条件revision、しおり原本、比較開始contextを差し替えない。
表示上書きはTonal要素の予算内に保存し、表示の根拠となるrevision/hashを残す。
比較への復帰時もその表示要求のscopeを照合し、別の項目やPOSTへ流用しない。

### 保存確定と共通DAW snapshot

聴取手順のローカル保存は、参照artifactの永続化 → journal／outboxのcommit → checkpoint公開 → dirty通知の順に確定する。
未確定の条件、長文メモ、snapshotを参照するcheckpointやOS反映ackを先に公開しない。
詳細文書第7節の復元表を適用し、ローカルcommit前の強制終了に未保存入力の復元を保証しない。
動作中の保存失敗では入力と再試行の出口を保ち、OS未反映とローカル未保存を区別する。

非RTの保存snapshot組立て責務を一つ設け、通常のReferenceChoices、base CaptureとTonalの一組、workflow追加要素を一回の公開で読めるようにする。
Tonalのready確定とworkflowのcheckpoint確定は、それぞれのCapture ID／取得hashまたはattempt ID／連番／hashとgenerationを照合して、自分の領域だけを更新する。
古いsnapshot全体を後から公開して他方の新しい状態を巻き戻さず、他方のI/O完了待ちも加えない。
別restoreは全領域の古い公開要求を失効させるが、一つのworkflow操作だけで進行中のCapture確定を取り消さない。
現行のsavedSettingsが直接現在のCheck選択を保存する責務は分離し、ReferenceChoicesには通常選択だけを投影する。
workflow追加要素に保存したmode、戻り先ref、checkpointは再開案内であり、復元直後に一時選択や試聴を開始しない。
追加要素が欠けた旧版経由の保存でも、通常選択とbase Captureを維持する。
getStateInformationとrestore直後の再保存には第7節の有界snapshot規則を共通適用する。

### 配信と履歴の区別

聴取手順のOS保存はreference/workflow-v1、配信はlibrary/workflow-v1へ分離する。
legacy、tonal-v1のmanifest、Preset保存、通常比較eventへ未知fieldを混ぜない。
通常比較は開始時のLibrary経路に対応するwriterを、一時比較はworkflow journalを一度だけ選び、途中の配信や状態移動で終了eventの保存先を変えない。
workflow eventの保存先namespaceと、音源条件の由来を示すpublication_refのnamespaceは別fieldとする。
元のpublication_refはlegacyまたはtonal-v1のまま保持し、workflow経路で配信されたことを理由に書き換えない。
一時比較の実条件は別のcondition receiptで特定し、元のpublicationだけでその後のCue変更まで証明したことにしない。
旧eventにないhash等はnullと証拠の範囲を残し、後から当時のreceiptを捏造しない。
通常比較をもとにしおりを作る操作はしおり保存であり、第二の試聴eventを作らない。

### 共通実証と実装順序

TonalはG0〜G4、聴取手順はWG0〜WG4で管理し、聴取手順内の順序は再利用 → 今日の確認 → しおりを保つ。
先行する機能群は、他方の製品実装を待たず、自分の実行可能な試作と相手領域の最大形式fixtureで共通契約を実証できる。
未実装側のfixtureを、その機能の実装passや実機passとして数えない。
両機能が実装された時点ではfixtureを実データに置き換え、下記の共存試験を同じ最終ソースで行う。

| 共通責務 | 先行実証の対応 | 共存時の必須試験 |
| --- | --- | --- |
| encoderと一組のsnapshot | G0-E／WG0-E | T5／T8とR10／R11。Tonal readyとworkflow commitの完了順を反転し、他方の巻戻しと欠落0 |
| 選択と戻り先 | G0-L／WG0-C | T1／T9とR4／R5／R6／R8。2枠使用中の確認 → しおり → 保存 → 再open → 確認へ復帰、旧試聴待ちの再生0 |
| 永続化、終了、資源 | G0-R／G0-LとWG0-F／WG0-L | R9／R10／R11。条件artifact欠損とTonal保存失敗を互いに波及させず、共存peakと実wrapper unloadを確認 |
| 履歴と表示 | G0-M／G0-UとWG0-M／WG0-U | T10、R9／R12、U1〜U3、V1〜V3。三つの保存先の同名IDを区別し、小サイズでも現在条件と保存状態を誤認させない |

実encoder、実filesystem、実際の一時比較入口、実保存ownerとwrapper終了は実行可能な試作で確認する。
契約モデルは遷移の検査に使うが、これらの実証を置き換えない。
共有責務を変更したら両機能の対応行を再検証し、古いcommitの証跡をまとめ直しただけで共存passとしない。
同じ対象差分とfixtureの証跡は重複実行せず参照できるが、対象や方式が変わった場合は取り直す。

統合計画の完成条件はG4、WG4、BL4、第12節と第13節の共存試験、共通安全契約CS1〜CS8の対象範囲の完了である。
操作数はU1〜U3とV1〜V3を別課題として測り、既存A/B/Cの非劣化だけで聴取手順の改善を証明したことにしない。
初見協力者は未定であり、手順短縮や理解しやすさの改善は現時点では未実証である。
いずれかの未完了範囲を承認なく後のPhaseへ移したり、個別機能の完了を統合完了と表示したりしない。

## 13. Local PRE/POST Blindの統合契約

### Local Blindの正本と変更しない範囲

Local Blindの詳細は[PRE/POST Blind計画](hypha_pre_post_blind_usability_plan_20260914.md)で定義する。
同文書のBL0〜BL4、BL-V01〜BL-V12、BL-U1〜BL-U3は任意の参考資料ではなく、本計画の受入条件である。
GainMatchは内部の実機比較対象として同文書に記載し、外部向けの優越主張へ変換しない。

Local Blindは既存の固定4秒Capture、固定Gain、両Sourceの完全聴取、回答、Reveal、明示Returnを維持する。
改善対象は、同じprocessorとexact pairでの比較用Context再利用、準備完了後の取り直し、型付き診断、比較条件の表示、Pauseを含む中断、実機での復帰確認である。
Reference A/B/C、Tonalの測定と保存、聴きどころ、今日の確認、比較しおり、Kirin OSのschemaをLocal Blindのために変更しない。
Local Blindの開始にKirin OS、Work接続、Tonal artifact、workflow snapshotを要求しない。

### 状態と所有権の共有境界

| 境界 | 統合後の契約 |
| --- | --- |
| 開始予約 | B-887のReference controllerによる予約と既存の比較排他を正本にし、Reference Capture、通常B/C、Reference Blind、Local Blindの入口ごとに別の開始可否を作らない |
| 解析枠 | Reference側はReferenceAnalysisOwner、Local Blindは既存Blind admissionを維持する。両者を一つのownerへ混ぜず、物理2枠には合算し、Local Blind用の3枠目、常駐thread、入力queueを追加しない。固定4秒PCMとTonal時系列は別の入力として維持する |
| 準備済みの取り直し | `ready → preparedDiscardPending → RT退役ack → PCM publication退役 → scope解放 → preflight`を一つの世代で進める。Startと取り直しは片方だけを受理する |
| 試聴開始後の復帰 | StopまたはEndとReturnを維持し、実適用済み減衰と比較コピーの停止を別に確認する。保存、decode、OS配信の完了を音声復帰条件にしない |
| Referenceの通常選択 | Local Blind開始前のVersion、Preset、Check、Candidate、Cueを上書きしない。Return後の可聴音はAとし、保持したB/C選択を自動再生しない |
| 比較用Context | 同じprocessor、exact pair、通常Contextに限る非永続情報とする。Editor close/openでは再表示できるが、project restore、state restore、processor再生成、pair変更で失効させる |
| 保存と履歴 | Context、PCM、Gain承認、Source、回答、再生許可をDAW state、Work、ReferenceChoices、workflowへ追加しない。既存のBlind履歴がある場合もTonalやworkflowのjournalへ二重記録しない |
| 中断 | Pause、停止、seek、loop変更、clock/PDC変更、offline、bypass、Editor closeを同じ失効へ丸めない。出力と再開条件はLocal Blind詳細文書第6.4節を正本とする |

一時Contextと診断は1 POST当たり4 KiB以内に収め、DAW保存の1 MiB予算へ加算しない。
ただし、processor、準備済みtrial、退役待ちがTonal workerやworkflow保存と同時に生存する場合は、全体のpeak RSSから除外しない。
Local Blindの一時操作はparameter gestureとdirty通知を発行せず、保存するReference選択と非同期commitの通知は止めない。
通知の分類と回数は共通安全契約第4節を正本とする。

### 三機能群の共存試験

TonalはG0〜G4、聴取手順はWG0〜WG4、Local BlindはBL0〜BL4で進める。
各機能群は独立して着手できるが、共有sourceを変更した工程は下表の影響先を同じ確定sourceで再検証する。
BL0で閉じるhost matrixはG4、WG4、CS1〜CS8にも使い、製品名、版、OS build、formatが異なる結果を寄せ集めない。

| 共通責務 | 所有する工程と試験 | 同じ最終sourceで確認する共存条件 |
| --- | --- | --- |
| 開始予約と解析owner | G1のT1／T9、BL1／BL3のBL-V07／BL-V11、CS8 | 2枠使用中の通常B/C待機、Reference Capture、両Blindの同時要求を直列化する。旧要求の再生、二重取得、停止ack前の解放を0にする |
| 音声出力と中断 | G4、WG4のR5、BL3／BL4のBL-V01／BL-V04／BL-V06、CS4／CS5／CS7 | Pause、offline、bypass、seek、clock/PDC変化から比較コピーを自動再開せず、実適用済み減衰の復帰待ちを失わない。通常Aのbit identityと0 samplesを維持する |
| 保存snapshotとhost通知 | G2のT5／T8、WG2〜WG4のR9〜R11、BL4のBL-V09／BL-V10、CS2／CS3／CS6 | Local Blindの一時操作は保存内容を変えず、Tonal readyとworkflow commitの必要な通知を落とさない。restoreで旧再生許可を復活させない |
| 表示と秘匿 | G3のU1〜U3、WG4のR12／V1〜V3、BL2／BL4のBL-V08／BL-V12とBL-U1〜BL-U3 | 全5サイズで現在音、固定コピー、保存済み条件、復帰待ちを取り違えない。Blind中の音源割当を描画、tooltip、keyboard、accessibilityから漏らさない |
| 負荷と終了 | G0-R／G0-L、WG0-F／WG0-L、BL3のBL-V11、CS8 | Tonal処理、workflow I/O、Local Blindの準備と退役が重なるpeakを測る。追加RT alloc／lock／I/O、3枠目、破棄済みcallback、unload後の実行中コードを0にする |

Local BlindのBL-V試験はReferenceのT試験や聴取手順のR試験を代用せず、逆方向も同様とする。
B-887のnativeと全体baselineは共有土台の確認に使えるが、未実施の実DAW、Windows、両Blindの共存、BL-U初見試行をpassにしない。
GainMatchとの比較はLocal Blindの操作と実測を評価するものであり、Tonalや聴取手順の完成証拠には使わない。

### 統合完了の判定

個別機能の完了は、それぞれG4、WG4、BL4で判定する。
統合計画全体は、三工程の完了に加え、第12節と本節の共存試験、CS1〜CS8、同じ閉じたhost matrixでの実機確認が完了した場合だけ完成とする。
Tonalまたは聴取手順の先行実装をLocal Blind待ちとして止めない一方、先行した個別完了を統合完了や公開リリース完了と表示しない。
初見協力者は未確定であり、U1〜U3、V1〜V3、BL-U1〜BL-U3の理解と操作数は未実証として残す。

## 14. 参照

- [比較機能の共通安全契約とCS受入試験](hypha_comparison_safety_contract_20260914.md)
- [聴取再利用と確認手順の詳細・WG工程・R/V受入試験](reference_listening_workflow_plan_20260914.md)
- [PRE/POST Blindの使いやすさ、診断、BL工程・BL-V/BL-U受入試験](hypha_pre_post_blind_usability_plan_20260914.md)
- [B-887 CaptureとReference解析所有権の実装記録](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/docs/reference_capture_b887_implementation_20260914.md)

- [Kirin OS Reference製品契約](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/docs/reference_product_contract_20260905.md)
- [ジャンル分布の設計と実測記録](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/docs/measure_genre_reference_balance_rebuild_plan_20260823.md)
- [現在のReference Library配信](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/docs/reference_library_delivery_20260913.md)
- [A/B/C receiver](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/docs/reference_library_receiver_20260913.md)
- [全体A/B表示の実装記録](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/docs/reference_visual_comparison_implementation_20260914.md)
- [進行中のCaptureと導線修正](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/docs/reference_capture_workflow_implementation_20260914.md)
- [Capture証跡と探索の契約](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/docs/reference_evidence_and_discovery_contract_20260914.md)
- [A観測がalignmentとdecodeに依存する現実装](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/juce_shell/src/reference_audition/ReferenceVisualObservation.cpp)
- [通常試聴と観測の許可切替](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/juce_shell/src/reference_audition/ReferenceComparisonController.cpp)
- [準備済み音源の試聴開始とgeneration照合](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/juce_shell/src/reference_audition/ReferenceRuntimeV2NormalSelection.cpp)
- [現行の試聴枠取得とCaptureからの借用](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/crates/kirin_hypha_ffi/src/audition_admission_ffi.rs)
- [既存Libraryの全Preset検証](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/juce_shell/src/reference_audition/ReferenceLibraryRepository.cpp)
- [OS W-3080のLibraryとVersions配信](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/src/services/referenceLibraryDelivery.mjs)
- [OSの窓配置](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/native/src/spectral_balance.rs)
- [OSの範囲選択と集計](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/src/services/readSpectralBalanceModel.mjs)
- [Capture最大保存量の既存試験](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/juce_shell/tests/ReferenceCaptureStorageBudgetTest.h)
- [OSの旧Preset保存reader](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/src/services/referenceWorkspaceTemplateRepository.mjs)
- [OSの履歴manifest解決](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/src/services/referenceLibraryHistory.mjs)
- [Hyphaの履歴contextとwriter](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/juce_shell/src/reference_audition/ReferenceRuntimeEventTransport.cpp)
- [Captureの非同期dirty通知](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/juce_shell/src/HyphaCaptureStateNotification.h)
- [解析枠の解放処理](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/crates/kirin_measure/src/analysis_lease.rs)
- [Hypha CE2226表示契約](hypha_ce2226_jungle_visual_system_20260901.md)

外部の表示例として、iZotopeの公式資料で四広帯域と全域詳細、単曲と曲集合の参照という構成を確認した。
Hyphaの測定方式は上記Kirin OS契約に従い、この資料から非公開の計算法を推定しない。
参照: [Tonal Balance Control 2公式「Target Curves and Views」](https://s3.amazonaws.com/izotopedownloads/docs/tbc2/en/targets-and-views/index.html)。
