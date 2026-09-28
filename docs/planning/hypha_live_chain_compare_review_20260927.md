# 連続比較計画の精査記録

この記録は、連続PRE/POST比較の実装計画と外部調査に対する精査を、新しい版から順にまとめる。
第1節は第7版から第8版、第2節は第5版から第6版、第3節は第4版から第5版（別セッション作成）、第4節は第3版から第4版、第5節は第2版から第3版（別セッション作成）への改訂記録である。
第6版から第7版への改訂記録は、別セッションの[第6版の精査記録](hypha_live_chain_compare_review_v6_20260927.md)にある。
実装計画の正本候補は[実装計画](hypha_live_chain_compare_implementation_plan_20260927.md)、外部調査は[外部調査](hypha_live_chain_compare_external_research_20260927.md)に置く。

## 1. 第7版の精査と第8版への改訂

作成日: 2026-09-27。
対象は、別セッションが本worktreeに置いた第7版と、その根拠である第6版の精査記録である。
第8版は第7版の本文を基に作り、このworktreeの版番号なしの名前の文書へ置いた（第6版の内容を置き換え）。
第7版と第6版の精査記録の原本は変更していない。
文書内の将来工程を、実装、実機操作、公開の許可へ読み替えていない。

### 1.1 判定

第7版の補正（V6-01〜V6-09）は、確認した範囲ですべて正しい。
いずれも、本記録の第2節で作成した第6版の誤りまたは過大な記述を直している。

- 片側先着のPRE継続を、旧PCMがringにあることだけで許すように読める記述（reset/flushをまたぐ証拠の有効性の認定が要る）
- 「最大3秒早く切れる」を上限のように書いた記述（3秒は保持候補で、通知差の実測値ではない）
- 境界blockに触れない範囲選択が4秒loopでも成り立つように読める記述
- 「固定範囲がloopに完全に含まれていれば次周回で一巡が始まる」という断定（非分割hostでは範囲先頭を含むcallbackが来ない反例がある）
- 「終端でRT seal」が、4秒全体を一回のcallbackで複製するとも読める記述
- E2のowner保持と解析lease解放を、現行`AuditionAdmission`のまま実現できるように扱った点

第7版に誤りは見つからなかった。
一方で、loopに余白がある場合の範囲の置き方が未定で、境界証拠とrenderer adapterが要る範囲を広く見積もっていた。
第8版ではこれを既定の規則として加え、遡及Pinの再生可否、exact 4秒loopの頻度、host認定と基点の現況を補った。

### 1.2 入力の識別

| 入力（本worktreeの`docs/planning/`） | 行数 | SHA-256 |
| --- | ---: | --- |
| hypha_live_chain_compare_implementation_plan_20260927_v7.md | 944 | 94f91730817e6799a34dfdd65b01eac2b127c35947ec1464ff4767e7229a693d |
| hypha_live_chain_compare_review_v6_20260927.md | 181 | c344fb5664265aef3de30ce1f1a34efb0c0a6625cd12dc1ce202e2a022f7975a |

第6版（SHA-256 `951026e5c109aa971c338be4a50d2141b8a7d4ca6bdbc5aba152163a6aff0199`）と第7版の差は155行（追加107、削除48）で、第6版の精査記録の記載と一致した。

### 1.3 第7版の指摘の検証

| 指摘 | 検証結果 |
| --- | --- |
| V6-01 境界通知とPCM内容 | 妥当。PDC Validation Delayの`reset()`は`FixedValidationDelay.h`でbufferをzero-fillする。旧PCMの存在だけではreset後の対応を示せない |
| V6-02 4秒loopと境界block | 確認。exact 4秒loopでは範囲がloop全体になり、境界blockを避ける余白が無い |
| V6-03 loop包含とautoarm | 確認。`LocalBlindTrial.cpp`170〜172行は範囲先頭を含むcallbackまで待ち、237〜238行のSource 2自動armはこの待機を免除しない。第7版の4条件を独立のfixtureで再現した（第1.4節） |
| V6-04 RT sealの処理量 | 妥当。768 kHz stereoのraw PRE/POST 4秒は12,288,000 floats、46.875 MiB |
| V6-05 admissionの束ね | 確認。`analysis_lease.rs`222〜230行の`AuditionAdmission`は解析lease、process/project試聴lease、capture barrierを持ち、305〜313行の`release()`はすべてを解放する。`LocalBlindProductSession.cpp`の`service()`は、unityへのreturn receiptを確認した後にだけ退役とepoch解除を行う |
| V6-06〜V6-09 | 妥当 |
| 製品のloop情報 | 追加確認。`PluginProcessorAudition.cpp`は`exactLoopRangeValid`へhostの`looping`真偽だけを渡し、`TrialBlock`の`loopStart`/`loopEnd`は設定しない。rendererも両者を参照しない |
| 実行中のepoch | 追加確認。製品の`TrialBlock.epochs`はsession開始時に一度publishした値で、loopの折返しでは変わらない（`LocalBlindProductSession.cpp`70〜71行と109行、`LocalBlindEpochSnapshot.h`）。第7版fixtureの一定epochは製品と一致する |

### 1.4 追加の模擬callback試験

使い捨てfixtureで、本worktreeの`LocalBlindTrial.cpp`を直接compileした。
製品への組込みやインストールは行っていない。
48 kHz、mono、artifact長4秒、5 ms（240 frames）の遷移を使い、hostが境界でblockを分割しない折返しを模擬した。
折返しをまたぐcallbackは折返し前の開始位置を報告し、次のcallbackは再開位置に折返し後のframes数を足した位置から始まる。
`exactLoopRangeValid`は製品と同じく、loop中は常にtrueとした（第7版のfixtureはblock整列の条件だけtrue）。
clock/epochsは一定の模擬値であり、host時計の実報告、IPC、gain算定、admission、実DAWを含まない。

| 条件（512 frames固定） | copyしたcallback数 | Source 1完走 | Source 2完走 | failure |
| --- | ---: | --- | --- | ---: |
| A 4秒exact loop、block整列 | 1125 | yes | yes | 0 |
| B 非分割4秒loop、位相96 | 0 | no | no | 0 |
| C 初回preroll −416、以後位相96 | 376 | yes | no（pending 2） | 0 |
| D 8秒loop内の範囲（開始48000）、整列 | 752 | yes | yes | 0 |
| E 8秒loop、位相96、範囲開始0 | 0 | no | no | 0 |
| F 同、範囲開始50 | 0 | no | no | 0 |
| G 同、範囲開始100 | 752 | yes | yes | 0 |
| H 同、範囲開始511（B − 1） | 752 | yes | yes | 0 |
| I 同、範囲終端 = 折返し位置 | 752 | yes | yes | 0 |

A〜Dは第7版の表の値と一致した。
E〜Gは、範囲先頭が再開位置からB − 1未満の余白内にあると、位相次第で開始しないことを示す。

