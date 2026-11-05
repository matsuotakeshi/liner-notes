# FreeBSDへの導入

<a id="preflight"></a>

## 導入前の確認

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
## 配布物とportsのインストール

配布物は `liner-notes-ports-stage74.tar.gz`、portsの版は **0.74**です。[正式リリース](https://github.com/matsuotakeshi/liner-notes/releases/tag/v0.74)から取得します。以下は空の作業ディレクトリでダウンロードと照合を行う例です。

途中でエラーが出たら、その手順を解決してから次へ進みます。以下では `$()` を使うため、rootのログインシェルがcsh/tcshの場合も、最初に `sh` を起動してください。

```sh
/bin/sh
LN_WORK=$(mktemp -d /root/liner-notes-0.74.XXXXXXXX)
cd "$LN_WORK"
(
  set -eu
  fetch -o liner-notes-ports-stage74.tar.gz https://github.com/matsuotakeshi/liner-notes/releases/download/v0.74/liner-notes-ports-stage74.tar.gz
  fetch -o SHA256SUMS https://github.com/matsuotakeshi/liner-notes/releases/download/v0.74/SHA256SUMS
  LN_EXPECTED=$(awk '$2 == "liner-notes-ports-stage74.tar.gz" {print $1}' SHA256SUMS)
  test -n "$LN_EXPECTED"
  test "$(sha256 -q liner-notes-ports-stage74.tar.gz)" = "$LN_EXPECTED"
  echo 'SHA256 OK'
  tar -xzf liner-notes-ports-stage74.tar.gz
)
```

`SHA256 OK`と展開成功を確認したら進みます。ファイルをWindowsから転送した場合も、同じ `SHA256SUMS` で照合します。接続し直した場合は、作業先へ `cd` し直してください。`pwd`の出力も控えておくと便利です。

```sh
cd "$LN_WORK/liner-notes-ports-stage74"
pwd
ls comms/liner-notes/Makefile distfiles/liner-notes-stage74.tar.gz
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

Web不要なら `OPTIONS_SET="DOCS ZMODEM" OPTIONS_UNSET=WEBUI` を指定します。ZMODEM不要なら `OPTIONS_SET="DOCS WEBUI" OPTIONS_UNSET=ZMODEM` とし、後のlrzsz検査とhelper設定を省略します。保管庫のWeb転送にlrzszは不要です。共有ボードリンクも使う場合は `OPTIONS_SET` に `LINK` を追加し、[BBS間リンクとスター・リング型共有](links.md)の専用会員・鍵・共有設定を行ってください。

**pkgで導入する場合は、上のmake installとどちらか一方を選びます。** 同梱pkgはFreeBSD 15/amd64・Python 3.12用、DOCS/WEBUI/LINK有効です。ZMODEMの依存指定は無効なので、使う場合は本家lrzszを別途導入します。配布物のルートから実行します。

```sh
pkg install python312 py312-sqlite3 py312-aiohttp py312-cryptography sqlite3
pkg add packages/liner-notes-0.74.pkg
pkg info liner-notes
```

異なるABIやPython構成ではportから構築します。`pkg install liner-notes`で公式リポジトリから取得する手順ではありません。Liner notes自体は、このリリースのローカルport/pkgです。
