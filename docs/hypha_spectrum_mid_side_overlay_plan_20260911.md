# SpectrumのMid/Side同時表示実装計画

更新日：2026-09-11。
調査基準：`171d16d1`（B-811）。
状態：製品実装済み。macOS自動検証済み。Windows、Studio One実機、配置、公開は未実施。

## 1. 完成させる操作

現在の`LR / MID / SIDE`の右へ`M/S`を追加する。
M/SではPOST入力のMidとSideの絶対スペクトラムを同じ座標へ重ね、EQ操作中に帯域ごとのバランスを読めるようにする。
ラベルの意味は「MidとSideの同時表示」である。

M/SとPRE/POST差分のΔは相互に排他とする。
Δ中はM/Sボタンを、M/S中はΔへの操作をグレーアウトする。
ボタンの位置を保持し、選択中の観測を自動で別の観測へ切り替えない。
M/SからはLR、MID、SIDEのいずれかへ戻るとΔを選択できる。

現行のSpectrum画面はPOST専用なので、対象はPOST絶対表示とする。
PREの新規画面、PRE/POSTとM/Sの四系列比較、M/S比率、品質評価、試聴切替は追加しない。
通常A経路、Record、Reference、正本のPRE/POST測定は既存契約を維持する。

## 2. 調査で確認した制約

| 現行の事実 | 今回の設計 |
| --- | --- |
| `SpectrumChannelMode`はLR/MID/SIDEの三値でSHARPとも共有する | Spectrum専用の四択型を追加し、共有enumは三値を維持 |
| `spectrumChannelModeWidths`も三値の描画に使われる | 四択専用のdescriptorとgeometryを追加し、SHARPへ第四ボタンを漏らさない |
| MIDは`(L+R)/2`、SIDEは`(L-R)/2` | 同じ正規化、窓、FFT、band定義を再利用 |
| stereo LRもL/Rの二回のFFTを行う | 同じworkerとFFT planでM/Sを二回計算し、負荷比較にstereo LRを使う |
| `SpectrumFrame`は一系列、`KirinSpectrumView`はPRE/POST/Δの配列を持つ | 専用の二系列frameと追加ABIを使い、既存配列の意味とサイズを維持 |
| POST絶対表示でもpairがあれば現在はPRE exchangeへ進む | M/S専用のPOSTローカル分岐でPRE解析要求を終了し、M/S用要求を発行しない |
| 現行の拡張probe表示にはモードボタンを省略する分岐がある | Spectrumの上段を常時操作行として確保し、probeとmode操作を分離 |
| `getStateInformation`はチャンネル選択を保存していない | M/Sもloaded instance内だけで記憶し、DAW保存形式へ追加しない |
| 最小サイズのGuideありでは絶対Spectrumの高さが約27px | 上部二段の中で完結させ、追加行でグラフを狭めない |

主な根拠は`spectrum.rs`、`spectrum_runtime_assemblers.rs`、`spectrum_runtime_worker.rs`、`spectrum_exchange_post.rs`、FFI `lib.rs`、`PluginProcessorAnalysis.cpp`、`PluginProcessor.cpp`、`HyphaSpectrumChromePainter.cpp`、`HyphaSpectrumGeometry.h`である。
画面全体の幅やdensityは`HyphaObservatoryResizeContract.h`と`HyphaPresentationContext.h`から受け取り、子componentの幅から文字サイズやcompact判定を再推定しない。

## 3. 五つのサイズと中間幅の配置契約

### 3.1 グラフ領域と情報量

以下は現行の共通shellとSpectrum geometryから計算した論理pxである。
二本表示のために縦軸や測定値を変更せず、既存のPOST絶対Spectrumと同じplotを使う。

| Editor | plot幅 | plot高さ、Guideなし | plot高さ、Guideあり | M/S読み取りの表記 |
| --- | ---: | ---: | ---: | --- |
| 300×200 | 241.04 | 47.28 | 27.28 | `M` / `S`と共通`dBFS` |
| 375×250 | 300.44 | 70.72 | 46.72 | `M` / `S`と共通`dBFS` |
| 450×300 | 359.85 | 99.36 | 73.36 | `MID` / `SIDE`と共通`dBFS` |
| 600×400 | 482.00 | 160.00 | 132.00 | `MID` / `SIDE`と共通`dBFS` |
| 900×600 | 726.31 | 277.28 | 243.28 | `MID` / `SIDE`と共通`dBFS` |

最小サイズでも二本、周波数、両値、単位、lock解除を保持する。
300×200のGuideありでは1 dBが約0.28pxなので、微小差を線の間隔だけで読み分けられるとは約束しない。
微小差は同じ周波数の二値で読み、線は帯域形状と相対的な分布を示す。
画面サイズに応じて機能や測定精度を落とさないことと、画素数による視認限界を区別する。

