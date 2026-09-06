# Reference案内と実音源検証

日付: 2026-09-06。
対象: B-718の追加実装。
状態: Reference案内は製品コードへ接続済み。
新しいローカルBlind、2MIX ATTACK、SPACE自動解析は開発用部品またはオフライン試作であり、製品機能としては未接続。
承認範囲は[実装承認記録](hypha_implementation_approval_20260906.md)を参照する。

## Referenceの入口

POSTのREFはKirin OSの権限を確認できなくても開ける。
案内画面は「音源をKirin OSに登録して比較する機能」という説明と、公式製品ページへの入口を持つ。
英語と日本語を選択してからブラウザーを開き、起動に失敗した場合は該当URLのコピーを提供する。
公式URLは[英語版](https://kirinmastering.com/os)と[日本語版](https://kirinmastering.com/ja/os)に固定し、音源、Work、licenseの情報を付けない。
画面を開くだけでは通信、購入、license再確認、接続、試聴を開始しない。

所有者向けの接続案内と明示的なローカルlicense再確認を用意した。
権限を確認できないことを未購入と断定せず、認識済みの所有者には購入案内を出さない。
後から権限を認識した際は、古い確認失敗表示を消す。
登録ReferenceのB出力と既存Blindの権限確認はそのまま維持した。
試聴中、開示後、無効化後の復帰操作を案内画面で覆わない。
小型表示の巡回先でも案内画面へ入り、次の操作で別domainへ移れる。

新設画面の本文は最小12 px、ボタンは28 px高で13 pxの文字を使う。
実行可能な案内ボタンは既存の通常文字色を使い、無効操作のような薄い表示にしない。
300〜900 pxの41サイズで折返し、横圧縮なし、領域内配置、重なり、鍵盤操作を検証する。
これは既存画面全体の読みやすさや実機DPIの合格を意味しない。

## 同一区間capture部品

既存Referenceの取得経路には先頭無音を切り詰める処理があり、同じ再生区間のPREとPOSTを作る経路としてはそのまま転用できない。
`ExactRangeCapture`を独立した新規部品として追加した。
非RTで容量を確保し、単一producerが指定されたnative sampleの半開区間をコピーする。
実際のゼロPCMも保存し、trim、resample、downmix、欠落のゼロ埋め、独立loop、PDCの推測は行わない。
全区間が揃うまでPCMを公開せず、欠落、重複、generation、SR、channel、offline、非有限値の異常で無効にする。
完了後のPCMは不変であり、cancelは解放やproducer退場の確認を意味しない。

この部品はAudio Threadへ未接続である。
同一pairの確認、共通取得barrier、hostの時間写像、RT終了receipt、参加scope、予約の退場順序は呼出側の責務として残る。
固定offsetの試験は部品の区間コピーを検証するもので、DAWのPDCを証明しない。
Trackの下流へ試聴音が届く問題や正本Recordの保護を、コピー部品だけで解決したとはしない。

## 素材の所在と分割

指定された範囲のメタデータから29,695音声ファイル、890,253,856,686 bytesを確認した。
ALOHA全体の管理用ディレクトリにはアクセスできないものがあり、権限変更はせず音楽用ディレクトリに検索を限定した。
この件数は全ボリュームの完全な棚卸しではなく、重複や別版を除いた楽曲数でもない。

| 所在 | 音声ファイル数 | bytes |
| --- | ---: | ---: |
| Dev | 432 | 1,441,323,148 |
| ALOHA / Kirin | 329 | 8,480,886,111 |
| ALOHA / Studio One | 28,629 | 861,326,818,817 |
| Music / Qobuz | 305 | 19,004,828,610 |

Qobuzのartist／album構造を確認できた277グループから、別artistの64本を提案用に分割した。
構造が異なる1ファイルは推測で分類せず除外した。
ATTACKは開発20本と評価予約20本、SPACEは別の開発12本と評価予約12本である。
同artistの別ファイルは別partitionへ流用しない。
今回decodeしたのは開発32本の30〜60秒だけで、評価予約32本はprobe runnerで開いていない。

過去の使用履歴、同じ曲の別artist表記、別版の関係は未確認である。
したがって「未使用holdoutが確定した」とはしない。
二人の独立した人の注釈も未取得であり、生成したJSONは未記入の依頼用様式にすぎない。
注釈者に渡す場合はWAVと未記入様式だけを分け、検出位置や解析結果を見せない。
外部送信はしていない。

## 自動解析の試作と結果

`mix_space_probe`は製品へ登録しないオフライン実行形式である。
SuperFluxの固定尺度計算を利用するが、既存DRUMのprofileや製品の入口は変更していない。
検出閾値、履歴長、refractory、floor、再上昇、R²、初期peak探索は明示的な開発用入力とし、採用定義として凍結しない。
設定とODF定義のhashを結果へ記録し、`product_qualified`と`human_annotation_evaluated`はfalseにする。

SPACEは初期80 ms内のenergy peakから、次の候補、floor、再上昇、入力終端、6秒上限のいずれかで区間を打ち切る。
後方から最もR²が良い区間を探したり、途切れた区間を接合したりしない。
EARLYは元の候補起点の80／250 ms固定窓のままで、fit起点へ追随させない。
計算器は手動区間probeと共通化し、原音のSRとchannelを維持する。

| 開発用試験 | ATTACK | SPACE |
| --- | ---: | ---: |
| 抜粋数／各30秒 | 20 | 12 |
| 検出候補数 | 3,201 | 1,775 |
| 1抜粋あたりの候補数 | 109〜217 | 50〜202 |
| EARLY算出数 | 対象外 | 1,764 |
| 減衰時間の採用数 | 対象外 | 0 |
| Debug計算時間／30秒入力 | 4.60〜23.27秒 | 5.10〜24.47秒 |

32本ともstereoで、全体のSRは44.1、48、88.2、96、192 kHzだった。
原本はdecodeの前後でSHA-256、サイズ、更新時刻が一致した。
PCMのframe数は30×SR、bytesはframes×channels×4に一致し、正規化、downmix、resampleはしていない。
mono、176.4 kHz、短いTrack、無音区間の実音源検証はこの集合には含まない。
計算時間はDebugのオフライン値であり、RTのCPU合格値には使わない。
decode方法はFFmpeg公式の[入出力オプション](https://ffmpeg.org/ffmpeg.html)、native metadataの読取りは[ffprobe](https://ffmpeg.org/ffprobe.html)に従う。

SPACEの棄却は、回帰点10点未満が1,512件、観測低下20 dB未満が254件、R²条件が9件だった。
打切り理由は再上昇1,358件、次候補415件、入力終端2件だった。
250 ms窓に次候補が入るものは1,250件あり、EARLYが算出できたことを残響の判別成功とは扱えない。
現在の候補と区間選択を組み合わせた方式は、この開発集合では減衰時間を提示できなかった。
これは「音源に減衰がない」という証明でも、SPACE自体を断念する根拠でもない。

次は人が減衰として読む区間と候補を照合し、再上昇や後続音を含む密な2MIXで何を観測対象とするかを検証する。
SAの計算契約を保ったまま、検出性能、区間の採否、観測可能な割合を別々に評価する。
結果を出すためだけに採用閾値を緩めたり、DRUMを2MIXの完成品として転用したりしない。

## 再実行とローカル証拠

音源のpath、title、hash、抜粋WAVと個別結果はprivateなローカル出力に置き、Gitへ追加しない。
出力親は`/tmp/hypha-b718-research.dN2n3R`であり、一時領域のため永続保管は保証しない。
開発結果は同領域の`attack-development-S6YSVO`と`space-development-VYuT0Z`にある。
パラメータは同領域の`diagnostic-parameters.json`に保存した。
同じ値の再実行用例は`scripts/research/diagnostic-parameters.example.json`に置いたが、製品の採用値ではない。

再実行時はまず`node --test scripts/research/*.test.mjs`を通す。
`audio_inventory.mjs`で所在台帳を作り、`plan_audio_corpus.mjs`で分割案を作る。
既存ファイルを上書きせず、出力先はprivateな新規ファイルまたは一時ディレクトリにする。
`probe_audio_corpus.mjs`は開発partitionだけを受理し、別partitionとのpathまたはartistの重複を拒否する。
これらの検査は過去の閲覧履歴や楽曲同一性の調査を代替しない。

Rustの計算器試験は`cargo test -p kirin_measure --example space_decay_probe --example mix_space_probe`で実行する。
capture部品は`juce_shell/tests/local_blind_capture_test.cpp`をC++17とAddressSanitizer／UndefinedBehaviorSanitizerで単独buildして実行する。
このcapture試験はまだ製品runtimeや既存CI targetに接続していない。

## 今回の検証記録

| 検証 | 結果 | ローカルログ |
| --- | --- | --- |
| 素材台帳と開発用選択 | 6 pass。元音源不在、構造不明、別partitionとの重複拒否を含む | `node --test scripts/research/*.test.mjs` |
| 手動と自動の計算器 | 各6件、計12 pass。既知減衰、無音、固定gain、逆相、端数SR、後続音、再上昇、無効入力、候補の不応期間を含む | `/tmp/hypha-b718-final-research-tests.log` |
| 区間capture | ASan／UBSan pass。14のbit一致条件、左右別PCM、signed zero、全無音、4種offset、8種無効化、5種容量境界、公開と退場を確認 | `/tmp/hypha-b718-final-capture.log` |
| 製品入口とReference権限 | 82 role／size、Referenceの41サイズ、既存Reference componentとOS access契約がpass | `/tmp/hypha-b718-final-entry.log` |
| PRE／POST × AU／VST3 | Debugの4 target pass。配布用署名ではない | `/tmp/hypha-b718-final-native-build.log` |
| Rust library全体 | `cargo test --workspace --lib --locked`が1,582 pass／0 fail／9 ignored | `/tmp/hypha-b718-final-workspace-lib.log` |
| OS access source契約 | 2 pass | `/tmp/hypha-b718-final-os-access.log` |
| Clippy | workspace／all-targets pass。本体警告0、依存先の既存警告は除外 | `/tmp/hypha-b718-clippy-clean.log` |
| 整形と行数 | fmt、diff check、source line budgetがpass。新規owned sourceは500行以下、既存33件のratchet維持 | 作業時のコマンド出力 |

全体描画テストは今回2回失敗した。
Focus Trailの100%表示でfocused paintが6.87 msと7.00 msとなり、4.5 ms上限を超えた。
両実行とも他のbuildが進行していたが、それだけが原因とは確定していない。
性能上限は変更せず、最終sourceの全体描画が合格したとはしない。
ログは`/tmp/hypha-b718-ui-render.log`と`/tmp/hypha-b718-ui-render-isolated.log`であり、後者のファイル名は負荷隔離が成立した証拠ではない。
原因の切分けと最終全体描画の合格を未完了条件に残す。

library試験は完走したが、integration、doctest、ignoredを含む`cargo test --workspace`全体の完走ではない。
FFIの製品sourceは変更しておらず、ignored parity／pairingの25件は今回再実行していない。
前回の25 passを今回の全体合格として数えない。
実DAW、DPI移動、聴取、browser起動とコピーの実操作は未検証である。

## 製品接続前の未完了条件

- Blind: 同一passのPRE／POST取得、PDCの証拠、短いTrackの固定Gain Match、同一DAW参加scope、既存2枠と単一Blind所有、Keep／All Keepとの相互排他、下流の正本保護、RT receipt後の解放、減衰保持と明示復帰。
- ATTACK: 同一曲と別版を含む台帳確認、二人の独立した人の注釈、開発用での定義凍結、隔離した評価集合のprecision／recallと表示時刻の検証。
- SPACE: 独立した区間注釈、候補検出と区間選択の見直し、採用条件と観測可能な割合の評価、定義凍結後の別集合評価。
- 共通: 描画性能の切分け、全workspace、負荷、エラーパス、再起動、旧版混在、実ホスト、macOS AU／VST3とWindows VST3の必須試験。

Windowsは操作していない。
Notion書込み、他担当への送信、配置、公開、pushは行っていない。
LSアップ用はskip、HP配布物はmacOS skip／Windows skipである。
3チャネルの公開readyや、実装全体の独立レビュー指摘0件とは報告しない。
