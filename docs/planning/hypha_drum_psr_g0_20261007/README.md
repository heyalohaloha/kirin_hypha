# DRUM・PSR — G0の成果と残る成立条件

2026年10月7日。利用者の決定を含む `git diff e909fc1b 284d772b | git apply` を適用した後、文書・実寸wire・development計測を行った。2026年10月7日の方針変更で、G1以降を1.1.51へ含める。PR #83のmerge通知後のmainからCodexが実装し、Claudeが途中確認／CI／merge／releaseを担当する。公開前はG3と本人の日常操作・品位確認、友人G4は今後も公開後だけで開発工程・公開条件に含めない。G0調査時点では、製品build、配置、CI、公開、新commitは行っていない。

**G0の調査資料と試作は揃った。設計の閉じ方とCaptureの選択が残る。** G0で契約候補・校正手順・未検証一覧を整理し、新timestamp等を要する400 ms期限、DRUM look-behind、応答上限の負荷証拠はG1／G2で取り、G3 freeze前に最終値を固定する。下記のPASSは設計模型・限定観測の結果で、製品・実host・利用者の受入ではない。

## 1. 開く成果物

| 成果物 | 用途 |
| --- | --- |
| [親計画](../hypha_drum_psr_usability_improvement_plan_20261007.md) | 利用者の三決定、実施順、製品受入の条件 |
| [実寸・動的試作](preview.html) | 300×200～900×600、日英、主値／scope、PSR表示と操作模型 |
| [配置仕様](ui_layout_spec.md) | shell内の配分、固定font、共有指数、125%以上のBAND／VIEW |
| [snapshot契約](snapshot_contract.md) | 型付き区間、全N／確定部分、点参加集合、Single寿命、新版境界 |
| [TIME・Capture契約](time_capture_contract.md) | C／E、完了起点期限、consumerの不整合、v2設計と必要変更範囲 |
| [現行経路の観測](measurements.json) | lib hash、ABI、native二位相の全records、consumer fixtureと再現source |
| [独立算術モデル](model_checks.py)・[結果](model_measurements.json) | 手で指定した反例、区間／丸め／時計／期限／maskの23検証 |
| [wire検証](validate_preview.cjs)・[結果](layout_measurements.json) | ブラウザーの文字bounds、280条件、操作と2／4／8 Hz比較 |
| [保存画像](screenshots/drum_100_ja_bound.png)・[PSR paired](screenshots/psr_125_ja_whole.png)・[PSR solo](screenshots/psr_125_ja_solo.png) | 今回の合成wire。製品の画面ではない |

試作はローカルで開く。音声、plugin_data、実Work、外部networkを読まず、合成fixtureだけを描画する。外側toolbarは検証用。製品側のBAND／VIEW入口は125%以上のinstrument内へ別に置いた。友人による確認は今後も公開後だけで、development工程には含めない。

## 2. G0で具体化した設計

