# Hypha サラウンド残作業の実行計画

初版: 2026-10-01。再作成・改訂: 2026-10-06、第2版。第3版: 2026-10-06、mainの再点検と利用者判断4件を反映。
状態: **計画書。以下の工程を今回実行したという報告ではない。**

**2026-10-06（第3版の後）: 利用者判断で、サラウンドは一旦保留する。** 他の機能を先に公開するため、公開する版はサラウンド無しとする。
R0は、build時の切替ではなく、plugin wrapperが5.1を宣言・受理する部分を外す形で行う。計測coreのexact 5.1とその試験は残す。
S0〜S7とS7の判断材料調査は保留し、再開は利用者の指示による。再開するときは、wrapperで5.1を受け付ける検証buildから始め、S0から進める。
N1のうち、mono/stereoの公式素材監査はサラウンドと独立に直す。

消失した未コミットの初版を、残存する申し送りと現行のsource・契約・証跡から再構成した。
以前の参照先を維持するため、ファイル名の日付は変更しない。
本書は残工程の実行順と判定方法を補う。製品仕様・署名・公開手順の正本を置き換えない。

## 1. 結論と完成させる範囲

**まずexact 5.1の計測専用経路を完成させる。** busを開けたことや6本のメーターが動いたことだけでは、
数値適合・長時間安定性・対象候補の実DAW受入まで完了したとは扱わない。

| 項目 | 今回の残工程の範囲 |
|---|---|
| layout | exact `L, R, C, LFE, Ls, Rs`。チャンネル数だけから役割を推測しない |
| 表示 | LEVEL / TIMEの計測専用。6役割のPeak / TP / Clipと共通Loudness / LRA / PLR |
| 通常音声 | PRE / POSTとも入出力bit identical、報告latency 0 samples |
| 閉じる機能 | INV-S53の出力の持ち主の表の全活動（LISTEN、LIVE BLIND、PRE/POST Blind〔PIN〕、VERSION BLIND、Aを下げる承認、B・C・Vの試聴、Keep／Record）、Reference、Hybrid VU、FREQ、SPACE。TIME配下の任意解析も開かない。チェーン経過時間（INV-LC25）は計らず、理由を示す |
| 公開build | S1〜S5に合格するまで、公開する版では5.1を受理しない（R0、2026-10-06利用者決定） |
| 非対応layout | 5.0、役割不明6ch、7.1.4などを拒否。無言のclamp、stereoへの代用、downmixをしない |
| AAX | 残工程から外さない。macOS / Windowsの通常Pro Toolsで対象候補のNative受入が必要 |
| 7.1.4 | 5.1の後続。開始条件・採用profile・公開metric・容量を第8節で判断する |
| 9.1.6 | 対象外。ABIの16 slotを製品対応の宣言にしない |

ReferenceやBlindのmono/stereo回帰は必要だが、5.1でそれらを開く計画ではない。
Kimeraは任意の付加価値であり、未搭載をbuild・署名・配布のblockerにしない。

**5.1の受入が済むまで、公開buildでは5.1を閉じる（2026-10-06利用者決定）。**
mainの5.1はB-1002から実DAW受入が済まないまま開いており、次の公開版と2026-09-23作成の1.1.51 LS用pkgにも入っている。
工程R0で公開buildの5.1を閉じ、S1〜S5に合格した候補で開く。

## 2. 基準と証拠の読み方

再点検したmain基準は`cd045837b4f4c28c08ade2cdb3ea707560b39ff7`（B-1229）。
確認時の作業checkoutは`6ba9fe20e681d369c1de08e1c74b1ef2406dc010`（B-1143）だった。
以下で参照するexact 5.1の製品範囲、role map、native試験、EBU helperは両者で一致する。
その他の機能まで両branchが同じとは扱わない。実行時にはmainの最新状態から採用候補を固定し直す。

2026-10-06に、origin/main `b9041accf47cfd6a84af9dd416c37eaeb70c614b`（B-1283、上の基準から109 commit後）を再点検した。
§10のrole map、ABI layout、Record/解析gate、5.1のUI/native試験、EBU試験とhelper、ingest容量契約に差分はない。
基準の後にmainへ入った次の面は、5.1での扱いをN6・N7・UI fixtureで確かめる:
ReferenceのA・B・C・V（VERSION BLIND、INV-S46〜S51）、出力の持ち主の表（INV-S53、mainの`juce_shell/src/OutputOwnership.h`）、
チェーン経過時間（INV-LC25）、光（INV-S45）、300%以上の説明（INV-S52）、言語（INV-S40）。

