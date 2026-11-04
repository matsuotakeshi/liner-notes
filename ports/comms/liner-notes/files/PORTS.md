> これはstage73レビュー版です。[変更内容・構築方法](STAGE73.md)を参照してください。正式公開と本番切替は未実施です。既存DB形式は自動変換しません。

# Liner notes FreeBSD port 0.73.a1（レビュー版）

サービス配置の基礎は [インストールと運用開始](INSTALL.md)、今回の差分は [STAGE73.md](STAGE73.md) を参照してください。INSTALLにある0.72公開アーカイブの取得例は、今回のレビュー版の取得方法ではありません。

> stage67: 正式なSQLite保存を使う場合の移行・バックアップ・復元・整理と新しいrc.conf項目はSTAGE67.mdを参照してください。旧backup/compactではなくdb-backup/db-compactを使用します。

stage72までの機能にWebUI帳票・転送改善・DB変換・シリアル／共有ボードコネクタを追加しました。stage65-r1のインストール配置を維持しています。

## レビュー版の構築

FreeBSDのportsツリーと依存パッケージが必要です。portsアーカイブを空のディレクトリへ展開し、ソースアーカイブを指定するDISTDIRへ置きます。

```sh
cd /path/to/extracted-port/comms/liner-notes
make BATCH=yes OPTIONS_SET="DOCS WEBUI LINK" \
    DISTDIR=/absolute/path/to/distfiles stage check-plist stage-qa package
```

DISTDIRには `liner-notes-stage73-review.tar.gz` が必要です。公開取得URLは設定していません。上の操作はstage/packageまでで本番にはインストールしません。今回の配布物はソースとportsで、依存関係を含む導入済みシステム一式ではありません。

## 配置

| 対象 | 既定の配置 |
|---|---|
| daemon本体 | `/usr/local/sbin/liner-notes` |
| バックアップ・検証・復元・整理 | `/usr/local/bin/liner-notes-admin` |
| 会員・保管庫の管理CLI | `/usr/local/bin/liner-notes-memberctl`、`liner-notes-vaultctl` |
| その他の公開コマンド | `/usr/local/bin/liner-notes-maintenance`、`liner-notes-zmodem`、`liner-notes-check-resume`、`liner-notes-ssh` |
| 追加コネクタ・DB変換 | `/usr/local/bin/liner-notes-serial`、`liner-notes-link`（LINK有効時）、`liner-notes-dbconvert` |
| バナー、HELP、メニュー、補助スクリプト | `/usr/local/share/liner-notes/` |
| 設定 | `/usr/local/etc/liner-notes/` |
| データ | `/var/db/liner-notes/` |
| 転送用一時領域 | `/var/db/liner-notes/spool/` |
| daemon出力・接続履歴・チャットログ | `/var/log/liner-notes/daemon.log`、`user.log`、`chat.log` |
| 監督プロセスPID | `/var/run/liner-notes.pid` |
| BBS子プロセスPID | `/var/run/liner-notes.child.pid` |
| rc.d | `/usr/local/etc/rc.d/liner_notes` |
| 説明書（DOCS有効時） | `/usr/local/share/doc/liner-notes/` |

記事操作履歴や転送履歴など、データの整合性に関わる保存ファイルは引き続きデータディレクトリに置きます。`PREFIX`の変更にも対応します。`/var`配下の既定値はPREFIXに連動しません。

サービス名とOSアカウントは `liner_notes` のままです。データ・spool・ログのディレクトリは所有者 `liner_notes:liner_notes`、権限0700です。新規アカウントのホームは `/var/db/liner-notes`、ログインシェルはnologinです。私用UID/GID **64964**（BBS）、**64965**（WEBUI有効時） を使うローカルportです。導入前に衝突を確認し、必要なら `files/UIDs` と `files/GIDs` を変更して再ビルドしてください。

新しい互換モード・ID・接続文書の設定は [STAGE69.md](STAGE69.md) を参照してください。互換モードは既定OFFです。

## rc.confからの設定と起動

`/usr/local/etc/liner-notes/liner_notes.conf` を読み、その後の `/etc/rc.conf` の値が優先されます。インストールだけでは起動しません。設定例:

```sh
# /etc/rc.conf
liner_notes_enable="YES"
liner_notes_bind="127.0.0.1"
liner_notes_port="2222"
liner_notes_language="ja"
liner_notes_data_dir="/var/db/liner-notes"
liner_notes_log_dir="/var/log/liner-notes"
# 初期会員の登録後に指定（会員番号を1行ずつ記載）:
# liner_notes_admins="/usr/local/etc/liner-notes/admins.conf"
```

