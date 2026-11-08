# バックアップ・復元・データ整理

<a id="backup-restore"></a>

## 初期バックアップと復元確認

SQLite運用では `db-backup` を使います。`liner.sqlite`だけでは保管庫・添付の実ファイルや外部設定を保存できません。次の例はサービス停止後、データと補助ファイルを同じ日時のディレクトリへ保存します。途中で失敗した場合は停止したままになるので、出力を確認してから再開してください。

```sh
(
  set -eu
  backup_dir="/var/backups/liner-notes/$(date +%Y%m%d-%H%M%S)"
  install -d -m 700 "$backup_dir"
  web_was_running=NO
  if service liner_notes_web onestatus >/dev/null 2>&1; then
    web_was_running=YES
    service liner_notes_web stop
  fi
  service liner_notes stop
  if service liner_notes onestatus >/dev/null 2>&1; then echo 'BBS is still running'; exit 1; fi
  if service liner_notes_web onestatus >/dev/null 2>&1; then echo 'Web is still running'; exit 1; fi
  liner-notes-admin db-backup --root /var/db/liner-notes \
    --database /var/db/liner-notes/liner.sqlite \
    --board notes.dat --members members.dat "$backup_dir/data.tar.gz"
  liner-notes-admin verify "$backup_dir/data.tar.gz"
  tar -czpf "$backup_dir/support.tar.gz" -C / \
    usr/local/etc/liner-notes usr/local/share/liner-notes \
    var/log/liner-notes etc/rc.conf
  tar -tzf "$backup_dir/support.tar.gz" > "$backup_dir/support-files.txt"
  if [ -d /var/db/liner-notes-web ]; then
    tar -czpf "$backup_dir/web-spool.tar.gz" -C / var/db/liner-notes-web
  fi
  printf 'バックアップ先: %s\n' "$backup_dir"
  service liner_notes start
  if [ "$web_was_running" = YES ]; then service liner_notes_web start; fi
)
```

rc.confには他サービスの設定も含まれるため、バックアップは管理者だけが読める場所で保管します。追加の設定ファイル・外部ゲーム・独自helper・別のデータ領域があれば、それらも対象に加えます。公開後も同じ手順で定期的に取得し、別の機器にもコピーしてください。

復元は**存在しない新しいディレクトリ**へ行います。以下の `YYYYMMDD-HHMMSS` は実際のバックアップ日時に置き換えてください。

```sh
liner-notes-admin db-restore \
  /var/backups/liner-notes/YYYYMMDD-HHMMSS/data.tar.gz \
  /var/db/liner-notes-restorecheck
```

終了コード0を確認し、復元先を本番とは別のポート・設定で起動して記事や会員を確認します。転送helperはこの確認では有効にしません。有効にする場合はspoolも本番から分離します。復元ファイルは実行したユーザーの所有になるため、起動前に**復元先だけ**をサービスユーザーへ渡します。本番のrc.confはこの確認のために変更しません。設定・表示ファイルはsupport.tar.gzを別の作業ディレクトリに展開して確認します。

DBには設定先がデータ領域外の論理ログも含まれます。これらは復元先の `external/元の絶対パス` 以下に対応するため、別の場所で起動するときはUSERLOGとチャットログの指定も合わせます。具体的な移行・復元・整理の仕様は [DB保存仕様](storage.md) を参照してください。元データやバックアップを先に消さないでください。

**0.74.1の `db-restore` / `db-compact` は元の保存形式を維持します。** blocks／structuredの起動モードを復元元に合わせてください。別形式へ変更する場合は、停止中の別コピーを対象に `db-structure` を使います。[保存・復元形式の仕様](storage.md)を参照してください。

記事・会員の読出しを確認するため、復元コピーを前景で起動する例です。12222が未使用であることを確認して実行します。再帰的な所有者変更の対象は、直前に作成した復元コピーだけです。

```sh
sockstat -4l -P tcp -p 12222
chown -R liner_notes:liner_notes /var/db/liner-notes-restorecheck
su -m liner_notes -c '/bin/sh -s' <<'SH'
set -eu
cd /var/db/liner-notes-restorecheck
exec /usr/local/sbin/liner-notes --database /var/db/liner-notes-restorecheck/liner.sqlite \
  --bind 127.0.0.1 --telnet --menu --lang ja \
  --menu-dir /usr/local/share/liner-notes/menus \
  --help-dir /usr/local/share/liner-notes/help --user-log - --chat-log - \
  12222 /var/db/liner-notes-restorecheck/notes.dat /var/db/liner-notes-restorecheck/members.dat
SH
```

