# G2追加性能点検 — 2026年10月8日

利用者の「全ての画面、機能を動かして、CPUを圧迫、重たくなる箇所がないか総点検」の指示に対する記録。追加機能の設計へ広げず、現在の製品経路の負荷を測り、確認できた重複処理を修正する。

## 測定の境界

Release／x86_64の使い捨てfixtureで、描画、出荷editorの更新、解析、PRE共有、Record、比較試聴、保存・更新の経路を区別する。process CPUはそのprocessが使ったCPU時間、wallは待ちや他processの割込みを含む。各々を別に記録し、他のアプリのCPUをHyphaの計算として足さない。共有マシン上の結果を実DAW全体の負荷、全hostの保証、CPUゼロへ読み替えない。

長時間集計fixtureは現sourceをコピーして内部のcanonical energy履歴へ既知の値をseedする。実際に6時間の音声を録音した証拠ではない。DSP処理は変更せず、48 kHz／stereoのPCM 1秒と32／128／512 samplesのdrain、Session＋Watch＋Record TRACE＋native summary＋Phase D 10 slotsを処理する。ring、threadのsleep、disk I/Oはこの計算範囲に含まない。32 samplesの1,500 drainsはcallbackごとにdrainした上側条件で、実workerの1 ms sleepによるbatchingを含む実DAW CPU値とは呼ばない。

## 確認できた重い箇所と修正

### Record中間集計の全履歴走査

Record中に入力をdrainするたび、I／LRAのcanonical `finalize()`を呼んでいた。音声処理より頻繁な全履歴走査が、曲の長さに比例して増える。責務を先行commitで抽出し、中間表示には既存のexact-energy cacheを接続した。

このcacheは同じnative-rate engineが受理したenergyを使う。値の有無、処理済みprefix、Max TP、更新期限は維持する。engineごとのI／LRA合計65,536 distinct nodesを超えると補助木だけを解放し、canonical exactへ戻る。node payloadの上限は約3.5 MiB／engine（allocator overheadは別）で、従来のcanonical履歴を削除・近似する変更ではない。丸めが曖昧なgateもcanonicalを使う。Stopのtight-drainと最後の保存は従来のcanonical `finalize()`を維持し、Record／plugin_data／work.json／旧ABIの形は変更しない。

| 60分seed履歴／PCM 1秒 | 修正前process CPU | 修正後process CPU | 修正後・1 coreに対する比率 |
| --- | --- | --- | --- |
| 32 samples／1,500 drains | 277.211 ms | 83.467 ms | 8.35% |
| 128 samples／375 drains | 122.956 ms | 74.339 ms | 7.43% |
| 512 samples／94 drains | 81.344 ms | 76.183 ms | 7.62% |

32-sample条件の中間summary計算のwall合計は185.149 msから0.234 msになった。CPU全体との差分をdiskやGUIの改善量として扱わない。上限を実際に越えた6時間seed条件ではcap=true／補助nodes=0で、32／128／512は87.266／82.310／88.081 msのprocess CPUだった。上限後の履歴更新時にはcanonical走査のcostが残るが、変更のない履歴を毎drainで走査しない。

fixtureでは各drainのI／LRAがcanonicalと絶対差1e−10未満、TP／layoutが一致した。製品の境界試験でも44.1／48／96 kHzの毎drain、pending 10 ms未満、mono／stereo RESET後を確認する。最終保存の既存ignored parity／pairingを省略しない。

### DRUM固定材質の重複描画

同寸法の各レーンに固定gradient／textureを重複描画していた。既存のbounded material storeを使い、corner／bed／vignette／寸法／physical DPIが同じ固定材質を共有した。観測値、波形、文字、時計はcacheへ含めず、cold／warmの同画素、容量、最後のeditor終了時の破棄を確認した。

正式DRUM gateはlegacy changing 120、resize 6、BAND 4、V2 dense 120条件を全て完走。元のFAIL条件である900／DPI 1.25／2 instances／ALLはmedian 16.0398 msから8.79301 ms、全V2条件最大medianは10.6228 ms、maxは12.1634 msになった。既存thresholdを変更していない。50枚の実描画PNGは修正前後で全hashが一致した。詳細は[G2 receipt](README.md)へ保持する。

## 全画面の描画・出荷editor

