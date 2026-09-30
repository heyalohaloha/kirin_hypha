---
title: "Kirin Hypha普及・Reference利用・Kirin OS導入 統合実行計画"
date: 2026-09-25
updated: 2026-09-25
aliases:
  - "Hypha普及からKirin OS導入までの統合計画"
  - "Hypha Reference Growth Plan"
tags:
  - Kirin-Hypha
  - Kirin-OS
  - Reference
  - marketing
  - launch-plan
  - conversion
  - product-media
version: "1.3"
status: "決定版・実機未確認事項および個別承認待ち事項あり"
owner: "Daisuke Nishio / Kirin Mastering"
decision: "2026-09-25、Daisuke Nishioにより統合実行計画v1.3を決定版として確定"
planning_horizon: "準備は完了条件で管理／統合キャンペーン開始後90日間を観測"
review_source: "Codex独立再監査とv1.2修正提案（2026-09-25・現行ソース、公開ページ、公開リリース証跡）"
implementation_status: "B-1014後の構造検査追従と診断UIの文字契約修正のみ実施。製品UI、販売設定、公開ページ、外部送信は未変更"
reference_date: 2026-09-25
---

# 決定版：無料Kirin HyphaからReference利用・Kirin OS導入までの統合実行計画

> [!summary] この計画の結論
> **Hyphaは、TRACK / STEMと2MIXの両方で、処理の前後を観測する無料の制作道具として普及させる。Referenceは無料機能の不足を埋める課金解除ではなく、制作中の信号を、Kirin OSで管理したVersionやCheckと比較する別の作業として体験してもらう。**
>
> 無料配布、初回成功、再利用、Reference評価、Kirin OS購入、購入後の継続利用を一つの流れとして運用する。ただし、公開版、開発候補、コード上の能力、実機受入、販売条件は混ぜない。既存の無料配布は維持し、Referenceを促す拡大は対象環境の受入と案内整備が終わってから始める。

本書は、無料Kirin Hyphaの普及と、Referenceを入口とするKirin OS導入を統合した実行文書である。広告だけの計画ではない。製品事実の確定、TRACK / STEMと2MIXの初回成功、実機受入、資料、公開ページ、外部展開、効果測定、停止条件までを同じ管理下に置く。

2026年9月25日、Daisuke Nishioは本書v1.3を統合実行計画の決定版として確定した。これは計画本文と実行順序の確定であり、本文に残るD-G判断、実機受入、product-media変更、公開、購入、外部送信を一括承認するものではない。

準備は日付ではなく完了条件で管理する。90日は統合キャンペーン開始後の観測期間であり、未完成の製品や資料を日程に合わせて公開する期限ではない。本書への記載を、製品仕様、コード変更、購入、公開、外部送信の承認に読み替えない。

---

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
| Kirin OS公開配布 | HPのpublication contractは1.0.6をLS/HPともverifiedと記録。現行OS repoのpackageは1.0.7候補 | v1.0.8配布済みという元案の記載を撤回 |
| OS Reference送信 | 現行ソースはlibrary format 1.1を生成し、Hyphaは1.0/1.1を受け入れる | format互換と評価配布物の実機成功を分離 |
| product-media | `hypha`のCurrentはnull。監視対象はKirin OS内のHypha Supportで、プラグインrepoを監視していない | GP-15/16を正式な前提作業にする |
| 公開ページ | Hypha英日ページは個別BUSとMIX / MASTER BUSを載せる。一方、OS英日ページはReference導入を説明していない | 既存の二用途を維持しつつ、Reference着地を追加 |
| パネル | `REFERENCE / KIRIN OS`パネルと`/os`・`/ja/os`への遷移は既存 | 新規UIを前提にしない。まず着地先を直す |
| 配布管理 | 三つの配布先とPKG／ZIP／EXEは別の軸 | 入口、取得物、内容物、利用結果を配布先ごとに記録 |
| guide工程 | 候補reviewと出荷後のCurrent・配布は同じ完了ではない | GP-14を候補制作と公開版照合の二段階に分ける |
| 音声経路 | 通常計測／比較試聴とReference A/B/Cは別の分類 | GP-01と公開文で記号の衝突をなくす |
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
| Kirin OS公開配布記録 | v1.0.6 / `ba2a70ed...`をHP publication contractがLS/HP verifiedと記録 | 評価版の注文後取得物は別途照合 |
| Kirin OS候補 | package version 1.0.7、reviewed repo HEAD `fc3e41f8...` | 公開配布済みとは呼ばない |
| Hypha公開ページ | 英日ともv1.1.50、2026-09-21公開を表示 | page表示と実配布物をGP-00で照合 |
| OS販売ページ | 海外$297/LS、日本29,700円税込/Stripe、14日評価はLSの同一商品入口 | 言語と居住地域を同一視しない |

三つの配布先と配布物の形式は、次のように別軸で管理する。

| 管理対象 | 記録すること |
| --- | --- |
| 配布先 | Lemon Squeezyの既存Kirin OS／Kirin Sense商品への同梱、HP無料配布、Windows配布。それぞれの入口と取得結果 |
| 配布物 | file名、PKG／ZIP／EXEなどの形式、version、hash |
| 内容物 | PRE／POST、AU／VST3、組み込まれたversionとcommit |
| 利用結果 | その入口から取得した実物のinstall、起動、対象操作の結果 |

HPの公開ページは2026-09-25時点でmacOSの主downloadをPKGへ案内し、ZIPをGitHub Release上の手動復旧用として案内している。一方、v1.1.50 tag内のrelease runbookはHP無料配布をZIPとして定義する。この差は手順書だけから推測せず、GP-00で公開linkと取得物を再照合する。GitHubにPKG／ZIP／EXEが存在することだけで、Lemon Squeezy同梱を含む三つの配布先の更新完了とはしない。

