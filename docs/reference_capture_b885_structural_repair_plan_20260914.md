# B-885のCapture開始とReference観測の構造修正計画

作成日：2026-09-14。
状態：B-886で確定した計画。実装後の結果と未実施項目は[B-887の記録](reference_capture_b887_implementation_20260914.md)を参照。
対象：B-885 `8cd604f6039106c649e65628c0e5f97fa0c07db9`。
本書はB-885レビューの3件について、実装範囲と受入条件を定める。
[既存の統合計画](hypha_capture_and_workflow_integrated_plan_20260914.md)と[根拠の契約](reference_evidence_and_discovery_contract_20260914.md)は維持し、開始処理、解析枠、入力配信、状態表示について本書を優先する。

## 1. 利用者に提供する動作

Capture Aは一度押すと開始要求を受け付け、連打しても取得内容を失わない。
取得済みAを残してLIVEへ戻ると、現在のA/Bグラフと取得時からの変更検知が両方進む。
この組合せは1つのPOSTの解析1枠で動き、もう1つのPOSTでも解析を利用できる。

変更を確認したら、前の取得失敗が残っていても短い `A DIFFERS` を表示する。
前回の正常なCaptureを保持した事実と、今回の再取得が失敗した事実を区別する。
利用者のクリック数、接続操作、確認ダイアログは増やさない。

通常Aは0 samples latencyとbit identicalを維持する。
保存Aの数値と過去のB表示Gain、現在のB/C試聴Gain、両Blindの回答と復帰条件は既存の契約に従う。
画像Captureはメニュー内、NOW/SESSIONの測定範囲説明は補助情報、Referenceの通常A/B/Cは全サイズ、Blindは300％という直近の決定を維持する。
INSPECTとの結合、全曲PCMの保存、検知による自動再取得は追加しない。

## 2. 再現した事実と原因

| ID | 再現した事実 | 原因 | 完了条件 |
| --- | --- | --- | --- |
| R1 | 開始受付の途中で2回目のStartを受理。48,000フレーム取得後も保持データなし、保存0 bytes、エラーなし | メールボックスを読み取った時点でpendingが解除され、activeになる前に別世代を発行できる | 開始受付から完了まで単一の所有者を維持する |
| R2 | 保存AありのLIVEで測定済み区間0、保存Aなしの対照で80。保存Aありでは別POSTの解析枠取得も失敗 | 変更検知の入力受理をLIVEへの配信停止条件に使用。両者が解析枠を個別取得 | 同じ入力の配信と1枠の共有を独立した責務にする |
| R3 | 状態が `A DIFFERS` へ変化しても、表示の変更ピクセル数0。以前のエラーを外すと1,232ピクセル変化 | 自由文messageが現在の比較状態より常に優先される | 取得結果、今回の操作結果、現在の比較状態から表示を一箇所で導く |

再現コードとログは開発worktreeの `target/b885-review/` にある。
R1は開始時のadmission callbackを意図的に遅延させた競合試験、R2は既存8秒fixtureを使うcontroller結合試験、R3は失敗後に到達できる状態を使う実描画試験である。
いずれもDAWを操作した実機試験ではない。
実装時にこの3件を追跡済みの回帰試験へ移し、失敗条件をassertionにする。

## 3. 開始から保存までの所有権

操作受付とworkerの進捗を分ける。
受付側の **CaptureOperation** が処理IDと文書世代を予約し、workerはその予約を使って取得を進める。
メールボックスからcommandを取り出しても予約は消えない。
公開用のactiveやpendingから、開始可能かどうかを別々に推測する構造を廃止する。

| 受付状態 | 可能な操作 | 保持するもの |
| --- | --- | --- |
| Idle | Start、Restore | 以前に確定したDocument |
| Starting | Cancel、Restore。重複Startは同じ処理の受付済み結果を返す | 予約した処理IDと文書世代 |
| Armed | Cancel、Restore | 同じ予約と取得資源 |
| Capturing | Finish、Cancel、Restore | 同じ予約、取得中draft、以前のDocument |
| Finalizing | 保存確定前のCancel、Restore。新規Startは受理しない | 終了理由、未確定draft、資源解放状況 |
| Restoring | 新しいRestore、破棄。Startは受理しない | 最新の上限付き復元payloadと世代 |
| Closed | 操作を受理しない | staleなUIからの要求を拒否する情報 |

