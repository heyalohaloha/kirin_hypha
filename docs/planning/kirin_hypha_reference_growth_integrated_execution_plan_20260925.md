---
title: "Kirin Hypha Reference利用と実機受入の技術整理"
date: 2026-09-25
updated: 2026-10-05
status: "履歴の技術整理・実機未確認事項を含む"
---

# Kirin Hypha Reference利用と実機受入の技術整理

本書は2026-09-25時点の製品境界、利用経路、配布物の照合、実機受入計画を保持する履歴資料である。現行仕様は[README](../../README.md)と[製品不変条件](../hypha_invariants.md)を参照する。

個人向け運用、非公開連携側のsource所在、販売・広報施策は公開contributor文書の対象外として除いた。公開文の事実、未確認範囲、無料機能とReferenceの境界は保持する。

## 0. v1.3で直した前提

### 0.1 TRACK / STEMと2MIXを最初から分ける

Hyphaはマスタリングチェインだけの製品ではない。共通HeaderのMeter Contextには、次の二つがある。

| Meter Context | 対象 | 測定・表示上の性格 | 無料で最初に成功させる用途 |
| --- | --- | --- | --- |
| `TRACK / STEM` | 個別track、instrument/vocal bus、group bus、stem | 短い・疎なeventを含む。LEVELはCrest中心、TIMEのDRUMはこのcontextだけ。ローカルBlindはevent energyでGain Matchする | vocal EQ前後、drum bus compression、stem処理前後を同じmixの中で確認する |
| `2MIX` | mix bus、master bus、2mix出力 | 連続active区間を扱う。LEVELはIntegrated中心。TIMEにDRUMを出さない。ローカルBlindは連続active blockでGain Matchする | mix/master chain全体、limiter前後、書き出し前の変化を確認する |

この選択をchannel数、名前、routing、signal levelから自動推定しない。Meter Contextは良し悪しの判定でも、製品tierでもない。Referenceも第三のMeter Contextではない。POSTから明示的に始める別の比較試聴である。

### 0.2 再監査で確定・訂正したこと

| 論点 | v1.3の結論 | 計画への反映 |
| --- | --- | --- |
| 製品の用途 | マスタリング専用ではない。TRACK / STEMと2MIXの両方を対象とする | メッセージ、実演、pilot、指標を二経路化 |
| 公開Hypha | 公開ページとGitHub Releaseはv1.1.50、source `3d2234ec78ba5924d5db92fd88498bec64233b5c`、B-999 | 公開基準をv1.1.50へ固定 |
| 開発候補 | 再監査worktreeはv1.1.51、`ab35af7287f3e9701ed1c8252ca1ff39739b85b2`、B-1018。公開tagではない | 素材候補にはできるが、公開版とは呼ばない |
| 5.1 | v1.1.51候補のexact 5.1 measurement-only。Reference、Blind、Record、FREQ、SPACE対応ではない | 主要訴求から分離し、公開時も制限を併記 |
| Windows Reference | v1.1.50公開リリースは専用Windows機でReference A/B/C、Version Blind、PRE/POST Blind合格を明記 | 「記録なし」を撤回。ただしv1.1.51候補へ流用しない |
| macOS Reference | v1.1.50公開リリースは署名、公証、auvalを明記するが、同じリリース記述だけではReferenceの実DAW受入範囲を確定できない | OS・DAW・format・版を付けた別の受入記録を探す。なければ実施 |
| OS Reference送信 | 現行ソースはlibrary format 1.1を生成し、Hyphaは1.0/1.1を受け入れる | format互換と評価配布物の実機成功を分離 |
| 公開ページ | Hypha英日ページは個別BUSとMIX / MASTER BUSを載せる。一方、OS英日ページはReference導入を説明していない | 既存の二用途を維持しつつ、Reference着地を追加 |
| パネル | `REFERENCE / KIRIN OS`パネルと`/os`・`/ja/os`への遷移は既存 | 新規UIを前提にしない。まず着地先を直す |
| 配布管理 | 三つの配布先とPKG／ZIP／EXEは別の軸 | 入口、取得物、内容物、利用結果を配布先ごとに記録 |
| 音声経路 | 通常計測／比較試聴とReference A/B/Cは別の分類 | 製品境界整理と公開文で記号の衝突をなくす |
| 根拠固定 | review本文の断定だけでは再現できない | repository、full commit、file、blob、取得結果を台帳化 |

### 0.3 先に直す公開上の不整合

公開Hyphaページには「Hyphaが音声を加工したり、音量を変えたりすることはない」という無条件表現がある。通常計測経路は透明だが、比較試聴経路は同じ一種類ではない。ReferenceのAはライブのDAW入力、BはKirin OSへ登録した同じ曲のVersion、Cは比較用のCheckである。Version BlindでもAはライブ入力のまま扱い、必要な場合だけ確認・承認条件に従う試聴gainを適用する。ローカルPRE/POST Blindは、同じ4秒区間から取得したPREとPOSTの比較用copyを使う。

