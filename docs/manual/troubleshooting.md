# 障害の切り分けと復旧

<a id="troubleshooting"></a>

## 読み取り専用の設定診断（次期版）

`liner-notes-doctor` は次期版向けに追加された外部管理コマンドです。公開済み0.74.1には含まれません。本体の機能を起動せず、設定・保存先・実行権限・補助プログラムを調べます。

```sh
liner-notes-doctor --check-all
liner-notes-doctor --check-config --check-database --lang en
liner-notes-doctor --check-all --json > doctor-report.json
liner-notes-doctor --check-selinux --require-selinux
```

| 選択 | 検査内容 |
| --- | --- |
| `--check-config` | OS/CPU、Python/SQLite/OpenSSL、パッケージ版、実行ファイル、Linux/FreeBSDの起動プログラム照合、カーネル上の待受、limits、Web設定・外部文書 |
| `--check-database` | SQLiteの一時コピーに対するquick_check、保存スキーマ、外部キー。原本のSQLite接続は作らない |
| `--check-links` | TOML、共有ID・peers/origins、Ed25519識別鍵と証明書、有効期限、リンク会員許可リスト、リンクDB・管理ソケット |
| `--check-doors` | doors.confの項目・ID重複・同時枠・DORINFO1設定、実行/作業パス、shebang、Clansラッパーの基本依存ファイル |
| `--check-transfers` | helper、lrz/lsz、保管庫・spool、ディスク空き容量、受信バイナリと再開検査記録のSHA-256照合 |
| `--check-permissions` | サービスアカウントのUID/グループ、モード、親ディレクトリの通過権、データの保護 |
| `--check-selinux` | Linuxのモード、専用ポリシー、稼働中本体/Web/リンクのドメイン、設定対象のファイルラベルとローカルマッピング |

終了コードは **0＝検査成功、1＝警告・判定保留、2＝設定/データ/権限エラー** です。引数の誤りも2です。JSONは`schema: 1`、`read_only: true`、`exit_code`と`results`を持ち、各結果には`check`、`code`、`status`、`location`、`message`、`remedy`、`detail`があります。`--lang ja|en`で原因と改善方法を切り替えます。警告を無条件に正常として扱わず、`code`と改善方法を確認してください。

FreeBSDは配布`liner_notes.conf`、`/etc/rc.conf`、`/etc/rc.conf.local`の順、Linuxは`/etc/liner-notes/liner-notes.env`を静的に読みます。シェルのsource/eval、コマンド置換、設定されたバイナリの実行はしません。動的設定は警告となるため、必要に応じて`--config`、`--program`、`--data-dir`、`--database`、`--service-user`で対象を明示します。`--prefix`はソース導入の配置変更、`--config-dir`は設定ディレクトリ変更に使用します。

未設定のWeb・リンク・Door・転送は正常な「対象外」です。設定された本体オプションとLinuxのサービス有効状態から検査対象を判断します。停止中のオプション機能を調べる場合は`--enable web|links|doors|transfers`、`--web-config`、`--link-config`、`--doors-config`を指定します。`--root`と`--os Linux|FreeBSD|SunOS`で配置済みの別ルートを調べる場合、ホストのプロセス・パッケージ照会は行いません。

DB本体とWALだけを権限0700の診断用一時ディレクトリへコピーし、そのコピーを検査・削除します。原本へSHM/WALやロックファイルを作りません。コピー中・検査後のファイル変更、残存rollback journal、時間/容量制限では警告にして判定を保留します。既定はコピー対象合計1GiB、30秒です。`--max-database-bytes`と`--database-timeout`で調整できます。稼働中に変化するDBの整合性を断定せず、停止済みの整合バックアップで再検査してください。旧ジャーナル保存は存在・権限の検査に留め、バックアップに対して管理ツールのverifyを使います。

診断は設定変更、DB更新、再開検査の再実行、ゲーム起動、サービス停止/再起動、外部への接続をしません。秘密鍵・パスワード・設定原文を結果へ出しません。読めない項目は権限エラー/警告です。ACL、systemd sandbox、外部ゲーム独自のライブラリ/環境、リンク会員の認証・停止状態とボードACLは静的検査だけで確定できないため、判定範囲を結果に明記します。診断成功は実際のログイン・ゲーム実行・転送成功の代替ではありません。

SELinux専用ポリシーがない状態と`unconfined_service_t`で動作する状態は、専用ドメインによる制約の成功とは区別します。`--require-selinux`はEnforcingと専用ポリシーを必須にするCI向け指定です。SELinuxのない非Linux OSでは対象外です。SolarisのJSON設定診断はPython 3.11以上が必要で、ネイティブ対応状況は[対応環境](overview.md)に従います。

## 起動と接続のトラブル

まず実行中のホスト名、待受、ログを確認します。

```sh
hostname
service liner_notes status
sockstat -4l -P tcp -p 2222
tail -n 80 /var/log/liner-notes/daemon.log
```

