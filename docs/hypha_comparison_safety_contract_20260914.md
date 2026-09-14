# Hypha比較機能の共通安全契約と検証計画

作成日: 2026-09-14。
改訂: 第2版。Pauseと停止後の出力、Undoと保存通知、実機対象の固定方法を追加。
状態: 利用者が指定した4条件を計画へ反映。新しい製品実装と実機検証は未実施。
適用対象: Reference通常A/B/C、Reference Blind、ローカルPRE/POST Blind。
今日の確認、比較しおり、音源の再選択や再開から入るReference試聴にも同じ条件を適用する。

## 1. 共通に守る四つの条件

1. 比較操作で制作上のGainやAutomationを書き換えない。
2. 一時的な比較操作でDAWのUndo履歴を不要に埋めない。
3. ホストからofflineと通知された書き出しへ、試聴コピーや比較用補正を混入させない。
4. 比較の取消、失敗による失効、終了、再起動、保存状態の復元から、比較音へ自動で戻らない。

第3条件は、通常再生と区別できないリアルタイム録音まで自動検知できるという保証ではない。
その場合は書き出し前に通常Aへの復帰を確認する操作を必要とし、この制約を隠さない。
条件を満たすことを画面文言だけで主張せず、音声出力、ホスト通知、保存と復元、遅延した要求を検証する。

共通化するのは安全条件と試験であり、異なる比較のGain基準や状態機械を一つに統合しない。
ReferenceのライブA、Version B、Check Cと、Local Blindの固定PRE/POSTコピーを区別する。
元音源、正本の測定、Record、DAWの制作設定は比較用の一時状態から分離する。

## 2. 現行で確認した土台

確認基点は`kirin_hypha_reference_abc` B-887 `53937c0da5916c7b771329e98fff79671fb5b22c`である。
B-887ではReference Captureの操作直列化、ReferenceAnalysisOwner、表示投影が実装され、対象native試験、全体baseline、実寸検証が完了した。
同じcommitでのDAW実機、Windows、両Blindを含む共存確認は未実施であり、CS1〜CS8の完了証拠にはしない。

| 確認した処理 | 現行の土台 | 今回必要な確認 |
| --- | --- | --- |
| `PluginProcessorAudition.cpp` | 正本計測後に比較出力を処理し、offlineとbypassではReference試聴許可を渡さない | B/Cと両Blindの全状態で最初のoffline bufferから入力不変か |
| `ReferenceRuntimeV2Realtime.cpp` | 試聴不可や不整合でAへ戻す処理があり、通常試聴とBlindを別に扱う | 失効後に遅い準備や旧選択が再生を復活させないか |
| `ReferenceRuntimeVersionSelection.cpp` | 選択の復元でAを選び、世代を進めて公開済み試聴を失効させる | project reopenだけでなく、動作中restoreと欠損復旧でも成立するか |
| `PluginProcessorState.cpp` | Reference選択とCaptureの保存を比較出力と分ける | 一時的な音源選択、Gain適用、Blind承認が再生許可として復元されないか |
| `ReferenceRuntimeV2BlindRealtime.cpp`、`local_blind/LocalBlindTrial.cpp` | 一時減衰の保持と明示復帰、offlineの出力無変更を区別する | 減衰保持を通常復帰済みと表示せず、書き出しにも適用しないか |
| `HyphaCaptureStateNotification.h` | 保存可能なCaptureが確定したことをDAWへ通知する | Undo対策がこの必要な保存通知を止めないか |

今回の読取りは、全hostで四条件を実証したという意味ではない。
GainMatchの不満を理由に、既にある処理を新機能として数え直さない。

## 3. 制作設定と比較用Gainの分離

通常Aは追加latency 0 samples、bit identicalを維持する。
比較用GainをDAWのFader、Trim、他プラグインのGain、Automation parameterへ書き込まない。
比較操作のためのhost parameter gesture、Automationへの書込み、Routing変更を追加しない。
元のReferenceファイル、PRE/POSTの取得PCM、測定値、RecordをGain適用後の音へ差し替えない。

