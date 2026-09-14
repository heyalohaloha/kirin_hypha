# Hypha比較機能 v5実行・検証契約

作成日: 2026-09-14。
対応: [統合実装計画v5 revision 3](hypha_comparison_integrated_plan_v5_20260914.md)。
状態: 規範付属書。実装、性能測定、実機受入を完了した記録ではない。
親計画のID、予算、host matrix、完成状態を使い、別の完成条件を作らない。

[B-890とW-3083の構造修正計画](reference_b890_structural_repair_plan_20260914.md)を今回の補修工程に適用する。
旧reader互換試験は利用者の指示により対象外とするが、既存原本、保存receipt、機能間の障害分離を維持する。
本書のB-887との性能比較に加え、補修前のB-890とW-3083からの増分も測り、修復処理による負荷を区別する。

## 1. 共有責務と先行統合S0

統合担当は実装を受け持つ主セッション一つとする。
Hyphaの統合先は`codex/reference-abc-delivery`、OSは`codex/reference-whole-song`とし、C0で両HEADと変更状態を再確認する。
他の作業による差分を除去せず、共有責務が既に変更されていればその差分を読んで基点を更新する。
共有sourceを複数工程で別々に編集する前提を置かず、S0を通した一つの境界へ利用側を接続する。

| 責務 | S0で先行して確定・統合する内容 | 実sourceの入口と必須検証 |
| --- | --- | --- |
| A観測と資源寿命 | B-887の表示／Capture入力共有を起点に、Tonalへの非RT分配、owner、running／draining／released、停止ackを一つの責務へ置く | `PluginProcessorReference.cpp`、`ReferenceComparisonController.*`、`ReferenceACaptureSession.*`、`ReferenceVisualObservation.*`、Rust lease／admission。G0-N／R／L、T1／T9、RB-V07 |
| 開始・取消と復帰 | 開始確定だけを直列化し、準備要求と実出力を区別する。競合取消はgenerationへ結び付けるが、二つのBlindの状態機械と復帰承認は統合しない | `PluginProcessorAudition.cpp`、`PluginProcessorLocalBlind.*`、`ReferenceRuntimeV2NormalSelection.cpp`、既存admission。BL-V01、BL-V04、BL-V06、BL-V07、RB-V03、RB-V04、RB-V08、CS4、CS5、CS7、CS8 |
| 通常選択と一時選択 | 通常ReferenceChoices、review中の条件、しおりの条件、戻り先を分離する。通常B／Cを一時条件で上書き保存しない | `ReferenceComparisonSettings.h`、`ReferenceComparisonController.*`、workflow state責務。WG0-C、R4からR8、RB-V01 |
| 共通保存snapshot | 一つの非RT組立て責務が通常選択、base Capture＋Tonal、workflowを公開する。各完了は自分の領域だけをID／hash／generationで更新する | `PluginProcessorState.cpp`、Capture codec、保存通知、workflow state責務。G0-E／WG0-E、T5／T8、R10／R11、CS2／3／6 |
| OS原本・配信・journal | legacy、tonal-v1、workflow-v1の保存先とwriterを分離し、比較開始時のwriterと音源由来publicationを保持する。旧readerへ未知fieldを渡さない | OSのrepository、Library delivery、runtime event／History／schema群。G0-M／WG0-F／M、T6／T7／T10、R7からR11、RB-V05 |
| IDと再送 | runtimeはprocessor寿命、attemptは実行寿命とする。restoreで旧commandを失効させても、commit済みoutboxの作成元IDと本文hashを保つ | journal／outbox、restore、event transport。R9からR11、RB-V05、CS6 |

上表のパス名はHyphaでは`juce_shell/src/`配下を中心とする。
OS側とRust／FFIを含む全ファイルは、参照v4第9節とworkflow詳細の変更対象表を入口としてC0で全呼出元まで追跡する。
表にない同種の入口や旧保存経路を残したまま、単一ownerを実装したと扱わない。
既存巨大sourceから今回の責務だけを先行commitで抽出し、新規owned sourceは500行以下、既存行数は増やさない。

