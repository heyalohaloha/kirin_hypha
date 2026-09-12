# Reference publication refresh — B-799 / W-3023–W-3024

## 実装した境界

一覧の更新で試聴を解除していた原因は、`ReferenceRuntimeV2Workspace.cpp`がManifest revisionを試聴許可のkeyに含めていたこと。未選択の候補やCheckが届いただけでも、BをAへ戻し、Blindを無効化していた。

`ReferenceRuntimeV2PlaybackIdentity.h`へ選択中の依存条件を集約した。

- 試聴に依存する条件: Work／Preset／Check／Candidateのidentity、Check modeと通常比較方式、source identityと完全なreceipt、Cue ID／sample rate／start／end／loop。
- 試聴に依存しない内容: Manifest revision、選択外のCheck・候補、Preset／Check／Candidate／Cueの表示名、表示viewとProfile、現在選択していないdefault Cue。
- source receiptが変わる場合は、測定summaryだけの更新であっても既存gainを維持しない。古いgain条件を新しい測定へ無断で適用しない。
- host設定変更、source検証失敗、A binding喪失、利用者の選択変更では既存の停止処理を維持する。更新からBを自動開始しない。

Cueの全条件を通常Bの位置対応とBlindの準備keyでも使う。同じCue IDのまま範囲、sample rate、loopが変わっても、以前の対応や凍結済みCueを再利用しない。音声処理、PRE／POST測定、Record、FFI、音源sampleは変更していない。

既に開始した試聴とBlindの履歴は、開始時に保持したevent contextを使い続ける。一覧更新後の新しい操作には新しいManifestのcontextを使う。

## OS側

OS worktree: `/Users/nishiodaisuke/.codex/worktrees/reference-check-version-blind-boundary`

W-3023でmainのReference publication transactionを抽出。W-3024で準備後のWork設定、Global catalog、全source revisionを確認し、古い準備結果を公開しないようにした。遅いdecode中はWork transactionを保持しない。Historyのみの追加では準備を破棄しない。

検証失敗で既存Manifestを消さない。利用者の明示操作が失敗した場合は既存のtyped acknowledgementでretry／source選択へ戻す。バックグラウンド準備の失敗を新しいUIエラーとして追加していない。

## 対象検証

- OS: publication関連15件、coordinator 4件pass。source不在／準備後差替え、設定競合、History追加、catalog更新、再起動cacheを含む。native実WAV試験は今回は反復せずskip。
- Hypha: `KirinReferenceAuditionRuntimeTests --workspace-only`でReference workspace境界を確認。全runtime suiteではない。
- 通常B: 一覧revision 5→6、Check 1→2、候補1→2でもB選択と−1 dB gainを保持。stereo 256 framesは更新前後でbyte同一。
- Blind: 一覧revision 9→10で試聴継続と出力一致を確認。gain条件変更時の古いB開始拒否、source変更時のBlind無効化、承認済みlower-Aの保持と正常A復帰も確認。
- `--blind-refresh-only`で前後の別操作を含めず同じ更新を独立確認できる。試験中はOS役がA binding leaseを更新する。
- 初回の追加fixtureは空Candidateと同一identity重複を含みparserに拒否されたため修正した。次の断続的な失敗では、保存されたA bindingの期限がworker確認時点より前だった。lease更新を模擬しないfixtureを修正した。製品の接続期限判定は緩めていない。
- Debug test targetをbuild。Rust／FFI未変更のため、その全体suiteとignored parityの再実行はskip。全体テスト、実DAW、release／install、Windows実動は行っていない。

## 次に残る作業

1. active B／Candidateの先行準備・公開。B-800／OS W-3025で全Preset待ちを分離し、未準備Presetは既存一覧とtyped requestで選べるようにした。選択Preset内の全Candidate待ちは残る。詳細は`docs/reference_lazy_presets_handoff_20260910.md`。
2. Manifest読込で多数のCheck／候補を再検証する際の非RT負荷の計測と削減。特に繰り返し構築されるregexを確認する。今回CPU性能の改善は測定していない。
3. OS所有のhost-rate試聴cache、同曲Version追加後の候補更新、PSR／chroma、実曲corpus、各サイズの実データ目視、macOS／WindowsのDAW実動。

新しいUIは追加していない。見た目・実機確認済み、Reference完成、公開readyとはしていない。