LEVEL／TIME HISTORY・RUN・DRUM・SHARP・LIVE／FREQの絶対・差分・SHAPE・MID／SIDE・PSB／SPACE／VU／Referenceと、比較試聴・Blind・版比較・復旧の副面を対象にする。描画fixture、解析の実データ到着を確認するnative peer、正式な製品試験の役割を分ける。表示状態を作っただけの描画値を、比較試聴serviceや実DAW全体のCPUへ読み替えない。

FREQ／SPACE／Referenceの背景はdirectionを使わず、Referenceはenergyも使わない。これらの値の変化で同じ大きい背景rasterを再生成していた。描画に使う入力だけをcache keyへ含め、LEVEL／TIMEのdirection、FREQ／SPACEのenergyとactive／capture／density／寸法／DPIは従来どおり失効させる。raster identityの試験は、使わない値だけの変化で同じ画像を保持し、使用値が変われば再生成することを確認する。周期・観測値・材質の生成内容は変えない。

静的well／frameのinverse clipも重いが、4分割clip／glass全raster描画の候補はfresh sourceの画素oracleに不適合が出たため採用しなかった。古いinline objectと混在した先行scratch結果は最終品質・速度の根拠から除外し、製品の元のclipを保持した。許容差を広げて候補を通していない。最終sourceの実行結果だけを以下へ記録する。

native peerは14経路×停止／再生／非表示の42条件を完走した。LEVEL、TIMEの各面、FREQ、SPACE、Reference、PRE／POST VUを含む。子componentの表示と解析demandを照合し、FREQは34 accepted frames（producer終端36）、SHARPは11（終端12）、LIVEは13（終端13）で実データ到着を確認した。DRUMはtrackStem／demand 8を確認したが、このpeerだけでは検出打音の受入を断定せず、正式ATTACK試験と分ける。通常A経路は各blockのbit identityと0 sampleを確認した。

同processの1 coreに対するCPU比率は停止3.08～13.23%、再生3.79～21.00%、非表示2.31～3.14%だった。最大はDRUM、FREQの再生は8.78%、POST／PRE VUは6.71／6.85%。これはMeasure／IO／任意worker、出荷editorのtimerとnative repaintを含むfixtureの全process値である。callbackはmessage-threadのTimerから呼び、独立した実RT threadのdeadline保証には使わない。callbackのwall medianは0.355～1.101 µs、p99は7.726～30.297 µs、thread CPU平均は計測自体を含み1.681～3.526 µsだった。

Referenceの登録6件あり／停止・再生・非表示を含む9条件も完走した。refreshは未登録0.0618～0.0845 ms／call、登録metadataあり0.5639～0.8550 ms／call。登録・選択は受理されたがruntimeはWAITING／`reference_candidates_empty`で、準備済み音声のB再生を実証したpeerではない。音声pages、B出力、MATCH、Blind、停止・復帰・排他は正式nativeの各製品試験で確認する。

全45描画caseを375×250／DPI 1.25と900×600／DPI 2の二条件で、cold、warm、変更＋描画、同値更新の四phase（360 rows）測定した。ReferenceのB／C／V、live／local／version Blind、LISTEN・復旧・footerの副面も含む。synthetic stateを実painterへ渡した描画計測であり、全serviceが実際にその状態へ遷移した証拠ではない。

最大DPIのFREQ絶対表示を強制的に全体描画した条件はwarm median 25.651 ms、変更＋描画26.976 ms／p99 32.637 ms。TIME POSTは変更＋描画median 15.574 ms／p99 19.000 msだった。この強制FREQ fixtureの30 Hz投影81.53%／coreを出荷画面のCPUとして使わない。出荷FREQの曲線更新は12 Hz、数値は2 Hzである。900／DPI 2のSPACE cold 94.639 msも別に保持する。最大寸法・高DPI・密集表示で描画costがなくなったとは報告せず、現候補の実DAW二枠・resize・表示／非表示と操作応答をG3で確認する。

各実行前後のhost snapshotには他のNode／esbuildの負荷が残る。所有するbuild／試験を重ねず実行したが、静穏hostの性能gateとは呼ばない。初期Clock不備で解析データ0だったpeer、DRUMがHISTORYへ降格したpeer、登録ready前に判定したReference fixtureは修正前診断として保存し、上記の受理済み状態の数値だけを採用した。最終peer binaryはformat修正前のFFI archiveをlinkしたため、その後に更新されたarchive hashへ差替えない。全正式nativeは最終archiveでfresh relinkして受入を区別する。

## 解析・配送の追加計測

