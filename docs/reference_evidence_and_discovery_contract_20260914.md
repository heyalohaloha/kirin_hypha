# Capture比較の根拠とPRE候補検索 — 実装契約

Date: 2026-09-14
Status: B-878計画レビュー3件を解消する設計。アルゴリズムの実測・製品実装は未実施。
Baseline: B-878 `3768f057`。製品コードはB-876から変更なし。

本書を、[統合計画](hypha_capture_and_workflow_integrated_plan_20260914.md)と[構造修正計画](reference_capture_structural_repair_plan_20260914.md)の音量根拠・入力差判定・探索上限の正本とする。
通常Aの0 dB、既定の固定Gain Match、位置校正、両Blindの明示操作は[既存契約](reference_whole_song_alignment_20260913.md)を維持する。
以下の数値は実装の予算・合格条件であり、達成済みの製品性能ではない。

## 1. 過去の表示と現在の音量を分ける

| 対象 | 音量の正本 | 更新条件 |
| --- | --- | --- |
| 通常A音声 | 現在のDAW入力、0 dB | 本変更から加工しない |
| 現在のB/C試聴とLIVE表示 | 既存の試聴controllerが準備・適用したGainとその世代 | 既存の校正・選択・復帰契約のみ |
| CaptureのA数値・波形 | 取得時の無加工入力 | 新しいCaptureを確定するまで不変 |
| Captureに対するB表示 | そのCaptureとBの組合せで検証した`CaptureGainReceipt` | 新しい有効な組合せを確定したとき。現在の再校正から上書きしない |

`CaptureGainReceipt`はCapture ID、Bのfile/PCM hash、位置対応revision、rate/channel、校正区間、policy revision、fullGainDb、displayGainDb、headroom根拠、成立状態を持つ。
通常の既定policyと同じ、400 ms窓・100 ms hop・27個以上の連続した対応active blockの差の中央値を使う。
全体LUFS-I差から別の補正を作らない。source TPとAの校正区間TPによる既存ceilingも引き継ぐ。
full Gainが既存ceilingを満たす場合はその全量を表示コピーへ適用し、満たさない場合はB原音の0 dBと`originalFallback`を保存する。中間Gainで帳尻を合わせない。
この表示用Receiptは「実際に聴いた」という証跡ではない。実音へ適用済みかどうかは別の音声側Receiptから表示する。

Receiptを作れるのは、校正が読んだAの全区間が当該Captureに含まれ、同じ入力である根拠がある場合だけである。
Capture中なら同じ入力queueの世代とsample範囲、後日の再生なら4個の完全な1秒単位のdigest一致で同じ校正区間を覆う。
後日の校正probeはCapture索引の1秒境界から4秒を受理し、既存の短いPCM観測を再利用する。任意位置からの4秒に端の未確認sampleを付け足して一致扱いしない。
部分一致、近いRMS、同じWork、現在のB試聴成功だけでは作らない。

| 保存AとBの状態 | CAPTURED表示 | 現在の試聴 |
| --- | --- | --- |
| 位置とCaptureGainReceiptが有効 | 固定したB表示Gainで比較。短い`CAPTURED`表示とBの補正量を添える | 現在のcontrollerが独立して判断 |
| 位置だけ有効、Gain根拠なし | A/B原音の比較、凡例`ORIGINAL LEVELS`。MATCHEDを出さない | 同上 |
| 位置の根拠なし | Aを保持。Bの独立した全体像は表示可能だが、共通位置線・B−A差は出さない | 同上 |
| 現在のAや試聴Gainが変化 | 過去の位置・Gainを維持。現在の出力側とGainは音声操作部へ表示 | 既存の再校正・失効規則を維持 |

CAPTUREDに裸の`MATCHED`を付けない。現在のAと音量が一致した印と誤認させない。
LIVE/CAPTURED変更で音声、Gain承認、B/C選択を変更しない。
現在のGainをCaptureの描画keyへ含めず、Capture ID・B hash・位置revision・CaptureGainReceipt revision・表示範囲だけでcacheを更新する。
旧schemaのGain根拠欠損はunknownとする。0 dBだったと推定しない。
新しいBを選んだ際は、そのB固有のReceiptを検証する。旧Bの補正を名前やWorkから引き継がない。
保存できるReceiptは最大16組、位置segmentを含め各4 KiB以内、合計64 KiB以内とする。現在選択中を保持し、超過分の再利用には再検証を必要とする。破棄するのは派生Receiptであり、Capture本体ではない。

