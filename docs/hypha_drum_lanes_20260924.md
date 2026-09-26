# DRUM lane表示と4量の精査

- **作成日**：2026-09-24
- **状態**：B-1015で画面側を実装し、2026-09-25に厳しめレビューの指摘13件を修正した。計測core側の3件はB-1016で実装した（§4）。B-1022でPhase Dの立ち上がりを除き、B-1024でdetailを頭→完全の2段にした（§4.1）。
- **変更範囲**：B-1015はJUCE native描画、表示契約、UI契約test。B-1016／B-1024は`kirin_measure`のATTACK detail、PRE→POST exchange codec（版3）、FFIのdetail layout（同じ512 byte）とJUCE表示。Audio Threadの仕事は変えない。
- **対象**：TRACK / STEMのDRUM ATTACK。
- **置き換える文書**：`hypha_attack_visual_completion_proposal_20260909.md`の中央標本、Texture、300%配置の各節。

## 1. 決定（2026-09-24 Daisuke）

1. 下部の中央標本（水中生命体の画像と3数値）を撤去し、HISTORYを大きくする。
2. HISTORYの下へ、同じ6秒時間軸を共有する4本のlaneを置く。
3. laneはPAIR時に打音ごとの`POST − PRE`だけを描く。PRE未接続時はPOSTの絶対値へ切り替える。
4. 300%（Inspection）だけ、選択打音の130 ms窓を拡大するloupeを置く。
5. 100%と125%はHISTORYと、選択打音の4値を1行で示す。
6. 選択打音は菌糸として描く。
7. 4量はTRANSIENT / STRENGTH / CREST / SHARPNESSとし、TEXTUREをCRESTへ置き換える。
8. 定義の欠陥は、今回は画面側で値を出さないことで止血し、計測core側の修正は別Bで検証してから入れる。
9. per-hit SHARPNESSは、区間の修正が入るまでPRE不在時も表示せず、差分だけを出す。
10. 2026-09-25（番人廃止後、Daisuke「進めて下さい」）：計測core側の3件をB-1016として実装する。§2.2の推奨どおりTRANSIENTはATTACK−BODYへ置き換え、SHARPNESSの区間は打音から始め、POSTはPREの検出位置で計算し直す。

## 2. 4量の精査

### 2.1 方法

Pro Tools付属`Simple Drum Loops`の48 kHzループ82本（Kick 21 / Snare 21 / Hats 20 / Claps 20、計701打）を使った。
各ループへ次の6処理をかけてPOSTとした：gain +6 dB、速いcomp（0.5 ms / 60 ms / 4:1）、遅いcomp（20 ms / 80 ms / 4:1）、peak−6 dBのhard clip、3 kHz high shelf +6 dB、fast/slow envelope比のtransient shaper。
打音検出は製品と同じ`analyze_drum_attacks_interleaved_offline`、特徴量は`AttackPerceptualFeatures::analyze`、SharpnessはDRUM detailと同じ100 ms格子規則の`SharpnessContinuousAnalyzer`を使った。
処理は監査用の簡易実装であり、plugin実機ではない。素材は打ち込みで打音間が無音のため、被りのある録音ドラムより無音直後の打音が多い。
PRE/POSTの対応は製品の`best_match`ではなく、25 ms以内の最近傍で近似した。音源は読み取りだけに使い、repositoryへ入れていない。

### 2.2 結果

| 量 | 実データの事実 | 判定 |
|---|---|---|
| STRENGTH | gain +6 dBでΔが正確に+6.00 dB | 維持 |
| TEXTURE | 701打の中央値0.00–0.01、上位10%でも0.04以下。−6 dB clipでもΔ 0.00–0.02。KickはLFのためedge項（184/210）、他はcrest>12 dBのdensity項かplateau項で0に張り付く | 撤去 |
| TRANSIENT | 直前100 msが無音の打音が43%（Kick 30%、Snare 85%、Claps 82%）。その値は+100 dB前後で、ΔはSTRENGTHのΔと一致し、gain +6 dBでΔ+6.00になる。全処理を通したΔ相関はSTRENGTHと+0.77 | 無音直後を除外して維持 |
| SHARPNESS | 100 ms窓が打音ではなく固定格子に揃う。同じSnareの同じ打音を10 msずつずらすと0.63–2.97 acum。40%の打音で窓が頭を約9 ms欠く。PRE/POSTは同じ窓なのでΔは同条件 | 差分だけ表示 |
| CREST | gainでΔ 0.00、clipで−2.7〜−4.9 dB、速いcompで+4.8〜+9.6 dB（頭のpeakが残り本体が下がる） | TEXTUREの代替として採用 |
| ATTACK−BODY（監査用のみ） | 頭30 ms RMS − 後続100 ms RMS。gainでΔ 0.00、遅いcompで+2〜+6 dB、shaperで+1〜+5 dB。他量とのΔ相関+0.07〜+0.21 | 計測core側の候補 |

