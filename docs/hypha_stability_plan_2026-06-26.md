# Hypha Stability Plan — 2026-06-26

## 目的

Hypha の頻発不具合を、個別パッチではなく構造的に減らす。
Windows 対応は、macOS/AU/VST3 の境界不具合を整理し、再発防止ゲートを作ってから着手する。
長期安定性と計測精度を優先し、内部構造は大胆に分離してよい。分離方針の正本は
`docs/adr/0001-separation-first-architecture.md` とする。

## 現状認識

- `main` と `origin/main` は一致しており、履歴の枝分かれはない。
- 作業ツリーには既存の `juce_shell/JUCE` サブモジュール内変更だけが残っている。
- 通常テストは 537 件あるが、Record / pairing / plugin_data の重要な FFI parity テストは `#[ignore]` 側にある。
- `kirin_hypha_ffi --test parity -- --ignored --test-threads=1` の対象は parity.rs の `#[ignore]` テスト 20 件（2026-06-28 `--ignored --list` 実測）。同種の FFI ignored テストが pairing_candidates.rs にも 5 件（計 25 件）あるが、`--test parity` 指定では走らない点に注意。
- 巨大ファイルが複数あるため、責務境界が読みづらく、片側修正・狭いテストが起きやすい。

## 原因分類

### A. 以前から存在した構造制約が表面化したもの

- PRE / POST が別バイナリ、別 cdylib、別 static state を持つため、`project_uuid` や `daw_session_id` が自然には一致しない。
- DAW は `prepareToPlay` / `setStateInformation` / `processBlock` の順序を保証しない。
- Studio One / Logic / AU sandbox / offline bounce で callback とファイル更新タイミングが違う。
- `$TMPDIR/kirin` と `plugin_data` の古い残骸が、新しいインスタンスの発見・pairing に影響する。

### B. 実装漏れ・境界契約不足

- VST3 egui 経路と JUCE AU/FFI 経路で、同じ概念が別実装になっている箇所がある。
- GUI 表示、Keep、All Keep、Delta、Record の選定ロジックが同じ不変条件を共有していない時期があった。
- 通常ゲートだけでは `kirin_hypha_ffi` の重い parity 検証が走らず、AU 実運用の失敗を先に捕まえにくい。
- `StoragePaths::default_macos()` が広範囲に散っていたため、Windows 対応時に同種の漏れが出やすかった。

### C. 修正不足・片側修正

- ある問題を display 側だけ、Keep 側だけ、egui 側だけ、または JUCE 側だけで直すと、別の入口で再発する。
- 直近の PRE 候補問題も、候補メニューと Keep/Delta 確定側の意味論が分かれていたことが背景にある。
- UI の表示可否、pair name 空文字、named candidate、latched PRE の扱いが、実装箇所ごとに散っている。

### D. 配置・リリース手順由来

- user-level / system-level の古い bundle が残ると、修正済みコードを検証しているつもりで古いバイナリを読める。
- JUCE submodule patch は出荷品質に直結するが、親 repo の通常 diff だけでは中身の差分が見えにくい。
- codesign / notarize / installer / LS upload は macOS 専用で、Windows 対応時は別ゲートに分離する必要がある。

## 潜在不具合として先回りする領域

1. Pairing / discovery
   - 同名 PRE が別セッションにある場合。
   - 既存 pair がある状態で2個目の pair を追加する場合。
   - PRE が Inactive / Bypassed / stale / renamed / deleted になる場合。
   - pair 名が空、UTF-8、日本語、16文字超、制御文字を含む場合。

2. Identity / restore
   - `setStateInformation` が enable 後に来る場合。
   - PRE 先、POST 先、GUI open 前、transport stopped の各順序。
   - role-scoped shared id の refcount clear と IO thread lifetime の競合。

3. Thread / RT safety
   - Audio Thread 上で lock / allocation / filesystem / blocking API が再混入すること。
   - offline bounce の巨大 block、ring overflow、oversized drop の扱い。
   - watchdog restart 後の producer 差し替え、Drop 順序、UAF。

4. File IO / stale cleanup
   - atomic write の tmp 残骸。
   - stale pending / stale active / orphan reservation。
   - permission error、HOME なし、plugin_data missing。

5. Shell parity
   - nih-plug VST3 と JUCE AU で同じ状態を表示すること。
   - `PostControls::update` と egui の button visibility が一致すること。
   - FFI C ABI の enum / buffer / string truncate / null pointer safety。

