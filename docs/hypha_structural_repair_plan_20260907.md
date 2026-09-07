**Hypha構造修正計画 — B-733レビューとHTML回答を反映**

更新日：2026-09-07。基準：`7746173c7853eabc923b6eb53ad45192155daef2` / B-733。
2026-09-07に本計画のS0〜S4とM0を実装した。
SGのsource内検証は完了した。
全体ゲートは1回実行し、そこで検出したPRE shelfの一時ファイル競合を修正したうえで、影響範囲と未実行分を個別に完走した。
ホスト配置と公開は実施していない。
本書を、既存統合計画のReference修正・RG・AAX混入検査・初回回答取込みについての現行計画とする。
ローカルBlind、軽量化、SPACE / ATTACK本体接続、両OS検証、配布3チャネルの既存範囲は維持する。

**1. 判定をやり直す起点**

[B-731〜733レビュー](/Users/nishiodaisuke/Downloads/Hypha_review_B731_B733_20260907/review.md)で、P1 4件、P2 1件を再現した。
旧レビューのF1〜F5と区別し、今回の指摘はRV1〜RV5と呼ぶ。

| 指摘 | 再現した事実 | 修正する境界 |
| --- | --- | --- |
| RV1 / P1 | lower-A承認後、2回目以降のcallbackがライブAの減衰へ入り、1 / 2切替と回答が進まない | 準備状態、実出力状態、減衰保持を一つのboolで兼用しない |
| RV2 / P1 | UIのsnapshot取得と競合すると、常駐ページでも3回の取得再試行が失敗しBを解除する | ページの観測とPCMの所有権を分ける |
| RV3 / P1 | source自動更新で通常復帰操作なしに0.249988 → 0.5へ戻る | 自動失効、明示復帰、予約解放を別の操作にする |
| RV4 / P1 | 新Preset revisionがoriginalなのに、旧計算の約−6 dBでBを開始する | 準備からRT採用まで同じ世代の再生条件を使う |
| RV5 / P2 | Git追跡済みbuild配下のAAX風headerでも混入検査が成功する | 公開対象の列挙とローカル生成物の除外を分ける |

if条件の追加と再試行回数の増加では、5件を閉じた扱いにしない。
利用者から見た完了条件は、同じ比較条件で1 / 2を聴けること、表示が実際のゲインと一致すること、自動更新で音量が戻らないこと、画面を開いたことで試聴が止まらないこと。

**2. 状態の所有者を固定する**

| 責務 | 正本を変更する主体 | 他の主体へ渡すもの |
| --- | --- | --- |
| Work / Preset / source読取、decode、比較条件の準備 | 単一の非RT制御owner。既存Controller workerへ集約 | 検証済みの不変な再生条件とPCMの参照 |
| UIの選択・開始・中断・通常復帰 | UIは要求のみ発行。ownerが直列に受理・却下 | command ID、要求元の世代、結果 |
| callbackで使う出力経路、聴取済みframes、適用した減衰 | Audio Threadのみ | session ID / command ID / callback sequence付きの実出力receipt |
| GUI表示・History・予約解放 | 非RT ownerがreceiptを消費 | 準備中と実際の選択状態を区別したsnapshot |
| ページのdecode・再利用・解放 | ページworkerのみ | 不変PCMと整合したページidentity |
| 音声ページの読取 | Audio Threadの短命なlease | callback終了時の所有権返却 |
| UIのbuffer準備表示 | 公開済みmetadataの観測のみ | PCMやslot状態を変更しないreadiness情報 |

UIとworkerが同じlifecycleを書き換える構造をやめる。
stateLockは非RT要求の受理・公開に使い、Audio Threadへ持ち込まない。
SPSCを使う場合はworkerへ要求を集めて本当に単一producerにしてから使い、UIとworkerから同じqueueへ直接pushしない。
要求数と保留数は有界にし、満杯時の明示開始は失敗を返す。通常復帰・失効は開始要求に埋もれない専用の通番で渡す。
RTは待機、mutex、I/O、decode、allocation、shared_ptrの最終解放を行わない。

既存の`LocalBlindTrial`のcommand / receiptと`LocalBlindSlot`の非RT回収を参照する。
共有するのは寿命・出力確認の小さな責務に限定し、Referenceのfile契約やローカルBlindのPDC / participant scopeを同一視しない。
汎用フレームワークの導入や、無関係な計測エンジンの再設計を前提にしない。

