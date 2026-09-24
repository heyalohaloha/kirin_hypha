# DRUM lane表示と4量の精査

- **作成日**：2026-09-24
- **状態**：B-1015で画面側を実装し、2026-09-25に厳しめレビューの指摘13件を修正した。計測core側の修正は別Bへ分離（Daisuke承認済み）。
- **変更範囲**：JUCE native描画、表示契約、UI契約test。計測core、FFI layout、transport、Audio Threadは変更しない。
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
| TRANSIENT | ±12 dB | 0–18 dB |
| STRENGTH | ±12 dB | −72..0 dBFS |
| CREST | ±12 dB | 0–24 dB |
| SHARPNESS | ±1 acum | 表示しない（`PAIR ONLY`） |

dB laneは同じ変化が同じ棒長になるよう尺度を共有する。範囲外の値はlane端でcapを付け、正確な値はreadoutに出す。
増減は棒の向きで示し、良し悪しの色を使わない（R-22）。

次の打音は値もΔも出さず、zero lineに中空の印を置き、選択時はreadoutへ理由を出す。

| 条件 | readout |
|---|---|
| PRE/POSTのonset sampleが異なる | `ONSET DIFFERS` |
| PREかPOSTの直前context（100 ms RMS）がHISTORY下限（−72 dBFS）未満。デジタル無音に限らない（TRANSIENTのみ） | `QUIET BEFORE` |
| PRE-only / POST-only / ambiguous | `PRE ONLY` / `POST ONLY` / `NO PAIR` |
| detail未着、非有限値、片側Sharpness欠測 | `--` |

onsetが同じなら、SHARPNESSの100 ms窓も同じ格子位置になる。

readoutは各laneの量だけを示す（PAIR時は`POST − PRE`、PRE未接続時はPOST値）。打音ごとのPRE値、POST値の内訳は出さない。
時間軸行の打音数は、laneが6秒内に描く列の数と一致させる（detail未着のeventは数えない）。

### 3.3 選択とloupe

選択打音は静的な菌糸線1本でHISTORYから最終laneまで貫く。
横揺れは打音sampleで決まる決定的なdriftで、正確なxから±1 px以内に留める。時間では動かさない。
細い芯に淡い発光を添え、HISTORY中央のbulbと下端の先端を持つ。

値は菌糸線に従う。LOCKした打音が再生で6秒の外へ出た後もLOCKは保つが、菌糸線と同じく、lane readout、1行表示、選択時刻、loupeもその打音を示さない（`--`、`NO HIT`）。ENDまたはNOWでLIVEへ戻る。
LIVE / HOLD / LOCKは時間軸行にも出す。header右の状態表示は幅470 px以上だけなので、100%〜150%では時間軸行がHOLDを示す。

loupeはInspectionだけに置く。
選択打音の`shape[96]`（区間内peak）をPRE traceとPOST bodyで描き、直前100 msと頭30 msのRMSを段として描く。
段の高さがTRANSIENT、頭の段の高さがSTRENGTH、頭のpeakと頭RMSの差（橙のbracket）がCRESTである。
PRE/POSTは各自のonset sampleに置き、ずれを揃えて隠さない。

### 3.4 外観

2026-09-24のDaisuke指示「テンションが上がる、スタイリッシュな見た目」を、CE2226の視覚層から次のように具体化した。

- 構造層：計測面は殻より一段深い黒とし、共通のHypha素材`bg_mycelium.png`を時間の床として低明度で敷く（HISTORY 0.30、lane 0.16、loupe 0.10）。deep tealの細い縁、上辺の低い反射、下辺の沈みで区切り、明るい四角枠を使わない。
- 目盛：HISTORYは各帯の−24 / −48 dBFS、laneは両端へ半分と全幅の刻みを置く。
- 見出し：`DRUM / ATTACK`を字間0.08で描き、選択中のTIMEページだけにcyanの短い光を置く。
- 観測層：POST包絡の縁とNOW線をcyanの狭い発光で描く。laneの棒は芯、halo、先端からなる発光filamentで、新しい打音ほど明るい（6秒で最大45%減光）。これは時間軸に連動する表示であり、自律的な装飾motionではない。
- 選択：菌糸線に加え、選択打音の各値の先端へlane色の点を置く。
- 数値：ivory（`observatoryValue`）で描き、lane色はreadout左の短いaccentだけにする。状態点はLIVE cyan、HOLD amber、LOCK 淡金で、点滅しない。
- 字間：labelの字間は200%と300%だけに付け、150%以下は全名が収まることを優先する。
- 線種：破線と点線を使わない。loupeのPRE段も細い実線とする。
- 色：金系laneを明るくして選択色（淡金）へ近づけない。先端、cap、点はlane本来の色で描く。

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

## 4. 別Bへ分離した計測core側の修正

1. TRANSIENTを「頭30 ms RMS − 後続区間RMS」（ATTACK−BODY）へ置き換えるか、BODYを追加する。密なパターンで次の打音が後続区間へ入る場合の切り方を検証する。
2. SHARPNESSの100 ms区間を打音の位置から始める。
3. POSTの特徴量をPREのonset sampleで計算し直し、PRE/POSTの窓を必ず揃える。

いずれも`kirin_measure`、PRE→POST exchange codec、FFIの変更と、ignored parity / pairing_candidates suiteの全件実行を要する。
入れた後は、3.2の画面側止血（`ONSET DIFFERS`、`AFTER SILENCE`、per-hit SHARPNESSの差分限定）を見直す。

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
- 未実施：Studio One / Pro Toolsでの実機表示、Windows実画面。
