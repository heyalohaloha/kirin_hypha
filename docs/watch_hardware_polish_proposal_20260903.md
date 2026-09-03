# Hypha Watch 現状監査と改訂実行計画

**作成日**: 2026-09-03
**実機対象**: Kirin Hypha 1.1.49
**公開基準**: `origin/main` / commit `5261c25`
**UI基準**: tag `v1.1.49` / commit `c9e458d` / B-688
**最新開発基準**: PR #17 / commit `6f551f0` / B-693
**位置づけ**: 現状監査、決定済み製品仕様、依存順を固定した実行計画
**実装状態**: Gate BからFを実装済み。Gate Gの自動検証を実施し、Studio One実機受入を残す

## 結論

公開履歴のmain収束、EBU v05全70素材の適合確認、BS.1770-5監査、macOSとWindowsの署名済み1.1.49リリースは完了している。

次の主工程は配布基盤の再整備ではなく、Watchを通常のメーターとして成立させる表示契約と描画安定性の改修である。

最初に`TRACK/STEM`と`2MIX`を、計測方式ではなく表示と比較の文脈を決める`Meter Context`として導入する。

この分類はPRE/POST、POST/Delta、各domain、画面倍率から独立させる。

測定値、保存値、PRE/POST差分の定義は分類によって変えない。

分類後の最初の実装対象は見た目ではなく、RESET、リサイズ、TIME上部の重なり、6秒FIELD、Focus Trail、ATTACKの描画安定性とする。

安定性が成立した後に、文字、情報階層、スケール、ATTACKの生物発光を整える。

実装はPR #17のB-693を基準に、同じ作業branch上で完了させた。

公開releaseは行っていないため、今回の実装を1.1.49の既存配布物へ反映したとは扱わない。

## 実装結果

- `Meter Context`を導入し、新規は`2MIX`と`FOCUS`、保存済みインスタンスはContext、Scale、表示サイズをstateから復元する。
- LEVELをContext別に再編し、TRACK/STEMはM、S、CRESTとPSR先頭、2MIXはM、S、Iを主表示にした。
- TIMEへ`WIDE`と`FOCUS`の固定軸、正側を含むTrue Peak軸、独立したPLRとCORRレーンを実装した。
- PRE名入力を全倍率で表示し、POST小型表示は接頭辞を省略して実名を優先し、footerへ実versionを表示した。
- RESETをMeter SessionとWatch MAXの同期resetへ統合し、再生中も旧True Peak MAXが復活しないようにした。
- Focus Trailの表示専用平滑化とgap保持、6秒FIELDの固定age bucket、増分peak holdを実装した。
- 300%を独立したInspection密度にし、footer、補助文字、グリフ、ATTACK下部標本の物理サイズを拡大した。
- ATTACK下部へ、実イベントの強度と特徴量に同期する左から右への生物発光、膨張、収縮、残光、配色混合を追加した。無入力時の自律アニメーションは追加していない。
- ATTACKの重いsnapshotコピーは新規eventまたはpair状態変更時だけ行い、描画更新も状態変化時へ制限した。
- rootの画像cacheを廃止し、ホストresize後に古い描画面が残る経路を除いた。

Studio One実機では、5倍率の往復、close到達、DAW再読込後のContext、Scale、サイズ復元、30分連続再生時のATTACKとFIELD、黒枠再発有無を最終受入する。

## 0. 現時点の基準線

### 0.1 完了済みで、今回やり直さないもの

- 公開mainへの履歴収束は完了している。
- `v1.1.49`はLemon Squeezy用macOS pkg、HP用macOS zip、GitHub Release、署名済みWindows installerの3チャネルを同一版で成立させている。
- EBU v05全70素材、BS.1770-5監査、macOS署名とnotarize、Windows Authenticodeとinstall、同版reinstall、1.1.48からのupgrade、uninstallの検証記録が公開releaseに付属している。
- 画面の選択倍率と自由リサイズ後の幅、高さはDisplayState v3ですでにインスタンスstateへ保存される。
- optional Analysisの大型表示はDAW process内2枠上限を維持する。

### 0.2 現時点で残っているもの