G0-EとWG0-Eで実encoder／decoderの共通fixtureを一つ作り、S0で同じ形式を製品へ統合する。
未実装側の最大形式fixtureは実現可能性の証明であり、C1では実装済み両機能の値へ置き換える。
getStateInformationは公開済みの有界snapshotを読み、filesystem操作、待機、無制限encodeを行わない。
restore直後の再保存は有界なpending要素を保ち、旧readyや別attemptの完了で現在状態を巻き戻さない。
一方の保存完了を他方のI/O待ちで止めず、比較からAへ戻す操作も保存queueと切り離す。

B-887の不変条件には、FREQ M/SとLocal Blind UIの両方に`INV-S25`が使われている。
C0では番号だけで参照せず「INV-S25（FREQ M/S）」「INV-S25（Local Blind UI）」と内容を併記する。
S0の文書整備でLocal Blind UIをINV-S25のまま保持し、FREQ M/Sを未使用番号へ移す。
B-887で未使用の候補はINV-S28であり、C0の更新基点で空きを再確認してから確定する。
現行参照と試験索引を一括更新し、旧計画の本文は変更せず、旧番号からの対応表を不変条件へ残す。
番号整理で不変条件の内容や試験アサーションを削除しない。

## 2. 処理負荷と資源の受入

表示窓数、解析lease数、実行job数、保持データ量を別々に記録する。
300%のウィンドウを開くだけで解析枠を消費したり、表示可能数を2へ制限したりしない。
既存の物理2枠を維持し、Referenceのshared ownerとLocal Blindの独立admissionを合算する。
同一POSTの両Blindは排他とし、別POSTを含む既存の排他範囲も縮小しない。

| 対象 | 上限と運用 | 満杯・取消・終了時 |
| --- | --- | --- |
| A表示／CaptureのRT入力 | 共有queue＋受渡しbufferの合計2,097,152 bytes以下。Tonal用の追加RT PCM queueとRT側の追加音声コピーは0 | Tonalの遅延で通常Aやbase Captureを待たせない。欠測を埋めず、Tonalだけを未完了とする |
| 重いReference解析 | 照合、Tonal FFT、Tonal再集計は占有leaseに紐付け、同じleaseで重い解析を同時に二つ走らせない。プロセス合計で物理2枠を超える重い解析jobを作らない | 退役jobも実行数へ数える。停止ack前に後継jobを起動して三つ目を作らない |
| Tonal範囲集計 | owner当たり実行1件、待機1件。待機は最後の範囲だけに置換する | 有界chunk間で取消と入力処理へ制御を戻す。範囲連打で全曲scanを積み上げない |
| 通常B/Cの準備待ち | receiver当たり1件。本人が明示したsource／Cue／Gain根拠／要求世代を固定する | 条件変化、A、Cancel、restore、失効で取消。枠が空くだけで古い要求を復活させない |
| workflow保存 | POST当たりwriter実行1件、待機は32 operation以下。待機中eventは1件16 KiB以下、条件や長文artifactを重複コピーしない | 上限時は新規保存を受理せず、現在の入力と再試行を保持する。受理済みメモをlast-winsで削除しない |
| Tonal I/O | owner当たり実行1件、集計の待機は上記1件に含める。保存は有界bufferで逐次処理する | 保存を捨てて別の集計を先に進めず、未確定と確定済みを区別する |
| UI公開 | snapshotは10 Hz以下、静的pathは値か寸法が変わったときだけ再構築する | Blind秘匿は公開snapshotから適用し、描画だけ隠してtooltipへ残さない |
| Local追加処理 | Contextと診断は4 KiB／POST以下。追加PCM、常駐thread、解析枠は0 | 診断や再取得待ちでReturnを止めない |