**3. ページ所有権を観測から切り離す（RV2、旧F1 / F2 / F5）**

ページに対するAPIを、workerのpublish / retire、RTのacquire / release、非所有のobserveへ分ける。
`readyAt()`と`snapshot()`はslotを`inUse`へ変更しない。
UIへはworkerが公開したページ世代・範囲の観測情報を渡し、観測値を実出力の成功通知として使わない。
workerが既存ページの有無を調べる処理も、RTのleaseを取得しない。

PCMの所有を取得するのはRTだけとし、取得後にsource generation、page start、必要範囲を再検証する。
所有前のmetadata確認だけで受理する旧ABA問題へ戻さない。
同callbackで必要な全ページを確保してから出力し、途中失敗では入力Aを一部だけ書き換えない。
current / 前後 / loop端の必要集合を保持してから追出し対象を選ぶ既存方針と、Cue終端で分割する出力を残す。

close / source交換では先に旧公開を撤回し、新たなleaseを止める。
保持中のPCMはretire待ちに置き、最後のcallbackの返却後に非RT側だけで再利用・破棄する。
失効後の旧lease、slot再利用、metadataの読取交差をテスト用の制御点で固定する。
テスト用停止は製品callbackに残さない。

合格条件：観測だけによるページ取得失敗0、B解除0、旧identityの受理0、範囲外PCM出力0、失敗時の部分出力0。
単体の交差試験に加え、Controllerのsnapshotと実描画を並行させて確認する。
3回再試行が偶然成功することを所有モデルの正しさの証明にしない。

**4. source・mode・gain・承認を同じ再生世代に束ねる（RV4）**

比較に影響する変更を受理した時点で、非RT ownerが単調増加するadmission epochを更新する。
対象はconfiguration、Work / Preset revision、source identity、Cue / mapping、比較モード、sample-rate承認、Blind binding / capture世代。
表示名だけの更新まで音声を失効させる必要はないが、その区別は明示的なidentity比較で行う。
外部manifest revisionと内部epochを混同せず、同じmanifestでもconfiguration変更があれば別epochにする。

非RTで作る不変の再生条件は、epoch、source / PCM handle、format、Cue写像、comparison mode、gain、承認根拠、session IDを一緒に保持する。
RTは文字列やhashを評価せず、検証済み条件と整数のidentityだけを参照する。
ゲインを独立した`bLinearGain`へ先に書いてから新snapshotを公開する二段構成を解消する。

開始要求は利用者が選んだepochを保持し、予約取得前後、準備完了時、RTが採用するcallback境界で照合する。
古い開始要求は拒否して、その要求が取得した予約だけを返す。新sourceで黙って再計算し、自動開始しない。
古いprepare完了やack到着が新しい選択を上書きする経路も同じ照合を通す。
`ready`は表示用の派生値にし、開始の権限証明として使わない。

callbackは入口で採用した単一epochの条件を最後まで使う。
新しい失効を観測したcallbackから旧条件の新規出力を止め、1ブロックの途中でsourceやgainを混在させない。
採用条件の参照はcallback終了までpinし、破棄は非RT側へ返す。
GUIのmode / gainとHistoryの比較条件も、準備中の値とRT採用済みの値を区別して同じreceiptから作る。

合格条件：割込み順を固定したsource / mode / approval / reset更新で旧epochの開始0。
original採用時のPCM比は1で、表示0 dBと一致する。旧−6 dBが残った場合は失敗。
予約の二重取得・他sessionの解放・古いreceiptの受理は0。

**5. Blindの状態・保持・通常復帰を一つの遷移表にする（RV1 / RV3）**

準備済みPCMの寿命と、今どの音声を出しているかを分離する。
準備状態は非RT ownerが管理し、出力状態と減衰適用の記録はRTが管理する。
`renderInvalidatedA()`を通常描画より先に無条件評価する構造をやめ、Controllerの一か所で出力stateをdispatchする。

| 出力state | 入力・要求 | callbackでの動作 | 保持・予約 |
| --- | --- | --- | --- |
| Normal | 準備・読込・復元のみ | Aをそのまま通す | lower-Aは適用しない |
| Armed | 有効epochへの明示開始 | 条件が揃った非空callbackから凍結PCMを出す | 承認しただけでは保持実績を作らない |
| Listening / Revealed | 1 / 2切替、Reveal | 選んだ凍結A / Bを固定gainで出力 | 実出力した減衰と聴取framesを記録。保持経路は割り込まない |
| Held | source / binding失効、seek、worker再準備などの自動中断 | 許されたrealtime callbackでは既存の減衰をライブAに適用 | 適用済み保持と試聴予約を継続。未出力なら新たな減衰を作らない |
| ReturnRequested | 利用者の通常復帰 | 次の適格な非空callbackでAを通常出力し、同じrequest IDを確認 | GUI操作時点では解放しない |
| NormalConfirmed | 通常出力receiptを非RT ownerが受領 | Aを通常出力 | そのsessionの予約を解放し、退役PCMを回収 |