成功、失敗、取消は操作結果として記録し、後処理が完了してからIdleへ戻す。
取得状態とDocumentの完全性は別に保持し、再取得の失敗で以前の完全なDocumentをPARTIALへ変えない。

### 受付と確定の規則

1. Startの可否判定と処理IDの予約を、同じ非RTの短いcritical sectionで行う。最初に受理したStartだけが文書世代を進める。
2. 重複Startは世代、queue、meter、以前のDocumentを変更しない。返り値はaccepted / already in progress / stale / unavailableを区別する。
3. workerは予約と入力configurationを確認して準備し、資源取得後にも予約を再確認する。取消やRestoreで失効していたら取得を公開せず、取得済み資源だけを解放する。
4. FinishとCancelは対象処理IDを持つ。保存確定前に受理したCancelは確定を阻止し、確定後のCancelは完了済みとして応答する。古い操作が次の取得を終了させない。
5. 有効な取得のencode失敗やcommit失敗は、以前のDocumentを保持して明示失敗を返す。新しいRestoreに置換された古い結果は、その置換結果へ集約し、通常の保存失敗と混同しない。
6. 保存payloadを確定した後にhost dirtyを通知する。finalizeと資源解放が終わるまで次のStartを許可しない。
7. Restoreは同じ所有権管理を通して旧処理を失効させる。最新の上限付きpayloadを受理直後からhost保存へ返し、decode前に空へ戻るB-875の不具合を再発させない。
8. 破棄時は受付を閉じ、RTへの公開を止め、workerを退役させる。古いUIの要求、遅延したadmission結果、二重解放を拒否する。

外部gate、encode/decode、資源解放、worker待機は受付のcritical section内で行わない。
Storeと操作状態の更新順は操作管理側へ集約し、相互に逆順のlockを取る経路を作らない。
RTは公開済みの取得世代と入力許可を読むだけとする。
Captureボタン以外の開始判定もこの受付状態から導く。
`beginBlindGuard`、Blindのeligible表示、再取得可否、compact表示が古いactive/pendingの組合せを読み続けないよう、呼出元を一括更新する。
Startingから資源解放完了まで、両Blindの新規開始と同じPOSTの次のCaptureを許可しない。

ボタンには描画時の操作種別と処理IDを結び付ける。
押下と発火の間に状態が変わっても、描画時のStartを現在のactiveからFinishやCancelへ読み替えない。
二重クリックとキーのauto-repeatを同じ操作として扱い、開始直後の取消への化けを防ぐ。
Startingでは受付済みを表示し、明示Cancelは独立した操作として残す。

保存schemaは変更しない。
受付状態、処理ID、Cancel意図はruntime限りとし、DAWプロジェクトから復元しない。

## 4. 解析枠の所有者

**ReferenceAnalysisOwner** をPOSTごとの解析枠の所有者とし、LIVE表示、変更検知、Capture、Reference試聴からの需要をまとめる。
物理的な2枠の正本は既存のOS leaseとし、C++のカウンタで上限を代用しない。
GUIの表示窓数は制限しない。

共有を成立させる境界はRust側のadmissionに置く。
現在の `VisualAdmission`、`CaptureAdmission`、`AuditionAdmission` が持つ解析leaseの所有関係を整理し、同じPOSTのReference用途では1つの所有者から利用権を渡す。
利用権にはprocessor生存世代とscopeを結び、別POST、別project、別moduleの見かけ上の一致を借用の根拠にしない。
既存のOS leaseを使うため、AU/VST3/AAXが同じstaticを共有するという仮定も置かない。

| 状況 | 解析需要 | 追加条件 |
| --- | --- | --- |
| 非表示、明示取得なし、試聴出力なし | 0枠 | 過去の要約は保持する |
| LIVE、保存Aなし | 必要なら1枠 | 有効なB対応がある区間だけA/Bを測定する |
| LIVE、保存Aあり | 合計1枠 | LIVEグラフと変更検知へ両方配信する |
| CAPTURED、保存Aあり | 合計1枠 | 変更検知と必要なB投影を同じ枠で進める |
| 明示Capture | 合計1枠 | 非表示でも取得を継続。Captureの既存排他を取得する |
| Reference B/C試聴と観測の併用 | 合計1枠 | 試聴のprocess/project排他と既存権限を別に確認する |
| もう1つのPOSTが解析 | 全体で2枠 | 3つ目は取得不可。先行POSTを止めない |

