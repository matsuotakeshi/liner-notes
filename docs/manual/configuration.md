# 設定ファイルと配布設定値

<a id="config-reference"></a>

設定を変える前に、実際の起動引数・実行ユーザー・保存先を確認します。FreeBSDは配布liner_notes.confの値より `/etc/rc.conf` の指定を優先します。LinuxのLINER_NOTES_OPTIONSはsystemdの環境値であり、シェルのコマンド置換は使えません。オプションは本体のポート番号より前へ置きます。

以下は0.74.1の配布例です。**設定例の値と、省略時にコードが採用する既定値は別**の場合があります。パス・ホスト名・会員番号・共有IDを実環境へ置き換えます。秘密情報を公開ファイルへ入れないでください。

## 日時・外部文書・機能選択

本体のtimezone既定はUTC、設定例でAsia/Tokyoを選択できます。日付検索は設定TZの午前0時、時間帯挨拶と誕生日もそのTZです。Webの時間帯メッセージはweb.timezone（既定Asia/Tokyo）で独立します。保存の時刻値をローカル時刻に書き換える操作ではありません。

接続用banner.txt、welcome.txt、systemnews.txt、birthday.txt、greeting-00.txt〜greeting-23.txt、sysinfo.txt、goodbye.txtはUTF-8の外部ファイルです。欠落・空のバナーは通常起動を妨げません。時間帯挨拶は現在時刻以前で直近の非空ファイルを日付をまたいで選び、全て空なら表示しません。ログイン用文書は1ファイル4096バイトまでで本体再起動時に読み直します。

メニューはmenus、操作HELPはhelp/ja・help/enです。独自変更を保持して.sampleと比較します。WebのメッセージJSON・テンプレートは別設定で、[Web構成](web-server.md)を参照してください。

チャット・メール・保管庫などの機能単位の無効化、登録方式、接続時間、記事件数は起動設定に従います。disabled機能をWebが独自に許可することはありません。設定値0の意味は項目ごとに異なるため、[上限](limits.md)を確認してください。


## FreeBSDサービス設定

```text
# Shell configuration, loaded before rc.conf; only administrators may edit.
# Keep the service disabled until its data and administrator account are ready.
liner_notes_enable="NO"
# Working directory defaults to liner_notes_data_dir.
liner_notes_program="/usr/local/sbin/liner-notes"
liner_notes_data_dir="/var/db/liner-notes"
liner_notes_run_user="liner_notes"
liner_notes_menu_dir="/usr/local/share/liner-notes/menus"
liner_notes_help_dir="/usr/local/share/liner-notes/help"
liner_notes_banner="/usr/local/share/liner-notes/banner.txt"
liner_notes_bind="127.0.0.1"
liner_notes_port="2222"
liner_notes_language="ja"
liner_notes_registration="open"
liner_notes_id_mode="free"
liner_notes_registration_form="simple"
# Set after registering the first BBS account (one numeric member ID per line):
# liner_notes_admins="/usr/local/etc/liner-notes/admins.conf"
# Initialize this vault as liner_notes before enabling it:
# liner_notes_vault_dir="/var/db/liner-notes/vault"
# Requires a verified lrzsz engine and a private spool owned by liner_notes:
# liner_notes_zmodem_helper="/usr/local/bin/liner-notes-zmodem"

# Optional native games; leave unset to disable.
# liner_notes_doors="/usr/local/etc/liner-notes/doors.conf"
# Optional shared-board connector account allowlist (board ID and member ID):
# liner_notes_link_accounts="/usr/local/etc/liner-notes/link-accounts.conf"

# rc.conf may override these; derived log filenames follow liner_notes_log_dir.
liner_notes_log_dir="/var/log/liner-notes"
# liner_notes_logfile="/var/log/liner-notes/daemon.log"
# liner_notes_user_log="/var/log/liner-notes/user.log"
# liner_notes_chat_log="/var/log/liner-notes/chat.log"
liner_notes_pidfile="/var/run/liner-notes.pid"
liner_notes_child_pidfile="/var/run/liner-notes.child.pid"

# Authoritative SQLite. Existing installations: stop and use db-migrate first.
# Explicit opt-in avoids silently changing an existing installation.
# liner_notes_database="/var/db/liner-notes/liner.sqlite"
liner_notes_timezone="UTC"
liner_notes_limits="/usr/local/etc/liner-notes/limits.conf"
liner_notes_sysinfo="/usr/local/share/liner-notes/sysinfo.txt"
liner_notes_goodbye="/usr/local/share/liner-notes/goodbye.txt"

# Optional RTBBS member policy; saved private profile settings are retained.
liner_notes_rtbbs_member_policy="NO"
liner_notes_id_prefix="u"
liner_notes_id_digits="6"
# liner_notes_id_first="sysop"
liner_notes_login_text_dir="/usr/local/share/liner-notes"

# Optional for a NEW instance only. Existing schema-1 databases are not converted.
# liner_notes_database="/var/db/liner-notes/liner.sqlite"
# liner_notes_database_mode="structured"
```


