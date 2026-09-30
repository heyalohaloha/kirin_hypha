# PRE/POST Blindを1回再生にする実装計画

作成日: 2026-09-29
基点: `ca50b04f`
状態: 2026-09-29の利用者承認で製品コードを実装。対象ローカル試験はpass、全体UIのFREQ性能gateはfail。実機配置・公開は未実施。全体UI性能gateと実機matrixの未完了を完了扱いしない。
対象: Hypha単体のPRE/POST Live Blindと記名LISTENの終了。ReferenceとExact Blindは既存の試聴・復帰契約を維持する。

## 1. 結論

PRE/POST Blindの主動線を、固定4秒の取得と2周の匿名試聴から、**一度の連続したDAW再生中にSource 1／2を切り替えるLive Blind**へ変更する。入口は次の二つを同じ状態機械に接続する。

- 最短動線: `BLIND → 固定MATCHを自動準備 → Source 1／2 → 回答`。
- 確認動線: `LISTEN → 明示MATCH → BLIND → Source 1／2 → 回答`。有効なMATCHを再利用する。

さらに、利用者が押す「終了」は比較とMATCHを解除し、通常POST音量へ戻るところまでを含める。画面closeや故障による中断とは別の要求にする。

現行の直接Blindは、同じ場所について次の3通過を要求する。

1. PRE／POSTの固定4秒を取得する。
2. 匿名Source 1を4秒聴く。
3. 匿名Source 2を同じ4秒でもう一度聴く。

この操作量は主機能の既定として採用しない。新しい主動線の完成条件は次のとおりとする。

- DAWの停止、巻戻し、seek、loop設定を求めない。
- `BLIND`の明示押下は開始に必要な一度の固定MATCHを含む。有効なMATCHが既にあれば再解析しない。
- Blind開始から回答まで、DAWの一つの連続した再生runで完了できる。
- `CAPTURE 4 S`を求めない。
- Source 1／2は同じ再生中に何度でも切り替えられる。
- 両Sourceの実出力をAudio Threadが確認した後だけ回答できる。
- 「終了」後にもう一度RETURNを探す操作を通常動線に要求しない。音量上昇の明示と実出力の確認を一つの終了処理に含める。
- Gain Match、対応、ceiling、offline、bypass、Record保護は既存のlive比較契約を維持する。明示終了と不意の中断の復帰契約を分ける。

固定PCMで同一sample範囲を両側とも聴く方式は、`PIN 4 S`から入る任意の**Exact 4 S**として残す。直接の固定4秒Capture入口は主画面とMENUから外す。これにより、Exactを選んだ本人だけが2周の厳密比較を行い、通常のBlindには3通過を要求しない。

## 2. 一回再生の意味と限界

本計画の「一回」は、Blind開始から回答までDAW transportを一度だけ連続再生することを意味する。同じ時刻の同じsampleをPREとPOSTの両方で聴くことは、一つの時間軸ではできない。Live Blindは、連続する楽曲の中で利用者がSource 1／2を切り替え、異なる時点のPRE／POSTを比較する実用的なPreference Listening Trialである。

したがって、次を画面とREADMEで区別する。

| 方式 | 比較するもの | DAW操作 | 用途 |
| --- | --- | --- | --- |
| Live Blind（主動線） | 対応を確認できた現在のPRE／POSTを、固定した音量で匿名切替 | 一つの連続再生。巻戻しなし | 制作中にすぐ判断する通常のBlind |
| Exact 4 S（任意） | 同じrunから固定した同一4秒のPRE／POST PCM | PIN後に同じ範囲をSourceごとに再生 | transient、状態を持つ処理、同一区間が必要な確認 |

Live BlindをABX識別検定、同一刺激の統制試験、音質改善の証明とは呼ばない。Exact 4 SもPreference Listening Trialであり、勝敗や価値判断を出さない。

## 3. 利用者の主動線

### 3.1 最短動線: BLINDから始める

1. 利用者はPOSTでexact PREを選び、`BLIND`を押す。入口は「音量を合わせて匿名比較」と説明する。
2. Hyphaは現在のeditorサイズのまま準備面を開く。DAWが停止中なら再生を案内し、再生中なら同じrunを使う。準備中はPOSTを出力する。
3. 現在のpair、format、runに有効な完全MATCHがあれば再利用する。なければ対応を確認した直近3〜4秒から一度だけ固定MATCHを作る。履歴が短い場合は同じ再生の続きを待つ。
4. PRE増幅がceilingを超える場合だけ、「POSTを必要差分だけ下げて続ける」を選ぶか、ENDで比較を終了する。減衰承認には、下げる量と「終了時に同量戻る」ことを表示する。TP LIMITで匿名試聴を始めない。
5. 固定gainのRT実適用を確認し、非RTでCSPRNG割当を作る。匿名画面の隔離完了後にSource 1を要求する。
6. 利用者はDAWを止めず、`SOURCE 1`／`SOURCE 2`を任意の回数切り替える。
7. 両Sourceの安定出力をAudio Threadが確認した後、`ANSWER`を有効にする。回答は「1が好み」「2が好み」「好みなし」「区別できない」を維持し、回答でRevealする。
8. `END`は未回答からも押せる。trial、AUTO、MATCHを終了し、POSTへ切り替えて通常音量まで戻す。実出力確認後に主画面へ戻る（第5.3節）。