| 経路 | 実測 | 範囲 |
| --- | --- | --- |
| PREの最大ALL snapshot clone＋v5 encode | 144,252 bytes、1回58.926 µs process CPU、10 Hz換算0.0589%／core | 停止・同revisionでの重複encode候補も確認したが、このcostだけを理由に配送のauthorityや失効を変更しない |
| POSTの同snapshot decode | 1回181.525 µs、10 Hz換算0.1815%／core | file open／atomic replaceを含まない |
| FREQ LR／48 kHz | PRE＋POST、30 Hz換算0.2362%／core | analyzer単体、8192 FFT。描画／ring copyは別計測 |
| FREQ LR／384 kHz | PRE＋POST、30 Hz換算2.3698%／core | analyzer単体、65536 FFT。LR／MID／SIDEを48／96／192／384 kHzで測定 |
| 常時Session mono-sum／384 kHz | PRE＋POST、10 Hz換算0.7410%／core | FFT analyzer単体。常時経路と任意解析を区別 |
| capped Session summary／6時間 | energy更新時のI／LRA query pairは414.333 µs、10 Hz換算0.4143%／core | accepted I 100 ms／LRA 1 sの更新を含む。unchangedはmemoを再利用 |

各snapshotの本来のpublish周期はATTACK／Absoluteが100 ms、Spectrum／Perceptualが33 msである。ATTACKの262,144-byte上限を30 Hz配送と仮定して負荷を水増ししない。任意解析slot、非表示でのdemand終了、worker再起動、32-sample継続、timeout、欠損・別requestは既存契約を保持する。

## 長時間RecordのJSON保存

公開`PluginDataWriter`の実flushを使い、1時間36,000 TRACE／7,200 PSB、6時間216,000／43,200をseedした。空の模型関数ではなく、実際のHMAC、serialize、使い捨てfileへのwrite／rename、readback、checksum、件数と時計を照合した。署名key・個人dataをログへ出さない。

修正前の1時間は8,473,281 bytes、process CPU 100.85～110.97 ms、wall 160.33～208.29 ms。6時間は51,046,694 bytes、CPU 603.83～626.14 ms、wall 887.79～1,158.60 ms／30秒flushだった。6時間の分解ではclone約10 ms、checksum用serialize 182～194 ms、HMAC 179～184 ms、保存用serialize 192～194 msだった。disk write／renameのwallも別に記録し、計算削減でdisk latencyまでなくなるとは呼ばない。

flushはIO-owned writer上で行い、Audio／Measureのmutexを保持しない。checksum用と最終保存用の二重serializeを同じbytes・schema・HMACのまま一回へ統合した。正式なchecksum検証関数は数値・byte oracleとして保持する。checksumが末尾にある現schemaの形を照合し、形が変われば従来のcanonical経路へ戻る。serialize失敗／panicで旧checksumを復元し、write／renameの失敗は既存fileを保全する。7境界試験でUnicode、入れ子のescaped text、optional値、長期TRACE／PSB、失敗・panic・fallbackとwrite／renameを確認する。

| actual flush／3 trialsの中央値 | 修正前process CPU | 修正後process CPU | 削減率 |
| --- | --- | --- | --- |
| 1時間／8,473,281 bytes | 108.528 ms | 78.221 ms | 27.9% |
| 6時間／51,046,694 bytes | 622.755 ms | 428.932 ms | 31.1% |

6時間の30秒周期換算は2.076%から1.430%／core。修正後のCPU範囲は423.683～550.523 msだった。出力全byteと従来canonical HMAC、件数、先頭・末尾時計、実file readback、一時fileの消去が一致した。wall／外部負荷が前後で異なるため、disk latencyや実DAW全体が同率で改善したとは呼ばない。Release compile／実flushは各一回、runtime 14.884 s／exit 0。probe全体の最大RSSはverification用bufferを含む累積値なので、writer単体のmemory削減根拠に使わない。

## 最終の製品回帰gate

最終Rust workspaceは2,367 passed／0 failed／43 ignored。fresh Clippyは全workspace／all-targetsでPASSし、同じ655 Rust／Cargo sourceのhashで有効性を照合した。Record中間集計の2試験と保存の7境界試験を含む。必須ignored inventoryはparity20／pairing6で、26件を全てserial実行してPASS。Stop後のcanonical保存、plugin_data実出力、RecordとPRE／POST pairを通常suiteだけで代用していない。

