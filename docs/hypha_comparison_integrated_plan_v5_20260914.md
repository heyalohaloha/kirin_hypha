# Hypha比較機能 統合実装計画 v5

作成日: 2026-09-14。
改訂: v5 revision 2。レビューを既存実装と照合し、統合順序と受入条件を修正。
状態: 計画修正。実装開始の承認とは区別する。
製品コードの実装、DAWへの配置、実機検証、公開は未実施である。
対象: Reference CのTonal Balance、聴きどころの再利用、今日の確認、比較しおり、ローカルPRE/POST Blind、通常A/B/CとVersion Blindの回帰、比較機能の共通安全条件。

## 1. 本書の位置付け

本書を比較機能全体の唯一の親計画とする。
旧v4は作成時点の設計記録として残し、本書で上書きしない。
実装状況、共通ID、実装基点、保存予算、host matrix、初見試験、統合完成の判定は本書を正本とする。
[v5実行・検証契約](hypha_comparison_v5_execution_contract_20260914.md)を本書の規範付属書とし、共有責務、資源の内訳、RB0からRB4、RB-V01からRB-V08、RB-U1をそこで定義する。
本書と付属書は同じ計画revisionとして更新し、食い違いがあれば実装前に解消する。

旧文書の詳細設計は、次表で採用した範囲だけを本書から参照する。
旧文書の基点、改訂記録、全体完成条件、共通ID、共通予算が本書と異なる場合は、本書を優先する。

| 参照文書 | v5で採用する内容 | v5が置き換える内容 |
| --- | --- | --- |
| [Reference統合実装計画v4](reference_c_tonal_balance_plan_20260914.md) | 第3節から第10節までのTonal固有の測定、Capture、配信、表示、T1からT10、U1からU3 | 題名、実装基点、共通完成条件、DAW保存予算、host matrix、他機能の所有 |
| [聴取再利用と確認手順](reference_listening_workflow_plan_20260914.md) | 聴きどころ、今日の確認、比較しおり、WG0からWG4、R1からR12、V1からV3 | B-884基点、実行ID、共通保存予算、共通完成条件、文書更新記録 |
| [PRE/POST Blind](hypha_pre_post_blind_usability_plan_20260914.md) | 固定4秒Capture、再比較、診断、Pause、GainMatch比較、BL0からBL4、BL-V01からBL-V12、BL-U1からBL-U3 | host matrixの所有、統合完成条件 |
| [比較機能の共通安全契約](hypha_comparison_safety_contract_20260914.md) | CS1からCS8、制作設定、Undo、offline書き出し、失効後の自動再生禁止 | 実装基点、host matrixの所有、統合完成条件 |

v5作成時の参照文書を次のsha256で固定する。

| 参照文書 | sha256 |
| --- | --- |
| Reference統合実装計画v4 | `94172ae2d5b0373f385706a43f20f4323453d80559408def0ee393e841de5272` |
| 聴取再利用と確認手順 | `9a2b65a5742ccdbfbfbbb818704cbba36c5b91299bc276e1d8e7f30a58aaaccd` |
| PRE/POST Blind | `2ca2af8ab6580410f852824804f96d19d21dd49900102140b7407e97f0c071cb` |
| 比較機能の共通安全契約 | `820aff5ae4224d22935abd1cb1656b7eac284ea4de1a0b8261f41503261a16de` |

詳細文書を改訂する場合はv5用の新ファイルを作る。
旧v4の構成文書を書き換えて、過去の計画を後から別の内容にしない。
旧v4第12節の通常選択と一時選択の分離、非RTの共通保存snapshot、配信namespaceと音源由来の区別も採用し、責務と実行順序は本書第12節と付属書へ具体化する。
旧文書のG4、WG4等にある全体baselineの反復実行は、本書第12節のC2一回へ集約する。

## 2. 利用者に届ける結果

Tonal Balanceでは、現在の音Aと比較曲Cを同じ相対パワー尺度で確認し、ジャンル分布を補助基準として利用できるようにする。
聴取手順では、保存した曲とCueを別Presetへ再利用し、その回に選んだCheckだけを確認して、条件と本人のメモを次回へ残せるようにする。
Local Blindでは、制作設定を変えずに固定したPRE/POSTコピーを比較し、不成立理由を理解して再取得または通常制作へ戻れるようにする。
既存Referenceでは、Aを現在のDAW音、Bを同じ曲の別Version、Cを独立したCheckとして維持する。
Version Blindの短い照合観測を比較時間や保存済みA音声へ置き換えず、曲全体の試聴、固定音量、位置対応が今回の変更でも成立することを専用工程RBで確認する。

三機能群と既存Referenceは、次の共通条件を守る。

- 既知の曲、Cue、Preset、比較方式を再入力させない。
- 音声復帰、保存、解析待ちを再クリックで進めさせない。
- 比較操作で制作Gain、Automation、Routing、Transport、Loopを変更しない。
- 接続、復元、workflowの再開、条件選択だけでB、C、Blindの比較音を自動再生しない。有効な試聴中のDAW Pauseと再開は第10.4節の機能別契約で扱う。
- 保存済み、保存待ち、OS未反映を同じ状態として表示しない。
- 計測事実を品質点数、合否、EQ修正指示へ変換しない。

