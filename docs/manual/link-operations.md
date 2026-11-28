# 共有ボードの管理・保守

protocol 2の運用手順です。相手の鍵とボードを事前登録する原則、会員情報・私信を
共有しない原則は変わりません。通信とチェックポイントを使う全ノードをprotocol 2対応版に
そろえてください。旧protocolとの混在は、この版の運用対象ではありません。

## 起動・停止・自動起動

設定検査後に、BBS本体を先に起動します。初回は自動起動を無効のまま試験します。

| 操作 | Rocky / Debian | FreeBSD（rc.d同梱版） |
|---|---|---|
| 初回の起動 | `systemctl start liner-notes-link` | `service liner_notes_link onestart` |
| 状態 | `systemctl status liner-notes-link` | `service liner_notes_link onestatus` |
| 停止 | `systemctl stop liner-notes-link` | `service liner_notes_link onestop` |
| 再起動 | `systemctl restart liner-notes-link` | `service liner_notes_link onerestart` |
| 起動時に有効 | `systemctl enable liner-notes-link` | `sysrc liner_notes_link_enable=YES` |
| 起動時に無効 | `systemctl disable liner-notes-link` | `sysrc liner_notes_link_enable=NO` |
| ログ | `journalctl -u liner-notes-link -b` | syslogの `liner_notes_link`（出力先はsyslogの設定に従う） |

Linuxのunitはネットワークが利用可能になるのを待ち、BBSと同時起動するときはBBSのあとに開始します。リンクサービスからBBSを起動したり、BBSの停止をリンクへ伝播させたりする依存関係はありません。FreeBSDのrc順序もBBSのあとです。BBS停止中もリンクは署名済みイベントをDBに保留し、再接続後に反映します。リンクだけを停止している間は対向への交換が止まり、同じ鍵とDBで再開したあとに同期します。

FreeBSDのPIDファイル `/var/run/liner_notes_link.pid` は監督プロセス用です。サービス状態と同期状態を別々に確認してください。専用OSユーザーで `liner-notes-link status --config /usr/local/etc/liner-notes/link.toml`（Linuxは `/etc/liner-notes/link.toml`）を実行します。`node` は署名鍵の指紋、`peers` は相手ごとの最終成功、エラー、停止状態と受信位置です。`events`、`pending`、`deferred`、`retry_reasons` で適用待ちと再試行理由を確認します。短時間の通信断では増えた保留が復旧後に減ることを確認します。

## 手動サービス定義からの移行

更新前に自動起動の状態とノードIDを記録し、下記の保全手順を実施します。パッケージを導入したあとも、現在使用中のサービス定義を確認してください。既存のOSユーザーとそのUID/GIDを変更する必要はありません。

- **Linux**　`systemctl show -p FragmentPath -p DropInPaths liner-notes-link` と `systemctl cat liner-notes-link` で実際の定義を確認します。`/etc/systemd/system/liner-notes-link.service` があると `/usr/lib/systemd/system/liner-notes-link.service` より優先されます。手動定義を管理者専用領域へバックアップして差分を確認します。従来の専用アカウント（例 `liner_link`）を維持する場合は、`systemctl edit liner-notes-link` で `[Service]` の `User`・`Group` を既存値に設定します。カスタム設定パスは `ExecStart=` で解除後、新しい完全な起動行を指定します。切替時に手動unitを別の保存場所へ退避し、`systemctl daemon-reload`、必要に応じて `systemctl reenable liner-notes-link`（移行前に有効だった場合のみ）、管理者が計画した再起動の順に実施します。管理ソケットのディレクトリに使うグループとWebアカウントの所属も確認します。
- **FreeBSD**　`/usr/local/etc/rc.d/liner_notes_link` と `/usr/local/share/liner-notes/liner_notes_link.sample` を比較します。パッケージは配置済みの手動rc.dを残します。明示移行時は旧定義で停止し、管理者専用領域へ退避してから `install -m 0555 /usr/local/share/liner-notes/liner_notes_link.sample /usr/local/etc/rc.d/liner_notes_link` で切り替えます。従来アカウントを維持する場合は `sysrc liner_notes_link_run_user=liner_link` と `sysrc liner_notes_link_control_group=既存グループ` を設定します。設定パスを変えている局は `liner_notes_link_config` を設定します。PID・ソケットの旧プロセス残留がないことを調べ、保存済みの有効・無効状態を維持して起動します。

新定義で起動できない場合は停止し、手動定義・関連drop-inまたはrc.conf設定を戻してから再起動します。どちらのOSでも、削除前には明示的に停止して残留プロセスがないことを確認します。FreeBSDの手動rc.dはパッケージ削除後も残ります。

## バックアップ・更新・離脱

