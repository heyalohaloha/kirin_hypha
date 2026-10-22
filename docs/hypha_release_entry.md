# Hypha: ビルドからHPアップまでの統合入口

全工程の入口は **`node scripts/build_hypha.mjs --release`**。
公開用は、未署名診断buildとは異なる正規producerを使う。
Mac PRE/POST × AU/VST3/AAX Universal、Windows PRE/POST × VST3/AAX x64を一つの候補へ結び、
署名・公証、受入、配布物、LS確認、GitHub Release、英日HP更新、本番反映と公開物再取得までを扱う。
診断だけの場合は[build-only入口](hypha_build_entry.md)を使う。

## 開始前に確認すること

作業checkoutの[AGENTS.md](../AGENTS.md)と本書を読む。
Contributor buildは[build-only入口](hypha_build_entry.md)、正式配布は本書を正本とする。

```bash
# 作業checkoutで実行。SDK・署名認証・USBなしで入口だけ確認できる。
node scripts/build_hypha.mjs --help
node scripts/build_hypha.mjs --release --help
```

|依頼の目的|選ぶmode|実行前に確認するもの|
|---|---|---|
|日常のGUI / DSP用検証プラグインをすぐ作る|`--without-aax`|toolchainと採用source。AAX SDK / 署名 / iLokは不要|
|全formatを未署名でbuild / 既存buildを再確認|build-only|native OS、採用source、外部SDK / license、既存output / lock|
|署名・公証・3チャネル・HP反映まで進める|`--release`|clean exact candidate、必要CI / Windows factory、private profile、受入証跡、公開権限|
|所在・使い方だけ確認|どちらの`--help`も可|build / CI / 署名 / uploadを起動しない|

検証専用は`node scripts/build_hypha.mjs --without-aax`。詳しくは[build-only入口](hypha_build_entry.md)。
この選択肢を全format受入や公開gateの代用にしない。`--release`との併用は拒否する。

途中再開は、**作業checkout内**のignored `release_state/`のprofile・receipt・Handoffを先に照合する。
古いworktreeに入口が無い場合は未統合であり、利用者の変更を消さず必要差分を統合する。
別checkoutのscriptを絶対pathで呼ぶと、その別sourceをbuildする。古いcheckoutの代用として実行しない。
個別機器の所在は存在する場合だけ`release_state/aax_signing_local_handoff.md`を参照し、秘密値を転載しない。

## 完了の範囲

`--until hp`が既定。HPアップはファイルのuploadだけではなく、次を含む。

1. 同一候補のMac 6本とWindows署名済みinstallerを確認する。
2. A3のsource/JUCE/SDK/toolchain/payloadと、受入matrix・期待値を固定する。
3. exact signed candidateのA4実host受入を確認する。
4. 既存release-set producerでLS / HP通常導線のPKGと、公開Releaseに必須の復旧用ZIPを作り、Windows EXEと合わせてA5を確認する。
5. LSの全対象への配送を確認する。
6. exact commitへGitHub Releaseを作り、全配布物をdownloadしてhash照合後にdraftを公開する。
7. HPの中央`updates/hypha.json`へ実PKG / Windows EXEのURL・bytes・SHAを設定し、`data/news.json`へその版の標準のお知らせを1件足す。英日templateと対応formatは書き換えずに照合し、HPの試験は中央の記録から版・日付・お知らせを読む。
8. HPの標準手順でtest → commit/push → clean staged deploy → 検証 → production promotionを行う。
9. 英日公開ページの版・日付・notes、通常download routeの302転送先とno-store、導線から取得した配布物の実bytes/hash、HPのdeployment-sourceを確認する。

HPは現行サイトの**PKGを通常導線、ZIPを公開Releaseの復旧用**とする運用を維持する。
LSだけ、Macだけ、Windowsだけで公開リリース完了としない。

## 使い方

release担当が変更を統合してcleanなexact commitを確定した後、そのcheckoutでprivate profileを作る。
SDK・署名の入力は[署名入口](aax_build_signing_entry.md)の既存経路を使う。
以下は構成例であり、run ID・path・stateは担当者が実物から確定する。templateの値は実物から確認する。