50 native targetを最終FFI archiveでfresh build／relinkし、現release-sourceの90件＋Update7件のinventoryを照合。97件を一回ずつserial実行して全PASS、再試行0。正式ATTACKは環境変数で速度gateを有効にして既存全条件を完走した。EditorSurface、UiRender、ABI、TIME、Capture、Referenceの実音声runtime／AudioPages／streaming／component、出力所有者、live／local比較・復元・失敗、Updateの成功／失敗／取消／lifetimeを含む。

per-test child user＋system CPUとwallは`performance-native-receipt.json`へ保持し、集計境界と97件の一意性を`performance-native-closure.json`で照合した。fixture／oracle／起動や並列workerを含む値であり、表示各面の定常CPU割合へ読み替えない。正式ATTACKの成功CTestは数値stdoutを抑止するため、このfresh PASSと先行の詳細速度receiptを区別する。archive hashとCMake sourceを照合し、Root最終linkでは旧FFIの診断限界を引き継がない。実DAW／Windows・ARM64現候補／本人の受入は未実施である。

## 証跡

raw logは環境pathを含み得るためローカル保存とし、公開文書へはbasenameとSHA-256だけを記録する。

| 証跡 | SHA-256 |
| --- | --- |
| `measure-query-worker-results.json` | `4a822c8ca47a58f00426d9e80b3b45be4095f6c3e5a2c1ab2d36a0e092a26136` |
| `measure-query-worker-after-results.json` | `b59fbae1acc7b8b37668460920e6f144ab585337db0e188ff3cc49d9153b7fe1` |
| `io-freq-results.json` | `2c8170bb21448520bf82473f2a7e9517ea826353726e19258dbdae144e4e0920` |
| `long-flush/after/receipt.json` | `12201ee843c2e31c054dc7c9c46e39b5ba83d4c1475e54660765d63254550457` |
| `long-flush/after/artifact-hashes.json` | `641157e9c6514667ff32880636cf40356cd848d6691af431a6ca97a887a69a80` |
| `frozen-peer42.csv` | `f750fbf056c6b4942ad975433005b36b84f0182402e0ba77aa137fc188cfea02` |
| `frozen-reference9.csv` | `b29137166406db6f6e3ca5325c3384a1738335a0ae0061f7c13272406307495f` |
| `frozen-direct90.csv` | `ef0c717a1df9cacfc864b5a1cd8fa21607f95ba32d38fba8e83c83f3400f67d6` |
| `frozen-receipt.json` | `884a257d5359115905de8a5040849d51118e8483cad21ba21599b5cf198f01c2` |
| `performance-audit-workspace-final.log` | `20b432b080a5cfddfc9de289dc332aeebbb97f8a495900821aa232f82b6070c4` |
| `performance-audit-parity-ignored.log` | `1bf4a4535c22a28cd184a17e02a0b386715325f50f370104494f11634cce346c` |
| `performance-audit-pairing_candidates-ignored.log` | `d7f579d0159cedbb473e07c52a78c5a30fd372720c563a687a8e005111f21e65` |
| `performance-audit-rust-gates.json` | `a058a319880605005279c6404d44d2fce35b20883b145de47e1cbb9bca980d9b` |
| `performance-audit-clippy-final.log` | `33152199a3b95653f89208d22e64cf158797250af63928b3d717aeda11ba02e9` |
| `performance-native-build.log` | `ea5b303937fbb32082a63bbdd0fa1f9f25dfe41a1f26dd31e801e63292b3e554` |
| `performance-native-receipt.json` | `d7e4e4ecdf8496235e6bb8654fb2707be399ec331bfdc77b3e7d966d900ede13` |
| `performance-native-closure.json` | `aa40c1a90df6c82622d03018141412935f35e519662a452596b92e80486c5ce4` |
| `performance-native-kirin_attack_ui_contract.log` | `25846861c56dd8d9dba9354a37dc2c65cf3c88a43841c9374e5365c12ec914d0` |
| `performance-native-kirin_ui_render_contract.log` | `e6c98384b7ee57913270e27acd980aaa839b29e4d4ef9387c69c820f03a42bf1` |
| `performance-native-kirin_reference_audition_runtime.log` | `27a8075f3466f2c4706915be509970a95f39fc02042f138434f530d89d294c37` |

実DAW、Windows／ARM64の現候補、利用者本人の日常操作・品位はG3と本人確認で受け入れる。友人確認は公開後G4だけである。fixtureのCPU値を未実施の実機確認の成功へ流用しない。

最大DPIの追加修正と実DAW操作は[G3記録](../hypha_drum_psr_g3_20261008/README.md)に分ける。本記録の修正前の強制描画値を、追加修正後や実DAWの値へ置き換えない。
