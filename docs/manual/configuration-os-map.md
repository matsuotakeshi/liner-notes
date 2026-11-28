# FreeBSD・Linux設定対応表

この表はv0.76のFreeBSD `liner_notes.in` とLinux `liner-notes.service`・`liner-notes.env` の対応です。rc.dの省略時既定値であり、配布設定ファイルや管理者のrc.confの値は異なる場合があります。`%%PREFIX%%` と `%%DATADIR%%` はインストール時に展開されます。空値は未指定を表します。Linuxのオプションは既存の `LINER_NOTES_OPTIONS` に追加し、PORTとデータパスより前に置きます。

| FreeBSD変数 | 用途 | rc.d省略時 | Linuxの対応 |
|---|---|---|---|
| `liner_notes_enable` | 自動起動 | `NO` | systemctl enable / disable |
| `liner_notes_data_dir` | データ既定ディレクトリ | `/var/db/liner-notes` | unitのWorkingDirectory・StateDirectoryと位置引数 |
| `liner_notes_home` | 動作ディレクトリ | `${liner_notes_data_dir}` | unitのWorkingDirectory |
| `liner_notes_program` | 実行ファイル | `%%PREFIX%%/sbin/liner-notes` | unitのExecStart |
| `liner_notes_menu_dir` | メニュー | `%%DATADIR%%/menus` | --menu-dir |
| `liner_notes_log_dir` | ログ既定ディレクトリ | `/var/log/liner-notes` | unitのLogsDirectory / journal |
| `liner_notes_board` | ボード保存先 | `${liner_notes_data_dir}/notes.dat` | PORTの次の位置引数 |
| `liner_notes_members` | 会員保存先 | `${liner_notes_data_dir}/members.dat` | BOARDの次の位置引数 |
| `liner_notes_run_user` | 実行アカウント | `liner_notes` | unitのUser / Group |
| `liner_notes_bind` | Telnet待受IPv4 | `127.0.0.1` | --bind |
| `liner_notes_port` | Telnet待受ポート | `2222` | 最初の位置引数PORT |
| `liner_notes_ui_policy` | UI選択制限 | `FIXED` | --ui-policy |
| `liner_notes_ui_default` | 既定の端末UI | `rtbbs` | --ui-default |
| `liner_notes_ui_allow` | 許可する端末UI | `rtbbs` | --ui-allow |
| `liner_notes_ssh_port` | SSH待受ポート | `0` | --ssh-port |
| `liner_notes_ssh_bind` | SSH待受IPv4 | `127.0.0.1` | --ssh-bind |
| `liner_notes_ssh_host_key` | SSHホスト秘密鍵 | `未指定` | --ssh-host-key |
| `liner_notes_ssh_member_keys` | 会員公開鍵一覧 | `未指定` | --ssh-member-keys |
| `liner_notes_ssh_attempts` | 認証試行回数 | `6` | --ssh-attempts |
| `liner_notes_ssh_timeout` | SSH認証時間 | `60` | --ssh-timeout |
| `liner_notes_language` | 表示言語 | `ja` | --lang |
| `liner_notes_database` | SQLiteデータベース | `未指定` | --database または --database-structured |
| `liner_notes_database_mode` | SQLite保存方式 | `blocks` | --database / --database-structured |
| `liner_notes_limits` | 上限設定ファイル | `未指定` | --limits |
| `liner_notes_timezone` | 表示タイムゾーン | `UTC` | --timezone |
| `liner_notes_sysinfo` | 局案内 | `%%DATADIR%%/sysinfo.txt` | --sysinfo |
| `liner_notes_goodbye` | 終了時案内 | `%%DATADIR%%/goodbye.txt` | --goodbye |
| `liner_notes_pidfile` | 監督プロセスPID | `/var/run/liner-notes.pid` | systemdが管理 |
| `liner_notes_child_pidfile` | 本体プロセスPID | `/var/run/liner-notes.child.pid` | systemdが管理 |
| `liner_notes_logfile` | 監督ログ | `${liner_notes_log_dir}/daemon.log` | unitのStandardOutput / StandardError |
| `liner_notes_doors` | Door定義 | `未指定` | --doors |
| `liner_notes_door_isolation_socket`（v0.76公開後のrc.d） | 全Doorの隔離ソケット | `未指定` | --door-isolation-socket |
| `liner_notes_link_accounts` | リンク専用会員定義 | `未指定` | --link-accounts |
| `liner_notes_admins` | 管理者一覧 | `未指定` | --admins |
| `liner_notes_catalog` | ボード定義 | `未指定` | MEMBERSの次の位置引数 |
| `liner_notes_vault_dir` | 保管庫 | `未指定` | --vault-dir |
| `liner_notes_zmodem_helper` | ZMODEMヘルパー | `未指定` | --zmodem-helper |
| `liner_notes_registration` | 登録方針 | `open` | --registration |
| `liner_notes_id_mode` | 会員IDの割当方式 | `free` | --id-mode |
| `liner_notes_id_prefix` | 自動ID接頭辞 | `u` | --id-prefix |
| `liner_notes_id_digits` | 自動ID桁数 | `6` | --id-digits |
| `liner_notes_id_first` | 最初のID | `未指定` | --id-first |
| `liner_notes_rtbbs_member_policy` | 互換会員ポリシー | `NO` | --rtbbs-member-policy |
| `liner_notes_login_text_dir` | ログイン案内ディレクトリ | `%%DATADIR%%` | --login-text-dir |
| `liner_notes_registration_form` | 登録フォーム | `simple` | --registration-form |
| `liner_notes_max_articles` | 記事数上限 | `0` | --max-articles |
| `liner_notes_max_mail` | メール件数上限 | `0` | --max-mail |
| `liner_notes_max_user_posts` | 会員別投稿数上限 | `0` | --max-user-posts |
| `liner_notes_banner` | 接続案内 | `%%DATADIR%%/banner.txt` | --banner |
| `liner_notes_chat` | チャット有効 | `YES` | --disable chat（無効時） |
| `liner_notes_mail` | メール有効 | `YES` | --disable mail（無効時） |
| `liner_notes_vault` | 保管庫有効 | `YES` | --disable vault（無効時） |
| `liner_notes_help_dir` | HELPディレクトリ | `%%DATADIR%%/help` | --help-dir |
| `liner_notes_userlog_keep` | 履歴保持件数 | `1000` | --userlog-keep |
| `liner_notes_userlog_public` | 履歴の会員公開 | `NO` | --userlog-public（有効時） |
| `liner_notes_time_warning` | 時間制限の予告秒数 | `60` | --time-warning |
| `liner_notes_time_grace` | 時間制限の猶予秒数 | `30` | --time-grace |
| `liner_notes_level0_seconds` | レベル0接続時間 | `0` | --level-seconds 0:秒 |
| `liner_notes_level1_seconds` | レベル1接続時間 | `0` | --level-seconds 1:秒 |
| `liner_notes_level2_seconds` | レベル2接続時間 | `0` | --level-seconds 2:秒 |
| `liner_notes_level3_seconds` | レベル3接続時間 | `0` | --level-seconds 3:秒 |
| `liner_notes_level4_seconds` | レベル4接続時間 | `0` | --level-seconds 4:秒 |
| `liner_notes_level5_seconds` | レベル5接続時間 | `0` | --level-seconds 5:秒 |
| `liner_notes_idle_seconds` | 無入力時の制限秒数 | `600` | --idle-seconds |
| `liner_notes_gather_count` | Gather保持数 | `16` | --gather-count |
| `liner_notes_chat_log` | チャットログ | `${liner_notes_log_dir}/chat.log` | --chat-log |
| `liner_notes_user_log` | 会員利用ログ | `${liner_notes_log_dir}/user.log` | --user-log |