観測需要だけで `audition.active`、Keep禁止、Δ抑止、音声切替を発生させない。
Captureの共有barrier、Version BlindとPRE/POST Blindの排他barrier、試聴のprocess/project leaseは用途別に維持する。
解析1枠を共有していることを、同時試聴やBlind開始の許可へ格上げしない。

需要の追加時は同じ解析leaseを保持したまま、用途固有の条件だけを追加取得する。
失敗時は追加需要だけを取り消し、それまでのLIVE表示や既存Captureを失わない。
需要の解除時は、その需要に属するjobと未処理入力の世代を失効させる。
全需要が消え、進行中の有限な処理が退役したときだけ物理leaseを解放する。
B/Cの切替末尾や通常復帰待ちは、既存の音声側確認まで試聴需要に含める。
需要の有無と実行中jobの利用権を分け、jobの終了前に他POSTへ同じ枠を明け渡さない。
非RTの利用権は所有者の寿命を保持し、旧世代の結果の公開は拒否する。

C++の各workerから独立したadmission取得を除き、所有者が確認した用途別の許可と世代を公開する。
同じ所有者の需要更新は直列化するが、音源読込やworker joinをそのlock内で行わない。
viewを閉じた直後、解析枠を失った直後、古いjobが戻った直後にも、古い結果から観測を再有効化しない。

## 5. 入力配信と遅い処理の隔離

表示用の観測入力を、1回のRTコピーから配信する。
既存Captureの入力queueとworkerを観測入力の入口へ整理し、同じblockから取得処理と変更検知に必要な処理を行う。
LIVE側へは非RTから既存の固定長queueへ有限コピーを行う。
B音源の読込とdecodeは既存のVisualObservation workerに残す。

```text
DAWの無加工入力
  ├─ 正本の通常計測、通常出力（既存のまま）
  ├─ 既存の短い校正用PCM観測（既存のまま）
  └─ 表示観測のRTコピー 1回 → 固定長の入力queue
       → 既存Capture workerを整理した入力処理
          ├─ 明示取得中のA要約
          ├─ 保持Aへの変更照合
          └─ 非RTの有限コピー → 既存LIVE用queue
                → 既存VisualObservation workerでB読込とA/B計測
```

これは表示観測のコピー数を1回にする設計であり、正本の計測や既存4秒校正まで一つのqueueへ統合する設計ではない。
Capture中は現在の取得表示を維持し、不要なLIVE計測と保持Aの再訪処理を止める。
取得終了後のLIVE/CAPTURED切替で、必要なconsumerだけを有効にする。

### queueと世代

入口のblockはsample開始位置、frame数、rate/channelのconfiguration、clock/PDC、入力連続性、用途の世代を持つ。
新規用途はその世代以降のblockだけを受理し、開始前の滞留blockをCaptureへ混ぜない。
LIVE/CAPTUREDの表示変更で、明示Captureの取得世代を変更しない。

入口とLIVE用queueは、それぞれ単一producerと単一consumerにする。
LIVE用queueのproducerをRTから入力workerへ完全に移し、二つのproducerを残さない。
現在のCapture blockは256 frames、LIVE側のblockは最大8,192 framesなので、256 framesごとに大きなslotを1個消費する実装は避ける。
LIVE側も小blockに合わせてslotを再構成し、frame保持容量と全体2 MiB上限を実測する。

B読込が止まってLIVE用queueが満杯になった場合は、その配信だけを打ち切って欠落世代を進める。
入力worker、RT、変更検知、明示Captureを待たせず、再開後のLUFS窓を欠落区間越しにつなげない。
入口自体が満杯になった場合、明示Captureは不完全取得として終了し、以前のDocumentを保持する。
再訪とLIVE表示も欠落を未確認へ戻し、連続して観測できた区間から再開する。

