# DRUM・PSR改善計画 — 利用者の判断から設計を組み直す

2026年10月7日。今回の成果物は利用者の三決定を反映した改善計画とG0の文書・実寸試作・development計測であり、製品実装、製品build、CI、配置、署名、公開は開始しない。新しい表示周期・受入値は設計案として区別し、G0のdevelopment検証で既存契約との整合を確認してから固定する。計画の保存を、製品改善の完了とは呼ばない。

### 2026年10月7日の決定

| 決定 | 本計画への反映 |
| --- | --- |
| 人による受入は、開発責任者本人と友人1～2人で行う | 第7.3節。Hypha未使用の6人（日本語3・英語3）は集めない。初見の判定は、Hyphaの画面を見たことのない人だけが行う。該当者がいない言語・課題は未検証として残す |
| 打音検出の再評価を本計画から切り離す | 本計画は検出を変えず、既存の検出評価を回帰として守る（第7.2節）。追加の音響集合、弱打等の追加gate、二人の盲検annotatorは付録Aへ移し、検出を変える別計画で行う |
| G0は今行い、G1以降は1.1.51の公開後に始める | 第8章。G0は文書とdevelopment計測だけで、製品のsource・ABIを変えるcommitを入れない。計測に一時的なprobeが要れば採用sourceに残さない。G1以降は1.1.51をLS・HP・Windowsの三チャネルで公開した後に始める |

### G0の実施結果と現在地

`git diff e909fc1b 284d772b | git apply` をcheck後に適用し、上の三決定を保持した。[G0成果一覧と成立条件](hypha_drum_psr_g0_20261007/README.md)、[実寸試作](hypha_drum_psr_g0_20261007/preview.html)、[配置仕様](hypha_drum_psr_g0_20261007/ui_layout_spec.md)、[snapshot契約](hypha_drum_psr_g0_20261007/snapshot_contract.md)、[TIME・Capture契約](hypha_drum_psr_g0_20261007/time_capture_contract.md)へ具体化した。

合成wireの5サイズ×日英×2surface×14条件は、測った主要文字の切れ／重なり0。独立算術・時計モデル23項目と、固定候補／LOCK／LIVE等の操作模型を確認した。既存libのnative二位相観測と実consumerの使い捨てfixtureも実施した。製品・host・初見・日常操作・高級感のPASSではない。

**G0成立判定は保留。** 完了起点400 msの根拠、DRUM固有のL／正常jitter、密集二枠の1000 ms応答案の適切さは未検証。現Captureはcompact UUID／追加metadata受理／永続化の三境界で不適合を再現し、v2設計へ反映した。G1へ見かけのPASSで進めず、製品source／ABIを変える工程は1.1.51の三チャネル公開後とする。

## 1. 利用者が何を判断できれば、改善したと言えるか

目的は、再生中に外部コンプレッサーを調整し、Hyphaへ視線を戻した利用者が「何を対象に、何が測れたか」を短時間で読めること。必要な一打を調べ、現在の観測へ一操作で戻れることまでを一つの利用場面として設計する。数値の正しさ、初見理解、日常操作、動きの品位は別々に合否を判定する。

| 利用者の判断 | 画面が答えること | 改善完了の証拠 |
| --- | --- | --- |
| PREなしでも使えるか | POSTのATT・REL・LEVELは単体計測可能。DELAYと差分にはPREが必要 | 単体／ペアの表示fixture、初見課題 |
| 何の音・何打を見ているか | ALLは最新または固定した一打。BAND LIVEは現在6秒内の直近最大8検出打音 | 対象鍵・件数・時刻範囲と数値の一致 |
| 調整後に値が変わらないのはなぜか | 未完了、測れない、過去の打音が集計に残る、固定中を見分けられる | step-response fixtureと実ミックス課題 |
| 数値を信じてよい範囲はどこか | 全対象か確定部分か、確定値か限界か、現在値か過去の一打か | 条件付き値の誤認0、型付き区間の独立期待値 |
| 狙った一打を読めるか | 動く近接候補を選び、窓外でも保持し、LIVEへ戻れる | 選択・drag・窓外・再open試験 |
| 落ち着いて読み続けられるか | 固定した文字位置、説明可能な応答時間、静かな通常待ち、一定の時間移動 | 技術的motion gateと利用者の実再生評価 |

現状のDRUMを「初見でも、普段のミックスでも使いやすい」とは判定しない。写真とsourceから確認できるPOST単体計測は維持するが、PAIR選択と帯域比較成立の区別、欠測の意味、集計範囲、単打保持、動的な読み取りには改善が必要である。静止画の表示成功や描画時間だけで利用者の使いやすさを認定しない。

### 測定の意味を変えずに、説明を変える

ATT／RELはコンプレッサーの設定値ではなく、実際に入力された打音の包絡を測った時間である。帯域は楽器分類ではない。検出は30 msの非連鎖compound eventを使うため、表示語は「検出打音」とし、物理的な全打撃を常に一件ずつ分離するとは説明しない。

| BAND指標 | 正本となる定義 | POST単体 |
| --- | --- | --- |
| DELAY 到達の差 | 帯域包絡が各打音のピーク−20 dBを上向きに越えた時刻のPOST−PRE | 不成立。PREが必要 |
| ATT 立ち上がり | 帯域包絡の振幅10%から90%までの時間 | ms |
| REL 減衰時間 | 帯域ピークから、そのピークより20 dB低くなるまで。最大300 msまたは次onsetで区切る | ms／限界／欠測を区別 |
| LEVEL 帯域ピーク | 帯域RMS包絡のピーク | dBFS。差分はdB |

ALLは既存の四指標を維持する。TRANSIENT＝頭30 msと胴のRMS差、STRENGTH＝頭30 ms RMS、CREST＝頭30 ms sample peak−RMS、SHARPNESS＝頭100 msのDIN重み付き鋭さ。胴は次の100 msまたは次onsetまで。BANDの四指標・8打中央値をALLへ流用しない。ALLのpaired lane readoutは既存のΔ表示契約を維持する。

PSRの写真の `PEAK −0.6 − S −10.7` は、ピークとShort-term loudnessの開きを式で示している。現行計算は直近400 msのsample peak−3秒のShort-term loudness。True Peakとの差、処理のゲイン差、音質評価ではない。計算を変えず、常時の式を対象付き数値へ置き換える。

## 2. 主画面を「一指標、一つの読み取り先」にする

### PSRは対象と数値を一組にする

| 状態 | 主表示 | 履歴・補足 |
| --- | --- | --- |
| PRE単体で成立 | `PRE 10.1 dB` | PRE絶対観測 |
| POST単体で成立 | `POST 10.1 dB` | POST絶対観測 |
| 選択ペアの同時刻比較が成立 | `Δ −3.3 dB` | POST−PRE |
| 通常のPRE publication待ちで直前比較が期限内 | 同じ対象付き値を短時間保持 | 毎tickの文字・色の往復なし |
| ペア選択済みで比較なし／期限切れ | `Δ ---`＋短い理由 | 検証済みの同source履歴のみHOLD |
| 単体の現在PSRが不成立 | `PRE ---`／`POST ---` | 過去finite値を現在値に戻さない |

ペア時のPSRは利用者指定どおり自動Δ。M／S／TPの全体POST／Δ選択は保持し、PLRの可視性とCORRのsource・軸・helpもmain targetに従う。PSR targetを別にlayout／painterへ渡し、全体ボタンの適用範囲を明示する。ペア中の比較不能をPOSTへ無表示で切り替えない。

PEAK・S・式はヘルプへ移す。短文は「ピークと短時間平均音量の開き。ΔはPOST−PRE」、詳細には400 ms／3秒／sample peakを記載する。空いた幅は対象付き数値と履歴に使い、他メーターを重複掲載しない。100%では既存どおりPSR lane非表示、PSR専用の追加poll・履歴cloneは0。