## 共通の反映と注意

FreeBSDの配布設定は `/usr/local/etc/liner-notes/liner_notes.conf`、上書きは `/etc/rc.conf` です。設定は管理者のみが編集し、変更後は `service liner_notes restart`、`service liner_notes status` で確認します。ログは設定した出力先も確認してください。rc.confはシェル設定です。

Linuxでは `/etc/liner-notes/liner-notes.env` の既存の `LINER_NOTES_OPTIONS` を保持して編集し、`sudo systemctl restart liner-notes`、`sudo systemctl status liner-notes` で確認します。EnvironmentFileはシェルスクリプトではありません。シェルの変数展開やコマンド置換を利用しません。unit自体を変更した場合は `sudo systemctl daemon-reload` を実行します。unitの `User`、`Group`、`WorkingDirectory`、`ReadWritePaths` と保管先・ログ先の権限を揃えます。

## 値・前提・依存関係

- UIポリシーは `FIXED` / `MEMBERS` / `ALL`。UI識別子は登録済みのものを指定し、既定UIを許可一覧に含めます。登録上限16種類は実装済み種類数ではありません。詳細は[個人設定](personal-settings.md)と[UI横断表](ui-comparison.md)。
- SSHはビルド時の対応とホスト鍵が必要です。ポート0は無効、有効時のポートは1〜65535。認証試行は1〜20回、認証時間は1〜300秒。公開鍵一覧は任意。ホスト鍵と会員鍵の権限を保護し、[接続](connections.md)を確認してください。
- `database_mode` は `blocks` / `structured`。既存DBを設定変更だけで自動変換しません。[保存方式](storage.md)と[更新](upgrade.md)のバックアップ・移行手順を先に確認してください。
- `registration` は `open` / `approval` / `closed`、`id_mode` は `free` / `auto`、登録フォームは `simple` / `detailed` です。[会員登録](accounts.md)を参照。
- `chat` / `mail` / `vault` と履歴公開等は `YES` / `NO`。記事・メール・会員別投稿の上限0はquotaなし。保持件数0も無制限です。
- 接続時間・無入力・予告・猶予の単位は秒、レベルは0〜5。FreeBSDのrc.dによる秒数検査は最大86400、Gatherは0〜1000です。各上限の詳細は[システム上限](limits.md)。
- `admins`、`catalog`、`vault_dir`、`doors`、`link_accounts`、`limits`、HELPやヘルパーの指定は絶対パスと権限を確認します。ZMODEMヘルパーは実行可能である必要があります。ログは絶対パスまたは `-` で無効にできます。
- WebUIは別のサービス・設定を使用します。[Webサーバー](web-server.md)。リンクサービスは対象OSの配布・有効化状況を[リンク運用](link-operations.md)で確認します。