自動失効に`selectA()`や`endBlind()`を流用しない。
`invalidate(reason)`、`requestNormalReturn()`、`retirePreparation()`、`releaseReservation(receipt)`を呼出し意図ごとに分ける。
source再公開、sample rate変更、workspace交換、binding削除、lease失効、UI閉鎖、worker再起動の全呼出し元を同じ表へ移す。
保持中の失効は何度来ても保持値とsessionを変えず、準備し直せても自動再開しない。

現行の`invalidateBlind()`とRT側のdeferred処理は、中断時にselectionGateを解放する。
減衰保持だけを修正してこの経路を残さず、予約の寿命も通常出力receiptまで揃える。
実際のgateは`PluginProcessorGuideTransport.cpp`からFFIへ入るため、旧sessionの解放が新しいownerに届かないtoken照合を非RT側に置く。
FFI境界の意味変更が必要なら最小の変更として扱い、Record / pairingの検証ゲートを適用する。

offline / bypassでは従来通りAを変更しない。停止・位置不明を含む出力禁止条件と、論理上の保持状態を区別する。
出力禁止だけを通常復帰完了と誤認せず、再開時に比較を自動開始しない。
通常復帰receiptを発行できる条件をprocessor入口と一致させ、0 frames、無効layout、停止中の無callbackを成功として数えない。
plugin削除はcallback停止が確定したhost teardownとして別処理にし、存在しないcallbackを偽造しない。

合格条件：Controller経由で開始 → 複数callback → 1 / 2 → 回答 → Reveal → 中断 → 保持 → 通常復帰を完走する。
無回答側の回答不可、未出力承認だけの減衰なし、自動更新前後の保持gain一致、通常復帰確認前の予約解放0を検証する。
通常Aのbit identity、0 samples latency、正本PRE / POST測定とRecordの不変性を維持する。

**6. AAX混入検査を公開対象の列挙から作り直す（RV5）**

Gitのindex / 追跡treeのファイルを、build、dist、target等の名前に関係なく全件検査する。
ローカル生成物を省略するwalkは別の入力集合とし、その除外を追跡ファイルへ適用しない。
CIのcheckout、ローカルの追加済みファイル、Git管理情報のない配布用treeで、何を対象にできたかを結果に記録する。
Git列挙の失敗を空集合と扱ってpassしない。
submoduleについては公開gitlinkと、配布で展開・コピーされる範囲を区別し、展開する対象の検査も抜かさない。

一時git repoで、通常path、追跡済みbuild / build-* / dist、ignoreへ強制追加したheader / SDK風archiveを検証する。
既存JUCE wrapperや通常のAAX計画文書は誤検出しない。
検出器の名前パターンだけで未知のSDK内容まで証明できるとはせず、少なくとも既知パターンの追跡漏れを0にする。
既定OFFのCMake、外部SDKパス、ライセンス確認、未配備時skipの既存設計は維持する。

**7. HTML回答の受領と評価への接続**

回答6 / 6を受領し、既存modelの形式検証、pack / source identity、manifest hash、HTML hash、6音声のhashとbytesを確認した。
音声合計135,432,596 bytes。回答原本のSHA-256は`c0eed609550af52a88f7c261186140a72279f0b34284855803c1499fb3800d55`。
[私用の受領・補足記録](/Users/nishiodaisuke/Downloads/Hypha_structural_plan_inputs_20260907/answer_intake.json)に確認結果を保存した。
回答原本、音源、曲名、判定者、自由記述はGitへ追加しない。

| 対象 | 有効な入力 | 直ちに進める評価 | まだ証明できないこと |
| --- | --- | --- | --- |
| SPACE | 補足回答を反映し3件とも「減衰を追える区間なし」。初回の1区間は原本保持・positive評価から除外 | この提示範囲で採用した区間があるか、棄却理由と人の判断がどう対応するかを点検 | 正例のCoverage、現代曲全般の適用範囲、D20検出の最終精度 |
| ATTACK | 3件、9 + 5 + 26 = 40点。候補を見ずに指定した代表点 | 注釈点との一対一対応、時刻差、未対応点、重複候補を開発診断として出す | 網羅注釈に基づくPrecision / Recall / F1、最終合格 |

