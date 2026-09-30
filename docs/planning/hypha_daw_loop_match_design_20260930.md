# DAWループ再生と固定MATCH保持の設計案

2026-09-30。利用者が指定した主用途は「DAWのループ再生に合わせ、MATCHを保ってPRE／POSTを聴き比べる」。
直前の中断理由・復帰案内・本文配置の構造修正は別の未commit作業であり、この検証commitには含めない。
本書はその次の設計案であり、LOOP対応の実装・契約改定・実機検証を完了したものではない。
利用者の実装指示後、下記の成立性fixtureを追加した。現行Consumerに整数周回ずれを再現したため、
製品のLOOP継続判定・MATCH保持・INV-LC19は未変更。初回の対応取得と、既知Kからの継続を分けて検証する。

推奨は、LOOP専用モードを増やさず、確定した音量差と現在の音声対応を分けて扱うこと。
正常と確認できた折り返しではMATCHを保持する。確認不能な音をPREとして出すことや、
毎周の自動MATCHで音量差を動かすことで継続を装わない。

## 利用者の動線

`LISTEN → MATCH → DAWでループ再生 → PRE／POST切替` を主動線とする。
すでにループしている場合も、対応を確認できる範囲ではそのままMATCHできることを受入目標に含める。
Hypha用のLOOPボタン、範囲入力、周回ごとの承認は追加しない。DAWの範囲や再生位置も操作しない。

- 同じループの折り返しで、確定済みのPRE gain・POST減衰・ceilingを変えない。
- 対応を一時確認できないときは、保持している音量のPOSTへ退避し、PRE選択とMATCH値を残す。
  確認できた最初のblockから既存の遷移でPREへ戻る。通常の折り返しに再MATCHを要求しない。
- 再MATCHは利用者の明示操作だけで行う。新しい測定と必要な承認が成功するまで前回値を維持し、
  無音・信号不足・キャンセル・古い測定結果の棄却では前回値を消さない。
- 明示的な終了は、従来どおりMATCHを解除して通常のPOST音量へ戻す。上昇量と実RT完了を示す。
  LOOP対応を理由に終了の意味を「MATCHへ戻る」へ変えない。
- 「MATCH保持」は測った音量差を固定している意味であり、全周回の現在のラウドネスが常に等しいという意味ではない。

## 明示操作と自動継続の境界

利用者から、専用の明示操作を経てloopを許可する案と、追加操作なしで継続する案の正確性について確認があった。
設計上の推奨は後者を維持する。ただし自動化するのは、利用者がすでに開始した比較の、確認済みloopでの継続だけである。
DAWがloop中というだけで比較を開始せず、接続・editor open・state restoreでもPREやBLINDへ切り替えない。

専用の許可操作は対象や継続意図を明確にする利点があるが、音声の時刻対応・周回・PDCの正しさを保証しない。
利用者が範囲を選んでも、その範囲のどの周回をPOSTが処理しているかは別に確かめる必要がある。
したがって許可ボタンを追加しても内部の安全判定は同じだけ必要であり、未検証の継続を利用者承認で通す設計にはしない。

明示操作は、LISTEN／BLINDによる比較開始、PRE／POST選択、再MATCHの要求、必要な追加減衰の承認、終了と通常音量復帰に残す。
同じ比較内の正常な折り返しには、追加クリックも周回ごとの確認も要求しない。
確認が崩れたときは理由を表示して退避・中断し、「自己責任で続行」のような安全判定の迂回を用意しない。
必要なhost情報が足りない条件は、未対応・確認不能として扱う。明示モードを追加して対応済みに見せない。

## 正確性の合格条件

正確性は確認ボタンではなく、独立した期待値と実際の出力で検証する。
各周回で異なるPCMを使い、期待するPREのsample位置・周回ID・gainと出力を照合する。
正常条件だけでなく、同じ見かけの位置へ飛ぶseek、短loopより長い遅延、古いrun、欠損、PDC変更を含める。
安全判定を試験と共有して正解を作るのではなく、別の遅延モデルから期待値を生成する。

