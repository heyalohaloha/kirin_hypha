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
4. 既存release-set producerでLS PKG、HP ZIPを作り、Windows EXEと合わせてA5を確認する。
5. LSの全対象への配送を確認する。
6. exact commitへGitHub Releaseを作り、全配布物をdownloadしてhash照合後にdraftを公開する。
7. HPの英語・日本語のMac PKG / Windows EXEリンク、版、日付、対応formatと関連testを更新する。
8. HPの標準手順でtest → commit/push → clean staged deploy → 検証 → production promotionを行う。
9. 英日公開ページ、配布物の実bytes/hash、HPのdeployment-sourceを確認する。

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
  --hp-root /absolute/path/to/kirin_hp \
  --ls-state release_state/current_ls.state.json \
  --notes release_state/reviewed-release-notes.md \
  --provenance-report release_state/distribution-provenance.json \
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

release coordinatorはMacで動く。Windows build/署名は既存の承認済みfactoryが行い、
同一commitの`KirinHypha-Windows-signed-full`を入力にする。MacからWindowsをcross-compileしない。
Windowsのローカル診断buildは同じ`build_hypha.mjs`のbuild-only modeで実行できる。
AUはApple専用。Windows x64を「Universal Binary」とは呼ばない。

## 自動化する部分と、人による確認

署名・公証・配布・GitHub・HPのコマンド接続はscriptが行うが、実DAWの聴取試験を自動PASSにしない。
`provenance-inputs`、`ci`、`macos-au-vst3`、`macos-aax`、`windows`、`freeze`、`hosts`、`packages`、
`provenance`、`distribution`、`ls`、`github`、`hp`、`postrelease`の順にreceiptを保存する。

確認待ちは`CHECKPOINT`／exit 2。後段へは進まない。担当者は表示されたgateを確認し、
profileで指定されたreportへ実測と証跡を記録する。接続や資料の不足を、利用者の手動編集で補わない。

- **CI / Windows**: exact commit、正しいworkflow、全必須job green、署名・pluginval・installer lifecycle・
  external validation・Native-only provenanceが必要。旧runは代用できない。
- **A4**: SDK/機器を含まないfixture結果で実host matrixを代用しない。ARM64 / Intelの各format、
  Windows両format、pair組合せ、LISTEN / Blind / Exact 4 S、通常透明性、保存、stress等を確認する。
- **Provenance**: build/signing前に素材の用途を確認し、packaging後に実payloadのNOTICE・license・対応source配送を確認する。保持したprivate reportと実bytesを[配布gate](provenance/distribution_gate.md)で照合する。fixture PASSだけで配布readyにしない。
- **A5**: packaging前後のpayload連続性、両OSのclean install、全署名、Rollback準備の証跡が必要。
- **LS**: 現行Runbookのauthenticated uploadはoperator工程。scriptはPKG/stateを揃え、各商品の
  ダウンロード控えのhashとChromeの配送表示を確認する。LS uploadを自動実装済みとは扱わない。
- **A7**: 公開導線のdownload検証に加え、公開物をinstallしてscan、pair、通常音声、session reopenを確認する。

`hosts.json`、`distribution.json`、`ls.json`、`postrelease.json`のtemplateは必要gateで自動生成する。
reportはcandidateとpayload/containerのSHAへ結び、各rowの`expected`、`measured`、`oracle`、
実ファイルのpath/hashを要求する。SKIPやPENDINGをPASSで相殺しない。
通常透明性のsample不一致・latency・non-finite、誤Source、未確認PRE出力、安全でないgain復帰、
Blind漏洩、Exact 4 Sのframe誤差は0を要求する。最低期待値を緩めない。
宣言host範囲と追加の数値閾値はA0で担当者が照合し、A3前にmatrix／期待値へ含める。
reportで事後に期待値を変更して通すことはできない。
`PENDING` / `SKIP`は確認待ち、実測に基づく`FAIL`は受入失敗として区別する。
A7の確認済みFAILを、単なる検証待ちの`RELEASED`へ読み替えない。

## 再利用、省エネ、失敗時

各PASSの実file/tree hashを再確認して再利用する。source、JUCE実bytes、resources、permissions、
symlinks、package/installer/sidecarを確認し、receiptがあるだけでは再利用しない。
CIは指定したgreen runを検証して再利用する。新規dispatch、rerun、予算変更、artifact削除は自動で行わない。
必要CIが無い場合は、共通予算ルールと既存runを確認して正規CI／factoryを実行する必要が残る。
これを省略や恒久停止に読み替えない。

- 同一profileはexclusive lock。停止したprocessのlockは所有者・状況を確認するまで自動解除しない。
- foreign／中断した署名済みoutput、既存pkg/zipをproducerで自動上書きしない。担当者が保全・照合してから再開する。
- `FAIL` / 中断した`RUNNING`のbuild/署名は無根拠に再実行しない。正常な確認待ちだけはreport完成後に再開できる。
- GitHubではdraftと既存assetsを照合し、無いassetだけuploadする。公開済みの別bytesは置換しない。
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
  scripts/ls_release/hypha_release_hp.test.mjs
```

既存の正本: [3チャネルRunbook](ls_release/kirin_hypha_ls_runbook.md)、[AAX署名入口](aax_build_signing_entry.md)、
HP repositoryの`README_DEPLOY.md`。
CLI仕様: [GitHub Release create](https://cli.github.com/manual/gh_release_create)、
[download](https://cli.github.com/manual/gh_release_download)、
[Vercel staged deployment](https://vercel.com/docs/cli/deploy#skip-domain)。