PRE/POSTのonset sampleは、gain +6 dBだけでも7–26%の打音で異なった。
窓がずれたまま差を取ると、本来0のΔCRESTに最大+0.7 dB、Hats shaperのΔTRANSIENT中央値に2.6 dB（+5.57 vs 揃えた場合+8.18）の差が出た。

## 3. 画面契約

### 3.1 配置

`attack_ui::layoutFor`が正本であり、5基準寸法を`static_assert`で固定する。

| 表示 | body | 配置 |
|---|---:|---|
| 100% | 292×94 | header、HISTORY、時間軸、選択打音の4値1行 |
| 125% | 363×128 | 同上 |
| 150% | 434×164 | header、HISTORY、時間軸、4 lane |
| 200% | 580×248 | 同上 |
| 300% | 872×412 | 同上＋HISTORY右列のloupe |

laneはbodyの13%（18–52 px）を1本ずつ受け、HISTORYが残りを受ける。
lane配置では全行が同じ横plot列（左にlabel列、右にreadout列）を共有し、打音はHISTORYと各laneで同じxに立つ。
label、plot、readout、loupe、1行表示の各cellも`attack_ui`の関数（`plotColumn`、`lanePlot`、`readoutCell`、`lineCell`など）が正本で、描画、pointer判定、testが同じ値を使う。5基準寸法でplot列の共有を`static_assert`で固定する。

### 3.2 lane

| lane | PAIR時（POST − PRE） | PRE未接続時（POST） |
|---|---|---|
| TRANSIENT（頭 − body） | ±12 dB | −12..24 dB（0 dBから伸ばす） |
| STRENGTH | ±12 dB | −72..0 dBFS |
| CREST | ±12 dB | 0–24 dB |
| SHARPNESS | ±1 acum | 0–8 acum |

dB laneは同じ変化が同じ棒長になるよう尺度を共有する。範囲外の値はlane端でcapを付け、正確な値はreadoutに出す。
増減は棒の向きで示し、良し悪しの色を使わない（R-22）。

次の打音は値もΔも出さず、zero lineに中空の印を置き、選択時はreadoutへ理由を出す。

| 条件 | readout |
|---|---|
| 次の打音が近く、bodyが20 ms未満になる（TRANSIENTのみ） | `NEXT HIT` |
| PREかPOSTのbody RMSがHISTORY下限（−72 dBFS）未満。デジタル無音に限らない（TRANSIENTのみ） | `QUIET AFTER` |
| PRE-only / POST-only / ambiguous | `PRE ONLY` / `POST ONLY` / `NO PAIR` |
| detail未着、PREの位置でのPOST測定がまだない、非有限値、Sharpness欠測 | `--` |
| 頭だけ測定済み（bodyがまだ確定していない、または再生停止で来なかった）のTRANSIENTとSHARPNESS | `--` |

対応が取れた打音では、POSTをPREのonsetで同じ窓のまま測り直す（§4）。PRE/POSTの差はいつも同じcontent sampleどうしの差になる。
B-1015の`ONSET DIFFERS`と`QUIET BEFORE`は、B-1016で原因がなくなったため撤去した。

readoutは各laneの量だけを示す（PAIR時は`POST − PRE`、PRE未接続時はPOST値）。打音ごとのPRE値、POST値の内訳は出さない。
時間軸行の打音数は、laneが6秒内に描く列の数と一致させる（detail未着のeventは数えない）。

