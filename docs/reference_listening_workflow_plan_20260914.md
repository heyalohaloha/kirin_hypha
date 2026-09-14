# Referenceの聴取再利用と確認手順の実装計画

作成日: 2026-09-14。
改訂: 第2版。レビュー4点を反映した、Reference統合計画v4の構成文書。
状態: 計画修正と統合。製品コードの実装、実画面の検証、配置、公開は未実施。
対象: 聴きどころの再利用 → 今日の確認 → 比較しおり。

計画全体の入口と共通契約は[Reference統合実装計画v4](reference_c_tonal_balance_plan_20260914.md)に置く。
本書は聴取手順の詳細とWG0〜WG4、R1〜R12、V1〜V3を定義する。
統合計画のTonal工程G0〜G4と区別するため、本書の工程IDにWGを使う。

2026-09-14追加指示により、全試聴入口に[比較機能の共通安全契約](hypha_comparison_safety_contract_20260914.md)とCS1〜CS8を適用する。
今日の確認としおりの再開は準備までとし、比較音を自動再生しない。
Undo対策では試聴の一時操作と必要な永続化通知を分離し、Capture、確認checkpoint、しおり、メモの保存を欠落させない。

## 1. 完成範囲

利用者が登録した聴きどころを別のPresetで呼び出し、その回に確かめるCheckだけを順に聴き、比較条件と本人のメモを次回へ残せるようにする。
MIXとMasteringを主な利用場面とし、同じ保存契約と操作を使う。
工程ごとの採点、推奨閾値、合否判定は設けない。

本計画に含める機能は次の3つに限定する。

| 機能 | 利用者に追加する操作 | 既存機能からの差分 |
| --- | --- | --- |
| 聴きどころの再利用 | 曲の区間と聴取目的を登録し、別PresetのCheckへ適用する | CandidateごとのCueとListening focusを、独立した再利用単位にする |
| 今日の確認 | Presetから今回の項目を選び、順に確認して途中から再開する | Presetの項目や順序を変えず、その回だけの選択と本人の確認状態を持つ |
| 比較しおり | 比較条件とメモを保存し、履歴からその条件を準備する | Historyの閲覧に、条件を再検証して呼び出す操作を加える |

Tonal Balanceの計測、ジャンル分布、自分の基準セット、A/C区間ペア、自動選曲、自動Cue抽出、Audio onlyの追加導線、帯域Solo、Mono/M/S試聴、Album専用機能は追加しない。
Tonal Balanceの数値契約を変更せず、その実装を本機能群の着手条件にしない。
比較状態、保存snapshot、合算資源、配信識別、共存時の完成条件は統合計画第12節に従う。
Work接続の必須化、DAW transportの自動操作、音源の自動再生、クラウド同期、音源同梱の新しい持出し機能も含めない。
以下の新しいデータ形式とモジュール名は実装契約案であり、現行製品に存在するものではない。

## 2. 確認した基点と既存の制約

| 対象 | 確認した基点 | 本計画での扱い |
| --- | --- | --- |
| 計画の保存先 | kirin_hypha、main、9cb40e56ddbd7c3b9ebb0186c2169467ebaae317 | 本書と統合計画v4を更新する |
| Hypha Reference | kirin_hypha_reference_abc、B-884、ef9bc148f14adfafd1edede8ac1e771a8800504e | 初版のB-883から進んだgain解析とCapture codec等の差分を統合計画へ記録。既存の未コミット差分を保持 |
| Kirin OS Reference | kirin_os_reference_delivery、W-3080、31fd330c4b297637bebe321d2d60d6bd59ff643f | Preset保存、Candidate、Library、Historyの基点。確認時はclean |

実装開始時には両Reference worktreeのHEADと未コミット変更を取り直す。
別worktreeの古い製品契約にあるA/B表記やWork必須の条件を、現在のA/B/Cと独立Libraryへ持ち込まない。
移動したHEADについては対象責務の差分を確認し、今回のテスト結果を後続commitの証拠にしない。

| 確認済みの現状 | 実装に必要な境界 |
| --- | --- |
| Candidateはsource identity、1〜4個のCue、default Cue、最大4,000文字のNoteを持つ | CueやNoteを新設扱いにしない。適用先のCue数と既存メモを保護する |
| Cueはsample rateと整数sampleの半開区間で保存される | 秒表示、曲名、ファイル名だけで別音源へ再適用しない |
| 1 Presetは最大64 Checkで、保存済みrevisionは不変 | 今日の確認をPresetの並べ替えや削除で代用しない |
| AはライブDAW入力、BはVersion、CはCheck。選択変更はAへ戻り、自動試聴しない | 新しい開始、次へ、再開も既存の音声復帰と選択処理を通す |
| Historyは開始と完了を照合し、過去manifestから表示を解決する | ラベル一覧だけでは比較の完全再現を保証できない |
| 旧runtime eventはclosed schemaで、上限16 KiB | 新しい進捗やしおりを旧eventへ未知fieldとして足さない |
| DAW状態はReferenceChoicesとCaptureを保存し、復元で試聴を開始しない | 新しい状態を別要素に置き、既存の保存と復元を維持する |

## 3. 三つの保存対象を分ける

同じ曲に関係する情報でも、使い回す目的と一回の比較結果では保存寿命が異なる。
以下の単位を分け、後から片方を編集しても過去の比較条件を変更しない。

- **聴きどころ**：一つの検証済み音源、その中の一つのCue、名称、本人の聴取目的をまとめた不変revisionである。
- **確認セッション**：「今日の確認」で選んだCheckの順序と、その回の確認状態を持つ。Presetから独立したIDを持つ。
- **比較しおり**：一回の比較条件のsnapshotと、その比較について本人が書いたメモを持つ。

| 情報 | 所有と保存 | 更新の意味 |
| --- | --- | --- |
| 聴きどころの原本 | OSのReference共通保存領域 | 新revisionを作る。適用済みPresetは更新しない |
| Checkへ適用したCueと説明 | 既存の利用者Preset | 編集結果を新Preset revisionとして保存する |
| 確認セッションの定義 | OSが不変snapshotを作り、選んだPOSTが受け取る | 別の確認回を作る。元Presetを編集しない |
| 実行中に変更した項目条件 | Hyphaがattempt内の不変snapshotをローカルへ確定し、OSは検証して取り込む | OS停止中も新しい条件revisionを作れる。元の定義と別attemptの条件は変更しない |
| 確認セッションの実行記録 | 実行中のHyphaがローカルjournalへ記録し、OSが検証して取り込む | 操作ごとの追記。聴取事実と本人の確認状態を別fieldにする |
| 比較しおりの原本 | OS。Hyphaからの保存は永続化済みoutboxを経由する | 新しいしおり、または明示したメモ改訂を保存する |

