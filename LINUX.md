# Rocky Linux / AlmaLinux

以下はRocky Linux 9 / AlmaLinux 9、x86_64、Clang、Python 3.11で確認したソース構築手順です。FreeBSD向けのports/pkgはLinuxにはインストールしません。0.74のRocky Linux 10.2向けRPM・systemd導入手順は[RPM.md](RPM.md)を参照してください。10系はPython 3.12を使います。

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
モデムの設定は [SERIAL.md](SERIAL.md) を参照してください。
共有ボードは専用Python環境へ依存を入れ、BBSとは別ユーザーで実行します。
ソースディレクトリでの準備例です。

```sh
sudo python3.11 -m venv /opt/liner-notes-link-python
sudo /opt/liner-notes-link-python/bin/pip install -r link-requirements.txt
```

[共有ボードの設定手順](BBS_LINKS.md)中の `liner-notes-link` は、この構成では
`/opt/liner-notes-link-python/bin/python /usr/local/share/liner-notes/scripts/link_service.py`
に置き換えます。設定は `/etc/liner-notes/link.toml`、保存先は
`/var/lib/liner-notes-link` 等を使います。鍵・接続先を設定する前に自動起動しません。
システム標準Pythonへ追加パッケージを入れる必要はありません。

## 更新と検証範囲

0.73の [Linux CI](https://github.com/matsuotakeshi/liner-notes/actions/runs/41783614429) はRocky Linux 9／AlmaLinux 9を対象に、`make check`、stage67〜73の追加試験、実lrzsz 0.13.1を使うZMODEM、配置・設定保持・導入後起動を検査します。新しいCOMは疑似端末、共有ボードは複数BBSと相互TLSで検査します。実行結果とログは配布物の `evidence/stage73/` を参照してください。Linuxでの1GiB実転送とFreeBSD専用SSH入口・rc.d試験は対象外です。

更新前にサービス・外部転送を停止し、データと`/etc/liner-notes`、編集済み`/usr/local/share/liner-notes`をバックアップしてください。新しいソースを別ディレクトリで構築し、`install-linux`を実行します。

```sh
sudo /usr/local/bin/liner-notes-admin upgrade-check \
  --assets /etc/liner-notes --assets /usr/local/share/liner-notes \
  --database /var/lib/liner-notes/liner.sqlite
sudo systemctl daemon-reload
sudo systemctl start liner-notes
```

DBの詳細・復元上の制約は[UPGRADING.md](UPGRADING.md)を参照してください。Linux対応のために保存形式を変更する必要はありません。

[Linux CI](https://github.com/matsuotakeshi/liner-notes/actions/workflows/linux.yml)は、Ubuntuホスト上のRocky/Almaコンテナ内でビルドし、一般ユーザーで回帰試験と転送試験を実行します。systemd unitの構文と、インストールした設定引数を使うサービス用ユーザーでの起動・Telnet接続・SQLite作成・SIGTERM終了も検証します。ただし、この起動試験はsystemdを介さず実行します。コンテナ試験だけではOS起動時の自動起動、SELinux enforcing下の運用、実際のファイアウォール設定まで実証できません。SELinuxを無効にせず、設置先でラベルと拒否ログを確認してください。

対応OS全体の見通しは[PLATFORMS.md](PLATFORMS.md)を参照してください。

依存パッケージの参考: [AlmaLinux 9.2のPython 3.11提供](https://wiki.almalinux.org/release-notes/9.2)、[RHEL 9のPython導入](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/9/html-single/installing_and_using_dynamic_programming_languages/index)。