callback長を64〜512 framesで変え、周回長をblockの倍数でない8.013秒（384624 samples）とした200通りの位相では、次の結果だった。

| 範囲 | 両source完走に至らなかった数 |
| --- | ---: |
| J 開始511（B − 1） | 0 / 200 |
| K 終端 = 折返し位置 | 0 / 200 |
| L 開始0 | 193 / 200 |

fixture: `loop_margin_audit.cpp`（精査セッションの一時領域に置いた使い捨てのもの。リポジトリには含めない）。
SHA-256: `59e9712fad9291f7b0854d54723627b61f92c26e76ffbf3f04d6dd7cff351689`。
clang++のC++17、O2、Wall、Wextra、Werrorでcompileし、全12条件の期待結果を確認してexit 0。
一時fileは実行証跡であり、将来の受入試験が実装済みという意味ではない。

### 1.5 主要な追加と改訂

| ID / 優先度 | 第7版の箇所 | 指摘、根拠、影響 | 第8版の対処 |
| --- | --- | --- | --- |
| W01 高 | 第7.2節、第1節、第7.3節 | 境界blockを避ける方式を「限定案にすぎず」とし、loopに余白がある場合の範囲の置き方を定めていない。範囲先頭が再開位置の近くにあると、非分割hostでは取得に境界blockの折返し後の部分が要り、再生も位相次第で始まらない（第1.4節のE、F、L）。その結果、境界証拠とrenderer adapterが要る範囲を広く見積もり、実装の優先順位がずれる | 余白付き窓 s ≥ q + B − 1、e ≤ p + 1 を既定の範囲とした。q、pは観測した折返しの前後のcallback開始位置、Bは最大callback frames。PPQ換算を使わず、取得と再生の両方でexact境界を要しない。周回長 ≥ 4秒 + 2Bで4秒の窓が取れる。前提（折返しの報告位置、周回ごとの一致、時計と内容の境界差U）をG1-03へ加えた |
| W02 中 | 第7.2節、第7.1節 | 遡及Pinは取得の可否だけを判定する。取得できても範囲先頭が再開位置の余白内にあると、loopの次周回で開始しないことがあるが、事前に示されない | Pin時に再生可否を判定し、DAWでの巻戻しか余白付き窓での次周回予約を選べるようにした。LC-32に予告と実際の待機の一致を加えた |
| W03 中 | 第7.2節、第12.1節 | exact 4秒loopを特別な条件のように扱える。4/4拍子、120 BPMの2小節は48 kHzで192000 samplesであり、4秒のartifactと一致する | 頻度を明記し、G1-07で余白付き窓とexact loopを分けて評価するようにした。未達profileでは、loopを4秒 + 2Bより長くする案内を加えた |
| W04 低 | 第3節、第10節 | 統合候補c60ab679では`HyphaChainClockPolicy.h`がDebug buildに限られ（B-1052）、release buildは認定hostを返さない | 現況を記し、live比較の認定には計測用の判定とは別の認定一覧と証跡を使うとした |
| W05 低 | 冒頭、第3節 | 統合branchの読取りが805ff180のまま | c60ab679（B-1056）、origin/mainに対して22/0、remoteに同branchが無いこと（`git ls-remote`）を記した。根拠のsourceは三基点で同一blob |
| W06 低 | 外部調査 | 第7版が指摘した三つの一般化が外部調査に残る | 外部調査を第5版へ改めた |
| W07 低 | 第16節 | 残る論点は実測でしか決まらず、文書の改訂を重ねても確度は上がらない | G1の結果まで改訂を止める提案と、G1-01〜03、G1-07の順序を記した |

### 1.6 検証と限界

| 確認 | 結果 |
| --- | --- |
| 第7版全文、第6版との差、関連ソースの照合 | pass。第1.3節の各行をコードで確認 |
| 模擬callback（第1.4節） | pass。12条件すべて期待どおり |
| 基点 | pass。ee522a53、5a6a9db5、c60ab679で、第8版の第14節が挙げる根拠sourceが同一blob |
| 改訂文書のlocal link、code fence、末尾空白、章/試験ID、表の列数 | pass。local linkは計画5件、外部調査3件、精査記録17件ですべて解決。計画の章0〜16、LC-01〜32、G1-01〜10、fence対、末尾空白0を確認 |
| Rust/native/実host/性能/実音/installer試験 | skip。文書改訂のみで製品変更なし |

余白付き窓の規則は、fixtureでは既存rendererの開始条件に対して成り立つことを示しただけである。
非分割hostが実際に折返し前の開始位置を報告するか、周回ごとに同じ位置で折り返すか、POSTの補償済み時計と内容の境界が一致するかは、G1-03で測る。

### 1.7 未処理と申し送り

- 採用commit（統合branchの受入）は利用者の判断待ち。受入後に参照を照合し直す。
- G1は未実施。最初にG1-01〜G1-03、続いてG1-07を行う。
- exact 4秒loopへの対応可否は、G1-07の結果を添えてG1Rで判断する。
- 文書の置き場所: main checkoutに旧版（版番号なし、`_v3`、`_v5`、`review`、`review_v4`）、本worktreeに`_v7`と`review_v6`が未追跡で残る。commit時は計画、外部調査、精査記録の3件へ集約し、別セッションの記録をどう扱うかを決める。
- Notionの現在地、日次ログ、Handoffは利用者の指示により記録していない。
- Commitは作成していない。

## 2. 第5版の精査と第6版への改訂

作成日: 2026-09-27。
対象は、メインcheckoutに未追跡で置かれた第5版と、その精査記録である。
第6版は、このworktreeの版番号なしの名前の文書へ統合した。
`_v5`文書と別セッションの精査記録の原本は上書きしていない。
文書内の将来工程を、実装、実機操作、公開の許可へ読み替えていない。

### 2.1 判定

改訂が必要。
第5版の指摘（R1〜R14）は、確認した範囲で正しく、第4版にあった誤りを直している。
特に、既存future captureを次周回の取得へ流用できるとした第4版の記述、非分割の境界から範囲を確定できるとした記述、E2の表現、INV-S7、Blindの画面の扱いは第4版の誤りだった。
一方、第5版には、境界の片側先着でPREを早く切るという実害のある規則が一つ残り、次周回予約の優先順位、非表示中の解析queue、E2のowner、G1の具体的な実験が未定だった。

### 2.2 入力の識別

| 入力（メインcheckoutの`docs/planning/`） | 行数 | SHA-256 |
| --- | ---: | --- |
| hypha_live_chain_compare_implementation_plan_20260927_v5.md | 826 | 55a80c09eeaac87232644d29412a0a96cf80b0ad59a39c23959f5fa91fdd0cf5 |
| hypha_live_chain_compare_review_v4_20260927.md | 134 | c0c51ebdb8d02dcab856be865601eed480c3a3bccf0360360d3039134f74a78b |