Reference通常試聴は選択したB/Cの試聴出力へ、各モードの既存Gain規則を適用する。
両Blindは、既存契約で本人が承認した場合に限り、比較用のAまたはPOSTを一時減衰できる。
この試聴出力の変化と、制作上のGain設定の書換えを混同しない。
既存のOriginalモードとfallbackの採否は変更せず、成立していないGain Matchを成立済みと表示しない。

一時減衰が実際に適用された後は、既存の通常復帰待ちを維持する。
安全契約を口実に、明示承認を経ず通常音量へ急に戻す処理は加えない。
offline、bypass、processor再生成と、同じprocessor内の実時間復帰は別に試験する。

## 4. Undoと保存通知の分離

| 操作または変化 | DAW保存への扱い |
| --- | --- |
| 試聴A/B/C切替、Blind Source切替、出力確認、試聴中のGain適用、進行表示 | 一時的な比較状態。Automationや周期的なdirty通知を生成しない |
| 通常のReference選択、明示したPreset/Cue設定の変更 | 保存対象が変わった場合だけ、確定snapshotに対応する必要な通知を送る |
| Captureの確定、保持範囲など保存対象の編集 | 非RTで保存可能なsnapshotを公開した後に通知する。保存通知を一律OFFにしない |
| 今日の確認の確定checkpoint、しおりとメモの明示保存 | 既存のローカル永続化とcheckpoint確定を維持。試聴の毎tickをhost通知へ変換しない |
| 内部再試行、再描画、同じ値の再公開 | 保存内容が変わらなければ追加のdirty通知を出さない |

利用者の明示編集までUndo不能にすることを目標にしない。
Hypha内部の履歴記録とDAWのUndo履歴も同一視しない。
必要なローカルjournalや履歴eventを消さず、ホストへの通知と分離する。
複数更新の通知をまとめる場合は、後続の正常snapshotが保存可能になる前に保存済みと通知せず、最後の確定更新を落とさない。

通知試験は、次の三群を別々に計数する。

| 群 | 対象 | 要求する通知 |
| --- | --- | --- |
| 一時試聴 | 可聴A/B/C、Blind Source 1/2、出力receipt、Gain適用、進行表示 | parameter gesture 0、保存内容が不変ならdirty通知0 |
| 保存するReference選択 | Version、Preset、Check、Candidate、Cue、保存対象の表示設定 | 値が変わった確定snapshotごとに必要なdirty通知1回。同値再選択は0回 |
| 非同期commit | Capture、Tonal ready、workflow checkpoint、しおり、メモ | 依存物とsnapshotの確定後に必要なdirty通知1回。古いgenerationと同じcommitの再公開は0回 |

通知回数はHyphaが発行した事実とhostが作るUndo項目を分けて記録する。
一時試聴の通知を抑えた結果、保存するReference選択や非同期commitが再openで失われた場合は不合格とする。

試聴前に他プラグインへ一つの編集を加え、通常の比較切替だけを繰り返した後、Undoでその編集へ戻れるかを対象DAWで確認する。
Captureやメモ保存を含むケースは別に計数し、その正当な通知まで「履歴汚染」と数えない。
host独自のplugin state履歴が残る場合は、通知を抑制した事実とhostの挙動を分け、全DAWでUndo event 0と断定しない。

## 5. 書き出しと遅延した再生要求

offline通知を受けた最初のbufferから、比較出力と一時減衰を適用しない。
GUIやworkerの応答、保存I/O、復帰receiptを待ってから安全な出力へ切り替える構造にしない。
fade中、Source切替要求中、Gain承認後、Blind中、減衰保持中も同じ条件を検証する。

offlineで失効した試聴の許可を、realtimeへ戻っただけで再利用しない。
出力ownerと要求世代で、旧B/C選択、遅いdecode、Gain準備、枠取得待ち、古い復帰receiptを無効化する。
Audio Threadは既存のRT-safeな出力拒否と事実通知にとどめ、非RT側が取消と退役を処理する。
同じ中断を毎callbackで新しい要求として積まない。