解析jobの上限は安全上の設計制約であり、現実装が達成済みという記述ではない。
通常B/CとCaptureの許可済み共存を保ったまま、重い処理を非RTの有界区間でスケジュールする。
Tonal入力の処理を先送りし続ける方式や、full Captureを一度に再解析する方式は採らない。
方式の成立はG0-RとS0で実測し、負荷内で欠測0を満たせなければ実装方式を再検討する。
待機上限は性能測定で緩めず、超過は利用者の明示保存操作に対応した短い案内と再試行へ結び付ける。

Tonal追加RAMは同じPOSTの現ownerと退役ownerを合算して32 MiB以下、workflow追加RAMは8 MiB／POST以下とする。
FFT、入力受渡し、条件projection、32件の保存待ち、restore pending、作成中artifact、encode、公開snapshotを予算へ含める。
共有所有された同じbufferを二重計上せず、異なるコピーを同じものとして除外しない。
C0で既存のVersion／Check観測、PCM、decode pages、threadとqueueを個数・bytes・寿命付きで固定する。
新規のTonal計算workerは占有lease当たり最大1、Localとworkflow専用の常駐workerは追加しない。
既存workerの再利用を先に検討し、新規workerを使う場合も上表の重いjob数とRAM予算へ含める。

負荷試験は同じ機械、audio device、rate／buffer、fixture、POST数でB-887と実装後を比較する。
POST 1個、2個、8個の構成を使い、3個目以降は表示や待機から解析を増殖させないことも確認する。
48 kHz stereoの追加Tonal worker時間は100 ms入力当たりp95 1 ms以下、その他の対応rateでは実時間の10%未満を維持する。
これは既存の設計閾値であり、新たな測定済み値ではない。
callbackはp95／p99／最大値、全体CPU、peak RSS、job数、停止時間を記録し、追加RT alloc／lock／I/Oは0とする。
条件を固定した30分の共存試験で音声dropout追加0、base Capture欠測追加0、正常入力に対するTonal欠測0を要求する。
基準版から既にdropoutがある構成は比較を成立とせず、原因と未完了を記録する。

runningとdrainingの負荷を除外しない。
タイマー満了を停止ackとせず、非RTで実workerとI/Oの停止を確認してから解放する。
UIやAudio Threadからjoinせず、実wrapperのunload後にcallbackや実行中コードを残さない。
B-887記録にあるWindows DLL unload未完了も対象に含め、今回の試作だけの成功で解消済みとしない。

## 3. 通常A/B/CとVersion Blindの回帰工程

この工程は既存Referenceの回帰と不具合修正を担当する。
Tonalやworkflowの追加を理由に、same-song照合方式、Gain方式、試聴範囲を新方式へ変更しない。
変更が必要なら既存方式との差と測定根拠を示し、計画の変更として扱う。

| 工程 | 内容と終了条件 |
| --- | --- |
| RB0 | B-887の通常A/B/C、whole-song alignment、Gain、履歴、Pause／終了の契約と既存fixtureを読み、対象source、試験コマンド、実データhash、操作baselineを固定する |
| RB1 | S0上でRB-V01からRB-V05を実行し、通常試聴とVersion Blindの保存・音声回帰を修正する |
| RB2 | source変更、gain変更、同一PCMの配置変更、無音、曖昧区間、欠損をRB-V02からRB-V04で照合する。準備失敗と既存試聴の失効を区別する |
| RB3 | Tonal、workflow、Localの実装と共存させ、RB-V06からRB-V08、RB-U1を検証する。未実装の模擬状態だけでは完了にしない |
| RB4 | 親計画の必須5行で実機を受入し、全RB試験とRB-U1を同じ最終sourceへ結ぶ。全体baselineはC2で一回だけ実行する |

### 維持する音声契約