Listening focusは「この音源の何を聴くか」、比較しおりのメモは「この比較で何を考えたか」とする。
一方の入力を他方へ自動転記しない。
WorkやVersionとの関連は確認できたIDだけを記録し、不明な場合はnullのままにする。

## 4. 聴きどころの再利用

### 登録と検索

OSの既存Candidate詳細で、現在のCueを「聴きどころとして保存」できるようにする。
新しい波形編集器や再生器を作らず、既存Cue editorと単曲試聴を使う。
保存画面には曲、区間、名称、聴取目的を表示する。
名称は必須、聴取目的は任意とし、既存Listening focusを初期候補として本人が確認できるようにする。

1件の聴きどころは一つのCueを持つ。
同じ曲の「低域」と「ボーカル」を別々に登録でき、同じ区間でも目的が異なれば重複を禁止しない。
登録時の音源identity、file/PCM hash、sample rate、channel、総sample数を保存する。
Cueの境界と音源の整合を検証できなければ保存を完了扱いにしない。

既存の比較曲選択に「聴きどころから選ぶ」を加える。
曲名、名称、本人の聴取目的を検索対象とし、最近使った順と名前順で探せるようにする。
自動タグ付け、類似曲推薦、Reference Labelsの改修は行わない。
原本の編集、一覧からの取り下げ、再登録は過去revisionを残して行う。

### Checkへの適用

適用は原本へのライブ参照ではなく、確認したrevisionのCueと目的を利用者Presetへ複製する操作とする。
移動先のCheckと変更内容を表示し、保存後に既存のLibrary配信へ載せる。
Factoryから始める場合は、既存の利用者Presetへの派生処理を使う。
元のFactory、別Preset、他POSTの選択を更新しない。

| 適用先 | 処理 |
| --- | --- |
| 同じsource identityのCandidateがない | Candidateを追加し、選んだCueをdefaultにする。既存の候補上限を超えた場合は保存前に止める |
| 同じCandidateに同一区間とLoop設定がある | Cueを再利用し、同じCueを重複追加しない |
| 同じCandidateに空きCue枠がある | Cueを追加し、既存Cueを残す |
| 同じCandidateが4 Cueを持つ | 置き換えるCueを本人が選ぶか取消する。末尾やdefaultを自動削除しない |
| 既存Listening focusがある | 保持を既定にし、保存画面で置換または追記を選べる。文字数超過時に切り捨てない |

既存Candidateへ適用する場合のdefault Cue変更は明示する。
曲名が同じでも音源hashが違えば別音源として扱う。
移動した同一ファイルは既存resolverで再検証し、別masterや別Versionを自動代入しない。
適用元の聴きどころIDとrevisionは追加領域へ記録し、旧Presetのclosed schemaへ未知fieldを混ぜない。

聴きどころの原本を更新しても、適用済みCueと過去の確認セッションは不変とする。
更新を取り込む場合は、差分を示した再適用として新Preset revisionを作る。
原本を一覧から取り下げても、適用済みPresetやしおりを壊さない。

## 5. 今日の確認

### 作成と開始

OSの既存Preset画面で、今回確かめるCheckを選び、順序を決める。
一回の確認セッションは一つのPresetから作り、MIXとMasteringの項目を自動合成しない。
項目数は1〜64の既存上限内とし、3項目を強制しない。
開始前に、各項目の比較曲、Cue、比較方法、未準備の有無を確認できるようにする。
既存の明示選択または保存された既定値を初期値に使い、空のCheckへ音源を推測して割り当てない。

作成時のPreset revisionとCheck一覧を不変snapshotとして保存する。
比較対象が決まっている項目は、そのCandidate、Cue、比較方法も固定する。
空の項目は未設定として残せるが、試聴準備が完了した表示にはしない。
後から曲やCueを選ぶと、その項目の新しい条件revisionを作り、以前の結果を新条件へ継承しない。
このrevisionはOSの定義を編集するものではなく、当該attemptの派生条件である。
Hyphaは元定義のreceipt、項目ID、親条件のreceipt、source/Cue/方式を結んだ不変snapshotを作る。
OS不在でも取得済みで再検証できる音源を使って変更でき、未取得の音源を利用可能とは表示しない。
保存順は第7節に従い、ローカルcommit前の条件を確定した項目状態へ昇格させない。

各POSTは、配信された確認セッションを自分で選んで開始する。
OSから特定POSTを自動選択せず、Work接続や新しいConnect操作を要求しない。
同じ定義を二つのPOSTで使う場合は、別の実行IDを発行して進み具合を分離する。
通常のB/C選択と確認セッション内の一時選択は、下記の比較状態の契約で分離する。

### 比較状態と戻り先

receiverが持つ操作上のmodeはnormal、review、bookmarkのいずれか一つとする。
これは音声A/B/CやTonal表示の選択とは別であり、UIへ内部名を常設することを要求しない。
通常B/Cの選択、確認回の再開checkpoint、しおりの一時条件を別に保持する。
戻り先は通常選択と最大一つの中断中reviewへのrefで表し、しおりを重ねるたびに履歴stackを増やさない。

| 現在の状態と明示操作 | 遷移と戻り先 |
| --- | --- |
| normalから今日の確認を開始 | 通常選択を控えてreviewへ進む。Bの通常Version選択は変更しない |
| normalからしおりを開く | 通常選択を戻り先として控え、検証した一時条件をbookmarkで準備する。通常B/Cの保存内容は変更しない |
| reviewで曲やCueを編集 | 現在項目の派生条件revisionを確定し、その条件だけを未確認にする |
| reviewからしおりを開く | 項目、条件revision、確認状態を確定保存してreviewを一時中断し、bookmarkへ進む。同じCheckで別Cueでも項目編集とは扱わない |
| bookmarkから別しおりを開く | 同じ戻り先を保って一時条件を置き換える。前しおりの原本と既存記録は変更しない |
| bookmarkから戻る | 中断したreviewがあれば同じcheckpointの項目を再検証して戻る。なければ通常選択を再検証する |
| reviewから別の確認回を明示開始、またはbookmarkから確認回を明示開始 | 以前のreviewの記録は中断状態で残し、指定した回を選ぶ。古い戻り先を新しい回へ混同せず、通常選択の控えは維持する |
| 一時比較中に通常のB/C一覧で対象を明示選択 | 必要なreview記録を中断保存し、normalへ戻ってその選択を通常選択として扱う。一時項目の確認記録にしない |
| reviewを中断または終了 | 記録を残してnormalへ戻る。開始前の通常選択が失われていれば未選択とし、似た曲へ代入しない |

