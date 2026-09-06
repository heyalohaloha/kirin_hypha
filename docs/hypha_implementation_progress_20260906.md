# Hypha A0 実装と G1／Blind 実証の記録

日付: 2026-09-06。
承認: [推奨方針と 2 枠の使用条件](hypha_implementation_approval_20260906.md)。
状態: 開発コードと研究用試作。公開候補ではない。

後続作業のReference案内、同一区間capture部品、実音源32本の開発検証は[追加実装と実音源検証](hypha_reference_and_audio_research_20260906.md)に記録する。
以下の試験結果はB-717までの記録であり、後続変更の全体合格を意味しない。

## 今回実装したもの

現行 ATTACK の表示を DRUM に変え、TRACK/STEM の入口に限定した。
2MIX では直接タブと小型画面の巡回先から外し、画面操作と DAW state 復元の双方で解析要求を終了する。
文脈判定と開始要求は既存の非 RT ロックで直列化する。
検出器の係数、C ABI、通常の音声 callback は変更していない。
DRUM という名前は自動の楽器判別を意味せず、2MIX 専用 ATTACK の完成を意味しない。

HYPHA PRE／POST のタイトルを共通情報メニューの入口にした。
小さな footer 記号は増やさず、元のタイトル領域を鍵盤操作できるボタンとして使う。
ロード中の version、role、format、platform、build 時点の source ID と変更の有無を表示する。
同じ SemVer の開発版を「公式最新版」と表示せず、公式配布物との同一性は未確認と明記する。
Git 不在時は unknown／unverified に戻し、誤った clean 表示を作らない。

英日 HP、公開リリース一覧、公式 URL のコピーは利用者の明示操作だけで実行する。
現行 GUI に言語設定はないため、存在しない設定を仮定せず英語と日本語の入口を併記した。
通信 client、自動確認、自動インストールは追加していない。
固定 HTTPS の URL だけを扱い、pair、音声、Work、license、instance ID は URL に含めない。
既存 Reference Blind の実行中は、この POST の情報表示と外部ページの起動を止め、操作確定時にも状態を再確認する。
この局所判定を、未実装の PRE/POST Blind の participant scope 全体の遮蔽と扱わない。
ブラウザーの起動に失敗した場合は、失敗表示と該当 URL のコピー操作を同じメニューに出す。
PRE からも共通の hover help 設定を変更できる。

## 2 枠の扱いと、まだ接続していないもの

既存の 2 枠は大きいウィンドウの数ではなく、`AnalysisLease` の所有者数である。
新しい Blind の条件は「対応する大きな表示 ＋ 既存枠の所有 ＋ 単一試聴予約」とする。
POST が 1 枠を所有し、その PRE のために別枠を消費しない。
もう一方の解析枠は維持するが、Blind を同時開始できない。

実際の 2 枠を使った予約試験を `cfg(test)` の範囲に追加した。
3 番目の枠を作らないこと、同時開始の勝者が 1 つであること、拒否・ファイル障害で他方の解析を解放しないことを試験する。
予約の寿命中は基になる解析枠を可変借用するため、試験上は先に解析枠を解放できない。
失敗が起きても競合試験の相手を barrier に置き去りにしない。

これは製品の開始ボタン、PCM 転送、PDC、Record 排他、出力確認、減衰保持からの復帰には接続していない。
新しい Blind は現時点では利用できず、既存 Reference の権限も変えていない。
特に TRACK の試聴は下流の測定へ届くため、単一プロセス内の予約試験だけでは安全性を証明できない。

## SPACE の固定区間試作

`space_decay_probe` は Float32 WAV の明示区間を扱う、プラグインへリンクしない研究用コマンドである。
mono／stereo、8〜768 kHz を受理し、リサンプルやゼロ埋めはしない。
入力は実読込量で 256 MiB、fit 区間は 6 秒に制限する。
区間境界は毎回起点から丸め、端数 hop の累積を避ける。
EARLY は最初の 80 ms と続く 170 ms のエネルギー比であり、窓長で正規化した平均同士の比ではない。
10 ms 区間の平均パワーを回帰し、10 点以上かつ観測した低下が 20 dB 以上の場合だけ 20 dB 相当時間を算出する。
R²、再上昇、floor、区間選択の採否条件はまだ凍結せず、出力にも `product_qualified: false` を付ける。