したがって公開文は、次の意味へ統一する。

> 通常の計測では、Hyphaは入力音声を変更しません。利用者が明示的に比較試聴を開始した場合だけ、比較対象の切替や、確認・承認条件に従うgain調整を試聴出力へ適用します。ReferenceではライブのDAW入力と、Kirin OSで管理したVersion／Checkを比較します。ローカルPRE/POST Blindでは、同じ4秒区間から取得した比較用copyを試聴します。元のReference fileと、正本の測定・Record dataは変更しません。

また、公開ページはPREへ名前を付ける手順を必須のように説明しているが、現行契約では名前は任意表示であり、pairの権威は利用者が選ぶexact PRE locatorである。名前一致やtrack位置による自動pairを示唆しない。

---

## 1. 証拠の扱い

### 1.1 証拠区分

| 区分 | 意味 |
| --- | --- |
| 公開版証拠 | 公開tag、Release、配布物sidecar、公開ページに結び付いた事実 |
| 現行ソース確認 | 2026-09-25に読んだworktreeの実装・契約。公開済みとは限らない |
| 実機受入 | exact commit・binary・OS・DAW・format・操作・結果を記録した試験 |
| 公開表示確認 | URLで表示された文言・リンク。注文、download内容、実行成功までは証明しない |
| 外部公式情報 | 媒体、競合、platformの公式ページで当日に確認した条件 |
| 提案／承認待ち | 本書が定義する作業。採用、実装、公開を意味しない |

レビュー報告だけを製品事実の最終根拠にしない。古い実装記録に「未完了」とあっても、後続のexact release証跡が完了を示す場合は後続証跡を優先する。逆に、過去版の合格を次のcommitへ流用しない。

### 1.2 版と配布物の現在地

| 対象 | 2026-09-25時点の確認 | 扱い |
| --- | --- | --- |
| Hypha公開版 | v1.1.50 / B-999 / `3d2234ec...`。Lemon Squeezy同梱、HP無料配布、Windows配布の三つの配布先を持つ | 現在の一般向け基準。配布先とpackage形式を分けて照合する |
| Hypha候補 | v1.1.51 / B-1018 / `ab35af72...`。公開版より12 commits先のreviewed branch | 内部候補。CI、実ホスト、署名、公証、三つの配布先の照合は未完了 |
| Hypha公開ページ | 英日ともv1.1.50、2026-09-21公開を表示 | page表示と実配布物を候補・配布物照合で照合 |

三つの配布先と配布物の形式は、次のように別軸で管理する。

| 管理対象 | 記録すること |
| --- | --- |
| 配布先 | Lemon Squeezyの既存Kirin OS／Kirin Sense商品への同梱、HP無料配布、Windows配布。それぞれの入口と取得結果 |
| 配布物 | file名、PKG／ZIP／EXEなどの形式、version、hash |
| 内容物 | PRE／POST、AU／VST3、組み込まれたversionとcommit |
| 利用結果 | その入口から取得した実物のinstall、起動、対象操作の結果 |

HPの公開ページは2026-09-25時点でmacOSの主downloadをPKGへ案内し、ZIPをGitHub Release上の手動復旧用として案内している。一方、v1.1.50 tag内のrelease runbookはHP無料配布をZIPとして定義する。この差は手順書だけから推測せず、候補・配布物照合で公開linkと取得物を再照合する。GitHubにPKG／ZIP／EXEが存在することだけで、Lemon Squeezy同梱を含む三つの配布先の更新完了とはしない。

候補を用いた内部撮影は可能だが、公開時には承認済みtagと三つの配布先の実物へ照合し直す。候補差替え後に古い画面や操作をそのまま使わない。

## 2. 目的と成功の定義

### 2.1 最終目的

TRACK / STEMと2MIXの通常観測に加え、必要な利用者がVersionやCheckをReferenceとして比較し、保存後も同じ作業を継続できることを確認する。

無料Hyphaの継続利用は、それだけで成功である。Referenceが不要な人に広告や操作制限を重ねない。Referenceを使わないことを失敗と数えない。

### 2.2 三つの利用経路

