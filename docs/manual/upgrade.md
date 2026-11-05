# バージョンアップと切り戻し

<a id="upgrade-entry"></a>

[詳細導入マニュアル](install-freebsd.md)と併せて使用します。

## 0.74から0.74.1への更新

HELPとマニュアルを訂正した修正版です。既存DBの変換、ボードやゲームの作り直しは不要です。本体の版表示は0.74.1になります。

未編集のHELPはパッケージ更新で新版へ置き換わります。編集済みのHELPは保持されるため、`help/ja/` と `help/en/` の `content.txt`、`settings.txt`、`unread.txt` を新しい `.sample` と比較し、必要な訂正を手動で取り込んでください。サイト用ZIPの更新だけではBBS内HELPは変わりません。

Linuxのソース導入でも、旧 `.sample` と同じ内容のHELPは更新します。編集済み、または比較用の旧sampleがない既存HELPは保持します。新しいマニュアルの入口はFreeBSDで `/usr/local/share/doc/liner-notes/index.html`、RPMで `/usr/share/doc/liner-notes/index.html` です。Gitから直接ソース導入する場合はHTML未生成なら `index.md` を使用します。

以下の停止・バックアップ・更新手順に従ってください。サービスや利用者データを自動的に初期化する処理はありません。

更新前に利用者へ停止を案内してください。ゲームの初期化や既存DBの形式変更は不要です。