候補を用いた内部撮影は可能だが、公開時には承認済みtagと三つの配布先の実物へ照合し直す。候補差替え後に古い画面や操作をそのまま使わない。

### 1.3 根拠の所在を固定する

2026-09-25の再確認で、Kirin OS配布記録とproduct-media状態はローカルの該当fileまで特定できた。これらは公開表示や利用者経路の実物確認とは別の、source内記録である。

| 事実 | repository / file | fileを最後に変更したfull commit | blob hash | 確認値 |
| --- | --- | --- | --- | --- |
| Kirin OS公開配布記録 | `kirin_hp/updates/publication-contracts/os.json` | `60bb198524016a838b33df049e4b83183e4852ff` | `92dfa497fff48d64f93eabc7cdfc95403e78fd80` | OS 1.0.6、source `ba2a70ed7885f3df73bffc56515058af5128313b`、LS/HP verified |
| Kirin OS候補version | `kirin_sense_lens/package.json` | `2183502cabaac9b1dd91d138602421bcc6b41107` | `d3aeec00ea13bb94379a8a04ddf39044b3f3a42a` | 1.0.7 |
| Hypha guide監視対象 | `kirin_sense_lens/docs/product_media/product_media_config.v1.json` | `6cbbb10ecadaf64ef0c9a9ff74c8eacabdf65dae` | `9d9fa446fef210c8509c4b232adc34c643c9ce99` | OS repo内のHypha Supportを監視。plugin repoは未対象 |
| Hypha Current | `kirin_sense_lens/docs/product_media/current_releases.v1.json` | `658b7c74820d8a1805c9d1099b43a1892e7a825b` | `ccc832f726197b2942369010dc4859335a6b71a1` | `hypha: null` |

各repositoryのworktreeには本計画と無関係な未commit差分があるが、上表の該当file自体は再確認時にcleanだった。repository全体がcleanだったという意味にはしない。GP-00では、このsource内記録、公開page、注文後の取得物、実行結果を別々の段階として残す。

---

## 2. 目的と成功の定義

### 2.1 最終目的

無料Hyphaを使った人が、自分のTRACK / STEMまたは2MIXで次の制作にも使う。そのうち、VersionやCheckを制作中に呼び出す必要を持つ人がReferenceを試し、Kirin OSを導入し、購入後も同じ作業を続ける。これを最終成果とする。

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
5. **導入成功**：購入または評価から永久licenseへ移り、保存済みのWorkとReference作業を継続できた。

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

### 3.3 GP-01を唯一の文言正本にする

GP-01は次を日英で一度だけ定義し、README、HP、guide、字幕、Press Kit、製品登録へ転記する。

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

## 4. 中心メッセージ

### 4.1 製品全体

> **Track、Stem、2Mix。処理の前と後を、同じ制作の中で確かめる。**

「マスタリングチェインの前後」だけに狭めない。一方で「どこへ挿しても何でも分かる」と広げない。PREとPOSTの二点が観測境界である。

### 4.2 TRACK / STEM

> **そのvocal、そのdrum bus、そのstem。処理で何が変わったかを、mixの中で見る。**

画面では`TRACK / STEM`、Crest、必要に応じてTIME / DRUMを使う。DRUMを楽器自動判定や2MIX transient detectorとして説明しない。

### 4.3 2MIX

> **Mix全体を聴きながら、チェインの前後を確かめる。**

画面では`2MIX`、Integrated、LEVEL/TIME/FREQ/SPACEを使う。masteringだけでなくmix busも含む。

### 4.4 Reference

> **Kirin OSで管理したVersionとCheckを、制作中のHyphaから呼び出す。**

無料メーターの制限解除とは説明しない。無料のローカルBlindとも分ける。OS側は比較対象を管理し、Hypha側は制作中に明示選択して聴き、ライブ入力（Reference A）へ戻る。

---

## 5. 公開ページと製品内導線

### 5.1 Hypha英日ページ

現行ページにある「MIX / MASTER BUS」と「個別のBUS」は残し、現行製品の`TRACK / STEM`と`2MIX`へ対応付ける。最初の画面は無料downloadを妨げず、次の順にする。

1. TRACK / STEMと2MIXの両方でPRE/POST間を観測できる。
2. 通常計測は透明である。
3. 30～60秒の二つの初回成功を選べる。
4. 無料範囲とKirin OS連携範囲を表で示す。
5. Referenceの短い説明とOSへの案内を置く。
6. 公開版、対応format、検証資料、downloadを明示する。

先行訂正の対象は、無条件の音声非変更表現、名前必須に見えるpair説明、古いWatch/Record中心の画面説明である。公開ページ変更は別の承認・repo作業として行う。

### 5.2 OS英日ページ

既存ページはHyphaのRecord連携を説明するが、Reference導入理由を説明していない。既存パネルが開く`/os`と`/ja/os`へ、少なくとも次を追加する。

- Referenceでできること
- HyphaとKirin OSの役割分担
- VersionとCheckの違い
- 無料ローカルBlindとの違い
- 対応を確認した環境
- 14日評価で試す最短手順
- 日本向け／海外向けの購入経路

着地先の事実訂正は専用ページや完成guideを待たずに進められる。ただし、未確認の期限後挙動やplatform対応を補って書かない。

### 5.3 Reference専用ページ

専用ページは、正規guideと実機受入のあとに整える。公開URLは確定するまで文書内で捏造しない。

| 順序 | 内容 | 答える疑問 |
| --- | --- | --- |
| 1 | 現在の2MIXと以前のVersionを比べる実演 | 自分の作業に何が増えるか |
| 2 | Checkを呼び出す実演 | 普段の比較曲をどう使うか |
| 3 | OSで管理、Hyphaで呼び出す関係 | なぜ二つ必要か |
| 4 | 導入、保存、再開、ライブ入力へ戻る | 自分で完了できるか |
| 5 | 評価、地域別購入、対応範囲 | 何が無料で何を購入するか |
| 6 | 失敗時の安全復帰とsupport | 制作を壊さず戻れるか |