### 3.2 上段は常設の操作行

全Spectrumモードで上段を`LR / MID / SIDE / M/S`、PSB切替、MARK予約領域の順に配置する。
MARKの表示条件は従来どおりΔに限定し、非表示時も他の操作を移動させない。
hover、lock、欠測、操作失敗の通知でこの行を置換しない。

Spectrum専用の四択descriptorとgeometryを描画、hit test、tooltip、Capture検査で共有する。
四択のindexはprocessorの専用display selection APIで検証し、共有channel enumへ直接渡さない。
既存の三択配列と三択geometryはSHARP用として維持する。

寸法は次の式で決める。
`s`は既存Spectrumのgeometry scale、`N`は共通navigation書体のfontHeightである。

- 四択の基準幅は`[20, 26, 30, 32] × s`とする。
- native testは各基準幅が`ceil(実書体のlabel幅 + N/2)`以上であることを全5サイズで要求する。
- 隣接gapは`g = max(2, 2s)`とし、mode間と右端の予約領域間に適用する。
- PSB切替幅は`max(64s, ceil(SPECTRUMの実書体幅 + N/2))`とする。
- MARK予約幅は`42s`とし、MARK使用時は既存action書体でlabelと×が収まることも検査する。
- 操作行の高さは`ceil(1.20N) + 2`とする。
- 四択領域とPSB領域の間にも最低`g`を残す。

操作は実際に表示する矩形内でのみ受ける。
非表示のMARK用hit領域を残さない。
既存Spectrum canvasの操作境界を維持し、disabledなM/SまたはΔはcallbackを実行しない。

### 3.3 下段は凡例とprobeの共用行

上段の2px下から読み取り行を配置する。
読み取り行の高さは共通readout書体に対する`ceil(1.12 × fontHeight)`とし、下に1px残す。
二行全体を既存のheader予約高`max(32, 30s)`以内に収める。
余った高さは余白として扱い、第三行を作らない。

下段には周波数、Mid値、Side値、共通dBFS、解除領域の五列を固定して予約する。
×の領域は`16s`を確保し、非lock時は空欄にする。
列間は`g`、数値列の幅は`ceil(tabularTextWidth(最大桁文字列) + fontHeight/2)`を使う。
ラベルを含めた最大幅で予約し、値が変わっても列を移動しない。
符号、小数点、単位を省略したり、数値をellipsisや横圧縮で収めたりしない。

周波数の最大幅は`~35 Hz`、`999 Hz`、`9.99 kHz`、`22.0 kHz`など、実際のformatterが生成する文字列の最大値から決める。
この代表例だけでなく、対応rateのband中心とprobe補間位置を走査して幅の上限を確認する。
値は小数第一位までとし、`-144.0`、正の最大桁、`0.0`、欠測記号を検査する。
負のゼロは`0.0`へ整形する。
最大値の文字幅で有限の測定値が収まらなければ、符号付き指数表記へ明示的に切り替え、値のclipで表示を偽らない。

hoverもlockもない場合は同じ列内にcyanのMID、violetのSIDEという色凡例を表示し、帯域を選んでいない数値を作らない。
probe中は同じ列に色付きのM/MIDとS/SIDEの値を表示する。
凡例の線見本は数値と同時に追加せず、同じ予約領域を使う。

単独POSTは周波数とPOST値、Δの小画面は周波数とΔ値を同じ下段へ配置する。
Δの大画面は既存のPRE、POST、Δ値を保ち、それぞれ実書体の最大幅で割り付ける。
現行の固定X座標と拡張probeによる操作行の省略をSpectrum全体から除去する。
単独表示の読み取りとMARKを退行させないことも本機能の合格条件とする。

### 3.4 中間サイズと実装前の検査

compact表記は共通densityのcompact/focused、詳細表記はstandard以上を使う。
閾値は現行の338、413、525、750pxを正本から取得し、別の閾値を定義しない。
全601幅の有効な3:2寸法とGuide有無を走査し、境界直前、境界、直後を画像でも確認する。

矩形は一つのlayout結果から描画と操作へ渡す。
float差分の診断許容は0.001pxとし、実際の整数配置では隣接領域の重なりを0pxにする。
文字の幅、高さ、背景との可読性を正式書体と各platformのfallbackで検査する。
正式書体で収まらなければ画面寸法gateはfailであり、文字を縮めて通さない。
最初にこのgateを通してから解析実装へ進む。

## 4. 線、軸、読み取りの契約

