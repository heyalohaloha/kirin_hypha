# Stage 2 GitHub baseline — 2026-10-05

本書は読み取り専用の設定候補資料。GitHub設定変更・push・PR・dispatch・rerun・secret取得・private factory接続・test/buildを行っていない。

- Local source: `b1b3df847b3d9b9af021e09e69b982d7e8e6fa15`。
- Remote default main: `a8f5a4a4a791cd4a81346e2c5462d08269619b16`。候補のhardeningはまだGitHubへ反映されていない。
- API主snapshot: 2026-10-05 01:15 UTC。collectorはGETのみ。collaborator permission追加取得はJSON内の独立timestamp参照。
- Repository admin read可、owner.type=User。private factoryの設定・コード・credential/local secretファイルは調査対象外。
- 根拠: address/credential値を除いたread-only API snapshotは公開Git外へ保存。過去24 branch/40 tagtipの既存監査は継承し、Stage 2では再fetch/再列挙していない。
- APIの取得失敗は安全判定に使わない。secret scanning alertsの内容は値露出を避けるため取得していない。credential発見/rotation実施の証拠はない。

## Current / Recommended / impact / approval

下表の「候補」は設定済みを意味しない。ユーザーが明示承認したPVR/email privacy/noreply以外の新しい設定変更はこの段階で行わない。

