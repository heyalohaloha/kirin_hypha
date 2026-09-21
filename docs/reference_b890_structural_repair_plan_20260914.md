# B-890とW-3083の構造修正計画

作成日: 2026-09-14。
状態: 計画。製品実装と新しい動作試験は未着手。
親計画: [Hypha比較機能統合計画v5 revision 3](hypha_comparison_integrated_plan_v5_20260914.md)。
対応する付属書: [v5実行検証契約](hypha_comparison_v5_execution_contract_20260914.md)。

利用者が準備したReferenceを繰り返し設定せずに使い、保存後の再起動でも同じ比較条件へ戻れることを目的とする。
前回レビューの7件を、成果物の検証、配信、保存、状態更新の所有者を定め直して解消する。
追加の確認画面や手動Connectを作らず、回復可能な内部障害は対象だけを自動修復する。

## 1. 基点と適用範囲

| 対象 | 作業先 | 固定する製品基点 |
| --- | --- | --- |
| Hypha | `/Users/nishiodaisuke/Dev/kirin_hypha_reference_abc`、`codex/reference-abc-delivery` | B-890 `db2769cc0490f2468a347e1ea3cc3e76949e3ded` |
| Kirin OS | `/Users/nishiodaisuke/Dev/kirin_os_reference_delivery`、`codex/reference-whole-song` | W-3083 `163bd9656ae89d69fe5b3b790c560de01366f1c6` |

作成時に両作業先はcleanで、取得済みoriginと一致していた。
元の`kirin_hypha/main`には既存の音源変更と未追跡資料があり、本修正の作業先にしない。
実装開始時は両HEADと差分を再確認し、移動していれば対応する試験範囲を更新する。

本書は下記7件と、同じ処理が持つ容量、失敗、取消、再起動の境界を扱う。
v5の機能構成、音量基準、位置照合、保存済みID、解析2枠、Blind排他、完成状態は維持する。
利用者の互換性不要の指示に従い、旧readerとの互換試験や新しいfallback層は今回の受入条件から外す。
現在の音源、利用者が保存した原本、Capture、メモを削除して移行することは認めない。
再生成できる内部キャッシュは新形式へ更新し、namespaceの分離は障害を他機能へ広げないために維持する。

## 2. レビュー結果と設計上の原因

| ID | 確認した問題 | 確認水準 | 対応する責務 |
| --- | --- | --- | --- |
| F1 | 同一manifestでは成果物確認前にreturnし、欠損したBalanceを復元しない | 前回、削除後の再発行で復元されないことを再現 | 配信側の整合確認と受信側の再試行 |
| F2 | 旧観測キャッシュにBalanceがなくても有効として扱う | 前回、analyzerが再実行されずnullが続くことを再現 | 機能別キャッシュと解析能力の識別 |
| F3 | head確定とindex更新の間の失敗で、原本と検索結果が食い違う | 書込順と再試行条件をコード確認。障害注入は未実施 | 回復可能なcommitと派生index |
| F4 | workerによる`activeWorkflow`更新とlockなし読取りが重なる | 呼出経路をコード確認。TSanは未実施 | workflowの単一更新者と通知の寿命 |
| F5 | 巨大な有限frame時刻を範囲確認前に整数化する | 数値変換と後続演算をコード確認 | 整数sampleを基準とした境界検証 |
| F6 | Hyphaのsource Balance readerが一部の測定契約を検証しない | OSのnormalizerとC++ readerを対照 | 完全検証済みデータだけを集計へ渡す境界 |
| F7 | OSの読み上げ名と状態文にSpectral Balanceが残る | JSXと英日localeを確認 | 利用者向け名称の統一 |

F5は、範囲外の`llround`で有効なsample位置を保証できず、その後の整数演算にも問題を持ち込む指摘である。
クラッシュの実証や、すべての入力で未定義動作になるという断定はしない。
F3についても、正常系テストのpassをプロセスクラッシュからの回復証拠にしない。