Tonal、聴取手順、Local Blindの固有責務はC0と共有基盤S0の完了後に実装できる。
一機能の失敗や未完成を、他機能の通常利用へ波及させない。

## 3. 完成状態の定義

完成という語を一種類にまとめない。
各状態は次の条件で判定する。

| 状態 | 判定条件 | 公開との関係 |
| --- | --- | --- |
| `engineering_complete` | 対象機能の自動試験、保存境界、対象host実機、負荷、安全条件がpass | 初見試験や配布を完了した意味ではない |
| `ux_accepted` | 第9節の初回導入と全課題の操作試験をpassし、学習後の結果を初見と混同しない | 技術的な安全性を代替しない |
| `feature_complete` | 対象機能が`engineering_complete`かつ`ux_accepted` | 他機能の完成を意味しない |
| `reference_regression_complete` | RB4で通常A/B/CとVersion Blindの専用回帰、対象host、RB-U1をpass | 既存機能の回帰受入。新しい照合・Gain方式の採用ではない |
| `integration_complete` | 三機能が`feature_complete`、既存Referenceが`reference_regression_complete`となり、C1とC2、CS1からCS8が同じ最終sourceでpass | 公開リリース完了を意味しない |
| `release_ready` | `integration_complete`後、明示された公開作業で三つの配布チャネルを同一versionに揃えた | 本計画だけでは到達しない |

先行した機能は`feature_complete`まで個別に判定できる。
Local Blindの未完了を理由にTonalや聴取手順の実装を止めない。
ただし、個別完成を`integration_complete`または`release_ready`と表示しない。

## 4. 実装基点

2026-09-14のv5作成時に、次の状態を実読した。

| 対象 | branch | exact commit | 作成時の変更状態 | 扱い |
| --- | --- | --- | --- | --- |
| 計画保存先 | `kirin_hypha/main` | `9cb40e56ddbd7c3b9ebb0186c2169467ebaae317` | 既存の変更済みS-1音源と未追跡計画4件あり | 本書だけを新規作成し、既存変更を保持する |
| Hypha Reference | `codex/reference-abc-delivery` | `53937c0da5916c7b771329e98fff79671fb5b22c`（B-887） | clean | 製品実装の基点候補とする |
| Kirin OS Reference | `codex/reference-whole-song` | `31fd330c4b297637bebe321d2d60d6bd59ff643f`（W-3080） | clean | OS側実装の基点候補とする |

実装開始時はC0で三つのworktreeを再取得する。
HEADが移動していた場合は、対象責務の差分を読んで新しい組合せを実装記録へ固定する。
旧commitの試験結果を、未確認の後続差分へ転用しない。
main、別worktreeのcheckout、merge、resetは計画作成に含めない。
Notionへの書込みは行わない。
実装先は上表のHypha ReferenceとOS Referenceの二つのbranchに固定し、計画保存先の旧mainへ製品変更を重ねない。
統合担当は実装を受け持つ主セッション一つとし、両branchの対応commit、共有境界、試験記録を管理する。
同時編集や別担当への委任を本計画の前提にしない。
実装開始前に、親計画、付属書、参照4文書を一つの文書commitとして固定する。
未追跡文書を読み取っただけではC0の計画固定を完了としない。
既存S-1変更は文書commitと製品commitへ含めない。

## 5. 機能境界

似た名前の状態を一つに統合しない。
共有するのは音声安全条件、保存publication、物理資源上限、最終検証だけとする。

| 機能群 | 正本の入力 | 保持する状態 | 変更しない対象 |
| --- | --- | --- | --- |
| Tonal Balance | AのLIVEまたはCapture、比較曲C、任意のジャンル分布 | 60帯域の観測、Cue集計、Capture Tonal artifact、表示設定 | BのVersion、元PCM、既存64点Spectrum、制作音 |
| 聴取手順 | Candidate、Cue、Preset、Check、History | 聴きどころ、review定義、attempt、checkpoint、比較しおり | 元Preset revision、別POST、過去の比較条件 |
| Local Blind | exact PRE/POST pair、固定4秒PCM、固定Gain | trial、回答、Reveal、Return、同じprocessor内の一時Context | Reference schema、Work、DAW制作設定、過去trial |
| 既存Reference回帰 | live A、Kirin OSで計測済みのVersion B、独立したCheck C | 検証済みmap、固定Gain、実聴取receipt、既存trial | Aの全曲LUFSの捏造、Capture Aへの音声置換、Local Blindの4秒契約、INSPECT |
| 共通安全 | 実際の音声出力、要求世代、host通知、保存receipt | 出力拒否、失効、必要なdirty通知 | 各機能固有のGain基準と状態機械 |

Local BlindをReferenceAnalysisOwnerのshared grantへ混ぜない。
Reference側のownerとLocal Blindの既存admissionは別に保ち、物理的な解析2枠だけを合算する。
第三の解析枠、Local Blind専用の常駐thread、追加PCM queueは作らない。

