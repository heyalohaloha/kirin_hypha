# Hyphaの軽量化とWindows再検証

状態: 部品回帰はB-774候補で合格。実ホストの最大構成と長時間負荷は最終候補で確認する。
対象: B-725で保存したebur128原版、B-726の計測改善、その後のローカル変更。
PSBとPRE/POST Blindの完成記録ではない。

## 計測を間引かないM/S窓の再利用

M/Sの窓長と10 msごとの最大値観測を維持し、ebur128のフィルター済み音声を128フレーム単位で集計するキャッシュを追加した。
更新された区画だけを再集計し、読取りでは区画の和と両端の実サンプルを使う。
大きな累計から古い値を引き続ける方式を使わず、大音量の後の小さい残響や無音での桁落ちを避ける。
WatchとMeterSessionは同じ変更を使うが、それぞれの保持期間とreset条件は統合しない。
フィルター、True Peak、I/LRAの履歴挿入、正本Recordの窓と更新周期は変更していない。

Windowsの同一部品試験では、M/Sを10 msごとに読む従来処理が30.73523 ms/音声1秒、キャッシュ利用時が2.89423 ms/音声1秒だった。
削減率は約90.6%で、これは入力とTrue Peakを含む当該部品の比較である。
Hypha全体やDAW全体の90.6%削減を意味しない。
入力はS-1の実WAV、48 kHz stereo、480,000 frames、peak 0.501187205で、各3回の中央値を使った。
Rust dev opt-level 2、debug assertionsとoverflow checksは有効である。

## PRE/POST 2個のプロセス負荷

同じWindows検証機で、GUIを開かない独立ホストにPRE/POSTを1個ずつ置いて比較した。
両方ともC++ DebugとRust opt-level 2であり、旧配置の未最適化Rustとの比較ではない。
APPDATA、LOCALAPPDATA、TEMP、TMPは試験ごとに分離した。
再生区間は48 kHz stereo、512 frames、12秒で、両方1,125 callbacksだった。
以下は1組の順次測定であり、公開版の性能認証や長時間の最悪値ではない。

| 条件 | 再生中のPC全体比 | 停止後の安定区間 | 再生中のプロセスCPU秒 |
| --- | ---: | ---: | ---: |
| B-724 opt-level 2対照 | 6.68658% | 1.65842% | 6.42188 |
| M/Sキャッシュとatomic writer改善後 | 3.15481% | 1.59339% | 3.03125 |

再生中は約52.8%下がったが、停止中の改善は小さい。
再生周期の遅れは対照2回、変更後3回だった。
最大callback wall timeは4.4493 msと2.7703 msで、これを音切れゼロやRT負荷0.1%未満の合格に読み替えない。
停止区間のI/Oも残っている。
100 msのheartbeat、ペアの消失検出、Record制御を維持しているため、今後も通信処理を分けて計測する必要がある。

## 通信と表示の変更

atomic writerは、毎回の親ディレクトリ作成確認をやめ、tempファイルのopenがNotFoundを返した場合だけ一度再作成する。
原子的renameと既存ファイルの保護は維持する。
親ディレクトリの消失と再作成、親が通常ファイルである場合、rename失敗時のtemp回収を追加試験した。

表示側はABIのpaddingを比較せず、意味を持つ全フィールドの変更時だけ再集計と再描画を要求する。
未取得を示すNaNは繰り返し同じ状態として扱い、整数時刻は64 bitのまま比較する。
履歴の途中の変更も検出し、PRE側の遅着を取り逃がさないようpoll自体は止めない。
Spectrumの線分統合とサブピクセル誤差を制限した幾何簡約も実装済みで、
B-774候補の共通UI回帰では全倍率と更新条件が合格した。

任意解析は表示ページに応じて一つだけ有効になり、ページを離れると前のruntimeを停止する。
Observation、Spectrum、Focus Trail、ATTACK、絶対時間軸は同じ内容を再受領した場合に再描画しない。
停止して新しい音声が届かない間は、これらの解析と描画を追加実行しない。
100 ms周期のheartbeat、owner lease、pair、Record制御は生存と状態遷移の正本なので維持する。
inactive JSON更新まで止める変更はpair discoveryの世代契約を変えるため、この描画修正へ混ぜていない。

## 検証曲の固定

Windowsのデスクトップにあるデモ用ショートカットは、保存済み.songではなく元の.dawprojectを読み込む。
今回これを開き直したため、新しい番号のプロジェクトができ、前回の検証状態を維持できなかった。
この取り違えは検証操作の誤りであり、別の曲を選ぶ必要があったわけではない。

前回のメインPRE/POSTのinstance IDとproject IDを、保存済み.song内のVST3 presetと照合した。
両方が一致したのは次のファイルだったため、以後のDAW確認先を固定した。

```text
C:\Users\hello\OneDrive\ドキュメント\Studio Pro\Songs\Peach_Hypha_Demo(13)\Peach_Hypha_Demo.song
```

20:00にこのファイルを明示して開いた。
44.1 kHzの曲と48 kHzのデバイスの不一致通知は確認を閉じただけで、外部機器とデバイス設定は変更していない。
新しく読み込んでしまった(21)での画面操作は、前回と同条件の検証結果に含めない。
通常配置には旧版のバックアップを保持した上で、計測改善を含む検証用PRE/POSTを一時配置している。
署名済み公開版ではなく、Source identity表示を公開版証明に使わない。