Blind開始から回答まで、transportのstop、seek、loop wrap、固定Capture、同一区間の再演を要求しない。

### 3.2 確認動線: LISTEN → MATCH → BLIND

`LISTEN`では記名PRE／POSTを聴き、本人が`MATCH`を押す。完全MATCHの実適用が確認できたら、その場に`BLIND`を提示する。押下は共通の開始処理へ入り、次の事実を引き継ぐ。

- exact pairと共有ring
- 確認済みの時計対応
- 直近の連続history
- 完全なMATCHと、そのceiling
- 本人が承認して実適用したPOST減衰

Blind開始要求でAUTOを停止し、最新の適用済みgainへのramp完了を待って固定する。AUTO更新中の値や未適用targetをMATCH成立と見なさない。TP LIMIT中はこの文脈内の`BLIND`を有効にせず、MATCHで完全一致にする方法を説明する。主画面の直接入口は引き続き利用でき、必要な承認は共通準備で求める。

有効な完全MATCHがあり、対応も現在成立していれば、再解析待ちを増やさず匿名割当へ進む。MATCH後にpair、format、ceiling、playback run、無許可のgain変更があれば成立情報を破棄する。ボタン表示後から押下までに失効していた場合は、押下時に再検証して共通準備へ戻る。Blindが実際に始まった後の失効では自動再MATCHも自動再開もしない。

ここでいう完全MATCHは「測定窓から計算した差をTP LIMITで切り詰めず適用した」ことを指す。楽曲全体の全時点で同じloudnessになる保証ではない。

### 3.3 Exact 4 S

`PIN 4 S`は`LISTEN`の任意操作として維持する。直前4秒を同一runの固定PCMへし、現行Local Blindのready画面へ渡す。

- 直接の`CAPTURE 4 S`入口は製品面から外す。
- PINは専用Capture通過を増やさない。
- ExactではSource 1／2をそれぞれ同一区間で聴くため2周を要することを、開始前に隠さず示す。
- 記名A/BはExact内の任意操作として維持し、Blindの前提にしない。
- 既存の固定PCM、hash、generation、pair barrier、5 ms範囲端遷移、通常復帰契約は変更しない。

## 4. Live Blindの状態機械

状態はprocessorが所有し、editorだけのboolで音声許可を作らない。保存対象にはしない。

| 状態 | 出力 | 主な操作 | 次の状態 |
| --- | --- | --- | --- |
| inactive | 通常POST、または記名LISTEN | BLIND | preparing、有効MATCHならarming |
| preparing | POST。既存の承認済み減衰だけ適用可 | END | approval、settling、arming、finishing |
| approval | POST。新しい減衰は未適用 | 下げて続ける／END | settling、finishing |
| settling | 承認したgainへramp。割当なし | END | arming、finishing |
| arming | 固定gain。隔離後にSource 1へ遷移 | END | active、finishing、invalidated |
| active | 匿名Source 1または2 | 1／2、ANSWER、END | active、revealed、invalidated、finishing |
| revealed | PRE／POST名を開示したlive切替 | PRE／POST、END | revealed、invalidated、finishing |
| finishing | POSTへ遷移後、承認した通常音量へramp | 要求済み表示。連打で重複しない | inactive（RT復帰確認後） |
| invalidated | POST。実適用減衰があれば保持 | 通常音量へ戻して終了／閉じる | finishing、held |
| held | realtimeでは保持POST、offline／bypassでは入力不変 | RETURN | finishing |

finishingでは比較への再入場を受理しない。callbackが来なければ「復帰待ち」とし、完了を捏造しない。画面は閉じられ、要求は同じprocessorが保持する。

### 4.1 匿名割当

- OS CSPRNGを非RT側で1 bit取得し、Source 1がPREかPOSTかをtrialの間だけ固定する。
- CSPRNG失敗時は割当を作らず、POSTのまま開始失敗を通知する。
- trial開始ごとに新しいgenerationと割当を作る。前回の割当、回答、played receiptを再利用しない。
- Source番号からgain、source、応答時間を推定できる固定規則を作らない。
- Reveal前は名前、gain、meter、色、tooltip、accessibility、status、保存画像、通知に割当を出さない。

### 4.2 Source要求と実出力receipt

UI操作は`requestedStimulus`と単調増加sequenceだけを公開する。Audio Threadは一つのcallbackの先頭でcommand snapshotを取り、対応を確認したsampleだけで既存5 ms遷移を行う。

`played`に数える条件は次のすべてとする。

- request generationとsequenceが現在のtrialに一致する。
- PREの場合は当該blockの全frameで対応が成立している。
- TP guardとnon-finite guardを通過している。
- 5 ms遷移が要求した側へ到達し、その側だけを出したsampleが存在する。
- realtime、非bypassで、ReferenceやExact Blindが出力を所有していない。

Source 1／2それぞれのplayed bitが揃うまでANSWERを受理しない。これは「耳に届いた」ことの証明ではなく、POST callbackがそのSourceを安定出力した事実である。画面は「聴取済み」と断定せず、Sourceの実出力を示す。

遅い旧receipt、切替途中のcrossfadeだけ、POST安全fallback、ゼロframe callbackをplayedに数えない。

## 5. Gain Matchと音量安全

### 5.1 Blind開始を明示MATCHとして扱う

`BLIND`は、匿名試聴と、その開始に必要な一度の固定Gain Matchを明示的に要求する操作と定義する。接続、再生開始、pair復旧だけではGainもBlindも開始しない。

