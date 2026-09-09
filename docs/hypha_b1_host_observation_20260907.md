# B1 ホスト実機観測と取得失敗の診断

更新日：2026-09-09。B-741、B-743〜B-747、B-751、B-752、B-754〜B-760、B-764、B-765追記。

ローカルPRE/POST Blindは、Windows Studio ProとmacOS Studio Pro 8のVST3についてB1の同一区間取得と時刻整列を実証した。
両環境とも4096 samplesの既知遅延を挟んだ4秒の取得でPRE／POST PCMがbit一致し、推定残差は0 samplesだった。
macOS AUと他の対象条件は未実証であるため、製品のBlind開始機能は引き続き無効のままとする。
Windows Studio Proの保存済み検証曲では、ホストがJUCEのclient extension hookを呼び、context変更も通知した。
しかし、PreSonus/Fenderの`IContextInfoProvider`はv3、v2、v1のいずれも取得できなかった。
入力presentation latencyも通知されていないため、このhost APIだけから時刻対応を確定することはできなかった。

## 検証対象

最初の観測には、既に一時配置されていたB-726の最適化Debug版を使った。
その後、診断だけを追加した候補を段階的に一時配置し、同じ保存済み曲で取得失敗箇所とhost callbackを調べた。

| 段階 | PRE SHA-256 | POST SHA-256 | 調べた内容 |
| --- | --- | --- | --- |
| B-726既存配置 | `2D4564FB57964D392EE9A0C9EB19272FA4ADA9583815CABC6307BA0FCE827BD3` | `C8C083DF9C9A0F3A9990F3E7E5E593B5DA74FEB0F37AB953677C87E4F734715E` | host clockと識別情報の初回観測 |
| 失敗段階診断 | `DE021BB14352342DDABCF4A382E4121DE7A2505C9E457114129F84A49E9CE18D` | `76FA506CC2ED7CBAB1B5BBB3B67D190FE54399B834096C3ECDFBFE9286FFA958` | 取得処理が停止したAPI段階 |
| hook活動診断 | `BCA0CF1BE42545E83AC70758D084D22B3F775E61730DFC428381FF7A63EB154E` | `D3FAE7BB96C54A29E4E6E496014665646A57B6E38B47243C8189064CDE12F196` | component、application、query、notificationの回数 |
| provider世代診断 | `C1E9F591ABF357FF0E450E964CC396337B091098615A7C7138D42BAF56897AEA` | `1B1B948B8CB7CDB30046F17D22C47176C9F34D876A5DF1580E1EBB80CE22F897` | v3からv1までの順序付き取得 |
| B-757 PDC実証 | `B6CA70CF410B6C68F4955C61348537AC56B2317865E91BECC0559015FD53628A` | `940BC1EDE652EC7EF70425C5FF88E220CBDCF9DB36A57F005399B24AF1DA3ACC` | 既知4096-sample遅延を挟んだ同一4秒範囲のbit一致と残差 |
| B-764 macOS初回実測 | `C76EC58579399723BC7187E6C69FFF7883D24CAE61C7EFAF9AF55C0CE8B7F8E7` | `F977212BC1BEEDE0B1882CD25AABC6D6D6463B43B9E22915FA445A1F261FAB8B` | PREのpair claim読取り競合の特定とHybrid VU実画面確認 |
| B-765 macOS PDC実証 | `0AE58B53F01252BB90F072F67E9E8E137B2E20A756DDB833C23A958283054BF4` | `FF0DC72E583F147A3ED708AB6CBD06FB515B37717CEA927063690C1A1125BC95` | 一時的なpair claim競合を分離し、96 kHzの同一4秒範囲をbit一致で取得 |

B-765のmacOS診断候補はPRE 79,066,240 bytes、POST 86,177,568 bytesだった。
Windowsの最初の観測後はB-726の両bundle、macOSのB-765観測後はB-764の両bundleへ戻した。
どちらもStudio Proを保存せず閉じた。
Windows検証曲も観測前後で`A4399186FC150FB6FF9DF14082F1DE84D6F30B0F475BE20CBBB005C2B6AAFED3`、206,239 bytesのまま一致した。

## ホスト識別APIの観測