## 2. 入力差の通知と、同曲・位置校正を分離する

通知が答えるのは「同じDAW sample区間で、記録時と今回の入力が異なるか」である。
Reference校正が答えるのは「現在または保存したAと、登録済みBが同じ内容位置に対応するか」である。
前者へ音の一致を前提条件として課さない。クリップ差替えも入力差であり、プラグイン操作を特定した意味ではない。

### 2.1 時間軸の根拠

Captureにはruntime生存期間のtoken、入力configuration世代、clock種別とPDC署名、rate/channel、hostStartと受理sample範囲を記録する。
同じruntime・configuration・clock/PDCの再生では、その絶対sample範囲を比較する。pause/seekだけで設定変更と断定しない。
同じ時間に別のクリップが来た場合は入力差として通知できるが、「同じ曲の対応位置」とは扱わない。
clock欠損・原点を確認できない変更・PDC/rate/channel変更・queue欠落は、その区間を`unverified`へ戻す。値を0で補わない。
runtime tokenをDAW再起動、processor再作成、state複製の同一性証拠に使わない。

復元後は時間軸も未確認から始める。
保存位置で4個の連続した完全な1秒単位がdigest一致し、少なくとも3単位に有効な音がある場合に限り、その再生の時間軸を再承認する。
この承認も未訪問区間の音の一致、Work、routingの不変を証明しない。
同じ4単位が保存索引の複数位置に現れる場合は曖昧として承認しない。
満たさない場合、復元Aは保持したまま照合未確認とし、現在のA/B/Cは従来の校正で利用できる。

### 2.2 保存する軽量索引と判定

1単位はCapture開始からちょうどsampleRate個のframeとし、最大7,200単位。末尾の長さはDocumentから復元する。
各単位64 bytesを次の形式に固定してG0で検証する。unit番号からoffsetを求めるため、単位ごとの時刻配列を増やさない。

| 内容 | bytes | 用途 |
| --- | ---: | --- |
| SHA-256 digest | 32 | finiteな取得PCMの正規化したfloat32列の一致確認。rate/channel/framesもdigest入力に含める |
| 4帯域×2chの平均power、0.01 dB刻みint16 | 16 | 通知用特徴。通常メーターや厳密な位置・Gain成立判定へ流用しない |
| 2chの無重みRMS、float32 | 8 | 音量・強弱の変化を確認 |
| 2chのsample peak、float32 | 8 | 強弱の変化を確認。True Peakとは呼ばない |

PCM正規化はinterleavedのlittle-endian float32、±0を+0へ統一し、NaN/Infを無効とする。演算やゲイン正規化は行わない。
monoの未使用chは規定値とし、元のchannel数を維持する。digest一致を「全曲がbit identical」と拡大解釈しない。
帯域特徴はworkerで3本の一次low-passを計算し、LP1、LP2−LP1、LP3−LP2、入力−LP3のpowerを積算する。
cutoffは120 Hz、1 kHz、min(6 kHz, 0.375×rate)。係数は`alpha = 1 − exp(−2πfc/rate)`、更新は`y += alpha * (x − y)`とする。
各1秒単位の開始で特徴用filterだけを0初期化する。同じPCMをseek後に読んでも同じ特徴になる。通常計測のfilterはresetしない。
log変換は単位確定時のみ。bandの定量化はnearest整数へ丸め、無音・範囲外は予約値とvalidityで扱う。無効値を有効な測定へ丸め込まない。
これらは変更通知用の特徴であり、製品の帯域測定値や音色の診断として表示しない。

判定は次の順序に固定する。

1. 時間軸と完全な比較単位の受理を確認する。不成立なら未確認であり、変更とは表示しない。
2. digest一致なら、その単位の入力一致を記録する。UIへ全曲一致の印は付けない。
3. digest不一致は内部で保持する。旧新の双方で−70 dBFS以上のRMSまたはband powerに0.1 dB以上の差、または双方で−60 dBFS以上のsample peakに0.2 dB以上の差があるとき、`A DIFFERS`対象とする。
4. 無音からの有音／有音からの無音は、RMSまたはband powerが−70 dBFS境界を0.5 dB以上の余裕で跨いだときに通知する。境界付近の揺れは保留する。
5. その他はraw差があって通知閾値未満である。完全一致の根拠として使わず、MATCHEDの裏付けにも使わない。