- READMEとWindows検証文書が1.1.48の手動ZIPを現行版としており、公開済み1.1.49 installerと矛盾している。
- 実機指摘WHP-001からWHP-020は未実装である。
- `Meter Context`と`WIDE`、`FOCUS`のstate fieldは存在しない。
- 表示倍率の保存はあるが、Studio Oneでの終了、再起動、再読込を含む実機復元証跡は不足している。
- 500行を超えるgrandfathered fileは公開mainに34件あり、500行超過分は合計48,579行である。
- 進行中のPR #17はWatchと同じJUCE editor、processor、observatory領域へ変更を加えるため、同時実装すると競合と再監査を増やす。
- PR #17のB-691で失敗したrelease source contractはB-692で修正済みであり、対象test、release metadata 11件、行数ratchetはlocalでgreenである。全必須CIは再実行中であり、完了まではmerge Gate未成立とする。

### 0.3 実装基準の固定方法

1. 公開文書の1.1.48表記だけを独立した小変更で1.1.49の実態へ合わせる。
2. PR #17のrelease source contract失敗を直し、全必須checkをgreenにする。
3. PR #17をmergeまたはcloseし、Watchと競合する変更の最終形を確定する。
4. 確定mainでrelease source、UI render、Windows preflightを再実行する。
5. そのcommitからWatch専用branchを作る。
6. 以下のGateを順番に進め、Gateを跨いだ見た目の先行実装を行わない。

## 1. 最初に確定する分類

### 1.1 正式な軸

製品上の軸名を`Meter Context`とする。

画面に出す選択肢は次の2種類とする。

| 選択肢 | 対象 | 主な観察目的 |
|---|---|---|
| `TRACK/STEM` | 単体トラック、バス、ステム | 素材や処理単位を詰め、同種の対象と比較する |
| `2MIX` | 完成前後のステレオミックス、マスター最終段 | 完成域の小さな変化、納品状態、全体傾向を監視する |

TRACKとSTEMは初期版では一つにまとめる。

両者を分ける必要性が実機記録から確認された場合だけ、後から第三分類を提案する。

### 1.2 他の軸との関係

| 軸 | 値 | 意味 |
|---|---|---|
| Meter Context | TRACK/STEM、2MIX | 表示優先度、固定スケール、比較文脈 |
| Role | PRE、POST | チェーン前後の測定位置 |
| Observation Target | POST、Delta | 絶対値か、POST minus PREか |
| Domain | LEVEL、TIME、FREQ、SPACE | 観察する領域 |
| View Size | 100、125、150、200、300% | 情報密度と物理的な可読性 |
| ATTACK Profile | DRUM、将来の2MIX | ATTACKイベントの測定定義 |

`Meter Context`の`2MIX`を選んでも、ATTACKの2MIX profileを起動しない。

現行ATTACKは検証済みのDRUM定義だけを公開しており、ATTACKの2MIX profileは独立した精度契約と検証を終えるまで出してはならない。

### 1.3 選択規則

- 音声から分類を自動推定しない。
- 新規インスタンスの初期値は`2MIX`とする。
- 新規`2MIX`の初期スケールは`FOCUS`とする。
- `TRACK/STEM`へ変更した時の初期スケールは`WIDE`とする。
- 利用者が変更したContext、スケール、表示倍率はインスタンス単位でDAW stateへ保存する。
- DAW stateにContextが存在する場合は、保存値を新規インスタンスの初期値より必ず優先する。
- `TRACK/STEM`で保存したインスタンスは、プロジェクト再読込時も`TRACK/STEM`で復元する。
- 保存済みの`TRACK/STEM`を、起動時やversion更新時に`2MIX`へ置き換えない。
- プラグイン全体の既定値変更によって、保存済みインスタンスのContextを上書きしない。
- Contextを切り替えた後に手動で選んだ`WIDE`または`FOCUS`も、再読込時に復元する。
- 利用者が選んだ表示倍率も、プロジェクト再読込時に同じ倍率で復元する。
- 新規インスタンス用の既定倍率は、保存済みの表示倍率がない場合だけ適用する。
- PREとPOSTの計測成立条件へ分類一致を追加しない。
- 分類を変更しても計測エンジン、履歴の原データ、保存値をリセットしない。
- 表示スケールが変わったことは画面上で即座に識別できるようにする。
- Context保存fieldを持たない過去セッションは互換対象外とし、新規インスタンスと同じ`2MIX`へフォールバックする。
- 過去versionの表示状態を再現するための`UNSET`やlegacy modeは設けない。

### 1.4 A/Bとの境界

PRE/POST Deltaは一つの処理チェーンの前後差であり、A/Bではない。

A/Bは二つの別対象を並べて比較する操作として定義する。

