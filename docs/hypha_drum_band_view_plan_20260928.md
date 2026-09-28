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
- **交換**：スナップショットは帯域を要求されている間だけ version 4（v3 の末尾に帯域の節を追加、1打 692 バイト、最大 64 打）。要求のない POST（古い POST）には v3 を書く。上限を 262,144 バイトに上げた。帯域の節は自分の event を持ち、details と独立に復号できる。
- **FFI**：`kirin_hypha_set_attack_band(handle, 0..8)` と `kirin_hypha_poll_attack_band`（`KirinAttackBandBatch`：status、band、`pre_band_available`、最大 64 打。各打は kind 0=matched / 2=POST のみ、resolution_micros、delay、PRE/POST 各側の arrival・ATT・REL・LEVEL・HEAD・TAIL）。C の配置は `band_c_layout_is_fixed` で固定。
- **負荷の実測**（`reports_the_worker_cost_with_and_without_a_band`、release、48 kHz ステレオ、2打/秒を実時間で 20 秒、プロセスの CPU 時間）：
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
- **理由の語**：`RINGING`（前の打音の余韻で到達が取れない。DELAY・ATT）、`NEXT HIT`（次の打音で尾が切れた。REL）、`PRE NO BAND`（ペアだが PRE が帯域を返していない。DELAY）、`POST ONLY` / `NO PAIR` は従来どおり。
- **古い PRE**：`pre_band_available == 0` の間は「届く前」と「帯域より古い PRE」を区別できないので、両方を同じ言葉で扱う。段は POST 値、DELAY は `PRE NO BAND`、HEAD の見出しは `PRE: NO BAND YET`、選んだチップのホバーで「帯域より古い PRE からは届かない。PRE を更新すると比べられる」と説明する（R-28）。
- **エンジンとの同期**：帯域は `AttackComponent` のエディター寿命の状態（VIEW と同じ）。エディターは `onBandChange` で `setAttackBand` を呼び、DRUM ページに入るたびに自分の帯域を送り直す（開き直したエディターは ALL、エンジンに前の帯域が残っていても揃う）。プロセッサは帯域を覚え、ATTACK をエンジン（新規・再有効化）へ適用するたびに送り直す。届いた batch の帯域が選んだ帯域と違う間（切替直後）は何も出さない。
- **ホバー**：チップ・HEAD・TAIL・帯域の4段に日本語つきの説明（INV-S40）。従来の4段には付けていない。
- **画像**：`KIRIN_ATTACK_UI_SHOWCASE_DIR` で 63 Hz の見本画像（900 overlay / rows、600、450、375、300）を書く。
- **検証**：`verifyBandModel`（差分・POST 値・PRE 待ち・理由・D4・文字）、`verifyBandRendering`（5 サイズ、チップ、面、段、ALL で完全に元へ戻る）、`verifyBandInteraction`（チップ、面は選ばない、段は選ぶ、ホバー、100% で反応なし）、`verifyChromeCache`（帯域・PRE 待ち・2 ROWS を含む）。`KIRIN_ATTACK_FRAME_BUDGET` で 300% 帯域表示の描画時間も測る。
