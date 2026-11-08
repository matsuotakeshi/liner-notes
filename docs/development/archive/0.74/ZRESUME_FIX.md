# ZRESUMEと長時間アップロードの修正（ports 0.69_3）

## 変更内容

- 引数なしの `ZRESUME` は、現在選択している保管庫の中断データを探し、保存済みの説明文を使用します。別の保管庫は `ZRESUME apps` のように指定できます。`ZRESUME apps|説明` では従来どおり説明文の一致も確認します。ルートは `-` です。現在の保管庫以外を勝手に選ぶことはありません。
- `ZUPLOADBATCHRESUME` も説明文を省略できます。単一／一括の区別、会員、登録先、権限、受信プログラムの検証は引き続き必要です。
- 受信タイムアウトは「転送開始から120秒」から「ファイルサイズに変化がない状態が120秒」に変更しました。受信が進む限り継続します。`--timeout` を指定した専用helperも受信時はこの無進捗時間として扱います。送信側のhelper時間制限は従来どおりです。
- BBS側の10分・1時間の経過時間制限も受信を打ち切らないよう整理しました。helperが通信・登録処理ともに進めなくなった場合の10分監視は維持しています。
- 再開データなし、説明／単一・一括の不一致、再開検証記録なし・不一致、タイムアウトを日本語／英語で区別して表示します。daemonの標準エラーにも `ZMODEM failure: NO_PENDING / MISMATCH / RECEIVER / TIMEOUT` を記録します。パスワードや本文は記録しません。

端末ソフト側の転送終了後も、BBSは受信データの検証と保管庫への登録を続けます。大容量ではこの後処理にも時間がかかります。BBSの登録完了メッセージが出るまで切断・再送せず待ってください。

0.69_2の「新規ZUPLOAD時に中断データを退避する修正」も含みます。退避済みの `retained-*` はZRESUMEの検索対象ではありません。以前の中断データを続行したい場合は、新規ZUPLOADを挟まずZRESUMEを使用してください。

## 既存portsからの更新

今回はBBS本体とhelperの両方を変更しています。0.69_2用の1ファイル差し替えだけでは適用できません。データ移行やlrzszの入れ直しは不要です。手元の旧配布物とバックアップは保持してください。

サーバーへ `liner-notes-ports-stage69-fix2.tar.gz` を転送し、rootで次を実行します。csh/tcshの場合は先に `sh` を起動してください。まず稼働中にビルドを済ませます。

```sh
sh
tar -xzf liner-notes-ports-stage69-fix2.tar.gz
cd liner-notes-ports-stage69-fix2/comms/liner-notes
make BATCH=yes OPTIONS_SET="DOCS ZMODEM" \
  DISTDIR="$(realpath ../../distfiles)" all
```

成功したらメンテナンス時間にサービスを止め、現在のデータ・設定を通常のバックアップ手順で保存して更新します。停止すると接続中の利用者は切断されます。以下は標準配置用です。バックアップ先にデータ一式が収まる空き容量を確保してください。

```sh
(
  set -eu
  backup=$(mktemp -d /var/backups/liner-notes-0.69_3.XXXXXXXX)
  echo "Backup: $backup"
  service liner_notes stop
  tar -czf "$backup/data-config.tar.gz" -C / \
    var/db/liner-notes usr/local/etc/liner-notes
  cp -p /etc/rc.conf "$backup/rc.conf"
  make BATCH=yes OPTIONS_SET="DOCS ZMODEM" \
    DISTDIR="$(realpath ../../distfiles)" deinstall install clean
  service liner_notes start
  pkg info liner-notes
)
```

途中で失敗するとその位置で停止します。原因を確認してから復旧・再開してください。既存設定と編集済みのHELPは保持されます。HELPの追加説明を取り込む場合は `help/ja/files.txt.sample` / `help/en/files.txt.sample` と比較してください。

## 再開検証のエラーが表示された場合

修正済みlrzszであっても、検証記録がない／バイナリ交換後に古くなっていると再開を拒否します。標準配置では管理者が次を実行します。

```sh
su -m liner_notes -c '/usr/local/bin/liner-notes-check-resume /usr/local/bin/lrz /usr/local/bin/lsz --receipt /var/db/liner-notes/lrz-resume.json'
```

成功を確認した後、BBSで `ZRESUME` を再実行し、同じ名前・同じ内容の元ファイルを送信します。検証に失敗した場合はその出力を確認してください。検証記録を手作業で作成して回避しないでください。

「中断データがありません」の場合は登録先を確認します。直接の `ZUPLOAD` はルートへ登録します。保管庫メニューの `U` は現在の保管庫を使用します。ルートの中断データは `ZRESUME -` で指定できます。

## 検証記録

FreeBSD 15.1/amd64で実Telnet・lrzszを使い、引数なし再開、説明文の不一致、検証記録の欠落、中断データなし、タイムアウト、従来の一括／メール添付転送を検証します。指定されたThunderbirdインストーラは転送データとしてのみ使用し、実行・配布物への同梱は行いません。実ファイルのサイズ・SHA-256・所要時間とports検査結果は `evidence/resume-fix/` に記録します。本番への適用は行っていません。
