# Stage 2 public CI / signing boundary — 2026-10-05

Local candidate `b1b3df847b3d9b9af021e09e69b982d7e8e6fa15`、remote main `a8f5a4a4a791cd4a81346e2c5462d08269619b16`。公開source/runbook/validatorとpublic repo GET snapshotを調査した。private factory、credential/local secret、Keychain、実機、productionには接続していない。設定変更・CI・build/test・署名・公開・commitは行っていない。

## 実在する境界とproducer

```
外部PR / same-repo PR / main push / manual dispatch
  → public hosted CI: unsigned Windows VST3 + public source/AU/SDK-absence checks
  → reviewed exact clean source/B + successful exact source CI (all 4 jobs)
  → existing trusted Windows signing factory / existing Mac Keychain-backed producer
  → actual Authenticode/PACE/Apple notarization + lifecycle + Native-only evidence
  → signed-full installer / 6 Mac bundles + hashes/provenance/host acceptance
  → release coordinator: exact candidate approval + packaging + 3 channels + postrelease
```

この図は既存contractを表す。private factoryのref制限・allowlist実適用順・runner隔離・token/secret/environment enforcementまで確認済みという意味ではない。公開CIの署名経路を取り除いても、secretや常設runnerが後からpublic repoへ供給されれば旧refsのconsumerが残る。

| ID | 境界 | 確認状態 | 実確認した制御 | 限界 / 未確認 | 公開根拠 |
|---|---|---|---|---|---|
| C1 | Public CI source candidate | implemented locally / not published | PR/push/dispatch hosted validation with contents:read, checkout persist:false, full SHA actions; Windows unsigned, external validation pending, no signing input/secret consumer; no public self-hosted AAX job. | Editable source/tests; repository read default is not a same-repo permission ceiling. | `.github/workflows/ci.yml:3-12`; `.github/workflows/ci.yml:419-446`; `.github/workflows/aax-phase-a.yml:3-33`; `scripts/ci_security.test.mjs` |
| C2 | Global public repository supply | verified GitHub GET snapshot | Repository secrets, variables, registered self-hosted runners, environments count 0. Organization-owned policy scopes N/A for this user-owned public repository. | No proof about past values, removed credentials, runner files, private factory runner groups or machine access. | `actions/secrets`; `actions/variables`; `actions/runners`; `environments`; `owner.type=User` |
| C3 | Old reachable workflow refs | prior exact all-ref audit retained | 64 tips/84 workflow instances/31 blobs. 21 tips retain four named eSigner secret receivers; 20 tips retain optional persistent AAX SDK runner leg. | New main cleanup does not rewrite those refs or remove dispatch/rerun ability. No public signing credential/runner registration must be maintained globally. | [all-ref workflow audit](public_repo_exposure_audit_20261005.md) |
| C4 | Reviewed source entering factory | documented contract; private enforcement unverified | Exact clean commit/B, source CI four jobs green, reviewed executable distribution source, private factory SHA-256 allowlist, tracked installer script not rewritten after checkout, licensed SDK outside public repo. | Private workflow code revision/input ref constraints/allowlist entries/check order/admin bypass and permissions were not accessed; docs are not server attestation. | `docs/ls_release/kirin_hypha_ls_runbook.md:48-75`; `docs/aax_build_signing_entry.md:20-30`; `docs/aax_windows_build_20260910.md` |
| C5 | Release source CI acceptance | existing public executable validator | GET run requires exact candidate head_sha, ci.yml, completed and success; separately exact four public jobs must conclude success. No auto dispatch/retry. | Required GitHub check semantics allow skipped jobs, unlike this validator. Public validator source remains editable. | `scripts/ls_release/hypha_release_local.mjs:53-68` |
| C6 | Installer/AAX metadata and byte continuity | existing public executable validator | Actual EXE SHA matches checksum and manifest; exact version/source/B; trusted origin allowlists; 4/6 complete signature records; PRE/POST payload hashes; signed AAX sidecar hash/source/distribution/Native-only stamp; upgrade/reinstall/uninstall/external report bindings; public_ready true required. | Signature/lifecycle facts are read from mutable JSON; this Mac-side validator does not itself execute Windows Authenticode/PACE on EXE bytes or independently authenticate sidecar producer. | `scripts/ls_release/build_kirin_hypha_release_set.mjs:106-320` |
| C7 | Actual Windows signature/lifecycle execution | existing public producer/verifier; candidate execution unverified | Get-AuthenticodeSignature checks real installed executable surfaces; unsigned requires NotSigned, signed requires Valid; exact payload hash equality; AAX additionally wraptool local verify and expected publisher; signed mode demands prior-release upgrade candidate. Install/reinstall/upgrade/uninstall and unrelated sentinels preserved. | No Windows host or signing run was executed in Stage 2. Valid signing metadata in a receipt is not equivalent to rerunning actual verifier. | `scripts/windows/verify-installer.ps1:42-65`; `scripts/windows/verify-installer.ps1:114-168`; `scripts/windows/verify-installer.ps1:255-291`; `scripts/windows/windows-aax-bundles.mjs:150-166` |
| C8 | Factory workflow freshness | existing public coordinator API validator | Source CI run must equal selected exact-commit CI; referenced factory run is fetched and must be completed/success with .github/workflows/hypha-windows-signing.yml. | Reviewed function does not fetch factory artifact digest/attestation/download or bind local EXE hash to a GitHub artifact returned by that run. Private artifact ingress/origin enforcement remains unverified. | `scripts/ls_release/hypha_release_local.mjs:71-88` |
| C9 | macOS signing/notarization | existing public scripts/runbook, secret store uninspected | Unsigned diagnostic / diagnostic-sign no distribution / clean-source distribution --sign are distinct. PACE+Developer ID and Apple Accepted v3 receipt bound to submitted archive/bundle source hashes; installer certificate separate. | Keychain/authorization/current cert and notary profile were not inspected; old successful evidence cannot qualify new candidate. | `docs/aax_build_signing_entry.md:20-25`; `docs/aax_build_signing_entry.md:33-59`; `docs/aax_build_signing_entry.md:63-76`; `scripts/ls_release/hypha_release_local.mjs:43-50`; `docs/ls_release/kirin_hypha_ls_runbook.md:172-204` |
| C10 | Explicit publication and 3-channel acceptance | existing client-side executable contract + operator procedures | Exact candidate publication authorization; source/frozen evidence checked during each stage; Windows signed-full, Mac six bundles, host/A5/LS/GitHub/HP/A7 gates retained, failures/checkpoints stop advancement. | Client-side authorization/source checks can be edited by authorized local actor. GitHub Release/token and HP/LS/private host access enforcement not inspected. | `scripts/ls_release/hypha_release_contract.mjs:146-149`; `scripts/release_hypha.mjs:183-246`; `docs/hypha_release_entry.md:35-50`; `docs/hypha_release_entry.md:79-120` |