```bash
node scripts/build_hypha.mjs --release --init \
  --state release_state/hypha-release.json \
  --sdk /external/licensed/aax-sdk --license-confirmed \
  --ci-run GREEN_EXACT_COMMIT_RUN_ID \
  --windows-installer-dir dist/WINDOWS_CI/KirinHypha-Windows-signed-full \
  --windows-factory-repository REVIEWED_FACTORY_OWNER/REPOSITORY \
  --windows-factory-commit REVIEWED_FACTORY_FULL_SHA \
  --hp-root /absolute/path/to/kirin_hp \
  --ls-state release_state/current_ls.state.json \
  --notes release_state/reviewed-release-notes.md \
  --provenance-report release_state/distribution-provenance.json \
  --source-archive /retained/private-windows-producer/Kirin-Hypha-X.Y.Z-Corresponding-Source.zip \
  --fix-rows release_state/fix-rows.json \
  --date YYYY-MM-DD

# planのみ。build / 署名 / CI / 公開 / HP変更は実行しない。
node scripts/build_hypha.mjs --release --state release_state/hypha-release.json --dry-run

# buildからHPまでを進める／確認待ちの続きから再開する。
node scripts/build_hypha.mjs --release --state release_state/hypha-release.json \
  --execute --publish-approved EXACT_CANDIDATE_ID

# HPまでの後、A7の公開後smoke reportも確認してRelease Completeへ進む。
node scripts/build_hypha.mjs --release --state release_state/hypha-release.json \
  --execute --until complete --publish-approved EXACT_CANDIDATE_ID
```

初期化が出すcandidate IDを、公開を承認した担当者が`--publish-approved`へ渡す。
script作成の依頼や`--execute`だけを、個別candidateの公開承認に読み替えない。
`--init`は既存profileを上書きしない。profile、商品target、管理URL、reportはignored `release_state/`内。
秘密値はprofileへ入れず、Keychain／private factoryから渡す。
Windowsのfactory repository/commitは初期化時に固定する。promotion run・元artifact ZIPは
初期化時に渡すか、Windows検証の開始前に一度だけ後から結ぶ。後結びは実行と併用しない。

```bash
node scripts/build_hypha.mjs --release --state release_state/hypha-release.json \
  --bind-windows-promotion QUALIFIED_PROMOTION_RUN_ID \
  --windows-artifact-archive /retained/signed-full-artifact.zip
```

Mac製造はWindows受入と同時に進められる。未結合ならwindows段の前で確認待ちとなる。
Windows検証開始後と凍結後の結び直しは拒否する。
候補署名runとpromotion runの両方を審査済みfactoryへ結び、artifactのAPI digestと実ZIP、
展開済みfile集合、元候補manifestを照合する。factory identityが欠ける場合は製造前、deliveryが欠ける場合はWindows検証前に止まる。
candidateとsigned-fullは、distribution（A5）が通るまで期限内で保持する。
windowsとdistributionで両artifactを再照合し、A5待ちからの再開も対象になる。
期限の関門は[配布gate](provenance/distribution_gate.md)の4段に従う。
順序は、公開CI → 同じ審査済みfactory commitでbuild_onlyとWindows AAX署名
（PACE＋eSigner。最初の有料署名。2つの順は問わない）→ candidate署名（eSigner）→
Windows実機検証 → promotionを後結び → windows → A3 freeze → A4 → A5。
Macの署名・公証・PACEはWindows実機検証と並行できる。Windows実機検証をA3と呼ばない。
署名前には[未署名の通し](release/presign_smoke.md)を手で一度行う。
Mac製造の並列数は同時に動く処理を考慮して`CMAKE_BUILD_PARALLEL_LEVEL`で明示する。
既定はMacのCPU数。変更は製造入力を変えず、同じtarget archiveを同じ順でlipoへ渡す。
実製造では未署名の同一入力からのbytesを比較してから署名へ進める。

1. AAX署名の前（build_onlyが先ならその前）: 保存期間設定のGETと取得時刻、両workflowの
   全uploadのretention-days、全工程で使う審査済みfactory commit、各工程の予定日と余裕、
   AAXとcandidate両方の署名予算と署名機の空きを記録する。candidate署名をAAXとbuild_onlyの
   各uploadから3日以内（余裕を引く）に置けない、またはA5をcandidate署名＋有効保存期間
   （余裕を引く）より前に置けないなら署名しない。未生成のartifact IDや実期限は要求しない。