隣接する境界として、観測キャッシュの3 MiB上限とBalance成果物の16 MiB上限が一致していない。
現行はBalanceを観測JSONへ埋め込み、3 MiB超過を任意キャッシュ書込の例外処理より前でthrowする。
F2の修正ではこの容量問題も同時に解消し、Balanceの欠損や大きさで既存の音量測定と位置照合を失わせない。

## 3. データの正本と再生成可能なもの

| データ | 正本とするもの | 修復の扱い |
| --- | --- | --- |
| 元音源とsource identity | 元ファイル、file/PCM hash、audio facts | 別ファイルで代用しない。元ファイルへの書込なし |
| 観測キャッシュ | 正本ではない。解析能力と入力identityに結び付く派生物 | 必要な機能だけ再計算する |
| Balance配信 | 検証済みの元観測から生成した内容hash付き成果物 | 同じ内容を再生成できる場合だけ修復する |
| 聴きどころ、review、しおり、event | 検証済み不変artifactと、それを指すcommit済みhead | 原本の破損を検索indexから推測して復元しない |
| workflow検索index | headから生成する派生物 | headから再構築する。index更新失敗で原本commitを取り消さない |
| Hyphaの現在状態 | processorに属するworkflow制御責務 | UIやworkerが独立した成功状態を持たない |

両repositoryで共有するのは文書化した形式と公開可能な人工fixtureだけとする。
GPL側へKirin OSの実装コードをコピーしない。
汎用の巨大な保存基盤を新設せず、Balance配信とworkflow保存は別の処理、別の待ち行列とする。

## 4. Balanceのキャッシュと配信

### 4.1 機能単位の準備状態

観測キャッシュを新形式へ更新し、keyに入力identity、file revision、audio facts、計測方式と解析能力の版を含める。
元観測の小さなmanifestと、Balanceの大きな成果物を分け、キャッシュmanifestにはreceiptだけを置く。
3 MiBの既存小型キャッシュ上限を16 MiBへ単純拡大する方法は採らない。

Balanceの準備状態は、未解析、処理中、検証済み、対象外、一時失敗を内部で区別する。
元PCMが対応範囲内でも、無音や短い音源で有効cellがない場合は、正しい欠測として保持する。
未解析を検証済みにせず、対象外を毎回再解析しない。
一時失敗を有効キャッシュに固定せず、source変更、能力版変更、再接続、明示再試行で再評価できるようにする。

旧キャッシュは検証できる既存観測を利用しつつ、Balanceが必要になったsourceを一度だけ補う。
すでにKirin OSが持つ検証済みBalanceはreceiptから再利用し、全Libraryの一括再解析を移行条件にしない。
同じsourceへの同時要求は一つに集約し、取消後や入力変更後の遅い完了を現在sourceへ公開しない。
キャッシュ保存失敗でも、既に検証済みのmeasurementとalignmentを破棄しない。

### 4.2 配信の確定手順

公開は`検証済み入力 → 全参照成果物の確認または修復 → manifest確定`の順にする。
内容の同一判定と、ディスク上の配信が利用可能かという判定を別に持つ。
起動時とsource初回利用時に確認し、その後は変更通知と有界な整合確認を既存serviceから処理する。
同じ内容で全成果物が正常なら、manifestの書換えとrevision増加は0回とする。

欠損は同じhashの内容で再作成する。
破損は所有する派生成果物に限定して隔離し、検証した一時ファイルから置換する。
同名の別内容を新しい正本として採用せず、元音源やworkflow原本にこの修復手順を適用しない。
rootから末端までのsymlink、Windowsのreparse point、path逸脱、同時置換を確認する。

修復開始前に小さな回復記録を永続化し、修復後のmanifest revision確定まで保持する。
修復後、manifest更新前に終了しても、次回は記録を読んで公開を完了する。
同じ修復操作の再実行でrevisionを増やし続けず、manifestが参照する全成果物の検証後に一度だけ進める。
公開直前にsource、設定、権限、要求世代を再照合し、修復中に選択が変わっていれば古い内容を現在の配信へ上書きしない。
manifest自体が破損した場合に備え、最後の検証済みmanifestを一つ保持する。
それも失われた場合は利用不能を明示し、検証済み入力から新しいpublicationとして再構築する。古いreceiptを現在の配信と同一視しない。