| 項目 | 採用する仕様 |
| --- | --- |
| Mid | cyan `#75D6E8`、実線 |
| Side | violet `#A695D6`の実線 |
| 線幅 | 両方1.25論理pxに既存`spectrumStrokeScale`を適用 |
| 描画順 | Midを先、Sideを透過付きで後に描き、完全重なりは二色の合成色で示す |
| 面と発光 | M/S用のfill、glow、6秒field、peak holdは描かない |
| 縦軸 | 両系列共通の既存絶対値軸、0〜−96 dBFS。片側だけのauto rangeはしない |
| データ範囲 | 測定floorを含む元の有限値を保持し、plot端でのclipは描画だけ |
| 平滑化 | 既存の表示専用低域calmを同じweightsで両系列へ適用 |
| 更新 | 既存定数の解析30Hz、曲線12Hz、数値2Hzを継承 |
| probe | 共通の周波数から両系列を読む。clickでlockし、×で解除 |
| 禁止する値 | PRE/POST差分、M/S比率、推奨範囲、品質判定、Gain Match |

色定数はM/S専用の意味を持つ名前で定義し、既存のPRE/POST色の変数を別用途へ転用しない。
両線とも既存stroke scaleへ従わせ、cyanとvioletの対応を凡例とtooltipへ記す。
破線、点線、周期的な欠けは使わず、全5サイズで連続した輪郭を保つ。
質感は現行VUの暗部、細い光、数値整列を基準とし、VUそのものは変更対象にしない。
線の重なり、交差、片側floor、低域の急な山を同じfixtureで確認する。
Guideは既存の別情報として重ね、色や値の意味をM/Sへ混ぜない。

両曲線は一つのframeから同時に更新する。
両読み取り値も一つのframeから同時に更新する。
数値は2Hzの保持値、曲線は12Hzの表示なので、両者の更新時刻まで同一とは称さない。
旧い数値位置を最新曲線上の点として描くmarkerは追加せず、probeの縦線だけを共有する。
低域位置の`~`とeditor内に収まるhover helpは既存契約を使う。

M/S中はΔ用のMARKとFocus Trailを表示せず、生成もしない。
単独表示へ戻った場合のfield、peak hold、MARK、Focus Trailは新しい観測定義から再開する。
M/S画面のCaptureは同じ描画componentとsnapshotを使用し、同一画像内で左右の値を別時刻に更新しない。

## 5. 要求状態と復帰操作

Spectrum専用選択を`LR / MID / SIDE / MidSide`、観測targetを`POST / Delta`、subviewを`Spectrum / PSB`として区別する。
processorがloaded instanceの選択を所有し、editorはその結果を表示する。
SHARPとの互換用に最後の単独チャンネル選択も維持する。

四択の表示だけを先に変更せず、検証済みの要求が受理されてから選択を反映する。
解析枠待ちと不正要求を区別し、枠待ちは選択を保持して既存の所有者表示を出す。
値がない時点を測定成功として表示しない。

| 事象 | 必須の状態遷移と表示 |
| --- | --- |
| POST、stereo、単独表示でM/Sを押す | M/Sを選択しΔを無効化。旧frameとinteractionを破棄し、新しい二系列を待つ |
| ΔでM/Sを押す/直接callbackを呼ぶ | UIはdisabled。要求を拒否し、Δと単独モードを維持 |
| M/SでΔを押す/直接callbackを呼ぶ | Δ要求を拒否。単独モードへ戻る操作は常時有効 |
| M/SからLR/MID/SIDEを選ぶ | 選択を受理して単独解析へ戻し、ΔのM/S制限を解除 |
| monoでM/Sを選ぶ | UIをdisabledにし、backendも拒否。既存MIDはmonoでも使える |
| 入力channel数がまだ不明 | M/Sの新規選択はdisabled。有効な保存中選択は準備完了まで待機 |
| M/S中にstereoからmonoへ変わる | 二系列を破棄しLRへ正規化。Δへの自動変更はしない |
| monoからstereoへ戻る | M/Sを選択可能にするが、自動で再選択しない |
| FREQから別domainへ移る | M/S runtimeを停止。次の解析へ枠を引き渡すか解放 |
| 同じinstanceでFREQへ戻る | POSTかつstereoなら記憶したM/Sを再開。Δなら最後の単独選択を使う |
| PSBへ移る | M/Sを休止し、既存の固定LRとtarget操作を使う |
| PSBから戻る | POSTなら記憶したM/S、Δなら最後の単独選択を使う |
| SHARPへ移る | 最後の単独チャンネルを使用。M/S要求をSHARPへ渡さない |
| editorを閉じる/非表示にする | M/Sを停止し、解析枠とUI履歴を解放。processorの選択は保持 |
| editorを再度開く | 復元されたdomain/targetと選択を一度正規化してから再開 |
| 同じinstanceのengine再生成 | 同じstereo条件なら選択を保持。旧handleのframeと世代を排除して再開 |
| DAWを閉じて開く/新instanceを作る | 現行同様LRから開始。M/S、probe、frameをDAW stateへ保存しない |
| 既存instanceへhost stateを読込む | 保存されたdomain/targetを尊重し、M/SとΔの競合は単独表示へ正規化 |
| PRE接続の変更/欠損/旧版PREとの接続 | M/SはPOSTローカルのため継続。PRE接続だけを理由にM/Sの値やlockを消さない |
| 停止/無音/bypass | 現在値を欠測にし、live曲線を表示しない。選択と同じ周波数のlockは保持 |
| transport jump/worker再起動 | 旧frameを破棄して新runを待つ。同じ観測定義なら周波数lockを保持 |
| rate/channel/layout変更 | 旧frameとlockを破棄し、新しい定義で再開 |
| 明示操作の失敗 | 選択を不一致にせず、既存の通知経路で短く通知。操作行を隠さない |

