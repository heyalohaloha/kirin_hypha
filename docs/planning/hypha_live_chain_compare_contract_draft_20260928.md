# 連続PRE/POST比較 契約改定案（G1Rの決定を正本へ落とす下書き）

作成日: 2026-09-28。
改訂: 第2版（同日）。厳しめの見直しと、AAXを重要な対象とする利用者の指示を反映した。主な変更は次の6点である。
- 転送方式を決め打ちしない書き方にした。
- 遅延の報告への依存と、中身のずれの警告（INV-LC7）を加えた。
- 周回単位の取り違えの限界を明記した。
- 定数をhostごとの値にした。
- 自動で戻るときの遷移と表示を加えた。
- AAX（Pro Tools）の条件を加えた（第2.5節、INV-LC8〜LC10）。INV-LC9とINV-LC10は、同日に利用者が推奨を採用した。同日のPro Toolsでの実測（G1記録第9節）で第2.5節を改めた。
状態: 下書き。AGENTS、不変条件表、READMEの正本はまだ変更しない。
[実装計画](hypha_live_chain_compare_implementation_plan_20260927.md)（第14版）の方針どおり、正本は実装の承認時に改める。
本書は、そのときに入れる差分を先に固定し、承認の判断材料にする。

## 1. 入れる決定

| 決定 | 日付 | 根拠 |
| --- | --- | --- |
| 対応の鍵は連続時計（VST3連続時刻、AU render時刻）と、定常区間で較正した差K | 2026-09-28 | [G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第2節 |
| PREの周回（run）を照合に含める | 2026-09-28（第10版） | 同第6節、第7節 |
| 各側の呼出しの空白を時計の不連続として扱う | 2026-09-28（G1R） | 同第7節、第8節 |
| 静かな区間はinfinite tailの報告でplugin sleepを避ける。副作用と報告する範囲を確かめてから採る | 2026-09-28（G1R） | 同第8.4節 |
| 遅延変更の直後に時計で検出できない短い区間の誤対応を許容し、説明書に記す。AUはM1 | 2026-09-28（G1R） | 同第8.3節 |
| 境界規則C（時計の規則で対応を確かめられない間はPOST、確かめられ次第PREへ自動で戻る） | 2026-09-28（G1R） | 同第7節、第8節 |
| 遅延の報告の誤りへの備え: 比較の開始時と定期的に中身のずれを推定し、時計の対応と食い違えば警告する（止めない）。DAWの遅延補償に依存することを説明書に記す | 2026-09-28（見直し。利用者が判断を委任し、推奨を採用） | 同第8.6節 |
| AAX（Pro Tools）をVST3、AUと並ぶ重要な対象として扱い、同じ規則が成り立つかを実測で確かめる | 2026-09-28（利用者の指示） | 本書第2.5節、実装計画第5.4節 |
| AAXはプロが使うので、出来る限り完璧に近く、他のプラグインより高い精度を保つ。品質目標を出荷の条件にする | 2026-09-28（利用者の指示） | 実装計画第5.4節の品質目標 |
| 比較の途中で中身のずれが跳んだら（補償を改めないhostや、報告しないpluginの遅延変更など）、POSTへ倒し、再生の停止と再開の後に戻す（INV-LC10）。AAXのmulti-monoでも比較を提供し、全channelを同じblockで切り替える（INV-LC9） | 2026-09-28（利用者が推奨を採用） | 実装計画第5.4節 |

## 2. AGENTS.md

### 2.1 R-12 製造境界

現行（抜粋）:

> 利用者の明示操作によるReferenceの比較試聴は、この禁止対象に含めない。登録済みの不変なReferenceを試聴用B経路で再生し、試聴コピーにだけ一時的なGain Matchを適用できる。Referenceファイル、A経路、正本のPRE/POST測定・Recordは変更せず、接続、読込、復元だけでBへ自動切替しない。offline render、Reference欠損、検証失敗時はA経路を維持する。

案: 上の段落の後に、次の段落を加える。

> 利用者の明示操作によるlive PRE/POST比較試聴も、この禁止対象に含めない。同じchainのPREが公開した直前の入力を、POSTは時計の規則で対応を確かめた範囲だけ試聴用B経路で出力し、試聴コピーにだけ承認済みのgainを適用できる。PREからPOSTへの転送経路はplatformごとに事前確保し、方式は実測で決める。対応は連続時計、定常区間で較正した差K、PREの周回、各側の呼出しの空白で確かめ、時計の一致や到着だけでは受け入れない。確かめられない区間はPOSTを出力し、PREの選択と承認済みのgainは保持して、確かめられた最初のblockからPREへ戻す。PREの入力、A経路、正本のPRE/POST測定・Recordは変更せず、接続、読込、復元だけでBへ自動切替しない。offline render、検証失敗、説明できない欠落、pair／format失効ではPOSTを維持して比較を中断し、再選択まで戻さない。この対応は、PREとPOSTの間のpluginが遅延を正しく報告し、DAWが遅延を補償することを前提にする。前提が崩れた疑いは利用者に警告し、遅延が変わる設定変更の直後の短い区間に時計で検出できない誤対応があり得ることも示す。

「証明」ではなく「時計の規則で確かめる」と書く。
遅延の報告の誤り（G1記録第8.6節）と周回単位の取り違え（同）は、時計の規則では検出できないからである。

現行（抜粋）:

> Audio Thread（processBlock）は通常計測では読み取り・コピー・通知だけを行う。比較試聴では、非RT側で検証・decode・準備した事前確保済みReference bufferの選択とRT-safeな出力だけを許可する。

案:

> Audio Thread（processBlock）は通常計測では読み取り・コピー・通知だけを行う。Reference比較試聴では、非RT側で検証・decode・準備した事前確保済みReference bufferの選択とRT-safeな出力だけを許可する。live比較では、PREは事前確保済みの転送領域への書込みと周回の公開だけを、POSTは同じ領域の読取り、対応の判定、RT-safeな出力だけを許可する。

### 2.2 3層隔離

現行: `明示的な比較試聴は準備済みReference bufferのRT-safeな選択・出力だけを許可（alloc/lock/IO 禁止）`

案: `明示的な比較試聴は準備済みReference bufferの選択・出力と、live比較の事前確保した転送領域の書込み・読取り・判定・出力だけを許可（alloc/lock/IO 禁止）`

転送領域にfile-backed mappingを選ぶ場合も、RTからfile操作をしない。
確保、権限確認、初回接触は非RTで済ませ、RTでのpage faultの有無はG1-06で測る。

### 2.3 現行製品面の正本

現行: `Reference比較試聴と承認済みのローカルBlindは通常A経路とは別の明示操作である。`

案: `Reference比較試聴、live PRE/POST比較、承認済みのローカルBlindは通常A経路とは別の明示操作である。`

### 2.4 転送方式を正本に書かない理由

G1のプローブはPOSIX共有memoryを使ったが、Studio Proが同じprocessにPREとPOSTを読み込んだ場合だけの結果である。
方式は、次の制約を確かめてからG1-06で決める。

| 制約 | 内容と出典 |
| --- | --- |
| 現行の表示共有 | macOSのPRE表示共有はatomic file、Windowsはpagefile-backed共有メモリ（AGENTS） |
| AUのsandbox | 出荷AUは、resourceUsageに`temporary-exception.files.all.read-write`だけを宣言する（`juce_shell/CMakeLists.txt`、B-130）。[TN2247](https://developer.apple.com/library/archive/technotes/tn2247/_index.html)がresourceUsageに挙げる鍵は、`iokit.user-client`、`mach-lookup.global-name`、`network.client`、`temporary-exception.files.all.read-write`の4つで、共有memoryの例外はない |
| sandboxの中の共有memoryの名前 | POSIX semaphoreの名前はapp group IDを接頭辞にした`GGG/NNN`の形で、最大31 bytes（[Apple DTSの回答](https://developer.apple.com/forums/thread/756420)）。共有memoryもApp Group経由で共有する（[同](https://developer.apple.com/forums/thread/719897)）。app groupはprocessのentitlementであり、hostに読み込まれたpluginが自分で持てるとは扱わない（実装計画第5.1節） |
| 別processのhost | LogicはApple siliconでAUを別process（AUHostingService）で動かすという報告がある（未確認）。PREとPOSTが同じprocessに入るか、sandboxが効くかは、G1-06で確かめる |
| AAXのmulti-mono | Pro Toolsはmulti-monoでchannelごとに別instanceを作る（AAX SDKのPro Tools Guide）。PREとPOSTの組、転送、Kがchannelごとになる（第2.5節） |

### 2.5 AAX（Pro Tools）で成り立つかを確かめる前提

AAXでは次の点がVST3、AUと異なる。2026-09-28にPro Tools Developer 2026.4（Intel Mac、48 kHz）で実測し（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第9節）、その結果を右の列に含めた。
出典は、Avidが公開するAAX SDKの[Pro Tools Guide](https://learn-cdn.avid.com/AAX_SDK_2p1p1/Documentation/Doxygen/output/html/a00274.html)と、手元のAAX SDK 2.9のheaderの説明である。

| 点 | AAXの仕様 | live比較への意味 |
| --- | --- | --- |
| 連続時計 | `AAX_ITransport::GetCurrentNativeSampleLocation`は、再生中だけ、callbackのbuffer先頭のtimeline位置を返す。`AAX_IController::GetTODLocation`は、再生開始からplayheadが進んだsample数を返す。現行のJUCE patchは前者をAAXの補助時計として渡す | 前者はproject時刻にあたり、loopで折り返す。後者は、audio engine全体の値なので、PREとPOSTで同じ値になる見込み（未測定）。pluginが数えるframe数（AUのrender時刻と同じ性質）を連続時計にすると、実測で成り立った |
| 遅延補償された位置 | POSTに渡る位置が、PREとPOSTの間のpluginの遅延の分だけ補償されているかは書かれていない | 実測では補償されていた（中身と位置の差は、照合できた全blockで0）。loopの折返しでもPOSTの位置は中身と一致した。補償されない版やplatformでは、project時刻の一致からKを較正できない |
| 再生中の遅延変更 | 公開ガイド（2.1.1版）は、Pro Toolsは再生中に遅延補償の設定を更新しないと書く。遅延を動的に変えるpluginは、再生中の変更を避けるか、ずれを利用者に示すべきとされる | Pro Tools 2026.4の実測では再生中に反映され、時計（位置とK）が変わった。M1で誤対応は1〜2 block（1024 framesで21〜43 ms）。補償を改めない版やhostでは、ずれは再生を止めて再開するまで続くので、INV-LC10で扱う |
| 遅延補償のOFF | Pro Tools 12.6以降は、遅延補償の全体の有効・無効をpluginに通知する（`AAX_eNotificationEvent_DelayCompensationState`）。JUCEはこの通知を扱っていない | OFFの間は、対応の前提が成り立たないと分かる（INV-LC8） |
| Dynamic Plug-In Processing | Pro Tools 11以降は、一定時間無音のtrackや停止中のpluginを止める。止めさせない方法はdescriptorの`AAX_eProperty_Constraint_AlwaysProcess`で、そのpluginのchain全体を処理させ続ける。SDKは、実際に支障があるときだけ使うよう求める | 実測では、入力が無音になって約8秒後に、再生中も停止中も呼出しが止まった。AlwaysProcessを付けると、停止中も含めてchain全体が呼ばれ続けた。この属性は静的なので、「比較中だけ」は選べない（INV-LC6） |
| loop | loopの終わりから始めへ、pluginの状態をresetせずに続けて処理する | 実測では、callbackはloop終端で分割されず、終端をまたぐblockは折返し前の開始位置で報告された。POSTの位置は中身と一致したまま折り返した |
| offline bounce | 実時間より速く呼ぶ。wall-clockに依存する処理を避けるよう求める | 呼出しの空白の規則は使えない。offlineではA経路を保つ（R-12） |

## 3. 不変条件表（docs/hypha_invariants.md）

live比較の節（INV-LC）を新設する。
不変条件表の規則（各行に実在の試験名を紐づける）に従い、試験名は実装時に確定する。
下表の試験欄は、実装計画第13節の受入試験IDである。
閾値と回数はhost profileの値とし、固定値を不変条件に書かない。
今の値（空白は直前block長の2.5倍かつ20 ms超、較正は同じ候補8回）は、Studio Pro 8.1.2、2048 framesで試しただけの初期値である。
blockの長さが変わるhostや、呼出しが不規則なhostでは、空白を誤って検出し、PREが頻繁に途切れるおそれがある。
例えば、LogicにはProcess Buffer Rangeの設定があり（[Apple](https://support.apple.com/en-us/108295)）、非live trackは先行して処理されるとされる。REAPERには、空いたCPUで先にeffectを処理するanticipative FX processingがある（[Sound On Sound](https://www.soundonsound.com/techniques/running-multiple-plug-ins)）。
どちらも、呼出しの間隔への影響は未測定である。

| ID | 不変条件（案） | 紐づく試験（計画） |
| --- | --- | --- |
| INV-LC1 | PREとPOSTのsample対応は、連続時計と、定常区間でproject時刻の一致から較正した差Kで決める。較正に要る連続一致の回数はhost profileの値とする。project時刻だけ、counter差、wall-clock、相関で対応を決めない | LC-02、LC-04 |
| INV-LC2 | PREは、連続時計が直前のblockと連続しないとき、または自分の呼出しの空白の後に周回を改め、周回番号、先頭、書込み末尾を一貫して公開する。空白の判定値はhost profileの値とする。POSTは今の周回で書かれ、転送領域の容量内にある範囲だけを受け入れ、書込み末尾だけでは受け入れない | LC-04、LC-05、LC-26 |
| INV-LC3 | POSTは自分の呼出しの空白でKを無効にし、較正し直すまでPREを出さない。AUでは候補の食い違いを1回見た時点でもKを無効にする（M1） | LC-20、LC-26 |
| INV-LC4 | 境界（seek、再生開始、loop、sleep、遅延変更）で時計の規則が対応を確かめられない区間はPOSTを出力し、PREの選択と承認済みgainを保持して、確かめられた最初のblockからPREへ戻す。戻すときは既存の対称5 ms遷移を使い、確かめたPREのsampleだけを遷移に使う。確かめられなくなったときは、そのblockの先頭でPOSTへ切り替え、確かめていないPREを遷移に使わない。PREを選んだままPOSTが鳴っている間は、画面にその状態（PRE待ち）を示す。説明できない欠落、protocol／pair／format失効、上限超過では比較を中断し、再選択まで戻さない | LC-06、LC-12、LC-14、LC-26 |
| INV-LC5 | 遅延変更の直後は、時計で検出できた時点で直ちにPOSTへ倒す。検出できない最初の区間を許容するのは、pluginが変更を報告し、hostが再生中に補償を改め、区間の長さをhostごとに実測したprofileに限る。長さはprofileと説明書に記す（Studio Pro 8.1.2、2048 frames、音と報告を同時に変える型で最大4 block、171 ms）。中身の推定で比較を止めたり、Kを補正したりしない | LC-20 |
| INV-LC6 | （副作用と範囲の確認後に採用）PREとPOSTはinfinite tailを報告し、hostのplugin sleepで比較が途切れないようにする。音声とlatencyは変えない | LC-01、LC-21、LC-25 |
| INV-LC7 | 比較の開始時と、その後は定期的に、非RTのworkerでPREとPOSTの中身のずれを推定する。時計の規則による対応と食い違えば、推定したずれと、考えられる原因（遅延の報告の誤り、意図したdelay、強い加工）を事実として示し、警告する。比較は止めず、Kも補正しない。推定できないとき（無音、周期信号、強い加工など）は判定不能とし、警告も「一致」の表示も出さない。比較の途中の跳びの扱いはINV-LC10 | LC-29 |
| INV-LC8 | （案）hostが遅延補償の無効を通知している間（AAXの`AAX_eNotificationEvent_DelayCompensationState`が0）は、対応を確かめられないものとしてPOSTを出力し、理由を画面に示す。通知が有効へ戻れば、Cの規則で確かめ直してPREへ戻す | LC-20、LC-26 |
| INV-LC9 | AAXのmulti-monoでも比較を提供し、channelの間でPREとPOSTを混ぜない。あるblockを最初に判定したPOSTのinstanceが全channelの判定を決めて公開し、他のinstanceはそれに従う。全channelで対応を確かめられたblockだけPREを出す | LC-12、LC-26、LC-33 |
| INV-LC10 | 比較の途中で中身のずれが基準値から跳び、時計は変わらないときは、補償されていない遅延変更の境界として扱う。POSTを出力し、PREの選択は保ち、再生の停止と再開の後に時計と中身で確かめ直して戻す。初めからある絶対値の食い違いは、INV-LC7の警告だけにする | LC-20、LC-29、LC-33 |

INV-LC1は、project時刻の一致が、POSTが聞いている周回のPREの記録と結び付くことを前提にする。
chainの遅延がloop長以上だと、この前提が崩れ、Kを周回単位でずらして較正し得る（G1記録第8.6節。判定コードの作りからの推論で、未試験）。
その場合、中身は多くの場合ほぼ同じだが、周回ごとに変わる音（前の周回からの残響、LFO、live入力）は別の周回のものになる。
前提を確かめる手段と、確かめられない場合の扱い（PREを出さない、またはINV-LC7の警告）は、周回ごとに印が変わるfixtureを使うG1-03で決める。

INV-LC1は、hostがPOSTへ遅延補償済みの位置を渡すことも前提にする（Studio Proでは成り立った。G1記録第2節）。
AAXでは、Pro Tools Developer 2026.4で、この前提が成り立ち、pluginが数えるframe数を連続時計にして方式が成り立った（第2.5節）。
補償済みの位置が渡らない版やplatformでは、時計だけではKを較正できない。
その場合の選択肢（中身による較正を認めるか、そこでは提供しないか）は、事実を添えて利用者の判断を求める。

INV-LC4の自動復帰は、既存のReference試聴と逆の振る舞いである。
Reference試聴は、停止、位置不明、準備未完了でBの選択を解除してAへ戻し、再選択を求める（ReferenceRuntimeV2。実装計画第3節）。
live比較のPOSTは同じ曲の同じ位置の音であり、境界ごとに時計の規則で対応を確かめ直せるので、選択を保つ。
Referenceの規則は変えない。
画面とREADMEで違いを示す。

INV-LC5の許容の外になる型がある。報告が来ない型と、hostが再生中に補償を改めない型である。
[VST3のFAQ](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html)では、latencyの変わったpluginが`restartComponent(kLatencyChanged)`を呼ぶ。hostは`getLatencySamples()`を読み直し、対応していれば補償を改める。
これらの型では、ずれは自然には終わらないので、INV-LC7の警告で扱う。
Avidの公開ガイドはPro Toolsを後者の型と書くが、Pro Tools 2026.4の実測では再生中に反映された（第2.5節）。hostの版ごとにprofileで扱う。
補償を改めないhostでは、警告に「再生を止めて再開すると補償が改まる」ことを示す。

INV-LC6は、現行の`getTailLengthSeconds()`が0を返す実装（`juce_shell/src/PluginProcessor.cpp`）を変える。
[VST3のFAQ](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html)は、`getTailSamples`で`kInfiniteTail`を返すことを、pluginを常に処理させる方法として挙げる。
JUCEはVST3でinfinityを`kInfiniteTail`として返す。
infinite tailを報告する使い捨てのTail Monitorは、VST3でpluginval（strictness 5）に、AUでauval（Tail Timeの検査を含む）に合格した。
AUのhostが`kAudioUnitProperty_TailTime`を読むと、無限大のFloat64（`inf`）を受け取る（2026-09-28に確認）。
報告する範囲は、次の3案から、副作用を確かめて選ぶ。

| 案 | 内容 | 確かめること |
| --- | --- | --- |
| 常に報告 | 比較していないときも報告する | 比較しないときのDAWの振る舞いも変わる。bounceの長さ、trackのfreeze、停止中のCPU、AAX |
| 比較中だけ報告 | 比較の開始で報告し、終了で0へ戻す | hostが再生中にtailを読み直すか（VST3のFAQにtailの変更の通知の記述はない。AUはproperty変更の通知）。読み直さないhostでは効かない |
| 報告しない | 静かな区間の後はCで自動復帰する（G1Rの代替案） | 無音が数秒続いた後、PREが戻るまでの時間 |

AAXには、tailによる手段は定義されていない。
Dynamic Plug-In Processingを止めさせる手段は、descriptorの`AAX_eProperty_Constraint_AlwaysProcess`だけである（JUCEでは`JucePlugin_AAXDisableDynamicProcessing`）。
この属性は静的なので、AAXでは「常に報告」と「報告しない」の2案になる。
付けると、Hyphaを置いたchain全体が、比較していないときも止まらなくなる。
AAX SDKは、実際に支障があるときだけ使うよう求めている。

## 4. README.md

`## Local PRE/POST Blind Compare`の前に、live比較の節を加える。
READMEは英語なので、案も英語で書く。画面の日本語は翻訳catalogに入れる。

> ## Live PRE/POST compare
>
> Switch between PRE and POST of the same chain while the song keeps playing, at matched level. When you select PRE, Hypha plays the input that PRE received, lined up with what POST is processing by the DAW's timing and delay compensation. Where Hypha cannot confirm that line-up, you hear POST, and PRE comes back by itself as soon as it can confirm it again. The display tells you when you hear POST while PRE is selected.
>
> - The line-up relies on every plug-in between PRE and POST reporting its latency correctly. If the audio of PRE and POST looks offset from the DAW's timing, Hypha shows a warning and keeps playing. An intentional delay effect can also cause this warning.
> - Right after a seek or a new start, you hear POST for about the latency of the chain between PRE and POST.
> - If the input stays silent for several seconds, the DAW may stop calling Hypha. PRE comes back shortly after the sound returns.
> - If you change a plug-in setting that changes its latency (look-ahead, oversampling, linear phase) while comparing, PRE can be misaligned for a moment right after the change. How long depends on the DAW and its buffer size.
> - If your DAW does not update its delay compensation during playback and a latency change shifts the audio while you compare, you hear POST until you stop and start playback again. While delay compensation is turned off in Pro Tools, you hear POST.
> - Offline render and a broken or changed pair end the comparison. Select PRE again to continue.

hostごとの実測値（seek直後の長さ、遅延変更の直後の長さ）は、対応host一覧に載せる。
この項の最後の一文（Pro Toolsの遅延補償のOFF）は、INV-LC8を採用した場合に入れる。
sleepの一文は、INV-LC6を採用したら「DAWが止めても比較は途切れない」の趣旨へ差し替える。
短いloopと長い遅延の扱い（INV-LC1の注記）が決まったら、1行を加える。
Referenceとの振る舞いの違い（INV-LC4の注記）は、Referenceの節と本節の両方に1行ずつ書く。

## 5. 正本へ入れる前に確かめること

| 項目 | 内容 |
| --- | --- |
| infinite tailの副作用と範囲 | 第3節の3案。Studio Pro、Logic、Pro Toolsでのbounceの末尾（AUのhostは`TailTime`として`inf`を受け取る）、freeze、停止中のCPU、AAX。VST3のpluginvalとAUのauvalには合格済み |
| 他のhostの遅延変更 | Windowsと他のDAWで、時計で検出できない区間の長さ。報告が先に来る型、報告が来ない型、hostが再生中に補償を改めない型 |
| 定数 | 空白の判定値と較正の連続一致の回数を、hostとbuffer設定ごとに実測で決める。呼出しが不規則になり得るhost（LogicのProcess Buffer Range、REAPERのanticipative FX）で、空白の誤検出によるPREの途切れを数える |
| 周回の特定 | 周回ごとに印が変わるfixtureで、chainの遅延がloop長以上の条件を試す。INV-LC1の前提を確かめる手段と、確かめられない場合の扱いを決める |
| 中身のずれの警告 | INV-LC7の推定の方式（窓、探索範囲、周期）、誤警告（意図したdelay、reverb、強い加工）、判定不能の割合。以前のG1-05の構成を、警告だけの形で使う |
| 転送経路 | G1-06。macOSのPOSIX共有memoryとfile-backed mapping、sandboxのhostと、AUを別processで動かすhost（Logic）での到達性。Windowsのmapping。30分以上 |
| AAX（Pro Tools） | Pro Tools Developer 2026.4（macOS、Intel）では、補償済みの位置、loop、再生開始、Dynamic Plug-In Processing、再生中の遅延変更を確かめた（G1記録第9節）。残りは、WindowsのPro Tools、製品版（PACE署名が要る）、Apple silicon、multi-mono、遅延補償のOFF、`GetTODLocation`、hostの通知を待ってから音の遅延を変える型 |
| M1 | plugin内での実装と、再配置の直後の追加棄却（記録からの再計算ではAUで30 block） |
| 遷移と表示 | 対称5 msでの復帰、PRE待ちの表示の最短時間（短い区間でちらつかない）、Referenceとの振る舞いの違いの伝え方 |
| 実装の承認 | 本書の差分は実装の承認と同時に正本へ入れる。承認前に正本の文言だけを先に変えない |