## 6. 共通ID契約

「確認セッション」はUI上の呼び名に限定する。
保存形式と実装では、定義と一回の実行を次のIDで分ける。
旧文書の「実行ID」は使用せず、`attempt_id`へ統一する。

| field | 所有者 | 発行時点と寿命 | 保存先と照合 |
| --- | --- | --- | --- |
| `review_id` | OS | 「今日の確認」の定義を初回作成した時点で発行し、その定義系列で維持する | OS原本、配信snapshot、Hypha journalで同じ値を使う |
| `review_revision_id` | OS | 項目順または初期条件を変更した新しい不変snapshotごとに発行 | `review_id`と本文hashを組にし、attemptは開始時のrevisionを固定する |
| `attempt_id` | Hypha | 一つのPOSTがreviewを新規開始またはcheckpointから明示再開した時点で発行 | 一つのPOSTのjournal headだけを所有し、別POSTと共有しない |
| `runtime_instance_id` | Hypha | processor生成ごとに発行し、processor破棄で終了 | eventとoutboxへ記録し、DAW restore後の現在instanceとして復活させない |
| `checkpoint_id` | Hypha | commit済みattemptの連番とhead hashから作成 | `review_id`、`attempt_id`、連番、head hashを照合する |
| `condition_revision_id` | Hypha | attempt内で曲、Cue、方式を変更してcommitした時点で発行 | 元定義と親条件receiptを保持し、別attemptへ継承しない |
| `event_id` | event writer | journalへ一つの事実を追記するときに発行 | namespace、runtime、attemptと組にして重複判定する |
| `operation_id` | 操作を受理した側 | 保存または更新の一操作を受理した時点で発行 | 結果不明の再送では同じIDと本文hashを使う |
| `listening_moment_id` | OS | 聴きどころの原本を初回保存した時点で発行 | revisionと本文hashを別に持つ |
| `bookmark_id` | 最初の保存操作を受理したOSまたはHypha | 比較しおりの初回保存操作でUUIDとして発行し、作成元を`bookmark_origin`へ固定する | OSはHypha発行IDを再採番せず、改訂は同じIDの新revisionとする |
| `capture_id` | Hypha | base Captureの取得開始時に発行 | Capture内容hash、Tonal artifact、DAW状態を照合する |
| `publication_ref` | 公開側 | manifest公開ごとに作成 | `namespace`、`revision`、`sha256`の三つを必須とする |

同じ`review_id`を二つのPOSTで使う場合は、それぞれ異なる`attempt_id`と`runtime_instance_id`を持つ。
Editorを閉じて同じprocessorを開き直すだけなら、失効していないattemptを継続できる。
`runtime_instance_id`を再発行するのはprocessorを生成したときだけとする。
同じprocessorでのrestoreではruntimeを維持し、非永続の`restore_generation`を進めて古い操作、保存公開、試聴要求を失効させる。
project複製や再openでprocessorが再生成された場合は新runtimeとし、保存値を現在runtimeへ復活させない。
保存checkpointからの明示再開は、同じprocessor内でも新しい`attempt_id`を発行する。
単なるEditor再表示や同一attempt内のしおり往復では、runtimeとattemptを再発行しない。
再開元の`checkpoint_id`は親参照として残すが、親attemptのjournal headへ追記しない。
異なるnamespaceの同じrevision番号、runtime ID、event IDを同一記録として扱わない。
review journalの重複判定keyは`namespace`、`runtime_instance_id`、`attempt_id`、`event_id`の組とする。
reviewに属さないしおり操作は`namespace`、`runtime_instance_id`、`operation_id`で照合し、存在しない`attempt_id`を作らない。
commit済みoutboxの再送では、旧runtimeとattemptを作成元として保持し、event ID、operation ID、本文hashを書き換えない。
現在runtimeやrestore世代との不一致を理由に保存済みeventを再採番せず、現在の音声操作への適格性とは別に重複排除する。

## 7. 保存と永続化

### 7.1 保存namespace

既存legacy、`tonal-v1`、`workflow-v1`を固定namespaceとして分ける。
旧readerが読むmanifest、Preset、Historyへ新fieldや新bindingを混ぜない。
不変artifactを先に保存して検証し、その後にhash付きheadをatomicに進める。
一方のnamespaceの破損や遅延で、他方のLibrary、History、試聴を失効させない。

### 7.2 永続化の保証水準

v5の必須保証は、正常終了とプロセスクラッシュ後にcommit済みheadまで回復できることである。
write受付、queue投入、一時ファイル作成だけを保存完了と表示しない。

writerは同一filesystemで次の順序を守る。

1. 不変artifactを一時pathへ書き、長さとhashを検証する。
2. file dataをplatformの永続化APIで確定し、不変pathへatomicに移す。
3. commit記録と新headを書き、file dataを確定してからheadをatomicに切り替える。
4. platformがdirectory metadataの永続化を提供する場合は、親directoryも確定する。
5. 再読取りで依存関係、連番、hashが一致してから保存済みsnapshotまたはOS ackを公開する。