直接入口では既存live historyの最新4秒、最低3秒を使う。BS.1770の連続active policyを先に使い、成立しなければ既存live MATCHと同じ短音policyを使う。結果は±24 dB以内だけ受理する。解析は非RT側で行い、同じwindowを二重解析しない。

手動MATCHと直接BLINDの準備は、同じMATCH処理とprocessor-ownedの成立情報を使う。成立情報はpair、format、run、gain／ceiling、MATCH世代、RT適用receiptへ結び付ける。現在のeditor-only `liveCompareMatched`だけで開始を許可しない。遅い解析結果や承認popupは世代を確認し、END、取消、pair変更後に適用しない。信号不足なら新しい有効windowを待てるが、既存MATCHがある場合の重複解析やBlind中の更新は行わない。

### 5.2 完全一致だけで開始する

- POSTを基準に、PRE試聴コピーへ固定gainを適用する。
- PREを上げてもceiling内なら、そのgainを固定する。
- ceilingを超える場合は、本人の明示承認後だけPREを原音量に保ち、POSTを差分だけ下げる。
- `TP LIMIT`のように両Sourceのlevel差が残る選択は、記名LISTENでは維持するがBlindでは開始条件にしない。
- 両側normalize、AUTO、再生中の追従、無断clampは行わない。
- Blind中の新しいMATCHは受理しない。終了して新しいtrialを開始する。

### 5.3 「終了」で通常音量まで戻す

同じlive sessionで本人が承認したPOST減衰はLive Blindへ継承できる。別sessionや復元stateからの減衰承認を推定しない。

現行コードを読んで確認した事実は次のとおり。利用者の実機で起きた現象を再現した結果ではない。

- `PluginEditorLiveCompare.cpp`のENDは`stopLiveCompare()`を呼び、MATCH／AUTOのUI状態を消す。
- `PluginProcessorLiveCompare.cpp`の`stopLiveCompare()`はPRE選択とringを解放するが、`postTarget`を戻さない。session終了後もAudio ThreadがPOSTへ減衰を適用する。これは現行INV-LC14の意図どおりである。
- RETURNは`postTarget = 1`を要求し、その直後にeditorが「POSTを通常の音量に戻しました」と表示する。`Status`は目標値しか公開せず、RTの実gainと復帰完了を確認していない。
- AUTOメニューの「AUTOを止める」は追従だけを止め、固定MATCHを残す。これをMATCH解除や比較終了と呼ばない。

計画上の「終了」は、記名LISTENとLive Blindの両方で次の一つの処理を要求する。

1. 開始・解析・Source要求の世代を失効させ、回答を閉じ、AUTOを止める。以後PREやBlindを復活させない。
2. 対応が有効なら既存5 ms遷移でPOSTへ戻す。対応が不成立なら当該blockからPOSTへ退避する。PREがまだ混ざる間にPRE gainをunityへリセットしない。
3. POSTへ戻った後、承認済みの通常音量復帰を実行する。既存`PostLevel`の全範囲500 msの上昇勾配を維持する（実所要時間は残差による）。POSTをunityより上げない。
4. 同じ終了世代のRT receiptで「PRE成分0、POST gain 1、比較出力なし」を確認し、MATCH成立情報を消して主画面へ戻る。ring等の退役と必要な所有権解放はRTの利用終了に合わせる。

減衰がなければ、POSTへの復帰だけで完了する。通常の明示終了では二回目のRETURN押下を求めない。音量が戻ることを次のように事前に伝える。

- 記名LISTEN: 終了操作に「通常音量へ戻して終了」を明記し、POST減衰中は上昇量を同じ操作領域へ表示する。小画面でも量をtooltipだけに隠さない。
- 直接BLIND: 減衰承認に「POST −x.x dB／終了時に +x.x dB戻る」を併記する。匿名画面の終了は「通常音量へ戻して終了」とし、Source別のgainを出さない。
- LISTENからBLIND: 現在の減衰量と終了時の戻り量は記名面で既に見えるようにする。匿名化後は終了の意味だけを維持する。承認記録を持たない旧状態や別条件の減衰を引き継ぐ場合は、匿名化前に戻り量を示して確認する。

終了押下は通常音量へ戻す明示要求であり、減衰を採用しただけで自動復帰する許可にはしない。Blind未回答からのENDはtrialを先に破棄するため、その後にPOSTへ戻る音や音量から回答を続けられない。

回答とRevealでは試聴を継続し、POST減衰を解除しない。故障とeditor close/hideでは試聴を中断して減衰を保持し、表示可能になった面に「試聴は終了／POSTは −x.x dB」とRETURNを残す。一度明示した終了要求は、その後editorを閉じても取消さず、processorが復帰まで処理する。

停止中にcallbackが来ないときは「復帰待ち」を表示し、次の利用可能なrealtime callbackで完了を確認する。offline、bypass、他のauditionが出力を取るblockは無変更とし、それを通常realtime復帰完了のreceiptに流用しない。host停止による待ちと、操作が無視された状態を表示で区別する。

## 6. 匿名性と失効

live経路はPREだけが対応待ちになる可能性がある。この待ちを同じ割当のまま見せると、切替応答からPRE／POSTを推定できる。したがって、Blind active中は通常LISTENの`PRE WAIT`方式を使わない。