| 証拠ID | 確認できたこと | この証拠では閉じないもの |
|---|---|---|
| E01: 現行source | exact 5.1 bus、6役割map、計測専用gate、UI/native試験が存在 | 現候補の試験PASS、実DAW挙動、性能 |
| E02: B-1002 `c45c3caafa0b652acdd917c7fac8796962c636cd` | Windows Studio ProでPRE/POST VST3を5.1としてロード・再生、6役割表示 | 後続候補の受入、各役割の数値適合、6系統の物理出力 |
| E03: 同B-1002 | macOS Pro Tools Developerで未署名診断AAXをexact 5.1 Auxへロード | 通常Pro Tools向け署名候補の受入、Windows AAX受入 |
| E04: 同B-1002 nativeログ | Mac / Windows VST3の6ch realtime / offline透明性、0 samples、非対応layout拒否 | DAW保存・再open、実DAW数値適合、長時間負荷 |
| E05: 容量資料と現行test | history entryは344 B。24 instance × 2本の満杯ringは343.96 MiBの算術値（下記） | 現候補・現OSの実RSS、実DAWでの24時間安定性 |
| E06: 今回のsource読解 | 公式5/6ch素材を渡すEBU試験と、mono/stereo限定helperの不整合が残る | 実行時の失敗ログ。未実行なので再現済みとはしない |

E04のログの`6ch samples=179808`は6役割合計であり、1役割179,808 sampleではない。
現在の集計式では1役割29,968 framesに相当する。次の証跡は`frames_per_role`と
`scalar_samples_total`を分け、試験素材の長さ・反復数も記録する。
旧B-1002の報告書の「各役割179,808」という表記を新候補へコピーしない。

E05の内訳: 1本は21,843 entry × 344 B = 7,513,992 B（7.17 MiB）、engine 1台が2本、24 instanceで360,671,616 B。
entryの344 Bと合計の約344 MiBの数字の一致は偶然であり、片方からもう片方を導かない。

E02〜E04のローカル原記録: `hypha_b1002_exact51_implementation_validation_20260922.md`と関連ログ。
原記録は現在Codexの出力フォルダ（private）にだけある。S0でhash付きの控えを第9節の証跡置き場へ写す。
後続の5.1全受入PASSは今回探した記録では見つからなかった。「証跡未発見」と「試験FAIL」は区別する。
署名設定が存在することと、対象commitの署名済み成果物・host受入があることも別である。

## 3. 工程、依存関係、進捗の更新方法

| ID | 工程 | 着手条件 | 終了条件・成果物 | 現時点 |
|---|---|---|---|---|
| R0 | 公開buildの5.1閉鎖 | いつでも（S0〜S5と独立）。次の公開の前に必須 | 公開buildはexact 5.1をAU / VST3 / AAX、macOS / Windows、PRE / POSTで拒否し、検証buildだけが受理。README・INV-S33を同じ変更で更新。第7節R0の試験と必要CI | 実施中（wrapperの受付を外す形） |
| S0 | 候補・試験条件の固定 | main、既存差分、署名/CI/検証記録の棚卸し | candidate manifest、受入matrix、独立oracle、計測定義、復元控え | 未実施 |
| S1 | 数値と境界のfixture検証 | S0のfixture条件が固定済み | 第4節の正常・異常・境界の実測と、必要な構造修正 | 未実施 |
| S2 | 短時間のnative / DAW baseline | S1合格、正確なbuild identity | 第5・6節のbaseline。診断build結果は診断と明記 | 未実施 |
| S3 | 必要な構造修正と回帰 | S1/S2で欠陥を特定、影響する責務を一覧化 | 欠陥ごとの検出test、mono/stereo回帰、必要CI | 未実施 |
| S4 | 最終候補の署名・freeze・実DAW受入 | S3と短時間性能/満杯history fixture合格、必要CI green、candidate同一性 | A3固定とA4受入。第5節の全必須rowに証跡 | 未実施 |
| S5 | 性能・長時間受入 | 短時間で音声/数値が成立、性能対照buildを固定 | 第6節の比較、最終候補stress、復元確認 | 未実施 |
| S6 | 3チャネル配布と公開後確認 | S4/S5合格、5.1を受理する構成のcandidate、配布provenance、個別候補の公開承認 | 第7節の全配布物、再取得hash、A7まで合格 | 未実施 |
| S7 | 7.1.4着手判断 | 5.1残工程の結果と第8節の判断材料 | 明示的な範囲・profile・資源契約。実装承認後に別候補へ | 判断未確定 |

S2のbaselineは原因切分け用で、S4の最終signed candidate受入の代用ではない。
S5の試験設計・対照採取はS2から始めてよい。最終合格はS4と同じ候補に結び付ける。
S2/S3で短時間性能と満杯history容量の欠陥を解消してから署名へ進み、既知の性能FAILによる再署名を避ける。
最終signed candidateの実DAW性能・長時間stressはS5で維持する。
S1ではN1の試験入口修復と、再現したG/C型の数値誤り・無言変換の修正を行う。
原因未確定の共有状態/lifecycle再構造化はS2の観察後にS3で行い、baselineとの切分けを維持する。
修正でsource、JUCE実bytes、resource、toolchain、payloadが変われば影響するgateを再実行する。
7.1.4の未決事項やDownmix Researchを理由に、独立して進められる5.1検証を止めない。
S7の判断材料調査もS6のLS/HP担当者待ちと独立して進められる。製品実装の開始判断とは分ける。