| 経路 | 最初の成功 | 次の成功 | Referenceとの接点 |
| --- | --- | --- | --- |
| TRACK / STEMから始める | vocal、drum、group、stemの処理前後を観測できる | 別のtrack/stemや次のsessionでも再利用する | 個別処理の確認を終え、mixのVersionや比較曲を確認するときに2MIX側のReferenceへ進む |
| 2MIXから始める | mix/master busの処理前後を観測できる | limiter前後、書き出し、別Versionでも再利用する | 現在のmixと以前のVersion、またはCheckを比較する |
| Referenceへ直行する | VersionまたはCheckをOSで準備し、Hyphaから選び、比較後にライブ入力（Reference A）へ戻る | 保存・終了後、別の日に再開できる | Kirin OS評価から始める。無料Hyphaを何日も使わせて待たせない |

Track/Stem用Referenceの一般的な用途は、現行のVersion/Check契約と実機証拠が揃うまで宣伝しない。Track/Stemの無料計測とローカルBlindを見せた後、同じ制作が2MIXのVersion比較へ進む流れは説明できる。

### 2.3 成功を段階ごとに分ける

1. **無料初回成功**：対象contextを利用者が選び、POSTの接続先一覧から確認したいPREを選び、自分の音で最初の観測を読めた。
2. **無料再利用**：別の日または別のsessionで、同じ用途へ再び使った。
3. **Reference初回成功**：意図したVersionまたはCheckを選び、比較試聴し、ライブ入力（Reference A）へ戻れた。
4. **Reference再利用**：保存後に再開し、別の制作判断で再び使った。
5. **導入成功**：license状態が変わっても、保存済みのWorkとReference作業を継続できた。

download、評価注文、activation、画面を開いたことを、それぞれ次段階の成功へ繰り上げない。

---

## 3. 製品境界の正本文言

### 3.1 無料／連携機能

| 機能 | 必要なもの | 公開説明 |
| --- | --- | --- |
| LEVEL / TIME / FREQ / SPACE、通常Watch | Hypha | TRACK / STEMと2MIXの両方で使う無料計測 |
| ローカルPRE/POST Blind | Hypha | 選択したPRE/POST間の同じ4秒区間から取得したcopyの比較。Preference Listening Trialであり、改善証明ではない |
| Reference A/B/C、Version Blind | Hypha + Kirin OS権利 | OSで管理したVersion/Checkを、POSTから明示的に比較する |
| Keep / Record / plugin_data | Hypha + Kirin OS権利 | 制作中の測定をWorkへ残す |

Referenceは単独販売の追加licenseではない。購入するのはKirin OSのlicenseである。Kirin Senseの購入を中間条件にしない。

### 3.2 言ってよいこと、言わないこと

- 「通常計測は透明」と言う。明示的比較試聴まで含めて「出力へ一切触れない」と言わない。
- 「観測した差」と言う。音質改善、正解、推奨、score、品質判定と言わない。
- 公開自己検証と第三者認証を分ける。EBU Mode認証や第三者認証を主張しない。
- 5.1 measurement-onlyを、Reference、Blind、FREQ、SPACE、Recordの5.1対応へ広げない。
- PRE名は任意labelと説明する。名前一致やtrack位置で自動pairされると言わない。
- TRACK / STEMと2MIXは利用者が選ぶ。Hyphaが楽器、bus、routingを自動分類すると言わない。

### 3.3 製品境界整理を唯一の文言正本にする

製品境界整理は次を日英で一度だけ定義し、README、HP、guide、字幕、Press Kit、製品登録へ転記する。

- TRACK / STEMと2MIXの意味、違う表示、適する実演
- 通常計測経路と比較試聴経路の違い
- Referenceのsource A（ライブDAW入力）／B（Version）／C（Check）
- ローカルPRE/POST Blindが同じ4秒区間の取得済みcopyを使うこと
- 比較試聴で適用し得るgainの確認・承認条件
- StandaloneとKirin OS連携の境界
- 公開版、対応OS/format、5.1制限
- 地域別の評価・購入経路
- 評価終了後の確認済み挙動と未確認範囲

短文や翻訳は媒体へ合わせてもよいが、必要条件と意味を媒体ごとに決め直さない。

---

## 6. 実機受入計画

### 6.1 記録単位

試験はID、日付、担当、取得経路、Hypha/Kirin OSのversionとfull commit、配布fileとhash、OS、DAW、format、Meter Context、license状態、操作、結果、log、画面、制限を記録する。

結果は`未実施 / 合格 / 不合格 / 条件付き / 判定保留`とする。記録がないことを不合格へ置換しない。source test、mock時刻、開発build、公開binaryの実機試験を区別する。

### 6.2 独立試験