次のどれかが起きたtrialは、その場で割当と回答権を失効させる。

- 時計対応がacceptedでなくなった。
- content offsetのjumpが確定した。
- Pro Toolsの遅延補償がOFFになった。
- PRE ring、pair、format、sample rate、channel layoutが変わった。
- TP／non-finite guardが作動した。
- stop、seek、loop wrap、説明できないcallback gapが起きた。
- offline、host bypass、別auditionの出力所有が始まった。
- editorが閉じた、隠れた、別insertに置き換わった。

失効時は当該blockの先頭からPOSTを出す。Source番号と原因を結び付けず、「Blindを中断しました」と、修復に必要な事実だけを通知する。対応が戻っただけでは同じ割当を再開しない。利用者がもう一度BLINDを明示したとき、新しいgenerationとCSPRNG割当で開始する。

準備中はまだ割当が無いため、停止や信号不足でtrial失効とはしない。POSTを維持し、再生または信号を待つ。ただしpair、format、ownerが失効した場合は準備を終了して理由を示す。

## 7. 画面契約

### 7.1 主画面の入口

- 現在の`PRE/POST BLIND`／`BLIND 300%`は直接Live Blindを開く。名前から`300%`を外す。
- 100%〜150%ではMENUから開ける。300%への自動拡大はしない。
- `LISTEN`は記名live比較として残し、完全MATCH後の`BLIND`を同じ開始処理へつなぐ。二つの入口をユーザー設定で選ばせない。
- Exactの直接Capture入口はMENUから外し、`LISTEN`内の`PIN 4 S`だけを入口にする。
- Hybrid VU、5.1、PRE、未対応AAX multi-monoでは既存gateを維持する。

### 7.2 Blind画面

新しい`HyphaLiveBlindComponent`を作り、300×200から900×600まで現在サイズのまま表示する。既存Local Blind全画面へ状態を追加しない。

| 状態 | 100%／125% | 150%〜300% |
| --- | --- | --- |
| preparing | `POST`、短い状態、`END` | `LEVEL MATCHを準備中／今はPOST`、`END` |
| active | `1`、`2`、`ANSWER`、`END` | `SOURCE 1`、`SOURCE 2`、`ANSWER`、`END` |
| revealed | `PRE`、`POST`、`END` | Source番号とPRE／POSTの対応、回答、`END` |
| finishing | `復帰中`または`復帰待ち` | 同じ状態と終了要求済みの説明 |
| held／invalidated | `RETURN`と上昇量 | 試聴終了と保持中のPOST減衰、`RETURN +x.x dB` |

ENDの説明と戻り量は第5.3節に従う。小画面の記名LISTENでもMATCHとその後のBLINDへ到達できる操作メニューを残す。幅不足を理由にMATCH、BLIND、終了、復帰のいずれかを行き止まりにしない。

ANSWERはpopupで4回答を出す。4つの回答buttonを常時並べず、小画面でもSource切替を主操作として残す。両SourceのRT receipt前は無効である理由を同じ面へ示し、古いpopupやshortcutから回答が来ても受理しない。

Source要求中は要求先を押下状態にしない。RT receipt後だけ現在Sourceを点灯する。切替が通常の画面更新より短くても、古いSourceを現在値として残さない。

### 7.3 隔離

Blind画面の表示中は、背後のmeter、pair、Capture、Reference、INFO、MENUをmouse、keyboard、accessibilityから隔離する。正本の計測とRecordは継続する。表示解析の追加枠は解放できるが、RecordやMeasure engineの所有権を奪わない。

既存`setLocalBlindIsolation`を固定Blind専用のまま重複実装せず、`setComparisonIsolation`へ責務を抽出してLocal BlindとLive Blindから利用する。editor close/hideの終了契約も二つの画面で同じownerから呼ぶ。

### 7.4 英日とR-22／R-26／R-28

- title、状態、通知、tooltip、accessibility、回答を英日両方で用意する。
- 「良い」「改善」「正解」「勝ち」を表示しない。
- 内部の任意診断失敗は無言でskipする。
- Blind、Source、ANSWER、END、RETURNという本人の明示操作が失敗した場合は、誤って進んだと思わせないよう通知する。
- 100%の状態帯と300%の全幅で、省略記号なしに収まる文だけを採用する。

## 8. 実装構造

### 8.1 新しい責務

| 新規ファイル | 責務 |
| --- | --- |
| `juce_shell/src/live_compare/LiveBlindSession.h/.cpp` | 非永続のphase、generation、CSPRNG割当、Source command／receipt、played mask、answer、reveal。音声bufferを持たない |
| `juce_shell/src/live_compare/LiveCompareCompletion.h/.cpp` | LISTEN／Live Blind共通の明示終了要求、RT実gain／復帰receipt、保持と完了の状態。close由来の中断を区別 |
| `juce_shell/src/PluginProcessorLiveBlind.cpp` | 非RTのprepare/start/select/answer/endと、Audio Thread reportからの失効・receipt公開 |
| `juce_shell/src/PluginEditorLiveBlind.cpp` | 入口、準備poll、approval、AUTO停止、回答、終了、通知、Exactとの遷移 |
| `juce_shell/src/HyphaLiveBlindComponent.h/.cpp` | 全サイズの匿名画面、操作、accessibility、英日表示 |
| `juce_shell/tests/live_compare/live_blind_session_test.cpp` | 状態、乱数、sequence、receipt、失効の単体試験 |
| `juce_shell/tests/live_compare/live_compare_completion_test.cpp` | 減衰の有無、END／closeの違い、遅いreceipt、停止・offline・bypass中の終了 |
| `juce_shell/tests/live_blind_product_test.cpp` | 実PRE／POST processor、C ABI、ring、editor、一つのtransport runを通す製品試験 |
| `juce_shell/tests/LiveBlindUiContractTest.h` | 全サイズ、英日、秘匿、回答、復帰、操作可能性 |

