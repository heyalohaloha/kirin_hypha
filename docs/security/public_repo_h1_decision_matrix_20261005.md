# H1判断表 — 2026-10-05（値を転載しない）

Stage1の分類と証拠を引き継ぎ、H1の将来処置と履歴判断を分ける。限定的な旧path・nickname・内部メモ・build pathについて、利用者の今回の方針は「履歴rewriteをしない」。個人メールと画像の照合可能な識別子は個別に評価する。以下のYes / Noは判断欄であり、この文書がrewriteや設定変更を実行する許可ではない。新たなH2は発見していない。既知のPhase1 embedded HMAC defaultは意図して公開されるprotocol/default値（H0、private signing credentialではない）であり、既報のsecurity limitationを維持する。

確認した公開main：`a8f5a4a4a791cd4a81346e2c5462d08269619b16`（Stage2 fresh APIでも同じSHA、保護あり）。PR-A：`d27e1638a7a0c1fada7f7a317a09a124b52ab42c`。local security候補：`b1b3df847b3d9b9af021e09e69b982d7e8e6fa15`。いずれのcandidateも、この判定時点では公開mainに反映されていない。公開mainから既に消えたとは報告しない。

64 refsは2026-10-05の前回API snapshot（24 branches / 40 tags）であり、Stage2に同時刻の全ref一覧を再取得していない。existing local objectsで各snapshot commitを解決し、fetchをせず到達可能性を計算した。63 cached refsはsnapshot SHAと一致。`origin/claude/hypha-abcv-h`のみcached tip `a5bb22347e59cd20cb6c00599aa88b5a97b229b4`、snapshot tip `3d74286f7635bdbdaab9b3681948f830aa78351d`と異なる。両objectは存在し、表のcoverageはsnapshot tipを使う。この差を「欠落履歴なし・全refs現時点同期」と読み替えない。

machine-readable evidenceは公開Git外のprivate archiveに保存。公開のoriginal/candidate行対応は[全変更・location ledger](public_hygiene_change_manifest_20261005.md)を参照。Aの分類locationは605 overlapping rows、前回actual-owner history407 rows、retained-origin5 rowsを合わせ1,017 location rows。混合semantic blockはH0の技術contextを含む。row数は個人情報値の件数でも人数でもない。分類categoryは全てlocation appendixへ対応させる。原文、個人メール、home名、private URL、metadata識別子、秘密値を転載しない。

coverageは、既に分類したblobとexact original-line variantsの集合に対する事実。tip =該当known blobがtreeに存在、ancestor=そのblobが到達可能。PE01だけはcommit metadata、H1-09だけはRelease binaryなので別扱い。known anchorの不一致は、未知のH1が無い証明ではない。日付範囲はknown blobを更新したreachable local commitのauthor datesであり、全categoryの最初の露出/最終流通日を断定しない。各ref内rangeはJSONに記録する。

## 共通の公開面確認と限界

|面|確認した実範囲|結果と未確認|
|---|---|---|
|PR / Issue|73 PR title/body、73 Issue API records（全てPR-backed、通常Issue 0）、2 issue comments、review comments 0|既知PE01/email・owner-home anchorsの対象絞り照合0。前回のbounded candidate scanも0。全PR diff、隠れたreview、添付/外部linkの本文を網羅した判定ではない|
|Release metadata|40 releasesのname/body、195 small checksum/JSON sidecars|既知personal anchors0、前回candidate scan0。source archiveは該当旧tag treeを含む。全旧binaryを確認していない|
|Release payload|v1.1.50のofficial-checksum一致したPKG/ZIP/EXE計167,223,551 bytes|Mac ZIP/PKGをread-only展開・scan、ownerCargo-pathあり(H1-09)、原画本体混入検出なし。Windows EXEはraw ASCII/UTF16のみ、compressed payload未展開|
|Actions logs|run 37077361064 / 37228898432 / 36860903973 / 36860907827 の4logs、20 files|598 raw path candidatesを手動分類しhosted runner/runneradminのH0。owner path/credible credentialなし。全860runのlogは未確認|
|Actions artifacts|909artifact metadata、860run metadata|artifact payloadは未確認。expired表示だけで内容消去・完全非公開とは判定しない|
|外部残存|GitHub forks/caches/backups、第三者clone、検索index、private account UI、他旧binary約2.7GB|未確認。履歴rewriteだけで回収・消去できると断定しない|

表中「公開面」は上記共通範囲に限定される。PR/Issue/Releaseの照合0は本文title/bodyの既知anchorについての結果であり、H1-03〜07の全語句、画像metadata、全PR diff・asset・logに対するゼロ判定ではない。実credentialが見つかる場合はH2へ上げ、値を出さずrotation/revoke優先で停止・報告する。

## 一覧

|ID|分類|public main → local A|既知ancestor refs / tip refs|推奨history処置|
|---|---|---|---|---|
|H1-01|個人commitメール（PE01）|metadata保持 → future noreply|64/64 / 63/64|個別判断、既定rewriteなし|
|H1-02|個人home・user・machine path|あり → 整理|64/64 / 22/64|keep / rewriteなし|
|H1-03|過去checkoutのnickname・隣接checkout誘導|あり → 整理|16/64 / 16/64|keep / rewriteなし|
|H1-04|非公開の検証資料・一時fixture・隔離先storage|あり → 整理|59/64 / 59/64|keep / rewriteなし|
|H1-05|個人cloud運用通知|あり → 整理|20/64 / 20/64|keep / rewriteなし|
|H1-06|AI・Notion・個人operator・session申し送り|あり → 整理|64/64 / 64/64|keep / rewriteなし|
|H1-07|非公開repository・署名factory・販売/広報topology|あり → 整理|59/64 / 59/64|keep / rewriteなし|
|H1-08|原画の生成platform account/job/style/time metadata|あり → 整理|64/64 / 64/64|個別判断、既定rewriteなし|
|H1-09|既公開macOS binaryのビルド元home path|既存asset残存 → 今回変更なし|assetのみ（source ancestor14/64）|keep / rewriteなし|
|H1-10|意図して残すorigin allowlist・署名provenance識別子|保持 → 保持|21/64 / 21/64|keep / rewriteなし|
|H1-11|技術上不要な個人承認・担当者帰属|あり → 整理|64/64 / 64/64|keep / rewriteなし|
|H1-12|既削除の内部methodology文書|既削除 → 再導入なし|3/64 / 1/64|keep / rewriteなし|