## BEFORE → AFTER / blockedとresidual

「AFTER」はlocal実装と現server設定を分けている。remote mainにはまだ候補sourceが掲載されていない。新H2は検出していないが、private側値の安全/rotation済みを証明したものではない。

| Threat | BEFORE | AFTER | 残る条件 / server確認 | Evidence |
|---|---|---|---|---|
| T1 外部fork PRからsigner credential取得 | legacy installer stepはsecret envを無条件に受け取るsource。fork以外same-repo経路もある。actual public secrets=0 | candidate CIはsecret受取がなくunsigned。GitHubの通常fork token read制限＋repo secret absence | 今のpublic repoから署名credentialは供給されない。ただし旧refは残るので登録し直さない。default readをsame-repo上限と主張しない | C1/C2/C3 |
| T2 PR変更で常設SDK/signing host侵入 | old AAX dispatchにoptional self-hosted OS matrix。現在registered runner=0 | candidateはhosted absence gateのみ。actual public runner0維持 | 別private factory runnerのrepo/group/host isolationは未確認。envだけでは常設hostの隔離にならない | C1/C2/C3/C4 |
| T3 同一repo branch PR / workflow変更でwrite取得 | default readでもwrite利用者はworkflow権限を要求可能。review count0 | candidateはcontents:readとsource regression。main PR/check保護・admin enforcementは現設定 | source/testを同時変更できるためserver権限上限ではない。独立review導入と任意action許可縮小はowner判断 | C1/C2 + baseline |
| T4 old branch/tag dispatchで署名復活 | 21tips secret receiver /20tips persistent runner route。write利用者はref選択可能 | main候補を整理しても旧refは保持。global no signer credentials/no public persistent runnerを維持 | SHA policyを有効化すると旧mutable refsが実行不可になり得る。rewriteせず実行運用をowner判断 | C2/C3 |
| T5 未審査sourceがprivate factory credentialに接触 | factoryはtracked installer scriptを採用sourceから実行する契約 | existing allowlist /clean exact source / CI4success contractは保つ。通常PRのunsigned結果だけでsignerへ進めない | factory allowlist検証がcheckout code実行より先か、ref入力/reviewer/admin/token範囲は未確認。任意ref実行なら隔離名だけで安全ではない | C4/C5 |
| T6 PR artifactの差替え / artifact poisoning | untrusted public build artifact自体はrelease-readyではない | version/commit/B/source run/hash/PREPOST/AAX/signature/lifecycle/external-report gateが異なるsource混入をreject | local directory自体がattacker-controlledならself-consistent fake receiptはauthenticity証明でない。approved factoryからの取得とtrusted hash/origin照合が必要 | C5/C6/C7/C8 |
| T7 green/skip CIだけでmergeまたはrelease | GitHub required checksはskipも許容。auto merge=false; review0 | existing 4check names保持。release verifyCiは全4jobsuccess。signed/full/host/manual publication別gate | workflow definitions/editable reportsの信頼は独立review/producer accessに依存。CIだけでreleaseは進まないがserver publication permissionは未確認 | C5/C10 |
| T8 action tag差替え / malicious third-party workflow | 全old ref mutable action uses、server allowed_actions=all/sha=false | candidate full SHA pins+reviewed updates Actions/Cargo、contents read | pinはaction実bytesを固定するが内部で取得するtool/dependency全体や任意run sourceの安全を証明しない。server selected/SHAは未適用候補 | C1 + baseline |
| T9 secret log/artifact残存 | 過去consumer pathから露出可能性。stage1 secret value未検出 | public CI signer受取なし。実factory側値はprivate runtime供給するcontract | private logging/credential cleanup/old logs/revocation実態は未確認。新H2は値redactedで停止しrotation/revoke優先 | C2/C4/C9 |