### 4.3 Hypha側の再読込

LIVE表示とCapture表示に、同じ成果物検証と失敗分類を使う。
cache keyはroot/namespace、publication hash、source identity、方式版、範囲を区別する。
一度の読込失敗を同じpublicationの永久失敗として記憶しない。
欠損や一時I/O失敗は、表示中に限り1秒、2秒、4秒と待ち、以降15秒を上限として再評価する。
形式不正の同じbyte列を無限にdecodeせず、publicationまたはfile revisionが変わるまで再decodeしない。
再接続と明示再試行は待ち時間を解除するが、同じ対象のjobを増殖させない。

現在表示している欠損を全Libraryの巡回順まで待たせないため、Hyphaは検証済みreceiptを使う小さな自動修復要求を`tonal-v1`内へ出す。
要求はPOSTごとに現在対象一つ、4 KiB以内とし、sourceとpublicationが変われば置き換える。
OSは現manifestの参照対象であることを検証して優先処理し、任意pathの書込要求として受け入れない。
同じ成果物への要求は集約する。
要求の期限は60秒、OSの受付待ちは32成果物までとし、表示中の未解決要求は同じIDで更新する。
期限切れを成功として扱わず、応答は通常のmanifest更新で通知する。
接続済み利用者の追加操作は0とし、要求処理も既存の非RT serviceで行う。

修復後は現在の選択へ戻り、比較音は自動再生しない。
A観測、Version B、Genre、選択中Cを個別に扱い、CのBalanceだけの失敗で他方を未準備にしない。

## 5. Balanceの検証と範囲集計

読込を`byte数とhashとpathの確認 → 厳密なwire形式検証 → 検証済みの読取専用データ → 範囲集計`へ分ける。
範囲集計に生のJSONや未確認のframe時刻を渡さない。
OSのnative入力normalizerと、配信用wire形式のvalidatorも区別し、入力のcamelCase変換をwire検証の緩和に使わない。

検証する項目は、未知fieldと必須field、型、版、source binding、duration、全60帯域の境界、三planeの割当、窓、hop、FFTサイズ、bin幅、単位、式、silence gate、floor、payload長、valid数、bitmap余りbit、無効cellのzero byteである。
sourceとGenreは異なる形式として検証し、一方の形式を他方へ読み替えない。
Source成果物の正常fixtureと異常fixtureを追加し、Genreだけの試験で完了にしない。

時間は整数sampleを基準とする。
rate、sample count、FFTサイズ、hopからframe数と中心sampleをオーバーフロー検査付きで求める。
JSONの秒値は、有限性、duration内、順序、期待する中心sampleとの許容誤差を照合するためだけに使う。
任意の秒値を先に`llround`しない。
窓が完全に含まれる区間だけを集計し、秒/sample変換、sample rate変換、境界frameの扱いをOSとHyphaの共通fixtureで照合する。
F5とF6は同じvalidatorへの置換で解消する。

hashとschemaの全検証は新しい成果物の初回読込時に行い、同じ検証済みデータのCue変更では再度JSON展開しない。
範囲選択は既存の実行1件、待機1件の枠で最後の待機範囲へ集約する。
payloadの宣言値を検証してから確保し、JSON、base64、decoded buffer、集計scratchの同時保持を予算へ算入する。
有効な最大fixtureが既存RAM予算を超える場合は、逐次decodeなどの実装方式を修正し、精度低下や予算の引上げで通さない。

## 6. workflow保存の回復

### 6.1 正本のcommitとindex更新

OSの原本writerをrootごとに一つとし、IPC、原本保存、outboxのindex反映、回復、再構築の同種更新を同じwriterへ接続する。
Hyphaが所有するevent headをOSが勝手に進めず、OSは検証済みeventの検索投影だけを更新する。
別rootの保存とBalance配信を、このwriterで待たせない。