### 3.3 選択とloupe

選択打音は静的な菌糸線1本でHISTORYから最終laneまで貫く。
横揺れは打音sampleで決まる決定的なdriftで、正確なxから±1 px以内に留める。時間では動かさない。
細い芯に淡い発光を添え、HISTORY中央のbulbと下端の先端を持つ。

値は菌糸線に従う。LOCKした打音が再生で6秒の外へ出た後もLOCKは保つが、菌糸線と同じく、lane readout、1行表示、選択時刻、loupeもその打音を示さない（`--`、`NO HIT`）。ENDまたはNOWでLIVEへ戻る。
LIVE / HOLD / LOCKは時間軸行にも出す。header右の状態表示は幅470 px以上だけなので、100%〜150%では時間軸行がHOLDを示す。

loupeはInspectionだけに置く。
横軸は打音を含むbinの20 ms前からbody最大長の終わりまでの150 msに固定する。
選択打音の`shape[96]`（その区間のうち測定済みの部分の区間内peak）をPRE traceとPOST bodyで描き、頭30 msとbodyのRMSを段として描く。
run開始前や未測定の部分には何も描かない。頭だけの打音はbodyの段を描かない。
頭の段の高さがSTRENGTH、頭からbodyへ下がる段差がTRANSIENT、頭のpeakと頭RMSの差（橙のbracket）がCRESTである。
対応が取れた打音はPOSTもPREのonsetで測るため、PRE/POSTの窓と段は同じ位置に並ぶ。

### 3.4 外観

2026-09-24のDaisuke指示「テンションが上がる、スタイリッシュな見た目」を受け、2026-09-26に2.5Dの奥行き（A3、強め）とHypha共通配色を実画像の比較で決めた（Daisuke）。GPUは使わず、JUCEの2D描画で光、素材、奥行きを作る。立体化は器（ガラスの溝、光、素材）に持たせ、値を読む線と数字は鋭いまま残す。調整値は`attack_depth::look()`（`HyphaAttackDepth.cpp`）が正本である。

- HISTORY：10 ms RMSは符号を持たない量なので、帯の床から全高へ片側で立ち上げる（旧：中央から上下対称）。同じ高さで縦の分解能が2倍になる。各帯に−24 / −48 dBFSの線、12 dBごとの壁の刻み、床線を置く。
- 光：POST包絡は床へ向かって減光する塗り、縁をまたぐガラス管の壁（内側が壁、外側がglow）、縁のすぐ内側のkey light、主要なpeak（帯上部30%より高いもの）の小さなglintで描く。PREは明るい無彩色の細い実線をPOSTの上に描き、POSTの塗りに埋もれさせない。
- 時間の奥行き：測定した光は古いほど薄くなる。NOWで1、−6 sで0.28（直線）。包絡の線と塗り、glint、laneの棒が同じ規則に従う。背景は暗くしない。
- 器：計測面は凹んだガラスの溝として描く（上と左の内側の影、下と右の縁の反射光、上辺の切り欠きの影、HISTORYだけに端の減光と中央の淡い光）。すべて寸法ごとに一度だけ画像へ描き、毎frameは描かない。
- lane：0線は象牙色の細いレールとし、色は棒とlabelとreadoutのaccentだけに置く。棒はkey light側が本来の色、反対側が影のprismで、先端の外側に1段の光を持つ。48打を超えて密集した時は陰影と先端の光を省く。
- 選択：菌糸線に落ち影とglowを添え、bulbとlaneの点をkey lightの球として描く。HISTORYでは選択打音の周りだけガラスが照らされる。
- 見出し：`DRUM / ATTACK`は刻印（1 px下の影）とし、選択中のTIMEページの短い光にglowを添える。
- 数値：ivory（`observatoryValue`）で描き、lane色はreadout左の短いaccentだけにする。状態点は点滅しない。
- 領域：光と影はそのデータの領域から出さない。HISTORYの光はlaneへ、1打の光は隣の列（±3 px）へ、値の光は0線の反対側（±2 px帯の外）へ届かない。
- 線種：破線と点線を使わない。