2. candidate署名の前: 両入力artifactのrun/artifact ID・digest・expires_at・commitをGETし、
   未失効、同じfactory commit、job時間を含む余裕を確かめる。保存期間設定、共有concurrency
   groupの空き、candidateの予算も再確認する。未署名VST3だけが切れた場合はbuild_onlyを
   やり直す。signed AAXが切れた場合は自動で再署名せず資格判断を取る。
   candidateのupload直後に実run/artifact IDとexpires_atを記録する。
3. promotionの後、Windows検証と凍結の前: 両artifactの実ID・expires_at、
   signed-fullの元ZIP digestと全tree、A3・A4・A5予定日を記録し、A5が早い方の期限に余裕を
   残して収まるか確かめる。provenance-inputsは入力形式だけを確認し、API・ZIP照合は後の
   windowsとdistributionで行う。この関門は担当者が閉じる。
4. A4開始前とA5再開前: 両artifactの期限をGETし直す。A5に間に合わなければA4に進まない。

署名済みAAXと未署名VST3は各3日、同run内の未署名AAXは1日で保持する。
candidateとsigned-fullはupload時のrepo設定を使う。両workflowは同じconcurrency groupなので、
前のrunが終わってから次をdispatchする。A5前にartifactを削除せず、期限切れを再署名の理由にしない。

Mac両producerとWindowsの全formatを同じ鍵へ揃える。ただし現在の正式なWindows経路は、
鍵が空の候補だけを作る。factoryの未署名producerは鍵を空に固定し、Hypha側の取込も空以外を
拒否する。AAX署名とinstaller署名のworkflowにも鍵の入力は無い。
Windowsを含む公開では`--init --update-public-key`を使わない。鍵を有効にするには、
Hypha側script・factoryの変更、trusted closureの更新と事前レビュー、新しいrunが先に要る。
公開CIのdispatch入力`update_public_key`は公開CI自身の未署名buildにだけ効き、正式候補を作らない。
各公開で鍵を使うかどうかは非公開のprofileに記録する。

鍵を指定できる個別scriptの入力は、正式なWindows factoryが鍵入り候補を作れる根拠にはならない。
省略時は空で更新確認を無効にする。指定する場合はRSA-2048/65537、2048-bitの奇数modulus、
正規小文字表現を要求する。鍵を組み込むcandidate IDにはdigestの短縮値も含め、
同commit/版の別鍵へ公開承認を流用しない。CMakeはconfigureごとの
`KIRIN_HYPHA_UPDATE_PUBLIC_KEY_INPUT`だけを採用し、古いcacheを持ち越さない。
公開鍵のSHA-256はbinary内marker、Mac plist、freeze、PKG/ZIP/EXE/AAX provenanceへ記録し、
packaging後の実payload hashと照合する。空・異なる鍵・旧provenanceを通知有効候補として受理しない。
Mac UniversalはARM64/Intel各sliceを個別に検証し、markerと各slice hashを保持する。
本番鍵の所有・rotation承認、manifest endpointの公開/readback、exact binaryの実host受入は別のgate。

release coordinatorはMacで動く。Windows build/署名は既存の承認済みfactoryが行い、
同一commitの`KirinHypha-Windows-signed-full`を入力にする。MacからWindowsをcross-compileしない。
Windowsのローカル診断buildは同じ`build_hypha.mjs`のbuild-only modeで実行できる。
AUはApple専用。Windows x64を「Universal Binary」とは呼ばない。

## 自動化する部分と、人による確認

署名・公証・配布・GitHub・HPのコマンド接続はscriptが行うが、実DAWの聴取試験を自動PASSにしない。
`provenance-inputs`、`ci`、`source-delivery`、`macos-au-vst3`、`macos-aax`、`windows`、`freeze`、`hosts`、`packages`、
`provenance`、`distribution`、`ls`、`github`、`hp`、`postrelease`の順にreceiptを保存する。

確認待ちは`CHECKPOINT`／exit 2。後段へは進まない。担当者は表示されたgateを確認し、
profileで指定されたreportへ実測と証跡を記録する。接続や資料の不足を、利用者の手動編集で補わない。

