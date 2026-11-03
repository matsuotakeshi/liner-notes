# 0.72への更新

0.71およびWeb/Doorレビュー版からの更新です。本番で利用者へ停止を案内してから行います。
ゲームの初期化や既存DBの形式変更は不要です。

## 1. 現在の設定とバックアップ

rootの `/bin/sh` で実行します。バックアップ先は毎回新しく作成します。

```sh
/bin/sh
set -eu
LN_BACKUP=$(mktemp -d /root/liner-notes-backup-XXXXXXXX)
cp -p /etc/rc.conf "$LN_BACKUP/rc.conf"
if service liner_notes_web onestatus >/dev/null 2>&1; then service liner_notes_web stop; fi
if service liner_notes onestatus >/dev/null 2>&1; then service liner_notes stop; fi
if service liner_notes onestatus >/dev/null 2>&1; then echo 'BBS is still running'; exit 1; fi
if service liner_notes_web onestatus >/dev/null 2>&1; then echo 'Web is still running'; exit 1; fi
tar -cpf "$LN_BACKUP/config.tar" -C / usr/local/etc/liner-notes
tar -cpf "$LN_BACKUP/data.tar" -C / var/db/liner-notes
pkg create -o "$LN_BACKUP" liner-notes
printf 'Backup: %s\n' "$LN_BACKUP"
```

独自データパス、Webのspool、ゲームの設定・世界データは実際の場所も別途バックアップします。
レビュー版のruntime・Webプログラムを使っている場合はその実体も保存してください。
既存StarRoute/Clansの設定・世界を初期化しないでください。

## 2. portsで構築・配置

新しい配布物を空のディレクトリへ展開し、SHA256SUMSを確認します。
配布物のルートから実行してください。最初にpackageまで作成するため、
ビルド失敗時は旧パッケージをそのまま残せます。

```sh
cd comms/liner-notes
make BATCH=yes OPTIONS_SET="WEBUI ZMODEM" DISTDIR="$(pwd)/../../distfiles" package
pkg delete liner-notes
make BATCH=yes OPTIONS_SET="WEBUI ZMODEM" DISTDIR="$(pwd)/../../distfiles" install
test -x /usr/local/sbin/liner-notes
test -x /usr/local/sbin/liner-notes-web
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

lrzszを交換した場合はサービス所有者で再開検査を実施します。記録が存在していても
再実行でき、成功時だけ更新されます。既存の検査記録を先に削除する必要はありません。

```sh
su -m liner_notes -c '/usr/local/bin/liner-notes-check-resume /usr/local/bin/lrz /usr/local/bin/lsz --receipt /var/db/liner-notes/lrz-resume.json'
service liner_notes start
service liner_notes status
service liner_notes_web start
service liner_notes_web status
```

Webを使用しない構成はWeb関連の手順を省略します。ログイン、記事、添付・保管庫転送、
GAMEからStarRoute/Clans起動と一覧復帰を確認してください。
`Invalid liner_notes configuration` の場合は最初に `sysrc -n liner_notes_program` と
そのパスの `ls -l` を確認します。runtime未配置のままパスだけを変更しないでください。

## 4. 戻す場合

両サービスを停止し、保存した旧pkgを再導入します。レビュー版を使っていた場合は
保存した旧プログラムとrc.confの起動先を戻します。編集した設定はバックアップと比較して戻します。
DBモードの自動変更はありませんが、旧版へ戻すときはその版が読める形式か確認してください。
データを復元する場合は更新後のデータも別に保存し、新しい投稿やゲーム進行を失わないようにします。
0.69以前へ戻す場合は特に会員設定・DB形式の互換性に注意が必要です。