R0は5.1検証と独立に進め、S1〜S5に合格するまでの全ての公開版に適用する。
S1〜S5は5.1を受理する検証構成で行う。S4で署名する最終candidateは、公開の既定を「5.1受理」へ切り替えた構成で作り、
そのままS5・S6へ進む。
release入口との対応: S0はA0（宣言host範囲と追加の数値閾値の照合）、S4はA3（freeze）とA4（実host matrix）、
S6はA5（packaging・clean install・全署名・Rollback）からA7（公開後のinstall確認）までに当たる。
数値閾値と期待値は遅くともA3の前に固定する。

### S0で固定するもの

- exact commit / B番号 / version、sourceとJUCE patchの実bytes、build profile / toolchain、OS / architecture、format、PRE/POST hash。
- host/version、fixture/hash、DAW bus role順、device設定と実callback長、pair identity、epoch/resetの条件。
- rowごとの`expected` / `measured` / `oracle` / 単位 / 許容差 / 証跡path/hash。期待値は測定後に緩めない。
- 比較前後で同じSong複製・再生区間・routing・他plugin・editor条件を使う。単なるbypassを「Hyphaなし」としない。
- privateな運用値・顧客情報・署名認証はignored stateへ保持し、公開文書やログへ出力しない。
- mainへ統合する対象を明示する。利用者の既存差分を消さず、別branchの未完了機能を無断で取り込まない。
- AAXを含む候補は、B番号の付いたmergeでないcommitにする（diagnostic signはmerge commitを拒否する）。
  mainのmerge commitを採る場合は、同じtreeのB番号commitを`git diff`で確かめて使う。
- ARM64の実DAW受入は、利用者の友人のApple Silicon Macで、完成した候補（S4で署名した最終candidate）を公開前に試してもらう
  （2026-10-06利用者決定）。開発用のiMacはIntelのため、S2のbaselineはIntelだけで行う。
  友人の機種・macOS・hostの版・使えるformatは、試してもらう前にprivateの記録へ残し、gitへ入れない。
  渡すもの（配置と実ロードmoduleのhashを出すscript、fixtureのcopy、手順、記入用の表）をS0で作る。
- 1.1.51（5.1入り）をLSへ配布したかを確かめる。配布済みなら、R0の版で5.1 sessionを開いたときの挙動と告知を決める。
- 5.1の基準の版を固定する: ITU-R BS.1770-5、EBU Tech 3341 / 3342とLoudness Test Set v05
  （archive SHA-256 `9cc500b4df83f7c21855c74dce795ef5209a752bf884253ae57d0ce512efb062`）。
  2026-10-01付のBS.1770改訂草案（R23-SG06-C-0199）は、正式な本文を確かめて利用者が採用を決めるまで基準にしない。
- 既存のgreen runのlogで、arm64のauvalとVST3のpluginvalが6ch（5.1）の設定を実際に通しているかを確かめる。
  確認のために新しいCIは起こさない。
- 文書の食い違いを直す。INV-S33の禁止一覧にlive比較が無い（AGENTS.mdとREADMEにはある）ため、INV-S53の表を指す形に揃える。
  AGENTS.mdの「processメソッド単体0.1%」と[LB15](hypha_one_pass_live_blind_implementation_plan_20260929.md)の
  「process全体」の表現を、第6節の定義に揃える。

## 4. S1 — 数値・役割・拒否境界を先に閉じる

### N1: 公式素材の入口を修復する

現在の`tests/ebu_v05_reference.rs`は5/6ch素材を`support::measure`へ渡す一方、
`tests/support/ebu_v05.rs`のhelperは`mono_or_stereo_by_count(...).expect(...)`を使う。
この不整合を最初に再現し、旧「全70素材PASS」を現行5.1の合格根拠にしない。

helperが`mono_or_stereo_by_count`を使い始めたのはB-956（2026-09-18）で、5/6ch素材の行はB-605（2026-09-01）からある。
コード上、最初の5ch素材（`seq-3341-6-5channels-16bit.wav`）でpanicし、同じ`#[test]`の後続
（3341-7と3341-2011-8のI値、Tech 3342のLRA 6件）も実行されない。未実行のため、再現はまだ確認していない。
したがって2026-09-18以降は、5.1だけでなくmono/stereoの公式素材監査も未確認として扱う。
READMEの「EBU v05の70素材すべて」は[2026-08-31の監査](../hypha_bs1770_5_r128_v5_audit_20260831.md)の記録であり、
N1修復後の再実行結果で日付と範囲を更新する。
N1で6ch素材を5.1製品の証拠に使う場合は、READMEの「5.0/5.1素材は5.1製品の証拠に使っていない」という記述も同じ変更で直す。

素材の役割・channel mask・参照値を実読して、以下の試験を分離する。

1. mono/stereoの既存数値監査。
2. 製品が受理するexact 5.1の数値適合とnative/host経路。
3. 5.0等の素材decoder/role確認と、明示mapを使う非製品core・独立参照の既存公式数値監査。
   既存5ch素材のI値検証等を落とさず、製品の5.0 bus拒否とは別の試験として保持する。

5ch素材へ無言で6ch mapを当てたり、5.0を製品対応に変えてtestを通したりしない。
各素材の公式期待値・許容差をmanifestへ記録する。LUFS-Mの既存±0.1 LU契約も維持する。
別の測定窓やmetricの許容差を流用せず、TP / LRA / I等は対応するoracleを個別に固定する。

