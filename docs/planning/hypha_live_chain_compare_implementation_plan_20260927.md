# Hypha 連続PRE/POST比較とBlindをつなぐ実装計画 第14版

作成日: 2026-09-27。
改訂: 第14版（2026-09-28）。厳しめの見直し（[精査記録](hypha_live_chain_compare_review_20260927.md)第0節）の指摘と、AAXについての利用者の指示（重要な対象とする、出来る限り完璧に近く他のプラグインより高い精度を保つ）を反映し、Pro Tools（AAX）の実測（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第9節）を加えた。
第13版（同日、1096行、SHA-256 `454762c1e1bf39f81ea3cf1f2e7d61b5119c5070508e4c9d1d92d96a7213c78e`）は、G1Rで利用者が決めた4件を記録した版である。
それより前の版の記録、確認基点、版ごとの改訂表は、付録Aへ移した（内容は第13版のまま）。
状態: 計画案。製品実装、契約変更、実機検証、公開は未実施。
本版は新たな実装や契約変更の承認記録ではない。
併読: [G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)、[契約改定案](hypha_live_chain_compare_contract_draft_20260928.md)（第2版）、[外部調査](hypha_live_chain_compare_external_research_20260927.md)（第5版）、[精査記録](hypha_live_chain_compare_review_20260927.md)、[第6版の精査記録](hypha_live_chain_compare_review_v6_20260927.md)（別セッション作成）。
併読文書と本書が食い違う場合は、本書第5.3、7.2、7.3、8.1節の条件付きの記述を優先する。

## 要約

目的: 同じチャンネルのPRE→処理→POSTを、再生を止めずに等音量で切り替え、同じ4秒を固定してBlindまで一続きに使えるようにする。

### 今の設計

| 要素 | 設計 | 根拠と状態 |
| --- | --- | --- |
| 対応の鍵 | 連続時計（VST3連続時刻、AU render時刻）と、定常区間でproject時刻の一致から較正した差K | Studio Pro 8.1.2で実測（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第2、6〜8節）。AAXはPro Tools Developer 2026.4で、pluginが数えるframe数を連続時計にして成り立った（同第9節、第5.4節） |
| 周回 | PREは時計の不連続と自分の呼出しの空白で周回を改める。POSTは今の周回で書かれた範囲だけを受け入れる | plugin内で誤受入れ0。ただし識別PCMで見分けられる誤りに限る |
| 呼出しの空白 | 各側の空白を時計の不連続として扱う。POSTは自分の空白でKを無効にして較正し直す。AUは候補の食い違いでも無効にする（M1） | 判定値と較正回数はhost profileの値。今の値はStudio Proでの初期値（第5.2節） |
| 境界 | 案C。時計の規則で対応を確かめられない区間はPOSTを鳴らし、確かめられた最初のblockからPREへ対称5 msで戻す。PREを選んだままPOSTが鳴る間は「PRE待ち」を示す | 2026-09-28決定（G1R）。遷移と表示は第14版で加えた（第5.3節） |
| 静かな区間 | hostのplugin sleepを、infinite tail（AAXはAlwaysProcess）で避ける案が本命。副作用と報告する範囲を確かめてから採る | G1R。AAXの属性は静的なので「比較中だけ」は選べない |
| 遅延変更の直後 | 時計で検出できない短い区間の誤対応を許容し、説明書に記す | G1R。報告されない変更と、再生中に補償を改めないhostは許容の外。Pro Tools 2026.4は再生中に反映し、M1で1〜2 block |
| 遅延の報告の誤り | 開始時と定期的に中身のずれを推定し、時計の対応と食い違えば警告する。止めず、補正しない。DAWの遅延補償への依存を説明書に記す | 2026-09-28、見直しで決定（利用者が判断を委任し、推奨を採用）。比較の途中で中身のずれが跳んだら、POSTへ倒して再生の停止と再開の後に戻す（同日、利用者が推奨を採用。第5.4節） |
| multi-mono（AAX） | 提供する。全channelを同じblockで切り替え、channelの間でPREとPOSTを混ぜない | 2026-09-28、利用者が推奨を採用（第5.4節） |
| 故障 | 説明できない欠落、protocol/pair/format失効、上限超過は比較中断と再選択 | 第4版から維持 |
| 転送 | platformごとに事前確保した転送経路。方式はG1-06で決める | 第5.1節 |

### 決定済み（計画上）

- 2026-09-27: editor非表示はE1＋E2。100%でもPOST復帰とRETURNを残す。Pinの代替は次周回予約と次の4秒で、遡及は採らない。
- 2026-09-28: 上表の対応の鍵、呼出しの空白、静かな区間、遅延変更の直後、境界C（A′を置き換え）、遅延の報告の誤りへの警告。AAXを重要な対象とする（利用者の指示）。
- 2026-09-28: AAXはプロが使うので、出来る限り完璧に近いpluginにし、他のプラグインよりも精度を高く保つ（利用者の指示）。品質目標は第5.4節。
- 2026-09-28: 比較の途中で中身のずれが跳んだら（補償を改めないhostや、報告しないpluginの遅延変更など）、POSTへ倒し、再生の停止と再開の後に戻す。AAXのmulti-monoでも比較を提供し、全channelを同じblockで切り替える（利用者が推奨を採用）。
- 契約の正本（AGENTS、INV、README、共通安全契約）は、実装の承認時に[契約改定案](hypha_live_chain_compare_contract_draft_20260928.md)に沿って改める。

### 分かっている限界

- **遅延の報告への依存**: 間のpluginが遅延を正しく報告し、DAWが補償することを前提にする。崩れると、時計の規則では検出できず、ずれが続く。中身の推定による警告で扱う。
- **周回単位の取り違え**: chainの遅延がloop長以上だと、Kを周回単位でずらして較正し得る。G1の照合（識別PCM）は、この誤りを検出できない。
- **AAX**: Pro Tools Developer 2026.4（Intel Mac、48 kHz）では、POSTに遅延補償済みの位置が渡り、方式が成り立った（G1記録第9節）。製品版、Apple silicon、Windows、multi-mono、他のbufferは未確認である。

### 未決

R-12と安全契約の改定、既存Blindのhost gate、E2でのowner、exact 4秒loopへの対応、Kの較正と照合の細則、infinite tailとAlwaysProcessの副作用と範囲、周回を特定する手段、AAXの時計と鍵、補償済みの位置が渡らないhostの扱い、multi-monoの同時切替の実現方式、遅延補償のOFFの扱い、中身のずれの跳びを検出する条件。

### 次の一手

0. 第1段階の実装を始めた（2026-09-28、利用者が段階的な実装を承認）。macOSで、再生中のPRE/POST切替（固定の音量一致、境界規則C、VST3・AU・AAX）を作る。最初のPRは、対応の中核（ring、周回、空白、K、M1）と試験、契約の正本（AGENTS、INV-LC1〜LC3）である。
   - 実機確認（2026-09-28、bb1cfc26の公証済みVST3・AUと未署名のAAX診断版、48 kHz）: Studio Pro 8のVST3で、PRE→4096サンプル遅延→−6 dB→POSTのchainに、POSTの後ろの計測用Hyphaで出力を測った。POST選択で−29.0 LUFS、PRE選択で−23.0 LUFS、MATCHは−6.00 dBで、PRE選択中の出力は−29.0 LUFSになった。AUでは対応が成立し（PRE表示）、無音でのMATCHは信号不足を通知した。Pro Tools Developer 2026.4のAAX（stereo、−6 dBのTrim）でも、−29.0→−23.0 LUFS、MATCH −6.00 dB、終了を確かめた。停止中はPRE WAITを示した。
   - 実機で分かったこと: Studio Proは同じchannelのplugin画面を1つの窓で使い回し、Pro ToolsはTarget窓を置き換える。どちらも画面が閉じてsessionが終わる（E2どおり）。画面を開いたままにする方法（ピン留め、Targetのオフ）を入口とREADMEに書いた。通知は300%の状態欄に全文が入る長さにした。短い待ちが画面の更新の間に終わると見逃すため、待ちを記録してWAITを必ず示すようにした。マスタリングのように大きく持ち上げるchainでは、MATCHがTP上限で止まりやすいため、MATCHの操作にTP LIMITを出す。
   - 実機確認の後の追加（2026-09-28、利用者「推奨で良い。どんどん進めて完成させて下さい」）: 承認付きのPOST減衰を第3段階から前倒しした（B-1076、INV-LC14。MATCHがTP上限を超えるときに「POSTを下げる／PREを上限まで」を選ぶ。減衰はRETURNまで保持）。中身のずれの推定と警告、跳びでのPOST保留を入れた（B-1077、INV-LC7、LC10。GCC-PHAT、2回一致で確定）。第2段階としてPIN 4 Sを入れた（B-1078、INV-LC15。直前4秒を一続きのproject範囲として固定し、ローカルBlindの受け入れへ渡す）。記名固定AB、追従（AUTO）、Windowsの転送はこの後に続ける。
   - 実機確認（2026-09-28、ab931f9cの公証済みVST3、Studio Pro 8、48 kHz、300%）: PRE → Mixtool +18 dB → Limiter → POSTで、MATCHは「PREは+14.97 dB必要、TP上限までは+11.63 dB」を示し、「POSTを14.97 dB下げる」を選ぶとPOST −15.0 dBになり、Studio Proの出力メーターはPRE選択時とPOST選択時で同じ長さだった（RETURNの後はPOSTで長くなった）。終了の後は「RETURN +15.0 dB」が残り、RETURNで通常の音量に戻った。PIN 4 Sは直前4秒（00:08.756〜00:12.756）でBlindを「範囲の準備完了」で開き、試聴2周、回答、結果（SOURCE 1 = POST / SOURCE 2 = PRE）、「今の音に戻す」まで進んだ。報告のない遅延として、Beat Delay（1/32T、wet 100%、feedback 0、Pong-Factor Same）をPREとPOSTの間に入れると「PREが41.69 ms早く鳴っています」（2001 samples。filterの群遅延で1 sample）を示し、再生中に1/32へ変えると「PRE WAIT」（POSTを保持）になり、停止と再生で解除されて新しい基準の62.52 msを示した。Pong-Factor Dotted（Sameに変えると決まったため、左右の遅延が違うと見られる）と、LFO量0.02が残ったAnalog Delayでは、ピークが1つに決まらず何も示さなかった（INV-LC7の「決まらないときは何も言わない」どおり）。+18 dBとlimiterを通したchain（POSTの短期ラウドネス約−8 LUFS、MAX TP +0.7 dBTP）でも誤った警告は出なかった。
   - 同じ確認で見つかったこと（B-1079）: ずれのms表示がhostの`getSampleRate()`を使っていたため、hostを通さずに準備したprocessorでは値が壊れた（試験で「PRE 2000000.00 ms early」）。ringに刻んだ標本化周波数で換算するように直し、実物のPRE/POST、ring、画面を通す端から端までの試験（報告のない2000 samplesで「PRE 41.67 ms early」、3000への跳びで保持と「PRE held: latency changed」、停止で解除）を加えた。
   - 第3段階の追従（B-1080、INV-LC16）: 一致済みのMATCHを押すと「もう一度MATCH」と「AUTO」を選べる。AUTOは第6.2節の実験値（1秒ごと、許容差0.5 dB、MATCHから±6 dB、50 msのramp）で、MATCHで承認したceilingを上げず、POSTを動かさず、範囲を出るときは止めて通知する。フッターの幅を増やさないよう、AUTOはMATCHの枠に表示する。PREのgainの変化は、PREが鳴っている間は50 msの直線rampにした（新しいMATCHにも効く）。値は聴取で決めるまで実験値として画面に出す。
1. AAXの残り: WindowsのPro Tools、製品版、multi-mono、遅延補償のOFF、hostの通知を待ってから音の遅延を変える型の遅延変更。
2. Windows、他のbuffer設定、報告が先に来る型の遅延変更。
3. 周回ごとに印が変わるfixtureと、遅延がloop長以上の条件。
4. infinite tailとAlwaysProcessの副作用、中身のずれの警告の方式。
G1の結果なしに対応hostを確定しない。

## 0. 採用方針と実装前の障害

「調整しながら比較する → 区間を固定する → 匿名で聴く → 調整へ戻る」を一つの比較sessionにする方針は維持する。
後発としての価値は機能数よりも、音量、時間、音源、終了方法について利用者が迷わないことに置く。
loop、seek、追従補正、Blind統合を、承認なく後続フェーズへ送ることはしない。

第2版の同期説明は、完成した4秒captureの実証から連続再生の保証へ踏み込みすぎていた。
第3版はこれを実装前の成立性gateへ戻した。
第4版と本版もその判断を引き継ぐ。

| 論点 | 判断（第3版から維持） |
| --- | --- |
| PRE/POSTのlocal counter差 | 同期の一般的な権威にしない。異なるcallback分割でもexactな範囲coverageで判定する |
| 同一native位置 | 既存の有力な対応候補。ただしhostごとの静的PCM対応、到着期限、境界動作を別々に実証する |
| loop、seek、動的PDC | 固定stream差だけでは保証できない。未実証のまま「再Sync不要」と宣伝しない |
| 固定ABとBlind | PCMと再生処理は共有し、匿名割当と回答状態は毎回新規にする |
| 直前4秒の固定 | RTで確定した終端を使う。workerの古い末尾へ置き換えない。聴き直すDAW操作を含める |
| 容量 | 117.25 MiBは限定したpayload小計。既存factoryの複製を含むと768 kHz stereoで164.125 MiBになり、128 MiB案を超える |

### 第4版から維持する判断と第5版での補足

| 論点 | 第3版 | 第4版の決定と第5版の条件 |
| --- | --- | --- |
| 調査の基点 | ローカルmain ee522a53 | 分岐は維持。第4版の83b383a6と今回の5a6a9db5を区別し、統合後の参照と日本語UI契約をG0で再照合する |
| host起点の境界 | 音声不足は一律に比較中断 | resetやhost scheduling次第で対応未証明の区間が生じ得る。片側証拠の先着は分類待ちとして扱い、POSTの必要範囲に証明済みの対応がある間はPREを続ける（第6版で修正）。A′の明示再選択を維持する（2026-09-28のG1RでCへ置き換え） |
| Pinの失敗 | 直前4秒が単一範囲でなければ失敗 | 次周回のexact4秒を明示予約する代替を維持。ただし既存の1秒先＋4秒取得の直用では4秒loopに収まらず、新しい予約方式の実証が必要。4/Tは一様位相の仮定下の割合のみ |
| 固定音の再生 | DAWの巻戻しを明示 | loopへの包含に加え、範囲先頭を含む正確なcallbackと完走までの連続性が得られる場合に限り、巻戻しを省ける。余白付き窓の範囲では、非分割hostでもこの条件が位相によらず成立する（第8版） |
| editorの非表示 | 契約は変えずに観察 | host設定による画面置換にE1+E2で対応する。PRE/固定音の停止、既承認POST減衰、解析lease、非可聴cacheを区別する |
| 小さい画面 | 全基準サイズで操作 | liveでは100%に入口を出さず復帰操作を残す。Blindは既存の最小900×600の隔離面を維持し、縮小時のPRE/POST名指しを匿名面へ持ち込まない |
| 動的PDC | 観測できなければ出荷の障害 | 開始や補正には使わない停止専用の相関監視を、G1の評価候補に加える（2026-09-28のG1Rで不採用。同日の見直しで、止めない警告へ改めた。第5.2節） |
| RT sample guard | 閾値が未定義 | 承認時の観測TP基準Cを、sample peakの閾値に使う。inter-sample peakはworkerで別に記録する |
| live転送の保持 | 記載なし | 固定AB/Blind中も予算内で保持候補とする。新鮮な証拠が維持される場合だけ復帰準備を短縮し、待ちなしを保証しない。背景経路の故障を固定artifactと分離 |
| 競合資料 | Perception ABのPRE基準は「clipの可能性が低い」 | 同じマニュアルでp.8は可能性が低い、p.21は決してclipしないと書き、記述が一致しない。どちらもHyphaの根拠にしない |

### 利用者の決定（2026-09-27）

| 判断 | 決定 | 反映先 |
| --- | --- | --- |
| 基点の統合 | 別セッションで調査と統合案の作成を開始した。結果を受けて統合方法を決めるまで、本計画の着手条件は満たさない | 第3節、第12節のG0 |
| 境界規則 | A'（中断と1操作の再選択）を採用した。loopの折返しのたびに未証明区間が生じるhostでBを採るか対象外にするかは、G1の結果を見て改めて決める。2026-09-28のG1RでCへ置き換えた（下記） | 第5.3節 |
| editor非表示時の扱い | E1（固定方法の案内）とE2（可聴の比較だけを終え、準備と承認済みgainを保持）を採用した。E3は採らない | 第8節 |
| 100%での戻る操作 | POSTへ戻す操作とRETURNを100%にも残し、INV-S38の例外とする | 第9節 |
| Pinの代替 | 観測したloop範囲の次周回取得と、次の4秒の取得を採用した。直前周回の末尾4秒を遡る代替は採らない。G4の観察で次周回の待ちが負担と分かれば再提案する | 第7.2節、第11節 |
| 対応の鍵（2026-09-28） | Studio Pro 8.1.2の実測を受け、「連続時計（VST3連続時刻、AU render時刻）+ 定常区間で較正した差K」を対応の鍵の設計基準にした。対応hostとしての確定は、WindowsとG1-04（動的PDC）の実測の後にする | 第5.2節、第15節 |
| 呼出しの空白（2026-09-28、G1R） | 各側の呼出しの空白を時計の不連続として扱う規則を採用した。PREは周回を改め、POSTはKを無効にして較正し直す | 第5.2節、第15節 |
| 静かな区間（2026-09-28、G1R） | infinite tailの報告でplugin sleepを避ける案を本命にした。bounceの末尾、AU、他のhostでの副作用を確かめてから採用し、副作用があれば無音の後の自動復帰にする | 第5.3節、第15節 |
| 動的PDCの直後（2026-09-28、G1R） | 遅延変更の直後に残る2〜4 block（最大171 ms）の誤対応を許容し、説明書に記す。AUにはM1を加える。中身による検出と、遅延変更を伴う構成での比較停止は採らない。同日の見直しで、止めずに警告する中身の推定を加えた（下記） | 第5.2節、第12.1節、第15節 |
| 境界規則（2026-09-28、G1R） | A′を案Cに置き換えた。どの境界でも、証明できない区間はPOSTを鳴らし、証明が戻り次第PREへ自動で戻す。説明できない欠落と失効は従来どおり中断と再選択 | 第5.3節、第15節 |
| 遅延の報告の誤り（2026-09-28、見直し） | 時計の規則は、遅延の報告の誤りを検出できない。比較の開始時と定期的に中身のずれを推定し、時計の対応と食い違えば警告する。止めず、Kも補正しない。DAWの遅延補償に依存することを説明書に記す。利用者が判断を委任し、推奨（案a）を採用した | 第5.2節、第12.1節（G1-05）、LC-29 |
| AAX（2026-09-28、利用者の指示） | AAXを、VST3、AUと並ぶ重要な対象として扱う。同じ規則が成り立つかを、Pro Toolsで最初に測る（同日に実施。G1記録第9節） | 第5.4節、第12.1節、LC-33 |
| 遅延変更の跳びとmulti-mono（2026-09-28、利用者が推奨を採用） | 比較の途中で中身のずれが跳び、時計は変わらないときは、補償されていない遅延変更の境界として扱う。POSTを出し、再生の停止と再開の後に確かめ直して戻す（INV-LC10）。AAXのmulti-monoでも比較を提供し、全channelを同じblockで切り替える（INV-LC9） | 第5.4節、LC-20、LC-33 |
| 実装の開始（2026-09-28、利用者の決定） | 段階的に実装する。第1段階はmacOSで、再生中のPRE/POST切替（固定の音量一致、境界規則C、VST3・AU・AAX）。続いて4秒の固定→固定AB→Blind、追従の音量一致、Windows。契約の正本は実装の開始と同時に改める（AGENTS、INV-LC1〜LC3。READMEとINV-LC4以降は、実装と試験ができた時点） | 第12節、第16節 |
| AAXの品質（2026-09-28、利用者の指示） | 「AAXではプロが使うので、出来る限り完璧に近いプラグインにする必要がある。他のプラグインよりも精度を高く保つ」。品質目標を出荷の条件にし、満たせない条件が見つかったら事実と選択肢を示して判断を求める | 第5.4節、第6.2節、LC-33 |