```sh
service liner_notes start
service liner_notes status
service liner_notes restart
service liner_notes stop
```

既定でTelnetメニューを有効にして起動します。最初はサーバー内から `telnet 127.0.0.1 2222` で登録・管理者設定を済ませます。外部公開するときは `liner_notes_bind="0.0.0.0"` 等へ変更してください。

既定の作業ディレクトリは `liner_notes_data_dir` です。`liner_notes_program`、`liner_notes_home`、`liner_notes_board`、`liner_notes_members`、`liner_notes_catalog`、`liner_notes_pidfile`、`liner_notes_child_pidfile` も個別指定できます。HELP等は `liner_notes_help_dir`、`liner_notes_menu_dir`、`liner_notes_banner`、ログは `liner_notes_logfile`、`liner_notes_user_log`、`liner_notes_chat_log` で上書きできます。接続履歴・チャットログを無効にする場合は各パスを `-` にします。

任意のシェル文字列を評価するflags方式ではなく、各rc.conf変数を対応する引数へ組み立てます。会員ID発行方式は `liner_notes_id_mode="free"` / `"auto"`、申請フォームは `liner_notes_registration_form="simple"` / `"detailed"` です。

## メニューとHELPの更新

**実行ファイルとメニュー・HELPを一緒に更新してください。** 編集済みファイルは@sampleにより保持されるため、新しい `share/liner-notes/menus/*.sample`、`help/*/*.sample` と比較して変更を反映します。主画面のMはメール、Eは共通エディタ、Oは接続終了です。保管庫の番号入力はファイル操作画面を開きます。D 番号は直接取得、Bはバッチ画面です。新しいサブメニューはboard-/reader-/settings-/profile-の各言語ファイルです。詳細はCOMMANDS.mdを参照してください。

## 旧portからの更新

**停止・バックアップ後に更新してください。ファイルやデータの自動移動は行いません。** 古い配布アーカイブと更新前バックアップを残してください。0.70では会員の行番号・転送方式の保存項目が増えます。その設定を保存した会員データは0.69系で読み出せません。schema 2は0.70以降、今回の新規schema 3は0.71以降が必要です。旧版に戻す場合は更新前バックアップを使ってください。以前の版から0.69へ更新した後のQ履歴・新しい転送方式名は旧実行ファイルでは読めません。旧版へ戻す場合は更新前のバックアップ一式を復元します。

1. 旧rcスクリプトで `service liner_notes stop` を実行し、転送ワーカーも停止していることを確認します。
2. 旧データ全体、旧設定、編集したバナー・HELP・メニュー、既存ログを保存します。
3. ローカルpkgの置換は、対象版のインストール・削除試験を別環境で完了してから行います。以前の配置移行では旧パッケージを除去して新パッケージを導入しました。今回のレビュー版では本番向けの置換試験は未完了です。依存パッケージのautoremoveは行わず、編集済み `liner_notes.conf` と `.sample`、`rc.conf` にある旧指定を比較します。
4. 旧 `liner_notes_home="/usr/local/libexec/liner-notes"` は削除し、必要ならデータディレクトリを指定します。本体の指定は `/usr/local/sbin/liner-notes` へ変更します。
5. 編集済み表示ファイルは旧 `/usr/local/etc/liner-notes/` 配下から `/usr/local/share/liner-notes/` の対応箇所へコピーします。旧配置を使い続ける場合はmenu/help/bannerの変数で明示できます。
6. 旧 `/var/db/liner_notes/` を新 `/var/db/liner-notes/` へ、属性を保って**コピー**します。新側に運用データがある場合は混ぜず、別の空ディレクトリで検証してください。移動しない場合は `liner_notes_data_dir="/var/db/liner_notes"` を明示します。旧 `liner_notes_board` 等の個別指定があれば、それらも確認します。
7. 旧接続履歴・チャットログを使っていた場合は停止中に新ログディレクトリへコピーするか、`liner_notes_user_log` / `liner_notes_chat_log` に旧ファイルの絶対パスを明示します。既定の旧名前は通常 `members.dat.userlog` / `members.dat.chatlog` です。新ログファイルへ既存データを上書きしないでください。
8. ZMODEM helper、spool、再開検証記録、ゲームの実行パスを新配置へ合わせます。PID指定は `/var/run/liner-notes.pid` と `/var/run/liner-notes.child.pid` へ変更します。
9. 所有者・権限を確認し、起動・ログイン・記事読出し等を確認します。旧データは確認後もバックアップとして残せます。