M/Sで別画面のΔを選びFREQへ戻った場合は、表示に使う単独選択と休止中のM/S希望を区別する。
その場でPOSTへ戻す操作だけではM/Sへ自動復帰せず、FREQを開くときの正規化か利用者のM/S選択で再開する。
表示中の四択は常に実効選択を示す。

300/375/450のPOST/Δ兼用ボタンでは、M/S中はPOSTを表示したままΔへの切替をdisabledにする。
600/900の分割ボタンではPOSTを選択表示し、Δだけをdisabledにする。
この制限をFREQのSpectrum以外へ持ち越さない。
tooltipは「M/Sを見るにはPOSTへ」「Δを見るにはLR/MID/SIDEへ」と復帰先を伝える。
hover helpがOFFでもdisabled、選択、復帰ボタンは見える。

## 6. DSP、世代、FFIの構造

### 6.1 一つの入力窓から一つの二系列frameを作る

既存SpectrumAssemblerが整列した一つのL/R窓を再利用する。
同じSpectrumAnalyzerの`analyze_mode`をMID、SIDEの順に呼び、FFT plan、scratch、入力窓を再利用する。
計算途中でworkerを増やしたり、LRや他の解析を並走させたりしない。
同じsample endpoint、layout、runtime generationの結果だけを一つの`MidSideSpectrumFrame`へ格納する。

frameは既存`SpectrumFrame`のMIDとSIDEを所有し、両配列、sample rate、aperture、FFT size、band count、周波数範囲、channel数、endpoint、runtime generationを保持する。
非finite入力だけでなく、合成、FFT、出力の非finiteも拒否する。
片側だけ成功した場合は両方を欠測にし、前の片側で補完しない。

request変更は既存control境界で直列化し、modeとtargetの矛盾する中間状態をworkerへ公開しない。
Spectrum専用のM/S edgeはcoordinatorのcontrol lockで既存analysis mode edgeと直列化し、選択変更ごとに既存runtime generationを更新する。
workerはblockごとにanalysis modeとM/S edgeを読み、既存runtimeのgenerationも照合して他の解析への移動やengine終了を見落とさない。
workerは窓の開始時と公開時の両方でruntime generationを照合する。
公開用slotのlock取得後にも照合し、切替前のframeが遅れて戻る競合を防ぐ。
sample endpointとruntime generationを分離し、loopによる正当な後方移動と旧frameの遅延到着を区別する。
追加の世代管理はcontrol/worker側で行い、Audio Threadへ新しい制御処理を置かない。

### 6.2 二系列を一度に取得する

M/Sは最新frame一個の固定slotで保持する。
6秒history、batch回収ring、peak holdはM/S用に追加しない。
UI stall後は最新の完全な二系列へ進み、未受信の中間frameを補間して作らない。

既存`KirinSpectrumView/Batch`を拡張せず、専用の`KirinMidSideSpectrumView`と追加set/poll APIを設ける。
新set APIはvisible edgeを受け、Rust側でmonoとPRE roleを、processor側の専用選択APIでM/S+Δと未知値を変更前に拒否する。
型と入口は専用の`spectrum_ffi` / `spectrum_mid_side_ffi` moduleとheaderへ分け、既存のpublic入口は互換wrapperとして維持する。
単独表示とSHARPの古いAPIはM/S値を共有channel enumとして受け付けない。

C境界では固定幅整数と固定配列を使う。
pollは一回で両系列と状態を返し、Rust/C++でsizeof、alignment、主要offsetを照合する。
null pointer、未知選択、非finite値、非stereo、不正metadataの試験を用意する。
lock競合時はpollをskipし、出力bufferを途中まで書かない。
M/S中に旧Spectrum pollを呼んでも、古いΔや片側を有効値として返さない。

