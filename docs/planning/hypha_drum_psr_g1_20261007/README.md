# DRUM・PSR G1 — 観測と整合snapshot

G1はmain `7b0c301c`で開始し、保存先隔離を含むmain `f5424878`へ統合して、1.1.51へ含める観測・snapshot基盤を実装する。[改善計画](../hypha_drum_psr_usability_improvement_plan_20261007.md)のG1受入を対象とする。G2の全面表示統合、G3の実DAW、公開前の本人の日常操作・品位確認は別の工程であり、基盤試験の成功で代用しない。Claudeが途中確認、CI、merge、releaseを担当する。

## 最新の受入判断（2026年10月8日）

[CI 37693270001](https://github.com/heyalohaloha/kirin_hypha/actions/runs/37693270001)の対象は`8b86da6fad3236d36efb7d03271caf0c5dba4900`。macOSのrelease source contract、public history、AU検証はPASSした。native89件、UPDATE実動7件、core1705件、FFI185件、Record／pairingのignored20＋6件を含み、capstoneとRecord spoolの2秒期限もPASS。CIログを読取確認し、利用者・Claudeの2026年10月8日の判定により、手元のcapstone／spoolのFAILは機械の負荷・共有Cargo targetによる揺れであり、G1の後戻りではないと記録する。ローカルFAILの実測とlogは履歴として保持する。

12組Dropと同時bindは既存main fixtureの時刻順序の前提であり、修正する場合は別PRに分ける。このPRでは製品の受理条件やfixtureの期待値を緩めない。Windowsの`kirin_live_blind_loop_product`は0.8秒の機械停止時に設計どおりLOOPを扱った結果、`kirin_live_lifecycle_restore-invalid`はtimeoutで、ともにG1の変更範囲外という途中確認を記録する。Windowsの残りのskipをPASSとはしない。

PR #88のmerge済main `6036eae6`を取り込み、`kirin_snapshot_abi_contract`と`kirin_live_blind_loop_stall_product`を両方含む一覧を90件にした。CMakeの実登録も90件で、通常52件と一度だけの再実行を許すlive製品38件に欠落・重複なく分かれることを確認した。試験実行はこの一覧確認に含めない。mainの再実行設定と、Windowsの対象stepも保持した。G1のRust・C header・JUCE表示sourceはCI対象から変更していないことを照合した。main統合後commitの必須4checkはClaudeが改めて確認し、旧runを更新後commitの全件PASSへ流用しない。

G1の観測基盤の開発受入はレビュー可能と判断し、Draftを外して提出する。mergeと正式候補の受入・公開は別の判断である。G2の表示・Capture・動きと品位、G3の実DAW、本人確認は未完了。[CI判断の証跡](ci_review_receipt.json)と、以下のローカル履歴を分けて読む。

## 決定と実装境界

Captureは利用者の2026年10月7日の回答によりv1を維持する。測定の意味を保存できないWork添付は理由を明示して失敗とし、ローカルPNGを残す。Kirin OS側の別repo変更とv2は今回含めない。G2で通常添付、不対応、timeoutと再試行、通知を検証する。

ODF、onset、非連鎖30 ms compound、帯域分割、ATT／REL／LEVELとALLの測定定義は変更しない。新しいsnapshot型と独立したsized ABIを追加し、旧ABIのlayoutとrevisionを保持する。通常音声経路、正本のPRE／POST計測、Recordを変更しない。

## 構造

- TIMEは各100 msの元のraw値と完了時刻を保存する。原POST完了からの残り期限を返し、IOや再joinで期限を延ばさない。mainとPSRのtarget、cutoff、proof、履歴停止を一つのpacketに分けて載せる。
- 停止またはseekでclockのrunが変わった時点で、旧runのcurrentを退役する。再開後の入力が100 ms未満でも旧値へ戻らず、同sourceの履歴は保持する。新しい完全slotができるまではcurrentを待機にする。
- 旧10点／100点の履歴bucketを維持し、新TIMEのmixed／partial bucketだけを保持exactから復元する。保持できない境界は欠線とし、未来のsuffixを平均へ入れない。exactの保持範囲から復元不能と分かるbucketは走査を省略する。11025 Hzでは元engineと同じ1103 samplesの幅を使い、連続点と実際の欠測を区別する。
- 新TIMEの縮約は65535件までの件数を正確に保持する。表現できないbucketはFFIから不対応を返し、packetと両配列を変更しない。2時間・1 Hzの72000件は1点では不対応、2点では36000件ずつとする。中間縮約と再縮約の両方で検査し、飽和した件数を次の平均の分母へ持ち込まない。件数上限の拒否は新TIMEだけに適用し、旧ABIと旧縮約の件数飽和の動作を維持する。
- BAND Summary V2は最大8打を先に固定し、欠測やpendingを分母から外して過去の打を補充しない。型付き端点による全件中央値と、確定部分の中央値・件数・ageを区別する。ALLの集計はこの変更の対象ではない。
- BAND Summary V2の平均図用データは実測maskと参加集合を保持し、PRE／POSTの比較点で同じ参加集合を要求する。集合が変わる点は`connect_previous=0`を返す。実plotと塗りの統合・断線はG2で確認する。
- POSTは自分の打音検出がない場合も、完全な対応proofがあるPRE起点の窓を自分のPCMから実測する。過去の比較を新しいpairへ付け替えない。
- Singleの選択要求は一枠に限定し、要求identity、deadline、producerの完了状態で管理する。transport停止とAudioEndを同じ意味にしない。ALLは旧raw detailと固定shape窓を別fieldで保持し、BANDの四指標へ写像しない。同じPREのHEAD先行公開は取得中とし、同source・同eventの完了だけを受け入れる。旧ALL producerが終了を宣言しない途中データはAudioEndと推定せず、完了が届かなければRequestDeadlineへ退役する。受理時刻・token・keyを取り直して期限を延ばさない。
- 一時的に比較値を消した後も、event ledgerのsource組は値の有無と独立して照合する。同じsourceの表示復帰ではidentityを保ち、PRE／POSTのどちらかが新しいsourceへ変わると旧eventとaliasを退役する。
- 不正PCM・workerのpanic・無効化後の復帰では、workerが処理中blockの残sample数を保持して同期を回復する。未公開blockのPCMを先に捨てず、次のblockを元のclockで読み取る。Audio Threadのproducerと通常音声経路は変更しない。
- CとRustの契約は実ライブラリにリンクしたnative試験で照合する。短いbuffer、未知version、NULL、不一致では出力を変更しない。

## 接続側が守る契約

新しいTIMEとDRUMのtargetは共通の0=POST／1=Δ／2=PREへ統一する。旧APIのenumは変更せず、G2のadapterで明示的に写像する。TIMEの残り期限はcallerのpoll開始時刻へ付け、poll終了時刻へ足して寿命を伸ばさない。compactのPSR capacity 0は専用current／履歴の取得を省略する。

TIMEのcurrent配列はM／S／TP／PSR／PLR／CORR、historyのrangeとvalid_count配列はM／S／TP／CORR／PSRの順である。G2では名前に対応させ、同じindexと推定してCORRをPSRへ表示しない。PLRは全sessionの値で、historyへ複製しない。

mainの保存先隔離を統合し、`KIRIN_HYPHA_TEST_STORAGE_ROOT` は実装済みとなった。Cargoとnativeの試験ではcoreの通常解決先を専用fixture配下へ向け、個別sandboxの環境復元も検証する。nativeの完了時刻probeは引き続きidentityとIO roleを未割当にしてstorageを使用しない。環境変数名の指定だけで隔離を認定しない。

## 検証記録

基盤fixtureは以下の独立期待値に対応する。G0の模型・旧libの観測をG1製品PASSへ流用しない。数値処理の合格を、実描画・使いやすさ・実hostの合格へ読み替えない。

400 msは期限算術と元の完了clockを検証する候補値である。native probeはIO roleを割り当てないローカル計測を使う。通常IOと二枠負荷で不要な空欄が出ないかの校正、1000 ms応答期限、DRUMの提示周期と正常jitterの校正はG2に残り、G3 freeze前に最終値を固定する。

| 計画ID | G1の期待値とfixture |
| --- | --- |
| D1／D2／D3／D4／D5 | 末尾ID3～10、五分類合計8、N/A／NextHitを補充しない。20／20／N/Aの全体なし、10／10／≥20の全体10、−10一件／≥200七件の全体≥200。`attack_summary_v2_tests.rs` |
| D6／D8 | 逆片側を含むSilentのLEVEL限界、両Silent不成立、到達差の打毎中央値1 ms、RELの持続時間差5 ms。`attack_snapshot_classify_tests.rs` |
| D7／D10 | 比較点は共通参加者だけ、未観測を床へ入れない。集合11→01→10は同countでも接続不可。`attack_summary_v2_tests.rs` |
| D9／D14 | 完全proofと旧wire・意味不一致の拒否、PRE起点／POST検出0の実PCM窓実測、logical300／actual150。producerとcodec／exchange fixture |
| D11／D12 | open／closed端点、順列不変、偶数中点、無限端点、invalid区間、subnormal／最大値。`snapshot_interval_tests.rs`。表示丸めはG2 |
| D13／D15 | 新cohortのpendingを旧値で埋めない。4→5→5.5、step中央値10／10／10／25／40、lane別age100／3000 ms。`attack_summary_v2_tests.rs`。提示遅延と実表示はG2 |
| P1／P2 | 100 msの原raw64上限、6値とNone、299／399／400 ms期限、IO／poll／再joinによる期限延長0。TIME core／FFI fixtureとnative完了probe |
| P3 | Reset／pair／project／workerの競合、owner／claim／source、Busyと不正ABIで両配列とpacketの全byte不変。`time_snapshot_ffi_tests.rs`とnative ABI |
| P4 | None／gap断線、prefixの手計算mean3.5／max5へ未来100を含めない、valid分母30、新segmentと1200上限。TIME history fixture |
| P6 | mainのCと比較PSRのEを分離し、C−E400 msで有限値を退役。seek後に消費済・未消費の旧PREを使わず、新sourceのcounterへ旧floorを持ち越さない。TIME pair／FFI fixture |
| P1／P6の部分入力境界 | 停止再開とseekの各10 samples入力で、旧cutoffを新しいcurrentと誤認しない。finite mask 0、全値NaN、待機、同source履歴保持。実FFIの回帰を先に実行してfinite mask 15のFAILを確認し、修正後は両条件PASS。`time_snapshot_ffi_tests.rs` |
| bounded Single | fake clockのdeadline／応答不能／終端不変、実workerのBAND測定窓への次打混入防止、accepted token・key・logical end保持、worker停止／再起動／invalid PCM／clock変化の退役。stale Noneからの新取得、同じ物理窓の再利用、ALLのHEAD先行→同PREの完了と別source／期限後拒否。Single fixture |
| worker同期 | 不正PCMを含むblockの後に、新sourceの正常入力だけを与えた基準と同じ打音位置19712 samplesを検出する。途中panic・公開前PCM・短い入力の純SPSC fixtureで、次blockの位置のずれと誤消費を0にする。`attack_observation_worker_tests.rs`／`attack_runtime_worker_ingress_tests.rs` |

新queryは248 bytesで、size／align、38個のoffset、enum revision、capacityを照合する。Summary 13528 bytes、Single4560 bytes、TIME536 bytesをC／Rustの独立literalで固定する。旧query112 bytes／revision7は保持する。全新版APIは共通statusを使い、未知版・短いbuffer・NULL・misalignment・競合・取消済tokenを区別する。

ローカルnative全89件の初回は87件PASS・2件FAILだった。描画のwall計時が5サイズ中4サイズで既存上限を超え、TIME probeは供給末尾slotの完了前に固定待ち時間で判定していた。初回の失敗を保存し、描画上限を変更していない。描画の試験sourceとpainterは起点mainとbyte同一で、この計時経路はRustを呼ばない。初回に別sessionのCPU負荷process 40個を確認し、その後0個になった時点で再判定した。このsessionから停止操作は行っておらず、負荷の差だけから失敗原因を確定しない。

main統合前の検証sourceは1658件、SHA-256 `7ea3d7595f5cfabbd89204aeca56a7c54547c0479d06391b7e258252d0ea14cc`。実archiveを再buildし全47targetを再リンクした全体再判定で、native89件・UPDATE実動7件はPASS。100／125／150／200／300%のAbsolute TIME描画は0.699／1.104／1.405／2.108／4.909 ms/frameで、既存上限1.5／2.5／3.5／6／9 ms/frame内だった。使いやすさ・品位・正式候補の合格を示す数値ではない。

TIME probeは250 msの完了期限を保持し、供給末尾192000 samplesのspan／run／cutoff／endpointを確認してから二度目のpollを行う方式へ直した。main統合前sourceの1 kHz mono正弦波のPSRは3.003595673 dB、供給終了から23.211 msで最終slotを確認。原完了ageは0.243→28.526 ms、残り期限は399.757→371.474 msで、再pollによる延長は0。期限後のfinite退役とResetでのsource更新も確認した。[main統合前の検証記録](validation_receipt.json)にarchitecture x86_64、source、archive／binaryのhash・byte数を保存する。数値は実行中のCTest一時logから確認した。完了logはUPDATEのinventory取得で置換されたため、全89件の結果を保持したsource gate logと数値観測を区別する。[以前のnative証跡](native_snapshot_evidence.json)はその時点のsourceと対応する履歴であり、最終sourceの証拠として使わない。通常IOや二枠の校正、実DAWは未実施。

main統合前のworkspace全体は38 suite、2313件PASS・0件FAIL・41件ignored。core1704件とFFI185件を含む。ただし全source gateの最初のRust実行は、Record spoolの2秒完了期限で1件FAIL（1703件PASS）となりexit 101で停止した。単独実行でも同じ期限でFAILした。該当Recordコードは起点mainと同一。別のdebug spool計測では1,572,864 samplesを1629.015 msでcloseし、全件を値違い0で読み出したが、元fixtureのFAILの代用にしない。その後のworkspaceではsource・期限を変えず同fixtureもPASSした。原因・安定性は未確定であり、CI側で確認する。全source scriptをPASSと呼ばず、その後に残る必須componentを個別検証する。

[DRUM sourceとfixture証跡](drum_source_fixture_evidence.json)の38ファイルはmain統合前sourceと差分0。targetedの過去結果と最終gateを区別する。ignoredのRecord／pairingは一覧で20件＋6件を実測し、全26件PASS。optimized一組PRE／POSTはmedian9.904%／worst13.365%、二枠POSTはmedian10.492%／worst15.329%で、既存18%上限内。vendor vtable、owned RustとPRE／POSTのClippyはPASS。debug archiveの14個のC ABI定義も確認した。整形、公開文書・履歴、500行規約、lightweight source contractはPASS。件数overflowによる不対応を実FFIまで発生させる専用fixtureは未実施。coreの独立件数試験、既存FFIの不対応時の出力不変、全Errがwrite前に返る読取確認を分けて扱う。CIの必須4checkはPR作成後にClaudeが確認する。

DRUMのframe budgetは今回の環境で未設定のためskip。表示の性能・使いやすさ・品位をPASSとはしない。G2で採用値と校正手順に沿って実表示を検証する。

## 現在地・日次ログ・申し送り

2026年10月8日06:20 JST。新しいmain起点でG1を開始し、G1の観測基盤とsource別の検証を保存する。巨大FFIファイルのpair controlを先行抽出してB-1318、比較復旧fixtureの分割をB-1319に保存し、その後の変更を独立moduleへ置く。元の作業checkoutにある別計画やReferenceの未commitファイルは混ぜない。

Notionへの書込みは禁止されているため、SECTION:DEV、日次ログ、Notion Handoffは未記録。この文書に作業状態と後続工程を残す。CIの正式確認、必須4check、Record spool期限の安定性、mergeはClaudeへ引き継ぐ。全source scriptの初回FAILとworkspaceでの再実行PASSを区別する。G2の表示、Captureの明示失敗、動きと速度の校正、G3の実DAW、本人確認、三チャネル公開、公開後G4は基盤試験から完了と判断しない。

main統合（2026年10月8日）：保存先隔離のmain変更を保持した。未pushの先行commitはB番号の重複を避けてB-1318／B-1319へ付け直した。上の実行結果はsource hashへ結び付いた統合前の履歴である。統合後sourceと保存先・native ABI・Rust回帰の結果を別の記録へ保存し、旧sourceの成功を新sourceのPASSへ繰り上げない。

## 保存先隔離統合後のローカル履歴（CI判断前）

統合後sourceは1659ファイル、SHA-256 `38605508d4685e18f57f6a8285a49bd37ba833d8508513808e51f12b90982610`。統合前との差は保存先隔離の14ファイルで、既存の成果を新sourceの成功へ繰り上げない。CMakeを再configureし、実release archiveを再buildしてnative ABIを再リンクした。専用fixture rootの設定を確認したnative ABIは1件PASS。最終192000 samplesの完了を供給終了から30.111 msで確認し、PSR3.003595673 dB、原完了age3.694→31.752 ms、残り期限396.306→368.248 msだった。

workspaceの全target実行はexit 101。通常試験2311件PASS・3件FAIL・41件ignored、FFI185件はPASSした。FAILは12組Dropの全件受理、同時bind、Record spoolの2秒close期限。さらに二つのdoctest targetが依存rlibの不在で失敗した。Dropの保存済fixtureを調べると、両試験とも12組中2組のKeep markerがDrop作成時刻より後だった。最後のmarkerとの差は178 ms／100 ms。fixtureはmarker公開前にDrop時刻を設定し、1秒先の時計で順序を仮定している。製品の拒否条件とfixtureの時刻逆転が一致する証拠を保存し、受理条件を緩めない。Record完了期限のFAILは別件で、安定性未確定。

実行後、別checkoutのCargoがこの作業checkoutの`target/debug/.cargo-lock`を保持していることを読取確認した。共有targetへの同時buildが存在した事実と、doctest中のrlib不在を区別する。削除主体や直接原因は未確定。他sessionを停止せず、必須componentの検証はCargoの排他を待って進める。

統合後のsource、nativeのraw logとbinary／archive hash、Rust実行結果、失敗根拠は[統合後の検証記録](integration_validation_receipt.json)へ保存する。CIの必須4checkが揃うまでmerge受入済みと扱わない。全native89件の統合後再実行と、正式候補の実host受入はCI・後続工程で確認する。

統合後のignored一覧は20件＋6件を実測して全件実行した。parityは19件PASS・1件FAILで、`capstone_paired_record_output_and_linkage`が一度の`poll_delta()`をSomeと期待する箇所で失敗した。`poll_delta()`の取得は変更前と同じ`try_lock`で、lockを取得できない場合はNoneを返す。失敗時に競合とpoisonを区別する記録はなく、数値の不一致やRecord出力の不一致と断定しない。pairing candidatesは全6件PASS。統合前sourceの全26件PASSと区別し、この1件をmerge前の確認事項に残す。

capstoneを単独で一度確認すると、packet自体は取得できたが`lufs`がNoneのため別のassertでFAILした。したがって最初のFAILをlock競合だけで説明しない。統合後はΔの利用可能性を含むRecord受入が未完了であり、fixture時刻順序、Record完了期限、Δ欠測を別のmerge前blockerとして残す。試験の待ち時間・期待値・製品の受入条件を変更して通していない。

必須componentをすべて実行し、owned／legacy Clippy、vendor vtable、debug／release archiveのbuild、lightweight source contract、source行数規約、公開text、整形はPASSした。依存を再buildした後のworkspace doctestもPASS。統合後sourceの再照合は1659ファイル・差分0だった。全source script、workspace全体、parity全体のFAILは取り消さない。

現在地：G1の実装と証跡を保存し、Draft PRでClaudeの途中確認・必須CIへ渡す。G1受入は未完了。日次ログ：観測基盤、停止／seekの境界、source別の検証を保存した。Handoff：Claudeは必須4checkとRecord／Δの上記blockerを確認し、問題の解決と同一sourceの受入が揃ってからmergeを判断する。G2はその後の表示・Capture・動きと品位の統合。公開・実DAW・本人確認をこの保存で完了と扱わない。

2026年10月8日の現在地：PR #88のmainを保持して一覧90件と両試験を統合し、G1の開発受入の根拠を確認してレビューへ提出する。日次ログ：CIの成功とローカルFAILの環境分類を保存し、別作業の未commit変更を持ち込まず統合した。Handoff：Claudeは更新後commitの必須4checkを確認してmergeを判断する。Drop fixtureの修正は別PR。NotionのSECTION:DEV・日次ログ・Handoffは書込み禁止のため未記録。この文書に現在地と申し送りを残す。