[私用の派生評価](/Users/nishiodaisuke/Downloads/Hypha_review_evaluation_20260907_v2.json)を生成した。
SPACEは3件とも有効判定がnoneで、対象窓内のD20採用は0件だった。
ATTACKは代表40点のうち37点が50 ms以内で一対一対応し、3点が未対応だった。
各窓の未割当候補は非網羅注釈のためfalse positiveへ数えていない。
したがって、この37 / 40をRecallとして扱わず、最終Precision / Recall / F1も算出しない。

回答は`pilot_representative_marks_not_exhaustive`であり、印のないATTACK候補を直ちにfalse positiveとしない。
自己申告の再生秒数を物理出力確認や注釈精度の証明に使わない。
SPACEの補足は元のpositive判定・確信度を無言で上書きせず、訂正authorityを持つsidecarとして適用する。
訂正後の確信度は未指定なので新しい値を推測しない。

取込みは既存`model.js`に加え、原本hash、素材manifest、訂正sidecar、評価対象範囲、annotation scopeを結ぶ小さな非RTモジュールにする。
native整数sampleを正本にし、ATTACKの点は点のまま扱う。抜粋座標と原曲座標を明示する。
SPACEの二つのカーソル位置を時間境界として、解析用の派生データは[start, end)の半開区間にする。
現UIの包含規則は未明記なので、採用した座標規約IDを派生データへ記録し、原本の整数値は変えない。
今ある無効化済み1区間へ便宜的な+1 sampleを加えてpositiveに復活させない。
重複import、壊れたJSON、異なるpack / hash、範囲外mark、音源欠損を拒否し、部分回答は区別して受け入れる。

**8. SPACEの選曲の偏りを是正する**

初回はSPACE3件中2件がジャズで、固定抜粋の中央10秒を使っていた。
実装に年代を条件とする選択はなく、記録上の狙いは編成の対比だったが、現代の空間処理を確認する素材として十分な構成ではなかった。
「追える減衰なし」を、リバーブやディレイの使用がないことへ読み替えない。
既存のEARLY / D20は観測可能な減衰の指標であり、楽曲全体の空間効果の多寡と同じ尺度にはしない。

使用可能な素材範囲は、2026-09-07のDaisukeの明示指定により`/Volumes/ALOHA`配下と`/Users/nishiodaisuke/Music/Qobuz`配下。
両rootの実在を確認した。ALOHAではAlbums / Songs / Releases / Clients / Live / Studio One、Qobuzではトップレベル278ディレクトリを確認した。
この使用許可は以後のHypha検証にも引き継ぎ、同じ範囲の素材利用を毎回確認し直さない。
まず既存台帳とメタデータを使って候補を絞り、選定した音声だけを読み取る。
原本を保持して私用の抜粋と解析結果を作り、私用素材と回答をGitへ入れない。
同じ曲の別mix / master、ALOHAとQobuzの重複、過去の開発利用を照合し、予約済みの評価曲を新たな開発素材へ混ぜない。

次の開発用SPACEパックは既存HTMLを再利用し、少数の追加素材で以下を各2例、計6例確認する案とする。
現代の制作による曲を中心に、音源metadataと実際の聴取を根拠に選ぶ。年代だけ、疎密だけで正例と決めない。

| 層 | 選びたい箇所 | 調べること |
| --- | --- | --- |
| 効果が聞こえ、減衰も追える | リバーブtail、音数が減った後の残響など | 固定区間計算と自動区間選択を分けて正例を確認 |
| 効果は聞こえるが重なる | 密なミックス、次の音・反復と重なるtailなど | 空間効果を使った曲で欠測になる頻度と、欠測理由が妥当か |
| 追える減衰がない対照 | 持続・短い終端・明瞭なtailのない箇所など | 偽のD20を出さず、数値を出せない理由が成立するか |

[追加SPACE判定用HTML](/Users/nishiodaisuke/Downloads/Hypha_SPACE_Followup_01_20260907/index.html)を6件で作成した。
初回に使った3件と評価予約素材を除外し、開発台帳の未使用素材から各層2件を選んだ。
候補区間や検出器の結果は画面へ出していない。
各30秒を提示し、音声は合計76,896,574 bytes、HTMLは1,911,813 bytesである。
manifest内の正規identityは`32992ac4a48b25c95478644148d0884ce8949adbc946ed169113453345b7693b`である。
層分けは聴取前の仮説であり、人の回答を受けるまで「効果あり」や正例とは確定しない。