| 項目 | Current | Recommended | Reason | Security impact | Development impact | Codexが安全に変更できるか | Owner approval | Evidence |
|---|---|---|---|---|---|---|---|---|
| Default branch / source | main; public; remote a8f5a4a4; local b1b3df84 unpublished | main維持。候補が公開された後もexact SHAで再確認 | 採用sourceの混同防止 | 旧runを候補の合格へ流用しない | 既存開発入口を維持 | 変更不要 | 不要（維持） | repository/default_branch |
| PR / approvals | PR review ruleあり; required_approving_review_count=0; collaboratorはadmin 1人 | PR必須を維持。独立reviewerを確保できる場合のみ1 approvalを検討 | 現状、独立承認は強制されない | レビューなしの同一repo workflow変更は残る | enforce_admins下でsole authorの自己承認は不可。1にすると開発停止のおそれ | 不可（自動変更しない） | 必須。reviewer/運用を先に決定 | main_protection/collaborator_permission_summary |
| Stale approval | dismiss_stale_reviews=false | 独立reviewer導入時、trueを併せて検討 | 承認後の追加変更を再審査 | 承認の使い回しを防ぐ | 更新ごとに再承認が必要 | 不可（自動変更しない） | 必須 | main_protection |
| Last push approval | require_last_push_approval=false | 独立reviewerと最後のpush担当を分離できる場合true検討 | 最後の変更の別人確認 | review後の差込みを防ぐ | solo運用をblockする可能性 | 不可（自動変更しない） | 必須 | main_protection |
| CODEOWNERS | require_code_owner_reviews=false; candidate .github/CODEOWNERSなし | sole ownerの複雑な形式導入は保留。独立ownerが確保された場合、workflows/release/signing/packagingを対象に検討 | 自己owner指定だけでは独立確認にならない | 導入時は機微変更のreviewを明示 | owner追加/レビュー責任の調整が必要 | 今は変更しない | 導入時必須 | main_protection + tracked files |
| Required checks | strict=true; GitHub Actions app_id=15368に結び付く4 checks（下表） | 4件を保持。SDK absenceの追加は候補CI確認後に所有者が決定 | 既存gateを落とさない | status producerのApp制限を維持。ただしworkflow定義は編集可能 | strict updateで必要再実行は生じる | 設定変更しない | 追加時必須 | main_protection/commit_checks |
| Force push / deletion | allow_force_pushes=false; allow_deletions=false | false維持 | 履歴/参照破壊を防ぐ | main保護維持 | 既存保護と同じ | 変更不要 | 不要（維持） | main_protection |
| Admin enforcement / bypass | enforce_admins=true; restrictions=null; review dismiss/bypass listsなし | enforce_admins維持。admin自身は設定を変更できるため、絶対的な改変不可と表現しない | GitHub保護の適用範囲を正確にする | 通常merge/pushに保護が効く。所有者設定変更の残余あり | solo approval変更のblock要因 | 変更不要 | 追加保護の設計は必須 | main_protection |
| Rulesets | API200; includes_parents=true; [] | 既存branch protection維持。ruleset移行は重複/適用範囲を検証した別判断 | 既存保護を壊さない | ルール追加はserver enforcement候補 | 誤適用/二重制約で停止し得る | 不可（自動移行しない） | 必須 | rulesets |
| Conversation / history / merge | conversation resolution=true; linear history=false; merge=true; squash/rebase/auto=false; auto branch deletion=false | conversation/auto-merge無効維持。merge方式を今回変更しない | 意図しないmerge/開発変更を避ける | CI成功だけでauto mergeしない | 既存merge方式を維持 | 変更不要 | 不要（維持） | main_protection/repository |
| GITHUB_TOKEN default / approvals | default_workflow_permissions=read; can_approve_pull_request_reviews=false | read/false維持。candidateはcontents:read、その他noneを明示 | 最小権限。defaultは上限ではない | public fork PRのread制限とsame-repoのwrite要求可能性を区別 | 通常CIにwrite不要 | 変更不要 | 将来write例外を作る場合必須 | workflow_permissions + workflow source |
| Fork workflow approval | first_time_contributors | all_external_contributors候補を所有者へ提示 | 初回だけでなく反復contributorのworkflow差分を確認 | 実行予算/未信頼コードの実行前審査を強化。実行承認はmerge/署名承認ではない | 外部contributionごとに待ち時間/owner作業 | 不可（自動変更しない） | 必須 | fork_approval |
| Actions / reusable workflow policy | Actions enabled; allowed_actions=all; selected-actions409=all allowed; no reusable uses in candidate/history | 必要な4 action originだけのselected許可を候補にする。外部reusable workflowは必要時のみ個別審査 | 任意action追加の縮小 | 許可policyはserver enforcement候補。許可actionやrun:内の任意コードまでは安全化しない | 新action/将来reusable追加ごとに設定調整 | 不可（自動変更しない） | 必須。全ref/再実行への影響確認 | actions_permissions/selected_actions |
| Action immutable SHA policy | sha_pinning_required=false; candidateは全uses full40 SHA | true候補。ただし全old refsがmutable usesを含むためhistoric rerun/dispatch停止を明示 | 参照移動を防ぐ | source guard以外のserver enforcement候補 | 過去refsをrewriteせず旧実行の扱いを決める必要 | 不可（自動変更しない） | 必須 | actions_permissions + prior all-ref audit |
| Workflow events / dispatch | remote CI/AAX active2; push main / PR main / dispatch; old signing inputsあり | candidateのunsigned-only/hosted-onlyを公開後に維持。dispatchもsigning trust証明にしない | write利用者は別branch/tagをdispatch可能 | PR Target等のprivileged eventなし。old refへのdispatchは残る | licensed SDKの実buildは既存local/factoryへ | source候補済。設定は変更しない | 公開/CI起動は別scope | actions_workflows + source/all-ref audit |
| Repository runners | API200 total_count=0 | 0維持。public repoへsigning/SDK常設runnerを再登録しない | 旧refのself-hosted routeもrepo全体設定を使う | 公開codeから常設host/残留credentialへ到達させない | licensed buildは独立trusted routeが必要 | 変更不要 | 新runner登録を考える場合必須 | repository_runners |
| Organization / runner groups | owner.type=User; organization-owned policy/scopeはN/A | public repoのorg共有scopeと混同しない。別factoryのrunner/group/host隔離は未確認として別審査 | 他repoのrunnerの安全性を推定しない | factory scope隔離は重要だが今回証明なし | private側owner確認が必要 | 不可（privateアクセスしない） | private側監査時必須 | repository owner type |
| Environments | API200 total_count=0; protected signing envなし | public側にsigner envを追加する必要はない。factory側はplan/featureが確認できた場合のみselected refs/reviewer/no bypass候補 | 公開CIへsecretを戻さない | env gatingはfeature依存; self-hosted host自体の隔離ではない | private required reviewersのplan条件/別reviewer確保が必要 | 今は変更しない | private側確認と設計に必須 | environments + official env docs |
| Secrets / variables | repository secrets=0; variables=0; env=0; owner Userのためorg shared secret scope N/A | signing credentials/credential相当variablesをpublic repoへ登録しない | 21 tipのlegacy secret receiversは残る | absenceが全ref共通のsecret供給防止。credential値自体の存在/rotation履歴は未確認 | private factory/local Keychain経路を継続 | 変更不要。secret値を取得しない | 登録/rotation等は別途必須 | repository_secrets/repository_variables + prior all-ref audit |
| PVR / Security reporting | enabled=true | true維持、SECURITY.mdのprivate reportingを利用 | public issueへ未修正脆弱性を書かせない | 非公開report受付が利用可 | 既存owner対応が必要 | 変更不要 | 不要（既承認済の維持） | pvr |
| Email privacy | public profile email=null; fresh authenticated GET /user/emailsでprimary=1件; verified=True / visibility=private。addressはすべてredacted。 Keep private / Block pushes UI未確認 | 新commit noreply継続。ownerがaccount UI2設定を確認 | public email欠如だけではGit author/web merge privacyを証明しない | 将来個人emailの再露出を減らす | 全アカウントに影響するため勝手に変更しない | 不可（account UI未確認） | 必須 | public_owner_email; previous noreply decision |
| Dependency graph | SBOM API404 Not Found; graph有効状態/graph contents未確認 | UI/API取得手段で状態確認し、無効なら有効化を候補にする | 404をdependencyなし/safeへ読み替えない | alertsが利用する依存の可視化 | 通知/scan機能の準備 | 不可（未確認のまま設定変更しない） | 有効化時必須 | dependency_graph_sbom |
| Dependabot alerts | check API404 explicitly disabled; alerts API403 explicitly disabled; open count未取得 | graph確認後alerts有効化を候補にする | 実dependencyの既知脆弱性通知 | scan未確認を安全とみなさない | maintainer通知/triage作業が増える | 不可（自動変更しない） | 必須 | dependabot_alerts_enabled/dependabot_open_alerts |
| Dependabot security updates | security_and_analysis=disabled | alerts/graph確認後security update PRを候補にする; auto merge導入なし | 脆弱性の修正候補をreview可能にする | 修正PRも同じpublic read-only checks | PR/CI増加と互換性審査 | 不可（自動変更しない） | 必須 | repository.security_and_analysis |
| Dependabot version updates | candidate .github/dependabot.yml Actions/Cargoのみ weekly limit2; remote mainに未掲載 | candidate維持。GitHub有効稼働はdefault branch公開後に確認 | 実ecosystemのみに限定 | SHA action更新もreview; fullSHA format gateは特定digestに固定しない | 更新PRから必要CIをまとめる。auto mergeなし | source候補済。起動/公開なし | 公開は別scope | tracked .github/dependabot.yml |
| Secret scanning | repository security_and_analysis=disabled; non-provider patterns/validity checksもdisabled | public対応secret scanning有効化を候補、履歴/既存public面を再scan。partner automatic scanとrepo alertsを混同しない | credential検出をgrepだけに依存しない | 新H2なら値を記録せずrevoke/rotation優先 | 過去false positiveのtriageが必要 | 不可（自動変更しない） | 必須 | repository.security_and_analysis |
| Push protection | secret_scanning_push_protection=disabled | secret scanningと併せてrepository push protection有効化を候補 | 既知secretのpushをblockするserver機構 | 全secret種/個人情報を検出するものではない; bypass監査が残る | synthetic fixtureの誤判定は個別に検証。本物を例外にしない | 不可（自動変更しない） | 必須 | repository.security_and_analysis |