## H1-01 — 個人commitメール（PE01）

**重要度 / 種類：** Medium（privacy、強い照合可能性）。H1。

**現在・候補：** 公開mainのcommit metadataと過去履歴に残る。PR-Aでは作者情報・履歴を変更しない。新しいローカル候補commitは承認済みGitHub noreplyを使う。

**確認事項 / 第三者推論：** 同一の個人メール1件をauthor 1,243 commits、committer 1,118 commitsで確認。重複を除くaffected commitは1,243件。これはlocalで到達可能な全commit metadataの計数であり、後述のsnapshot refごとの件数はJSONを参照。公開氏名とメールが結び付き、連絡先収集・spam・他サービスとの照合に使われ得る。credentialやaccount recovery可能性の証拠はない。

**location：** Git commit author/committer metadata（PE01）。メール値は記載せず、affected commit full SHA・dates・分類のみJSONにある。

**観測履歴範囲：** `2026-04-19T11:33:14+09:00` / `5ef05c51f848d18715bba98003c742d457855e2c` 〜 `2026-10-05T08:52:14+09:00` / `a5bb22347e59cd20cb6c00599aa88b5a97b229b4`。commit author dateの観測範囲であり、最終露出の終了日ではない。

**ref coverage：** ancestor 64/64、tip 63/64。appendixの各ref行とJSONのexact集合を参照。

**公開面 / 再生成 / 副作用：** 全64 snapshot refsの祖先にaffected commitがあり、過去タグとの結び付けも広い。rewriteは公開source SHA/tag、commit署名、exact-source証跡、既配布物のprovenanceと第三者cloneを変える。primary emailのAPI visibilityはstage1承認変更後とstage2 GETでprivate/verifiedを確認。Keep private/Block pushesのaccount UIは未確認。 共通のpublic-surface確認範囲外は未確認。

**推奨処置：** future-only。過去履歴は保留、既定案はrewriteしない。今後のrepo-local Git identityとGitHub email privacyの設定を整合させる。.mailmapは表示の正規化であって元commit emailの除去ではないので、削除策として追加しない。

**owner判断欄：** 個人メールの過去履歴rewriteを個別に望むか：Yes / No。Noならfuture-onlyで閉じる。Yesでも実行は別承認・別計画で、全branch/tag・PR参照・署名/provenance・第三者cloneの影響を先に評価する。

## H1-02 — 個人home・user・machine path

**重要度 / 種類：** Low〜Medium（限定的privacy）。H1。

**現在・候補：** 公開mainに残る文書の個人home pathをAで相対path・役割名・非公開証跡markerへ整理。過去削除済みtestのpathはcurrent main/Aには存在しない。

**確認事項 / 第三者推論：** 前回の全history監査で実ownerのpathと分類した407 blob/line rows（25 historical paths）に、Aの個人path121 rowsを統合。30 distinct paths。/Usersの一般例、hosted runner path、public OS標準pathとは分けた。第三者はOS、user名、checkoutの場所を推測できる。credential取得や実機接続可能性の証拠はない。

**location：** `crates/kirin_measure/tests/inspection_report_smoke.rs`、`crates/kirin_measure/tests/tp_offline_reference.rs`、`docs/hypha_comparison_integrated_plan_v5_20260914.md`、`docs/hypha_comparison_v5_execution_contract_20260914.md`、`docs/hypha_completion_plan_20260907.md`、`docs/hypha_drum_fan_b_implementation_20260906.md`、`docs/hypha_drum_membrane_implementation_20260907.md`、`docs/hypha_integrated_implementation_plan_20260907.md`、`docs/hypha_lightweight_runtime_progress_20260906.md`、`docs/hypha_listening_review_01_20260907.md`、`docs/hypha_local_blind_runtime_progress_20260906.md`、`docs/hypha_meter_current_implementation_audit_20260831.md`、`docs/hypha_meter_product_contract_20260831.md`、`docs/hypha_plans_review_and_update_path_20260906.md`、`docs/hypha_pre_post_blind_feasibility_20260906.md`、`docs/hypha_reference_session_repair_plan_20260907.md`、`docs/hypha_remaining_work_handoff_20260910.md`、`docs/hypha_space_attack_plan_20260906.md`、`docs/hypha_space_decay_design_review_20260906.md`、`docs/hypha_structural_repair_plan_20260907.md`、`docs/hypha_windows_cpu_psb_diagnosis_20260906.md`、`docs/hypha_windows_observability_fix_20260906.md`、`docs/planning/hypha_surround_remaining_execution_plan_20261001.md`、`docs/reference_b890_structural_repair_plan_20260914.md`、`docs/reference_c_tonal_balance_plan_20260914.md`、`docs/reference_capture_workflow_implementation_20260914.md`、`docs/reference_lazy_presets_handoff_20260910.md`、`docs/reference_listening_workflow_plan_20260914.md`、`docs/reference_os_preparation_handoff_20260910.md`、`docs/reference_publication_refresh_handoff_20260910.md`。original/candidate行範囲・blob SHA・category全件はlocation appendix / JSONで対応。

**観測履歴範囲：** `2026-04-19T11:33:14+09:00` / `5ef05c51f848d18715bba98003c742d457855e2c` 〜 `2026-10-05T08:52:14+09:00` / `a5bb22347e59cd20cb6c00599aa88b5a97b229b4`。blobの更新範囲であり、最終露出の終了日ではない。

**ref coverage：** ancestor 64/64、tip 22/64。appendixの各ref行とJSONのexact集合を参照。

**公開面 / 再生成 / 副作用：** 再現に必要なコマンド/相対path/公開source SHAは保持。old tagsのsource archiveには該当treeが残る。generatorに旧homeを埋めるproducerはAで追加していない。build由来の再混入はH1-09の別課題。 共通のpublic-surface確認範囲外は未確認。

**推奨処置：** redact current + future-only、old historyはkeep。利用者の「限定的な旧pathはrewriteしない」方針に沿う。

**owner判断欄：** 旧pathの履歴を残す：Yes（既定・利用者方針）/ No（別審査へ）。新規home pathの公開はNo。

