# DRUM 帯域表示（BAND）計画（2026-09-28）

## 0. 目的と承認の状況

- **背景**：2026-09-28、中川統雄氏の投稿が共有された。自作の Multiband Envelope Shaper で「対象の周波数を選ぶと、そこの Att/Rel の実測を見ながらトランジェントやリリースを弄れる」「キックが遅くなった、○○Hz の鳴りが後ろに残る、が可視化できる」という内容。
  - これを受け、Daisuke から「DRUM はこのような機能も持てるか」と問われた。
- **承認済み**：見本の形で作ることを Daisuke が承認した（2026-09-28）。見本は次の構成。
  - VIEW の BAND と、帯域のボタン
  - 選んだ1打の HEAD / TAIL
  - 下の4段（DELAY・ATT・REL・LEVEL）
- **条件**：
  - **負荷を増やさない**。
  - **帯域の選び方は外部調査で見直す**（§2）。
- **2026-09-28、D1〜D5 は推奨どおりで Daisuke が確定した**（§9）。
- **2026-09-29、PR-A（計算・FFI・負荷の実測）を実装した。** 実装で決めた点は §11。
- **2026-09-29、PR-B（画面）を実装した。** 実装で決めた点は §12。不変条件は INV-S44。
- **2026-09-29、厳しめレビューの11件を構造から直した（B-1098）。** 規則と使いやすさの改善、負荷の測り直しは §13。§11・§12 と食い違う点は §13 を正とする。
- **Hypha の立場**：
  - 音を変えない（R-12）。シェイパーやコンプは PRE と POST の間にある利用者のプラグインで、Hypha はその前後を**同じ打撃で**比べて示す。
  - 良し悪しは言わない（R-22）。

## 1. 今の DRUM（コードで確認した事実）

- **1打の測定**（`attack_bins.rs`）
  - 約 1 ms ごとの**全帯域**の値（電力・ピーク・シャープネス）を 7 秒ぶん保持する。
  - 頭 30 ms、胴 100 ms、形 96 点（`KirinAttackDetail`）から TRANSIENT・STRENGTH・CREST・SHARPNESS を出す。
  - **元の音（サンプル）は保持していない。** 帯域ごとの形を出すには、新たに音をためる必要がある。
- **PRE → POST**
  - PRE は打撃と詳細を2進のスナップショットで公開する（ATTACK exchange codec 第3版）。
  - POST は自分の bins を **PRE の onset の位置で**測り直す（`details_at`）。
  - 両者を matched / PRE-only / POST-only に突き合わせる（`attack_pair.rs`）。
- **POST → PRE**
  - 解析要求ファイル `kirin_hypha_analysis_request_v4`（`analysis_mode`、`channel_mode`）で、POST が PRE に解析を頼む。
  - ATTACK は要求されている間だけ動く（1プロセスで ATTACK・FREQ・SHARP・LIVE のうち最大2つ）。
- **Audio Thread**：サンプルと記述子を SPSC リング（2 秒）へ写すだけ（`attack_runtime.rs`）。

## 2. 帯域の選び方（外部調査）

### 2.1 調べたこと

| 対象 | 帯域（出典の範囲） |
|---|---|
| キックの胴・基音 | 40–100 Hz（60–100 Hz に基音と打感） |
| キックのビーター（クリック） | 2–5 kHz |
| スネアの胴 | 150–250 Hz（資料により 200–400 Hz） |
| スネアのクラック | 2–4 kHz（2.5–5 kHz） |
| タム | 基音 80–120 Hz、鳴り（リング）250–600 Hz |
| ドラム全般の基音 | 50–250 Hz |
| ハイハット・シンバル | きらめき 6–9 kHz、空気感 8–16 kHz |
| ドラムバスの低域の締まり | 100–200 Hz |

- **規格**：ISO 266 と IEC 61260 は、オクターブ帯域の中心を 31.5・63・125・250・500・1k・2k・4k・8k・16k Hz と定めている。世界共通で、1/3 オクターブも同じ系列に乗る。
- **時間と周波数の交換関係**：帯域幅 × 立ち上がり時間 ≒ 一定（1極モデルで約 0.35）。狭い帯域、つまり低域の1オクターブほど応答が遅い。
  - 1オクターブ帯域の包絡の時間分解能は、**おおむね中心周波数の1周期**（63 Hz で約 16 ms、1 kHz で約 1 ms）。§7 で実測して確かめる。
- **フィルターと遅れ**：
  - ミニマムフェイズのフィルターやクロスオーバーは、低域ほど群遅延で遅れて鳴る。
  - リニアフェイズは、キックのような低域の打撃にプリリンギングを生む。
  - これはまさに DELAY と HEAD が示すことになる現象である。

### 2.2 見本の帯域（50・100・200・500・1k・2k・5k）の評価

- **すき間がある**：各帯域を1オクターブ幅にすると、283–354 Hz と **2.83–3.54 kHz（スネアのクラックの中心）** が抜ける。
- **8 kHz より上がない**：ハイハットやシンバル（6–9 kHz）の打撃を見られない。
- **間隔が不揃い**：200→500 と 2k→5k が 1.3 オクターブあり、ほかの資料や測定と比べにくい。