これらは計画上の決定である。
AGENTS、INV、README、共通安全契約の正本は、実装の承認時に改める。

第5版の追加修正は、既存future captureと次周回Pinの区別、境界証拠の片側先着、E2の出力と資源、Blindのサイズ/匿名性、段階間gain復帰である。
同期とPinの実現方式、保持中の資源権限、現行契約の例外はG1/G1Rへ残し、文書改訂を実証済みの意味にしない。

## 1. 完成させる操作

同じチャンネルのPRE→処理chain→POSTを選び、明示開始後に同期とgain候補を準備する。
準備中は通常POST入力を変更しない。
準備が整ったら、適用予定のPREとPOSTの補正量を示し、利用者のMATCHで比較gainを承認する。
準備完了や承認だけではPREを鳴らさず、利用者の選択でPRE/POSTを切り替える。
開始後の切替は1操作、全体終了も1操作から要求できる設計を目標にする。
同期所要時間は未測定であり、「数blockで完了」とは約束しない。

比較を始めるとき、hostに応じてHyphaの画面を開いたままにする方法を短く示す（第8節）。
固定補正を通常の判断用とし、調整中に補正を追従させたいときだけ追従補正を明示選択する。
どちらも同じsessionの操作として扱うが、追従中の音をそのままBlindの固定条件とは呼ばない。

区間固定は、クリック後にRTが受理した時点までの直前4秒のraw PRE/POSTを候補にする。
直前4秒がloopの折返しをまたぐなどで取得できなければ理由を示し、観測済みのloop内からexact4秒を指定して次周回に予約する操作と、次の4秒を取得する操作を別々の明示選択として用意する。
過去の固定範囲を聴くには、現行rendererではDAWがその範囲より前から再生する必要がある。
固定範囲がDAWのloopに含まれ、現行rendererが範囲先頭を含む正確なcallbackと完走までの連続性を確認できる場合は、巻戻しの操作を省ける。
Source 1の完走後は既存のSource 2自動armを維持するが、次周回で鳴るためにも同じ開始条件が必要である。
範囲が折返し後の再開位置から最大callback分以上後ろで始まり、折返し位置までに終わる場合（第7.2節の余白付き窓）は、hostが境界でblockを分割しなくても、この条件が位相によらず満たされる。
exact 4秒loopや、範囲先頭がこの余白に入る場合は満たされないことがある。
範囲先頭を取り逃す場合は、待機を成功扱いせず、第7.3節の再生可否と操作案内へつなぐ。
HyphaがDAWを自動seekする機能や、loop範囲を変える機能は本案に含めない。

固定ABで聴き比べ、そのPCMと固定gainのまま、新しい匿名trialへ進む。
Blindで両sourceを聴いて回答または保留し、reveal後は調整へ戻れる。
live比較だけで終了してよく、既存Blindの直接入口も共通factoryへ接続して残す。
liveや記名ABをBlind開始の必須前提にしない。
未回答、回答保留、reveal前を含む任意の段階から調整へ戻るか全体終了でき、回答とrevealを強制しない。
liveへ戻る際はPOSTを先に確認し、PREと追従補正の再開は明示操作にする。

## 2. 対象範囲

「チェーン全体」は選択したPRE入力からPOST入力までを指す。
他社pluginは処理を続け、POSTの試聴出力だけを置き換える。
DAWのbypass、Solo、Mute、Fader、Automation、Routing、Transport、Loop設定は操作しない。
CPU削減機能でもない。

| 対象 | 本案での扱い |
| --- | --- |
| 同一チャンネルのtrack、stem、mix/master bus | mono/stereo、TRACK/STEMと2MIXを対象候補にする。残りのmixと時刻が合うことも必須 |
| 別track、別bus、途中で他音源が加算される経路 | 対象外。Hyphaがroutingを自動確認できるとは扱わない |
| 同じhost内でformatが異なるPREとPOST | 追加の対象候補。protocol共通化だけでは同じ時計/latency意味を保証できない。PRE format×POST format、方向、process配置ごとに認定してから提供範囲を決める |
| channel数の不一致、surround、Atmos | 比較開始を拒否する。現行計測の対応から試聴対応を推定しない |
| POST前で分岐したsend、sidechain、並列経路 | 元のroutingを維持。一括切替済みと表示しない |
| POST後の処理 | 選んだ音を通常どおり処理。最終出力まで等音量になる保証はない |
| 複数trackのLink / Bypass All | 別の範囲判断。既存の同時試聴1枠を暗黙に拡張しない |
| 通常のloop、seek、停止と再開 | 同じsessionで比較を続けられることが製品目標。境界の扱いは第5.3節の判断による。無言でPOSTを混ぜて達成扱いしない |
| 4秒未満のloop | live推定から一律除外しない。既存native範囲方式の4秒固定AB/Blindは、その4秒全体を再生できる範囲設定が必要 |
| 外部機器、別processのplugin | 自動的には対象にしない。遅延対応、到着、sandbox、権限を含む認定が必要 |
| Reference A/B/C、Version Blind | 音源と再生契約を維持。勝手に4秒化せず、排他と復帰の回帰を確認 |
| 未認定host/format | 新しいlive入口を許可しない。提供範囲は第15節で判断する |

## 3. 現行コードから確認した事実

以下の「remote側」は確認済みの5a6a9db5を指し、ローカルHEADと混同しない。
第4版の83b383a6から増えた日本語UIを含めて確認した。
ファイルの存在、実装、過去の実機証跡はそれぞれ別に扱う。

| 確認事項 | 正本と設計への意味 |
| --- | --- |
| 基点の分岐 | 83b383a6に対して14/43、5a6a9db5に対して14/48。ローカル14commitはremote側へ未統合。B番号とINV-S34以降の意味の衝突をhash基準で照合し、別セッションの統合結果をG0で待つ。統合branchではB番号付け替えとB-1049取り込み後に8d13800eでmerge済み。第8版の読取りはB-1056のc60ab679まで（origin/mainに対して22/0）。main未反映と統合内容未検証を区別する |
| 正本計測と試聴出力の分離 | PluginProcessorAudition.cpp。raw入力を先に捕捉し、置換後の音を正本POSTとして自己計測しない |
| Local Blindは完成した4秒PCMを転送 | Local Blind transport。既存file転送の高頻度反復でlive転送を代用しない |
| 表示用transportと音声transportは別 | analysis_exchange_transport.rs。macOS atomic file、Windows mapping/mutexの表示経路をRT音声リングへ直用しない |
| PRE/POSTは別バイナリ | README。C++ staticやRust OnceLockだけで両role共有とはしない |
| 時刻には異なる意味がある | HostProcessClock.h、時計取得。optional presentation値を任意chainの遅延量と解釈しない |
| 同一将来native範囲の4秒capture | INV-S22、CaptureClockGuard.h。発行後の巻戻し、seek、loopを当該要求の失敗にする。live継続の証明ではない |
| B1の静的PCM一致 | B1観測。固定4096 sample delayでB-757 Windows VST3 48 kHz、B-765 macOS Studio Pro 8.1.2 VST3 96 kHz、B-773同AU 96 kHzが対象。完成したcaptureの残差0 sampleを確認した過去の証跡 |
| 再生位置の移動でplugin内部がresetされ得る | Apple AU guide、JUCEのVST3 setProcessing(false)/AU Reset、PDC Validation Delayのresetを確認。全seekでの発生やPOST未対応区間の長さを保証するものではない |
| local counterは直接比較できない | meter_chain_join.rs（ローカルのみ）はrole-localなframe counterを比較できないと明記し、周回の再結合に両側の新鮮な証拠を求める。counter差一定という契約はない |
| 計測にはexact host allowlistがある | HyphaChainClockPolicy.h（ローカルのみ）。Windows Studio Pro 8.1.2.113407 VST3の例。live認定済みという意味ではない。統合候補c60ab679（B-1052）ではDebug buildに限られ、release buildは認定hostを返さない |
| Blind開始gateとhost実証は別 | PluginProcessorPairing.cpp。formatの入口有効化をhostごとのclock/PDC実証と同一視しない。AAX実機受入待ちも残る |
| Referenceの通常B試聴は停止や準備未完了で選択を解除する | origin/mainのReferenceRuntimeV2Realtime.cppとReferenceRuntimeV2Selection.cpp。停止、位置不明、準備未完了でBの選択とaudition epochを解除してAへ戻し、再開には再選択を要する。live比較の境界規則の既存先例 |
| Reference audioは非RT refillとRTのpage leaseで連続再生する | origin/main B-1038、ReferenceAudioPages.h。process内の連続音声の所有パターン。process間のPRE転送には直接使えないが、refill、lease、close中のI/Oの試験観点を流用できる |
| gain算定とartifact長は別 | reference_gain.rs。2MIXは400 ms窓/100 ms hopの27連続active blockで、標準rateでは3秒から成立。TRACK/STEMはexact 4秒内の3つ以上のpaired active 20 ms窓であり、別々の音楽イベント3個ではない |
| 既存gainと承認 | LocalBlindPreparation.cpp。POST基準と、TP条件に抵触する増幅の代わりに承認付きPOST減衰を用いる |
| PCMとtrial状態の現行所有 | LocalBlindTrial.h。PCMに加え匿名割当、heard、回答、revealを持つ。factoryはcapture vectorsをコピーする |
| 切替遷移 | LocalBlindTransition.hとINV-S25の対称5 ms遷移。weightは現在値から新しい到達先へ進む。Referenceの全曲再生も固定5 ms |
| 排他と退役 | audition_admission_ffi.rs、RtPublicationSlot.h。一つのowner内の段階移行は追加設計が必要 |
| origin/mainのUI不変条件 | INV-S37は100%と125%でFooterを上段2行目へ畳む。INV-S38は100%を見るだけの面にする。INV-S39は900×600を超える大きさを一様拡大で表示する。比較の操作配置はこれらと整合させる |
| editorを閉じると固定音/選択PREが停止 | READMEとLocalBlindTrial.cpp。既適用POST減衰はstop後も保持し、unity Returnとは異なる |
| 非表示時の解析lease | 両基点のINV-S7は全optional Analysis lease解放を要求。E2のcache保持と解析を続けることは別 |
| 現行Blindの画面 | PluginEditorLocalBlind.cppは最低900×600へ拡大し、Blind中の自由リサイズを止める。100% live規則をそのまま適用しない |
| 日本語表示 | remote側INV-S40、HyphaLanguage.h、HyphaJapaneseCatalog.h。案内/状態/通知の英日対訳と小型折返し、匿名性を両言語で試験する |

過去のB1は静的なsample対応の有力な証拠であり、host内部のPDC実装を一意に証明するものではない。
当該commitと条件を超える現在callbackへの到着、loop、seek、latency変更、別processのRT安全性は別途検証する。

## 4. 契約変更の提案

通常A経路の入出力bit identity、追加latency 0 samples、raw PRE/POST測定とRecordを維持する。
明示比較中だけ、認定されたPRE転送と試聴gain、切替遷移を許す案とする。
接続、復元、準備完了、通信復旧ではPREへ自動切替しない。
Audio Threadでallocation、free、blocking lock、I/O、OS待機、無制限retryを行わない。

実装承認後に、R-12、README、不変条件、表示契約、共通安全契約、9/14 Blind計画を同時に整合させる。
INV-S21/S22には連続転送からの遡及固定と対応の証明条件、INV-S24にはliveだけの追従許可、INV-S25には共通ownerと終了条件の変更案が必要になる。
さらに、host起点の境界の扱い（第5.3節）、editor非表示時の扱い（第8節）、100%での戻る操作（第9節）を、選んだ案に応じて安全契約とINV-S38へ反映する。
新しいINVの番号は、統合後のmainの次の空き番号で採る。
次周回PinにはINV-S22のfuture requestを直用できない条件があるため、周回予約の新しい例外と試験を別に定義する。
E2は非可聴cache保持と解析lease解放を両立させる案とし、INV-S7へ追加例外が必要ならG1Rで承認を求める。
INV-S25の匿名隔離と既存Blind画面、remote側INV-S40の英日表示も対象へ含める。
固定stream差を新たな不変条件にはしない。
本書はこれらの正本を変更していない。

## 5. 音声経路と同期

PREのraw入力を事前確保済みの転送へ公開し、POSTは対応済みPREと自分のraw入力を参照する。
正本計測と解析用tapは試聴出力処理より前に置く。
音量workerはrawの対応pairから候補を作り、RTは承認済み候補と準備済みbufferだけを扱う。

### 5.1 転送、所有権、寿命

macOSはPOSIX shared memoryとfile-backed mappingの候補を、Windowsはpagefile-backed named mappingを比較する。
RTからfileやmutexへアクセスする設計にはしない。
mapping作成、権限確認、初回接触、破棄は非RTで行う。
同一processでも別moduleで動くことを確認し、別process対応は実測した構成だけに限定する。
formatが異なるPREとPOST（例えばAU PREとVST3 POST）は、同じprotocolを使えても時刻意味とcallback動作を別に認定する。
host profileにはPRE/POSTそれぞれのformatを入れ、逆方向のpairを同一条件とみなさない。

macOSの確認したXNU実装では名前の上限はNULを除く31 bytesであり、31 Unicode文字ではない。
サイズ変更は一度の割当を前提にし、変更時は新generationのmappingを作る。
App Group風の名前だけで第三者AU hostのsandbox権限が得られるとは扱わない。
実host processのentitlementsとAU形態に対する到達性を試験する。
出荷AUのresourceUsageは`temporary-exception.files.all.read-write`だけである（B-130）。TN2247がresourceUsageに挙げる鍵に、共有memoryの例外はない。
sandboxの中のPOSIX semaphoreと共有memoryの名前は、app group IDを接頭辞にした`GGG/NNN`で、31 bytes以内とされる（Apple DTS）。app groupはprocessのentitlementなので、sandboxのhostではfile-backed mappingが現実的な候補になる。
LogicがApple siliconでAUを別process（AUHostingService）で動かすという報告は未確認であり、G1-06で確かめる。
AAXはPro Toolsのprocessの中で動くが、multi-monoではchannelごとに別instanceになる（第5.4節）。
Windowsは同じuser/session内を基本とし、Global namespaceの特権を前提にしない。
出典は第14節に置く。

headerにはprotocol版、layout長、endian、rate、channels、capacity、完全なpair/session/generation識別子を含める。
短縮hashはlocator用に限り、衝突時もheaderの完全一致で検証する。
別project、再起動後の残骸、異なる版、所有権不明なmappingを受理しない。
B-758では同名C ABIの引数列の不一致がhost process内の異常終了を起こしたため、共有領域のlayoutもC++とRustの双方で固定し、旧版との接続を試験で拒否する。

payloadはbounded SPSCとして所有権を移す。
producerはconsumerの読取り完了が確認できるまで領域を上書きせず、満杯なら待たない。
run descriptorのstart、generation、mapping種別は公開後immutableにする。
進行中の長さは別のatomic published frontierで公開し、close frontierと退役ackも定義する。
release済みの非atomic lengthをproducerが延長し続ける方式は採用しない。
descriptor再利用とpayload再利用はそれぞれconsumer確認を要する。

process間atomicの幅、alignment、lock-free性、address-independent性を対象OS/architectureで確かめる。
同一processのRT退役slotの実績だけでは、この証明を代用できない。
容量が2の冪でない場合も正しいwrapを実装し、残りframesを検査してから読む。
rate、channel、capacity変更では旧mappingを伸縮せず、新generationを非RTで準備する。
unmapや最終所有者の解放はRT退役確認後に非RTで行う。
peer消失時のcleanupは現存readerの安全を優先し、接続復旧だけで比較を再開しない。

live転送は、固定ABとBlindの段階でも非可聴のまま保持する候補とする。
保持できても同期が新鮮なままとは限らず、復帰時の再検証が必要である。
保持の資源と背景consumerを第11節のledgerに計上し、予算超過やcache故障では退役させ、戻る際の再準備を示す。
背景経路と固定artifactの失効範囲は第8節で分ける。

### 5.2 同期を三つの独立した問いに分ける

| 問い | 必要な証拠 |
| --- | --- |
| どのPRE sampleがPOST入力のどのsampleに対応するか | 識別PCMと既知delayによるexactな対応。native位置一致は候補であり、全host共通の保証ではない |
| 必要なPRE sampleが出力deadline前に到着するか | 実callback trace、範囲coverage、可変block、負荷、分離processでの確認 |
| loop、seek、PDC変更をまたいで対応が維持されるか | 周回識別、reset/flush、境界block、通知タイミングを含む独立試験 |

直列chainでもPRE256 framesの公開後にPOST64 framesを4回処理すれば、公開末尾と消費counterとの差は256、192、128、64になる。
すべてのPOST要求範囲が存在する場合でも差は一定でない。
したがってlocal counter差一定を必要条件にも十分条件にも使わない。
counterは各sideの進行、coverage、drop診断に使う。
stream offsetは対応が証明されたepoch内の記述に使えても、別周回の同じproject位置を許可する根拠にはならない。

host認定profileはDAW build、OS build、format、architecture、rate/block条件、PDC設定、process分離とrouting条件を記録する。
同一native写像を採用できるprofileから検証を始める。
Metric ABの変更履歴は、Logic Proでbus trackを有効にした場合などにhostが正しいPDC情報を渡さないとして手動補正を設けている。
Logic Proはrouting条件別にprofileを分け、同じDAWでも一括で認定しない。
相関（中身のずれの推定）は、非出荷のdiagnosticでの残差の確認と、製品での警告（下記）にだけ使い、任意のEQ、reverb、意図的delayを実行中に「誤差」として自動補正しない。
異なる時刻写像や追加出力delayが必要なら、0 sample境界への影響と製品範囲を再提案する。