## H1-03 — 過去checkoutのnickname・隣接checkout誘導

**重要度 / 種類：** Low（作業identity/topology）。H1。

**現在・候補：** 公開mainの3文書にbare checkout nicknameがあり、Aで「公開履歴」とfull commitへ置換。隣接checkoutへの不要な相対linkは公開Hyphaの技術資料に置換した。

**確認事項 / 第三者推論：** docs/hypha_comparison_safety_contract_20260914.md:43、docs/hypha_pre_post_blind_usability_plan_20260914.md:40、docs/reference_listening_workflow_plan_20260914.md:41（original main行）。実際のpublic branch/tag名・B番号・公開commit SHAとは区別する。nickと作業継続関係を推測できるが、認証を与えない。

**location：** `docs/hypha_comparison_safety_contract_20260914.md`、`docs/hypha_pre_post_blind_usability_plan_20260914.md`、`docs/reference_listening_workflow_plan_20260914.md`。original/candidate行範囲・blob SHA・category全件はlocation appendix / JSONで対応。

**観測履歴範囲：** `2026-09-14T16:04:58+09:00` / `a9cf66d4e947eff7265e2b60ca90cfd918ecf041` 〜 `2026-10-01T01:07:56+09:00` / `17749c580b21ea7957aa008c519cebdcd3d7807d`。blobの更新範囲であり、最終露出の終了日ではない。

**ref coverage：** ancestor 16/64、tip 16/64。appendixの各ref行とJSONのexact集合を参照。

**公開面 / 再生成 / 副作用：** public source provenanceは維持。nickの除去だけでは既存PR branch名や第三者の参照を消せない。source archive/PR diffの古い内容は残存可能。 共通のpublic-surface確認範囲外は未確認。

**推奨処置：** redact current + keep history。

**owner判断欄：** 限定的な旧checkout nicknameを履歴に残す：Yes（既定・利用者方針）/ No（別審査へ）。

## H1-04 — 非公開の検証資料・一時fixture・隔離先storage

**重要度 / 種類：** Low〜Medium（非公開storage topology）。H1。

**現在・候補：** 公開mainのprivate storage root・Downloads・Trash・validation CSV/PCM/analysis source・DAW session保管場所をAで非公開証跡markerへ整理。

**確認事項 / 第三者推論：** 23 supplemental storage linesと4 earlier local-evidence rows。host観測、G1実機受入、live比較review、research foundationの7 paths。実測値、CSV行数、PCM/hash/サイズ、source SHA、immutable違反・隔離・holdout blockerの事実は保持。home pathを含まない相対storage rootも対象にした。第三者は何をどこに保管するか推測できるが、pathだけで資料取得可能とは判定しない。

**location：** `AGENTS.md`、`docs/hypha_b1_host_observation_20260907.md`、`docs/hypha_release_entry.md`、`docs/hypha_structural_repair_plan_20260907.md`、`docs/planning/hypha_live_chain_compare_g1_studio_pro_20260928.md`、`docs/planning/hypha_live_chain_compare_review_20260927.md`、`docs/transient_delta_phase2_research_foundation_report_20260830.md`。original/candidate行範囲・blob SHA・category全件はlocation appendix / JSONで対応。

**観測履歴範囲：** `2026-06-08T21:15:51+09:00` / `4f8442d5e0d9059cf1663f20c93f2c18817ae3c7` 〜 `2026-10-04T11:51:03+09:00` / `7a9c18e164e4a0e2b6cb9ee90782e574af9d9077`。blobの更新範囲であり、最終露出の終了日ではない。

**ref coverage：** ancestor 59/64、tip 59/64。appendixの各ref行とJSONのexact集合を参照。

**公開面 / 再生成 / 副作用：** pathを除いても試験不合格や受入保留を消さない。技術者が公開物だけで再現できない実機証跡は「非公開の検証記録」と明記する。標準的なhost diagnostic folderやgeneric fixture codeはH0として維持。 共通のpublic-surface確認範囲外は未確認。

**推奨処置：** redact current + keep history。実データが公開されていた証拠はなく、旧pathのrewriteは行わない。

**owner判断欄：** 限定的な旧storage pathを履歴に残す：Yes（既定・利用者方針）/ No（別審査へ）。非公開fixture/証跡本体を公開repoへ移動する：No。

## H1-05 — 個人cloud運用通知

**重要度 / 種類：** Low（個人運用状態）。H1。

**現在・候補：** docs/hypha_b1_host_observation_20260907.md original:234のcloud capacity/account-sync運用文をAで削除。host試験結果・sample/spectral制約は保持。

**確認事項 / 第三者推論：** 実cloudのcapacity通知とaccount/syncを変更しなかったという運用事実。個人account名・メール・credential・cloud pathはこの文にない。容量状況/操作習慣は推測できるが、認証情報と誤分類しない。

**location：** `docs/hypha_b1_host_observation_20260907.md`。original/candidate行範囲・blob SHA・category全件はlocation appendix / JSONで対応。

**観測履歴範囲：** `2026-09-07T17:59:48+09:00` / `58a93026c26b02c9264cad12f8e7512f188e16fb` 〜 `2026-09-29T01:21:30+09:00` / `37396101f0d13fc4383b2334fd78474409483291`。blobの更新範囲であり、最終露出の終了日ではない。

**ref coverage：** ancestor 20/64、tip 20/64。appendixの各ref行とJSONのexact集合を参照。

**公開面 / 再生成 / 副作用：** 同期されたデータ内容やprovider側公開設定は取得していない。実cloudへ接続・設定変更していない。docs generatorで復活する経路は発見していない。 共通のpublic-surface確認範囲外は未確認。

**推奨処置：** redact current + keep history。

**owner判断欄：** この限定的な旧cloud運用メモを履歴に残す：Yes（既定・利用者方針）/ No（別審査へ）。

## H1-06 — AI・Notion・個人operator・session申し送り

**重要度 / 種類：** Low〜Medium（内部作業手順）。H1。

**現在・候補：** AGENTS/CLAUDE/release entry、TRACE/Reference/repair/planningなど38 paths。個人AI向け報告形式、Notion/current/daily、room/stash/handoff、forced reread、tool availability、network egress手順をAで公開contributor向け技術条件へ整理。

