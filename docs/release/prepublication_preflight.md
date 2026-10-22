# 公開の前日の点検

当日になって認証や配置で止まらないよう、前日に次を一束で確認する。確認だけで署名、dispatch、公開を始めない。

| 対象 | 確認 | 記録 |
|---|---|---|
| Chrome | upload に使う専用 profile と現在の選択、翻訳 off。Apple Events の JavaScript は API 確認には不要。既存の別用途で必要ならその許可を担当者が確認 | profile の私的な控えと設定の結果 |
| LS | 手動 upload に使うログインと全商品へのアクセス、API key の読み取り権限 | 成否だけ。key、cookie、管理 URL を公開 source や log へ入れない |
| iLok License Manager | 署名と実 host に使う license の有効な側。Windows の署名後、必要なら Mac へ一度移す | 担当者が確認した状態。script は移動も活性化もしない |
| Mac AAX | 採用候補の配置、退避、復元に必要な権限 | 読取点検が不合格なら、担当者が対象二 bundle と親を確認して本人の認証操作を一度にまとめる。script は chmod しない |
| Windows | 検証に使うログインと必要な権限、更新前の保全 | 担当者による確認。自動ログインや実機変更はしない |
| Actions と署名予算 | 公開 CI と private factory の予算を分け、利用量・上限・artifact 期限・進行 run と予定工程を照合 | 確認時刻、予定 run、見込み回数、承認済み範囲。API アクセス成功だけで残枠ありとしない |

設定 file と書込権限は読み取りだけで点検できる。

```bash
node scripts/ls_release/release_preflight.mjs release_state/preflight.json
```

private config に `chromeProfile`（Chrome profile の絶対 directory）、`macAaxBundles`（対象 bundle の絶対 path の配列）、
個人 account は `githubUser`、組織 account は `githubOrganization`、LS の読み取り対象は `lsProductId` を置く。秘密値を入れない。
script は保存済み profile 選択と翻訳設定、実行者の AAX tree 書込可否、予算または個人の利用量の読取可否、
LS の鍵が読めるかと対象商品を GET できるかを返す。鍵の値や API の本文は出さない。
保存値は実行中 Chrome の現在値の証明ではない。API の pagination が残る場合や読めない場合は CHECKPOINT。
個人 account は `GET /users/{user}/settings/billing/usage` の成否を確認する。利用量の読取成功から予算や実残枠を推定せず、課金画面で担当者が照合する。MANUAL は自動 PASS にしないため exit 2 とする。

[GitHub の個人利用量 API](https://docs.github.com/en/rest/billing/usage#get-billing-usage-report-for-a-user)は本人 account の課金情報の読取権限を要する。
[組織の予算 API](https://docs.github.com/en/rest/billing/budgets)は組織の管理者／課金権限を要する。
権限が無ければ課金画面の読み取りで確認し、重い build を残枠確認のために起動しない。

# LS の upload 後の確認

upload だけは担当者が手動で行う。確認は Chrome の画面操作から JSON:API の GET へ移す。

```bash
node scripts/ls_release/kirin_hypha_ls_dry_run.mjs \
  --state release_state/current_ls.state.json --with-ls-api
```

API key は実行環境の `LEMONSQUEEZY_API_KEY` または `LEMONSQUEEZY_API_KEYCHAIN_SERVICE` が指定する
Keychain 項目から実行時に読む。値をコマンド、repo、fixture、log に書かない。API の response body、
署名付き download URL、credential を結果へ出さない。API は product、variant、その variant の全 file page を読み、
商品への結び、live mode、商品の published、variant の published または既定の pending、各配布物の名前と正確な bytes を検証する。
`expectedFilesCount` は draft を含む API の全 file 数と比較する。各配布物には published と live mode を要求する。
到達不能・認証不可・429・5xx は最大3回で打ち切り、未確認（exit 2）として再開できる。読めた内容の不一致は FAIL（exit 1）。
失敗した check の ID と、秘密でない期待値・実際値を結果に残す。
[variant の状態](https://docs.lemonsqueezy.com/api/variants/the-variant-object)と
[Files API](https://docs.lemonsqueezy.com/api/files/list-all-files)と
[file の属性](https://docs.lemonsqueezy.com/api/files/the-file-object)に従う。
API metadata だけでは配送された bytes の hash は証明できない。正式 LS report の全対象商品の
実 download と hash 証拠は引き続き必要。対象の macOS PKG と Windows EXE を同じ候補へ揃える。

# 公開 source の衛生

`node scripts/check_public_environment.mjs` は Git が公開対象として扱う tracked と untracked の text、
file 名、symlink の行先を検査する。ignored な私的証跡は読まない。一般の user path、LAN address、
引用された local hostname に加え、Git 共通 directory の私的な `hypha-public-environment-names.txt` と
`hypha-public-text-names.txt`、または `PUBLIC_REPO_PRIVATE_NAMES`（改行か comma 区切り）で渡した名前を使う。
人名・機器名・LAN 名・検証機名の実際の一覧は repo に置かない。私的一覧が無い CI では構造の検査だけとなる。
検出結果は path と行、種類だけを出す。既存の記述が検出された場合も自動削除せず、担当者が削除または
検査範囲の根拠をレビューする。upstream の dependency tree と byte 検証済み原文 license は既存の所有境界に従う。
