# StarRoute Traders → Liner notes 本番導入手順

> この文書は2026-11-02のStarRoute 0.18.0単独導入キットの記録です。Liner notes 0.72には必要なPTY連携を収録済みです。既存の宇宙を使う場合は [現在の導入マニュアル](INSTALL.md#doors-setup)に従い、initを再実行しません。別版のStarRouteを新規導入・更新する場合は、その配布物の手順を優先してください。

対象: srv.ysni.org / FreeBSD 15.1 amd64、単独BBSの新規宇宙。
StarRoute Traders 0.18.0 Phase 5を同梱。既存Liner notesのDoor機構を使用し、
ゲーム専用の常駐daemon・マスターサーバー・TLS証明書・追加待受ポートは不要です。
このキットは本番へ自動接続せず、rc.conf・doors.confの変更やサービス再起動も行いません。

## 1. 前提確認

以下は本番サーバーのrootシェルで実行する手順です。

```sh
service liner_notes status
id liner_notes
grep -n '^liner_notes_run_user=' /etc/rc.conf /usr/local/etc/liner-notes/liner_notes.conf
/usr/local/bin/python3.12 -c 'import sqlite3; print(sqlite3.sqlite_version)'
```

BBSの実行アカウントが`liner_notes`であることを確認してください。
未指定時は現行rc.dの既定値が`liner_notes`です。別アカウントの場合は以下の
`--user`と`su -m`を同じアカウントへ置き換えます。**root実行のBBSはDoor起動を拒否します。**
ポート23での待受設定は既存のものを維持します。

Pythonまたはsqlite3モジュールがない場合だけ導入します。

```sh
pkg install python312 py312-sqlite3
```

SQLiteモジュールはFreeBSD Portsの[databases/py-sqlite3](https://github.com/freebsd/freebsd-ports/blob/main/databases/py-sqlite3/Makefile)です。
ゲーム実行にpipパッケージは必要ありません。

このキットの検証時は`liner-notes-web-review-20261102.zip`相当を使用しました。現在はLiner notes 0.72に対応を収録しています。
Door実行中のNAWS→PTYサイズ伝達を取り込み済みです。古いstage70でも起動自体は可能ですが、
起動後の端末サイズ変更に追従しません。StarRoute側のコピー用パッチを本番へ直接適用しないでください。
本体更新が必要なら、BBSの既存導入手順に従い、使用中のruntimeだけを更新します。

## 2. キットを展開して配置

提供ZIPを本番へ転送して、例えば`/root/temp`で展開します。FreeBSDのtarはZIPを展開できます。
併せて提供するSHA256.txtで受信ファイルを照合してください。

```sh
mkdir -p /root/temp
cd /root/temp
sha256 starroute-liner-standalone-20261102.zip
tar -xf starroute-liner-standalone-20261102.zip
cd starroute-liner-standalone
/usr/local/bin/python3.12 install.py --user liner_notes --bbs-id srv_ysni_org --bbs-name 'Liner notes' --door-id 1
```

既存のゲーム番号1を使っている場合は、未使用の番号を`--door-id`へ指定します。
`install.py`は既存の配置先・設定・保存先がある場合に上書きを拒否します。
これは**新規導入専用**です。失敗時は残ったファイルを調べてから対処し、
既存宇宙を消して再実行する運用にはしないでください。

配置:

| 場所 | 用途・権限 |
| --- | --- |
| `/usr/local/share/starroute-traders/0.18.0/` | ゲームソース。root所有、BBSからは読み取りのみ |
| `/usr/local/libexec/liner-notes/starroute-door` | 固定パスのDoorラッパー。root所有、実行可能 |
| `/usr/local/sbin/liner-notes-starroute-admin` | 新規宇宙作成・診断用ラッパー |
| `/usr/local/etc/starroute-traders/door.json` | 単独宇宙の接続設定。認証情報の値は含まない |
| `/usr/local/etc/starroute-traders/setup.json` | 初期化用のBBS ID・保存先など |
| `/usr/local/etc/starroute-traders/doors.conf.line` | Liner notesへ追加する設定行 |
| `/var/db/starroute-traders/world/` | ゲームDB・生成宇宙・署名鍵・サイトプロファイル。BBSアカウント所有、0700 |

Liner notesの会員・記事・メール・保管庫データへゲームを混在させません。
DoorはBBSと同じOSユーザーで実行するため、OS上で別ユーザーに隔離する構成ではありません。

## 3. 新しい宇宙を一度だけ作る

```sh
su -m liner_notes -c '/usr/local/sbin/liner-notes-starroute-admin init'
su -m liner_notes -c '/usr/local/sbin/liner-notes-starroute-admin check'
```

`init`はランダムな新規シードで48惑星の宇宙を生成し、クール1を有効化します。
ルールは現在のPhase 5に同梱された`phase3-trial.json`等の試遊設定、クールは既定30日です。
検証用宇宙・既存プレイヤー・試験用の資金や認証情報は持ち込みません。
秘密鍵・サイト認証情報は本番サーバーで新しく作り、画面へ表示しません。

成功時は`initialized: true`、診断は`ok: true`・`integrity: "ok"`が出ます。
`init`は作成済み／途中作成の`world`があれば停止し、初期化し直しません。
新しいプレイヤーは認証済み会員が初めてDoorへ入った時点で作成します。

`srv_ysni_org`というBBS IDとLiner notesの会員番号の組がプレイヤーの識別子です。
運用開始後にBBS IDを変更しないでください。ログイン名をゲーム内で再入力する必要はありません。
クール更新は自動初期化ではなく、StarRouteの管理CLIで別途行います。
詳細は同梱ソースの`PHASE4_COMPLETE.md`と`PHASE5_COMPLETE.md`を参照してください。

## 4. Liner notesへゲームを登録

まず現在使っている`liner_notes_doors`の設定を確認します。

```sh
grep -n '^liner_notes_doors=' /etc/rc.conf /usr/local/etc/liner-notes/liner_notes.conf
cat /usr/local/etc/starroute-traders/doors.conf.line
```

すでにゲーム設定ファイルを使用していれば、そのファイルをバックアップして編集します。
以下は新規・標準パスの場合の例です。既存ファイルがある場合は、日付付きの別名へ
`cp -p`してから編集し、他のゲームの行は残してください。

```sh
ee /usr/local/etc/liner-notes/doors.conf
```

追加する行（標準配置・番号1の場合）:

```text
1|0|3600|/usr/local/libexec/liner-notes/starroute-door|/var/db/starroute-traders|StarRoute Traders
```

最低会員レベル0、1回最大3600秒です。ゲストには起動を許しません。
既存の番号と重複させないでください。`doors.conf`はroot所有で、BBSから読める0644にします。

```sh
chown root:wheel /usr/local/etc/liner-notes/doors.conf
chmod 644 /usr/local/etc/liner-notes/doors.conf
sysrc liner_notes_doors=/usr/local/etc/liner-notes/doors.conf
```

既存の別パスへ追加した場合は、そのパスを`sysrc`へ指定します。
Door設定は起動時に読み込まれるため、利用者へ停止を案内したうえで反映します。

```sh
service liner_notes restart
service liner_notes status
```

この再起動は接続者を切断します。StarRouteのためにWebサーバーのVirtualHost、
ファイアウォール、公開ポートを追加する必要はありません。

## 5. 本番の受け入れ確認

Telnetで普段の会員としてログインして、次を入力します。

```text
GAME
1
status
market
map
help
quit
```

ゲーム内は`SRT>`です。`quit`でゲーム一覧へ戻り、もう一度`1`で同じプレイヤーへ
復帰することを確認します。売買も試す場合は市場に表示された商品IDを使ってください。
端末の横幅を変更し、再度`map`を入力して星図が追従することも確認してください。

WebUIではページを再読み込みし、ログインし直すと左の「ゲーム」が表示されます。
「ゲーム用端末を開く」→**端末内でもBBSへログイン**→`DOOR 1`です。
Web画面のログインと補助端末の接続は別で、パスワードを使い回して自動ログインしません。
現時点でStarRouteは端末型ゲームであり、HTMLの専用ゲーム画面ではありません。

## 6. エラー時の切り分け・戻し方

- `GAMES`にない: 実際の`liner_notes_doors`のパス、設定行、再起動、会員レベルを確認。
- Door起動を拒否: BBSがrootでないこと、ラッパー実行権限、作業ディレクトリの権限を確認。
- `Authenticated Liner notes Door environment required`: 通常のシェルから直接Doorを起動している。
  起動確認はBBS内の`DOOR`を使う。診断には`liner-notes-starroute-admin check`を使う。
- 設定・DBエラー: `su -m liner_notes -c '/usr/local/sbin/liner-notes-starroute-admin check'`を実行。
  鍵・プロファイルの中身をログや問い合わせへ貼り付けない。
- 星図サイズが固定: Liner notes本体の更新、TelnetクライアントのNAWS設定を確認。
- 戻す場合: 追加したDoor行を外すか、バックアップした設定へ戻してBBSを再起動。
  ゲームDBを削除する必要はなく、確定した資産を保持できる。

更新前のバックアップではBBSを停止し、`/var/db/starroute-traders`全体と
`/usr/local/etc/starroute-traders`をセットで保存してください。SQLite本体だけでなく
WAL・参照鍵・サイトプロファイル・生成宇宙も対象です。復元は稼働中DBへ上書きせず、
別の保存先で検証してから切り替えます。同梱ソースとキットも保管してください。

## 検証範囲

結果は`VERIFICATION.md`を参照してください。本番での反映・再起動はこの作業では実施していません。