突然の電源断に対する保証は、fileとdirectoryの永続化を対象platformの実filesystemで実証できた場合だけ`power_loss_safe`と記録する。
実証できないplatformは`process_crash_safe`を上限とし、電源断からの完全回復を製品表示や説明で約束しない。
この違いを曖昧な「永続化済み」の一語で隠さず、実装記録のreceiptへdurability tierを残す。

### 7.3 DAW保存予算

DAW plugin stateのhard limitは1,048,576 bytesとする。
実encoderの最大fixtureで16,384 bytes以上の未配分余裕を残す。

| 内訳 | v5設計上限bytes |
| --- | ---: |
| 既存Captureの宣言上限 | 988,172 |
| Tonalの全域要約、状態、receipt、表示設定 | 8,192 |
| workflowのmode、戻り先、checkpoint、最小receipt | 2,048 |
| その他の既存stateと外側XMLの予約 | 32,768 |
| 設計合計 | 1,031,180 |
| 1 MiBまでの未配分余裕 | 17,396 |
| 最低必要な未配分余裕 | 16,384 |

8 KiBと2 KiBはbase64、escape、field名、paddingを含むencode後の増分上限である。
詳細な条件、長文メモ、帯域時系列はDAW stateへ埋め込まず、hash付きartifactを参照する。
実encoderが各増分または最低余裕を満たさない場合はG0-EまたはWG0-Eをfailとする。
その場合は大きなfieldを外部artifactへ移し、既存Captureの削除、精度低下、文字列の無断切捨てで合格させない。
Local BlindのPCM、Gain、承認、回答、再生許可、一時ContextはDAW stateへ保存しない。

次表を旧文書の同名試験の有効な判定値とする。
8 KiBと2 KiBへの優先変更はv5初版にも存在していたが、旧表の数値を実装へ転記しないよう判定箇所を固定する。

| 試験 | このrevisionで使う判定 |
| --- | --- |
| G0-E、T5、T8 | Tonal増分8,192 bytes以下。最大Capture、最大workflow、最大既存stateを同時投入し、読戻し欠落0 |
| WG0-E、R11 | workflow増分2,048 bytes以下。同じ共通encoder fixtureを使い、旧reader経由とrestore直後の再保存も照合 |
| S0、C1、C2の保存検証 | encode済み総stateは1,032,192 bytes以下。hard limitとの差16,384 bytes以上、各内訳も上限以下 |

最大Unicode、escape、namespace、receipt、pending restore、一時選択からの戻り先を同時に含める。
raw struct長や各要素を別々に測った結果だけで合格にしない。

### 7.4 artifactとメモリ

Tonalの一Capture当たりの時系列artifactとrecovery記録は16 MiB以内とする。
Tonalの追加RAMは一owner当たり32 MiB以内、workflowの追加RAMは一POST当たり8 MiB以内とする。
2 MiB上限はB-887のA表示／Capture共有入力queueと受渡しbufferの合計に適用し、Tonalのための追加RT PCM queueは0とする。
既存のVersion／Check照合用観測、decode pages、Local Blind PCMまで含む全Referenceメモリが2 MiBであるとは扱わない。
それらは既存分としてC0でサイズ、個数、寿命を棚卸しし、共存peak RSSに重複なく含める。
runningとdraining、作成中と公開中、restore待ち、encode一時領域をpeakから除外しない。
Local Blindの一時Contextと診断は一POST当たり4 KiB以内とするが、全体peak RSSから除外しない。
Tonalの32 MiBは同じPOSTの退役ownerと後継ownerを合算する予算とし、owner再生成のたびに32 MiBを追加しない。
物理2枠、解析job、I/O、待機要求の数は別に検証し、付属書第2節の上限を満たすまでS0を完了にしない。

## 8. 検索と保存物の寿命

WG0は1,000件の聴きどころと10,000件の比較しおりを含むfixtureを使う。
最大長の原本と25%のindex及びtransaction余裕を含むworkflow保存領域を1 GiB以内に収める。
1 GiBは検証fixtureの設計予算であり、利用者データを自動削除するquotaではない。

検索とindexは次の数値を満たす。
性能値はC0で固定した対象機のうち最も遅い構成で測り、warmとcoldをそれぞれ30回以上実行してp95を算出する。

| 対象 | 合格条件 |
| --- | --- |
| warm検索 | 10,000件でp95 100 ms以下、1回128件以下、個別原本の全走査0件 |
| cold検索 | 検証済みindexの初回検索がp95 500 ms以下 |
| query読取り | 一検索当たり追加512 KiB以下、16 file open以下 |
| index検証 | cold startでp95 2秒以下とし、通常Referenceの利用を停止しない |
| index再構築 | 10,000件で30秒以下、非RTで実行し、途中終了後に旧検証済みindexを利用可能に保つ |
| index RAM | OS側の追加peak 32 MiB以内 |