配色はHypha共通（`docs/hypha_ce2226_jungle_visual_system_20260901.md`の§3）に従う。DRUMではPOST包絡がシャンパンゴールド、選択が淡い氷色、TRANSIENTが水色、STRENGTHが金、CRESTが銅、SHARPNESSが薄紫である。金系laneと選択色は色で近づけない。

### 3.5 描画負荷

殻、見出し、凡例、計測面、label、目盛、0線、NOW線を端末解像度の画像1枚へまとめて保持する。
寸法、DPI、presentation context、VIEW、pair、データ有無のどれかが変わった時だけ作り直す。
上限は1 instanceあたり8 MiBで、超える場合は直接描画する。毎frameは観測（包絡、棒、数値、菌糸線、loupeの中身、状態）だけを描く。

寸法が前回の描画から変わった描画（角のドラッグ、Capture用の一時寸法）では画像を作らず直接描き、最後に保持した寸法の画像は残す。
同じ寸法で次に描く時に作るため、ドラッグ中に1段ごとの確保と転写が起きず、Capture後は元の寸法の画像をそのまま使う。
画像はDRUM pageを離れた時、DRUM viewが非表示になった時、hostがeditorを破棄せず隠した時に解放する。
layoutは1回の描画で1度だけ計算する。

このIntel MacのReleaseで`KIRIN_ATTACK_FRAME_BUDGET=1`を実行した結果（5サイズ、DPI 1 / 1.25 / 2、31 / 240打、1 / 2枠、二段 / 重ね表示）を示す。

| 最悪条件 | 変更前（51cf30a9） | 変更後 | 予算 |
|---|---:|---:|---:|
| 1枠の中央値 | 24.7 ms | 8.5 ms | 12 ms |
| 2枠の中央値 | 56.1 ms | 15.1 ms | 16 ms |
| 更新frameの最大 | 58.9 ms | 17.7 ms | 24 ms |
| 初回 | — | 70.7 ms | 80 ms |
| 判定 | FAIL | PASS | |

変更前は同じ検査がこのMacでFAILしていた。過去記録（B-774）の12.4 msは別の機械の値である。
2枠の余裕は0.9 msで小さい。

2026-09-25に、300%から100%へ戻る角のドラッグ（101段、毎段で寸法が変わる）を同じ検査へ加えた（中央値16 ms以下、最大40 ms以下）。
他アプリの負荷がある同じMacで、修正前（a39dc12b、毎段で画像を作り直す。1回、31 / 240打）と修正後（3回、31 / 240打）を測った結果を示す。

| 角のドラッグ | 修正前 中央値 / 最大 | 修正後 中央値 / 最大 |
|---|---:|---:|
| DPI 1 | 5.6–7.1 / 12.7–13.8 ms | 5.2–7.4 / 12.4–15.8 ms |
| DPI 1.25 | 7.4–8.7 / 18.3–19.4 ms | 6.9–9.5 / 16.7–19.4 ms |
| DPI 2 | 14.9–15.6 / 33.2–35.7 ms | 12.1–14.8 / 30.4–35.5 ms |

修正後も最大は約30–36 msで残る。内訳はDPI 2・300%の直接描画約26 msのうち、計測面へ敷く`bg_mycelium.png`の拡大描画が約15 ms（lane 1本1.7 ms、HISTORY 5.7 ms）、殻の素材が約5 msであり、角丸clipを矩形にしても1本1.7 msが1.4 msになるだけだった。今回は画像の作り直しを止めるところまでとした。
残る候補として、4本のlaneは同じ床の同じ切り出しなので、1回の拡大を共有すれば約5 ms下がる見込みがある。ただし端数DPIでは画素が僅かに変わるため、見た目の確認と合わせて別に扱う。
同じ負荷の下では、通常frameの2枠中央値の最悪値が修正前15.0–18.0 ms、修正後16.5–17.0 msとなり、双方とも16 msの予算を超える回があった（修正前3回中2回、修正後3回中3回）。交互に測った全条件の平均中央値は修正前3.71–4.16 ms、修正後3.82–3.97 msで同等だった。
画像を多く描くUI契約testの後では同じ描画が約0.5 ms遅くなる（DPI 2・300%）ため、描画予算の検査は他の検査より先に走らせる。

