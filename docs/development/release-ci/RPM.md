# Linux RPM（0.74）

Rocky Linux 10.2 x86_64用の本体、Web、共有ボード用RPMを用意しています。
EL9はPython 3.11を使う本体ビルドの分岐がありますが、この版のRPM実機試験は
Rocky Linux 10.2が対象です。AlmaLinux/EL9のRPMを試験済みとは扱いません。

## 配置とサービス

| 内容 | 配置 |
|---|---|
| 本体 | `/usr/sbin/liner-notes` |
| 管理ツール | `/usr/bin/liner-notes-admin` など |
| HELP・バナー | `/usr/share/liner-notes/` |
| 設定 | `/etc/liner-notes/` |
| BBSデータ | `/var/lib/liner-notes/` |
| BBS利用ログ | `/var/log/liner-notes/` |
| サービスログ | systemd journal |
| unit | `/usr/lib/systemd/system/liner-notes*.service` |
| Webの一時領域 | `/var/lib/liner-notes-web/` |
| リンクDB・鍵 | `/var/lib/liner-notes-link/` |
| リンク管理ソケット | `/run/liner-notes-link/control.sock` |

RPMはサービスアカウントを作りますが、サービスの有効化・起動・更新時の再起動を
自動実行しません。設定・HELP・バナー・Webテンプレートは`%config(noreplace)`です。
新しい標準設定との違いは`.sample`と、RPMが作る`.rpmnew`を確認してください。
削除時も会員・記事・保管庫とアカウントを消しません。変更済み設定はRPMにより
`.rpmsave`に退避される場合があります。

## 新規導入

依存パッケージを解決できる状態で、必要なRPMだけ指定します。
Web用`python3-aiohttp`は検証機ではEPELから、`python3-cryptography`はbaseosから導入しました。
RPMスクリプトでpipや外部ネットワークからライブラリを取得することはありません。

```sh
dnf install ./liner-notes-0.74-*.x86_64.rpm
# 必要な場合のみ追加する（debuginfo/debugsourceは通常不要）。
dnf install ./liner-notes-web-0.74-*.x86_64.rpm
dnf install ./liner-notes-link-0.74-*.x86_64.rpm

systemctl start liner-notes
systemctl status liner-notes
journalctl -u liner-notes -n 50
```

本体の引数は`/etc/liner-notes/liner-notes.env`の`LINER_NOTES_OPTIONS`です。
初期値は127.0.0.1:2222。管理者の作成・公開設定は[INSTALL.md](INSTALL.md)の
BBS内の手順に従ってください。公開前にOSのファイアウォールも確認します。
起動確認後、OS起動時にも開始する場合だけ`systemctl enable liner-notes`を実行します。

Webは`/etc/liner-notes/web/web.toml`を編集します。初期値はlocalhostのHTTP確認用で、
外部HTTPS公開時には`origin`を公開URL、`secure_cookies = true`に変更してください。
既存Webサーバーとの接続は[WEBUI.md](WEBUI.md)を参照します。

```sh
systemctl start liner-notes-web
journalctl -u liner-notes-web -n 50
```

リンクは鍵・相手・共有ボード・専用BBS会員を設定するまで起動できません。
[BBS_LINK_OPERATIONS.md](BBS_LINK_OPERATIONS.md)を参照してください。
SELinuxを無効にして解決する手順は採用しません。なお本体サービスは検証環境で
`unconfined_service_t`として動作しました。専用のSELinux制約ポリシーを追加したという意味ではありません。

## 更新

1. 外部管理ツールでデータをバックアップし、現在のRPMと設定を保存します。
2. 必要な本体・追加RPMを同じ版にそろえて`dnf upgrade ./対象RPM...`を実行します。
3. 設定の差分を確認し、利用者への告知後にサービスを再起動します。
4. ログイン、記事、メール、保管庫、Web、Doorの利用確認を行います。

インストール済みファイルが新しくなっても、動作中プロセスは再起動まで旧実行ファイルです。
ソース配置版から切り替える場合は、`systemctl show liner-notes -p FragmentPath`で
`/etc/systemd/system`や`/usr/local/lib/systemd/system`にある旧unitが優先されていないか
確認してください。RPMは既存の別配置unitを削除しません。

## ビルドと検査

```sh
dnf install clang make openssl-devel sqlite-devel rpm-build systemd-rpm-macros python3
python3 scripts/build-rpm.py /新しい作業ディレクトリ
```

Git作業ツリー、または`SOURCE-MANIFEST`を持つ検証ソースで実行します。
出力先の再利用は拒否します。`RPMS/`にバイナリ、`SRPMS/`にソースRPMを生成します。
`tests/test_rpm_systemd.py`、`tests/test_rpm_update.py`は専用検証機限定です。

## CI限定のRPMリビジョン更新試験

`Rocky 10 Package CI` はGitHub-hosted runnerの使い捨てRocky Linux 10コンテナ内で、
生成済みの0.74-1本体/Web/Link RPMを導入後、**同じソースからCI専用の0.74-2を生成して更新**します。
`%config(noreplace)` 対象のローカル編集済み設定、合成SQLiteデータ、ダミーのリンク鍵が
更新後に保持され、削除後もBBSデータ・鍵が残ることを確認します。

CI専用の0.74-2はGitHub Actionsのリリース候補に保存しません。
この試験ではsystemdサービスを稼働していないため、**実稼働プロセスの無停止更新、
旧バージョンとのDB/API互換性、実バックアップからの復元は未検証**です。
本番3局への配備やOS/Apache/SofChatの停止・再起動はしません。