## FreeBSD rc.conf追加例

```text
# Copy the required settings to /etc/rc.conf. Paths must be absolute.
liner_notes_enable="YES"
liner_notes_home="/usr/local/libexec/liner-notes"
liner_notes_run_user="liner_notes"
liner_notes_data_dir="/var/db/liner_notes"
liner_notes_bind="127.0.0.1"
liner_notes_port="2222"
liner_notes_language="ja"

# Override to retain existing data in a different directory.
# liner_notes_board="/absolute/path/notes.dat"
# liner_notes_members="/absolute/path/members.dat"
# liner_notes_menu_dir="/absolute/path/menus"
# liner_notes_admins="/absolute/path/admins.conf"
# liner_notes_catalog="/absolute/path/boards.conf"

# Defaults: /var/run/liner_notes.pid, /var/run/liner_notes.child.pid,
# /var/log/liner_notes.log. The daemon supervisor owns the PID files and log.
# liner_notes_pidfile="/var/run/liner_notes.pid"
# liner_notes_child_pidfile="/var/run/liner_notes.child.pid"
# liner_notes_logfile="/var/log/liner_notes.log"

# Optional initialized, independent file vault directory:
# liner_notes_vault_dir="/var/db/liner_notes-vault"

# Optional configured ZMODEM launcher (requires vault_dir and lrzsz):
# liner_notes_zmodem_helper="/usr/local/libexec/liner-notes/zmodem-helper"

# open, approval, closed
liner_notes_registration="open"
# YES or NO; chat includes private talk
liner_notes_chat="YES"
liner_notes_mail="YES"
liner_notes_vault="YES"

# cumulative quotas; 0 means no configured quota.
liner_notes_max_articles="0"
liner_notes_max_mail="0"
liner_notes_max_user_posts="0"
# Optional UTF-8 banner; missing/empty is allowed.
liner_notes_banner="/usr/local/libexec/liner-notes/banner.txt"

# Gather: retained messages (0 disables); chat log: blank uses members file + .chatlog, - disables.
liner_notes_gather_count="16"
liner_notes_chat_log=""

# Empty: MEMBERS_FILE.userlog (or BOARD_FILE.userlog without members). -: disable.
liner_notes_user_log=""

# Seconds without input; 0 disables. Transfers use their own timeout.
liner_notes_idle_seconds="600" # global admins are exempt

# default HELP is home/help; set a complete custom tree if desired.
# liner_notes_help_dir="/absolute/path/help"
liner_notes_userlog_keep="1000"
liner_notes_userlog_public="NO"
liner_notes_time_warning="60"
liner_notes_time_grace="30"
# Seconds per TCP connection at the current level; 0 means unlimited.
liner_notes_level0_seconds="0"
liner_notes_level1_seconds="0"
liner_notes_level2_seconds="0"
liner_notes_level3_seconds="0"
liner_notes_level4_seconds="0"
liner_notes_level5_seconds="0"

# free|auto login IDs and simple|detailed application form.
liner_notes_id_mode="free"
liner_notes_registration_form="simple"

# Optional native games; leave unset to disable.
# liner_notes_doors="/usr/local/etc/liner-notes/doors.conf"

# Optional RTBBS member policy; saved private profile settings are retained.
liner_notes_rtbbs_member_policy="NO"
liner_notes_id_prefix="u"
liner_notes_id_digits="6"
# liner_notes_id_first="sysop"
liner_notes_login_text_dir="/usr/local/share/liner-notes"

# Optional for a NEW instance only. Existing schema-1 databases are not converted.
# liner_notes_database="/var/db/liner-notes/liner.sqlite"
# liner_notes_database_mode="structured"
```


