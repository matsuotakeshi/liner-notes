# アンインストール

まずWeb・共有ボード・COM・BBS本体と、それらを自動再起動する監視を止めます。[バックアップと復元](backup-recovery.md)で、会員・記事・メール・添付・保管庫・設定・外部文書・ゲームのデータを保存してください。

FreeBSDでは `sysrc liner_notes_enable=NO`、Webを使用する場合は `sysrc liner_notes_web_enable=NO` を設定し、停止を確認してから `pkg delete liner-notes` を実行します。lrzszやApacheを他用途でも使用している場合、それらまで削除する必要はありません。

Linux RPMでは使用中の `liner-notes`、`liner-notes-web`、`liner-notes-link` を `systemctl disable --now` で停止・無効化し、`dnf remove` に実際に導入したパッケージ名を指定します。削除予定の一覧を確認してください。ソース配置にはRPMの削除管理がありません。導入時の配置記録と `install-linux` の出力に基づいてプログラムとunitを扱います。

アンインストールは会員・記事・保管庫・ゲームの消去操作ではありません。変更済み設定は残存するか `.rpmsave` になる場合があります。サービスアカウントとデータを再利用する可能性がある間は保持します。データの完全削除を望む場合も、別媒体の復元確認が済むまで削除しないでください。