### BANDの主値は母集団を先に読ませる

常時二つの数値を四laneへ並べる案を廃止する。主面は各指標に主値一つと固定scope領域を置く。scopeは値の先頭または直上に置き、末尾の小さな注記や色だけで区別しない。下記の選択はproducerが返す統計の**提示種別**であり、GUIで母集団や中央値を再計算しない。

| 全対象の状態 | 主値 | 必ず見えるscope・状態 | 詳細面 |
| --- | --- | --- | --- |
| 全Nの中央値が成立し、情報を持つ | 全Nの確定値／限界／区間 | `全8打`等。記号と単位を維持 | 全Nと確定n/Nの双方、五分類、理由 |
| 全N未成立または全実数、exact部分がある | exact部分の条件付き中央値 | `確定7/8打`等＋全N未成立の短い理由・件数 | 全Nを出せない理由と確定部分の根拠 |
| 全N未成立または全実数、exact部分もない | `---` | 対象N＋取得中／不明／不成立の内訳 | 各打音の状態 |

全Nが情報を持つ限界値として成立する場合も最優先する。全実数のmedianはavailableとinformativeを分離し、情報なしなら主数値に∞を出さず「全体不定」等の理由と明示した確定n/N、または---を使う。全実数の入力があってもmedianが有限なら除外しない（exact0×2＋全実数1は全N=0）。1打が−10 ms、7打が≥＋200 msなら主値は `全8打 ≥＋200 ms`。確定部分−10 ms（1/8）は詳細で読む。確定部分へSHORTER等の全体方向語を付けない。全N↔確定部分の切替はscopeと主値を同じrevisionで交換し、同じ数字でも意味が変わったことを省略しない。

正常pendingでは結果のある部分を読めるが、古い完了cohortを現在cohortへ持ち越さない。全Nと確定部分が同じ値の時に重複数値を主面へ増やさない。詳細を開く入口は全サイズで発見可能にし、scope・単位・限界・全N未成立の短い理由をhover専用にしない。

LIVE captionに対象Nと集計の最古／最新時刻または時間幅を置く。exactの古さは指標別の主値根拠へ紐づけ、同snapshot cutoff C−その指標の最新exact event時刻とする。主面の共有captionは、表示中ConfirmedSubsetのこのageの最大値を「確定部分の古さ（最大）」と明示し、detailでは指標別に示す。条件付き主値なしならageを表示せず枠だけ維持。cohort最新打ageやATTだけのfresh ageでRELまで新しいように見せない。外部調整前の打音が残ることを読めるようにし、自動の外部ノブ検出やcohort resetは追加しない。窓応答と提示遅延を別に測る。

### ALL・単打・サイズの役割を固定する

ALL LIVEは6秒の全公開イベント系列と最新一打、LOCKは選んだ一打。主面の「最新／固定した一打」をBANDの「全N／確定n/N」と区別する。BAND図の平均（dB）と下段中央値も区別する。PAIR選択と帯域比較成立は別状態とし、旧PRE等でDRUMがPOSTへfallbackする場合は理由を示す。PSRのペア時Δ待ち規則へ統一しない。

| サイズ | 主面の優先情報 | 詳細・操作 |
| --- | --- | --- |
| 100% | POST／Δ、帯域、最新／固定または対象N、意味の分かる四指標、各主値・scope・単位・限界／欠測、LIVE復帰 | 全帯域の時間履歴。BAND／VIEW設定は既存どおり125%以上。根拠入口は発見可能 |
| 125% | 100%の内容 | BAND／VIEW、時刻順event strip、根拠表示 |
| 150% | 主値とscopeの2×2、集計時間範囲 | event strip、件数／理由。四lane値軸と分解能は根拠面へ |
| 200% | 同snapshotの主値、全体／確定部分の根拠、平均包絡の参加数 | 重複カードを増やさず、分類と対象を読ませる |
| 300% | 200%の内容、PRE／POST形、区間・分解能 | 選択一打の形、footer help |

LOCKでは平均・中央値・n/Nを単打の指標と状態へ置き換える。100%波形は全帯域履歴と明記し、選択帯域包絡と誤認させない。ALL横軸は時間、BAND下段は測定値。全サイズに帯域ごとの分解能を残し、固定16 msを全帯域へ適用しない。

G0の実寸wireは[配置仕様](hypha_drum_psr_g0_20261007/ui_layout_spec.md)のbody内配分を使う。100%の2×2、125%のPSR追加、150%値軸の根拠面移設、200%pane内部への枠配分、locale短名は旧契約からの変更案と明示し、G2で正本を同期する。5サイズ×日本語／英語を実寸wireにする。片側／両側無音、1確定＋7長尾、両端区間、全pending、旧PRE、窓外LOCK、最大桁を使う。locale別短名を使い日英を二重掲載しない。日本語名案はINV-S40の現行英語label契約との差を明示して同期する。font下限、contrast 4.5:1、符号・単位・scope・理由の定位置を守り、切れ／重なり／重要情報の省略0を要求する。成立しなければ面積配分を直し、文字を縮めて先へ進めない。300%超は既存Inspection拡大であり、第六layoutを追加しない。

## 3. 共通構造 — 観測、統計、提示、操作を分離する

| 層 | 責務 | 次の層へ渡すもの |
| --- | --- | --- |
| 観測producer | 実測、actual／requested区間、完成／打切り、source／対応の検証 | immutable ObservationEvidence。未観測を無音へ変えない |
| snapshot assembler | 固定cohort、分類、型付き中央値、mask／参加集合、TIMEのraw current・proof・gap | version／size／revision付きの整合snapshot |
| presentation | target、提示種別、方向付き丸め、期限、数値提示周期、移動時計 | 数値・shape・scope・軸・状態が揃ったPresentationSnapshot |
| interaction | event鍵による選択、候補凍結、単打取得、LIVE復帰 | 選択意図。統計や時刻を書き換えない |
| render／Capture | 同じpresentationを描画／保存 | 画像とmetadataが同じstamp |

source、event、cohort、snapshot revision、presentation timeを別のidentityとして扱う。既存ALL、BAND LIVE、単打LOCK、TIME main、PSRはそれぞれ明示したsnapshot種別を使う。個別setterや別pollの最新値を寄せ集めて一画面にしない。Capture用の別計算経路も作らない。

### 共通ABI・互換性の境界

DRUM Summary V2はBAND用、Single Snapshot V2はALL／BANDを明示する。値と形を同じevent／cohort・source・cutoffで取得する。payloadはtarget、各側source span／run／clock、binding proof、ODF／band意味identity、requested／actual区間と終了理由、五分類・区間・中央値・件数・mask・参加集合・接続可否を含む。TIMEは独立したmain／PSR componentを同じpacketへ載せる。

旧struct／pollのサイズと意味、特に `KirinAbiContract` layoutは変更しない。旧照合関数はbuffer sizeなしで版確認前に書くため、structを拡大してrevisionで拒否する方法を使わない。新境界へversion／size／count／revisionを持たせ、旧staticlib／新shellの混在は起動時に拒否する。null、短buffer、未知version、raceでは全出力不変。reserved／NaNへ状態を隠さない。

TIME新schemaはepoch／incarnation／declared spanを含み、raw tail全pointがそのspanに属すことをpublisherで確認する。band wire v4にはactual span_endがあるため、mask導出だけを増版理由にしない。band意味identity追加は版付きで設計する。旧peer／旧schemaを新Δ authorityとして受理せず、POST絶対観測は維持して比較理由を示す。Capture consumerもG0で実schemaとround-tripを確認し、未知field保存を推定しない。

通常A経路のbit identical、0 sample latency、Record／plugin_dataの測定正本を維持する。Audio Threadへ解析・alloc・lock・blocking I/Oを追加しない。解析はworker、保存／publicationは対応する非RT層。raw tail・履歴・出力・二枠leaseは固定上限とし、UIへraw PCMや全候補PCMを複製しない。