## 実行時上限

```text
# Operational quotas. Existing records remain readable when a quota is lowered.
# Zero is invalid for these resource limits; article/mail quotas use separate flags.
members=999999
boards=1024
input_bytes=4096
body_bytes=262144
file_bytes=1073741824
batch_files=16
batch_bytes=17179869184
connections=128
transfers=24
```


## 保管庫・添付quota

```text
# Copy to PUBLIC_VAULT/limits.conf and/or MEMBERS_FILE.mailfiles/limits.conf.
# Read at store open; restart the BBS after changes. Omitted keys use defaults.
# disables that quota. IDs still have a format ceiling of 2147483646.
max_files=2000
max_events=10000
max_bytes=17179869184
```


<a id="board-acl"></a>
## ボードACL

`all`（ゲスト閲覧可）、`member`（会員）、`none`（拒否）、`u2,u5`（最大32会員ID）、`l3`（レベル3以上）、`l2+g1+g4`（レベル2以上かつグループ1と4）を指定します。グループは1〜100。ゲスト投稿は別途BOARDGUEST ONが必要です。保管庫はallでも会員向けです。親閲覧条件を満たしてから対象の投稿・登録条件を確認します。

オンライン変更は元のcatalog設定より優先されます。元ファイルの編集だけでオンライン設定を戻さず、BOARDINFO / FILEBOARDINFOとオンライン変更命令で確認・修正します。


```text
# UTF-8 without BOM. id|parent|read|write|title
# Named permissions must reference accounts already registered: u1 or u1,u2.
1|0|all|member|General
2|1|member|member|Members
3|2|all|member|Projects
4|1|all|none|Announcements
5|1|none|none|Private (configure after registration)

# Optional sixth field: id|parent|read|write|title|description (UTF-8, max 320 bytes)
# 1|0|all|member|General|Welcome. Use HELP for command information.
```


## Door

```text
# id|minimum-level|seconds|absolute-executable|absolute-work-directory|title
# 1|0|600|/usr/local/libexec/liner-notes/scripts/door_guess.py|/var/db/liner_notes/games|Number guessing
```


## Web

```toml
[web]
listen = "127.0.0.1"
port = 8088
origin = "https://bbs.example.org"
base_path = "/notes/"
secure_cookies = true
max_sessions = 64
browser_idle_seconds = 1800
messages = "messages"
templates = "templates"
timezone = "Asia/Tokyo"
# A private directory owned by the Web service account; never under the document root.
spool = "/var/db/liner-notes-web/spool"
max_upload_bytes = 1073741824
max_spool_bytes = 17179869184
# Optional, administrator-only local link management. Grant this Web account
# access to the link daemon's control socket group, not its private keys/DB.
# link_control_socket = "/run/liner-notes-link/control.sock"

[bbs]
host = "127.0.0.1"
port = 23
telnet = true
timeout = 30
# Final hash verification may take longer on slower storage.
transfer_timeout = 120
```


## 共有リンク