## 実在するcheck名と適用範囲

required_status_checksの4件はすべて`app_id=15368`（check API上`github-actions`）。legacy commit statusesは調査した5 SHAで`total_count=0`。設定のrequired contextを推測のjob/matrix名へ置き換えない。

| Check name（正確な値） | Current required | candidate runner / matrix | candidate event / skip | 実API根拠 | Proposal |
|---|---|---|---|---|---|
| `public history identity` | yes / strict | ubuntu-latest / matrixなし | PR main・main push・dispatchすべて実行 | main a8f5でsuccess; latest PRでもsuccess | 必須を維持。security regressionsはこのjobのstepであり独立check名ではない |
| `release source contract (macos)` | yes / strict | macos-14 / matrixなし | PR・dispatch・push message `[ci full]`時のみ。普通のpushはskip | main a8f5はskipped、PR run37228898432はsuccess | 必須を維持。source contract / Rust / native gatesを継続 |
| `auval arm64 (AU validation)` | yes / strict | macos-14 / arm64固定、matrixなし | 上と同じ | main a8f5はskipped、PR run37228898432はsuccess | 必須を維持。Intel/Universal/actual DAW受入を代用しない |
| `windows VST3 preflight` | yes / strict | windows-latest / x64、matrixなし | 上と同じ | main a8f5はskipped、PR run37228898432はsuccess; run37225487818はfailure | 必須を維持。candidateはunsigned VST3で、signed full/AAX release gateと別 |
| `licensed AAX SDK absence` | no | ubuntu-latest / matrixなし | PR main・main push・dispatchすべて | main/観測PRすべてsuccess | 候補公開後に実checkが常時出ることを確認してrequired追加を所有者判断。追加しなくても現workflowのgateは維持 |
| `AAX external SDK build (${{ matrix.runner_os }})` | no | remote old workflowのself-hosted OS matrix。candidateから削除 | optional dispatch条件により今回観測全件skipped。skipのためliteral expressionのcheck名のみ観測 | main/PR check API | requiredにしない。成功したexpanded OS check名は未観測、推測しない |

