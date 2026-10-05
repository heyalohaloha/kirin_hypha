# Security hardening review packet — 2026-10-05

状態: ローカル候補。main未merge、push/PR/new GitHub CI未実施。第三者が理由と残存リスクを評価する入口。秘密値、個人email、実home pathを再掲しない。

## 1. 前段監査と今回の追加範囲

前段は公開24 branches・40 tags・到達可能1,323 commits、tracked files、31種類のworkflow、73 PR/Issue recordsと取得できたコメント、40 Releasesのmetadata/195小型sidecars、Actions inventory/4 runsのlog、最新3配布物を確認した。全過去binary・全Actions payload/log・private factory実機は未確認。新しいfull secret scanを繰り返さず、保存済み証拠、固定Git objects、最新GitHub設定、Mach-O sectionとcompiler fixture、独立agent diff review、最終local gateを追加する。

前段監査と全workflow/ref inventory: [exposure audit](public_repo_exposure_audit_20261005.md)。Credential/private key/token/passwordのH2は確認範囲で未検出。値を見つけた場合は転載せず停止してrotation/revoke優先へ戻す。

## 2. 元問題と遮断点

| Severity | 変更前の経路 / 影響 | 候補の遮断点 | 残存リスク |
|---|---|---|---|
| High | 通常CIのinstaller stepへeSigner 4 secret bindings。same-repo PR/dispatch/full pushのuntrusted source実行に将来secret追加が重なると署名credentialへ到達可能 | bindings/Java/CodeSignTool/signed dispatchを除去。公開CIはunsigned/pending。repo secretsは現在0 | 古いrefsへ再追加secretを渡せば危険が復活。editable workflowやsource guardはserver権限の上限ではない |
| High | 公開AAX manual workflowが常設SDK runnerをlabel指定できる | 公開SDK/self-hosted routeを除去しhosted SDK absenceのみ。repo runnersは現在0 | 私有runnerを公開repoへ再接続しないことをrunner側ACLで維持。私有factory実機は未確認 |
| Medium | mutable third-party Actions、token保存、permissions省略 | 完全SHA pin、contents read、checkout persist-credentials false、review-driven更新 | hash pinはAction自体の安全性の証明ではない。同一repo workflow editorは変更可能 |
| Medium | 公開repoのAI/個人運用・private storage/識別path、作者metadata | PR-Aで公開contributor契約へ整理、秘密のないtemplates/ignore/PVR、metadataだけ除去 | 過去refs/PR/ReleaseのH1は残る。正式binaryのfuture producer対策は未実装 |
| Informational | 独立review required数0、security scanning無効 | 最新設定とowner判断を文書化 | 設定は今回変更していない。local peer reviewはGitHub上の独立人間approvalではない |

具体的境界・公的origin/hash gateを残す理由: [signing boundary](signing_trust_boundary_20261005.md)。

## 3. PR-A: Public Repository Hygiene

Baseは監査時main `a8f5a4a4a791cd4a81346e2c5462d08269619b16`。stage1 A tipは`d27e1638a7a0c1fada7f7a317a09a124b52ab42c`。今回H1 matrix、binary future plan、全file change manifestをforward commitで追加する。

全変更fileと処置理由: [PR-A manifest](public_hygiene_change_manifest_20261005.md)。AGENTS/CLAUDE、contributor/security文書、private operation/path cleanup、gitignore、PNGの個人編集metadataが対象。product DSP/audio/measurement/UI/API/layout/file format、licence/version、release producersを変更しない。workflow-discovery testは削除した私有heading依存を公開入口の技術契約へ合わせるだけ。

7 LICENSE textsおよびlicense-engine sourceの計8関連filesと10 test WAVsは元mainとbyte一致。元test signal作者とAI artwork attributionは保持。PNG decoded pixels/IDAT/非metadata chunks一致。内部情報を別の公開internal.mdへ移さず、元資料の退避先は公開Git外のprivate archiveとした。恒久private運用文書の移管はowner判断が必要。

## 4. PR-B: CI / Security Hardening

stage1 B `b1b3df847b3d9b9af021e09e69b982d7e8e6fa15`の14 filesに今回のsigning boundary/settings baseline/review evidenceを加える。A-finalをBへ通常mergeし、既存commitを保持する。A-finalをbaseとするB diffにA hygieneを混ぜない。

