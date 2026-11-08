# バージョンアップと切り戻し

<a id="upgrade-entry"></a>

[詳細導入マニュアル](install-freebsd.md)と併せて使用します。

## 承認付き段階更新（次期版候補・本番未導入）

この節は開発中の更新機構です。0.74.1の配布物には含まれません。ソースの
`production-staged-update.yml` は既定で `plan` を実行し、本番へSSH接続しません。
本番へのhelper・鍵・sudo設定の設置、および実際の更新は、それぞれ明示的な承認後に行います。
本番機を検証先には使いません。

更新順序はRocky Linux → Debian → FreeBSDです。各局のヘルスチェックが成功してから
次局へ進みます。必要な条件は次のとおりです。

1. 公開済み安定リリースのtagと署名付き `DEPLOYMENT-MANIFEST.json` が同じ完全SHAであること。配備controllerは実行時の最新mainに固定し、release SHAがその祖先であることを確認します。リリース後の文書更新だけで既存リリースを作り直す必要はありません。ただし承認待ち中にmainが進んだ場合は新しいrunと承認が必要です。
2. native/Linux/pkg/RPM/DEB/Release Set/manual/deployment安全性、Solaris native、Rocky SELinux Enforcing、FreeBSD jail更新CIと本番SSH事前検査がreleaseと同一SHAで成功していること。文書変更によるskipは実行済み試験として認めません。controllerだけが新しいSHAであることを理由に過去のrelease検証を他のreleaseへ流用しません。
3. CI由来manifest・Release Set receiptと、ダウンロードしたpackageの名前・版・サイズ・SHA-256が一致すること。
4. `production-release` Environmentにmain専用のブランチ制限とオーナーのrequired reviewerが実在すること。
5. 今回のrun summaryに示す `DEPLOY v版 SHA rocky.ysni.org,debian.ysni.org,srv.ysni.org RUN run番号/attempt` を、承認コメントへそのまま記載すること。古いrun/attemptの承認は再利用できません。

配布計画の署名鍵と実行承認の署名鍵は分離します。`scripts/deploy_manifest.py` は既存の
`verify_release_set.py` で確認済みのCI成果物だけから署名計画を作成します。
リリース公開や本番更新は実行しません。root側helperは公開鍵を固定し、短期の実行承認と
署名計画の両方を検証します。公開鍵はrepository variable `LN_RELEASE_PUBLIC_KEY`、
実行承認用秘密鍵は保護Environmentの `LN_DEPLOY_AUTH_KEY`、SSH鍵は既存の
`ID_ED25519_LN_DEPLOY` を使います。秘密鍵を原稿・issue・Artifactへ貼らないでください。

host側は `/etc/liner-notes-deploy/host.json` と公開鍵をroot専用で設置します。
導入補助 `ci/install-deploy-helper.sh OS名` はhelperの配置だけを行い、sudo権限・鍵・
サービスの変更は行いません。OS名は `rocky10`、`debian13`、`freebsd15` です。
sudoは `ln-deploy` に対し引数なしの `/usr/local/libexec/liner-notes-deploy-host` だけを
許可します。一般的な `sh`、`systemctl`、`pkg` のsudo許可を与えないでください。

root管理profileにはtarget、private state directory、incoming directory、公開鍵、
導入済みサービス、旧package manifest、停止確認用PID一覧、バックアップ対象、
復元照合用SELECT、別コピーのBBS起動設定 `restore_application`、ローカルのlogin/article/mailヘルスチェックを設定します。
有効なWeb/link/登録Doorには追加のprobeが必須です。profileのprobeには検証会員の
秘密情報が入り得るためrootのみが読み書きできるようにし、GitHubへアップロードしません。
probeはlocalhostへの送受信で、任意のroot shellコマンドではありません。

バックアップ対象はdata/config/attachments/vault/spool/link/web/doors/binaries/old-packages
の10区分すべてを明記します。data/config/binaries/old-packagesは空にできません。
他の区分は `backup_features` で使用/未使用を明記し、未使用は空配列、使用中は実パスを列挙します。
Web/linkサービスを有効にして対応する保存先を空にする設定は拒否します。data配下にまとめて
保存している機能は同じdataパスを指定して構いません。外部保存先・Doorデータは個別に含めます。
停止後に専用サービスアカウントのworkerが残っていないことを確認し、ACL・拡張属性を含むarchiveを作成します。
別コピーへ復元して全file hash、SQLite integrity、指定の会員・記事・メール等のSELECT結果を照合し、
SQLiteの照合はDB/WALのさらに別コピーで行います。原本DBへSQLite接続せず、原本側にSHMを
作成することも避けます。コピー中の変化・未回復のrollback journalは更新を拒否する理由です。
さらに使い捨てコピー上のBBSを非root・loopbackで起動してlogin/article/mailを確認します。外部のゲームdaemon等が
まだデータを書いている場合は停止し、ゲーム側の整合バックアップ手順を先に設計します。別ホストの専用backup
receiverへ転送し、保管後hashが一致するまでpackage更新を開始しません。
backup識別子は局名・release ID・SHAで区別します。転送先SSH host keyに加え、OS machine IDの
SHA-256も固定して照合し、同じ局への「ホスト外バックアップ」を拒否します。
backup receiverはprivate directory `/var/lib/liner-notes-release-backups` を使い、
専用SSHアカウントには `deploy_backup_store.py` のみを実行可能にします。
ゲーム外部データ、秘密鍵、証明書を含むバックアップをActions Artifactに保管しないでください。

