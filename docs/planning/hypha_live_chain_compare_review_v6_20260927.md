# 連続PRE/POST比較計画 第6版の精査記録

作成日: 2026-09-27。
対象: 利用者が指定したworktreeの[実装計画 第6版](hypha_live_chain_compare_implementation_plan_20260927.md)。
結論: 方針は維持できるが、境界、4秒loopの取得と再生、RT seal、E2の資源境界に改訂が必要。
成果物: [実装計画 第7版](hypha_live_chain_compare_implementation_plan_20260927_v7.md)。
原本と既存の精査記録は上書きしていない。

## 1. 入力と範囲

入力は885行、SHA-256 `951026e5c109aa971c338be4a50d2141b8a7d4ca6bdbc5aba152163a6aff0199`。
以下の計画行番号はこの第6版を指す。
Downloadsの旧版やメインcheckoutの第5版ではない。

依頼は精査と必要な改訂であり、文書内のG0〜G6を実行する許可とは扱っていない。
製品コード、契約正本、DAW、実機、配布物、Git branchを変更していない。
既存の未commit差分と別セッションの統合作業も維持した。
日本語技術文書スキルに従い、事実、条件付きの提案、未実証の事項を分けた。

## 2. 主要指摘

### V6-01 高: 境界通知とPCM内容の遅れを同一視している

計画77、278〜285行。
第6版は、PREの境界からchain latencyだけ遅れてPOSTの境界が来ると一般化し、一律解除なら最大3秒早く切れると説明する。
しかしcallbackの実行順、host時計の変化、PCM内容の境界は別の観測である。

[Steinberg ProcessContext](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ProcessContext.html)はblock単位の時計と、loop境界で分割しないhostを認めるが、role間の境界通知差をchain latencyと定義していない。
[PluginProcessorHostClock.cpp](../../juce_shell/src/PluginProcessorHostClock.cpp)もhostの時計を読み取る処理であり、その差を保証しない。
PDC Validation Delayのresetはdelay bufferをzero-clearする。
古いPRE PCMがringに残っていることだけでは、reset後のPOST入力との対応は証明できない。

第7版5.3では、第6版の「有効な対応が続く範囲ではPREを一律に切らない」という改善を維持した。
そのうえで、旧証拠が境界後も有効と認定された場合だけ継続する。
継続証拠がなければPREを解除し、原因分類は別に続ける。
第5版の無条件の一律停止へ戻してはいない。
3秒は音声保持の候補であって、通知待ち時間の実測上限ではない。

### V6-02 高: 境界blockを避ける方式では4秒loopの4秒取得が成立しない

計画450〜453行。
第一候補は「一つのrunの内側に選び、境界を含むblockに触れない」とする。
ちょうど4秒のloopからexact4秒を取れる範囲はloop全体だけなので、非分割の境界blockを避ける余白がない。
G1-07/LC-27が含める4秒loopの条件と両立しない。

第7版7.2はlive経路上の予約を第一候補として維持し、境界blockのsample-level subrangeとepoch証拠を成立条件に加えた。
hostが分割する場合と、独立したexact境界offsetがある場合を区別する。
その証拠が取れないprofileは未達であり、範囲短縮、padding、別周回への置換で解決したことにしない。

### V6-03 高: loop包含とSource 2のautoarmは再生開始を保証しない

計画81、101〜102行は、固定範囲がloopに含まれていれば次周回で聴けると断定する。
[LocalBlindTrial.cpp](../../juce_shell/src/local_blind/LocalBlindTrial.cpp)171〜172行は、範囲先頭を含むcallbackが来るまで待つ。
232〜238行のSource 2自動armは実在するが、待機条件を免除しない。

実コードへの模擬callbackで、Source 1完走後もSource 2だけが待機する条件を再現した（第3節）。
第7版1/7.3は、開始callbackと完走までの連続性を必要条件にした。
Pin取得時にexactな境界が得られても、既存rendererへ同じ証拠が伝わるとは限らない。
G1-07/LC-32に取得から両source完走までを含め、必要なrenderer adapterはG3の責務へ加えた。
実装済みや実DAWでの発生確認という意味ではない。

### V6-04 中: RT sealの処理量と上書き防止を具体化する必要がある

計画452行、11節。
「終端で直近4秒から封印」を4秒全体の一括copyと解釈すると、callback framesに比例するという同書のRT条件に反する。
768 kHz stereoのraw PRE/POST 4秒は12,288,000 floats、46.875 MiBである。