初期版のA/Bは同じ`Meter Context`内だけで成立させる。

TRACK/STEMと2MIXを二画面で同時に見ることは許可するが、異種間のA/B差分値は作らない。

既存の大型表示2枠は維持する。

想定構成は、2MIXを一枠に常設し、残る一枠を作業中のTRACK/STEMへ割り当てる形である。

TRACK/STEM同士または2MIX同士を比較する場合は、同じ二枠をA/Bとして使う。

Captureや過去セッションとのA/Bは別工程とし、今回の分類導入へ混ぜない。

## 2. Context別の表示初期案

### 2.1 LEVEL

| Context | 上部の主指標 | 中部の補助指標 |
|---|---|---|
| TRACK/STEM | M、S、CREST | PSR、TP、MAX TP、I、LRA |
| 2MIX | M、S、I | TP、MAX TP、LRA、PLR、CREST |

100%と125%は最大3事実に制限する。

TRACK/STEMでは`M`、`S`、`CREST`を表示する。

2MIXでは`M`、`S`、`I`を表示する。

小型表示でもContextの意味を変えず、TPなどの補助指標を主指標へ無断で置き換えない。

補助指標は、倍率に応じた中部、別domain、または明示操作で到達できるようにする。

### 2.2 TIMEの固定スケール

| Context | LUFS初期スケール | 範囲外の扱い |
|---|---|---|
| TRACK/STEM | 0から-60 LUFS | 上下端に範囲外マーカーと実数値を出す |
| 2MIX | 0から-36 LUFS | 上下端に範囲外マーカーと実数値を出す |

この数値をContext別の初期スケールとして実装する。

TRACK/STEMは小さい素材を失わない広い範囲を持たせる。

2MIXは完成域の変化を大きく見せる。

利用者は`WIDE`と`FOCUS`を手動で切り替えられる。

`WIDE`は-60から0 LUFS、`FOCUS`は-36から0 LUFSの固定スケールとする。

比較基準が再生中に動く動的オートスケールは採用しない。

0 dBTPを超えた値は上端へ潰さず、正側の変化と実数値を保持する。

True Peakの正側ヘッドルームと軸範囲はDaisuke決定事項ではないため、既知信号を使った表示試作後に固定する。

PLRとCORRは細い付帯線ではなく、上下限、基準線、変化量を読める独立レーンにする。

### 2.3 ATTACKの扱い

TRACK/STEM選択時でも、ATTACKを自動でDRUMとして起動しない。

DRUMまたはpercussion対象であることを利用者が明示した場合だけ、現行DRUM定義を使う。

2MIX選択時のATTACKは、2MIX profileの検証完了まで利用不可と明示する。

一般のMeter ContextとATTACK Profileを別にすることで、表示用途の分類から未検証の解析を誤って有効化する事故を防ぐ。

## 3. 実機指摘を生んだ共通原因

### RC-01: Meter Contextが存在しない

現行は同じLEVEL配列と同じTIMEスケールをTRACK、STEM、2MIXへ使っている。

これが、2MIXでLUFSの動きが上部に集中することと、LEVEL上部と中部を用途別に使えないことの共通原因である。

該当指摘はWHP-012とWHP-020である。

### RC-02: TIME上部の配置所有者が二重になっている

100%と125%では、親のTIME切替ボタンがbody上端へ重ねて配置される。

ATTACK、SHARP、LIVE、HISTORYの各子画面も同じ上端へ見出しと凡例を描画する。

150%以上のObservatoryでは親が24 pxを予約するが、小型表示では子画面の領域を縮めていない。

これがATTACK、SHARP、LIVE、HISTORYの文字が隠れる直接原因である。

該当指摘はWHP-008、WHP-009、WHP-010、WHP-011である。

### RC-03: 倍率ではなく同じ描画密度を使い回している

v1.1.49の300%は900 x 600をネイティブ解像度で描画している。

しかし200%と300%は同じ`Density::observatory`を使い、footerは両方24 px、共通ボタン文字は最大10 pxである。

ATTACKの補助文字も6.2から9.2 pxの固定値が多く、900 x 600専用の情報階層がない。

イベントグリフも横幅50 pxで上限に達するため、画面だけを広げるほど相対的に小さくなる。

これが300%でも文字、footer、グリフが大きく感じられない直接原因である。

該当指摘はWHP-002、WHP-014、WHP-017、WHP-019である。

### RC-04: 接続領域が固定幅で、実文字を測っていない

