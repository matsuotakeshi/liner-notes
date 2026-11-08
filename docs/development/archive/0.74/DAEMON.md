> この文書は機能追加時点の記録です。現行の導入入口は [README](README.md)、今回の変更は [STAGE65](STAGE65.md) を参照してください。古い段階名・上限・未実装事項は当時のものです。

> stage60の掲示板管理・入力操作と管理者の無入力切断除外は [STAGE60.md](STAGE60.md) を参照してください。

> stage59では、`liner_notes_idle_seconds="600"` が既定の無入力切断時間です。`0` で無効にできます。ログ保存期間の整理は停止中に専用ツールで実行します。[詳細](STAGE59.md)

# 第32段階: FreeBSD daemon / rc.d サービス

FreeBSD標準の `/usr/sbin/daemon` を利用し、端末から切り離して動かすサービス定義 `rc.d/liner_notes` を追加しました。BBS本体に自己daemon化処理を追加せず、PID・ログ・終了通知はOS標準の仕組みで管理します。

## できること

- `service liner_notes start / stop / restart / status`
- `liner_notes_enable="YES"` によるOS起動時の自動起動
- 指定した一般ユーザーでBBS本体を実行（管理用のdaemonは起動したユーザーの権限）
- stdout/stderrをログへ追記。会員のパスワードや投稿本文はログ出力しません。
- 2種類のPIDファイルで監督プロセスとBBS本体を管理し、二重起動を防止
- 起動後、BBS本体のPIDで実際にTCP待受が開いたことを確認。失敗した場合は失敗を返します。
- 停止時は監督プロセスへSIGTERMを送信し、BBS終了とPIDファイル削除を待ちます。

異常終了後の自動再起動は今回有効にしていません。設定やデータの不正で終了した場合に再起動を繰り返さず、ログを確認してからstartしてください。OS起動時の自動起動とは別です。

## 今の検証機へ設定する例

新版は `/home/takeshi/liner-notes-stage32-20261026` にあります。以下はrootで実施する設定例です。まだシステムへの登録や既存BBSの切替は自動実行していません。

まず、端末で動かしている旧BBSをCtrl-Cで終了してください。同じ保存ファイルを2つのruntimeで開くことはできません。実行ユーザー `takeshi` に、保存ファイル一式の読書き権限と、その親ディレクトリへの書込み・通過権限が必要です。旧版をrootで動かしていた場合はファイル所有者を確認してください。サービス定義は既存データの所有者を自動変更しません。

```sh
cd /home/takeshi/liner-notes-stage32-20261026
install -m 755 rc.d/liner_notes /usr/local/etc/rc.d/liner_notes
sysrc liner_notes_enable=YES
sysrc liner_notes_home=/home/takeshi/liner-notes-stage32-20261026
sysrc liner_notes_run_user=takeshi
sysrc liner_notes_board=/home/takeshi/notes-stage28-20261026-a/notes.dat
sysrc liner_notes_members=/home/takeshi/notes-stage28-20261026-a/members.dat
sysrc liner_notes_bind=0.0.0.0
sysrc liner_notes_port=2222
service liner_notes start
service liner_notes status
```

既存データの場所が異なる場合は、board・membersの絶対パスを合わせてください。新規データで開始する場合は、run_userが書き込める保存先ディレクトリを作って `liner_notes_data_dir` を指定します。既存の `.moderation`・`.extras` 等の関連ファイルを一組でバックアップしてください。

メニューは標準で `$liner_notes_home/menus`、プログラムは `$liner_notes_home/runtime` を使用します。別の配置先も設定できます。管理者・ボード設定を使っている場合は `liner_notes_admins`・`liner_notes_catalog` に既存の絶対パスを設定してください。

```sh
service liner_notes stop
service liner_notes restart
tail -f /var/log/liner_notes.log
```

停止・再起動は接続中の利用者を切断します。メニューファイルの変更はrestartで反映します。未送信・未保存の本文を保存する機能ではないため、再起動前に利用者へ案内してください。

自動起動だけを無効にするには `sysrc liner_notes_enable=NO` を設定します。既に動いているプロセスはstopするまで動作します。無効のまま一時的に起動・停止する場合は `service liner_notes onestart` / `onestop` を使用できます。

## 設定項目

`liner_notes.rc.conf.example` を参照してください。パスは絶対パスで設定します。空白等を含むパスも単一引数として扱い、コマンド文字列としてevalしません。rc.conf自体は通常のFreeBSD設定と同様にシェルコードとして読み込まれます。

| 設定 | 初期値 |
| --- | --- |
| liner_notes_enable | NO |
| liner_notes_home | /usr/local/libexec/liner-notes |
| liner_notes_program | home/runtime |
| liner_notes_menu_dir | home/menus |
| liner_notes_run_user | liner_notes（既存のOSユーザーを指定） |
| liner_notes_data_dir | /var/db/liner_notes |
| liner_notes_board / members | data_dir/notes.dat / members.dat |
| liner_notes_bind / port | 127.0.0.1 / 2222 |
| liner_notes_language | ja |
| liner_notes_admins / catalog | 空（未指定） |
| liner_notes_pidfile | /var/run/liner_notes.pid |
| liner_notes_child_pidfile | /var/run/liner_notes.child.pid |
| liner_notes_logfile | /var/log/liner_notes.log |

ユーザー・保存先ディレクトリ・システム設定を勝手に作成する処理はありません。ログ/PIDファイルの親ディレクトリはサービスを起動するユーザー、保存先はrun_userが使えるよう用意してください。daemonはログを追記し、既存ログを消去しません。ログローテーション設定は自動追加していません。daemonの-Hを有効にしているため、必要なら監督プロセスへのSIGHUPでログを開き直せます（BBSの設定再読込ではありません）。

## 外部Telnet・SSH

daemon化しても接続方法は変わりません。`liner_notes_bind="0.0.0.0"` なら外部Telnetと従来のSSH入口を併用できます。SSH入口のパスを新版の `scripts/liner-notes-ssh` にすると同じ常駐BBSにつながります。SSHだけで利用する場合はbindの初期値127.0.0.1を使えます。詳しくは [STAGE31.md](STAGE31.md)。

## 隔離検証

`tests/test_daemon.py` は一時ディレクトリの設定・保存データ・ログ・PIDと空きポートで検証します。`LINER_NOTES_RC_CONF` で独立した設定を読み込み、/etc/rc.confやシステムのrc.d登録を変更しません。テスト終了時は停止して一時データを削除します。起動時の待受確認は最大15秒、停止待機も最大15秒です。停止に時間がかかった場合は強制終了せず失敗を通知します。

検証済み: FreeBSD 15.1で一般ユーザーのサービス管理、root管理からtakeshiへの実行権限切替、start/status/restart/stop、二重起動、TTY切離し、ログ、古いPID、設定不正・ポート競合、投稿の永続化を確認しました。通信回帰の2系列も成功。実機再起動による自動起動テストは行っていません。結果は `evidence/freebsd-stage32.txt`、照合済みハッシュは `evidence/freebsd-stage32.sha256` を参照してください。
