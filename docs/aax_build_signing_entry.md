# AAXビルド・署名の共通入口

更新: 2026-09-30。次のセッションは、本書から対象OSの手順書へ進む。
本書は操作の選択と設定の参照先をまとめる。署名処理の詳細は各OSの手順書と実装を正本とし、
顧客番号、アカウント、認証情報、管理画面の画像を公開資料へ転記しない。

全formatをまとめてビルドする場合は[統合入口](hypha_build_entry.md)の
`node scripts/build_hypha.mjs --sdk PATH --license-confirmed`を使う。
Macは6本のUniversal、Windowsは4本のx64を同じCoreから作る。これは未署名診断buildで
iLok USB / Cloudの移動は不要。以下の署名・配布経路とは分けて扱い、成功を公開可能と読み替えない。
ビルドからHPアップまでの全工程は同じscriptの`--release` modeへ進む。
[release統合入口](hypha_release_entry.md)が、本書の正規producerを使って署名・公証・受入・配布・
GitHub Release・英日HP本番反映をつなぐ。未完了の実host試験や個別候補の公開承認は省略しない。

## 最初に区別すること

「AAXをビルドした」と「通常のPro Toolsで検証できる」「公開できる」は別の状態である。

| 目的 | 正規入口 | 得られるもの／残る条件 |
|---|---|---|
| Macの未署名診断ビルド | `scripts/build_aax_universal.sh --diagnostic` | Universal PRE/POSTと診断receipt。未署名を許可する診断host専用 |
| Macの通常Pro Tools向けローカル検証 | 同scriptの`--diagnostic-sign` | PACE + Developer ID署名。公証なし・配布禁止 |
| Macの配布候補 | 同scriptの`--sign` | PACE + Developer ID署名、Apple Accepted、公証receipt。実host試験・packagingは別 |
| Windowsの署名前ビルド | `scripts/build_aax_windows.ps1 -Distribution` | x64 PRE/POSTとbuild provenance。**まだ未署名** |
| Windowsの署名 | private factory → `scripts/windows/sign-aax-wraptool.ps1` | PACE + Authenticodeを一度に適用し、signed provenanceを生成。installer・実host試験は別 |

表はモードの選択用であり、実行時にはSDK pathとlicense確認などが必要。
完全なコマンドは[Mac手順書](aax_macos_universal_build_20260910.md)と
[Windows手順書](aax_windows_build_20260910.md)にある。
Windowsで`-Distribution`を省略したものは診断用で、配布署名の入力にはできない。
Kimeraは全モードで任意。フォント未搭載を署名・配布のblockerにしない。

## 「署名設定」は一つのファイルではない

ビルドscriptは下記の環境変数や引数を受け取る。共通の設定ファイルを自動読込みする設計ではない。
shellで変数が未設定でも、既存の署名経路や認証情報が失われたとは限らない。

| 入力 | 確認する参照先 | 混同しないもの |
|---|---|---|
| `KIRIN_AAX_PACE_CUSTOMER_NUMBER` | PACE CentralのAdmin画面の`Customer Number`。Windows factoryでは同名のGitHub Secret | `Publisher GUID`ではない |
| `KIRIN_AAX_PACE_CUSTOMER_NAME` | PACEに登録した組織／publisher名。Windows factoryでは同名のGitHub Secret | site administratorの氏名・メールアドレスではない |
| `KIRIN_AAX_PACE_ACCOUNT` | Macの既存PACE/iLok認証経路。scriptでは任意 | 管理画面のメールから推測しない。未指定時の既存同期済み認証の可否は署名結果で確認 |
| `KIRIN_AAX_APPLE_SIGN_IDENTITY` | Mac Keychain内のDeveloper ID Application署名identity | pkg用のDeveloper ID Installerとは別 |
| `KIRIN_NOTARY_PROFILE` | Mac Keychainのnotarytool profile。scriptの既定名は`kirin-notarize` | PACE認証ではない。profile名だけでは認証成功の証拠にならない |
| Windowsの`CertificateThumbprint` | private factoryがeSigner CKAでその実行ユーザーにロードした証明書 | MacのApple証明書ではない。秘密鍵を書き出さない |

PACE Centralの画像は顧客情報の参照資料であり、現在のPACE Tools authorization、ログイン成功、
GitHub Secretsの登録状態、署名成功を証明するものではない。
`Publisher GUID`は署名者識別情報であってcustomer numberの代用品ではない。

値が必要なときは、まず既存のprivate入力経路とローカル引継ぎを確認する。
存在未確認の「設定ファイルの保存場所」を利用者に尋ねたり、秘密値のチャット貼付けを依頼しない。
`env`の全件表示、shell trace、secretを埋めたコマンド例やログは使わない。
確認結果には変数名と「設定済み／未設定」「検証済み／未検証」だけを記す。

このMacの機器path・提供済み資料の所在・直近候補は、存在すれば
`release_state/aax_signing_local_handoff.md`に記録する。同fileはgitignore対象のローカルメモであり、
認証情報の保管庫でも自動実行する設定fileでもない。別checkoutへ自動では引き継がれない。
公開資料にはその内容をコピーせず、手元に無ければprivate引継ぎの所在だけを確認する。

## Macで進める順序

1. 対象commit／B番号と`crates/hypha_pre/Cargo.toml`のversionを確認する。
   過去の手順書のversionや、同versionの別commitの署名済みbundleを流用しない。
2. 外部SDK、承認済みJUCE patch stack、既存build/cacheを確認する。
   配布候補にはclean sourceが必要。利用者の変更を消さず、適切な既存clean checkoutを使う。
3. 上表からモードを選ぶ。署名する場合は、顧客入力、PACE Tools authorization、
   Developer ID Applicationを別々に確認する。`--sign`には公証profileも必要。
