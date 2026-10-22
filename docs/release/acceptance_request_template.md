# 受入の依頼

- 候補 ID: `<CANDIDATE_ID>`
- 採用 source: `<COMMIT>`
- 凍結: `<FREEZE_SHA256>`（未署名の通しなら「未凍結・正式受入前」）
- host / format / OS: `<HOST_FORMAT_OS>`
- 対象行と期待値: `<FROZEN_ROW_IDS_AND_EXPECTATIONS>`
- 直した所: `<FIX_ROWS>`
- 束の置き場: `<PRIVATE_EVIDENCE_DIRECTORY>`
- report: `<PRIVATE_HOSTS_REPORT>`
- 担当範囲と復元先: `<SCOPE_AND_RESTORE>`

[未署名の通しと記録の型](presign_smoke.md)に従う。identity は run の始めに一度、
一行一束で記録し、hash は候補／凍結／packages／最終の四点で固める。
独立 review は host の最後に一度。二分進まなければ状態と再開条件を残して次へ進む。

止まる条件: 誤った候補、通常経路の音の変化、不正な PRE 出力、安全でない gain 復帰、
匿名割当の漏洩、Record の不整合、必要権限の不足。確認済み FAIL を PASS にしない。
本人判断が必要な非安全行は理由と再開条件を返し、凍結済みの条件は書き換えない。

返す物: 各行の結果、実測、証拠 path/hash、FAIL と未実施の区別、復元状態、
host 最終 review、次に必要な判断。毎回すべての hash を回答へ転記しない。