## 4. DRUM — 対象の確定から一打の寿命まで

### 4.1 BANDの集計契約

現在6秒窓の同じALL公開系列から、直近最大8打のevent鍵と時刻順を**成立判定より先に**固定する。Silent、pending、対応なしを除いて過去の打音で補充しない。Matchedは一件、PRE-only／POST-only／ambiguousもNへ残す。帯域変更でも同じ対象鍵を使う。ALLの6秒系列を8打へ切り詰めない。

各指標を exact／bound／unknown／pending／not-applicable の排他的五分類にし、合計はN。各側の理由は別に保持し、両側件数を足してNにしない。判定順は「指標不成立が確定→必須条件の欠如が確定→有効依頼の結果待ち→型付き区間評価」。片側で成立不能と分かった指標をpendingへ戻さない。

| 境界 | 確定する扱い |
| --- | --- |
| Silent | ATT／REL不成立。到達なしのDELAY／ATTも不成立 |
| Arrival Ringing＋Rises | Arrivalだけを理由にRELまで除外しない。全体RingsOnは自分のREL不成立 |
| NextHit／未対応／時計不成立／PCMなし | coreに有効boundがなければunknown。有効依頼のないMEASURINGを作らない |
| LEVELの無音 | POST単体Silentは≤−72 dBFS等の実測限界。片側SilentのΔは限界、両側SilentのΔは不成立。−72をexactとして作らない |

全N中央値の成立条件は `N>0 && not-applicable=0 && unknown=0 && pending=0`。exactは閉区間 `[v,v]`、boundは開閉付き区間。N/Aを無限区間へ変換しない。全Nとexact部分の条件付き中央値はassemblerで同時に算出し、presentationはWholePoint／WholeInterval／ConfirmedSubset／NoScalar／Singleを明示して第2章の優先規則で選ぶ。全N中央値が一点でも、全N打それぞれがexactとは表示しない。

下限／上限列の中央値を求め、下限同値はclosed→open、上限同値はopen→closedの順。奇数は中央、偶数は中央二端点の平均で、選択した両端が到達可能な時だけclosed。差区間は `[post下限−pre上限, post上限−pre下限]`、使う二端点がともにclosedの時だけ閉じる。無限端点はopen、閉じた同一点だけexact。逆転区間／開いた同一点は契約違反として数値化しない。一打ごとのΔを集計し、両側中央値の差や区間中点、EMAで代替しない。

「判別範囲内」は全N中央値区間全体がproducerの分解能範囲内に収まる時だけ。広い区間が0をまたぐだけで一致としない。exact部分の方向を全Nの結論へ広げず、良い／悪い等の価値判断を表示しない。

### 4.2 包絡図は実測した同じ集合だけを結ぶ

HEAD96／TAIL64の各点にproducer実測maskを持たせる。actual終了位置とsample時刻から作り、床−120 dB、0、NaNからGUIが推測しない。比較点のmaskは `PRE実測 && POST実測 && 比較proof成立`。PRE／POSTのmean／min／maxには同じ参加打音を使う。平均はdB値の算術平均で「平均（dB）」と明記する。

各点にvalid_countとcohort順u8参加集合を持たせる。隣接点は同じ非0集合の時だけ線・薄い範囲を結び、集合変更は同数でも断線する。0件は線も塗りもなし。図captionにNと有効数の幅、集合変更境界に静的な切れ目と短い説明を置く。詳細で各点の参加打音を読めるようにし、hoverだけで集合変化を知らせない。

平均図へ中央値マークやΔ括弧を重ねない。単打DELAY括弧は実到達差として残せる。単打REL終了時刻間のΔ括弧は撤去し、各側終了マーク＋数値ΔRELにする。ピーク時刻のずれを減衰時間差へ混ぜない。実測振幅や平均点を滑らかさのためmorphしない。

### 4.3 PRE-onlyを無音と決めつけない

POST検出0はPOST無音の証拠ではない。PRE-onlyをそのまま残し、有効なODF位置支持／clock mappingと保持POST PCMがあれば、workerでPRE onsetに対応するPOST区間を実測する。無音blockでもODF／ringは進むため、POSTイベント0だけでproof欠如としない。検出pair kindと音声観測比較proofを分ける。proofは両run／epoch／incarnation／generation、rate／layout、意味identity、必要区間保持を含み、公開時にも再検証する。

cacheは実測DataKey（POST local identity・帯域意味・POST sample区間・終了理由）と比較ProofKey（mapping epoch・両run・PRE anchor・ODF／band意味・requested end／終了理由）を分ける。peer不一致では比較authorityだけ退役し、local実測cacheは維持する。pair kind／poll revisionを鍵にしない。遅着Matchedでも同一区間は再利用し、区間変更は新実測。容量は既存history上限のown／anchor枠と選択一打に限定。区間改訂はentry置換としrevision履歴を増殖させない。二枠lease、既存worker budget、run／band変更時のpending取消、ALL復帰時の帯域PCM解放を維持する。

ODF hashはFFT／mel／log等の対応意味であり、帯域測定全体の意味証明ではない。別のband semantic version／hashをV2・wire・cache・LOCKへ通す。descriptorはfilter／中心補償、RMS一周期・rounding／settle、presence−72／rise3 dB、arrival peak−20 crossing、ATT10/90%、REL−20 dB、peak130 ms／tail300 ms、HEAD96／TAIL64、span終了・区間端点・分解能・algorithm revisionを含む。rate／layout／band／区間は別identity。同ODFでもband意味不一致なら比較不成立。同bandでもODF support不一致なら対応不成立。意味不変の最適化や表示修正ではhashを変えない。

### 4.4 入力状態と単打取得の完成を分離する

LOCKは選んだ一打、HOLDは入力・更新の停止状態。この二つを一つのenumにしない。GUIの停止通知だけで取得をterminalにせず、producerの完成／AudioEnd部分結果／確定NotKeptを待つ。

| 独立状態 | 許される挙動 |
| --- | --- |
| LIVE・入力Active | ALL最新一打、BAND同cohortまとめを更新 |
| 一打取得中 | event／source／帯域／rateを固定。同じstampのlate detailだけ受理 |
| 入力停止・一打取得中 | HOLD＋同じ一打の取得中。停止後に届く正当な結果を受理 |
| producer terminal | Full、AudioEnd実測部分、確定保持なしを区別。実測値／coreが認めるbound／指標別欠測を固定 |
| 一打固定・窓外 | 値・理由・形を保持。「固定した過去の一打」、時刻、帯域、sourceを表示 |
| source失効 | pair／owner／epoch／incarnation／rate／layout／意味／seek／Resetで旧取得・LOCKを解除 |

AudioEnd部分はfull完了と区別する。停止後の150 ms区間がpeak130 ms等の必要条件を満たせば、遅着した有効ATT／LEVELとREL boundを採用する。100 msで必要区間不足なら、その指標は確定欠測。timeoutだけで測定事実を作らない。worker応答のdeadline／死活は既存200 ms idle・4 ms service・30 ms partial publicationと照合した。4 msはhard deadlineではない。受理から1000 msを有限応答案とし、非RTでRetired（RequestDeadline／WorkerUnavailable）へ閉じる。NotKeptは実際に必要音声がないと確認した場合だけ。期限後late reply拒否、期限の再延長0。密集二枠での適切さは未検証。永続MEASURINGを許さず、別打音で穴埋めしない。

ALLも同eventのhead30 ms、body、sharp100 msのlate完成だけを受ける。再開後の区間延長は新observation revisionであり、terminal LOCKへ黙って追記しない。100%で選ぶ時にも同じ単打形を取得し、300%へ拡大しても同eventを描く。形なしは理由を保持。帯域変更は同eventの新取得で、旧帯域値を消す。PCMなしは「この帯域は未計測」でterminal。