別のSSH接続から `telnet 127.0.0.1 12222` で確認し、終了後は起動側の端末でCtrl-Cを送ります。この確認では保管庫・外部ゲーム・転送helper・履歴ログの記録を有効にしていません。通常運用へ復元するときは、それぞれの復元先と設定も合わせます。

## 項目別テーブルへの変換

`liner-notes-admin db-structure` は、停止中のSQLite保存領域を
新しいディレクトリへ変換します。元のDB、会員、添付、保管庫は変更しません。
稼働中DBのロックを取得できなければ失敗します。本番サービスの停止や切替は
このコマンドでは実行しません。

```sh
liner-notes-admin db-structure \
  --root /var/db/liner-notes \
  --database /var/db/liner-notes/liner.sqlite \
  --board notes.dat --members members.dat \
  /backup/liner-notes-structured
```

出力先は未作成のディレクトリを指定します。入力と出力の両方を置ける空き容量が
必要です。変換は、既存のC側レコード検証処理を使用します。記事・メール・会員と
対応履歴をschema3のテーブルに格納し、元の全ストリームと再構築した内容の
SHA-256を照合します。対応外ストリームはblocksに保持します。ファイル本体は
従来どおり外部ファイルです。「すべてのファイルをSQL化する」変換ではありません。

失敗した出力には `.liner-maint-incomplete` が残ります。起動せず、エラーを
解消して新しい出力先で再試行してください。既存出力先への上書きは拒否します。
低水準の `liner-notes-dbconvert` は管理ツール用であり、単独実行しません。

structured形式から作った `db-backup` は、形式情報もアーカイブに含めます。
`db-restore` と `db-compact` はその情報に従い、structured形式を保持します。
形式情報を持たない古いバックアップは既定でblocks形式へ復元します。
古いバックアップを別コピーのstructured形式へ復元する場合は `--structured`
を追加します。旧ファイル形式からの `db-migrate` にも同じ指定ができます。

復元後は別ポート・ループバック待受でログイン、記事、メール、保管庫、添付と
管理権限を確認してください。正式切替は停止、別途バックアップ、設定と所有者の
確認を行ってから手動で実施します。[バージョンアップと切り戻し](upgrade.md)も参照してください。

## 整理と本文回収

SQLite運用では `liner-notes-admin db-compact バックアップ.tar.gz 新しい出力先` を使用します。`--all-stores` は会員・プロフィール・設定・保管庫・添付等を現在状態へまとめ、`--reclaim-withdrawn` は取り消し記事の本文を整理先で回収します。記事ID・返信関係・移動対応は維持します。HIDEだけの記事本文は回収しません。

ログの期間整理は `--keep-log-days 90 --log members.dat.userlog` のように対象を明示します。対象はUSERLOG、取得、チャット、活動、閲覧・投稿ログで、設定・メール等の状態ジャーナルをログとして削ることはできません。`--log` は保存ルートからの相対パスです。0日指定は日付削除なしですが、ボード側の件数制限は別に適用します。

USERLOGの累計利用時間・回数、ファイル取得数は集計を保持します。Gatherの保持件数は `--gather-count`。業務上の記事・メール・ファイルIDは再利用しません。内部ログ連番や接続番号は整理で変わる場合があります。転送ワーカーを停止してから整理してください。

削除が確定していない未使用らしい添付、受信中・用途不明のデータを推測で消しません。終了会員の古い認証・プロフィールは整理できますが、番号とログイン名は永久予約です。元データとバックアップには過去本文が残るため、別途保存期限を管理します。

## 旧Liner Notes形式との間で移す

`db-migrate` は旧Liner NotesファイルからSQLiteへ、`db-export` はSQLiteから検証可能なファイルへ書き出します。旧RTBBSの変換機能ではありません。SQLite運用に旧 `backup` / `compact` を代用せず、`db-backup` / `db-compact` を使います。`--help` で実際のファイル名・ルート・出力先を確認し、新しいコピーへ出力してください。

## 復元先の扱い

復元先は未作成のディレクトリを指定し、既存データとマージしません。ディレクトリは0700、通常ファイルは0600、実行可能ファイルは0700です。所有者は実行ユーザーになり、元のACL・所有者・更新日時を自動復元しません。

失敗時の `.liner-maint-incomplete` がある出力は運用しません。runtimeの自動停止保証ではなく管理ツールの目印です。元データを保持して原因を解決し、別の新しい出力先で再試行します。設定内の絶対パス・PID・ログが本番を指さないよう確認して試運転します。

`verify` のハッシュ一致は保存バイト列の完全性で、業務上の整合や第三者の改ざんがないことを署名で保証するものではありません。復元後の実利用確認を必ず組み合わせます。外部配置のログ・設定・ゲーム・リンクDBは自動追跡されないため別途保存します。
