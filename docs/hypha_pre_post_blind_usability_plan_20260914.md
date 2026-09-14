# PRE/POST Blindの使いやすさと比較条件の実装計画

作成日: 2026-09-14。
改訂: 第2版。初回レビューで確認した7件を反映。
状態: 実装計画。製品実装、DAW操作、GainMatchとの実機比較、配置、公開は未実施。
対象: ローカルPRE/POST Blind。
目的: 制作音の設定を変えずに比較を始め、取得時の条件を理解して判断し、制作と再比較へ戻れるようにする。

追加指示: Referenceにも制作設定、Undo、書き出し、中断後の再生に関する同じ安全条件を適用する。
これらの正本を[比較機能の共通安全契約](hypha_comparison_safety_contract_20260914.md)へ分離し、本書はLocal Blind固有の操作を定義する。
本書は[Reference統合実装計画v4](reference_c_tonal_balance_plan_20260914.md)の構成文書であり、BL0〜BL4とBL-V01〜BL-V12、BL-U1〜BL-U3を統合計画の完成条件に含める。
統合計画は共通の所有権、通知、共存試験を正本とし、本書はLocal Blind固有の状態、操作、診断、実機比較を正本とする。

## 1. 完成範囲

今回の完成範囲は、比較開始と再比較の操作、比較条件の表示、短い音を含む不成立時の案内、通常出力への復帰、同条件での検証とする。
既存の4秒固定Capture、固定Gain、両Sourceの完全聴取、回答、Reveal、明示的な通常復帰を維持する。
音質の採点、POSTの推奨、改善の証明、ABX識別検定にはしない。

回答不要の即時比較、リアルタイムAUTO Gain、可変Capture長、全曲Blind、複数ペアの一括切替、音声履歴の永続保存は追加しない。
これらを次期版へ移す決定でもなく、本計画とは別の範囲判断を要する機能として扱う。
Reference A/B/C、CのTonal Balance、聴きどころ、今日の確認、比較しおり、Kirin OSの保存形式も変更しない。
Kirin OSやWork接続をLocal Blindの必須条件にしない。

利用者がDAWで再生と位置移動を行う境界を維持する。
HyphaからSolo、Mute、Fader、他プラグインのBypass、Routing、Transport、Loop設定を変更しない。
画面の簡略化を理由に、Gain承認、回答、Reveal、Return to Liveを自動化しない。

## 2. 基点と既存計画の関係

| 対象 | 確認した基点 | 扱い |
| --- | --- | --- |
| 計画保存先 | `kirin_hypha` main `9cb40e56ddbd7c3b9ebb0186c2169467ebaae317` | 本書と統合計画v4を更新する。既存の未コミット計画と変更済みWAVは保持 |
| 製品実装の参照先 | `kirin_hypha_reference_abc` B-887 `53937c0da5916c7b771329e98fff79671fb5b22c` | Local Blindの現行契約とソース、B-887のCapture直列化とReference解析所有権を確認。旧mainを製品実装の基点にしない |
| 直前の共有責務修正 | [B-887実装記録](../../kirin_hypha_reference_abc/docs/reference_capture_b887_implementation_20260914.md) | native、全体baseline、実寸検証は完了。DAW実機、Windows、現行候補での共存確認は未実施 |
| 競合検証の候補 | GainMatch v1.53、2026-08-06公開 | 公式履歴で確認。導入済みversionや性能は未確認 |

既存の[Captureと操作導線の統合計画](../../kirin_hypha_reference_abc/docs/hypha_capture_and_workflow_integrated_plan_20260914.md)はH01〜H08の範囲を所有する。
その[実装記録](../../kirin_hypha_reference_abc/docs/reference_capture_workflow_implementation_20260914.md)にはB-882のBlind導線とB-883のPRE候補探索が記録されている。
本書はこれらを再実装せず、Local Blindの追加改善と受入試験を定義する。
旧計画の「未実装」という記述だけで現状を判断せず、確定commitのソースと実装記録を突き合わせる。

B-885のレビューは、Reference Captureの二重Start、保持AによるLIVE観測と解析枠の占有、過去エラーによる現在通知の隠蔽を再現した。
B-887は、この3件についてCaptureの操作直列化、ReferenceAnalysisOwner、表示投影を実装し、対象native試験と全体baselineを完了した。
これらはLocal Blind固有の準備済み取消、診断、Pause、実機復帰を実証した結果ではない。
共有する開始排他、解析枠、通知の所有についてはB-887を実装基点とし、本書の共存試験でLocal Blindからの利用を確認する。
[B-885構造修正計画](../../kirin_hypha_reference_abc/docs/reference_capture_b885_structural_repair_plan_20260914.md)とB-887の修正を本書で重複実装しない。
B-887の未実施項目を解決済みの根拠にせず、関連する実DAW共存試験が通るまで製品完成とはしない。
無関係なReference機能全体の完成をLocal Blindの着手条件にはしない。

## 3. 調査から採用する課題