**確認事項 / 第三者推論：** 100 overlapping mapping rows。AGENTSのmixed blockにはworkstation/private repo topologyも含む。広いoriginal_rangeはH0の技術contextを含み、すべての行を秘密とは分類しない。第三者は内部agent/承認/ネットワーク調査の運用を推測できる。記載だけからprivate systemへ到達できると推測しない。

**location：** `.gitignore`、`AGENTS.md`、`CLAUDE.md`、`docs/hypha_capture_and_workflow_integrated_plan_20260914.md`、`docs/hypha_comparison_integrated_plan_v5_20260914.md`、`docs/hypha_drum_lanes_20260924.md`、`docs/hypha_implementation_approval_20260906.md`、`docs/hypha_implementation_progress_20260906.md`、`docs/hypha_integrated_implementation_plan_20260907.md`、`docs/hypha_jungle_activation_and_visual_plan_20260911.md`、`docs/hypha_local_blind_runtime_progress_20260906.md`、`docs/hypha_plans_review_and_update_path_20260906.md`、`docs/hypha_pre_post_blind_usability_plan_20260914.md`、`docs/hypha_pro_tools_full_issue_audit_20260912.md`、`docs/hypha_reference_and_audio_research_20260906.md`、`docs/hypha_release_entry.md`、`docs/hypha_remaining_work_handoff_20260910.md`、`docs/hypha_spectrum_mid_side_overlay_plan_20260911.md`、`docs/hypha_surround_channel_map_findings_20260918.md`、`docs/hypha_surround_decisions_20260918.md`、`docs/hypha_surround_metric_contracts_20260918.md`、`docs/hypha_trace_record_inbox_recovery_handoff_20260806.md`、`docs/hypha_windows_observability_fix_20260906.md`、`docs/ls_release/kirin_hypha_ls_runbook.md`、`docs/planning/hypha_live_chain_compare_external_research_20260927.md`、`docs/planning/hypha_live_chain_compare_implementation_plan_20260927.md`、`docs/planning/hypha_live_chain_compare_implementation_plan_20260927_v7.md`、`docs/planning/hypha_live_chain_compare_review_20260927.md`、`docs/planning/hypha_live_chain_compare_review_v6_20260927.md`、`docs/planning/hypha_live_compare_lifecycle_repair_20260930.md`、`docs/planning/hypha_one_pass_live_blind_implementation_plan_20260929.md`、`docs/planning/hypha_one_pass_live_blind_validation_20260929.md`、`docs/planning/hypha_post_reference_entry_plan_20260928.md`、`docs/reference_b890_structural_repair_plan_20260914.md`、`docs/reference_c_tonal_balance_plan_20260914.md`、`docs/reference_listening_workflow_plan_20260914.md`、`scripts/hypha_workflow_discovery.test.mjs`、`test_signals/source.md`。original/candidate行範囲・blob SHA・category全件はlocation appendix / JSONで対応。

**観測履歴範囲：** `2026-04-27T22:53:58+09:00` / `a4dc9d70fd11cdd6b278f4778a05af258d984f57` 〜 `2026-10-04T11:51:03+09:00` / `7a9c18e164e4a0e2b6cb9ee90782e574af9d9077`。blobの更新範囲であり、最終露出の終了日ではない。

**ref coverage：** ancestor 64/64、tip 64/64。appendixの各ref行とJSONのexact集合を参照。

**公開面 / 再生成 / 副作用：** R-12/RT-safe/Reference/live比較/明示承認/失効fallback/5.1 gate/GPL/正規build/test/release3channelは保持。内部AI命令の削除を製品承認や実機release gateの省略に読み替えない。 共通のpublic-surface確認範囲外は未確認。

**推奨処置：** redact current + keep history。内部指示はpublic internal.mdへ移さず、非公開側への移管候補として扱う。

**owner判断欄：** 限定的な旧内部手順を履歴に残す：Yes（既定・利用者方針）/ No（別審査へ）。不要な内部手順を新規公開する：No。

## H1-07 — 非公開repository・署名factory・販売/広報topology

**重要度 / 種類：** Low〜Medium（内部topology/運用strategy）。H1。

**現在・候補：** README、LS runbook、private counterpart checkout誘導、growth integrated planなど14 paths。private factoryのmachine/account/workflow運用、非公開counterpart repo/commit/blob所在、販売/広報campaign/outreach/conversion戦略をAで整理。

**確認事項 / 第三者推論：** 69 overlapping mapping rows。private URL/repo識別子・内部担当handoffは認証値と異なる。第三者は構成、署名・販売の担当境界、検証対象を推測できるが、権限が取得できると判定しない。実credential・private key・passwordの発見はない。機能上必要なpublic source identityや正規provenanceはH0/H1-10で保持。

**location：** `AGENTS.md`、`README.md`、`docs/hypha_comparison_integrated_plan_v5_20260914.md`、`docs/hypha_completion_plan_20260907.md`、`docs/hypha_implementation_progress_20260906.md`、`docs/hypha_integrated_implementation_plan_20260907.md`、`docs/hypha_pre_post_blind_feasibility_20260906.md`、`docs/hypha_remaining_work_handoff_20260910.md`、`docs/ls_release/kirin_hypha_ls_runbook.md`、`docs/planning/kirin_hypha_reference_growth_integrated_execution_plan_20260925.md`、`docs/reference_b890_structural_repair_plan_20260914.md`、`docs/reference_c_tonal_balance_plan_20260914.md`、`docs/reference_listening_workflow_plan_20260914.md`、`docs/reference_runtime_v2_handoff_20260905.md`。original/candidate行範囲・blob SHA・category全件はlocation appendix / JSONで対応。

**観測履歴範囲：** `2026-06-08T21:15:51+09:00` / `4f8442d5e0d9059cf1663f20c93f2c18817ae3c7` 〜 `2026-10-05T07:59:19+09:00` / `0c8aa5f8a0dd0a9887f7d80c05e90a429314ad57`。blobの更新範囲であり、最終露出の終了日ではない。

**ref coverage：** ancestor 59/64、tip 59/64。appendixの各ref行とJSONのexact集合を参照。

**公開面 / 再生成 / 副作用：** growth planの全18 RT受入表はbyte-identical。不要な私的todo/広報行を削っても公開feature contract、価格/format、3channel release gate、Windows/host未検証状態を変更しない。origin allowlistは別IDで保持。 共通のpublic-surface確認範囲外は未確認。