- 誤った周回・位置のPRE出力、未確認PREの遷移混入、未承認の音量上昇は試験内で0件を必須とする。
- 全部POSTへ退避させて誤出力0を達成しても合格にしない。認定正常fixtureでは、指定したSourceが
  実際に鳴ること、100周継続できること、意図しない再MATCH・試行中断がないことを確認する。
- 実hostではPRE出力時間と待機時間・回数・理由を別々に記録する。毎周の待機で比較が成立しない条件を
  「安全だから使える」と扱わず、対応条件と有限の復帰時間を示す。
- 単体fixture、実processor/editor、対象hostのexact candidateの三段階を通す。負荷・0 samples・通常Aのbit同一も別に確認する。

有限の試験で全環境の誤り0を証明したとは言わない。未知の条件では推測で進まない実装と、
検証済みhost／format／buffer／遅延条件の記録を併せて出荷判断する。

## 現行実装で絡み合っている責務

`PluginProcessorLiveCompareRealtime.cpp`はproject位置の不連続をすべて`block.afterGap`へまとめ、
`timelineGeneration`と`sessionGeneration`を進めて`matched=false`にする。
`clock.looping`は取得しているが、live比較の不連続判定には使っていない。
したがって折り返しでもMATCHの有効表示が消え、LIVE BLINDは中断する。
gainとPOST減衰の数値自体がその場でunityへ消えるわけではない。この違いを表示にも反映する。

同じ`afterGap`がConsumerのKとPOST測定履歴をリセットする。
`computeMatch`は既定で最低3秒、最大4秒の履歴を読むため、短いループでは履歴が貯まらない。
`matched=false`を止めるだけでは、短ループの初回MATCH、承認待ちの失効、BLINDの中断、
AUTOの停止、PINの範囲契約のどれも整理できない。

## 三つの責務を分ける

汎用状態管理基盤は作らず、既存のAuthority・Completion・commandを保った小さな分離にする。

| 責務 | 所有する事実 | 折り返し時の扱い |
| --- | --- | --- |
| 確定済みMATCH | PRE gain、承認POST減衰、ceiling、完全MATCHかTP LIMITか、pair／format／承認世代 | 正常折り返しでは変更しない |
| 現在の音声対応 | PRE run、連続時計、K、対象範囲の到着、欠落、設定されたloopの情報 | blockごとに判定。MATCH値だけでPREを許可しない |
| 測定窓と試行 | 同じ対応で読める履歴区間、未承認の測定結果、BLINDの割当と聴取receipt | 確認済みの折り返しだけ継続。未知の境界を結合しない |

MATCHには「なし」「固定値を使用可能」「固定値は保持しているが再確認が必要」を区別する小さな状態を持たせる。
さらにPRE WAITは現在の出力状態として別に表示する。「値がある」「比較条件が有効」「PREが鳴った」を
一つのboolへ戻さない。試聴sessionを跨ぐ永続保存や、別pairへの承認移植は追加しない。

既存generationの意味を整理し、session／承認の失効と、測定窓の区切りを別扱いにする。
ただし独立serialを多数追加するのではなく、Authority世代、gain revision、窓のproof世代を再利用する。
古いcallback・承認メニュー・worker結果は採用した世代を照合し、後のMATCHやENDを上書きできない。

## 境界ごとの動作