| 判断 | 採用した設計／候補 |
| --- | --- |
| PSRに何を出すか | pairedは `Δ −3.3 dB`、単体は `POST 10.1 dB`／`PRE …`。式はhelpへ。全体POST／ΔとPSR targetは別 |
| POSTだけでDRUMを使えるか | ATT／REL／LEVELはPOST実測。DELAYはPREが必要。計測値をコンプレッサー設定値として説明しない |
| 100%へどう収めるか | history34＋四値2×2（43 px×2）。target／band／scope／unit／短理由／限界／LIVE／根拠を残す。全帯域履歴と表示 |
| 125～300%の配分 | 125：24＋44＋90、150：24＋40＋100、200：22＋82＋144、300：32＋160＋220 px。文字は縮めない。150値軸は根拠面へ |
| VIEWの意味 | 既存OVERLAY／2 ROWS。Summary／Single切替へ流用しない。200%は64 px pane内へ枠を含め、各rowを分割 |
| 主値の母集団 | 全Nの情報を持つ中央値を優先。成立不能／全実数なら確定n/Nを明示。D4は全8≥＋200、確定−10（1/8）は根拠へ |
| 全実数の例外 | median availableとinformativeを分離。exact0×2＋全実数1の全体median0は有効。全実数7＋exact−10は明示した確定1/8 |
| 大きな有限区間 | 絶対値1000以上は共有指数をunit欄へ。±3202.6→[−3.21,+3.21]／×10³ dB。端点は外向丸め、raw値保持。最大指数308も模型で確認 |
| 平均図と単打 | 平均は同じ点参加集合のdB平均。集合変更で断線、中央値マークなし。Singleは同eventの値・形・理由・改訂を保持 |
| producer応答 | GUI stopで完成を偽造しない。1000 msを有限応答候補とし、非RTでRetired。NotKeptと応答不能を分離。期限後late replyは拒否 |
| 境界 | 旧structとKirinAbiContractは拡大しない。独立したversion／size／count／revision入口、混在拒否。ODFとband意味を別identityにする |
| Capture | G0を閉じる時にv2採用とv1維持＋明示失敗の二案を利用者へ提案する。v2は別repo変更が必要な候補であり採否未定 |

日本語の短名「立上」「頭音量」「確定7/8」「全体不定」は候補。現行の英語label契約とは異なるためG2で正本を同期する。意味の理解は未判定。

## 3. 検証結果を三つに分ける

### 設計模型の結果

ブラウザーwireは5サイズ×日英×2surface×14条件＝280ケースで、測った主値／scope／unit／短理由のbody・cellからのはみ出しと重なり0。最初の検査はmetric内とscope内だけを比較し、異なるrole間の208交差を見落としていた。独立review後に同cellの全role対検査を追加し、compact各行のline-heightを明示、125%主値はfont15のまま18 px行へ予約して再試験した。最終保存boundsのrole間交差0。glyphの高い125%／200%は面積配分を直した。16／24 px等の低い行へ無理に押し込まず、指定fontを保持した。14条件は全体、確定部分、限界、最大区間、共有指数、最大指数、全実数、全pending、片側・両側無音、solo、旧PRE、ALL最新、窓外LOCK。

固定三候補の操作模型でdragによる変更0、巡回、resize後同Single保持、EndでLIVE、D4の主面／根拠分離、instrument内BAND／VIEW、PSRの独立target、raw／表示区間のJSON保存を確認した。2／4／8 Hzの各2秒・500 ms欠落模型でV≤C、HOLD、一回の回復anchorを確認した。数値・shapeは合成例であり、これを測定した波形や統計とは呼ばない。

独立モデルは23 PASS／0 FAIL。Fraction／Decimalによる指定期待値で区間median、端点open／closed、外向丸め、IEEE有限最大値、注入時計、source退役、1000 ms後のlate拒否、同数別maskの断線を確認した。製品moduleのimportは0。pointのhalf-evenは模型の規則で、製品の最近接tie契約を変更しない。

### 現行native経路の観測

同一PRE／POST、48 kHz stereo正弦波を使い、既存libと旧ABIを限定観測した。各3.6秒、warm-up 0.6秒除外、native poll予定100 ms。Δendpointを入力slot供給から初めて観測する遅れは、位相30／75 msでmedian339.94／294.14 ms、P95 539.09／484.99 ms、max539.35／510.83 ms。C−E max500／400 ms、400 ms以上は6/30／1/30 tick。

これは原slotの**計測完了起点**の遅れではない。旧ABIにそのtimestampがなく、libのexact source provenanceも未確定。driverの供給・scheduler・file観察負荷、旧独立pollの競合を含む。実DAW、production新packet、最大遅延保証、400 ms TTL合格には使わない。

### 実consumerの再現

純validator／既存storeを使い捨てfake Workで確認した。現senderのcompact32hex UUIDはreceiverが拒否し、filenameも走査対象0。canonical dashedのv1は受理・PNG bytes保持。v1へcomponentsを追加するとfieldsで拒否。保存Workにはtype／path／notesだけが残る。これはG0参照sourceでの観測で、UUID修正はPR #83に含まれる。merge後のmainを再照合する。v1での新版metadata保持とv2 round-tripは未検証。採用方式に応じて新版添付の受入、または保持不能な添付の明示失敗を確認する。