**推奨処置：** redact current + keep history。旧topologyへのrewriteは行わない。securityは公開手順を隠すことではなくsecretとwrite/signing権限境界で確保する。

**owner判断欄：** 限定的な旧topology/strategyメモを履歴に残す：Yes（既定・利用者方針）/ No（別審査へ）。

## H1-08 — 原画の生成platform account/job/style/time metadata

**重要度 / 種類：** Low〜Medium（生成platformへの照合可能性）。H1。

**現在・候補：** assets_source/IMG_3113.pngのtEXt Author/Description/Creation TimeとXMP DigImageGUID等から個別account/job/style-reference/timeをAで除去。metadata全削除ではなく、一般的なAI source attributionとtrainedAlgorithmicMediaは残る。

**確認事項 / 第三者推論：** 原画に生成platform account/job/style reference/time/GUIDがあったという前回のsemantic分類を引継ぐ。owner home/email/GPSの文字列候補は該当metadataに確認していない。job/account/GUIDから公開生成画像やplatform accountへ横断照合できる可能性はあるが、実際の到達先・個人account所有関係は未確認。

**location：** `assets_source/IMG_3113.png`。original/candidate行範囲・blob SHA・category全件はlocation appendix / JSONで対応。

**観測履歴範囲：** `2026-04-19T11:33:14+09:00` / `5ef05c51f848d18715bba98003c742d457855e2c` 〜 `2026-04-19T11:33:14+09:00` / `5ef05c51f848d18715bba98003c742d457855e2c`。blobの更新範囲であり、最終露出の終了日ではない。

**ref coverage：** ancestor 64/64、tip 64/64。appendixの各ref行とJSONのexact集合を参照。

**公開面 / 再生成 / 副作用：** 全nonmetadata chunks、IDAT bytes、decompressed scanlines、2048×2048 RGB pixelsは同一。source imageは全64 snapshot refsのtip/祖先に同一blobで残る。latest public Mac ZIP/PKGに原画本体のbytesは検出されず、release packageへmetadataが入る直接経路は確認していない。GitHub source archives・古いtreeは残る。 共通のpublic-surface確認範囲外は未確認。

**推奨処置：** redact current。historyは既定keep、high-identificationの個別判断候補として保留。AI生成の出所/著作権・licenseに必要な一般attributionは保持。

**owner判断欄：** 過去画像metadataの識別子について個別rewriteを希望するか：Yes / No（既定No）。NoならAの今後の除去で閉じる。

## H1-09 — 既公開macOS binaryのビルド元home path

**重要度 / 種類：** Medium（既配布物の個人build path）。H1。

**現在・候補：** v1.1.50 macOS Universal ZIP/PKG内のPRE/POST×VST3/AU、4 binariesにowner home/Cargo registry pathが残る。Aの文書/metadata整理では消えず、既存assetsを変更する許可もない。

**確認事項 / 第三者推論：** 各binary164 strings（x86_64 86 + arm64 78）、全て__TEXT/__cstring。各sliceに__DWARFなし。debug symbolsをstripするだけでは再混入防止にならない。4 binary×164=656 unique-container occurrences、ZIP/PKGの同一payload複製を数えると1,312。credentialと判定する根拠はなく、Rustのpanic/macro source location由来と推定する。各call siteとの対応は未確認。

**location：** v1.1.50 ZIPとPKG内同一payload。4 filesの正確なmember名・binary SHA・arch/sectionは[binary評価](binary_path_normalization_plan_20261005.md)を参照。raw section evidenceは公開Git外へ保存。

**観測履歴範囲：** v1.1.50、2026-09-21公開、source commit `3d2234ec78ba5924d5db92fd88498bec64233b5c`。最初の発生releaseは未確認。

**ref coverage：** v1.1.50 assetだけ確認。source commitが祖先にある14/64 refsをJSONに別記し、他tag binaryの内容へ一般化しない。

**公開面 / 再生成 / 副作用：** source checkoutだけのremapではCargo registry homeを覆えない。両arch、全deps、clean cacheを含めてunsigned build/scan/signal invariant/package/provenance、必要なら正式署名/公証/Windows/実機gateを検証する必要がある。新production build未実施、現在recurrence未解決。最古発生version、他旧releasebinary約2.7GB、Windows compressedpayloadは未確認。 共通のpublic-surface確認範囲外は未確認。

**推奨処置：** keep existing assets + future-only。将来候補でRust source/dependency Cargo home、Clang source、MSVC/compiler/linker pathを正規化する設計・fixture検証を別文書へ記録。production flag・product binary・version・asset・tagは今回変更しない。

**owner判断欄：** 既存assetsを保存し、将来の正式候補で正規化を導入する：Yes（既定・利用者方針）/ No（別判断へ）。既存v1.1.50を無断差替えする：No。

## H1-10 — 意図して残すorigin allowlist・署名provenance識別子

**重要度 / 種類：** Informational〜Low（必要性あるorigin識別子）。H1。

**現在・候補：** scripts/ls_release/build_kirin_hypha_release_set.mjs:222,313、hypha_release_local.mjs:84、release_metadata.test.mjs:392,408のorigin/workflow識別子はcurrent mainとA/B候補に意図して保持。5 locations、3 paths。

**確認事項 / 第三者推論：** private origin repo/run URL allowlistとsigning workflow identity、および対応するfixture。署名済みartifactを正しいtrusted originへ結び付け、別repo/別commitのpayload受理を防ぐために実装上必要。公開run URLの通常repo binding・generic validationfolderとは区別。私的topologyの小さな開示はあるがcredentialではない。

**location：** `scripts/ls_release/build_kirin_hypha_release_set.mjs`、`scripts/ls_release/hypha_release_local.mjs`、`scripts/ls_release/release_metadata.test.mjs`。original/candidate行範囲・blob SHA・category全件はlocation appendix / JSONで対応。

**観測履歴範囲：** `2026-09-02T11:50:54+09:00` / `f0d3a79e0cdb3300c8ea1f2a1f6f887eeeda0102` 〜 `2026-10-01T20:36:07+09:00` / `707c63d041f28e11e4e286c061ced90955701230`。blobの更新範囲であり、最終露出の終了日ではない。

