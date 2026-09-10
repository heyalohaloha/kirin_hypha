# Reference OS preparation handoff — B-798 / W-3022

Kirin OS worktree: `/Users/nishiodaisuke/.codex/worktrees/reference-check-version-blind-boundary`

## 今回の到達点

- OSが同Work／Recordingの検証済み過去VersionからFactoryの初回Work snapshotを生成する。Global templateと通常Presetは変更しない。
- OS専用workerが詳細測定・alignment特徴を生成し、設定済みProfileとともにproduction publisherへ接続する。再起動後もsource revision一致のcacheを再利用する。
- Hypha本番実装は変更せず、実OS出力を直接読むtest-only入口を追加。5 CheckのManifest、source、詳細測定、alignment、Profileが受理された。PCM binding不一致は拒否された。
- 997 Hz／−14 dBFS／48 kHz stereo 10秒: I −14.00848 LUFS、TP −13.96591 dBTP、100 time bins、64 bands、詳細測定9,618 bytes。

## 未完了

全Preset／全Candidate準備を待つ構造、Hypha内SRCのOS移管、同曲Version追加後の候補更新、PSR／chroma、実曲corpusの精度検証、各サイズでの実データ目視、macOS／WindowsのDAW実動は残る。

OSの特徴artifact公開と、Hypha既存`blind.prepare`経路の自動照合は別物。特徴が読めたことを音声alignment完了と呼ばない。

全体suite、release build、配布物作成、インストールは実行していない。Reference完成／公開readyではない。

正本: OS `docs/reference_source_preparation_20260910.md` と `docs/reference_release_readiness_audit_20260910.md`。