### N2〜N7: 必須fixture

| ID | 入力・操作 | 合格条件 |
|---|---|---|
| N2 役割識別 | 6役割を単独で順番に励振。識別可能な周波数/振幅/パルスを組合せ、同重みroleの入替も試す | Peak / TP / Clipの値と名札が6役割すべて一致。残り10 slotは未測定。LUFS合計だけでrole一致を判定しない |
| N3 LFE/重み | LFEのみ、frontのみ、surroundのみ、組合せ | LFEはloudnessから除外し、Peak / TP / Clipは観測。独立式・採用profileと照合し、製品mapから期待値を生成しない |
| N4 数値・透明性 | 既知レベル、無音、閾値直下/同値/直上、超過パルス、realtime/offline | 数値がoracle内、欠測を0で代用しない。全6役割のA音声不一致0、non-finite追加0、latency 0 samples |
| N5 PRE/POST | PRE単独、POST単独、exact pair、既知gain chain、role/layout/rate不整合、片側再起動/失効 | absoluteは維持。Δは検証済み同区間だけ。異なるepochの番号が偶然同じでも一致としない。拒否理由と欠測を捏造しない |
| N6 状態/更新 | reset、停止/再生、無音、bypass、callback停止、editor開閉、bus/rate変更 | 窓・resampler・historyが世代を跨いで混ざらず、名札と値が同一snapshot。WAITING / BYPASSED / FORMAT HELD / 5.1 MEASURE（100%・125%の折りたたみ表示）を区別 |
| N7 禁止機能/復元 | INV-S53の表の全活動とReference・Hybrid VU・FREQ・SPACE・TIMEの任意解析を、UI操作と内部開始APIの両方で試す。チェーン経過時間、stereo stateの5.1復元、未知layout、旧/未知ABI、壊れた/不在の公開セル | 5.1の禁止機能をUIと内部gateの両方で拒否（INV-S53の`layout`理由）。チェーン経過時間は計らず、5.1では計測しない旨の理由を示す。明示操作失敗は通知、非操作fallbackはR-28に従う。通常A音声は継続 |

チェーン経過時間（INV-LC25）はlive比較のPRE→POST経路に載り、その経路は2chまでしか扱わない
（[LiveCompareSession.h](../../juce_shell/src/live_compare/LiveCompareSession.h)）。
5.1では計れず、今の理由表示も5.1向けではない見込み（コードからの推定、未実測）。
既定では5.1で計らず、5.1では計測しない旨の理由を示す。

rateは44.1 / 48 / 96 / 192 kHz、blockは32 / 64 / 128を中心に可変長・非整除blockもfixtureで扱う。
現行上限262,144 framesの直下・同値・超過はhost負荷試験ではなくnative境界試験にする。
超過時もA音声は不変で、計測欠落は正常値として公開しない。
native入力rateと48 kHz Watch、100 ms cadence、400 ms / 3 s窓を混同しない。

UI fixtureは実editorの全対応倍率、PRE/POST、停止/保持/不整合の組合せを覆う。
言語（英語／日本語、INV-S40）、300%以上の説明の一行（INV-S52）、光の規則（INV-S45）も軸に含める。
600%は開発用iMacの画面（最大450%）では表示できないため、offscreenの描画試験で確かめ、実画面で見た倍率と分けて記録する。
最小画面の文字/矢印の重なり、6役割の名札、旧背景混在、切れた単位も確認する。
破線は禁止。新しい表示や任意解析を増やして軽さを損なわない。

既存の7.1.4上方roleによるmap適用検出testは機構回帰として維持する。
5.1 default mapとの偶然の一致に頼らないための検出器であり、7.1.4の製品対応宣言ではない。

## 5. S2 / S4 — 実DAWでのGate F2

### 必須host/format表

| 実行環境 | format / host | B-1002の履歴 | 最終候補で必要な確認 |
|---|---|---|---|
| Windows x64 | VST3 / Studio Pro | exact 5.1ロード・再生の部分証跡 | 署名済み候補、6役割既知信号、数値、保存/再open、bus遷移、stress |
| Windows x64 | AAX / 通常Pro Tools Native | 診断buildのみ。当時host試験skip | scan/load、exact 5.1 multichannel bus、役割、通常透明性、保存/再open、Offline Bounce |
| macOS Intel / ARM64 | VST3 / Studio Pro 8 | native契約試験 | architectureごとの実ロードとF2。stereo-only hostをsurround PASSにしない |
| macOS Intel / ARM64 | AU / Logic Pro | この報告で実DAW全受入なし | AU layout交渉、実channel順、F2 |
| macOS Intel / ARM64 | AAX / 通常Pro Tools Native | Mac Developer診断hostの部分証跡 | 対象signed candidateのexact 5.1 F2。multi-monoを単一5.1 busの代用にしない |

host名/versionと使用可能なarchitectureはS0/A3で確定する。
対象hostが5.1を扱えない、通常Pro Toolsや必要機器が無い場合はそのrowをPENDINGとし、
別formatの成功で相殺しない。製品の対応宣言を縮める判断は利用者へ確認する。