**ref coverage：** ancestor 21/64、tip 21/64。appendixの各ref行とJSONのexact集合を参照。

**公開面 / 再生成 / 副作用：** 値を隠すだけで署名factoryの権限境界は強くならない。allowlist/provenance契約の意味を変える処置はA hygiene範囲ではない。public auditの値なしlocation記載は再現性と攻撃面説明を両立する。 共通のpublic-surface確認範囲外は未確認。

**Release sidecar残存：** v1.1.49のexternal report asset `541167516` / installer JSON `541167501`、v1.1.50のexternal report `579174069` / installer JSON `579174053`に計6 origin run URL occurrencesを前段で確認。signing.workflow_run / external_validation.candidate_workflow_runのprovenanceであり、credential/admin URLではない。既存assetを保持する。

**推奨処置：** keep。公開不要なoperator文書と同じ理由でliteralを消してwildcardへ広げない。将来のconfig一般化は許可originの厳密bindingとnegative testsを保持して別検討する。

**owner判断欄：** 現行の厳密origin verifierを維持する：Yes（推奨）/ No（置換設計・別検証が必要）。履歴rewrite：No。

## H1-11 — 技術上不要な個人承認・担当者帰属

**重要度 / 種類：** Low（不要な個人運用帰属）。H1。

**現在・候補：** docs/invariants/Reference/research/plansを中心に61 paths。個人名付き承認・operator判断・手動報告をAで「設計承認」「利用者」「maintainer」等へ整理。

**確認事項 / 第三者推論：** 283 semantic inventory rows。同じ箇所が別H1 categoryと重複し、283 distinct persons/valuesという意味ではない。個人の承認習慣/関与箇所は推測できるが、公開copyright/作者/test WAV原creatorはlicense/provenanceのH0として保持。

**location：** `.gitignore`、`AGENTS.md`、`docs/attack_perceptual_visual_contract_20260831.md`、`docs/hypha_attack_visual_completion_proposal_20260909.md`、`docs/hypha_c3_development_evaluation_20260909.md`、`docs/hypha_ce2226_jungle_visual_system_20260901.md`、`docs/hypha_comparison_integrated_plan_v5_20260914.md`、`docs/hypha_comparison_v5_execution_contract_20260914.md`、`docs/hypha_completion_plan_20260907.md`、`docs/hypha_drum_band_view_plan_20260928.md`、`docs/hypha_drum_fan_b_implementation_20260906.md`、`docs/hypha_drum_lanes_20260924.md`、`docs/hypha_drum_membrane_implementation_20260907.md`、`docs/hypha_drum_render_diagnosis_20260906.md`、`docs/hypha_implementation_approval_20260906.md`、`docs/hypha_integrated_implementation_plan_20260907.md`、`docs/hypha_invariants.md`、`docs/hypha_jungle_activation_and_visual_plan_20260911.md`、`docs/hypha_lightweight_runtime_progress_20260906.md`、`docs/hypha_listening_review_01_20260907.md`、`docs/hypha_local_blind_runtime_progress_20260906.md`、`docs/hypha_meter_current_implementation_audit_20260831.md`、`docs/hypha_meter_product_contract_20260831.md`、`docs/hypha_meter_visual_concepts_20260831.md`、`docs/hypha_plans_review_and_update_path_20260906.md`、`docs/hypha_post_os_guide_integration_plan_20260831.md`、`docs/hypha_pre_post_blind_feasibility_20260906.md`、`docs/hypha_reference_session_repair_plan_20260907.md`、`docs/hypha_remaining_work_handoff_20260910.md`、`docs/hypha_responsive_typography_plan_20260910.md`、`docs/hypha_space_attack_plan_20260906.md`、`docs/hypha_space_decay_design_review_20260906.md`、`docs/hypha_space_mono_sum_plan_20260918.md`、`docs/hypha_structural_repair_plan_20260907.md`、`docs/hypha_structural_repair_plan_20260912.md`、`docs/hypha_surround_aggregation_candidates_20260918.md`、`docs/hypha_surround_channel_map_findings_20260918.md`、`docs/hypha_surround_decisions_20260918.md`、`docs/hypha_surround_implementation_plan_20260918.md`、`docs/hypha_surround_ingest_capacity_20260918.md`、`docs/hypha_surround_metric_contracts_20260918.md`、`docs/hypha_surround_p0_inventory_20260918.md`、`docs/hypha_surround_review_request_20260919.md`、`docs/hypha_trace_record_inbox_recovery_handoff_20260806.md`、`docs/hypha_true_peak_self_check_20260707.md`、`docs/hypha_windows_exchange_safety_20260906.md`、`docs/hypha_windows_observability_fix_20260906.md`、`docs/planning/kirin_hypha_reference_growth_integrated_execution_plan_20260925.md`、`docs/public_history_identity.md`、`docs/reference_b890_structural_repair_plan_20260914.md`、`docs/reference_c_tonal_balance_plan_20260914.md`、`docs/reference_lazy_presets_handoff_20260910.md`、`docs/reference_listening_workflow_plan_20260914.md`、`docs/reference_os_preparation_handoff_20260910.md`、`docs/reference_publication_refresh_handoff_20260910.md`、`docs/reference_runtime_v2_handoff_20260905.md`、`docs/reference_visual_comparison_plan_20260914.md`、`docs/transient_delta_phase2_drum_pilot_report_20260830.md`、`docs/watch_hardware_polish_notes_20260903.md`、`docs/watch_hardware_polish_proposal_20260903.md`、`docs/windows_external_validation.md`。original/candidate行範囲・blob SHA・category全件はlocation appendix / JSONで対応。

**観測履歴範囲：** `2026-04-19T11:33:14+09:00` / `5ef05c51f848d18715bba98003c742d457855e2c` 〜 `2026-10-05T08:52:14+09:00` / `a5bb22347e59cd20cb6c00599aa88b5a97b229b4`。blobの更新範囲であり、最終露出の終了日ではない。

**ref coverage：** ancestor 64/64、tip 64/64。appendixの各ref行とJSONのexact集合を参照。

**公開面 / 再生成 / 副作用：** 製品機能を許可した日付・ceiling/gain/RT/失効gate・source provenanceの削除や改変をしない。実名は公開copyright等で元から必要なため、不要な文書名だけのrewriteで匿名化を達成したとは言えない。 共通のpublic-surface確認範囲外は未確認。