閾値はG0の初期値として明示した。入力差が特徴に現れない位相だけの変更等は、通知を保証しない。
ディザー・ランダム変調も入力差の原因になり得るため、「操作された」とは表示しない。
時間軸の根拠なしに、この特徴からoffsetを推定してsample一致へ格上げする実装を禁止する。
通知は条件を満たした一つの完全な単位を受理した後のUI更新で出す。位置既知の連続再生では開始境界待ちを含め1〜2秒＋処理遅延を計測する。
末尾が1秒未満でも全frameが再訪された場合は同じ特徴を比較できるが、4秒の再承認・Gain校正には不足区間を継ぎ足さない。

通知を出した区間には観測passと時刻を持たせ、同じ区間の再確認でのみ解除する。未訪問部分の古い印を一括解除しない。
Referenceを非表示にした間は照合を停止する。再表示時は小さな`LAST CHECK`表示で過去の確認であることを示し、新しい観測で更新する。
新しい検知器は音声切替や再校正commandを発行しない。既存のPCM校正guardは独立して動作し、その失効・Blind中断規則を維持する。

### 2.3 BなしCapture、変更後、復元後の動作

| ケース | 入力差の通知 | 保存AとBの位置・Gain | 現在のA/B/C |
| --- | --- | --- | --- |
| BなしCapture後、同じruntimeでEQ等を変更 | 同じDAW区間の差として上記判定 | 取得時の根拠がないため、現在の校正を過去Aへ渡さない | 選んだBと現在の4秒PCMで既存校正を実施 |
| BなしCapture後、Aを変えずにB選択 | digest一致区間を確認 | 一致した4秒と現在のPCM校正を結び、その範囲のReceiptを作る | 従来どおり |
| 未調整でもdither等により取得PCMが再現しない | 通知閾値を満たさなければ通知せず、raw差は保持 | 新しい過去A/BのReceiptを完全一致として作らない | 現在のPCMによる従来校正は利用できる |
| Capture時にBとの根拠が成立済み、後でAを変更 | 対応DAW区間の差を表示 | 取得時の位置・Gainを保持 | 現在の校正は独立 |
| 復元Aが現在と一致しない | 時間軸が未確認なら変更と断定しない | 保存Receiptが有効なら過去A/Bだけ表示可能。欠損ならA保持 | 従来どおり |
| Capture内または現在の音に切貼り・反復がある | 同じDAW区間の差と、内容位置不明を区別 | 既存の曖昧さ・範囲検証を通ったsegmentだけ比較 | 既存のstrictな校正を維持 |

「Bなし取得→全体EQ変更→B選択」でも、入力差の通知と現在のA/B比較は成立させる。
取得時のPCMや校正証拠がない過去Aについて、同曲・sample位置・Gainを新たに保証することはしない。
その場合の再取得は利用者が過去Aも更新したいときの選択肢であり、現在のA/Bを使う必須操作にはしない。
この境界を未実装機能の次期送りとして扱わず、今回の正常な未確認状態としてUI・試験へ実装する。

## 3. 候補検索の仕事量を制限する

新しい常設PRE候補表示は、既存の無制限列挙を1秒ごとに呼ぶ構造にしない。
既存の列挙はroot内のディレクトリとJSONを読むため、C ABIの32件制限からCPU/I/O上限を推定しない。
読取専用の`CandidateDiscoverySnapshot`を非RTで作り、フィルタと接続の権威を分ける。

