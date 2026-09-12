# Hypha SPACE DECAY 実装定義

- 日付：2026-09-10
- 状態：計算coreを固定。自動区間選択と製品表示は評価待ち
- 計算定義ID：`hypha.space.broadband-fixed-window.v2`

## 採用する構造

現在のSPACE FIELDは成立済みの別観測として維持する。
SPACE DECAYでは、局所的な複数の減衰を主観測とし、単一の直線的な20 dB減衰が成立した場合だけD20相当時間を補助観測として扱う。
どちらも残響、奥行き、RT60、良否を断定しない。

計算は`kirin_measure::space_decay`だけを正本とする。
手動区間probeと自動解析probeも同じcoreを呼び、別実装の同名指標を残さない。

## 固定した計算

EARLYはイベント起点から`[0,80 ms)`と`[80,250 ms)`の非正規化energy比を`10 log10(E0_80 / E80_250)`で表す。
各境界はnative sample rateの固定起点から丸め、block分割や繰返し加算へ依存させない。
monoは一チャンネル、stereoは左右の二乗値を平均し、逆相を無音と取り違えない。

減衰包絡線は10 msごとの平均powerをdBへ変換する。
floor以下のbinは欠測境界であり、前後を連結、補間、ゼロ埋めしない。
局所episodeはpeakからtroughまでの事実を、開始sample、排他的終了sample、点数、実測低下幅、傾き、R²、終了理由とともに保持する。

D20相当時間は負の回帰傾きから`20 / abs(slope)`で求める。
実際の区間終端が20 dB以上低下していない場合は算出しない。
さらに最低点数、最大再上昇、最低R²を明示したpolicyに通った場合だけ製品候補とする。
policyには暗黙のdefaultを設けず、評価版と係数の固定なしに製品routeから利用できない構造にする。

## 2026-09-10の実音診断

人が追跡可能とした5区間では、6 dB再上昇条件による局所episodeを全区間で観測した。
一方、「追える減衰なし」と確定した4音源の30秒全体にも、同条件でそれぞれ35、89、78、117件のepisodeが生じた。
最大低下は13.671〜25.940 dBだった。

よって、局所episodeの存在や6 dB閾値だけをSPACEの成立判定には使えない。
この診断は局所減衰を客観的に列挙できることを示すが、聴感上のSPACEを識別する精度を示さない。

## 製品接続のgate

1. 候補を表示しない状態で、対象区間の網羅注釈を取得する。
2. 開発集合でepisodeの選択規則と集約を決め、誤認、区間Coverage、曲別偏りを測る。
3. sample rate、block境界、固定gain、mono/stereo、無音、持続音、fade、gate、次onset、drop、seek、worker再起動を試験する。
4. PRE起点と同じ区間をPOSTへ写像し、同一入力と固定gainで値が不変になることを確認する。
5. 定義ID、policy、係数、評価器、素材hashを固定してから未使用holdoutを開く。
6. PrecisionとCoverageの採用値を実測根拠付きで確定し、合格後だけABI、Capture、JUCE表示を有効化する。
7. macOS AU、macOS VST3、Windows VST3で音声bit identity、0 sample latency、worker CPU、queue、描画時間を確認する。

gate未通過時はSPACE FIELDだけを維持し、閾値緩和、D20の捏造、DRUM検出器への暗黙fallbackを行わない。