## UI選択を許可する例

既存局の設定に以下を追加・変更します。すでに登録されていないUIは指定せず、既存のデータや接続設定はそのまま保持します。

```sh
liner_notes_ui_policy="MEMBERS"
liner_notes_ui_default="rtbbs"
liner_notes_ui_allow="rtbbs,mmm,mash,ktbbs,big-model,wani-bbs,net-cock,vs"
service liner_notes restart
service liner_notes status
```

MMMを既定にする場合は `liner_notes_ui_default="mmm"` に変更します。固定する場合は `liner_notes_ui_policy="FIXED"` とし、既定と許可一覧を対応させます。

Linuxでは既存の環境ファイルにある完全なオプション文字列の**PORTの前**へ、次の部分を追加します。これは既存の行全体を置き換える例ではありません。

```text
--ui-policy MEMBERS --ui-default rtbbs --ui-allow rtbbs,mmm,mash,ktbbs,big-model,wani-bbs,net-cock,vs
```

LinuxでMMMを既定とするには `--ui-default mmm`、固定は `--ui-policy FIXED` とします。`FIXED` は切替を許可しません。

## 構成を追加するとき

| 目的 | FreeBSDで追加する設定 | Linuxで追加する設定 | 準備・確認 |
|---|---|---|---|
| 日本語 / 英語 | `liner_notes_language="ja"` / `"en"` | `--lang ja` / `--lang en` | 再起動しメニューとHELPの言語を確認 |
| 会員登録の受付を制限 | `liner_notes_registration="approval"` または `"closed"` | `--registration approval` / `closed` | 既存管理者で申請・ログインを確認。ゲストの権限はボードACLに従う |
| SQLite | `liner_notes_database` と必要に応じ `liner_notes_database_mode` | `--database` または `--database-structured` | 新規領域で初期化。既存局は[移行・バックアップ](storage.md)を先に確認 |
| 内蔵SSH | SSHポート・bind・ホスト鍵 | `--ssh-port` / `--ssh-bind` / `--ssh-host-key` | SSH対応ビルド、専用鍵と権限、[接続](connections.md) |
| 保管庫・ZMODEM | `liner_notes_vault_dir` / `liner_notes_zmodem_helper` | `--vault-dir` / `--zmodem-helper` | 保管庫の初期化・書込権限・実転送検証。 [転送](transfers.md) |
| Door | `liner_notes_doors` | `--doors` | 定義・実行ファイル・作業領域・権限。[外部ゲーム](external-games.md) |
| BBS間リンク | `liner_notes_link_accounts` | `--link-accounts` | 専用会員、接続先・署名鍵・TLS・個別サービス設定。[リンク](links.md) / [管理](link-operations.md) |
| WebUI | 別サービス・Web設定 | 別service・Web設定 | BBSへの接続とApache・TLSの準備。[Web](web-server.md) |