| 単位 | 固定する上限と動作 |
| --- | --- |
| 需要 | 未接続画面のopen/foreground、名前欄へのpointer進入・keyboard focus、接続境界変更で要求。既存のdiscovery更新結果も再利用 |
| 周期 | 同じ需要の自動要求は最大1 Hzに集約。無変更の画面から全探索を周期起動しない。再要求は新しいイベントか既存providerのrevision変更がある場合 |
| 所有者 | 一つのloaded module内で共有するlazyな非RT実行者。1実行＋1個の再要求bit。インスタンスごとのthread・watcher・待ちqueueを作らない |
| 1 batch | directory entry最大32、file operation最大8、読込合計64 KiB。2 ms経過を次の操作前に確認してyield |
| 1 scan | entry最大512、JSON最大64件、1ファイル64 KiB、総読込1 MiB、累積実行30 ms、経過250 msのいずれかで打ち切る |
| cache | 同一moduleで公開中・退役中を合わせて8個のsnapshot slot、総計1 MiB以内。全slotが参照中なら更新をskipし、追加確保も解放待ちもしない |
| 公開 | PREとPOST claimsを同じscan世代へまとめ、complete/incomplete/failed/cancelledを返す。途中結果を単一候補の根拠にしない |
| 鮮度 | 最終scanから2秒以内、かつ境界revision一致の場合だけ1操作接続のpreviewに使う。実際の確定は別途再検証 |

entry上限は候補数でなく、無関係・古いものを含む訪問数である。JSON parse前にファイル長と読込量を制限する。
打切り後、同じ上限不足のscanをタイマーで自動再開し続けない。次の自然な要求または明示メニュー操作まで待つ。
共有単位はloaded moduleであり、AU/VST3/AAX間で一つのstaticが共有されるとは仮定しない。複数形式同時使用の総負荷をG0で別に確認する。
cache keyはroot identity、host processと生存世代、project hash、DAW session ID、schema/filter revisionを含め、POST自身のIDと既存operation-group規則でviewを作る。
表示需要がなくなった場合、境界変更、processor破棄ではcancel世代を進め、結果を破棄する。古い結果は新しいPOSTへ公開しない。

ファイルI/OはOSの呼出中に設定時間を超える可能性がある。上記時間をハードなOS応答保証とはしない。
読み取りjobは値として複製した境界情報を使い、engine pointer、UI lock、`handleLock`を保持しない。
workerが遅延してもAudio Thread・UI・engine破棄はjoinしない。既存のRecord保存・計測workerへこの探索を直列追加しない。
最後のinstance破棄とmodule unloadは区別する。moduleのcode/dataを使用中のjobが解放後に動かないよう寿命を固定し、退役時のdrainは非RTで行う。module unloadを含む寿命試験をG0-Sへ加える。
バイト上限を守る有限readと、各操作間のcancel確認を使う。返却が遅れた結果は世代と鮮度で破棄する。

完全なsnapshotで利用可能な候補が一つなら相手のIDをpreviewする。文字が収まらない場合も一覧へ戻す。
不完全なら名前欄は候補メニュー入口のままとし、「PREなし」や「一つだけ」と断定しない。
候補メニューを明示的に開く既存の経路は維持する。新しい背景探索の予算を、その選択機能の削除理由にしない。
押下時にpreviewのIDと世代を固定し、更新された先頭候補へ置き換えない。
接続は既存のexact identity・claims・再生制限を再検証するcommandへ渡し、待機中の二重要求を抑止する。snapshotだけで接続成功にしない。
background snapshotの変更に合わせて別のPOSTの接続を解除したり、通常のKeep/Recordの可用性を上書きしたりしない。

## 4. 実装開始時のG0検証と失敗時の行動

G0は最初の小さなnative検証であり、製品のUIやschemaへ新方式を組み込む前に行う。
既存の実音fixtureと合成信号を使い、出力JSONへoffset誤差、Gain誤差、検知区間、誤通知、処理時間、メモリ、読込回数を記録する。
今回の計画作成では実行しない。閾値・時間・保存予算を達成したとは報告しない。

| ID | 必須ケース | 合格条件 |
| --- | --- | --- |
| G0-G | 同じCapture/Bのまま現在Aを±6 dB変更して再校正、B再選択、保存復元、headroom不足 | 過去の表示GainとA値は不変。現在試聴Gainだけ既定policyで更新。純Gain fixtureの誤差0.01 dB以内。根拠なしのMATCHEDは0件 |
| G0-D | Bなし取得→Gain/EQ/dynamics変更→後からB、無変更、16/24-bit相当dither、反相stereo、無音、短い末尾 | 位置既知で通知基準を超える各加工を検知。ditherだけの対照で誤通知0件。通知対象区間のずれ0単位。Bなしでも判定可能 |
| G0-P | ±1 sample／1秒移動、途中の切貼り、反復、rate/PDC変更、復元・複製、旧schema | DAW区間差と同曲のoffset推定を混同しない。曖昧・根拠欠損時の偽aligned/MATCHEDは0件。成立した既知offset fixtureは既存のsample誤差基準を維持 |
| G0-S | 1/32/33候補、同名、他POST所有、512/513 entries、64/65 files、1 MiB超過、64 KiB超過JSON、遅延I/O、破棄・境界変更 | 回数・byte上限を超えない。不完全を単一候補と表示しない。並列jobはmodule内1件、待ちqueue増加0。RT/UIを待たせない |
| G0-M | 最大2時間、全rate/channel、保持A＋取得＋復元、1/2 POSTと混在wrapper | 下記保存・RAM予算内。新方式のためのRT alloc/lock/I/O追加0。worker追加時間は100 ms入力当たりp95 1 ms以下（48 kHz stereo）、各rateでも実時間の10%未満 |