数値を満たさない場合はWG0をfailとし、測定値を記録しただけでpassにしない。
検索閾値を後から緩める場合は、利用者操作への影響と新しい実測を示して計画を改訂する。

一覧からの取り下げ、archive、実体削除を別の操作にする。
archiveは検索既定から隠すだけで、不変原本と参照を削除しない。
実体削除の前にはPreset、review、checkpoint、bookmark、History、既知のDAW receiptからの参照を列挙する。
有効な参照がある原本は削除せず、参照元を示してarchiveまたは取消を選べるようにする。
参照がない原本を削除する場合もtombstoneへID、最終revision、hash、削除時刻を残し、古いDAW状態を別音源へ結び直さない。
古いDAW projectの全所在は列挙できないため、その可能性を削除確認で明示する。
保存領域はobject数と使用bytesを表示できるようにし、空き容量不足では新しい保存だけをfailとして既存原本を保持する。

## 9. 初回導入と操作試験の共通規約

U1からU3、V1からV3、BL-U1からBL-U3、RB-U1へ同じ判定方法を適用する。
協力者は未確定であり、募集と連絡は別途承認された範囲で行う。

| 項目 | 規約 |
| --- | --- |
| 人数 | 各機能群について、当該機能群をまだ操作・説明されていないDAW経験者5人。Reference回帰RB-U1も5人とする |
| 合格人数 | 最初に割り当てた課題を5人中4人以上が誘導なしで完了し、さらに各課題も5人中4人以上が初回試行で完了する |
| 計数対象 | 機能群を初めて使う試行と、先行課題で学習した後の試行を別欄で集計する。説明後の再試行を成功数へ足さない |
| 課題順 | 三課題の開始順を2人／2人／1人に割り当て、順序と先行課題を記録する。2課題目以降は学習後として扱う |
| 操作上限 | G0-U、WG0-U、BL0、RB0の該当工程で、同じ開始状態の操作列と総操作上限を実装前に固定する |
| 説明 | 目的と開始条件だけを伝え、操作箇所、用語、正解を先に教えない |
| 記録 | 匿名ID、環境、開始状態、全操作、menu開閉、入力、待ち、誤操作、引返し、介入、回答、完了を残す |

次の重大誤認は全課題を通じて0件を要求する。

- 現在鳴っているA、B、C、固定PRE/POSTコピーを取り違える。
- 取得済みPCMを現在の制作設定の音だと取り違える。
- 未保存、ローカル保存済み、OS反映済みを取り違える。
- Return未完了を通常出力へ復帰済みだと取り違える。
- 比較音が残った状態で書き出し可能だと判断する。
- 接続、再open、復元だけで比較音が自動再生されると判断する。

重大誤認が一件でもあれば、原因箇所を修正し、影響する課題を新しい初見協力者5人で再実施する。
以前の成功試行を修正版のpassへ転用しない。
人数不足はfailではなく未完了と記録するが、`ux_accepted`と`integration_complete`には進めない。

TonalのU1からU3は旧v4第10節の課題内容と個別操作上限を使う。
聴取手順のV1からV3は詳細文書第11節の課題内容を使う。
Local BlindのBL-U1からBL-U3は詳細文書第9.3節の課題内容を使う。
同じ5人の課題順を入れ替えても、各課題について5人分の完全な初見試行にはならない。
この規約では各課題の初見成功率を算出せず、機能群への初回導入と、その後の操作の習得を分けて判定する。
初回割当の参加者が一人しかいない課題の独力成功を、普遍的なわかりやすさの証明には使わない。
特定課題の初見性だけが未解決なら、その課題に限って新しい協力者を追加する計画を立て、既存の全課題を繰り返さない。
別機能群で同じ協力者を使う場合も、共有画面や用語を先に学んでいれば初見から除外する。
人数の確保を実装着手条件にはしないが、人数不足を設計者の成功試行で埋めない。

## 10. 機能固有の実装契約

### 10.1 Tonal Balance

Tonalの測定式、60帯域、三plane、窓起点、Capture artifact、旧新版配信、History識別は旧v4第3節から第10節を使う。
G0からG4、T1からT10、U1からU3のIDを維持する。
DAW保存増分だけは本書第7.3節の8 KiBへ置き換える。
G0-Eでは全域要約と最小receiptが8 KiB以内で成立する形式を実encoderで証明する。

Aの観測はC、alignment、OS接続から独立させる。
Cの失敗をAへ波及させず、Captureの範囲変更で再Captureを開始しない。
相対パワーへLoudness Match gainを加算せず、曲線ごとの自動縦移動を行わない。
Blind中は曲線、数値、分布、tooltip、accessibilityから音源情報を漏らさない。

### 10.2 聴取再利用と確認手順

Candidateの`note`をUIで`Listening focus`または「聴くポイント」と表示している現実装を正本とする。
新しい`listening_focus` fieldをCandidateへ追加しない。
聴きどころの目的をPresetへ適用するときは、既存`Candidate.note`の保持、追記、置換を本人が選べるようにする。