| 起きたこと | MATCHと出力 | LIVE BLIND |
| --- | --- | --- |
| 確認済みの同一loop折り返しで全frameが対応 | 固定値と選択を維持。Kも根拠が続く限り維持 | 同じ割当・固定gainで継続可能 |
| 同じloopだがPREの到着等を一時確認できない | 値と選択を保持しPOSTへ。理由と自動復帰を表示 | POST代替をSourceとして数えず、その試行を理由付きで中断 |
| 停止・手動seek・loop範囲変更・未知の位置飛び | 値は保持し、窓と未承認案を失効。有効MATCHと断定せず再確認を表示。記名PREの復帰は既存の対応確認規則に従う | 試行を中断。同じ割当を自動再開しない |
| PDC無効・content hold・callback空白 | MATCH保持と出力許可を分離。既存の停止／再生等の復帰手順を維持 | 既存の安全中断を維持 |
| pair／format変更・state restore | 旧MATCHを新条件の承認として使わない。保持POST減衰の安全な解除手順は維持 | 旧試行・開示・承認を復活させない |
| 明示END／RETURN | 比較とMATCHの使用を終了し、上昇量を示して実unity完了を待つ | 終了。MATCHへ暗黙には戻さない |

音声処理設定の変更をHyphaがすべて検出できるとは扱わない。
他社pluginのパラメータを監視したり、変更を推測して自動でMATCHし直したりしない。
既存の内容ずれ警告、ceiling guard、報告されたPDC条件を維持する。

## LOOPの認定と音声の対応

### 必要な情報

HostProcessClockへ、取得できるloop有効状態、loop points、PPQ、各値の有効性を固定サイズで渡す。
PREとPOSTの両側で観測し、未知の情報をfalseやゼロの既知情報に置き換えない。
各wrapperの取得結果の信頼性も区別する。現在のbool単独では、情報不在と明示OFFを区別できない。
標準JUCEで取得成否が分からないboolは、そのまま「既知OFF」へ昇格させず、loop認定の根拠なしとして扱う。
まず既存wrapperが渡す情報で成立させる。追加のnative provenanceが必要と分かった場合だけ、
対応するJUCE patch stackと時計probeの変更を影響範囲へ含め、推測で有効性を補わない。

LOOP ON、project位置の後退、連続時計が進んだという三点だけで正常折り返しとは認定しない。
LOOP ONのまま手動seekでき、AUのrender時計やAAXの自前frame時計はseekしても進むからである。
認定は、以下の証拠が揃ったhost profileと条件だけで行う。

- pair、format、PREの連続run、両側のcallback連続性、出力owner、PDC条件が維持されている。
- 同じloop設定に対する移動で、実測で確認した非整列境界・POST位置の通知パターンに収まる。
- 現在のKに対応するPRE範囲が同じ連続実行に存在し、旧周回の音へ取り替えていない。
- loop範囲変更、異常な時計差、既知の遅延変更、content holdなどの反証がない。

PPQとその瞬間のBPMだけからexactなsample境界を作らない。テンポ変化や丸めに加え、
hostが境界でcallbackを分割しない場合がある。loop情報は境界判定の補助であり、PCMの索引は連続時計とKのままにする。
境界を含む全blockを検証できないときは、未検証PREをcrossfadeに混ぜずPOSTへ退避する。

正常wrapとseekで全観測値が同じになる反例があるなら、その情報だけの判定器では区別できない。
fixtureでたまたま正常例が通ることを認定根拠にせず、追加の観測が得られない条件では継続認定しない。
この場合も固定MATCH値は失わせないが、常に待機なしでPRE／BLINDを継続できるとは約束しない。

### Kを毎周作り直さない

一意に較正済みのKと連続runが維持される正常折り返しでは、その根拠を引き継ぐ。
project時刻の後退だけでKを消して、同じproject位置の最新PREへ結び直さない。
既存ringの`run`は連続書込みの世代であり、DAWのloop周回番号ではない。この二つを混同しない。

既存G1記録では、Studio ProのPOST位置が折り返し付近で先頭に留まる間も、連続時計のKは一定だった。
このような区間は「較正候補として使えない範囲」をhost profileで限定し、偽の候補を作らない。
LOOP中という理由で候補不一致の規則M1を丸ごと無効化してはならない。
範囲を外れた不一致や期限超過は既存どおりKを失効させる。
遅延変更直後の検出限界は既存契約より都合よく拡大せず、境界付近の変更も試験する。
境界確認にはsample時計で進む有限の期限を設け、profileで確認した遅延・block条件から上限を決める。
期限不明のprofileは採用しない。実時計停止時の操作案内は既存UI周期で行い、新timerを足さない。

