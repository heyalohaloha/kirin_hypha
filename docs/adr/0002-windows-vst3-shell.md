# ADR 0002: Windows 第一弾は JUCE VST3 を出荷する

- 状態: 採択（2026-06-27、暫定・可逆）
- 関連: [0001-separation-first-architecture](0001-separation-first-architecture.md) / `docs/hypha_stability_plan_2026-06-26.md` Step 6 / メモリ `windows_readiness`

## Context（背景）

macOS の出荷セットは **egui VST3 + JUCE AU**（`README.md`、`xtask/src/install.rs`）。
Windows には VST3 が要る。選択肢は2つ:

1. **egui VST3**（macOS と同じ shell を Windows でも）
2. **JUCE VST3**（macOS が AU として出している JUCE shell を Windows では VST3 で）

2026-06-27 の Windows 調査（`windows_readiness`）で判明した事実:

- 計測エンジン (`kirin_measure` FFI) と `%TEMP%\kirin` 経由の PRE/POST 通信は **shell に依らず共通**。
  どちらを選んでもペアリング・計測ロジックは同一。
- **JUCE VST3 は既に Windows MSVC でコンパイル・リンク成功**（B-181 緑ビルド）。JUCE shell 自体は
  macOS が AU として出荷・auval 検証済みの実績ある経路。
- **egui/baseview の MSVC ビルドは完全に未検証**。vendor の baseview/egui-baseview fork は
  macOS first-click 対応で入れたもので、Windows backend の生死が不明＝**最大のリスク**。
- class-id / state フォーマットが両 shell で異なる:
  egui = `KirinHyphaPREv01` / `KirinHyphaPOSTv1`、JUCE = manufacturer `Kirn` / code `Khpr`・`Khpo`。

## Decision（決定）

**Windows の第一弾は JUCE VST3（PRE / POST）を出荷する。** egui-on-Windows は将来の統一課題として保留。

## Consequences（結果）

- 利点: 実ロードまでの最短・最小リスク経路。既に Windows でビルドが通り、JUCE shell は
  macOS AU として実績がある。egui-on-MSVC という最大の未知を回避できる。
- 受容するトレードオフ: **Windows の VST3 class-id は macOS の egui VST3 と異なる**。
  そのため mac(egui) ↔ win(JUCE) でプロジェクトの plugin identity / 保存 state は互換でない。
  計測のみで音声を加工しないプラグインであり、保存 state は最小（pair 名等）なので、
  第一弾はこの非互換を**文書化のうえ受容**する。
- egui との統一（cross-platform で同一 class-id）が必要になった時点で再検討（本 ADR を改訂）。

## Bring-up 順序（ゲート）

`windows_readiness` の最小経路に沿う。**上から順に、前段が緑になってから次へ**:

1. **B3 — HEAD の Windows CI を実際に走らせる**（`[ci full]` コミット or workflow_dispatch）。
   現行 CI は一度も完走しておらず、Verify/Upload を含む windows-vst3-preflight が HEAD で
   緑になることを最初に確認する。**他すべての前提**。※GitHub Actions 分（過去に上限到達）が要る。
2. **B2 — CI に pluginval ステップを追加**（公式ドキュメントで CLI を確認してから書く）。
   ビルド成果物を host なしでロード検証し「リンクする」→「インスタンス化できる」へ進める。
3. **B1 — Windows インストール経路を実装**（`%CommonProgramFiles%\VST3` 等へ配置）。
   `install.rs` に `cfg(windows)` 分岐を追加。実 Windows でのビルド確認後に行う（未検証コードの
   speculative 追加を避ける）。
4. **実 Windows ホストでロード + ペアリング検証**（Studio One/Cubase 等）。S-1〜S-5 で計測。
5. コード署名（Authenticode）は後段。

### Status 2026-07-01

- B3/B2 は B-203 CI で green: Windows PRE/POST VST3 build, artifact upload, pluginval pass。
- B1 は未実装: user/global VST3 install path は `xtask windows-vst3-layout` で定義済みだが、Windows installer / LS package は未作成。
- 次ゲートは実 Windows ホスト検証。外部テスターには `docs/windows_external_validation.md` を渡し、DAW load / Watch files / Pairing / Keep / offline bounce / audio transparency を順に確認する。
- LS upload は、実機検証と Windows 用 LS packaging/state/runbook が揃うまで blocker。

## Acceptance（第一弾 Windows VST3 の合格条件）

- CI（windows-latest）で PRE/POST VST3 が緑ビルド＋ pluginval pass。
- 実 Windows DAW でロードでき、PRE が `%TEMP%\kirin\{uuid}\{iid}\pre.json` を書き、
  POST が発見して Keep が成立する。
- 音声はビット同一（R-12 製造境界）。