WG0からWG4、R1からR12、V1からV3の課題内容を維持する。
旧文書の「実行ID」は本書第6節の`attempt_id`と読み替える。
DAW保存増分、永続化、検索、削除、初見試験は本書第7節から第9節へ置き換える。

review定義、attempt内条件、journal、bookmarkは別の不変snapshotとして保存する。
しおりから比較を準備しても可聴音はAとし、BまたはCは本人の明示操作で開始する。
過去の固定Gain、alignment、A音声を現在のAへ再利用したと表示しない。

### 10.3 Local PRE/POST Blind

固定4秒Capture、固定Gain、両Sourceの完全聴取、回答、Reveal、明示Returnを維持する。
同じprocessorとexact pairで再利用できるのは比較用Contextだけとする。
PCM、Gain、減衰承認、Source割当、回答、復帰receiptを次の比較へ引き継がない。

BL0からBL4、BL-V01からBL-V12、BL-U1からBL-U3の課題内容を維持する。
GainMatchは内部の実機比較対象として記載できる。
比較結果を外部向けの優越主張へ自動転用しない。
Local Blindのhost matrixはBL0ではなく本書のC0で固定する。

### 10.4 共通安全条件

CS1からCS8を通常Reference、Reference Blind、Local Blind、今日の確認、しおりからの比較へ適用する。
対象に存在しない状態は理由付きN/Aとし、他機能の結果を代用しない。
通常Aはbit identical、追加latency 0 samplesを維持する。
Audio Threadへalloc、lock、blocking I/O、探索、JSON、文字列生成を追加しない。
offline、bypass、失効、restore後は比較音を自動復活させない。
比較用の一時操作はparameter gestureや不要なdirty通知を発行せず、Capture、Tonal ready、workflow commitに必要な通知は止めない。

Pauseと終了は次の区別を維持し、共通化で操作を増減させない。

| 経路 | Pause／再開 | 終了と復帰 |
| --- | --- | --- |
| 通常A/B/C | 既存のtransport・許可世代の検証を維持。失効した待機要求は再生再開で復活させない | 明示Aを優先し、job待ちを挟まず通常出力へ戻す |
| Version Blind | 内容矛盾のないPauseとseekは既存mapを保持。再開時の有効clockと内容検証を必須とし、無音だけで再calibrationしない | 既存ENDが通常Aへの復帰を承認する。host停止中の既存確認経路も維持し、LocalのRETURN TO LIVEを追加しない |
| Local Blind | 詳細文書の取得前／準備済み／一巡前／一巡後／途中停止を区別。途中停止や不正seekの失効を維持 | ENDまたはSTOPとRETURN TO LIVEは別の明示操作。対応する非空audio callback確認後、そのEditorからの同一trialの復帰要求に限り追加Closeなしで通常画面へ戻す |

### 10.5 通常A/B/CとVersion Blind

専用工程RB0からRB4とRB-V01からRB-V08を付属書第3節に置く。
既存のsame-song照合、位置精度、固定Gain、全曲streaming、Pause、Historyを、共有安全試験の一部だけで代替しない。
Library受信に手動Work接続を追加せず、INSPECTをReferenceへ組み込まない。
Capture A後の変更表示は音声から確認した差だけに基づき、他社pluginの操作検知や未観測区間までの完全一致を主張しない。

## 11. 共通host matrix

host matrixはC0が所有する。
BL0へ依存させず、必要な実機構成の固定をC0へ集約する。

| 必須対象 | 製品名と版 | OS build | format | sample rateとbuffer | C0状態 |
| --- | --- | --- | --- | --- | --- |
| macOS Studio One | C0で実読して固定 | C0で固定 | VST3 | C0で固定 | 未確定 |
| Windows Studio One | C0で実読して固定 | C0で固定 | VST3 | C0で固定 | 未確定 |
| macOSの出荷対象AU host | C0で実読して固定 | C0で固定 | AU | C0で固定 | 未確定 |
| macOS Pro Tools | C0で実読して固定 | C0で固定 | AAX | C0で固定 | 未確定 |
| Windows Pro Tools | C0で実読して固定 | C0で固定 | AAX | C0で固定 | 未確定 |

「Studio One系」「出荷対象host」のように対象が増減する名称をC0完了後へ残さない。
製品名、版、OS build、formatの一欄でも未確定ならC0は未完了とする。
sample rate、buffer、CPU／architecture、audio device、clock/PDC確認手順も実試験前に同じ行へ固定する。
上表の5行すべてで、Tonal、workflow、通常A/B/C、Version Blind、Local Blindを必須とする。
Local Blindを使わないPRE単体や非stereo状態のstereo専用表示など、存在しない個別状態だけを理由付きN/Aにできる。
未所持host、署名未完了、未実証clock/PDC、実機未接続をN/Aへ変更しない。
B-887では利用者の2026-09-13の指示によりAAXのLocal Blind入口は有効である。
入口を無効化して試験を省かず、exact-range clock/PDCの不成立時の拒否と、実機での成立の両方をBL4で検証する。
この計画はAAX実証済みの宣言ではない。
host matrixを変更した場合は、変更対象に関係するG4、WG4、BL4、RB4、CS試験を新しい組合せで再実施する。
Windows検証機を操作する前に指定のremote access Runbookを読む。
Merging機器へ触れる場合だけ、共有Audio Routes手順を先に実行する。