DRUM離脱／editor closeで取得・LOCK・候補sessionを解除する。帯域／VIEWの選択意図だけを観測snapshotと分け、再open時の保持をG0で既存state互換性と確認する。旧LOCK／数値／形をDAW stateから復活させない。INV-S14の窓外LOCKとINV-S35の停止時現在値を区別し、手動の全体HOLDは追加しない。

### 4.5 密集した一打を選ぶinteraction

時間marker、値lane、event stripから選ぶ。欠測打に0の値座標を作らず、時刻markerを実時刻からずらさない。ALLの全markerと、BAND最大8のstripを区別する。一候補ならクリック選択、再クリックでLIVE。重なりは最初のpointer-downでevent鍵・時刻順・anchorを凍結したcluster sessionへ入る。

| 操作 | 候補巡回中 | 通常LOCK |
| --- | --- | --- |
| 再クリック／前後ボタン／←→ | 凍結した候補だけ巡回。時刻＋安定event鍵順 | 現在6秒窓を欠測含め時刻順、端で循環 |
| pointer drag | event選択を変えない。新publication／手ぶれでcluster外へ移さない | drag開始時のevent鍵／候補集合を固定し、drag中に新しい窓のnearestを引き直さない |
| HOME | 凍結先頭 | 窓内最古。窓外LOCKの左は窓内最新、右／HOMEは最古 |
| ESC | clusterだけ終了、LOCK保持 | 既存規則と整合をG0で固定 |
| 候補外クリック | 新選択session | 実位置の一打を選ぶ |
| LIVE／END | cluster・LOCK解除 | LIVEへ一操作 |

clusterは新publicationで候補を増減させず、窓外でも鍵を保持して「過去の候補」と示す。未選択候補PCMが失効したら、その鍵の欠測を示し別打へ代替しない。選んだ一打だけsnapshotを保持。source失効・帯域変更・離脱でcluster終了。resizeは鍵を保ってanchorを新geometryへ移す。空窓ではLOCKを保持し、存在しない候補を作らない。

全サイズで `LIVE`／`固定した一打` を区別し、固定表示自体を常設LIVE復帰入口にする。既存NOWは同じ復帰意味へ揃える。cluster chip `候補1/3` と前後操作、根拠入口、LIVE帰還はhoverなしで発見可能。通常drag変更も含め先行input fixtureで契約を固定する。

## 5. TIME・PSR — 六つの整合契約

| 契約 | 必須内容 |
| --- | --- |
| T1 観測事実 | 現在PSRは未集約100 ms raw値と最新None。history mean／decimation／最後のfinite値から復元しない。同cutoffのM／S／TP／PSR／PLR／CORR frameをMeasure Threadの最大64件raw tail（既存LOCAL_JOIN_POINTS）に保持。固定multi-rate retentionを拡張しない |
| T2 比較の証明 | main／PSR別target・cutoff・proofとselection intent、exact PRE locator、opaque binding revision、owner／POST claim、両source epoch／incarnation／generation／rate／layout／run／clockをpacketへ。generationの大小や一致を対応の証明にしない。新pair有効化前に旧比較公開を無効化 |
| T3 時刻と期限 | main最新local cutoff C、PSR比較E≤C。global POST mainはCで更新、PRE待ちへ巻き込まない。global mainΔも独自proof／cutoffで成立判定。共通軸はCで非減少、PSR末尾はEで止める。通常pending保持はPOST slot完了から400 ms未満かつC−Eも400 ms未満、完全binding／両span／runが一致してActive、後続確定invalidなし |
| T4 component寿命 | 正常publication待ちだけ直前同pair比較を期限内保持し沈黙。None、対応不能、stop、bypass、run／clock変更は影響current即退役。pair／PRE失効は比較componentだけ、POST span／rate／layout／Resetは両component。同source過去履歴はHOLD、run／seekはsegment境界、別sourceは破棄。callback退役後にpoll失敗しても旧値を復活させない |
| T5 履歴と提示 | gap／runを跨がず集約、valid数を平均分母とし0置換なし。TIME native10Hzでpacket一括apply、PSRの4Hz間引きなし、PLR／CORRはmain。軸・scale・font固定。main／PSR出力各1200以下、要求は既存幅予算、compact PSR capacity=0 |
| T6 Capture | 採用presentationへ期限／退役を適用後freeze。再poll／source選択／測定再計算0。component target／cutoff／proof／状態／gap／期限と共通軸、DRUM表示kind／event鍵／revision／型付き端点／時計状態が画像と一致。表示丸めでraw端点を変更しない。opaque identity使用、private path／owner管理情報を出さない |

T2の取得順は authority前読み→session `try_lock`（raw／frame／history）→解放→exact exchange `try_lock`（binding付き比較）→解放→authority／signal後読み→最終整合検査→全出力commit。同時に複数lockを持たずGUI pollでfilesystem joinしない。前後authorityへPOST span tokenも含め、session解放後のReset／worker再起動を検出する。通常C進行だけではrejectしない。最終検査をlinearization pointとし、その後の変更はGUI lifecycle境界で退役させる。競合／authority不一致は全出力不変で無言skip。

authorityと比較producer bindingだけが違う場合は、新authorityの比較waiting componentを成功返却する。main target=POSTなら同POST絶対frame Cを継続し、main target=Δならmainも比較waitingとする。local Cは軸の根拠として保持するが、選択ΔをPOST値へfallbackしない。旧比較を混ぜない。既存 `build_observatory_frame()` のlegacy delta別取得を新TIME authorityに使わず、新packetでframeを構成する。GUIは一括applyし、選択callbackで影響componentを先に退役する。

T3の400 msは現行4×100 ms live_windowを参照した表示期限案で、IO最大遅延保証ではない。G0の旧ABI観測ではΔ初観測の供給起点median294～340 ms、max511～539 ms、C−E max400～500 msだった。原slot完了timestampは旧ABIにないため完了起点TTLは未検証。400 msを不要な空欄の出ない保証とせず、期限を恣意的に延長しない。詳細は[TIME契約と計測限界](hypha_drum_psr_g0_20261007/time_capture_contract.md)。poll成功、IO service、同endpoint再joinで期限を延ばさない。GUI monotonic clockでも残り時間を減らし、連続poll失敗中も期限切れにする。単体currentにも測定完了起点の期限とsignal失効を適用する。

T5はNone、未対応endpoint、slot抜け、clock／run変更でsegmentを切り、entryにfirst／last endpoint、total／valid count、min／max／meanを持つ。decimationはsegmentを跨がず、予算超過は古いsegmentを落とす。欠測位置が復元不能な旧bucketを連続mean線にしない。共通rangeは `[max(0,C−durationFrames),C]`、データ上限は各component cutoff。疎なΔ点数で時間幅を作らない。両端を跨ぐbucketは保持exact観測から部分区間を正確に再集約できる時だけ使い、不能なら欠線。座標clampだけで将来mean／min／maxを残さない。最近prefixには既存10Hz exact historyを利用し、64件tailを全履歴へ拡張しない。

T6の旧Capture `target` とWork `observation_target` はmain targetの意味を維持する。global POST＋PSR Δを画像全体のPOST絶対観測とだけ記録せず、version付きcomponent metadataを追加する。consumer互換不成立時は明示添付の失敗を通知し、添付受入のblockerにする。現実consumer fixtureでcompact UUID拒否・v1追加field拒否・Work metadata欠落を確認した。v2は別root／version、canonical dashed UUID、hash付きbounded sidecar、typed Work参照、両hash／stampのreceiptを使う。旧targetの意味保持と現保存済みtargetの存在は別で、現consumerにその永続化はない。v2 round-trip未実施を正式添付blockerにする。metadataを黙って捨てず、ローカルPNGは利用可能。通常待ち／内部fallbackの沈黙と、利用者の明示操作失敗の通知を分ける。

## 6. 動き、応答時間、高級感を同じpresentationで設計する

### 数値、結果の完成、画面移動の速度を別に扱う