Aは現在のDAW入力であり、Capture Aの保存グラフを開いても録音済みAを試聴音にしない。
BはKirin OSが計測した同じ曲のVersionを使い、Work／Recording／Version、file／PCM hash、audio factsを検証する。
live AへBのWork IDをコピーしてsame-songの証拠とせず、Library受信だけのPOSTも既存の音響照合で準備できるようにする。
CのPresetやCheckの有無をB選択の必須条件にしない。

位置は既存の粗探索と分離窓の精密照合を維持し、Aに遅延、time stretch、DAW locateを加えない。
既存契約の相関0.75、各窓ambiguity margin 1.5 dB、中央値3 dB、窓間offset差max(2, sampleRate/20,000) samplesを緩めない。
既知のpadding fixtureではsource-sample誤差0を要求し、EQ等の実素材は既存許容値と不確実性を記録する。
許容値内という結果をあらゆる素材の完全一致と表現せず、矛盾や曖昧な位置では比較を準備しない。

音量は既存のBS.1770 aligned-active-block方針を使う。
400 ms窓、100 ms hop、対応する連続active block 27個以上のA−B中央値で固定し、通常はAが0 dB、Bだけへ全量を適用する。
既存ceilingを超える場合は通常Bを原音量とし、Blindでは表示したA減衰の全量へ明示承認を要求する。
部分補正、A/Bへの分割補正、再生中の追従Gainを追加しない。
live Aの短い観測を曲全体LUFSとせず、保存済みAのLUFSやBの全曲測定を現在Aの測定値へ流用しない。
既存の定数Gain fixtureは0.01 dB以内、比較中の承認済みGain変動は0とする。

Version Blindは照合用4秒観測を越えてBをstreamingし、各stimulusのcallbackで確認した3秒以上の聴取を回答条件として維持する。
この条件は全曲を聴き終えた証明でも、Local Blindの固定4秒一巡条件でもない。
Pause、seek、無音だけでGainを再推定せず、正の内容・位置矛盾があれば試聴を失効させる。
未観測区間での他社plugin編集を即時検知できるとは主張しない。
Captureとの差は同じ位置で実際に比較できた区間と世代を示し、波形の見かけからGainや全曲一致を断定しない。

### 専用試験

| ID | 入力と操作 | 合格条件 |
| --- | --- | --- |
| RB-V01 | Work未接続のLibrary受信、B候補のみ、C未設定、A/B/Cとdropdown、同じBの再選択、復元 | 手動Connect追加0、既知条件の再入力0。A/B/Cの試聴要求は各1操作。C失敗でBを失効させず、準備・復元で自動再生0 |
| RB-V02 | 同曲のGain／EQ／dynamics／head padding、anti-phase、承認済みrate変換、別曲、反復箇所、tone、無音、同一PCMのhost位置移動 | 上記の位置／ambiguity条件。別曲と曖昧区間は拒否。同一PCM hashだけで旧mapを再利用しない。初回とcache照合の時間／読取り量も記録 |
| RB-V03 | 定数Gain差、peak超過、減衰未承認／承認、Aを0.5倍、既存観測区間でのDSP編集、未観測区間、沈黙 | 定数Gain誤差0.01 dB以内、固定後の追従0。確認できた差だけを表示し、古いGainで回答可能にしない。必要な減衰と安全なENDを保持 |
| RB-V04 | 曲頭・中間・末尾、4秒を越える試聴、source終端、Pause中clock欠損、seek、streaming page欠損、rate／source変更、停止中END | live Aを維持。Bは正しい位置の準備済みpageを再生。適格Pauseを保持し、再開は有効clockを検証。欠損・失効で不正音を出さず、停止中ENDにLocalの追加Returnを要求しない |
| RB-V05 | trial開始と完了、未Reveal、再接続、重複・遅延event、破損archive、別namespace同番号、旧outbox再送 | B identityとmap／Gain／実聴取receiptを照合。重複記録と誤結合0。短い観測hashを全曲A hashへ昇格させず、新runtimeで旧eventを再採番しない |
| RB-V06 | 全5サイズ、300%でのBlind、途中縮小／Editor再表示、曲線／凡例／tooltip／keyboard／accessibility、長い名前とTP -14.5／-100.0 | 通常A/B/Cは全サイズ、Blind開始は300%。復帰操作を失わず、割当漏出、はみ出し、重なり0。NOW／SESSIONは補助情報、画像CaptureはMenu内とし、音声Capture Aとは混同しない |
| RB-V07 | 1／2／8 POST、Tonal LIVE／Capture、再集計連打、通常B/C待機、両Blind開始、保存、取消／解放遅延、実wrapper unload | 第2節のjob／queue／RAM上限内。第三枠、停止ack前の再利用、base Capture汚染、破棄済みcallback、unload後実行0。30分の共存条件を満たす |
| RB-V08 | 通常試聴とBlindのoffline、bypass、Editor close、restore、失効後transport再開、END／Return競合 | 親計画第10.4節とCS4／5／7に従う。通常Aはbit identical、追加latency 0。不正な比較音の再開0。LocalとVersionの終了承認を取り違えない |

