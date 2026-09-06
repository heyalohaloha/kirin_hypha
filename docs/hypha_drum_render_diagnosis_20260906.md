# TRACKのDRUMが止まって見える現象の検証

状態: 描画負荷を再現し、重い処理を特定した。修正完了の記録ではない。
対象: Windowsに一時配置したB-726検証版と、同じDRUM描画コードを使う単独UI試験。
通常音声、計測アルゴリズム、音源原本、公開版を変更する検証ではない。

## 実機で確認した状態

Daisukeの「操作を止めたので、検証してください」を受け、Windowsの操作を再開した。
保存済みの `Peach_Hypha_Demo(13)\Peach_Hypha_Demo.song` を使い、曲の再読込みや差し替えは行っていない。
TRACK/STEM、300%表示でLIVEからDRUMへ切り替え、再生、停止、editorを閉じた状態を順に比較した。
追加されていたChorusとTricompを取り消さず、外部機器やデバイス設定も変更していない。

8 logical coresのPCで、各10秒のCPU時間差を測った。
PC全体比と1 core比は別の分母であり、次の2列を直接比較しない。

| 状態 | Studio ProのCPU、PC全体比 | 同じ画面関連thread 17084、1 core比 |
| --- | ---: | ---: |
| LIVE再生 | 63.10% | 97.83% |
| DRUM再生 | 66.32% | 96.56% |
| DRUMのまま停止、描画は黒 | 35.17% | 97.31% |
| 停止したままHypha editorを閉じる | 20.24% | 6.24% |

DRUM解析の名前を持つ2本のthreadは、再生中に合計0.3042 coreを使った。
これはPC全体比で約3.80%に相当するが、Hypha全体の負荷ではない。
停止中は両threadともCPU上位20本に現れなくなった。
停止後も画面関連threadが約1 coreを消費し、editorを閉じると下がることから、表示を開いている間の更新処理に別の負荷がある。
thread 17084を画面関連とするのは開閉比較からの推定で、Windowsのsymbol付きcall stackによる同定ではない。

各状態は順次測定で、再生位置も完全一致していない。
残余20.24%をすべてDAWや他社プラグインの負荷と断定しない。
同じprocessに他のHyphaも残っており、その内訳は未確認である。

## 最適化しても残る描画時間

DAW、音声出力、Rust計測を含まない `KirinAttackUiContractTests` に診断用fixtureを追加した。
31イベントという件数は実機画面に合わせたが、特徴量は合成した強い値であり、実曲の31イベントそのものではない。
600個の波形点、48 kHz stereo、6秒窓、対応済みPRE/POSTを使う。
描画先は300% presetからTIME navigationを除いた872 × 382 logical pixelsのimageである。
実機のWindows 125% DPIでの画面転送時間やDAW全体のframe時間は含まない。

| Windowsのビルド | 31イベント、2段 | 31イベント、重ね表示 |
| --- | ---: | ---: |
| C++ Debug | 2,385.03 ms | 2,071.54 ms |
| C++ RelWithDebInfo | 499.582 ms | 412.447 ms |

初回描画を別測定し、表は10回平均を3組取った中央値である。
Debugの密な240イベント試験と最適化版のbuild/runは一部重なったため、両版の比率を厳密な最適化効果として扱わない。
機能試験は両版ともpassしたが、現行試験にはDRUM描画の時間上限がなく、この遅さでもpassしてしまう。
最適化版でも33.3 msのeditor更新周期を大幅に超えるため、Debugだけが原因とは判断できない。

次に、他の診断build/testが終わったWindowsで、最適化版の描画責務を分けて測った。
初回描画の後に3回平均を3組取り、その中央値を示す。
各行は別の描画条件なので、行同士の加算や差引きを厳密な占有時間として使わない。

| 残す描画 | 2段 | 重ね表示 |
| --- | ---: | ---: |
| 全体 | 446.448 ms | 362.267 ms |
| 波形の流線のみ | 337.615 ms | 227.187 ms |
| イベントの特徴表示と選択詳細のみ | 192.521 ms | 264.178 ms |
| 停止中の黒い画面と見出し | 0.981 ms | 1.196 ms |

DRUM単独の停止画面は軽いが、実機では停止後も画面を開いている間の負荷が残った。
再生中の流線描画と、停止中のeditor更新を別の問題として修正する必要がある。

## 重い処理と更新の構造

macOS Debugの単独試験を3秒samplingし、1,835 samplesのmain call treeを確認した。
重ね表示の波形描画2本が1,317 samples、イベント差分のoverviewが430 samples、選択詳細が85 samplesだった。
波形では `HyphaAttackPainter.cpp` の `drawMeasuredFlow` が、19本の流線と4段階の強度ごとにpathを構築する。
各pathには最大600点を使い、毎paintで `Graphics::strokePath` が線を輪郭へ変換する。
stroke生成と塗りつぶしがcall treeの主要部分を占めた。
sampling中のwall timeは性能比較値に含めていない。