実物として `test_signals/S-1_1kHz_sine_m6dBFS_10s.wav` の stereo／48 kHz／Float32、10 秒を確認した。
PCM は 3,840,000 bytes、ファイルの SHA-256 は `5e526afe85549fe7daeace8824696f6b1adf7238273cd30e7132f87c8ea77a1d`。
起点 0、fit 0〜1,000 ms、floor −90 dB の結果は EARLY −3.2735893439 dB、100 点、観測低下 0 dB、減衰時間 null だった。
一定音から存在しない減衰時間を作っていない。
既知の −40 dB/s の試験入力では、6 種の sample rate で 0.5 秒に対する誤差が 1 µs 未満だった。
この結果は計算器の検証であり、実楽曲の自動区間選択や 2MIX event 検出の合格証拠ではない。

## 更新先 HP の準備

別リポジトリ `kirin_hp` の `1d2d3cf`／W-2943 で、英日 HP の更新説明とリンク検査を揃えた。
公開済み v1.1.49 の version、公開日、PRE／POST 同梱、変更内容、PKG への補助導線を追加した。
保存→DAW 終了→両方を導入→再起動／rescan→ロードした両方の版確認を案内する。
再購入を求めず、重複した配置場所と安全な公式導入手順に触れる。
既存の macOS ZIP／Windows Setup EXE の版と主導線は維持し、未出荷の SPACE／Blind を宣伝していない。
静的 build と 209 試験は pass。外部公開と push は行っていない。

## 検証台帳

製品入口と研究用試作は `0d549c1`／B-714。
責務の先行抽出は B-710〜B-713 に分けた。
通知試験の隔離は `048a7bd`／B-715、メニュー操作後の Blind 再判定の統一は `85f2c88`／B-716。
ログ名の B 番号は実行開始時の接頭辞であり、最終 commit の名称ではない。

| 要求 | 実装・試作 | 確認範囲 |
| --- | --- | --- |
| A0／DRUM | navigation、processor admission、state restore | 2MIX の 4 タブ、TRACK/STEM の 5 タブ、DRUM からの遷移、復元の source contract |
| UP-01 | 共通情報入口、固定 URL dispatch | PRE／POST × 41 サイズ、鍵盤入口、重なり、Blind 中の外部副作用 0、ブラウザー失敗時のコピー先 |
| UP-02 | build-time source identity | Git ありの modified source と Git 不在の unknown fallback |
| SA-02／SA-03 | 固定窓 EARLY／回帰 | 既知解、無音、floor、欠けた窓、非有限値、stereo 逆相、一定 gain、端数 SR |
| BL-04 の一部 | test-only slot reservation | 2 枠・1 予約、同時開始、明示 retry、ファイル障害。他の BL-04 条件は未完了 |

### 試験中に見つかった競合

計測コアの並行試験で `materialize_preserves_safe_and_news_up_unsafe` が一度失敗した。
結果は 1,410 pass／1 fail／9 ignored で、失敗内容は empty 入力の通知キューが空ではないことだった。
原因を追うと、`fresh_events` が process 全体のキューを drain する一方、別の path 試験が同じキューへ通知を追加できた。
同じコードの別実行では 1,411 pass となったが、再実行で成功したことだけで閉じていない。

通知の内容を検査する 6 試験に、明示的な試験用キューの所有を追加した。
通常ビルドの通知先は従来の global sink のままである。
テスト用の差し替え、thread-local の保持、RAII の復帰は `cfg(test)` の範囲に置いた。
並行 capture が互いの通知を drain しない試験と、panic 後に外側のキューへ戻る試験を追加した。
修正後の計測コアは 1,413 pass／0 fail／9 ignored、26.95 秒だった。
ログ: `/tmp/hypha-b714-measure-core.log`、`/tmp/hypha-b715-measure-core.log`。

### 現時点の自動試験