2026-10-06利用者決定: macOSのAUはLogic Pro、VST3はStudio Pro 8で受け入れる。WindowsのVST3と同じhostにそろえる。
ARM64のrowは、友人のApple Silicon Macで最終candidateを試してもらう。結果は候補のhashと結び付けて記録する。
友人のhostに無いformatのARM64 rowはPENDINGとし、別formatやIntelの成功で相殺しない。
macOSの通常Pro ToolsはUltimate（2026-09-28の記録で26.4.1）が5.1を扱える。
WindowsのAAX rowは、検証機へ通常版Pro Toolsを入れることが前提（2026-09-29の記録ではDeveloper版だけ）。
Pro Tools DeveloperのiLok cloud licenseは同時に1台で、Windowsで使う間はMacのPro Toolsが動かない（同日の記録）。
通常版も同じ制約かをS0で確かめ、両OSのPro Tools rowとstressは日程を分ける。

### 各rowで行う手順

1. 配置と**実ロードmodule**のpath/hashを照合し、旧候補の残存を排除する。version文字列だけで同一性を認定しない。
2. 専用Song/sessionに6役割をデジタル配線し、N2/N3の信号を各roleへ投入。スクリーンショットと数値/raw結果を対応付ける。
   Pro ToolsはimportしたWAVを書き換えるため、fixtureはcopyをimportし、終了時に`test_signals/`に差分がないことを確かめる。
3. PRE単独、POST単独、exact pair、既知gain chainで、absoluteと許可されたΔを別々に測る。
4. reset、停止直後/安定後、再生、bypass、editor開閉、session保存→host終了→再openを確認する。
5. stereo→5.1→stereoとrate変更を確認。保留中の旧値と、新世代の値をつなげない。禁止機能は復元後も開始しない。
6. realtime / offlineで通常音声、0 samples、全role、時間窓を確認。可能なデジタルcapture/比較結果を残す。
7. 終了時に一時設定・配置・Songを控えと照合して戻し、復元未確認を完了にしない。

6役割の**デジタル入出力検証**と、6スピーカーの**物理配線・聴取**は証跡を分ける。
旧RealtekのL/Rのみの聴取から、6系統物理出力を確認したとは言わない。
物理試験は既定でN/Aとする。5.1は計測専用で、通常音声のbit identityはデジタル比較の方が強い証拠になるため。
製品宣言に物理出力の確認が要ると判断した場合だけ、必要機器・復元手順を具体化する。未実施をPASSにしない。
機器操作前にはWindows remote access / Audio Routesの正本手順を読む。
既承認範囲の検証は進め、新しい機器設定変更・物理操作が必要なときだけ事前確認する。

## 6. S5 — 軽さと長時間安定性を数値で判断する

### 短時間matrix

48 kHz、32 / 64 / 128 samplesで、まずHyphaなし / stereo / exact 5.1を同条件で測る。
各blockの公称音声時間は0.667 / 1.333 / 2.667 ms。これをhost全体の余裕やDAW表示CPU%と同一視しない。

計測の道具: process単体の費用は既存の[live_compare_processor_benchmark](../../juce_shell/tests/live_compare_processor_benchmark.cpp)
（`Benchmark*.h`）と[watch_cpu_components](../../crates/kirin_measure/examples/watch_cpu_components.rs)を出発点にし、
5.1を測る入口が無ければS0で作る道具として数える。実DAWのcallback時間はmacOSではInstruments、WindowsではWPR/WPAで取り、
取り方をS0で固定する。

| 軸 | 固定する条件 |
|---|---|
| 個数 | PRE単独1、POST単独1、PRE+POSTを1 / 2 / 4 / 8 / 12組、および棚卸しした実運用数 |
| 状態 | 再生、停止直後、停止安定後。明示bypassは別条件 |
| UI | 全editor閉鎖、LEVEL、TIME/HISTORY。開いた個数・倍率・pageを固定 |
| pair | 無pair / exact pairを区別。失効試験は通常負荷と混ぜない |
| 反復 | 30秒warm-up後60秒を3回。順序を入替え、温度・背景build/他process負荷を記録。開始時のload averageと他worktree・Codexのbuildの有無を記録し、重い処理が動いている間は測らない（他のbuildでload averageが25〜60になり、paint時間・parity試験が落ちた記録がある） |
| 指標 | process user/kernel CPU、最繁忙core、callback時間分布/最大、DAW負荷/overload、dropout、overflow/欠測、RSS、serialize/replace/bytes/fsync、履歴鮮度 |

HyphaなしにUI軸・pair軸は適用せず、同じ試験を重複起動しない。
任意解析が開かない5.1にFREQ等の負荷matrixを足さない。mono/stereo回帰は別枠にする。
**上表の全軸を直積にはしない。** 次の有効rowをS0で列挙し、同一条件の重複は1回の反復群にまとめる。