リアルタイム書き出しをホストが通常再生として渡す場合は、Hypha単独で確実に区別できない。
CPU負荷、callback間隔、GUIの非表示、Record状態から書き出しだと推測しない。
利用者向けには、Reference通常比較はAを選び実出力の復帰を確認、BlindはEnd/StopとReturnを完了してから書き出す手順を示す。
ホストから得られる追加の公式通知で保護できる場合も、当該hostの検証なしに共通保証へ広げない。

## 6. 中断と復元後の出力許可

| 起点 | 保持できる情報 | 再開の条件 |
| --- | --- | --- |
| 比較の取消、失敗、終了 | 選択情報、表示用の原因、必要な通常復帰情報 | 新しい明示試聴操作。旧Startの再実行なし |
| Editor close/reopen | 保存対象と、同一processorが所有する必要な復帰待ち | 開くだけでは試聴しない。必要なら既存のReturn画面を出す |
| project save/reopen、processor再生成 | 検証できるReference選択、Capture、workflow checkpoint | 通常Aで開始し、準備後も明示操作を待つ |
| 動作中のstate restore | 復元した保存対象。旧出力許可は失効 | 旧世代の準備完了で再生しない。同一processorの実適用減衰は既存復帰契約で扱う |
| 音源欠損後の再接続、OS再起動、Library再配信 | 再検証できた音源と選択 | 復旧や新しい配信の到着だけではB/Cへ切り替わらない |
| 今日の確認やしおりの再開 | 本人の確認状態、比較条件、戻り先 | 条件を準備するだけ。試聴には別の明示操作を必要とする |

DAWの通常の一時停止と、比較の取消または失効を区別する。
既存契約が認める準備済み区間待ち、完走後の回答保持、同じ有効な比較内のPauseは残せるが、終了済み試聴の復活には利用しない。
待機中に何の出力を許可しているかを表示し、無効化された要求を「Pause中だった」として自動再開しない。
初回Start後にDAWを対象区間から再生する通常手順へ、不要な追加承認は増やさない。

| 中断境界 | 全経路に共通する条件 |
| --- | --- |
| 明示試聴前のPause | Aを維持する。準備完了だけを再生receiptとしない |
| 比較区間へ入る前のPause | 同じ要求世代と入力条件が有効な経路だけ、既存の明示Startを保持できる |
| 不完全なpassまたは不連続の発生 | 断片を完走として足さず、経路固有の失効と復帰へ進む |
| 完走後のPause | 聴取済み事実と回答は保持できるが、ResumeだけでSource要求を再実行しない |
| hostのexact loop | 対象経路が事前に許可し、loop境界と要求世代が一致する場合だけ継続できる。HyphaがLoopを設定しない |
| offlineまたはbypass | 当該bufferへ比較コピーと一時減衰を適用しない。解除だけで比較コピーを再開しない |
| 実適用済み一時減衰 | offlineまたはbypass中の出力無変更と、realtime復帰後の通常復帰待ちを区別する。復帰済みと推定しない |

Local Blindの詳細な状態と表示は[PRE/POST Blind計画](hypha_pre_post_blind_usability_plan_20260914.md)第6.4節を正本とする。
Reference通常比較とReference Blindは、それぞれの状態機械について同じ境界表を作り、Local Blindの結果を代用しない。

## 7. 全比較経路の受入試験