UIはengine再生成時のrevisionとsnapshot世代を合わせて検証し、古いhandleの結果を再表示しない。
poll競合では直前の完全なframeを一時保持できるが、再生中に新endpointを250ms以上受け取れなければ両値と曲線を欠測へ移す。
同一endpointの再pollやUI時計の進行を新しい測定として数えない。
停止、bypass、明示的な無効状態、要求変更はこの期限を待たずに反映する。

配列本体は`2 × 256 × sizeof(float) = 2,048 bytes/frame`である。
metadata込みの専用ABIは2,304 bytes以下、新規の保持slotとUI配列の合計は一instanceあたり32KiB以下を実装上限とする。
再利用するFFT scratchや既存の他表示のmemoryは別に記録し、全instance数でも測定する。

### 6.3 POSTローカル解析と所有権

M/Sは既存の一つの解析leaseを取得し、最大枠数と所有者表示を継承する。
既存`AnalysisViewMode`のwire値と`SpectrumChannelMode`は維持し、Spectrum内の専用processing選択で単独/M/Sを区別する。
新しい選択はPRE requestへencodeしない。

coordinatorはpair照合より前にM/Sローカル分岐へ進む。
切替前のrequestのうち当該POSTが所有するものだけを既存cleanupで終了する。
競合中のpublicationが古いrequestを再作成しないことを世代とsession lockで検証する。
PREのpair接続とRecordの所有権は変更しない。
macOS atomic fileとWindows共有メモリのwire schemaは変更しない。

Audio Threadは既存のlock-freeコピーと通知を使う。
M/S合成、FFT、allocation、lock、ブロッキングI/Oを追加しない。
通常A経路のbit identity、0 samples latency、offline時の既存動作を維持する。

## 7. 変更対象と先行分離

| 責務 | 対象と変更内容 |
| --- | --- |
| layoutと四択 | 新規`HyphaSpectrumSelection.h`、`HyphaSpectrumHeaderLayout.h`。既存Spectrum geometry/chrome/component/tooltipから共有して使う |
| 描画 | 新規`HyphaSpectrumMidSidePainter.*`、Spectrum painter/chrome、UI色と表示契約、Capture接続 |
| editor制御 | `PluginEditor.cpp`、`PluginEditorAnalysis.cpp`、`PluginEditorObservatory.cpp`、必要なeditor header。callbackと実効選択を共通化 |
| processor | `PluginProcessorAnalysis.cpp`、`PluginProcessor.cpp/.h`。loaded-instance選択、再prepare、旧APIとの対応 |
| shellのΔ制御 | `HyphaObservatoryView.*`、ViewState/Layout、capabilityの入力。FREQ Spectrum内の制限を明示 |
| 解析 | `spectrum.rs`、runtime/assembler/worker/state。新規`spectrum_mid_side.rs`と専用frame |
| coordinator | exchange control/post/view。新規`spectrum_exchange_mid_side.rs`にローカル分岐と所有requestの終了をまとめる |
| ABI | FFI `lib.rs`からSpectrum対象責務を抽出し、専用module/header、既存headerの互換include、追加APIと変換を配線 |
| snapshot比較 | 新しいM/S型の全metadataと両配列を比較。paddingに依存するmemcmpを新規の同値判定に使わない |
| buildと試験 | `juce_shell/CMakeLists.txt`の製品、render test、previewの各source一覧、Rustのmodule公開、FFI/native/source契約 |
| 文書 | README、表示契約、不変条件、CE 2226 visual system、機能完了時の検証記録 |

SHARPの三択、PSBの固定LR、既存wire codec、Record/Referenceは回帰対象であり、機能変更対象にはしない。
ただし共有入口が影響する場合は呼出元まで追って修正する。
現在のJUCE submodule差分と別件の未追跡文書は取り込まない。

調査時点で`spectrum.rs`と`HyphaSpectrumComponent.cpp`は500行、`PluginEditor.cpp`は499行、`spectrum_exchange_post.rs`は497行である。
`PluginProcessor.cpp`は1,080行、FFI `lib.rs`は5,339行、共通FFI headerは839行、CMakeは823行である。
最初のcommitで変更する責務だけを500行以下のmoduleへ抽出する。
製品の振る舞いは変えず、既存ABIと対象試験を確認する。
巨大ファイルのbaselineは同じcommitで現在値へ下げる。
新規owned sourceは未追跡分を含め500行以下とし、CMakeは適用対象規則を確認したうえで共通source一覧の重複も抑える。

## 8. 実装順序と各工程の出口