提示区間は効果と後続音の文脈を含めて選び、現検出器が採用した場所だけを選ばない。
選定根拠と開発用であることを記録し、評価予約素材は開かない。
聴取前の候補を「残響あり」と確定せず、印の範囲と効果の聴こえ方を別に確認する。
既存3件の回答は負例・難例として保持し、再入力を求めない。

既知の人工減衰を使う独立計算試験も併用するが、現代曲で役立つことの代替にはしない。
遅延反復、再上昇、ノイズ床、fade、gateによる欠測を分ける。
正例が不足している状態で採用閾値を下げず、Coverageと使える条件を開発データで提示する。
空間効果を多用する現代曲で恒常的に観測できないなら、実装成功と製品価値を分けて報告する。
SPACEの意味を拡張する必要が判明した場合は根拠と選択肢を提示し、現在のEARLY / D20契約を黙って別指標へ変更しない。

**9. 変更範囲と実装順序**

| 単位 | 対象ファイル・責務 | 完了して渡すもの |
| --- | --- | --- |
| S0 | レビューのprobe、既存runtime / pages / Blind試験、source_line_budget | 5件と旧F1〜F5の期待値を固定した製品経路fixture。必要な責務だけ先行抽出 |
| S1 | `ReferenceAudioPages.h/.cpp`、`ReferenceAudioPageRender.cpp`、旧ControllerとV2のreadiness呼出し | 非所有観測、RT lease、非RT retirement、cache / Cue回帰 |
| S2 | V2 Controller / Workspace / Lifecycle / Commands / Selection / Realtime / Model / Events、新規の小さなpublication・admission・receiptモジュール | 一貫した再生epochと開始transaction、表示と実出力の一致 |
| S3 | V2 Blind / BlindLifecycle / BlindRealtime / BlindState、processorのAudition / GuideTransport、PluginEditorReference、Reference UI | 単一出力dispatch、保持と明示復帰、receipt後の予約解放、操作一巡 |
| S4 | AAX scanner / test、ci.yml、必要なpublic-source列挙 | 公開対象の検査漏れを閉じたゲート |
| M0 | 既存review model / script、新規import・対応付け評価器、mix_space_probe / space_decay_probeとの接続 | 今回の回答と訂正を使える開発評価、追加SPACE素材の選定台帳 |
| SG | CMakeの対象native試験、source契約、必要なFFI / Record / pairing、sanitizer | 固定した候補で5件と既存回帰の合否を揃えた記録 |

S1 → S2 → S3を同じ設計契約で統合し、分岐ごとの場当たり修正に分割しない。
責務抽出と製品変更はcommitを分けるが、最終成果物は全呼出し元・試験・契約を含む一式とする。
新規owned sourceは500行以下。既存巨大ファイルは変更する責務だけを先行抽出し、減ったbaselineも下げる。
新しいB番号は実装commit時に採番する。
S4とM0は独立して進められる。追加の人による回答待ちでReference修正を止めない。

実装ではS1の観測seqlock、S2のadmission epochと準備済み選択transaction、S3のBlind出力遷移と通常A receipt、S4のGit index列挙、M0の回答評価器と追加SPACE pack生成器を追加した。
通常BとBlindの予約にはowner tokenを付け、古い遅延解放が新しい比較の予約を解除しないようにした。
通常Bのgainと表示値は準備中に製品状態へ書かず、同じepochとselection generationをゲート取得後に再確認してから一緒に公開する。

RGは未完了として扱い、SG合格後に既存B1のhost / PDC実証、B2のローカルBlind接続へ進む。
軽量化の残件とM0はその待ち時間にも進め、SPACE / ATTACKの定義固定と本体接続は各評価の成立条件を通す。
Windows VST3、macOS VST3 / AUの実機検証と配布3チャネルは既存計画の必須工程として残す。

**10. 省エネで取りこぼさない検証**

実装中は変更単位に対応する小さなnative / Node試験のみ実行し、既存buildとfixtureを再利用する。
RV1とRV3はBlind部品の直呼びだけで済ませず、processor / Controller入口から音声と予約のreceiptまで検証する。
RV2とRV4は交差順を固定する試験を主にし、短い並行stressを補助にする。タイミング待ちだけで合格を狙わない。
ASan / TSanは変更したnative境界に限定して実行し、実行できなければ未検証のまま記録する。