主ボタンは「Kirin OSの14日間評価版でReferenceを試す」とする。「Reference無料download」と書かない。

### 5.4 既存Reference案内パネル

現行パネルは新設不要である。パネルの表示文言は英語で、`ABOUT KIRIN OS`、`ALREADY OWN IT?`、`RECHECK LICENSE`を持つ。製品情報menuでは英語と日本語を利用者が明示選択し、それぞれ`/os`と`/ja/os`を開く。OSやDAWの表示言語による自動切替とは説明しない。

優先順位は次の通りとする。

1. 既存URLの着地先へReference説明を加える。
2. 正規guideと専用ページを用意し、旧URLからも到達可能にする。
3. パネル文言、専用URL、日本語UIはGP-17として別に採否を決める。

常時広告、再生を妨げるpopup、無料機能を使うたびに出る販売表示は採用しない。

### 5.5 READMEと公開文書

次のrelease sourceでは、少なくとも以下を揃える。

- 残存する「current v1.1.49 Windows release」を実際の公開版へ更新する。
- 冒頭のTIME subviewを現行`HISTORY / RUN / DRUM / SHARP / LIVE`へ合わせる。
- TRACK / STEMと2MIXを、Reference節より前の基本用途として説明する。
- 公開ページと同じ、通常計測／比較試聴／Reference source A・B・C／ローカルBlindの境界文言を使う。
- AAXの内部候補注記を、公開対応formatの説明へ混ぜない。

公開済みv1.1.50 tagの履歴は書き換えず、次releaseへ含める。READMEや製品内文言などrelease sourceの変更は、GP-00の最終候補commitを固定する前に完了する。固定後にsourceまたはbinaryを変える必要が生じた場合は新しい候補として扱い、影響するgate、実機受入、素材を再確認する。

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

RT-10/11では、license表示、library file、Reference B/C選択、試聴開始、進行中試聴、新規登録、Record、無料機能を分ける。Daisukeが意図する仕様は結果を見た後にD-G01で決める。決まるまで「期限後は必ず使えない」「期限後も使える」のどちらも宣伝しない。

### 6.4 キャンペーン候補版

v1.1.51を採用する場合、B-1018までの内容だけでrelease readyとはしない。release source gate、public CI、macOS AU/VST3、Windows VST3、実DAW、署名、公証、installer lifecycle、三つの配布先を同じcommitで揃える。

v1.1.50のWindows Reference合格やmacOS公証をv1.1.51へ流用しない。候補を差し替えた場合、画面と操作に関係する素材を差分再確認する。

---

## 7. 14日間評価の設計

評価の起点は公開条件どおり、試用licenseの初回activationである。注文日、download日、activation日を混同しない。

| 時点 | 利用者の行動 | 運営側が確認すること |
| --- | --- | --- |
| 開始前 | VersionかCheckを選び、必要な音源を用意 | 何を試すか、2MIXで試す理由が分かるか |
| 初日 | OSをactivateし、最初のReference比較を完了 | install、license、library、選択、試聴、ライブ入力への復帰の停止点 |
| 2～3日 | 保存・終了後に再開 | 初回だけでなく戻れるか |
| 4～7日 | mix修正または別sessionで再利用 | 日常作業へ置く場所があるか |
| 8～11日 | 必要な人だけRecord/Workへ広げる | OS全機能の説明が負担にならないか |
| 12～14日 | 必要性を判断し、必要なら永久licenseへ移行 | 価値、価格、互換性、導入負担、購入経路のどれが影響したか |

正規guideを基本に自走できる設計とする。任意メールはGP-18で明示同意、撤回、配信停止、最大回数、activation起点を実装・確認した後だけ行う。注文やlicense発行の連絡先を自動的にmarketing購読へ転用しない。

---

## 8. 制作する素材

### 8.1 最小素材セット

| ID | 素材 | 内容 | 成功地点 |
| --- | --- | --- | --- |
| V1 | TRACK / STEM短編 | vocal EQまたはdrum bus compressionをFREQ/LEVELで見る | 個別処理にも使えると分かる |
| V2 | 2MIX短編 | mix bus EQ/limiter前後をLEVEL/FREQで見る | mix全体の常設用途が分かる |
| V3 | 無料導入 | context選択、PRE配置、exact pair、最初の観測 | 無料初回成功 |
| V4 | ローカルBlind | TRACK / STEMと2MIXで異なるGain Match説明、4秒比較、Reveal、通常制作への復帰 | 無料比較の境界が分かる |
| V5 | Reference Version | OS準備→Reference B選択→比較→ライブ入力へ復帰→保存・再開 | OSを導入する具体的理由 |
| V6 | Reference Check | Reference C選択とVersionとの違い | 比較曲を管理して呼び出せる |
| I1 | 現行静止画 | 両context、LEVEL/TIME/FREQ/SPACE、Blind、Referenceを区別 | 記事・guide・Press Kit |

同じ録画から日英字幕と静止画を作る。動画の短さを実際の処理時間と誤認させない。待機を編集する場合はその旨を示す。

### 8.2 既存素材

`docs/media/`のFREQ 10秒、PRE/POST 32秒、Record/Keep 45秒は下書きとして扱う。現行背景、domain、Meter Context、pair文言、公開版と照合し、流用箇所と再撮影箇所を記録する。Record動画をReference実演の代用にしない。

### 8.3 素材の順序

無料側は、見た目の差が分かりやすいFREQだけへ偏らせない。TRACK / STEMでは短いeventとmix内の変化、2MIXでは連続区間とsession全体を見せる。Reference側は、ボタン切替ではなく、OSでの準備、Hyphaでの呼出し、ライブ入力への復帰、別日の再開までを見せる。