新規owned sourceは各500行以下にする。`PluginProcessorLiveCompare.cpp`と`PluginEditorLiveCompare.cpp`へ状態機械を積み増さず、既存live比較の開始・renderer・MATCHを利用する薄い接続だけを加える。`PluginProcessor.h`は調査時499行なので、宣言追加前にlive比較の状態型・API宣言境界を整理し、500行超へ増やさない。

### 8.2 変更する既存責務

| 既存ファイル | 変更 |
| --- | --- |
| `juce_shell/src/live_compare/LiveCompareProcessorState.h` | MATCH成立情報、Live Blind owner、RT-safeなcommand／receipt／失効facts、実POST gainと終了世代を追加 |
| `juce_shell/src/live_compare/LiveCompareSession.h` | `RenderReport`に遷移完了側、安定出力、gain実適用、POST unity復帰の事実を追加 |
| `juce_shell/src/PluginProcessor.h` | Live Blindの非RT APIとsnapshotを宣言 |
| `juce_shell/src/PluginProcessorLiveCompare.cpp` | renderer reportをLive Blind ownerへ渡す。明示終了と不意のstopを分離し、ring不在時もPOST復帰receiptを公開する |
| `juce_shell/src/PluginEditorLiveCompare.cpp`、`PluginEditorLiveCompareAuto.cpp` | MATCH再利用、AUTO停止と固定、ENDの共通処理、目標値だけで完了通知しない表示 |
| `juce_shell/src/PluginProcessorAudition.cpp` | 出力所有順を明示し、Exact／Reference出力時にLive Blindを成立させないことを確認。原則として順序は変えない |
| `juce_shell/src/PluginEditor.h` | Live Blind画面とeditor-owned preparation表示を追加。音声許可は持たない |
| `juce_shell/src/PluginEditorLifecycle.cpp` | close/hideで試聴を中断し減衰を保持。明示ENDが先に要求済みなら復帰要求を保持 |
| `juce_shell/src/PluginEditorObservatory.cpp`、`PluginEditorAnalysis.cpp` | Blind表示中の隔離、解析表示需要、refreshを共通化 |
| `juce_shell/src/HyphaObservatoryView.h`、`HyphaObservatoryLiveCompare.cpp`、`HyphaObservatoryViewFooter.cpp` | BLINDを新入口へ接続。LISTEN中にBLINDを出す。PINをExactと明記 |
| `juce_shell/src/PluginEditorLocalBlind.cpp` | 直接Capture入口との接続を外し、PIN済みExactと復帰回収に限定 |
| `juce_shell/src/PluginEditorMenu.cpp` | 小画面のBLINDをLive Blindへ変更。直接Exact Capture項目を削除 |
| `juce_shell/src/HyphaJapaneseBlind.cpp`、`HyphaJapaneseObservatory.cpp`、`HyphaJapaneseNotices.cpp`、`HyphaJapaneseMenus.cpp` | 英日で自動準備、終了、復帰待ち、AUTO停止を区別し、Exactと分離 |
| `juce_shell/tests/LiveCompareFooterContractTest.cpp`、`live_compare/live_compare_session_test.cpp` | 全サイズの終了と戻り量、MATCH後のBLIND、音声遷移と復帰完了を検証 |
| `juce_shell/CMakeLists.txt`、`juce_shell/cmake/LiveCompare.cmake`、`LocalBlind.cmake` | sourceと新しいnative/product test targetを追加 |
| `xtask/src/rt_safety_live_compare_tests.rs` | Live BlindのRT禁止、所有順、AUTO停止、offline、close、Source receiptのsource contractを追加 |

`kirin_hypha_ffi`のABI追加は予定しない。live ringとGain解析の既存C ABIだけを使う。FFI変更が必要になった場合は、通常workspace試験に加えignored parityとpairing_candidatesを一覧で実測して全件実行する。固定件数を完了根拠にしない。

## 9. Audio Thread契約

Live Blindは既存live比較のAudio Thread境界を維持する。

- PRE: demand中だけ事前確保済みringへ入力と時計を公開する。
- POST: ringを読み、対応を判定し、承認済み固定gainと5 ms遷移を試聴出力へだけ適用する。
- 通常計測、Record、PRE/POST正本値は出力切替より前の入力を扱う。
- alloc、free、lock、sleep、file I/O、CSPRNG、Gain解析、文字列生成、host queryをAudio Threadへ入れない。
- commandはcallback先頭で一度だけsnapshotする。途中でUI commandが変わっても、そのcallbackのreceiptを新commandへ付けない。
- correspondenceを失ったblockではPRE sampleを1 frameも使わず、block先頭からPOSTにする。
- offline、bypass、3ch以上、別audition出力時は入力を変更しない。