### 2.3 推奨

- **ISO のオクターブ中心 8 帯域：63・125・250・500・1k・2k・4k・8k Hz（＋ALL）**。各帯域は1オクターブ幅で、すき間なく連続させる。
  - キック（基音 50 Hz は 63 帯域 = 44–89 Hz に入る。クリックは 2k・4k）
  - スネア（胴は 125 と 250、クラックは 2k・4k）
  - タム（63 と 125、鳴りは 250・500）
  - ハイハット（8k = 5.6–11.3 kHz）
- **31.5 Hz（808 のサブ）**は時間の細かさが約 30 ms しかないので入れない（D1 で確定）。
- **1/3 オクターブにはしない**：周波数は細かくなるが、時間の細かさが約3倍悪くなり、打撃の解析に向かない。

## 3. 表示（承認済みの見本に沿って）

- **入り方**：帯域のボタン列（ALL・63…8k）を DRUM の見出しの下に置く。
  - ALL は今の DRUM のまま。帯域を押すと BAND 表示になる。
  - VIEW は今どおり OVERLAY / 2 ROWS。見本の「VIEW BAND」との違いで、VIEW を3状態にしないためである（D3 で確定）。
- **履歴の段**：選んだ1打を描く。選んでいなければ最新の matched の1打。
  - **HEAD**（−5〜+25 ms を拡大）と **TAIL**（0〜300 ms）の2面に分ける。
  - PRE は線、POST は塗りで描く（今の拡大表示と同じ描き方）。
  - DELAY と REL の差を括弧で示す。
- **右の拡大表示**：今のまま全帯域（HIT 150 ms）。
- **下の4段**：6 秒ぶんの打撃ごとに POST − PRE の差を棒で並べ、右端に最新値を出す。
  - DELAY（ms）
  - ATT（ms、振幅 10→90 %）
  - REL（ms、ピーク → −20 dB）
  - LEVEL（dB、帯域のピーク）
  - 目盛りの初期値は DELAY ±10 ms、ATT ±10 ms、REL ±100 ms、LEVEL ±12 dB。実測のあと見直す。
- **対にならない打撃**：DRUM の今の規則（PRE がなければ POST の絶対値）に従う。DELAY は差でしか意味がないので「---」とする。
- **100 %〜150 %**：4つの値だけを出す。帯域は 125 % 以上で選んだものを保つ（INV-S38：100 % は見るだけ）。
- **日本語**：DELAY などの見出しは英語のまま。ホバーの説明（各値の意味、低域の時間の細かさ）を日本語にする（INV-S40）。
- **精度の表示**：ATT が帯域の時間の細かさより短いときは、値をそのまま出さず「≤16 ms」のように上限で示す（D4 で確定）。
- **何もないとき**（R-26）：帯域に打撃がない、または音量が下限（例 −60 dBFS）未満のときは、0 ではなく「---」を出す。

## 4. 計算（負荷を増やさない設計）

- **音のため置き（リング）**
  - ATTACK のワーカー（Audio Thread ではない）に、チャンネルごとの f32 リングを 7 秒ぶん持つ（bins の保持と同じ長さ）。POST が PRE の onset で測り直すのに要る長さである。
  - **帯域を選んでいる間だけ**確保して埋め、ALL に戻したら解放する。ALL のときの負荷とメモリは今と同じ。
  - メモリの目安（ステレオ、f32）：48 kHz で 2.7 MB、96 kHz で 5.4 MB、192 kHz で 10.8 MB。
- **計算するとき**：確定した打撃ごとに1回、選んだ1帯域だけ。POST では PRE の onset ごとに1回。
  - 帯域を切り替えたときは、リングに残る打撃（最大 7 秒）を1回だけ計算し直す。
- **計算の中身**
  - 帯域フィルター：1オクターブ幅。4次バターワース（双2次2段）か、IEC 61260 クラス1相当の6次かを、実測で選ぶ。
  - 立ち上がりの前に、フィルターが落ち着く時間（約 4 / 中心周波数）を読む。
  - 包絡：帯域の二乗を平滑化して dB にする。1 ms 刻み、高い帯域は 0.25 ms 刻み。
  - 各値の求め方：
    - 立ち上がり点：ピーク − 20 dB を最初に越える時刻
    - ピーク時刻
    - ATT：振幅 10 → 90 %
    - REL：ピーク → −20 dB
    - LEVEL：帯域のピーク dBFS
  - **DELAY**：PRE と POST の帯域信号の相互相関が最大になるずれ。
    - 頭の窓で ±20 ms の範囲を探し、放物線補間でサンプル以下の細かさまで求める。
    - 低域で1周期ずれた山を選ばないよう、包絡から求めた立ち上がり点の差で探す範囲を絞る。
- **負荷の見込み**：1打・片側あたり、約 0.4 秒ぶんの音に双2次2段をかけるだけで、0.2〜0.5 ms。1秒に2打なら1コアの 0.1 % 前後。§7 で実測し、数字でお見せしてから画面に進む。
- **PRE 側**：解析要求を第5版にして `attack_band` を加える。PRE は**頼まれた1帯域だけ**を計算し、ATTACK exchange（第4版）で次を公開する。
  - 帯域の値
  - 最近の打撃の HEAD / TAIL の包絡点（例：各 64 点）。スナップショットの大きさの上限に収まる数に限る。