一つの保存操作へ、受理時から不変のoperation ID、本文hash、期待する親head hashを付ける。
保存先rootも受理時に固定し、待機中のWorkspace変更で別rootへ書き込まない。
同じoperation IDと同じ本文の再送は同じ結果を返す。
IDが同じで本文が違う場合は拒否する。
新しい操作で親headが変わっていた場合は競合とし、最新原本を上書きしない。
重複操作の判定を親head競合の判定より先に行う。

保存順は`原本検証 → pending操作記録 → 不変artifact → head確定と読戻し → index投影 → 操作完了記録`とする。
head確定を原本保存の確定点とし、以降のindex失敗を「保存されなかった」と返さない。
保存済みだが一覧更新待ちの状態を短く表示し、同じ原本をもう一度保存させない。
確認できない終了は結果不明として同じoperation IDで照会し、新しいIDを作らない。

### 6.2 再起動時の扱い

| 再起動時の状態 | 回復動作 |
| --- | --- |
| pending記録前に終了 | 保存済みとは表示しない。原本headはそのまま |
| pendingあり、headは期待した親のまま | 記録済みの同じ操作を再開する。別の入力を混ぜない |
| headがこの操作の成果物を指す | artifactを検証し、index反映と完了記録だけ再開する |
| headが別の新しい操作へ進んでいる | 巻き戻さない。保存履歴で同一操作のcommitを照合し、現在headからindexを作る |
| index欠損または破損 | 検証済みheadを分割走査し、別ファイルへ再構築してから切り替える |
| headまたは原本が破損 | 推測で修復しない。対象だけを利用不能とし、他の保存物の回復を続ける |

索引の再構築中も直前の検証済み索引を利用できるようにする。
利用可能な索引が一つもなければ「0件」と断定せず、検索準備中として扱う。
再構築中の新規保存は有界な変更記録へ残して、新索引公開前に反映する。
一検索ごとの全head走査は行わない。
未処理操作記録は自動削除せず、完了記録の整理も再送判定と既存参照を保持した範囲に限定する。
process crashからの回復を必須とし、電源断保証はv5の実filesystem検証を満たした場合だけ記録する。

## 7. Hyphaのworkflow状態の所有

workflow定義、現在項目、通常選択、戻り先、pending transitionを一つの制御責務へ抽出する。
更新者はprocessorに属する非RTの制御処理一つとし、Editorの存在に依存させない。
実装方針はJUCE message thread上のprocessor所有の非同期通知とし、既存timerへ結合する場合も停止条件で保存完了処理を落とさない。
新しい常駐threadや、音声callbackからの同期呼出しを追加しない。

workerからは不変のcommit receiptを有界mailboxへ渡し、worker callbackがControllerを直接変更しない。
mailboxは既存の32 operation予算に含め、message threadが遅れてもcommit済みreceiptを捨てない。
未反映receiptの容量を先に予約し、満杯なら新しい保存の受理に制限をかける。元の入力と即時A復帰は保持する。
制御処理はreceiptのnamespace、runtime、restore generation、attempt、operation ID、定義hashを照合してから遷移する。
旧receiptは保存履歴として保持できても、現在の項目移動、選択、Blind開始許可へ流用しない。
UIとDAW保存は公開済みsnapshotを読み、`activeWorkflow`を外部から直接読まない。
snapshotが更新される前のrestoreでも、既存のpending保存要素を欠落させない。

通常のPreset、Check、Candidate、Cue、Version変更、workflow開始と終了、Blind開始を同じ制御責務の判断へ通す。
受理と完了を区別し、保存完了後の遷移と新しい選択を一つの順序で処理する。
比較出力をAへ戻す操作と失効通知は待ち行列の完了を待たせず、既存の出力許可を即座に取り消す。
両BlindのGain基準と復帰契約は個別に維持する。