VST3ではloop境界でhostがblockを分割する義務はない。
JUCEのPPQ loop情報だけからexactな境界sampleを捏造しない。
境界の位置不一致を固定offsetだけで免除せず、各sampleの対応を証明できるhost条件に限り継続する。
Studio Pro 8.1.2（macOS VST3、48 kHz）の実測では、遅延4096を挟んだPOSTのproject時刻が、loopの折返しのたびに4096 samplesの間loop先頭に留まった。
その間の内容は折返し前の末尾であり、project時刻でPREを引くと誤対応または欠落になる。
同じ実測で、VST3連続時刻は同じ周回の同じ内容にPREとPOSTで同じ値を持ち、loopの折返しで途切れなかった。
AUのrender時刻はinstanceごとに起点が異なるが、同じ内容でのPREとPOSTの差Kは一定で、loop、seek、再生開始のどれでも途切れなかった。
「連続時計 + K」を対応の鍵にし、Kを定常区間のproject時刻の一致から較正して照合し続ける案は、このhostでは折返しでの解除を避けられる見込みがある。
Kが変わったら（動的PDC、instanceの再生成など）対応を切る。
連続時刻は任意の情報であり、hostごとの実測で認定したprofileだけに使う。鍵の設計基準としての採用は2026-09-28に決定した。
同じhostのプローブ（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第6節）では、VST3連続時刻がseekと再生開始でproject位置へ戻り、同じ値が別のrunで再利用された。
鍵にはPREのrun世代を含め、PREの現在のrunで書かれた範囲だけを受け入れる。時計値の一致とringへの到着だけでは受け入れない。
第11版の実測では、この周回の規則をplugin内で動かし、VST3では誤受入れ0だった。
AUのrender時刻は、そのinstanceが呼ばれた分だけ進むので、hostのplugin sleepでKが変わる。POSTだけが眠ると、古いKのまま過去の音を指し、周回の規則でも誤って受け入れた。
そのため各側は、自分の呼出しの空白（壁時計）も時計の不連続として扱う。PREは周回を改め、POSTはKを無効にして、定常区間の連続一致で較正し直す。
Kの照合は定常区間の連続一致で行う。再配置の直後に出る単発の食い違う候補（古いPREの記録との結び付き）で対応を切らない。
周回の規則は、seek直後の古い音にhostが新しい周回の外の番号を付けることを前提にするので、host認定では中身の照合で確かめる。
seek時にchainのdelay bufferがflushされる条件も別に扱う。

動的PDCについて「最初の影響blockから誤対応0」を現時点で保証しない。
第12版の実測（Studio Pro 8.1.2、遅延4096 ↔ 0、2048 frames）では、音の変化が時計の変化より2〜4 block（85〜171 ms）先に来た。
呼出しの空白も出ず、この間は周回、呼出しの空白、Kのどの規則でも誤対応を検出できなかった。
VST3は2〜3 block後にPOSTの時計が補正されて自然に戻り、AUはKの較正し直しが済むまで誤対応が続いた（M1で3〜4 blockまで縮む）。
音声遅延が変わっても、Hyphaに見える時計とcounterが変わらなければ、それだけで変更を識別できない。
最初の検出可能な不整合でPREを失効させることと、変更前に必ず検出できることを区別する。
後者を要求するprofileには、影響音声より前のbarrier/generation通知または同等の実証が必要になる。
観測不能な変更が残る場合は出荷判断の障害とし、「自動検出済み」としない。

その障害を小さくする候補として停止専用の相関監視を挙げていたが、2026-09-28のG1Rで採らないと決めた。
相関はworkerで補正前のPREとPOSTを比べる方式で、意図的なdelay、reverb、無音、周期信号、強い非線形処理で誤停止や判定不能が起き、検出までの間にずれた音が出ることも避けられない。
代わりに、変更直後の2〜4 block（このhostで最大171 ms）の誤対応を許容して説明書に記し、AUにはM1を加えて区間を縮める。
遅延変更を伴う構成での比較停止は、事前に判別できないので採らない。
この許容は、時計で検出できない区間が実測で有限（2〜4 block）だったhostに限る。他のhostでは、G1-04の実測で区間の長さを確かめてから同じ扱いにする。
許容は、pluginが遅延の変更を報告し、hostが再生中に補償を改める場合に限る。
報告しない変更と、再生中に補償を改めないhostでは、ずれは自然には終わらない。Avidの公開ガイドはPro Toolsを後者と書くが、Pro Tools 2026.4の実測では再生中に反映された（第5.4節）。

**遅延の報告への依存（2026-09-28の見直し）**
対応の鍵は、PREとPOSTの間のpluginが遅延を正しく報告し、hostがそれで補償することを前提にする。
遅延を報告しない、または誤って報告するpluginが間にあると、時計の対応は正しく見えたまま、PREが誤差の分ずれ続ける。
どの時計の規則でも検出できない。上のMetric ABの例のように、hostの側で補償が崩れる条件もある。
そこで、比較の開始時と、その後は定期的に、非RTのworkerでPREとPOSTの中身のずれを推定し、時計の対応と食い違えば警告する（契約改定案のINV-LC7）。
比較は止めず、Kも補正しない。推定できないときは判定不能とし、「一致」も表示しない。
意図したdelay、reverb、強い加工でも食い違いは出る。警告は、推定したずれと考えられる原因を事実として示し、価値判断を出さない（R-22）。
補償を改めないhostでは、再生を止めて再開すると補償が改まることも示す。
比較の途中でずれが跳んだ場合は、警告に加えてPOSTへ倒す（第5.4節。2026-09-28、利用者が推奨を採用）。
説明書には、DAWの遅延補償に依存することを記す。
この警告は、以前の停止専用の相関監視と違い、比較を止めない。誤警告、見逃し、判定不能の率は、G1-05の構成で測る（第12.1節）。
利用者が判断を委任し、推奨（案a）を採用した。

**周回単位の取り違え**
Kの較正は、project時刻の一致が、POSTが聞いている周回のPREの記録と結び付くことを前提にする。
chainの遅延がloop長以上だと、新しい周回のPREの記録と結び付き、周回単位でずれたKを採り得る（判定コードの作りからの推論。未試験）。
G1の照合に使った識別PCMは、位置ごとに一意だが周回ごとには同じなので、この誤りを検出できない（G1記録第8.6節）。
G1-03で、周回ごとに印が変わるfixtureを使って試し、前提を確かめる手段と、確かめられない場合の扱い（PREを出さない、または警告）を決める。

**定数はhostごとの値**
空白の判定（直前block長の2.5倍かつ20 ms超）と較正の連続一致（8回）は、Studio Pro 8.1.2、2048 framesで試しただけの初期値である。
blockの長さが変わるhostや、呼出しが不規則になり得るhost（LogicのProcess Buffer Range、REAPERのanticipative FX。影響は未測定）では、空白を誤って検出し、PREが頻繁に途切れるおそれがある。
host profileの値として扱い、hostとbuffer設定ごとに実測で決める。

### 5.3 境界、故障、解析不足を分ける

音声出力の安全判断と、原因の分類確定を分ける。
時刻対応の側のPRE継続条件は、POSTが今のblockで必要とする各sampleに、現在も有効な対応証拠があることである。
別途、owner、承認gain、RT guard、format等の出力条件も満たさなければならない。
必要な対応が失われた時点でPREを止めるが、正常な境界通知が片側に先着しただけでPREを止めたり、恒久的な故障と決めたりしない。

callbackの実行/公開順、host時計やtransportの境界通知、PCM内容の境界到着は別の事実である。
直列chainでも、この三つの差が同じ長さになるとは限らず、境界通知差をchain latencyと同一視しない。
第6版の「最大3秒早く切れる」は測定済みの上限ではないため、採用根拠にしない。
3秒は第11節の音声保持候補であり、通知待ちの上限は別に測る。

認定profileが境界後も旧runの対応の継続を証明し、reset/flush、clock/pair/generation変更などの失効条件がない場合に限り、必要な旧範囲のPREを継続できる。
旧PCMがringに存在することは必要条件の一つであり、現在のPOST入力との対応が有効であることの証明ではない。
profileにその継続証拠がない場合は保守的にPOSTを鳴らし（PREの選択は保つ。C）、原因分類は期限内で続ける。
全sampleの対応を新runへ途切れず引き継げると実証された場合には、中断せず継続できる。

| 区分 | 判定と出力 | 次の状態 |
| --- | --- | --- |
| 境界確認待ち | 片側先着後も旧証拠が有効だとprofileが示し、POSTの必要範囲がその対応に含まれる間だけPREを続ける。証拠が失効した最初のsampleからPOSTへ移す。POSTだけが境界を示して対応証拠がない場合もPOSTへ移す。範囲と理由をreceiptへ記録 | 有限の期限内に両側の新鮮な証拠を集める。証明の無いPRE継続はしない。証明が戻れば、Cにより最初のblockからPREへ戻す（対称5 ms）。期限を超えたら説明できない欠落として扱う |
| host起点の境界 | 両側の対応する遷移、run、位置、順序を認定profileの規則で確認 | 対応が途切れた区間はPOSTを鳴らし、PREの選択と承認済みgainを保持する（C）。証明が戻った最初のblockからPREへ戻す。全sampleの有効な対応が継続する場合はPREのまま |
| 説明できない欠落 | 連続再生中のcoverage欠損、期限後も片側だけの境界、protocol/pair/format失効 | 比較中断をlatch。失効した証拠に応じ再準備を求める |
| 解析だけの不足 | 音声coverageとRT guardは正常でworker queueだけが遅延/欠測 | 追従更新とPinを停止。独立性を実証した構成では承認済み固定gainの試聴を維持 |

境界確認待ちの最大時間、最大frames、queue容量はprofileごとのG1で測り、有限値として固定する。
通知と音声の先行により複数の未処理境界が生じる条件は、順序付きepochとboundedな履歴で扱う。
最新の一件だけへ上書きせず、対応できない容量超過は中断する。
同じ順序で位置が下がったという情報だけでは、loopかseekか、どの周回か、同じcontentかを証明できない。
無期限待機、同じproject位置の古い証拠の再利用、固定Δによる対応の補完をしない。
上限超過は理由付きの中断とし、復旧だけではPREを再開しない。

境界直後に対応未証明の区間が生じる可能性はあるが、必ずchain遅延と同じ長さになるとは限らない。
例えば純粋なdelay lineをzero-clearし、hostがprerollを行わない構成では、再生開始後に有効な遅延出力が揃うまでの区間が生じ得る。
pluginのreset実装、hostのPDC scheduling、preroll、tailによって異なるため、存在と長さはprofile別に実測する。
AU Resetの説明から、全seek/loopでresetされることやPOSTが必ず無音になることを導かない。

第4版の決定であるA′は、2026-09-28のG1Rで案Cに置き換えた。
第9版の実測では、Studio Pro 8.1.2で遅延のあるchainのPOSTは、project時刻だけで対応を取ると折返しのたびに証拠を失う。
この方式のままでは、A′でloopのたびに再選択が必要になる。
VST3連続時刻を鍵にすれば折返しでの解除を避けられる見込みがあるが、seekと再生開始ではchain遅延の長さだけ現在のrunに対応するPREが無いので、A′の再選択は残る。
この区間が指すringの格納位置には旧runの音が残るため、run世代で棄却する。
AUのrender時刻では、このhostのseekと再生開始をまたいで全sampleの対応が続いたので、認定後は解除しない境界として扱える候補である。ただしplugin sleepの後は、Kの較正し直しが要る。
Studio Proは、入力が約4秒無音のPREの呼出しを再生中でも止める。静かな区間ではPREの比較音がなく、POSTへ倒れる。
音が戻った後は、Cにより対応の証明が戻り次第PREへ自動で戻す（2026-09-28決定）。加えて、infinite tailの報告でsleep自体を避ける案を本命とし、bounceの末尾、AU、他のhostでの副作用を確かめてから採用する。

| 案 | 境界直後 | PREの再開 | 状態 |
| --- | --- | --- | --- |
| A 一律中断 | POSTへ戻す | 毎回再準備 | 比較案 |
| A′ 中断と1操作の再選択 | PRE選択を解除し、必要なPOST減衰は保持 | pair/format/context/承認条件と新しいcoverageが有効なら、ready後に1操作で再選択 | 第4版で採用。2026-09-28にCへ置き換え |
| B 境界区間だけPOST | 境界区間をactual receiptへ記録 | 証明後に同じ選択のままPREへ復帰 | Cに含める |
| C すべての境界でBを適用 | seek、再生開始、loop、sleep、遅延変更のどれでも、証明できない区間はPOSTを鳴らし、PREの選択と承認済みgainは保持。区間をreceiptへ記録 | 対応が証明できた最初のblockからPREへ戻す。利用者の操作は要らない | 採用（2026-09-28）。説明できない欠落、protocol/pair/format失効、上限超過には適用せず、従来どおり中断と再選択 |

境界があっても全sampleの対応を維持できるprofileでは、PREを中断する必要はない。
Cでは、未証明区間の間はPOSTが鳴り、証明が戻ればPREへ戻る。利用者の操作は要らない。
証明が戻るまでPOSTを鳴らす仕組みは、第12版までの実測でplugin内で機能した（誤受入れ0）。これが、再選択を求める安全上の理由をなくした根拠である。
この「誤受入れ0」は、識別PCMで見分けられる誤りに限り、遅延の報告が正しいchainでの結果である（第5.2節）。
時計の規則で検出できない誤り（遅延の報告の誤り、周回単位の取り違え）は、A′の再選択でも防げない。Cを選んでも、その危険は増えない。

Cで自動で戻るときは、既存の対称5 ms遷移（LocalBlindTransition.h、INV-S25）を使い、確かめたPREのsampleだけを遷移に使う。
確かめられなくなったときは、そのblockの先頭でPOSTへ切り替える。確かめていないPREを遷移に使わない。
この切替は、seek、loop、sleep、遅延変更の境界で起こり、音そのものが不連続になる場面と重なる。
PREを選んだままPOSTが鳴っている間は、画面に「PRE待ち」を示す。区間が短いとちらつくので、最短の表示時間をG4で決める。
既存のReference試聴は、停止、位置不明、準備未完了でBの選択を解除し、再選択を求める（第3節）。Cはこれと逆の振る舞いである。
live比較のPOSTは同じ曲の同じ位置の音であり、境界ごとに時計の規則で確かめ直せるので、選択を保つ。Referenceの規則は変えず、画面とREADMEで違いを示す。
説明できない欠落、protocol/pair/format失効、上限超過は従来どおり比較中断とし、再選択を要する。
「1操作」はready時の再開操作数であり、境界の証明待ちをゼロにする保証ではない。
固定gainの保持と、現在の音も同じラウドネスであることは別であり、測定条件が変われば残差や再MATCHの必要を示す。

故障を短いPOST代替で隠し、次blockで自動PRE復帰させない（Cの自動復帰は境界だけに適用する）。
block途中までPREを出した事実があればtransition/中断範囲をreceiptへ残し、純粋なPRE完走と数えない。
解析worker復旧後も古い候補を使わず、再解析と追従再開の明示確認を要する。

### 5.4 AAX（Pro Tools）