| 速度 | 提案／維持する契約 | 測るもの |
| --- | --- | --- |
| 観測・完成 | TIME100 ms。DRUM48 kHz／256 sample hop約5.333 ms、head30 ms／sharp100 ms、BAND必要tail等の定義を維持 | onset、区間終了、worker完了、publication時刻 |
| 取得・失効監視 | DRUM native30Hz、TIME HISTORY native10Hz | 実tick、欠落、期限反映。失効は提示周期を待たない |
| BAND LIVEまとめの提示 | 4Hz（250 ms）を第一案。値・scope・件数・理由・平均図を同revisionで交換 | 取得済みsnapshot→提示≤250 ms＋1正常tick。G0でdevelopmentの2／4／8Hz比較後に一周期固定 |
| ALL最新一打／LOCK取得中 | 同一single stampをnative30Hzで適用。4Hzを適用しない | 選択強調・形・四値のevent一致。terminal LOCKは固定 |
| TIME／PSR提示 | native100 ms一括更新。装飾ease・PSR別4Hz composerなし | main C継続、PSR E末尾、不要な期限切れ空欄0 |
| 選択・LIVE復帰 | cadenceを待たず入力を反映。P95≤100 msを新案とする | 入力→状態表示。worker完成待ちと分ける |
| 統計窓の応答 | 最新最大8打の既存窓を維持 | 調整前の打音が残る時間、exact完成待ち、提示待ちを分離 |

BAND対象選択は数値snapshot cutoff時点の6秒窓で行い、表示時計や間引きでevent／測定／Recordを削らない。ナビゲーションは現在factsで更新し、読取まとめのsnapshot時点をcaptionで区別する。ALL一打の自動選択だけが進み数値が前打に残る構成は禁止する。

8打が全exactでATT10→40 msへstep変化する例では、新打1～3件で中央値10、4件で25、5件で40になる。120 BPM四分音符では5新打まで約2.5秒かかり得る。250 ms提示gateを通っても、この窓応答を速いとは認定しない。時間幅／指標に紐づくexact ageを見せ、利用者が新打を待つか一打を選ぶ判断をU2で確認する。外部ノブ値とATT値の一致は要求しない。

### DRUMの移動時計をpublication到着から分離する

既存の「新target到着ごとに現在位置から100 msで移動」を一定速度の根拠にしない。fact cutoff C、到着時刻、viewport position Vを別にする。同source／run開始時だけsource sample clockとGUI monotonic clockをanchorし、正常範囲のpublicationではanchorを更新しない。

基準案は `V(t)=C0−L×rate+(t−t0)×rate`。Lは有限look-behindで、G0で測った正常到着位相・取得遅延上限＋描画tickを含め固定する。正常jitterは基準時計に対する有界位相として定義し、「各間隔90～120 msなら無限に正常」とは定義しない。独立fixtureのL=150 ms、到着0.10／0.19／0.31秒は設計確認用であり製品保証値ではない。

Vが保持facts Cへ追いつく、source鮮度切れ、確定欠落ならHOLDで止める。回復時は一度だけ新anchorへ整列し、高速catch-up／毎slot easeを使わない。stop／seek／Reset／rate／run変更は旧clockを破棄する。開始時のpre-roll不足は別の初期状態として扱い、Vを未定義区間へ進めて通常speed合格に数えない。移動時計はActiveの実時間再生に限定し、offline／非連続時計はnative停止・境界規則へ従う。Lが加えるmarker提示遅延は別に測り、通常jitterの滑らかさと欠落回復を同じ成功件数へ混ぜない。

Vは画面位置だけで、cohort、onset、測定cutoff、event鍵、振幅を変更しない。ALL最新一打がlook-behindの履歴窓へまだ入らない場合も最新singleの鍵を保持し、画面内の最新一打locatorで時刻と状態を示す。前打を選択強調したままBの値を出さない。履歴markerは実時刻の位置へ現れた時に同じ鍵で強調する。locatorと有限遅延の実寸wire／U2理解をG0の成立条件に含め、意味不明な空白で隠さない。CaptureにC・V・clock状態を同じpresentationから保存する。TIMEはこの新clockへ統一せずnative10Hzを維持する。

### 読める数値と落ち着いた外観

exactは既存の最近接丸め規則で表示し、−0は正規化する。限界／区間は表示精度でも区間を狭めない。下限は−∞方向、上限は＋∞方向へ丸め、open／closedと型を保つ。例：整数表示の≥146.6 msは≥146 ms、0.1 dB表示の≤−52.06は≤−52.0、[−10.14,−9.96]は[−10.2,−9.9]。丸めた端点が同じになってもexactへ昇格しない。精度はbandのproducer分解能に合わせ、型と値を別々に変更しない。LEVELの絶対値1000以上は共有指数を固定unit欄へ出す。例：[−3202.6,+3202.6]を[−3.21,+3.21]／×10³ dB。指数は両端へ適用し、外向丸めとraw端点を保持する。全有限型の最大指数308も例外fixtureへ含め、cap・ellipsis・font縮小を使わない。

同size／locale／mode／文字役割でfont、baseline、符号・桁幅、単位、scope、件数、caption anchorを固定する。tabular数字と最大桁予約を使い、値→限界→欠測や7/8→8/8でfont縮小、count追従、行高変化をしない。軸scaleはmode／bandで固定。通常pendingは予約欄の状態名を維持し、件数だけを更新する。毎pollの全文通知、点滅、色往復は0。確定不明／不成立と、明示選択の失敗は理由を読めるようにする。

CE2226の暗い暖色の面、champagne goldのPOST、cyanのΔ／選択、PREの既存warm neutral、低明度素材、固定した一光源、ivoryの数値を維持する。背景の自律animation、全面発光、強い二重枠を増やさず、余白を主値・scope・curve境界へ使う。PRESENCE値は変更しない。静止画だけでなく実再生映像と実UIで評価し、測定値の平滑化や欠測の隠蔽で落ち着きを演出しない。

## 7. 独立期待値と受入方法

試験はproductionの集計を試験側へ複製せず、独立した数値と鍵を固定する。時刻／publication／raceは注入し、sleepの偶然で再現しない。以下の正式製品fixtureは未実施。G0の独立算術・時計模型と限定wire／consumer fixtureは[G0証拠](hypha_drum_psr_g0_20261007/README.md)へ区別して保存し、製品fixtureのPASSへ流用しない。

### 7.1 技術fixtureの反例表