終了時は新規受付を止め、世代を失効させ、callbackの参照先を切り離してから所有物を退役させる。
生の`this`を保持した遅延callbackを残さず、weak参照先を確認した直後の破棄も含めて寿命を保証する。
Editor close、processor再生成、同じprocessorのrestore、wrapper unloadを別々に試験する。
Audio ThreadやUIで待機joinを追加せず、既存の終了経路についても実行中callbackが残らないことを確認する。

## 8. 名称と利用者の操作

HyphaとKirin OSの利用者向け名称は英日ともに`Balance`へ統一する。
タイトル、選択肢、tooltip、読み上げ名、未準備表示、エラー文を同時に棚卸しする。
内部型名、保存済みID、測定方式名の一括renameは不要とし、利用者が付けた日本語名を翻訳しない。
Hypha固有UIは英語、OSは英日という既存方針を維持する。

内部修復に成功した場合は操作を増やさない。
利用者が求めた表示や保存がまだ成立しない場合だけ、対象と次の操作を短く出す。
キャッシュ、hash、transactionなどの内部用語を主画面へ追加しない。
通常A/B/Cは全サイズ、Blind開始は300%、画像CaptureはMenu内、NOWとSESSIONは補助情報という決定を維持する。

## 9. 負荷の制約

| 対象 | 設計上限または検証条件 |
| --- | --- |
| Audio Thread | 追加alloc、lock、I/O、PCM queue、音声コピーは0。通常Aはbit identical、追加latency 0 |
| 解析 | 既存物理2枠以内。Balanceはownerごと実行1、待機1。停止待ちも実行数へ含める |
| workflow | 新規常駐workerは0。writer待機は既存32 operation以内。保存済みメモをlast-winsで捨てない |
| 無変更時 | Balance再解析0、全payload再decode0、manifest更新0、全index再構築0 |
| 整合確認 | 既存serviceで分割実行。1 tickの開始上限16 receipt、hash読取りは256 KiB単位で制御を返す。全件一括hashを毎秒行わない |
| 再読込 | 同じsourceとpublicationの重複jobなし。失敗再試行は表示対象だけ。retry timerをPOST数分の常駐threadにしない |
| UI公開 | v5の10 Hz以下。静的pathは内容か寸法の変更時だけ再構築 |
| メモリ | Balanceは現ownerと退役owner合計32 MiB/POST、workflowは8 MiB/POST、OS indexは32 MiB以内。新しいcacheとmailboxも内数 |
| 検索 | v5の10,000件fixture、warm p95 100 ms、cold p95 500 ms、再構築30秒以内。検索ごとの原本全走査0 |

statの一致はbyte列の正当性を保証しないため、初回のhash検証を省略する根拠にしない。
通常の変更検知にはfile identityとsize、mtime、ctimeを使い、通知取りこぼし用の分割整合確認も行う。
重いhash検証とdecodeは非RTの低優先度処理とし、メイン画面の操作や元観測の入力消費を待たせない。
上限16件と256 KiBは今回の設計上限で、達成済みの性能値ではない。
処理途中のbufferを含め、1、2、8 POSTでCPU、callback p95/p99/最大、peak RSS、file open、読取りbytes、job数を基準版と比較する。
新しい検証のために既存の性能基準を引き上げず、超えた場合は処理単位と保持方法を修正する。

## 10. 実装範囲と検証の対応

以下の既存ファイルから同種の入口まで追い、変更責務ごとに500行以下のモジュールへ分離する。
新モジュール名は実装時の既存命名に揃えるが、ここで定めた所有者と入出力境界は変えない。