AAXは、VST3、AUと並ぶ重要な対象である（2026-09-28、利用者の指示）。
2026-09-28にPro Tools Developer 2026.4（Intel Mac、48 kHz）で実測した（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第9節）。下表の「設計への意味」には、その結果を含める。
出典は、Avidが公開するAAX SDKの[Pro Tools Guide](https://learn-cdn.avid.com/AAX_SDK_2p1p1/Documentation/Doxygen/output/html/a00274.html)と、手元のAAX SDK 2.9のheaderの説明である（第14節）。

| 点 | AAXの仕様 | 設計への意味 |
| --- | --- | --- |
| 連続時計 | `AAX_ITransport::GetCurrentNativeSampleLocation`は、再生中だけ、callbackのbuffer先頭のtimeline位置を返す。`AAX_IController::GetTODLocation`は、再生開始からplayheadが進んだsample数を返し、audio engineの実時間の処理の中で増える。現行のJUCE patch（0008）は、前者をAAXの補助時計として渡している | 前者はproject時刻にあたり、loopで折り返す。後者は連続時計の候補だが、engine全体の値なので、同じcycleのPREとPOSTで同じ値になる見込み（未測定）。pluginが数えるframe数（AUのrender時刻と同じ性質）を連続時計にすると、実測で成り立った |
| 遅延補償された位置 | POSTに渡る位置が、PREとPOSTの間のpluginの遅延の分だけ補償されるかは書かれていない。Studio ProのVST3とAUでは補償されていた（G1記録第2節） | 実測では補償されていた。下のClock Diagnosticの位置は、再生開始で上より4096小さく（−4353と−257）、中身と位置の差は照合できた全blockで0だった。loopの折返しでも、POSTの位置は中身と一致したまま折り返した。他の版やplatformで補償されない場合の選択肢（中身による較正を認めるか、提供しないか）は、利用者の判断を求める |
| 再生中の遅延変更 | 公開ガイド（2.1.1版）は、Pro Toolsは再生中に遅延補償の設定を更新しないと書く。遅延を動的に変えるpluginは、再生中の変更を避けるか、ずれを利用者に示すべきとされる。`SetSignalLatency`の即時の適用も保証されない | Pro Tools 2026.4の実測では、再生中に反映された。4096→0では1 blockだけ位置が古い補償のままで、0→4096では同じblockで改まった。frame数のKが遅延の差だけ変わるので、M1で誤対応は1〜2 block。補償を改めない版やhostでは、中身の跳び（INV-LC10）で扱う |
| 遅延補償のOFF | Pro Tools 12.6以降は、遅延補償の全体の有効・無効をpluginに通知する（`AAX_eNotificationEvent_DelayCompensationState`）。JUCEはこの通知を扱っていない | OFFの間は対応の前提が成り立たないと分かる。POSTを出し、理由を示す案（契約改定案のINV-LC8） |
| Dynamic Plug-In Processing | Pro Tools 11以降は、一定時間無音のtrackや停止中のpluginを止める。止めさせない方法は、descriptorの`AAX_eProperty_Constraint_AlwaysProcess`（JUCEの`JucePlugin_AAXDisableDynamicProcessing`）で、そのpluginのchain全体を処理させ続ける。SDKは、実際に支障があるときだけ使うよう求める | 実測では、入力が無音になって約8秒後に、再生中も停止中も呼出しが止まり、空白の規則で誤対応はなかった。AlwaysProcessを付けると、停止中も含めてchain全体が呼ばれ続けた。属性は静的なので、「比較中だけ」は選べない |
| loop | loopの終わりから始めへ、pluginの状態をresetせずに続けて処理する | loopでの位置とTODの振る舞いはG1-03で測る |
| offline bounce | 実時間より速く呼ぶ。wall-clockに依存する処理を避けるよう求める | 呼出しの空白の規則はofflineでは使えない。offlineではA経路を保つ（R-12） |
| multi-mono | channelごとに別instanceを作る | PREとPOSTの組、転送、Kがchannelごとになる。channelごとに切り替えると、channelの間でPREとPOSTが混ざる。全channelを同じblockで切り替えて提供する（2026-09-28、利用者が推奨を採用。契約改定案のINV-LC9） |

**品質目標（2026-09-28、利用者の指示）**
「AAXではプロが使うので、出来る限り完璧に近いプラグインにする必要がある。他のプラグインよりも精度を高く保つ」。
これを受け、AAXのlive比較は次の目標をすべて満たすことを出荷の条件にする。
満たせない条件が見つかったら、提供範囲を黙って縮めず、事実と選択肢を示して利用者の判断を求める。

| 項目 | 他製品（公開資料。外部調査第3節、第5.1節） | HyphaのAAXでの目標 |
| --- | --- | --- |
| 時刻の対応 | 自動検出か手入力。Perception ABは遅延変更の後に再Syncを案内し、Metric ABはPDC Modeで手動補正する。GainMatchとABLM2も自動検出と手入力 | hostの時計で毎block確かめ、定常で0 sample。確かめられない区間はPOSTを出し、ずれたPREを出さない |
| 遅延が変わった後 | 利用者が再操作する | 自動で確かめ直して戻る。時計の変化はM1で直ちに検出してPOSTへ倒し（Pro Tools 2026.4で1〜2 block）、時計が変わらない場合は中身の跳びで倒す（下記） |
| 遅延補償のOFF | 記載なし | Pro Toolsの通知を読み、OFFの間はPOSTを出して理由を示す（契約改定案のINV-LC8） |
| multi-mono | 記載なし | channelの間でPREとPOSTを混ぜない。全channelを同じblockで切り替える（下記） |
| 音量一致 | GainMatchはAUTOの許容差±1 dB（公式manual）。Perception ABは毎秒のAuto Match | loudnessの測定は参照実装との差0.1 LU以内、適用するgainの分解能は0.01 dB以内。追従の許容差は聴取で決め、値を画面に出す（第6.2節） |
| 切替 | GainMatchは短いfade | 対称5 ms。確かめたPREのsampleだけを使う |
| 検証 | 公開なし | Pro Toolsの版、macOSとWindows、rate（44.1〜192 kHz）、Pro Toolsで選べるbufferの最小・最大・代表値、stereoとmulti-mono、Dynamic Plug-In Processing、遅延補償のOFFの各条件で、周回ごとに印が変わるfixtureによるbit一致を確かめて認定する |

**時計が変わらない遅延変更（2026-09-28、利用者が推奨を採用）**
hostが再生中に補償を改めない場合、間のpluginの遅延が変わると、時計は変わらないまま中身だけがずれる。
Avidの公開ガイドはPro Toolsをこの型と書いていたので、推奨はこれを前提に作った。同日の実測では、Pro Tools 2026.4は再生中に反映し、この型ではなかった。
補償を改めない版やhost、報告せずに遅延を変えるpluginでは、第5.2節の警告だけだと、利用者は再生を止めるまでずれたPREを聴き続ける。
そこで、比較の開始時に中身のずれの基準値を記録し、比較の途中でずれが跳んだら（時計は変わらない）、補償されていない遅延変更の境界として扱う。POSTを出し、PREの選択は保つ。
再生の停止と再開の後に、時計と中身で確かめ直してPREへ戻す。
絶対値の食い違い（意図したdelayなどで初めからあるずれ）は、従来どおり警告だけにする。跳びは比較の途中で遅延が変わったことを示すので、初めからある意図したdelayとは区別できる。
比較の途中で意図したdelayの時間を変えた場合も跳びになり、区別できないので、安全側としてPOSTへ倒す。
2026-09-28の判断（止めずに警告）を、この場合に限って強めた。同日、利用者が推奨を採用した（契約改定案のINV-LC10）。

**multi-monoの切替**
channelの間でPREとPOSTを混ぜないことは必須とする。
推奨は、POSTのchannelのinstanceのうち、あるblockを最初に判定したinstanceが全channelの判定を決めて事前確保の領域へ公開し、他のinstanceはそれに従う方式である。
全channelで対応を確かめられたblockだけPREを出す。判定の順序、事前確保、RTで待たないことは、G1-06とLC-33で確かめる。
multi-monoでもlive比較を提供する（2026-09-28、利用者が推奨を採用。契約改定案のINV-LC9）。

AAXのG1は、Pro Tools Developer 2026.4（macOS、Intel）でG1-01、G1-03、G1-04、Dynamic Plug-In Processingを行った（G1記録第9節）。
署名の要らない開発版を使い、PACEでの署名は行っていない。
残りは、WindowsのPro Tools、製品版、Apple silicon、multi-mono、他のbufferとrate、遅延補償のOFF、`GetTODLocation`、hostの通知を待ってから音の遅延を変える型の遅延変更である。
AAXのプローブは、Pro Toolsが読み込める署名が要る。製品と別identityの非出荷fixtureとし、署名は既存のAAXの経路（`docs/aax_macos_universal_build_20260910.md`、`docs/aax_windows_build_20260910.md`）に沿う。PACEでの署名は、行う前に利用者の確認を得る。

## 6. Gain Matchの方式

### 6.1 基準と承認を共通にする

2MIXは対応するactive blockのLU差、TRACK/STEMはpaired active windowのenergy差から、PREへ加える差分dを求める。
dはPOST−PREの符号とする。
既存のpolicy名と丸めを維持する。
現行TRACK/STEMのdeltaは絶対値24 dB以内だが、2MIXのdeltaはlevel用の判定を通るため−100 dB超から+24 dBまでを受理する。
新しいlive適用gateを±24 dBに統一することは候補であり、既存動作を確認済みとして読み替えない。
Blind側も統一する場合は互換性とpolicy versionへの影響を示して承認を求め、両符号の境界値を試験する。

| 基準 | PRE gain dB | POST gain dB | 扱い |
| --- | --- | --- | --- |
| POST基準 | d | 0 | 既定候補。POSTのmix内音量を維持 |
| 明示承認付き減衰 | min(d, 0) | min(−d, 0) | 両側を小さい側へ合わせる式。d>0ではPREを原音量に保ちPOSTを下げる |

既存Blindでは、POST基準の正のPRE補正が観測TP基準を超える場合に減衰の承認を求める。
これをliveにも統合し、「Quietest」という独立エンジンを重ねない。
超過前から減衰基準を任意選択できるようにするかは追加の製品判断とする。
POST減衰は残りのmixとの相対音量を変えるので、適用量と通常へ戻る音量差を提示する。
この式は入力が既にclipしている場合や下流の非線形処理まで安全にするものではない。

### 6.2 live窓と更新方法

2MIXの27連続active blockは400 ms窓、100 ms hopなので、標準rateの最短範囲は0.4+26×0.1=3.0秒となる。
TRACK/STEMの現行policyはexact 4秒の中で3つ以上のpaired active 20 ms窓を要求する。
固定AB/Blindのartifact長4秒と、算定器の最短量を混同しない。
live履歴は両policyを扱う4秒を候補とし、対応保証と窓の連続性を含むversionを別に定義する。

loopで同じproject位置を通ることだけを理由に、liveの実際の再生音を一律deduplicateしない。
繰返しは聴いている信号の重み付けであり、独立した試行数や音質改善の証拠ではない。
一方、同じbufferを都合よく複製して不足を埋めたり、対応不明なseek前後を接合したりしない。
認定済みのloopを時間順に展開した4秒と、連続4秒のgolden PCMを比較し、短loop、疎な打音、非定常処理の偏りを検証する。
対応の途切れた窓は無効にし、実際の入力量、採用量、候補の古さを保持する。

固定補正は明示MATCH時に承認した値を保持する。
追従補正は同じ基準のまま、最新の有効pairから非RTで候補を作る。
候補にはpair/run、policy、測定範囲、gain revision、承認epoch、期限を付け、RTで一致したものだけを受け取る。
欠測や無音ではgainを増やさず、古い候補を「一致」と表示しない。

更新1秒、残差許容0.5 dB、直近の明示承認gainから±6 dB、50 ms rampは実験用の初期案に留める。
±6 dBは前回更新からのstep幅ではなく承認点からの累積変位と定義し、少しずつ無制限に動かさない。
これらはHyphaの設計候補であり、Perception ABの曖昧な「gain difference」の定義を確認済みとして輸入しない。
範囲外や基準変更では追従を止め、理由と新候補を示す。
Blindへ追従を持ち込まない。
pumping、transientの変形、無音復帰、密度変化の聴取結果から値を決める。

精度の目標（第5.4節の品質目標）は、追従の許容差とは分けて扱う。
loudnessの測定は参照実装との差0.1 LU以内（計測の合格基準と同じ桁）、適用するgainの分解能は0.01 dB以内とする。
追従の許容差は、聴取で決めた値をそのまま画面に出し、「一致」を許容差の中の意味でだけ使う。
公開資料の他製品（GainMatchのAUTOの許容差±1 dB）より粗い値にはしない。

### 6.3 観測TP基準、RT guard、終了

既存の固定artifactの観測TP基準はC=max(−1 dBTP, raw POST TP, raw PRE TP)である。
これは−1 dBTPと観測raw最大の大きい方を超える補正を避ける判定であり、絶対−1 dBTPのlimiterではない。
raw自体が0 dBTPを超えていればCも超える。
liveへ適用するCはMATCH時の観測範囲と承認revisionに結び付け、追従によって無言で上方更新しない。
再承認時に変える場合は変更を示す。

非RTのtrue-peak測定と、当該RT blockのsample値によるguardを分ける。
RT guardの閾値は、承認revisionに結び付いたCからlinear値 `10^(C/20)` を一度計算して保持し、補正後sampleの絶対値と比較する。
CのdBTP数値とlinear PCM値を直接比較しない。
RTでblock全体を出力前に検査し、nonfiniteまたは増幅後のsample peakがCを超えた場合は、当該blockへPRE増幅を適用せず中断する。
sample点の検査ではinter-sample peakを取り逃すため、このguardだけでtrue-peak上限を保証しない。
workerのtrue-peak測定がCを超えた観測は記録し、次の承認時に示す。
遅れて到着するworkerのTP結果も、既に出した音の安全証明にはならない。
新しいlimiterを無断追加してclip-freeを保証する案にはしない。

通常POSTへ戻る際、直前の試聴より音量が大きくなる場合がある。
故障時のPOSTも元から大きい可能性があり、「POSTへ戻せば耳を絶対に保護できる」とは表現しない。
自動故障復帰では、既に承認・実適用されているPOST減衰だけを保持する案とし、PREの無効PCMをfadeに使わない。
新しいPOST減衰を自動生成する保護は本案の既定に含めず、必要なら事前承認の範囲、上下限、失敗時処理を別途提案する。
減衰が残る状態は通常A経路と区別し、終了要求後に急な増大を避ける遷移と、unity復帰の明示確認を設ける。
offlineまたはhost bypassがcallbackへ通知されたbufferでは、gain/fadeを含め試聴処理を適用せず入力不変を最優先する。
一時的な非適用と承認済み減衰の状態消去は別にし、解除だけでPREや追従を再開しない。
hostがbypass中にprocess自体を止める場合は音声を書き換えず、既存heartbeatの観測遅れと再開時の失効処理を区別して試験する。
正常Return、故障復帰、停止中、offline、host bypass、processor破棄の優先順位と適用gainをnative試験で固定する。
この復帰policyの確定は実装gateであり、単なる文言で解決したことにしない。

## 7. session、固定AB、Blind

### 7.1 状態と実出力receipt

| 状態 | 音と操作 |
| --- | --- |
| 通常 | raw POSTを変更しない。試聴ownerなし |
| 準備中 | owner取得と準備を実行。raw POSTを維持。取消可能 |
| live POST / live PRE | 承認済みgainとsourceを適用。固定補正/追従補正を区別 |
| 境界確認待ち/処理中 | 証明済みの範囲ではPREを鳴らし、範囲を出た時点でPOSTへ切り替える。PREの選択は保持し、対応が再び証明された最初のblockからPREへ戻す（C）。片側先着の原因判定待ちと、復帰の証明待ちを区別する |
| 解析保留 | 音声条件が正常なら固定gainを保持。追従更新とPinは停止 |
| 比較中断 | PRE選択を失効。POSTへの復帰状態を表示し、通信回復だけでPREを再開しない |
| 区間準備/再生待ち | 対象範囲、loop内で巻戻しなしに再生できるか、DAW再生手順を示す。準備完了だけで固定音へ切替しない |
| 固定AB | 不変PCMと固定gainを記名して聴く。背景liveの保持は予算と独立性の条件付き |
| Blind | 同じartifactを匿名の新trialで聴く。両source完走後に回答可能 |
| 終了処理/減衰保持 | 現在の実gainを示す。必要なownerとRecord排他を保持 |
| 通常へ復帰済み | unity POSTのRT receiptを確認してownerと資源を解放 |

receiptは要求sourceと実出力sourceを分ける。
session、command sequence、pair/run、gain revision、実gain、transition、fallback範囲、境界区間、中断理由を含める。
UI threadの要求を「適用済み」と表示しない。
GUI更新周期の遅れは残るため、RTと同時刻に画面が変わるとは保証せず、次のreceiptで確実に追従する。
不足を含むblockをPREの正常聴取や完走として数えない。
停止中にcallbackが来ない場合も「音声で復帰確認済み」と捏造せず、次callbackでの通常出力を予約して区別する。

### 7.2 遡及Pinと次周回予約を分ける

Pin要求にはsessionとcommand sequenceを付け、RT受理時のstream終端、project範囲、run/generation、実適用gainとtransition状態をreceiptで返す。
RT受理時に後続の追従gain更新を凍結する。
取消や失敗だけで追従を再開せず、再開は明示操作にする。
対象は受理終端までのexact4秒とし、workerがそのwatermarkへ追いつくまで待つ。
workerの古い末尾へ対象を置き換えない。
queue、seal領域、timeoutは容量に計上する。
RT sealはcutoff、epoch、必要なsegmentの寿命を確定する操作であり、4秒全体を終端callbackで一括コピーする意味ではない。
未来予約では、事前確保したsnapshot領域へ各callbackの必要分だけを書き込み、終端でimmutableな完了記録を公開する方式を第一候補に含める。
遡及Pinでは、事前確保したsegmentのlease/切替等でcutoffまでのデータを上書きから守り、非RTで必要な組立てを行う。
いずれもdescriptor操作数に上限を置き、copyはcallback framesに比例させ、RTで待機、全履歴の複製、最終freeを行わない。
cutoffがblock途中にある場合は、その後半を書き込む前に必要な先頭を保護する。
workerが0.5秒遅れた場合も保護を維持し、領域やcreditが足りなければ明示失敗とする。
履歴を8秒にしないことと、seal/stagingの追加領域が不要なことは同義ではない。

一つの再生可能なnative範囲、PRE/POST全sample対応、完全なraw PCMを必要条件にする。
loop/seek/pair切替をまたぐ履歴を、単一native範囲artifactへ無言で押し込まない。
受理位相をloop内で一様と仮定すると、直前4秒が折返しをまたぐ位相の割合はmin(1, 4/T)である。
8秒loopでは1/2、16秒では1/4となるが、これは幾何学的割合であって利用者の実測失敗率ではない。

取得できた場合も、再生可否を別に判定する。
範囲先頭sが、現在のrunの最初のcallback開始位置qに対して s < q + B − 1 の場合（q、Bは後述の余白付き窓と同じ定義）、loopの次周回では範囲先頭を含むcallbackが来ないことがある。
その場合はPin時点で待機し得ることを示し、DAWで範囲より前から再生する操作か、余白付き窓での次周回予約を選べるようにする。
一様位相の仮定では、この条件に当たる割合はおよそ(B − 1)を周回のsample数で割った値であり（8秒loop、48 kHz、B=512で約0.13%）小さいが、待機を成功扱いしない。

失敗時の代替として、次周回の予約と次の4秒取得を明示選択にする決定は維持する。
「観測したloop範囲」とはloop全体を4秒artifactへ入れる意味ではなく、loop内の一つのexact4秒範囲を指す。
候補のnative start/end、長さ4秒、取得対象の周回、待ち状態を表示し、利用者が選んだ範囲を無言で変更しない。
既定の候補は後述の余白付き窓の中に置き、窓の外を選んだ場合は再生可否の条件を示す。
loopが4秒より短い場合は、paddingや複数周回の連結をせず、この代替は成立しないと示す。

既存のfuture captureはPOST観測位置の1秒後から4秒を取得する。
発行後の不連続とlate armを拒否し、非RTのPRE arm/ackを経てPOSTをarmする。
したがって「次の折返しを観測してから既存APIを呼べば条件を満たす」とは言えない。
4秒loopの先頭で呼んでも、取得区間[1秒, 5秒)は次の折返しをまたぐ。
8秒/16秒のloop全体を指定する案も、既存要求の最大4秒を超える。

次周回予約の実現方式は以下をG1で比較し、採用意図と実証済みの方式を分ける。

| 方式 | 成立条件 | 現行との差 |
| --- | --- | --- |
| live経路上で範囲を予約し終端でRT seal | 予約時点から対象周回とexact4秒を識別でき、両側PCMを完全に保持できる | 既存future captureとは別のsnapshot取得protocol |
| 次周回epochを指定して両側を事前arm | 対象範囲より前に双方の準備とackが完了し、対象epochの境界で同じ範囲を取得できる | request/guardの新しい周回予約意味論とINV-S22の追加契約が必要 |
| 現行future captureの流用 | 両側armのleadと4秒が同じ連続区間に収まり、選んだ範囲と正確に一致する | 条件付きの流用。4秒loopの代替にはならない |

G1の第一候補は、認定済みのlive経路で範囲を予約し、対応する両側PCMを継続保持して終端でRT sealする方式とする。
PREが対象範囲を継続公開し、POSTが期限内に対応を検証できる場合には、既存future captureの追加arm/ackを省ける可能性がある。
これは未実装の成立条件であり、live転送が完成済みという意味ではない。
可聴PREの選択状態と、Pin用の完全なraw対応coverageは別々に判定する。

一つの周回に属する連続native範囲を取得する。
範囲の選び方は、loopに余白がある場合とexact 4秒loopとで分ける。

余白付き窓を既定の範囲とする。
BはPREとPOSTの最大callback frames（宣言値と観測値の大きい方）とする。
観測した折返しの直後のcallback開始位置をq、直前のcallback開始位置をpとし、範囲[s, e)は s ≥ q + B − 1、e ≤ p + 1 を満たすように選ぶ。
hostが折返し後に再開する位置はq以下、折り返す位置はp + 1以上なので、この窓はPPQ換算を使わずに周回の内側へ収まる。
非分割hostでは、折返しをまたぐcallbackのうち折返し後の部分はB − 1 frames以下であり、次のcallbackは再開位置 + B − 1以前から始まる。
したがって、sを含むcallbackが周回ごとの位相によらず来る。
取得は境界blockの折返し後の部分を使わず、再生も既存rendererの開始条件を満たす。
PRE側のproject時刻では、終端側は境界blockの折返し前の部分だけを使うため、この条件のほかに余白を要しない。
遅延のあるchainのPOST側では、project時刻が内容より先に折り返すので、両端にU = chain遅延の余白が要る（Studio Pro 8.1.2で実測）。
VST3連続時刻を鍵にする場合、このUは要らない。
周回長を折返し位置 − 再開位置とすると、一回の観測では窓の幅は最悪で周回長 − 2B + 2 samplesとなり、4秒の窓には周回長 ≥ 4秒 + 2B が要る（48 kHz、B=512で約21 ms、B=4096で約171 ms）。
複数の折返しを観測すれば、qの最小値とpの最大値を使って窓を広げられる。
q、pは観測値であり、loopとseekを区別する根拠にはしない。取得中の連続性、epoch、対応の検証は本節の他の条件のままとする。
前提は、非分割hostが折返しをまたぐcallbackを折返し前の開始位置で報告すること、周回ごとに同じ位置で折り返して再開すること、POSTの補償済み時計の折返しがPCM内容の境界と一致することであり、G1-03で確かめる。
Studio Pro 8.1.2では、前の二つはPRE側で成り立ち、三つ目は遅延のあるchainのPOST側project時刻では成り立たなかった（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)）。
時計と内容の境界の差Uが観測されたprofileでは、両端の余白にUを加える。
宣言値を超えるcallback、窓の外での折返し、loop範囲の変更を観測したら、その予約を失敗にする。

exact 4秒loopでは対象がloop全体になり、余白付き窓を作れない。
4/4拍子、120 BPMの2小節は48 kHzで192000 samplesであり、珍しい条件ではない。
この場合は、境界blockをsample-levelで正しいepochのsubrangeへ分ける証拠と処理が必要になる。
hostによるblock分割、または独立に実証したexact境界offsetがなければ、PPQや次callbackからの逆算だけで補完しない。
証拠を得られないprofileはG1の未達とし、短縮、padding、別周回への置換で成功扱いしない。
未達のprofileでは、loopを4秒 + 2Bより長くする操作か、範囲より前から再生する操作を案内する。
HyphaはDAWのloop設定を変更しない。