- **POST 側**：PRE の onset の位置で自分の帯域の値を測り、対にして差を出す。
- **Audio Thread**：変更しない（3層隔離、Audio Thread での確保・ロック・I/O 禁止）。リングの確保はワーカーで行う。

## 5. FFI と PRE/POST の境界

- `kirin_hypha_set_attack_band (handle, uint8_t band)`：0 は ALL（計算しない）。UI スレッドから呼ぶ。
- `KirinAttackBandBatch` と `kirin_hypha_poll_attack_band`：打撃ごとに次を返す。
  - 帯域、PRE / POST それぞれの有無
  - 立ち上がり、ピーク、ATT、REL、LEVEL、DELAY
  - その帯域の時間の細かさ
  - HEAD / TAIL の包絡点
- **互換**：
  - POST が新しく PRE が古いときは、帯域の値は出ない。理由（「PRE を更新すると帯域を比べられます」）をホバーとクリックで示す（R-28）。
  - 古い要求（第4版）を受け取った新しい PRE は、帯域なしとして動く。
- **検査**：C ヘッダーと Rust の ABI テストに加えて、`kirin_hypha_ffi` 変更時の ignored スイート（parity 20 件、pairing_candidates 6 件）を実測件数で全件回す（AGENTS.md）。

## 6. 画面の実装

- **新しいファイル**（500 行以内）
  - `HyphaAttackBandPainter.cpp/h`：HEAD / TAIL の2面
  - `HyphaAttackBandModel.h`：帯域の値と目盛り
- **手を入れるファイル**
  - `HyphaAttackComponent.cpp`（今 415 行）：帯域の選択とボタン。500 行を超えそうなら、先に帯域の部分を別ファイルへ切り出す。
  - `HyphaAttackChrome.cpp`：見出しの下の帯域のボタン列
  - `HyphaAttackUiContract.h`：ボタン列の高さ、各サイズの配置
  - `HyphaAttackLaneModel.h`：帯域の4段
  - `HyphaAttackGlancePainter.cpp`：100 % の4つの値
- **帯域の選択の保存**：プラグインの状態に保存する（DRUM の VIEW と同じ扱い）。
- **日本語の辞書**：ホバーの説明を加える（`check_screen_text.mjs`）。

## 7. 検証

- **正しさ**（Rust の単体テスト、合成信号）
  - キック（50 Hz のバースト＋クリック）に、既知の差を付けた POST で次を確かめる。
    - 遅れ（0、1、2.4、5 ms）
    - 立ち上がり（+1 ms）
    - 余韻（τ +10 ms）
    - 音量（−0.8 dB）
  - 各帯域で、測った値が許容差に入ることを確かめる。許容差は、帯域ごとの時間の細かさの表に基づいて決める。
  - ミニマムフェイズのハイパスやクロスオーバーを POST 側にかけ、DELAY が理論上の群遅延と合うことを確かめる。
  - リニアフェイズの FIR をかけたときのプリリンギングの見え方を記録する。
  - 無音、帯域にエネルギーがない、リングの一巡、サンプルレート（44.1 / 48 / 96 / 192 kHz）、帯域の切り替えのときの再計算も確かめる。
- **負荷**（Daisuke の条件）
  - DRUM を開いた状態で、ワーカーのスレッド時間を3通り測る。
    - (a) 今の main
    - (b) 新版で ALL
    - (c) 新版で帯域を選んだとき
  - (a) と (b) が誤差の範囲で同じであることを確かめる。
  - (c) は1打あたりの ms とコア比、メモリを 48 / 96 kHz で出す。
  - 別の作業ツリーのビルドなどで負荷の高い時間を避けて測る。
  - この数字を Daisuke に見せてから、画面の PR に進む。
- **画面**
  - 5つのサイズ × 英日で、帯域の表示が切れずに収まることの契約テスト
  - 300 % の描画時間の予算
  - 見た目の確認画像
- **実機**：Studio One で、キックにミニマムフェイズのマルチバンドをかけ、63 Hz の DELAY がもっともらしいことを確かめる。

## 8. 進め方（PR の分け方）

1. **PR-A（Rust と FFI、画面なし）**
   - 帯域の解析、リング、解析要求の第5版、exchange の第4版、FFI
   - テストと負荷の実測
   - 負荷の数字を Daisuke が確認する。
2. **PR-B（画面）**
   - BAND 表示、4段、100〜150 % の数値、日本語、契約テスト、見た目の確認
   - 文書：不変条件（INV-S44 の見込み。番号はコミット時に確定）、README、表示契約
- B 番号はコミット時に `git log --all` の最大値の次を取る。

## 9. 決定事項（2026-09-28 Daisuke 確定、全件推奨どおり）

