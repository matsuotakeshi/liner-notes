# 中断した転送データの管理

## 中断データの管理

サービス所有者または管理者が、外部管理ツールで実行します。公開ファイル・添付の本体には触れず、転送用spoolだけを対象にします。

```sh
liner-notes-admin transfers list --root /var/db/liner-notes/spool
liner-notes-admin transfers restore --root /var/db/liner-notes/spool retained-表示されたID
liner-notes-admin transfers restore --root /var/db/liner-notes/spool retained-表示されたID --apply
liner-notes-admin transfers remove --root /var/db/liner-notes/spool pending-表示されたID
liner-notes-admin transfers remove --root /var/db/liner-notes/spool pending-表示されたID --apply
```

IDはlistの`id`をそのまま指定します。restore/removeは`--apply`がなければ確認表示だけです。

- `active`: 使用中。変更不可。
- `interrupted`: 通信中断。退避済みならrestoreでZRESUMEの対象へ戻せます。同じ会員・名前空間・ボードのpendingがある場合は上書きしません。
- `publication-pending`: 受信済みで登録結果の照合が必要。削除・復帰を拒否し、BBSからZRESUMEで照合・登録を完了させます。
- `invalid`: メタデータやファイル構造が不正。自動削除せず個別確認します。

一覧には会員・保管庫/添付の区別・ボード・容量・経過時間を表示します。リンク、公開権限のあるspool項目、使用中のロックを拒否します。削除に自動的な経過日数判定は設けていません。旧スタンドアロン転送領域も一覧・詳細へ表示しますが、共通ロックがないため自動削除しません。

## 中断データの照合

外部管理ツールの`show`と`reconcile`を使います。`show`はファイル名、サイズ、SHA-256、予約番号、保存済み状態を表示します。`reconcile`は**元の登録会員として認証**し、サーバ側の状態を問い合わせます。管理者という理由で他会員の非公開添付へアクセスする例外は設けません。管理ツールはサービス所有者またはrootが使用します。

```sh
liner-notes-admin transfers list --root /var/db/liner-notes/spool
liner-notes-admin transfers show --root /var/db/liner-notes/spool pending-ID
liner-notes-admin transfers reconcile --root /var/db/liner-notes/spool pending-ID \
  --host 127.0.0.1 --port 2222 --user original_uploader
```

`pending-ID`はlistで表示された完全なIDに置き換えます。接続先・ポート・会員名は記録と完全一致する値を明示します。パスワードは非表示入力で、コマンド行や記録には保存しません。照合にはBBSの稼働が必要です。使用中の項目はロックにより拒否します。

結果は`NOT_STARTED`、`UNKNOWN_START`、`ACTIVE`、`COMMITTED`、`CANCELLED`に分類します。転送予約を新規作成する操作は行いません。

- 通常は照合結果を見て、`--apply`を付けて予約状態の記録を更新し、BBSでZRESUMEします。
- 開始応答が失われ予約番号が不明な場合、FILEAUDIT等で元の予約を確認し、`--bind 'filename=予約番号'`を指定できます。登録会員・名前空間・保存済みファイル名・ボードと、リモートの容量・SHA-256を検査します。ファイル名・ボードはローカル記録同士の整合確認であり、予約番号の選択を自動推測しません。容量・ハッシュが一致しない番号や他会員の番号は拒否します。
- 全ファイルがリモートで`COMMITTED`と確認できた場合だけ、`--cleanup --apply`でローカルの中断領域を回収できます。公開ファイル・添付本体は削除しません。
- `--apply`がなければ保存記録を変更せず、cleanupも確認だけです。
- 旧`liner-zmodem-*`領域もlist/showへ統合しました。旧ヘルパーには共通ロックがないため、この領域の自動削除はしません。元の`upload.json`を使った再開・取消を、旧転送プロセスの終了確認後に行います。

受信途中のpending/retainedは前節の復帰・削除を使います。受信済みの不明データを通常のremoveで消すことは引き続き拒否します。
