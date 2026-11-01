# ZUPLOAD中断後の再試行修正（ports 0.69_2）

## 原因と修正

ZMODEM受信を途中で中断すると、途中再開用のデータがspoolの `pending-*` に残ります。0.69 / 0.69_1のhelperは、このデータがあると次のZUPLOADをlrz起動前に拒否していました。BBS上の表示は一般的な転送失敗だったため、原因を判断できませんでした。同じ会員・接続先・登録先では再接続しても残存データが影響します。

0.69_2では次の動作に変更します。

- `ZUPLOAD`：受信途中のデータを非公開の `retained-*` ディレクトリへ退避し、新しい受信を開始します。既存の中断データにも適用されます。
- `ZRESUME ボード|元の説明`：従来どおり、現在の中断データを使って再開します。ルートボードは `-` を指定し、同じ名前・内容の元ファイルを送信します。
- 公開処理の成否が不明な場合：自動退避せず、元データと公開記録を保持して再開を要求します。二重登録を防ぐためです。
- ほかの接続が転送中の場合：その作業ディレクトリは移動しません。

共通helperを使用する一括アップロードとメール添付にも適用されます。BBS本体・保存形式・lrzszの変更はありません。

退避データは自動削除しません。`ZRESUME`の対象は現在の `pending-*` であり、`retained-*` は管理者による回収用です。新規受信を選んだ後は、以前の途中データへZRESUMEで戻ることはできません。spoolの使用量を確認し、不要になった退避データだけを後日整理してください。

## 0.69 / 0.69_1への最小修正

再ビルドを待たずに、同梱の `hotfix/vault_zmodem.py` だけを更新できます。portsを標準の `/usr/local` へインストールした環境用です。サービス停止中に旧ファイルを保存してから入れ替えます。会員・記事・ファイル・中断データは変更しません。接続中の利用者は切断されます。

サーバーへ `liner-notes-ports-stage69-fix1.tar.gz` を転送し、rootで次を実行します。途中で失敗した場合は、その原因を確認してから再開してください。

```sh
sh
tar -xzf liner-notes-ports-stage69-fix1.tar.gz
cd liner-notes-ports-stage69-fix1
(
  set -eu
  test "$(sha256 -q hotfix/vault_zmodem.py)" = "$(cat hotfix/SHA256)"
  target=/usr/local/share/liner-notes/scripts/vault_zmodem.py
  test -f "$target"
  backup=$(mktemp -d /var/backups/liner-notes-zupload.XXXXXXXX)
  cp -p "$target" "$backup/vault_zmodem.py"
  echo "Backup: $backup"
  service liner_notes stop
  install -o root -g wheel -m 644 hotfix/vault_zmodem.py "$target"
  service liner_notes start
)
```

この方法ではpkgの登録版は変わりません。後から0.69_2のportsを入れると同じ修正が正式に含まれます。古いパッケージを再インストールすると修正も戻るので注意してください。復旧時はサービスを停止し、表示されたbackupのファイルを同じtargetへ戻して起動します。

## portsによる更新

同梱の `comms/liner-notes` と `distfiles` を使用します。新規導入手順はINSTALL.md、既存環境の更新手順はPORTS.mdを参照してください。今回の版は0.69_2です。lrzszの再インストールやデータ移行は不要です。設定・表示ファイルの既存内容は保持されます。

## 検証範囲

FreeBSD 15.1/amd64の専用検証環境で、実際のTelnet接続とlrzszを使って中断を再現しました。修正前は2回目のZUPLOADでlrzが起動せず、修正後は同じ接続のまま2回続けて新規アップロードが成功し、保存内容のSHA-256が一致しました。中断データも退避先で一致しています。

関連する途中再開・一括転送・メール添付・公開結果不明時の再試行とports検査の結果は `evidence/zupload-fix/` に収録します。本番bbs.example.orgへの適用は行っていません。