| 状況 | 確認する箇所 |
| --- | --- |
| Connection refused | BBSを起動したホストか、ポートが同じか、serviceが起動しているか |
| サーバー内は接続可能だが外から接続できない | bind、TCP許可、NAT、DNS。IPv6の接続先を案内していないか |
| 識別表示だけでメニューが出ない | rc起動を使用。直接起動する場合は `--telnet --menu` が必要 |
| usageを表示して終了 | コマンドが二重に貼り付いていないか。引数・ポート番号を確認 |
| Invalid configuration | 指定パス、admins.confの読み取り権限、旧homeの指定、設定値の綴り |
| Permission denied | データ・spool・ログの所有者。全体を777へ変更しない |
| 管理者メニューがない | admins.confはログイン名でなく会員番号。再起動と再ログイン |
| ZMODEMを開始できない | helperの絶対パスと実行権限、検証記録、端末のBINARY設定 |
| db-backupが失敗 | サービスの停止、DB指定、出力先がデータ領域外か、空き容量 |

`listen setup failed`は、詳細にある操作名・アドレス・ポート・OSエラーを確認します。
`bind ... Address already in use`は別プロセスの待受や停止後に残ったTCP接続、
`Permission denied`は起動ユーザーのポート使用許可、`Can't assign requested address`は
bind先IPを確認します。旧版の短いエラー表示だけでは原因を区別できません。
この版ではUNIXの待受に`SO_REUSEADDR`を指定し、正常停止後のTCP接続がTIME_WAITに
残っても同じポートで再起動できるようにしています。稼働中の同一アドレス・ポートへの
二重起動は許可しません。旧版から最初に切り替える際は、旧接続の状態解消を待つ必要が
残る場合があります。mac_portaclの許可はUIDに対する設定なので、実行ファイルの
パス変更だけで許可が失われることはありません。

FreeBSDのサービス管理の基本は [FreeBSD Handbookの設定とサービス](https://docs.freebsd.org/en/books/handbook/config/#configtuning-rcd) を参照してください。本マニュアルの設定名は同梱のrcスクリプトに対応しています。

## ログイン・登録・認証

登録方式がclosedか承認待ちか、会員が停止中か、パスワード変更で他接続が失効したかを確認します。管理者はMEMBER/RIGHTS/APPLICATIONを使い、パスワードをログやコマンド行へ出しません。レベル5・sysop名だけでは全体管理者になりません。admins.confの数値IDと読み取り権限を確認してください。

## 転送が止まる・再開できない

端末の完了後もBBS登録処理が続く場合は最終結果を待ちます。`NO_PENDING` は中断データと登録先、`MISMATCH` は説明・単体/一括・名前/サイズ/内容、`RECEIVER` はlrzszと再開検証記録、`TIMEOUT` は無通信の原因を調べます。ZUPLOAD直指定はルートなのでZRESUME -を確認します。

新規ZUPLOADは途中データを退避して新しく受けます。旧データを続けたい場合はZRESUMEを使い、retainedを戻すときは[中断データ管理](transfer-admin.md)を使います。公開成否不明のデータを手で削除しません。lrzsz交換後の再開検証は既存receiptがあっても再実行可能です。

WebはHTTP受信制限、spool空き容量、BBS接続、BBS側権限・上限、最終ハッシュ照合を順に確認します。ApacheのLimitRequestBodyはmultipartの余白も必要です。最大ファイルそのものと同じ値にするとHTTP本文が超過します。

## WebUI

`service liner_notes status` と `liner_notes_web status` を両方確認し、web.tomlのbbs.portと実待受を合わせます。バックエンドへのcurlには公開originに合うHostヘッダを付けます。403のUnexpected Host、Apache自身のForbidden、BBS connection unavailableを区別してください。

テンプレートのディレクトリは通常0755、公開本文は0644。秘密を含むweb.tomlはサービスグループに0640で読ませます。テンプレート読取失敗をBBS障害と決めつけず、Webログを確認します。`/notes/`を使う場合はApacheとbase_pathと転送先パスを一致させます。HTTPSから同じHTTPSへの無条件リダイレクトを入れません。[詳細な層別確認](web-server.md#web-troubleshooting)を参照してください。

## Door

GAMEで専用メニューへ入り、一覧の番号を押します。登録なし、会員レベル不足、同時枠満員を確認します。実行ファイルと親ディレクトリの通過権、作業領域とゲームデータの書込権、設定の絶対パスをBBS実行ユーザーで調べます。

Clansの小さなclansスクリプトは異常ではありません。同じ場所のOS別実体が必要です。既存世界をresetしたり、全体を777にしたりしません。online.flgやロックが残った場合は関連プロセスが停止していることを確認してからゲーム側の復旧手順を使います。サイズ変更はBBSのNAWSとゲーム側のPTYサイズ再取得の両方が必要です。

## DB障害と復旧

同じデータの二重起動・所有者・空き容量・本体とDB形式の互換を確認します。SQLiteのWALを消したり、features.sqliteをキャッシュと決めつけて削除したりしません。停止して原本を保存し、管理ツールで検査・別コピー復元を行います。`.liner-maint-incomplete` の出力は使いません。[復元手順](backup-recovery.md)へ進んでください。

## BBS間リンク

`check` は設定検査、`status` は状態です。TLSの相手ID・証明書期限、peersとoriginsの共有範囲、BBS専用会員の認証・ボード許可・容量、pending/deferredを確認します。protocol 2に全ノードをそろえます。

対向が古いDBへ復元されカーソルが進み過ぎた場合は、状態を確認して停止中にreset-peerします。本体だけを古いデータへ戻しリンク側の適用済み記録を残さないでください。履歴整理後の参加・復旧は署名付きスナップショットを新しいDBへ検証して取り込みます。[リンク保守](link-operations.md)を参照してください。