6. Windows readiness
   - macOS 固定 path、AU/codesign/notarize、CMake OSX 設定、`.component` 前提。
   - Rust staticlib `.a` から MSVC `.lib` への切替。
   - VST3 install path、APPDATA / LOCALAPPDATA、atomic rename、Unicode path。

## 構造対策計画

### Step 1: 不変条件をコードの近くに固定する

- `docs/` に pairing / identity / record / shell parity の不変条件表を置く。
- PRE/POST 状態遷移を「表示」「Keep」「Delta」「Record」「All Keep」で同じ表にまとめる。
- 不変条件ごとに、対応するテスト名を紐づける。

完了条件:
- 「この不具合はどの不変条件違反か」を即答できる。
- 追加修正時に、どのテストを増やすべきか迷わない。

進捗:
- `docs/hypha_invariants.md` を作成し、PRE 状態 × {表示/Keep/Delta/Record/All Keep} 統一表と、
  pairing / 表示 / record / all-keep / identity / license / shell-parity の不変条件を実在テスト名へ
  紐づけた（INV-P/D/R/A/I/L/S）。唯一の未紐づけは INV-D8（要追加）として明示。

### Step 2: Pairing / discovery を最優先で分離する

現在の中心ファイル:
- `crates/kirin_measure/src/record_signal.rs`
- `crates/kirin_measure/src/io_thread_post.rs`
- `crates/hypha_post/src/editor.rs`
- `crates/kirin_hypha_ffi/src/lib.rs`

実施内容:
- PRE candidate discovery、candidate menu、target selection、latch、record signal を小モジュールに分離する。
- `display == commit == keep target` の不変条件を single source に寄せる。
- 今回の「Drum 既存 pair 後に Mix が候補から消える」系を scenario fixture 化する。

追加するテスト:
- 既存 pair あり + 新規 PRE 追加。
- 同名 PRE が別セッションに2件。
- stopped Inactive PRE を Keep でき、再生後 Delta が出る。
- Bypassed PRE は候補・Keep から除外。
- latch 後に同名2件目が現れても既存 pair を維持。

進捗:
- `crates/kirin_measure/src/pairing_scope.rs` を作成し、PRE scope 推定、候補列挙、厳格 target selection、latch/read/Arm 解決を集約。
- `crates/kirin_measure/src/post_candidates.rs` を作成し、POST candidate scan / peer enumerate / pair claim self-check を IO thread 実装から分離。
- `record_signal.rs` は互換 re-export を外し、record_signal.json の読み書き責務へ縮小。
- ラッチ済み Delta は選定済み `pre.json` を直接読む境界に寄せ、再スキャンで別 PRE を拾う余地を削減。

### Step 3: Shell parity をゲート化する

実施内容:
- egui と JUCE が読む候補数、Keep可否、ボタン表示条件を Rust/FFI 側の共通 API に寄せる。
- `PostControls::update(recording, license, pairNonEmpty)` のような C++ 側判断を、Rust側状態と比較できるテストにする。
- FFI parity ignored suite を、変更ファイルに応じた必須ゲートとして明文化する。

必須ゲート:
- `cargo test --workspace`
- `cargo clippy --workspace --all-targets --exclude baseview --exclude egui-baseview -- -D warnings`
- `cargo test -p kirin_hypha_ffi --test parity -- --ignored --test-threads=1`
- JUCE shell 変更時: `scripts/verify_juce_patch_state.sh` と JUCE build smoke。

### Step 4: IO / thread / restore を分解する

対象:
- `io_thread_post.rs` 4256行
- `io_thread_pre.rs` 2437行
- `record_writer.rs` 1963行
- `measure_thread.rs` 978行
- `kirin_hypha_ffi/src/lib.rs` 2339行

実施内容:
- IO loop、sub-tick poller、record writer、broadcast receiver、cleanup を分離する。
- Audio Thread から呼べる FFI 関数を `rt_safe` group として分け、禁止操作をテストで監視する。
- restore / enable / state chunk の順序テストを増やす。

### Step 5: 診断を標準化する

実施内容:
- `$TMPDIR/kirin` と `plugin_data` の状態を1コマンドで snapshot する診断スクリプトを整備する。
- PRE/POST project、instance、name、signal_state、mtime、pair target、record_signal を表で出す。
- UI には不要なエラーを出さず、利用者操作に紐づく失敗だけ toast/status に出す。

### Step 6: Windows 着手前の platform 分離