公開・配布権限が明確な自作音源だけを使う。client音源や市販Referenceを素材へ同梱しない。

---

## 9. 正規guideとproduct-media

### 9.1 既存基盤を使う

新しいPDF生成系や別の公開guide系列を作らない。Kirin OS repoのproduct-mediaから、日英それぞれのoffline HTML、quick PDF、complete PDFの計6成果物を作る。

現状、`hypha`のCurrentはnullで、設定のwatchPrefixesはKirin OS内のHypha Supportを向いている。これはHypha plugin guideの更新検知として不十分である。

### 9.2 GP-15：監視対象

Daisuke承認後、Hypha guideの製品根拠としてplugin repoを明示する。OS内Hypha SupportはKirin OS側の依存として残し、plugin本体の代用にしない。

### 9.3 GP-16：二つのrepo根拠

Reference guideはHyphaとKirin OSの両方に依存する。release manifestとfreshness判定は、最低限次を保持する。

| 系統 | 必須の根拠 |
| --- | --- |
| Hypha | repo identity、公開tag、full commit、三つの配布先と対応する配布物、依存する画面・操作・文書 |
| Kirin OS | repo identity、公開version、full commit、取得経路、配布物、Reference delivery・license境界 |

片方を読めない、commit不明、dirtyな未承認差分だけがある状態をfreshとしない。既存schemaとの互換、過去guideのmigration、片側変更時の更新範囲を設計してから実装する。

### 9.4 GP-14を二段階で完了させる

GP-14は新しいtask番号を増やさず、候補guideと公開版guideを内部で分ける。

| 段階 | 行うこと | この段階では行わないこと |
| --- | --- | --- |
| 候補guideの制作・review | GP-00で固定した候補commitと実物画面を使い、日英6成果物を作る。producer QAと独立reviewを通し、候補commitと未確定事項を記録する | 公開版としてCurrentへ設定しない。外部配布しない。候補review合格を出荷照合の代わりにしない |
| 出荷物との最終照合・公開 | 公開tag、三つの配布先、実際の配布物、hash、画面、操作、GP-01を候補guideと照合する。最終承認後にCurrent設定と配布確認を行う | 別commitや別配布物の合格を流用しない |

これは計画内の工程分離であり、既存product-mediaに新しいstate名やcommandを実装済みという意味ではない。既存のcandidate、QA、独立review、approve、Current、配布手順に対応付けて運用する。

### 9.5 guideの内容

- quick：TRACK / STEMか2MIXを選ぶ、PRE/POST配置、POSTの接続先一覧から確認したいPREを選ぶ、最初の観測、戻り方。
- complete：二つのcontext、全domain、通常計測経路、比較試聴経路、Reference source A/B/C、ローカルBlind、Version、Check、保存・再開、評価、地域別購入、support。
- Reference：2MIXのVersionを第一例とする。Track/Stem Referenceを確認なしに一般化しない。

既存手順のcandidate、4項目producer QA、別sessionの8項目review、approve、Current、各配布先確認を省略しない。原稿作成sessionの自己点検を独立reviewの代わりにしない。候補段階の独立review後に候補が変わった場合は、変更範囲を特定して必要なQAとreviewへ戻る。

---

## 10. 外部掲載と連絡

### 10.1 優先候補

| 優先 | 媒体 | 現行の公式入口 | 提案 |
| --- | --- | --- | --- |
| 1 | KVR Audio | Developer Account。代替はsubmission page記載の連絡先 | 既存登録の版・format・無料範囲を訂正。release newsは実際の新規変更だけ |
| 2 | Bedroom Producers Blog | 公式Contact form | 無料で単体実用になるTRACK / STEMと2MIXの導入。独立評価を尊重 |
| 3 | DTMステーション | 公式CONTACT form | 日本語で、個別busから2MIXまでの実演と地域別購入条件 |
| 4 | Gearnews | 公式ContactのPR/news窓口 | 公開済み更新と短い実演。未公開候補をnewsにしない |
| 保留 | Audio Plugins for Free | 公式upload formはplugin ZIPそのものを要求 | 第三者hostへbinaryを複製すると更新・署名・三つの配布先の管理から外れる。公式download linkだけで掲載できる条件が確認できるまで送らない |

送信直前に窓口と条件を再確認する。KVRでは製品登録の現行化とrelease newsを分ける。既存機能を新機能として提出しない。KVRの公開方針どおり、誇張を避け、更新newsにはその版で変わったことだけを書く。

### 10.2 個別連絡

一斉送信をしない。相手の最近の内容とHyphaの一つの用途を結び、なぜその相手へ送るかを一文で書ける場合だけ送る。未返信のfollow-upは7～10日後に一回までとし、新しい理由がないまま追わない。

開発者本人であること、提供物、広告・対価関係を隠さない。良い評価や掲載を条件にしない。

### 10.3 コミュニティ

RedditやFacebookは各communityの当日rulesを確認する。Reddit公式も、promotional content自体を一律spamとはしていない一方、反復的・不要・無関係な投稿や大量DMをspamとしている。投稿許可済みのsubredditやgroupを本書で仮定しない。

---

## 11. 初期利用者と発信者

### 11.1 初期利用者12人

| 区分 | 目安 | 主に確認すること |
| --- | ---: | --- |
| TRACK / STEMから始めるHypha未経験者 | 4 | context理解、exact pair、個別処理の初回成功 |
| 2MIXから始めるHypha未経験または既存利用者 | 4 | 常設、mix全体、再利用、Referenceへの接点 |
| 既存Reference tool利用者 | 4 | OS導入負担、Version/Check、保存・再開、既存手順との違い |

12人は不具合と説明上の詰まりを見つける小規模pilotであり、市場全体のconversion推定には使わない。macOS/Windowsの対象範囲はRT結果に合わせ、未受入環境を「確認済み」として募集しない。