GitHub branch protectionはsuccessfulだけでなくskipped/neutralも許容する。通常main pushで3つskipのrunを署名候補のgreenとみなさない。既存`verifyCi`はexact head SHA / workflow path / completed / successに加えて4jobそれぞれ`conclusion=success`を明示的に要求する（`scripts/ls_release/hypha_release_local.mjs:53-68`）。この強いrelease-only gateを保つ。[Protected branch status semantics](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches)

job-level ifによるskipとworkflow全体のbranch/path filterによる非起動は別である。後者はrequired contextがpendingのままになり得る。将来needs付きaggregateを導入する場合、dependency failure/skipを隠さないalways()と明示的なfail-closed結果判定が必要で、今回未実装のaggregate名はrequired候補へ載せない。

stacked PR-BのbaseがPR-A branchの間はpull_request.branches:[main]に該当せず、このworkflowはPR-Bイベントで起動しない。必要なhosted確認は予算/重複runを照合し、明示承認後のexact B workflow_dispatchまたはmainへのretarget後で行う。source候補/ローカル結果をGitHub合格にしない。

旧workflowからのlicensed SDK matrix/private signing/real host・installer lifecycleは秘密・SDK・実機を伴うrelease-only gateであり、外部PRのrequired checkへ直接追加しない。既存4checksの実名を変えず、公開CIで署名credentialを要求しない。merge queue/merge_groupは導入されておらず、今回候補にしない。

## 観測runの実結果（再利用/新規起動なし）

最新12runs、先頭8runのjobs、mainとrecent4 SHAのcheck-runs/statusを読取った。最新PRは他作業で、開始・cancel・再利用していない。観測時点のin_progressを失敗または成功とみなさない。

| Run ID | workflow | SHA（短縮） | event | snapshot status / conclusion | Scope |
|---|---|---|---|---|---|
| 37248984870 | CI | `a5bb2234` | pull_request | in_progress / pending | 既存の別候補 |
| 37248984832 | AAX Phase A | `a5bb2234` | pull_request | completed / success | 既存の別候補 |
| 37248354391 | AAX Phase A | `a514a3db` | pull_request | completed / success | 既存の別候補 |
| 37248354388 | CI | `a514a3db` | pull_request | completed / cancelled | 既存の別候補 |
| 37228898485 | AAX Phase A | `3d74286f` | pull_request | completed / success | 既存の別候補 |
| 37228898432 | CI | `3d74286f` | pull_request | completed / success | 既存の別候補 |
| 37225487889 | AAX Phase A | `ef9c3a5e` | pull_request | completed / success | 既存の別候補 |
| 37225487818 | CI | `ef9c3a5e` | pull_request | completed / failure | 既存の別候補 |
| 37203771277 | CI | `350be497` | pull_request | completed / success | 既存の別候補 |
| 37203771255 | AAX Phase A | `350be497` | pull_request | completed / success | 既存の別候補 |
| 37196689449 | CI | `6df2915f` | pull_request | completed / success | 既存の別候補 |
| 37196689415 | AAX Phase A | `6df2915f` | pull_request | completed / success | 既存の別候補 |