| 試験群 | 必須row | 切り分けるもの |
|---|---|---|
| P0 baseline | 各block × Hyphaなし / PRE単独1 / POST単独1 / exact pair 1組 / 実運用数。editor閉鎖で再生/停止 | 単独処理と通常pairの費用。単独PRE/POSTにpair有無の二軸を作らない |
| P1 scale | 各block × exact pair 2 / 4 / 8 / 12組。editor閉鎖で再生/停止 | 個数に対するCPU/RSS/I/Oの伸び。P0の実運用数と重なるrowは再利用 |
| P2 UI | 各block × exact pair 1組 / 実運用数 × LEVEL / TIME-HISTORY。表示個数・倍率を固定して再生/停止 | 表示追加の費用。P0閉鎖を対照にし、全個数×全倍率へ機械的に増やさない |
| P3 pair | 64 samples × PRE+POST 1組 / 実運用数、無pair / exact pair、editor閉鎖、再生 | 同じ配置で接続有無の費用。無pairの作り方（PREとPOSTを別trackに置く等）はS0で固定する。pair失効/復帰は別のnegative runにする |

停止直後と安定後は停止run内で時刻を分けて集計し、遷移を平均に埋め込まない。
主要なWindows Studio Proと各Mac architectureの代表hostでP0〜P3を実施する。
ARM64は友人の環境で行うため、P0〜P3のうち頼める行をS0で決め、頼めない行はPENDINGとする。
他の必須host/formatでもP0と実運用数のUI/stressを測り、差やoverloadがあればP1/P2/P3を展開する。
共有coreだから別formatの実DAW性能確認を不要とはしない。適用したrow・展開理由・未実施を記録する。

性能修正の効果は、同一baselineへ性能修正だけを加えた対照buildで比較する。
旧modified-source配置と現在の多数変更済み候補の差を、今回の最適化効果と呼ばない。

### 長時間と容量

- 10分smoke→2時間実再生/停止遷移→24時間stressの順。短時間で欠陥が出たら同じ失敗を長時間反復しない。
- 1組と実運用数/宣言最大構成を必須stress対象にし、block/UI/hostの理由をmanifestへ固定する。
  全短時間matrixの全rowを無根拠に24時間へ展開しないが、長時間gateを短時間PASSで代用しない。
- 24時間stressは既定で、Windows rowはWindows検証機、macOS Intel rowは開発用iMacの夜間で行う。
  ARM64の性能・長時間は友人に頼める範囲をS0で確かめる。頼めない行はPENDINGのまま5.1検証完了のblockerに残り、
  その扱いは利用者に確認する。
- 満杯history fixtureでは現行344 B entryを実際に書き込み、PRE/POSTのどのringが何点埋まったか記録する。
  24 instance × 2本 = 344 MiBは論理容量。旧328 B entryのLinux RSSを現在のPASSにしない。
- 現行5.1計測専用の既知pipeline算術値324,108,288 bytesはprocess RSS上限ではない。
  legacy 384 MiB、旧Record込み5.1見積り、現在の実RSSを混同しない。
- 新規processを対照にし、historyページをtouchした後のRSS・private bytes等をOSごとの定義とともに測る。
  加速fixtureの容量検証と、実DAWの24時間スケジューリング耐性は別成果物にする。

### 判定

通常Aのsample不一致/追加non-finite/latency、RT alloc/lock/blocking I/O、説明できない欠落は0。
正常受入区間のdropout・overflow・測定endpoint欠落も0を要求する。意図的な超過/失効のnegative試験は別集計。
CPUの定義と合格線（2026-10-06利用者決定）:

- 既存の「process単体0.1%未満」は、1 instanceのcallback時間 ÷ block時間（1コア）と定義して記録を続ける。
  2026-10-05の開発用iMacの記録では、stereoのPRE+POSTが64 framesで1組4.3 µs（1 instanceあたり約0.16%）であり、
  0.1%は合格線ではなく目標として扱う。
- 5.1の合格線は、同じhost・rate・block・UI・pair条件のstereoと比べて、1 instanceあたりのcallback時間がp50とp99.9の
  どちらも3倍以内（6ch ÷ 2ch）、かつcallback時間のp99.9がblock時間の5%以内とする。
- process全体CPUやDAW表示へ読み替えない。

S0では計測定義と既知の不変閾値を固定し、S2で実測baselineを採取する。
RSSの追加上限・許容退行はその対照と製品宣言に基づき、遅くともA3の前に固定する。
この判断が必要なら材料を添えて確認する。測定後の後付け閾値や、未定のままの性能完成宣言はしない。

平均だけでなくp95/max・周期的peak・deadlineを比較し、audio/measure/UI/IOの実行と待機を同じ時刻軸へ合わせる。
fsync内の待機やstack sample頻度をCPU実行時間に変換しない。取得不能な項目はN/Aで、0扱いしない。
payload revisionが不変かつ公開先健全な区間の履歴再serialize/replaceは0。初回/reset/retry/外部削除後再作成は別計数。
必要なalive leaseを削って更新ゼロを作らず、Guide消失・pair解除・Record退行を改善の代償にしない。

## 7. S3 / S6 — 回帰、AAX、公開を閉じる

### 修正と必要検証

欠陥の再現条件・原因・影響する責務を先に確定し、表示だけの隠蔽や個別場当たり修正にしない。
巨大sourceへ製品変更を加える場合は対象責務を先行抽出し、500行規約と行数ratchetを守る。