### 11.2 発信者候補

候補12件、最初の提案3件を上限の目安とする。無料Hyphaに無料copyを渡すこと自体は価値提案ではない。正規guide、権利処理済み素材、確認する用途、対応できる環境を渡す。

既存比較tool利用者には「A/Bできる」「loudness matchできる」だけを固有価値としない。たとえばMetric ABは複数reference、loudness match、DAW sync、cue/loop、各種meterを公式に提供している。Kirin側では、OSでVersion/Checkを管理し、Hyphaから呼び出し、Workと制作記録へ戻れる作業全体を検証対象にする。置換や優越を先に主張しない。

---

## 12. SNSと解説運用

### 12.1 投稿の型

困りごと → context → 実際の操作 → 観測できた事実 → 次の行動、の順にする。

| 区分 | テーマ | 次の行動 |
| --- | --- | --- |
| TRACK / STEM | vocal EQ、drum bus compression、stem saturation | 無料Hyphaで同じ境界を作る |
| TRACK / STEM | 4秒ローカルBlind | mixの中で二つの処理を聴く |
| 2MIX | mix bus EQ、limiter、session LEVEL | 2MIXに常設する |
| 2MIX | FREQ/SHAPE、SPACE MONO | 観測した変化を耳と照合する |
| Reference | 以前のVersion | OS評価で自分のmixを比較する |
| Reference | Check | 管理した比較曲を呼び出す |
| Reference | 保存後の再開 | 別の日に同じ比較へ戻る |

通常は週2本を上限の基本量とし、対応や検証が滞留したら減らす。無料1本、Reference1本へ機械的に固定せず、TRACK / STEM、2MIX、Referenceの偏りを週次で見る。

---

## 13. 効果測定

### 13.1 測定できること

| 段階 | 記録 | 境界 |
| --- | --- | --- |
| 認知 | 掲載、投稿、動画、公開日 | 転載を独立reviewと数えない |
| 訪問 | page、button、Reference案内の集計 | 個人、install、購入を断定しない |
| download | GitHub asset別増分、取得可能な販売側記録 | downloadを利用者数にしない |
| 無料初回成功 | 同意したpilot/supportでcontext別に確認 | pluginへ常時telemetryを追加しない |
| Reference関心 | page、動画、質問 | 評価開始と数えない |
| 評価 | 注文、activation、初回成功を別々に記録 | LS $0注文を利用成功としない |
| 購入 | LSと日本Stripeを分ける | 通貨を単純合算しない。目的を推定しない |
| 継続 | 7日/30日の同意済み確認 | 未回答、期間未到来を離脱としない |

GitHub Release Asset APIの`download_count`はfile単位であり、再取得、複数format、更新を含む。HP経由とGitHub直接取得を分けられない場合、「HP利用者数」や「Hyphaの総利用者数」と呼ばない。

### 13.2 context別に見る

無料初回成功は`TRACK / STEM`と`2MIX`で分ける。両方を一人が使う場合も、利用者数と成功event数を混ぜない。plugin内telemetryを持たないため、初期は同意pilot、support分類、任意surveyで確認する。

専用pageの訪問者がpluginから来たとは限らない。media、SNS、検索、共有linkを含む。source別linkや集計を導入する場合は、privacy、保存期間、cookie、個人識別の有無を別途承認する。

### 13.3 三つの台帳

1. **事実・版台帳**：事実ごとにrepository identity、repository HEAD、該当file path、fileを最後に変更したfull commit、blobまたは配布物hash、取得日時、取得経路、確認値、未確認範囲を記録する。公開表示、source内配布記録、利用者経路で取得した実物、実行結果、実機受入を別行にする。
2. **接触・掲載台帳**：相手、用途、context、送信、返信、掲載、次action。
3. **導入・成果台帳**：匿名ID、同意、環境、context、初回成功、Reference、再利用、購入の任意回答、未回答。

個人情報やclient名をPress Kit、公開guide、集計へ混ぜない。

「source内の公開配布記録にverifiedとある」ことと、「利用者と同じ経路で取得した実物を確認した」ことは別である。短縮commitしか得られない場合にfull commitを推測せず、未確認として残す。repositoryに無関係なdirty差分がある場合も、該当fileの状態とrepository全体の状態を分けて記録する。

---

## 14. 完了条件と90日間

### 14.1 準備phase

| Phase | 作業 | 次へ進む条件 |
| --- | --- | --- |
| P1 事実確定 | GP-01暫定、次releaseへ含めるREADME・製品文言、GP-00、D-G判断材料、公開訂正 | 公開版・候補・三つの配布先・package形式・OS取得物・地域経路・未確認が区別され、release source変更後の最終候補が固定される |
| P2 実機受入 | RT-01～18の該当範囲 | 公開対象環境で無料とReferenceの初回成功、ライブ入力への復帰、再開が確認される |
| P3 資料・page | GP-14候補guide、出荷照合、GP-14公開版、HP、専用page、動画、Press Kit | 候補guideが独立review済みで、出荷物との最終照合後にだけCurrent・配布へ進み、文言、操作、価格、link、版が一致する |
| P4 小規模展開 | 初期利用者、最初の媒体・発信者 | support可能な量で詰まりを記録し、次の拡大を判断できる |

公開中の誤情報訂正、候補調査、台本は並行できる。未受入機能を試せると案内したり、draft guideを完成版として公開したりしない。

### 14.2 統合キャンペーン開始条件

Daisukeが対象言語、地域、OS、DAW/format、Hypha版、Kirin OS取得経路を明示して開始を判断する。最低条件は次である。

- GP-01確定
- 対象RTの合格または公表可能な制限整理
- 正規guideの承認・Current・配布確認
- HP/専用page/製品内linkの整合
- supportと停止判断を実行できる余力