各遷移は要求ID、mode generation、戻り先refと対象receiptへ結び付ける。
保存確定と必要なA復帰receiptが揃ってから新しいmodeと選択を一組で公開し、連打や古い完了で別の要求を適用しない。
A復帰自体はローカル保存を待たずに要求し、保存失敗で元のB/Cを自動再開しない。
取消が選択確定より先なら元のmodeと戻り先を保持するが、既にAへ戻した音声はAのままにする。
既にcommitした要求の取消は同じ要求IDへの取消記録を追記し、再開時に取り消した移動先を現在項目として採用しない。
取消記録の保存失敗は未保存として扱い、音声のA維持と古い待機要求の失効は取り消さない。
戻り先の音源が欠けていてもreviewの項目と確認記録は保持し、その項目を未準備として再指定と通常比較への出口を出す。
しおりのメモを保存するだけの操作はmodeを変更しない。
しおりを閉じた後も、しおり中に聴いた事実を中断中reviewの確認状態へ合算しない。

### 確認状態と項目移動

各項目の本人による状態は、未確認、確認した、保留の三つとする。
音声が切り替わった事実や区間を聴いた回数は、既存receiptに基づく別の記録である。
「確認した」は品質合格や全区間の聴取証明ではない。
再生しただけで確認状態を更新せず、表示回数から理解や判断を推定しない。

| 操作 | 保存と移動 |
| --- | --- |
| 次へ／前へ | 本人の確認状態を変えず、指定した隣の項目へ移る |
| 確認した／保留 | 現在の項目と条件revisionへ本人の状態を保存する |
| 確認して次へ／保留して次へ | 一つの操作で要求する。状態と移動先をローカルcommitし、必要なA復帰receiptが揃った後に次の項目を選ぶ |
| 項目一覧から選ぶ | 指定項目へ移る。未準備項目を黙って飛ばさない |
| 一時中断 | 実行記録を保持し、A復帰後に通常Referenceへ戻る |
| 今回を終了 | 未確認や保留が残っていても本人の終了意思を記録する。Presetや音源を削除しない |

既にAが選択され、外部試聴や復帰要求が残っていない場合は、新しいaudio callbackを待たずに項目を選べる。
B/C試聴中または復帰途中の項目移動では、既存のA復帰commandに対応する音声receiptを確認してから選択を確定する。
callback不在などで復帰を確認できなければ、次の比較が始まった表示にせず、現在の復帰待ちを維持する。
新しいCは準備するだけで、試聴は既存Cボタンによる明示操作とする。
BのVersion選択を今日の確認で書き換えない。
Blindなど別の比較が所有中の場合は開始と項目適用を実行せず、理由を示してその比較を維持する。
Blind終了時に自動適用する待ち要求は残さず、通常比較へ戻った後の新しい明示操作を必要とする。

実行中に元Presetが更新されても、その回の順序と条件は変えない。
現在の候補曲やCueを変更した場合は、新条件を未確認にし、旧条件に対する確認記録を残す。
通常一覧からCheckを選ぶ操作は、同じCheck IDでも上記のnormalへの移動として扱う。
確認セッション内の項目移動と通常一覧の選択を入口で区別し、表示名の一致から操作の目的を推定しない。
終了時の戻り先は上記の遷移表に従う。
戻る操作でもB/Cを自動再生しない。

### 日付と再開

「今日」は利用者向けの呼び名とし、日付を保存identityや自動削除条件にしない。
日付変更、OS再起動、Editor closeで確認状態を初期化しない。
翌日は「続きから」か「同じ項目で新しく始める」を選べる。
新しく始める場合は確認状態を初期化した別セッションとし、前回の記録を上書きしない。

復元したDAW状態は保存時点のcheckpointを示し、実行の再開は明示操作とする。
再開時は新しいattempt IDを発行し、継承元checkpointへ結び付ける。
別POSTや複製したDAW projectから同じcheckpointを開いても、その後の記録を同じattemptへ書かない。
ローカルjournalに保存時点より新しい記録があれば、その続きも選べるようにし、古いDAW保存を黙って最新状態へ変更しない。
reviewからしおりへ寄り道した状態の保存でも、中断reviewのcheckpointとしおりの戻り先を保持する。
再open直後はAと通常選択を維持し、「しおりを開く」「確認の続きへ」の有効な出口を示す。
しおりを先に開いてから確認へ戻る場合も、その最初のreview再開時に新attemptを発行する。
同じruntime内での単なるしおり往復ではattemptを増やさない。

## 6. 比較しおり

### 保存する場面と条件

Hyphaの通常Reference比較から、またはOSの対応するHistoryからしおりを作る。
対象は通常のA/BとA/Cであり、今日の確認に属さない比較も保存できる。
保存画面は名称、今回のメモ、対象曲とCue、比較方法を示す。
現在のdropdown表示と、直前に実際に試聴した条件が違う場合は、どちらの条件を保存するかを明示する。

| 保存する情報 | 保証すること |
| --- | --- |
| A/BまたはA/Cと対象source receipt | どの登録音源との比較だったか |
| Preset/CheckまたはVersionのreceipt、Cueの整数範囲 | 選んだ曲と区間を名前だけで取り違えないこと |
| 比較方法と当時の適用gainの根拠 | 当時の条件を説明できること。現在のAへのgain再利用は保証しない |
| publicationのnamespace、revision、本文hashと開始eventの識別 | 同じrevision番号やevent IDを別経路の履歴へ結び直さないこと |
| Aについて実際に得た情報 | Capture IDやWork関連は確認できた分だけ。A音声を保存したとは扱わないこと |
| 本人のメモと確認セッションの項目ref | 聴きどころの原本メモとは独立した、その回の判断であること |

再生前の選択を保存する場合は「条件のみ」とし、試聴済みeventを作らない。
旧Historyからはarchiveを検証して得られる条件だけを保存する。
不足fieldを現在の選択、現在のA、現在のheadから補って当時の条件と見せない。
Blind中はしおり操作と内容を隠し、通常比較へ復帰してから利用できるようにする。
Blind試行の再開、回答の持越し、未Reveal音源の識別を本機能で実装しない。

### しおりから比較を準備する

しおりを開く操作は、音源選択と比較準備の要求である。
現在の試聴があれば既存手順でAへ戻し、その確認後に対象POSTだけへ条件を適用する。
準備完了後も可聴音はAとし、BまたはCの試聴は別の明示操作にする。