- Rust変更では対象test、workspace test、clippy、RT/source契約、関係するnative testを実行する。
- FFI変更ではignored parity / pairing_candidatesを一覧から実測して全件実行し、通常workspace greenで代用しない。
- mono/stereoのWatch、Record/Keep、ReferenceのA・B・C・V（VERSION BLIND、Aを下げる承認、VのAUTOを含む）、
  C Check中のA確認、PRE/POST Blind（PIN）、LIVE BLIND、live比較（LISTEN）、チェーン経過時間、保存/復元を回帰する。
  5.1での禁止とmono/stereoでの継続使用を両方守る。
- 必須release-source gate、required CI、変更影響の全OS/architecture/format matrixは維持する。
  文書だけの更新には無関係な重いbuildを起動しない。
- push/dispatch/rerun前に共通予算ルール、trigger、exact commit/workflow/inputs/signing条件、既存run/artifactを照合する。
  必要CIは実行し、失敗ログと次の仮説なしの反復はしない。新規/再利用run IDと理由を残す。

### R0 — 公開buildの5.1閉鎖

- 閉じ方: plugin wrapperが5.1を宣言・受理する部分（`KirinJucePluginConfig.h`の`{6, 6}`と`isBusesLayoutSupported`の5.1）を外す。
  サラウンドを保留したため、検証buildを並行して保つ切替は作らない（2026-10-06）。runtimeの設定や隠し操作で開けるようにしない。
- 対象: AU / VST3 / AAX、macOS / Windows、PRE / POSTのすべて。
- 試験: 公開buildでexact 5.1・5.0・役割不明6ch・7.1.4を拒否し、mono/stereoの受理と挙動は変えない。
  計測coreとUI契約の5.1試験は走らせ続け、閉じたことで5.1の試験が止まらないようにする。
- 文書: README・INV-S33・HP・リリースノートの5.1の記述を、公開buildでは使えない状態に合わせて同じ変更で直す。
- 閉じた状態でも、Pro Toolsは5.1 trackへHyphaをmulti-monoで挿せる。これは各chのmono計測で（5.1を持たないv1.1.50と同じ扱い）、
  5.1の計測ではない。この区別を文書に書く。
- 開く: 再開時に、wrapperで5.1を受け付ける変更を検証buildとして入れてS1〜S3を行い、
  S4で署名する最終candidateで公開の既定にしてS5・S6の対象にする。

### 対象候補の公開条件

統合入口は[build / release入口](../hypha_release_entry.md)に従う。
AAXは[署名入口](../aax_build_signing_entry.md)から既存のMac Keychain / Windows private factoryを確認する。
設定情報が既にあるのに顧客番号や秘密値の再送を求めない。環境存在、実署名認可、成果物生成、実host受入を分ける。

1. source/asset用途と配布licenseを確認し、必要CIと正規producerでMac 6本、Windows 4本を同一commitへ結ぶ。
2. AAXはPACE + AppleまたはPACE + Authenticodeの対象候補を検証。Native-only、signed provenance、提出archive/hash、Mac公証receiptを照合する。
3. A3でcandidateをfreezeし、A4の実host matrixを閉じる。第5節の5.1受入と、mono/stereo比較試聴の受入を別rowにする。
4. payloadを保全してLS PKG、Mac ZIP、Windows署名済みinstallerを作る。NOTICE/license/対応sourceとpackaging前後のhashを確認する。
5. Windowsはpayload / installer / uninstallerの全署名、install→同版再install→旧公開版からupgrade→uninstallを確認する。
6. **LS、GitHub Release + 英日HP、Windowsの3チャネルを同版で揃える。** AAXを抜いた候補やMacだけを公開完了にしない。
7. 現行HPの通常導線はMac PKG / Windows EXE。署名・公証済みMac ZIPも公開Releaseに保持し、復旧導線として検証する。
8. LS authenticated uploadは担当者工程。各配布物を実downloadしhash照合し、HPを標準clean deployで本番反映する。
9. HPまでの`RELEASED`と、公開物install/scan/pair/音声/session reopenまでのA7 `RELEASE_COMPLETE`を区別する。

旧署名済みbundle・同versionの別commit・diagnostic AAX・手動Windows VST3 ZIPは、上記の代用品にしない。
必要外部検証やoperator工程が残れば、具体的な停止row・必要物・再開条件をblockerとして残す。
この計画書の作成は、課金・署名機器操作・upload・個別候補の公開承認ではない。

## 8. S7 — 7.1.4へ進むための判断

**「内部で12chを測れる」と「7.1.4が製品として完成している」を分ける。**
5.1の成果を見て、以下を確定した後に製品busを開く。黙ってPhase 2へ延期したり、無断で範囲を広げたりしない。

| 判断 | 先に作る材料・合格条件 |
|---|---|
| 開始時点/公開metric | 5.1受入の残り、次候補の責務、7.1.4も計測専用にするかを明示して承認 |
| loudness edition/profile | 採用一次資料・適用範囲・role別重み・独立期待値を固定。必要なvendor追補を特定。2026-10-01付のBS.1770改訂草案（R23-SG06-C-0199）は正式な本文で評価する |
| 12役割とformat map | host/formatごとの宣言と実順を照合。未知layoutやrole欠落は拒否 |
| history/保持構造 | 12roleの保存内容と満杯容量、実RSS、公開更新量。16 slotを全metricへ機械的に拡張しない |
| selector/UI/永続化 | indexではなくrole。測定対象/名札/世代を一体化し、layout変更と復元で別roleの履歴を連結しない |
| 負荷とhost受入 | 5.1と同じ精度・透明性・拒否/復元・性能判定に12role/高さを加え、実hostで再検証 |