履歴は4秒を候補に維持し、上記seal所有方式と第11節のピークledgerを同時に成立させる。
二番目の方式は、live転送が動いていない入口（既存Blindの直接入口など）で次周回予約が必要になった場合に評価する。
既存future captureは、直接入口の「次の4秒」として残す。

単にcallback開始位置が後退しただけでは、exactなloop start/endを確定できない。
hostが境界でblockを分割しない場合、次callback開始位置は既にloop先頭を過ぎ得る。
余白付き窓はexact値を使わず、観測から言える安全側の上下限だけを使う。
窓を作れない範囲（exact 4秒loopなど）は、PPQ換算ではなく、sample-levelの境界証拠と対応を実証したprofileでのみ次周回候補を提示する。
証拠不在、loop範囲変更、seek、pair/format/context変更、arm締切超過、対象周回取り違え、欠測はその予約を失敗にする。
締切超過でさらに次の周回へ黙ってずらさず、必要なら再予約を明示する。
失敗時は第6.3節の復帰policyに従い、既承認・既適用の減衰保持を含むPOST出力へ戻し、別範囲を成功扱いしない。

「次の4秒」も同じowner内で予約し、取得開始時刻とrangeを示す。
継続中の短loopで取得できない条件は残るため、必ず成功するescapeではない。
HyphaはDAWのloop設定を変更しない。
直前の周回末尾4秒へ遡る代替は、最大8秒の履歴による容量増を避けるという第4版の決定に従い採用しない。
次周回案がその代わりとして実用になるかはG1/G4で検証し、未達なら再提案する。

snapshotにはraw PCM、exact範囲、pair/clock証拠、policy、live実適用gain、exact再算定候補、採用gain、承認revisionを保持する。
ramp途中なら瞬時値と到達先を区別し、安定した固定条件が承認されるまでreadyにしない。
live値が許容条件内なら実適用float値をbit単位で維持し、許容差を理由に小さく変更しない。
条件外なら変更量を示して再承認し、新snapshot revisionを作る。
追従中のraw素材を固定することは、過去の可変gain出力を録音再現することではない。
固定ABとBlindの間では、採用済みのPCMとgainを厳密に維持する。

### 7.3 再生と匿名化

LocalBlindTrial.cppは固定範囲の開始からの再生を待つ。
遡及固定後は、DAWが対象範囲より前から再生する必要があるため、範囲表示と待機理由を入口で提示する。
固定範囲がDAWのloop内にあり、明示選択したsourceが待機状態なら、次の周回で範囲へ入り手動巻戻しを省ける場合がある。
これは最初の一巡を開始する条件であり、同じsourceの無制限な自動反復の保証ではない。
現行rendererのexact wrapには、artifact終端とcallback終端の一致、次callbackのartifact開始一致などの条件がある。
既存のSource 1完走後にSource 2を自動armして次の一巡を待つ動作も維持する。
ただしautoarmは再生開始の保証ではない。
例えば48 kHz、loop/artifact [0, 192000)、512 framesの非分割callbackで、折返し後の開始位置が毎周96なら、現行rendererはsample 0を含む開始callbackを受け取れず待機する。
初回にprerollから完走できても、次のSource 2が同じ条件で始まるとは限らない。
この反例は実装を使った模擬callbackで確認し、実DAWの観測とは区別して精査記録へ記載した。

同じ実rendererへ非分割の折返しを与えた第8版の模擬callbackでは、範囲が余白付き窓にあれば、callback長を64〜512 framesで変えた200通りの位相すべてで両sourceが完走した。
範囲先頭を再開位置に置いた場合は、200通り中193通りで両sourceの完走に至らなかった。
したがって、余白付き窓の範囲にはrenderer adapterが要らない見込みであり、adapterの要否はexact loopと余白内の範囲について判断する。
この模擬callbackはhost時計の実報告、IPC、gain、admissionを含まない。実DAWでの成立はG1-07で確かめる。

取得用のexact境界対応を追加しても、既存rendererへ自動的に伝わるとは限らない。
G1-07では取得からSource 1/2の完走までを接続し、再生側の正規化されたsub-block入力が必要かを検証する。
必要なら共通renderer adapterの責務としてG3へ含め、現在のguardを弱めるだけの対応にしない。
未対応時は待機理由とDAWで範囲前から再生する操作を示し、待機上限後の取消/再選択を可能にする。
対象hostで操作目標を満たせない場合はG1Rで判断し、手動巻戻しを無断で完成仕様へ格下げしない。
これは利用者が開始したtrial内の待機であり、準備完了だけで新たなtrialを鳴らす動作ではない。
初回、Source 2自動arm後、明示source切替後、両側完走後、同じsourceの再試聴を別試験にし、一般的な包含loopとexact loopを区別する。
TRACK/STEMでは残りのmixも同じ位置で鳴ることを維持し、Hyphaだけ自由走行で過去音を再生しない。
DAWのloop範囲変更は自動化しない。
Hypha内のclick数だけでなく、DAW操作数、再生待機時間、初回固定音までの総時間を測る。

共有するのはimmutable PCM payloadとrendererの処理である。
記名ABとBlindのmutable Trial objectを共有しない。
初回も再入場も、Blind開始ごとに新trial ID、新CSPRNG割当、空のheard/回答/reveal/command receiptを作る。
乱数失敗時は準備失敗とし、決め打ちのsource順へfallbackしない。
記名ABで両側を完走していても、Blindは回答不可から始める。

BlindではPRE/POST名、source別gain、波形、meter、tooltip、accessibility textから対応が漏れないようにする。
通常の正本計測は続けても、匿名聴取中のsurfaceでは正体を推測できる表示を分離する。
両sourceの同じ区間の完走、選好なし、区別できない、回答保留を維持する。
revealで音を変えず、回答はsnapshotとtrial revisionへ結び付ける。
reveal後の再試行は新trialとし、以前の聴取事実を引き継がない。
Preference Listening TrialをABX検定や改善の証明と呼ばない。

## 8. 共通owner、editor非表示、段階間の復帰

live、固定AB、Local Blindは一つのcomparison_sessionが排他的に所有する。
独立した二つの可聴ownerを許可せず、段階移行途中でownerを解放して取り直す隙間も作らない。
移行は非RTのtransactionとし、失敗時は獲得済み分だけを巻き戻す。
試聴は既存process/project排他へ参加し、gain推定と中身のずれの推定（警告用）は既存2枠中の1枠で行う。
PREに第三の解析枠を設けない。
資源共有は名前だけでなく、仕事量、所有権、上限、停止時の解放を定義する。
現行`AuditionAdmission`はanalysis、process/project試聴排他、capture barrierを束ね、公開の`release()`はまとめて解放する。
E2の「owner保持と解析枠解放」は、そのままのAPI呼出しだけでは実現しない。
G2で解析grantの停止/再取得とcomparison ownerの寿命を分離し、直接取得と共有grantの両方について解放範囲を定義する。
解析枠の再取得に失敗しても、既適用POST減衰、owner、Record排他を誤って解放しない。

| 相手 | 条件 |
| --- | --- |
| 別POST、Reference通常試聴、Version Blind、別入口Blind | 共通owner保持中は同時開始を拒否 |
| Keep、All Keep、Record | 予約、armed、記録、finalizeの双方の入口を保護。減衰保持や終了未確認を早く解放しない。拒否時は保持中の比較と終了手段を示す |
| Reference Capture A | 新規取得と比較開始は排他。保存済み情報の閲覧とは分ける |
| 通常meter | raw正本を観測。試聴音を新たなPOST測定として扱わない |
| 下流Record、別project、別DAW | 既存の保守的scopeを維持。未知routingや同名pairを安全と推定しない |

切替やgain追従をAutomationへ登録せず、不要なgesture、dirty、Undo履歴を増やさない。
必要なpair選択の保存通知は維持する。
再open/再接続では可聴比較を復元しない。
offline/bypassは入力不変を優先し、通知のない実時間printは自動検出を保証しない。
export前にReturnの完了を確認する。

### 8.1 E1とE2の意味

E1＋E2という第4版の決定は維持し、E3の画面なしPRE/固定音継続は採用しない。
E1は比較の入口でhostの画面固定方法を短く案内する。
設定によりplugin windowが再利用されることは確認できるが、すべての現行hostが同じ既定値とは言わない。

| host | 画面が置き換わる条件 | 残す方法と資料の範囲 |
| --- | --- | --- |
| Studio One / Studio Pro | 単一editorを再利用する設定 | Pin / Keep Editor Open。Studio One 4.1公式manualの説明を確認。Studio Pro 8の現行設定とcallbackはG4で確認 |
| Logic Pro | LinkがSingleの場合 | Link Off。設定はproject全体。Singleが工場既定かは今回の公式記述から未確定 |
| Pro Tools | Targetがonのwindow | Target off、またはShiftを併用して開く。Avid 2025.12 guideはTarget onを既定と説明 |

操作説明だけでは、Hyphaへ破棄/非表示callbackがどの順で来るかは証明できない。
既定の実機設定と固定表示設定を記録してLC-22で確認する。
host表示名だけで未知の版の操作を推定せず、不明時は一般案内にする。

E2は「live PRE/固定コピーの出力を止め、POSTへ戻す」と定義する。
これは「必ずunityへ戻す」「音への作用が完全になくなる」と同義ではない。
既承認のPOST減衰が残る状態を、非可聴cacheだけの状態と混同しない。
E2の準備保持はlive段階の例外であり、固定AB/Blindの試行は既存の終了規則に従う。

| editor非表示後の状態 | 実出力 | 保持と再表示 |
| --- | --- | --- |
| POST unityで準備保持 | RT確認済みのraw POST | PRE選択と追従許可を解除。承認済み数値と準備storageを保持できるが、readyは再検証する |
| POST減衰保持 | 既承認・既適用のPOST減衰 | 出力変更は継続中。減衰量、保持理由、Returnが必要なことを再表示時に示す |
| RT確認待ち | 停止やcallback未到着で遷移の実適用が未確認 | 要求済みと完了を区別。終了receiptを捏造せず、次callbackで検証する |

本案ではE2の準備保持中も共通ownerを保持し、Recordと他試聴の排他は続く。
画面を閉じただけでは全体終了でないことを入口と再表示時に示す。
owner解放後に再取得する方式を選ぶ場合は、再開競合と承認の契約変更になるため別途提案する。
非表示のままownerを長く占有する負担はG4で測り、解決済みと扱わない。
比較候補として、POSTがunityで減衰を保持していない場合に限り、非可聴のcacheを残したままownerを解放する方式をG4で測る。
隠れたownerによるRecordと他試聴の拒否を避けられるが、再表示時にownerを取り直せない場合があり、その理由と再準備を示す必要がある。
減衰を保持している間は、ownerとRecord排他を必ず保持する。
この方式の採用は契約変更として第15節で扱う。
数値だけ保持して非表示中に勝手に可聴gainを更新することはしない。

### 8.2 非表示時の資源

| 資源 | E2の規則 |
| --- | --- |
| 可聴PRE選択、追従許可、旧command | 解除し、再表示で復元しない |
| comparison owner、必要なRecord排他 | 全体Return完了まで保持 |
| 承認済みgain/C/policy/revision | 数値と承認範囲を保持。現在の音量一致を意味せず、古さと再検証結果を別保存 |
| optional Analysis leaseとgain/ずれ推定worker | INV-S7に従い解放/停止。追従、中身のずれの推定、Pin候補の更新も止める |
| 音声IPCとclock coverage | 予算内で保持候補。非可聴consumerと寿命管理を続けられる構成だけに限定。RTは解析queueへ積まず、ringの消費とcoverage検証だけを続ける。計画的な停止をqueue overflowの故障に数えない |
| snapshot | 不変payloadを保持できるが、Blind mutable trialの自動復活には使わない |

IPCを保持しても、解析leaseを返したまま同じ解析を別名workerで走らせない。
音声coverageの維持に解析workerが不可欠なら、「準備の全維持」とINV-S7は両立しない。
その場合はreadyを取り下げて再表示時に再準備し、必要な例外はG1Rで承認を求める。
停止中は中身のずれの推定もないことを含め、非表示中に時刻対応が有効だったと推測しない。
解析停止は同一run中でも起こるので、analysis epochまたは同等のgap barrierを更新する。
RTのenqueue停止を確認した後、旧queue、部分的な音量窓、ずれ推定の履歴を非RTで退役させる。
再開後の新鮮な連続窓から候補を作り、非表示前後のデータを連続と扱わない。
承認済み固定gainの保持と、新しいMATCH/FOLLOW/Pin候補のreadyを区別する。
G1-08/LC-22ではworker停止の表示だけでなく、別instanceが実際に空いた解析枠を獲得できることを試験する。

再表示時にはpair、format、context、source/run証拠、fresh coverage、実gainと承認revision、必要なleaseを検証する。
準備が有効ならPREの明示再選択は1操作でよい。
失効や資源競合時は理由と再準備を示し、「必ず即時に1操作で鳴る」とは保証しない。
古いcommandと追従許可を再適用せず、確認待ちのクリックを後で無言に消費する予約にも使わない。

### 8.3 liveとsnapshotのgainを分ける

live承認profileとsnapshot採用profileを別IDで保持する。
各profileはsource別gain、C、policy、承認revision、pair/contextを持ち、保持値と実出力値を分ける。
固定AB/Blindからliveへ戻るとき、以前のlive数値を戻すだけで音量が増えることがある。
例えばPOST減衰がlive −8 dB、snapshot −12 dBなら、旧live値への復帰は+4 dBである。

復帰要求では戻り先をPOSTとして、実適用gainと復帰予定量を提示する。
増大を伴う変更は「調整へ戻る」操作に差分を明示して承認を結び付け、古い承認だけで無言に適用しない。
承認前は既適用のPOST減衰を保持する。
新しい減衰を故障時に自動生成することとは区別する。
unity Return、snapshotからliveへの復帰、live PREの再選択、追従再開を同じ暗黙操作にしない。
receiptには復帰先profile、実gain、承認revisionを含める。
匿名trialの停止receipt、liveへの復帰receipt、全体のunity Return receiptを区別する。
未回答やreveal前でもtrialを終了・失効でき、停止receiptで既承認POST減衰への切替を確認してから、中立の復帰面に減衰量と増大量を示して承認を求める。
増大を承認する表示の前にunity Return完了を要求しない。
終了したtrialへ戻って回答を続けることはせず、再度Blindに入る場合は新trialにする。

### 8.4 背景liveの故障を固定artifactから分離する

| 失効 | 固定AB/Blind | liveへ戻る際 |
| --- | --- | --- |
| 背景liveのqueue/coverage/mappingだけの故障 | 固定payload、trialのclock/pair/formatが有効なら継続可能 | cacheを失効し、必要な再準備を表示 |
| live gain解析だけの不足 | 固定gainを変更しない | 古い候補を適用せず再解析 |
| 共通pair、format、context、試聴clockの失効 | 既存の固定試聴失効条件に従い中断 | 両段階の証拠を更新して再準備 |
| host offline/bypass、processor破棄 | 最上位の入力不変/終了規則に従う | 自動PRE再開なし |

「背景cacheが落ちたから全Blind停止」と「固定PCMだからpair失効も無視」の両極端を避け、失効理由のscopeを明示する。

## 9. UIと利用者観察

共通操作階層とCE2226表示規則へ接続する。
remote側のINV-S37/S38/S39/S40は統合後の番号で再確認し、ローカル側の同番号と取り違えない。
Header/Footerへ専用行を安易に増やさず、現在の段階、実出力、終了方法を優先する。

100%で比較入口を出さず、POST復帰とRETURNを残す決定は、live面の例外として維持する。
記名固定ABにも同じ復帰の考え方を用いるが、Blindの匿名面へsource名とgainを流用しない。

| 段階 | サイズと操作 | 表示 |
| --- | --- | --- |
| live | MATCH、固定/追従、Pin、基準は125%以上。100%にもPOST復帰とRETURN | 実際のsource、適用gain、準備/境界/減衰保持を名指し |
| 記名固定AB | liveと同じ小型の復帰操作を候補にし、固定試聴の開始/詳細は125%以上 | 固定された音であることと固定gainを示す |
| Blind | 現行PluginEditorLocalBlind.cppの最小900×600の隔離面とresize制限を維持 | Source 1/2、回答条件、中立的な試聴終了操作。PRE/POST名とsource別gainを出さない |
| Blind停止後 | 匿名trial停止receiptで中立の復帰面へ移り、必要な増大承認後にlive/全体Returnへ進む | trial停止後にPOSTと必要な減衰保持を表示。unity Return済みと混同しない |

Blindへ入る際に必要な拡大を行い、Blind中のhost resize要求が現行規則どおり処理されることを試験する。
将来100% Blindを許可する場合は別のUI契約変更であり、本版で自動的に採用しない。
既存INV-S25の「300×200への縮小でも操作を失わない」と実装の拡大/resize制限の対応を、統合時に本文も含めて明確化する。
小型化の都合で匿名trialの背後へmeterや情報操作を露出しない。

900×600を超える画面は新しい配置を増やさず、INV-S39の許可した一様拡大を用いる。
全基準サイズ、許可された拡大段階、Windows DPI、keyboard、accessibilityを段階別に試験する。
既存FOLLOW/HOLD/LIVEの意味と競合しない名称を選び、内部状態名をそのまま画面へ出さない。

remote側INV-S40に合わせ、案内、状態、通知、menu、tooltipは英日双方を用意する。
機能名、略語、単位、数値、外部の名前を無差別に翻訳せず、既存のcatalogと描画境界を使う。
新しい境界待ち、Pin予約、E2、減衰保持、Returnの文言は小型の折返し、missing translation、accessibilityを確認する。
匿名性は両言語のtooltipと読み上げでも保ち、英語だけの合格で終えない。

色やhoverだけに依存せず、要求中、適用済み、再生待ち、境界確認待ち、PRE待ち（PREを選んだままPOSTが鳴っている）、中身のずれの警告、中断、減衰保持、通常復帰を区別する。
未認定hostや明示操作の失敗は通知し、無操作時の内部fallbackで不要なエラーを出さない。
観察試験は開始、切替、隣のEQ、編集、Hypha再表示、Pin/代替予約、DAW再生、Blind、回答/保留、reveal、再試行、live復帰、全体Returnを一巡する。
回答やrevealを退出の条件にしない。
E2で隠れたownerがRecordや別比較を妨げる場面も観察する。
準備click3以内、切替1、終了要求1は暫定目標とし、承認、DAW操作、待ち時間、再準備を数から除外しない。

## 10. 変更対象と責務

メインcheckoutには、JUCE submodule、host clock診断、構造検査、別の計画書の未commit変更がある。
host clock診断はG1で拡張する対象である。
本書の「ローカルのみ」はee522a53と5a6a9db5の比較を指し、後続の統合branchに存在しないという意味ではない。
着手前に、ローカルmainのB-1010〜B-1023とorigin/mainの統合状況、未commit変更の所有者と基点を照合し、cleanな隔離worktreeで進める。
無断で既存差分を取り込んだり、戻したりしない。