イベント側は `HyphaAttackOverviewGlyphPainter.cpp` が膜、繊維、ぼかしを複数回描く。
重ね表示ではPRE、POST、PREの3回の層を持ち、イベントが多いほど処理が増える。
下部の選択詳細だけを直しても、上部overviewの大きな負荷が残る。

更新側では `PluginEditorAnalysis.cpp` が30 Hzでpollし、同じデータでも `AttackComponent::setSnapshot` を呼ぶ。
`setSnapshot` は無条件で再描画を要求し、`presentationTick(false)` も停止中に毎回要求する。
遅れて届くPREやevent detailのためpoll自体は必要だが、取得確認と再描画要求が分離されていない。
この更新構造はコードで確認した事実である。
停止中の約1 coreの全量をこの2箇所へ帰属させるには、親画面と他のtimer処理を含む実機profileが必要である。

## 修正時の対象と合格条件

1. 流線のgeometry生成、stroke生成、合成を分け、同じgeometryを毎paintで作り直さない。窓の移動と新しい測定値の到着を別に扱う。geometry簡約を使う場合は画面上の誤差上限を試験し、測定値や色の閾値を変更しない。
2. イベントの特徴量に対応する描画を再利用する。PRE/POST、sample rate、generation、全特徴量、寸法、DPIを有効性判定に含める。過去イベントへの遅着や途中要素の訂正をendpointだけで見落とさない。保持数とmemory上限を固定し、終了時に回収する。
3. pollとdirty判定を分離し、同じ内容や安定した停止状態では再描画を要求しない。停止への遷移では黒へ切り替え、再開時は最初の新しい観測を表示する。LOCK中の明示選択は従来の契約を守る。
4. 親画面、背景、非表示のAnalysis、他のtimerを含めて停止中の負荷を測る。opaque宣言だけで透明な角を残したり、表示を隠すだけで解析要求を残したりしない。
5. 100/125/150/200/300%、Windows DPI、mono/stereo、POST単独とpaired、2段と重ね表示、31件と容量上限、内容が毎frame変わる条件を時間試験へ追加する。静止cacheだけで合格にしない。遅着、seek、loop、generation変更、worker再開、無音、LOCK、縮小、editor再開も確認する。
6. 1画面の時間だけでなく、既存の2解析枠を同時に使ったときのUI応答とaudio callbackを測る。30 Hzの33.3 msを両画面で使い切らない予算を設け、実機で再生と停止の操作が遅延しないことを確認する。

通常の音声経路、正本Record、解析の精度を犠牲にする軽量化は行わない。
今回のM/S窓キャッシュは計測側の改善であり、上記のDRUM描画を改善する変更ではなかった。
PSBとBlindを完成とする前に、この描画負荷の検証を通す必要がある。
2MIX向けATTACKやBlindを承認なく次期へ送る決定ではない。

## 再現方法と証拠

`KIRIN_ATTACK_PAINT_PROFILE=1` を指定して `KirinAttackUiContractTests` を実行すると、31件と240件を測る。
さらに `KIRIN_ATTACK_PAINT_PROFILE_LAYERS=1` を指定すると、31件の責務別試験を行う。
環境変数を指定しない通常のUI試験には、長い性能診断を追加しない。
この診断は所要時間を記録するもので、合格を保証するperformance gateではない。

- 実機: `/tmp/hypha-drum-thread-cpu-live.log`、`/tmp/hypha-drum-thread-cpu-drum.log`、`/tmp/hypha-drum-thread-cpu-stop.log`、`/tmp/hypha-drum-thread-cpu-editor-closed.log`。
- Windows: `/tmp/hypha-drum-ui-profile-windows2.log`、`/tmp/hypha-drum-ui-profile-windows-optimized.log`、`/tmp/hypha-drum-ui-layers-windows.log`。
- macOS: `/tmp/hypha-drum-ui-layers-mac.log`、`/tmp/hypha-drum-paint-stack.txt`。
- 画面: `/tmp/hypha-drum-entered-2048.png`、`/tmp/hypha-drum-stop-2050.png`、`/tmp/hypha-drum-editor-closed-2051.png`。

診断中はPRE/POSTの追加配置をしていない。
Windowsは同じ曲を停止し、Hypha editorを閉じた状態にしている。
今回だけの画面取得task `HyphaValidationCaptureB728` は終了を確認して削除した。
取得scriptと画像は保持しており、常設サービスと他taskは削除していない。
Analysis v3、PSBの非ゼロ差分と再起動、Blindの製品接続は引き続き未完了である。

