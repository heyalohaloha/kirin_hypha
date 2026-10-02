# LOOP製品試験の時計設定を診断instrumentationから分離する

2026-10-02。B-1141のCI run `36995687922`はmacOS source契約、AU arm64、公開履歴がpass、
Windows VST3は`kirin_live_timing_product`でfailした。旧候補のgreenや実DAWの成功で相殺しない。

## 原因

fixtureは両OSで同じStudio Pro 8.1.2.113407 / VST3 / 48 kHzのcontent clock、4096-sample
物理遅延、native先頭clamp / 負PPQを再現していた。しかしprocessorの時計authority設定が
Clang/macOSだけの`KIRIN_HYPHA_TIMING_PRODUCT_DIAGNOSTIC`内に置かれていた。
Windowsの実行fileはStudio Proではないため、製品のexact-host classifierがauthorityを与えない。
安全条件が満たされず、60秒後もstage 4 / preparing / loopUnprovenのままだった。
開始時clock欠損の注入、正確な匿名commandの取得、初回LOOP試験の登録も同macroに依存していた。

これは製品の対応条件を広げる理由ではなく、非出荷fixtureの前提がOS間で違っていた問題である。

## 境界

非出荷の`LiveTimingFixtureAccess`だけが、callback開始前に既存classifierへexact wrapper / host /
version / rateを渡す。certifiedContent / maximumDelay 0を確認してからfixtureのprocessorへ設定する。
製品に新しいfield、実行時override、exported API、host allowlistは追加しない。headerのfriendだけで
fixtureのアクセスを限定し、optional tracingの`-fno-access-control`を通常試験の前提にしない。

audio側の監査は完全なatomic command wordとatomic receiptを使う。表示用`liveBlindStatus`は
audio callbackから呼ばない。clock欠損注入も両OSの初回fixtureで同じ条件から実行する。
初回LOOPと途中からLOOPの両試験を、diagnostic optionと無関係にCTestへ登録する。
macOSの追加traceは観測だけを増やし、時計authorityや製品試験の有効化は担わない。

60秒deadline、実native clamp通過、動的compressor / band、固定MATCH、20周、全frameのbit同一
source / gain、4クリック、END receipt、通常Aの0 sample / bit同一は変更しない。
source契約もaudio側の表示query禁止、exact fixture profile、無条件CTest登録を検査する。

## ローカル確認

- macOS x86_64 Release / trace ON: 初回・途中からの2/2 pass、35.47秒。
- 同じgraph / trace OFF: 同じ2/2 pass、35.45秒。macOS専用access flagなし。
- Windows MSVC x64 Release / trace OFF: 同じ2/2 pass、35.22秒。
- source契約164/164 pass。製品clock / PCM / gain / admission sourceの変更なし。

WindowsはB-1141 sourceへ所有する4 fileのfixture overlayだけを適用し、元fileをhash付きで保全した。
既存graphで試験targetだけを作り、4本の通常product bundleのhashが不変であることも照合した。
このdirty fixture実行をclean-source製品build、CI成功、実host、署名済み配布受入へ読み替えない。
変更確定後のexact commit CIと残る実host確認は別途必要である。

## B-1141 Windows実hostで確認できた範囲

Studio Pro 8.1.2.113407 / VST3 / stereo / 48 kHz / 528 frames / 4096-sample delayで、
最初からLOOP ON → 直接BLIND → 両Source → 1クリック開示 → ENDを実行した。
独立identity oracleは105周、30,174,272 framesを全frame照合し、content clock差0、
source未観測 / span不一致 / clock欠損 / block内clock不一致は0だった。
Source切替等の4つの非identity blockと127のstartup / exhaustion blockは成功件数へ含めない。

通常再生のLOOP範囲内でMATCH → 停止せずLOOP ON → BLINDも、固定0.0 dBを維持して両Sourceと
開示まで確認した。68周、19,774,256 framesが全frame照合され、clock差0、上記4 faultは0だった。
切替等の4非identity blockと7 startup blockは別計上した。END操作後はDAWを停止し、両probeを
停止・exportしてSongを保存、hostを通常終了した。END後の通常footerの独立画面確認は未収録。

LOOP終点を過ぎた再生位置でLOOPを有効にし、DAWが範囲内へ戻るケースではPRE WAITが残った。
通常の範囲内切替の成功でこれを相殺しない。原因確認と安全な復帰条件の検証が残る。
また追加負荷の旧版比較と同一実行file対照にfailがある。測定器の再現性と製品差の分離、
Mac exact候補のAU / VST3、Windows AAX Developer実host、通常Pro Tools / 公開受入は未完了である。