**推奨処置：** redact current + keep history。技術的合意の日付・条件・公開source SHAは残す。

**owner判断欄：** 技術上不要な旧個人承認帰属を履歴に残す：Yes（既定・利用者方針）/ No（別審査へ）。法定/必要copyrightを消す：No。

## H1-12 — 既削除の内部methodology文書

**重要度 / 種類：** Low〜Medium（history-only内部手法）。H1。

**現在・候補：** docs/methodology/guardian_audit_robustness_methodology_v5_20260506.mdはcurrent main/A treeから既に削除済み。Stage1/2で新規削除・rewriteしていない。

**確認事項 / 第三者推論：** 内部methodology597linesを含む既存blob1件。2026-05-06導入commitと削除commit04900e93efc4a2736a477d2aa9c160302a5592a6を前回証拠から確認。今回exact graphでは2 backup branchesの祖先と1 old feature branchのtipに残る。current mainのknown-object ancestryには存在しない。

**location：** `docs/methodology/guardian_audit_robustness_methodology_v5_20260506.md`。original/candidate行範囲・blob SHA・category全件はlocation appendix / JSONで対応。

**観測履歴範囲：** `2026-05-06T08:48:57+09:00` / `8c49309f0ff5343df04dfba574ac509ccdfb0228` 〜 `2026-05-06T08:48:57+09:00` / `8c49309f0ff5343df04dfba574ac509ccdfb0228`。blobの更新範囲であり、最終露出の終了日ではない。

**ref coverage：** ancestor 3/64、tip 1/64。appendixの各ref行とJSONのexact集合を参照。

**公開面 / 再生成 / 副作用：** 旧ref deletionとhistory rewriteは別操作。refを削るだけで第三者clone・GitHub retentionから消えるとは言えない。公開source/技術methodologyとして必要な情報の範囲を精査せず全内部手法を秘密扱いしない。 共通のpublic-surface確認範囲外は未確認。

**推奨処置：** keep history、現行から再導入しない。古いbackup/feature refsの処置は一括削除せず別判断。

**owner判断欄：** この旧内部methodologyを履歴に残す：Yes（既定・利用者方針）/ No（別審査へ）。

## H0として保持するもの

GPLv3およびvendor license、必要なcopyright/作者、test WAV原creator/source、Hypha/Rust/JUCE source、計測・audio・Reference/live比較の公開契約、全format/全3配布channelの受入gate、公開B番号・commit SHA・tag・配布hash/signature verification、公開CI provenance、秘密のないexample/template、意図的なpublic HMAC defaultの制約説明を保持する。generic `/tmp/` / user-home example / public Windows validation fixtureのpath、GitHub hosted runner pathsは個人の実pathと同一視しない。安全な出所bindingを広げるためにorigin allowlistを削らない。

## 独立PR-A reviewの範囲

history_audit担当はPR-A変更を執筆していない。a8→d27の固定objectsをread-onlyで独立確認：全98 changed paths、license系7textsとlicense.rsのbyte一致、test WAV10filesのbyte一致、invariants数値multiset一致、growth全18 RT acceptance table rowsのbyte一致、PNG全chunk CRC・nonmetadata chunk/IDAT/decompressed scanline byte一致、13 build/release/source-validation producers/manifestsのbyte一致。唯一のcode/config拡張子変更は `scripts/hypha_workflow_discovery.test.mjs` で、Notion見出しの境界assertを公開release3channel見出しへ変更し、forced-reread語の要求を外したdoc contract更新。製品runtimeを変更しない。

前回の独立semantic reviewでR-12、bit-identical/0-latency/RT alloc-lock-IO禁止、Reference gain/明示承認、live PRE/POST fallback/ceiling、研究holdout/hashの保留事実、3channel gateを確認済み。Stage2では固定commitでbyte/数値/受入rowを再確認し、worker自身の335 local-link target resolution証拠は別worker作成として区別する。本稿で新しいbuild/test/署名/実機/CIや全98文書の全文semantic再審査は実行していない。Stage2の新doc3件は別commitになるためd27レビュー対象外。

## exact ref appendix

各行のH1集合は「このsnapshot tipからknown evidence objectへ到達可能」と「tip tree/metadataにknown evidenceがある」の別集合。H1-09 source containmentは括弧内09s。main未来候補の非公開commitはこの公開ref inventoryに加えない。