CI両YAML、実在Actions/Cargo Dependabot、security boundaryの負例、xtask/preflight/readinessの契約を変更。通常CIから4bindings/署名input/常設SDK runnerを除去しunsigned/pendingを明示。required4 jobs、pluginval、audio transparency、native/installer lifecycle、artifact識別子とrelease verifierのorigin/hash checksを保持する。下流provenance互換性のため歴史的step名 `Build Windows installer and sign all executable surfaces`は保持するが、実行はalways unsignedである。

Actionsの完全SHAとtrade-offはbaselineに記録。CODEOWNERSはsole ownerかつcodeowner approval未要求の現状では独立reviewにならず、今回導入しない。自動mergeは導入しない。

| File | Read-only review disposition |
|---|---|
| .github/dependabot.yml | reviewed updates only for present ecosystems; no automatic merge |
| .github/workflows/aax-phase-a.yml | hosted absence gate unchanged; public manual SDK/self-hosted route removed; contents read and checkout SHA/persistence boundary |
| .github/workflows/ci.yml | public validation only unsigned/pending, no sign secrets/manual signer, read permissions/SHA pin, security check, env-safe workspace path; other gates retained |
| README.md | reflects source/public CI versus separately trusted signed release boundary without changing product or acceptance contract |
| docs/aax_phase_a_readiness_20260907.md | removes obsolete public self-hosted manual matrix description; local SDK scripts/separate trusted factory and host gates remain |
| docs/ls_release/kirin_hypha_ls_runbook.md | public unsigned CI route and separately trusted signing/source promotion described; formal all-channel acceptance preserved |
| docs/security/public_repo_exposure_audit_20261005.md | records changed security boundaries and evidence, not product source |
| scripts/ci_security.test.mjs | new nine source-policy tests; supplementary to review/YAML/server boundaries, not an authorization system |
| scripts/ls_release/aax_distribution.test.mjs | public hosted absence/no-selfhosted route assertions; retains local SDK scripts and production distribution gates |
| scripts/windows/windows_installer.test.mjs | public unsigned/no-secret/no-manual signer assertions; production eSigner and package tests remain |
| xtask/src/ci_usage_guard.rs | test mutation target refreshed after SHA pinning; production guard unchanged |
| xtask/src/windows_preflight.rs | enforces unsigned/pending public source and immutable artifact upload while preserving layout/package/validation gates |
| xtask/src/windows_preflight/tests.rs | adds negative public signing/secret/mutable-ref cases; existing source/layout gates retained |
| xtask/src/windows_readiness.rs | readiness adds pinned-upload negative test; upload presence check no longer hardcodes mutable action version |

Stage2 B追加: `signing_trust_boundary_20261005.md`、`github_repository_security_baseline_20261005.md`、本packet、最終local validation evidence。すべて判断/検証文書で、stage1 B実行sourceは変更しない。

## 5. H1 / binary判断

[H1 decision matrix](public_repo_h1_decision_matrix_20261005.md)はremote main残存とlocal候補除去を区別し、情報種別ごとに到達ref・PR/Release/log/asset coverage、第三者が分かること、再生成、推奨処置、rewrite副作用とowner判断を具体化する。

限定的な過去path/checkout名/credential-free操作メモを理由とするrewriteは勧めない。個人emailは既存作者帰属を保ち、今回新commitは承認済みnoreply。重大PII/継続攻撃の直接材料/H2の新事実があれば再評価する。

最新Mac4 binaryのhome registry pathは各164箇所、全件`__TEXT/__cstring`。PKGは同一hash。debug stripでは足りず、Rust/C++/linker producer全段の将来正規化を提案する。[binary plan](binary_path_normalization_plan_20261005.md)。4回のRust/Clang fixtureは成功したが、shipping flags変更や新releaseを完了した証拠ではない。既存assetは保持する。

## 6. GitHub設定 / required checks

[security baseline](github_repository_security_baseline_20261005.md)が最新read-only API evidence、Current/Recommended/影響/承認の正本。

既存main required contextsは次の4実名で、GitHub Actions app 15368、strict=true。local test名から作っていない。

- `public history identity`
- `release source contract (macos)`
- `auval arm64 (AU validation)`
- `windows VST3 preflight`

これらを維持し、候補の新GitHub CIで各jobが実行・成功したことを確認する。`licensed AAX SDK absence`のrequired追加はowner判断。消えたSDK matrixやrelease-only signature/host受入を通常PRのrequiredにしない。