第7版7.2は、未来予約では事前確保済み領域へのblock単位書込み、遡及Pinではboundedなsegment lease等を候補にした。
終端callbackではcutoff、epoch、寿命の確定だけを行い、非RTで必要な組立てを行う。
block途中のcutoff後にlive書込みが続き、workerが0.5秒遅れた場合も必要な先頭を上書きしないことをLC-15へ追加した。
履歴を8秒へ増やさない方針は維持し、stagingやseal領域が無料になるとは扱わない。

### V6-05 中: ownerを保ったまま解析枠だけ返すAPI境界が必要

計画8.2、571行。
E2はcomparison ownerを保ちながらoptional Analysis leaseを解放する仕様であり、その意図は正しい。
一方、現行[analysis_lease.rs](../../crates/kirin_measure/src/analysis_lease.rs)222〜229行のAuditionAdmissionは、解析枠、process/project試聴排他、capture barrierを一括所有する。
305〜312行のreleaseは全解放であり、解析枠だけを停止/再取得する公開操作ではない。
[LocalBlindProductSession.cpp](../../juce_shell/src/local_blind/LocalBlindProductSession.cpp)247行以降も、unityのRT receiptまでscopeを保つ。

第7版8/G2にownerと解析grantの寿命分離を必須として記載した。
workerが止まっただけでleaseを返したと判断しない。
LC-22では他instanceが実際に空いた枠を取れることと、再取得拒否でも既適用減衰とRecord排他を維持することを確かめる。
既存Blindの終了操作をそのまま流用するだけでは満たせない。

## 3. 実コードを使った限定試験

使い捨てfixtureで指定worktreeのLocalBlindTrial.cppを直接compileした。
製品への組込みやインストールは行っていない。
48 kHz、mono、512 frames、artifact長4秒、実製品と同じ5 ms（240 frames）の遷移を使用した。
clock/epochsは一定の模擬値であり、host clock取得、IPC、gain算定、admission、実DAWを含む試験ではない。

| 模擬callback条件 | callback数 | copyしたcallback数 | Source 1完走 | Source 2完走 | failure |
| --- | ---: | ---: | --- | --- | ---: |
| block整列した4秒exact loop | 1125 | 1125 | yes | yes | 0 |
| 非分割4秒loop、開始位相96 samples | 1125 | 0 | no | no | 0 |
| 初回preroll −416、以後の位相96 | 1125 | 376 | yes | no | 0 |
| 4秒artifactが8秒loopの内部 | 2250 | 752 | yes | yes | 0 |

3番目のpending sourceは2だった。
反例のloopは[0,192000)、callback先頭は折返し後96,608,…,191584を繰り返すため、範囲先頭0を含む開始callbackが来ない。
この位相列が続く限り同じ待機条件が続く。
試験時間内の待機を、実DAWの恒久停止や製品全体の障害率へ一般化しない。

exactLoopRangeValidは、artifactとloopが一致し、callbackが整列したケースだけtrueとした。
非分割2ケースと8秒包含loopはfalseであり、過剰なexact-loop証明に依存した反例ではない。
5 ms遷移をゼロにして得た結果でもない。

fixture: 非公開の描画検証資料。
SHA-256: `a04fd11ac88171ff8995d4d2e73413620b8a82ce667e41b93dffcc8ee96ab610`。
compileはclang++のC++17、O2、Wall、Wextra、Werrorで成功し、4条件の期待結果をassert相当で確認してexit 0。
独立した再compileと再実行でも同じ値を得た。
一時fileは実行証跡であり、将来の受入試験が実装済みという意味ではない。

追加の算術fixtureもpassした。
payload小計は48/192/768 kHzで7.38671875 / 29.359375 / 117.25 MiB。
保護しない4秒ringへcutoff後100 samplesを書けば、必要な先頭100 samplesが上書きされるという抽象反例を確認した。
これは候補実装の性能測定ではない。

## 4. 補足修正と維持した改善

