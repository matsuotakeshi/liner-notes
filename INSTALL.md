> 現行版は0.71です。[変更内容](STAGE71.md)、[更新手順](UPGRADING.md)を参照してください。既存DB形式は自動変換しません。

# Liner notes インストールと運用開始

Liner notes 0.71をFreeBSD 15.1/amd64へ新規に構築する手順です。portsでインストールし、SQLite保存、Telnetメニュー、ファイル保管庫、ZMODEMを有効にします。最初はサーバー内だけで接続し、管理者登録と動作確認を済ませてから公開します。

OSのコマンドは、特記のない限りサーバー上のrootで実行します。会員登録などのBBS操作はTelnet接続後に入力します。例の公開先は `bbs.example.org:2222` です。他のサーバーでは読み替えてください。

今回は公開前の新規構築を対象とし、検証環境の会員・記事データは引き継ぎません。最初から本番用の会員台帳とSQLiteデータベースを作成します。

## 1 導入前の確認

```sh
uname -r
uname -m
test -f /usr/ports/Mk/bsd.port.mk
getent passwd 64964
getent group 64964
getent passwd liner_notes
getent group liner_notes
pkg info liner-notes
pkg info lrzsz
```

`test` が成功すればportsの基盤があります。portsツリーがない場合の準備は [FreeBSD HandbookのPorts Collection](https://docs.freebsd.org/en/books/handbook/ports/#ports-using) を参照してください。依存ソフトとlrzszの原本を取得するため、ビルド時には外部への接続が必要です。

このローカルportはOSアカウント `liner_notes` にUID/GID **64964**を使います。番号と名前の両方の `getent` に何も表示されなければ未使用です。別のアカウントが表示された場合は、配布物の `comms/liner-notes/files/UIDs` と `GIDs` を未使用番号に変更してからビルドします。既存アカウントの番号を変更する手順ではありません。

`pkg info`の「not found」は未導入なら正常です。Liner notesや同名のデータ領域がすでにある場合は、新規構築先が正しいか確認してください。lrzszがある場合は、他の用途でも使用しているか確認し、後述の再開検査を行います。版番号だけでは修正パッチの有無を判別できません。

## 2 配布物とportsのインストール

配布物は `liner-notes-ports-stage71.tar.gz`、portsの版は **0.71**です。アーカイブをサーバーへ転送してください。次のコマンドは、転送したファイルのあるディレクトリから実行します。

途中でエラーが出たら、その手順を解決してから次へ進みます。以下では `$()` を使うため、rootのログインシェルがcsh/tcshの場合も、最初に `sh` を起動してください。

```sh
sh
tar -xzf liner-notes-ports-stage71.tar.gz
cd liner-notes-ports-stage71
```

配布物には `comms/liner-notes`、修正版 `comms/lrzsz`、Liner notesのソースを収めた `distfiles`、マニュアル、検証記録があります。システムのportsツリーへ上書きコピーする必要はありません。

**lrzszが未導入の場合**は、同梱の修正版を先に入れます。

```sh
(cd comms/lrzsz && make BATCH=yes install clean)
```

続いてLiner notesをインストールします。`OPTIONS_SET=ZMODEM` はlrzszを依存関係に登録する指定で、BBS側の転送設定は後の手順で行います。

```sh
cd comms/liner-notes
make BATCH=yes OPTIONS_SET="DOCS ZMODEM" \
  DISTDIR="$(realpath ../../distfiles)" install clean
pkg info liner-notes
```

インストール直後はサービスを自動起動しません。配布物の検証済みソースを使う場合は、`distfiles` と `DISTDIR` を指定します。未指定時はGitHub Releasesから取得します。

## 3 インストール先

| 用途 | 既定の場所 |
| --- | --- |
| BBS daemon | `/usr/local/sbin/liner-notes` |
| バックアップと復元 | `/usr/local/bin/liner-notes-admin` |
| 会員と保管庫の管理 | `/usr/local/bin/liner-notes-memberctl`、`liner-notes-vaultctl` |
| 設定 | `/usr/local/etc/liner-notes/` |
| HELPとメニューと案内文 | `/usr/local/share/liner-notes/` |
| データと添付と保管庫 | `/var/db/liner-notes/` |
| 転送作業用領域 | `/var/db/liner-notes/spool/` |
| daemon出力とログの設定先 | `/var/log/liner-notes/` |
| PID | `/var/run/liner-notes.pid`、`/var/run/liner-notes.child.pid` |
| 起動スクリプト | `/usr/local/etc/rc.d/liner_notes` |
| このマニュアル | `/usr/local/share/doc/liner-notes/INSTALL.md` |

データ・spool・ログのディレクトリは `liner_notes:liner_notes` 所有、0700で作られます。daemonをrootで直接常用せず、rcスクリプトを通してサービスユーザーで動かします。

## 4 初回起動の設定

設定は `/usr/local/etc/liner-notes/liner_notes.conf` を読み、その後の `/etc/rc.conf` の指定が優先されます。この手順では `sysrc` でrc.confを設定します。汎用の引数文字列ではなく、各設定値をrcスクリプトが引数へ組み立てます。

初回起動前にSQLiteの保存先を指定します。データベースは初回起動で作成されるため、移行コマンドの実行は不要です。

```sh
sysrc liner_notes_enable=YES
sysrc liner_notes_bind=127.0.0.1
sysrc liner_notes_port=2222
sysrc liner_notes_language=ja
sysrc liner_notes_timezone=Asia/Tokyo
sysrc liner_notes_database=/var/db/liner-notes/liner.sqlite
sysrc liner_notes_registration=open
sysrc liner_notes_id_mode=free
sysrc liner_notes_rtbbs_member_policy=NO
```

この時点では外部から接続できません。`localhost`はそのコマンドを実行したホスト自身を指します。接続テストはBBSをインストールしたサーバー上で実行してください。

## 5 保管庫とZMODEMの準備

公開保管庫をサービスユーザーで初期化します。この操作は新規保管庫に対して行います。

```sh
su -m liner_notes -c \
  '/usr/local/bin/liner-notes-vaultctl init /var/db/liner-notes/vault'
```

ZMODEMの途中再開を実際に検査し、使用バイナリに対応した検証記録を保存します。成功するまで転送helperを有効にしないでください。

```sh
su -m liner_notes -c \
  '/usr/local/bin/liner-notes-check-resume /usr/local/bin/lrz /usr/local/bin/lsz --receipt /var/db/liner-notes/lrz-resume.json'
```

0.71では既存の検証記録があっても再実行できます。同一内容は保持し、変更時は旧記録を退避してから更新します。`receipt_status`が`created`、`unchanged`、`updated`のいずれかなら保存処理も完了しています。

既存のlrzszで失敗した場合は、出力と `lrz --version`、`lsz --version` を確認します。修正版への置換は、既存パッケージと利用サービスを確認してから実施してください。確認せずに `pkg delete -f` で依存関係を壊さないでください。lrzを交換した後は検証記録も更新します。

```sh
sysrc liner_notes_vault_dir=/var/db/liner-notes/vault
sysrc liner_notes_zmodem_helper=/usr/local/bin/liner-notes-zmodem
```

既定のhelperは上記のspool・エンジン・検証記録を使います。データの場所を変える場合、`liner_notes_data_dir`だけを変えてもhelperの場所は変わりません。専用helperの生成は [PORTS.md](PORTS.md) を参照してください。メール添付だけを使う構成では公開保管庫の初期化と `liner_notes_vault_dir` は不要です。

## 6 起動と管理者の登録

```sh
service liner_notes start
service liner_notes status
sockstat -4l -P tcp -p 2222
telnet 127.0.0.1 2222
```

`sockstat`で `127.0.0.1:2222` の待受を確認します。Telnetではバナーとメニューが表示されます。`RTBBS-PROBE`は内部識別表示であり、それだけが表示されメニューがない場合は、後述のトラブル対応を確認してください。

BBS内で `REGISTER 希望するログイン名` を入力し、本番用のパスワードを設定します。次に `ACCOUNT` で**会員番号**を確認し、`QUIT`で切断します。OSの `liner_notes` アカウントとBBS会員は別物です。`sysop`というログイン名だけでは管理者になりません。

OS側のshへ戻り、管理者にする会員番号を入力します。この手順は新規のadmins.confを作成します。既存ファイルがある場合は上書きせず、その内容へ会員番号を追加してください。

```sh
printf '管理者にする会員番号: '
read -r admin_id
case "$admin_id" in ''|*[!0-9]*|0*) echo '会員番号を確認してください' ;;
*)
  if [ -e /usr/local/etc/liner-notes/admins.conf ]; then
    echo 'admins.confが存在します。既存の内容を確認してください'
  else
    (umask 027; printf '%s\n' "$admin_id" > /usr/local/etc/liner-notes/admins.conf)
    chown root:liner_notes /usr/local/etc/liner-notes/admins.conf
    chmod 640 /usr/local/etc/liner-notes/admins.conf
  fi
  ;;
esac
```

番号とファイル内容を確認してから指定し直します。

```sh
cat /usr/local/etc/liner-notes/admins.conf
sysrc liner_notes_admins=/usr/local/etc/liner-notes/admins.conf
service liner_notes restart
telnet 127.0.0.1 2222
```

再ログインして管理者メニューと `HELP ADMIN` が表示されることを確認します。パスワードをrc.confやadmins.confへ書く必要はありません。

## 7 公開前の動作確認

| 確認する操作 | 確認する結果 |
| --- | --- |
| 投稿と返信と再ログイン | 記事と返信関係が保存される |
| 一般会員でログイン | 管理者用の操作を実行できない |
| メールの送信と受信 | 宛先会員が本文を読める |
| 小さいファイルのZMODEM送受信 | 完了表示が出て、取得内容が元と一致する |
| バッチ転送 | 成功したファイルがDONEになる |
| serviceのstopとstart | 再接続後も会員・記事・設定が残る |
| 起動ログ | 起動失敗や保存エラーがない |

ファイル転送は、Telnet BINARYとZMODEMに対応した端末を使います。通常のコマンドラインtelnetは画面確認用で、ZMODEM受信機能は備えていません。サーバーの完了表示まで確認してください。

0.69の検証ではXMODEM-1Kが一度中断し、その後の再試験は成功していますが、原因は未特定です。運用開始時はZMODEMを中心に確認してください。詳細は [STAGE69.md](STAGE69.md) に記載しています。

## 8 初期バックアップと復元確認

SQLite運用では `db-backup` を使います。`liner.sqlite`だけでは保管庫・添付の実ファイルや外部設定を保存できません。次の例はサービス停止後、データと補助ファイルを同じ日時のディレクトリへ保存します。途中で失敗した場合は停止したままになるので、出力を確認してから再開してください。

```sh
(
  set -eu
  backup_dir="/var/backups/liner-notes/$(date +%Y%m%d-%H%M%S)"
  install -d -m 700 "$backup_dir"
  service liner_notes stop
  liner-notes-admin db-backup --root /var/db/liner-notes \
    --database /var/db/liner-notes/liner.sqlite \
    --board notes.dat --members members.dat "$backup_dir/data.tar.gz"
  liner-notes-admin verify "$backup_dir/data.tar.gz"
  tar -czpf "$backup_dir/support.tar.gz" -C / \
    usr/local/etc/liner-notes usr/local/share/liner-notes \
    var/log/liner-notes etc/rc.conf
  tar -tzf "$backup_dir/support.tar.gz" > "$backup_dir/support-files.txt"
  printf 'バックアップ先: %s\n' "$backup_dir"
  service liner_notes start
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

DBには設定先がデータ領域外の論理ログも含まれます。これらは復元先の `external/元の絶対パス` 以下に対応するため、別の場所で起動するときはUSERLOGとチャットログの指定も合わせます。具体的な移行・復元・整理の仕様は [STAGE67.md](STAGE67.md) を参照してください。元データやバックアップを先に消さないでください。

## 9 外部公開

管理者登録、送受信、初期バックアップが終わったら外部待受へ変更します。

```sh
sysrc liner_notes_bind=0.0.0.0
service liner_notes restart
sockstat -4l -P tcp -p 2222
```

これは全IPv4アドレスでの待受です。特定のアドレスだけで公開したい場合は、そのIPv4アドレスを指定します。IPv6待受を追加する設定ではありません。

使用中のファイアウォールでTCP 2222を許可します。NATがある場合は転送先をBBSサーバーへ合わせ、DNSのAレコードが公開先を指していることを確認します。既存のファイアウォール設定全体を置き換える必要はありません。

**別の端末**から次を実行します。

```sh
telnet bbs.example.org 2222
```

Telnetは暗号化されません。BBS用のパスワードを他サービスと共用しないでください。暗号化された入口にはOSのOpenSSHを使えます。すでにSSHでログインできるOSアカウントなら、クライアントから次の形で接続します。

```sh
ssh -t OSアカウント@bbs.example.org /usr/local/bin/liner-notes-ssh 2222
```

この入口はBBS内蔵SSHではなく、sshdやOSアカウントを自動設定しません。OSの `liner_notes` はサービス専用なので、利用者のSSHログイン用には使いません。SSH経由のファイル転送は、使用する端末でも別途確認してください。

## 10 運用設定と案内文

| 設定 | 値と用途 |
| --- | --- |
| `liner_notes_registration` | `open` 即時登録、`approval` 承認制、`closed` 新規登録停止 |
| `liner_notes_id_mode` | `free` 希望名、`auto` 自動採番 |
| `liner_notes_registration_form` | `simple` 簡易、`detailed` 詳細フォームと確認 |
| `liner_notes_chat`、`liner_notes_mail`、`liner_notes_vault` | 各機能の `YES` / `NO` |
| `liner_notes_rtbbs_member_policy` | 既定 `NO`。`YES`で旧RTBBS寄りの会員情報・USERLOG方針 |
| `liner_notes_idle_seconds` | 無入力切断までの秒数。管理者は対象外 |
| `liner_notes_limits` | 件数・本文長・同時接続等の設定ファイル |

互換モードを有効にしても、保存済みの非公開プロフィール設定は保持します。一方、USERLOGは会員へ公開され、住所・誕生日の本人変更やグループ検索に制限が加わります。[STAGE69.md](STAGE69.md)を読んでから選んでください。

案内文は `/usr/local/share/liner-notes/` にあります。`banner.txt`は接続バナー、`welcome.txt`は接続直後の案内、`systemnews.txt`は接続時・ログイン時の案内、`birthday.txt`は誕生日、`sysinfo.txt`はシステム案内、`goodbye.txt`は終了時の文書です。各文書はUTF-8・4KiB以内にします。HELPは `help/ja` と `help/en`、メニューは `menus` を編集します。内容を反映する際はサービスを再起動してください。

設定や案内文の `.sample` は配布時の見本です。運用中のファイルは更新時に保持されるため、新しいsampleと比較して追加項目を反映します。サーバーの停止・再起動は接続中の利用者にも影響します。通常操作の説明は [COMMANDS.md](COMMANDS.md)、ゲーム登録は [DOORS.md](DOORS.md) にあります。

## 11 起動と接続のトラブル

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

FreeBSDのサービス管理の基本は [FreeBSD Handbookの設定とサービス](https://docs.freebsd.org/en/books/handbook/config/#configtuning-rcd) を参照してください。本マニュアルの設定名は同梱のrcスクリプトに対応しています。