1. 保守時間を決め、リンク、BBSの順に停止します。BBSデータ・ボード構成・`link-accounts.conf` を本体の[バックアップ手順](backup-recovery.md)で保全します。続いて `link.toml`、公開証明書、`identity/`、`bbs-password`、リンクDB、実際のunit・rc.confを含めます。鍵・認証ファイルを含む保管先は0700、バックアップファイルは0600を使用します。
2. リンクDBは停止確認後に保全します。SQLite Backup APIまたは `sqlite3 /状態領域/link.sqlite '.backup /管理者専用の保存先/link.sqlite'` を使い、バックアップで `PRAGMA integrity_check` が `ok` になることを確認します。SQLite CLIは別途必要な場合があります。稼働中のDB単体をコピーするとWALの更新を取りこぼすため、単純コピーは避けます。
3. 手動更新では対象パッケージを導入し、設定・鍵・DB・所有権・有効状態を点検します。管理者がBBSを起動し、更新前にリンクが稼働していた局だけリンクを起動します。承認済みCD更新では、既存の状態復元と管理ソケットの読み取り専用 `status` とノードID照合を使用します。
4. 復元試験は全対向を遮断した隔離環境で行い、同じ時点のBBSデータとリンクDBを組にして戻します。鍵とDBの所有者・モードを確認してから、本体、リンクの順で起動します。同じ署名鍵の複製を実ネットワークへ接続しないでください。
5. 一時停止や自動起動の無効化ではDB・鍵・履歴を残します。追加局は証明書と公開範囲を全関連局で確認し、離脱局は管理者が合意して関係局の `peers` と `origins` を見直します。共有IDの変更は新しい共有関係として扱います。署名鍵の再生成は別ノードへの変更です。通常の復旧や離脱でDB削除、鍵の再生成、カーソルのリセットを行いません。

## 障害の切り分け

| 症状 | 確認事項 |
|---|---|
| 起動失敗・更新後停止 | 実際の定義、設定ファイル、Python依存、アカウント、ログ |
| FreeBSDで停止表示やSSHから戻らない | `-P` の監督PID、`procname=/usr/sbin/daemon`、`-f` を含む配布rc.dかを照合 |
| 対向へ未接続 | 実際の待受IP、DNS、TCPポート、NAT、ファイアウォール |
| TLS拒否 | 対向証明書と公開鍵指紋、双方向の登録、証明書期限・時刻 |
| BBSへ未接続 | ローカルTelnet待受、専用会員・認証ファイル、停止状態 |
| 同期停滞 | 共有ID、`link-accounts.conf`、ボード権限、保留理由、前提イベント |
| Permission denied | UID/GID、ディレクトリ通過、鍵・秘密ファイル、RockyのSELinuxラベルと拒否ログ |

Rockyで専用SELinuxポリシーを使う場合は関連ディレクトリのラベルを `ls -Z` で確認し、固定したポートをローカルポリシーに登録します。[Linuxへの導入](install-linux.md)のポリシー手順を参照してください。SELinuxをEnforcingのまま原因を調査します。FreeBSDで強制終了後にソケットが残った場合は、リンクを停止し、そのソケットを所有するプロセスがいないことを確認してから残留ソケットだけを除去してください。

## 状態表示・停止・再試行

リンク設定の`[node]`で`control_socket`を指定します。親ディレクトリはサービス所有、
管理グループだけが通過できる0750、ソケットは0660です。Webアカウントにはそのグループだけを
追加し、DBや秘密鍵の読み取り権限は与えません。Linuxの付属unitは`liner_notes_linkctl`を使います。
FreeBSDの配布rc.dも同じグループを既定とし、ソケット用に `/var/run/liner-notes-link` を準備します。Web側の設定パスはOSに合わせます。

Webの`[web]`に次を指定し、Webサービスを再起動します。

```toml
link_control_socket = "/run/liner-notes-link/control.sock"
```

管理者メニューの「BBS間リンクの管理」から、相手ごとの最終成功、エラー、巡回位置、
反映待ち件数を確認し、一時停止・再開・再試行できます。一時停止は再起動後も維持します。
再試行は受信位置を巻き戻しません。鍵の信頼登録とボード割当は外部設定ファイルで管理します。
古いソケットが異常終了後に残った場合は、そのソケットを使うdaemonがいないことを確認して
ソケットだけ除去します。通常終了ではdaemonが除去します。

## 大規模ボード

```toml
[node]
scan_budget = 512
pull_batches = 64
```