| 工程 | 作業 | 次へ進める条件 |
| --- | --- | --- |
| P0 配置仕様を実行可能にする | 5サイズと全中間幅のheader、四択、probeを既存共通書体で検査。単独POST/Δも検査する | 正式書体とplatform書体で必須情報が収まる。最小サイズの描画を確認 |
| P1 変更責務の先行分離 | 前節の上限付近のsourceから対象責務を抽出 | 対象の既存試験、ABI、行数gateがpass |
| P2 要求と状態遷移 | processor、editor、shell、backendの一つの要求経路を実装。fixtureで状態表を通す | disabledをcallbackで迂回できず、全遷移で復帰操作が残る |
| P3 解析とABI | 同時窓、世代、最新slot、POSTローカルlease、追加ABIを接続 | 数値、同期、競合、再起動、旧API試験がpass |
| P4 描画と製品統合 | 二本、probe、凡例、通知、Captureを全サイズへ接続 | 全画面画像、操作、単独表示と他画面の回帰がpass |
| P5 最終候補の検証 | 一つの候補で全体gate、性能、実ホストを実施し、文書を同期 | 下記の完了条件に未達がない。実行できなかった項目は未検証と記録 |

P0は製品の解析変更に先行する配置診断であり、P1の先行分離commitを迂回して巨大sourceへ機能追加しない。
開発中は対象試験で進め、全体gateを工程ごとに反復しない。
失敗、共有契約の変更、未解決の懸念がある場合だけ該当検査を再実行する。

## 9. 合格条件

### 9.1 数値、時刻、障害

| 試験 | 判定 |
| --- | --- |
| 同じ窓を単独MID/SIDEとM/Sで解析 | floorを含む全256bandが各単独解析とbit一致 |
| `L=R` | Midが入力のmono解析と一致、Sideは既存floor |
| `L=-R` | Sideが入力のmono解析と一致、Midは既存floor |
| Lのみ/Rのみ | MとSが一致。有効bandで入力mono基準から−6.0206 dB、誤差0.001 dB以内 |
| 共通の±6 dB gain | floorにかからない有効bandで両系列が同量移動、誤差0.001 dB以内 |
| MとSへ異なる周波数を合成 | 対応する線に山が現れ、系列の取り違えがない |
| Mだけ/Sだけのテスト用EQ | 処理しない系列は有効bandで基準との差0.001 dB以内、処理系列は既存EQ fixtureの応答許容内 |
| generation/endpoint/layout/片側欠損 | 不整合frameを公開せず、正常入力の次runで回復 |
| 非finite入力、合成/FFT overflow | 両系列を無効化し、finiteな偽値や古い片側を出さない |
| 切替中に古いworkerを遅延させる | lock前後の世代検査で旧結果を排除 |
| loop、seek、停止/再開 | run世代を分け、正当な新しい後方endpointを受け入れる |
| PRE不在、IO欠損、旧版PRE | M/SはPOSTの実測を継続。PRE requestを増やさない |
| lease満杯、owner解放、worker再起動 | 所有者を表示し、二重取得や枠漏れなく回復 |

数値fixtureの通常振幅を定義し、floorやoverflowを含む試験とは分ける。
rateは8/44.1/48/88.2/96/192/384 kHzを含むcoreの対応範囲を検証し、pluginのhost対応範囲は既存仕様に従う。
blockは1、64、127、256、1024、および受理する上限とその境界を検査する。
coreが受け付けるrateをそのままDAW製品保証とはしない。

### 9.2 五サイズと操作の検査

全601幅×Guide有無で、四択、PSB、MARK予約、probe、解除、文字が領域内にあり、hit領域が重ならないことを検査する。
二本とも、分離、交差、完全重なり、片側floor、両側floor、plot上限外、微小差を含むfixtureを使う。
同時刻の値を確認できることと、線が曲線の形状を失わないことを別に判定する。

5サイズの同一fixture画像を横断比較し、境界直前/直後、hover、lock、warming、INACTIVE、枠待ち、mono disabled、Δ disabledも確認する。
300×200でGuideあり、lockありの状態からLR/MID/SIDEへ戻れることを必須とする。
単独POST/Δのprobe、MARK、Focus Trail、PSB、SHARP、LEVEL/TIME/SPACE/Referenceへの往復を確認する。
2MIXとTRACK/STEM、複数instance、editor閉開、host state読込を含める。

文字は共通の11px下限を維持し、利用可能な文字のcontrastは既存4.5:1契約に従う。
正式埋込書体、macOS fallback、Windows fallbackを区別して記録する。
RetinaとWindowsの100/125/150/200% DPIで、実際の描画とclick位置を確認する。
Captureの2倍出力へlogical sizeを再適用せず、元の選択と凡例と二値を保持する。

### 9.3 性能と全体gate

M/S専用fixtureの定常paint予算は既存Spectrumのサイズ別上限を継承する。

| サイズ | 上限ms/frame |
| --- | ---: |
| 300×200 | 4.5 |
| 375×250 | 6.5 |
| 450×300 | 8.5 |
| 600×400 | 12.5 |
| 900×600 | 22.0 |