| ID | 必須試験 | 合格条件 |
| --- | --- | --- |
| CS1 制作設定 | 各Gainモードと両Blindを一巡 | DAW Fader/Trim/Automationと他plugin設定の比較起因の変更0。元音源と正本の変更0 |
| CS2 Undo | 可聴A/B/CとBlind Sourceを100回切替。隣接pluginの既知編集をUndo | 一時切替によるparameter gestureとdirty通知0。保存する選択と非同期commitは別に計数し、hostの実履歴とUndo結果を記録 |
| CS3 保存 | Version、Preset、Check、Candidate、Cueを変更または同値再選択し、Capture、確認checkpoint、しおりメモとともに保存して再open | 変更した確定snapshotは必要な通知1回、同値操作は0回。最後の確定編集を保持し、CS2対策による保存欠落0 |
| CS4 Offlineとbypass | 待機、通常B/C、fade中、Source要求中、承認済み、Blind中、減衰保持中からofflineまたはbypassへ移行 | 対象bufferから入力と出力がbit identical。比較PCMと補正の混入0。解除後の状態は第6節と一致 |
| CS5 復旧 | Pause、offline終了、bypass解除、失敗復旧、欠損音源復活、OS再起動、Library再配信 | 明示操作なしの比較コピーへの切替0。古い準備と開始要求の再実行0。実適用済み減衰の復帰待ちを消さない |
| CS6 再起動 | Editor再表示、project reopen、動作中restore、processor再生成 | 保存対象は維持し、再生許可を復元しない。必要な復帰待ちを消さない |
| CS7 実時間出力 | 両Blindの一時減衰と通常復帰、通常ReferenceのA復帰 | 実適用の量と復帰表示が一致。古いreceipt、callback不在で通常復帰済みとしない |
| CS8 境界と共存 | 複数POST、両Blind排他、Keep、Capture保存中に上記を実行 | 一方の旧要求が他方へ作用しない。音声復帰は保存やdecodeを待たない。追加RT alloc/lock/I/O 0 |

CS1〜CS8をReference通常比較、Reference Blind、Local Blindへ適用する。
対象に存在しない状態は理由付きN/Aとし、他方の試験結果を代用しない。
CS3のworkflow項目は機能実装後に実データで実行する。それ以前のfixtureをworkflow完成と数えない。
ReferenceのC Originalと通常のLoudness/Peak、BのVersion補正、両Blindの減衰あり/なしを区別して試験する。

nativeでは識別可能なAと比較音を使い、最終出力の数値とhash、要求世代、host通知の計数を照合する。
実DAWではGainや音色を変えない再現可能なfixtureを使い、比較なしの対照書き出しと照合する。
意図的にランダムな処理やditherを含む実曲の差分を、そのまま比較音混入の証拠にしない。
Studio One VST3の両OS、出荷対象AU、Pro Tools AAXの両OSを最低対象とする。
各実装計画の最初のgateで、製品名、版、OS build、formatを閉じたhost matrixへ固定し、未記入の対象を完成扱いにしない。
外部録音とリアルタイムbounceでは、通常復帰後の安全な手順を別試験にする。

## 8. 実装責務と他計画への接続

主な対象は`PluginProcessorAudition.cpp`、`PluginProcessorState.cpp`、`HyphaCaptureStateNotification.h`、各Reference controllerのnormal/Blind/restore経路、Local Blindのtrial/return経路である。
最終出力の拒否と要求失効はaudio/control owner、永続化通知はsnapshot owner、表示は各UIが所有する。
UIだけのフラグや一つの新しい万能状態機械へ寄せない。
新たな自動保護が必要なら、その経路の再現試験を先に追加し、既存の減衰承認と通常復帰を保って修正する。

Referenceの[統合計画](reference_c_tonal_balance_plan_20260914.md)と[聴取手順計画](reference_listening_workflow_plan_20260914.md)、[PRE/POST Blind計画](hypha_pre_post_blind_usability_plan_20260914.md)の共通完了ゲートにCS1〜CS8を加える。
個別機能は互いの実装を待たず進められるが、共存する同じ最終sourceの試験を省略しない。
四条件に関係する共有sourceを変えた場合は、影響する全比較経路を再検証する。
更新後のexact commit、OS/DAW/format、素材、通知の取得方法、数値結果、未完了事項を一つの証跡へ記録する。

本書は共通安全条件の計画であり、実装完了、全DAWでの保証、配布候補の受入完了を宣言するものではない。