`scan_budget`は1ボード・1巡回の最大記事数（1～8192）、`pull_batches`は相手ごとの
最大受信回数（1～1024）。既定値は従来と同じ128／16です。メタデータは最大32件ずつ取得し、
変更判定では索引上の題名・版番号を使います。変更のない本文を読み直しません。
処理位置はDBへ保存するため、再起動で初回からやり直しません。
変更検出は引き続き巡回方式です。予算を大きくすると追従が早くなりますが、他ボード・接続先へ
戻るまでの時間も延びるため、稼働状況を見て調整します。

## 履歴の整理

リンクdaemonを停止してから、専用サービスユーザーで実行します。
稼働中のDBロックがあれば管理操作は拒否されます。

```sh
liner-notes-link checkpoint --config /etc/liner-notes/link.toml
liner-notes-link compact --config /etc/liner-notes/link.toml --backup /backup/link-before-compact.sqlite
liner-notes-link snapshot --config /etc/liner-notes/link.toml \
  --peer 受信先の64桁の鍵ID /backup/shared-boards.snapshot
```

`checkpoint`は自ノードが発信した記事について、現在の本文・題名・返信先、または本文を持たない
取り消し状態を新しい版として署名します。他ノードの署名を作り直すことはありません。
各発信元で生成したチェックポイントを同期すれば、中継側でも古い履歴を整理できます。

`compact`は必ず事前バックアップを作り、BBSへの反映済みチェックポイントより古い履歴を削除します。
SQLiteの空き領域とWALも整理します。長期切断中の相手は次回接続時に署名付き最新状態を受け取り、
途中の版を飛ばして追いつけます。再送された古い版は本文を再登録せず無視します。
履歴の通番は再利用しません。

この回収対象はリンクDBです。BBS本体に以前保存された取り消し本文は、既存のBBS管理ツールで
別途回収します。バックアップ・過去のスナップショットには以前の本文が残り得るため、保存期限は
設置者が管理してください。

## 新規BBSの参加

信頼する発信元・共有ボードを設定した新しいノードで、署名付きスナップショットを読み込みます。
先に新規ノードの公開鍵を提供元へ登録します。提供元は`--peer`で指定した相手への共有を
許可したボードだけを書き出します。署名付きの末尾の件数・ハッシュも検査するため、
ファイルが行の境界で途中切れになった場合も拒否します。

```sh
liner-notes-link bootstrap --config /etc/liner-notes/link.toml \
  --output /var/lib/liner-notes-link/new.sqlite /受け取った/shared-boards.snapshot
```

既存出力は上書きしません。全レコードの署名・発信元・共有範囲・容量を確認し、成功した場合だけ
新しいDBを公開します。不正・途中切れのファイルで既存DBが部分更新されることはありません。
受信先は新規ノードの鍵を使用します。スナップショットには秘密鍵、会員データ、BBS内の記事番号、
相手の受信カーソルは含めません。停止中に`database`を新しいDBへ変更して起動すると、
ローカルBBSへ記事を反映します。取り消し済み親記事は旧本文を持たない印として作り、返信関係を保ちます。

## 通信鍵の更新

`rotate_tls = true`で、通信専用のEd25519鍵と90日有効のTLS証明書を使用します。
有効期限30日前を目安に起動時および1時間ごとの点検で更新します。
元の識別鍵が通信証明書へ署名するため、相手の登録IDや共有ボード、記事の発信元IDは変わりません。
通信証明書の鍵だけを信頼して別ノードとして登録し直す必要はありません。

長期の発信元署名鍵そのものは自動交換しません。漏洩時は相手側で信頼登録を止め、
別の管理手順で再登録する必要があります。期限切れが迫った識別用証明書も管理者の更新対象です。
TLS鍵の旧世代は復旧用に残します。鍵ファイルをWebから取得・編集する機能は設けません。

## 取り消し後の再同期

取り消しを受信したあと、古い投稿・題名変更を再受信しても公開状態へ戻しません。重複は同じ発信元・記事識別子・版で処理し、親や前の版が未着なら保存して後の巡回で適用します。通信断・再起動時も未適用イベントとカーソルを保持します。新規参加のチェックポイント・スナップショットでは取り消された親を本文なしで再現し、その配下も不可視にします。

受信先のHIDEは同期エラーではありません。非表示の親への返信は保存されます。復旧時にHIDEを解除する必要はなく、権限・容量・署名・欠けた前の版を調べてください。RESTOREで投稿元の取り消しを解除してはいけません。全局の表示規則をそろえる更新条件は[共有ボード](links.md)を参照してください。

## Solaris移植中の保守ロック

Solarisではリンクdaemonと排他保守に同じPOSIX記録ロックを使用し、専用の保持プロセスで寿命を管理します。daemonの起動設定検査に失敗した場合もleaseを閉じます。FreeBSD/Linuxのflock、署名・DB・チェックポイント形式は変更しません。実機のリンク統合試験は[Solarisの検証状況](install-solaris.md)に記載します。