先に [配布物の取得・SHA-256照合](install-freebsd.md#install-package)を済ませ、展開した配布物のルートを控えます。この段階で新規構築のmake install、保管庫init、REGISTERを繰り返す必要はありません。

現在の設定を確認します。

```sh
pkg info liner-notes
service liner_notes status
service liner_notes_web status
grep -nE '^liner_notes_(program|data_dir|board|members|database|database_mode|doors|run_user|bind|port)=' \
  /etc/rc.conf /usr/local/etc/liner-notes/liner_notes.conf
grep -nE '^liner_notes_web_(program|config|user)=' /etc/rc.conf
```

**既存DBのモードと保存先は保持します。** 新規構築用の `database_mode=structured` を既存DBへ追加して切り替える手順ではありません。現在のサービス実行ユーザーが標準と違う場合、以下の所有者指定も合わせます。

## 1. 現在の設定とバックアップ

rootの `/bin/sh` で実行します。バックアップ先は毎回新しく作成します。

```sh
/bin/sh
set -eu
LN_BACKUP=$(mktemp -d /root/liner-notes-backup-XXXXXXXX)
cp -p /etc/rc.conf "$LN_BACKUP/rc.conf"
pkg info liner-notes > "$LN_BACKUP/package-info.txt"
if service liner_notes_web onestatus >/dev/null 2>&1; then service liner_notes_web stop; fi
if service liner_notes onestatus >/dev/null 2>&1; then service liner_notes stop; fi
if service liner_notes onestatus >/dev/null 2>&1; then echo 'BBS is still running'; exit 1; fi
if service liner_notes_web onestatus >/dev/null 2>&1; then echo 'Web is still running'; exit 1; fi
tar -cpf "$LN_BACKUP/config.tar" -C / usr/local/etc/liner-notes
tar -cpf "$LN_BACKUP/data.tar" -C / var/db/liner-notes
tar -cpf "$LN_BACKUP/assets-logs.tar" -C / usr/local/share/liner-notes var/log/liner-notes
if [ -d /var/db/liner-notes-web ]; then
  tar -cpf "$LN_BACKUP/web-spool.tar" -C / var/db/liner-notes-web
fi
pkg create -o "$LN_BACKUP" liner-notes
printf 'Backup: %s\n' "$LN_BACKUP"
```

独自データパス、Webのspool、ゲームの設定・世界データは実際の場所も別途バックアップします。
レビュー版のruntime・Webプログラムを使っている場合はその実体も保存してください。
既存StarRoute/Clansの設定・世界を初期化しないでください。

標準配置のゲームがある場合の追加バックアップ例です。ゲームやその保守プロセスが停止してから実行します。実際の配置が別ならそちらを保存します。

```sh
if [ -d /var/db/starroute-traders ]; then
  tar -cpf "$LN_BACKUP/starroute-data.tar" -C / var/db/starroute-traders
fi
if [ -d /usr/local/etc/starroute-traders ]; then
  tar -cpf "$LN_BACKUP/starroute-config.tar" -C / usr/local/etc/starroute-traders
fi
if [ -d /usr/local/clans ]; then
  tar -cpf "$LN_BACKUP/clans.tar" -C / usr/local/clans
fi
ls -lh "$LN_BACKUP"
```

`pkg create`で保存されるのは登録済みパッケージです。レビュー版runtimeは含まれません。たとえば起動先が標準以外のruntimeなら、そのファイルも `cp -p` でバックアップ先へコピーします。旧プログラムと旧設定の組を残してから先へ進んでください。

## 2. portsで構築・配置

新しい配布物を空のディレクトリへ展開し、SHA256SUMSを確認します。
配布物のルートから実行してください。最初にpackageまで作成するため、
ビルド失敗時は旧パッケージをそのまま残せます。

接続し直した場合は `LN_BACKUP` を実際のバックアップ先で設定し直します。カレントディレクトリにも注意してください。

```sh
cd comms/liner-notes
make BATCH=yes OPTIONS_SET="WEBUI ZMODEM" DISTDIR="$(pwd)/../../distfiles" package
pkg delete liner-notes
make BATCH=yes OPTIONS_SET="WEBUI ZMODEM" DISTDIR="$(pwd)/../../distfiles" install
test -x /usr/local/sbin/liner-notes
test -x /usr/local/sbin/liner-notes-web
pkg info liner-notes
```

編集済みの設定・メニュー・HELP・Webテンプレート・メッセージは保持します。
既存のweb.tomlがroot専用ならWebアカウントに読み取りを許可してください。
Web設定の `[bbs] port` は実際の待受ポート（23等）に合わせます。
既存のWeb実行ユーザーが異なる場合、rc.confの指定とファイル権限もそのユーザーに揃えます。

```sh
chown root:liner_notes_web /usr/local/etc/liner-notes/web/web.toml
chmod 640 /usr/local/etc/liner-notes/web/web.toml
liner-notes-admin upgrade-check --assets /usr/local/etc/liner-notes --assets /usr/local/share/liner-notes
```

新sampleと独自HTMLの差を確認して統合します。既存DBモード、ポート23の権限設定、
doors.conf内のStarRoute行は維持します。レビュー版へのプログラム指定がある場合、
上記の配置確認が成功してから切り替えます。

```sh
sysrc liner_notes_program=/usr/local/sbin/liner-notes
sysrc liner_notes_web_program=/usr/local/sbin/liner-notes-web
```

## 3. 再開検査と起動

同梱pkgで更新する場合は、上のmakeによる構築・導入の代わりに、配布物のルートから `pkg delete liner-notes` → `pkg add packages/liner-notes-0.74.1.pkg` を実行します。必要な依存関係は [導入マニュアル](install-freebsd.md#install-package)を参照してください。古いpkgの削除前に、新pkgと依存ソフトが揃っていることを確認します。`pkg autoremove`や依存関係を無視する強制削除は行いません。

`upgrade-check`は差分の検出であり、自動統合しません。運用ファイルを直接編集した場合は、その内容を残しながら `.sample` の新しい要素を取り込みます。WebのHTMLに新しいフォームがなければ、本体だけ更新しても新機能の入口は出ません。

lrzszを交換した場合はサービス所有者で再開検査を実施します。記録が存在していても
再実行でき、成功時だけ更新されます。既存の検査記録を先に削除する必要はありません。

```sh
su -m liner_notes -c '/usr/local/bin/liner-notes-check-resume /usr/local/bin/lrz /usr/local/bin/lsz --receipt /var/db/liner-notes/lrz-resume.json'
service liner_notes start
service liner_notes status
service liner_notes_web start
service liner_notes_web status
sockstat -4l -P tcp -p 23,2222,8088
```

Webを使用しない構成はWeb関連の手順を省略します。ログイン、記事、添付・保管庫転送、
GAMEからStarRoute/Clans起動と一覧復帰を確認してください。
`Invalid liner_notes configuration` の場合は最初に `sysrc -n liner_notes_program` と
そのパスの `ls -l` を確認します。runtime未配置のままパスだけを変更しないでください。

## 4. 戻す場合

Web起動後の詳細確認・エラー表は [WebUI導入](web-server.md#webui-setup)を参照してください。

両サービスを停止し、保存した旧pkgを再導入します。レビュー版を使っていた場合は
保存した旧プログラムとrc.confの起動先を戻します。編集した設定はバックアップと比較して戻します。
DBモードの自動変更はありませんが、旧版へ戻すときはその版が読める形式か確認してください。
データを復元する場合は更新後のデータも別に保存し、新しい投稿やゲーム進行を失わないようにします。
0.69以前へ戻す場合は特に会員設定・DB形式の互換性に注意が必要です。

旧pkgを使って戻す操作例です。`LN_OLD_PKG`は `ls "$LN_BACKUP"` で確認した実ファイルへ変更してください。`set -e`の有効な上記のshで実行し、旧ファイルが存在することを確認してから新pkgを除去します。

```sh
if service liner_notes_web onestatus >/dev/null 2>&1; then service liner_notes_web stop; fi
if service liner_notes onestatus >/dev/null 2>&1; then service liner_notes stop; fi
LN_OLD_PKG="$LN_BACKUP/実際に保存した旧版.pkg"
test -f "$LN_OLD_PKG"
pkg delete liner-notes
pkg add "$LN_OLD_PKG"
```

保存したrc.conf全体を無条件に上書きすると他サービスの変更も戻ります。バックアップと比較し、Liner notesの起動先・設定値を戻してください。レビュー版へ戻す場合は保存した実行ファイルが以前のパスにあることも確認します。単なる起動先の修正で直る問題に、データの復元は不要です。

Linuxの更新は[Linux導入](install-linux.md)のRPM更新、または停止・バックアップ後の `install-linux` を使用します。本体・Web・リンクを同じ版にそろえてください。共有ボードはprotocol 2対応ノード同士で運用し、更新中は交換を止めます。
