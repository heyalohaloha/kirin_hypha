# 連続比較の外部調査とHyphaの製品要件

調査・改訂日: 2026-09-27。
改訂: 第5版（同日）。別セッションの実装計画第7版が指摘した三つの一般化（境界直後の区間の長さ、loop内の固定を次周回で聴けること、host画面の既定値）を、実装計画第8版の条件付きの記述に合わせて改めた。
第4版（同日）: 第3版（160行、メインcheckoutの未追跡`_v3`版）を公式資料と現行コードで再照合し、境界、plugin画面、競合資料の記述差を加えた。
同日、実装計画の第6版に合わせて、plugin画面の資料をStudio OneとAvidの公式資料へ差し替えた。
本書は製品比較と設計提案であり、競合の実機試験やHyphaの実装結果ではない。
実装の正本候補は[実装計画](hypha_live_chain_compare_implementation_plan_20260927.md)、指摘一覧は[精査記録](hypha_live_chain_compare_review_20260927.md)に置く。

## 1. 結論

Perception ABは「常に大きい側だけを下げる」方式ではない。
PRE、POST、指定Referenceを基準にでき、POST Channelの既定はPOST基準で、PRE側を増幅する場合がある。
PRE基準について、同じマニュアルはp.8で「clipを起こしにくい」、p.21で「決してclipを起こさない」と書いており、記述が一致しない。
PRE基準でもPOST側を増幅する場合があるため、Hyphaはどちらの記述もclip-freeの根拠にしない。
[公式マニュアル、Loudness Reference Mode](https://www.meterplugs.com/files/perception-ab-guide.pdf)。

Hyphaは「POST基準でmix内の音量を保つ」「増幅が観測TP条件を超えるときは承認付きの減衰を選ぶ」という既存Blindの考え方をliveへつなぐのが自然である。
ただし、調整中の追従、固定AB、匿名Blindは条件が異なる。
PCMとgainの共通化と、試行状態の分離を両立させる。

差別化の候補は、追加の比較pluginを入れず、既存PRE/POSTから調整、固定、Blind、通常復帰までを一続きにすることにある。
「同期不要」「どのDAWでも連続」「誤対応0」は、現在の証跡では販売上の約束にできない。
再生位置を動かしたときにplugin内部がresetされ得ること、主要hostにplugin画面を置き換える設定があり一部の公式資料がそれを既定と説明することは、連続比較の使い勝手を左右する外部条件である（第5.4節、第5.5節）。

## 2. 根拠の読み方

公式マニュアルは公開された動作説明、利用者投稿は当人の体験、Hyphaへの提案は本書の設計判断として分ける。
利用者投稿から発生率、現行版の不具合、原因まで推定しない。
第三者の音量事故の投稿を、特定製品への一般的な評価に使わない。
文書の版と配布pluginの版は別に記録する。
今回の外部調査は公開資料の照合であり、有償製品の購入、インストール、実機評価はしていない。
第三者の解説記事と利用者forumの回答は、公式資料と区別して扱う。

## 3. 六製品の対処

| 製品と資料 | 音量基準と更新 | 同期・保護・適用範囲 |
| --- | --- | --- |
| [Perception AB、24ページの公式guide](https://www.meterplugs.com/files/perception-ab-guide.pdf) | PRE / POST / Ref。手動Matchと毎秒のAuto Match。Autoは記載上のgain differenceが6 dB超なら手動を要求 | 前後のpair。Sync中にmuteし、latency変更後は再Syncを案内。最終exportでは無効化する用途。複数projectを同時に開くとpairを取り違え得るとして一つずつの使用を求める |
| [LetiMix GainMatch、公式manual](https://www.letimix.com/products/gainmatch/manual)、[更新履歴](https://www.letimix.com/products/gainmatch/update) | 通常Afterを補正。Listen GainではBeforeをAfterに合わせる。Manual/Auto、既定K-weighted RMS 2秒、AUTO許容差±1 dB | delay自動/手動、短いfade、正負gain上限、Ear Protection、低レベル比較停止。別track転送と追加delay/PDC申告は、manual自身が実験的用途と書いている |
| [TBProAudio ABLM2、公式manual](https://www.tbproaudio.de/assets/content/manuals/ABLM2%20manual.pdf)、[製品ページ](https://www.tbproaudio.de/products/ablm) | 通常POSTを補正、InverseではPRE試聴時にPREを直前のPOST音量に合わせる。Manual/Auto、6計測方式、gain上下限と許容差 | 同一trackを基本にPDC自動検出またはsample手入力。manual p.7は異なるformatのsenderとreceiverが通信できないと書く。Over protection、短いRMS窓の可聴gain変化への注意 |
| [REFERENCE 3 + REFSEND、公式manual](https://www.masteringthemix.com/pages/reference-manual) | Original基準、Quietest、−14 LUFS short-termを選択。リアルタイム追従とgain表示 | REFSENDでchain比較、ファイルReferenceも扱う。Track Align/手動整列。Original側を変えるモードはbounce前の解除に注意 |
| [ADPTR Metric AB、製品と変更履歴](https://www.plugin-alliance.com/products/metric-ab) | A→B、B→A、指定LUFS、最も静かなtrackの4方式 | 主用途はlive mixとReferenceファイル。pair型chain比較と同一視しない。公開資料だけで追従周期やclip保護の詳細は確定できない |
| [iZotope Ozone 12、公式日本語解説](https://izotope.jp/jp/news/251215-mix-tutorial/)、[公式export注意](https://www.izotope.com/community/blog/how-to-use-master-assistant-in-ozone) | 処理後を入力に合わせる方式と、bypass時の入力を処理後へ合わせる方式。継続的gain調整 | Ozone内部chainの比較。任意の外部insert間のpairとは異なる。処理後補正ではexport前の解除を案内 |

### 3.1 何を「保護」と呼んでいるか

Perception ABの6 dB条件の原文は「gain difference」であり、前回更新からのstep幅か必要補正量かを公開記述だけでは確定できない。
これをHyphaの±6 dB仕様の根拠にしない。
Auto停止とtrue-peak limiterは別の動作である。

GainMatchのSuspend comparisonは、機能有効時の低レベル判定の既定閾値が−60 dBで、変更できる。
常時ON、あるいは−60 dBの出力ceilingという意味ではない。
Ear Protectionは急な増大を抑える説明であり、公開manualにtrue-peak上限の保証は見当たらない。
無音復帰後にRMS窓の情報が集まるまで待つ設定もある。
[公式manual](https://www.letimix.com/products/gainmatch/manual)。

Hyphaもgain上限、無音停止、観測TP判定、sample guard、終了時の音量復帰を区別する必要がある。
増幅を避ける減衰方式も、元から過大な入力や下流の処理まで安全にする保証ではない。

### 3.2 資料の版

GainMatchは調査時の配布版が1.53だが、現行公式リンク先のGoogle文書本文は2020–22表記とv1.4動画を含む。
本書の詳細はその公開manualと更新履歴を区別して参照し、1.53専用manualと呼ばない。
ABLM2も配布版2.2.13と、manualの2023年表記を分ける。
Metric ABの4方式は現行1.5（2026-07-29）でも使えるが、導入は変更履歴上の1.4.0（2023-08-04）であり、1.5の新機能ではない。
Perception ABのguideは画面の表記がv1.5.0である。
今後の実装着手時に配布版と必要なAPI資料を再確認する。

## 4. 利用者の痛みとHyphaの答え

| ID | 確認した声や資料 | 読み取れる課題 | Hyphaが答える要件 |
| --- | --- | --- | --- |
| P1 比較のためにmixが変わる | [KVR、2022-10-21](https://www.kvraudio.com/forum/viewtopic.php?start=45&t=552477)で、Afterを下げずBeforeを上げ、mixの中で比較したいという要望。現行GainMatchにはListen Gainがある | 等音量にするだけでは、他の楽器との相対音量を守れない | POST基準を既定候補とする。POSTを下げる承認付き減衰を明示選択にし、POSTが下がる量を適用前から示す |
| P2 突然大きくなる | [DOA、2021-06-24](https://www.dogsonacid.com/threads/gainmatch-and-similar-plugins.813104/)に、HoRNet製品で音量が大きくなった体験。製品名、版、原因は未確定。GainMatchの公式資料には無音停止と保護がある | 静かな区間への追従と、その後の復帰を別々に考える必要がある | 無音、片側欠測、古いgain、急変を試験。上限に当たった比較を「一致」と偽らず、適用を止めて実出力と復帰時の音量差を示す |
| P3 比較の準備が面倒 | [Steinberg Forums、2021-01-11](https://forums.steinberg.net/t/feature-request-vst-gain-trim-should-the-base-vst-frame-protocol-have-gain-trim-yes/685797)で、二つのinsert枠、設定時間、個別presetに補正が含まれないことを指摘 | 正しく比較する作業そのものが制作を遮る | 既に置いたHypha PRE/POSTを再利用し、専用sidechainや追加pluginを不要にする。新規利用者には依然PRE/POSTの二枠が必要。隣のpluginを開いたときの画面置換も手間になる |
| P4 音ズレを音質差と誤認する | [KVR、2021-08-31](https://www.kvraudio.com/forum/viewtopic.php?start=45&t=552477)に、oversampling変更後のDeltaの違和感と再検出での解消報告 | latencyの変更で古い同期が無効になる。これはDeltaでの報告であり、単純切替の同じ不具合を証明しない | 同期の有効性をpair/runに結び付ける。変更後に古いPCMで比較を続けない。対応を失った試聴は中断を明示し、通信回復だけではPREを再開しない |
| P5 Undoが使いにくくなる | [GainMatch更新履歴](https://www.letimix.com/products/gainmatch/update)に、2025年のautomation/Undo対策、2026年のReaper履歴削減。ABLM2§8.20にも自動gain保存のUndo増加への設定がある | gainを追従させるたびに制作側の編集履歴を増やすと、元のplugin編集を戻しにくい | 試聴状態とhost parameterを分離。切替と追従を繰り返しても隣接pluginのUndoを妨げないことを実hostで試験 |
| P6 書き出しが聴いた条件と違う | [Ozone 10の利用者投稿、2023-01-23](https://www.reddit.com/r/iZotopeAudio/comments/10js0w6/gain_match_on_or_off_before_bouncing/)に、Gain Matchを切るとrenderが大きくなった報告。現行Ozone公式も解除を案内 | 試聴用の補正と納品用の音量を混同する | 一時補正を保存音源と正本Recordへ混入させない。offline通知時は入力不変。通知のない実時間printは自動検出できると約束しない |
| P7 数値が合っても耳では違う | [Reddit、2023-04-26](https://www.reddit.com/r/audioengineering/comments/12zy1af/)に、kickやsnareでAuto適用後のBeforeが大きく感じるという疑問 | 計測方式と活動区間の違いを、計算ミスや音質の優劣と混同しやすい | TRACK/STEMと2MIXのpolicyを分け、補正量と残差を示す。「同じLUFSだから同じ知覚」とは表示しない |
| P8 視覚の目標が判断を支配する | [Metric ABの利用者投稿、2021-01-17](https://www.reddit.com/r/AudioProductionDeals/comments/kza9la/)に、グラフを合わせるために編集してしまわないよう注意する声 | 比較の道具が、曲の良し悪しを決めるものに見える | 聴取用の簡潔な面と既存Blindへの入口を用意する。優劣、改善点数、好みの正解を自動判定しない |

以上は困り方の存在を裏付ける資料であり、利用者全体の順位や頻度を表す調査ではない。
メーカーの更新履歴は対策を行った証拠であって、現在の全hostでの再現試験結果ではない。
無断で当事者へ連絡したり、投稿したりしていない。

第3版の再照合では、DOA、Steinberg Forums、Ozone投稿の本文を確認した。
KVRは403、P7/P8のReddit本文は取得できなかったため、これらは第2版からの継承情報として保持し、独立再確認済みとはしない。
投稿の日付は第2版の記載であり、取得画面の相対時刻を根拠に再確定していない。
P1は公式のPOST基準/Listen Gain、P4は公式の再Sync案内からも製品要件の必要性を支持できるが、投稿の再取得の代わりにはしない。
P7/P8を含む痛みの頻度と重要度は、Hypha利用者の観察試験で検証する仮説として扱う。

## 5. 同期と操作環境について何が分かったか

### 5.1 競合の公開説明と未知の内部方式

Perception AB、ABLM2、GainMatchは自動検出と手入力を案内している。
Perception ABはSync中のmuteとlatency変更後の再操作、ABLM2はchainを中立に近づけると検出しやすいことを説明する。
GainMatchは遅延の自動検出で極性も検出すると書いている。
ただし、この説明だけで三製品がhost情報を一切使わない、または特定の相関アルゴリズムを使うとは断定しない。
[Perception公式guide](https://www.meterplugs.com/files/perception-ab-guide.pdf)、[ABLM2公式manual](https://www.tbproaudio.de/assets/content/manuals/ABLM2%20manual.pdf)、[GainMatch公式manual](https://www.letimix.com/products/gainmatch/manual)。

Metric ABの公式変更履歴は、1.4.0（2023-08-04）の「PDC Mode」を、Logic Proでbus trackを有効にした場合など、hostが正しいPDC情報をpluginへ渡さない場合の手動補正と説明している。
これはメーカー側の特定条件についての報告であり、Logicが全pluginへ与えるsample位置の一般仕様ではない。
一方で、同じDAWでもrouting条件によって時刻の意味が変わり得ることを示す資料として、Hyphaのhost認定をrouting条件別にする根拠にする。
[公式変更履歴](https://www.plugin-alliance.com/products/metric-ab)。

### 5.2 HyphaのB1が証明した範囲

[B1の正本](../hypha_b1_host_observation_20260907.md)は、特定のStudio Pro、format、rate、固定4096-sample delayにおける完成したPRE/POST captureの一致を記録している。
当該条件の同一native位置対応を支持するが、live出力の期限までにPREが到着する証明ではない。
loop、seek、latency変更中の対応や現在のcommitの受入を代用しない。

role-localなframe counterを直接比較できないことは、ローカルmainの計測Δ照合（`meter_chain_join.rs`、origin/main未統合）にも明記されている。
例えばPRE256 framesに対しPOST64 framesが4回なら、欠落なしでも公開末尾と消費counterとの差は変化する。
直列であることから同一callback分割や固定差を導けない。

[VST3 ProcessContext](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ProcessContext.html)ではloop境界でblockを分割することも必須ではない。
[JUCE AudioProcessor](https://docs.juce.com/master/classjuce_1_1AudioProcessor.html)もcallback長が変化すると説明する。
[Abletonの公式PDC FAQ](https://help.ableton.com/hc/en-us/articles/209072409-Delay-Compensation-FAQ)には曲位置に依存する処理の制約があり、全hostが同じ意味の時刻を渡すとは推定できない。

Hyphaでは、sample対応、到着期限、境界とPDC変更の三条件を独立に測る必要がある。
内部遅延が変化してもclock/counterが同じなら、その観測だけでは変化を識別できない。
最初の検出可能な不整合で止めることと、最初の影響sampleを必ず防ぐことは別である。
manual Syncを不要にできるかは、この未実証部分の結果次第である。

### 5.3 shared memoryの条件

確認した[Apple XNU header](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/sys/posix_shm.h)と[実装](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/kern/posix_shm.c)では、名前上限はNULを除く31 bytesである。
文字数とbyte数を混同しない。
割当済みshmへの再ftruncate拒否もAppleの実装で確認できる。
永続的な全macOS保証とせず、対象OSで実測する。

[App Groups](https://developer.apple.com/documentation/BundleResources/Entitlements/com.apple.security.application-groups)の命名だけで第三者AU hostのsandbox権限は得られない。
[Apple TN2247](https://developer.apple.com/library/archive/technotes/tn2247/_index.html)も踏まえ、host process、entitlements、AUの形態を区別する。
file-backed mappingもsandboxを自動回避する手段ではない。
[Windowsの公式mapping説明](https://learn.microsoft.com/en-us/windows/win32/memory/creating-named-shared-memory)はpagefile-backed方式の候補を支持するが、RT-safeな所有権protocolの完成を意味しない。

### 5.4 再生位置の移動とplugin内部のreset

[Apple Audio Unit Programming Guide](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/TheAudioUnit/TheAudioUnit.html)は、Resetを、再生位置を動かしたときに残響の減衰などが新しい位置の再生へ干渉しないよう内部状態を戻す処理と説明している。
HyphaのJUCE submoduleでも、VST3 wrapperは`setProcessing(false)`で、AU wrapperは`Reset`でpluginの`reset()`を呼ぶ。
Hyphaの非出荷fixtureであるPDC Validation Delayも、`reset()`でdelay bufferを消す。

したがって、遅延を持つchainでは、seekや停止後の再開の直後に、resetされた無音や前の位置の残りがPOSTへ届き得る。
その区間の有無と長さは、pluginのreset実装、hostのPDC scheduling、prerollで変わり、チェーン遅延と同じ長さになるとは限らない。
その区間の処理前の音を、PREは新しい再生位置では処理していない。
境界の直後に一度もPOSTを出さない連続比較は、一般には保証できない。
これは第3版の「PRE不足は一律に中断」を境界にも当てはめると、seekのたびに比較が止まることを意味する。
実装計画の第5.3節で、境界と故障を分けて扱う三案を示した。

HyphaのReference通常B試聴は、停止、位置不明、準備未完了でBの選択を解除してAへ戻し、再開には再選択を要する（origin/mainの`ReferenceRuntimeV2Realtime.cpp`）。
境界で選択を解除し、同じsessionのまま1操作で選び直せるようにする案は、この既存の考え方と揃う。
実装計画ではこの案（A'）を採用した（2026-09-27）。

### 5.5 主要hostのplugin画面

調整しながら比較するには、Hyphaの画面と調整するpluginの画面を行き来する。
主要hostには、別のpluginを開くとplugin画面を置き換える設定がある。
Studio One 4.1の公式manualとAvidのEuControl資料（Pro Tools）はそれを既定と書くが、現行Studio Pro 8の既定とLogic Proの工場既定は確認できていない。

| host | 画面が置き換わる条件 | 画面を残す方法 | 資料と範囲 |
| --- | --- | --- | --- |
| Studio One / Studio Pro | Studio One 4.1 manualでは、既定で開いたInsertのUIを一つの画面だけで表示する | 画面右上のPin。Pinした画面は残り、次のInsertは新しい画面で開く | [Studio One 4.1公式manual](https://pae-web.presonusmusic.com/downloads/products/pdf/Studio_One_4.1_Reference_Manual1.pdf)の「Navigating Inserts」。現行Studio Pro 8の既定値は実機で確かめる。[利用者forumの管理者回答（2025-07-13）](https://studiooneforum.com/threads/how-can-i-handle-seeing-plugins-in-separate-windows.1118/)も同じ操作を説明する |
| Logic Pro | Linkが「シングル」のとき一つの画面を使い回す。設定はproject全体に適用される | Linkを「オフ」にする | [Apple公式ユーザガイド](https://support.apple.com/ja-jp/guide/logicpro/lgcpbc21a1fd/mac)。「シングル」が工場既定かは公式記述から確定できない |
| Pro Tools | Targetが有効（赤）な画面は、次に開いたpluginへ置き換わる。既定ではTargetが有効な状態で開く | Targetを無効にする。Shiftを押しながら開く | [Avid Using EuControl Surfaces v2025.12](https://resources.avid.com/SupportFiles/ProMixing/Using_EuControl_Surfaces_v2025.12.pdf)のp.151「Tip for Managing Pro Tools Windows」 |

HyphaのLocal Blindは、editorを閉じると試聴を終える契約である。
この契約をliveへそのまま当てはめると、画面を残す設定をしていない利用者は、隣のEQを開くたびに比較が止まる。
実装計画の第8節で、固定方法の案内と、可聴比較だけを終えて準備を保持する案（E1+E2）を採用した（2026-09-27）。
各hostの版や設定によって挙動は変わり得るため、G4の観察試験で確かめる。

## 6. Hyphaに採用する設計提案

| 利用者へ返す価値 | 設計上の対応 |
| --- | --- |
| mixを崩さず比較する | POST基準を既定候補にする。POSTを下げるときは理由と量を示して承認を求める |
| 判断中に補正が動かない | 固定補正と明示的な追従補正を分ける。UI名は既存FOLLOW/HOLD/LIVEとの衝突を避ける |
| 音源が勝手に変わらない | PRE不足では比較中断を示し、POSTを混ぜながらPRE表示を続けない。境界ではPREの選択を解除し、同じsessionで1操作で選び直せる案（A'）を採用した |
| 同じ素材でBlindへ進める | exact snapshotとgainを共有する。ただし匿名trialの割当、heard、回答は毎回初期化 |
| 固定した音を迷わず再生する | RT Pin終端と表示範囲を一致させ、DAW巻戻しを明示。loop内の固定は、範囲先頭を含むcallbackが来る場合に限り次の周回で聴ける。範囲を折返し後の再開位置から最大callback分以上後ろに置けば、非分割hostでも既存rendererで開始できる（実装計画第7.2節）。exact 4秒loopは境界証拠が要る。折返しをまたぐ場合は観測範囲の次周回取得を明示選択にする。次4秒への自動代替をしない |
| 調整を遮らない | 各hostの画面を残す方法を入口で案内する。editorを閉じたら試聴を終える契約は保ち、可聴の比較だけを終えて準備を保持し、再選択を1操作にする案（E1+E2）を採用した |
| 書き出しを汚さない | raw計測/Record分離、offline入力不変、実時間print前の明示Return |
| 道具に好みを決めさせない | 選好なし、区別できない、保留を維持し、優劣点数や改善証明を出さない |

「同じ4秒の素材」と「その4秒を過去に聴いた出力」は異なる。
追従gainが動いた後に固定gainで聴くなら、その違いを説明する。
固定ABとBlindの間ではPCM、gain、再生位置、transition条件を一致させる。
Blind画面にはsource名、source別gain、波形を残さず、正体を漏らさない。
これが、既存AB Blindを単なる付加機能でなく、調整から判断までの共通基盤へ育てる方向である。

## 7. 次に確かめること

最初に、ローカルmainとorigin/mainの分岐を解消し、本書と実装計画の参照を統合後のmainで照合する。
その後にG1で、時刻対応、deadline、loop/seek/停止再開の境界直後の未証明区間、PDC変更の観測可能性、共有memory到達性を測る。
続いて、POST基準と減衰時のmix内体験、短loop/疎な打音の推定、追従の可聴変化、固定音の再生手順、各hostの画面置換による中断を観察する。
同時にpayloadの単一所有化とピーク容量を確認し、仕様を広げる前に成立条件を確定する。

本書の文書改訂で技術的障害が解決したとはしない。
製品コード、実機、配布は未変更。
