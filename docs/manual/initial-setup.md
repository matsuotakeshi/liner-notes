# 初期設定と公開前の確認

<a id="acceptance"></a>

<a id="initial-config"></a>

## 初回起動の設定

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
## 保管庫とZMODEMの準備

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

現行版は既存の検証記録があっても再実行できます。同一内容は保持し、変更時は旧記録を退避してから更新します。`receipt_status`が`created`、`unchanged`、`updated`のいずれかなら保存処理も完了しています。

既存のlrzszで失敗した場合は、出力と `lrz --version`、`lsz --version` を確認します。修正版への置換は、既存パッケージと利用サービスを確認してから実施してください。確認せずに `pkg delete -f` で依存関係を壊さないでください。lrzを交換した後は検証記録も更新します。

```sh
sysrc liner_notes_vault_dir=/var/db/liner-notes/vault
sysrc liner_notes_zmodem_helper=/usr/local/bin/liner-notes-zmodem
```

既定のhelperは上記のspool・エンジン・検証記録を使います。データの場所を変える場合、`liner_notes_data_dir`だけを変えてもhelperの場所は変わりません。専用helperの生成は [転送設定](transfers.md#専用helperの生成) を参照してください。メール添付だけを使う構成では公開保管庫の初期化と `liner_notes_vault_dir` は不要です。

<a id="first-admin"></a>
## 起動と管理者の登録

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

### 最初のボードと案内

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
## 公開前の動作確認

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

ZMODEM以外の方式を使う場合は、使用する端末がその方式・変種に対応していることも確認します。