| 責務 | 既存の主な接続先 | 新設候補 |
| --- | --- | --- |
| RTのtapと出力順序 | PluginProcessor.cpp、PluginProcessorAudition.cpp、PluginProcessorFormat.cpp | PluginProcessorLiveCompare.cpp、live_compare/LiveCompareRenderer.* |
| 時計、範囲coverage、run照合、境界の分類 | HostProcessClock.h、PluginProcessorHostClock.cpp、local_blind/CaptureClockGuard.h。ローカルのみのHostAuxiliaryClock.hとmeter_chain_join.rsの再結合規則は統合状況を確かめてから使う | live_compare/LiveCompareClock.* |
| host認定 | ローカルのみのHyphaChainClockPolicy.hのexact host判定。統合候補c60ab679ではDebug限定 | live_compare/LiveCompareHostPolicy.h。計測用の判定とは別の認定一覧と証跡 |
| 連続PCMと寿命 | 既存pair locatorとcontrolの検証境界、RtPublicationSlot.h、Referenceのpage lease | live_compare/LivePcmRing.*、LivePcmTransportMac.*、LivePcmTransportWindows.*、Rust側のlayout定義 |
| 開始、終了、排他 | PluginProcessorPairing.cpp、analysis_lease.rs、project_audition_lease.rs、reference_capture_admission.rs、audition_admission_ffi.rs | live_compare/LiveCompareSession.*、live_compare_admission.rs、専用C ABI module。ownerと解析grantの独立停止/再取得 |
| 音量推定と更新policy | reference_gain.rs、reference_gain_ffi.rs、LocalBlindPreparation.cpp | live_compare/LiveCompareGainWorker.*、live用policy、固定/追従とblock guard、中身のずれの推定と警告（INV-LC7案） |
| 固定ABとLocal Blindの共通準備 | ExactRangeCapture*、LocalBlindProductSession.*、LocalBlindPreparation.*、LocalBlindTrial.*、LocalBlindTransition.h | comparison/ComparisonSnapshot.*、共通factory、次周回予約と余白付き窓の算定、bounded seal、exact loopで必要な場合のsub-block renderer adapter、段階gain profile。匿名TrialViewは分離 |
| Referenceとの競合 | reference_audition/ReferenceComparisonController.*、ReferenceComparisonCapture.cpp | 共通ownerの拡張と競合試験 |
| UIと保存 | PluginEditor.*、PluginEditorMenu.cpp、PluginEditorLocalBlind.cpp、PluginProcessorState.cpp、HyphaObservatoryView*、HyphaUiContract.h | PluginEditorLiveCompare.cpp、HyphaLiveCompareView.* |
| 英日表示と小型状態 | remote側の`HyphaLanguage.h`、`HyphaJapaneseCatalog.h`、`HyphaJapaneseBlind.cpp`、screen text検査 | 統合後のcatalogへ新しい状態/案内を追加し、匿名性を両言語で確認 |
| 検証装置（非出荷） | tests/pdc_validation_delay/、CapturePairComparison.*、ローカルのみのtests/host_clock_diagnostic/ | latency切替fixture、AU/AAX用の時計trace、転送試作用の別identity plugin |
| 検証と配布 | juce_shell/tests/、juce_shell/CMakeLists.txt、.github/workflows/ci.yml、source契約とline budget | 連続比較のnative試験、実host証跡、protocol互換試験 |
| 正本文書 | AGENTS.md、README.md、docs/hypha_invariants.md、meter表示契約、共通安全契約、9/14 Blind計画 | 本計画を採用した範囲の同期 |

新設名は責務を示す候補であり、存在するAPIやfileとして引用しない。
既存の500行超fileとline budget baselineは着手時に再確認する。
例えばFFIのlib.rsは、origin/mainで5252行、ローカルmainで4800行と異なる。
製品変更の前に対象責務を500行以下のmoduleへ抽出する先行commitを置き、line budgetを下げる。
無関係な巨大fileの全分割は着手条件にしない。
新しいC ABIは専用moduleへ置き、既存libへ実装を積み増さない。
JUCE wrapper変更はG1で必要性が判明した場合だけ行い、既存の未commit診断変更と区別する。

## 11. 容量と性能の予算

比較の準備中だけPCM資源を確保し、inactiveな全pairへ常設しない。
以下は3秒の遅延保持、4秒の履歴、4秒のsnapshot、0.5秒のworker queueを仮定したpayload小計である。
3秒は設計候補であり、Perception ABの既定値からHyphaの実現性が証明されるわけではない。

```text
F = sample rate, C = channels, B = 最大callback frames
R = ceil(3 × F) + 2 × B
PREリング = R × C × 4 bytes
raw PRE/POST履歴4秒 = 2 × 4 × F × C × 4 bytes
snapshot PRE/POST 4秒 = 2 × 4 × F × C × 4 bytes
worker queue 0.5秒 = 2 × 0.5 × F × C × 4 bytes
```

| stereo、B=4096 | リング | 履歴 | snapshot | queue | 小計 |
| --- | --- | --- | --- | --- | --- |
| 48 kHz | 1.16 MiB | 2.93 MiB | 2.93 MiB | 0.37 MiB | 7.3867 MiB |
| 192 kHz | 4.46 MiB | 11.72 MiB | 11.72 MiB | 1.46 MiB | 29.3594 MiB |
| 768 kHz | 17.64 MiB | 46.88 MiB | 46.88 MiB | 5.86 MiB | 117.2500 MiB |

これは実測RSSでも総容量でもない。
現行factoryのvectorコピーをそのまま追加すると、768 kHzで46.875 MiB増え、164.125 MiBになる。
旧rendererの退役だけを待ってもこの複製は消えない。
共通immutable payloadをmove/adoptまたは共有し、記名ABとBlindの軽量なmutable stateを分離する責務変更が必要である。
shared ownershipを使っても、最後のrelease/freeをAudio Threadへ持ち込まない。

固定ABとBlindの間もlive転送を保持すると、リングは小計に含まれたまま残る。
直前の周回の末尾4秒を遡る代替は採らないため、履歴は4秒のままとする（採れば最大8秒で、768 kHz stereoではさらに46.875 MiB増える）。
次周回予約は履歴を8秒へ拡張しない方針だが、採用する予約方式のstaging、arm領域、周回metadata、seal領域を計上する。
768 kHz stereoのraw PRE/POST 4秒は12,288,000個のfloat、46.875 MiBであり、終端callbackへ一括コピーを集中させない。
既存の履歴/snapshot枠をadoptするのか別領域を増やすのかをledgerに明記し、segment保護中の書込み先と旧artifactの退役待ちも重複計上する。
既存future captureの領域だけで収まるとは仮定しない。
さらにrun metadata、alignment、mapping丸め、RT scratch、filter、Pin封印待ち、future capture、転送decode用buffer、旧新artifactの重なり、中身のずれの推定の作業領域を計上する。
capture開始から解析、AB、Blind、取り直し、終了までのピークledgerを作り、allocation creditで同時確保を制限する。
128 MiBは暫定の新経路予算であって、全rateで達成済みとはしない。
既存Referenceや別の解析枠を含む総プロセス量も測る。
収まらなければ所有権と保持方法を再設計し、上限や対応範囲の変更は理由付きで再提案する。
高rateだけを無断で対象外にしない。

inactive時の追加PCMとworkerは0、RT入口はboundedな状態確認のみを目標にする。
有効時は当該blockのframesに比例した処理とし、queueを一callbackで無制限に消化しない。
RTのallocation/free/lock/I/O/waitは0件を必須にする。
mappingの初回接触は非RTで済ませるが、一般OS上でpage residencyを永久保証したとはしない。
追加RT時間p99.9がbuffer時間の1%以内、観測最大5%以内を暫定予算とし、機器、build、rate、buffer、並行解析数と測定時間を記録する。
平均CPUだけで合格にせず、通常経路の既存性能基準も緩めない。

## 12. 実装gate

本レビューでは以下を実行していない。
本書承認と製品実装の依頼を受けてから進め、実機、接続、公開に必要な権限はその時点で確認する。

| Gate | 作業 | 通過条件 |
| --- | --- | --- |
| G0 基点と調査範囲の承認 | ローカルmainとorigin/mainの統合状況（B-1010〜B-1023、重複したB番号、INV番号）を確定する。対象host候補、統合workflow、安全契約の変更候補を整理する。既存差分と正本を、統合後のmainで再照合する | 判断事項と実験事項が分離される。未実証の技術選択を利用者に保証させない。本書の参照が統合後のmainで成立する |
| G1 成立性prototype | 静的対応、deadline、境界確認待ち、動的PDC、IPC、次周回予約、E2資源、容量を非出荷fixtureで検証。中身のずれの警告は誤警告、見逃し、判定不能も評価 | 三条件と予約の締切を独立判定。4秒loopと非分割境界を含め、未達は障害として提示。固定Δや警告の沈黙で免除しない |
| G1R 採用判断 | G1結果からhost認定単位、境界規則、同期方式、復帰policy、追従値、容量を提案 | 利用者が提供範囲と契約変更を承認。必須項目の無断先送りなし |
| G2 transport/owner | 対象責務を先行抽出し、共有protocol、寿命、排他transaction、解析grantの独立停止/再取得、故障経路を実装 | RT禁止操作0。owner保持中の解析枠解放、再取得失敗、旧版混在、競合を含む試験が合格 |
| G3 gain/snapshot/Blind | 共通gain、追従、RT guard、exact Pinとその代替、payload共有、新trial、Returnを実装 | raw計測不変、同じ固定AB/Blind出力、匿名性、ピークledgerが合格 |
| G4 操作と観察 | live/固定AB/Blind別のサイズ規則、英日表示、実receipt、DAW再生、E2、Undo/復元を確認 | 匿名性、現在音源、gain復帰、隠れたownerと全体終了が理解される |
| G5 同一候補の受入 | 第13節、通常回帰、native、性能、各hostをexact commitで検証 | 全必須条件が合格。過去の別commitの証跡を完了根拠に流用しない |
| G6 配布 | 別途許可されたreleaseとして同一commitの三チャネルを用意 | LS signed/notarized pkg、HP zip/GitHub/英日リンク、署名Windows installerの全条件を満たす |

G1候補はmacOS Studio Pro VST3/AU、Windows Studio Pro VST3、Logic AU、AAX NativeのmacOS/Windowsを個別に扱う。
AAXはPro Toolsで、対応の鍵の前提（POSTに遅延補償済みの位置が渡るか）を最初に測る（第5.4節）。
これは対応を約束した一覧ではない。
OS/DAW build、architecture、rate、buffer、process mode、PDC設定、経路は開始前に記録する。
Logic Proはbus trackの有無など、PDC情報が変わり得るrouting条件を分けて記録する。
既存PDC Validation Delay 4096、Debug PCM比較を流用し、host clock診断は統合状況を確かめてから流用する。
可変delay、通知遅延、AU/AAX traceを追加する。
Studio OneのStereo設定、S-1〜S-5、Windows接続runbookなど既存安全手順を省略しない。
今回はDAW、Windows検証機、Merging機器を操作していない。

### 12.1 G1の実験仕様

各実験は製品とは別identityの非出荷fixtureで行い、結果はhost profileごとに記録する。
profileはDAW build、OS build、PREとPOSTそれぞれのformat、architecture、rate、buffer、process mode、PDC設定、routingを含む。
測定事実と、事前に定めた条件に対するpass/fail/判定不能を別々に記録し、どの判断に使うかを明示する。

| ID | 問い | 構成と操作 | 記録する値 | 決まること |
| --- | --- | --- | --- | --- |
| G1-01 静的対応 | 同一native位置のPRE/POSTは同じ音か | PRE→既知遅延（0、1、4096 sample、保持上限付近）→POST。epochを識別できるPCM。AAXはPro Toolsで、同じcycleのPREとPOSTの`GetCurrentNativeSampleLocation`と`GetTODLocation`を記録 | 同一範囲のbit一致、残差、位置の付き方。信号遅延と通知順を分離。AAXでは位置の補償の有無 | host認定の候補、同一native写像を使えるprofile、AAXで対応の鍵が成り立つか |
| G1-02 到着期限 | POSTが必要とするPREは間に合うか | G1-01の構成に可変buffer、CPU負荷、mixed format、別process（対象時） | 未到着件数、PREの公開とPOSTの要求の時刻差 | 転送方式、別processとmixed formatの提供範囲 |
| G1-03 境界 | loop、seek、停止と再開で何が起きるか | loop長2/4/8/16秒、非整列境界、前後seek、reset有無。3秒delay/2秒loop等の遅延がloopより長い条件と保持容量超過。hostのplugin sleep（無音入力で呼出しが止まる。AAXはDynamic Plug-In Processing）をまたぐ条件。照合は周回ごとに印が変わるfixtureで行い、直前の周回の誤採用を検出できるようにする | callback順、host時計変化、PCM境界ID、reset時点を別記。折返しをまたぐcallbackの報告位置、周回ごとの折返し/再開位置、POSTの時計と内容の境界差U。複数pending epoch、証拠失効、未対応区間、ringに残る旧runの音の誤受入れ | 境界待ちの上限、旧証拠の有効範囲、Cで境界ごとにPOSTが鳴る区間の長さ、周回を特定する手段と、確かめられない場合の扱い |
| G1-04 動的PDC | latency変更を音より先に観測できるか | 切替式遅延（0↔4096）を再生中に切り替え、通知の先行、遅延、欠落を作る。Pro Toolsでは、再生中に補償が改まらないことと、停止と再開での復帰、遅延補償のOFFの通知 | 音の変化から位置の変化までの時間、誤対応sample数、自然に終わらないずれの有無 | 動的PDCが出荷の障害か、barrierの要件、警告で扱う型 |
| G1-05 中身のずれの警告 | 時計の対応と中身のずれの食い違いを警告できるか（止めない） | G1-04に加え、遅延を誤って報告する（報告しない）fixture、Pro Toolsの再生中の遅延変更、意図的delay、reverb、周期信号、無音、極性反転、非線形処理 | 警告までの時間、誤警告、見逃し、判定不能の割合 | INV-LC7案の推定の方式（窓、探索範囲、周期）と警告の条件、跳びの検出の条件（INV-LC10）。2026-09-28の見直しで、停止専用監視からこの形へ改めた |
| G1-06 転送 | PREからPOSTへの転送をRTで安全に使えるか | macOSのPOSIX shmとfile-backed、Windowsのmapping。同一process別module、別process、30分以上。sandboxのhost（GarageBand等）と、AUを別processで動かすhost（Logic）で、名前の規則（app groupの接頭辞、31 bytes）とresourceUsageの範囲で届くか。AAX（Pro Tools）のstereoとmulti-mono | 欠落、page fault、追加RT時間のp99.9と最大、到達の可否 | platform別の転送方式 |
| G1-07 次周回予約と再生 | loop内の4秒を取得し、両sourceを完走できるか | 余白付き窓（8/16秒loop、4秒 + 2B付近）とexact 4秒loopを分ける。非分割境界、予約、bounded seal、worker 0.5秒遅延、Source 1/2再生まで接続 | 同一passの独立raw tapとの各sideのhash一致、誤epoch、先頭上書き、RT最大処理量、待機/完走。窓の縁と窓外の対照 | Pin方式、余白付き窓の実DAWでの成立、exact loopでのrenderer adapterの要否 |
| G1-08 editorと資源 | 非表示時に何が起き、何を保てるか | hostの画面置換/固定、hide/show、別instanceの解析枠取得、再取得拒否、同一run中の解析gap | callback順、実lease所有、Record排他、queue epoch、転送、ready待ち | E2の細部、admission分離、INV-S7例外の要否 |
| G1-09 容量 | 予算に収まるか | 48、96、192、384、768 kHzのstereoで、live、Pin、Blind、取り直しを通す | ピークledgerと実測RSS | 128 MiB案の可否、所有権の再設計の要否 |
| G1-10 既存Blindの前提 | 既存Local Blindの同一native範囲は他hostでも成立するか | G1-01と同じ装置で既存Blindの取得経路を使う | bit一致と残差 | 既存Blindのhost gateの見直し |

G1-01、G1-02、G1-03を最初に行い、その結果で後続の実験範囲を絞る。
実施状況: G1-01（遅延0と4096）とG1-03（4秒と8.125秒のloop、seek、停止と再生）を、Studio Pro 8.1.2のmacOS VST3とAU、48 kHz、process block 2048で実施した（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)）。
G1-02とG1-06は、同じhostの同一process、負荷なし、各試験の再生が約1分という限定条件で、プローブにより実施した（同記録第6節）。
周回の規則をplugin内で動かすプローブv2で、G1-02、G1-03、G1-06を再試験し、plugin sleepの影響を測った（同記録第7節）。
呼出しの空白の規則をplugin内で動かすプローブv3で同じ試験を再実施し、G1-04を音と報告を同時に変える1つの型で測り、infinite tailの報告を観察した（同記録第8節）。
Pro Tools Developer 2026.4（macOS、Intel、48 kHz、AAX）で、G1-01、G1-03、G1-04とDynamic Plug-In Processingを行った（G1記録第9節）。
Windows（VST3とAAX）、他のbuffer設定、保持上限付近の遅延、報告が先に来る型や報告が来ない型の遅延変更、G1-02の可変bufferと負荷と別process、G1-06の30分以上と別processとfile-backed、M1のplugin内での実装は未実施である。
これまでの照合は、位置ごとに一意で周回ごとに同じ識別PCMで行った。周回単位の取り違えは検出できないので、G1-03で周回ごとに印が変わるfixtureを使う。
G1-07の余白付き窓は、G1-03で折返しの報告位置を確かめたprofileから行い、exact 4秒loopは境界証拠を得られたprofileに限る。
4096 samplesは48 kHzで約85.33 msであり、これだけで3秒保持と短loopの組合せを検証済みにしない。
保持容量内の最大需要と上限+1を分け、超過を正常な対応へ丸めない。

G1-07のoracleは、同じpass/epochのPREとPOSTそれぞれのraw tapを、被試験matcherとは独立に切り出す。
fixtureは周回ごとに異なる決定的な有限PCMを出し、直前周回を誤採用する故障注入を検出する。
別の再生passのhashを正解にせず、状態を持つ処理の正常な変化を誤失敗としない。
PREとPOSTのbit一致は純粋な既知delay fixtureの評価に限定し、一般の加工chainには同一passの各sideの忠実性を要求する。
どの実験でも、未達を固定Δや警告の沈黙で埋めない。

## 13. 受入試験

ここでの数値は将来の合格条件であり、実測結果ではない。
未実証の項目を「pass」と表記しない。