1. しおりと参照snapshotのversion、hash、参照関係を検証する。
2. 登録音源の同一性、Cue境界、利用可能な方式を現在の環境で再検証する。
3. 現行一覧に同じ条件がある場合はその条件を使い、ない場合は保存したsnapshotを一時比較として準備する。
4. 現在のAに対する音量合わせ、位置対応、peak条件を既存admissionで再確認する。
5. 条件を表示し、本人がB/Cを選んで新しい比較を開始する。

過去の固定gainやsame-song alignmentを現在のAへそのまま適用しない。
再開した比較は新しい開始eventを持ち、元のしおりと履歴は不変とする。
一時比較は現行Preset一覧へ偽のPresetを追加せず、通常選択と区別して表示する。
終了時の戻り先は第5節の遷移表に従い、reviewから来た場合はその項目へ戻れるようにする。
既にない音源や通常選択を別の曲へ置換しない。

| 復元可能な範囲 | 表示と出口 |
| --- | --- |
| 条件と音源が揃う | 現在のAとの比較を準備できる |
| 音源はあるが現在のgainや位置の根拠が足りない | 必要な再確認を示す。別方式への切替は本人が選ぶ |
| source欠損、変更、非対応方式 | メモと当時の条件を読める。音源の再指定と取消を用意する |
| 旧履歴の条件不足 | 読取り専用のしおりとして残す。不足を推定した再現はしない |
| Capture情報はあるが過去A音声がない | 過去の観測は読めるが、当時のA音声は再生できないことを示す |

原音試聴へ切り替える場合も自動で行わず、しおりの保存内容を変更しない。
新しい音源へ付け替える操作は派生したしおりとして保存し、過去の証拠を上書きしない。

## 7. 追加データの保存と互換性

### OSの原本と適用transaction

追加データはOSのreference/workflow-v1配下へ分離する。
moments、reviews、bookmarksにはIDとrevisionごとの不変JSONを置き、indexのheadはhash付きsnapshotをatomicに指す。
既存reference/presets、templates、settings、editor_draftsのschemaへ新しい種類のレコードを混ぜない。
原本と関連するindexを検証保存してからheadを更新し、途中終了で不完全な一覧を公開しない。
原本の更新とindex更新は既存の単一OS writer権限と期待revisionで保護する。

聴きどころ適用は、既存Preset保存と適用元linkの二つを更新する。
先に適用元revision、適用先の期待revision、新しい本文hashをpending transactionへ永続化する。
既存保存処理でPresetを確定し、実際のreceiptが一致してから適用元linkを確定する。
途中終了後は一致したPresetだけにlinkを補完し、同名Presetや後続revisionへ付け直さない。
linkの確定が遅れても、保存したCueとメモはPreset単体で使える形式にする。
Tonal形式が先に実装された場合は、その形式別保存dispatcherを使い、旧領域へ新版Presetを書き戻さない。

### Hyphaへの追加配信

既存Reference transport root内のlibrary/workflow-v1を追加経路とする。
旧Library manifestと旧event journalは変更せず、追加のmanifest、snapshot、command、ack、eventを別schemaで検証する。
OSのproducer sessionと公開headのhashを結び付け、再起動前の残存headを新しい配信と誤認しない。
旧OSまたは旧Hyphaは追加領域を読まず、通常のA/B/Cと旧Presetの利用を継続できるようにする。

今日の確認としおりは、選択済み比較に必要な不変snapshotを配信する。
元のpublication_refとsource receiptを保持し、古い条件を現在のLibrary head由来と偽らない。
一時比較のcontextを生成するadapterと通常Libraryのadapterを分け、音声admission、準備、終了処理は既存実装を使う。
古い表示名しかないデータを、再生可能なcontextとして受理しない。

### 実行journalとしおり保存の確認

Hyphaのreview実行journalはreview ID、attempt ID、runtime ID、event ID、連番、直前event hashを持つ。
reviewに属さないしおりの開閉等は別kindの操作記録とし、存在しないreview IDやattempt IDを捏造しない。
いずれもclosed schemaで種別を判定し、通常比較の試聴eventとは混ぜない。
同じevent IDの再送は同じ本文なら一回として扱い、異なる本文や飛び越した連番を現在状態へ適用しない。
OSは個別attemptを検証して取り込み、異なるPOSTの進捗を一つの状態へ合算しない。
親checkpointが同じ分岐は別の再開として表示し、遅い同期で新しい記録を上書きしない。

確認状態の更新としおり保存は、Hyphaの非RT側でローカルjournalまたはoutboxへ永続化する。
OSが停止中でも、取得済みの定義による確認とローカル保存を使えるようにする。
OS未反映とローカル保存失敗を分け、前者は再接続時に同じIDで再送する。
OSへの反映完了は、対象IDと本文hashが一致する永続化ackでのみ認定する。
ローカル書込みに失敗した明示操作は通知し、未保存の入力と再試行の出口を保持する。

### 条件、メモ、checkpointの確定順

Hypha所有のreference-workflow-v1保存領域に、不変artifact、journal、outboxと検証済みheadを分けて置く。
実行中の条件revisionはHyphaが所有し、OSは同じIDとhashの実行記録として取り込む。
OSの確認定義、元Preset、別attemptの条件を受信側の都合で書き換えない。
OSにしか存在しない条件snapshotをofflineで使う場合は、必要な依存物も先にローカルへ検証保存する。

1. 条件snapshot、必要な比較projection、長文メモ等を先に不変artifactとして保存し、長さ、hash、参照関係を検証する。
2. そのreceiptを持つjournal eventまたはoutbox記録を永続化し、期待する連番と直前hashを確認してcommit headをatomicに進める。
3. commit済みの連番とhashから公開候補を組み立てる。遷移を伴う操作は必要なA復帰receiptも揃った後に、checkpoint、確認状態、mode、戻り先と選択を一組で公開する。確認して次への次項目refは同じ操作記録に含め、待機中は既存の公開状態を維持する。
4. 有効なlifecycleとgenerationへ非RTのdirty通知を出す。OSへの再送はこの後に独立して行う。

ローカル保存完了の境界は、依存artifactとcommit headの永続化・再検証が終わった時点とする。
一時ファイルの作成、writeの受付、メモリ上のqueue投入だけを保存完了と呼ばない。
各platformで必要なファイルとdirectoryの永続化を実writerの試作で確認し、プロセス終了と電源断に対する保証範囲を分けて記録する。
OSは受け取ったartifactの依存関係をすべて検証し、原本とindexの永続化後にだけIDと本文hashを持つackを返す。
依存物未着、hash不一致、途中までの連番は保留し、条件不足のまま確認済みやしおり保存済みへ進めない。
保存結果が不明な再試行は同じ操作IDと本文hashで照合し、再クリックごとに別の記録を作らない。