APIの確認には、[CMakeのbuild設定](https://cmake.org/cmake/help/latest/manual/cmake.1.html#build-a-project)、[JUCEのstroke生成](https://docs.juce.com/master/classjuce_1_1PathStrokeType.html)、[Windowsのthread名取得](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreaddescription)を使った。
JUCEの実装は、このrepositoryの7.0.12 `Graphics::strokePath` も照合した。

## 下部の生命体表現についての追加指摘

Daisukeは、下部が有機的に躍動する形ではなく、カチカチ光って見えると指摘した。
求められているのは、斬新さ、読み取りやすさ、Hyphaの世界観を保ち、4項目を一目で判別できる表現である。
以下は再設計の提案であり、新しい見た目の実装や採用決定ではない。

現行 `HyphaAttackSpecimenPainter.cpp` は、固定PNGを4領域へ分け、特徴量に応じて発光量を変える。
別途描く輪郭も正規化したshapeによる変形が小さく、brightnessは主に膜の明るさへ反映される。
選択イベントの切替で特徴量が置き換わり、420 msを基準に光の走査を行う。
描画が遅いと途中の動きを示せないが、速く描けても「4項目を形で読む」という要求が成立するとは限らない。
性能問題と表現上の問題を分けて扱う。

提案の中心は、一つの連続した生体形状に、区別できる4種類の変形を割り当てることである。
4色の点滅を増やすのではなく、色を外しても部位と形の違いを読めることを目標にする。

- STRENGTH：核の大きさや膨張。30 ms ATTACK RMSに対応する。
- BRIGHTNESS：外膜の張りや輪郭の尖り。SHARPNESSに対応する。単に画面を明るくする量にはしない。
- TEXTURE：表面の繊維の密度や粗さ。現行のedge、crest、plateauの複合表示との対応を説明できる形にする。歪み量の直接測定と称さない。
- TRANSIENT：前方への張り出し。局所CONTRASTに対応する。尾の長さを減衰時間と誤認させない。

この対応はデザイン仮説であり、4項目を一つずつ変えた入力と組合せで、独立に判別できるかを確かめる。
BRIGHTNESSとTEXTUREがともに尖りに見える、STRENGTHとTRANSIENTがともに大きさに見える場合は不合格とする。
数値と短いラベルも残し、小さな文字や色の記憶だけへ判断を委ねない。
PREとPOSTの形を重ねて読めなくなる場合も不合格とし、対応した薄い基準輪郭などの比較方法を試す。

動きは実際の発音と観測された包絡へ結び付ける。
無音でも勝手に呼吸する装飾は加えず、停止と無音では黒へ戻す。
新しいイベントへの描画補間は正本の測定値を変えず、音に対する表示遅延を併記して評価する。
多数のpathの毎frame再構築や無制限の粒子に依存せず、少数の制御点と保持量が固定された描画で試作する。
本体へ接続する前に、同じ入力を使う動く試作で判別性とWindowsの描画時間を同時に確認する。

## SHARPNESSとBlindの追加確認

SHARPNESSのPOST単体表示は、前回の指摘がまだ解消していない。
`HyphaObservationPageContract.h` はSHARPを差分へ固定して対象切替を禁止し、FFIは `perceptual_difference` が成立した場合だけ値を出す。
UIの有効判定もPRE、POST、差分の3値を要求し、描画は差分曲線だけを使う。
解析側は各入力のsharpnessを計算しているため、POST単体が計測不能なのではなく、単体の観測を画面へ届ける契約が欠けている。
POSTはPREがなくても単体履歴を表示し、差分を明示選択した場合だけexact PREを要求する形が修正対象となる。
PREの消失時に差分画面を黙って絶対値へ変えることはしない。
通常は単一の解析結果を使い分け、単体表示のために同じ重い解析を二重起動しない。

PRE/POST Blindは開始UIと共通の開始許可が未接続で、現在は利用者が開ける入口がない。
REF側の既存BLINDはKirin OS Reference用であり、PRE/POST Blindがそこに完成しているわけではない。
配置案は、対応する大きなPOST画面の共通操作部から入る比較画面とする。
REFに埋め込まず、Hypha単体利用とKirin OS Referenceの権限を混同させない。
承認済みの制約どおり、既存Analysisの2枠のうちBlindを所有できるのは1枠だけとし、もう1枠には開始不可の理由を示す。
入口の実装だけで完成とせず、同一区間取得、固定Gain Match、Record排他、試聴、Reveal、中断後の減衰保持と明示復帰を通して確認する。