Live Blindは固定PCMを追加保持しない。既存ring、renderer scratch、4秒historyを使い、常駐PCM bufferを追加しない。新しい状態は固定長atomicと小さい非RT objectに限定し、その増分も計測する。

## 10. 排他と保存

- Live Blind、Exact 4 S、Reference B/C/Blindは同時に一つだけが出力を所有する。
- Keep／Recordは正本入力を継続できるが、既存の開始排他と解析枠を回帰試験する。
- Live BlindのSource、割当、回答、receipt、Gain、phaseをDAW state、Work、Record、plugin_dataへ保存しない。
- Source切替、回答、Reveal、表示更新でparameter gesture、dirty通知、Undo項目を作らない。
- project restore、processor再生成、editor reopenだけでBlindを再開しない。
- editor reopen時はPOSTから始める。明示ENDが未要求なら保持減衰をRETURNで回収し、要求済みなら復帰の進行を表示する。
- realtimeと区別できない書き出しを自動推定しない。hostがofflineを通知したbufferには最初から比較出力と減衰を適用しない。

## 11. 契約文書の改定

実装の最初の契約commitで次を更新する。

| 文書 | 改定 |
| --- | --- |
| `docs/hypha_invariants.md` | Live Blind invariantを追加。INV-LC12〜LC16へ二入口、MATCH再利用、AUTO停止、失効を接続。INV-LC14の明示復帰をENDとRETURNへ拡張し、close時の保持と区別。INV-S25／S43はExactの画面契約として範囲を明記 |
| `README.md` | PRE/POST Blindの主説明を一回再生へ置換。現行4秒手順は`Exact 4 S via PIN`へ移動 |
| `docs/hypha_comparison_safety_contract_20260914.md` | live匿名切替と明示終了のoffline、Undo、復元、出力所有、receiptを追加。Reference／Exactの復帰契約は維持 |
| `docs/hypha_pre_post_blind_usability_plan_20260914.md` | 旧3通過を既定とした計画をsupersededとし、本計画を正本に指定 |
| `docs/planning/hypha_live_chain_compare_implementation_plan_20260927.md` | LISTENからLive Blindへの遷移、Exact PINとの役割分離、追加受入試験を追記 |
| `docs/hypha_meter_product_contract_20260831.md` | Local Blindを主Live Blindと任意Exactへ分け、入口と対応formatを更新 |

R-12の通常A経路、0 samples、bit identity、試聴だけのB経路という境界は変更しない。通常音量復帰を明記したENDを利用者の明示要求とし、閉じただけの自動増大を許可しない。DAW設定、元音源、Reference、正本測定は変更しない。

## 12. 実装順序

### P0 契約と失敗する試験

1. 上記文書を二つの入口と明示終了へ改定する。
2. 現行のENDでPOST減衰が残ることと、RETURNの完了表示がRTより先行することを製品fixtureで再現し、新契約に対する回帰試験にする。`LiveBlindSession`の単体試験も追加する。
3. 両入口で一つのtransport run、0 Capture、0 seek、0 loop wrapを要求するproduct testを追加し、未実装でfailすることを確認する。
4. UI契約へ全5サイズ、英日、tooltip、accessibilityの秘匿試験を追加する。

### P1 終了の共通化と状態・receipt

1. `LiveCompareCompletion`を実装し、既存LISTENの明示ENDをPOST unity復帰まで接続する。close、停止、offline、bypassを分け、RT完了前の成功表示をなくす。
2. 共通MATCH成立情報と`LiveBlindSession`を各500行以下で実装する。
3. CSPRNG失敗、古いsequence、連打、Reveal、再入場、played mask、回答gateを完成する。
4. rendererの遷移・gain実適用reportを追加し、終了以外の通常LISTENのgolden出力が変わらないことを確認する。

### P2 processor接続

1. `PluginProcessorLiveBlind.cpp`で両入口共通のprepare、MATCH再利用／一度の計算、start、select、answer、共通endを接続する。
2. blind active時の対応喪失を同じblockからPOSTへ倒し、trialを失効させる。
3. POST減衰、ceiling、guard、offline、bypass、output owner、pair／format変更を接続する。END後の遅い解析・承認結果を拒否する。
4. Audio Thread source contractとallocation監査を通す。

### P3 製品画面

1. `HyphaLiveBlindComponent`を全サイズ対応で実装する。
2. 共通のcomparison isolationを抽出し、Local／Liveの両Blindへ適用する。
3. 直接BLINDとMATCH後のBLIND、準備、approval、Source、ANSWER、Reveal、END、異常中断後のRETURNを接続する。終了時の戻り量も英日・全サイズで示す。
4. AUTOをBlind開始前に停止し、固定gain以外を表示・適用しない。

### P4 旧3通過動線の降格

1. `PRE/POST BLIND`と小画面MENUをLive Blindへ付け替える。
2. 直接`CAPTURE 4 S`を製品入口から外す。
3. PINだけをExact 4 Sの入口にする。
4. Exactの既存native/product試験を維持し、機能を壊していないことを確認する。

### P5 自動検証と実寸確認

1. native unit、processor/editor product、UI render、英日、source contractを完了する。
2. 300×200、375×250、450×300、600×400、900×600の実画像を確認する。
3. source line budget、Clippy、Rust workspaceを通す。
4. CPU、memory、切替sample、出力相関を数値で記録する。

### P6 DAW実機

認定対象を同じcommitで次の順に確認する。