| ID | 独立入力・操作 | 合格条件 |
| --- | --- | --- |
| P1 | raw値／Noneを全表示期間・decimationで取得 | current値は期間非依存、None即退役、last finite復元0 |
| P2 | PRE遅れ1～3tick、age299／399／400 ms、poll失敗／再join | main C継続、PSRだけ期限内保持、元期限延長0、正常待ち文字／色往復0 |
| P3 | 各取得境界でpair／owner／epoch／POST spanを変更、callback直後poll失敗 | 混在0、失効span成功0、FFI出力不変、旧比較復活0、main POST選択時は同POST継続、mainΔはwaiting |
| P4 | gap／slot抜け／range両端bucket、E後だけ極値100 | valid分母、gap維持、PSR prefixに100混入0／main Cには含む、不能bucket欠線、各1200以下 |
| P5 | global POST＋PSR ΔのCapture、期限／失効直前直後、旧consumer | 再poll0、画像／component metadata一致、誤添付0、compact追加poll0 |
| P6 | HOLD C=10.0→E=9.8／C=10.1秒、stop／seek | 同span軸逆行0、PSRをCへ延長0、seek境界を区別 |
| P7 | 全体POST／ΔとPSR target組合せ、全size／日英 | PLR可視性・CORR source／軸／helpはmainだけ |
| D1 | 10件末尾8へSilent／pending | ID3～10が対象、過去補充0、五分類合計N |
| D2 | REL20、20、N/A | 全3なし、確定20 ms（2/3）＋不成立1 |
| D3 | REL10、10、≥20、および0、10、≥10 | 全3中央値は10 ms。後者の確定部分5 msは詳細、全3打exactとは表示しない |
| D4 | ΔREL−10が1、≥＋200が7 | 主値全8≥＋200、確定−10（1/8）は詳細。SHORTER／1/1表示0 |
| D5 | REL20が1、NextHitが7 | 全体なし、確定20（1/8）、不明7 |
| D6 | PRE−20／実測POST≤−72、両Silent、ArrivalRinging＋Rises | 片側Silent時間lane不成立、ΔLEVEL≤−52、両Silent差不成立、Rises REL保持 |
| D7 | A PRE−20／POST−18、B PRE−40だけ、全未観測 | 比較平均はAだけ、両count1。count0は線／塗りなし、床を含めない |
| D8 | PREarrival[0,10,11]、POST[10,0,12]。PREpeak0＋REL100／POSTpeak10＋REL105 | per-hit Δ中央値＋1、ΔREL＋5（終了差＋15を使わない）、図／数値一致 |
| D9 | PRE-only、POSTイベント0、同clock／held PCM、後着Matched／span変更 | POST窓実測、pair kind維持、同spanのみcache reuse、proofなし差分0 |
| D10 | A−20／−10、B−60／−60、集合11→01、01→10 | 平均−40／−35→−20／−10を接続しない。同countの集合変更も断線 |
| D11 | [0,1)、[0,1]、[0,1]の全順列、open端2件の上下 | [0,1]、上端open2件は[0,1)、下端open2件は(0,1]。順列不変 |
| D12 | 偶数[0,1)／[2,3]、>10／≥20、invalid区間、表示丸め | [1,2)、>15、invalid数値0。≥146.6→≥146、≤−52.06→≤−52.0、[−10.14,−9.96]→[−10.2,−9.9] |
| D13 | ID1～8（値1～7／8pending）→ID2～9（2～8／9pending）→9exact | 全体pending→pending→5.5。確定4（7/8）→5（7/8）→全8 5.5。旧cohort持越し0 |
| D14 | ODF一致／band意味不一致、旧peer、双方一致 | 比較cache／Δ authority不成立、local POST cache可。両意味一致のみ比較 |
| D15 | 8exact ATT10→40の新打1～5。別cohortでATT最新exact0.1秒前／REL exact1/8は3秒前 | median10／10／10／25／40。窓応答と提示遅延を別計測。指標別ageと共有最大3秒が正しく、RELをfreshに見せない |
| L1 | BAND単打・有効pre-roll、acquiring→late complete、onset＋150／100 ms stop、worker reply200 ms遅延 | stop単独terminal0。150 msは有効ATT／LEVEL・REL bound、100 ms不足は指標別欠測。窓外・resizeで同一打保持 |
| L2 | cluster click→時間進行／新打→数px drag→up、keys、窓外／PCM失効、100→300% | 凍結鍵／順序不変、cluster外選択0、時刻ずらし0、選択単打形一致、LIVE一操作 |
| L3 | source／意味／seek／Reset／帯域変更／離脱／再open | 旧snapshot復活0、選択意図と新LIVE観測を区別、明示取得失敗の理由 |
| L4 | A−30 dBFSを0 ms、B−10を125 ms、250 ms境界前にB選択 | ALLのlocator／可視marker強調／shape／四値はBの同stamp。A値の4Hz保持0 |
| U1 | 5size×日英、ALL／8band、全N↔確定部分、区間／欠測／最大桁、拡大表示 | 切れ／重なり0、意味・scope・単位・分解能可視、固定anchor、Capture／input座標回帰 |
| F1 | Release、ALL／BAND、二枠、密集、V2、LOCK／resize、DPI | 明示有効化した既存frame budget PASS。skip／DebugをPASSにしない |
| F2 | 値桁／件数／型の変化、native tick、summary cadence、入力応答 | 別revision混在0、位置変化0、通常pending往復0。技術的遅延・欠落を測る。利用者の高級感採点は含めない |
| M1 | 固定clockに有界位相0.10／0.19／0.31秒arrival、L=150 ms | 通常jitter区間のV傾きrate、毎到着reanchor／停留／加減速0。source時刻／振幅不変 |
| M2 | C到達／鮮度切れ／欠落→回復 | HOLD停止、一回整列、catch-up burst0。marker表示遅延とlatest locator identity一致 |
| M3 | stop／seek／Reset／rate／run変更 | 旧移動clock継続0。BAND母集団cutoffをVへ置換0 |
| E1 | 付録Aの別計画へ移した | 本計画では判定しない。検出sourceの不変と既存検出評価の回帰はG3で確かめる |
| U2 | 初見理解、外部調整→glance→LOCK→再調整→LIVE、100／200／300% | 以下の理解／日常操作／品位rubricを満たす。技術PASSで代用しない |

追加境界はATT帯域一周期、LEVEL無音閾値前後、PRE-only／POST-only／ambiguous、pending→terminal、停止中帯域変更、再起動、ファイル不在、interval無限側。旧／新ABI、短buffer／未知version、出力不変、worker応答不能も含める。

### 7.2 検出は変えず、既存の評価を回帰として守る

本計画は打音の検出（ODF、onset判定、30 msの非連鎖compound、帯域の分割）を変えない。既存kick／snare各50音・二群以上、先頭250 ms coverage≥0.95を維持し、一音ごとの検出精度とは呼ばない。既存recoveryの正式development／holdout、fold、transform別gateと最低構成（30 performance ID・15分等）も縮小しない。

G3では、検出に関わるsourceとODF意味identityが既存評価の対象と同じであることを照合し、その範囲だけ既存評価を引き継ぐ。正式候補では既存の自動検出試験を回帰として実行する。表示・集計・単打取得の変更で検出が変わった場合は本計画の受入を止め、付録Aの別計画で検出を再評価してから戻る。古いMIDI成績を現候補の合格へ転用しない。

### 7.3 初見、日常操作、高級感を利用者で確かめる

人による受入は、開発責任者本人と友人1～2人で行う。この人数の結果を全利用者への統計的保証とは呼ばず、一人の重大誤認も未達として残す。参加者の名前・連絡先は計画・repo・証跡に書かず、指標の経験（初心者／経験者）だけを記録する。

| 参加者 | 行う課題 | 扱い |
| --- | --- | --- |
| Hyphaの画面を見たことのない友人 | 初見課題1～5、その後の日常操作と品位 | 初見を判定できるのはこの人だけ。development中のwire・試作・説明を事前に見せない |
| 開発責任者本人、画面を見たことのある友人 | 日常操作と品位 | 初見の判定に数えない |

画面を見たことのない参加者がいなければ、初見理解は未検証として残す。英語の初見は、英語を常用する参加者がいなければ未検証とし、英語表示はU1の切れ／重なりと文言reviewで確かめる。日本語の結果を英語の合格へ転用しない。U1や技術PASSで初見理解を代用しない。

初見の参加者は5/5課題を各120秒以内に無介助完了、重大誤認0。UI help可、事前講義／口頭誘導／外部manual不可。開始は100%。最初に課題3の主値／scopeを100%主面だけで無介助回答し、200／300%の根拠面や説明を先に見せない。主面の未回答／重大誤認は未達として残す。その後にPSRは125%以上、平均図は200%以上等の必要sizeへ進み、残る理解課題はUI help可。学習後の日常試験のsize交互順で、この最初の100%判定を置き換えない。

1. POST単体／差分、PSR paired／soloを見分ける。
2. ATT／REL／LEVELの定義とコンプレッサー設定値の違い、平均図と中央値の違いを読む。
3. 全8≥＋200／確定1/8=−10、全N未成立→確定7/8→全8の連続切替を見て、確定部分を全体効果と誤認しない。
4. 限界／不明／不成立、分解能、ALL時間軸／BAND値軸を見分ける。
5. 欠測打と近接候補を選び、6秒超保持して読み、LIVEへ戻る。