## 4. G0の判定と残る条件

| 項目 | 状態 | 再開時の具体的作業 |
| --- | --- | --- |
| 主値優先、schema、互換境界、時刻・寿命、受入rubric | 文書化済み | G1実装前に採用sourceと全affected filesを再照合 |
| 主面実寸 | 模型の280条件はPASS | native fonts・全variant・Guide有無・PRE role・Windows DPI・SVG/軸/caption・input座標を正式候補で受入 |
| 完了起点400 msと正常待ち | 未検証 | 新completion timestampとcontrolled schedulerで、strict expiryと不要な空欄0の根拠を得る。旧観測を理由に期限を延長しない |
| DRUM look-behind／正常jitter | 未固定 | DRUM固有のpublication・取得遅延を測り、有限Lを校正する。TIMEの約500 msを転用しない。模型150 msは例示 |
| BAND 4 Hz | 第一候補 | 2／4／8 Hz模型は技術比較のみ。実数値の窓応答・読み取り・品位の診断を経て、G3の候補freeze前に固定 |
| 1000 ms worker応答 | 有限設計候補 | 密集二枠・idle・worker死活・応答不能で有限退役を検証。4 ms serviceをhard WCETと呼ばない |
| Capture方式 | v2案のschema・必要変更を整理、採否未定 | G0終了時にv2（別repo変更とround-trip受入）かv1（保持不能添付の明示失敗・ローカルPNG維持）を利用者へ提案。採用方式のfixtureと制限を公開前に確認 |
| 使いやすさ・高級感 | 未判定 | 公開前は本人の実UI・実再生の日常操作／品位。友人の初見等は今後も公開後G4だけで、開発工程・公開条件に含めない |

HTMLは精密なproduct rendererではない。scope／主要数値のboundsと操作模型を確認したが、平均shape／点maskは例示、値軸・分解能／source詳細は説明だけでnative同等に表示していない。historyクリックは固定三候補の模型で、実際のhit testing、通常dragの候補集合、空窓／expired PCM、producer late completion、lifecycle、再openを実装していない。styleは低忠実度でありnative素材・書体・高級感の合格ではない。PNG+metadataの製品Captureにも接続しない。これらをG2のPASSへ繰り上げない。

G0は契約候補・校正手順・未検証一覧とCapture提案を整理して閉じる。source変更なしでは取れない証拠はG1／G2で取得し、G3 freeze前に表示周期・正常jitter・閾値を固定する。G0模型PASSを製品PASSに読み替えない。Capture v2は別repo変更が必要であり、G0終了時にv1維持＋明示失敗案と比べて利用者へ提案する。G3と本人確認、G5、正式release gateで公開し、友人確認は今後も常に公開後だけに行う。

## 5. 公開前の本人確認と公開後G4の記録用欄

| 匿名role | 割当 | 現在の確認 |
| --- | --- | --- |
| D：利用者本人 | G3後、公開前に日常操作、数値・動き・品位 | 公開条件。初見には数えない。確認未実施 |
| F1：友人一人目 | 公開後G4だけ。未見なら100% scope課題と初見5課題、その後日常操作／品位 | 画面未見か、使用言語、初心者／経験者、参加可能性は未確認 |
| F2：友人二人目（いる場合） | 公開後G4だけ。F1と同じ区分 | 同上。F1を上書きせず各人の結果を残す |

各参加者の記録欄は「匿名role／言語／未見か／経験／candidate hash／size順／各課題の秒数と正誤／help回数／重大誤認／日常操作秒数／動的品位3項目の個別点／未達理由」。名前・連絡先は不要。本人の公開前結果と友人の公開後結果を分ける。友人は既見なら公開後に日常操作・品位だけ。友人確認は開発中や公開条件には今後も含めない。該当する未見参加者がいない言語・課題は未検証。日本語結果を英語PASSへ転用しない。外部募集・連絡・送信は行わない。