以下は利用者投稿、公式仕様、Hyphaへの設計判断を区別した対応表である。
個別投稿から発生頻度や現行版の不具合率は推定しない。

| 調査で得た根拠 | 本計画への反映 |
| --- | --- |
| 処理後の音量を保ち、処理前の試聴音だけを合わせたいという声。GainMatchでは既存のListen gain設定で解決した事例 | POST基準と試聴限定の補正を開始前に説明し、設定探索を不要にする |
| 短い打音のTarget loudness用途で追従が落ち着かないという個別報告 | 固定Gainを維持。短音の成立境界と再Capture案内を検証する。即時サンプル選別の代替とは称さない |
| GainMatch公式のUndo対策、AUTOのレンダー運用、v1.53の履歴と読み込み改善 | HyphaのUndo、Automation、書き出し、起動時間を検証。競合の未解決欠陥として訴求しない |
| 遅延変更後にDeltaを再検出した古い利用例 | Captureと試聴中のclock/PDC変化を検証。過去の固定コピーと現在の制作音を区別する |
| GainMatchの手早い比較に対する肯定的な利用者評価 | 初回比較と再比較を別に測定。Blind回答の追加時間を隠して速度比較しない |

根拠は[GainMatch公式](https://letimix.com/products/gainmatch)、[公式マニュアル](https://letimix.com/products/gainmatch/manual)、[更新履歴](https://www.letimix.com/products/gainmatch/update)による。
利用者投稿は[Listen gain発見の事例](https://www.kvraudio.com/forum/viewtopic.php?start=60&t=552477)、[2025年の短音用途](https://www.kvraudio.com/forum/viewtopic.php?start=75&t=552477)、[2021年のDelta遅延事例](https://www.kvraudio.com/forum/viewtopic.php?start=45&t=552477)、[手早さと動作環境の評価](https://www.reddit.com/r/AudioProductionDeals/comments/1p7bxdh/letimix_gainmatch_helps_you_objectively_compare/)を参照した。

## 4. 既存実装と追加改善

| 責務 | B-887で確認した既存実装 | 今回の差分 |
| --- | --- | --- |
| 入口 | 通常POSTの600/900に直接入口。小サイズから900×600へ拡大 | 増設しない。初回と再比較の迷いを実測して既存入口を検証 |
| PRE選択 | exact identityの選択、条件付きの単一候補接続、同じ開始前画面からの修復 | 名前からの自動接続は追加しない。重複名、消失、再生成の回帰試験 |
| 再Capture | 試聴出力前の失敗から、資源解放確認後に同じ画面で再取得 | 準備完了後の取り直しを追加。同じprocessorとexact pairで比較用Contextを保持 |
| 比較条件 | 4秒区間、固定Context、必要時のPOST減衰承認 | 取得時点の音であること、補正の作用先、待機中の出力の説明を整理 |
| 不成立理由 | 開始条件は分類済み。Gain解析のFFIはboolで失敗を集約 | 解析で確認できる理由を型付きで渡し、修復可能な案内へ写像 |
| 聴取と回答 | Source完走、選択要求と実出力の区別、四つの回答、Reveal | 既存事実を維持し、条件表示や失敗通知が秘匿を破らないか検証 |
| 復帰 | EndとReturnは別操作。対応する音声確認後に追加Closeなしで通常面へ戻る | 新規成果に数えない。再比較用Contextと旧試聴の分離を加える |

確認ソースは`PluginEditorLocalBlind.cpp`、`HyphaLocalBlindPresentation.cpp`、`HyphaLocalBlindFailureText.h`、`local_blind/LocalBlindProductSession.*`、`LocalBlindPreparation.cpp`、`LocalBlindTrial.cpp`である。
コードに存在することと、現候補の実DAWで使いやすさを確認したことは分ける。

## 5. 比較開始と再比較の操作

### 5.1 初回の比較

接続済みの通常POSTでは、既存の直接入口から開始前画面を開く。
主面には比較相手、2MIXまたはTRACK/STEM、4秒取得、現在必要な次の操作を残す。
未接続の場合だけPRE選択を案内し、復帰待ちやKeep所有中の場合は当該操作へ戻す。
自動接続、自動Capture、他の作業の自動終了は行わない。

開始前の短い説明は「取得した4秒を、固定した音量補正で比較する」という事実を伝える。
準備完了では、通常のPOST基準と、必要な場合だけ代替の減衰承認を示す。
利用者はStartを押し、DAWを表示区間より前から再生する。
既存どおりStartはSource 1を要求するため、開始直後にSource 1を選び直す操作を増やさない。

### 5.2 再比較用Contextの保持

保持するのは、利用者が明示選択した比較用Contextと、それを適用したexact PRE/POSTペアの識別だけとする。
同じprocessorの非RT control ownerが一時情報として所有し、通常Meter Context、DAW保存state、Workへ書き込まない。
Editorを閉じて開き直した場合も、同じprocessor、ペア、通常Contextが有効なら、このContextを開始前画面に表示する。
Editorの再表示だけでCapture、Start、Source選択を実行しない。
通常Meter Contextを利用者が別途変更した場合はその変更を優先し、過去の比較用選択で上書きしない。

ペアのlocatorまたはgeneration、POST instance、通常Contextの変更、processor破棄、project restore、動作中のstate restoreで保持を失効させる。
Editor破棄だけではContextを失効させないが、進行中のtrialは既存の終了と復帰契約に従って取消し、再表示から自動再開しない。
名前だけの一致や同名PREへの差し替えでは引き継がない。
初回または失効後は現行どおり通常Meter Contextを初期値にし、変更可能な状態で見せる。

音声、前回区間への開始許可、Gain、減衰承認、Source割当、聴取完了、回答、復帰receiptは引き継がない。
再Captureは現在のDAW位置から新しい要求世代を作り、両側のPCMとGainを新たに検証する。
前回と同じ区間を聴く場合も、DAWの位置移動は本人が行う。

### 5.3 準備完了後の取り直し

まだStartしていない準備完了画面では、主操作Startと区別した「取り直す」操作を追加する。
この操作は準備済み試聴を破棄し、専用の`preparedDiscardPending`を経て、同じ開始前画面へ戻す。
破棄の対象は今回の一時的なBlind PCMだけで、元音源やRecordではないことを補助説明で伝える。
資源解放後に自動取得せず、別の明示Captureを待つ。

`preparedDiscardPending`ではStartとCaptureを受理せず、旧trialがAudio Threadから参照されなくなったacknowledgement、PCM publicationの退役、scope解放を順に確認する。
一度も比較音と一時減衰を適用していない準備済みtrialの破棄では、利用者にReturn to Liveを要求しない。
ただし、acknowledgement前にPCMを解放したり、scopeを空きとして再利用したりしない。

Start要求と取り直しは、一方だけを同じ制御所有者が世代付きで受理する。
取り直しが先に受理された場合はStart権限を失効させ、退役完了まで「準備を解除しています」と表示する。
Startが先に受理された場合、出力確認がまだなくても準備前の破棄として扱わず、既存のStopとReturn手順へ進める。
二重押下、遅い解放、旧準備結果の到着、古い音声receiptによって新しい世代を壊さない。

### 5.4 試聴開始後の制作への復帰

試聴を始めた後は、StopまたはEnd、その後Return to Liveを維持する。
対応する音声receiptを確認してから通常画面と元サイズへ戻す。
次の比較は通常入口から開き、条件確認とCaptureを行う。
今回の改善で結果画面へ新しい履歴、連続試行モード、勝率表示は加えない。

## 6. 比較条件と表示の契約

### 6.1 固定コピーと現在の音

取得完了後の画面は「取得済みの4秒」を比較していることを示す。
表示する開始と終了は実際に取得した整数sampleの半開区間から作り、丸めた秒表示を開始判定へ逆流させない。
区間は準備、待機、聴取、結果、復帰で同じ位置に置く。

周囲のプラグインを編集しても、取得済みPCMは変わらない。
自動の再取得、Gain追従、全プラグインの変更検知は追加しない。
「現在の設定と一致」「変更なし」といった確認できない表示も出さない。
再比較の補助説明で、制作上の変更を聴くには通常復帰後の再Captureが必要だと伝える。

### 6.2 音量補正と出力状態

通常の比較はPOSTを基準にし、PRE試聴コピーだけへ固定Gainを適用する。
PRE増幅が既存の比較ceilingを超える場合は、PREを原音量に保ちPOSTを固定減衰する既存の承認方式を使う。
比較ceilingを「常に−1 dBTP」や「モニターまでの絶対的な安全上限」と言い換えない。
計測値が一致することを、あらゆる素材で主観的な音量が完全一致する保証にしない。

減衰を実際に適用した試聴では、区間外や中断後に既存の一時減衰保持が残る場合がある。
この状態を「通常音量へ復帰済み」と表示せず、Return時の上昇量は実適用の事実から示す。
準備と承認だけで減衰済みと扱わない。
offlineとbypassの出力無変更、明示Returnまでの実時間出力の保持という既存の区別を維持する。

Source要求中、取得区間待ち、固定コピーの実出力中、完走後、復帰待ちを区別する。
区間外のライブ入力までSource 1/2の比較音だと誤認させない。
表示用の独自タイマーや推測で出力状態を作らず、既存の要求と音声確認を投影する。

### 6.3 秘匿と比較範囲

開始前とReveal後は補正の作用先と取得条件を説明できる。
Start以降Revealまでは、PRE/POSTに結び付くGain、波形、メーター、色、tooltip、accessibility、画像保存から割当を漏らさない。
準備や切替の応答時間、切替音が割当の手掛かりにならないかも検証する。
検証で見つからなかったことを、知覚上の手掛かりが完全に存在しない証明にはしない。

TRACK/STEMでは、選択したPOSTの出力だけが置き換わり、残りのミックスは現在のDAW再生に従う。
POST前で分岐したsendやparallel経路は切り替わらず、POST後の非線形処理は選択したコピーへ反応する。
この説明は短い補助情報に置き、挿入位置間の比較をプロジェクト全体の比較と称さない。
試聴中に下流処理や伴奏を変更すれば聴取条件も変わり得るため、検証ではそれらを固定する。

### 6.4 Pause、停止、offline、bypass

DAWの一時停止は、比較の取消、失効、終了と同じ状態に丸めない。
Pause前に成立した明示操作と実際の出力だけを保持し、停止中の時刻や画面表示から再生許可を作らない。

| 中断した位置 | 停止中の出力と状態 | 再生再開時の扱い |
| --- | --- | --- |
| 準備完了、Start前 | Aを変更せずreadyを保持 | Playだけでは比較を始めない。Startを明示する |
| Start後、取得区間へ入る前 | Aを変更せずarmedを保持 | 同じ世代、clock、pairが有効で、再生が区間先頭へ到達した場合だけ、既存Startの要求で最初のpassへ入れる |
| Sourceのpass途中 | 当該passを不成立にし、必要な復帰情報を保持 | 比較コピーを再開しない。StopとReturnの画面へ進む |
| 一方または両方のSource完走後 | 聴取済みの事実と回答状態を保持 | Resumeだけで同じSourceを再armしない。再試聴は明示したSource操作を必要とする |
| 検証済みexact loopの連続wrap | 既存StartまたはSource要求の範囲内で同じtrialを保持 | stop、seek、clock変化を挟まず、hostのloop範囲が取得区間と一致する場合だけ次のpassへ入れる。HyphaはLoopを設定しない |
| seek、loop範囲変更、clock/PDC変化 | trialを失効し、古い要求を無効化 | 通常復帰後の再Captureを必要とする |
| offline開始 | 最初のoffline bufferから比較コピーと一時減衰を適用しない | realtimeへ戻っただけでは比較コピーを再開しない。既に実適用した減衰の復帰待ちは別状態として保持する |
| bypass開始 | bypass中のbufferを変更しない | bypass解除だけでは比較コピーを再開しない。実適用済み減衰がある場合はReturn待ちを表示し、既存復帰契約に従う |
| Editor close/open | 進行中trialを既存契約で取消し、必要な復帰待ちをprocessorが保持 | 開くだけでは再生しない。比較用Contextだけを第5.2節の条件で再利用できる |

一時減衰が実適用済みの場合、offlineまたはbypass中の出力無変更と、realtimeへ戻った後の復帰待ちを区別する。
比較コピーが再開していない状態を「通常音量へ復帰済み」と表示しない。
各行は2MIXとTRACK/STEM、減衰ありとなし、callbackありとなしで検証する。

## 7. 短い音と不成立時の案内

### 7.1 Gain解析の診断を失わない構造

現在のRust解析は失敗理由を返すが、C ABIはboolへ集約し、製品側では主にGain Match unavailableになる。
表示だけで理由を推定せず、同じ解析実行が生成した診断を準備結果へ渡す。
診断のための二重解析、追加の音声取得、別の合否判定は作らない。

新しい診断付きFFI入口と固定長の結果を追加し、既存の関数と構造体layoutは維持する。
結果は`abi_version`、`struct_size`、`status`、固定幅の`reason_code`、成立時の既存Gain factsを持つ。
calleeは入力検証前に書込み可能な結果全体を既定値へ初期化し、未初期化fieldを返さない。
schema/version、構造体サイズ、reason code、成立時の既存Gain factsを検証する。
内部は型付き結果を正本にし、既存APIは互換写像とする。
未知code、panic、初期化されていない結果は「準備できなかった」へ写像し、生の内部文字列をUIへ出さない。

理由は、不正な入力、区間または形式不一致、対応する有効区間不足、Gainの許容範囲外、True Peak取得不能、内部失敗に分ける。
解析で区別できない事象を細分化しない。
特に有効区間不足だけから「PREが無音」「必ず別の区間なら成功」と断定しない。
Gain factsと診断は同じcapture generationへ結び、古い失敗が新しい結果や区間表示を覆わないようにする。

| RustまたはFFIの結果 | reason code | 再試行の扱い | UIへ示す事実と出口 |
| --- | --- | --- | --- |
| 入力pointer、channel、frame数、sample rate、非finite値の不正 | `input_invalid` | 同じ入力の再試行は不可 | 「取得内容を確認できなかった」。現在のtrialを使わず通常復帰を示す |
| 4秒長、range、window構成の不一致 | `capture_contract_invalid` | 同じartifactの再試行は不可 | 「取得した区間の条件が一致しなかった」。退役後の再Captureを示す |
| 2MIXの連続block不足 | `paired_blocks_insufficient` | 別区間なら成立する可能性あり | 「両側で比較に使える連続区間が足りない」。音が続く区間の再Captureを示す |
| TRACK/STEMの対応event窓不足 | `paired_events_insufficient` | 別区間なら成立する可能性あり | 「両側で比較に使える音を確認できなかった」。別区間と通常復帰を示す |
| Gain deltaが方式の許容範囲外 | `gain_delta_out_of_range` | 取り直しだけでの解決は保証しない | 「固定Gainを安全に決められなかった」。入力条件の確認と通常復帰を示す |
| 片側のTrue Peakを確定できない | `true_peak_unavailable` | 音のある別区間で成立する可能性あり | 原因を無音または故障と断定せず、別区間と通常復帰を示す |
| meter初期化または解析核の失敗 | `analysis_unavailable` | 同じ入力の即時反復を通常手順にしない | 「解析を完了できなかった」。一度の再試行と通常復帰を示す |
| panic、未知code、未知version、結果size不一致 | `internal_failure` | 自動再試行しない | 「準備できなかった」。通常復帰を示し、内部診断だけにcodeを残す |

実装時には両方式の既存Rust errorを上表へ全件対応付け、未対応errorを残さない。
同じreason codeでも修復条件を確認できない場合は、成功を約束する操作文へ変換しない。

| 現在確認できる状態 | 利用者へ示す次の操作 |
| --- | --- |
| PRE未選択 | 同じ開始前画面からPREを選択 |
| 再生前 | DAWで再生を始めてCapture |
| Keepまたは別比較の所有中 | 対象の操作へ戻る。勝手に停止しない |
| 2MIXの対応区間不足 | 音が続く区間を取得。短音用途ならTRACK/STEMを本人が選択 |
| TRACK/STEMの対応区間不足 | 両側で比較可能な音を含む区間を取り直す。成功は約束しない |
| Gain deltaが許容範囲外 | 補正なしで続行せず、入力条件の確認と通常復帰を示す。取り直しだけで解決すると約束しない |
| True Peakを確定できない | 原因を断定せず、音を含む別区間と通常復帰を示す |
| 取得中のseek、停止、clock/PDC変化 | 当該取得を終端化し、解放後に再Capture |
| Start後の条件変化 | 試聴を止め、通常復帰を経て再Capture |
| 下位層で原因不明 | 原因を作らず、現在可能な再試行または通常復帰を示す |

通知は失敗した本人の操作へ結び付ける。
内部探索の互換fallbackなど、利用者が開始していない処理の失敗を常設エラーへ昇格させない。
同じ理由のtoastを繰り返さず、主面の理由一つと修復操作を使う。

### 7.2 解析方針の維持と短音の試験

2MIXの`alignedActiveBlocksV1`とTRACK/STEMの`exactTrackEventEnergyV1`は変更しない。
前者の連続active block、後者の対応event窓の条件は[既存不変条件](../../kirin_hypha_reference_abc/docs/hypha_invariants.md)を正本とする。
短音を通すためのloop padding、無音の水増し、閾値の引下げ、未補正再生fallbackは加えない。

検証素材は既存S-1〜S-5に加え、権利を確認したキック、クラップ、スネア、疎な打音、減衰音、連続2MIXを用いる。
現在のnative fixtureはS-1実ファイルから1秒音、4秒内の疎な30 ms音、既知Gain差を作っていることを確認した。
これを一般の打楽器に対する使いやすさの実証とは扱わない。

対応event窓が必要数の直前、同数、直後になるfixtureと、窓境界に対する位置をずらしたfixtureを追加する。
窓の必要数をそのまま「最短何msの音なら必ず使える」という製品仕様へ置き換えない。
片側だけの無音、Gateによる対応不足、低いnoise bed、非有限値、4秒末端を跨ぐ打音を含める。
4秒を超える残響は欠ける事実を記録し、本計画で全体を評価できるとは称さない。
現行policyで扱えない重要な利用例は実測とともに残し、policy変更が必要なら別途裁定を求める。

## 8. 状態の所有と実装対象

新しい情報はprocessorが持つ非永続の再比較用選択、非RTの取得条件、同じ解析の診断に限定する。
既存のPCM、pair barrier、trial、return receiptを複製しない。

| 責務 | 主な既存ファイル | 作業 |
| --- | --- | --- |
| 再比較と準備済み取消 | `juce_shell/src/PluginEditorLocalBlind.cpp`、`PluginProcessor.h`、`local_blind/LocalBlindProductSession.*`、新しい非永続Context owner | 同一processorとexact pairに限定した一時Context、`preparedDiscardPending`、RT退役ack、解放待ち |
| 表示と通知の寿命 | `HyphaLocalBlindComponent.*`、`HyphaLocalBlindPresentation.cpp`、`HyphaLocalBlindPresentationState.h`、`HyphaLocalBlindAdmissionText.h`、`HyphaLocalBlindFailureText.h` | 条件表示、型付き診断、世代付き通知、秘匿と表示更新の一致 |
| 取得要求と準備 | `PluginProcessorPairing.cpp`、`local_blind/LocalBlindPreparation.*`、`LocalBlindCaptureService.*` | 同じ要求の診断を製品状態へ渡す。開始と取消の競合を検証 |
| Gain解析とC ABI | `crates/kirin_measure/src/reference_gain.rs`、`crates/kirin_hypha_ffi/src/reference_gain_ffi.rs`、`crates/kirin_hypha_ffi/include/kirin_hypha_reference_ffi.h` | 算式を変えず診断付き入口を追加。必要な小モジュールへ責務を分離 |
| 音声とclockの回帰対象 | `local_blind/LocalBlindTrial.*`、`LocalBlindTransition.h`、`HostClockProbe.h`、`PluginProcessorLocalBlindTransport.cpp` | 原則変更しない。出力事実と復帰の検証不足があれば当該境界のみ修正 |
| native試験 | `local_blind_preparation_test.cpp`、`local_blind_trial_test.cpp`、`local_blind_capture_service_test.cpp`、`local_blind_product_test.cpp`、`LocalBlindUiContractTest.h` | 理由写像、再比較、取消競合、各Source、秘匿、復帰 |
| 全体回帰 | `juce_shell/tests/editor_surface_product_test.cpp`、pairing/Record試験、`scripts/test_release_source.sh` | 入口と全サイズ、共有枠、FFI互換、通常出力を検証 |
| 製品文書 | `README.md`、`docs/hypha_invariants.md`、本書の実装証跡文書 | 操作、比較範囲、確認したcommitと未完了事項を更新 |

表は責務単位の影響範囲であり、全ファイルの変更を義務付けるものではない。
実装開始時に移動したファイル、現在のtest target、進行中の所有権修正を再確認する。
新しいowned sourceは500行以下とする。
変更する既存巨大ファイルは対象責務だけを先行抽出し、baselineを増やさない。

Audio Threadのalloc、lock、blocking I/O、探索、文字列生成は追加しない。
新しい背景監視、音声hashの周期照合、PCM保持、専用workerも追加しない。
追加の再比較情報と診断は1 POST当たり固定上限4 KiB以内とし、今回の変更による追加PCM容量は0とする。
主面の自動更新は既存timerの最大10 Hzを使い、明示操作は即時反映し、状態不変時は再構築しない。
診断fieldを表示比較のキャッシュキーから落とさない。

## 9. 検証計画

### 9.1 数値と安全性

| ID | 試験 | 合格条件 |
| --- | --- | --- |
| BL-V01 | 通常出力とRecord | 開始前、通常復帰後、offlineで入力と出力がbit identical、追加latency 0 samples。比較PCMを正本の測定とRecordへ混入させない |
| BL-V02 | 既知のGain差 | 同じ素材の0.5、1、2倍で既存の固定Gain期待値との差0.002 dB未満。これは既知Gain fixtureの数値基準であり、音色変更後の知覚精度基準ではない |
| BL-V03 | 固定条件 | 試聴中のGain、PCM、Source割当を変更しない。新Captureは新世代となり、旧回答と承認が無効 |
| BL-V04 | 減衰承認と復帰 | 未承認では開始しない。実適用した量だけ復帰表示へ反映。Pause、offline、bypass、Cancel、Editor再表示、callback不在、古いreceiptで復帰完了を捏造しない。第6.4節の各行と一致 |
| BL-V05 | 短音と不正入力 | 成立境界の前後、両側/片側無音、Gate、noise、末端event、NaN/Inf、形式不一致を検証。両方式の全Rust error、FFI拒否、panic、未知codeが第7.1節のreason code、再試行条件、UIの出口と一致 |
| BL-V06 | clock/PDC | 既知遅延の無加工pairはsample残差0。途中のOversampling、報告latency、seek、停止で既存の拒否条件を維持。相関による勝手な補正なし |
| BL-V07 | 非同期と再比較 | 二重Start/取り直し、`preparedDiscardPending`、遅いPRE、取消中の結果到着、pair変更、再生成、資源解放待ちを試験。Startと取り直しの受理は一方だけ。未試聴の破棄で利用者Return 0回。旧結果が新試聴を上書きしない |
| BL-V08 | 秘匿 | Source名、数値、色、背後のmeter、tooltip、keyboard、accessibility、保存操作で割当が漏れない。実画素と操作で確認 |
| BL-V09 | UndoとAutomation | 可聴A/B/CとBlind Source切替はparameter gestureと周期dirty通知0。Version、Preset、Check、Candidate、Cueは値が変わった確定snapshotごとに必要な通知1回、同値再選択は0回。Captureとworkflowの正当な保存通知を分け、隣接pluginの編集をUndoできることを実DAWで確認 |
| BL-V10 | 再起動と欠損 | Editor close/reopenでは、同じprocessorとexact pairの比較用Contextだけを保持し、進行中trialを再開しない。project save/reopen、processor再生成、state restoreではContextと再生許可を失効。PRE遅延読込、artifact欠損/破損でも自動試聴なし。必要な通常復帰を保持 |
| BL-V11 | 共存と負荷 | 1/2/3 POST、Reference保持A、別Blind、Keepと共存。共有枠の二重取得、資源漏れ、新規RT割当0。CPU、RSS、callback p95/p99、準備時間を基準版と比較 |
| BL-V12 | UI一巡 | 全5サイズの入口と復帰先、900×600の全状態、長い名前、失敗からの再試行、mouse/keyboard/accessibilityを確認。通常面への行追加と文字縮小なし |

同一PCM試験で意味のあるfade境界は、既存の対称5 msの期待出力と比較する。
試聴用fadeを含む出力全体へ、通常経路のbit identical条件を誤適用しない。
音色やダイナミクスを変更した素材では、全時間窓の音量差を0へ追従させることを合格条件にしない。

offline exportはホストがofflineを通知する経路で、全状態から固定コピーが混入しないことを検証する。
通常再生と区別できないリアルタイム書き出しや外部録音まで自動検知できるとは保証しない。
それらは書き出し前にReturn to Liveを完了する手順を示し、各DAWで実際の挙動を記録する。

### 9.2 実機と競合比較

最低対象はmacOS/WindowsのStudio One VST3、macOSの出荷対象AUホスト、macOS/WindowsのPro Tools AAXとする。
BL0で、実際に出荷対象とする製品名、版、OS build、plugin formatを閉じたhost matrixへ固定する。
「Studio One系」「出荷対象AU」のように対象が増減する名称を最終完成判定へ残さない。
製品UIの共通実装と、各formatの実ホスト動作は別に合格させる。
AAX入口の有効化やAAX指定native harnessのpassを、実Pro Toolsのclock/PDC実証へ置き換えない。
現在候補のexact commit、OS、DAW版、plugin format、sample rate、buffer、配線を各結果へ付ける。
matrixの未記入行、未実施行、対象版と異なる結果はpassにしない。

GainMatchはv1.53を候補として、試験時の公式最新版と導入済みversionを確認し固定する。
未導入なら公式trialの利用条件と期限を確認して準備する。購入や期限回避は行わない。
既定設定の初回体験と、Listen gain等を理解した設定済み体験を分ける。
同じ素材、挿入位置、再生区間、監聴経路で試験し、片方だけに有利な既定値を使わない。

| 比較する作業 | 記録する値 |
| --- | --- |
| 初めて接続して比較 | 設定を探す操作、説明参照、最初の有効な比較音までの秒数、失敗理由 |
| 接続済みで比較 | Hypha内操作数、DAW操作数、取得待ち、準備待ち、聴取時間を別集計 |
| 処理を一箇所変えて再比較 | Context再選択数、再取得の明示、旧音との混同、再比較までの時間 |
| 短い打音と疎な素材 | 設定、成立/不成立、再試行数、固定値または追従の変動、本人の迷い |
| 中断から通常制作へ復帰 | 操作数、復帰確認、出力level、Undoへの影響 |
| 多数挿入と再open | 同じinstance数での起動時間、CPU、RSS、overload、再open結果 |

Hyphaの4秒取得と各Sourceの4秒聴取は必須作業であり、待機やUI探索と別に集計する。
GainMatchにHyphaと同じ回答手順を仮設して速度を比較しない。
共通の音量比較までと、Hypha固有の秘匿回答までを別の結果として報告する。
同じ環境の改善前後を交互の順序で反復し、外れた結果を除外せず、回数、中央値、分布を残す。

Windows操作前は指定のremote access Runbookを読み、機器経路へ触れる場合だけ共有Audio Routes手順も実施する。
認証情報やDAW素材を報告へ露出させない。
検証機や外部協力が必要なら調整を依頼し、未実施をpassにしない。

### 9.3 利用者目線の受入条件

同一processorとexact pairで繰り返す試験では、Editorを閉じて開き直した場合も、本人が変えていない比較用Contextの再選択を0回にする。
processor再生成、project restore、pairまたは通常Context変更後は再選択を要求し、保持成果へ数えない。
新しい比較ではGain承認とSource割当を引き継がず、通常復帰の明示操作数を削減成果に含めない。
正常な初回比較の操作数は確定した基準版より増やさない。
準備済みの取り直しは通常面へ戻って入口を探さずに行え、失敗時も既存の同画面再取得を退行させない。

初見評価はDAW経験のある協力者5人を目標とし、2MIXとTRACK/STEMを扱う。
三つの利用課題を次の入力と操作で固定する。

| 利用課題 | 組み込む操作 | 完了条件 |
| --- | --- | --- |
| BL-U1 初回の2MIX比較 | 最初のCapture後に準備完了画面から一度「取り直す」を使い、別区間をCaptureして両Source、回答、Revealまで進む | 通常面へ戻って入口を探さず、一度も鳴っていないtrialの破棄でReturnを要求されない |
| BL-U2 短いTRACK/STEM | 最初は対応event不足になる疎な区間を与え、理由を読んで音を含む区間を再Captureする | 原因を無音や故障と誤認せず、補正なしで続行せず、同じ画面から比較または通常復帰へ進む |
| BL-U3 制作変更後の再比較 | 通常復帰、Editor close、制作pluginの変更、Editor reopen、再Capture、比較、Return、書き出し前の通常状態確認まで行う | Context再選択0回。旧PCM、Gain、回答、Source割当は再利用せず、自動再生しない |

各課題を4人以上が口頭誘導なしで完了できることを暫定の受入基準とする。
2MIXとTRACK/STEMを一つの人数へ合算せず、BL-U1〜BL-U3ごとに成功数、操作数、所要時間、迷いを記録する。
取得済み音声と現在音の混同、試聴音の書き出し、通常復帰の誤認は1件でも原因を調べて修正する。
これは小規模な使いやすさ試験であり、市場全体の満足度や統計的な優越性の証明には使わない。
協力者の募集や連絡は別途承認された範囲で行う。人数不足なら初見評価未完了と記録する。

## 10. 実装順序と完成判定

| 工程 | 成果物 | 次へ進む条件 |
| --- | --- | --- |
| BL0 基準確定 | 現行commit、既存導線、未解決事項、操作ログ、素材一覧、Pause表、host matrix、通知回数表、全Rust errorの診断写像 | 別作業との差分と共有責務を確定。状態と診断に未定義行がなく、既存成果を新規項目から除外 |
| BL1 状態と診断 | processor所有の非永続Context、`preparedDiscardPending`、RT退役ack、解析診断、世代付き結果 | 取消競合、未試聴trialの無操作退役、FFI互換、全reason code、旧結果拒否の回帰試験がpass |
| BL2 操作と表示 | 同一画面の取り直し、取得条件、出力状態、修復案内 | 全状態のrenderと操作、秘匿、元画面への復帰がpass |
| BL3 数値と共存 | 短音fixture、通常出力、Gain、Record、共有枠、負荷の自動証跡 | BL-V02、BL-V03、BL-V05、BL-V07と、BL-V01、BL-V04、BL-V06、BL-V08、BL-V10〜BL-V12のnativeまたはfixture部分がpass。実DAW部分を自動検証済みと数えない |
| BL4 実機受入 | 閉じたhost matrix、offline export、bypass、Undo、画素、accessibility、負荷、GainMatch比較、BL-U1〜BL-U3 | BL-V01、BL-V04、BL-V06、BL-V08〜BL-V12の実host部分とBL-U1〜BL-U3がpass。未検証hostを残さず、未達条件は是正または裁定 |

工程は一つの完成範囲を実装する順番であり、未達項目を無断で次期版へ送る区分ではない。
共通安全契約のCS1〜CS8も完成条件に含め、Referenceとの共存試験は同じ確定sourceで行う。
計画作成の完了、ソース検証の完了、実機受入の完了、公開リリースの完了を分ける。
Windowsのmodule unloadとAAX実機clock/PDCなど既知の未完了を、既存suiteのpassで消さない。

実装時は`cargo test --workspace`、owned `cargo clippy`、`scripts/test_release_source.sh`の現行対象、source line budgetを実行する。
FFI変更時はparityとpairing_candidatesのignored一覧件数を実測し、両suiteを全件実行する。
nativeの`kirin_local_blind_*`と`kirin_editor_surface_product`を含め、同じ確定sourceに対して検証する。
既存ログ中の修正済み失敗と、現在の未解決を分けて残す。

計画で選んだ操作と診断を実装し、対象hostで通常制作への安全な復帰と再比較を確認して初めて製品改善の完了とする。
競合より速い、軽い、正確と主張する場合は、その比較条件を満たす実測を別に提示する。
公開作業は今回の依頼に含めない。後にリリースを行う場合は、既存RunbookのmacOS LS、HP、Windowsの全チャネルを同一versionで揃える。

## 11. 本セッションの確認と申し送り

本書は日本語技術文書スキルに従い、既存実装、追加提案、確認済みの数値、将来の受入条件を区別した。
Local Blindのソース、既存の実装記録、B-885レビュー、B-887の実装差分と記録、関連buildログ、native fixtureとS-1ファイルの形式を読み取った。
参照worktreeのS-1は48 kHz stereoで、SHA-256は`5e526afe85549fe7daeace8824696f6b1adf7238273cd30e7132f87c8ea77a1d`だった。
保存先mainの変更済みS-1は変更も置換もしていない。

追加指示により共通安全契約を作成し、既存のReference統合計画と聴取手順計画へ接続した。
第2版では、準備済みtrialの専用退役、Pauseと出力の対応、非永続Contextの所有、診断写像、Undoと保存通知、実機gate、BL-U1〜BL-U3を確定した。
統合時にはB-887を製品基点へ更新し、BL工程、BL-V試験、BL-U試験を統合計画v4第13節の完成条件へ接続した。
製品コード、Notion、DAW、音響機器、配布物は変更していない。
次の作業はBL0でB-887以後の製品基点を取り直し、共有所有権を重複実装しないLocal Blind固有の実装範囲を確定することである。
新しい即時比較やpolicy変更が必要になった場合は、根拠と影響を示して追加判断を求める。