### 短いloopと長い遅延

loop長以上のchain遅延があると、同じproject位置に複数周回の候補が存在する。
現在の「最新の一致候補」を選ぶ方式だけでは、整数周回ずれたKを排除できない。
固定の識別PCMが各周で同じなら、この誤りはPCM一致試験でも見逃す。

初回Kの確定と、すでに確定したKの引継ぎを分ける。
loop中に初めて開始する場合は、当該hostの遅延・先行処理の境界と周回を識別できる根拠が必要。
単に探索範囲256件の中で候補が一つだったことを、全体で一意だった証拠にしない。
過去の非loop区間で得たKも、その後に停止・空白・失効があれば再利用しない。

一意性が確認できない場合は推測でPREへ進まず、
「このループではPREの対応を確認できません。範囲を広げて再生してください」など、理由と実行できる対処を示す。
これは全hostへの固定制限ではなく、必要な証拠がない場合のfallbackである。
loopを一度解除する回避策だけで「loop中からMATCHできる」を合格扱いにしない。
具体的な認定述語とprofileの数値は、後述の成立性fixtureで確定する未解決の技術ゲートである。

## 短いloopでもMATCHの窓を貯める

正常と確認できた周回は、project座標を平坦化した偽の4秒ではなく、
PRE／POSTの実際の連続再生時間として既存の履歴へ蓄積する。
2秒loopで現行の最低3秒を測る場合も、コピーを水増しせず実際に鳴った3秒を使う。
固定回数の3周聴取やSourceごとの録り直しを求める設計にはしない。

測定可能な区間は、全frameが同じ対応根拠で読める連続区間に限定する。
履歴の終端だけでKが有効でも、窓の途中に未知の境界・K変更・PRE run変更・欠落があればその窓を使わない。
固定サイズのproof区間情報を既存履歴へ添え、copy前後の世代・run・上書きを照合する。
未知の区間を削って前後を結合したり、前周のPCMで穴埋めしたりしない。

この区分はMATCH、AUTO、中身のずれ推定が共有する。
正常loopだけで内容ずれの基準をリセットし、遅延変化の警告を隠すことはしない。
周期信号で相関が別周期へ移った可能性は、既存の判定不能規則とfixtureで確認する。

承認待ちの新MATCH案も、その窓とpair／format／出力承認の世代に結び付ける。
同じ正常loopが回っただけなら承認を破棄しない。seekや設定変更など窓の前提が変われば新案だけ棄却する。
新MATCHはPOSTを上げない。より浅いPOST減衰への変更が必要なら、現在のEND／RETURN契約に従う。

PIN／Exact 4 Sは別のnative範囲契約である。loopを跨いだMATCH窓を単一範囲artifactとしてPINへ渡さない。
project座標の連続区間を調べる既存ProjectViewは分離して維持する。

## LIVE BLINDとAUTOの扱い

主用途は記名PRE／POSTだが、同じMATCHからBLINDに入った途端に正常loopで止まる不整合も設計対象に含める。
LIVE BLINDは、認定済みloopかつ全blockの対応・固定gain・guardが成立した場合だけ継続する案とする。
匿名割当、Source選択、聴取receiptを周回ごとに作り直さず、開示で音やMATCHを変えない。
これは同一sample・同一処理結果の比較を保証する方式ではない。残響・LFO等は周回で変わり得る。

一度でも実際のPOST代替や未知の境界が必要なら、試行を理由付きで失効させる。
両Sourceを同じPOSTへ黙って置き換えたり、正常と見せたまま判定を再開したりしない。
この例外追加はINV-LC19の改定が必要であり、本書では正本を変更しない。
主画面のSource 1／2、開示、終了は維持し、新しい回答収集を追加しない。

