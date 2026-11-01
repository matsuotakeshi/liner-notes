# Liner notes 更新手順

0.70から0.71への更新を含む、現在のports配置用の手順です。新規構築はINSTALL.mdを参照してください。設定・データの自動移行、サービスの自動再起動は行いません。

## 1. 停止とバックアップ

接続者へ終了予定を案内し、転送終了を待ってからrootで停止します。外部から起動した転送ツールも終了させます。

```sh
service liner_notes stop
service liner_notes status
```

statusが停止を示すことを確認します。旧版のports配布物・pkgを保管してください。データに加え、`/etc/rc.conf`、`/usr/local/etc/liner-notes/`、`/usr/local/share/liner-notes/`、`/var/log/liner-notes/`も別途バックアップします。変更したHELP・メニュー・挨拶はshareの下にあります。

標準配置のSQLite運用の場合、データの論理バックアップ例は次のとおりです。保存先はデータディレクトリの外です。実際のボード・会員・DBの名前に合わせてください。

```sh
backup_dir="/root/liner-notes-backup-$(date +%Y%m%d-%H%M%S)"
install -d -m 700 "$backup_dir"
liner-notes-admin db-backup --root /var/db/liner-notes \
  --database /var/db/liner-notes/liner.sqlite \
  --board notes.dat --members members.dat "$backup_dir/data.tar.gz"
liner-notes-admin verify "$backup_dir/data.tar.gz"
tar -czpf "$backup_dir/system-files.tar.gz" -C / \
  etc/rc.conf usr/local/etc/liner-notes usr/local/share/liner-notes var/log/liner-notes
```

SQLiteを使わない場合は`db-backup`を`backup`に変更し、`--database`を省略します。標準外の場所にある保管庫・添付・設定も必ず保存します。DB形式も完全に同じ状態へ戻したい場合は、停止中のデータディレクトリ全体も別コピーで保管してください。論理`db-restore`は互換形式のschema 1を作ります。

## 2. プログラムを更新

新しい配布物を別ディレクトリへ展開してSHA256SUMSを確認します。0.70と0.71は同じインストール配置です。旧パッケージを外してから、新しいportを導入します。依存パッケージのautoremoveは不要です。

```sh
pkg delete liner-notes
cd liner-notes-ports-stage71/comms/liner-notes
make BATCH=yes OPTIONS_SET=ZMODEM \
  DISTDIR="$(realpath ../../distfiles)" install clean
```

上の`cd`は新しい配布物を展開した場所で実行します。配布物のpkgを使う場合は、portのmakeに代えて`pkg add packages/liner-notes-0.71.pkg`を配布物のルートで実行します。旧パッケージの除去はデータディレクトリの削除ではありません。編集済みの設定・メニュー・HELPはportsの`@sample`で保持します。更新中は停止したままです。

## 3. 設定と外部文書を確認

```sh
liner-notes-admin upgrade-check \
  --assets /usr/local/etc/liner-notes \
  --assets /usr/local/share/liner-notes \
  --database /var/db/liner-notes/liner.sqlite
```

SQLiteを使わない場合は`--database`を省略します。この操作はファイルを上書きしません。

- `matches-sample`: 配布内容と同じです。
- `review-required`: 現在の内容と新しい`.sample`が違います。変更理由は自動判定しません。差分を確認して必要な行だけ取り込みます。
- `missing`: 対応する実ファイルがありません。必要な設定・文書ならsampleから配置します。
- `unsafe`: リンク等のため自動で読みません。設置者が確認します。

今回の読書中`S`の説明は`menus/reader-*.txt.sample`と`help/*/read.txt.sample`にあります。rc.confの既存設定はそのまま使用します。**更新のために`liner_notes_database_mode`を変更しないでください。** 既存schema 1/2は維持し、初回構築でstructuredを選んだ新規DBだけがschema 3になります。

## 4. ZMODEMの再検証

0.71の検証ツールは何度でも実行できます。既存記録を先に削除・移動する必要はありません。

```sh
su -m liner_notes -c \
  '/usr/local/bin/liner-notes-check-resume /usr/local/bin/lrz /usr/local/bin/lsz --receipt /var/db/liner-notes/lrz-resume.json'
```

実転送検証に成功した後、同じ内容なら`receipt_status: unchanged`、初回は`created`、変更時は`updated`を表示します。更新前の記録は同じディレクトリの`.bak-日時-識別子`へ残し、新しい記録を一括で置換します。テスト失敗や置換失敗時は旧記録を保持します。リンク・不適切な権限の記録は拒否します。lrzを変更後にテストが失敗した場合、残っている旧記録を新しいlrzの検証済み証拠として使用できません。

## 5. 起動と確認

```sh
service liner_notes start
service liner_notes status
tail -n 50 /var/log/liner-notes/daemon.log
```

ログイン、既存記事・メールの閲覧、プロフィール、保管庫一覧、実際のファイル転送を確認します。試験用会員で投稿する場合は、運用上問題のないボードを使ってください。失敗した状態でバックアップを削除しません。

## 戻す場合

サービスを停止し、旧パッケージと更新前の設定・データ一式を組で復元します。現状のデータも別途保存してから戻します。復元ツールは空の別ディレクトリへ出力し、その内容を確認してから手動で切り替えます。

0.70以降の補助設定を保存した会員データは0.69系では読めません。schema 3は0.71以降が必要です。**実行ファイルだけを古くして新形式のデータを開かないでください。** 0.70から0.71への通常更新ではschema 1/2を変換しません。