| ID | 試験と合格条件 |
| --- | --- |
| LC-01 通常透明性 | inactive、音量適用前の準備取消、offline/host bypass通知buffer、再openでraw入出力bit同一、追加latency 0、正本Record不変。音量適用後の中断はLC-10と区別 |
| LC-02 静的対応 | 識別PCM/既知delay、mono/stereo、rate/block matrixで残差0 sample。意図的delayやEQを勝手に補正しない |
| LC-03 callback分割 | PRE256→POST64×4、その逆、0 frame、可変block、境界跨ぎ。counter差ではなくcoverageで採否 |
| LC-04 host認定 | profile外、optional clock欠落、同位置別周回、古いpacketで誤って対応成立としない。周回ごとに印が変わるfixtureで、遅延がloop長以上の条件を含める |
| LC-05 転送 | wrap、descriptor frontier、短loop、producer/consumer競合、別module/process、再起動、旧版混在、formatが異なるPRE/POST。data race/未初期化読取り0 |
| LC-06 故障 | PCM underflow/overflow、peer終了、pair消失、rate変更でPRE失効。通信回復だけのPRE再開0 |
| LC-07 gain基準 | d=0、正負、±24 dB境界、ceiling境界、承認拒否。POST基準と承認付き減衰をgolden値で照合 |
| LC-08 窓 | 2MIX 3秒/27 blocks、TRACK exact4秒/3 active windows、無音、片側欠測、短loop実再生、疎な打音を区別 |
| LC-09 追従 | 無音復帰、急変、累積±6 dB候補、古いrevision、ramp反転。上限制約を一致表示で隠さない。可聴pumpingを評価 |
| LC-10 保護/終了 | observed TPとsample guardを区別。nonfinite、raw自体のover、音量適用後の中断/Return、停止、offline、host bypass、破棄の実gainと優先順位を照合。承認済み減衰保持と新規減衰を区別し、bypass解除でPRE復活0 |
| LC-11 排他 | 両Startの競合、段階移行失敗、遅延した開始、Record予約/armed/finalize、Reference capture/試聴、owner解放順 |
| LC-12 表示 | requestedとactualを区別。transition、部分出力、境界、中断、gain保持のreceiptが実音に一致し、PRE完走を誤計上しない |
| LC-13 保存/Undo | 自動gain更新の不要なgesture/dirty/Undo追加0。隣接plugin編集を戻せる。再openで比較自動再開0 |
| LC-14 loop/seek | 認定条件で100回の反復とseek、非block境界、chain flushを実行。隠れたPOST代替なし。未達は機能障害として残す |
| LC-15 Pin cutoff | workerを0.5秒遅らせ、block途中cutoff後もlive書込みを継続してexact4秒の先頭/終端を照合。古い範囲への置換、先頭上書き、RT一括4秒copyは0。未来取得は明示操作のみ |
| LC-16 固定出力 | 同一artifact、同一source、同一再生位置、同一transition状態で記名AB/Blindの出力bit同一。live/fixed gainの無言の変更0 |
| LC-17 trial分離 | 記名AB完走後の初回Blind、reveal後、再入場で新ID/乱数/heard初期化。乱数失敗、旧回答、旧commandを拒否 |
| LC-18 操作 | DAW巻戻しまたはloopを含む全工程のclick/待ち時間/再armを記録。固定音と現在chainの混同0を目標に観察 |
| LC-19 メモリ/RT | 全rateのピークledger、連打、取り直し、退役待ち、future capture、live転送の保持、allocation失敗。予算超過とRT禁止操作0 |
| LC-20 動的PDC | delay変更と通知先行/遅延/欠落を分ける。時計で最初に検出できた不整合でPOSTへ倒し（C）、較正し直してPREへ戻る。検出できない区間の長さを記録し、profileの許容値と照合。報告しない変更と、再生中に補償を改めないhostでは、中身のずれの警告が出ること、比較の途中の跳びでPOSTへ倒れ、再生の停止と再開の後に戻ることを確かめる。Pro Toolsは版ごとに、再生中に補償を改めるかを確かめる |
| LC-21 長時間 | 30分以上のlive、並行解析、pause/resume、CPU負荷、分離process。deadline、欠落、page fault、p99.9と最大を記録 |
| LC-22 editor | 画面置換/固定表示、E2のunity/減衰保持/RT未確認を区別。owner保持中に別instanceが空いた解析枠を取得でき、再取得拒否でも減衰/Record排他が維持される。旧command解除、ready時の1操作、失効時の再準備、終了導線を確認 |
| LC-23 匿名UI | 英日双方で名前/gain/meter/波形/tooltip/accessibilityの対応漏れなし。Blindの900×600最小/resize制限と終了receipt後のlive表示復帰を確認 |
| LC-24 worker隔離 | 解析停止中のenqueue停止、同一runでのanalysis epoch/gap更新、旧queue/部分窓の破棄、新鮮な連続窓での復帰を確認。追従/Pin保留と固定gain試聴を分離し、計画的な停止を音声故障と混同しない |
| LC-25 回帰 | 既存Local Blind、Reference A/B/C、Version Blind、計測、Keep/Record、PRE不在、再起動、ファイル欠損を確認 |
| LC-26 境界の分類 | 時計通知/PCM境界/実行順を分け、片側先着、古い証拠、reset、複数pending epoch、timeoutを注入。旧PCMの存在だけでは継続せず、現在も有効な証拠の範囲のみPRE。失効時はPOST、有限期限で分類する。境界では証明が戻った最初のblockでPREへ自動復帰し、証明前の復帰0。説明できない欠落と失効では自動復帰0。自動復帰は対称5 ms遷移で、確かめていないPREを遷移に使わない。PRE待ちの表示が実音と一致 |
| LC-27 Pinの代替 | 周期4/8/16秒、余白付き窓の縁（q + B − 1、p + 1）と窓外、非分割境界のexact subrange、予約締切、arm遅延、loop変更、宣言値を超えるcallback、誤epochを確認。同一passの独立raw tapと比較し、前周回の故障注入を拒否。既存1秒leadと境界block回避だけでは4秒loopを取れない対照を保持。無言置換0 |
| LC-28 小さい画面 | live/記名ABの100%復帰例外、125%以上の詳細操作、Blindへの拡大とresize制限を別試験。英日通知、language切替、許可拡大段階、DPI、keyboard/accessibilityを確認 |
| LC-29 中身のずれの警告 | 遅延を誤って報告するfixture、Pro Toolsの再生中の遅延変更、無音、周期、低SNR、極性反転、非線形、reverb、意図したdelay、探索範囲外で、警告までの時間、誤警告、見逃し、判定不能を記録。比較の停止、Kの補正、開始や再開の条件に使わない。「一致」を表示しない |
| LC-30 段階gain復帰 | live −8 dB→snapshot −12 dB→liveの+4 dB復帰を含め、profile/revision、承認、実gain receiptを照合。未回答Blindの停止→減衰表示→増大承認→live/unityを試験。停止/live復帰/全体Returnのreceipt混同と無言増大0 |
| LC-31 背景故障 | 背景liveだけの故障では固定artifactの条件が有効なら継続。共通pair/format/clock失効は既存規則で中断。帰還時のready失効/再準備を確認 |
| LC-32 loop再生 | 余白付き窓、範囲先頭が余白内の包含loop、exact loopを分け、初回、Source 2自動arm後、明示切替、両側完走後、再試聴を分離。余白付き窓は可変callbackでもadapterなしで両側完走。preroll初回成功後に非分割境界でSource 2が待機する対照を保持し、exact loopは対応profileのadapterで両側完走を実証。遡及Pinの再生可否の予告と実際の待機が一致。無限待機の成功扱い、完走誤計上、準備だけの開始は0 |
| LC-33 AAX | Pro Tools（macOS、Windows）で、補償済みの位置、Dynamic Plug-In Processing、遅延補償のOFFの通知、multi-monoでchannelの間にPREとPOSTが混ざらないこと、offline bounceでA経路、AudioSuiteのinstanceで比較を開かないことを確認。第5.4節の品質目標を、認定範囲の全条件（版、OS、rate、buffer、stereo/multi-mono）で満たす。再生中の遅延変更では、ずれたPREを出す時間を測って記録する |

利用者の痛みとの対応はP1→LC-07/10、P2→LC-09/10/30、P3→LC-18/22/27/32、P4→LC-02/03/14/20/26/29/33、P5→LC-13、P6→LC-01/10/13、P7→LC-08/09、P8→LC-16/17/23/28とする。
実装変更時はcargo test、cargo clippy、対象native試験、source契約、line budgetを実行する。
kirin_hypha_ffi変更時は通常workspace greenだけで終えず、ignored parity/pairing_candidatesの一覧件数を実測して全件実行する。
release時の三チャネルgateはLS runbookに従い、macOSだけで完了としない。

## 14. 外部仕様とコード参照の適用範囲

