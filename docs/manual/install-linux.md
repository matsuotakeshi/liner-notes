# Linuxへの導入

Rocky Linux 10.2ではRPM、Rocky Linux 9 / AlmaLinux 9ではソース構築の手順を選びます。RPMの `/usr` とソース配置の `/usr/local` を混同しないでください。

## RPMによる導入

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
初期値は127.0.0.1:2222。管理者の作成・公開設定は[FreeBSDへの導入](install-freebsd.md)の
BBS内の手順に従ってください。公開前にOSのファイアウォールも確認します。
起動確認後、OS起動時にも開始する場合だけ`systemctl enable liner-notes`を実行します。

Webは`/etc/liner-notes/web/web.toml`を編集します。初期値はlocalhostのHTTP確認用で、
外部HTTPS公開時には`origin`を公開URL、`secure_cookies = true`に変更してください。
既存Webサーバーとの接続は[WebUIとApacheの構成・差し替え](web-server.md)を参照します。

```sh
systemctl start liner-notes-web
journalctl -u liner-notes-web -n 50
```

リンクは鍵・相手・共有ボード・専用BBS会員を設定するまで起動できません。
[共有ボードの管理・保守](link-operations.md)を参照してください。
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

## ソースからの構築・配置

## ビルド

管理者で依存パッケージを導入します。Python 3.11はOS標準Pythonを置換せず、明示的に指定します。

```sh
sudo dnf install clang gcc make openssl-devel sqlite-devel python3.11 git tar gzip telnet
git clone --branch v0.74 https://github.com/matsuotakeshi/liner-notes.git
cd liner-notes
make CC=clang PYTHON=python3.11
```

一般ユーザーで試運転できます。

```sh
mkdir -m 700 trial
./runtime --telnet --menu --lang ja 2222 trial/notes.dat trial/members.dat
```

別端末から`telnet localhost 2222`で接続します。Telnetは暗号化されません。外部公開前に管理者登録・接続経路を設定してください。

## インストール

```sh
sudo make CC=clang PYTHON=python3.11 install-linux
```

`PREFIX`の既定値は`/usr/local`です。`DESTDIR`を指定した配置確認もできます。プログラムは`sbin/liner-notes`、管理コマンドは`bin/liner-notes-*`、外部文書は`share/liner-notes`へ入ります。Linuxの設定は`/etc/liner-notes`、データは`/var/lib/liner-notes`、利用者・チャットログは`/var/log/liner-notes`を使います。daemonの標準出力・エラーはjournalへ出します。

インストーラーは既存の設定・メニュー・HELP・バナー・unitを保持し、新しい配布内容を`.sample`へ書きます。会員データの作成、サービス開始、既存DBの変換は行いません。

初回だけ、サービス用ユーザーとデータディレクトリを作ります。

```sh
getent passwd liner_notes || sudo useradd --system --home-dir /var/lib/liner-notes --shell /sbin/nologin liner_notes
sudo install -d -o liner_notes -g liner_notes -m 700 \
  /var/lib/liner-notes /var/lib/liner-notes/spool /var/log/liner-notes
sudo systemctl daemon-reload
sudo systemctl start liner-notes
sudo systemctl status liner-notes
sudo journalctl -u liner-notes -n 50
```

`/etc/liner-notes/liner-notes.env`の`LINER_NOTES_OPTIONS`で引数を設定します。systemdが読む環境ファイルであり、シェルスクリプトではありません。`$(...)`や環境変数を使ったシェル展開はしません。すべてのオプションをポート番号より前に記載してください。

初期値は127.0.0.1:2222です。まず会員を登録し、その会員番号を`/etc/liner-notes/admins.conf`へ1行で記載します。所有者root、グループliner_notes、モード0640とし、`LINER_NOTES_OPTIONS`へ`--admins /etc/liner-notes/admins.conf`を追加して再起動します。新規の最初の会員は通常1ですが、表示された実際の番号を確認してください。外部待受が必要なら`--bind`も変更し、必要なポートだけファイアウォールで開放します。

動作確認後、自動起動を有効にします。

```sh
sudo systemctl enable liner-notes
```

このunitは前景で動くruntimeをsystemdが監視します。独自PIDファイルやFreeBSDのrc.dは使いません。ゲームはrootでは動かさず、サービス用ユーザーの権限で実行します。

## 保管庫・ZMODEM

```sh
sudo -u liner_notes /usr/local/bin/liner-notes-vaultctl init /var/lib/liner-notes/vault
```

`LINER_NOTES_OPTIONS`へ`--vault-dir /var/lib/liner-notes/vault`を追加します。ZMODEM用のlrz/lszは別途必要です。LP64のCRC修正を含む0.13.1は、従来と同じ`python3.11 scripts/build-lrzsz.py 新しいビルド先`で構築できます。既存のビルド先は上書きしません。

サービスが実行できる場所へ配置し、環境ファイルの`LINER_NOTES_LRZ`と`LINER_NOTES_LSZ`へ絶対パスを書きます。unitの`ProtectHome=yes`により、サービスからユーザーのホームディレクトリは参照できません。例えば`/opt`配下に配布物一式を配置してください。上流ソースとライセンスも保管します。

```sh
sudo -u liner_notes /usr/local/bin/liner-notes-check-resume \
  /実際の場所/bin/lrz /実際の場所/bin/lsz \
  --receipt /var/lib/liner-notes/lrz-resume.json
```

成功後、`LINER_NOTES_OPTIONS`へ`--zmodem-helper /usr/local/bin/liner-notes-zmodem`を追加して再起動します。繰り返し検証しても既存記録は安全に再利用・更新されます。ディストリビューション付属lrzszの再開可否を、パッケージ名だけで判断しません。

## ドアゲーム

ゲームを使用する場合は、`/etc/liner-notes/doors.conf`のサンプルを編集し、`LINER_NOTES_OPTIONS`へ`--doors /etc/liner-notes/doors.conf`を追加します。ゲーム用作業ディレクトリをサービス所有者で作成してください。付属サンプルは`/usr/local/share/liner-notes/scripts/door_guess.py`です。Windows/DOS用ゲームの互換実行は含みません。

## COMと共有ボードリンク

COMは `liner-notes-serial --platform linux` を使用します。速度・デバイス・
モデムの設定は [COM・Hayesモデム接続](serial-connector.md) を参照してください。
共有ボードは専用Python環境へ依存を入れ、BBSとは別ユーザーで実行します。
ソースディレクトリでの準備例です。

```sh
sudo python3.11 -m venv /opt/liner-notes-link-python
sudo /opt/liner-notes-link-python/bin/pip install -r link-requirements.txt
```

[共有ボードの設定手順](links.md)中の `liner-notes-link` は、この構成では
`/opt/liner-notes-link-python/bin/python /usr/local/share/liner-notes/scripts/link_service.py`
に置き換えます。設定は `/etc/liner-notes/link.toml`、保存先は
`/var/lib/liner-notes-link` 等を使います。鍵・接続先を設定する前に自動起動しません。
システム標準Pythonへ追加パッケージを入れる必要はありません。
