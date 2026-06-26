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
- `kirin_hypha_ffi --test parity -- --ignored --test-threads=1` は 18 件 pass。
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
- `StoragePaths::default_macos()` が広範囲に散っていて、Windows 対応時に同種の漏れが出やすい。

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