PREの名前欄は375 px未満で明示的に非表示となる。

これは既存製品契約の300 x 200 PREにnameを出す要件と矛盾する。

POSTの接続領域はLED、dropdown、pair文字列が固定幅を分け合い、長い名前の省略契約を持たない。

これがPREの名前入力欠落とPOST右上の文字切れの直接原因である。

該当指摘はWHP-001とWHP-003である。

### RC-05: RESET対象の状態所有者が分裂している

RESETは`MeterSession`とDelta historyを初期化する。

小型LEVELのMAX表示は別の`WatchMaxTracker`から取得する。

RESET経路は`WatchMaxTracker::reset()`を呼ばない。

したがってRESET自体が成功しても、画面には古いMAXが残る。

これがWHP-007の直接原因である。

### RC-06: 履歴の表示縮約が時間に対して安定していない

6秒FIELDは履歴件数を32で割った`rowStride`を使う。

履歴が64、96、128、160件へ増えるたびにstrideが変わり、過去行の選択位置がまとめて入れ替わる。

さらに新規フレームごとに最大180フレーム x 256 bandのpeak holdを再計算する。

この再索引と増加する描画負荷が、再生開始から数秒後にFIELDが不安定になる直接原因である。

Focus Trailは100%で180点の3点ごとを直線接続し、表示用の時間平滑化を持たない。

さらに履歴が持つgap判定を描画側が使わず、欠測の前後を線で結んでいる。

これがFocus Trailの折れた見え方を強め、Measurement Truthの欠測原則とも矛盾している。

該当指摘はWHP-005とWHP-006である。

### RC-07: TIMEグラフの範囲と補助レーンが固定されている

HISTORYのLUFSは0から-48、True Peakは0から-24へ固定されている。

0 dBTPを超えた値はすべて同じ上端座標へ制限される。

PLRとCORRは倍率にかかわらず各13から22 pxへ制限される。

M、S、TPは色を主な識別手段とし、小型表示では凡例がRC-02の重なりで見えなくなる。

これがWHP-011とWHP-012の直接原因である。

### RC-08: ATTACKが更新ごとに大きな状態と描画を再構築する

ATTACK画面は30 Hzで更新される。

各tickで複数の大容量固定batchを取得し、内容の更新有無にかかわらずsnapshotをコピーして再描画する。

描画側は最大600点のwaveform、19本のstrand、複数のveil、最大240 event glyph、下部specimenを再構築する。

LIVE追従では最新eventへの選択も更新され、選択標本と4発光レイヤーが一度に切り替わる。

この構造は画面更新時間を一定に保つ契約を持たず、密なeventと大画面で不安定になりやすい。

黄色い弧は選択eventの位置だが、下部詳細が省略されるサイズでも単独で残る。

4発光レイヤーには実測event内の伝播位相がなく、4箇所が同時に点滅して見える。

該当指摘はWHP-015、WHP-016、WHP-018である。

### RC-09: ホストを含むリサイズ契約がない

Hypha本体は5段階の選択で直接`setSize()`を呼び、300 x 200から900 x 600までの自由リサイズも許可する。

実機の黒い領域はHyphaのbodyではなく、Studio One側の上部chromeと拡張されたplugin client領域の境界に出ている。

v1.1.49でroot image bufferが有効になるのは600 px超だけなので、125%で出る黒領域をroot bufferだけでは説明できない。

現時点で原因範囲はホストとのresize negotiation、peer bounds、親window再配置へ絞れたが、一つのAPI呼出しまでの確定には計測付き再現が必要である。

該当指摘はWHP-004である。

### RC-10: テストが完成画面と実ホストを再現していない

native composite testはshellと各bodyを別々に描いて合成する。

実際の`timePageNavigation`を含む完成component treeを描かないため、RC-02の重なりを検出できない。

描画テストは領域内にinkがあることを主に確認し、実文字列の収まり、物理的な最小文字、意味の識別を判定しない。

Studio Oneの親windowとchromeも対象外なので、RC-09を検出できない。

通常画面にSemVerがないため、実機バイナリと監査ソースの一致も画面だけでは確認できない。

該当指摘はWHP-003、WHP-004、WHP-008、WHP-009、WHP-010、WHP-011、WHP-012、WHP-013、WHP-014、WHP-019である。

## 4. 改訂工程

### Gate A: 実装基準を収束させる