## 12. 実行順序

次表を担当セッションの入口とする。
詳細な試験入力と状態遷移は各構成文書を使う。

| 工程 | 前提 | 主なrepo | 成果物と終了条件 |
| --- | --- | --- | --- |
| C0 基点と共通境界 | 実装開始の指示 | Hypha、Hypha Reference、OS Reference | 計画commit、実装基点、共有責務の全呼出元、ID、host matrix、操作baseline、容量／検索fixture、既存資源内訳を固定 |
| G0／WG0／BL0／RB0 実証 | C0 | Hypha Reference、OS Reference | 各詳細文書と付属書の試作・基準を検証。G0-EとWG0-Eは同じencoderを使う。相手の未実装fixtureを製品passに数えない |
| S0 共通基盤の先行統合 | 共通責務に対応するG0／WG0／BL0／RB0の実証 | Hypha Reference、OS Reference | 付属書第1・2節のowner、snapshot、ID、取消、retirement、予算を主セッションが直列統合。実encoder、実worker、実wrapperで対象試験をpass |
| G1からG4 | G0、S0 | Hypha Reference、OS Reference | Tonal固有の実装、T1からT10、U1からU3、対象hostを受入 |
| WG1からWG4 | WG0、S0 | Hypha Reference、OS Reference | 再利用→今日の確認→しおりの順で実装。R1からR12、V1からV3、対象hostを受入 |
| BL1からBL4 | BL0、S0 | Hypha Reference | Local固有の実装、BL-V01からBL-V12、BL-U1からBL-U3、対象hostを受入 |
| RB1からRB4 | RB0、S0 | Hypha Reference、OS Reference | 通常A/B/CとVersion Blindの専用回帰、共存、RB-U1、対象hostを受入 |
| C1 共有責務の共存 | 各工程の共有sourceを指定branchへ統合済み | Hypha Reference、OS Reference | 第13節を実装済み全機能で通し、同じsourceで保存、出力、資源、履歴、表示の競合0 |
| C2 統合受入 | G4、WG4、BL4、RB4、C1 | 全対象 | 同じ最終sourceに対するCS1からCS8、初見課題、実host、全体baseline一回をpass |
| R0 公開準備 | C2と利用者の明示指示 | release対象repo | 公開Runbookにより同一versionの三チャネルをreadyにする。本計画の範囲外 |

工程は主セッションが順に進めることを既定とする。
独立したfixtureや固有責務の試作は分離できるが、共有sourceを別々に改変して最後に合流させる方式は採らない。
S0完了後の共有境界変更も主セッションが先に統合し、対応する全工程の対象試験を通してから利用側を更新する。
既存の500行超sourceを変更する責務は先行commitで抽出し、機能追加commitと分ける。

G4、WG4、BL4、RB4は機能固有試験と実機を担当し、全体baselineをそれぞれで繰り返さない。
C2で、変更対象に必要なRust workspace、clippy、native、OS suite、FFI変更時のignored parity／pairingを一つの検証セットとして一回実行する。
固定件数を転記せず、その時点の対象一覧とskip理由を記録する。
途中は影響する対象試験だけを実行し、文書修正のために全体suiteを起動しない。
C2後に製品sourceの追加変更や失敗修正が必要になった場合だけ影響範囲を再検証し、初回結果だけで新sourceを合格にしない。

C0は文書レビューだけでpassにしない。
実encoder、実writer、実worker、実wrapper、操作可能な小サイズ試作を必要な工程で動かす。
契約モデルは状態遷移の検査に使えるが、filesystem、停止ack、unload、実音声の証拠を代替しない。

## 13. 共存試験

共有sourceを変更した工程は、次表の影響先を同じexact commitで検証する。

| 共通責務 | 必須試験 | 合格条件 |
| --- | --- | --- |
| 開始予約と解析枠 | T1、T9、BL-V07、BL-V11、RB-V07、CS8 | 競合する開始確定を直列化。許可された同一ownerのCaptureと通常B/Cは共存でき、第三枠、二重取得、停止ack前の解放0 |
| 音声出力と復帰 | R5、BL-V01、BL-V04、BL-V06、RB-V03、RB-V04、RB-V08、CS4、CS5、CS7 | 第10.4節のPause／終了契約を区別し、失効後の自動再生0。通常Aのbit identityと0 samplesを維持 |
| 保存snapshot | T5、T8、R9からR11、CS2、CS3、CS6 | Tonal readyとworkflow commitの完了順を反転しても他方の巻戻し、欠落、古いdirty通知0 |
| HistoryとID | T10、R7からR9、RB-V05 | legacy、tonal-v1、workflow-v1の同じ番号をnamespace、hash、ID契約で分離し、誤結合0。新runtimeからの旧outbox再送でも重複0 |
| 表示と秘匿 | U1からU3、V1からV3、BL-V08、BL-V12、BL-U1からBL-U3、RB-V06、RB-U1 | 全5サイズで現在音、固定コピー、保存状態、条件差を誤認せず、Blind情報の漏出0 |
| 負荷と終了 | G0-R、G0-L、WG0-L、BL-V11、RB-V07、CS8 | runningとdrainingを含む合算peakとjob数が上限内で、RT待機、破棄済みcallback、unload後の実行中コード0 |

