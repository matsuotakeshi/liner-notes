# ディレクトリと配置

<a id="layout"></a>

## インストール先

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

LinuxのRPMは `/usr`、ソース配置の既定は `/usr/local`、設定は `/etc/liner-notes`、データは `/var/lib/liner-notes` です。[Linuxの配置表](install-linux.md)を参照してください。BBS・Web・リンク・ゲームの保存先は分離します。