```toml
# Replace sample identities and boards; no service is enabled on installation.
[node]
identity = "/var/db/liner-notes-link/identity"
database = "/var/db/liner-notes-link/link.sqlite"
listen = "127.0.0.1"
port = 9044
interval = 5
# Protocol 2 peers only. The pinned signing identity stays unchanged.
rotate_tls = true
scan_budget = 512
pull_batches = 64
# Optional local management socket; directory/group permissions limit access.
# control_socket = "/var/run/liner-notes-link/control.sock"
max_events = 1000000
max_bytes = 1073741824

[bbs]
host = "127.0.0.1"
port = 23
login = "bbslink"
password_file = "/var/db/liner-notes-link/bbs-password"

[[boards]]
id = "0000000000000000000000000000000000000000000000000000000000000001"
local = 2

# Every origin (including nodes reached through a relay) needs explicit trust.
[[origins]]
certificate = "/usr/local/etc/liner-notes/link-peers/peer.crt"
boards = ["0000000000000000000000000000000000000000000000000000000000000001"]

# Only direct neighbors need endpoint entries; certificates are public.
[[peers]]
certificate = "/usr/local/etc/liner-notes/link-peers/peer.crt"
host = "192.0.2.1"
port = 9044
boards = ["0000000000000000000000000000000000000000000000000000000000000001"]
```


## リンク専用会員

```text
# LOCAL_BOARD_ID CONNECTOR_MEMBER_ID
# Registered non-administrator account; no implicit parent-board permission.
# Enable explicitly with --link-accounts FILE / liner_notes_link_accounts.
# 10
```


## Linux環境

```text
# systemd EnvironmentFile syntax; this is not a shell script.
# Keep the listener on localhost until the first administrator is configured.
# Edit the complete options string. Options must precede PORT and data paths.
LINER_NOTES_OPTIONS="--telnet --menu --lang ja --bind 127.0.0.1 --timezone Asia/Tokyo --limits /etc/liner-notes/limits.conf --menu-dir @PREFIX@/share/liner-notes/menus --help-dir @PREFIX@/share/liner-notes/help --banner @PREFIX@/share/liner-notes/banner.txt --login-text-dir @PREFIX@/share/liner-notes --sysinfo @PREFIX@/share/liner-notes/sysinfo.txt --goodbye @PREFIX@/share/liner-notes/goodbye.txt --database /var/lib/liner-notes/liner.sqlite --user-log /var/log/liner-notes/user.log --chat-log /var/log/liner-notes/chat.log 2222 /var/lib/liner-notes/notes.dat /var/lib/liner-notes/members.dat"
# Enable transfers only after initializing the vault and verifying real resume.
# LINER_NOTES_LRZ=/usr/local/bin/lrz
# LINER_NOTES_LSZ=/usr/local/bin/lsz
# LINER_NOTES_RESUME_RECEIPT=/var/lib/liner-notes/lrz-resume.json
# Optional shared boards: add --link-accounts /etc/liner-notes/link-accounts.conf
# before the port in LINER_NOTES_OPTIONS. See BBS_LINKS.md; disabled by default.
```


## FreeBSD rc.dの省略時既定値

以下はサービス起動スクリプトの既定値です。配布設定ファイルやrc.confに値があればそれが優先されます。空欄は未指定です。`%%PREFIX%%` は通常 `/usr/local`、`%%DATADIR%%` は通常 `/usr/local/share/liner-notes` です。