| # | 内容 | 決定 |
|---|---|---|
| D1 | 帯域のセット | ISO の 63・125・250・500・1k・2k・4k・8k。31.5 Hz（808 のサブ）は入れない（時間の細かさ約 30 ms） |
| D2 | FREQ でクリックした任意の周波数を中心にする選び方 | 今回は入れず、使ってみてから決める（Phase 2 は Daisuke の承認が必要） |
| D3 | BAND への入り方 | 帯域のボタンで入り、ALL で戻る。VIEW は OVERLAY / 2 ROWS のまま |
| D4 | 低域の ATT の出し方 | 時間の細かさより短いときは「≤16 ms」のように上限で示す |
| D5 | 帯域を選んだ直後は過去の打撃が出ない | 受け入れる。選ぶまで音をためないことで、ALL のときの負荷とメモリを今と同じに保つ |

## 10. ほかのセッションとの関係

- **PRE/POST Blind**（INV-S43、`claude/hypha-blind-steps`）：DRUM のファイルとは重ならない見込み。先に入れば、この計画の番号は INV-S44 になる。
- **「Hypha ATTACK下部の設計」**（クラウドのセッション）：DRUM の下の段と重なる可能性がある。着手前に内容を確かめ、1つのセッションにまとめる。

## 11. PR-A の実装記録（2026-09-29）

計画からの違いは次のとおり。いずれも計画の目的（負荷を増やさない、同じ打撃で比べる、古い版と壊さない）のために選んだ。

- **フィルター**：下端の2次バターワース HP と上端の2次バターワース LP の2段（各12 dB/oct）。中心周波数の利得を 0 dB に正規化した（正規化なしでは中心が約 −1.9 dB になる）。名目の端は約 −1.4 dB で、−3 dB 点は名目の端の少し外側になる。IEC 61260 クラス1相当の6次は使わない。
- **包絡**：帯域信号の電力を中心周波数1周期の中央窓で平均し、毎サンプルで求める（累積和で O(1)）。表示用の HEAD 96点（−20〜+40 ms）と TAIL 64点（0〜300 ms）はそこから採る。
- **DELAY**：相互相関ではなく、**各側の包絡が自分のピーク −20 dB を上向きに越える時刻（arrival）の差**にした。全サンプル分解能で線形補間するため、低域でも 1 周期の制約を受けない（合成信号で 2.4 ms のずれを 63 Hz 帯域で誤差 0.5 ms 以内、500 Hz で 0.3 ms 以内。40 Hz のミニマムフェイズ HP の群遅延と 25 %+0.5 ms 以内で一致）。
- **ATT**：ピーク振幅の 10 %（= arrival）から 90 % まで。**REL**：ピークからピーク −20 dB まで。次の onset で尾が切れて −20 dB に達しないときは「なし」。arrival は前打の余韻が −20 dB より上に残るときは「なし」（ATT も）。
- **存在の下限**：帯域包絡のピークが −72 dBFS 以下なら、その帯域はその打撃に「なし」。
- **計算するタイミング**：打撃が complete（頭 30 ms + 胴 100 ms）になった後、尾の 300 ms 内の onset がすべて決まり、リングに尾＋半窓ぶんの音が入ってから 1 回。POST は PRE の onset と PRE の span 終端で自分のリングから測る。
- **要求**：`kirin_hypha_analysis_request_v4` に任意項目 `attack_band` を足した（v5 にはしない）。serde は未知の項目を無視するので、**古い PRE は帯域なしで従来どおり動く**。ATTACK 以外のモード、1〜8 以外の値は帯域なし。
- **交換**：スナップショットは帯域を要求されている間だけ version 4（v3 の末尾に帯域の節を追加、1打 692 バイト、最大 64 打。B-1098 で 1打 384 バイトの固定長・最大 240 打に変更、§13.1）。要求のない POST（古い POST）には v3 を書く。上限を 262,144 バイトに上げた。帯域の節は自分の event を持ち、details と独立に復号できる。
- **FFI**：`kirin_hypha_set_attack_band(handle, 0..8)` と `kirin_hypha_poll_attack_band`（`KirinAttackBandBatch`：status、band、`pre_band_available`、最大 64 打。各打は kind 0=matched / 2=POST のみ、resolution_micros、delay、PRE/POST 各側の arrival・ATT・REL・LEVEL・HEAD・TAIL）。C の配置は `band_c_layout_is_fixed` で固定。B-1098 で PRE の4状態・最大 240 打・包絡の別問い合わせに変更（§13.1）。
- **負荷の実測**（`reports_the_worker_cost_with_and_without_a_band`、release、48 kHz ステレオ、2打/秒を実時間で 20 秒、プロセスの CPU 時間。PRE 側 worker だけの測定で、POST が PRE の onset で測る分を含まない。§13.3 で両方を含めて測り直した）：
  - 帯域なし（ALL、今の main と同じ経路）：6.02 %（1コア比）
  - 63 Hz：5.87 %
  - 8 kHz：6.01 %
  - 差は測定のゆらぎの中。1打あたりの帯域計算は 48 kHz で 230〜275 µs、96 kHz で 460〜560 µs、192 kHz で 900〜1,120 µs（`reports_the_cost_of_one_band_measurement`）。2打/秒なら 1 コアの 0.05 % 前後。
  - メモリ：帯域を選んでいる間だけ 7 秒のリング（48 kHz ステレオ 2.7 MB、192 kHz 10.8 MB）。ALL では確保しない。