- SPACE 固定窓: 6 pass。`/tmp/hypha-b713-space-tests.log`。
- Analysis 枠関連: 10 pass。このうち新しい Blind 予約は 4 件。`/tmp/hypha-b713-blind-slots-final.log`。
- 共通情報・DRUM 入口: pass、82 role／size 条件。最終版は `/tmp/hypha-b716-product-entry.log`。
- native 全体描画: pass、28.83 秒。最終版は `/tmp/hypha-b716-native-ui.log`。
- DRUM native UI: pass。`/tmp/hypha-b713-ui-diagnostic.log`。
- PRE／POST × AU／VST3 の Debug build: 4 target pass。最終版は `/tmp/hypha-b716-wrappers.log`。JUCE の自動 ad-hoc 処理を含む開発用 build であり、配布用署名ではない。
- FFI ignored parity: 20 pass、135.03 秒。pairing_candidates: 5 pass、3.01 秒。`/tmp/hypha-b715-parity.log` と `/tmp/hypha-b715-pairing.log`。件数は `: test` で終わる列挙行から実測した。
- Clippy: workspace／all-targets pass。本体の Clippy 警告は 0、vendor の既存 3 警告は除外。`/tmp/hypha-b715-clippy.log`。
- `cargo fmt --all -- --check` と `git diff --check`: pass。
- build identity: modified source と Git 不在 fallback の 2 条件 pass。
- 行数予算: 33 個の既存負債を exact ratchet として維持し、新規 owned source は 500 行以下。予算検査とその self-test は pass。
- 英日 HP: 209 pass。`/tmp/hypha-b712-hp-build.log`。

`cargo test --workspace` は完走していない。
B-714 の試作を含む実行は 19 suite／1,669 pass／0 fail／35 ignored まで進んだが、`io_thread_post_cleanup_wiring_test` の起動前待機が 6 分を超えた。
2026-09-06 14:22 JST に採取した sample は footprint 12 KiB、`_dyld_start + 0` のままで、テスト本体の出力はなかった。
通常起動に加えて `--list` の事前読込も試し、別の executable では 300 秒の起動 timeout を記録した。
約 56 分時点でこの実行と当方の事前読込プロセスを終了し、全 workspace の合格とはしていない。
ログ: `/tmp/hypha-b713-workspace-test.log`、`/tmp/hypha-b716-workspace-startup-block.txt`、`/tmp/hypha-b716-list-only-startup.log`。
上の 1,669 件には通知試験の隔離前の計測コア 1,411 件が含まれるため、修正後の 1,413 件を加算した合計を作らない。
最終 Rust 修正後のコア、FFI ignored 25 件、Clippy は個別実行で pass を確認した。
EBU v05 の全 70 WAV の検証は別途供給される公式音源が必要であり、今回の合格には含めない。

描画試験は初回にコンパイルとの同時負荷で既存 SPECTRUM の 12 ms 上限を超えた。
また、macOS の executable 起動で `_dyld_start + 0` の待機を採取し、テスト開始前の timeout も記録した。
性能上限は変更していない。
入口試験を個別にも起動できる形にした後、入口と全体描画をそれぞれ通した。
この結果は実 DAW の DPI／モニター移動や主観的な読みやすさの検証を代替しない。

## 継続に必要な証拠

- 2MIX ATTACK: 使用権と隔離履歴のある開発用／holdout 素材、各 20 本の 30 秒抜粋、二人の独立した人の注釈。AI のラベルで代用しない。
- SPACE: 別の素材台帳と区間注釈、R²／再上昇／floor 余裕／区間選択条件の実測と凍結。固定区間の式だけで自動解析を有効化しない。
- Blind: 同一区間の取得・PDC・短い TRACK の固定 Gain Match、実際の DAW participant scope、Keep／All Keep との相互排他、下流の正本保護、RT 出力確認後の解放、減衰保持と明示復帰。
- 共通: 旧版との組合せ、macOS AU／VST3 と Windows VST3、DPI・モニター移動、実ホストでの browser／コピー操作、表示と聴取の検証。
- 自動試験: macOS の実行前待機が解消した環境で、最終 source の `cargo test --workspace` を完走させる。今回の個別 pass や一覧取得で代替しない。

Windows は引き続き操作しない。
Notion 書込み、インストール、notarize、公開、push は実施していない。
既存の JUCE ローカル差分と別件の未追跡 handoff は変更・取り込みしていない。
LS と公開パッケージの準備は今回の作業範囲外であり、3 チャネルの公開完了とは報告しない。
未完了を Phase 2 へ移していない。

## 配置と公開の状態

LS アップ用: skip。
HP アップ用配布物: macOS skip／Windows skip。
HP の英日案内コードは準備済みだが、外部公開していない。
Windows 検証機と実 DAW への配置は未実施。
全 workspace の完走、実機試験、新機能の未完了 gate、3 チャネルの配布準備を残しているため、公開 ready や独立レビュー指摘 0 件とは報告しない。