入力workerの処理単位は有限にし、block間で取消と需要変更を確認する。
ファイル読込、Bのhash検証、decode、波形全体の再構築を入力workerへ移さない。
Captureの要約計測と索引を同じ入力に必要な範囲で処理し、同じ用途のmeterを二重に常時実行しない。
入口queueが無需要ならRTコピーを止める。
workerへの通知方法は既存のRT非通知方式を維持し、音声callbackからcondition variableやUIイベントを呼ばない。

## 6. 状態から表示を導く責務

自由文messageの有無を、現在の状態を隠す条件にしない。
表示の入力を次の三つへ分け、純粋なpresentation関数で表示文と操作を作る。

| 情報 | 正本 | 有効範囲 |
| --- | --- | --- |
| 保持Aの状態 | DocumentのID、範囲、完全性 | そのDocumentを表示している間 |
| 今回の操作結果 | 処理ID、成功/失敗/取消、以前のAを保持したか、修復先 | 対象操作の結果として有効な間 |
| 現在の比較 | Capture ID、時間軸、再訪pass、観測時刻、差の区間 | 確認した範囲と鮮度に限る |

主表示には現在のCapture/比較状態を置く。
直近の失敗は短い補助表示と既存の修復操作へ結び、長い原因説明はhoverとkeyboard focusから読める補助面へ置く。
失敗したという事実をtooltipだけに隠さず、現在の差の表示も上書きしない。

| 組合せ | 主表示 | 補助表示と動作 |
| --- | --- | --- |
| 正常な保持A、差なし | `CAPTURED` | 範囲などの詳細 |
| 完全性の不足した保持A | `PARTIAL` | 再取得への案内 |
| 差を現在の再訪で確認 | `A DIFFERS` | PARTIALならその事実も表示。該当範囲に既存の細い印 |
| 再取得失敗、以前の完全Aを保持、差あり | `A DIFFERS` | `RETRY FAILED`。以前のAを保持したことと再取得先 |
| 再表示直後、過去の差だけ保持 | `A DIFFERS` と `LAST CHECK` | 過去の確認であることを明示 |
| 入力を現在確認できない | 確認済みの保持状態 | 未確認を一致へ変えない |

文言は英語UIへ合わせる。
小サイズでは経過時間などの補助項目を先にhoverへ移し、差、完全性、明示操作の失敗を省略しない。
必要な組合せには既存Capture領域内で短い2行を確保し、文字縮小だけで詰め込まない。
通常の画面には長い説明段を増やさず、全5サイズで実寸を確認する。

描画、tooltip、accessibilityは同じpresentation結果を使う。
失敗案内の寿命は処理IDとDocument IDへ結び、次の正常取得などで対象が変わったら退役させる。
失敗直後に自動消去するタイマーを解決策にしない。
変更検知を終えても過去の操作失敗を成功へ書き換えず、新規取得が始まっても古い差を新しいCaptureへ渡さない。

## 7. 変更対象と実装順序

新規module名は責務を示す案であり、既存の命名規約へ合わせる。
新規owned sourceは500行以下とし、既存巨大ファイルへ機能条件を積み増さない。
変更する巨大ファイルがある場合は、その責務だけを先行抽出してbaselineを更新する。

| 順序 | 完結させる範囲 | 主な既存対象と追加する責務 |
| --- | --- | --- |
| 1 | 再現3件を回帰試験へ移す | Capture runtime、controller結合、CaptureControls描画。各試験の失敗理由を固定 |
| 2 | 操作所有権と保存を接続 | `ReferenceACaptureModel/Store/Session/Restore`。CaptureOperationを抽出し、受付から保存確定まで同じ予約を使う |
| 3 | 解析枠の共有 | `ReferenceComparisonCapture/Controller`、`audition_admission_ffi.rs`、`analysis_lease.rs`、`reference_capture_admission.rs`、`reference_visual.rs`。ReferenceAnalysisOwnerと用途別grantを導入 |
| 4 | 観測入力とconsumerを接続 | `ReferenceACaptureSession/Revisit`、`ReferenceVisualObservation`、`ReferenceACaptureProjection`。入口queueと世代を整理し、LIVEへの非RT配信を実装 |
| 5 | UIと操作受付を接続 | `HyphaReferenceCaptureControls`、`PluginEditorReferenceCapture`、Reference layout。CapturePresentationへ集約し、ボタンの操作種別と処理IDを固定 |
| 6 | 保存と終了処理を結合確認 | `ReferenceComparisonSettings/Controller`、processorの設定復元と破棄、`HyphaCaptureStateNotification`。旧状態復元、dirty通知、両Blindとの境界 |
| 7 | 検証と証拠を更新 | 対象試験、最終全体baseline、全サイズ、同じcommitのDAW実機。実装記録へ結果と残件を記載 |

