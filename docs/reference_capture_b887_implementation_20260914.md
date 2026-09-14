# B-887 CaptureとReference観測の構造修正

作成日：2026-09-14。
対象：B-886の[構造修正計画](reference_capture_b885_structural_repair_plan_20260914.md)。
状態：実装、対象native試験、全体baseline、最小画面を含む実寸検証は完了。
DAW実機とWindowsの現行候補での受入確認は未実施であり、公開リリースの完了を示す文書ではない。

## 修正した動作

Captureの開始要求をworkerが受け取っても、開始受付済みの予約は保持する。
処理IDは準備、取得、保存確定、資源解放まで一貫させる。
重複Startは新しい保存世代を発行せず、古いFinish/Cancelは次の取得へ作用しない。
保存確定前のCancelと新しいRestoreは古い結果の確定を阻止する。
確定した保存payloadを公開した後にhost dirtyを通知し、解放が終わるまで次のStartを受け付けない。

PRE/POST BlindとVersion Blindは、同じPOSTのCapture予約と排他的に開始する。
解析枠の取得より前に開始予約を取り、反対側から同時に開始される経路も閉じる。
既存の試聴排他、Capture barrier、音声側の通常復帰確認は維持する。

ReferenceのLIVE表示、保持Aへの変更照合、明示Capture、比較試聴は、同じengineのRust所有者から解析利用権を得る。
物理的な上限は既存OS leaseの2枠のままである。
画面を閉じても、進行中のjobが終わるまではその利用権を保持する。
engineの再生成時は旧jobの終了を待ってから新しい所有者で解析する。
観測だけで試聴のactive、Keep禁止、Δ抑止を発生させない。

表示用入力のRTコピーを既存Capture queueへ集約した。
そのworkerが変更照合とLIVEへの配信を行い、Bの読み込みと変換は別の既存workerに残す。
LIVEの旧RT入力口と、借用を示すだけのboolによるadmissionを削除した。
LIVE側のqueueも256 frames単位へ揃え、滞留可能なframe数を保つ。
新しい常駐thread、watcher、全曲PCM、保存schemaは追加していない。

表示は保持文書、今回の操作結果、現在の照合結果から一箇所で作る。
以前の失敗messageが現在の `A DIFFERS` を隠すことはない。
PARTIAL、過去の確認、再取得失敗は別の事実として残す。
必要な場合だけ既存Capture領域を2行にし、小さいサイズでも省略しない。
100%の短いReference領域では重複する見出しと余白を減らし、文字を縮小せずA/B/C、選択、取得状態を収める。
範囲と長い理由はhoverとaccessibilityの補助情報へ置く。
ボタンの操作は描画時の処理IDへ結び、マウス連打とReturn/Spaceの押し続けでStartがCancelへ化けないようにした。

## 対象試験の結果

以下はnative harnessとRust試験の実測であり、DAWの操作・音声出力の実証ではない。

| 対象 | 確認結果 |
| --- | --- |
| R1 開始競合 | 遅延したadmission中の2回目のStartは不受理。48,000 frames、保持文書あり、保存1,420 bytes |
| 取消・復元 | Starting中のCancel/Restore、Finalizing中のCancel、古い処理の完了と取消、新しいengineへの交代を検証 |
| R2 LIVE入力 | 保持Aあり/なしの両方で既存8秒fixtureの80区間を取得 |
| 解析枠 | LIVEと保持Aの照合を併用しても別POSTが2枠目を取得可能。3つ目は拒否 |
| jobの寿命 | UI需要とengineを破棄しても最後のjobが終了するまで物理枠を保持 |
| 遅いB | Visual workerをbarrierで停止。LIVE queueが満杯でもCaptureは192,000 framesを保持し、同じ位置のA変更も検知 |
| R3 描画 | 差と失敗を同時表示。主表示の画素変化をassertし、全5サイズの2行配置、PARTIAL、LAST、Blind秘匿を確認 |
| keyboard | Returnの連続入力を1操作として扱い、離して押したCancelは受理 |
| 既存Capture | 保存/復元、固定Gain、B不在、欠損B、seek、clock/rate/channel変更、非有限値、queue飽和を検証 |
| FFI | 対象admission試験5件pass。観測がΔ/Keepに作用せず、Capture/Reference/Blindの排他を維持 |

対象実行ログは `target/b887/capture-repair.log`、`visual-repair.log`、`ffi-admission-tests.log`、`ui-entry-final.log` にある。
描画は同ディレクトリの `render-actual-bounds/capture-*.png` と `render-actual-bounds/capture-failure-*.png` に保存し、300と900の通常/複合状態を目視確認した。

## 軽量性

| 項目 | 実測・境界 |
| --- | --- |
| 新しい配信処理 | 48 kHz stereo、100 ms入力あたりp95 0.027435 ms。目標0.1 ms以下 |
| 2本のqueue | 実sizeof合計2,019,840 bytes、約1.93 MiB。上限2 MiB |
| 入口の保持容量 | 122,624 frames（制御用の空き1 slotを除く） |
| RT heap操作 | 入力と飽和試験で0。通常Aのサンプル不変を確認 |
| 常駐thread | 追加0。既存のCapture/LIVE/過去B投影workerを使用 |

配信の測定はqueueに空きがあることを確認して行い、queue満杯でcopyを省いた時間を性能値に使っていない。
p95は40回の測定から求めた。
変更照合の数式、既存4秒校正、過去Bの位置/Gain根拠は変更していない。
通常停止では観測の鮮度だけを失わせ、時刻軸が変わったという判定にはしない。
seekや途切れをまたぐ途中の照合単位は連結せず、過去の確認はLASTとして残す。

## 最終検証と残件

`scripts/test_release_source.sh` を1回実行し、PASSを確認した。
ソース契約、native 23件、Rust/xtask、ignored parity 20件とpairing_candidates 6件、clippyを含む。
その後の入力継続性と操作境界の修正は、影響するnative 4件を再実行し、全件PASSを確認した。
最後にReferenceの実表示領域へ試験を合わせ、Editor/UIの2件を再実行してPASSを確認した。
100%のbodyは292×118、300%は872×450であり、ウィンドウ全体の寸法をReference領域の代用にしていない。
100%のC選択では複数Preset/Checkと再取得失敗を同時に用意し、選択欄と状態表示の非重複も確認した。
全体baselineを繰り返してはいない。
ログは `target/b887/full-baseline.log`、`final-boundary-tests.log`、`final-runtime-evidence.log`、`actual-bounds-tests.log` にある。
描画の目視には新しい出力先を使い、既存PNGへの追記による古い画像の読み取りを避けた。

同じcommitでのStudio One/Pro Toolsの再生、CaptureからLIVE、両Blindからの復帰、2 POSTでの30分CPU/RSSと音切れ確認は未実施。
Windowsの同一候補のhost確認と既存DLL unload問題も未完了。
これらの証跡を以前のB番号から流用しない。

今回の作業ではプラグインの配置、署名、公証、公開を行っていない。
LSアップ用、HP用macOS/Windows成果物はskipとする。