同じ測定器で二本表示、hover、lock、Guideありを検査する。
既存の単独Spectrum、Focus Trail、Absolute Spectrumの各予算も緩和しない。
worker時間は一frameの既存cadence内に収まり、対応環境で一枠/最大枠の10分連続再生に定常dropがないことを要求する。
意図的に過負荷を与える試験では、UI側のskipを許容してもAudio Threadが止まらず、負荷解除後に回復することを確認する。
CPUはAudio Thread、解析worker、通信、描画、host全体を分けて記録し、stereo LRとの差と固定memory量を示す。

最終候補では以下を一度まとめて実施する。

- `cargo test --workspace --locked`
- `cargo clippy --workspace --all-targets --no-deps`
- `cargo fmt --all -- --check`
- `bash scripts/check_source_line_budget.sh`
- `bash scripts/test_release_source.sh`と変更対象のsource契約
- FFI変更必須のignored parityとpairing_candidates。各`--ignored --list`の`: test`行を数え、`--ignored --test-threads=1`で全件実行
- Spectrum/SHARP/PSB/Observatory/Capture/ABI/RT透明性の対象native試験
- macOS PRE/POST AU/VST3とWindows PRE/POST VST3のbuildおよび既存plugin検証
- 通常A経路のbit identity、0 samples latency、bypass、offline、可変block、worker障害、欠損ファイル、旧版混在

実ホストでは同一candidateのbuild IDとhashを記録し、Stereo入力のStudio OneでEQを操作する。
macOS AU/VST3、Windows VST3でM/S選択、二本の反応、Δ排他、サイズ変更、停止/復帰、editor閉開を確認する。
Windows検証機の操作前には共通remote-access Runbookを読む。
計画用のfixture、native試験、実DAWで確認したことを区別し、どれかのpassで残りを代用しない。

## 10. 計画段階の診断証拠

再実行用sourceは[配置診断](planning/spectrum_mid_side_layout_probe_20260911.cpp)に保存した。
これは製品にlinkしない計画用プログラムで、現在のshell/geometry/typographyのheaderと文字幅APIを用いる。
計測日2026-09-11、Apple clang 17.0.0、x86_64 macOS、JUCE 7.0.12。
現行`HyphaTypography.cpp`を直接compileし、既存DebugのJUCE module objectを再利用した。

| Editor | 操作領域の残り幅 | M/S最大桁行の残り幅 | 二行の使用高/予約高 |
| --- | ---: | ---: | ---: |
| 300×200 | 10.16px | 31.08px | 32.00/32.00px |
| 375×250 | 17.02px | 84.80px | 32.00/38.30px |
| 450×300 | 23.89px | 92.53px | 34.00/45.40px |
| 600×400 | 38.00px | 167.00px | 37.00/60.00px |
| 900×600 | 66.23px | 332.95px | 45.00/89.20px |

300〜900pxの601幅×Guide有無で1,202条件、寸法違反0件。
最初の診断ではfloat減算誤差で300/301pxの高さ比較が不合格となったため、診断の許容を0.001pxに固定して再実行した。
製品の領域を広げたり、文字を縮めたりして通した結果ではない。

測定したのは現行`monoFont`のmacOS fallback指定`.SF NS Mono`である。
この内部名はfont一覧に列挙されなくても文字幅APIで計測できる。
正式なKMR Waldenburg BookとWindows用Consolasはこの実行では利用できていない。
正式書体/Windowsの幅とglyph、実ホストは未検証である。
したがって1,202条件のpassは「配置案の初期寸法が成立する」証拠に限定する。
単独Δの列、全5サイズのM/S曲線と操作、macOS fallbackの描画は後述の製品native試験で別に確認した。

再実行は既存のJUCE Debug module objectを用い、scratch出力先を`mktemp -d`で確保する。
以下はrepository rootから実行する。
`probe_out`は専用scratch内の実行file、`juce_obj`は今回確認したtest targetのObjects-normal/x86_64を指す。

```bash
probe_dir=$(mktemp -d /tmp/hypha-ms-plan.XXXXXX)
probe_out="$probe_dir/probe"
juce_obj="juce_shell/build-pdc-macos/build/KirinUiRenderContractTests.build/Debug/Objects-normal/x86_64"
clang++ -std=c++17 -DDEBUG=1 -D_DEBUG=1 \
  -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 \
  -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 -DKIRIN_HYPHA_KIMERA_EMBEDDED=0 \
  -I juce_shell/JUCE/modules -I juce_shell/src -I crates/kirin_hypha_ffi/include \
  docs/planning/spectrum_mid_side_layout_probe_20260911.cpp \
  juce_shell/src/HyphaTypography.cpp \
  "$juce_obj/juce_graphics.o" "$juce_obj/juce_core.o" "$juce_obj/juce_events.o" \
  "$juce_obj/juce_data_structures.o" "$juce_obj/juce_gui_basics.o" \
  -framework Cocoa -framework Foundation -framework IOKit -framework Security \
  -framework QuartzCore -weak_framework Metal -weak_framework MetalKit \
  -o "$probe_out"
"$probe_out"
```