admissionの新しいFFIは専用の小さいheader/moduleへ置き、既存の巨大な総合headerへ追加を集めない。
`PluginProcessorReference.cpp` のcontroller生成とcallback配線、`PluginProcessorAudition.cpp` の入力配信、関連CMakeとsource契約も変更対象として追跡する。
入力配信から削除する旧入口、observerごとのadmission、旧message優先描画は同じ実装単位で取り除く。
移行中の二重入力、二重lease、二重状態を最終形に残さない。

順序は依存関係を表し、3件を別々の未完成版として提供するものではない。
UIだけ先に完了扱いせず、次の試験まで含めて一つの修正として仕上げる。

## 8. 検証と負荷の合格条件

全体テストは最終状態で1回にまとめ、途中は変更した境界の試験を行う。
失敗後は修正の影響対象と未実行部分を再開し、成功済みの全体試験を理由なく反復しない。
本計画の作成ではテストを再実行していない。

| 試験 | 入力と操作 | 合格条件 |
| --- | --- | --- |
| 開始競合 | admission待ち、二重Start、二重クリック、keyboard repeat | 受理する取得は1件。48,000 framesを正しく保持し、世代の誤失効0件 |
| 取消と終了 | Starting/Armed/Capturing/FinalizingでCancel、Finishとの前後入替、古いcommand | 保存確定の境界が一意。二重解放と意図しない取消0件 |
| 復元と保存 | 開始待ち/取得中/終了中のRestore、直後のsave、破損/旧schema、processor再作成 | 最新の受理payloadを返す。旧結果の混入0件。失敗時は以前の有効Aを保持 |
| LIVEと変更検知 | 保存Aあり/なし、LIVE/CAPTURED切替、Bなし/選択後、入力のgain/EQ変更 | 既存8秒fixtureの対応80区間をLIVEで取得。差の検知も同じsample区間で進む |
| 解析枠 | 1/2/3 POST、保存Aの有無、B/C切替末尾、Capture、Hide/Show | 1 POSTのReferenceは最大1枠、2 POSTが同時利用、3つ目は拒否。終了時の残留0枠 |
| 用途の排他 | Keep、PRE/POST Blind、Version Blind、通常復帰待ち、権限なし | 解析の借用で既存の禁止操作が通らない。観測だけではKeep/Δ/音声へ副作用0件 |
| 遅いB読込 | Visual workerの読込を停止、LIVE用queue満杯、Source削除/変更 | RTと入力照合が継続。明示Captureのframe欠落なし。LIVEだけ欠落を表示し再開 |
| 入口の欠落 | RT側queue満杯、seek、loop、clock/PDC/rate/channel変更 | 未確認区間を捏造せず、取得失敗と再訪の未確認を用途別に処理 |
| UIの組合せ | 差＋失敗、差＋PARTIAL、LAST CHECK、新Capture、Blind秘匿 | 主表示の変化を画素と文字領域で確認。titleのみの成功で通さない |
| 寿命 | Editorを閉じる/再表示、processor破棄、遅いjob完了、engine再作成 | 古い世代の公開0件。入力先へのdangling参照、解放待ち中の再取得0件 |

入力配信はmono/stereo、既存対象sample rate、1/256/最大8,192 framesと端数blockで検証する。
2枠の試験にはAU/VST3/AAXの混在と非表示への切替を含め、moduleごとのstaticでは通せない実OS leaseの競合を確認する。
既存校正のframe上限で拒否されるrateの組合せを、この修正によって校正対応済みと扱わない。