2026-09-26の2.5D外観（A3）と共通配色では、静的な器（溝の影、縁の光、端の減光、刻印、目盛）はすべて上の画像へ入り、毎frameの追加は包絡の光（ガラス管の壁、key light、glint、時間の減光）、棒の陰影と先端の光、選択の影とglowと球である。
包絡の上下対称をやめて片側にしたため、描く縁は半分になった。重い処理は避けた：打音ごとのgradientと切り抜きは使わず単色の矩形にし、包絡の光は1本の線にまとめ、48打を超える密集時は棒の陰影と先端の光を省く。
他アプリの負荷がある同じIntel Mac（load average 約4）でDPI 2・300%を測った。同じ負荷で変更前の外観も1枠2 msの揺れがあり、2枠の予算（16 ms）を超える回があった。

| DPI 2・300%（中央値） | 変更前の外観 | A3 |
|---|---:|---:|
| 1枠・31打 | 約6.0–6.7 ms | 約8.7–9.8 ms |
| 2枠・240打 | 約18–19 ms | 約22–24 ms |
| 角のドラッグ | 約14–15 ms | 約19 ms |

この検査はCIでは走らない。実機のStudio Proで表示中の負荷を確認する。

## 4. 計測core側の修正（B-1016）

§2.2の推奨（2026-09-24承認）を、2026-09-25に次の定義で実装した。

### 4.1 窓

- 全ての窓は、onsetを含む約1 msのcontent bin（48 kHzで48 sample、44.1 kHzで44 sample）の先頭から始める。binはcontent sample 0から数えるため、PREとPOSTは同じonsetで同じsampleを読む。
- 頭：30 bin（30 ms）。RMSがSTRENGTH、sample peakとの差がCREST。
- body：頭の後の100 bin。次のonsetが先に来れば、そのonsetを含むbinの手前で切る。20 bin未満なら測らない（`NEXT HIT`）。
- TRANSIENT：頭RMS − body RMS（ATTACK−BODY）。直前contextは使わない。body RMSが−72 dBFS未満なら画面で`QUIET AFTER`とする。
- SHARPNESS：頭から100 binの、DIN 45692 Sharpnessの音量加重平均（0.1 sone未満のPhase D frameは除く）。旧来の値は100 ms固定格子の区間終わりの瞬間値だった。
- detailは2段で出す（B-1024）。検出器がonsetを確定し、頭30 binが揃った時点で、頭だけのdetail（STRENGTH、CREST、`complete`=0）を出す。bodyの終わりまでのbinとPhase D frameが揃い、その手前の全onsetが確定した後（onsetから約0.18〜0.2秒後。body 130 ms、ODFの半窓21 ms、確定待ち30 ms、Phase Dの100 ms区切り）に、同じ打音の完全なdetailで置き換える。完全なdetailを頭だけへ戻すことはない。
- 再生を止めるとshellはATTACKへ音声を送らないため、停止直前の打音は頭だけのdetailのまま残る。HOLDでもSTRENGTHとCRESTは値を出し、TRANSIENTとSHARPNESSは`--`とする。窓を短くした値や停止後の無音で埋めた値は出さない。
- shapeは`[頭の20 ms前, body最大長の終わり)`のうち測定済みの区間だけを持つ。run開始より前、保持から外れた部分、まだ来ていない部分は含めず、無音として埋めない。打音がrunの先頭にあるとshapeはonsetから始まる。

### 4.2 POSTはPREの検出位置で測る