1. README、Windows検証文書、release metadata testを公開済み1.1.49 installerの事実へ合わせる。B-692で完了。
2. PR #17のrelease source contract失敗を解消する。B-692でlocal検証完了。全必須CIのgreenを待つ。
3. PR #17をmergeまたはcloseし、Watchと重なるeditor、processor、observatoryの最終形を確定する。
4. 確定mainのcommitとB番号をWatch実装基準として記録する。
5. release source、UI render、Windows preflightのgreenを基準証跡として残す。
6. 現在の1.1.48作業branchへWatch変更を積まない。

公開文書の訂正はWatch実装と独立して先に完了できる。

PR #17が確定するまでは原因監査とfixture設計だけを進め、同じソースへの実装は開始しない。

### Gate B: Presentation StateとContextを一本化する

1. `MeterContext`を`TRACK/STEM`と`2MIX`の二値で定義する。
2. `ScaleMode`を`WIDE`と`FOCUS`の二値で定義する。
3. 新規インスタンスだけを`2MIX`と`FOCUS`で開始する。
4. Contextと手動scaleをインスタンスのDAW stateへ保存する。
5. 既存のDisplayState v3が保存する倍率、自由リサイズ後の幅、高さを保持し、新しいstate追加で失わない。
6. 保存値を復元した後に新規インスタンス用初期値を再適用しない。
7. Context別の上部指標、中部指標、TIME軸を一つの`PresentationProfile`から決める。
8. Context変更を計測engine、履歴原データ、PRE/POST pairing、保存値へ伝播させない。
9. 一般ContextとATTACK Profileを別軸として維持する。

このGateの完了条件は、状態保存の単体testだけではない。

Studio Oneで新規2MIX、TRACK/STEMへの変更、手動FOCUS、手動WIDE、自由リサイズを別々のインスタンスへ保存し、DAW終了と再起動後に全項目が復元されることを確認する。

### Gate C: 操作阻害と表示上の事実欠落を直す

1. RESETを一つのtransactionにし、MeterSession、WatchMax、全hold、対象history、表示cacheへ同じgenerationを発行する。
2. TIMEの上部行を親だけが所有し、全サイズでbodyから領域を予約する。
3. PRE nameとPOST pairを一つのconnection layout contractで扱う。
4. 100%でもPRE name入力へ到達できるようにする。
5. pair名は実測した文字幅から省略し、全文へ到達できる導線を持たせる。
6. バージョンをPRE、POST、AU、VST3の通常画面から到達できる共通情報面へ出す。
7. 0 dBTP超を保持したまま表示する方式をfixture比較で決め、上端張り付きをなくす。
8. Studio Oneのresize要求、返答、editor bounds、peer bounds、親window boundsを記録する診断buildを作る。
9. 125%以上で出る黒い余剰領域の原因を確定し、host操作を塞がないresize transactionへ直す。
10. hostが要求サイズを受理しない場合は直前の安全なサイズへ戻す。

黒い余剰領域は全倍率の実機評価を妨げるため、文字や生物発光より先に閉じる。

### Gate D: 描画時間と時間表現を安定させる

1. 6秒FIELDを固定時刻bucketへ変え、履歴件数によって既存行の時刻が入れ替わらないようにする。
2. peak holdを全履歴再走査ではなく、固定上限の処理で更新する。
3. Focus Trailはgapで必ずpathを切る。
4. Focus Trailの連続run内だけへ表示用平滑化を適用し、実測点、数値readout、保存値を変更しない。
5. ATTACKはgenerationまたはendpointが変化した時だけsnapshotを更新する。
6. ATTACKのpath、glyph、specimenをcacheし、UI tickごとの再構築量へ上限を設ける。
7. timerとpresentation tickからの重複repaintを一つの更新規則へまとめる。
8. 900 x 600のroot bufferingは有無を同じfixtureで測り、改善が確認できない場合は外す。

既存testは画面別に異なるpaint上限を持つ。

本Gateでは一律の新しい数値を先に置かず、現行上限を悪化させないことと、二つの大型枠を30分動かしてUI負荷が時間経過で増えないことを最初の性能契約とする。

このGateでは計測アルゴリズム、保存値、Audio Threadを変更しない。

### Gate E: 5サイズの情報階層を再設計する