| 終了・失敗した境界 | 回復と表示 |
| --- | --- |
| ローカルcommit前 | 保存済みcheckpointは前のまま。動作中は未保存入力と再試行を保持するが、強制終了でその入力まで復元する保証はしない |
| artifactのみ完成、eventまたはheadが未確定 | 完成artifactを現在状態として単独採用しない。記録と依存物を照合してcommitを確定できた分だけ回復し、孤立物から本人の操作を推定しない |
| ローカルcommit後、checkpoint公開またはdirty通知前 | commitと依存物から回復可能。古いDAW保存はそのまま示し、より新しいローカル記録を続きとして選べるようにする |
| ローカルcommit後、OS反映前 | このPCでは保存済み、OSには未反映と区別する。同じIDで再送し、反映待ちのための再入力を求めない |
| OS確定後、ack受信前 | 再送の照合で同じackを返し、しおりや確認操作を二重登録しない |
| 依存物が欠損・破損、または別PCにない | 対象checkpointを復元済みとせず、最後に検証できた状態と欠損の事実を示す。通常ReferenceとCaptureは保持する |

進捗や本人のメモを持つ未保存操作を、表示要求のように最後の一件へ黙って置き換えない。
保存待ちを有界に保ち、満杯時は新しい保存操作を受理していないことと再試行の出口を示す。
A復帰、取消、通常音声の継続はこの保存待ちの容量に依存させない。

通常Libraryの試聴eventは開始時のLibrary形式に対応するwriterへ、一時比較の試聴eventは追加journalへ送る。
同じ試聴を両方へ二重記録せず、音声側で確認した事実を共通の投影から受け取る。
OS Historyは保存元namespaceを含むkeyで統合し、開始と完了のcontext全体を照合する。
Tonal計画のpublication_refと同じ識別規則を用いるが、その新版event実装が完成済みとは仮定しない。
保存先namespace、由来のpublication_ref、実際の条件revisionのreceiptは統合計画第12節に従って区別する。
壊れた追加eventはその記録だけを保留し、既存Historyや正常な確認回を表示不能にしない。

## 8. DAW保存、資源、失敗時の契約

DAW状態へはversion付きの別XML要素を追加し、mode、確認セッションとしおりのref、checkpoint、現在項目、戻り先refを含む再開用の最小状態だけを保存する。
曲、Preset全文、音声、全履歴、しおりのメモ一覧を埋め込まない。
ReferenceChoicesの既存versionとACaptureのdata属性を変更しない。
ReferenceChoicesへは通常B/C選択だけを保存し、reviewやbookmarkの一時選択を混入させない。
追加要素はcommit済みの条件とcheckpointを参照し、記録の連番とhashより先の状態を保存しない。
旧版を経由した再保存で追加要素が失われる場合は自動再開を保証せず、OSの原本から明示的に開き直せるようにする。

getStateInformationは事前に組み立てた一組のsnapshotを読むだけとし、その場でファイルI/Oやencodeを行わない。
統合計画第12節の共通組立て責務を使い、Tonal readyとworkflow更新の完了順で他方を巻き戻さない。
setStateInformation直後の再保存でも受信済みの有界な追加要素を保持し、非同期復元前に消さない。
checkpoint更新後はEditorの有無と無関係に非RTでdirty通知し、古いgenerationからの通知や結果公開を拒否する。
DAW保存時点以後の未保存操作はローカルjournalから回復可能な分だけ提示し、dirty通知を自動保存完了と呼ばない。

別PCに必要な追加保存物がない場合は、通常Referenceと既存Captureを維持する。
そのPCへ存在しない確認セッションを復元済みと表示せず、元のOS保存領域を利用できる状態での再読込みを案内する。
今回、新しい音源同梱bundleや同期サービスは作らない。
確定した原本やjournalをcacheとして自動削除しない。
一覧からの取り下げと、古いDAW保存に影響する実体削除を同じ操作にしない。

| 対象 | 設計上限と検証条件 |
| --- | --- |
| 聴きどころ | 1 revisionは32 KiB以内。既存CueとNoteの入力制約も守る |
| しおり | 1 revisionは64 KiB以内。大きな条件はhash付きsnapshotへの参照で保持する |
| 確認セッション | 最大64項目。定義は64 KiB以内、必要な比較projectionの合計は既存Library Preset上限と同じ2 MiB以内 |
| journal event | 16 KiB以内。長文メモや比較snapshotは別artifactに置き、eventにはreceiptを入れる |
| DAW追加要素 | encode後4 KiB以内。既存Captureと他の設定を含む完成stateで1 MiB未満を検証する |
| 検索と履歴表示 | 1ページ128件以下。永続indexを使い、入力のたびに全ファイルを探索しない |
| 追加の常駐メモリ | 1 POSTあたり8 MiB以内を初期予算とし、読取り中、公開中、保存中、再送中、退役ownerの残存分を含む合計peakを実測する |

上限は新実装の達成値ではなく、WG0で最大入力を使って確認する設計予算である。
Tonal計画の保存増分が共存する場合も、既存state用予約を機能ごとに重複計上しない。
Capture、Tonal、今回の追加要素を含む実encoder出力で合算し、入り切らなければメタデータ配置を改訂する。
共通の配分は統合計画第8節の1,041,420 bytesであり、1 MiBまでの余裕は7,156 bytesである。
この算術値を実encoderの達成値とせず、戻り先、最大Unicode、escape、restore待ちも含めて検証する。
既存Captureの内容削除や、メモの無断切捨てで合格扱いにしない。

新しいFFT、計測worker、PCM保存、Analysis枠を追加しない。
通常Aはbit identical、0 samplesを維持し、audio callbackへのalloc、lock、I/O追加を0とする。
OS停止、配信遅延、ディスク停止で通常Aを止めず、UIやprocessor破棄から探索や保存完了を待たない。
保存の所有者はprocessorの寿命と分離し、wrapper unload時に実行中コードや遅延callbackを残さない終了方式を実証する。

## 9. 画面への組込み

OSは既存Reference画面内で、Candidate詳細の登録、比較曲選択の再利用入口、Presetの確認項目選択、Historyのしおり絞込みを追加する。
新しいTop navigationや独立した3画面は作らない。
長文の編集はOSを基本とし、Hyphaのしおり保存は名称と任意メモに絞る。
日本語と英語の両方で、曲名が長い場合、同名曲、空項目、保存失敗を確認する。