1. macOS Studio Pro VST3: stereo 2MIX、mono TRACK、複数buffer。
2. macOS Studio Pro AU: 同条件。
3. macOS Pro Tools AAX: stereoと組の唯一のmono instance、PDC ON/OFF、Target OFF。
4. Windows Studio Pro VST3: 同じCI artifact。
5. Windows Pro Tools AAX: signed artifact、stereoとmono track。

各hostで、通常gain、承認POST減衰、明示END、closeによる保持、停止中の復帰待ち、seek、bypass、offline bounce、editor置換、隣接pluginのUndoを別々に記録する。Windows検証前は指定runbookを読み、同一commitのartifactを使う。Merging機器を使う場合だけAudio Routesのmanual/status/begin/confirm/restore手順を先に行う。外部送信、実機接続・設定変更が必要な段階ではその操作の権限を確認する。

公開リリースは本実装とは別の承認範囲である。公開時はmacOS pkg、HP zip/GitHub/英日リンク、同一commitの署名済みWindows installerを揃える三チャネルgateを省略しない。

## 13. 自動試験

| ID | 試験 | 合格条件 |
| --- | --- | --- |
| LB1 一回再生 | 直接BLIND、LISTEN→MATCH→BLINDの両方でSource 1、2、回答までhost clockを単調増加 | stop 0、seek 0、loop wrap 0、Capture要求0、回答成功 |
| LB2 匿名割当 | CSPRNGの両割当でSource 1／2を切替 | 実出力のPRE／POST相関とRevealが一致。開始ごとにplayed／answerを初期化 |
| LB3 receipt | 連打、遅いreceipt、crossfade、ゼロframe、旧generationを注入 | stable outputだけをplayedに数え、両bit前の回答0 |
| LB4 Gain | 0、正負、±24 dB、ceiling境界、承認、取消、TP LIMIT済みLISTENから移行 | 完全一致だけ開始。AUTO変化0。POST増幅0。無断clamp 0 |
| LB5 通常透明性 | 通常inactive、減衰なしの準備・取消・失敗、終了完了、offline、bypass | 通常・対象外blockはbit identical、latency 0、測定／Record不変。承認減衰保持は別条件で照合 |
| LB6 失効 | clock不成立、jump、PDC OFF、pair/rate/layout変更、guard、stop、seek、loop、gap、editor close | 当該blockからPOST、回答不可、同じ割当の自動再開0 |
| LB7 秘匿 | active中の画面、通知、tooltip、accessibility、画像、statusを走査 | PRE／POST名、gain、割当、待ち側の漏洩0。失効理由とSource番号を結び付けない |
| LB8 UI | 全5サイズ、英日、keyboard、screen reader、popup回答、END／RETURN、MATCH後の入口 | 操作欠落、重なり、省略、背後focus 0。終了時上昇量はtooltipだけに隠れない |
| LB9 排他 | Exact、Reference B/C/Blind、Keep、Capture、別POSTと開始競合 | 出力ownerは1つ。旧commandが別ownerへ作用0 |
| LB10 ライフサイクル | hide/show、project restore、processor再生成、CSPRNG失敗、PRE消失 | 自動再生0。同一processorでは実減衰と要求済み復帰を正しく保持。再生成で試聴を復元しない |
| LB11 RT | source契約、allocation counter、最大block、可変block、mono/stereo | Audio Thread alloc/lock/I/O/wait 0。3ch以上はPOST |
| LB12 数値 | S-1〜S-5、既知±6 dB、極性識別、非有限値、silence | source相関、gain誤差、ceiling、切替5 ms、出力sample数を期待値と照合 |
| LB13 Exact回帰 | PIN→ready→記名A/B任意→2 pass→回答→RETURN | 固定PCM、hash、generation、既存native／product契約を維持。直接Capture入口は見えない |
| LB14 Undo／保存 | Source切替100回、回答、Reveal後、隣接plugin編集をUndo、project再open | gesture／不要dirty 0、隣接編集を戻せる、Blind再開0 |
| LB15 負荷 | 48/96/192 kHz、64/256/2048 frames、30分、Source連打 | process全体の既存0.1%目標を維持。PCM buffer追加0、欠落・data race 0 |
| LB16 終了 | LISTEN／Blind、未回答／Reveal、PRE／POST、減衰0／−6／境界値、END連打 | END一回でPOST unityへ。AUTO停止、旧Source拒否、gain上昇の事前表示、実復帰前の成功表示0、二回目RETURN不要 |
| LB17 復帰待ち | END直後close、callback停止、offline／bypass／他owner、旧世代receipt | 既要求ENDは保持。未要求closeは減衰保持。対象外block無変更、実復帰の誤認0、次の適用可能callbackで正常完了 |
| LB18 MATCH再利用 | 有効／TP LIMIT／期限切れ、AUTO ramp中、準備中END、遅い承認 | 有効MATCHの再解析0、未成立は共通準備、匿名中gain更新0、終了後に旧解析が適用される件数0 |

`kirin_hypha_ffi`を変更しない通常経路では、最低限次を実行する。

```bash
cargo test --workspace
cargo clippy --workspace --all-targets -- -D warnings

# 対象native（構成後のbuild directoryで実行）
ctest --test-dir <juce-build> --output-on-failure \
  -R 'kirin_live_blind|kirin_live_compare|kirin_local_blind|kirin_ui_render_contract|kirin_product_entry_contract'

cargo test -p xtask rt_safety::live_compare_tests -- --nocapture
scripts/check_source_line_budget.sh
node scripts/check_screen_text.mjs
```