RB-V06のTPは文字列だけの試験にせず、実フォント・実寸の全5サイズで数値と単位のboundsを検証する。
Hypha固有UIは英語、OSは英日で確認し、利用者が付けた日本語名は勝手に翻訳しない。
主画面の説明を増やして試験を通すのではなく、操作と現在状態を優先し、補足をtooltipと詳細へ置く。

RB-U1では「同じ曲の別Versionを通常比較し、先入観なしに好みを確かめ、制作の音へ戻る」という目的だけを渡す。
準備済みLibraryと同じ開始状態を使い、B選択、A/B試聴、300%への入口、Blind、回答と公開、ENDまでを観察する。
操作上限はRB0で既存の正常経路を実数で固定し、Hypha内の追加操作0、Connect／既知条件の再入力0とする。
DAW操作、聴取時間、機械の準備待ちをクリック数へ混ぜない。
5人の適格な初見試行中4人以上が誘導なしで完了し、live Aと保存済みCapture、実出力、終了状態の重大誤認0を受入条件とする。
既存Reference画面を先に使った協力者の成功を初見成功へ数えない。

## 4. 証拠と完成の扱い

S0の対象試験とC1の共存試験は異なる。
S0は共有基盤の成立、C1は実装された全機能の共存を検証し、S0の模擬相手をC1の実機能へ代用しない。
同じsource、入力、環境で既に得た対象試験の結果は再実行せず参照できるが、後続製品差分の影響を確認せず最終sourceのpassへ転用しない。
最終sourceが固まってからC2の全体baselineを一回実行する。
初見協力者の確保や実機での受入が未完了でも、C0と前提工程を満たしたコード実装は進められる。
該当する利用試験と実機受入は未完了のまま残す。
host構成の読み取りが未完了なら親計画C0も未完了であり、固有製品実装へ進めたことにはしない。

既存実装の根拠は次の資料と対応するsource／fixtureとする。
資料に記載された旧passは本計画の新sourceに対するpassではない。

- [B-887実装・検証記録](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/docs/reference_capture_b887_implementation_20260914.md)
- [Whole-song alignmentと固定Gain契約](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/docs/reference_whole_song_alignment_20260913.md)
- [通常・Version試聴のcontroller](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/juce_shell/src/reference_audition/ReferenceComparisonController.cpp)
- [Version BlindのPause・END・全曲出力](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/juce_shell/src/reference_audition/ReferenceWholeSongRealtime.cpp)
- [Gain／配置変更の既存fixture](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/juce_shell/tests/reference_calibration_regression_test.cpp)
- [全曲試聴の既存fixture](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/juce_shell/tests/reference_whole_song_fixture.h)

今回の計画修正では、これらの製品source、測定入力、旧4文書を変更しない。