4. [Mac手順書](aax_macos_universal_build_20260910.md)の正規scriptを実行する。
   `--dry-run`は引数とコマンド構成の確認のみで、認証・署名・公証の合格ではない。
   再実行は生成済みPRE/POST AAX productを作り直すため、必要な既存成果物は先に保全する。
5. 出力の署名とreceiptを照合する。配布用は現在のcommit／bundleに結び付くAccepted v3 receiptが必要。
   Apple notary logのarchive SHA-256と、保持した提出archiveの実bytesのSHA-256の一致も検証する。
   hash結合のない旧v2 receiptは配布用の合格根拠にしない。
   AU/VST3の公証成功や古いAAX receiptでは代用できない。
6. 配置は別の操作として扱う。未署名AAXで既存の署名済みAAXを置換しない。
   公開候補は`--with-aax`のpkg／zip検証と、exact candidateのPro Tools試験へ進める。

## Windowsで進める順序

1. 検証機へ接続する前に、共通指示のWindows remote access Runbookを読む。
2. [Windows手順書](aax_windows_build_20260910.md)に従い、外部SDKとVS x64環境でビルドする。
   `kirin-hypha-windows-aax-build.json`のcommit／version／Native-only／hashを検証する。
3. 既存private release-control repositoryの`.github/workflows/hypha-aax-signing.yml`を確認する。
   workflow名は`Hypha Windows AAX signing`。public repoのAAX Phase A workflowは署名入口ではない。
   private側の所在地はローカル引継ぎを参照し、public GPL repoへ秘密値を移さない。
4. 新規build経路にはexact full commitと、そのcommitの完了済みHypha CI runが必要。
   通常経路は`public history identity`、`release source contract (macos)`、
   `auval arm64 (AU validation)`、`windows VST3 preflight`の4 jobのgreenと、
   署名手順の承認済みhash、SDK license、専用runnerと物理PACE authorizationを確認する。
   過去のunsigned run再利用は明示された承認対象だけ。任意のローカルZIPを代入しない。
5. factoryがGitHub Secretsを注入し、eSigner CKA証明書をロードして、PACE + Authenticodeを
   **同じwraptool操作で**適用する。PACE署名後に別の署名処理でbinaryを変更しない。
   入力は保持し、新しい別output directoryへ署名する。
6. `kirin-hypha-windows-aax-signed.json`とPRE/POST両方の署名を検証する。
   factoryは終了時に一時証明書とCKA設定をunload／removeする。
   次回の`CurrentUser`証明書storeが空でも、それだけで設定紛失とは判定しない。
7. 同一release commitの公開CI合格と、審査済みprivate factoryの`windows-latest`未署名
   producer由来VST3、同じ審査済みfactoryのsigned AAX provenanceを結ぶ。
   正規installerとinstall／同版reinstall／旧公開版upgrade／uninstallを検証し、
   同じfactoryでpromotionしたsigned-full全体の出どころを照合する。

統合公開では次の関門を適用する。

期限の関門は[配布gate](provenance/distribution_gate.md)の4段に従う。
順序は、公開CI → 同じ審査済みfactory commitでbuild_onlyとWindows AAX署名
（PACE＋eSigner。最初の有料署名。2つの順は問わない）→ candidate署名（eSigner）→
Windows実機検証 → promotion → coordinator（Macの署名・公証・PACE → windows →
A3 freeze → A4 → A5）。Windows実機検証をA3と呼ばない。

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
3. promotionの後、coordinatorの`--execute`とMac工程の前: 両artifactの実ID・expires_at、
   signed-fullの元ZIP digestと全tree、A3・A4・A5予定日を記録し、A5が早い方の期限に余裕を
   残して収まるか確かめる。provenance-inputsは入力形式だけを確認し、API・ZIP照合は後の
   windowsとdistributionで行う。この関門は担当者が閉じる。
4. A4開始前とA5再開前: 両artifactの期限をGETし直す。A5に間に合わなければA4に進まない。

署名済みAAXと未署名VST3は各3日、同run内の未署名AAXは1日で保持する。
candidateとsigned-fullはupload時のrepo設定を使う。両workflowは同じconcurrency groupなので、
前のrunが終わってから次をdispatchする。A5前にartifactを削除せず、期限切れを再署名の理由にしない。

private workflowのソース確認は、現在のGitHub Secret登録やjob成功の確認ではない。
CI dispatch、artifactの外部受渡し、機器設定、配置、公開に新しい権限が必要なら、その前に確認する。

## 止まった場所を具体的に残す

| 状態 | 次に確認すること |
|---|---|
| 顧客変数が未設定 | Macの承認済み入力経路／Windowsのprivate Secret注入。ファイル紛失とは断定しない |
| Windows証明書がない | factoryのload工程・実行ユーザー・cleanup履歴。新規証明書発行を先に求めない |
| build provenanceだけ存在 | 未署名。通常Pro Tools用の完成品・signed installerとは報告しない |
| Mac署名は通るが公証receiptがない | diagnostic-signか、配布公証の途中かを区別。配布gateは解除しない |
| GUI検証が失敗しheadlessだけpass | GUI failureを残す。host／製品原因を推測で断定しない |
| 別commitの実機試験だけ存在 | 現候補のhost試験は未実施。同versionでも証拠を流用しない |

Handoffにはcommit／B番号／version、checkout、成果物path／hash、署名・公証・配置・host試験の
各状態、未解決gate、次の一手を残す。認証値と私的な管理URLは記録しない。
「ビルド完了」で止めず、どこまで使える成果物かを明示する。
公開完了は[3チャネルのrelease Runbook](ls_release/kirin_hypha_ls_runbook.md)に従う。
AAXは4つ目のチャネルではなく、既存macOS／Windows成果物への追加formatである。