G0-DのEQ/dynamics fixtureは、単なる全体Gain差だけで合格させない。出力RMSを原音へ戻した加工も含め、band/peak特徴で検知できることを確認する。
判定に必要なband特徴が通知基準を超えない加工も残し、その結果は「通知保証外、raw不一致」と記録する。検知できたと数えない。
G0不合格では、精度を下げて合格扱いにせず、失敗ケースと予算差を示して同じ計画内で方式を改訂する。
索引形式・閾値を変える場合は本書、schema revision、fixture期待値を一緒に更新する。無条件の全曲PCM保存・再decode・予算増大で通さない。
この変更検知の成立範囲を縮める必要が生じた場合は利用者へ具体的な差を提示し、黙って対象外へ送らない。
既知のCapture4不具合の修正はG0から独立して進められるが、新しい索引の保存形式確定とUI統合はG0合格後とする。

## 5. 保存・計測の予算と最後の合格判定

索引7,200×64＝460,800 bytes、表示要約2,048×80＝163,840 bytes、coverage/validity用bitsetを合計1,800 bytes以内とする。
Receiptは64 KiB以内、その他JSON metadataは64 KiB以内。前者3項とReceiptをbase64化した上限は922,636 bytesで、metadata込み988,172 bytesとなり、1 MiBに60,404 bytesの余裕がある。
これは圧縮率に依存しない設計値であり、文字列escape、header、末尾paddingを含めた実encoder出力の1 MiB gateを最終的な正本とする。
既存256 KiB形式は読み取り条件を維持し、新schemaだけへ新上限を適用する。Capture以外の既存plugin stateをこの予算で切り捨てない。
追加RAM目標16 MiBはactive/held/pending restore、作成中と公開済みsnapshot、encode一時領域、Receipt cacheを含めて測る。queueは合計2 MiB以内。
既存校正が保持する短いPCMを参照する場合も寿命延長を計上し、CaptureDocumentへPCMを保存しない。

G0で形式と成立条件を固定し、実装中は対象試験、最終状態では全体suiteを一度にまとめる。
その後の同じ最終版で、5サイズの実寸、300％Blind、Studio One/Pro Tools、Windows、30分連続動作と負荷を検証する。
実機で不合格なら修正と影響対象の再検証を行う。成功済み全体suiteを根拠なく反復しない。
試験のpass、実機のpass、初見利用者の操作観察を別に記録し、計画の完成と製品の完成を区別する。

## 6. レビュー項目の対応

| 指摘 | 解消する規則 | 実装時の証拠 |
| --- | --- | --- |
| P1 過去Aの音量基準が未定義 | 1章のCaptureGainReceipt、原音fallback、LIVEとの別表示 | G0-Gと保存・B変更回帰試験 |
| P1 入力差検知と位置一致の循環 | 2章のDAW区間差と同曲校正の分離、64-byte形式、Bなし・復元時の明示動作 | G0-D/Pと反例一覧 |
| P2 候補検索の仕事量が無制限 | 3章のentry/file/byte/job上限、需要起動、非同期破棄 | G0-Sと混在wrapperの負荷記録 |

参照実装: `ReferenceVisualBinding.cpp`、`ReferenceACaptureProjection.cpp`、`HyphaReferenceCapturedView.cpp`、`ReferenceACaptureRevisit.cpp`、`ReferenceCalibrationObservation.h`、`ReferenceContentAlignment.cpp`、`pair_candidates_ffi.rs`、`pre_candidates.rs`、`pair_operation_group.rs`。