job skipがmergeを防がない場合とworkflow全体のbranch filterによるpendingを区別する。[GitHub required checks説明](https://docs.github.com/en/pull-requests/how-tos/merge-and-close-pull-requests/troubleshooting-required-status-checks)。stacked B PRをA branchへ向けると現行`pull_request.branches: [main]`対象外。[target branch filter](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax)。候補Bの新manual CIは明示承認とexact ref照合後に行うか、A merge後main向けBで実行する。旧runのgreenを本候補へ転用しない。

## 7. 最終local test evidence

最終候補の実行command・結果・失敗/再実行は、追記する検証記録を参照する。前段のPASSを今回再実行のPASSと混同しない。private raw logsは公開repo外に保管する。

## 8. 未確認・保留理由

- exact候補のGitHub checks/new CI: 未publishでrunなし。local PASSと同一視しない。
- arm64 AU実環境/Windows host・圧縮payload: このMacのlocal source gateで代用できない。通常CI/正式release受入で確認する。
- private signing factory: ACL、runner workspace/material/network、credential lifetime、artifact producer attestationの独立取得は未確認。public verifierはdirectory/receiptsがtrusted factoryから渡る前提を持つ。
- 全過去binary/Actions artifact/log: inventoryと選択payloadだけを確認。未取得を問題なしへ変換しない。
- email privacy UIのkeep-private/block-personal-pushは未確認。API primary visibility privateと過去commitの残存は別問題。

## 9. Owner判断（Yes/No粒度）

1. 限定的H1について既存Git/PR/Releaseを保持し、現行整理とfuture-only改善で進めるか（推奨Yes）。H2の新事実は別扱い。
2. 公開repoへsigning secrets/常設trusted runnerを再追加せず、私有factoryのrepo ACL・exact commit受入・成果物搬送を独立監査するか（推奨Yes、factory変更は別承認）。
3. 各producerのpath正規化実装を別taskで行い、既存assetを差し替えず次の正式候補で検証するか（推奨Yes）。
4. security scanning/push protection/Dependabot alertsを有効にし、required SDK absenceとActions SHA enforcementを採用するか（個別Yes/No。設定変更の承認は今回未取得）。
5. 独立reviewerを指定してrequired approval 1、stale dismissal/last-push approval/CODEOWNERSを設定するか（reviewer確保後の判断。sole ownerをself-review不能にしない）。
6. 検証済みlocal A/B branchesをpushしdraft PRを作成し、必要な新CIだけを実行するか（public送信/CI予算の承認が別途必要）。

既承認のPVR有効化、primary visibility private、今回commit noreplyは完了済み。history rewrite、force push、既存tag/asset変更、secret削除/rotation、factory変更、rulesets/bypass変更、release/HP/LS/main mergeの承認と読み替えない。

## 10. 独立reviewの範囲

A cleanupを一部実装したhygiene agentのpreservation self-reviewと、Aを実装していないCI agentの独立read-only reviewを区別する。B workflow/guardを実装していないhygiene agentは固定d27→b1の全14 filesをreviewし、17 SHA-pinned uses、5 checkout token非保存、unsigned3箇所、hosted SDK absenceの6 commands一致、8 producers一致、条件/timeout/upload数の保持を確認した。新たなmaterial defectは指摘されなかった。H1のsnapshot/ref coverage、最新settings、実signingコード/receipt前提のread-only確認も別agentで実施した。これは人間security reviewerの承認、別GitHub accountのapproval、private factory実機監査ではない。最終document/doc-link/privacy reviewの結果を検証記録に追記する。

## 11. Merge後検証

順番: owner/human review → A/Bそれぞれ新候補checks → owner merge判断 → APIでmain SHA/settings再照合 → merged sourceのCI結果。公開repoのsecrets/runners/environmentが空のままで旧refから権限が復活しないことを確認する。本文/metadata・generators・unsigned artifactsをscanし個人path再生成がない範囲を確認する。正式signed releaseは別境界と全既存release gateを通す。

branch protectionの変更はowner承認後のみ。strict checksとreview受入を省略しない。PVRのprivate窓口はmaintainer権限で再確認するが、テスト報告をpublic issueへ投稿しない。

## 12. Rollback条件

CIの実host gate、packaging識別子/provenance互換、公開build/docs契約が壊れた場合は該当候補をmergeせず調査する。merge後に発見したら通常revertでレビューし、DSP等へ広げない。Hygiene rollbackが私有情報を再公開する場合は元private textを復元せず公開契約だけを修正する。署名binding/常設runner経路の復活はrollback手段にしない。credential発見時は停止し失効・rotation判断を優先する。force push、history rewrite、既存Release操作で解決しない。