この表と例はv0.76のソース・配布設定と照合した文書です。環境ごとの設定変更・接続・サービス再起動が成功することを保証するものではありません。実行前にバックアップし、各OSの導入ページに従って検証してください。

## v0.76のBBS間リンク配布

Rocky Linux 10とDebian 13では、本体と同じ版の `liner-notes-link` パッケージに
`liner-notes-link.service` と `liner-notes-link` コマンドが入っています。設定は
`/etc/liner-notes/link.toml`、標準の状態領域は `/var/lib/liner-notes-link` です。
各局で[共有ボードと専用会員を準備](links.md#共有単位と権限)し、環境ファイルのPORTより前に
`--link-accounts /etc/liner-notes/link-accounts.conf` を追加します。鍵とパスワードファイルは
専用OSユーザーの読み取りに限定します。設定・告知・バックアップ後に本体を再起動します。

```sh
sudo -u liner_notes_link liner-notes-link check --config /etc/liner-notes/link.toml
sudo systemctl start liner-notes-link
sudo systemctl status liner-notes-link
sudo -u liner_notes_link liner-notes-link status --config /etc/liner-notes/link.toml
# 双方向の投稿・返信を確認したあと、起動時に有効化する場合だけ実行
sudo systemctl enable liner-notes-link
```

FreeBSDのv0.76はLINKオプションのCLIを配布します。リンク専用rc.dの同梱はこのタグ以降の変更です。
v0.76では `sysrc liner_notes_link_accounts=/usr/local/etc/liner-notes/link-accounts.conf`
で本体へ割り当てを設定し、[共有ボードの構築手順](links.md)に従って専用OSユーザーで
`check` と `serve` を実行します。停止・監視は設置者が構成します。署名鍵を交換先へ渡さず、
公開証明書を別経路で照合してから接続します。[状態確認・バックアップと再同期](link-operations.md)。

## 新規局の設定例

| 構成 | FreeBSDのrc.conf | Linuxのオプション | 確認 |
|---|---|---|---|
| 最小構成 | `liner_notes_bind="127.0.0.1"`、`liner_notes_port="2222"` | `--bind 127.0.0.1`、位置引数のPORTに `2222` | [初期設定](initial-setup.md)で最初の管理者を用意し、ローカルからログイン |
| RTBBS専用 | `liner_notes_ui_policy="FIXED"`、既定・許可を `rtbbs` に設定 | `--ui-policy FIXED --ui-default rtbbs --ui-allow rtbbs` | ログイン後にRTBBSが選ばれること |
| MMM専用 | `liner_notes_ui_policy="FIXED"`、既定・許可を `mmm` に設定 | `--ui-policy FIXED --ui-default mmm --ui-allow mmm` | ログイン後にMMMが選ばれること |
| ゲストを許可 | `liner_notes_ui_policy="ALL"` で許可UIを列挙する場合でも、閲覧権限はボード設定で指定 | `--ui-policy ALL`。接続方式と権限の設定も確認 | 未ログインで許可ボードだけ閲覧できること。会員登録ポリシーは別に設定 |

既存局ではポート・保存先・ACLを上記の値へ一括置換しません。