- 各instanceは直近7秒のbin（音量、peak、Phase D frameの和）を保持する。
- POST側のpair joinは、対応が取れた打音ごとに、PREのonsetとPRE detailのbody終端でPOSTのbinを測る。POSTの検出器自身のonset（±50 ms以内でずれうる）は窓に使わない。
- FFIは、対応が取れた打音のPOST detailをPREのonsetで渡し、`post_event_sample`もそのonsetにする。POSTの検出器自身がそのPOST onsetで測ったdetailは渡さない（6秒分のbatch上限240件を重複で使い切らないため）。
- pair viewの読み取りが他threadと重なった回は、PREのonsetで測ったdetailを欠いたbatchを渡さず、editorは前回のbatchを保つ。
- PRE detailが頭だけの間、またはPOSTのbinがまだbody終端まで揃わない間は、POSTも頭だけを測る。
- PRE→POST exchange codecは版2でbody終端、TRANSIENT、新しいSharpnessを運び、版3（B-1024）で頭だけのdetailを示す`complete`を加えた。版の違うsnapshotは読まない。
- PREはsnapshotを、waveformの末尾が進んだときだけでなく、detailの追加や完了を含むhistoryの変化ごとに書き直す。停止直前に出た頭だけのdetailもPOSTへ届く。
- ペアの交換セッションを作り直しても（相手が一時的に外れて戻る、復元したペアが確定する、POSTの依頼が変わる）、PRE・POSTとも動いているATTACKの計測は作り直さない。ATTACKは両側とも絶対content格子で測っており、セッションは両者の履歴を結ぶだけだからである。以前は作り直しで両側の履歴が消え、停止中はHOLDが空（WARMING UP）になった（B-1026、Studio Pro 8実機で観察）。SpectrumとSHARPは相手と状態を合わせるため、従来どおり作り直す。

### 4.3 Phase Dの区切り

既存のPhase D実装は、入力の区切り位置で出力がわずかに変わる。
DAWの処理ブロックではなく、content sample 0からの100 ms格子（SHARP timelineと同じ）で区切って流す。
このため、host block sizeが変わっても値は1 bitも変わらず、PREとPOSTは同じ位置を同じ区切りで処理する。
run開始点の後の最初の100 ms境界から300 ms後より前に始まる窓には、Sharpnessを出さない（44.1 kHzでは境界がbinの途中に来るため、次のbinから数える）。
Phase Dはresetの直後、状態が落ち着くまで値が偏る。同じドラム状の入力を1秒ずらしてresetした比較では、100 ms窓の差が区切りの直後で+0.42 acum、50 ms後で+0.13、200 ms後で+0.025、300 ms後で+0.003だった（48 kHz・44.1 kHzとも）。このため、再生開始やloop折り返しの直後の打音にはSHARPNESSを出さない（`--`）。PREとPOSTのrunが別の時点で始まっても、ΔSHARPNESSに入る偏りは上の比較で0.003 acum以内になる。
100 msの区切りがbinの途中で終わり、そのbinの音量がまだ揃っていない場合も、そのbinのPhase D frameはbinが揃うまで保持し、捨てない。

### 4.4 実データでの確認（2026-09-25）

§2.1と同じ82本・701打と6処理で、POSTをPREのonsetで測る条件で比べた（監査用の再実装。音源はrepositoryへ入れていない）。

| 項目 | 結果 |
|---|---|
| bodyに次の打音が入る打音 | Kick 50/214、Snare 1/74、Hats 156/348、Claps 8/76 |
| 次の打音の手前で切る場合と除外する場合 | Δの中央値は同傾向（例：Hats shaper +5.27 / +4.76 dB、Kick 遅いcomp +5.37 / +5.89 dB）。除外するとHatsの46%（156/337）を失い、切る場合は6%（20/337）を失う |
| 切らずに次の打音を含める場合 | 処理の効果が薄まる（Hats shaper +3.38 dB、下位10% 0.00 dB） |
| gain +6 dBでのΔTRANSIENT | 全楽器で0.00 |
| 同じSnareを10 msずつ動かしたSHARPNESS | 旧来の格子終わりの瞬間値は0.63–2.97 acum、新定義は幅0.07 acum以内（例：2.68–2.74） |
| 3 kHz high shelf +6 dBでのΔSHARPNESS | +0.06（Kick）〜+0.29（Snare）acum |
| gain +6 dBでのΔSHARPNESS | 中央値で±0.04 acum以内 |

同じ打音の繰り返しでも、onsetが256 sample格子で揺れるため、POST単独のTRANSIENT絶対値は打音ごとに1〜4 dB揺れる。PRE/POSTの差はonsetを共有するため、この揺れを含まない。

B-1016で、§3.2の止血のうち`ONSET DIFFERS`と`QUIET BEFORE`を撤去し、PRE未接続時のper-hit SHARPNESSを表示に戻した。