target名とfilterは実装時にCMake／Rustが実際に列挙する名前で確認し、存在しないfilterの成功を試験完了に数えない。既存の通常suite、shell parity、whitespaceも実行する。

## 14. 実機受入

各formatで次の一つの閉じた手順を通す。

1. PRE→既知gain／既知delay→POSTを挿す。
2. 直接BLINDとLISTEN→MATCH→BLINDを別trialとして、それぞれ一つの連続再生で開始する。
3. Source 1／2を複数回切り替え、外部または後段の測定で実出力を記録する。
4. 途中でstop、seek、PDC変更、bypassを別trialとして注入し、失効とPOST退避を確認する。
5. 正常trialでは両Source出力後に回答し、Revealと測定した割当を照合する。
6. 正常trialではEND一回だけで通常POSTのlevel、bit identity、0 samplesへ戻ることを確認する。未回答からの終了、停止中の要求、closeによる保持とRETURNは別trialで行う。
7. offline bounceが比較なしの対照と一致することを確認する。
8. 同じsessionでExact 4 SをPINから一巡し、旧経路の回帰を確認する。

操作量として次を記録する。

- Blind開始から回答までのtransport開始回数: 1
- stop／seek／loop設定／巻戻し: 0
- Capture操作: 0
- Source選択回数と、各要求からRT receiptまでのcallback数
- 準備待ち時間、MATCH解析時間、Source切替の5 ms遷移
- 有効MATCHからの再解析回数: 0
- 正常な明示終了の操作回数: 1。復帰要求から実POST unityまでのsample数と、表示が完了する順序
- 失効時に誤ってPREを出したsample数: 0

## 15. 完了条件

Live Blindは、次がすべて揃ったときだけ製品実装完了とする。

- 二つの入口の両方から一つの連続再生で回答まで完了する。有効MATCHは再利用される。
- 直接Capture、巻戻し、loop、2周目を要求しない。
- 両SourceのRT receipt前に回答できない。
- Reveal前の識別情報漏洩が、画面、accessibility、通知、切替待ちにない。
- 不成立時は同じblockからPOSTへ倒れ、同じ割当を自動再開しない。
- 固定MATCH、ceiling、POST減衰、offline、bypass、Record保護が契約を満たす。
- LISTEN／Live Blindの明示END一回で通常POSTへ復帰し、実復帰確認後にだけ終了する。不意のclose／失効は減衰保持とRETURNを残す。
- Exact 4 SはPINからのみ利用でき、既存の固定PCM検証がgreenである。
- 全自動試験、source契約、line budget、Clippyがgreenである。
- macOS VST3/AU/AAX、Windows VST3/AAXの閉じたhost matrixに未記入がない。未実施hostは公開全体のblockerとして残す。
- 実画面を全5サイズ・英日で確認し、操作欠落と識別情報漏洩がない。

実装完了と公開完了は分ける。配置、署名、公証、installer、Lemon Squeezy、HP、GitHub Releaseは、別途依頼と三チャネルrelease gateなしには実行しない。

## 16. 今回の記録と申し送り

現在地: `codex/hypha-one-pass-blind`。先行の責務抽出は `ad58619e` / B-1099。
直接BLIND、自動固定MATCH、有効MATCH再利用、匿名切替、実出力receiptに基づく回答、CSPRNG失敗時の拒否を実装。
明示ENDは通常POST unityまで戻り、close/hideは減衰保持。MATCHからBlindに入ってもENDでMATCHには戻らず比較全体を終了する。
上昇量は英日・全サイズで事前表示し、小サイズの操作列は比較操作を優先する。匿名面は全階層のaccessibilityと背後の操作を隔離する。
Exact 4 SはMENUのPINに残し、直接Capture入口を主面から除去した。Reference改善は別計画のまま未変更。

日次ログ（2026-09-29）: JUCEの不足していた正本patch 0009/0010を適用し、10本全体の一致を検証。
macOS x86_64 DebugでPRE/POST共通shellと試験targetをbuild。配布用build、DAW配置、release、署名、公証、外部送信は行っていない。
Rust FFIのソース／ABIは未変更。staticlibは現行ソースからDebug再buildした。
最終の対象CTest 24件、Blind UI 90件、上昇量表示20件、Rust workspace 2,165件、Clippyがpass。41件のignoredは未実施でありpassへ含めない。

検証の正本は [ローカル検証記録](hypha_one_pass_live_blind_validation_20260929.md)。実機host matrixは本変更のexact commitで全件未実施。
通常suiteのgreenだけを実機検証や公開準備完了へ読み替えない。

Handoff: FREQ全体UI性能gateの未達を切り分け、実機matrixを承認された検証・配置工程で実施する。
今回未変更のSpectrumLandscapeContractの900×600/DPI 2が単独実行でも18.6429 ms/frame（基準12 ms未満）だった。
閾値は緩めず、失敗を保持した。新規Blindの表示試験と混同しない。

セッション記録手順を再読。このリポジトリはNotion書込み禁止のため、SECTION:DEV、日次ログ、Notion Handoffは未記録。
その未送信内容を上記の現在地、日次ログ、Handoffの順に残す。LS/HPは全チャネルskip、実機未検証と性能gate未達は公開blocker。