## 本体への反映が保存エラーになった場合

`ERR link storage; restart inspection required` は、本体への反映完了を確認できない状態です。リンク側の未適用イベントや本体の発信元対応表を削除せず、本体とリンクのログ・保存領域・空き容量を確認してください。記事が一部保存済みの場合もあるため、別の発信元識別子や記事番号で作り直してはいけません。原因を解消し、バックアップを確保したうえで本体を再起動し、処理記録の回復成功を確認してからリンクを再試行します。起動時の回復にも失敗する場合は、その状態を保存して管理者が調査してください。

## English operations guide

| Action | Rocky / Debian | FreeBSD (with packaged rc.d) |
|---|---|---|
| Start or stop before enabling at boot | `systemctl start liner-notes-link` or `systemctl stop liner-notes-link` | `service liner_notes_link onestart` or `service liner_notes_link onestop` |
| Inspect or restart | `systemctl status liner-notes-link` or `systemctl restart liner-notes-link` | `service liner_notes_link onestatus` or `service liner_notes_link onerestart` |
| Enable or disable at boot | `systemctl enable liner-notes-link` or `systemctl disable liner-notes-link` | `sysrc liner_notes_link_enable=YES` or `sysrc liner_notes_link_enable=NO` |
| Logs | `journalctl -u liner-notes-link -b` | The `liner_notes_link` syslog tag |

The service is opt-in. The service manager orders it after the BBS on joint startup. The link stays up when only the BBS is stopped. It stores signed incoming events and retries the BBS connection. Restarting the link on its own uses its existing identity and SQLite state. FreeBSD tracks the supervisor PID. Inspect both the supervisor and the link's operation with `liner-notes-link status --config CONF/link.toml` as the link OS user. Check the node fingerprint, peer last success/error and cursors, and the pending/deferred counters. A backlog during an outage should drain once service is restored.

### Migration, backup and troubleshooting

- On Linux, inspect `systemctl show -p FragmentPath -p DropInPaths liner-notes-link` and `systemctl cat liner-notes-link`. An administrator unit under `/etc/systemd/system/` takes precedence. Back it up and compare it with the packaged definition under `/usr/lib/systemd/system/`. If retaining a pre-existing service account, set its `User` and `Group` with `systemctl edit liner-notes-link`. When changing `ExecStart`, clear it first with an empty assignment. During a planned migration, move the old unit into admin-only storage, reload the service manager, reenable only if previously enabled, then restart and verify the node and peers.
- On FreeBSD, compare the live rc.d file with `/usr/local/share/liner-notes/liner_notes_link.sample`. The package preserves a manually installed rc.d. To migrate, stop using the old definition, back it up, and install the sample at `/usr/local/etc/rc.d/liner_notes_link` with mode 0555. Preserve the existing account with `liner_notes_link_run_user`, set `liner_notes_link_control_group` to the group authorized for socket access and use `liner_notes_link_config` for a custom config path. Keep the previous boot setting. To roll back, stop the link and restore the saved service definition and associated settings before restarting.
- Stop the link and BBS in that order for a coordinated backup. Save the [BBS data](backup-recovery.md), board settings, `link-accounts.conf`, the service definitions, `link.toml`, peer certificates, the identity directory, BBS password file and link DB. Use the SQLite Backup API or SQLite's `.backup` command and verify `PRAGMA integrity_check` on the copy. Protect the backup directory with 0700 and private files with 0600. Restore the BBS and link DB from the same backup point. Test restoration with peers blocked. A restored clone with the same node key must not connect to the live network.
- For start failures, check account, dependencies, config and filesystem permissions. For FreeBSD status or an SSH command that does not return, check the supervisor PID, its process name and the `daemon -f` option. A hard kill may leave a stale control socket. Stop the connector, verify the socket has no owner and remove only the stale socket before restarting. On Rocky with the dedicated SELinux policy, verify file labels, the link port type and the audit messages while keeping Enforcing active.
- For peer failures, check TCP, DNS, firewall/NAT, mutual certificate registration and time. For local login failures, check the BBS listener, access-file binding and password file. If pending or deferred work persists, inspect the shared ID, ACL and missing prerequisite events. Preserve the DB and keys while diagnosing. Cursor reset is an exceptional recovery operation.
- Disable automatic startup or stop the service for a temporary pause. Preserve the identity and DB when resuming. To add or remove a peer, coordinate `peers` and `origins` at relevant sites and verify the resulting publication scope. Changing the signing key introduces a new node identity. For permanent removal, stop the service explicitly before removing its package and retain the configuration, identity and history for recovery.