AUTOは既存の明示選択を尊重する。loopで勝手にONにせず、正常loopのみで勝手に解除もしない。
確認できた窓だけを既存周期で測り、待機中はgainを固定する。BLINDへは引き継がず停止する。
承認済みceiling、POST減衰、MATCHからの±6 dB境界は変更しない。

## 表示と音量安全

正常loopのたびにtoastや確認を出さない。固定MATCH表示を保ち、利用者はPRE／POSTを切り替えるだけにする。
待機や失敗は別作業の「最初の理由」と「現在必要な操作」の表示経路へ統合する。統合時には同じcommitで検証する。

表示文案は「PRE確認中・MATCH保持／POST出力」「再測定できませんでした・前回値を保持」など、
値が消えたのか、確認待ちなのか、何が鳴っているかが分かるものにする。
停止・再生が必要な場合と自動復帰を混同せず、未対応条件で無期限の準備中を表示しない。
英日・全サイズ・REF共存・accessibilityで同じ事実と操作を示す。

退避、折り返し、再MATCH失敗でPOSTをunityへ上げない。POST減衰は承認済み値を保持する。
PREの新gainは既存rampとceiling guardを通し、上がる量が承認の外なら勝手に適用しない。
非finite、offline、bypass、別owner、state restoreの境界をLOOPの特例で弱めない。

## 負荷を増やし過ぎない実装境界

- RTへ追加するのはblock単位の固定サイズ時計情報と、回数上限のある判定だけ。
  新しい音声走査・コピー・解析・delay・alloc/free・lock・I/O・常時logを追加しない。
- PCM ringの容量は増やさない。必要なdescriptor追加を設計しても、metadata増分はringあたり64 KiB以内を予算とする。
  これは実測値ではなく設計上限。足りない条件を無制限キューで吸収しない。
- PREからloop情報を渡すため共有layoutが変わる場合はversionを更新する。旧新版混在は明示拒否し、
  旧PREをloop対応済みとして扱わない。既存mappingの再stampやRT中の再確保はしない。
- 時計分類とMATCH承認はそれぞれ500行以下の小さなmoduleへ置く。イベントバスや巨大な状態機械は作らない。
- MATCH／AUTO／ずれ推定は既存の非RT経路と周期を使う。新workerやtimerは追加せず、状態不変時のUI更新省略を維持する。
  正常loopでAUTOが継続する分の実CPUも測る。「threadを増やさない」だけを低負荷の証拠にしない。
- 64／128／256／512 framesで、通常無試聴、記名両側、BLIND、境界失敗の前後CPU中央値・p99とRT確保回数を測る。
  前回の局所benchmarkだけでfull processorや実DAWの負荷を合格扱いにしない。

## 影響箇所と実装順

| 責務 | 主な対象 |
| --- | --- |
| host情報と境界分類 | `HostProcessClock.h`、`PluginProcessorHostClock.cpp`、`LiveCompareClock.h`、必要時の小さなTransportPolicy module |
| 周回とKの根拠 | `LiveCompareRing.h`、`LiveCompareCorrespondence.h`、`LiveCompareSharedRing.*`、PRE/POSTのRT接続 |
| MATCHと履歴 | `LiveCompareProcessorState.h`、`LiveCompareSession.h`、`LiveCompareWindows.h`、`LiveCompareMatch.*`、`PluginProcessorLiveCompare.cpp` |
| BLINDと復帰 | `PluginProcessorLiveBlind.cpp`、`LiveBlindSession.h`、`LiveCompareRecovery.h`、共通RecoveryPresentation |
| 画面とAUTO | `PluginEditorLiveCompare.cpp`、`PluginEditorLiveCompareAuto.cpp`、`PluginEditorLiveBlind.cpp`、LiveBlind／footer／英日catalog |
| 契約と試験 | live_compare unit、実processor/editor fixture、`LocalBlind.cmake`、RT source契約、README、invariants |