Hyphaは通常時のA/B/C、BのVersion選択、CのCheck選択、A復帰を全サイズで維持する。
今日の確認が選ばれた間だけ、現在項目と前後移動、確認状態への入口を表示する。
300×200と375×250は選択欄と補助メニューを使い、文字縮小で全操作を押し込まない。
450×300、600×400、900×600も共通状態投影から描画し、OS Previewを同時に更新する。
実寸で主面に収まる前後移動の配置をWG0で決め、通常の試聴操作数を増やさないことを条件にする。
「確認して次へ」と「保留して次へ」はreview中の主面に置き、メニューを開く別クリックを含めずに済む各1操作を全サイズで満たす。
しおり中は戻り先が確認回か通常比較かを示し、戻る操作は1回で要求できるようにする。
処理待ちを理由に同じ開始や確認操作を押し直させず、未保存とOS未反映の違いをその場で短く示す。

Hyphaの固定UIは英語とし、本書の日本語操作名は仕様説明として扱う。
CE2226の共通素材と意味色を保ち、星、品質点数、連続利用日数、点滅する未完了通知を追加しない。
keyboard操作とaccessibilityも同じ状態と可用性を使い、DAWの再生shortcutを横取りしない。
Blind中は名称、メモ、tooltip、accessibilityを含めて対象情報を漏らさない。

## 10. 影響範囲と実装順序

パスは第2節の両Reference worktreeを基準とする。
新モジュール名は候補であり、着手前に既存責務との重複と全callerを確認する。

| 所有 | 主な対象 | 変更責務 |
| --- | --- | --- |
| OSの再利用 | referenceWorkspaceCandidate*.mjs、referenceWorkspaceEditor.mjs、referenceWorkspaceTemplateRepository.mjs、新referenceListeningMoment*.mjs | 聴きどころ、Cue適用、適用transaction、検索index |
| OSの確認定義 | referenceWorkspacePresetSelection.mjs、新referenceReviewSession*.mjs | Preset snapshot、項目順、条件revision、再開checkpoint |
| OSのしおりとHistory | referenceLibraryHistory.mjs、referenceWorkspaceRuntimeEvent.mjs、新referenceComparisonBookmark*.mjs、referenceWorkflowHistory*.mjs | しおり、旧履歴の不足判定、namespace付き統合、原本保存 |
| OSの配信 | src/port/reference/ipcRouter.cjs、libraryService.mjs、新referenceWorkflowDelivery*.mjs | optional配信、outbox取込み、権限、同一ID再送と永続化ack |
| OSの画面 | ReferenceCandidateEditor.jsx、ReferenceSourcePicker.jsx、ReferenceCheckEditor.jsx、ReferencePresetPanel.jsx、ReferenceWorkspaceScreen.jsx、ReferenceHistoryPanel.jsx、ReferenceGlobalHistory.jsx、locales/{ja,en}.json | 登録、選択、今回の項目、しおり作成と再開 |
| Hyphaの状態と準備 | PluginProcessorReference.cpp、ReferenceComparisonController.*、ReferenceRuntimeV2Controller.*、ReferenceRuntimeV2Repository.*、ReferenceLibraryRepository.*、新ReferenceWorkflowSession.*、ReferenceFrozenComparison.* | 排他的mode、通常選択と戻り先、一時context、POST分離、項目遷移、旧試聴待ちの失効、既存admissionへの接続 |
| Hyphaの保存と履歴 | ReferenceComparisonSettings.h、PluginProcessorState.cpp、ReferenceRuntimeEventTransport.*、新ReferenceWorkflowJournal.*、ReferenceWorkflowState.*、共通保存snapshot組立て責務 | attempt別条件artifact、依存物先行commit、checkpoint、通常選択だけの旧XML投影、dirty通知、outbox、journal、writer振分け |
| 共通表示 | PluginEditorReference.cpp、HyphaReferenceComponent.*、HyphaReferenceLayout.cpp、新HyphaReferenceWorkflowControls.*、ReferenceHyphaPreview.jsxとPreview adapter | 全サイズの操作、保存状態、前後移動、Blind秘匿 |
| 検証と契約 | 各repoのReference/schema/native/UI試験、README、Hypha invariantsとReference製品契約 | 新しい保存範囲と非再現条件、回帰試験、最終実機記録 |

既存500行超ファイルへ変更を加える場合は、対象責務を先に500行以下のモジュールへ抽出する。
既存巨大ファイルの行数を増やさず、無関係な責務の一括分割は行わない。
OSとHyphaでコードを共有せず、GPL境界を越える連携は検証済みファイル契約で行う。

| 工程 | 作業 | 次へ進む条件 |
| --- | --- | --- |
| WG0 共通契約の実証 | 実encoder、実writer、一時比較入口、保存ownerとwrapper終了、実寸操作を試作する | 下記WG0-E／F／C／L／M／Uをすべて満たす。状態モデルだけで実行試作のpassを埋めない |
| WG1 聴きどころの再利用 | 原本保存と検索、Cue適用、非破壊更新を完成する | 別Presetへ再利用でき、旧revision、既存Cue、Noteを失わない |
| WG2 今日の確認 | 定義、Hyphaの順次確認、journal、checkpoint、再開を完成する | 元Preset不変、他POST分離、日付と再起動をまたいだ再開、artifact先行commitが成立する |
| WG3 比較しおり | 作成、メモ、History統合、条件の再検証と一時比較を完成する | 通常A/BとA/Cの条件を呼び出せ、確認回との往復が成立し、不足時は読取りと修復の出口を残す |
| WG4 全体検証 | 3機能の通し操作、全サイズ、初見試行、両platform、対象DAWで確認する | 下記R1〜R12、V1〜V3と既存回帰が最終ソースでpassする |

WG1→WG2→WG3は利用者が指定した実装順であり、いずれも本計画の完成範囲とする。
WG0は三つの実装が別々の保存方式へ分かれないための共通実証である。
失敗した境界は同じ計画内で修正し、承認なく後のPhaseへ移して完成扱いにしない。

### WG0の必須証跡

各証跡に両repoのexact commit、試作差分のhash、fixture、実行環境、コマンド、期待値と実測値を記録する。
Tonal側のG0と共通の証跡を使う条件と、未実装側の最大形式fixtureの扱いは統合計画第12節に従う。