試験IDは表と実装記録の両方で完全名を使い、Tonal、workflow、Local Blindの番号を混同しない。

## 14. 完了判定

TonalはG4、T1からT10、U1からU3を満たして`feature_complete`とする。
聴取手順はWG4、R1からR12、V1からV3を満たして`feature_complete`とする。
Local BlindはBL4、BL-V01からBL-V12、BL-U1からBL-U3を満たして`feature_complete`とする。
既存ReferenceはRB4、RB-V01からRB-V08、RB-U1を満たして`reference_regression_complete`とする。

統合完了には、三つの`feature_complete`、`reference_regression_complete`、C1、C2、CS1からCS8が必要である。
すべての実機結果は同じ閉じたhost matrixと同じ最終sourceへ結び付ける。
未検証host、人数不足、未達の重大誤認、容量余裕不足、durability未確認をpassへ丸めない。
未完了項目を承認なく後のPhaseへ移して完成扱いにしない。

正式レビューへ出す前に、入力、内部判定、保存、表示、操作、遷移、失敗時の出口、役割差、OS差、他機能境界を試験IDへ対応付ける。
最初の正式レビューで指摘が出た場合は、該当箇所だけでなく、影響範囲と事前検証の不足を再点検する。

## 15. 実装記録の必須項目

各工程の記録には次の情報を残す。

- 親計画と付属書のrevision、文書commit、sha256、参照4文書のhash。
- Hypha、Hypha Reference、OS Referenceのbranch、exact commit、開始時と終了時の変更一覧。
- 対象責務と変更ファイル、追加行、削除行。
- fixtureのidentity、hash、sample rate、channel、範囲。
- 実行コマンド、終了コード、pass、fail、skip、未実施。
- state、artifact、index、eventの実測bytes。
- CPU、RSS、callback p95とp99、停止ack、unload結果。
- host matrixの製品名、版、OS build、format、sample rate、buffer、配線。
- 初見試験の匿名記録と、重大誤認の有無。
- 未完了事項と、完成状態が第3節のどこまで進んだか。

古いテストログ、別worktree、契約モデル、別commitの実機結果を現在のpassへ転用しない。
文書作成、製品実装、実機受入、公開完了を別の状態として報告する。

## 16. revision 2時点の未完了事項

C0のhost matrixは未確定である。
G0、WG0、BL0、RB0、S0以降の本計画の実試作と製品実装は未実施である。
U1からU3、V1からV3、BL-U1からBL-U3、RB-U1の協力者は未確定である。
本書の8 KiB Tonal要素、2 KiB workflow要素、16 KiB最低余裕は実encoderで未検証である。
検索時間、index再構築、1 GiB fixture、durability tierは実writerで未検証である。
B-887の既存試験結果は実装基点の情報であり、本計画の完成証拠ではない。

revision 2の作業は本書の修正と規範付属書の追加である。
製品コード、旧v4文書、既存の変更済みS-1音源、他worktreeは変更していない。
製品B番号と配布物は発行しない。
文書のGit固定状態は実際のcommit結果を作業報告へ記録し、未固定の場合はC0へ未処理として残す。

レビューで指摘した保存予算の優先順位とCPU上限は、v5初版と参照文書に既に記載されていた。
今回の修正は未定義と見なして閾値を緩めるものではなく、試験IDごとの有効値と合算する処理を明確にするものである。
初版で一括記載されていたPause後の再生禁止は、既存Version Blindの有効なPause／seek保持と衝突するため第10.4節へ修正した。
初見試験は既定の5人／機能群を維持し、機能群の初回導入と、先行課題の学習を含む操作試験を別の判定欄にした。
同じprocessor内のcheckpoint再開でruntime IDも発行する記述は、第6節の寿命定義へ揃えた。

## 17. 参照

- [v5実行・検証契約](hypha_comparison_v5_execution_contract_20260914.md)
- [Reference統合実装計画v4](reference_c_tonal_balance_plan_20260914.md)
- [聴取再利用と確認手順](reference_listening_workflow_plan_20260914.md)
- [PRE/POST Blind](hypha_pre_post_blind_usability_plan_20260914.md)
- [比較機能の共通安全契約](hypha_comparison_safety_contract_20260914.md)
- [B-887 CaptureとReference解析所有権の実装記録](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/docs/reference_capture_b887_implementation_20260914.md)
- [Reference製品の不変条件](/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc/docs/hypha_invariants.md)
- [Kirin OS Reference製品契約](/Users/nishiodaisuke/Dev/kirin_os_reference_delivery/docs/reference_product_contract_20260905.md)