1. 300 x 200、375 x 250、450 x 300、600 x 400、900 x 600を別々の完成画面として設計する。
2. 900 x 600専用の`Inspection` densityを追加する。
3. 文字を用途別tokenへ統合し、実機で固定した最小値より小さくしない。
4. 入らない補助情報は文字を縮めず、省略順位に従って減らす。
5. TRACK/STEMは全サイズで上部M、S、CRESTを保つ。
6. 2MIXは全サイズで上部M、S、Iを保つ。
7. TRACK/STEMの中部先頭をPSRにする。
8. WIDEとFOCUSの現在値をTIMEで常に識別できるようにする。
9. 200%と300%でfooter、操作領域、event glyph、補助レーンへ追加面積を配分する。
10. LIVEはM、TP、Sharpnessの3系列を維持し、1系列が1本に見えるstrokeへ整理する。
11. HISTORYは色に加えて線種、直結label、値表示で系列を識別できるようにする。
12. PLRとCORRへ上下限、基準、変化を読める独立した縦幅を与える。

各サイズは同じ画面を単純拡大しない。

ただしContextが決める主要指標の意味は、サイズによって入れ替えない。

### Gate F: ATTACKの観察表現を仕上げる

1. 選択event、時間軸marker、下部詳細を一つの視線経路で結ぶ。
2. 詳細を出せないサイズでは意味を失った選択arcを単独表示しない。
3. event glyphを画面密度とevent密度の両方で拡大し、重なり時は集約表示する。
4. event時刻を起点に、左から右へ一続きの発光を走らせる。
5. Strengthは明度と広がり、Transientは立上り、Textureは残光、Brightnessは色温度寄りの明度として連続量へ対応させる。
6. 各色の境界を重ね、4個の独立ランプではなく一つの生体標本として見せる。
7. 自由点滅は使わず、再生停止とLOCKでは時刻の進行を正しく停止する。

生物発光はGate Dの描画安定性とGate Eの情報階層が成立した後に実装する。

### Gate G: 統合実機検証と同版releaseを成立させる

1. macOSのAUとVST3で5倍率を順方向、逆方向、自由リサイズで確認する。
2. WindowsのDPI 100、125、150、200%で同じ表示契約を確認する。
3. Studio Oneでclose、pin、preset、reopen、別monitor移動、DAW state復元を確認する。
4. PRE、POST、paired、unpaired、Active、Inactive、Bypassedを通す。
5. release source、Rust workspace、ignored parity 20件、ignored pairing 5件、UI render、pluginval、bit transparencyを通す。
6. Lemon Squeezy、HP無料配布、Windows installerを同一commit、同一versionで揃える。

## 5. 構造債務の扱い

Watch完成を34件すべての巨大file解消で止めない。

ただし、Watch変更を既存の巨大fileへそのまま追記しない。

次の責務は実装前に専用moduleへ分離する。

| 現在の集中箇所 | Watchで分離する責務 |
|---|---|
| `PluginProcessor.cpp` | Presentation Stateのserialize、restore、default適用 |
| `PluginEditor.cpp` | TIME navigation、更新判定、ATTACK polling、resize transaction |
| `kirin_hypha_ffi/src/lib.rs` | reset generationとreset対象の集約 |
| 大型UI contract test | state、layout、performance、host診断fixtureごとのtest分割 |

新規fileは500行以下とし、既存ratchet allowanceを増やさない。

分離によって既存fileが500行以下になった時点でbaselineから削除する。

`plugin_data.rs`や`record_writer.rs`などWatchと無関係な巨大fileの返済は、Watch releaseを止めず、別の構造債務工程として続ける。

## 6. 合格条件

### 6.1 分類

- TRACK/STEMと2MIXの選択が全5サイズで確認できる。
- Context fieldのない新規インスタンスは`2MIX`と`FOCUS`で始まる。
- TRACK/STEMは全サイズで上部M、S、CRESTを表示する。
- 2MIXは全サイズで上部M、S、Iを表示する。
- TRACK/STEMの中部先頭はPSRである。
- `TRACK/STEM`へ変更して保存したインスタンスは、DAW終了と再起動後も`TRACK/STEM`へ戻る。
- `TRACK/STEM`で`FOCUS`を手動選択したインスタンスは、再読込後も`FOCUS`へ戻る。
- `2MIX`で`WIDE`を手動選択したインスタンスは、再読込後も`WIDE`へ戻る。
- 利用者が選んだ表示倍率と自由リサイズ後の幅、高さは、DAW終了と再起動後も同じ大きさへ戻る。
- 保存済みContextの復元後に、新規インスタンス用初期値を再適用しない。
- 保存済み表示倍率の復元後に、新規インスタンス用既定倍率を再適用しない。
- Context変更前後で同一入力の測定値と保存値がbit-identicalである。
- Context変更でATTACK Profileが暗黙に変わらない。
- 異なるContext間ではA/B差分を生成しない。