- **CI / Windows**: exact commit、正しいworkflow、全必須job green、署名・pluginval・installer lifecycle・
  external validation・Native-only provenanceが必要。旧runは代用できない。
- **性能予算**: CIの両probeは`scripts/perf_budget.tsv`の中央値比上限で検査する。上限はgreenなCIの実測最大値に10%の余裕を加えて固定し、手元のMacの実測とは混ぜない。手元のrelease-mode Mac試験は絶対CPU 18%未満で判定し、較正比は診断値として記録する（CIの比上限は適用しない）。
- **Source / NOTICE**: `source-delivery`はcleanな採用source・承認済みJUCE bytes・依存sourceを検査し、対応source ZIPとlicense/NOTICEを準備する。Windows private factoryの未署名producerが作ったZIPを`--source-archive`で取り込み、Macでも全entryを照合して同じbytes/hashを使う。PKGの`Library/Application Support/Kirin Hypha/Legal`、ZIPの`Legal`、Windowsのインストール先`Kirin Mastering/Kirin Hypha/Legal`に同じsource配送情報を入れる。署名済みbundleは変更しない。source準備の成功はactual extraction/linkage reviewを代用しない。
- **A4**: Intel macOSのAU / VST3 / AAX、Windows x64のVST3 / AAXで普通の通し
  （導入、path/hash、pair、通常音声、Keep、保存、再起動）を確認する。共通の流れは代表hostへ寄せ、
  host固有の時計・ループ・PDC、OSごとのpair組合せと192 kHz、Windowsの更新・uninstallを確認する。
  通常透明性・LISTEN・Blind・Exact 4 Sの定量的安全条件は維持する。
  rate/layoutの既存計測・pair試験、editor操作と到達性、installer lifecycleは契約のCI oracleへ結び、
  jobとstepの成功を確認する。新しい全組合せのnative試験が済んだことは意味しない。
  macOS Universal両sliceとARM64の自動CIを維持する。
- **Provenance**: build/signing前に素材の用途を確認し、packaging後に実payloadのNOTICE・license・対応source配送を確認する。保持したprivate reportと実bytesを[配布gate](provenance/distribution_gate.md)で照合する。fixture PASSだけで配布readyにしない。
- **A5**: packaging前後のpayload連続性、両OSのclean install、全署名、Rollback準備の証跡が必要。
- **LS**: 現行Runbookのauthenticated uploadはoperator工程。scriptはPKG/stateを揃え、各商品の
  ダウンロード控えのhashとAPIの配送metadataを確認する。LS uploadを自動実装済みとは扱わない。
- **A7**: 公開導線のdownload検証に加え、公開物をinstallしてscan、pair、通常音声、session reopenを確認する。

`hosts.json`、`distribution.json`、`ls.json`、`postrelease.json`のtemplateは必要gateで自動生成する。
reportはcandidateとpayload/containerのSHAへ結び、各rowの`expected`、`measured`、`oracle`、
実ファイルのpath/hashを要求する。SKIPやPENDINGをPASSで相殺しない。
通常透明性のsample不一致・latency・non-finite、誤Source、未確認PRE出力、安全でないgain復帰、
Blind漏洩、Exact 4 Sのframe誤差は0を要求する。最低期待値を緩めない。
宣言host範囲と追加の数値閾値はA0で担当者が照合し、A3前にmatrix／期待値へ含める。
reportで事後に期待値を変更して通すことはできない。

新規profileは`kirin-hypha-release-pipeline-v2`。旧schemaの凍結済みprofileは読み込まない。
`--init --fix-rows FILE`で版ごとの深い検証を結ぶ。入力例は次の形。ファイルのhashと全行・期待値を
受入条件へ凍結する。`kind: host`だけをhosts.jsonへ要求し、`kind: ci`は名前付きCI oracleへ結ぶ。
CIの標準の期待は`{ "passed": true }`。追加の数値条件には実測oracleの証拠が必要で、無ければ確認待ちにする。

```json
{ "schema": "hypha-release-fix-rows-v1", "release": "X.Y.Z", "rows": [
  { "id": "clock-recovery", "kind": "host", "hosts": ["macOS/x86_64/AU"],
    "depth": "deep", "oracle": "independent callback capture", "expected": { "lostFrames": 0 } },
  { "id": "pair-recovery", "kind": "ci", "hosts": [], "depth": "deep",
    "oracle": "pair-recovery-parity", "expected": { "passed": true } }
] }
```