| ID | 必須の実証と終了条件 | 代替不可の条件と後続検証 |
| --- | --- | --- |
| WG0-E | 実encoder／decoderの試作に最大Capture、Tonal最大形式、workflow最大形式、最大既存設定を同時投入し、stateが1 MiB未満、workflow増分4 KiB以内、読戻し欠落0を確認 | 手計算やJSON文字列長では不可。旧readerとrestore直後再保存も実行し、最終DAW保存はWG4で確認 |
| WG0-F | 実filesystem上のwriterで条件artifact、journal、commit head、outbox、OS原本、ackの各境界へ終了・欠損・書込み失敗を注入し、第7節の表どおりに回復する | メモリ上の保存モデルでは不可。OS不在の条件改訂と偽ack、依存物未着を含め、製品入口の全R2／R9／R10は後続実装で確認 |
| WG0-C | 現行の準備・admission・A復帰入口へ接続した一時context試作で、同じCheckの別Cue、reviewからしおりへの往復、古いreceipt、callback不在を実行する | modeを変えるだけのモデルでは不可。検証済み条件だけが適用され、自動B/C再生0、通常選択の汚染0を確認。最終DAW音声経路はWG4で確認 |
| WG0-L | 実保存ownerと遅延I/Oを使い、macOS／Windowsの隔離した検証ホストで対象wrapperをload／unloadする。退役中を含む追加RAM8 MiB以内と、破棄後の実行中コード・callback残存0を確認 | threadを持たない模擬unloadやgeneration更新だけでは不可。停止できないI/Oを残してunloadする方式は不合格。UI／RTの完了待ち0を満たし、対象DAWではWG4で再検証 |
| WG0-M | 遷移表、条件revision、複製projectの分岐、保存とA復帰の完了順を契約モデルで検査する | モデルとして記録し、WG0-E／F／C／Lの証跡へ転用しない |
| WG0-U | 実データによる300×200と375×250の操作可能な試作でV1〜V3を通し、部分操作上限と各課題の総操作上限を固定する | 静止画と設計者の説明付き成功だけでは不可。初見試行はWG4で別に行う |

WG0終了時に各行をpass／fail／未実施で示し、実行試作、契約モデル、動線試作を別欄へ記録する。
実行必須行が未実施、またはV課題の総操作上限が未確定ならWG1へ進めない。
形式や所有者を変更した場合は該当する行を再実証する。

## 11. 受入試験

| ID | 入力と操作 | 合格条件 |
| --- | --- | --- |
| R1 再利用 | 同じ曲から二つの目的を登録し、MIXとMasteringの別Presetへ適用する | 曲、整数Cue、目的が一致し、元の聴きどころと他Presetが不変 |
| R2 適用境界 | 同じCue、既存4 Cue、候補上限、既存Note、最大Unicode、保存競合を含める | 重複や無断削除、切捨てなし。取消で変更なし。適用transactionの各境界から正しいrevisionへ復旧 |
| R3 音源と原本変更 | 原本改訂、一覧から取り下げ、音源移動、同名別master、削除、再登録 | 過去条件不変。検証済み同一音源だけ再解決し、別音源を代入しない |
| R4 今回の項目 | 1、3、64項目、空Check、並べ替え、元Preset更新、OS不在の項目内Cue変更、同じCheckで別Cueのしおりを開く、通常一覧への移動を操作する | Presetと通常B選択が不変。本人の確認はcommit済み条件revisionに対応。項目編集だけが新条件を作り、しおり閲覧で中断項目を変更しない |
| R5 音声復帰 | 既にAで停止中の次へ、C試聴中の次へ、B試聴中の開始、callback不在、Blind所有中、連打、古い復帰receipt、旧試聴開始待ちを試す | 安全なA状態では新callbackを要求しない。必要なA復帰確認と保存commit後だけ新選択を確定。保存待ちでもA復帰と取消は可能。Blind終了後の自動適用、旧待ち要求の再生、新Cの自動再生は0回 |
| R6 再開と分離 | 日付変更、OS停止、古い保存、project複製、二つのPOST、review → bookmark → 保存 → 再open → review復帰、旧reader経由の再保存を試す | checkpointと戻り先を保持し、明示再開で新attemptを分離。旧XMLには通常選択のみ。追加要素欠損でも通常ReferenceとCaptureを保持し、自動再開しない |
| R7 しおり作成 | A/B、A/C、今日の確認内外、未試聴条件、選択変更直後、旧Historyを使う | 保存対象が明示され、聴取事実を捏造しない。目的と今回のメモが独立 |
| R8 しおり再開 | 当時と違うA、同じ曲の別Version、gain不足、位置不明、音源欠損、旧Preset削除、二つのしおりを続けて開く、reviewへの復帰と途中取消を含める | 検証済み一時contextだけ準備し、gainを再検証。通常選択と中断項目は不変。戻り先の入れ子や似た曲への代入なし。当時のA音声再現を約束せず、自動再生0回 |
| R9 配信と履歴 | legacy／tonal-v1／workflow-v1の同じrevision/event ID、OS再起動、途中公開、重複再送、順序逆転、偽ack、依存artifact未着を使う | 保存先と由来namespace、condition receiptを区別。依存物の永続化前にackなし。二重取込みなし。異常記録だけ保留し、旧LibraryとHistoryを維持 |
| R10 保存と破棄 | 条件・メモartifact、journal、commit head、checkpoint、dirty、OS確定、ackの各前後で終了。restore直後保存、書込み失敗、遅延I/O、Tonal readyとの完了順反転、wrapper unloadも確認 | 第7節の復元表どおり。ローカルcommit後は依存物を含め回復可能、commit前の未保存を復元済みとしない。動作中の失敗は入力と再試行を保持。snapshot混在と巻戻し0、破棄済みcallback0、RT/UIの完了待ち0 |
| R11 最大値と安全性 | 最大Capture、最大既存設定、今回の最大状態、Tonal共存、超過長、symlink、path逸脱、破損を使う | 第8節のencode/RAM上限内。既存データ欠落0。通常Aのbit identityと0 samplesを維持 |
| R12 利用手順と表示 | V1〜V3を全5サイズ、英日OS、Preview、keyboard、Blindで通す。Tonal表示あり／なし、reviewとしおりの往復も確認する | V課題の操作上限を満たし、通常A/B/Cの操作数増加なし。追加Connect、既知条件の再入力、別条件や保存状態の誤認、秘匿情報の漏出は0 |

WG0では1,000件の聴きどころと10,000件のしおりを検索fixtureとして用意し、先頭だけでなく末尾の項目と同名曲を確認する。
検索結果の件数と読取り量を別々に測り、128件表示を全探索量の上限とは扱わない。
保存失敗時、OS不在時、部分的に壊れた履歴でも、正常な通常Referenceを使えることを検証する。

### 操作数と理解を確認する三つの課題

V1〜V3は、Tonal側のU1〜U3とは別の聴取手順の課題である。
初回登録、反復確認、翌日再開を分け、準備時間と反復時の省力化を混ぜない。