B-740の失敗段階診断では、PREとPOSTの両方が`context provider`で停止した。
host applicationや各識別子の文字列を読む前の失敗である。

そこで、JUCE同梱版より新しい公式headerを確認した。
現行の[PreSonus/Fender Plugin Extensions](https://github.com/fenderdigital/presonus-plugin-extensions)は、v1とv2に加えて`IContextInfoProvider3`を定義している。
v3のIIDをローカルadapterとして追加し、v3、v2、v1の順に照会した。
このadapterは継承した読取りmethodだけを使い、context値の変更methodを呼ばない。

| 対象 | provider API | hooks | edit controller query | context handler query | context通知 | host revision |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| PRE | none | component 1 / application 1 | 15 | 2 | 4 | 7 |
| POST | none | component 1 / application 1 | 15 | 2 | 4 | 7 |

hostは`IContextInfoHandler`と`IContextInfoHandler2`を照会し、両plug-inへ4回のcontext変更通知を送った。
したがって、JUCEのclient extension hookが呼ばれていないという説明は観測値と一致しない。
一方、同じhookから保持したcomponent handlerは、公式providerの3世代を一つも返さなかった。

この結果が示す範囲は、今回のStudio Pro、保存済み曲、VST3候補に限られる。
Studio Proの全状態や将来版でproviderが常に存在しないとは断定しない。
ただし、現在のB1をこのproviderだけで成立させることはできない。

2026-09-07の追加決定により、host固有のdocument／channel IDはpairingやBlind開始の必須条件にしない。
利用者が候補から明示選択したPREを正本とし、Hypha内部ではそのinstance ID、locator、pair generationを固定する。
名前は候補を見分ける任意の表示ラベルであり、同名一致から自動選択しない。
host contextは取得できた場合の補助診断に限定する。

## host clockの観測

曲設定44.1 kHzとデバイス48 kHzの相違を告げる起動時警告が出た。
設定を変更せず警告を閉じると、callback側は48 kHz、stereo、nominal block 528 framesだった。

| 対象と状態 | callback番号 | sample位置 | clock source | 入力presentation | 出力presentation | identity |
| --- | ---: | ---: | ---: | --- | --- | --- |
| B-726 POST、停止 | 30,820 | 8,246,431 | 1 | 未通知 | 0（意味未確定） | unavailable |
| B-726 POST、再生 | 38,937 | 10,469,215 | 1 | 未通知 | 0（意味未確定） | unavailable |
| B-726 PRE、再生 | 62,983 | 10,565,599 | 1 | 未通知 | 96 samples | unavailable |
| provider世代診断 POST、停止 | 6,346 | 8,246,431 | 1 | 未通知 | 0（意味未確定） | unavailable |
| provider世代診断 PRE、停止 | 16,926 | 8,246,431 | 1 | 未通知 | 96 samples | unavailable |

nominal blockは`getBlockSize()`の値であり、callbackごとのframe数ではない。
採取時刻が異なるB-726のPREとPOSTを引き算してPDCを求めることはできない。
停止状態で位置が一致した診断候補も、共通の音声区間とparticipant scopeを証明しない。
PREの96 samplesとPOSTの0も、入力presentationが未通知のままでは整列後の残差を示さない。

## 実装した診断境界

`HostContextFacts`は、取得失敗段階、採用したprovider世代、host callbackの活動回数を保持する。
Debugの明示的な診断メニューだけがこれらを表示する。
診断値は開始許可へ接続せず、識別情報の読取りが一つでも失敗した場合は部分的なIDをすべて消す。

providerは現行公式APIに合わせてv3、v2、v1の順に照会する。
JUCE更新時に置き換えられるように、v3の最小宣言を`local_blind`へ隔離した。
Audio Thread、音声出力、Record、C ABIは変更していない。

Windows実機では、日本語の更新情報メニューが四角い代替字形になった。
原因は共通`PairMenuLookAndFeel`が製品用mono fontを全メニュー項目へ強制していたことだった。
共通メニューを日本語と利用者入力に対応する`nativeTextFont()`へ変更し、日本語字形とsource契約を対象試験へ加えた。
provider世代診断は古い隔離ステージングから作ったため、このfont修正をWindowsへ一緒に配置していない。
Windowsでの表示確認は、現行ソース全体から作るV工程の候補に残る。

B-756では実機試験を再現可能にするため、Debug POSTから一つの4秒取得要求を明示発行し、完成したPRE／POST PCMを低優先度service threadで比較する診断面を追加した。
比較結果はbit一致、推定残差sample、0位置と最良位置の相関、取得native範囲だけを示し、範囲の補正、Blind開始、試聴出力には接続しない。
既知遅延は通常buildに含まれない別identityの`Kirin Hypha PDC Validation Delay 4096` VST3で与える。
このVST3は4096 samplesの固定遅延を非RTで事前確保し、同じ4096 samplesをhostへ報告する。
可変block境界、mono／stereo、reset、不正形式は純粋C++試験で固定した。
ハーネスの実装と部品試験はPDC残差0 sampleの実機証明ではない。

B-756を最初にStudio Proで動かした際、POST所有者は一度`complete`へ進んだ後、比較結果を保持せず`idle`へ戻った。
POST serviceは失敗時に試行状態を消すだけでなく所有者まで即座にresetしていたため、失敗理由も同時に失われていた。
B-757では、POSTの失敗状態と理由を明示resetまで保持し、`stalePair`と`receiptRejected`の両経路をnative試験で固定した。
初回取得が失敗した直接原因は保存されておらず、推測で確定しない。

B-758のmacOS候補を最初にbuildした際、C headerが要求発行関数の新しい引数列を宣言している一方、CMakeが以前の同名symbolを持つRust staticlibを参照してもlinkが成立した。
同名C ABIの型はlinkerが検証しないため、旧calleeが整数引数を出力pointerとして解釈し、Studio Pro 8.1.2のhost process内で異常終了した。
要求発行symbolを`kirin_hypha_issue_local_blind_capture_request_v2`へ更新し、手書きC structの240-byte layoutをRustのsize、alignment、全padding境界とC++ `static_assert`で固定した。
旧staticlibとの意図的なlink試験はundefined symbolで失敗し、ABI不一致をhost起動前に止めることを確認した。
crash reportは`Downloads/Hypha_PDC_Evidence_20260908/macos-vst3/Studio One-2026-09-08-173644.ips`へ保存した。

取得laneの詳細な失敗理由もownerのlock-free atomicへ退避し、lane回収後も明示resetまでDebug情報から読めるようにした。
通常buildのAudio Threadにはこの診断counterを含めず、PRE／POST製品名から開くversion、format、source、公式更新先、release notes、hover helpの情報入口だけを常設する。
4秒取得、host clock、callback回数、PDC比較は引き続きDebugの明示操作に限定する。

macOS Studio Pro 8.1.2、VST3、96 kHz、stereo、nominal block 2048でPRE、4096-sample validation delay、POSTを同一trackへ挿入し、POSTからexact PREを明示選択した。
最初の取得は停止時に`capture failed / lane transport`となり、POST所有callback 215、最終所有位置49,920 samplesで、4秒範囲の完了前だった。
再試行ではPOST所有callbackが累計909、最終所有位置1,815,296 samplesまで進んだが、非RT serviceが`expired / lane none`を保持した。
この段階では比較結果が生成されていないため、PDC不一致とは判定しない。

B-759で、15秒のwall-clock leaseを新しいownerの受付／arm期限として限定した。
Audio Threadがexact native範囲を完了している場合は、そのterminal factを期限判定より先に回収し、期限前のPRE armed証拠、request digest、現pair claim、全世代、範囲、形式、PCM hashが一致する間だけ非RT finalizationを続ける。
未完了のcaptureは期限後に失敗し、pair変更、transport停止、clock変更、不正receiptも従来どおり失敗する。
これにより期限を延ばさず、受付前の古いrequestから新規captureを開始させず、完了済みPCMだけをscheduler遅延から保護する。

B-759候補をmacOS Studio Pro 8.1.2、VST3、96 kHz、stereoで再実行した。
POSTは要求`69a2540d-c4e7-4a50-93e6-03bb753d85f4`の384,000 framesを`complete / none`まで取得した。
要求はposition 339,968で発行され、native startは435,968だった。
PREのarmed証拠は要求発行から約56 ms後に保存されており、開始より96,000 samples前にarmが成立している。
一方、PREの成功PCM、失敗応答、POSTの消費応答は一つも生成されず、POSTは比較結果のない`complete`で待ち続けた。
したがって、この実測はPOSTの4秒取得完了とPRE結果の欠落を示すが、PDC一致／不一致の結果ではない。

B-760では、PREの成功PCMだけだった一方向transportへ、exact request digest、pair／capture／clock generationに固定したterminal失敗artifactを追加した。
PRE laneの失敗理由をPOSTが受領し、PCMを待ち続けず同じ要求を失敗として確定する。
完成PCMの不変artifactを20回連続で公開できない場合もterminal失敗へ移し、無期限の再試行を止める。
PREが終了するなど成功／失敗のどちらも返らない場合は、POSTのcapture完了から30秒の独立したfinalization期限で失敗を確定する。
この期限は15秒の受付／arm期限とは別であり、完了済みPCMを受付期限で捨てる動作には戻さない。
成功artifactと失敗artifactはAudio Threadでは扱わず、従来の低優先度service threadだけで読み書きする。

B-764候補をmacOS Studio Pro 8.1.2、VST3、96 kHz、stereo、nominal block 2048で実測した。
POSTが発行した要求、選択済みpair、pair ownerのIDは変わっていなかったが、PREは`stalePair`で終了した。
非RT serviceがpair claimのatomic fileを読んだ瞬間に別processのlockと競合し、`matches_current_claim`が競合と安定した不一致を同じ`false`へ潰していた。
このため、受理済みの同一要求が一時的な読取り競合だけで失効していた。

B-765では、active capture requestのpair確認を`Unavailable`、`Contended`、`Current`の三状態へ分けた。
PREは既に受理した不変な要求について、pair claimが`Contended`の間だけその要求を保持し、lock解消後に同じpairを再確認する。
要求本体の不正、安定したpair不在、別pair、期限切れは従来どおりfail-closedで終了する。
Audio Threadの処理、音声buffer、PDC報告値は変更せず、低優先度service thread上のcontrol-planeだけを修正した。
Rustでは実際にpair claim lockを保持した競合試験と競合解消後の再確認を追加し、C++ serviceでは取得中の競合保持と、その後の安定したpair喪失で失敗する境界を固定した。

B-765候補を同じmacOS Studio Pro 8.1.2、VST3、96 kHz、stereo、nominal block 2048で再実行した結果は次のとおりだった。

| 観測項目 | 結果 |
| --- | --- |
| 挿入順 | PRE → PDC Validation Delay 4096 → POST |
| validation delay SHA-256 | `FC704C84E66062038D878AA163790FAE32A7315CAC29D738C590237821BC3769` |
| request ID | `fb5fb077-6d86-4735-a57e-b74e0be07bb2` |
| 発行位置 / native範囲 | issued 254,976 / start 350,976 / 384,000 frames / 96,000 Hz / stereo |
| capture | paired / failure none |
| bit一致 | yes |
| 推定残差 | 0 samples |
| 相関 | zero 1.00000000 / best 1.00000000 / margin 0.00000000 |
| 正規化zero RMS誤差 | 0.00000000 |

この結果は、同一の将来native範囲をPRE／POSTが取得し、hostへ4096 samplesを報告する別identityのvalidation delayを挟んでも、Studio ProのPDC後にPCMが完全一致したことを示す。
macOSのVST3条件についてB1の実機証明は成立した。
AUや他DAWへは一般化しない。

同じ実測中、MT VADを音声デバイスにした状態では、全insertをbypassしてもStudio Proのtransport位置が進まなかった。
macOS内蔵出力へ切り替えるとcallbackとtransportが進み、上記取得が成立したため、この停止はHyphaのAudio Threadではなく当該デバイス状態に依存する。
検証後はStudio Proを保存せず終了し、再生／録音デバイスをMT VADへ戻した。

B-764のHybrid VUもmacOS Studio Pro 8のrecord状態で実画面を確認した。
大型の左右VU、True Peak、LUFS、Crestが入力へ追従した。
trackのrecord armは有効にせず、音声eventは作成していない。

同じ保存済みchainをB-757のDebug PRE／POSTで再読込みし、Studio Pro上で実行した結果は次のとおりだった。

| 観測項目 | 結果 |
| --- | --- |
| 挿入順 | PRE → bypass済み既存insert → PDC Validation Delay 4096 → POST |
| validation delay SHA-256 | `46CB2E6E7F8ACE7386E42368DE809CBF0FE1076A7B55E0764B220AC43B1C1578` |
| native範囲 | start 9,367,592 / 192,000 frames / 48,000 Hz / stereo |
| capture | paired / failure none |
| bit一致 | yes |
| 推定残差 | 0 samples |
| 相関 | zero 1.00000000 / best 1.00000000 / margin 0.00000000 |
| 正規化zero RMS誤差 | 0.00000000 |

成功時のoptional output presentationは`0 (ambiguous)`であり、補正根拠には使っていない。
実際にロードされた別identityのvalidation delay moduleのhash、同moduleの4096-sample遅延部品試験、同一native範囲のPCM比較を証拠とする。
この結果は今回のWindows 11、Studio Pro、VST3、保存済みroutingに限り、他DAWやmacOS形式へ一般化しない。

## 検証

- macOSのHostContext対象native試験：pass。provider優先順位、世代fallback、欠落、不正ID、inactive document、競合通知、復旧を確認した。
- Windowsの隔離したHostContext対象native試験：pass。provider世代診断のPRE/POST VST3もbuildできた。
- Windows実機：PREとPOSTの両方でprovider `none`、hooks 1 / 1、query 15 / context 2、通知4を確認した。
- Windows Studio Pro PDC実機：4096-sample validation delayを挟んだ192,000-frame stereo取得がbit一致し、推定残差0 samples、zero RMS error 0だった。
- 失敗保持の対象native試験：macOSとWindowsでpass。POSTの`stalePair`と`receiptRejected`が明示resetまで失われないことを確認した。
- B-759期限境界の対象試験：完了済みPRE／POSTは受付期限後も同一pairへfinalizeでき、未完了capture、PRE arm証拠なし、pair解放はfail-closedになることを確認した。
- B-760 terminal結果の対象試験：PRE lane失敗とPCM公開失敗がexact requestに固定した失敗artifactとなり、PREが結果を返さない場合も独立したfinalization期限でPOSTが待機を終了して理由を保持することを確認した。
- B-765 pair競合の対象試験：pair claim lockの競合を安定した不在／変更から分離し、受理済み要求を競合中だけ保持して解消後に同じpairへ戻ることを確認した。C++ serviceでは競合中の取得継続と、その後の安定したpair喪失によるfail-closedを確認した。
- macOS Studio Pro 8 VST3 PDC実機：96 kHz、stereoで4096-sample validation delayを挟んだ384,000-frame取得がbit一致し、推定残差0 samples、相関1.00000000、zero RMS error 0だった。
- macOS Hybrid VU実機：record状態で大型VUとLUFS／True Peak／Crestの追従を確認した。record armは無効で、曲は保存していない。
- macOS復旧：Studio Proを保存せず終了し、再生／録音デバイスをMT VAD、利用者level VST3を検証前のB-764 hashへ戻した。
- Windows復旧：Studio Proを終了し、B-726のPRE／POST配置と検証曲を元のhashへ戻した。validation delay、一時task、補助scriptが残っていないことも確認した。
- source行数制限と`git diff --check`：pass。
- release source contract：1回実行してpass。native表示4件、`kirin_measure`、`kirin_hypha_ffi` 73件、`xtask` 137件、owned clippyを含む。
- B-765 workspace suite：1回実行してpass。`kirin_measure`本体1,430件、`kirin_hypha_ffi`本体77件、`xtask` 138件を含み、失敗0件だった。
- FFIの必須ignored suite：一覧を実測し、parity 20 / 20件、pairing candidates 6 / 6件を単一threadでpass。
- B-765 owned clippy：pass。inlineのcapture requestは非RT pollごとのheap確保を避けるため、上限256 bytesのstack値として保持する。

画面、buildと配置のhash台帳、復旧結果、公式headerの比較結果は、ローカルの`Downloads/Hypha_B1_Host_Evidence_20260907/`へ保存した。
B-757のWindows証拠とB-765のmacOS画面、比較値、候補binary、要求artifact、復旧結果は、ローカルの`Downloads/Hypha_PDC_Evidence_20260908/`へ保存した。
OneDriveの容量100%通知も表示されたが、アカウントや同期設定は変更していない。

## 次に閉じる条件

1. B-743とB-744で、明示選択したPREのinstance ID、locator、pair generation、owner claimを一つの不変な取得要求へ束ねた。
   同名候補、未命名候補、選択後のrename、instance再生成で別PREへ付け替えず、pair解放後は配信済み要求もarmed応答も無効にする。
2. B-745でprotocolのRust C ABIとJUCE非RT操作は接続した。
   B-746でrole別capture publication slot、B-747でrole、期限、prepare形式を固定したprocessorのAudio Thread入口を追加した。
   B-751で、共有する低優先度scheduler上の単一非RT所有者が要求protocolをpollし、PREはlane公開後に応答、POSTは応答とexact pairの再確認後にlaneをarmする境界を接続した。
   B-752で、PRE PCMと全体hash付き完了receiptを不変artifactでPOSTへ運び、role-local POST receiptと一つのpair barrierで照合した。
   同じcapture generation、clock generation、sample rate、layout、両側の連続native範囲が一致し、POSTの消費応答をPREが確認した場合だけ取得を保持する。
   B-754で要求schemaをv2へ進め、POSTのlive host clockから一つの将来native範囲を作り、発行位置とともにPRE／POSTへ同値で配る境界へ変更した。
   呼出側が別々の開始位置や推定PDC offsetを注入する入口は廃止した。各roleは最初のcallbackを発行位置から取得開始までに限定し、以後のclock source、連続位置、optional presentation通知を固定する。発行後の巻戻し、seek、loop、late arm、通知変更は当該要求だけで拒否する。
   これは構造上のfail-closed化である。
   B-755で出荷Releaseのclock probe常時書込みを要求発行者のPOSTだけに限定し、Debugでは両roleの診断を維持した。probeは128-byte境界へ分離し、非RT読取り時に隣接processor状態とcache lineを共有しにくい配置にした。
   B-756でDebugだけの明示取得、完成後比較、既知4096-sample遅延VST3を用意した。
   B-757でWindows Studio ProのVST3は、既知4096-sample遅延を挟んだ同一4秒範囲のbit一致と残差0 sampleを実証した。
   optionalなhost通知がなくても内部事実で対応区間を証明できることを確認した。
   macOS VST3の初回実測で完了後の非RT回収が受付期限に負ける境界を特定し、B-759で受付期限とfinalizationを分離した。
   B-759再実測ではPOSTが全範囲を完了した一方、PREが成功PCMも失敗理由も返さず待機が終わらないprotocol欠陥を特定した。
   B-760でPREのterminal失敗応答とPCM公開再試行の上限を追加した。
   B-764のmacOS実測でPREのpair claim読取り競合を安定した不一致と混同する欠陥を特定し、B-765で三状態の再確認へ変更した。
   B-765のDebug VST3はmacOS Studio Pro 8、96 kHzで同一4秒範囲のbit一致と残差0 sampleを実証した。
   次は同じ条件をmacOS AUで確認する。証明できない取得だけを開始不可にする。
3. Blind、Reference、Keep / All Keep、Recordの競合はHypha自身の共有leaseで調停する。
   DAWのtrack名、PID、host固有IDからroutingや未知の参加者を推測しない。
4. Windows VST3とmacOS VST3の確認済み条件を正本とし、macOS AUで明示pair、同一区間、既知遅延の残差0 sampleを同じ条件で確認する。
5. B1成立後にB2の開始排他と取得barrierを接続する。
   B1の未成立中はBlind開始機能を有効にせず、依存しないU工程とM工程を進める。

LSアップ用：skip。
HPアップ用：macOS skip、Windows skip。
今回は署名、notarize、公開を実施していない。