| 資料 | 確認内容と限界 |
| --- | --- |
| [Steinberg ProcessContext](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ProcessContext.html) | loop境界でのblock分割は必須ではない。project時刻とoptional連続時刻を区別 |
| [VST3 Processing FAQ](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html) | 自分のlatency変更通知（`restartComponent(kLatencyChanged)`。hostは読み直し、対応していれば補償を改める）を、隣接pluginの変更前barrierや総遅延取得APIとみなさない。`getTailSamples`の`kInfiniteTail`は、pluginを常に処理させる方法として挙げられている |
| [JUCE AudioProcessor](https://docs.juce.com/master/classjuce_1_1AudioProcessor.html) | 可変callback長。typical block sizeだけで対応を決めない |
| [Apple AU Programming Guide](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/AudioUnitDevelopmentFundamentals/AudioUnitDevelopmentFundamentals.html) | ResetでDSP状態を戻す説明。全seekでの呼出し、chain全遅延分の無音、preroll不在までは定義しない |
| [XNU header](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/sys/posix_shm.h)、[実装](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/kern/posix_shm.c) | 確認した実装の31 bytes、割当済みshmの再ftruncate拒否。対象OSで再測定する |
| [Apple App Groups](https://developer.apple.com/documentation/BundleResources/Entitlements/com.apple.security.application-groups)、[TN2247](https://developer.apple.com/library/archive/technotes/tn2247/_index.html) | 第三者hostへ命名だけでsandbox権限を付与できない。TN2247がAUのresourceUsageに挙げる鍵は`iokit.user-client`、`mach-lookup.global-name`、`network.client`、`temporary-exception.files.all.read-write`の4つ |
| [Apple DTSの回答（POSIX semaphore）](https://developer.apple.com/forums/thread/756420)、[同（共有memory）](https://developer.apple.com/forums/thread/719897) | sandboxの中の名前は、app group IDを接頭辞にした`GGG/NNN`で最大31 bytes。共有memoryもApp Group経由で共有する |
| [Microsoft Named Shared Memory](https://learn.microsoft.com/en-us/windows/win32/memory/creating-named-shared-memory) | pagefile-backed方式と寿命。Global特権を必須にしない |
| [Ableton PDC FAQ](https://help.ableton.com/hc/en-us/articles/209072409-Delay-Compensation-FAQ) | 曲位置依存の処理の制約。全host共通の時刻意味とはしない |
| [Metric AB変更履歴](https://www.plugin-alliance.com/products/metric-ab) | Logicのbus track等のPDC情報に対する手動補正のメーカー説明。Logic全routingへの一般保証ではない |
| [Sound On Sound: Studio One 5.4](https://www.soundonsound.com/techniques/studio-one-54-plug-nap-real-time-chord-detection) | Plug-in Nap。instrumentとMain出力のinsertには働かない。外部の解説であり、Studio Pro 8の挙動は実測（G1記録第7.4節、第8.4節）で確かめる |
| [Apple: Logic Proの過負荷を避ける](https://support.apple.com/en-us/108295)、[Sound On Sound: REAPER](https://www.soundonsound.com/techniques/running-multiple-plug-ins) | LogicのProcess Buffer Range、REAPERのanticipative FX processing。呼出しの間隔への影響は未測定であり、空白の判定値をhostごとに決める理由として挙げる |
| [AAX SDK Pro Tools Guide](https://learn-cdn.avid.com/AAX_SDK_2p1p1/Documentation/Doxygen/output/html/a00274.html) | 再生中は遅延補償を更新しない（Pro Tools 2026.4の実測では再生中に反映された。G1記録第9節）。Dynamic Plug-In Processing（Pro Tools 11以降）。loopで状態をresetしない。offline bounceは実時間より速い。multi-monoはchannelごとのinstance |
| AAX SDK 2.9のheader（手元。SDKはrepositoryの外） | `GetCurrentNativeSampleLocation`（再生中だけ、buffer先頭のtimeline位置）、`GetTODLocation`（再生開始からの進み）、`SetSignalLatency`（即時の適用は保証されない）、`AAX_eNotificationEvent_DelayCompensationState`（Pro Tools 12.6以降）、`AAX_eProperty_Constraint_AlwaysProcess`（chain全体を処理させ続ける） |
| [Apple Logic Pro guide](https://support.apple.com/guide/logicpro/work-in-the-plug-in-window-lgcpbc21a1fd/mac) | Link Singleで画面再利用、Offで別画面、project全体の設定。Singleが工場既定かは未確認 |
| [Studio One 4.1公式manual](https://pae-web.presonusmusic.com/downloads/products/pdf/Studio_One_4.1_Reference_Manual1.pdf) | 単一editorとPinの説明。現行Studio Pro 8の既定値やHypha callbackを証明するものではない |
| [Avid Using EuControl Surfaces 2025.12](https://resources.avid.com/SupportFiles/ProMixing/Using_EuControl_Surfaces_v2025.12.pdf) | p.151でTarget on時の画面置換、off、Shift操作、既定onの説明。実際のhost lifecycleは別試験 |
| [Perception AB公式guide](https://www.meterplugs.com/files/perception-ab-guide.pdf) | 印刷p.8はPRE基準のclip可能性が低いとし、p.21は起きないとする。説明の強さが異なり、Hyphaのclip-free保証へ転用しない |

Perception ABのページ間の説明差は第4版の指摘が正しい。
第3版の調査はp.8を根拠に限定したが、マニュアル全体に強い表現が存在しないという意味にはしない。
競合製品の実機でどちらが正しいかを今回確定したものではない。

JUCE wrapperはsubmodule内のVST3 setProcessingとAU Resetを参照し、版更新時に再確認する。
ソースの接続点は統合後のcheckoutで再検証するが、今回確認した主要根拠は次のとおりである。

| 論点 | 基点とfile |
| --- | --- |
| 既存の1秒先開始 | HEAD/remote両方で同じ `juce_shell/src/PluginProcessorPairing.cpp` のrequest planning |
| 要求長とarm | `local_blind/PairCaptureBarrier.h`、`CaptureClockGuard.h`、`LocalBlindCaptureService.cpp` |
| 固定範囲への待機、exact wrap、減衰保持 | `juce_shell/src/local_blind/LocalBlindTrial.cpp`。`PluginProcessorAudition.cpp`はexactLoopRangeValidへhostのlooping真偽だけを渡し、loop点のsample値は渡さない |
| Blindの最小サイズ | `juce_shell/src/PluginEditorLocalBlind.cpp` |
| 非表示時の解析停止 | INV-S7、`PluginEditorAnalysis.cpp`、`PluginEditorObservatory.cpp`。`crates/kirin_measure/src/analysis_lease.rs`の`AuditionAdmission`と`LocalBlindProductSession.cpp`のscope解放も照合 |
| remote側の英日表示 | 5a6a9db5のINV-S40、`HyphaLanguage.h`、`HyphaJapaneseCatalog.h` |

新しい外部APIを採用する際は、その時点で公式仕様を再確認する。
利用者投稿と競合の比較表は外部調査を併読し、今回追加した訂正と確認範囲を精査記録へ残す。

## 15. 維持する決定と残る判断

第4版に記録された利用者の決定を、技術検証でしか決められないことと分ける。
本版で既存契約を更新したり、決定の範囲を広げたりしない。

| 区分 | 状態 |
| --- | --- |
| 維持する計画上の決定 | editor E1＋E2、liveの100%復帰例外、次周回/次4秒の明示代替、末尾4秒遡及代替は採らない。境界はC（2026-09-28、A′を置き換え）。遅延の報告の誤りは止めずに警告する（2026-09-28、見直し）。AAXを重要な対象とする（同、利用者の指示） |
| 基点の統合 | 統合branchはPR #50（9fa244d9）でmainへ入った。本書の参照とINV番号はmainで照合し直す |
| 対応の鍵 | 「連続時計（VST3連続時刻、AU render時刻）+ 較正した差K」を設計の基準とする（2026-09-28決定）。照合にはPREのrun世代を含める（第10版）。各側の呼出しの空白も時計の不連続として扱う（2026-09-28決定。第12版でplugin内で確認）。AUでは候補の食い違いで直ちにKを無効にする（M1、2026-09-28決定。plugin内での実装は未確認）。Kの較正と照合の細則、対応hostの確定はWindowsの実測の後に決める。判定値と較正回数はhost profileの値とする。周回を特定する手段はG1-03で決める。AAXは、pluginが数えるframe数を連続時計にして、Pro Tools Developer 2026.4で成り立った（G1記録第9節） |
| 動的PDC | 変更直後に時計で検出できない短い区間（Studio Pro 8.1.2、2048 framesで最大4 block、171 ms。Pro Tools 2026.4、1024 framesではM1で1〜2 block）の誤対応を許容し、説明書に記す（2026-09-28決定）。中身による停止と、遅延変更を伴う構成での比較停止は採らない。報告しない変更と、再生中に補償を改めないhostは許容の外で、止めない警告と、中身の跳びでPOSTへ倒す規則（INV-LC10）で扱う（2026-09-28） |
| 静かな区間 | infinite tailの報告でplugin sleepを避ける案を本命とする（2026-09-28決定）。bounceの末尾、AU、他のhostでの副作用を確かめてから採用し、副作用があれば無音の後の自動復帰（C）にする。報告する範囲（常に、比較中だけ）も選ぶ。AAXはAlwaysProcessの静的属性なので、常にか、なしの2案 |
| AAX | 重要な対象で、出来る限り完璧に近く他のプラグインより高い精度を保つ（2026-09-28、利用者の指示）。品質目標（第5.4節）を出荷の条件にする。対応の鍵の前提（POSTに補償済みの位置が渡るか）は、Pro Tools Developer 2026.4で成り立った。遅延補償のOFFの間はPOSTを出す案（INV-LC8）。再生中の遅延変更でPOSTへ倒すこと（INV-LC10）と、multi-monoでの提供（INV-LC9）は、2026-09-28に利用者が推奨を採用。他の版やplatformで補償済みの位置が渡らない場合の扱いは、利用者の判断 |
| 製品範囲 | live/固定AB/Blind統合、追従、遡及、host/format、無料範囲の承認状況を確認。mixed-formatは追加候補として別認定 |
| 安全契約 | R-12、減衰と復帰、既存Blindのhost gateは未決。E2とINV-S7、次周回予約とINV-S22、サイズとINV-S25/S38を一貫させる |
| G1で決める方式 | 境界pendingの上限と証拠、次周回予約の方式（live経路上の予約とRT sealを第一候補）、余白付き窓の前提（折返しの報告位置、周回ごとの一致、境界差U）、exact 4秒loopの証拠とadapter、予約締切、動的PDCの観測可能性、IPC、容量、ramp/更新周期/許容差、周回を特定する手段、中身のずれの警告の方式 |
| G1後の追加承認 | E2で解析lease例外が必要か、出荷可能なprofile範囲、infinite tailとAlwaysProcessの副作用の確認結果、補償済みの位置が渡らないhostの扱い。中身のずれの推定を認定や対応の代用にしない |
| G4で観察する操作 | 隠れたowner、再表示時の再準備、Pin予約待ち、loop長の案内、gain増大の復帰承認、小型/英日表示、既存Blind直接入口、PRE待ちの表示の最短時間、Referenceとの振る舞いの違い、中身のずれの警告の文言 |
| E2のowner | unity時にownerを解放し非可聴cacheだけ残す方式を、保持方式とG4で比べる。採用するなら契約変更として承認を求める |

既存Blindのformat gateも同じ時刻根拠を用いるため、新しいliveだけの問題として切り離さない。
提供範囲を縮めたり、必須項目を後続phaseへ送ったりする必要が出たら、事実と選択肢を示して利用者が判断する。
通常のDAW bypassを等音量比較と同等の代替機能とは案内しない。

## 16. 現在地と申し送り

第14版は、厳しめの見直し（[精査記録](hypha_live_chain_compare_review_20260927.md)第0節）の指摘を反映し、AAXの前提とPro Toolsでの実測（G1記録第9節）を加えた改訂である。版ごとの経緯は付録Aに置く。
製品コード、AGENTS、不変条件、README、DAW設定、配布物は変更していない。
G0〜G6、live成立性、性能、実音、installerの検証は未実施である。
G1の実測は、Studio Pro 8.1.2（Intel Mac、48 kHz、2048 frames）のVST3とAUと、Pro Tools Developer 2026.4（Intel Mac、48 kHz、1024 frames）のAAXである。

統合branchは2026-09-28にPR #50でmainへ入った。
INV番号、clock診断、Reference audio、英日表示、画面規則、「ローカルのみ」とした参照は、mainで照合し直す。
残る論点は、hostごとの時計と境界の実報告、到着期限、容量など、実測でしか決まらない。
文書だけの改訂は、実測の結果か、見直しで見つかった誤りに限る。

次の順で進める。

1. AAXの残り: WindowsのPro Tools、製品版（PACE署名が要る。行う前に利用者の確認を得る）、multi-mono、遅延補償のOFF、`GetTODLocation`、hostの通知を待ってから音の遅延を変える型の遅延変更。
2. 同じ装置でのWindows、他のbuffer設定、報告が先に来る型の遅延変更（G1-04）。
3. 周回ごとに印が変わるfixtureでの照合と、遅延がloop長以上の条件（G1-03）。
4. infinite tailとAlwaysProcessの副作用、中身のずれの警告の方式（G1-05）。
5. G1-06の長時間、別process、sandbox。続いてG1-07。

決定済みの方針を変更せずに閉じられない部分は、G1の結果を添えて再判断を求める。
G1のプローブのsource、解析script、CSVは公開repositoryの外にあり、第三者は検証できない。
repositoryへ入れるか（場所、500行の規約、license）を、実装の承認の前に決める。
Notionの現在地、日次ログ、Handoffは利用者の指示により記録せず、引継ぎ内容は精査記録へ残す。

## 付録A. 改訂履歴と確認基点

第14版で、要約、第0節、第16節から版ごとの経緯をここへ移した。
内容は第13版の原文のままであり、当時の基点と判断を示す。今の設計は要約と本文を正とする。

### A.1 版の記録と確認基点


第13版（2026-09-28）: G1R（第12節）で利用者が決めた4件（呼出しの空白の規則、静かな区間、動的PDCの直後、境界規則）を記録した。実測の追加はない。
第12版（同日、1089行、SHA-256 `1cf32a68b64534b901328d14138894d402c56419bc84d6c2d9b2207a1ccb7b81`）は、同じ[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)の第8節（呼出しの空白の規則のplugin内実装、G1-04の限定条件、infinite tail）を反映した版である。
第11版（同日、1074行、SHA-256 `0aa87eb780163be1ebac3a0c41f5d4e0beb7fff2a69f2d0c526d267678b46fd4`）は、同記録の第7節（周回の規則のplugin内実装とplugin sleep）を反映し、対応の鍵に呼出しの空白の規則を加えた版である。
第10版（同日、1053行、SHA-256 `05360dbe6818d4fb513969dd38805ff66592a9637062ebce8d3b41d08fd0cb96`）は、同記録の第6節（G1-02とG1-06のプローブ）を反映し、対応の鍵にPREのrun世代を加えた版である。
第9版（同日、1034行、SHA-256 `537b14894ae9f5223e7b5c2a9a2e7213c1571b0ce0713059c067f7a3af572476`）は、Studio Pro 8.1.2でのG1-01とG1-03の実測を反映し、統合後のmainを確認した版である。
第8版（2026-09-27）: 別セッションが作成した第7版（944行、`_v7`版、SHA-256 `94f91730817e6799a34dfdd65b01eac2b127c35947ec1464ff4767e7229a693d`）を精査し、その本文に追加と補正を加えた。
第7版の原本と、その根拠である[第6版の精査記録](hypha_live_chain_compare_review_v6_20260927.md)は変更していない。
本ファイルの旧内容は第6版（885行、SHA-256 `951026e5c109aa971c338be4a50d2141b8a7d4ca6bdbc5aba152163a6aff0199`）であり、第8版で置き換えた。
第8版の変更根拠と検証は[精査記録](hypha_live_chain_compare_review_20260927.md)の第1節に置く。
第4版に記録した利用者の決定のうち、E1＋E2、100%での復帰操作、Pinの二つの代替と遡及代替の不採用を、本版でも計画上の前提として維持する。境界規則A′は、2026-09-28のG1Rで案C（境界ではPOSTへ切り替え、対応が再び証明されたらPREへ自動で戻す）に置き換えた。

確認基点はローカルHEAD `ee522a536fa7f418ce3247c64eb77ec55d1d8d86`（B-1023）と、remote-tracking ref `5a6a9db5c4d82ee28d5ef26839e782e3bd4882c0`（B-1044を含む）である。
第4版が参照した `83b383a65b6c92a3e6f32f579b8a93cf39088e98`（B-1041を含む）も履歴として照合した。
2026-09-27 14:38 JSTのローカル読取りでは、HEADは83b383a6に対してahead 14 / behind 43、5a6a9db5に対してahead 14 / behind 48だった。
同日15時台には、別セッションが統合branch `claude/local-main-reconcile` で、衝突したB番号の付け替え（B-1015→B-1046、B-1016→B-1047、B-1022→B-1048）と、main checkoutの未commit修正の取り込み（B-1049）を行い、origin/mainとのmergeを進めていた。
第6版の観測後、同branchにmerge commit `8d13800e6d1ff3797e244633cf2b7e75b0a60637`（15:11 JST）が作られた。
第7版の読取りsnapshotでは、同branchは `805ff180822d924d0ec71dd26ffe8466d456d30f`（B-1053）を指していた。
第8版の読取り（16:01 JST）では、同branchは `c60ab6790880240f8a02473ae0a42e1a4b41e42b`（B-1056）まで進み、ローカルorigin/mainに対してahead 22 / behind 0だった。
mainと本worktreeのHEADはee522a53、ローカルorigin/mainは5a6a9db5のままだった。
同時刻の`git ls-remote`では、remoteに同branchは無かった。
統合内容の全面検証、fetch、merge、pushは行っていない。
本書が根拠に使うBlind、admission、pairing、gainのsource（第14節）は、ee522a53、5a6a9db5、c60ab679の三基点で同一blobだった。
2026-09-28 00:33 JST、統合branchはPR #50（merge commit `9fa244d9`）でmainへ入った。
第9版で確かめた上記のsourceは、`9fa244d9`でも同一blobである。
B番号には重複と付け替えがあるため、証跡はcommit hashとfile pathで識別する。

### A.2 版ごとの要約（第8版〜第12版）

- 第8版の追加: loop内の範囲を、折返し後の再開位置から最大callback分以上後ろに置けば、非分割hostでも既存rendererで両sourceが開始・完走する（実rendererへの模擬callbackで確認）。exact 4秒loop（4/4拍子、120 BPMの2小節など）は引き続き境界証拠が要る。
- 第9版の実測: Studio Pro 8.1.2（macOS VST3、48 kHz）はloop境界でcallbackを分割しない。遅延のあるchainのPOSTでは、project時刻が折返しのたびにchain遅延の長さだけ内容とずれる。VST3連続時刻はPREとPOSTで同じ内容に同じ値を持ち、AUのrender時刻はPREとPOSTの差が一定で、どちらもloopで途切れなかった（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)）。
- 第10版の実測: 同じhostで、連続時計とKでPREを引くプローブを動かした。到着は全blockで間に合い（隣接構成の余白は0）、RTの追加は最大30 µs程度だった。VST3ではseekと再生開始の直後に、ringに残っていたseek前のrunの音を、書込み末尾の判定だけで誤って受け入れた。PREのrun世代を照合に含めると、誤受入れ0、正常blockの棄却0になった（記録済みcallbackのオフライン再生）。AUではseekと再生開始をまたいで対応が続いた（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第6節）。
- 第11版の実測: 周回の規則をplugin内で動かし、VST3では事前に決めた条件（誤受入れ0、棄却の説明と件数、旧規則の誤受入れの再現、再計算との一致）をすべて満たした。AUでは、Studio Proのplugin sleep（無音入力が約4秒続くと呼出しを止める。再生中も起きる）でPREとPOSTの差Kが変わり、POSTだけが眠った場合は周回の規則でも8 blockを誤って受け入れた（事前に予測して再現）。呼出しの空白を時計の不連続として扱う規則を加えると、全記録で誤受入れ0になった（記録からの再計算）（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第7節）。
- 第12版の実測: 呼出しの空白の規則をplugin内で動かし、再配置、PREだけのsleep、POSTだけのsleepで、両formatとも誤受入れ0だった（対照のv2の規則は、AUのPOSTだけのsleepで8 blockを誤受入れ）。再生中の遅延変更（G1-04）では、音の変化が時計より2〜4 block（最大171 ms）先に来て、どの時計の規則でもその間の誤対応を防げなかった。infinite tailを報告したpluginは一度も眠らなかった（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第8節）。

### A.3 第6版から第12版までの改訂表

#### 第6版の改善と第7版の補正

| 論点 | 維持する改善 | 第7版で明確にする条件 |
| --- | --- | --- |
| 境界証拠の片側先着 | 有効な対応が続く範囲ではPREを一律に切らない | callback順、host時計の境界、PCM内容の境界を分ける。旧PCMがあるだけでは継続を許さず、reset等をまたぐ証拠の有効性を認定する |
| 次周回予約 | live経路上の予約とRT sealを第一候補にする | 4秒loopでは境界blockを避けられない。sample-levelの分割証拠、事前確保、上書き防止、callback当たりの処理上限が必要 |
| 非表示中の解析queue | enqueueとworkerを停止し、計画的な停止をoverflow扱いしない | 再開時は解析epochまたはgap barrierを更新。旧queueと部分窓を混ぜない |
| E2のowner | unity時に限るowner解放案を、未採用の比較候補として残す | 現行admissionは解析枠と試聴排他を束ねる。解析枠だけの停止/再取得をG2の責務へ加える |
| 固定音の再生 | Source 1完走後のSource 2自動armを維持 | loopへの包含だけでは鳴らない。範囲先頭を含む正確なcallbackと、完走までの連続性が必要 |
| G1実験 | G1-01〜10を維持 | 同一passの独立oracle、遅延がloopより長い条件、保持上限、取得後の実再生までを追加 |
| 基点 | hashで観測状態を区別する | 統合branchのmerge済みsnapshotと、main未反映、統合結果未検証を区別 |

#### 第8版での追加

| 論点 | 第7版 | 第8版 |
| --- | --- | --- |
| 範囲の置き方 | 境界blockを丸ごと避ける方式は、4秒に余裕のあるloopでの限定案とした | 余白付き窓を既定にする。範囲の先頭を折返し後の再開位置から最大callback分以上後ろに置き、終端を折返し位置までに収める。取得は境界blockの折返し後の部分を使わず、再生でも範囲先頭を含むcallbackが位相によらず来る。exact 4秒loopは第7版の条件のまま（第7.2節） |
| 再生の確認 | 非分割境界で範囲先頭を取り逃す反例を示した | 同じ実rendererで、余白付き窓の範囲がadapterなしで両source完走することを確認した（callback長64〜512 frames、200通りの位相）。範囲先頭を再開位置に置くと200通り中193通りで完走しない対照も得た |
| 遡及Pinの再生可否 | 取得の可否だけを判定 | Pin時に再生可否も判定する。範囲先頭が再開位置の余白内にあれば待機し得ることを示し、DAWでの巻戻しか、余白付き窓での次周回予約を選べるようにする |
| exact 4秒loopの頻度 | 記載なし | 4/4拍子、120 BPMの2小節は48 kHzで192000 samplesとなり、4秒のartifactと一致する。珍しい条件として扱わない |
| host認定の現況 | ローカルのみのexact host判定 | 統合候補c60ab679（B-1052）ではこの判定がDebug buildに限られ、release buildは認定hostを返さない。live比較の認定に流用しない |
| 基点 | 805ff180まで | c60ab679（B-1056）まで確認。根拠のsourceは三基点で同一blob |

#### 第9版での実測の反映

| 論点 | 第8版 | 第9版（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)） |
| --- | --- | --- |
| loop境界の分割 | 非分割hostを想定し、G1-03で確かめるとした | Studio Pro 8.1.2は分割しない。境界をまたぐcallbackは、PRE側では折返し前の開始位置で報告された |
| POSTの時刻 | 時計と内容の境界差Uが観測されたら余白に加える | 遅延4096のchainでは、POSTのproject時刻が折返しのたびに4096 samplesの間loop先頭に留まり、内容は折返し前の末尾だった。U = chain遅延 |
| 対応の鍵 | 同一native位置を候補とした | VST3連続時刻はPREとPOSTで同じ内容に同じ値（K = 0）を持ち、AUのrender時刻はPREとPOSTの差Kが一定で、どちらもloopで途切れなかった。「連続時計 + 較正したK」を鍵の設計基準にした（2026-09-28決定） |
| seekと再生開始 | 境界確認待ちとして扱う | POSTの時刻は新位置からchain遅延を引いた位置へ即時に跳び、その間の内容は旧位置の続きか無音だった。対応するPREは現在のrunに存在しない（run世代を照合しなければ誤受入れになることを第10版で確認） |
| 既存Local Blind | 他hostの前提をG1-10で確かめる | POSTのproject時刻を使うため、遅延のあるchainで範囲がloop終端からchain遅延以内にかかると試行が失効する。音はPOSTへ戻る |
| 基点 | 統合branchの採用が未決 | PR #50（9fa244d9）でmainへ入った |

#### 第10版での実測の反映

| 論点 | 第9版 | 第10版（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第6節） |
| --- | --- | --- |
| 対応の鍵 | 連続時計 + 較正したK | 連続時計の値はrunをまたいで再利用される（VST3連続時刻はseekと再生開始でproject位置へ戻る）。鍵にPREのrun世代を加え、PREの現在のrunで書かれた範囲だけを受け入れる |
| seekと再生開始 | 対応するPREが無いので欠落として検出できる | POSTが指すringの格納位置には旧runの音が残るため、書込み末尾だけの判定は誤受入れになった（VST3、chain遅延と同じ2 blockを2回）。run世代の照合で、その4 blockだけを棄却できた（オフライン再生） |
| AUの境界 | render時刻はseekでも途切れない | seekと再生開始の後も、POSTの内容は「render時刻 − K」のPREとbit一致した。A′で解除しない境界の候補 |
| 到着 | 未測定 | このhostの同一process、負荷なし、2048 framesで未到着0。余白はchain遅延と同じで、隣接構成では0 |
| 転送 | 未測定 | POSIX共有memoryの事前確保ringで、callbackあたりの追加時間は最大30 µs程度、p99.9は25 µs以下 |
| plugin sleep | 記載なし | 停止中、editorを表示していないPREへの呼出しがほぼ止まった。再生中の扱いは未確認 |

#### 第11版での実測の反映

| 論点 | 第10版 | 第11版（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第7節） |
| --- | --- | --- |
| 周回の規則 | 記録からの再計算で確認 | plugin内で動かし、VST3で事前条件C1〜C4をすべて満たした。誤受入れ0、棄却はseekと再生開始ごとのchain遅延分（2 block）だけ、旧規則の誤受入れ10を再現、再計算と全block一致 |
| plugin sleep | 停止中にPREへの呼出しが止まった | 無音入力が約4秒続くと、再生中でも呼出しが止まる。editorを表示したinstanceは呼ばれ続けた |
| AUのK | 一定 | sleepで「PREの未呼出しframe数 − POSTの未呼出しframe数」だけ変わる。POSTだけが眠り、ずれがring容量（約10.9秒）内だと、古いKで過去の音を受け入れた（8 block） |
| 対応の鍵 | 連続時計 + K + PREの周回 | さらに各側の呼出しの空白（壁時計）を時計の不連続として扱う。PREは周回を改め、POSTはKを無効にして較正し直す。記録からの再計算で誤受入れ0 |
| host認定 | 中身の照合の必要を明記していない | seek直後の古い音に新しい周回の番号を付けるhostでは、周回の規則が働かない（host模型で確認）。認定は中身の照合で行う |

#### 第12版での実測の反映

| 論点 | 第11版 | 第12版（[G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第8節） |
| --- | --- | --- |
| 呼出しの空白の規則 | 記録からの再計算で誤受入れ0 | plugin内で動かし、再配置、PREだけのsleep、POSTだけのsleep、隣接構成で、両formatとも誤受入れ0。対照のv2の規則はAUのPOSTだけのsleepで8 blockを誤受入れ |
| 動的PDC（G1-04） | 未測定 | 再生中に遅延を4096 ↔ 0と変えると、音の変化が時計より先に来た。VST3は2〜3 block後にPOSTの時計が補正され、AUはKが4096変わった。どの時計の規則でも、直後の2〜4 block（最大171 ms）の誤対応は防げない。AUは候補の食い違いで直ちにKを無効にする規則（M1）で10〜11 blockを3〜4 blockに縮められる |
| sleepを避ける手段 | 未測定 | infinite tailを報告したVST3 pluginは、停止中も無音区間も一度も眠らなかった |

### A.4 各版の位置付け（第13版の第16節）

第7版は、第6版、確認基点のコード、公式仕様を照合した文書改訂と、使い捨てfixtureによる限定的な反例確認である。
第8版は、第7版の主張をコードと独立の模擬callbackで再確認し、余白付き窓、遡及Pinの再生可否、host認定の現況を加えた文書改訂である。
第9版は、Studio Pro 8.1.2（VST3とAU）でG1-01とG1-03の一部を実測し、その結果を反映した改訂である。
第10版は、同じhostでG1-02とG1-06の限定条件をプローブで実測し、対応の鍵にPREのrun世代を加えた改訂である。
第11版は、同じhostで周回の規則をplugin内で確かめ、plugin sleepによるAUのKの変化と、それを防ぐ呼出しの空白の規則を加えた改訂である。
第12版は、呼出しの空白の規則をplugin内で確かめ、G1-04の限定条件とinfinite tailの観察を加えた改訂である。
第13版は、G1Rで利用者が決めた4件（呼出しの空白の規則、静かな区間、動的PDCの直後、境界規則C）を記録した改訂である。
製品コード、AGENTS、不変条件、README、DAW設定、配布物は変更していない。
G0〜G6、live成立性、性能、実音、installerの検証は未実施である。

第5版の主要な指摘（既存future captureの1秒先の開始と4秒上限、非分割境界での範囲の不確定、E2の減衰と資源、INV-S7、Blindの900×600隔離面）は、コードと公式資料で正しいことを確かめ、そのまま維持した。
第6版のsample対応に基づく継続、次周回予約の第一候補、enqueue停止、未採用のowner解放候補、G1実験の具体化は維持した。
第7版では境界証拠の有効性、4秒loopの取得と再生、bounded seal、解析grantの独立寿命、試験oracleを補正した。
第8版では、loopに余白がある場合の範囲の置き方を既定として示し、exact 4秒loopと余白内の範囲だけを、証拠とadapterが要る条件として残した。

統合branchは2026-09-28にPR #50でmainへ入った。
INV番号、clock診断、Reference audio、英日表示、画面規則、「ローカルのみ」とした参照は、mainで照合し直す。
残る論点は、hostごとの時計と境界の実報告、到着期限、容量など、実測でしか決まらない。
文書だけの改訂は、実測の結果が出たときに限る。
次は、決定4件を製品の契約へ落とす準備（R-12、INV、READMEの改定案）、同じ装置でのWindows、他のbuffer設定、報告が先に来る型の遅延変更、infinite tailの副作用の測定、続いてG1-06の長時間と別process、G1-07である。
決定済みの方針を変更せずに閉じられない部分は、G1の結果を添えて再判断を求める。
Notionの現在地、日次ログ、Handoffは利用者の指示により記録せず、引継ぎ内容は精査記録へ残す。