無料Hyphaの既存配布は継続できる。Reference評価や購入を促す統合campaignだけを、この開始条件で管理する。

### 14.3 開始後90日

| 観測窓 | 活動 | 判断材料 |
| --- | --- | --- |
| 0～30日 | context別の小規模pilot、Reference直行経路、最初の媒体/発信者 | 無料初回成功、Reference初回成功、ライブ入力への復帰、停止点 |
| 31～60日 | 実証できた用途の動画/FAQ、許可済み事例、再開支援 | context別再利用、OS導入負担、support量 |
| 61～90日 | 成果がある経路を最大二つへ絞る | 評価→購入、購入後再利用、止める施策と根拠 |

開始80日目に評価を始めた人を90日目に終了者扱いしない。購入後30日の確認が期間外なら「確認期間未到来」とする。

---

## 15. 実行task

| ID | 作業 | 完了条件 |
| --- | --- | --- |
| GP-00 | 最初の対象セットと、公開版・候補・配布先・配布物・地域経路を固定 | Hypha/OSの取得経路、取得物、full commit、binary/hash、実行環境、Reference用途、成功条件を一組にする。三つの配布先とPKG/ZIP/EXEを別軸で台帳化 |
| GP-01 | 境界文言の正本を作る | 二context、通常計測／比較試聴、Reference A/B/C、ローカルBlind、無料/OS、地域、対応環境の日英文言と転記先が揃う |
| GP-02 | RT-01～18を実施 | 結果、証跡、不明、対象外、Daisuke判断が記録される |
| GP-03 | README・製品内文言とHP英日4 pageを更新 | 次releaseへ入るsource変更は最終候補固定前に完了。公開中pageの既知の誤記は先行訂正し、新出荷版のpage・販売導線は出荷照合後に最終整合 |
| GP-04 | Reference専用導入page | guide、実演、評価、購入、supportへ到達でき、旧OS pageからも案内される |
| GP-05 | V1～V6・I1 | context、版、権利、無料/連携、流用範囲が正しい |
| GP-06 | Hypha Press Kit | 承認済みguide、画像、文言、地域linkを既存scriptで生成・検品 |
| GP-07 | KVR登録/news | 登録訂正と新release newsを分離し、実際の変更だけ提出 |
| GP-08 | 媒体へ個別連絡 | 送信、返信、follow-up、掲載、案内先を記録 |
| GP-09 | 初期利用者pilot | context、OS、初回、再利用、失敗、不明を分ける |
| GP-10 | 発信者候補12件 | 各相手に一文の具体的理由がある |
| GP-11 | 最初の3件へ提案 | 反応と導入詰まりを次素材へ反映 |
| GP-12 | 週次集計 | 同じ分母、期間、context、地域、経路で比較し、欠測を残す |
| GP-13 | 7日/30日継続確認 | 同意、未回答、期間未到来を分ける |
| GP-14 | product-media正規guide | 候補commitで6成果物、producer QA、独立reviewまで完了。出荷物との最終照合後にapproved、Current、配布確認を行い、二段階の証跡を分ける |
| GP-15 | Hypha監視対象修正 | **Daisuke承認後**、plugin repoを根拠にし、OS Hypha Supportと分離 |
| GP-16 | 二repo freshness | **Daisuke承認後**、両repoの変更・不明を検出し、既存schemaを移行 |
| GP-17 | パネル文言/URL/日本語UI | **任意・承認待ち**。採用時は候補・RT・三つの配布先・guideへ反映 |
| GP-18 | 任意評価mail同意 | **施策承認待ち**。opt-in、撤回、停止、回数、activation起点を確認 |
| GP-19 | context別素材・pilot matrix | TRACK / STEM、2MIX、Referenceの対象、画面、成功条件、禁止主張を対応付ける |

主依存は、GP-01暫定 → 次releaseへ含めるREADME・製品変更の確定 → GP-00最終候補固定 → GP-02 → GP-01確定 → GP-14候補guide制作・独立review → GP-00出荷照合 → GP-14公開版との最終照合・承認・Current・配布 → GP-03の公開page整合／GP-04／GP-06 → 外部展開である。

公開中の事実訂正は、この直列工程とは分けて暫定正本から先行できる。既存OS pageへReference説明を加えるためにpluginの新releaseや完成guideを待つ必要はない。ただし、事実訂正を先行できることと、未受入機能の利用・評価・購入を促す拡大を開始できることは別である。

---

## 16. 判断と停止条件

### 16.1 Daisuke判断待ち

| ID | 判断 | 先に揃える根拠 |
| --- | --- | --- |
| D-G01 | OS未起動で評価期限へ到達した場合の仕様 | RT-10/11、既存利用、offline、data非破壊 |
| D-G02 | product-media監視対象をplugin repoへ拡張するか | 現行config、Hypha Support、既存guide影響 |
| D-G03 | 二repo evidence/freshnessのschema | 現行schema、migration、片側変更試験 |
| D-G04 | パネル文言、専用URL、日本語UIを変更するか | 着地先修正だけで解決できる範囲、候補release影響 |
| D-G05 | campaign基準版と対象環境 | tag、配布物、RT、guide、support範囲 |
| D-G06 | 任意評価mailを行うか | consent、停止、担当、保存期間、privacy |
| D-G07 | Track/Stem Referenceを公開用途に含めるか | 現行Version/Check意味、対象source、実機結果、誤解の余地 |

### 16.2 拡大停止

次の場合、Referenceや購入を促す新規拡散を止める。

- 通常計測経路の透明性、0 samples、選択したPREとのpair、測定整合性に重大な疑義がある。
- 比較試聴からライブ入力へ安全に戻れない、別音源を無言で代用する、権利表示と実際の可否が食い違う。
- 対象版・OS・DAW・formatを特定できない問題が増える。
- guideより問い合わせが先に増え、一人運用で解決を追跡できない。
- 評価・購入条件の説明と配布物が一致しない。