Linux更新はローカルpackageだけのRPM/dpkgで行い、依存解決のための一括OS更新はしません。
Debianの既存conffileは保持します。FreeBSDはOrigin `comms/liner-notes` を確認した
単一packageのprivate repositoryから `pkg upgrade` を実行し、New/削除を含むplanを拒否します。
`pkg delete` → `pkg add` はこのCD機構では使用しません。Apache・SofChat・OS操作を
helperのサービス許可一覧に追加しないでください。更新前後で無関係packageが変わっていないことも確認します。

成功済みの同一計画を新しい承認で再実行すると、healthのみを確認し再インストールしません。
中断・失敗状態は後続局を停止し、`planned-restore-required` として保持します。
DB互換性の推測による自動バイナリ切り戻しは行いません。private host state、
off-host backupと旧packageを確認して[計画的な復元](backup-recovery.md)を実施し、
整合性を確認した後に新しいrun/承認で再開します。状態fileを消すだけの再実行は禁止です。
失敗状態が一つでも残る局は別releaseへの更新も拒否します。計画復元の完了を管理者が確認してから、
失敗stateを復元証跡と一緒にroot専用の履歴directoryへ移して保持し、新しい承認で再検査します。
監査出力はSHA、版、対象、承認run/attempt、更新前後、backup識別子・hash、結果・停止理由です。
release SHAとcontroller SHAは別々に記録します。制限付きOSコマンドの詳細stdout/stderrは
private state directoryのroot専用 `commands.log`（0600）へ記録します。このログは秘密を含む
可能性があるため、SSH応答やActions Artifactには含めません。コマンドの標準入力は記録しません。

**検証上の区別:** hostedの3局試験はfile/SQLite/subprocessの隔離fixtureです。
RPM/DEBの既存disposable更新CIでは実package操作も検証します。
FreeBSDのrootdir合成package試験は実pkg resolverを使用しますが、service/maintainer script試験ではありません。
同じCIに実Liner Notes packageのrevision更新と `LINER_NOTES_RC_CONF` による独立rc起動・
login/article/mail・論理backup/restore・旧package回復の試験も用意しています。
こちらも非rootのrootdir内でmaintainer scriptsを無効化して実行するため、アカウント作成・
所有権設定を含む完全なOS更新試験の代わりにはなりません。
適用controllerは `FREEBSD-SERVICE-UPGRADE.json` に同一SHAの完全なpackage/service更新・復元証跡がない限りapplyを拒否します。
専用の `FreeBSD isolated upgrade acceptance` は、10.0.0.50上のroot所有テンプレートを
VNET jailへ複製し、maintainer script・会員/データ所有権、実rc起動、記事/メール、Web、link、
Door、バックアップからの復元と旧package回復を検査する候補実装です。宿主のサービスや
packageを更新しません。テンプレート・固定sudo entryの初期準備とネイティブ実行は別途必要です。
同一バージョンの開発候補はrevisionだけを上げて更新経路を試験できますが、その証跡だけでは
本番applyを許可しません。実release packageのhash/版と試験結果を照合し、試験開始時の旧版を
短期署名承認へ含め、本番局の旧版が異なる場合も停止します。
FreeBSDの実Liner Notes package・サービスを含む完全な隔離更新、全保存領域のsite profile、
本番用root helper/鍵/backup先の設置が確認できるまでは、本番更新可能と扱いません。

## 0.74から0.74.1への更新

HELPとマニュアルを訂正した修正版です。既存DBの変換、ボードやゲームの作り直しは不要です。本体の版表示は0.74.1になります。

未編集のHELPはパッケージ更新で新版へ置き換わります。編集済みのHELPは保持されるため、`help/ja/` と `help/en/` の `content.txt`、`settings.txt`、`unread.txt` を新しい `.sample` と比較し、必要な訂正を手動で取り込んでください。サイト用ZIPの更新だけではBBS内HELPは変わりません。

Linuxのソース導入でも、旧 `.sample` と同じ内容のHELPは更新します。編集済み、または比較用の旧sampleがない既存HELPは保持します。新しいマニュアルの入口はFreeBSDで `/usr/local/share/doc/liner-notes/index.html`、RPMで `/usr/share/doc/liner-notes/index.html` です。Gitから直接ソース導入する場合はHTML未生成なら `index.md` を使用します。

FreeBSDで `pkg add -f` のみを実行すると、古い既定HELPが残る場合があります。下記の削除・導入手順を使用します。パッケージのoriginが `comms/liner-notes` で一致すること、依存・ABIが合うことを先に確認し、誤ったoriginの候補を強制導入しないでください。

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
