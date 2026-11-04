> stage73の追加機能: [DBの別コピー変換](DATABASE_STRUCTURED.md) / [COM・モデム](SERIAL.md) / [鍵認証の共有ボード](BBS_LINKS.md) / [Door運用](DOOR_OPERATIONS.md)。本番への適用は設置者がバックアップ後に行います。

> 現行版は0.73です。[変更内容](STAGE73.md)、[更新手順](UPGRADING.md)を参照してください。既存DB形式は自動変換しません。

# Liner notes 0.73 導入・運用開始マニュアル

文書改訂: 2026-11-04。対象プログラムは正式版0.73です。

**すでに会員・記事・ゲームがある環境は、新規初期化を行わず [既存環境の更新](UPGRADING.md) から進んでください。** 本文の新規構築は空の保存先向けです。WebUIだけを追加する場合は [12 WebUI](#webui-setup)、ゲームだけ追加する場合は [13 ゲーム](#doors-setup)へ進みます。

読む順序:

1. [前提の確認](#preflight) → [配布物の照合・インストール](#install-package)
2. [配置](#layout) → [初回設定](#initial-config) → [保管庫・ZMODEM](#transfer-setup)
3. [管理者登録](#first-admin) → [基本動作確認](#smoke-test) → [バックアップ](#backup-restore)
4. [外部Telnet・ポート23](#public-telnet) → [WebUI・Apache HTTPS](#webui-setup)
5. [StarRoute・The Clans](#doors-setup) → [更新と戻し方](#upgrade-entry) → [公開前の最終確認](#acceptance)
6. 困ったとき: [BBS起動](#troubleshooting)、[WebUI](#web-troubleshooting)、[設定一覧](#config-reference)

Liner notes 0.73をFreeBSD 15.1/amd64へ新規に構築する手順です。portsでインストールし、SQLite保存、Telnetメニュー、ファイル保管庫、ZMODEMを有効にします。最初はサーバー内だけで接続し、管理者登録と動作確認を済ませてから公開します。

OSのコマンドは、特記のない限り **BBSを設置するFreeBSDサーバー上のrootの `/bin/sh`** で実行します。WindowsのPowerShellやBBSの入力欄へ貼り付けないでください。`sh` のコード枠はOSのコマンド、`text` のコード枠はBBS内の入力か設定例です。行頭のプロンプトは省いています。

rootのログインシェルがcsh/tcshでも、最初に `/bin/sh` を実行すれば本文の変数・引用符をそのまま使えます。接続し直したら変数は消えます。`cd`や変数設定も含め、その節の先頭から実行してください。エラーが出た手順は、原因を解消してから先へ進みます。

標準例は `bbs.example.org`、Telnetは最初2222、必要に応じて23、Webは `https://bbs.example.org/notes/` です。実環境が `srv.ysni.org` 上の `bbs.linernotes.site` なら、公開ホスト名を `bbs.linernotes.site` に読み替えます。BBSとWebコネクタ間は同じサーバーの `127.0.0.1` を使います。

新規構築では検証用の会員・記事データを持ち込まず、本番用の会員台帳とSQLiteデータベースを作成します。既存運用中のデータは更新手順で保持します。

<a id="preflight"></a>
## 1 導入前の確認

```sh
uname -r
uname -m
hostname
df -h /usr/local /var/db /var/log
test -f /usr/ports/Mk/bsd.port.mk
getent passwd 64964
getent group 64964
getent passwd liner_notes
getent group liner_notes
getent passwd 64965
getent group 64965
getent passwd liner_notes_web
getent group liner_notes_web
pkg info liner-notes
pkg info lrzsz
```

`test` が成功すればportsの基盤があります。portsツリーがない場合の準備は [FreeBSD HandbookのPorts Collection](https://docs.freebsd.org/en/books/handbook/ports/#ports-using) を参照してください。依存ソフトとlrzszの原本を取得するため、ビルド時には外部への接続が必要です。

この配布portはOSアカウント `liner_notes` にUID/GID **64964**、WEBUI有効時は `liner_notes_web` に **64965**を使います。番号と名前の両方の `getent` に何も表示されなければ未使用です。別のアカウントが表示された場合は、配布物の `comms/liner-notes/files/UIDs` と `GIDs` を未使用番号に変更してからビルドします。既存アカウントの番号を変更する手順ではありません。

`pkg info`の「not found」は未導入なら正常です。Liner notesや同名のデータ領域がすでにある場合は、新規構築先が正しいか確認してください。lrzszがある場合は、他の用途でも使用しているか確認し、後述の再開検査を行います。版番号だけでは修正パッチの有無を判別できません。

Webを使用する場合は、公開名のDNS、既存のApache 2.4.47以上、その名前に有効なTLS証明書を用意します。BBSのインストールだけではDNS・証明書・ルーター・ファイアウォールを変更しません。Web側とBBS側にそれぞれ転送用の空き容量が必要です。1GiBの送信でも作業中は複数のコピーができるため、単一ファイルと同じ容量だけでは不足します。

portsツリーがない新規ホストでは、次のように取得できます。既存の `/usr/ports` がある場合はこのcloneを実行せず、そのツリーの更新方針に従ってください。

```sh
pkg install git
git clone --depth 1 https://git.FreeBSD.org/ports.git /usr/ports
test -f /usr/ports/Mk/bsd.port.mk && echo 'ports framework OK'
```

既存ホストではportsとpkgの配布系統・依存関係を揃えます。全パッケージの更新や強制削除は、この導入のために一括実行しません。[FreeBSD公式のports手順](https://docs.freebsd.org/en/books/handbook/ports/#ports-using)も参照してください。

<a id="install-package"></a>
## 2 配布物とportsのインストール

配布物は `liner-notes-ports-stage73.tar.gz`、portsの版は **0.73**です。[正式リリース](https://github.com/matsuotakeshi/liner-notes/releases/tag/v0.73)から取得します。以下は空の作業ディレクトリでダウンロードと照合を行う例です。

途中でエラーが出たら、その手順を解決してから次へ進みます。以下では `$()` を使うため、rootのログインシェルがcsh/tcshの場合も、最初に `sh` を起動してください。

```sh
/bin/sh
LN_WORK=$(mktemp -d /root/liner-notes-0.73.XXXXXXXX)
cd "$LN_WORK"
(
  set -eu
  fetch -o liner-notes-ports-stage73.tar.gz https://github.com/matsuotakeshi/liner-notes/releases/download/v0.73/liner-notes-ports-stage73.tar.gz
  fetch -o SHA256SUMS https://github.com/matsuotakeshi/liner-notes/releases/download/v0.73/SHA256SUMS
  LN_EXPECTED=$(awk '$2 == "liner-notes-ports-stage73.tar.gz" {print $1}' SHA256SUMS)
  test -n "$LN_EXPECTED"
  test "$(sha256 -q liner-notes-ports-stage73.tar.gz)" = "$LN_EXPECTED"
  echo 'SHA256 OK'
  tar -xzf liner-notes-ports-stage73.tar.gz
)
```

`SHA256 OK`と展開成功を確認したら進みます。ファイルをWindowsから転送した場合も、同じ `SHA256SUMS` で照合します。接続し直した場合は、作業先へ `cd` し直してください。`pwd`の出力も控えておくと便利です。

```sh
cd "$LN_WORK/liner-notes-ports-stage73"
pwd
ls comms/liner-notes/Makefile distfiles/liner-notes-stage73.tar.gz
```

配布物には `comms/liner-notes`、ソースを収めた `distfiles`、マニュアル、検証記録があります。
独自のlrzsz portsは同梱しません。ZMODEMには本家FreeBSDの `comms/lrzsz` を使います。

```sh
pkg install lrzsz
```

portsからlrzszを構築する場合は、更新済みの `/usr/ports/comms/lrzsz` を使用します。
パッケージ配布系統によっては旧0.12系のままの場合があります。0.13.1以上が
取得できない場合は本家portsを更新して構築してください。導入後は以下の再開検査を行ってください。

続いてLiner notesをインストールします。`OPTIONS_SET=ZMODEM` はlrzszを依存関係に登録する指定で、BBS側の転送設定は後の手順で行います。

```sh
cd comms/liner-notes
make BATCH=yes OPTIONS_SET="DOCS WEBUI ZMODEM" \
  DISTDIR="$(realpath ../../distfiles)" install clean
pkg info liner-notes
```

インストール直後はサービスを自動起動しません。配布物の検証済みソースを使う場合は、`distfiles` と `DISTDIR` を指定します。未指定時はGitHub Releasesから取得します。

Web不要なら `OPTIONS_SET="DOCS ZMODEM" OPTIONS_UNSET=WEBUI` を指定します。ZMODEM不要なら `OPTIONS_SET="DOCS WEBUI" OPTIONS_UNSET=ZMODEM` とし、後のlrzsz検査とhelper設定を省略します。保管庫のWeb転送にlrzszは不要です。共有ボードリンクも使う場合は `OPTIONS_SET` に `LINK` を追加し、[BBS_LINKS.md](BBS_LINKS.md)の専用会員・鍵・共有設定を行ってください。

**pkgで導入する場合は、上のmake installとどちらか一方を選びます。** 同梱pkgはFreeBSD 15/amd64・Python 3.12用、DOCS/WEBUI/LINK有効です。ZMODEMの依存指定は無効なので、使う場合は本家lrzszを別途導入します。配布物のルートから実行します。

```sh
pkg install python312 py312-sqlite3 py312-aiohttp py312-cryptography sqlite3
pkg add packages/liner-notes-0.73.pkg
pkg info liner-notes
```

異なるABIやPython構成ではportから構築します。`pkg install liner-notes`で公式リポジトリから取得する手順ではありません。Liner notes自体は、このリリースのローカルport/pkgです。

<a id="layout"></a>
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
| Webコネクタ | `/usr/local/sbin/liner-notes-web` |
| Webのプログラム・静的ファイル | `/usr/local/share/liner-notes/web/` |
| Web設定・HTML・メッセージ | `/usr/local/etc/liner-notes/web/` |
| Webの転送作業用領域 | `/var/db/liner-notes-web/spool/` |
| Webのログ・PID | `/var/log/liner-notes/web.log`、`/var/run/liner-notes-web.pid` |
| Web起動スクリプト | `/usr/local/etc/rc.d/liner_notes_web` |
| このマニュアル | `/usr/local/share/doc/liner-notes/INSTALL.md` |

データ・spool・ログのディレクトリは `liner_notes:liner_notes` 所有、0700で作られます。daemonをrootで直接常用せず、rcスクリプトを通してサービスユーザーで動かします。

Webは別ユーザー `liner_notes_web` で動きます。Web用spoolだけを書き込み可能にし、BBSのDB・会員・私信を直接読ませません。両サービスの設定ファイルはrootが編集し、サービス用ユーザーに必要な読み取り権限を与えます。

<a id="initial-config"></a>
## 4 初回起動の設定

設定は `/usr/local/etc/liner-notes/liner_notes.conf` を読み、その後の `/etc/rc.conf` の指定が優先されます。この手順では `sysrc` でrc.confを設定します。汎用の引数文字列ではなく、各設定値をrcスクリプトが引数へ組み立てます。

初回起動前にSQLiteの保存先を指定します。この例は空の新規環境に、項目別テーブルを使う `structured` モードで作成します。データベースは初回起動で作成されるため、移行コマンドの実行は不要です。既存DBへ `database_mode=structured` だけを追加して変換することはできません。既存環境ではそのモードを保持します。

```sh
sysrc liner_notes_enable=YES
sysrc liner_notes_bind=127.0.0.1
sysrc liner_notes_port=2222
sysrc liner_notes_language=ja
sysrc liner_notes_timezone=Asia/Tokyo
sysrc liner_notes_database=/var/db/liner-notes/liner.sqlite
sysrc liner_notes_database_mode=structured
sysrc liner_notes_registration=open
sysrc liner_notes_id_mode=free
sysrc liner_notes_rtbbs_member_policy=NO
```

この時点では外部から接続できません。`localhost`はそのコマンドを実行したホスト自身を指します。接続テストはBBSをインストールしたサーバー上で実行してください。

起動先が古いレビュー版を指していないか確認します。新規環境では既定の `/usr/local/sbin/liner-notes` を使います。

```sh
ls -l /usr/local/sbin/liner-notes
grep -nE '^liner_notes_(program|run_user|bind|port|database|database_mode)=' \
  /etc/rc.conf /usr/local/etc/liner-notes/liner_notes.conf
```

`sysrc -n 変数名` はrc.conf側の確認用です。Liner notes専用confだけにある既定値は表示されないことがあるため、上の両ファイルと併せて確認します。SQLite運用でも `notes.dat` / `members.dat` のパスは保存領域の識別・補助ファイル名に使います。「.datという名前だからSQLiteが無効」とは限りません。

<a id="transfer-setup"></a>
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

0.71以降は既存の検証記録があっても再実行できます。同一内容は保持し、変更時は旧記録を退避してから更新します。`receipt_status`が`created`、`unchanged`、`updated`のいずれかなら保存処理も完了しています。

既存のlrzszで失敗した場合は、出力と `lrz --version`、`lsz --version` を確認します。修正版への置換は、既存パッケージと利用サービスを確認してから実施してください。確認せずに `pkg delete -f` で依存関係を壊さないでください。lrzを交換した後は検証記録も更新します。

```sh
sysrc liner_notes_vault_dir=/var/db/liner-notes/vault
sysrc liner_notes_zmodem_helper=/usr/local/bin/liner-notes-zmodem
```

既定のhelperは上記のspool・エンジン・検証記録を使います。データの場所を変える場合、`liner_notes_data_dir`だけを変えてもhelperの場所は変わりません。専用helperの生成は [PORTS.md](PORTS.md) を参照してください。メール添付だけを使う構成では公開保管庫の初期化と `liner_notes_vault_dir` は不要です。

<a id="first-admin"></a>
## 6 起動と管理者の登録

```sh
service liner_notes start
service liner_notes status
sockstat -4l -P tcp -p 2222
telnet 127.0.0.1 2222
```

`sockstat`で `127.0.0.1:2222` の待受を確認します。Telnetではバナーとメニューが表示されます。`RTBBS-PROBE`は内部識別表示であり、それだけが表示されメニューがない場合は、後述のトラブル対応を確認してください。

BBS内で `REGISTER 希望するログイン名` を入力し、本番用のパスワードを設定します。名前は英小文字・数字・`_`・`-`で1〜32文字、パスワードは8〜128バイトです。パスワードは案内が出てから入力し、コマンドへ書きません。次に `ACCOUNT` で**会員番号**を確認し、`QUIT`で切断します。OSの `liner_notes` アカウントとBBS会員は別物です。`sysop`というログイン名やレベル5だけでは管理者になりません。

```text
REGISTER sysop
```

パスワード設定後:

```text
ACCOUNT
QUIT
```

次回ログインは `LOGIN sysop` と入力してからパスワードを入力します。主メニューの `O` / `Q` では終了確認があるため、案内に従って `Y` を入力します。

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

初回管理者を確立した後、承認制にする場合はOS側で `sysrc liner_notes_registration=approval` とし、再起動します。管理者はBBS内の `APPLICATIONS`、`APPLICATION 会員番号`、`APPROVE 会員番号` で申請を扱います。自由登録のままにする場合は `open`、受付を止める場合は `closed` です。

### 6.1 最初のボードと案内

BBS内で管理者として入力します。番号1は初期ボードです。既存構成の場合は先に `BOARDINFO 1` で内容を確認し、必要な変更だけ行います。

```text
BOARDINFO 1
BOARDSET 1|0|all|member|お知らせ|このBBSのご案内です。
INFOEDIT 1
```

`all`はゲストを含む閲覧、`member`は会員の投稿を許す指定です。`INFOEDIT`では複数行の案内を書き、画面の保存・中止案内に従います。初期設定でゲスト投稿を許可する必要はありません。詳細は `HELP ADMINBOARDS` を参照してください。

保管庫を有効にした場合、ルートの閲覧・登録を会員限定にする例:

```text
FILEBOARDSET -|member|member
FILEBOARDNAME -|ファイル保管庫
FILEINFOEDIT -
```

ボード内容・権限のオンライン変更は保存されます。通常のボード変更のたびにOSのサービスを再起動する必要はありません。

<a id="smoke-test"></a>
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

ZMODEM以外の方式を使う場合は、使用する端末がその方式・変種に対応していることも確認します。0.73の実転送の検証記録はリリース同梱の `evidence/stage73/` にあります。

<a id="backup-restore"></a>
## 8 初期バックアップと復元確認

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

DBには設定先がデータ領域外の論理ログも含まれます。これらは復元先の `external/元の絶対パス` 以下に対応するため、別の場所で起動するときはUSERLOGとチャットログの指定も合わせます。具体的な移行・復元・整理の仕様は [STAGE67.md](STAGE67.md) を参照してください。元データやバックアップを先に消さないでください。

**0.73の `db-restore` / `db-compact` は、元がstructuredでもschema 1（blocks形式）へ復元します。** 復元先の起動には `--database`、rc.dなら `liner_notes_database_mode=blocks`を使います。`--database-structured`では復元先を変換できません。structured形式のまま戻す必要がある場合は、停止中に取得したデータディレクトリ全体の物理バックアップも保持してください。[保存・復元形式の仕様](STAGE71.md)を参照してください。

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

<a id="public-telnet"></a>
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

### 9.1 標準のTelnetポート23で待ち受ける

2222で起動できても、通常のサービスユーザーには1024未満のポートをbindする権限がありません。`liner_notes_run_user=root`へ変えるとDoorゲームも使えなくなります。FreeBSDでは `mac_portacl` でサービスのUIDにTCP 23だけを許可できます。**すでに23で動いている環境では、このOS設定を変更する必要はありません。**

まず現在の待受と設定を確認します。

```sh
sockstat -4l -P tcp -p 23
id liner_notes
sysctl net.inet.ip.portrange.reservedlow net.inet.ip.portrange.reservedhigh
kldstat -m mac_portacl || kldload mac_portacl
sysctl security.mac.portacl
```

以下は通常の新規ホストで、既定の低番号ポート保護をMACポリシーへ置き換える例です。既存のMAC・jail・他サービスのポート制御があるホストでは、その運用規則と統合してください。`reservedhigh=0`だけを先に設定せず、MACの保護と許可規則を有効にしてから変更します。許可は実行ファイル名ではなくUIDに結び付きます。[FreeBSD mac_portacl(4)](https://man.freebsd.org/cgi/man.cgi?query=mac_portacl&sektion=4)の仕様に基づきます。

```sh
(
  set -eu
  LN_UID=$(id -u liner_notes)
  LN_RULE="uid:${LN_UID}:tcp:23"
  LN_RULES=$(sysctl -n security.mac.portacl.rules)
  case ",$LN_RULES," in
    *",$LN_RULE,"*) ;;
    *) LN_RULES="${LN_RULES:+$LN_RULES,}$LN_RULE" ;;
  esac
  sysctl security.mac.portacl.rules="$LN_RULES"
  LN_HIGH=$(sysctl -n security.mac.portacl.port_high)
  if [ "$LN_HIGH" -lt 1023 ]; then sysctl security.mac.portacl.port_high=1023; fi
  sysctl security.mac.portacl.suser_exempt=1
  sysctl security.mac.portacl.enabled=1
  sysctl net.inet.ip.portrange.reservedhigh=0
)
```

次に再起動後も有効になるよう、既存設定を保存してから永続化します。

```sh
LN_OS_BACKUP=$(mktemp -d /root/liner-notes-port23.XXXXXXXX)
for LN_FILE in /boot/loader.conf /etc/sysctl.conf; do
  if [ -f "$LN_FILE" ]; then cp -p "$LN_FILE" "$LN_OS_BACKUP/$(basename "$LN_FILE")"; fi
done
sysrc -f /boot/loader.conf mac_portacl_load=YES
sysctl security.mac.portacl.rules security.mac.portacl.port_high
ee /etc/sysctl.conf
```

`/etc/sysctl.conf`の同名項目を編集します。重複して追記せず、**rulesには上で確認した全既存規則を含めます**。64964は既定UIDの例です。既存の `port_high` が1023より大きければ、その値を維持します。

```text
security.mac.portacl.port_high=1023
security.mac.portacl.suser_exempt=1
security.mac.portacl.rules=uid:64964:tcp:23
security.mac.portacl.enabled=1
net.inet.ip.portrange.reservedhigh=0
```

ここでは実行中の値はすでに変更済みなので、BBSのポートを変更して確認します。

```sh
sysrc liner_notes_port=23
service liner_notes restart
service liner_notes status
sockstat -4l -P tcp -p 23
telnet 127.0.0.1 23
```

正常ならruntimeが23で待ち受け、メニューが表示されます。外部公開する場合はTCP 23のファイアウォール・NATも揃えます。WebUIを導入済みなら `web.toml` の `[bbs] port=23` も変更し、Webサービスを再起動します。保守時間にOS再起動後の状態も確認してください。設定途中で失敗した場合は2222へ戻してBBSを再起動し、MAC設定を無条件に解除することは避けます。

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

<a id="troubleshooting"></a>
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

<a id="webui-setup"></a>
## 12 WebUIとApache HTTPS

### 12.1 接続経路と準備

```text
ブラウザー → https://bbs.example.org/notes/
           → Apache → http://127.0.0.1:8088/notes/
                    → BBS 127.0.0.1:2222（23運用なら23）
```

WebコネクタとBBSは別サービスです。WebはDBを直接開かず、通常の会員認証・権限判定をBBSへ依頼します。まずTelnetでログインできることを確認します。BBS本体とWebUIは0.73に揃えます。

```sh
service liner_notes status
pkg info liner-notes
ls -l /usr/local/sbin/liner-notes-web /usr/local/etc/rc.d/liner_notes_web
id liner_notes_web
ls -ld /var/db/liner-notes-web/spool
```

Web実行ファイルがなければ、WEBUIオプションが無効です。既存サービスを停止・バックアップしてから [更新手順](UPGRADING.md)でWEBUIを有効にしたpkgへ入れ替えます。ports導入済み環境に手動インストーラーを重ねる必要はありません。

### 12.2 web.tomlを編集する

```sh
ee /usr/local/etc/liner-notes/web/web.toml
```

例の全体です。すでに設定がある場合は対応する項目を編集し、同じセクションを重複して追加しません。

```toml
[web]
listen = "127.0.0.1"
port = 8088
origin = "https://bbs.example.org"
base_path = "/notes/"
secure_cookies = true
max_sessions = 32
browser_idle_seconds = 1800
messages = "/usr/local/etc/liner-notes/web/messages"
templates = "/usr/local/etc/liner-notes/web/templates"
timezone = "Asia/Tokyo"
spool = "/var/db/liner-notes-web/spool"
max_upload_bytes = 1073741824
max_spool_bytes = 17179869184

[bbs]
host = "127.0.0.1"
port = 2222
telnet = true
timeout = 30
transfer_timeout = 120
```

変更する箇所:

| 項目 | 合わせる値 |
| --- | --- |
| `origin` | ブラウザーの公開元。`https://bbs.linernotes.site`など。`/notes/`を含めない |
| `base_path` | `/notes/`。Apacheの転送元・転送先とも一致させる |
| `[bbs] port` | 実際のruntime待受。23運用なら23。8088ではない |
| `secure_cookies` | HTTPS公開ではtrue。ブラウザーから平文HTTPでログインする設定ではない |
| `max_sessions` | Webの接続上限。上の32は接続枠に余裕を持たせた運用例、配布既定は64 |
| `transfer_timeout` | BBSへの最終登録・照合待ち。既定120秒、許容30〜600秒 |

Web利用者は匿名でもBBS接続を1本使います。補助端末・ゲーム画面ではさらに1本使うため、BBSの `connections` をすべてWebへ割り当てず、Telnet利用分も残します。

配置権限を確認します。標準アカウントの場合の設定です。

```sh
chown root:liner_notes_web /usr/local/etc/liner-notes/web/web.toml
chmod 640 /usr/local/etc/liner-notes/web/web.toml
chmod 755 /usr/local/etc/liner-notes/web \
  /usr/local/etc/liner-notes/web/templates /usr/local/etc/liner-notes/web/messages
chmod 644 /usr/local/etc/liner-notes/web/templates/*.html \
  /usr/local/etc/liner-notes/web/messages/*.json
su -m liner_notes_web -c '/bin/sh -s' <<'SH'
set -eu
test -r /usr/local/etc/liner-notes/web/web.toml
test -r /usr/local/etc/liner-notes/web/templates/index.html
test -r /usr/local/etc/liner-notes/web/templates/components.html
test -w /var/db/liner-notes-web/spool
echo 'Web permissions OK'
SH
```

`Web permissions OK`が出てから起動します。spoolは `liner_notes_web:liner_notes_web`・0700です。WebユーザーをBBSのグループへ追加したり、`/var/db/liner-notes`を公開したりする必要はありません。

```sh
sysrc liner_notes_web_enable=YES
sysrc liner_notes_web_config=/usr/local/etc/liner-notes/web/web.toml
service liner_notes_web start
service liner_notes_web status
sockstat -4l -P tcp -p 8088
tail -n 50 /var/log/liner-notes/web.log
```

既存レビュー版から切り替える場合は、配置確認後に `sysrc liner_notes_web_program=/usr/local/sbin/liner-notes-web` も指定します。すでに起動中なら `start` の代わりに `restart` を使います。

### 12.3 Apacheより手前で動作を確認する

curlがなければ `pkg install curl` で導入します。`Host`は実際の公開名へ置き換えます。

```sh
curl -i -H 'Host: bbs.example.org' http://127.0.0.1:8088/notes/
```

正常なら `200 OK` とHTMLです。これは疎通確認であり、ブラウザーのログイン確認はHTTPS側で行います。`Host`を省くと `Unexpected Host` の403になります。`[web] origin`が `https://bbs.example.org` なら `Host`に8088を付けません。

### 12.4 ApacheのVirtualHost

既存のApache設定をバックアップし、実際に読み込まれるVirtualHostファイルを編集します。

```sh
/usr/local/sbin/httpd -v
/usr/local/sbin/httpd -M
/usr/local/sbin/apachectl -S
```

`proxy_module`、`proxy_http_module`、`ssl_module`を確認します。足りなければApacheのモジュール設定で有効化します。以下は**このBBS専用ホスト名**の例で、既存のVirtualHostへ必要な項目を統合します。すでに80/443のListenがあれば重複して追加しません。`SSLCertificateFile`と`SSLCertificateKeyFile`には、取得済みのその公開名用証明書を指定します。証明書の発行・更新は既存のACME等の運用を使います。

```apache
<VirtualHost *:80>
    ServerName bbs.example.org
    Redirect permanent / https://bbs.example.org/
</VirtualHost>

<VirtualHost *:443>
    ServerName bbs.example.org
    SSLEngine on
    SSLCertificateFile "/usr/local/etc/letsencrypt/live/bbs.example.org/fullchain.pem"
    SSLCertificateKeyFile "/usr/local/etc/letsencrypt/live/bbs.example.org/privkey.pem"

    ProxyRequests Off
    ProxyPreserveHost On
    RedirectMatch 302 ^/$ /notes/
    RedirectMatch 308 ^/notes$ /notes/
    ProxyPass        /notes/ http://127.0.0.1:8088/notes/ upgrade=websocket timeout=600
    ProxyPassReverse /notes/ http://127.0.0.1:8088/notes/

    <Location /notes/>
        Require all granted
        LimitRequestBody 1073741824
    </Location>

    ErrorLog "/var/log/httpd-bbs-error.log"
    CustomLog "/var/log/httpd-bbs-access.log" combined
</VirtualHost>
```

`base_path=/notes/`なら転送先も `/notes/` です。転送先を `/` に変えて解決しようとしないでください。専用ホストの `/` は上の302で `/notes/`へ案内するため、トップページがDocumentRootのForbiddenになる問題も避けられます。既存サイトと共存させる場合は `/` のリダイレクトを省略します。

HTTPSのVirtualHost内に「同じHTTPS URLへ常時転送するRewriteRule」を入れないでください。80側だけでHTTPSへ転送します。WebSocketの `upgrade=websocket` はApache 2.4.47以降のHTTPプロキシを使います。[Apache公式のWebSocket転送](https://httpd.apache.org/docs/2.4/mod/mod_proxy_wstunnel.html)を参照してください。

```sh
/usr/local/sbin/apachectl configtest
```

`Syntax OK`を確認してから反映します。既存のApacheを使っている場合:

```sh
service apache24 reload
curl -I https://bbs.example.org/
curl -I https://bbs.example.org/notes/
```

トップは `/notes/`への転送、`/notes/`は200が目安です。新規Apacheでは設定後に `sysrc apache24_enable=YES`、`service apache24 start` とします。証明書エラーを `curl -k` で隠して公開完了としないでください。

### 12.5 ブラウザーでの確認と差し替え

`https://bbs.example.org/notes/`を開き、ログイン→記事表示→小さいファイルのアップロード・ダウンロード→メール添付→チャットの順に確認します。補助端末は別のBBS接続なので、端末内でもログインします。Web端末内ではZMODEMを使わず、Webのファイル選択画面を使います。

| 差し替えるもの | 編集する場所 | 反映 |
| --- | --- | --- |
| Webの名称・バナー・案内・時間帯挨拶 | `web/messages/ja.json`、`en.json` | 次回の画面取得 |
| Web全体のHTMLとフォーム | `web/templates/index.html`、`components.html` | 次回のページ取得 |
| origin・接続先・上限 | `web/web.toml` | Webサービス再起動 |
| Telnet側のバナー・挨拶 | `/usr/local/share/liner-notes/` | BBS再起動 |

表の `web/` は `/usr/local/etc/liner-notes/web/`です。JSONはUTF-8で編集し、末尾のカンマ・引用符の欠落に注意します。HTMLの `id`、`data-*`、フォームの `name` は画面処理が参照するので保持します。設定・外部文書の詳細は [WEBUI.md](WEBUI.md)にあります。

### 12.6 転送上限・再開の確認

転送上限は次のすべてを通過する必要があります。大きい値を一か所だけ設定しても、他の上限を超えると拒否されます。

| 場所 | この手順の値・役割 |
| --- | --- |
| Apache `LimitRequestBody` | 1073741824バイト（1GiB） |
| Web `max_upload_bytes` | 1073741824バイト、実装上限も1GiB |
| Web `max_spool_bytes` | 17179869184バイト、全Web転送作業領域の上限 |
| BBS `limits.conf` の `file_bytes` | 1073741824バイト |
| 公開保管庫・添付の各 `limits.conf` | 登録数・総容量・履歴件数の制限 |
| ファイルボード権限・ディスク | その会員の登録権限、両spoolと保存先の空き容量 |

「ブラウザーからの送信完了」と「BBSへの登録完了」は別です。登録中の進捗と最終結果を確認してから公開一覧を見ます。失敗した場合は保管庫の再開候補から対象を選び、同じファイルを選び直します。Web→BBSは確定済み位置から再開しますが、ブラウザー→WebのHTTP送信はファイルを送り直します。名前・サイズ・SHA-256の異なるファイルでは再開しません。

タイムアウトを増やす前に、どの段階で止まったか、BBS/Webのログ、空き容量、取得ファイルのSHA-256を確認します。小さいファイル、数十MiB、実際に使う大きさの順に試してください。1GiB×16・長時間の同時大量転送は0.73での検証完了を宣言していません。

<a id="web-troubleshooting"></a>
### 12.7 Webのトラブルを層ごとに切り分ける

```sh
service liner_notes status
service liner_notes_web status
sockstat -4l -P tcp -p 23,2222,8088
tail -n 80 /var/log/liner-notes/daemon.log
tail -n 80 /var/log/liner-notes/web.log
tail -n 80 /var/log/httpd-bbs-error.log
```

| 症状 | 確認・対処 |
| --- | --- |
| ページ全体が `BBS connection unavailable` | `[bbs] host/port/telnet`、実runtime、接続枠、BBSが0.73かを確認。Webプロセスの起動だけではBBS接続成功を意味しない |
| `Unexpected Host` | `origin`のホスト名とHostヘッダー、ApacheのProxyPreserveHostを一致させる |
| `Origin rejected` / CSRFエラー | HTTPSの公開元を揃え、古い画面を再読込してログイン。検査自体を無効化しない |
| Apacheの403 | `apachectl -S`で対象VirtualHostを確認。`/`から`/notes/`への案内、Location、既存認証規則を確認 |
| 502・503でWebのログに到達しない | Webの8088待受とProxyPass先、Apacheログを確認 |
| `Web template unavailable` / `Web file access failed` | ファイルの存在・UTF-8・親ディレクトリの通過権限・Webユーザーの読み取りを確認。0.73はファイルの問題を500で区別する |
| `unrecognized arguments: -P ...` | 旧rc.dが残っている。0.73の `/usr/local/etc/rc.d/liner_notes_web`へ更新する |
| ログインできてもすぐ戻る | 公開origin、HTTPS、secure cookie、Web再起動によるセッション失効を確認 |
| アップロード413 | ApacheとWebの上限を確認。上の1GiBを超えていないか確認 |
| 登録中に止まる | BBS側の上限・権限・空き容量・最終照合時間、spool所有者を確認 |
| ゲームが見えない | BBSのdoors設定と会員レベルを確認し、ページを再読込。利用可能なゲームがなければ非表示が正常 |

<a id="doors-setup"></a>
## 13 StarRoute Traders・The Clansとゲームメニュー

### 13.1 共通の設定

ゲームはLiner notesとは別配布です。BBSと同じ `liner_notes` ユーザーで動きます。すでに動いているゲームはその実行パス・作業ディレクトリ・世界データを保持します。設定ファイル全体を見本で置き換えません。

```sh
grep -n '^liner_notes_doors=' /etc/rc.conf /usr/local/etc/liner-notes/liner_notes.conf
cat /usr/local/etc/liner-notes/doors.conf
```

使用中のパスが標準と異なる場合は以後のコマンドもそのパスへ揃えます。編集前にコピーします。

```sh
cp -p /usr/local/etc/liner-notes/doors.conf \
  /usr/local/etc/liner-notes/doors.conf.before-edit-$(date +%Y%m%d-%H%M%S)
ee /usr/local/etc/liner-notes/doors.conf
```

1行は `番号|最低レベル|秒数|実行ファイル|作業ディレクトリ|表示名[|起動方式]`です。番号は未使用のものを選び、実行ファイルと作業先は絶対パスで指定します。

### 13.2 既存StarRoute Traders

既存のStarRoute行はそのまま残します。次は以前の単独BBSキットの標準パス例であり、別版を導入している場合はそのキットが生成した行を使います。

```text
1|0|3600|/usr/local/libexec/liner-notes/starroute-door|/var/db/starroute-traders|StarRoute Traders
```

ゲームのBBS識別子・会員番号とプレイヤーの関係を維持します。Liner notes更新のためにStarRouteの `init` を再実行しません。新規宇宙の作成・ゲーム本体の更新は、使用するStarRoute配布物のマニュアルに従います。[STARROUTE.md](STARROUTE.md)は0.18.0の旧単独導入キットについての記録です。

### 13.3 既存の /usr/local/clans を登録する

FreeBSDネイティブのThe Clansがすでに動く状態なら、未使用番号で次を追加します。

```text
2|0|3600|/usr/local/share/liner-notes/scripts/door_clans.sh|/usr/local/clans|The Clans|dorinfo1:1
```

`dorinfo1:1`は会員情報のドロップファイルを作成し、同時に1人だけ起動する指定です。既存の世界で `reset` は実行しません。ゲーム内のSYSOP権限はBBSの管理者権限から自動付与しません。

権限をサービスユーザーで確認します。

```sh
ls -ld /usr/local/clans /usr/local/clans/outbound
ls -l /usr/local/clans/clans /usr/local/clans/clans.cfg
su -m liner_notes -c '/bin/sh -s' <<'SH'
cd /usr/local/clans || exit 1
test -x ./clans && echo 'launcher: OK' || echo 'launcher: NG'
test -r ./clans.cfg && echo 'config: OK' || echo 'config: NG'
test -w . && echo 'work directory: OK' || echo 'work directory: NG'
SH
```

`clans`が小さいシェルスクリプトでも正常です。同じ場所の `clans.freebsd.amd64.opt`などを実行します。`work directory: NG`なら一時ドロップファイルやロックを作れません。`www`所有の既存ゲームデータも更新権限の確認が必要です。

必要な権限だけを与える手順は [DOORS.mdの既存Clans](DOORS.md#既存の-usrlocalclans-を使う場合)を参照してください。ゲームを停止してバックアップし、実際にあるデータファイルへグループ書き込みを許可します。ディレクトリだけ直しても既存の `.dat` が書けなければ進行を保存できません。実行ファイル・設定・世界全体を777にする必要はありません。

### 13.4 反映と動作確認

```sh
test -x /usr/local/share/liner-notes/scripts/door_clans.sh
chown root:wheel /usr/local/etc/liner-notes/doors.conf
chmod 644 /usr/local/etc/liner-notes/doors.conf
sysrc liner_notes_doors=/usr/local/etc/liner-notes/doors.conf
service liner_notes restart
service liner_notes status
```

BBS内でログインしてから:

```text
GAME
2
```

The Clansをゲーム内の終了操作で終えるとゲーム一覧へ戻ります。StarRouteも `GAME` → `1` → ゲーム内の `quit` → 一覧復帰を確認します。ゲーム番号1/2は設定例です。`0`でBBS主メニューへ戻ります。`DOOR 番号`で直接起動した場合は起動元のBBS画面へ戻ります。

Webではゲーム用の補助端末内でもログインします。画面サイズ変更はPTYへ伝わりますが、ゲーム側も現在の端末サイズを取り直す必要があります。満員・切断・制限時間・異常終了の扱いと、Clansの `online.flg` の回復は [DOORS.md](DOORS.md)を参照してください。ロックファイルの存在だけで稼働中と判断したり、プレイ中に削除したりしないでください。

<a id="upgrade-entry"></a>
## 14 既存環境の更新・元に戻す手順

具体的なコマンドは [UPGRADING.md](UPGRADING.md)を順に実施します。基本の順序は次のとおりです。

1. 使用中のプログラム、rc.conf、DBモード、Web設定、ゲーム行を記録する。
2. 新しい配布物を照合し、別の作業先へ展開する。
3. 利用者へ案内し、Web→BBSの順で停止する。ゲーム・転送処理が終わったことも確認する。
4. 旧pkg、設定・HTML・HELP、全データ、ゲーム世界、Web spoolを保存する。
5. packageまで構築して成功を確認し、旧pkgを除去して新pkgを導入する。データ初期化・ゲームresetは行わない。
6. 実行ファイルの存在を確認してから、レビュー版の起動先を正式配置へ切り替える。
7. 編集済みの運用ファイルと新しい `.sample` を比較し、新しい項目を取り込む。
8. BBS→Webの順で起動し、記事・添付・ゲームを確認する。

動かない場合は両サービスを停止し、保存した旧pkg・旧プログラム・旧設定へ戻します。更新後に投稿があった場合は新しいデータも保存してから扱います。DBモードを切り替えて戻そうとしたり、保存形式の異なる旧版へ稼働中のDBを上書きしたりしないでください。

<a id="config-reference"></a>
## 15 よく変更する設定と上限

| 設定場所 | 対象・反映方法 |
| --- | --- |
| `/etc/rc.conf`の`liner_notes_*` | BBSの待受・保存先・機能・外部文書。BBS再起動 |
| `/usr/local/etc/liner-notes/liner_notes.conf` | BBSの配布既定値。rc.confが優先 |
| `admins.conf` | 管理者の会員番号を1行に1つ。BBS再起動・再ログイン |
| `doors.conf` | 外部ゲーム。BBS再起動 |
| `limits.conf` | 会員・ボード・本文・接続・転送の運用上限。BBS再起動 |
| `/var/db/liner-notes/vault/limits.conf` | 公開保管庫の累計ファイル数・総容量・履歴件数。BBS再起動 |
| `/var/db/liner-notes/members.dat.mailfiles/limits.conf` | メール添付の同じ上限。membersパスを変更した場合はそれに対応させる |
| `/usr/local/etc/liner-notes/web/web.toml` | Webの公開元・BBS接続先・作業領域。Web再起動 |

`limits.conf`の配布値は次のとおりです。これらの資源上限は0を指定できず、値を下げる運用に使います。

| キー | 最大値 |
| --- | --- |
| `members` / `boards` | 999999 / 1024 |
| `input_bytes` / `body_bytes` | 4096 / 262144（256KiB） |
| `file_bytes` | 1073741824（1GiB） |
| `batch_files` / `batch_bytes` | 16 / 17179869184（16GiB） |
| `connections` / `transfers` | 128 / 24 |

記事累計・メール累計・会員ごとの投稿数は別設定です。`liner_notes_max_articles`、`liner_notes_max_mail`、`liner_notes_max_user_posts`は0で件数の運用制限なしになります。ただしID表現の上限・ディスク容量は残ります。

保管庫・添付の見本は `/usr/local/etc/liner-notes/vault-limits.conf`です。新規作成した各保存先へ必要に応じてコピーして編集します。すでに `limits.conf` があれば上書きしません。配布値は `max_files=2000`、`max_events=10000`、`max_bytes=17179869184`で、この3項目は0でその制限を無効にできます。一般の `limits.conf` とキー・0の意味が異なります。

接続履歴・チャットログ・SQLite・spoolを、通常のテキストログと同じ感覚で稼働中に削除・切り詰めないでください。データ整理は停止・バックアップ後に [外部管理ツール](MAINTENANCE.md)で別コピーへ行います。

<a id="acceptance"></a>
## 16 公開前の最終確認と検証範囲

- [ ] 管理者と一般会員の両方でログインでき、一般会員に管理権限がない。
- [ ] 記事・返信・メール・添付・保管庫が再起動後も残る。
- [ ] 公開名・ポート・TLS証明書・Web originが一致している。
- [ ] Webアップロードは「BBS登録完了」まで確認し、取得したファイルのSHA-256が一致する。
- [ ] TelnetのZMODEM送受信と、中断後の再開・再試行を端末で確認した。
- [ ] StarRouteとClansの両方が起動し、終了後にゲーム一覧へ戻り、進行が保存される。
- [ ] 設置した全データ・ゲーム世界・設定を含むバックアップと、別ディレクトリへの復元を確認した。
- [ ] 予定した保守時間にOS再起動後の自動起動・ポート23・Webを確認した。
- [ ] 配布物・旧pkg・バックアップ・設定した値を保管した。

0.73のリリース検証はFreeBSD 15.1/amd64、portsの標準/別PREFIX構成、0.71からの更新・再導入、WebUI、汎用Door、実lrzsz送受信・再開を含みます。Rocky Linux 9/AlmaLinux 9のコンテナCIも成功しています。記録は配布セットの `evidence/stage73/verification.json`、または [GitHubの検証記録](https://github.com/matsuotakeshi/liner-notes/tree/v0.73/evidence/stage73)にあります。

この改訂は0.73のソース・配置・設定に照合した導入文書です。各設置先のDNS、証明書、ファイアウォール、既存ゲーム世界まで自動検証したものではありません。Linuxを使う場合はFreeBSD用rc.d・sysrc・pkgのコマンドを実行せず、[Rocky Linux / AlmaLinux手順](LINUX.md)を使ってください。