## 実行済みの試験

- ebur128原版と追加キャッシュの単体試験: 22 pass。7 sample rates、mono/stereo、不規則なblock、無音、reset、channel map変更、実WAVを含む。
- Rust workspace lib: 1,582 pass、9 ignored。その後のatomic writer追加を含むkirin_measure lib: 1,416 pass、9 ignored。
- Record parity ignored suite: 実測20件、20 pass。
- pairing_candidates ignored suite: 実測5件、5 pass。
- xtask: 136 pass。workspace clippyは完了し、既存vendorの警告のみ。
- Windows検証用VST3: PRE/POSTのstereo realtime、stereo offline、mono realtime、計299,680 samplesがbit identical、報告latency 0 samples。
- 表示フィールド比較と幾何簡約の専用試験: pass。Mac UI全体は通過した回もあるが、Focus Trail描画時間の上限超過も再現しており、安定合格とは扱わない。
- 追加後のmacOS workspace lib: 1,585 pass、9 ignored。Windows kirin_measure lib: 1,417 pass、9 ignored。両方のclippyは完了。
- 最新の全UI試験: fail。100% Focus Trailは静止4.37104 ms、毎frame更新4.54509 msで、更新時が4.5 msの上限を超えた。幾何と操作の試験はpassだが、以降の倍率はこの実行では未到達。

上記の古い不合格に対し、B-774候補で実コンポーネントの共通UI回帰を再実行した。
Focus Trailは100%で静止3.5876 ms、更新4.14978 msとなり4.5 ms予算内だった。
125 / 150 / 200 / 300%も各表示サイズの固定予算内で、Spectrum、PSB、絶対時間軸、
Perceptual、Hybrid VU、TIME五量、SPACE密度を含む全対象が合格した。
ログは`/tmp/hypha-b774-ui.log`に保存した。

## 同じ曲で確認できたPSBと操作の中断

Peach_Hypha_Demo(13)の150%表示で、停止中はPSBがINACTIVEとなり、再生するとPOSTの20帯域バーが現れた。
20:07と20:09の画像では値が更新されている。
POSTからΔへの切替後は、外部処理をbypassしたメインPRE/POSTでゼロ付近の線を確認した。
非ゼロΔ、停止後の消去、再起動後の再現は、まだ合格としていない。
画像は`/tmp/hypha-b726-psb-stopped.png`、`/tmp/hypha-b726-psb-playing.png`、`/tmp/hypha-b726-delta-requested.png`に残した。

POST単体Spectrumでも差分専用のFocus Trail領域を予約しており、表示が低く潰れる原因になっていた。
単体表示ではこの予約を外し、描画、click、hoverが同じ領域を使う変更を追加した。
全5倍率とGuide有無の実レイアウト試験はpassだが、この変更をWindowsのDAWへ配置した確認は未実施である。

20:23に、こちらの操作外で300%、TRACK/STEM、DRUMへ変わり、ChorusとTricompが追加されたことを画面で確認した。
操作担当の確認を依頼し、DAW操作と追加配置を停止した。
外部操作の変更を取り消したり、その状態で性能値を採用したりしていない。
共有領域の競合対策は別途[Windows Analysis共有領域の排他と再開](hypha_windows_exchange_safety_20260906.md)に記録した。

その後、Daisukeの操作停止と検証委任を受けて、同じ曲でTRACKのDRUMを検証した。
最適化した単独描画試験でも、31件の合成イベントで全体に362〜446 ms、流線だけで227〜338 msを要した。
停止後の実機editorにも残余負荷があり、描画と停止時更新を分けて修正する必要がある。
測定条件と修正対象は[DRUMの描画負荷の検証](hypha_drum_render_diagnosis_20260906.md)に記録した。
現時点では曲を停止し、Hypha editorを閉じている。追加配置は行っていない。

## ログと未完了の範囲

部品測定は`/tmp/hypha-b726-components-opt2.log`、独立ホストは`/tmp/hypha-b726-whole-plugin-cpu.log`に記録した。
回帰試験は`/tmp/hypha-b726-upstream-tests.log`、`/tmp/hypha-b726-measure-tests.log`、`/tmp/hypha-b726-parity.log`、`/tmp/hypha-b726-pairing.log`、`/tmp/hypha-b726-clippy-final.log`に残した。
描画全体の通過回は`/tmp/hypha-b726-ui-profile.log`で、Focus単独の上限超過は`/tmp/hypha-b726-focus-profile2.log`に残した。
毎frame更新も含む最新の不合格ログは`/tmp/hypha-b727-ui-full-test.log`、実レイアウトと下部操作の合格ログは`/tmp/hypha-b727-ui-geometry-test2.log`である。
プロファイラーを同時実行した回の時間は性能比較から除外する。

PSBの非ゼロ差分、停止・再起動、描画の固定予算は部品回帰で閉じた。
未完了は、同じ最終候補を実DAWへ置いた最大構成、10分以上の連続再生、停止中のhost全体負荷と、
音と表示の同期・読み取りやすさの確認である。
Blindの同区間取得とmacOS AUの既知遅延残差0 sampleは別のB-774候補で成立したが、
Blindの製品操作一巡は専用の進捗記録を正本とする。
Blindの部品試験を製品の完成に読み替えない。
LS、macOS HP、Windows HPはいずれも公開検証未完了のためreadyではない。