## 6. 再現と申し送り

模型算術は `python3 model_checks.py`。wire検証はPlaywrightとChromeを使い `node validate_preview.cjs`。依存runtimeは本体に同梱せず、必要ならNODE_PATH／G0_PLAYWRIGHTを設定する。G0_CHROMIUMを指定しなければ既存Chromeを隔離headless contextで使う。APIは[Playwright公式BrowserType](https://playwright.dev/docs/api/class-browsertype)を確認した。userのbrowser profileへ接続しない。Native probe sourceとhashはmeasurements.jsonへ保存したが、再実行時は採用libのprovenanceを先に確認する。

SECTION:DEVに相当する現在地：三決定patchとG0調査に続き、1.1.51へ改善を含める最新方針を反映。製品未変更。G0のCapture選択と校正方法を整理し、製品証拠未検証を追跡。G1はPR #83のmerge通知待ち。

日次ログ相当：2026-10-07 16時台 JST、B-1143 HEAD `6ba9fe20e681d369c1de08e1c74b1ef2406dc010`で文書保存。読取source B-1300 `bcb9ffd3b3913e9d5c4a526df38b4a7f430be99d`、consumer `9b2a055801939dbb5dd9d3b08befd17dbb76f2b7`。新commit／CI runなし。既存の他案件dirty treeを保持。

Handoff：PR #83のmerge通知後、その変更を含むmainからCodexがG1を開始する。Capture方式をG0終了時に利用者へ提案し、校正証拠はG1／G2で取得する。Claudeが途中確認・CI・merge・releaseを担当する。公開前はG3、本人の日常操作・品位、G5と正式release gate。友人G4は今後も公開後だけ。旧lib観測／模型PASSを正式候補へ流用しない。

セッション記録手順は読んだ。HyphaのNotion書込み禁止を優先し、SECTION:DEV・日次ログ・Notion Handoffは未記録。本書の引継ぎ内容だけを残した。LS／HP macOS／HP Windowsはskip。

### 専用ブランチへの保存

現在地：利用者の依頼により、取得した `origin/main`（B-1299、`0d89506ed276c37b041803704b87f7dbe94e2fa0`）から専用の作業領域を作成し、親計画と本フォルダーの28ファイルだけをB-1308で保存した。他案件の未commitファイル、製品source、ABIは対象外。

日次ログ相当：2026-10-07 17:36 JST、28ファイルのコピーhash、保存JSON／模型script hash、対象差分を確認。元の作業treeを保持し、push／PR／CI／公開は行わない。G0成立保留と三決定は維持。

Handoff：保存したG0成果を参照し、PR #83のmerge通知後のmainでG1を始める。未検証の証拠はG1／G2で取得する。資料の保存を製品やG0の合格に読み替えない。Notion三記録はプロジェクトの書込み禁止により未記録。

### 1.1.51方針更新（2026年10月7日）

現在地：DRUM・PSRのG1以降を1.1.51へ含める決定を計画と関連G0文書へ同期。公開前はG3の技術・実DAW受入と本人の日常操作・品位確認、友人G4は今後も常に公開後だけとした。製品G1はPR #83のmerge通知待ち。

日次ログ相当：2026-10-07 17:49 JST、公開文書から個別のbranch名を除き、G0校正手順とCapture採否未定を明示した。文書の整合・リンク・既存模型証拠のhashを確認し、公開文書検査のPASSを確認して計画PRを作成する。製品source／ABIと保存済み観測JSONは未変更。

Handoff：Claudeが計画PRの必須4チェック（公開履歴、macOS source、AU validation、Windows preflight）、mergeを確認する。CodexはPR #83のmerge通知後のmainから実装し、G0終了時にCapture v2採用とv1維持＋明示失敗を利用者へ提案する。途中確認／CI／merge／releaseはClaude担当。Notion三記録は書込み禁止により未記録。
