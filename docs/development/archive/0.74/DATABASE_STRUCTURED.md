# 項目別テーブルへの別コピー移行

Stage73の `liner-notes-admin db-structure` は、停止中のSQLite保存領域を
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
確認を行ってから手動で実施します。[UPGRADING.md](UPGRADING.md)も参照してください。
