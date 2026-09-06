# Windows Analysis共有領域の排他と再開

状態: Windowsの単体試験は合格。DAWへの配置と再起動確認は未実施。
対象: `analysis_exchange_transport.rs`から分離したWindows専用transport。
PSBの欠画がこの不具合で発生したと断定する記録ではない。

## 修正した問題

従来のv2共有領域はwriter同士を排他し、readerはactive bankをコピーした後でgenerationを確認していた。
writerが2回更新すると、遅いreaderがコピー中のbankへ戻って書き込める。
後からgeneration不一致を検出しても、非atomicな読取りと書込みが重なったデータ競合は解消されない。
また、writerが所有フラグを立てたまま終了した場合、そのフラグだけでは所有者の終了を検出できない。

## 新しい契約

- readerとwriterの両方が、同じWindows named mutexを取得してから共有領域へ触れる。
- 取得は`WaitForSingleObject(handle, 0)`の1回だけとする。使用中の書込みはWouldBlock、読取りはNoneで終わり、spinと再試行待ちは行わない。
- mutexを所有したままthreadまたはprocessが終了した場合、次の所有者は5種類すべてのslotを無効化する。見かけ上正常なlengthでも過去のpayloadを受理しない。
- 所有権はRAII guardに結び付ける。guardはSend/Syncではなく、slotの借用はguardの寿命を越えられない。
- request、ready、Spectrum、Perceptual、Attackを固定長で保持し、共有領域全体は224 KiB未満とする。容量超過は既存の正常な値を壊さず拒否する。
- namespaceを`Local\\KirinHyphaAnalysis-v3-...`へ変更する。v2バイナリとは共有せず、PRE/POSTは同時に更新する。

このtransportは非RTのAnalysis経路だけが使う。
Audio Threadにlock、待機、allocation、I/Oを追加していない。
Watch、Record、正本のPRE/POST測定、通常音声の出力は変更しない。
同一ユーザーtokenの既定ACLを使い、Global namespaceや広いアクセス許可は追加しない。

## 検証

Windows検証機の隔離したstagingで次を実行した。
DAWの終了やプロジェクト変更を伴う試験ではない。

- Windows transport専用: 7 pass。全5 slot、容量ちょうど／超過、別handle、最終close後の初期化、busy時の即時skip、200回の並行更新、所有processの強制終了後の再開を含む。
- 強制終了は専用の子test processをexit code 78で終了させる。親は全slotの無効化と新規publicationの成功を確認する。
- Windows kirin_measure lib: 1,417 pass、0 fail、9 ignored。
- Windows kirin_measure clippy all-targets: pass。既存build scriptの診断を除き、所有sourceの警告なし。
- macOS workspace lib: 1,585 pass、0 fail、9 ignored。workspace clippy: pass。既存vendor警告とbuild scriptの診断は残る。

ログは`/tmp/hypha-b727-windows-exchange-tests.log`、`/tmp/hypha-b727-windows-measure-tests.log`、`/tmp/hypha-b727-workspace-tests.log`、`/tmp/hypha-b727-clippy.log`。
全機能のDAW動作確認、Blindの製品接続、公開リリースの合格を意味しない。

## 配置前に残る確認

2026-09-06 20:23 JST、こちらの操作外でDAWの表示倍率、用途設定、挿入プラグイン構成が変わったため、画面操作と追加配置を止めた。
WindowsにはB-726の検証用PRE/POSTが残り、このv3 transportはまだ配置していない。
操作担当を確認した後、同じ保存済みPeach_Hypha_Demo(13)でPRE/POST同時更新、PSBのPOST／Δ、停止、再生再開、DAW再起動を確認する。
旧v2と新v3が混在する場合は、Analysisの接続が成立しないことも確認する。

## API根拠

Windows mutexの所有thread、既定ACL、名前の共有、既存objectへの接続は[CreateMutexW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexw)を参照した。
timeout 0、WAIT_TIMEOUT、WAIT_ABANDONEDの所有権と共有状態の再検証は[WaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject)を参照した。