### 6.2 配置と文字

- 実際の最大長pair名と翻訳文字列を使い、全5サイズで重なりが0件である。
- 省略された文字には全文へ到達できる方法がある。
- 100%でPRE nameの入力へ到達できる。
- 300%は200%と異なるInspection階層を持ち、footer、補助文字、event glyphが物理的に大きくなる。
- RetinaとWindows DPIで、文字を読ませるための自動縮小を最低値未満へ行わない。

### 6.3 RESET

- 再生中のRESET後、2回のUI tick以内に旧MAXが画面から消える。
- RESET後の最初の有効測定から新しいMAXが始まる。
- M、S、TP、CREST、I、LRA、PLR、channel hold、履歴が同じreset generationへ移る。
- 停止中、再生中、連打、PRE、POST、paired、unpairedを通す。
- 競合で受理できなかった明示操作は無言にしない。

### 6.4 履歴と描画

- Focus Trailは欠測区間を線で結ばない。
- 表示用平滑化は保存値と数値readoutを変更しない。
- 6秒FIELDは履歴が64、96、128、160件を跨いでも既存行が別の時刻へ飛ばない。
- 6秒FIELDとATTACKは30分の連続再生でちらつき、瞬間欠落、位置飛び、残像を出さない。
- 既存の画面別paint上限を全サイズで悪化させない。
- 性能Gateは100%と300%、一枠と二枠、疎なeventと密なevent、LIVEとLOCKで測る。
- 二枠を30分連続再生しても、UI tickの処理時間と取得量が時間経過で増えない。
- UI負荷がAudio Threadのdrop、再生停止、音声変化を発生させない。

### 6.5 スケール

- 2MIX fixtureで-36から0 LUFSの変化を読み取れる。
- TRACK/STEM fixtureで-60 LUFS付近を範囲外として消さない。
- 0 dBTPを超える既知信号が上端張り付きにならず、値と動きを保持する。
- True Peakの正側ヘッドルームはfixture比較とDaisuke確認後に固定されている。
- PLRとCORRは上下限、基準、時間変化を目視できる。
- A/Bする二画面は同じContext内で同じ固定軸を使う。

### 6.6 ホスト

- resize完了後、plugin clientとホストが報告するeditor boundsの差が各辺1 px以内である。
- 5倍率を20往復して黒い余剰領域を一度も出さない。
- 全倍率でStudio Oneのclose操作へ到達できる。
- reopen、DAW state復元、別monitor移動、Retina倍率変更でも安全なサイズへ復帰する。

## 7. 実機検証表

最低限、次の組合せを一つのrelease candidateで確認する。

| 軸 | 対象 |
|---|---|
| Role | PRE、POST |
| Format | AU、VST3 |
| OS | macOS、Windows |
| Size | 100、125、150、200、300% |
| Context | TRACK/STEM、2MIX |
| Signal | Active、Inactive、Bypassed |
| Pair | paired、unpaired、PRE OFF |
| Domain | LEVEL、TIME、FREQ、SPACE |
| TIME | HISTORY、ATTACK、SHARP、LIVE |
| ATTACK | LIVE、LOCK、OVERLAY、2 ROWS、疎event、密event |

完成画面のnative render testは、shell、実navigation、実body、overlay、tooltipを同じcomponent treeで描く。

文字衝突はpixel量ではなく、各semantic boundsの交差、文字の測定幅、最低font tokenで検証する。

Studio Oneのhost chromeはnative render testでは代替せず、独立した実機Gateとして残す。

## 8. Daisuke決定事項

1. 新規インスタンスは`2MIX`で開始する。
2. Context fieldを持たない過去セッションの互換性は対象外とする。
3. 一度保存したContextはDAW再読込時に必ず復元する。
4. TRACK/STEMの上部はM、S、CRESTとし、中部先頭へPSRを置く。
5. 2MIXの上部はM、S、Iとする。
6. TRACK/STEMは`WIDE`の-60から0 LUFSで開始する。
7. 2MIXは`FOCUS`の-36から0 LUFSで開始する。
8. `WIDE`と`FOCUS`は手動で切り替えられ、その選択もインスタンスへ保存する。
9. 利用者が選んだ表示倍率もインスタンスへ保存し、DAW再読込時に復元する。