## Trusted source: factoryへ渡す前の確認

既存Runbookは署名validation自体を公開承認と区別し、clean exact source/B、tracked executable filesの私的SHA-256 allowlist審査、checkout後にinstaller/sign-hookを無断書換えしないこと、既存4 CI job、prior-release upgradeの固定を要求する。今回はこのcontractを弱めず、ownerのprivate integration確認を残す（`docs/ls_release/kirin_hypha_ls_runbook.md:48-75`）。

未確認のserver/factory項目は、実workflowが許すevent/input ref（default branch以外の任意sourceを取るか）、source commit/allowlistを**採用source script実行より前に**検証するか、private workflow/action revisionのpin、credentialを受け取る最小step、runner/group/repository access隔離、persistent hostの残留資格情報、publication write token別境界、environmentのplan・review・bypassである。必要ならowner承認の別private監査で確認する。private factoryの存在だけをsecret boundaryの証明にしない。

GitHub environment secretsは承認ruleが実在すればそのjob承認後に利用可能になる。private required reviewersはFree/Pro/Teamの対象外なのでplan確認なしに利用可能と提案しない。environmentはself-hosted runner OSの隔離機構ではない。[Environment limitations](https://docs.github.com/en/actions/reference/workflows-and-actions/deployments-and-environments)

外部fork tokenの通常read制限と、同一repo/write利用者がjob権限を変更できる経路を区別する。source guard・CI成功・App identityだけで、レビューされていない同一repo sourceの実行権限が不変になるとはしない。[Permission calculation](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#how-permissions-are-calculated-for-a-workflow-job)

## Signed artifacts: public verifierが実際に証明すること

`requireWindowsInstaller`はEXEの実bytes/hashとsidecar2種の一致、exact release identity、trusted run origin、4/6surfaceの署名記録、installed payload hash、AAX signed sidecar、Native-only/Audiosuite OFF、installer lifecycle/external-report条件を確認する。origin allowlistは観測可能な内部topologyのH1例外だが、security contractとして保持する。実際のowner/private project識別値は本資料へ再掲しない（source: `build_kirin_hypha_release_set.mjs:222,313`、fixtures: `release_metadata.test.mjs:392,408`）。

`verifyWindows`はselected exact public CI runとmanifest source runの一致に加え、factory runをGETしcompleted/successと正規workflow pathを確認する。private factory repoのHEADは別sourceなのでpublic head_shaと無条件一致させるような未検討変更をしない。採用public sourceのhash bindingはmanifest/provenanceとtrusted producer contractで要求される。

条件付きの残余: 読んだMac側acceptanceはWindows Authenticode/PACEの再実行ではなく署名/lifecycle記録の検査であり、factory run APIとlocal EXEをActions artifact digest/署名attestationで結ぶ取得処理も当該`verifyWindows:71-88`にはない。attackerがlocal artifact directoryと全receiptを差替えられる場合、自己整合するchecksum/JSONと既存green run URLだけではそのbytesのproducerを証明しない。実際の署名検査はWindows側`verify-installer.ps1`とAAX loaderに存在し、そのtrusted実行結果を正規factoryから取得して使用する前提である。これはコード/受入入口の信頼条件の指摘であり、実exploit/production侵入を確認したという報告ではない。

Owner確認候補: 既存factory artifactをrun固有に取得する経路、GitHub返却artifact metadata/digestが利用可能な場合の照合、trusted runに保持されたhashと受入bytesの照合、real Windows signature/PACE受入のpublisher/証跡。artifact attestations等は当該private plan/実装の利用可否が未確認であり、実装済み扱いまたは必須の仮想serviceにはしない。現在のorigin/hash/signature/lifecycle gatesは一切緩めていない。

## Public CI / release-only gatingを維持する

公開PRに必要なのはunsigned buildと製品契約検証で、signing/SDK credentialや正式配布tokenを要しない。candidate retains the exact stable step identifier `Build Windows installer and sign all executable surfaces` for existing provenance consumers; YAML comment explicitly calls it a legacy identifier and execution is always unsigned. Cosmetic identifier変更によるprivate verifier hash変更は戻しており、この名前は署名実行の意味ではない。

GitHubのexisting4required checksを保持し、SDK licensed buildやprivate signing jobを外部PRrequired checksへ追加しない。観測したAAX optional job名はskip状態のliteral matrix expressionであり、成功matrix OS名を推測してrequiredにしない。source security testは`public history identity`のstepであり、まだ独立check/不変serverルールではない。

通常pushのskipをreleaseの4job成功へ読み替えない。stacked PR-BのbaseがA branchならmain対象PR triggerは非起動。CI受入は承認・重複確認を行ったexact candidate dispatchまたはmain retarget後の実runを要する。Stage 2ではCIを起動していない。

レビュー前codeを署名machineで実行しない、public-origin artifactsを署名inputとしてそのまま信頼しない、CI成功だけで公開しないという方針は既存source/私的運用/owner権限で分かれる。完全に達成済みのserver enforcementとして記述しない。[GitHub secure-use reference](https://docs.github.com/en/actions/reference/security/secure-use)

## Owner decisions / unverified

1. Public repoには将来もsigner credentials、credential相当variables、SDK/signing persistent runnersを登録しない。old refsはH0/H1履歴として残り、新mainのcleanupだけで消えない。
2. Private factoryでsource pin/allowlist/permissions/runners/approvalが実適用されているか確認する。公開Runbookと照合し、候補sourceのtrusted-file hash更新が必要ならprivate integrationでreviewする。今回private設定を書き換えていない。
3. Artifact directory/receiptの正規factory取得とhash originを確認する。適用済みproducer証拠なしで「署名済み/ready」と言わない。
4. Public GitHub settings候補はbaseline表参照。review count増加のsolo block、old refsのSHA policy blockを先に決める。CODEOWNERS/環境/外部serviceを実在確認なしに増設しない。
5. 新credential発見時は値を転載せず `SECRET DETECTED — value redacted` として停止・revoke/rotationを優先。Stage 2でcredential値は取得していない。履歴rewrite/old artifact削除も行っていない。

## Runner / credential capability decision table

| 境界 / capability | 実測 / 利用可能性 | 恒久対策 / 確認項目 | 今回の実装 |
|---|---|---|---|
| Public PR → persistent trusted runner | 公開repo登録runner0、labels0。旧refにrouteは残る | 公開repoへ常設SDK/signing runnerを登録せず、私有runner/groupのrepo ACLで公開repoを許可しない | candidate hosted-only、server登録0を確認。私有ACL未確認 |
| Persistent workspace / signing material | runnerが0のため公開repo経由で現hostを検査できない。私有factoryのworkspace/material残存は未確認 | 採用source実行前のallowlist確認、credential最小step供給、残留material/ログの扱いを既存運用と照合 | 私有machine操作なし |
| Network reachability / repository scope | owner User、public runner repo一覧0。私有hostのnetwork/scope未確認 | 公開PR codeを私有hostで実行しない。factoryのnetworkとGitHub repo/group accessを別監査 | no new runner/network rule |
| Ephemeral runner | 私有factoryのrunner方式・ephemeral化能力は未確認 | 利用可能性を確認できた場合に隔離と使い捨てworkspaceを検討。labelやenvironmentだけを隔離としない | 仮想runner/serviceを増設していない |
| Public signing secret removal | 現repository/env secrets0、variables0 | 旧refのconsumerへ再供給しない。削除すべき実secretは今回確認されていない | workflow全4bindings除去候補。secret削除操作なし |
| Private factory / source allowlist | 既存Runbookとpublic verifierに実在する | clean exact sourceとreviewed executable allowlistをsource script実行より先に確認。私有側実適用はowner確認 | contract保持、private変更なし |
| Short-lived credential / protected environment | private credential lifetime、provider機能、GitHub plan、reviewerは未確認 | 利用可能なprovider/planを確認した後の候補。現行credentialを勝手にrotation/migrateしない | 未実装・利用可能と断定しない |

通常CIのsource hardeningと、古いrefを含むrepository-wide credential/runner供給禁止を両方維持する。私有boundaryの残存リスクを未確認として残し、workflow削除だけで恒久解決したとは判定しない。