| ID | 開始条件と課題 | 操作と理解の合格条件 |
| --- | --- | --- |
| V1 初回登録と再利用 | 既存CandidateのCueから聴きどころを保存し、別Presetへ適用する。空き枠ありと4 Cue満杯を別に試す | 検証済み曲とCueの再指定0。選んだ聴きどころの適用は確認画面の1回の確定で保存と配信要求まで進み、同じ保存の再確認0。既存メモの保持が既定。原本と適用先のどちらが変わったかを説明できる |
| V2 登録済み3項目を反復確認 | 定義済みの確認回を開始し、3項目を聴く。確認して次へ、保留して次へ、しおりへの寄り道と復帰を含める | 確認して次へ／保留して次へは各1操作。移動後の曲／Cue再選択0、準備済みCへの試聴要求1操作、待機後の再クリック0。しおりから戻る要求1操作。本人の確認状態と実際の試聴を区別できる |
| V3 翌日の続きとしおり再開 | 同じPCでDAWを再openし、保存checkpointから再開する。続いて既存しおりを開く。より新しいローカル記録がある場合も別に試す | path再入力と既知の曲／Cue／方式の再入力0。有効な「続きから」は1操作で準備を要求し、B/C再生は別の明示操作。古い保存と新しい記録の選択は必要時だけ示す。過去A音声の再生ではないことと、ローカル保存／OS反映の状態を説明できる |

上表の1操作にはメニューを開く別操作を隠して含めない。
表にない登録入口、名称入力、項目選択、検索、適用先選択も課題の総操作に含める。
WG0-Uで同じ開始条件による既存手順と試作の操作列を取り、各課題の総操作上限と短縮対象を確定する。
既存手順で同じ結果を作れない部分は比較不能と記録し、架空の既存クリック数との差を改善率にしない。
短縮を主張する部分では再選択や確認回数の減少を実測し、新機能の成功や通常A/B/Cの非劣化だけで代替しない。
4 Cue満杯、音源欠損、競合など安全上必要な選択は正常系と別に数え、黙った上書きで上限を達成しない。

記録欄は「課題ID／版／参加者匿名IDと既利用課題／サイズと環境／開始状態／操作列／クリック・メニュー開閉・確定回数／アプリ切替／文字入力・drag・keyboard／準備時間と待ち時間／誤操作と引返し／説明介入／条件と保存状態の回答／完了または未完了」とする。
WG4の初見試行では目的だけを伝え、操作箇所や用語の答えを先に教えない。
同じ参加者の再試行や他課題の学習を初見の成功へ混ぜず、説明介入や条件誤認があった試行を合格にしない。
協力者は未定で、現在の観察結果と操作数の実測はない。
未実施のV課題を実証済みとせず、WG4と統合計画全体の完成条件に残す。

最終ソースで対象native/schema/UI試験とRust workspace test、owned warningなしのclippyを実行する。
FFIを変更した場合はignored parityとpairingの対象件数を実測して全件実行する。
OSは対象試験後に全体suiteを通し、共通Previewも検証する。
macOSとWindows、Studio OneとPro Toolsの対象wrapperで、通常試聴、保存復元、close/open、終了を実機確認する。
未確認のAAX clock/PDC証明をこの機能の完成証拠で代替しない。

## 12. 今回の確認記録と申し送り

計画作成時にW-3080のCandidate、CandidateCue、Editor、EditorPersistence、TemplateRepository、RuntimeEvent、LibraryDelivery、LibraryVersionsの8 test fileを実行した。
結果は47件pass、0 fail、0 skipだった。
既存入力と保存境界のbaseline確認であり、本計画の新機能R1〜R12のpassではない。
ローカルのLastTestsFailedログも確認したが、古いbuild結果を現在の不具合の再現結果として扱わない。

初版とレビュー時の既存試験記録はそのまま残し、今回の修正機能のpassへ転用しない。
統合改訂でもW-3080の同じ8 test fileを再実行し、47件pass、0 fail、0 skipだった。
文書の整合確認と新しいWG0／R／Vの実証は区別し、後者は未実施のまま残す。
計画置き場のmainで起動したRust workspace testとclippyは全体完了前に中断し、両方とも終了コード130だった。
既存3 suiteの110件passを新機能の完成証拠にせず、全体検証は未完了と記録する。
今回の変更は本書と統合計画v4だけで、作業開始前から変更されていたS-1音源と他worktreeの製品差分を保持する。
比較modeと戻り先、attempt内条件の確定順、WG0の実行必須証跡、V1〜V3の受入条件を追加した。
統合計画では共通DAW snapshot、Tonal表示との分離、三経路の履歴識別、合算容量、共存試験へ接続した。
新しいcommitとB番号は発行しない。
NotionのSECTION:DEVとTASKSは利用可能なread toolがなく未読であり、Notionへの書込みも行わない。
新しい外部APIや外部計測方式を導入しないため、今回は追加の外部調査を必要条件にしない。

次の聴取手順実装はWG0から始める。
特に、一時比較contextの安全な選択、保存stateの合算容量、複製projectのattempt分離、wrapper終了を先に実証する。
これらの未実証事項を解消するまで、新機能が完成したとは報告しない。
公開する場合は既存runbookに従い、LS用macOS pkg、HP用macOS zip、同一commitのWindows installerを揃える。

## 13. 参照

- [現行Reference Library receiver](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/docs/reference_library_receiver_20260913.md)
- [現行ReferenceとCaptureの利用者向け説明](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/README.md)
- [Captureと操作導線の統合計画](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/docs/hypha_capture_and_workflow_integrated_plan_20260914.md)
- [Candidateの保存契約](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/src/services/referenceWorkspaceCandidate.mjs)
- [Cueの保存と境界検証](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/src/services/referenceWorkspaceCandidateCue.mjs)
- [Presetの編集処理](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/src/services/referenceWorkspaceEditor.mjs)
- [Preset原本の保存](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/src/services/referenceWorkspaceTemplateRepository.mjs)
- [Library Historyの現行reader](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/src/services/referenceLibraryHistory.mjs)
- [Runtime eventの検証](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/src/services/referenceWorkspaceRuntimeEvent.mjs)
- [DAW状態の現行保存](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/juce_shell/src/reference_audition/ReferenceComparisonSettings.h)
- [共通Reference UI](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/juce_shell/src/HyphaReferenceComponent.cpp)
- [Hyphaの表示契約](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/docs/hypha_ce2226_jungle_visual_system_20260901.md)
- [Reference統合実装計画v4・共通契約とTonal詳細](reference_c_tonal_balance_plan_20260914.md)