| ID | 確認対象 | 現在の証拠 | 完了条件 |
| --- | --- | --- | --- |
| RT-01 | macOS無料導入 | v1.1.50 signed/notarized、AU auval | 公開PKGから対象DAWでTRACK / STEMと2MIXの初回成功、再open |
| RT-02 | Windows無料導入 | v1.1.50 signed installer、Studio One Pro実機合格 | 公開EXEから両context、再起動、終了、uninstall |
| RT-03 | LS評価取得物 | 公開リンクと$0説明のみ確認 | 正規注文後のOS版、file、hash、loaded appを記録 |
| RT-04 | 日英評価導線 | 日英が同じLS商品へ進むことを確認 | page→無料注文→key→download→activationを確認 |
| RT-05 | macOS Reference | source/native契約あり。公開release文だけでは実DAW範囲を確定しない | Version、Check、ライブ入力（Reference A）への復帰、保存・再開をOS/DAW/format付きで記録 |
| RT-06 | Windows Reference | v1.1.50 releaseはReference A/B/C、Version Blind、PRE/POST Blind合格を明記 | v1.1.50証跡を保存。次候補は同じ項目をexact binaryで再実施 |
| RT-07 | Version | 同曲・位置・固定gainの契約あり | 意図したVersion、曖昧反復、待機、ライブ入力への復帰を確認 |
| RT-08 | Check | 独立したC選択の契約あり | Check選択、欠損時、通常制作への復帰を確認 |
| RT-09 | library/接続表示 | lease終了がverified libraryを消さない契約 | OS終了、lease終了、library保持、試聴権、表示を分離記録 |
| RT-10 | 評価終了後にOS起動 | OS側の期限gateとidentity投影あり | 期限、identity、Reference、Record、無料機能を確認 |
| RT-11 | 評価終了後にOS未起動 | 実機未確認 | DAW継続、DAW再起動、plugin reloadを分けて判定 |
| RT-12 | LS評価→LS永久 | 公開説明はWork/record保持を案内 | library、Reference、保存状態を含めて移行確認 |
| RT-13 | LS評価→日本Stripe永久 | 未確認 | 日本円licenseで同じ状態を維持できることを確認 |
| RT-14 | media欠損・不一致・権利変化 | fail-closed契約あり | 誤った成功表示、別音源代替、ライブ入力への復帰不成立がない |
| RT-15 | 無料範囲への影響 | 境界契約あり | 未導入、評価中、終了後、永久化後に無料機能が説明通り |
| RT-16 | exact 5.1 | v1.1.51候補のmeasurement-only | role、layout、測定、停止されるstereo機能を候補binaryで確認 |
| RT-17 | TRACK / STEM | 現行UI・gain policy・DRUM契約あり | track、group bus、stemで測定と4秒Blind、残りのmixとの関係を確認 |
| RT-18 | 2MIX | 現行UI・continuous gain policy契約あり | mix/master busで測定と4秒Blind、下流処理の影響を確認 |

### 6.3 評価終了の判断境界

OSは起動時やlicense action後に期限を判定し、確定したtierをHyphaのidentityへ投影する。OSを起動しないまま期限へ到達したとき、既存DAW sessionや受信済みlibraryで何が可能かは、コード推論だけで公開仕様へしない。

RT-10/11では、license表示、library file、Reference B/C選択、試聴開始、進行中試聴、新規登録、Record、無料機能を分ける。設計担当が意図する仕様は結果を見た後に評価期限後の仕様判断で決める。決まるまで「期限後は必ず使えない」「期限後も使える」のどちらも宣伝しない。

### 6.4 リリース候補の受入

v1.1.51を採用する場合、B-1018までの内容だけでrelease readyとはしない。release source gate、public CI、macOS AU/VST3、Windows VST3、実DAW、署名、公証、installer lifecycle、三つの配布先を同じcommitで揃える。

v1.1.50のWindows Reference合格やmacOS公証をv1.1.51へ流用しない。候補を差し替えた場合、画面と操作に関係する素材を差分再確認する。

---

## 20. 根拠

### 20.1 Hypha repo

- [README](../../README.md)
- [Hypha invariants](../hypha_invariants.md)
- [Meter product contract](../hypha_meter_product_contract_20260831.md)
- [Reference library receiver](../reference_library_receiver_20260913.md)
- [Release distribution runbook](../ls_release/kirin_hypha_ls_runbook.md)
- [v1.1.50 GitHub Release](https://github.com/heyalohaloha/kirin_hypha/releases/tag/v1.1.50)

### 20.3 公開ページ

- [Hypha English](https://kirinmastering.com/hypha)
- [Hypha 日本語](https://kirinmastering.com/ja/hypha)
- [Kirin OS English](https://kirinmastering.com/os)
- [Kirin OS 日本語](https://kirinmastering.com/ja/os)
- [日本向けKirin OS購入](https://kirinmastering.com/ja/os/purchase)
- [Kirin OS 14-Day Evaluation](https://kirinmastering.lemonsqueezy.com/checkout/buy/005c2b74-6057-4392-8a1e-2604c377c2db)