初見の参加者は理解課題の後に、他の参加者は最初から日常操作を試す。100／200／300%順を交互にし、120 BPM四分→16分→roll、長尾／全pending、solo→pair、旧PRE、poll jitter、look-behindを含める。初見結果と学習後の結果を混ぜない。

| 日常操作 | 新しい受入案 | 記録 |
| --- | --- | --- |
| 外部調整→Hyphaへ戻る | 5秒以内にtarget、LIVE／固定、指定ATT／RELのscope・値／限界／欠測を正読 | glance時間、誤読、help／hover数 |
| step変化と最新8打の混在 | captionから古い打音が残ることを理解し、待つ／一打を選ぶ判断ができる | 調整前cohort／古いREL exact1/8を最新調整の結果とする誤認0、指標別age・窓と提示遅延の区別 |
| 近接した欠測一打を選ぶ | 巡回入口を発見し、動くmarker／latest locatorから意図鍵を選べる | 誤選択、巡回数、drag手ぶれ、入力応答 |
| LOCK中に再調整→LIVE | 固定値を現在調整の結果とする誤認0、帰還一操作 | mode誤認、操作数、旧値持越し |
| stop／再開／離脱／再open | 停止と取得完成、新LIVE、帯域意図を区別。数値消失／点滅で読めなくならない | 欠落時間、理由理解、再設定数 |

動的な品位は実UI／実再生映像で、全員が5段階4以上の「数字を追える」「動きが落ち着く」「操作応答が一貫する」を満たす案。低評価理由を残し、平均で個人の重大な読めなさを隠さない。F2の位置／revision／cadence技術PASSとは別判定。静止画・frame budgetだけでPASSにしない。是正は配置／周期／導線／説明の原因別に行い、条件を下げない。是正後の初見は、まだ画面を見ていない参加者でだけ判定し、いなければ未検証として残す。以前の参加者による再確認は診断として記録する。外部募集・連絡・データ送信は本計画から自動実行しない。

### 7.4 性能、既存回帰、実host

Releaseで `KIRIN_ATTACK_FRAME_BUDGET=1` を明示しAttackFrameBudget／BandFrameBudgetを実行。env未設定skip／Debug report-onlyはPASSでない。既存1枠中央値12 ms／2枠16 ms、更新最大24 ms、初回80 ms、resize中央値16 ms／最大40 msを維持する。5size、DPI1／1.25／2、密集二枠、V2、LOCK取得／解除／resizeを含める。TIMEは既存render gate、追加取得時間／容量／hidden poll数も確認する。

Rust変更は `cargo test --workspace` と `cargo clippy`、FFI変更はparity／pairing_candidatesのignored一覧件数を実測して全件 `--ignored --test-threads=1`。Record／pairing／bit identical／0 latency、worker再起動、ファイル不在を含む。JUCEはAttackUiContract／TimeHistoryContract／ObservatoryViewContract、Capture／help-line／translation／size／ABI、`check_screen_text.mjs`、source line budget。使い捨て `KIRIN_HYPHA_TEST_STORAGE_ROOT` だけを使い、実plugin_data／DAW設定を対象にしない。

macOS AU／VST3／AAX、Windows VST3／AAXの対象候補でmono／stereoを実host受入する。DRUMの2MIX／exact5.1禁止gateを維持し、native fixtureを実host証拠にしない。Windows操作前は専用runbook。未署名診断buildは採用checkoutの `scripts/build_hypha.mjs`、`--without-aax`をAAX受入にしない。実機接続・公開は別の明示された工程とする。

## 8. 依存関係に沿って実施する

### 影響範囲を先に揃える

| 責務 | 対象file family |
| --- | --- |
| TIME事実／authority／履歴 | meter_session、meter_delta_history、meter_history*、io_thread_post_observation*、pair_binding、meter_observation_ffi、publication／codec／C header |
| DRUM事実／統計／identity | attack_ffi_band_summary／band／map、attack_band*、attack_exchange_join、attack_runtime*、C header／ABI |
| 主面／根拠／interaction | HyphaAttackBandSummary*／Panes／View、model／component／interaction／chrome、lane／glance painter |
| snapshot取得／寿命 | PluginProcessorAnalysis、PluginProcessor／Editor、editor ATTACK／Observatory取得 |
| PSR・時間表示 | PluginEditorObservatory、HyphaObservatoryView／Frame、footer、HyphaTimeHistoryLayout／Painter |
| Capture／説明 | HyphaCaptureContract、PluginEditorCapture、HyphaObservatoryCapture、CaptureWorkAttachment、locale／help-line |
| 正本・gate | README、不変条件、meter／DRUM／Capture／visual契約、関連fixture／manifest／source budget |

採用sourceを確定し、実装前に全対象を列挙する。500行超sourceは変更責務を先行して500行以下moduleへ抽出し、既存巨大fileを増やさずratchetを更新する。無関係な責務の一括分割をUI着手条件にしない。別checkoutの絶対path scriptでbuildを代用しない。

### Gateと正式候補の順序

| Gate | 具体的な成果物 | 次へ進む条件 |
| --- | --- | --- |
| G0 利用場面と設計固定（資料作成済み・成立保留） | 実寸wire、主値／scopeと根拠面、ALL locator／cluster／drag／LOCK、C／E／Vとexpiry、ABI／意味identity／Capture consumer、cadence／jitter／表示遅延、development baseline、利用者評価のrubricと参加者の割当 | 主面に収まり操作が成立。契約の曖昧さを解消し、候補と利用者受入を見る前に表示周期／正常jitter／閾値／採点方法を固定。製品のsource・ABIを変えるcommitを入れない |
| G1 観測・整合snapshot | bounded TIME raw／proof／gap、DRUM固定cohort・区間・mask／集合、POST窓実測、producer finalization、V2 | P1～P4・P6、D1～D15、ABI／wire／応答不能の独立期待値一致 |
| G2 全面統合・development検証 | 主面／根拠面、単打／locator／cluster、提示時計／丸め、PSR／PLR／CORR、Capture／説明、全size／locale。README／invariants／製品・visual・Capture正本を同時同期 | P5・P7・L1～L4・U1・F1・F2・M1～M3、既存回帰。初見を担う参加者には見せず、開発中の確認は診断のみ |
| G3 最終候補freeze・技術受入 | 同期済みclean exact commit、definition／settings hash、検出sourceと意味identityの照合、正式候補の技術・対象host証拠 | 適合する同candidateの既存run／artifactを照合し、既存の検出試験を含む必要gateを正式実行。技術／host不足のまま利用者評価へ進めない |
| G4 利用者による判定 | 画面を見たことのない友人の初見、開発責任者本人と友人の日常操作・品位 | U2。該当者がいない項目は未検証として残す。FAIL／未検証を平均や別条件で相殺しない |
| G5 同一候補の最終照合 | read-onlyの契約／実装／画像／metadata／証拠一覧、未処理項目 | freeze後のsource／docs drift0、対象candidateとPASS証拠一致。全gateが揃って製品改善完了 |

development検証はfreeze前に反復し、正式候補の受入と分ける。正本編集はG2までに終え、G5で編集して候補commitを変えない。freeze後に必要な修正を見つけたら新candidateとして再freezeし、影響gateを再受入。画面を見た参加者の結果を、新候補の初見合格へ転用しない。文書だけのdriftも証拠との対応を再確認し、旧commit成功を新commit PASSと呼ばない。

G0は今行う。製品のsource・ABIを変えるG1以降は、1.1.51をLS・HP・Windowsの三チャネルで公開した後に始める。G0の文書・試作・限定観測は実施したが成立判定は上記の理由で保留。G1以降は未実施。push／CI前は共通予算ルール、workflow trigger、exact commit／inputs／OS／署名条件の合うrun／artifactを確認し、必要なgateを維持して重複を避ける。今回CIは起動しない。署名・公証・公開は別途承認されたrelease工程で同一候補のLS／macOS HP／Windows三チャネルと正式gateを満たす。