凍結後の本人判断は、hosts段の`--execute --owner-decision FILE`で結ぶ（複数ファイル可）。
判断ファイルは`schema: hypha-owner-acceptance-decision-v1`、`release`、exact `candidateId`、
`rows[]`の各行に`id`、`reason`、`resumeCondition`、`releaseNote`を必須とする。
通常のhost行、追加のhost/CI行を複数まとめられる。4件の定量的安全条件は対象外。
受入条件とfreezeは変えず、判断の内容・hash・freezeとの結びをprofileへ保存する。
hosts.jsonでは該当行だけ`OWNER_DEFERRED`を受け付ける。CI行はhosts段の証跡へ残る。
延期はPASSとは区別し、理由・再開条件・公開ノートに使う文を保持する。判断ファイルの変更は後段も拒否する。
確認済みのhost FAILは判断を明示した再開だけが再検証でき、署名・製造のFAILはこの経路で再試行しない。

本人がその版に限って必須の受入から外した行は、`--init`の`--owner-override FILE`で決定ファイル
（schema `hypha-owner-acceptance-override-v1`、同じ版、HOST_TESTSの1行、`excludedResultMustBe`が
`NOT_REQUIRED_OWNER_OVERRIDE`）を候補へ結ぶ（1行に1つ、行ごとに繰り返し指定する）。matrixと期待値は残り、決定のhashは受入の基準と一緒に凍結される。
reportでは結んだ行だけ`NOT_REQUIRED_OWNER_OVERRIDE`を受け付け、決定ファイルが変われば止まる。
`PENDING` / `SKIP`は確認待ち、実測に基づく`FAIL`は受入失敗として区別する。
A7の確認済みFAILを、単なる検証待ちの`RELEASED`へ読み替えない。

## 再利用、省エネ、失敗時

各PASSの実file/tree hashを再確認して再利用する。source、JUCE実bytes、resources、permissions、
symlinks、package/installer/sidecarを確認し、receiptがあるだけでは再利用しない。
CIは指定したgreen runを検証して再利用する。job一覧は`filter=latest`でページを最後まで読み、
同じrun・commitの各必須jobとstepの成功、jobごとのattempt番号を証跡へ残す。落ちたjobだけの
再実行では、成功を維持したjobとattemptが異なってよい。新規dispatch、rerun、予算変更、artifact削除は自動で行わない。
必要CIが無い場合は、共通予算ルールと既存runを確認して正規CI／factoryを実行する必要が残る。
これを省略や恒久停止に読み替えない。

- 同一profileはexclusive lock。停止したprocessのlockは所有者・状況を確認するまで自動解除しない。
- foreign／中断した署名済みoutput、既存pkg/zipをproducerで自動上書きしない。担当者が保全・照合してから再開する。
- `FAIL` / 中断した`RUNNING`のbuild/署名は無根拠に再実行しない。正常な確認待ちだけはreport完成後に再開できる。
- GitHubではdraftと既存assetsを照合し、無いassetだけuploadする。公開済みの別bytesは置換しない。
  下書きはtagで引けないため、作成時のrelease IDをprofileの`githubRelease`に保存し、再開はそのIDで照合する。
  作成の結果が不明なまま止まった場合も、事故の整理で`githubRelease`を消さない。消すと作成済みの下書きを照合できず、
  同じreleaseを二重に作る原因になる。
- HPの別セッションのdirty変更、baseline drift、project link driftを公開へ巻き込まない。
- HPは`deploy-production-clean.sh`を使い、repo-rootから直接`vercel --prod`しない。
- 公開途中の重大FAILは`RELEASE_INCIDENT`。担当者が影響と公開状況を確認するまで自動retryしない。
  Rollbackの外部操作も別途承認が必要。Rollback成功を新版のRelease Completeにしない。
- iLokの抜き差し、Cloud移動、driver/機器再起動はscriptが行わない。物理authorizationが必要なのは署名時だけ。

## 状態と検証

HPまでのPASSは`RELEASED`。A7待ちであり`RELEASE_COMPLETE`ではない。
全必須A7が通った場合だけ`RELEASE_COMPLETE`。fixture成功と実公開完了は別証跡として報告する。