既存無料版の適切な配布、事実訂正、修正案内、利用者supportは止めない。

---

## 17. 公開原稿案

### 17.1 無料Hypha

> Kirin Hyphaは、TRACK / STEMから2MIXまで、選んだPREとPOSTの間で何が変わったかを観測する無料プラグインです。vocalやdrum busの処理、mix busやmaster busのチェインを、音を聴きながらLEVEL、TIME、FREQ、SPACEで確認できます。通常の計測経路は音声を変更しません。

### 17.2 TRACK / STEM投稿

> VocalのEQ前後を、soloした数値だけでなくmixの中で確かめる。Kirin Hyphaを`TRACK / STEM`にし、PREとPOSTの間で観測した変化をFREQとLEVELで確認します。正解を出すためではなく、聴いている変化を見失わないために。

### 17.3 2MIX投稿

> Mix busの前と後を、同じ再生の中で見る。Kirin Hyphaの`2MIX`では、連続するmix全体を聴きながら、LEVEL、TIME、FREQ、SPACEを確認できます。

### 17.4 Referenceへの接続

> TrackやStem、2Mixの処理前後は、無料Hyphaで確認する。同じ曲の以前の書き出し（Version）や、比較用の音源（Check）とは、Kirin OSで管理したReferenceをHyphaから呼び出して聴き比べる。ReferenceにはKirin OSが必要です。

### 17.5 掲載依頼

件名：無料PRE/POST計測プラグイン Kirin Hyphaのご紹介

> Kirin Masteringの西尾大輔です。TRACK / STEMと2MIXの処理前後を、再生中に観測する無料プラグインKirin Hyphaを開発しています。PREを確認したい処理の前、POSTを後へ置きます。POSTの接続先一覧から、確認したいPREを選びます。通常計測はKirin OSなしで利用できます。
>
> 現行版、対応環境、無料範囲、短い実演をまとめた資料をご案内します。Kirin OSで管理した、同じ曲の以前の書き出し（Version）や比較用の音源（Check）を呼び出すReferenceは、無料の計測とローカルPRE/POST Blindとは分けて説明しています。

相手の媒体に合う理由を一文追加する。release newsでは、その版で実際に変わった点だけを書く。

### 17.6 地域別購入

必要なRTとGP-01が完了した後だけ使う。

- 日本国内：29,700円（税込）、Stripe、日本円の一回払い。
- 海外：$297、Lemon Squeezy、permanent license。
- 14日評価：日英とも同じLS evaluation商品入口。初回activationから14日、支払情報・subscription・自動課金なしという公開条件。

閲覧言語だけで居住地域を推定しない。評価終了後のReference挙動はD-G01決定まで付け足さない。

---

## 18. 公開前checklist

- [ ] 公開版、候補、Hypha/OSのfull commit、配布物、hash、取得経路をGP-00で固定した。
- [ ] Lemon Squeezy同梱、HP無料配布、Windows配布の三つの配布先と、PKG／ZIP／EXEのpackage形式を区別した。
- [ ] HPの公開linkと取得物を、release runbookの記載とは別に確認した。
- [ ] TRACK / STEMと2MIXを、機能差と用途を含めて説明した。
- [ ] Hyphaをマスタリング専用とも、全routingを観測する製品とも書いていない。
- [ ] 通常計測経路、比較試聴経路、Reference source A/B/Cを混同していない。
- [ ] ReferenceのAがライブ入力で、ローカルBlindは同じ4秒区間の取得済みcopyを使うと説明した。
- [ ] 比較試聴のgain条件を含め、全機能が無条件に音声非変更であるという表現を残していない。
- [ ] PRE名を必須またはpair identityとして説明していない。
- [ ] 無料計測、無料Local Blind、OS必須Reference、OS必須Recordを区別した。
- [ ] READMEの古いWindows版、TIME subview、AAX内部注記を最終候補固定前に次release sourceへ反映した。
- [ ] 候補固定後のsource／binary変更を新候補として扱い、影響する検証へ戻った。
- [ ] HP英日4 pageのReference説明、地域別条件、既存panel着地を揃えた。
- [ ] パネルの英日案内を、環境言語による自動切替と断定していない。
- [ ] LS評価の注文後取得物、version、hash、activationを確認した。
- [ ] macOSとWindowsの無料導入を別々に確認した。
- [ ] v1.1.50 Windows Reference証跡と、次候補の再受入を分けた。
- [ ] macOS Referenceを根拠なしに確認済みとしていない。
- [ ] RT-10/11とD-G01が終わるまで期限後の断定を書いていない。
- [ ] LS評価→LS永久とLS評価→日本Stripe永久を必要な経路で確認した。
- [ ] 5.1をmeasurement-onlyの範囲から広げていない。
- [ ] Track/Stem ReferenceをD-G07前に一般用途として宣伝していない。
- [ ] product-mediaのGP-15/16を承認・受入後にguideへ適用した。
- [ ] GP-14の候補guide制作・独立reviewと、出荷照合後のCurrent・配布を別の完了条件として記録した。
- [ ] 正規guide 6成果物が出荷物との最終照合、承認、Current、配布確認を通った。
- [ ] OS配布記録とproduct-media状態にrepository、full commit、file path、blob hash、取得日時がある。
- [ ] source内のverified記録と、利用者経路で取得した実物を別に記録した。
- [ ] 掲載依頼では内部用語を、実際の操作が分かる表現へ一段だけ翻訳した。
- [ ] 第三者siteへ管理外binaryを複製していない。
- [ ] 素材の音源権利、privacy、版、流用範囲を確認した。
- [ ] 任意mailは明示同意、撤回、停止が未整備なら送っていない。
- [ ] 専用page訪問をplugin起点または個人の利用と断定していない。
- [ ] 分母、期間、context、地域、未回答、確認期間未到来を分けた。
- [ ] 架空の対応、認証、実績、conversion、売上予測がない。
- [ ] Daisukeがcampaign範囲と停止条件を確認した。