## 5. 検証

- `KirinAttackUiContractTests`（Debug / Release）：lane model、符号、止血、HISTORY/lane分離、POST-only、loupe、1行表示、菌糸線の太さ、5サイズ×2 VIEW、HOLD、LOCK、pointer選択、描画予算。2026-09-25に次を追加した。
  - PRE/POSTを同じだけ動かしても、5サイズとCapture 2形式でlaneと1行表示が変わらないこと（SHARPNESSは画面全体が変わらないこと）。
  - 6秒外へ出たLOCK打音の値を5サイズのどこにも出さないこと。
  - 5サイズすべてでHOLDが見えること。
  - 画像cacheが寸法、density、VIEW、pair、データ有無、DPIの各変化へ追従すること（初回描画・2回目描画とも新規componentと画素一致）。page離脱、非表示、明示解放で解放されること。
  - POST-onlyでdetail未着のeventが打音数を変えないこと。
  - 各テストが対応する修正前の挙動を検出することを、該当箇所を一時的に戻して確認した（5件）。
- `KirinUiRenderContractTests`（Release）：Observatory合成、Capture、Typography（字間込みのlane名・値・理由の幅）を含め全件PASS。
- `KirinEditorSurfaceProductTests`（Release）：40ケースPASS。
- DebugのUI render suiteでは、未変更のFREQ Focus Trailの時間予算（100% 4.5 ms）が4.47〜5.03 msで揺れて停止した。同じ検査はReleaseで2.4 msとなりPASSした。
- B-1016（`kirin_measure` / `kirin_hypha_ffi`）：
  - bin窓：頭・body・TRANSIENTの値、次onsetでのbody終端と20 ms未満の扱い、gainでTRANSIENTとCRESTが動かないこと、保持外と別runの打音を測らないこと、run途中開始のbinを使わないこと、Sharpnessの音量加重平均と0.1 sone未満frameの除外、shapeの20 ms前置き。
  - `per_hit_sharpness_follows_the_onset_not_a_fixed_grid`：同じ打音を100 ms動かすと同値、他の移動でも0.05 acum未満。
  - `host_block_size_never_changes_a_value`：host blockを64 / 333 / 512 / 4096 sampleにしても同値。
  - `exact_pair_transports_real_pre_and_post_attack_histories_end_to_end`：PRE→POSTの実transportで、POSTをPREのonsetとbody終端で測り、窓がPREと一致し、振幅半分のPOSTが−6.02 dBになること。
  - FFI：detail layout（512 byte、`transient_db`=84、`body_end_sample`=104、`sharpness_acum`=112、`bin_frames`=116、`shape`=128）と、pair時にPREの位置で測ったPOST detailを優先し、matchedの`post_event_sample`をPRE onsetにすること。
- B-1022：Phase Dの立ち上がり（§4.3）、失敗したSharpness streamの再構築、DRUMの`setFont`の文字規約、重複B番号の承認。
- B-1024（`kirin_measure` / `kirin_hypha_ffi` / JUCE）：
  - 頭だけのdetailを出してから完全なdetailで置き換えること、停止で音声が来なくなった打音が頭だけで残ること、頭の値が完成後も変わらないこと。
  - shapeがrun開始前を含まず（onsetから始まる場合も有効）、未測定の部分で止まること。
  - historyで完全なdetailが頭だけのdetailを置き換え、逆はしないこと。そのたびにrevisionが進むこと。
  - codec版3で頭だけのdetailが往復し、不正な`complete`値と版2を拒むこと。FFIが`complete`=0を渡すこと。
  - UI：頭だけの打音でSTRENGTHとCRESTが値、TRANSIENTとSHARPNESSが`--`（`NEXT HIT`や`QUIET AFTER`にならない）で選択できること（PAIR時と非PAIR時）。loupeが150 msの軸を保ち、測定済みの区間の外にshapeを描かないこと。
  - UIの窓定数（`headBins`、`bodyBins`、`shapeLeadBins`）が`kirin_measure`の定義と一致すること（`drum_ui_windows_are_the_measurement_windows`）。
- 未実施：Studio One / Pro Toolsでの実機表示、Windows実画面。