旧homeの既定値が残る場合、または旧既定データだけが存在するのに新しい空の既定データで起動しようとする場合、rcスクリプトは移行案内を出して起動を拒否します。任意の独自パスを自動判別するものではありません。既存OSアカウントのホームディレクトリも自動変更しませんが、サービスの作業ディレクトリはrc設定から決まります。

設定・バナー・メニュー・HELPには `@sample` を使い、同じ設置先の編集済みファイルを更新時に上書きしません。削除時もデータ、編集済みファイル、サービスアカウントを残します。古い設定で直接使っていた `/usr/local/sbin/liner-notes-*` や `libexec` の実行パスは新配置へ変更が必要です。

## 管理とバックアップ

```sh
liner-notes-admin --help
liner-notes-admin db-backup --root /var/db/liner-notes --database /var/db/liner-notes/liner.sqlite --board notes.dat --members members.dat /backup/liner-notes-data.tar.gz
liner-notes-admin verify /backup/liner-notes-data.tar.gz
liner-notes-admin db-compact /backup/liner-notes-data.tar.gz /backup/liner-notes-compacted --all-stores --reclaim-withdrawn
```

これらは停止中に実行します。SQLite運用の例です。`db-restore` / `db-compact` は新しいディレクトリへ出力し、原本やバックアップを置換しません。`liner-notes-maintenance` は同じ操作への別名です。インストール済み `liner-notes-memberctl` / `liner-notes-vaultctl` を自動で選び、`--tools-dir` による指定も可能です。

**db-backupはSQLite内の論理記録と指定root内の実ファイルを保存します。外部設定・表示ファイル・daemon出力ログ・rc.confも別途保存してください。** 例（停止中、root、出力先は事前に準備）:

```sh
tar -czpf /backup/liner-notes-support.tar.gz -C / \
    usr/local/etc/liner-notes usr/local/share/liner-notes var/log/liner-notes
```

`rc.conf`のLiner notes設定も保存してください。復元は一旦別ディレクトリへ展開して確認します。ログ保持整理を行う場合は、ログのコピーをバックアップroot内へ収めて 旧形式の別コピーに対して `compact --log 相対パス` を使うか、補助スクリプト `share/liner-notes/scripts/log-retain.py` を使います。稼働中ログの切り詰めはしないでください。

保管庫の初期化はサービスユーザーで行います:

```sh
su -m liner_notes -c '/usr/local/bin/liner-notes-vaultctl init /var/db/liner-notes/vault'
```

## ZMODEM・外部ゲーム

DOCSとWEBUIは既定ON、ZMODEMとLINK依存は既定OFFです。ZMODEMを選ぶと本家FreeBSDのcomms/lrzszを依存に記録します。独自lrzsz portsは同梱しません。WEBUIを無効にする場合はOPTIONS_UNSET=WEBUIを指定します。LINKはcryptographyを依存に追加します。鍵・共有設定は [BBS_LINKS.md](BBS_LINKS.md)、COMは [SERIAL.md](SERIAL.md) を参照してください。

```sh
su -m liner_notes -c '/usr/local/bin/liner-notes-check-resume /usr/local/bin/lrz /usr/local/bin/lsz --receipt /var/db/liner-notes/lrz-resume.json'
```

受信バイナリを交換したら再検証してください。成功時に検証ツールが旧記録を退避して更新します。設定例:

```sh
liner_notes_vault_dir="/var/db/liner-notes/vault"
liner_notes_zmodem_helper="/usr/local/bin/liner-notes-zmodem"
# ゲームを登録した場合:
liner_notes_doors="/usr/local/etc/liner-notes/doors.conf"
```

既定のZMODEM helperは `/var/db/liner-notes/spool` を使います。別データ領域や別エンジンを使う場合は `share/liner-notes/scripts/configure-zmodem.py` で専用helperを生成してください。メール添付だけならvault_dirは不要です。ゲームのサンプルは新配置に対応済みです。詳細はDOORS.mdを参照してください。

## 検証・公開準備

今回の機能・転送・ports検査は [STAGE73.md](STAGE73.md) と別添のstage73検証記録を参照してください。旧版の機能試験とは分けて記録します。既存の稼働BBSやホストのrc.confは変更しません。

GPL-3.0-only表記は維持しています。レビュー版には公開ソース取得URLを設定していません。公式Ports Collectionへの提出は別作業で、担当者の連絡先と公式UID/GID割当の確定が残ります。現在は私用UID/GIDの配布portです。poudriereや他FreeBSD版・他ABIでの検証とは区別してください。