上記のrun成功はlocal候補b1b3df84の合格ではない。署名factoryのrun名/pathは公開validatorの要求を確認しただけで、private run・artifactは取得していない。

## Server enforcementとsource guardの区別

- 実設定で確認済み: main strict4checks、App identity、admin適用、force/deletion禁止、auto merge無効、default read/PR approval権限禁止、public repo secrets/runners/environment absence、PVR有効。
- source候補の制約: unsigned/pending固定、secret referenceなし、hosted-only、SHA pin、persist-credentials:false、security regressions。source/testはPRで一緒に編集可能で、GitHub不変ルールではない。
- default readは初期値。same-repo write利用者がworkflow/job permissionsを書き換えることを禁止する上限ではない。fork PRの通常read ceilingと同一視しない。[Token calculation](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#how-permissions-are-calculated-for-a-workflow-job)
- first_time_contributorsは初回寄与者の実行承認。繰り返す外部寄与者まで承認待ちにする候補はall_external_contributors。実行承認はreview/merge/署名承認ではなく、workflow差分を読む必要がある。[Fork workflow approval](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/approve-runs-from-forks)
- write利用者はdispatch先refを選べる。24/40 tipの既存監査では21tipsに4 eSigner secret consumers、20tipsにoptional persistent SDK runner routeが残る。main整理のみで旧ref経路が無くなるとは主張しない。public repo credentials/runnersのabsenceを全refにわたり維持する。[Manual dispatch](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow)
- selected actions/full SHA policyは確認済みrepository設定候補（GETでfield利用可）。全refに影響するため旧ref再実行の可否を先に決める。repository full-SHA enforcementはActionsに対するもので、reusable workflowのtag参照まで一律禁止する保証ではない。candidateにreusable usesはない。許可action制限も`run:`による任意source実行を禁止するsandboxではない。[Actions permissions API](https://docs.github.com/en/rest/actions/permissions)
- private required-reviewer environmentはプラン条件を確認する。Free/Pro/Teamではrequired reviewersはpublic repoのみ。environmentを使ってもself-hosted hostは隔離されない。[Environment capabilities](https://docs.github.com/en/actions/reference/workflows-and-actions/deployments-and-environments)

## Security features / privacy確認限界

APIのsecret_scanning=disabledはrepo alerts設定の実測。GitHubのpublic partner scan一般論を理由に「安全/scan済み」としない。scanner結果自体は未取得。push protectionは既知secretのpush防止候補で全種類の秘密/PII検出を保証しない。[Secret scanning](https://docs.github.com/en/code-security/concepts/secret-security/secret-scanning), [Push protection](https://docs.github.com/en/code-security/concepts/secret-security/push-protection)

Dependabot alertsはAPI本文がdisabledと明示したため無効と判定できる。SBOM404は理由不明でdependency graphの状態は未確認。open alerts countは未取得。candidateのversion updates YAMLを追加しただけでalerts/graph/security updatesが有効になったとは言わない。[Dependabot alerts settings](https://docs.github.com/en/code-security/how-tos/secure-your-supply-chain/secure-your-dependencies/configure-dependabot-alerts)

fresh authenticated email metadata: fresh authenticated GET /user/emailsでprimary=1件; verified=True / visibility=private。addressはすべてredacted。 profile email=nullやprimary visibilityだけでKeep private/Block pushesがONと判定しない。CLI future commit noreplyとweb mergeのKeep private、Block email pushesは別確認。[Commit email privacy](https://docs.github.com/en/account-and-profile/how-tos/email-preferences/setting-your-commit-email-address)

## 次の判断

Owner判断: independent reviewer設計と1 approval/stale/last-push、fork実行承認範囲、selected action/SHA policyの旧ref影響、dependency graph取得/alerts/scanning/push protection、private factoryのplan/permissions/runner isolation/allowlist integration。stage2で設定変更は一件もしていない。既承認のPVR/email privacy変更はstage1で実施済み、fresh GETで確認した。