まず製品コードを変えず、周回別PCMの模擬hostで認定条件と反例を確定する。
次にMATCH承認と対応根拠を分離し、履歴・承認待ち・AUTO・BLIND・表示まで一つの変更範囲として実装する。
最後に対象hostのexact candidateで確認する。成立性が不足した場合は条件と理由を利用者へ返し、
勝手にWindowsやAAXを除外したり、LOOPを後期工程へ送ったりしない。
公開正本のINV-LC18〜21等は承認された仕様と実装試験が揃った時点で改定する。

## 受入試験と未解決の技術ゲート

1. 各周回で異なるIDのPCMと独立した遅延モデルを使い、Kの整数周回ずれを必ず検出する。
   0／4096 samples、loop長の直前・同値・超過、ring容量超過を分ける。固定波形の自己一致を成功条件にしない。
2. 0.5／1／2／4／8秒loop、非block整列、可変block、境界非分割、POST位置clampを含む。
   各100周でgain revision、POST目標、選択、BLIND割当が正常折り返しだけでは変わらない。
3. LOOP ON中の手動前後seek、境界と同じ行先へのseek、範囲変更、テンポ変化、情報欠損・stale情報、
   PREだけ／POSTだけのsleep、PDC変更、旧runの同一時計再利用を注入する。判別不能なら安全に退避する。
4. loop途中から初回MATCH、短loopの実3秒窓、無音・疎な打音、再MATCHの失敗・キャンセル・古い承認を検証する。
   対応不明区間を含む窓が採用されないこと、失敗時に前回値が保たれることを確認する。
5. MATCH済み→BLIND、直接BLIND、開示後の継続、折り返しとEND／再選択／restoreの競合を実processorで確認する。
   POST代替のreceipt誤計上、匿名性漏れ、急なPOST増幅、同じ無効trialの再開は0とする。
6. 通常Aのbit同一・0 samples、保持減衰、PIN/Exact 4 S、Record/FFI境界、全対象表示を回帰検証する。
   cargo test/clippy、native/source契約、行数ratchetを通し、FFI変更時はignored suiteも全件実行する。
7. Studio One／Studio Proで、実際のloopからの入口と100周のPRE／POST試聴を確認する。
   VST3／AU／AAX、macOS／Windowsは実施条件ごとに記録し、別format・過去commitの証跡を流用しない。

未解決の中心は「初回からloopしている場合のKの一意性」と「host通知だけではseekと区別できない境界」である。
これを検証前から万能な自動判定として約束しない。固定MATCHの保持という利用者契約と、
各hostでPREを出せる認定条件を分離することで、失敗しても設定を失わず、未確認の音も出さない設計にする。

## 根拠と確認範囲