| 工程 | 既存の主要変更先 | 必須の対象検証 |
| --- | --- | --- |
| S1 検証境界 | OS `spectralBalanceContract.mjs`、Hypha `ReferenceTonalRepository.*`、`ReferenceCaptureTonalStore.*` | F5/F6。source正常、Genre正常、未知field、型違い、非有限/巨大値、frame順序/窓/数、長さ上限、bitmap、source/range不一致 |
| S2 キャッシュと配信 | OS `referenceWorkspaceSourcePreparation.mjs`、`referenceTonalDelivery.mjs`、`src/port/reference/libraryService.mjs`、Hypha `ReferenceVisualObservation.*`、`ReferenceACaptureProjection.*` | F1/F2。旧cache、3 MiB超Balance、欠損/同サイズ破損、repair途中終了、manifest破損、再接続、同条件再表示、権限喪失、入力変更、正常Bの継続 |
| S3 原本保存 | OS `referenceWorkflowRepository.mjs`、`src/port/reference/workflowIpc.cjs`、`libraryService.mjs`、workflow application/IPC呼出元、Hypha `ReferenceWorkflowRepository.*`、`ReferenceWorkflowStorage.*` | F3。各書込境界で子process強制終了、再起動、結果不明再送、同ID別本文、別操作競合、index欠損/再構築中保存、outbox重複、disk full |
| S4 制御責務 | `ReferenceComparisonController.*`、`ReferenceComparisonWorkflow.cpp`、`ReferenceRuntimeV2Controller.*`、`ReferenceRuntimeV2Commands.cpp`、processorのReference/state/通知入口 | F4。receipt順序逆転、二重完了、選択/Blind/restore/終了との同時実行、Editor不在、保存直後の再保存、破棄、TSan/ASan対象試験 |
| S5 表示 | OS `SpectralBalanceMap.jsx`、`src/locales/en.json`、`src/locales/ja.json`、関連表示/操作状態、Hypha Reference UI | F7。英日名称、読み上げ、明示操作失敗の出口、全5サイズ、Blind秘匿、TP長値、追加確認0 |
| S6 統合 | 上記の実装済み経路とv5のCS/RB対象 | OS解析からHypha表示、しおり保存からOS検索、再起動から再開、両Blind排他、通常A、共存負荷 |

OS側のservice名だけの行は`src/services/`配下、Hypha側は`juce_shell/src/reference_audition/`配下を指す。
S1では同形式を読むCapture保存側の境界も確認するが、別形式を無理に共通codecへ統合しない。
S1とS2の正常/異常fixtureはOS producerの出力をHypha consumerが実読して判定し、同じ誤りを写した二つの単体fixtureだけで済ませない。

## 11. 実装順と完了条件

1. 両基点、現行試験コマンド、入力fixture、メモリ予算、全呼出元を固定し、F1からF7と隣接境界を対象試験へ割り当てる。
2. S1の検証済み型とS4のworkflow所有境界を先に抽出し、旧呼出元を一括で付け替える。
3. S2のキャッシュと配信を、producerとconsumerを含む一つの利用経路として完成させる。
4. S3のcommitと回復を、原本保存、検索、outbox、IPC応答まで通す。
5. S5の文言と状態を実装済みの結果に結び、S6の障害注入、競合、負荷、実寸検証を通す。
6. 対象試験が安定した後、修正後sourceの全体baselineを一回だけ実行する。途中失敗は対象だけ直して再実行し、成功済み全体を繰り返さない。
7. 対応commit、環境、artifact hash、未実施を記録し、v5の対象host実機と利用試験へ渡す。

上記は内部の実装順であり、途中の一件修正を製品完成として配布する計画ではない。
Rust変更があれば該当suiteとclippy、FFI変更があれば件数を実測したignored parity/pairing suiteを含める。
Rustを変更しない場合も、その時点のAGENTSが要求する検証は上記一回のbaselineへ集約する。
TSanが実行環境上動かない場合はrace解消を実証済みとせず、障害内容と代替の順序制御試験を別に記録する。
Windows検証前は共通Runbookを読み、署名やCode Integrityの既存制限を弱めない。

構造修正の完了にはF1からF7、3 MiB境界、回復途中の中断、古いreceipt、負荷制約の全対象試験と内部監査を要求する。
実機受入、初見利用試験、Windows unloadなどの既存未完了を、今回の単体試験で解消済みにしない。
v5の`engineering_complete`、`ux_accepted`、`integration_complete`はそれぞれの条件で判定する。
この計画作成では製品コード、テスト信号、ユーザーデータ、DAW、配布物を変更せず、Notionへ書き込まない。