実施内容:
- `StoragePaths::default_macos()` を `PlatformPaths` に置き換え、macOS / Windows を分岐する。
- temp root、plugin_data root、install path、release package path を platform adapter に集約する。
- CMake は macOS AU と Windows VST3 を別 target / option に分ける。
- Windows 初期対応は VST3 のみに限定する。AU/codesign/notarize/LS pkg は macOS専用ゲートに閉じる。

Windows 着手条件:
- Pairing / restore / FFI ignored parity が green。
- JUCE patch state が説明可能。
- platform path の単体テストが macOS/Windows 両方の文字列 fixture で通る。
- macOS出荷ゲートとWindows開発ゲートが混ざっていない。

進捗:
- `PlatformPaths` / `PlatformKind` を storage 境界へ導入し、macOS fixture は既存配置、Windows fixture は APPDATA(identity) / LOCALAPPDATA(plugin_data) 分離を固定。
- `StoragePaths::default_macos()` は互換 wrapper として残し、呼び出し側移行前でも既存 macOS 挙動を維持。
- production 呼び出し側を `StoragePaths::default_platform()` に移行し、`default_macos()` 直接参照は storage wrapper 内に限定。
- Watch 通信 root は `PlatformPaths::current_kirin_tmp_root()` に集約し、production source で直接 `$TMPDIR/kirin` を組み立てないガードを追加。
- `cargo run -p xtask -- diagnose-watch` を追加し、PRE/POST watch JSON、record_signal / all_keep / all_stop、plugin_data record 数を1コマンドで snapshot できるようにした。既定は直近履歴を最新順・行数上限付きで表示し、必要時だけ `--all-history` / `--max-rows 0` で全量確認する。
- `diagnose-watch` に Findings を追加し、stale / bypassed PRE、同名PRE曖昧、POST target 不成立、pending signal 滞留を原因コード付きで表示できるようにした。
- `diagnose-watch --format json` を追加し、summary / Findings / watch rows / signal rows / plugin_data rows を共有・比較しやすい機械可読形式で出力できるようにした。
- JUCE/AU POST が呼ぶ C ABI 候補列挙で、既存の名前付き POST claim があっても別名の2つ目 PRE が候補から消えないことを FFI integration test で固定した。`Drum` / `Mix` は fixture 名であり、名前そのものの特別扱いはしない。
- `diagnose-watch` で POST の pair が未選択でも新鮮な PRE 候補が見えている場合を `POST_PAIR_NOT_SELECTED` として分類し、PRE不在と shell 選択/表示側の問題を切り分けられるようにした。
- JUCE/AU shell parity gate を `xtask` に追加し、候補メニューが現在の pair 名に依存せず PRE 候補を列挙すること、Keep 表示が選択済み pair にだけ依存すること、候補選択が processor と入力欄へ即反映されることを静的テストで固定した。
- FFI ignored integration に restore-order fixture を追加し、PRE name / POST pair target が `enable_*_writes` の前後どちらで復元されても watch JSON に反映されることを固定した。
- `xtask` に RT-safety gate を追加し、JUCE `processBlock` が呼べる C ABI を allowlist 化し、FFI `push_samples` に filesystem / blocking lock / allocation 系が再混入しないことを静的テストで監視するようにした。
- PRE/POST watch JSON 書込を共有 `atomic_file` 経由へ寄せ、固定名 `pre.json.tmp` / `post.json.tmp` の競合で rename source が消える経路を潰した。
- `record_signal` / All Keep / All Stop / identity の atomic write も共有 `atomic_file` 経由へ寄せ、通信系JSONのtmp命名とcleanup規則を揃えた。
- JUCE CMake を platform 分岐し、macOS は AU+VST3 / Windows は VST3-only + MSVC `.lib` 既定に分離し、`xtask windows-preflight` で崩れを検出できるようにした。
- B-174 review で、Windows VST3 経路に残っていた clang/gcc 専用 `-include` を MSVC `/FI` 分岐へ修正し、preflight で再混入を検出するようにした。
- CI に Windows VST3 preflight job を追加し、`windows-latest` 上で MSVC staticlib、`xtask windows-preflight`、JUCE PRE/POST VST3 target build を macOS AU release gate から分離して検証する入口を作った。
- Windows CI 初回で露出した CRLF checkout 差分を `xtask windows-preflight` 側で吸収し、preflight 自体が platform 改行差で誤検知しない fixture を追加した。
- macOS CI で露出した非同期 Phase D parity の収束/overflow 条件を固定し、FFI parity test が並列実行中でも同一サンプル列を落とさず比較するようにした。
- Windows VST3 link で露出した Rust staticlib native 依存を Windows-only CMake 変数へ分離し、`ntdll` / `userenv` 等を JUCE target へ明示 link する preflight を追加した。
- B-178 CI で残った Phase D parity の publish 窓差を direct last-frame 固定ではなく direct tail-frame 照合へ変更し、許容値を緩めずに非同期 FFI 経路の実仕様へ合わせた。
- B-179 CI では tail 8 frame でも runner 負荷下の途中 publish を拾ったため、Phase D parity に ring-drain barrier を入れ、全サンプル消費後の publish を比較するようにした。なおこの ring-drain barrier は parity テストハーネス側の判定ゲート（FFI の `#[doc(hidden)]` テスト専用フック `__ring_drained_for_test`）であり、production 計測エンジン `crates/kirin_measure/src/phase_d/` 側の経路ではない。
- B-180 CI で ring drain 前に読んだ古い `MeasureResult` を保持できる穴が残っていたため、Phase D parity は drain 後に poll できた完全結果だけを採用するようにした。
- Actions usage 上限到達を受け、full CI は `workflow_dispatch` / PR / `[ci full]` 明示コミットだけで走るようにし、通常 push はローカル厳格検証後の直列履歴積み上げに切り替えた。
- `xtask ci-usage-guard` を追加し、full CI gate が外れて通常 push で macOS/AU/Windows job を再び消費し始めないことを静的テストで固定した。
- `xtask windows-preflight` を GitHub Actions の Windows job まで拡張し、Windows VST3 preflight に AU / codesign / notarize / LS packaging など macOS release 手順が混入しないことを固定した。
- `xtask windows-readiness` を追加し、Pairing / restore / FFI ignored parity / Shell parity / RT safety / PlatformPaths / JUCE patch state / CI usage / Windows artifact layout / Windows VST3 preflight の 10 ゲートを Windows 着手前チェックとして一括監査できるようにした。
- `xtask windows-vst3-layout` を追加し、Windows PRE/POST VST3 の build output と Steinberg 定義の user-dev / global install root を明示し、macOS AU / notarize / LS packaging と混線しないことを固定した。
- B-187 review で CI usage / Windows preflight の静的ガードを厳格化し、コメント中の文字列・step-level `if`・plain push 逃げ条件では gate を通過できないようにした。
- Windows preflight job に PRE/POST `.vst3` 実 artifact の存在確認と upload-artifact を追加し、runner 復活後に生成物を取得して inspect できる状態にした。
- FFI 正本ヘッダ / README / Cargo metadata の staticlib 説明を Windows/MSVC `.lib` と macOS/Linux `.a` の両方に更新し、`xtask windows-preflight` で退行を検出するようにした。
- `xtask windows-vst3-layout` の PRE/POST artifact path を CI preflight の正本として再利用し、YAML 側の検証・upload path が layout 監査から分岐しないようにした。
- Windows VST3 artifact 検証を bundle directory だけでなく `Contents/x86_64-win/*.vst3` の実バイナリ存在・非空チェックまで拡張した。
- B-202 review で HEAD の Windows VST3 job が build / artifact upload / pluginval まで通過したことを確認し、全体 CI を赤くしていた macOS Phase D parity test は drain 後に direct tail と一致する publish を待つ形へ直した。
- B-203 review で B-202 の macOS CI 再赤を追跡し、Phase D parity の direct 参照を一括 stream ではなく 0.1s producer block 境界の GUI publish 候補へ合わせた。これにより STFT hold 値と Measure Thread の publish 観測点を一致させ、許容値を緩めずに CI 上の非同期差を吸収した。

## 優先順位

1. Pairing / discovery の scenario test と小モジュール分離。
2. Shell parity gate の整備。
3. IO thread の責務分割。
4. 診断 snapshot コマンド。
5. PlatformPaths 導入。
6. Windows VST3 build preflight。

## 当面の完了定義

- 新しい pairing 不具合は、実機報告前に scenario test で再現できる。
- バグ修正は egui / FFI / JUCE のどの入口に効くか明示されている。
- `cargo test --workspace` だけで済む変更と、ignored parity / JUCE build まで必要な変更が分類されている。
- Windows 対応に入る前に、macOS専用前提が platform adapter の外へ漏れていない。