|ref|snapshot full SHA|known ancestor H1 IDs|known tip H1 IDs|cached snapshot一致|
|---|---|---|---|---|
|`branch:backup/feature-pre-rebase-20260517_193855`|`04900e93efc4a2736a477d2aa9c160302a5592a6`|01, 02, 06, 08, 11, 12|01, 06, 08, 11|yes|
|`branch:backup/feature-snapshot-20260517_193855`|`04900e93efc4a2736a477d2aa9c160302a5592a6`|01, 02, 06, 08, 11, 12|01, 06, 08, 11|yes|
|`branch:backup/main-pre-rebase-20260517_193855`|`a318639b98b63ac00deb3245cf2134e678bd5805`|01, 02, 06, 08, 11|01, 06, 08, 11|yes|
|`branch:claude/gracious-pasteur-5b5452`|`c71c12e9b1fd08d23982db69cd723975643607e0`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11, 09s|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:claude/hypha-abcv-h`|`3d74286f7635bdbdaab9b3681948f830aa78351d`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11, 09s|02, 03, 04, 05, 06, 07, 08, 10, 11|no（既記差分）|
|`branch:claude/hypha-drum-band-mock`|`283ab7ad42efcecb35e772f45bb318e2822efebe`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11, 09s|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:claude/hypha-drum-band-summary`|`95da298b9129ee5291b60ccbd411c9b9e0fb9350`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11, 09s|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:claude/hypha-light-stage2`|`20d7dcd56543430ae51c1532277a94f15a6beec2`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11, 09s|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:claude/hypha-protools-windows-check`|`37396101f0d13fc4383b2334fd78474409483291`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11, 09s|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:claude/local-main-reconcile`|`5364e6292aa1326c5e265a8e9cc00d100f2eda88`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11, 09s|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:codex/b896-observatory-background-recovery`|`5200f8817c8adfd12bfe95531c7409205b0ecb7d`|01, 02, 04, 05, 06, 07, 08, 10, 11|01, 02, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:codex/b897-observatory-background-current`|`e7db39d225fce028f465616f279eb94dcd24ce3a`|01, 02, 04, 05, 06, 07, 08, 10, 11|01, 02, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:codex/hypha-aax-cka`|`0093a4f339d107fd1a4e49cfa9ef8e538b887893`|01, 02, 04, 05, 06, 07, 08, 10, 11|01, 02, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:codex/hypha-aax-qualification`|`7ad511a6556b4d4268a9155b55e9ee8192b87568`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11, 09s|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:codex/hypha-one-pass-blind`|`91079ae7cf200ac511f26fe1341f5541016d315c`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11, 09s|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:codex/hypha-perceptual-continuous`|`ad8aad129ede7679b6c9907508818aa51c15e598`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`branch:codex/hypha-ref-simple-abc`|`6ba9fe20e681d369c1de08e1c74b1ef2406dc010`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11, 09s|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:codex/hypha-reference-b-preparation`|`e0f2cc9dbb3517a4a92bc5e40cee8ef5c340192d`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11, 09s|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:codex/local-recovery-20260915`|`05d347ad19da96917c42ebd68ce55393753b40e8`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:codex/reference-abc-delivery`|`0aab301373c511ff6813a01363dbd99630079a69`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:codex/reference-library-receiver`|`9b2c827448931fda24abd85b5cd9a570fdc10269`|01, 02, 04, 05, 06, 07, 08, 10, 11|01, 02, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:feature/hypha-pre-post-stabilization`|`05a1a63d25bef1607faafa6978b4ea3467c874e0`|01, 02, 06, 08, 11, 12|01, 06, 08, 11, 12|yes|
|`branch:fix/b1007-modern-codesigntool-java`|`99ea232ff94e06e7b73e0b17d07a4a3538c7425d`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11, 09s|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`branch:main`|`a8f5a4a4a791cd4a81346e2c5462d08269619b16`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11, 09s|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`tag:v1.1.50`|`3d2234ec78ba5924d5db92fd88498bec64233b5c`|01, 02, 03, 04, 05, 06, 07, 08, 10, 11, 09s|01, 02, 03, 04, 05, 06, 07, 08, 10, 11|yes|
|`tag:v1.1.49`|`c9e458d1e98c43f9028afd974518a298cca94108`|01, 02, 04, 06, 07, 08, 10, 11|01, 02, 04, 06, 07, 08, 10, 11|yes|
|`tag:v1.1.48`|`992cb3094010b6eda44ce0d486ad01a0e94a9b54`|01, 02, 04, 06, 07, 08, 11|01, 02, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.47`|`b965ea12a90152038b01949c1ef8e2cea99dc26a`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.46`|`9d27808dd10d6c859340d0f9aec093d0bd8db4cf`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.45`|`5ba9bf5e3bcfc1df230f93ca933f39213477d446`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.44`|`6baa82d81575f0019412c4f9d5da79c8708f3661`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.43`|`5b1e8a9deb2bc41f7fa5631e92037c0f8a1fdf6f`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.35`|`547d7767f802b61e447f953d9887ca6253818170`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.34`|`eb99f1ad17e54dc32b8ad959e1798193b6f39d1b`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.33`|`019dc3fa85c4932605a2837e6c6b55adfb3c3bb9`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.30`|`58c975fba5489fb2612674169421bf483ca48fae`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.29`|`cc54e9ec121de0449d8ff91f99edbc21a47093e5`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.28`|`3e611b545dc3f1ca2bd85b065b264c702a7bc3dd`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.27`|`d861b8dba1b5228573b93497e4b5def67f75b2fc`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.26`|`a5ec2e11a7c933709a7e09e82088e121405b9b38`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.25`|`fc68e87795b2741e4eaf938c1f36ce1ce7a61a4e`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.24`|`a34e1e517b02ad7be8ae82327d47e782b7452195`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.23`|`724de70315f9f21aa754f39fc963070c073acfd0`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.22`|`1d8a10dc45d72f5bb8df65e8beff2d2fb65046c4`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.21`|`9a2e6f0062af0aa06f26522ddf00ed7b77c1a2c4`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.20`|`645fd4f3901428cc655dcf9972070cb16a5da912`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.19`|`51c7111151894409292c8710f92985b213d1f76d`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.18`|`c5a32b3c0acc64e5158c1471cbac0ca8e42610bd`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.17`|`f30051606d1eb9d36d50ae2bbd6f8605f32cca8b`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.16`|`e2e3e7409ae0bcd7d91dbe89a5aee9ab7b7676f8`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.15`|`199259360669a6b083a5c4d6dfcbb32f8bb49b90`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.14`|`aede33bcbb34c796272f8660dfdbbedbb2aa396b`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.13`|`e31656f9a6d87b151e2dee4ade62e3220af51563`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.12`|`03597bce44c3483aa146bda0bfa3f368f0b346d3`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.11`|`7fae45ad57be7b5e754bfa46aa3083c9b73ce173`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.10`|`bb304753e7a2737d9289cba78966531c4244815a`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.9`|`5e9c5fda19333ae454b2f61bd2260a0a52ed06f8`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.7`|`f5ac789e1fdac702de2d1fb51e84b831a415fe38`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.6`|`2f9554859227cbf4b3dd0e93be80e4c7e21a5b34`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.5`|`15b2cc7b1f11de7aef40e1652f88bf09f0c680fb`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.4`|`9aed8ec20fcfa7066f056ee1b5a9b44d1aaa29d6`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.3`|`6c4d71c462ecd43cb43ab4e18c2b0dfebfe00fd9`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.1.1`|`83738961e899f2276487a3353cf8c5820674f38e`|01, 02, 04, 06, 07, 08, 11|01, 04, 06, 07, 08, 11|yes|
|`tag:v1.0.0`|`81aefb70fadcca10702065f756f5cc1e01695430`|01, 02, 06, 08, 11|01, 06, 08, 11|yes|

本表のevidence収集はread-only Git/object/API既存証拠の分析のみ。fetch/push/PR作成/CI起動/rewrite/tag作り直し/asset変更/settings変更/production build/署名は行っていない。