| ID | 修正 | 第7版の反映 |
| --- | --- | --- |
| V6-06 中 | 同一runのhide/showではrun IDだけで解析gapを表せない | 8.2/LC-24にanalysis epochまたはgap barrier、enqueue停止確認、旧queueと部分窓の退役、新しい連続窓での再開 |
| V6-07 中 | G1-07の「独立capture」は別passか同じpassか未定 | 同じpassの各side raw tapと独立に照合。fixtureの周回識別PCM、前周回誤採用の故障注入を追加 |
| V6-08 中 | 0/1/4096 samplesだけでは3秒保持と2秒loopの組合せを検証できない | G1-01/03に保持上限付近、delayがloopより長い条件、容量超過、複数pending epoch |
| V6-09 低 | 統合branchの「未merge」は観測後に古くなった | 8d13800eのmerge、805ff180までのsnapshot、main未反映、サーバー未照会を分離 |

G1-01/10のbit一致は既知delay fixtureに限定されており、一般のEQ/compのPREとPOSTへbit一致を要求する誤りは確認していない。
修正したのはPinの同一pass照合の定義である。

第6版の改善は維持した。
有効な証拠の範囲でのPRE継続、live予約を第一候補にする方針、非表示中のenqueue停止、unity時owner解放の未採用候補、G1-01〜10は残した。
A′、E1＋E2、100%復帰例外、Pinの二つの代替、遡及代替不採用も変更していない。
Blindの900×600隔離面、Source 2 autoarm、停止/live復帰/unity Returnのreceipt分離、gain増大承認に新たな重大矛盾は見つからなかった。

併読先の外部調査には、境界の長さ、loop内なら次周回で聴けること、主要hostの既定画面に関する広い表現が残る。
今回はその原本は変更せず、第7版の5.3/7.3/8.1を優先する旨を冒頭に明記した。
六製品の現行配布版や利用者投稿の全件再調査は行っていない。

## 5. 外部資料と基点

今回の外部確認は[Steinberg ProcessContext](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ProcessContext.html)と[Processing FAQ](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html)を中心に行った。
前者は非分割loop blockを許し、後者はcallback長の変化、latency変更通知、RTの禁止操作を説明する。
これらから特定hostの境界継続を保証せず、G1で確かめる事項を限定した。

mainと指定worktreeのHEAD: `ee522a536fa7f418ce3247c64eb77ec55d1d8d86`、B-1023。
ローカルorigin/main: `5a6a9db5c4d82ee28d5ef26839e782e3bd4882c0`。
この二基点の差はahead 14 / behind 48。
統合branchの読取りsnapshot: `805ff180822d924d0ec71dd26ffe8466d456d30f`、B-1053。
merge commit `8d13800e6d1ff3797e244633cf2b7e75b0a60637`は2026-09-27 15:11 JSTで、親は59e9bb8fと5a6a9db5。
統合branchの存在とmainへの採用を同一視していない。
fetch/merge/pushは実施せず、サーバー状態や統合branch全体の品質は検証していない。

## 6. 検証結果と申し送り

| 確認 | 結果 |
| --- | --- |
| 第6版全文、前版との差、関連コード | pass |
| 境界/Pin/lifecycleの分担精査と第7版の再確認 | pass。概要表のloop断定も修正 |
| 実rendererへの4条件の模擬callback | pass。期待する反例と正常対照を再現 |
| 算術、位相、容量、上書きの抽象反例 | pass |
| 文書link、章/試験ID、fence、空白、入力hash | pass。ローカル参照9件、章0〜16、LC-01〜32、G1-01〜10、fence対、末尾空白、元入力SHA-256不変を確認 |
| cargo test/clippy、製品全native、実DAW、実音、性能、installer | skip。製品変更なし。G0〜G6は未実施 |


改訂: 第6版を精査し、第7版と本記録を作成。
検証概要: 2026-09-27、コードと公式仕様を照合し、4秒loopの再生待機を実rendererへの模擬入力で確認。
Handoff: 採用commitを確定して参照を再照合した後、G1-01〜03を先行し、G1-07で取得から両source完走まで、G1-08でadmission分離を実証する。
音声保持3秒、128 MiB、live転送、動的PDC、各host対応は未実証の候補であり、承認済みの完成仕様ではない。
必要な新しい契約や範囲変更は利用者の判断を求める。

Commit: 新規なし。基点 ee522a53 / B-1023。
変更: 新規文書2ファイル（+1125 / -0行）。計画本文の第6版との差は+107 / -48行。製品コード、原本、既存差分は維持。
Test: 文書/算術/限定native fixture pass、製品全体/実機 skip。
LSアップ用: skip。
HPアップ用: macOS skip、Windows skip。
未処理: 採用基点とG0/G1、必要な契約変更の承認。