### 2.3 第5版の指摘の検証

| 指摘 | 検証結果 |
| --- | --- |
| R1 既存future captureの直用 | 確認。`PluginProcessorPairing.cpp`は取得開始を`clock.position + sampleRate`（1秒先）とし、`PairCaptureBarrier.h`は長さを`sampleRate * 4`以下に制限する |
| R2 非分割境界での範囲 | 確認。block内で折り返すと、次callbackの開始位置だけではloopの開始と終了を一意に決められない |
| R3 E2と減衰 | 確認。`LocalBlindTrial::hold`はstop後も承認済みの`lowerPost`をPOSTへ掛ける |
| R4 INV-S7とBlindの画面 | 確認。INV-S7はeditorを閉じるか非表示にした時点で全optional leaseを解放すると定める。`PluginEditorLocalBlind.cpp`はBlind中に900×600へ拡大し、resizeを止める |
| R10 plugin画面の資料 | 確認。Studio One 4.1 manualは既定で一つの画面だけを使うと書き、Avidの2025.12 guide（p.151）は既定でTargetが有効な状態で開くと書く |
| R5〜R9、R11〜R14 | 妥当。R8の「resetの影響の長さは実測」、R12の「4/Tは一様位相の仮定」も適切 |

### 2.4 主要指摘と改訂

| ID / 優先度 | 第5版の箇所 | 指摘、根拠、影響 | 第6版の対処 |
| --- | --- | --- | --- |
| F01 高 | 第5.3節の表、第7.1節、LC-26 | 「片側の新しい境界証拠だけを観測したらPREを解除」としている。直列chainでは通常PREが先に境界を処理し、POSTはその後もチェーン遅延の分だけ境界前の音を受け取る。その区間の対応するPREは既にringにある。この規則ではloopやseekのたびに、最大でチェーン遅延（保持上限3秒）の分だけ早くPREが切れる。同じ節の「必要な対応が失われた時点でPREを止める」「全sampleの対応を保てるprofileでは中断不要」とも矛盾する | 片側先着は分類待ちの開始だけにし、POSTの必要範囲に証明済みの対応がある間はPREを続ける。POST側だけが境界を示す場合は直ちにPOSTへ移す |
| F02 中 | 第7.2節 | 次周回予約の三方式に優先順位が無く、G1の対象が定まらない | live経路上の予約とRT sealを第一候補にした。live転送が既に整列PCMを届けるのでPREの追加arm/ackが要らず、終端で直近4秒から封印するので8秒の履歴も要らない。予約範囲は境界blockに触れないrun内に選ぶ |
| F03 中 | 第8.2節 | 非表示中にworkerを止めるが、RTが解析queueへ積み続けるかが未定。積み続けるとqueue overflowが故障として数えられる | 非表示中はRTから解析queueへ積まず、ring消費とcoverage検証だけを続ける規則を加えた |
| F04 中 | 第8.1節 | E2の準備保持中もownerを保持するため、画面を閉じた後もRecordと他試聴が拒否され続ける。負担をG4で測るとするだけで比較候補が無い | 減衰を保持していないunity時に限り、非可聴cacheを残してownerを解放する方式をG4の比較候補に加えた。採用は契約変更として第15節で扱う |
| F05 低 | 第1節 | 固定音の自動再生の条件が「次の周回の開始位置を正しく観測できれば」で、rendererの実際の条件と違う | 固定範囲がloopに完全に含まれていれば、再生位置が範囲の先頭を通過した時点から一巡が始まる、と改めた。Source 1完走後の既存のSource 2自動arm（`LocalBlindTrial.cpp`の`issue (two)`）も記した |
| F06 低 | 第12節 | G1が一行で、何を測り何が決まるかが散在 | 第12.1節にG1-01〜G1-10の実験仕様を加え、最初に行う実験を示した |
| F07 低 | 冒頭、第3節 | 統合branchでのB番号の付け替え（B-1046〜B-1048）と未commit修正の取り込み（B-1049）が未反映 | 観測時点の事実として記録した |
| F08 低 | 冒頭 | 併読先が第3版の外部調査で、Perception ABの記述差、reset、plugin画面を含む第4版の外部調査を参照していない | 版番号なしの外部調査へ差し替え、plugin画面の表をStudio OneとAvidの公式資料へ更新した |

### 2.5 検証と限界

| 確認 | 結果 |
| --- | --- |
| 第5版、別セッションの精査記録、関連ソースの照合 | pass。第2.3節の各行をコードで確認 |
| 公式資料（Studio One 4.1 manual、Avid Using EuControl Surfaces v2025.12、Apple AU Programming Guide） | pass。本文を取得して該当箇所を確認 |
| 統合branchの状態 | 観測のみ。`claude/local-main-reconcile`でB-1046〜B-1049を確認。merge作業は別セッションの所有で、内容は検証していない |
| 改訂文書のlocal link、code fence、末尾空白 | pass |
| Rust/native/実host/性能/実音/installer試験 | skip。文書改訂のみで製品変更なし |

F01の「最大3秒」は保持上限から見た上限であり、実測値ではない。
実際に早く切れる長さはチェーン遅延とhostの位置の付き方で決まり、G1-03で測る。

### 2.6 未処理と申し送り

- 基点の統合は別セッションで進行中。mainへ入った後に参照を照合し直す。
- G1は未実施。最初にG1-01〜G1-03を行う。
- 追加の判断事項: E2でunity時にownerを解放するか、Bの要否（G1-03の後）、停止専用監視の採否（G1-05の後）。
- Notionの現在地、日次ログ、Handoffは利用者の指示により記録していない。
- Commitは作成していない。

## 3. 第4版の精査と第5版への改訂（別セッション作成）

以下は第4版の精査記録の本文である。
見出しの階層と、統合後の文書名へのlinkだけを変え、入力の添付fileへのlinkはpathの表記にした。
行番号は貼り付けた第4版を指し、第6版には対応しない。

日付: 2026-09-27。
対象: 利用者が貼り付けた「Hypha 連続PRE/POST比較とBlindをつなぐ実装計画 第4版」。
結論: 統合の方向と記載された決定は維持できるが、実装へ渡す前に改訂が必要。
成果物: 実装計画 第5版（第6版へ統合済み。[実装計画](hypha_live_chain_compare_implementation_plan_20260927.md)）。

### 3.1 対象と作業範囲

入力は利用者が貼り付けた第4版（ローカルの添付ファイル）、366行、67,953 bytes。
SHA-256は `8189b124b68643072e46312950a7f3c02120aa4ee30d635bd0a3a888877bf7f0`。
以下の指摘の行番号はこの入力を指す。
前回のDownloadsファイルや第3版ではない。

