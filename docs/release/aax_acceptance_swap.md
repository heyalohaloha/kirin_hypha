# macOS AAX の受入用差し替え

`aax_swap.mjs` は、manifest の PRE / POST の導入先だけを候補へ入れ替え、受入後に元の二つへ戻す。
標準の管理者確認は一度だけ。同じ固定 shell worker が復元と整理まで担当する。
公開 PKG に postinstall は追加しない。この成功は DAW のロード・音・保存・正式受入を代用しない。

## 準備と計画

すべての AAX host を終了する。候補は source / producer の確認を済ませた PRE / POST を使う。
[設定例](aax_swap_config.example.json)を repository 外の本人専用の場所へ保存し、0600 とする。
新しい receipt の保存先も用意する。設定と receipt の拡張 ACL、既存 receipt の上書きは拒否する。

利用者入力の `source` は絶対 path とし、空白・改行などの whitespace を含む path は計画時に拒否する。
必要なら利用者権限で空白のない private な場所に候補を用意し、そこで hash を取得する。
manifest の固定 install path と bundle 内の名前は shell で quote して保持する。bundle 内の改行・CR・tab は拒否する。
対象 path を設定で変更することはできない。候補も原本も symlink、hardlink（通常 file の link 数が1以外）、
特殊 file、危険な mode、拡張 ACL、未対応の属性があれば停止する。署名 link を含む公開版も対象外で、
その場合に link を外したり、公開版の署名や権限を書き換えて通してはならない。
公開版へ戻す作業は、その原本がこの無リンク条件を満たす場合だけ使える。

```bash
node scripts/acceptance/aax_swap.mjs --hash PRIVATE_PRE_BUNDLE
node scripts/acceptance/aax_swap.mjs --hash PRIVATE_POST_BUNDLE
node scripts/acceptance/aax_swap.mjs --plan PRIVATE_CONFIG --receipt NEW_PRIVATE_PLAN_JSONL
```

計画は利用者権限の Node と descriptor 相対の読取専用 scanner で、候補・原本・祖先を検査する。
scanner の Python は利用者権限だけで動く。root では Python、Perl、Node、CLT framework を実行しない。
設定の hash は名前・mode・file bytes に結ぶ既存の bundle hash。別途、全 xattr と shell と共有する tree hash を記録する。
対応する xattr は FinderInfo、ResourceFork、quarantine、OS provenance（試験では限定した fixture 属性も使用）。
1属性16 KiBを超えるものは認証前に拒否する。OS provenance は新設した file と一致する場合だけ受け入れ、強制変更しない。
属性を黙って落として通さない。

## 実行と復元

```bash
node scripts/acceptance/aax_swap.mjs --run PRIVATE_CONFIG --receipt NEW_PRIVATE_RUN_JSONL
```

認証前に候補・原本・転送する bytes・固定 shell 本文と全 command の SHA-256 を receipt に保存する。
認証直前に本文と hash を再照合し、標準管理者確認を一回だけ要求する。password は macOS が扱い、道具は読まず保存しない。
root の環境は `/usr/bin/env -i` で空にし、PATH は `/usr/bin:/bin:/usr/sbin:/sbin` に固定する。
`LC_ALL=C`、`HOME=/var/empty`、`OPENSSL_CONF=/dev/null` も固定し、shell の初期化や OpenSSL の外部設定を読ませない。

root は利用者の source path を受け取らない。認証前に確定した snapshot bytes を固定した FIFO で受け、
転送全体の hash を確認してから root 専有の inbound tree に新設する。
その私有 tree から `ditto` で候補 staging へ写し、コピーの全 tree を `openssl dgst -sha256` で計算する。
C の byte 順で、全 directory・通常 file の名前、mode、file bytes の SHA-256、属性名と属性 bytes の hex 表現の SHA-256 を結ぶ。
隠し項目も含む。計画の tree hash と一致するまで原本を動かさない。

導入先と同一 volume の `/Library/Application Support/Kirin/Hypha/acceptance-swap/` を root:wheel 0700 で使い、
session ごとに唯一の原本と journal を保存する。`/tmp`、`/private/tmp`、`/var/folders` は使わない。
通信だけは隣の `acceptance-swap-ipc/` 内に root だけが書ける 0711 の session を設け、request FIFO だけを利用者所有にする。
原本と全 tree の device を照合し、別 volume や入れ子の mount は拒否する。原本の移動は専用領域への rename。Avid の親や他社 bundle の owner/mode は変えない。
同時実行は専用領域の排他的な `active` lock で拒否する。

`Candidate PRE/POST installed` が出たら host で受入する。host を終了して Enter を押すと、
両控えと両導入物の hash を先に確認し、同じ worker が原本へ戻す。
戻した両 bundle の hash が一致した記録と private receipt の保存を確認してから、候補コピー・journal・通信を整理する。
原本を含む可能性のある session を先に削除しない。

途中失敗・入力終了・TERM では元へ戻すか、進んだ intent と状態を永続 journal に残して停止する。
受信は一回4096 bytes、転送は10分、操作待ちは12時間を上限とし、2秒ごとに親 process の生存を確認する。監視用の常駐 process は作らず、他 process に終了 signal を送らない。強制終了・OS停止からの自動復元を保証するものではない。
復元不能、控え改変、receipt 保存失敗では session と lock を残す。receipt の復旧場所と journal を保全し、
実 file の hash と進んだ rename を確認するまで再実行・削除・lock 解除をしない。
復旧には別途許可された管理者作業が必要になる場合がある。
復元後の host 再起動では、読み込んだ path と hash を担当者が確認する。

## root が使う system の道具

sealed read-only な system volume、SIP と authenticated root が有効な macOS を必要とする。
認証前と固定 root script の冒頭にこれを確認し、全実行 file が symlink ではなく、`/` と同じ device の root 所有の通常 file であることを検査する。
ユーザーが書ける tool や Data volume の interpreter へ fallback しない。root shell の外部 command は次の絶対 path だけ。

|用途|実行 file|
|---|---|
|固定環境・shell|`/usr/bin/env`、`/bin/sh`|
|SIP・SSV・属性の読取|`/usr/bin/csrutil`、`/sbin/mount`、`/usr/bin/id`、`/usr/bin/stat`、`/bin/ls`、`/usr/bin/grep`|
|列挙・並べ替え・hash・bytes|`/usr/bin/find`、`/usr/bin/sort`、`/usr/bin/openssl`、`/usr/bin/base64`、`/usr/bin/tr`|
|私有領域のコピー・属性|`/usr/bin/ditto`、`/usr/bin/xattr`|
|専用領域と rename・整理|`/bin/mkdir`、`/bin/rmdir`、`/bin/mv`、`/bin/rm`|
|新設物の権限・FIFO|`/bin/chmod`、`/usr/sbin/chown`、`/usr/bin/mkfifo`|
|journal 同期・監視|`/bin/sync`|

そのほかは固定した shell 関数と組込み（read、printf、trap、kill -0 等）だけを使う。
ACL は一つの path に対する `ls -lde` の全出力で検査する。mode 表示の `+` だけに頼らず、
xattr の `@` が優先される場合も追加の ACL 行を拒否する。祖先と全隠し項目も同じ検査を通す。