```bash
node --test scripts/build_hypha.test.mjs scripts/release_hypha.test.mjs \
  scripts/ls_release/hypha_release_hp.test.mjs scripts/ls_release/hypha_release_github.test.mjs
```

既存の正本: [3チャネルRunbook](ls_release/kirin_hypha_ls_runbook.md)、[AAX署名入口](aax_build_signing_entry.md)、
HP repositoryの`README_DEPLOY.md`。
CLI仕様: [gh api](https://cli.github.com/manual/gh_api)と[GitHub Releases REST API](https://docs.github.com/en/rest/releases)
（下書きはtagで引けないため、作成時のrelease IDで再開・upload・読戻し・公開する）、
[Vercel staged deployment](https://vercel.com/docs/cli/deploy#skip-domain)。

## 受入と記録の型

[手で通す手順と記録の型](release/presign_smoke.md)と[依頼文の雛形](release/acceptance_request_template.md)を使う。
identityはrunの始めに一回、一行一束、hashは候補／凍結／packages／最終の四点、独立reviewはhostの最後に一回。
二分進まなければ状態と再開条件を記録して次へ進む。

公開の前日に[読み取り点検とLS API確認](release/prepublication_preflight.md)を一束で行う。

Macの並列化の実製造確認は、同じsource、lockfile、SDK、toolchain、公開鍵、build pathで行い、
直列の未署名製品を別directoryへ保全してから並列製造と比較する。
保持するのは両targetのRust archive、Universal archive、PRE/POSTのAU/VST3/AAX製品。
build directoryや依存全体を複製せず、同じ相対配置の製品だけを二つのtreeへ集める。

```bash
node scripts/ls_release/verify_build_bytes.mjs /retained/serial-products /retained/parallel-products
```

この比較はfile集合、全fileのbytes、mode、symlinkの行先の完全一致を要求する。
同じdirectoryを二度渡した確認は拒否する。署名・timestamp・公証済みの製品同士を比較して
製造の再現性としない。fixture試験はこの比較器と製造入力の不変性を守り、実製品の比較証拠を代用しない。

## PKG の ticket 添付だけを再開する

署名済み PKG と Accepted submission を作った後の通信切断は、製造・署名・公証submitをやり直さない。
通常 producer は staple 前の signed bytes を ignored な private directory に控え、hash と submission ID、
notary log、凍結候補との結びを receipt へ保持する。PKG の公開 sidecar と配布 facts は最終bytesだけを使う。

```bash
node scripts/build_hypha.mjs --release --state release_state/hypha-release.json \
  --execute --until packages --resume-pkg-staple
```

この明示再開は、保存済み packages の FAIL／PENDING と、同じ候補の検証済み private receipt がある場合だけ使える。
FAIL の履歴は保持する。receipt、控え、候補、凍結条件が違う場合や、別の署名・製造の FAIL は再実行しない。
新しい経路が receipt を残す前の失敗に対して、既存PKGを自動取り込みしたり、公証結果不明の submit を再送したりしない。

PKG producer 単体では `--resume-staple`、coordinator は凍結から導出した
`KIRIN_PKG_STAPLE_STATE` と `KIRIN_PKG_STAPLE_BINDING` を渡す。resume は既存PKGの検証と
staple／validateだけを行い、productbuild・署名・submitへ戻らない。validateだけが未確認ならstapleも繰り返さない。
PKG完成後に未生成のZIPを作り、通常の全配布物検証を行う。既存の別容器は上書きしない。

通信切断（exit 68、CloudKit / URL connection loss）は各読取／添付確認で最大3回試し、
まだ確認できなければ exit 2 の確認待ちにする。非通信の失敗は FAIL として止める。
署名・payload・Gatekeeper と原本prefixの照合を、通信を要する `stapler validate` より先に行う。
添付後は「原本全bytesがprefix」「追記がticket一件だけ」「公証logのhashが原本hashと一致」を必須にする。
trailer の境界は [Apple の公開形式](https://github.com/apple-oss-distributions/Security/blob/main/OSX/libsecurity_codesigning/lib/notarization.cpp)に従う。
private receipt、原本、notary logをGitHub Releaseや配布factsへ追加しない。