---

## 19. この計画で最も厳しく検証すること

無料Hyphaの価値は、master busの最後だけにはない。vocal、drum、group、stemで一つの処理を確かめることと、2MIXで全体の流れを確かめること。その二つを同じPRE/POST境界で行えるから、次の制作にも挿しておく理由が生まれる。

しかし、それだけでKirin OSの導入理由になるわけではない。Referenceの価値は、無料機能を不便にして作るものではない。以前のVersionやCheckを管理し、制作中に呼び出し、比較を終えてライブ入力へ戻り、別の日に同じ作業へ戻れる。その全体が、もう一つのappを導入し、購入するだけの価値を持つかを確かめる。

最優先は、公開説明の事実訂正、評価配布物の同定、対象環境のReference受入、期限後挙動の判断である。次に、その事実をGP-01と正規guideへまとめる。その後でpage、動画、Press Kit、媒体、発信者へ広げる。

このv1.3以後は、一般論や承認項目をさらに増やすより、GP-00で最初の対象セットを一つ固定する。HyphaとKirin OSの取得経路・実物・commit・hash、OS／DAW／format、VersionまたはCheckの用途、初回比較・ライブ入力への復帰・保存後の再開という成功条件を一組にし、評価取得物の同定から実機確認へ進む。最初の一組へ絞ることは、製品全体の対応範囲や三つの配布先を縮小することではない。

目指すのは「無料だから取っておいたHypha」ではない。TRACK / STEMにも2MIXにも、次の制作でまた挿すHyphaである。そしてReferenceが必要な人には、Kirin OSを導入した後も、同じ比較作業を続けられる状態を作る。

---

## 20. 根拠

### 20.1 Hypha repo

- [README](../../README.md)
- [Hypha invariants](../hypha_invariants.md)
- [Meter product contract](../hypha_meter_product_contract_20260831.md)
- [Reference library receiver](../reference_library_receiver_20260913.md)
- [Release distribution runbook](../ls_release/kirin_hypha_ls_runbook.md)
- [v1.1.50 GitHub Release](https://github.com/heyalohaloha/kirin_hypha/releases/tag/v1.1.50)

### 20.2 ローカルsourceの固定根拠

| Repository | 再確認時HEAD | 根拠file | fileを最後に変更したcommit / blob |
| --- | --- | --- | --- |
| `kirin_hp` | `abcbd7bb9209d2472273d6c151485a1f9b1c4571` | `updates/publication-contracts/os.json` | `60bb198524016a838b33df049e4b83183e4852ff` / `92dfa497fff48d64f93eabc7cdfc95403e78fd80` |
| `kirin_sense_lens` | `fc3e41f89c967e264dbb2416e979eb7a193478a5` | `package.json` | `2183502cabaac9b1dd91d138602421bcc6b41107` / `d3aeec00ea13bb94379a8a04ddf39044b3f3a42a` |
| `kirin_sense_lens` | `fc3e41f89c967e264dbb2416e979eb7a193478a5` | `docs/product_media/product_media_config.v1.json` | `6cbbb10ecadaf64ef0c9a9ff74c8eacabdf65dae` / `9d9fa446fef210c8509c4b232adc34c643c9ce99` |
| `kirin_sense_lens` | `fc3e41f89c967e264dbb2416e979eb7a193478a5` | `docs/product_media/current_releases.v1.json` | `658b7c74820d8a1805c9d1099b43a1892e7a825b` / `ccc832f726197b2942369010dc4859335a6b71a1` |

再確認日は2026-09-25。各repositoryのHEADは取得時点の所在を示すが、事実の固定には該当fileを最後に変更したcommitとblobを使う。該当fileはcleanだったが、repository全体には本計画と無関係な未commit差分があった。利用者経路での注文・download・実行結果は、この表からは証明しない。

### 20.3 公開ページ

- [Hypha English](https://kirinmastering.com/hypha)
- [Hypha 日本語](https://kirinmastering.com/ja/hypha)
- [Kirin OS English](https://kirinmastering.com/os)
- [Kirin OS 日本語](https://kirinmastering.com/ja/os)
- [日本向けKirin OS購入](https://kirinmastering.com/ja/os/purchase)
- [Kirin OS 14-Day Evaluation](https://kirinmastering.lemonsqueezy.com/checkout/buy/005c2b74-6057-4392-8a1e-2604c377c2db)

### 20.4 外部公式情報

- [KVR Submissions](https://www.kvraudio.com/submissions)
- [Bedroom Producers Blog About / Contact](https://bedroomproducersblog.com/about/)
- [Gearnews Contact](https://www.gearnews.com/contact/)
- [Audio Plugins for Free upload conditions](https://www.audiopluginsforfree.com/upload-plugin/)
- [DTMステーション CONTACT](https://www.dtmstation.com/contact)
- [ADPTR AUDIO Metric AB](https://www.plugin-alliance.com/products/metric-ab)
- [Reddit Spam guidance](https://support.reddithelp.com/hc/en-us/articles/360043504051-Spam)
- [GitHub Releases REST API](https://docs.github.com/en/rest/releases/releases)

### 20.5 引継ぎ

次の担当者は、GP-00で最初の対象セットを一つ確定し、評価取得物の同定 → 対象環境での初回比較 → ライブ入力への復帰 → 保存後の再開 → guideへの反映、の順に進める。その前にD-G01～07、現在の公開tag、候補branch、三つの配布先を確認する。

本計画を読んだことを、product-media設定、公開page、plugin UI、注文、外部送信の承認にしない。変更した場合は、実行結果、対象版、未確認範囲、元へ戻す条件を同じ記録へ残す。