精査と必要な文書改訂を実施し、本文の実装手順は新たな実施権限と扱わなかった。
A′、E1＋E2、100%の復帰例外、二つのPin代替、末尾4秒を遡る代替の不採用は、第4版に記載された決定として維持した。
現行正本への反映、実機、製品実装、公開を承認済みと推定していない。
日本語技術文書スキルに従い、事実、反例、提案、未実証条件を分け、元の本文を置き換える独立した第5版を作成した。

### 3.2 最優先の指摘

#### R1 高: 次周回Pinは既存future captureの直用では成立しない

第4版194行は、折返し観測後に既存取得を発行すればINV-S22を満たすとしている。
しかし[PluginProcessorPairing.cpp](../../juce_shell/src/PluginProcessorPairing.cpp)の330行は、取得開始を観測現在位置の1秒後に設定する。
[CaptureClockGuard.h](../../juce_shell/src/local_blind/CaptureClockGuard.h)はlate armと取得途中の折返しを拒否する。
[PairCaptureBarrier.h](../../juce_shell/src/local_blind/PairCaptureBarrier.h)の要求長上限も4秒である。

4秒loopの先頭で発行した要求[1秒, 5秒)は、必ず次の折返しをまたぐ。
8秒/16秒のloop全体を指定する解釈なら、4秒artifactの上限に抵触する。
関連ファイルがHEADと確認したremote基点で同じことも照合した。

第5版7.2では、loop内のexact4秒範囲を明示し、次周回予約を新しい取得protocolの成立性項目へ戻した。
live経路上の予約とRT seal、または次周回epoch指定の事前armをG1で比較する。
代替の採用意図は変更していないが、成立方式を完成済みとはしない。

#### R2 高: 後退したcallback位置だけではexact loop範囲を確定できない

第4版194行の「観測した折返しの位置」には、sample単位の境界をどう得るかが欠けている。
119行自身が認める非分割のloop境界blockでは、次callbackの開始位置はloop先頭とは限らない。
[時計取得](../../juce_shell/src/PluginProcessorHostClock.cpp)もPPQ情報をexact native範囲に昇格させていない。

数理例として、callback開始96、長さ64、次callback位置60は、loop [0,100)でも[20,120)でも生じる。
同じ観測から異なる境界が成り立つため、単なる後退量だけでは範囲を一意にできない。
第5版5.2/7.2では、sample-levelの証拠があるprofileだけに候補を出し、周回、範囲、締切、両側coverageを必要条件にした。

#### R3 高: E2の「非可聴保持」と実適用POST減衰が混在している

第4版169行は実適用済みPOST減衰を保持するとし、222〜234行は可聴比較終了後を非可聴の準備保持と表現する。
[LocalBlindTrial.cpp](../../juce_shell/src/local_blind/LocalBlindTrial.cpp)の97〜104行も、stop後のPOSTへ既存減衰を掛ける。
これは音への作用が残る状態であり、unityへの復帰ではない。
ownerとRecord排他も残る。

第5版8.1ではPOST unity、承認済み減衰保持、RT未確認を分離した。
E2を「PRE/固定コピーの出力を止める」と定義し、Return完了とは区別した。
共通owner保持による隠れた予約の負担は解消したことにせず、G4で測る。
ownerを解放して再取得する方式を採るなら、別の契約判断が必要になる。

#### R4 高: E2の解析leaseと100% Blindの規則が未整合

第4版212/222/232行は既存解析2枠の使用と非表示での準備保持を並記するが、INV-S7は非表示時の全optional lease解放を要求する。
第5版8.2でowner、承認済み数値、IPC、解析workerを分離し、解析leaseは返し、追従と相関監視を止める案にした。
再表示後の1操作はready時の操作数とし、失効や資源競合時の待ちを保証から外した。

239行の100%でsourceとgainを名指しする規則も、209行のBlind匿名性へ一律適用できない。
[PluginEditorLocalBlind.cpp](../../juce_shell/src/PluginEditorLocalBlind.cpp)の187〜196行は、Blindを最小900×600へ拡大してresizeを止める。
第5版9ではliveと記名ABの100%復帰操作を残し、Blindは既存隔離面を維持した。
小型Blindを新たに認める変更は本版の承認済み事項へ含めていない。

### 3.3 その他の指摘

| ID / 優先度 | 第4版の箇所 | 問題 | 第5版での対処 |
| --- | --- | --- | --- |
| R5 中 | 129〜140行 | 正常な境界証拠の片側先着と、永久に片側だけの故障が未分離 | 5.3にboundedな境界確認待ち。出力をPOSTへ戻す判断と故障分類確定を分離 |
| R6 中 | 44/165/169/200行 | live −8 dB→snapshot −12 dB→旧live復帰は+4 dBになる。古いgain復元だけでは復帰の承認が閉じない | 8.3にprofile/revisionと実gain差分、増大を伴う復帰承認、LC-30。trial停止/live復帰/unity Returnのreceiptを分離 |
| R7 中 | 108/130/181行 | 背景liveだけの故障を固定artifact全体の失効と混同できる | 8.4にcache、解析、共通pair/format/clock失効のscope表、LC-31 |
| R8 中 | 132行 | Resetが起こり得ることから、chain遅延分が必ず無音/残りになると一般化 | 5.3で純遅延lineとprerollなしの条件付き例へ。未対応区間の存在/長さは実測 |
| R9 中 | 123行、LC-29 | 相関監視の評価が遅れと誤停止だけで、最後まで検出できないケースが不足 | 5.2/LC-29に周期、無音、見逃し、判定不能、探索範囲外。沈黙を正常証明にしない |
| R10 中 | 222〜232行 | 主要hostの画面置換を一括して「既定」と記述。現行版の条件とcallbackの証拠が別 | 8.1/14に版と設定条件、公式URL。操作説明とHyphaの実際の終了通知を区別 |
| R11 中 | 2/65行、UI節 | 14/43は第4版基点には正しいが、今回のremote-tracking refは進み、INV-S40の英日表示が増えている | 冒頭に二つのremote hashを固定。14/48と日本語UI回帰を追加 |
| R12 低 | 192行 | 4/Tは要求位相が一様という仮定が必要で、利用者の失敗率ではない | 7.2にmin(1,4/T)の適用条件と非実測を明示 |
| R13 中 | 204行 | loop包含による最初の待機再生と、同sourceの自動反復が混同され得る | 7.3/LC-32で初回、既存のSource 2自動arm、明示source切替、両側完走、再試聴を分離 |
| R14 中 | 52/98行 | mixed-formatの共通protocolを時刻整合までの根拠と読める | 2/5.1で追加候補とし、PRE×POST format、方向、process配置ごとに認定 |

### 3.4 外部調査と第4版が正しかった点