- **Audio Thread**：変更なし。

## 出典

- [Kick Drum EQ 101（Gear4music）](https://www.gear4music.com/blog/kick-drum-eq/)
- [Drum Mixing Tips（Production Expert）](https://www.production-expert.com/production-expert-1/2020/4/6/new-to-drum-mixing-try-these-fundamental-eq-starting-points-to-help-you-focus-the-sound-of-your-drums-fast)
- [Drum EQ Chart（Music Guy Mixing）](https://www.musicguymixing.com/drum-eq-chart/)
- [Drum EQ Frequencies Reference（Sounds Heavy）](https://www.soundsheavy.com/drums-percussion/drum-eq-frequencies/)
- [Guide To Mixing Drums（iDrumTune）](https://www.idrumtune.com/mixing-drums-know-your-drum-frequencies/)
- [The Best Hi Hat EQ Settings（Music Guy Mixing）](https://www.musicguymixing.com/hi-hat-eq/)
- [Multiband Transient Shaping 101（Unison）](https://unison.audio/multiband-transient-shaping/)
- [Octave Bands & 1/3-Octave Bands Explained（Commercial Acoustics）](https://commercial-acoustics.com/sound-advice/octave-bands-explained/)
- [ISO 266:1997 Preferred frequencies](https://standards.iteh.ai/catalog/standards/iso/f5f1664e-0e9b-4002-b164-fde8ceff5442/iso-266-1997)
- [ANSI/ASA S1.6 Preferred Frequencies（preview）](https://webstore.ansi.org/preview-pages/ASA/preview_ANSI+ASA+S1.6-2016.pdf)
- [Relationship Between Rise Time and Bandwidth（Thorlabs）](https://www.thorlabs.com/newgrouppage9.cfm?objectgroup_id=9817)
- [An overview of filters, Part 4: Time and phase issues（Analog IC Tips）](https://www.analogictips.com/an-overview-of-filters-and-their-parameters-part-4-time-and-phase-issues/)
- [Bandsplitting for mastering: minimum phase or linear phase?（Gearspace）](https://gearspace.com/board/mastering-forum/1378687-bandsplitting-mastering-purposes-minimum-phase-linear-phase.html)
- [Group Delay Calculator for Audio（CMUSE）](https://www.cmuse.org/group-delay-calculator/)

## 12. PR-B の実装記録（2026-09-29）

計画（§3・§6）からの違いと、実装で決めた点。

- **帯域の範囲の表示**：凡例とホバーの範囲は、エンジンの正確な中心（62.5 Hz × 2^n）から求める。63 の帯域は `44-88 Hz`、8k は `5.66-11.3 kHz` と出る（§2 の 44–89 は名目の 63 Hz から計算した概数）。
- **入り方**：見出しの2行目に `BAND` と9つのチップ（ALL・63・125・250・500・1k・2k・4k・8k）を置いた。凡例はチップの右に、収まるときだけ出す（`drawFitting`）。見本の「Hz」の見出しは `BAND` にした（ALL が先頭に来るため）。VIEW は OVERLAY / 2 ROWS のまま（D3）。100% にはチップがなく、選んだ帯域を保って HISTORY 左上に `63 Hz` と名指しする（INV-S38）。
- **HEAD の範囲**：見本の −5〜+25 ms ではなく **−5〜+40 ms** にした。エンジンが届ける HEAD は −20〜+40 ms で、63 Hz 帯域の立ち上がり（約 13〜20 ms）と到達が +25 ms に収まらない場合があるため。300% の HEAD 面は約 280 px なので 45 ms でも 6 px/ms あり、2.4 ms の DELAY は十数 px の隙間として見える。
- **面を出す大きさ**：HISTORY の plot が 64 px 以上のとき（200%・300%）だけ HEAD / TAIL に替える。150% 以下は HISTORY の 6 秒をそのまま保ち、段だけが帯域になる（§3「100%〜150% は 4 つの値だけ」）。面は打音を選ばず、時間軸と段だけが選ぶ。選択の菌糸線は面を通らず時間軸から下だけに立つ。
- **dB の尺度**：面は HISTORY と同じ −72〜0 dBFS 固定（自動スケールなし）。
- **VIEW 2 ROWS**：面を PRE 上・POST 下に分け、各行に自分の印を置く。括弧（DELAY・REL の差）は OVERLAY だけ。
- **段の尺度**：差分は DELAY ±10 ms、ATT ±10 ms、REL ±100 ms、LEVEL ±12 dB。POST 値は ATT 0〜40 ms、REL 0〜300 ms、LEVEL −72〜0 dBFS。DELAY は差でしか意味がないので POST 値の段は空で `NO PAIR`。実測のあと見直す。
- **D4（ATT の上限）**：|ATT| または |ΔATT| が帯域の時間の細かさ（1周期）未満のとき、読み出しは `<16 ms` の形で上限を示し、棒は 0 線の中空の印にする。`≤` ではなく ASCII の `<` にしたのは、既存の尺度表記が `+/-` を使っており、mono 書体と Windows で記号の有無を確かめずに済ませるため。
- **理由の語**（B-1098 で置き換え。§13.2）：`RINGING`（前の打音の余韻で到達が取れない。DELAY・ATT）、`NEXT HIT`（次の打音で尾が切れた。REL）、`PRE NO BAND`（ペアだが PRE が帯域を返していない。DELAY）、`POST ONLY` / `NO PAIR` は従来どおり。
- **古い PRE**（B-1098 で「切替中」と「帯域より古い版」を分けた。§13.1）：`pre_band_available == 0` の間は「届く前」と「帯域より古い PRE」を区別できないので、両方を同じ言葉で扱う。段は POST 値、DELAY は `PRE NO BAND`、HEAD の見出しは `PRE: NO BAND YET`、選んだチップのホバーで「帯域より古い PRE からは届かない。PRE を更新すると比べられる」と説明する（R-28）。
- **エンジンとの同期**：帯域は `AttackComponent` のエディター寿命の状態（VIEW と同じ）。エディターは `onBandChange` で `setAttackBand` を呼び、DRUM ページに入るたびに自分の帯域を送り直す（開き直したエディターは ALL、エンジンに前の帯域が残っていても揃う）。プロセッサは帯域を覚え、ATTACK をエンジン（新規・再有効化）へ適用するたびに送り直す。届いた batch の帯域が選んだ帯域と違う間（切替直後）は何も出さない。
- **ホバー**：チップ・HEAD・TAIL・帯域の4段に日本語つきの説明（INV-S40）。従来の4段には付けていない。
- **画像**：`KIRIN_ATTACK_UI_SHOWCASE_DIR` で 63 Hz の見本画像（900 overlay / rows、600、450、375、300）を書く。
- **検証**：`verifyBandModel`（差分・POST 値・PRE 待ち・理由・D4・文字）、`verifyBandRendering`（5 サイズ、チップ、面、段、ALL で完全に元へ戻る）、`verifyBandInteraction`（チップ、面は選ばない、段は選ぶ、ホバー、100% で反応なし）、`verifyChromeCache`（帯域・PRE 待ち・2 ROWS を含む）。`KIRIN_ATTACK_FRAME_BUDGET` で 300% 帯域表示の描画時間も測る。

## 13. レビュー後の構造の見直し（B-1098、2026-09-29）

厳しめレビューの11件を、同じ種類の問題が二度と起きない形に直した。
内部を厳しくしても、利用者の操作が増えたり、表示が止まったりしないことを条件にし、分かりやすさと判断のしやすさも同時に上げた。
§11・§12 と食い違う点は本節を正とする。

### 13.1 構造の規則

- **測るのは worker だけ、1回だけ**：各打音×帯域は ATTACK の worker だけが1回だけ測る。音のリングも worker だけが持ち、mutex を置かない。
  - POST が PRE の onset で測る位置（anchor）は、交換スレッドが `request_band_anchors` で頼むだけにした。中身が変わったときだけ revision を進め、交換スレッドは信号処理をしない。
  - B-1096 では、交換スレッドが tick（最大 30 Hz）ごとに全 anchor を測り直し、その間リングの lock を握っていた（指摘1）。
- **打音の鍵は段の鍵**：FFI は段と同じ鍵で1打1列を返す。鍵は、PAIR 中は対の共通 onset、それ以外は POST の detail の onset とする。
  - 測った位置（PRE の onset）は `measured_at_sample` に分けた。
  - batch と包絡の問い合わせは同じ `sources()` から作るので、両者が別の打音を指すことはない（指摘2・3）。
- **結果は履歴の外に置く**：帯域の結果は `AttackHistory` に入れず、`Arc<AttackBandResults>` で持つ。公開のたびに履歴の revision を進め、PRE が送り直す。
- **結果を必ず言葉にする**：エンジンは結果を型で言い切り、UI はそれをそのまま理由と下限・上限にする。
  - 音：`Rises`（立ち上がる）、`RingsOn`（前の打音の余韻が鳴り続けるだけ）、`Silent`（ピーク −72 dBFS 以下）
  - 到達：`At`、`Ringing`（余韻に隠れる）
  - REL：`At`、`CutByNextHit`、`AtLeast`（測った尾の終わりでもまだ鳴る）
  - 区間の終わり：`Window`、`NextHit`、`AudioEnd`
  - 測れなかった打音は `measure: None`（NOT MEASURED）
  - 300 ms を超える尾を `NEXT HIT` と言っていた点（指摘4）と、前の打音の余韻をその打音の LEVEL・REL にしていた点（指摘5）を直した。onset −20 ms の包絡から 3 dB 未満の上昇は `RingsOn` とする。
- **§4 のとおり、帯域の切替でリングを保つ**：帯域から別の帯域へ替えるとリングを保ち、直近 7 秒を停止中でも測り直す（B-1096 はリングを捨てていた）。ALL で解放する。
  - リングは最初から 7 秒ぶんを確保し、途中で再確保しない（指摘8）。
- **止まっても最後の打音を仕上げる**：音が 200 ms 途切れたら、保持した音の終わりまでで測る（区間の終わりは `AudioEnd`）か、測れないと明示する。
  - worker は、取り込んだ block の世代で判断する。停止中は block ごとに runtime の世代が進むため、以前は最後の打音が捨てられていた。
- **仕事の上限**：1回の処理は 4 ms まで、新しい打音から、anchor を自分の打音より先に測る。公開は 30 ms ごと以下（残りが無ければすぐ）。
- **交換の大きさを型で固定**：帯域の記録は1打 384 バイトの固定長（包絡は i16 の centi-dB）、最大 240 打（ATTACK の履歴と同じ数）。
  - 最悪の snapshot（231,456 バイト）が上限（262,144 バイト）に収まることを、コンパイル時に確かめる（`ATTACK_SNAPSHOT_WORST_CASE_BYTES`）。
  - 帯域の節が整合しないときは丸ごと拒む。
- **PRE の状態を4つに分ける**：`Off`、`Same`、`Waiting`、`Predates`。
  - 帯域を送ってから 2.5 秒たっても帯域の節が無い PRE を「帯域より古い版」とし、`UPDATE PRE` と案内する。
  - それまでは切替中として、`POST − PRE` の枠のまま全段 `--` にする。

### 13.2 使いやすさ

- 帯域を替えても、再生し直す必要がない（停止中も直近 7 秒を測り直す）。
- 停止の直前の打音にも値が出る。
- LIVE は、帯域が明示された最新の打音を追う。測っている途中の打音で面が `MEASURING` にちらつかない。
- 6 秒の中に測れた打音が無いときは、`PLAY TO MEASURE 63 Hz` を1か所に出す。
  - 200%・300% は TAIL の面、125%・150% は HISTORY の左上、100% は HISTORY の見出し。
- 値の無い段は、全サイズで理由を示す。
  - 収まれば全文：`RINGING`、`NO SOUND`、`PRE NO SOUND`、`POST NO SOUND`、`LONG TAIL`、`NEXT HIT`、`NOT MEASURED`、`UPDATE PRE`、`NO PAIR`
  - 狭ければ短い語：`RING`、`NONE`、`LONG`、`UPDATE`、`NEXT`、`QUIET`（日本語あり）
  - それも入らなければ `--`
  - 125% の1行読み出しも、`--` ではなく理由を出す。100% では理由を読み出しの書体で描き、他の値の書体を縮めない。
- 値の外側は、下限・上限で示す：`<2 ms`（時間の細かさの内側）、`>+146 ms`・`>288 ms`（尾がまだ鳴る）、`<-66.0 dB`（POST に帯域の音が無い）。
- ALL と帯域で、列と選択がそのまま残る（同じ打音）。時間軸行の `N EVENTS` も段の列数と一致する（指摘7）。
- 包絡は、面を出している間だけ、選んだ打音の分を取る。
- 余韻で到達が取れないことは、DELAY と ATT の読み出しが1回だけ言う。面の中には文字を重ねない。

### 13.3 負荷の測り直し（指摘9の訂正）

§11 の数字は PRE 側 worker だけの測定で、POST が PRE の onset で測る分を含んでいなかった。
B-1098 では anchor も同じ worker で1回ずつ測るので、両方を含めて測り直した。

- 条件：release、48 kHz ステレオ、2打/秒を実時間で 20 秒、プロセスの CPU 時間。帯域ありでは、自分の打音 40 と anchor 40（30 Hz で要求）を測る。
- 別セッションのビルドで、負荷平均が 9〜13 の時間帯に測った。

| 条件 | CPU（1コア比） | 帯域の計測回数 |
|---|---|---|
| 帯域なし（ALL）1回目 | 6.56 % | 0 |
| 63 Hz | 6.81 % | 78 |
| 8 kHz | 6.66 % | 78 |
| 帯域なし（ALL）2回目 | 6.19 % | 0 |

- 計測は 78 回で、結果 80 件（自分 40・anchor 40）より 2 回少ない。先頭の1打は onset が音の始まり（0 frame）にあり、onset 前 20 ms の音が無いので、自分と anchor の両方が NOT MEASURED になった。同じ打音を2回測ることはない。
- 1打あたり（`reports_the_cost_of_one_band_measurement`）：48 kHz で 236〜305 µs、96 kHz で 492〜591 µs、192 kHz で 1,027〜1,204 µs。
- 2打/秒で自分と anchor の2回を測ると、48 kHz で1コアの約 0.1 % になる。
- ALL との差（0.1〜0.6 %）は、ALL 同士の2回の差（0.4 %）と同じ程度で、測定のゆらぎの中にある。

### 13.4 検証

- **Rust**：
  - worker：1回だけ測る、切替で停止中も測り直す、止まった打音を仕上げる、anchor を1回ずつ測って保持しない打音を明示する。
  - 結果：無音と下限、区間の理由、音の終わり、余韻、キックの余韻の上のハイハット（`RingsOn`）、リングの容量が変わらないこと。
  - codec：全結果の往復、最悪の大きさ、整合しない節の拒否。join：`Waiting` と `Predates` の区別。
  - 統合：POST が 15 ms 遅い対で、鍵、DELAY（15 ms ±0.5）、LEVEL（−6.02 dB）、tick を重ねても計測回数が増えないこと、ALL で帯域を落とすこと。
- **FFI**：C の配置、段と同じ鍵で重複しないこと、ペアなしで POST の detail と包絡、C の入口。
- **JUCE**：
  - `verifyBandModel`：段の打音と鍵、全理由と下限・上限
  - `verifyBandRendering`：5 サイズ、案内、面、チップ、理由、ALL で元どおり
  - `verifyBandInteraction`：LIVE が明示済みの最新打音を追う、選んだ打音の包絡を取る、古い PRE の案内
  - `verifyBandTranslations`：実行時の文字列で日本語があること、日本語で全サイズを描けること（指摘11）
  - `verifyBandReasonsAtSmallSizes`：125% で理由を出す、100% で理由が他の値の書体を変えない
  - 描画予算：従来表示と帯域の両方を測ってから合否を出す（指摘10）

## 14. まとめ表示（2026-09-29、B-1104・B-1105）

### 14.1 なぜ

DAWで確かめた Daisuke の指摘：打音ごとの値は速く入れ替わり、どう判断すればよいか分からない。下の4つの数値も活かせていない。
打音1つずつの値を追わせるのをやめ、LIVE では直近の打音をまとめて1つの読みとして示す方向を Daisuke が承認した（「その方向で進めて」）。

### 14.2 エンジン（B-1104）

- `kirin_hypha_poll_attack_band_summary`（POST だけ）が `KirinAttackBandSummary`（2,880 バイト固定）を返す。
- まとめる打音：段と同じ打音の新しい方から最大8打。POST − PRE では対の両側、それ以外は POST が帯域で立ち上がった打音。余韻だけ・無音・未保持の打音は `left_out` に数え、測っている打音は数えない。
- 各段：各打音の値（古い順、無ければ NaN）、中央値、最小と最大、同じ向きの打音の数、`within`（DELAY は1周期の1/32と0.2 msの大きい方、ATT と REL は1周期、LEVEL は0.2 dB）。
  - 状態は `NONE`（値を持つ打音が無い）、`VALUE`、`WITHIN`（差を見分けられない、または POST の ATT が1周期より短い上限）。
  - `withheld`：値の無い打音に最も多い理由（`RINGING`・`NEXT HIT`・`LONG TAIL`）。DELAY の PRE 無しは画面が理由を言う。
- 面：到達と減衰の終わりの中央値、平均の包絡（PRE は POST − PRE のときだけ）、POST の各点の最小と最大。
- 何も測らない。ATTACK の worker が明示した数だけから作る。

### 14.3 画面（B-1105）

- 段は時間ではなく数直線：1打1点、広がり、中央値の棒、0の線と見分けられない範囲。
- LIVE：読み出しは中央値・向きの語（`LATER`・`SLOWER`・`LONGER`・`QUIETER` など）・同じ向きの数（`8/8`）。見分けられなければ `SAME`（`WITHIN 16 ms`）。
  - 300%・200%：平均の HEAD／TAIL と、カード（題、動いた段から1行1事実）。カードが狭ければ `LATER 2.7 ms` のような短い形、さらに値だけにする。向きの語はどれも1つの段にしか使わないので、短い形でも段が分かる。
  - 150%：題と事実の2行。125%：HISTORY に4本の小さい数直線と向きの語（点をクリックできる）。100%：6秒の上に題、下に4つの中央値。
  - 値の無い段は、全サイズで理由を示す（`NO PAIR`・`UPDATE PRE`・`RINGING`・`NEXT HIT`・`LONG TAIL`）。
- 打音を見る：点のクリック（または ←→・HOME）でその打音を固定し、その打音の値と理由、面、ルーペまたは時刻、150%以下では6秒と菌糸を示す。数直線はその点を輪で示す。同じ点、線の空いた所、END、NOW で LIVE に戻る。
- 包絡は、打音を固定して面を出している間だけ取る。LIVE の間はまとめの平均を使い、打音ごとには取らない。
- ホバー：LIVE の段は点と中央値の説明、固定中の段はその打音の値と理由の説明、カードはまとめの説明と戻り方。

### 14.4 検証

- **Rust**：`attack_ffi_band_summary_tests`（C の配置、12打から新しい8打と除外の数、中央値と一致の数、見分けられない範囲、PRE 無し、理由の多数決、何も無いとき、POST だけが取れる）。
- **JUCE**：
  - `verifyBandRendering`：5サイズで、まとめ・何も無いとき・固定・理由・END で同じまとめに戻る・ALL で元どおり。数直線の点、面、チップ。
  - `verifySummaryReasons`：`NO PAIR` と `UPDATE PRE`、`NEXT HIT` と `LONG TAIL` が全サイズで該当の段だけを変える。
  - `verifyBandInteraction`：LIVE では包絡を取らない、面とカードは選ばない、点で固定して包絡を取る、同じ点・線の空き・END で戻る、125% の点、ホバー。
  - `verifyBandTranslations`：まとめの文（題、除外、事実の全形、向きの語、軸、ホバー）が実行時の文字列で日本語を持つ。日本語で全サイズを描く。
  - `verifyChromeCache`：LIVE と固定を行き来しても構造の画像が新しい部品と一致する。描画予算は、まとめを毎フレーム変えて測る。
