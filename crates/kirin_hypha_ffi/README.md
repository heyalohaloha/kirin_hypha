# kirin_hypha_ffi

Kirin Hypha JUCE 移植 **Phase 1** — リアルタイム計測パスの C ABI ラッパ。

方式 **B2**: 検証済み Rust ランタイム（`kirin_measure` + Measure Thread）を **無変更** のまま
C ABI で包む。C++/JUCE 側に DSP・計測ロジックを一切移さない（計測器は精度が製品そのもの）。

## スコープ（Phase 1 / 確定）

| API | 状態 |
|-----|------|
| `kirin_hypha_create` / `set_signal_state` / `push_samples` / `poll_result` / `destroy` | 実装（RT 計測パス） |
| `kirin_hypha_poll_session` | **symbol のみ・常に false**（SessionSummary は Record=Phase 3 でのみ成立） |

触れない（Phase 3）: Record / plugin_data / preset / license / PRE-POST ペアリング / IO(pre|post.json) / state chunk。

## C ABI

正本ヘッダ: [`include/kirin_hypha_ffi.h`](include/kirin_hypha_ffi.h)（**手書き** / `src/lib.rs` と常に一致させる）。
`Option<f64>` は **NaN sentinel**。C 側は `isnan()` で「値なし」を判定する。

スレッド契約:
- `push_samples` … **Audio Thread 単独・RT-safe**（内部は rtrb lock-free push + heartbeat++ のみ。alloc/lock/syscall なし）。
- `poll_result` … **UI Thread**（内部 `try_lock` 非ブロッキング）。
- `push_samples` は毎オーディオブロック呼ぶこと（~200ms 呼ばないと Measure Thread が Inactive に落とし計測が止まる）。
- 状態コード: `0=Inactive 1=Active 2=Bypassed`。

## ビルド

```sh
# C 用 staticlib（本番成果物）: target/{debug,release}/libkirin_hypha_ffi.a
cargo build -p kirin_hypha_ffi --release
```

`crate-type = ["staticlib", "rlib"]`。`staticlib` が C 成果物、`rlib` は §5 の Rust 統合テスト
（`tests/parity.rs`）が本クレートの Rust API を使うために必要（C 成果物には無影響）。

ヘッダ生成は `cbindgen` 不在のため **手書きを正本**とする（オフラインで完結）。cbindgen を任意の
照合に使う場合も `cargo test` の前提にはしない。

## テスト

```sh
cargo test -p kirin_hypha_ffi
```

- `parity_phase_d_metrics_ffi_vs_direct` — FFI(create→push_samples→poll_result) と
  `kirin_measure` 直接駆動（PhaseDStream）を同一入力（L==R ステレオ）で比較。
  MoSQITo tolerance（N: `max_relative=1e-3` / Sharpness: `epsilon=0.05 acum`）内で一致を assert。
  対象は **Zwicker N / Sharpness / PSB / n_prime[20] / psb_bark[20]**。
  （tp_offline_reference / lra_plr_session は SessionSummary=Record 経路のため Phase 3。）
- `poll_session_is_false_in_phase1` — Phase 1 で `poll_session` が None/false。
- `rt_safety_no_overflow_at_juce_block_sizes` — 64/128/256/512/1024 frames×2ch を real-time 連投し
  ring overflow ゼロ（2s リングで足りる）を確認。

## 内部構成

`create` は本番と同一の実運用入口 `kirin_measure::spawn_measure_thread`（measure_thread.rs:59）で
Measure Thread を起動する（IO Thread / Watchdog は RT 計測に不要なため Phase 1 では立てない）。

- ring 容量 = `sample_rate × RING_BUFFER_SECONDS(2) × num_channels`。JUCE AU 経路は 1=mono / 2=stereo を渡す。
- `sample_rate ≠ 48000` の 48k 変換は Measure Thread 内 `ResamplerTo48k` が既存どおり担う（新規変換コードなし）。
- `record_sm` は Watch 固定ダミー（never recording）/ `session_summary` は None のまま。

**`kirin_measure` は無変更**（分離原則）。本クレートは新規追加のみ。