現vendorのside=1.41、rear/upper=1という挙動は現行profileの事実であり、将来採用する規格版の普遍的oracleではない。
版/profileを未決のまま、この重みを受入条件へ固定しない。

承認済み3Aの意味は再承認待ちへ戻さない: Attackはselected view、Correlationは左右対overview＋選択pair詳細、
Balanceは同じPairRoleごと、MONOはサラウンド適用外。overviewを平均やworst pairの単一値へ畳まない。
ただし現在の5.1でこれら任意解析を開く許可ではなく、後続で開くmetric範囲は上表で判断する。

Downmix Observation、renderer由来Stereo Direct、7.1.4→7.1係数のResearchは独立工程。
新しい測定量やrendererの実出力と固定行列を混同せず、基本7.1.4計測の着手条件へ一括で混ぜない。
保持構造→予算→利用条件の順に検討し、軽さのためのinstance数/機能縮小を無断で決めない。

## 9. 完了判定、停止条件、保存・引継ぎ

### 完了を三段階で報告する

1. **5.1検証完了**: S1〜S5の必須rowが対象候補でPASS。未実施/未取得を合格扱いしない。
2. **公開リリース完了**: S6の3チャネルとA7が同じ候補でPASS。5.1検証だけで配布readyと言わない。
3. **7.1.4**: S7の判断/実装/受入は別の状態として報告。5.1完了から自動的に「対応済み」にしない。

R0（公開buildの5.1閉鎖）の完了は、上のどの段階の完了でもない。別の状態として報告する。

wrong role、捏造Δ、古いepoch混入、通常A変更、RT blocking、説明できない欠測、hash不一致は当該gateをFAILにする。
未ロード/機器不足/未測定はPENDING、非適用は理由付きN/A。FAIL / PENDING / N/Aを別rowのPASSで消さない。
外部公開が途中なら公開状況とRollback候補を保全し、重大FAILに対する無根拠な再実行をしない。

実行記録はcandidate単位で、matrixと生ログ・hash・期待値を保持する。private情報と公開可能な要約は分離する。
生ログとprivate情報はmain checkoutのignored `release_state/surround51/<commit>/`へ置き、公開できる要約だけをgitへ入れる。
worktreeを消すとignoredの`dist/`と`release_state/`も消えるため、worktreeの中だけに証跡を置かない。
各Handoffは`候補 / gate・row / status / 実測・証跡 / blocker / 次の一手 / 復元状態`を必須とする。
本計画はGitへ保存し、未コミットの一時ファイルだけを正本にしない。参照先の存在とcommit内のblobを最後に確認する。
編集正本はGit上の本書とする。第2版の固定copyはCodexの出力フォルダ（private）の復元copyで、B-1270のblobと同一（2026-10-06確認）。
main checkout（Reference作業のbranch）には本書第2版の未追跡copyと、gate planへの未commitの同じ追記がある。
本書がmainへ入った後でそのbranchへmainを取り込むと同じpathで衝突するため、取り込む前にその2件の扱いを決める。

次の着手点は**S0のcandidate/oracle/matrix固定 → N1のEBU入口不整合の再現・修復**。
R0はこれと独立に進め、次の公開の前に終える。
署名・長時間stress・公開を先行させて、数値が正しいか分からない候補を積み上げない。

## 10. 正本と実装入口

- [現行製品範囲](../../README.md)、[不変条件（INV-S33）](../hypha_invariants.md)、[表示・計測契約](../hypha_meter_product_contract_20260831.md)
- [Gate計画（履歴とF2）](../hypha_surround_gate_plan_20260919.md)、[確定判断（D-1〜D-14 / 3A）](../hypha_surround_decisions_20260918.md)
- [metric契約](../hypha_surround_metric_contracts_20260918.md)、[容量とRSSの区別](../hypha_surround_ingest_capacity_20260918.md)
- [role map](../../crates/kirin_measure/src/channel_layout.rs)、[ABI layout](../../crates/kirin_hypha_ffi/src/channel_abi.rs)、[Record/解析gate](../../crates/kirin_hypha_ffi/src/record_layout_gate.rs)
- [5.1 UI契約test](../../juce_shell/tests/SurroundObservatoryContractTest.cpp)、[native透明性test](../../juce_shell/tests/audio_transparency_contract_test.cpp)
- [EBU試験](../../crates/kirin_measure/tests/ebu_v05_reference.rs)、[EBU helper](../../crates/kirin_measure/tests/support/ebu_v05.rs)、[ingest容量契約](../../crates/kirin_measure/src/ingest_contract.rs)
- [統合release入口](../hypha_release_entry.md)、[AAX署名入口](../aax_build_signing_entry.md)、[3チャネルRunbook](../ls_release/kirin_hypha_ls_runbook.md)