| 必須の対象回帰 | 条件 |
| --- | --- |
| ページ | ABA、close / reopen、source交換、cache一巡以上、往復seek、worker遅延、UI観測、Cue跨ぎ / 1 frame / 複数周回、非loopとfile終端 |
| 公開世代 | prepare中と予約中とRT採用直前の更新、true→false→true、古い承認、遅延ack、取消後の準備完了 |
| Blind / 保持 | 未出力承認、0 frames、1 / 2連続切替、回答、Reveal、source / binding交換、UI閉鎖・復元、worker再起動、通常復帰の確認と予約解放 |
| processor境界 | realtime / offline / bypass、停止・復帰、無効位置、format変更、Recordとの排他、通常Aのbit identity |
| 入力評価 | hash / sample座標 / annotation scope、訂正適用の重複防止、欠損・壊れた回答、非網羅注釈を負例にしない |
| AAX | 追跡済み生成物ディレクトリ、ignore強制追加、Git列挙失敗、正常wrapper |

最終候補を固定してから、`scripts/test_release_source.sh`相当の既存全体ゲートを一巡だけ行う。
同scriptに含まれるRust / clippy / ignored FFIと、追加native試験・Node試験の対応表を先に作り、同じsuiteを別コマンドでも重複実行しない。
FFIを変更した場合はparityとpairing_candidatesのignored件数を実測し、両suiteを必須にする。既存scriptを使う場合はその実行を充当する。
試験用Release executableの検証と製品のrelease bundle / installを混同せず、製品配置を行う場合はLS用PKGを含む配布Runbookを適用する。

全体実行が失敗した場合は停止箇所と未実行箇所を記録し、修正後は失敗・影響suiteと未実行分を続ける。
全体コマンドが一度でgreenになっていなければ、その事実も残す。
追加修正に影響する試験は再確認するが、理由のない全件やり直しはしない。

**11. 実装結果**

S0〜S4とM0はB-734〜B-736で実装した。
B-734はReferenceのページ観測、通常Bのadmission、Blindの保持・明示復帰・予約tokenを一つの遷移契約へ揃えた。
B-735はAAX混入検査の入力集合をGit追跡対象へ変更した。
B-736は回答原本・訂正・素材identityを検証する評価器と、追加SPACEパック生成器を接続した。

`scripts/test_release_source.sh`は最終候補に対して1回実行した。
Node / static契約、UI、ATTACK、Referenceまではpassしたが、PREの`retain the same runtime identity when the open PRE instance is prepared again`で停止したため、一度の実行ではgreenになっていない。
原因はatomic writeに使う`juce::TemporaryFile(target)`が一時leaseにも`.json`拡張子を与え、完了済みpresenceを列挙する処理から見分けられなかったことだった。
B-737で同じディレクトリの隠し`.json.tmp`へ変更し、未使用定数による所有sourceのRelease警告も除去した。
修正後のPRE display Release試験と、影響を共有するReference Release試験はpassした。
全体scriptは省エネ方針に従って繰り返さず、停止時点より後ろの未実行suiteを個別に続行した。

最終確認では、`kirin_measure`通常試験、`kirin_hypha_ffi`通常試験、RT handoff、C ABI 3シンボル、xtask 136件がpassした。
Release負荷試験の実測は、可視PRE / POST 1組が10.558%、POST解析2本が10.699%だった。
ignored対象はparity 20件、pairing_candidates 5件と実測で固定し、単一threadで全25件がpassした。
clippyは対象3crateを`-D warnings`でpassした。
review Node試験16件、AAX混入検査試験6件、実リポジトリのAAX走査、public history、source line budget、diff checkもpassした。

外部EBU v05 archiveを別途供給する試験は、素材がないため明示ignoreのままである。
ASan / TSanは今回の省エネ範囲では実行していない。
したがってsource内の修正と必須回帰はgreenだが、sanitizer、実ホスト、AAX実SDKによる外部検証まで完了した意味ではない。
新しいプラグイン配置、署名、外部公開は実施していない。
LSアップ用：skip。HPアップ用：macOS skip / Windows skip。
未処理申し送り：追加SPACE HTMLへの人による回答、ATTACKの網羅注釈と独立した二人目、実ホスト、sanitizer、AAX実SDK、配布条件。