Perception ABの公式guideは、印刷p.8でPRE基準のclip可能性が低いとし、p.21では起きないとする。
この説明の強さの差は実在し、第4版の指摘を採用した。
第3版はp.8を根拠に限定したが、マニュアル全体に強い表現がないという意味にしてはいけない。
どちらもHyphaのclip-free保証へ転用せず、競合実機の挙動を今回確定したとはしない。
[公式guide](https://www.meterplugs.com/files/perception-ab-guide.pdf)。

Logic Proの公式説明はLink Singleの画面再利用とproject全体への適用を支持するが、その工場既定値は示していない。
[Apple公式guide](https://support.apple.com/guide/logicpro/work-in-the-plug-in-window-lgcpbc21a1fd/mac)。
Studio Oneは公式4.1 manual、Pro ToolsはAvid 2025.12 guideでPin/Targetの説明を確認し、第三者資料だけに依存する状態を改めた。
現在のStudio Pro 8で同じ既定値か、各hostがどのlifecycle callbackを発行するかは実機未確認である。
[PreSonus公式manual](https://pae-web.presonusmusic.com/downloads/products/pdf/Studio_One_4.1_Reference_Manual1.pdf)、[Avid公式guide](https://resources.avid.com/SupportFiles/ProMixing/Using_EuControl_Surfaces_v2025.12.pdf)。

静的なcapture一致をlive保証へ拡大しないこと、fresh trial、immutable payload、容量ledger、raw測定の分離は、第4版でも維持されており、そのまま残した。

### 3.5 確認結果と未検証

| 確認 | 結果 |
| --- | --- |
| 入力全文と関連コード | pass。取得、clock guard、trial、editor、INV、二基点を照合 |
| Git基点 | pass。HEAD ee522a53、入力側83b383a6、確認remote 5a6a9db5を区別。fetch/mergeなし |
| 公式資料 | pass。根拠と未確定の版/条件を第14節へ記録 |
| 算術と抽象反例 | pass。1秒lead＋4秒、境界の非一意性、周期PCM、+4 dB復帰、割合、容量を確認 |
| 文書参照、空白、fence、入力hash | pass。ローカル参照10件、章0〜16、LC-01〜32、fence対、末尾空白、元入力SHA-256不変を確認 |
| Rust/native/DAW/実音/性能/installer | skip。製品コード変更なし。G0〜G6は未実施 |

数理fixtureは実DAW観測ではない。
64 sample周期のFloat32 wavetableを4096 samples繰り返した定常PCMでは、64 samplesずらしても全sampleの値差は0だった。
これは信号だけでは遅延差を識別できない入力があるという反例であり、特定の相関実装をテストした結果ではない。
gain −12→−8 dBは+4 dB、振幅比約1.584893になる。
容量小計は48/192/768 kHzで7.38671875 / 29.359375 / 117.25 MiBであり、実測RSSではない。

### 3.6 セッション記録と申し送り

セッション記録手順を読み、Notionの現在地、日次ログ、Handoffはプロジェクトの書込み禁止を優先して未実行とした。
迂回接続は行っていない。

現在地: 第4版を精査し、第5版と本記録を作成。
日次ログ相当: 2026-09-27、取得方式、境界、E2、匿名UI、gain復帰をレビューし、公式資料と算術反例で照合。
Handoff: 基点統合後にINV/参照/英日表示を再照合し、次周回予約、境界pending、E2資源、段階gain復帰をG1で実証する。
A′、E1＋E2等の決定は維持し、方式や契約の未確定を自動承認にしない。
統合作業そのものは本件では行っていない。

Commit: 新規commitなし。基点 ee522a536fa7f418ce3247c64eb77ec55d1d8d86 / B-1023。
変更: 新規文書2ファイル（+960 / -0行）。入力と第3版以前の文書、既存の未commit差分は維持。
Test: 文書/算術確認 pass、製品試験 skip。
LSアップ用: skip。
HPアップ用: macOS skip、Windows skip。
未処理: G0/G1、次周回予約の成立性、E2保持資源の最終承認、Notion禁止による未記録。

## 4. 第3版の精査と第4版への改訂

作成日: 2026-09-27。
対象は、メインcheckoutに未追跡で置かれた第3版の3文書である。
第4版は、このworktreeの`docs/planning/`にある版番号なしの名前の2文書へ統合した。
第3版の`_v3`文書と、その精査記録の原本は上書きしていない。
文書内の将来工程を、実装、実機操作、公開の許可へ読み替えていない。

### 4.1 判定

改訂が必要。
第3版の主要指摘（R01〜R16）は、現行コードと一次資料でおおむね確認できた。
R13だけは、競合マニュアル内の記述の食い違いを反映して修正した（第4.4節）。
一方、第3版には次の未解決点が残る。

1. 調査基点としたローカルmainがorigin/mainと分岐しており、根拠にしたファイルの一部がorigin/mainに無い。
2. 境界直後の未対応区間は物理的に避けられないのに、PRE不足を一律に中断とするため、loopとseekで比較を続けるという目標と両立しない。
3. 区間固定、editor、小さい画面の操作で、既知の外部条件（loopの折返し、hostの画面置換、origin/mainのINV-S37/S38/S39）を扱っていない。

第4版はこれらを判断事項として具体化した。
成立性を実証した完成仕様ではない。

### 4.2 入力の識別

| 入力（メインcheckoutの`docs/planning/`） | 行数 | SHA-256 |
| --- | ---: | --- |
| hypha_live_chain_compare_implementation_plan_20260927_v3.md | 548 | 0baa72b7c37e7ae0019f2e21500df78db6a5f12a817323f81cfa54db636da340 |
| hypha_live_chain_compare_external_research_20260927_v3.md | 160 | c557ac15dcda426293c090d0537531c9efd1b4d4d18eeb695fb5f3946176f923 |
| hypha_live_chain_compare_review_20260927.md | 99 | d8fd9c13d1c06a05925136b4cdeff2971f35d1ce7d624d89089ead4d8f6a937b |

以下の「第3版の箇所」は、上の計画第3版の節番号を指す。
照合したソースはorigin/main `83b383a65b6c92a3e6f32f579b8a93cf39088e98`（2026-09-27 11:57 JST）とローカルmain `ee522a536fa7f418ce3247c64eb77ec55d1d8d86`（B-1023）である。

### 4.3 主要指摘と改訂

| ID / 優先度 | 第3版の箇所 | 指摘、根拠、影響 | 第4版の対処 |
| --- | --- | --- | --- |
| V01 高 | 冒頭の基準、第3節、第10節 | ローカルmainはorigin/mainに対してahead 14、behind 43で、B-1010〜B-1023はどのremote branchにも含まれない。根拠にした`meter_chain_join.rs`、`HyphaChainClockPolicy.h`、`HostAuxiliaryClock.h`、`tests/host_clock_diagnostic/`はorigin/mainに無い。B-1015、B-1016、B-1022はorigin/mainの別commitにも使われ、INV番号もS34以降が別内容。FFIの`lib.rs`もorigin/main 5252行、ローカル4800行と異なる | 冒頭、第3節、第10節で「ローカルのみ」を明記。基点の統合判断をG0と第15節へ入れた |
| V02 高 | 第5.3節、第2節のloop/seek | Apple Audio Unit Programming GuideはResetを再生位置の移動で内部状態を戻す処理と説明し、JUCE submoduleのVST3 wrapperは`setProcessing(false)`、AU wrapperは`Reset`で`reset()`を呼ぶ。遅延を持つchainでは境界直後に未対応区間が生じ得る。第3版の一律中断のままでは、seekや停止後の再開のたびに比較が止まり、loop/seek継続という目標と両立しない | 第5.3節で境界、故障、解析不足を分け、境界の扱いを三案（A、A'、B）で示した。推奨A'はReference通常B試聴の既存挙動（停止や準備未完了でBの選択を解除）と揃う |
| V03 中 | 第7.2節 | 周期T秒のloopでは、任意の時点のPinのうち4/Tが折返しをまたぎ、8秒のloopでは半分が失敗する。第3版の代替は「次の4秒」だけ | 観測したloop範囲を次の周回で取得する代替を追加した。次の折返しの観測後に既存の将来範囲取得（INV-S22）を発行するため、発行後の巻戻しを拒否する既存条件を満たす。直前周回の末尾4秒を遡る代替は、8秒の履歴が要るため容量の判断へ回した |
| V04 中 | 第1節、第7.3節 | 固定範囲がDAWのloop内にあれば巻戻しなしで次の周回に固定音へ入ることが書かれておらず、DAW操作の負担を過大に見積もる | 第1節と第7.3節に追記し、LC-18の観察対象へ入れた |
| V05 中 | 第8節 | Studio One/Studio Pro（pin）、Logic Pro（Linkの「シングル」はproject全体）、Pro Tools（Target）は既定でplugin画面を置き換える。第3版は「可能性がある」に留まり、案内や代替案が無い | 第8節にhost別の表と三案（E1、E2、E3）を追加。推奨はE1+E2 |
| V06 中 | 第9節 | origin/mainのINV-S37（100%/125%でFooterを畳む）、INV-S38（100%は見るだけの面）、INV-S39（900×600超は一様拡大）が未反映 | 第9節で100%の入口、縮小時の名指し、POST復帰と`RETURN`の扱いを示し、INV-S38の例外として承認事項へ入れた。LC-28を追加 |
| V07 中 | 第5.2節 | 動的PDCの観測不能を出荷の障害とするだけで、影響を小さくする候補が無い | 開始、補正、再開には使わない停止専用の相関監視を、誤停止率と検出遅れの測定付きでG1の評価候補にした。LC-29を追加 |
| V08 低 | 第6.3節 | RTのsample guardの閾値が未定義 | 承認revisionに結び付いたCをsample peakの閾値とし、inter-sample peakはworkerで記録すると定義した |
| V09 低 | 第5.1節、第7節、第11節 | 固定ABとBlindの間のlive転送の寿命が未定義で、調整へ戻る際の待ちと容量が決まらない | 非可聴で保持する案とし、容量ledgerに計上した |
| V10 低 | 外部調査第1節、精査記録R13 | Perception ABのPRE基準について、マニュアルはp.8で「unlikely to cause clipping」、p.21で「ensures the plugin will never cause clipping」と書き、記述が一致しない。第2版の記述はp.21の引用であり、R13の「誤り」は片方だけを見た判断 | 外部調査第1節で両記述を示し、どちらもHyphaの根拠にしないとした |
| V11 低 | 第3節、第5.1節、第9節 | 第2版にあった有用な事実（INV-S25の対称5 ms遷移と`LocalBlindTransition`の流用、B-758のABI不一致の教訓、ReferenceのA/B/Cの常時到達、`MATCH`の手順）が落ちていた | 各節へ戻した |
| V12 低 | 第2節、第5.1節 | ABLM2のmanual p.7は、異なるformatのsenderとreceiverが通信できないと書く。Hyphaは既存pairがformatをまたぐため、live転送もformatに依存させない方がよい | 第2節と第5.1節に明記し、LC-05へ追加した |
| V13 低 | 第5.2節、第12節 | Metric ABの1.4.0の変更履歴は、Logic Proでbus trackを有効にした場合などにhostが正しいPDC情報を渡さないとして手動補正を設けている | Logic Proのhost認定をrouting条件別に行うと明記した |

### 4.4 第3版の指摘の検証

| 第3版の指摘 | 検証結果 |
| --- | --- |
| R01 counter差 | 確認。数学的な反例として正しい。role-local counterを比較できないという注記は`meter_chain_join.rs`にあるが、ローカルmainにしか無い |
| R02 B1の範囲と動的PDC | 確認し補強。PDC Validation Delayの`reset()`に加え、Appleの資料とJUCE wrapperの実装で、再生位置の移動によるresetが一般的な前提であることを確かめた |
| R03 不足時の矛盾 | 確認。ただし、第3版の一律中断は境界に対して新たな矛盾を生む（V02） |
| R04 descriptor | 確認 |
| R05 Pinのcutoff | 確認 |
| R06 DAW巻戻し | 確認。`LocalBlindTrial`は固定範囲の開始からの再生を待つ。loop内なら巻戻し操作は要らない（V04） |
| R07 trialの共有 | 確認。`LocalBlindTrial`は割当、heard、回答、revealを同じobjectに持つ |
| R08 容量 | 確認。`LocalBlindTrial`のconstructorはPCMのvectorを値で受け、`LocalBlindPreparation.cpp`はcaptureの`*a`、`*b`を渡すためコピーが生じる |
| R09 gain窓 | 確認。`reference_gain.rs`は400 ms窓、100 ms hop、27連続blockで、3秒の試験が27 blockを返す。TRACK/STEMはexact 4秒を200個の20 ms窓に分け、3窓以上を要求する |
| R10 editor | 確認し具体化（V05） |
| R11 解析と音声 | 確認 |
| R12 delta範囲 | 確認。2MIXは`checked_level_milli`で−100超〜+24、TRACK/STEMは`checked_gain_delta_milli`で絶対値24以内 |
| R13 Perception ABのclip | 一部修正（V10） |
| R14 6 dBと内部方式 | 確認 |
| R15 XNU | 確認。`PSHMNAMLEN`は31、名前は`PSHMNAMLEN + 1`で取り込まれ、割当済みの`pshm_truncate`は`EINVAL`を返す |
| R16 Metric AB | 確認。1.4.0（2023-08-04）で4方式とPDC Modeを導入。最新は1.5.0（2026-07-29） |

### 4.5 検証と限界

| 確認 | 結果 |
| --- | --- |
| 第3版3文書、関連ソース、不変条件、README、B1証跡の照合（origin/mainとローカルmainの両方） | pass。主要な差は第4.3節と第4.4節へ記録 |
| gitの分岐、B番号の重複、参照ファイルの有無 | pass。ahead 14、behind 43、重複3件、ローカルのみのfile 4種 |
| 公式資料の照合（Perception AB guide、ABLM2 manual、GainMatch manual、Apple AU guide、Apple Logic Proガイド、XNU header/実装、Metric AB変更履歴、VST3資料） | pass。Perception ABのguideはp.7、8、11、12、15、21を画像で確認 |
| 第三者資料（Studio One Forumの管理者回答、Production ExpertのPro Tools解説） | 取得。公式資料ではないと本文に明記 |
| 改訂文書のlocal link、code fence、末尾空白、容量の算術 | pass。容量は4/T、8秒履歴の追加46.875 MiBを含めて再計算 |
| Rust/native/実host/性能/実音/installer試験 | skip。文書改訂のみで製品変更なし |

4/Tは、DAWのloopを一定周期で回し、Pinの時点が周期内で一様に分布すると仮定した値である。
host別の画面置換は各資料の記載であり、Hyphaでの実機観察ではない。
停止専用の相関監視は候補であり、誤停止率を測っていない。

### 4.6 未処理と申し送り

- 基点の統合: ローカルmainのB-1010〜B-1023をorigin/mainへどう統合するかは、本計画の範囲外だが着手の前提である。利用者の判断を要する。
- 計画第15節の判断: 境界規則、editor非表示時の扱い、100%での戻る操作、Pinの代替、停止専用監視など。
- G0〜G6は未実施。
- Notionの現在地、日次ログ、Handoffは、利用者の指示により記録していない。
- Commitは作成していない。第4版はこのworktreeの未追跡文書であり、メインcheckoutの`_v3`文書と第1版の未追跡文書はそのまま残っている。

### 4.7 利用者の決定（2026-09-27）

第4版の提示後、利用者は次のとおり決めた。
計画の冒頭、第5.3節、第7.2節、第8節、第9節、第11節、第13節、第15節、第16節へ反映した。

| 判断 | 決定 |
| --- | --- |
| 基点の統合 | 別セッションで調査と統合案の作成を開始した |
| 境界規則 | A'（中断と1操作の再選択）を採用。折返しで未証明区間が生じるhostでのBの採否はG1の後に決める |
| editor非表示時の扱い | E1とE2の組合せを採用。E3は採らない |
| 100%での戻る操作 | POST復帰と`RETURN`を100%にも残し、INV-S38の例外とする |
| Pinの代替 | 観測範囲の次周回取得と次の4秒取得を採用。直前周回の末尾4秒を遡る代替は採らない |

最後の項目は、第4版の報告で推奨を明記していなかったため、「採らない」を推奨として示したうえで反映した。
正本（AGENTS、INV、README、共通安全契約）はまだ変更していない。

## 5. 第2版の精査と第3版への改訂（別セッション作成）

以下は第3版の精査記録の本文である。
見出しの階層と、統合後の文書名へのlinkだけを変えた。
行番号は第2版を指し、第4版には対応しない。
本文中の`meter_chain_join.rs`へのlinkは、ローカルmainにだけ存在するファイルを指す（第4.3節V01）。

作成日: 2026-09-27。
対象は利用者がDownloadsから添付した2文書であり、リポジトリ内に先行して存在した同名文書ではない。
添付文書の実装指示はレビュー対象として扱い、製品変更の許可へ読み替えていない。

### 5.1 判定

改訂が必要。
連続比較、固定AB、Blindを一つの比較sessionにする方針は支持する。
ただし、第2版のまま同期、障害復帰、固定音の再生、trial状態、容量を実装へ渡すと、比較の信頼性と操作に問題が残る。
第3版はこれらを修正した設計提案であり、技術的な成立性を実証した完成仕様ではない。

- [外部調査 第3版](hypha_live_chain_compare_external_research_20260927.md)
- [実装計画 第3版](hypha_live_chain_compare_implementation_plan_20260927.md)

日本語技術文書スキルの観点に従い、確認済みの事実、設計提案、未実証の条件、利用者判断を分離した。
追補だけで矛盾を残さず、本文全体を組み直した。
元の添付2ファイルとリポジトリ内の先行版は上書きしていない。

### 5.2 入力の識別

| 入力 | 行数 | SHA-256 |
| --- | ---: | --- |
| Downloads/hypha_live_chain_compare_external_research_20260927.md | 189 | f893baee104492c13a44772c53aff054ca7e13081308a75f127cc49bbbc7699a |
| Downloads/hypha_live_chain_compare_implementation_plan_20260927.md | 690 | 244795e236e01795cfe7075431855274319eda86b9be119360a57157deb33fc8 |

以下の行番号はこの添付第2版を指す。
現行ソース基点は `ee522a536fa7f418ce3247c64eb77ec55d1d8d86`、B-1023。
作業開始前から存在したJUCE submodule、host clock診断、構造検査、別計画の差分は維持した。

### 5.3 主要指摘と改訂

| ID / 優先度 | 第2版の箇所 | 指摘、根拠、影響 | 第3版の対処 |
| --- | --- | --- | --- |
| R01 高 | 計画230–257 | local counter差一定を時刻対応へ使う説明は不成立。PRE256→POST64×4でも欠落なしに差が変化する。[meter_chain_join.rs](../../crates/kirin_measure/src/meter_chain_join.rs)もrole-local counterの直接比較を否定 | 計画5.2。sample対応、deadline、境界を分離し、exact coverageと認定profileへ戻した |
| R02 高 | 計画234–247、370、LC-20 / 調査171、186 | B1は静的な完成captureの証拠。seek flush、loop境界、動的PDCの最初の影響blockを保証しない。通知なしで内容遅延だけ変わる場合はclock/counterでは識別不能 | 計画5.2、G1/G1R、LC-14/20。最初の観測可能な不整合と、影響前検出保証を分離。未達は出荷判断の障害 |
| R03 高 | 計画204と249/263/354 | 不足でrun失効と、不足区間だけPOST代替してPRE継続が衝突。PRE表示のまま処理後の音を聴かせ得る | 計画5.3/7.1。latched中断とactual receiptを定義。混在を正常PRE完走に数えない |
| R04 中 | 計画194–199 | 公開済みrun descriptorの非atomic length延長は、headのreleaseだけでは競合を防げない | 計画5.1。immutable descriptor、atomic frontier、close、退役ackを分離 |
| R05 高 | 計画395–405 | workerの直近4秒はRTクリック時点より古い可能性がある。追従中の素材を固定gainで鳴らしても過去の聴取出力と同じとは限らない | 計画7.2。RT受理のcutoff、watermark待機、gain更新凍結、raw/候補/採用gainを明示。別範囲へ自動代替しない |
| R06 中 | 計画394–411、D8、LC-18 | 遡及固定後のDAW巻戻しが欠ける。[renderer](../../juce_shell/src/local_blind/LocalBlindTrial.cpp)は範囲先頭の再生を待つため、取得待ちがなくても即聴取ではない | 計画1/7.3/9。DAW操作と総待ち時間を含むworkflowへ修正 |
| R07 高 | 計画410–433 | 「表示だけ変える」では匿名割当、heard、回答が記名ABから漏れる。[LocalBlindTrial.h](../../juce_shell/src/local_blind/LocalBlindTrial.h)はこれらを同じobjectへ保持 | 計画7.3。immutable payloadと再生処理だけを共有し、初回を含む全Blind開始でfresh trial |
| R08 中 | 計画525–547 | 117.25 MiBは小計。現行[factory](../../juce_shell/src/local_blind/LocalBlindPreparation.cpp)のコピーで768 kHz stereoは164.125 MiBとなり、scratch前に128 MiB案超過 | 計画11。単一payload所有への責務変更、future capture、seal、退役待ちを含むピークledger |
| R09 中 | 計画111、298–300、D7 | 2MIX算定器はexact4秒必須ではない。27個の400 ms窓/100 ms hopは標準rateで3秒。TRACKの3窓は独立した音楽イベント3個ではない | 計画3/6.2/LC-08。artifact長と算定最短量を分離。loop反復を独立試行の証拠としない |
| R10 中 | 計画373、D10、LC-22 | editor非表示終了は、隣のEQを開いて調整する操作を中断し得る。候補gain保持だけでは解決しない | 計画8/9/15。安全契約を勝手に緩めず、固定表示と中断負担を観察して判断 |
| R11 中 | 計画372 | 解析workerだけの遅延と、音声coverageの故障が同じ失効扱いで分離原則を弱める | 計画5.3/LC-24。解析保留と音声中断を分離。独立性が成立する場合だけ固定試聴を保持 |
| R12 中 | 計画111、gain範囲の説明 | [reference_gain.rs](../../crates/kirin_measure/src/reference_gain.rs)でTRACK deltaは絶対24 dB以内だが、2MIX deltaはlevel用判定の−100超〜+24。両policyが±24という説明は現行コードと異なる | 計画6.1。live共通±24 gateは新提案として扱い、既存Blind変更は互換性と承認を要する |
| R13 高 | 調査160 | Perception PRE基準をclip-freeとするのは誤り。公式は可能性が低いという説明で、POSTを増幅する場合もある | 調査1/3。増幅の有無とclipリスクを区別 |
| R14 中 | 調査36/160/164 | Perceptionの6 dBを前回からの更新幅と確定できない。競合3製品が信号のみで同期する内部方式も公開記述以上の断定 | 調査3.1/5.1。定義未確定と内部方式未確認を明示 |
| R15 中 | 調査179–182、計画181 | XNU制約は31文字でなく31 bytes。再ftruncate拒否はApple実装で確認でき、App Group命名だけではhost権限を得られない | 調査5.3、計画5.1/14。一次資料へ差替え、host sandbox試験を残した |
| R16 低 | 調査40/55/120/174 | Metric AB4方式は1.4導入。匿名性の「残し」は否定が脱落。調査側のQuietest/HOLD/FOLLOW案も計画側と不整合 | 調査3.2/5.1/6。版、匿名表示、基準と更新方法を揃えた |

R01のcounter例は数学的な反例であり、特定DAWでそのscheduleを観測した証拠ではない。
R02の観測不能性も設計上の区別であり、すべてのhostが通知を欠落させると主張するものではない。
R12はソース上の条件差であり、実際の利用者被害や安全事故を再現したという報告ではない。

### 5.4 再レビューで追加した条件

作成した第3版を構造とworkflowの二つの観点で再レビューした。
その結果、host bypassの入力不変、音量適用前の取消と減衰適用後の中断の区別、故障時に新しい減衰を勝手に生成しない条件を補った。
観測TP基準の説明を式に合わせ、Pin受理時の追従更新停止を明示した。
liveだけで終了できること、既存Blind直接入口を残すこと、未回答やreveal前でも終了できることも戻した。

### 5.5 検証と限界

実施した確認は以下であり、製品試験とは分ける。

| 確認 | 結果 |
| --- | --- |
| 添付2文書、関連ソース、既存不変条件と証跡の照合 | pass。主要な不一致は上表へ記録 |
| 公式manual、host API、OS資料の照合 | pass。GainMatchの公開Google文書も再取得。内部方式と版の不明点は明示 |
| 利用者投稿の再取得 | partial。DOA、Steinberg、Ozoneは本文確認。KVRは403、P7/P8 Redditは取得不可で継承情報と注記 |
| Nodeによる算術と抽象反例 | pass。gain式14ケース、3秒/27窓、3rate容量、counter差反例、delta validation差を確認 |
| 改訂3文書のlocal参照、fence、空白、入力hash | pass。local参照39件、受入試験ID25件、入力hash2件、fence/空白を確認。既存tracked差分のgit diff --checkもpass |
| Rust/native/実host/性能/実音/installer試験 | skip。文書改訂のみで製品変更なし |

容量小計は48 kHzで7.38671875 MiB、192 kHzで29.359375 MiB、768 kHzで117.25 MiBだった。
768 kHzに現行factoryのpairコピー46.875 MiBを加えると164.125 MiBになる。
これらはRSS実測ではない。
gainの14ケースは符号と等音量式の算術確認であり、聴感、LUFS算定、DSP保護の検証ではない。
モデルでPDC通知を与えない例は、実hostの観測ではない。

### 5.6 未記録のセッション情報と次作業

セッション記録手順を読み、プロジェクトのNotion書込み禁止を優先した。
現在地、日次ログ、Notion Handoffは実行していない。
代替接続の導入や権限迂回も行っていない。

現在地: 添付第2版を精査し、外部調査と実装計画の第3版、および本記録を作成。
製品コード、契約正本、実機、配布物は未変更。
日次ログ相当: 2026-09-27、公式資料再照合と三観点のレビュー、算術確認、文書改訂。
Handoff: 次の実装担当は第3版のG0/G1を出発点とし、同期三条件、動的PDC、editor制約、DAW再生手順、payload所有と復帰policyを先に検証する。
本文の将来工程を、そのまま実装・実機操作・公開への許可とみなさない。

Commit: 新規commitなし。基点 `ee522a536fa7f418ce3247c64eb77ec55d1d8d86` / B-1023。
変更: 本レビューで新規文書3ファイル（+807 / −0）。元の添付と先行版は維持。作業前からのtracked差分+20 / −8は本件に含めない。
LSアップ用: skip。
HPアップ用: macOS skip、Windows skip。
未処理: G0の承認、G1の成立性検証、投稿の再取得、Notion記録禁止による未記録。