## 9. 最新の厳しめレビューを、構造へ反映したか

| 残っていた問題 | 構造上の修正 | 検証先 |
| --- | --- | --- |
| ALLの4Hz数値と30Hz選択が別打になる | 4HzはBANDまとめだけ。ALL／LOCKは同single stamp | L4・F2 |
| stopでterminal化し、正当な部分結果を捨てる | transport状態とproducer finalizationを分離 | L1・G1 |
| bound丸めが区間を狭める | formatterの下限floor／上限ceil、端点型保持 | D12 |
| 100 ms再anchorで速度が変わる | facts C／到着／固定移動時計Vを分離、有限遅延も測る | M1～M3・U2 |
| 8打窓の遅さを250 ms gateが隠す | 窓応答と提示遅延を分離、時間幅／exact ageを表示 | D15・U2 |
| cluster dragで凍結候補を離れる | cluster中drag選択変更0、通常dragも開始集合固定 | L2 |
| 技術gateに初見／品位受入が混ざる | G2技術・development、G4利用者評価（初見は未見の参加者だけ） | F2・U2・G2～G4 |
| freeze後の正本同期でcandidateが変わる | G2正本同期→G3 freeze→G5 read-only照合 | G3～G5 |
| ATTのfresh ageで古いREL条件付き値まで新しく見える | 指標別age、共有captionは表示中条件付き値の保守的な最大age | D15・U2 |
| 大画面で学んだ後の100%を初見合格にできる | 全員100%主面のscope課題から開始、詳細の先見せなし | U2・G4 |

以前のレビューで確定したraw PSR／完全binding／component退役／gap／Capture target、型付き全N区間／条件付き統計／参加集合、PRE-only実測／意味identity、窓外LOCK／小画面／初見の再受入も第3～7章へ、弱打個別gateは付録Aへ統合した。過去版やレビュー表を別途読まなくても実施できる一つの契約にする。

## 付録A 検出の再評価 — 別計画へ渡す案

2026年10月7日の決定により、以下は本計画のG0～G5の条件ではない。検出を変える別計画で使う案として保存する。担当者と隔離方法が揃うまで、検出の追加受入は未検証として残す。

追加受入案はPrecision≥0.85、Recall≥0.75、F1≥0.80、FP/s≤1、signed timing median絶対値≤5.333 ms、absolute error P95≤15 ms、kick-only Recall≥0.75、hat-only Recall≥0.50。pooledと演奏macroを両方判定し、matched0をtiming PASSにしない。古いMIDI成績を現候補の合格へ転用しない。

候補blindの二annotatorが音声から構成onsetを整数µs half-upで記録する。MIDI／候補出力は見ない。独立一致F1≥0.90を要求し、曖昧箇所の第三者裁定／除外理由を候補score前に確定する。最初のonsetから30,000 µs以内の非連鎖compoundを作り、代表時刻は構成onset算術平均half-up（0／30 ms→15 ms）。種別／弱打／重なり属性を集合で保持する。30 ms未満連打は個別分離限界の診断を別にし、compound成功を全構成打撃成功に数えない。

予測sample p／rate rと正解µs lはi128の `abs(p*1,000,000−l*r)≤25,000*r` が対応条件。最大対応数→総絶対誤差最小→早い正解→早い予測で一対一割当。duplicateはFP、一予測で複数TPを作らない。inclusive境界／tieをE1で固定する。

追加集合案は独立20clip／評価可能200compound以上／二source群以上。kick-only／hat-only各50、通常打／weak-only・ghost-only／30 ms超dense・roll／重なり各50以上、各条件二演奏・二source群以上、背景静音／持続／noise計60秒以上。同event条件重複可だが分母を明示する。weak-onlyは全構成打音が弱打で、通常強打との近接併合は含めない。weak-containing混合は別診断。弱打定義・可聴性はdevelopmentで固定し、頭30 ms RMSと通常打相対levelを併記。候補blindで可聴とされた弱音をmeasurement floor近傍だけで除外しない。

通常／weak-only／dense／重なり各Recall≥0.75を追加hard gate案とする。pooled・macroの余裕で相殺しない。TP150／FN50／FP0ならpooled F1≈0.857でも弱打50全missはFAIL。件数不足は未検証、全missはFAIL。PRE／POST対応率、duplicate／merged、条件別timingも報告する。

baseline・原因調査・threshold調整はdevelopmentだけ。演奏／source／派生render group単位で分け、重複音声0。manifest、除外、正解schema、閾値、評価規則を事前凍結し、holdout音声／labelは開発者から隔離して候補blind担当が準備する。exact候補、ODF／band意味、settings、evaluator、manifest／annotation hashをfreeze後、fresh holdoutを一度評価。開封後FAIL素材はdiagnosticへ移し、是正候補の合格には新しい未開封holdoutを使う。失敗素材の修正確認をfresh合格と呼ばない。担当／隔離方法が無ければ未検証として残す。

別計画の技術fixture案E1：音響29／30／31 ms、25 ms境界／tie、TP150／FN50weak-only／FP0、ghost0＋強打20 msで、上記の音響評価規則と一致すること。weak-only Recall0はFAIL、混合TPをweak-only分母へ入れない。

## 参照、確認範囲、申し送り

source参照はローカルB-1300 `bcb9ffd3b3913e9d5c4a526df38b4a7f430be99d`。原文はB-1143 `6ba9fe20e681d369c1de08e1c74b1ef2406dc010` の作業treeに未commitで置かれ、同日にB-1299 `0d89506ed276c37b041803704b87f7dbe94e2fa0` を基に保存した。既存変更を保持し、実装時は採用sourceと候補を先に決める。前のB-1300 DRUM UI／TIME HISTORY native fixture PASS、日本語BAND100／125／300%確認は現状調査の証拠であり、本計画・検出精度・初見／日常操作・高級感・実hostの合格ではない。

正本は[不変条件](../hypha_invariants.md)、[meter product contract](../hypha_meter_product_contract_20260831.md)、[DRUM band view plan](../hypha_drum_band_view_plan_20260928.md)、[DRUM lanes](../hypha_drum_lanes_20260924.md)、[打音pilot report](../transient_delta_phase2_drum_pilot_report_20260830.md)、[評価recovery契約](../transient_delta_phase2_recovery_plan_20260830.md)、[CE2226 visual system](../hypha_ce2226_jungle_visual_system_20260901.md)、[release入口](../hypha_release_entry.md)。実装と正本が異なる箇所を新表示契約として明示し、無断でPhase 2へ送らない。

今回の追加確認は、利用者決定patch適用、schema／寿命／Captureの読取と設計、5サイズ日英の合成wireの280条件、独立モデル23項目、既存libの限定native観測、現consumerの使い捨てC1～C4。製品source・ABI変更、製品build、CI、実DAW／機器、v2 round-trip、参加者受入は未実施。native libのexact source provenanceも未確定。詳細と保存証拠は[G0成果一覧](hypha_drum_psr_g0_20261007/README.md)。

次はG0の残る成立条件の解消方法を確定する。完了timestampを含む新APIが必要な証拠を、旧ABIや模型PASSで埋めない。G1以降の製品変更は1.1.51三チャネル公開後に行い、G0／各gateの未達を明示する。検出変更と追加音響評価は本計画へ戻さない。LS／HP／Windows配布準備はskip。

セッション記録手順を読み、プロジェクトのNotion書込み禁止を優先してSECTION:DEV、日次ログ、Notion Handoffは未記録。[G0 READMEの現在地・日次・Handoff](hypha_drum_psr_g0_20261007/README.md)へ未記録内容を残した。製品改善完了・実host・使いやすさ・高級感を認定した記録ではない。