| 変数 | 省略時の値 |
|---|---|
| `liner_notes_enable` | `NO` |
| `liner_notes_data_dir` | `/var/db/liner-notes` |
| `liner_notes_home` | `${liner_notes_data_dir}` |
| `liner_notes_program` | `%%PREFIX%%/sbin/liner-notes` |
| `liner_notes_menu_dir` | `%%DATADIR%%/menus` |
| `liner_notes_log_dir` | `/var/log/liner-notes` |
| `liner_notes_board` | `${liner_notes_data_dir}/notes.dat` |
| `liner_notes_members` | `${liner_notes_data_dir}/members.dat` |
| `liner_notes_run_user` | `liner_notes` |
| `liner_notes_bind` | `127.0.0.1` |
| `liner_notes_port` | `2222` |
| `liner_notes_language` | `ja` |
| `liner_notes_database` | 未指定 |
| `liner_notes_database_mode` | `blocks` |
| `liner_notes_limits` | 未指定 |
| `liner_notes_timezone` | `UTC` |
| `liner_notes_sysinfo` | `%%DATADIR%%/sysinfo.txt` |
| `liner_notes_goodbye` | `%%DATADIR%%/goodbye.txt` |
| `liner_notes_pidfile` | `/var/run/liner-notes.pid` |
| `liner_notes_child_pidfile` | `/var/run/liner-notes.child.pid` |
| `liner_notes_logfile` | `${liner_notes_log_dir}/daemon.log` |
| `liner_notes_doors` | 未指定 |
| `liner_notes_link_accounts` | 未指定 |
| `liner_notes_admins` | 未指定 |
| `liner_notes_catalog` | 未指定 |
| `liner_notes_vault_dir` | 未指定 |
| `liner_notes_zmodem_helper` | 未指定 |
| `liner_notes_registration` | `open` |
| `liner_notes_id_mode` | `free` |
| `liner_notes_id_prefix` | `u` |
| `liner_notes_id_digits` | `6` |
| `liner_notes_id_first` | 未指定 |
| `liner_notes_rtbbs_member_policy` | `NO` |
| `liner_notes_login_text_dir` | `%%DATADIR%%` |
| `liner_notes_registration_form` | `simple` |
| `liner_notes_max_articles` | `0` |
| `liner_notes_max_mail` | `0` |
| `liner_notes_max_user_posts` | `0` |
| `liner_notes_banner` | `%%DATADIR%%/banner.txt` |
| `liner_notes_chat` | `YES` |
| `liner_notes_mail` | `YES` |
| `liner_notes_vault` | `YES` |
| `liner_notes_help_dir` | `%%DATADIR%%/help` |
| `liner_notes_userlog_keep` | `1000` |
| `liner_notes_userlog_public` | `NO` |
| `liner_notes_time_warning` | `60` |
| `liner_notes_time_grace` | `30` |
| `liner_notes_level0_seconds` | `0` |
| `liner_notes_level1_seconds` | `0` |
| `liner_notes_level2_seconds` | `0` |
| `liner_notes_level3_seconds` | `0` |
| `liner_notes_level4_seconds` | `0` |
| `liner_notes_level5_seconds` | `0` |
| `liner_notes_idle_seconds` | `600` |
| `liner_notes_gather_count` | `16` |
| `liner_notes_chat_log` | `${liner_notes_log_dir}/chat.log` |
| `liner_notes_user_log` | `${liner_notes_log_dir}/user.log` |

## 本体の起動オプション一覧

`runtime [オプション] PORT [BOARD_FILE [MEMBERS_FILE [CATALOG_FILE]]]` の順です。`liner-notes` はインストール時の実行名です。全オプションはポートより前へ置きます。

`--admins`, `--banner`, `--bind`, `--chat-log`, `--database`, `--database-structured`, `--disable`, `--doors`, `--gather-count`, `--goodbye`, `--help-dir`, `--id-digits`, `--id-first`, `--id-mode`, `--id-prefix`, `--idle-seconds`, `--lang`, `--level-seconds`, `--limits`, `--link-accounts`, `--login-text-dir`, `--max-articles`, `--max-mail`, `--max-user-posts`, `--menu`, `--menu-dir`, `--registration`, `--registration-form`, `--rtbbs-member-policy`, `--sysinfo`, `--telnet`, `--time-grace`, `--time-warning`, `--timezone`, `--user-log`, `--userlog-keep`, `--userlog-public`, `--vault-dir`, `--zmodem-helper`

`--disable chat` / `--disable mail` / `--disable vault` は繰り返し指定できます。`--max-articles` / `--max-mail` / `--max-user-posts` は0でquotaなし。`--idle-seconds` は0〜86400で0は無入力切断なし。`--level-seconds LEVEL:SECONDS` は0〜5のレベルごと、0秒は時間制限なし。警告と猶予は `--time-warning` / `--time-grace`。`--gather-count` は0〜1000で0は無効です。`--userlog-keep` の0は保持件数無制限。`--user-log -` / `--chat-log -` は該当ログを無効にします。

サービススクリプトと本体の両方の値検査に従います。FreeBSD rc.d経由の時間警告・猶予は86400秒以内、USERLOG保持は本体が1000000件以内、Gatherは1000件以内です。接続制限は管理者免除でも、転送コネクタの無通信制限は免除しません。