遅延試験は既存workerへのbarrierで実行順を固定し、偶然のsleep時間や試行回数で合格にしない。
検知器単体がpassしたことと、実際にcontrollerから入力が届いたことを別々に検証する。
両Blindの音声側復帰確認と通常Aのbit同一性も、所有権の変更を跨いだ結合試験へ含める。

### 処理予算

- 追加の常駐thread、常駐watcher、全曲PCM bufferは0。
- 表示観測のRTコピーは有効入力につき1回。通常Aのalloc/lock/I/Oは0、latencyは0 samples。
- 入口とLIVE用queueは合計2 MiB以内。小block化後の実sizeofと保持frame数を検証する。
- Capture要約の追加RAMは既存16 MiB/POST、保存領域は1 MiB以内を維持する。既存EBU履歴と既存4秒校正PCMは従来と同じ測定区分で別記し、総RSSも確認する。
- 変更照合は既存の100 ms入力あたりp95 1 ms以下（48 kHz stereo）と、対象rateで実時間の10％未満を維持する。
- 新しい配信管理は100 ms入力あたりp95 0.1 ms以下（48 kHz stereo）を初期予算とする。これは未測定の目標である。
- LIVEと照合を併用した状態は、修正前のLIVE単独と照合単独の測定を合算した処理量に、上記配信予算を加えた範囲で比較する。停止していた旧LIVE併用の低CPUを性能基準にしない。
- snapshotと再描画は最大10 Hz、同じ表示内容では不要な再構築をしない。非表示かつ明示取得/試聴がないとき、PCM処理を止める。

上限を超えたら、コピー、queue分割、job単位を同じ修正内で見直す。
時間一致や音量の根拠を緩めたり、解析枠を3枠へ増やしたりして通さない。

Rustのadmissionを変更するため、最終検証にcargo testとclippy、ignored parity/pairing_candidatesを含める。
ignoredの件数は実装時に一覧から数え、古い固定件数だけで完了判定しない。
source契約、RT検査、5サイズの描画、対象native結合試験を最終ソースへ結び付ける。

同じcommitでStudio OneとPro Toolsの通常再生、CaptureからLIVE、両Blindから復帰、2 POST同時解析、30分のCPU/RSSと音切れを確認する。
Windowsは同じcommitの対象試験とhost確認を行い、既存のDLL unload blockerも別途解消する。
外出中の利用者にPC操作を依頼せず進められる検証を先に終えるが、未実施の実機項目をpassにしない。

## 9. 完了の扱い

本計画の完成と実装修正の完了を区別する。
B-886の作成時点では成果物は計画書のみであり、レビューの3件は未修正だった。
実装後の結果と残件は[B-887の記録](reference_capture_b887_implementation_20260914.md)に記載する。

修正の完了には、R1〜R3の再現試験が期待動作を満たし、保存の互換性、解析2枠、負荷予算、全サイズ、該当する実機検証が揃うことを要求する。
既存B-885の全体suiteのpassを、新構造や実機の成功へ流用しない。
公開リリースは別の作業であり、その場合はmacOSのLS/HPと同一commitのWindows installerを既存runbookに従って揃える。

## 10. 参照元

- [B-885までの実装と検証記録](reference_capture_workflow_implementation_20260914.md)
- [Captureの根拠と軽量性の契約](reference_evidence_and_discovery_contract_20260914.md)
- [Captureの既存構造修正計画](reference_capture_structural_repair_plan_20260914.md)
- `juce_shell/src/reference_audition/ReferenceACaptureModel.h`、`ReferenceACaptureSession.cpp`、`ReferenceACaptureRestore.cpp`
- `juce_shell/src/reference_audition/ReferenceComparisonCapture.cpp`、`ReferenceComparisonController.cpp`、`ReferenceVisualObservation.cpp`
- `crates/kirin_hypha_ffi/src/audition_admission_ffi.rs`、`crates/kirin_measure/src/analysis_lease.rs`、`reference_capture_admission.rs`、`reference_visual.rs`
- `juce_shell/src/HyphaReferenceCaptureControls.h`
- 開発worktree内 `target/b885-review/{findings.md,start_probe.log,runtime_probe.log,ui_probe.log}`