- 現行ソースは基準`e02b4251`／B-1101上の未commit候補を読んだ。既存の製品コード差分は設計・成立性検証で変更していない。
- [JUCE PositionInfo公式仕様](https://docs.juce.com/master/classjuce_1_1AudioPlayHead_1_1PositionInfo.html)は、
  時刻情報がhost／formatで欠け得ること、loop pointsがoptionalであることを説明している。2026-09-30参照。
- 使用中の公式SDK header `juce_shell/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/pluginterfaces/vst/ivstprocesscontext.h`
  は、cycle境界でのblock分割が必須でないことと、連続時計・loop pointsがoptionalであることを記載している。
  公開Web版は取得できなかったため、このローカルの使用中SDKを直接確認した。
- 使用中のJUCE VST3／AU／AAX wrapperのloop情報取得を確認した。wrapperのコード存在は実hostの通知精度の証明ではない。
- [既存G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第2節・第8.6節を参照。
  限定条件の過去の実測であり、今回の候補、全buffer、長遅延、全platformへ一般化していない。
- 中断理由と復帰の別ローカル作業（未commit）とINV-LC1〜4／10／14／16／18〜21を照合した。
  本変更にその製品実装は含めない。成立性検証は次節に分け、製品のLOOP対応完了とは扱わない。

## 成立性検証で確認した制約

2026-09-30、製品のPublisher／Consumerだけを駆動する模擬hostを追加した。
processorの不連続判定・MATCH・gain・BLINDは通していない。実Studio One／Pro Toolsの再現録音でもない。
この照合器単体の結果を、製品で実際に誤音が出た証拠や、正常loopが使用可能な証拠と呼ばない。

`juce_shell/tests/live_compare/LiveCompareLoopOracle.h`は、入力PCMを独立したdelay lineへ通し、
Consumerが返したPCMと比較する。期待値をConsumerのKから逆算していない。
stereoの2値で再生通算frameを識別し、project位置が同じでも別周回を区別する。
両側のclock原点は明示したモデル仮定であり、各formatの全hostへ共通する保証ではない。

### 厳しめレビューで修正した試験の問題

- 旧seek試験は通常wrapの観測値をコピーして比較していた。独立したseek再現ではないため削除した。
  「同じ行先へのseekを区別できない」との結論は未検証へ戻す。今後はtransport操作と通知を独立に生成して確かめる。
- 旧判定モードは誤対応が存在することをassertし、将来の安全修正後に失敗する構造だった。
  誤対応件数は観測結果として扱い、正しい結果を成功にできる判定へ変更した。
- 遅延ごとにbufferが一つに固定されていた。長期surveyは遅延と全buffer scheduleを直積で検証する。
- 初回loopの反例だけで、MATCH済みからの継続まで停止必須としたのは根拠が不足していた。
  対応較正済みKからseekせずloopへ入る別fixtureを追加した。実MATCH操作の検証はまだ行っていない。
- コマンドの誤記が成功扱いになる点を修正。不明引数はexit 64。fixtureの長さ・delay・blockも範囲検査する。

oracle自体には正しいPCM、1 sampleずれ、1周ずれ、右chの最終sampleだけの誤り、NaNを与える。
正常時は一致し、すべての注入誤りを検出することを確認する。非loopの照合器positive controlは
2時計×2遅延×5bufferの20条件で、正しいPREが実際に読めることも確認する。

### 初回からループしている時の周回誤認

48 kHz、0.5秒loop、128 frames、独立counter型時計、content位置報告で、
遅延0と24,000 samplesの二つのhostを比較した。どちらもloopの途中から比較を開始する。
PRE／POST時計、loop範囲、PPQを含む1,000 callbacksの観測値は全て同じだったが、
正しく対応するPREのPCMは全て異なった。optionalなpresentation latencyは双方で欠損させている。
存在しない値を0 samplesの遅延として扱ってはならない。

| 条件 | accepted block | 誤対応block | 最初のずれ |
| --- | ---: | ---: | ---: |
| 遅延0の対照 | 992 | 0 | 0 samples |
| 遅延がloop長と同じ | 992 | 992 | 24,000 samples |

同じ1,000 blocksを毎周同じPCMにすると、遅延0と1周遅延の正解PCMが同じになり、周回差を検出できない。
この対照と周回別PCMの差は、Consumerの現在の誤対応をassertしなくても検証できる。
将来、追加の根拠を持つ照合器が対応拒否・正しく選択するようになっても、バグの存続をテストに要求しない。

### 既知Kからloopへ入る場合

非loop区間で正しいKを較正した後、現在位置から始まるloop範囲を有効にした。
有効化時のPRE／POST位置・時計が変わらず、1周後に初めて折り返すことをfixtureで確認した。
0.5秒loop、128 frames、content位置報告の4周では次の結果だった。

| 遅延 | VST3型Kの変化 | counter型Kの変化 | 各時計での誤対応 |
| --- | --- | --- | ---: |
| 4,096 samples | 0 → 0 | 374,784 → 374,784 | 0 |
| 24,001 samples | 0 → −24,000 | 394,689 → 370,689 | 554 blocks |

この条件では、正しく較正済みでも最新project一致候補がKを1周分作り直した。
初回Kの一意性とは別に、較正根拠の保持と候補更新の構造を扱う必要がある。
ただし「既知Kを常に固定すれば安全」とまでは示していない。seek・PDC変更・gapとの区別は別に必要。

### 遅延とbufferを分けた100周survey

loop長0.5／1／2／4／8秒、遅延0／4,096／loop長の1 sample前／同値／1 sample後、
VST3型・独立counter型時計、content位置・先頭clamp、64／128／256／512／可変64〜2,048 framesの
直積500ケースを各100周駆動した。実製品のBLIND／MATCH／AUTOを通す試験ではない。

照合器単体で誤対応を生じたのは100ケース、PRE取得frame比率95%未満は208ケースだった。
これら308ケースは探索基準を満たさず、残る192ケースでは誤対応0かつ取得95%以上だった。
95%は探索用の利用可能率であり、誤った音やBLIND代替を5%許可する契約ではない。
両項目は独立に集計し、将来の結果で重なりがあっても合算で誤カウントしない。

ring容量より長い524,289 samplesの遅延も5bufferで別途検証した。
64 framesは全拒否、残る4条件では誤った新しい周回をacceptedとした。
この容量超過条件の成功は全拒否であり、PRE取得率95%以上を要求しない。
128 framesの最初のずれは504,000 samplesで、584 accepted block全てが誤対応だった。
「ringから読めた」ことは、正しい周回を読めた証拠ではない。

### 再実行方法と結果の扱い

CMake targetは`KirinLiveCompareLoopFeasibilityTests`。
通常CTestは高速なoracle自己検証、metadata／PCMの対照、非loop positive control、既知Kの入口、判定の境界値を確認する。
通常の成功は試験基盤の確認であり、LOOP機能の合格ではない。

`KirinLiveCompareLoopFeasibilityTests --survey`で500ケース×100周と容量超過5ケースを実行する。
探索基準の未達または容量超過の受入れがあればexit 2、全て満たせばexit 0、不明引数はexit 64。
旧`--qualify`は廃止した。今回の未達は容量超過4条件を含む312条件で、exit 2だった。
このsurveyは製品・host・出荷の資格判定ではなく、照合器の改善箇所を調べるためのもの。

### 追加情報と次の検証範囲

AU v2の`HostCallback_GetTransportState`は汎用のtransport変更通知を返す。
現JUCE wrapperはその値を`playchanged`へ受け、Hyphaには転送していない。
Appleの使用中SDK headerで、この値が変更・不連続を表すことを確認した。
[Apple公式資料](https://developer.apple.com/documentation/audiotoolbox/hostcallback_gettransportstate)のWeb本文取得は失敗したため、
仕様の判断はローカル公式headerに基づく。seekだけを区別できるかはhost別に未検証。
VST3の[公式ProcessContext仕様](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ProcessContext.html)
と使用中SDK headerも確認し、連続時計・loop pointsを全host必須とは扱わない。

今回の変更は試験と設計記録に限定し、RT・UI・共有ring layout・INV-LC19は変えていない。
製品へ新しい解析、buffer、timer、worker、RT確保は追加していない。
製品CPUの前後差を測ったわけではなく、LOOP実装の性能ゲートは未実施である。

次は、固定MATCHと照合根拠を分け、既知Kの引継ぎ・候補更新・履歴proofをローカルfixtureで検証する。
初回からloopする場合の一意性、seek・PDC変更との区別は独立した未解決条件として残す。
追加情報が必要なhost profileは既存の時計probeで実測する。全製品作業を実機配置待ちにするとの結論にはしない。
実DAWへの配置は別途指示を得てから。Windows／AAXを勝手に対象外へ変更せず、実証前に対応完了としない。