この診断は製品のrelease build、公証、配置を行わない。
別architectureや正式書体の検証は、対応する既存native test構成へP0の同じ条件を配線して行う。

## 11. 完了判定、文書、配布

機能実装の完了条件は、P0〜P5を同一候補で満たし、状態表と数値表の未達がなく、全5サイズの画像と実ホストの結果を記録できることである。
配置だけのpass、macOSだけの確認、正常系だけの確認を完成としない。
正式書体、Windows、実DAWの未確認は解消するまで記録に残す。
追加の許可が必要な外部作業は、その時点の具体的な成果物と未実行項目を示して扱う。

実装と同時にREADMEの「一つのchannel viewだけを解析」を単独とM/Sで書き分ける。
表示契約には常設四択、二値probe、Δ排他、PSB/SHARP遷移、M/Sでfield/holdを表示しないことを記す。
不変条件には同時窓の二系列、POSTローカル、世代排除、RT境界を追加する。
visual systemにはM/Sの色、線種、五サイズの配置規則を同期する。

公開工程へ進む場合は、同一version/commitのLS向け署名公証済みPKG、macOS無料ZIPとGitHub Releaseと英日HP、署名済みWindows installerの三チャネルを揃える。
release build、公証、配置を行うセッションは既存Runbookに従ってLS PKGまで準備する。
Windowsのpayload/installer/uninstaller署名とinstall/reinstall/upgrade/uninstallの検証を省略しない。

Notionへの書込みは禁止に従い実施しない。
SECTION:DEV/TASKSは接続がなく未取得で、ローカルの現行契約とhandoffを参照した。
既存エラーログも確認したが、過去のnative passを現在の製品候補の検証結果へ読み替えていない。
次の着手点はWindows native描画と正式書体の確認、続いてStudio Oneでの同一candidate実ホスト検証である。

## 12. 2026-09-11実装候補の検証結果

製品実装は、同じL/R解析窓からMidとSideを連続して解析し、一つの専用frameと一回のFFI pollで両系列を渡す構造とした。
M/SはPOSTローカルであり、PRE要求を作らない。
UIは`LR / MID / SIDE / M/S`の四択を5サイズで常設し、M/SとΔを相互にdisabledとする。
SHARPの三択、PSBの固定LR、既存VU、Audio Threadの通常A経路は変更していない。

Midはcyan、Sideはvioletの連続した実線で描画する。
破線、点線、周期的な間引きは実装しない。
300×200、375×250、450×300、600×400、900×600のnative描画試験は、Side色がplot幅の97%以上を連続して覆うことを直接検査して全件passした。
最小と最大の出力画像も目視し、両値の欠け、ellipsis、線の途切れがないことを確認した。

同じDebug候補によるPRE、POST、UI test targetのbuildは成功した。
UI render contractは5サイズ、hit test、M/SとΔの排他、単独Spectrum回帰、SHARP、PSB、Observatory、Captureを含めてpassした。
M/Sのpaint実測は順に1.25616、1.34462、1.43027、1.55304、1.81631 ms/frameで、予算4.5、6.5、8.5、12.5、22.0 ms/frame以内だった。

Rustの対象試験では、単独MID/SIDEとの256 band bit一致、stereo限定、世代とendpointの整合、POSTローカルexchange、FFIのABIとnull/error pathを確認した。
通常suiteでignoreされる必須suiteは一覧を実測し、parity 20件、pairing_candidates 6件を`--test-threads=1`で全件passした。
`cargo clippy --workspace --all-targets --no-deps --locked`、`cargo fmt --all -- --check`、source line budget、`git diff --check`もpassした。

`cargo test --workspace --locked`はM/S対象を含む各crateとkirin_measureの1,453件までgreenだったが、最後のxtaskで既存のsource-contract 2件がfailした。
いずれもHEAD時点で実装が`HyphaTextButton`と新しい文字組みAPIへ移行済みなのに、testが旧`juce::TextButton`と旧`nativeTextFont(height)`文字列を要求している不一致で、今回の変更差分には含まれない。
省エネ方針に従い、無関係な既存testの修正と全体suiteの再実行は行っていない。

未検証は、正式KMR書体、Windows fallback/DPI/native build、Studio OneでのmacOS/Windows実動、長時間最大枠負荷、release build、署名、公証、配置、三チャネル公開である。
本候補は機能実装とmacOS自動検証までであり、公開readyとは判定しない。