初期A/Bをlive instanceだけに限定するか、Captureとセッション比較を同時に含めるかは別決定として残す。

## 9. Gate途中で確定する事項

次の項目は現時点で推測によって固定しない。

| 項目 | 確定時期 | 確定方法 |
|---|---|---|
| True Peakの正側ヘッドルーム | Gate C | 0 dBTP超の複数fixtureを100%と300%で比較する |
| version表示の位置 | Gate C | 通常観察を妨げず、全roleとformatから2操作以内で到達できる案を比較する |
| 各文字tokenの最小値 | Gate E | RetinaとWindows DPIの実機画像で決める |
| サイズ別の省略順位 | Gate E | 指標名、値、単位、状態、補助説明の順を完成画面fixtureで確認する |
| ATTACKの伝播時間と残光 | Gate F | event時刻との一致とCE 2226の見え方を実機で確認する |
| A/Bのlive、Capture、session範囲 | Watch後 | PR #17のReference契約と混同しない独立仕様として決める |

### 9.1 REF Blind候補

REFのBlindは、Referenceが`READY`で、Aの測定、音源identity検証、形式互換、実時間再生がすべて成立した時だけ入口を表示する。

開始後はA/Bの割当、選択色、名称、状態文から正体を推測できないようにし、利用者が明示的にRevealした後だけ割当を開示する。Kirin OSと同じく、Blind Compareでの事前assessmentは必須にしない。

Kirin OSの既存Blind Compareを振る舞いの正本とする。HyphaはGPLv3分離を維持し、コードを共有せず、同じ境界をReference runtimeへ独立実装する。

割当はHyphaの非公開runtime stateだけに保持する。画面には`1 / 2`だけを出し、実際のAudio Thread callbackがAまたはBを出力した後に限って選択表示を更新する。

開示前は音源名、source種別、測定値、delta、gain、alignmentを描画、tooltip、accessible nameへ出さない。明示Reveal後に割当と各事実を同時に戻す。

Reference変更、runtime再構成、画面終了、REF画面からの移動ではAへ戻す。条件変更による終了は割当を開示しない。

## 10. Watch release後の順番

Watchの完成後は、機能数を増やす前に、通常メーターとしての利用範囲とKirin固有価値を順番に広げる。

1. 読み上げ、keyboard操作、色以外の系列識別を全画面へ通し、アクセシビリティを製品契約にする。
2. Kirin OSとの記録、検証、セッション横断比較を、プラグイン単体の表示と混ぜずに完成させる。
3. Verification Reportを各releaseで継続生成し、計測精度と出荷物の一致を公開資産として保つ。
4. AAX、standalone、CLAPは、利用者需要、検証環境、署名運用を確認して優先順位を決める。
5. `plugin_data.rs`、`record_writer.rs`、FFIなどの構造債務を、機能工程と独立した返済計画で減らす。

この順番により、Watchで「普通に選べる」を先に成立させ、Kirin OS連動で「代替しにくい」へ進む。

## 参照

- `docs/watch_hardware_polish_notes_20260903.md`
- `docs/hypha_bs1770_5_r128_v5_audit_20260831.md`
- `docs/public_history_identity.md`
- `docs/windows_external_validation.md`
- `docs/hypha_meter_product_contract_20260831.md`
- `docs/transient_delta_design.md`
- `docs/attack_perceptual_visual_contract_20260831.md`
- `juce_shell/src/PluginEditor.cpp`
- `juce_shell/src/HyphaObservatoryResizeContract.h`
- `juce_shell/src/HyphaObservatoryContract.h`
- `juce_shell/src/HyphaObservatoryPresentation.h`
- `juce_shell/src/HyphaObservatoryMetrics.cpp`
- `juce_shell/src/HyphaTimeHistoryPainter.cpp`
- `juce_shell/src/HyphaSpectrumFocusTrailPainter.cpp`
- `juce_shell/src/HyphaAbsoluteSpectrumHistory.h`
- `juce_shell/src/HyphaSpectrumPainter.cpp`
- `juce_shell/src/HyphaAttackUiContract.h`
- `juce_shell/src/HyphaAttackComponent.cpp`
- `juce_shell/src/HyphaAttackPainter.cpp`
- `crates/kirin_hypha_ffi/src/lib.rs`
- `juce_shell/tests/ObservatoryCompositeContractTest.cpp`
